#include "ncbind.hpp"
#include "tp_stub.h"
#include "UtilStreams.h"

#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#define NCB_MODULE_NAME TJS_W("memfile.dll")

namespace {

class MemNode {
public:
    explicit MemNode(ttstr nodeName, bool directory = false) :
        name(std::move(nodeName)), isDir(directory) {}

    ttstr name;
    bool isDir = false;
    std::vector<tjs_uint8> data;
    std::map<ttstr, std::unique_ptr<MemNode>> children;
};

class MemWriteStream : public tTVPMemoryStream {
public:
    MemWriteStream(MemNode *node, bool truncate) : target(node) {
        if(target && !truncate && !target->data.empty())
            Write(target->data.data(), static_cast<tjs_uint>(target->data.size()));
        Seek(0, TJS_BS_SEEK_SET);
    }

    ~MemWriteStream() override {
        if(!target)
            return;

        const auto size = static_cast<tjs_uint>(GetSize());
        target->data.resize(size);
        if(size > 0)
            std::memcpy(target->data.data(), GetInternalBuffer(), size);
    }

private:
    MemNode *target;
};

static std::vector<ttstr> splitPath(ttstr path) {
    std::vector<ttstr> parts;
    while(!path.IsEmpty() && path[0] == TJS_W('/'))
        path = ttstr(path.c_str() + 1);

    const tjs_char *begin = path.c_str();
    const tjs_char *part = begin;
    for(const tjs_char *p = begin; *p; ++p) {
        if(*p == TJS_W('/')) {
            if(p > part)
                parts.emplace_back(part, p - part);
            part = p + 1;
        }
    }
    if(*part)
        parts.emplace_back(part);
    return parts;
}

static bool splitStorageName(const ttstr &name, ttstr &path) {
    const tjs_char *raw = name.c_str();
    const tjs_char *slash = TJS_strchr(raw, TJS_W('/'));
    if(!slash)
        return false;

    ttstr domain(raw, slash - raw);
    if(domain != TJS_W("."))
        TVPThrowExceptionMessage(TJS_W("no such domain:%1"), domain);

    path = ttstr(slash + 1);
    return !path.IsEmpty();
}

class MemStorage : public iTVPStorageMedia {
public:
    MemStorage() : root(TJS_W("root"), true) {}

    void AddRef() override { ++refCount; }

    void Release() override {
        if(refCount == 1)
            delete this;
        else
            --refCount;
    }

    void GetName(ttstr &name) override { name = TJS_W("mem"); }
    void NormalizeDomainName(ttstr &) override {}
    void NormalizePathName(ttstr &) override {}

    bool CheckExistentStorage(const ttstr &name) override {
        ttstr path;
        if(!splitStorageName(name, path))
            return false;
        auto *node = find(path);
        return node && !node->isDir;
    }

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        ttstr path;
        if(!splitStorageName(name, path))
            TVPThrowExceptionMessage(TJS_W("invalid path:%1"), name);

        auto *node = ensureFile(path, flags == TJS_BS_WRITE || flags == TJS_BS_APPEND);
        if(!node || node->isDir)
            TVPThrowExceptionMessage(TJS_W("cannot open memfile:%1"), name);

        if(flags == TJS_BS_READ)
            return new tTVPMemoryStream(node->data.data(),
                                        static_cast<tjs_uint>(node->data.size()));

        auto *stream = new MemWriteStream(node, flags == TJS_BS_WRITE);
        if(flags == TJS_BS_APPEND)
            stream->Seek(0, TJS_BS_SEEK_END);
        return stream;
    }

    void GetListAt(const ttstr &name, iTVPStorageLister *lister) override {
        ttstr path;
        if(!splitStorageName(name, path))
            return;

        auto *node = find(path);
        if(!node || !node->isDir)
            return;

        for(const auto &item : node->children) {
            if(!item.second->isDir)
                lister->Add(item.first);
        }
    }

    void GetLocallyAccessibleName(ttstr &name) override { name = TJS_W(""); }

    bool mkdir(const ttstr &path) { return ensureDirectory(path) != nullptr; }
    bool isExistFile(const ttstr &path) {
        auto *node = find(path);
        return node && !node->isDir;
    }
    bool isExistDirectory(const ttstr &path) {
        auto *node = find(path);
        return node && node->isDir;
    }
    bool remove(const ttstr &path) { return removeNode(path, false); }
    bool rmdir(const ttstr &path) { return removeNode(path, true); }

    tTJSVariant getInfo(const ttstr &path) {
        auto *node = find(path);
        return node ? makeInfo(*node) : tTJSVariant();
    }

    tTJSVariant getData(const ttstr &path) {
        auto *node = find(path);
        if(!node || node->isDir || node->data.empty())
            return tTJSVariant();
        return tTJSVariant(node->data.data(),
                           static_cast<tjs_uint>(node->data.size()));
    }

    tTJSVariant getDirectory(const ttstr &path) {
        auto *node = find(path);
        if(!node || !node->isDir)
            return tTJSVariant();

        iTJSDispatch2 *array = TJSCreateArrayObject();
        if(!array)
            return tTJSVariant();

        for(const auto &item : node->children) {
            tTJSVariant info = makeInfo(*item.second);
            tTJSVariant *param = &info;
            array->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, &param, array);
        }

        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

private:
    MemNode *find(const ttstr &path) {
        auto parts = splitPath(path);
        MemNode *node = &root;
        for(const auto &part : parts) {
            auto it = node->children.find(part);
            if(it == node->children.end())
                return nullptr;
            node = it->second.get();
        }
        return node;
    }

    MemNode *ensureDirectory(const ttstr &path) {
        auto parts = splitPath(path);
        MemNode *node = &root;
        for(const auto &part : parts) {
            auto &child = node->children[part];
            if(!child)
                child = std::make_unique<MemNode>(part, true);
            if(!child->isDir)
                return nullptr;
            node = child.get();
        }
        return node;
    }

    MemNode *ensureFile(const ttstr &path, bool create) {
        auto parts = splitPath(path);
        if(parts.empty())
            return nullptr;

        MemNode *dir = &root;
        for(size_t i = 0; i + 1 < parts.size(); ++i) {
            auto &child = dir->children[parts[i]];
            if(!child) {
                if(!create)
                    return nullptr;
                child = std::make_unique<MemNode>(parts[i], true);
            }
            if(!child->isDir)
                return nullptr;
            dir = child.get();
        }

        auto &file = dir->children[parts.back()];
        if(!file) {
            if(!create)
                return nullptr;
            file = std::make_unique<MemNode>(parts.back(), false);
        }
        return file.get();
    }

    bool removeNode(const ttstr &path, bool directory) {
        auto parts = splitPath(path);
        if(parts.empty())
            return false;

        MemNode *dir = &root;
        for(size_t i = 0; i + 1 < parts.size(); ++i) {
            auto it = dir->children.find(parts[i]);
            if(it == dir->children.end() || !it->second->isDir)
                return false;
            dir = it->second.get();
        }

        auto it = dir->children.find(parts.back());
        if(it == dir->children.end() || it->second->isDir != directory)
            return false;
        if(directory && !it->second->children.empty())
            return false;
        dir->children.erase(it);
        return true;
    }

    static tTJSVariant makeInfo(const MemNode &node) {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return tTJSVariant();

        tTJSVariant name(node.name);
        tTJSVariant size(static_cast<tjs_int64>(node.data.size()));
        tTJSVariant isDirectory(node.isDir);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &name, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"), nullptr, &size, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("isDirectory"), nullptr,
                      &isDirectory, dict);

        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }

    tjs_uint refCount = 1;
    MemNode root;
};

MemStorage *g_memStorage = nullptr;

class StoragesMemFile {
public:
    static bool isExistMemoryFile(ttstr filename) {
        return g_memStorage && g_memStorage->isExistFile(filename);
    }

    static bool isExistMemoryDirectory(ttstr dirname) {
        return g_memStorage && g_memStorage->isExistDirectory(dirname);
    }

    static bool deleteMemoryFile(ttstr filename) {
        return g_memStorage && g_memStorage->remove(filename);
    }

    static bool deleteMemoryDirectory(ttstr dirname) {
        return g_memStorage && g_memStorage->rmdir(dirname);
    }

    static tTJSVariant getMemoryFileInfo(ttstr filename) {
        return g_memStorage ? g_memStorage->getInfo(filename) : tTJSVariant();
    }

    static tTJSVariant getMemoryFileData(ttstr filename) {
        return g_memStorage ? g_memStorage->getData(filename) : tTJSVariant();
    }

    static tTJSVariant getMemoryDirectory(ttstr dirname) {
        return g_memStorage ? g_memStorage->getDirectory(dirname) : tTJSVariant();
    }
};

static void PreRegistCallback() {
    if(!g_memStorage) {
        g_memStorage = new MemStorage();
        TVPRegisterStorageMedia(g_memStorage);
    }
}

static void PostUnregistCallback() {
    if(g_memStorage) {
        TVPUnregisterStorageMedia(g_memStorage);
        g_memStorage->Release();
        g_memStorage = nullptr;
    }
}

} // namespace

NCB_ATTACH_CLASS(StoragesMemFile, Storages) {
    NCB_METHOD(isExistMemoryFile);
    NCB_METHOD(isExistMemoryDirectory);
    NCB_METHOD(deleteMemoryFile);
    NCB_METHOD(deleteMemoryDirectory);
    NCB_METHOD(getMemoryFileInfo);
    NCB_METHOD(getMemoryFileData);
    NCB_METHOD(getMemoryDirectory);
}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
