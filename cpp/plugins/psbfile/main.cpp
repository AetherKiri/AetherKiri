//
// Created by lidong on 2025/1/31.
// TODO: implement psbfile.dll plugin
// ref: https://github.com/number201724/psbfile
// ref: https://github.com/UlyssesWu/FreeMote
//
#include <spdlog/spdlog.h>
#include <cassert>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "tjs.h"
#include "ncbind.hpp"
#include "KAGParser.h"
#include "PSBFile.h"
#include "PSBHeader.h"
#include "PSBMediaRegistry.h"
#include "PSBMedia.h"
#include "PSBValue.h"
#include "SystemControl.h"

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

#define LOGGER spdlog::get("plugin")

using namespace PSB;
static PSBMedia *psbMedia = nullptr;

static bool psbDebugEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_PSB_DEBUG");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static std::string describeVariant(const tTJSVariant *value) {
    if(!value)
        return "<null>";
    std::string text = "type=" + std::to_string(static_cast<int>(value->Type()));
    try {
        text += " value=" + ttstr(*value).AsStdString();
    } catch(...) {
        text += " value=<unprintable>";
    }
    return text;
}

namespace PSB {
bool GetPSBMediaCacheStats(PSBMediaCacheStats &outStats) {
    if(psbMedia == nullptr)
        return false;
    outStats = psbMedia->getCacheStats();
    return true;
}

void SetPSBMediaCacheBudget(size_t maxEntries, size_t maxBytes) {
    if(psbMedia == nullptr)
        return;
    psbMedia->setCacheBudget(maxEntries, maxBytes);
}

PSBMedia *GetGlobalPSBMedia() {
    return psbMedia;
}
} // namespace PSB

static bool psbCacheInfoCallback(size_t &usedBytes, size_t &limitBytes) {
    PSBMediaCacheStats stats;
    if(!GetPSBMediaCacheStats(stats))
        return false;
    usedBytes = stats.bytesInUse;
    limitBytes = stats.byteLimit;
    return true;
}

namespace {
using ScenarioLabelSet = std::unordered_set<std::string>;

std::string StripLeadingStar(std::string value) {
    if(!value.empty() && value.front() == '*')
        value.erase(value.begin());
    return value;
}

void AddScenarioLabel(ScenarioLabelSet &labels, std::string value) {
    value = StripLeadingStar(std::move(value));
    if(!value.empty())
        labels.insert(std::move(value));
}

std::shared_ptr<PSB::PSBDictionary>
AsDictionary(const std::shared_ptr<PSB::IPSBValue> &value) {
    return std::dynamic_pointer_cast<PSB::PSBDictionary>(value);
}

std::shared_ptr<PSB::PSBList>
AsList(const std::shared_ptr<PSB::IPSBValue> &value) {
    return std::dynamic_pointer_cast<PSB::PSBList>(value);
}

std::shared_ptr<PSB::PSBString>
AsString(const std::shared_ptr<PSB::IPSBValue> &value) {
    return std::dynamic_pointer_cast<PSB::PSBString>(value);
}

void CollectJumpLabels(ScenarioLabelSet &labels,
                       const std::shared_ptr<PSB::IPSBValue> &value) {
    if(auto dict = AsDictionary(value)) {
        for(const auto &[key, child] : *dict) {
            AddScenarioLabel(labels, key);
            if(auto text = AsString(child))
                AddScenarioLabel(labels, text->value);
        }
        return;
    }

    if(auto list = AsList(value)) {
        for(const auto &child : *list) {
            if(auto text = AsString(child))
                AddScenarioLabel(labels, text->value);
        }
    }
}

ScenarioLabelSet CollectScenarioLabels(const std::shared_ptr<const PSB::PSBDictionary> &root) {
    ScenarioLabelSet labels;
    if(!root)
        return labels;

    auto scenes = std::dynamic_pointer_cast<PSB::PSBList>((*root)["scenes"]);
    if(!scenes)
        return labels;

    for(const auto &sceneValue : *scenes) {
        auto scene = AsDictionary(sceneValue);
        if(!scene)
            continue;

        if(auto label = AsString((*scene)["label"]))
            AddScenarioLabel(labels, label->value);
        CollectJumpLabels(labels, (*scene)["jumplabels"]);
    }

    return labels;
}

const ScenarioLabelSet *GetCachedScenarioLabels(const ttstr &storage) {
    static std::unordered_map<std::string, ScenarioLabelSet> cache;

    ttstr path = storage;
    if(path.IsEmpty())
        return nullptr;
    if(TVPExtractStorageExt(path).AsLowerCase() != TJS_W(".scn"))
        path += TJS_W(".scn");

    const std::string key = path.AsStdString();
    auto found = cache.find(key);
    if(found != cache.end())
        return &found->second;

    PSB::PSBFile psb;
    if(!psb.loadPSBFile(path)) {
        if(psbDebugEnabled()) {
            LOGGER->info("PSB scenario label load failed: {}",
                         path.AsStdString());
        }
        return nullptr;
    }

    auto inserted = cache.emplace(key, CollectScenarioLabels(psb.getObjects()));
    if(psbDebugEnabled()) {
        LOGGER->info("PSB scenario labels: path={} count={}", key,
                     inserted.first->second.size());
    }
    return &inserted.first->second;
}

bool HasCompiledScenarioLabel(const ttstr &storage, const ttstr &label) {
    const auto *labels = GetCachedScenarioLabels(storage);
    if(!labels)
        return false;

    std::string wanted = StripLeadingStar(label.AsStdString());
    const bool found = labels->find(wanted) != labels->end();
    if(psbDebugEnabled()) {
        LOGGER->info("PSB scenario label query: storage={} label={} found={}",
                     storage.AsStdString(), wanted, found);
    }
    return found;
}
} // namespace

namespace PSB {
void initPSBMedia() {
    if(psbMedia != nullptr)
        return;
    psbMedia = new PSBMedia();
    TVPRegisterStorageMedia(psbMedia);
    psbMedia->Release();
    TVPRegisterPSBCacheInfoCallback(psbCacheInfoCallback);
    LOGGER->info("initPsbFile");
}

void deInitPSBMedia() {
    TVPRegisterPSBCacheInfoCallback(nullptr);
    if(psbMedia != nullptr) {
        psbMedia->clear();
        TVPUnregisterStorageMedia(psbMedia);
        psbMedia = nullptr;
    }
    LOGGER->info("deInitPsbFile");
}
} // namespace PSB

void initPsbFile() {
    initPSBMedia();
    TVPRegisterCompiledScenarioLabelResolver(HasCompiledScenarioLabel);
}

void deInitPsbFile() {
    TVPRegisterCompiledScenarioLabelResolver(nullptr);
    deInitPSBMedia();
}

// ---------------------------------------------------------------------------
// PSB Lazy Proxy: converts PSB tree nodes to TJS on demand, avoiding the
// massive up-front allocation that toTJSVal() causes on large .scn files.
// ---------------------------------------------------------------------------

static tTJSVariant convertPSBLazy(const std::shared_ptr<PSB::IPSBValue> &val);

class PSBLazyDict : public tTJSDispatch {
    std::shared_ptr<PSB::PSBDictionary> _dict;

public:
    explicit PSBLazyDict(std::shared_ptr<PSB::PSBDictionary> dict)
        : _dict(std::move(dict)) {}

    tjs_error PropGet(tjs_uint32 flag,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint, tTJSVariant *result,
                                      iTJSDispatch2 *objthis) override {
        if(!membername || !result) return TJS_E_NOTIMPL;

        if(TJS_strcmp(membername, TJS_W("count")) == 0) {
            *result = (tjs_int)_dict->size();
            return TJS_S_OK;
        }

        auto val = (*_dict)[ttstr(membername).AsStdString()];
        if(!val) {
            if(flag & TJS_MEMBERMUSTEXIST)
                return TJS_E_MEMBERNOTFOUND;
            result->Clear();
            return TJS_S_OK;
        }
        *result = convertPSBLazy(val);
        return TJS_S_OK;
    }

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                                            tTJSVariant *result,
                                            iTJSDispatch2 *objthis) override {
        if(!result) return TJS_E_FAIL;
        auto val = (*_dict)[num];
        if(!val) {
            if(flag & TJS_MEMBERMUSTEXIST)
                return TJS_E_MEMBERNOTFOUND;
            result->Clear();
            return TJS_S_OK;
        }
        *result = convertPSBLazy(val);
        return TJS_S_OK;
    }

    tjs_error PropSet(tjs_uint32 flag,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint,
                                      const tTJSVariant *param,
                                      iTJSDispatch2 *objthis) override {
        return TJS_E_NOTIMPL;
    }

    tjs_error GetCount(tjs_int *result,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint,
                                        iTJSDispatch2 *objthis) override {
        if(membername) return TJS_E_NOTIMPL;
        if(result) *result = (tjs_int)_dict->size();
        return TJS_S_OK;
    }

    tjs_error EnumMembers(tjs_uint32 flag,
                                           tTJSVariantClosure *callback,
                                           iTJSDispatch2 *objthis) override {
        for(auto it = _dict->begin(); it != _dict->end(); ++it) {
            tTJSVariant name = ttstr(it->first);
            tTJSVariant flags = (tjs_int)0;
            tTJSVariant value = convertPSBLazy(it->second);
            tTJSVariant *args[] = {&name, &flags, &value};
            tTJSVariant res;
            callback->FuncCall(0, nullptr, nullptr, &res, 3, args, nullptr);
        }
        return TJS_S_OK;
    }

    tjs_error IsInstanceOf(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            const tjs_char *classname,
                                            iTJSDispatch2 *objthis) override {
        if(!membername && classname) {
            if(TJS_strcmp(classname, TJS_W("Dictionary")) == 0)
                return TJS_S_TRUE;
        }
        return TJS_S_FALSE;
    }

    tjs_error IsValid(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       iTJSDispatch2 *objthis) override {
        return membername ? TJS_E_MEMBERNOTFOUND : TJS_S_TRUE;
    }
};

class PSBLazyList : public tTJSDispatch {
    std::shared_ptr<PSB::PSBList> _list;

public:
    explicit PSBLazyList(std::shared_ptr<PSB::PSBList> list)
        : _list(std::move(list)) {}

    tjs_error PropGet(tjs_uint32 flag,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint, tTJSVariant *result,
                                      iTJSDispatch2 *objthis) override {
        if(!membername || !result) return TJS_E_NOTIMPL;

        if(TJS_strcmp(membername, TJS_W("count")) == 0) {
            *result = (tjs_int)_list->size();
            return TJS_S_OK;
        }

        try {
            int idx = std::stoi(ttstr(membername).AsStdString());
            if(idx >= 0 && idx < (int)_list->size()) {
                *result = convertPSBLazy((*_list)[idx]);
                return TJS_S_OK;
            }
        } catch(...) {}
        if(flag & TJS_MEMBERMUSTEXIST)
            return TJS_E_MEMBERNOTFOUND;
        result->Clear();
        return TJS_S_OK;
    }

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                                            tTJSVariant *result,
                                            iTJSDispatch2 *objthis) override {
        if(!result) return TJS_E_FAIL;
        if(num < 0 || num >= (tjs_int)_list->size()) {
            if(flag & TJS_MEMBERMUSTEXIST)
                return TJS_E_MEMBERNOTFOUND;
            result->Clear();
            return TJS_S_OK;
        }
        *result = convertPSBLazy((*_list)[num]);
        return TJS_S_OK;
    }

    tjs_error PropSet(tjs_uint32 flag,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint,
                                      const tTJSVariant *param,
                                      iTJSDispatch2 *objthis) override {
        return TJS_E_NOTIMPL;
    }

    tjs_error GetCount(tjs_int *result,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint,
                                        iTJSDispatch2 *objthis) override {
        if(membername) return TJS_E_NOTIMPL;
        if(result) *result = (tjs_int)_list->size();
        return TJS_S_OK;
    }

    tjs_error EnumMembers(tjs_uint32 flag,
                                           tTJSVariantClosure *callback,
                                           iTJSDispatch2 *objthis) override {
        for(size_t i = 0; i < _list->size(); i++) {
            tTJSVariant name = (tjs_int)i;
            tTJSVariant flags = (tjs_int)0;
            tTJSVariant value = convertPSBLazy((*_list)[(int)i]);
            tTJSVariant *args[] = {&name, &flags, &value};
            tTJSVariant res;
            callback->FuncCall(0, nullptr, nullptr, &res, 3, args, nullptr);
        }
        return TJS_S_OK;
    }

    tjs_error IsInstanceOf(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            const tjs_char *classname,
                                            iTJSDispatch2 *objthis) override {
        if(!membername && classname) {
            if(TJS_strcmp(classname, TJS_W("Array")) == 0) return TJS_S_TRUE;
        }
        return TJS_S_FALSE;
    }

    tjs_error IsValid(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       iTJSDispatch2 *objthis) override {
        return membername ? TJS_E_MEMBERNOTFOUND : TJS_S_TRUE;
    }
};

static tTJSVariant convertPSBLazy(const std::shared_ptr<PSB::IPSBValue> &val) {
    if(!val) return {};

    auto type = val->getType();

    if(type == PSB::PSBObjType::Objects) {
        auto dict = std::dynamic_pointer_cast<PSB::PSBDictionary>(val);
        if(dict) {
            auto *proxy = new PSBLazyDict(std::move(dict));
            tTJSVariant result(proxy, proxy);
            proxy->Release();
            return result;
        }
    }

    if(type == PSB::PSBObjType::List) {
        auto list = std::dynamic_pointer_cast<PSB::PSBList>(val);
        if(list) {
            auto *proxy = new PSBLazyList(std::move(list));
            tTJSVariant result(proxy, proxy);
            proxy->Release();
            return result;
        }
    }

    return val->toTJSVal();
}

// ---------------------------------------------------------------------------

static tjs_error getRoot(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                         iTJSDispatch2 *obj) {
    auto *self = ncbInstanceAdaptor<PSB::PSBFile>::GetNativeInstance(obj);
    const auto &root = self->getRootValue();
    if(root) {
        // Gallery scripts inspect and copy small PIMG roots as real
        // Array/Dictionary objects. The lazy proxy is kept for large scenario
        // PSBs where eager conversion is expensive.
        if(self->getType() == PSB::PSBType::Pimg) {
            *r = root->toTJSVal();
        } else {
            *r = convertPSBLazy(root);
        }
    } else {
        r->Clear();
    }
    return TJS_S_OK;
}

static void registerPsbResources(PSBFile *self, ttstr path) {
    if(!psbMedia)
        return;
    psbMedia->NormalizeDomainName(path);
    // PSB archives are immutable for the lifetime of an engine session.
    // Re-register in place so PSBMedia::add can retain converted subimages
    // when scripts repeatedly construct/load the same archive.
    registerRootResources(path, *self);
}

static tjs_error load(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                      iTJSDispatch2 *obj) {
    bool loadSuccess = true;
    auto *self = ncbInstanceAdaptor<PSB::PSBFile>::GetNativeInstance(obj);
    if(psbDebugEnabled()) {
        LOGGER->info("PSBFile.load enter count={} first={}", count,
                     count > 0 ? describeVariant(p[0]) : std::string("<none>"));
    }
    if(count < 1) {
        return TJS_E_BADPARAMCOUNT;
    }

    if(p[0]->Type() == tvtString) {
        ttstr path{ *p[0] };
        try {
            if(!self->loadPSBFile(path)) {
                LOGGER->info("cannot load psb file : {}", path.AsStdString());
                loadSuccess = false;
            } else {
                registerPsbResources(self, path);
            }
        } catch(const std::exception &e) {
            LOGGER->warn("PSBFile load error: {} ({})", e.what(), path.AsStdString());
            loadSuccess = false;
        } catch(...) {
            LOGGER->warn("PSBFile load unknown error: {}", path.AsStdString());
            loadSuccess = false;
        }
    } else if(p[0]->Type() == tvtOctet) {
        auto *octet = p[0]->AsOctetNoAddRef();
        try {
            if(!octet || !self->loadPSBData(
                              octet->GetData(), octet->GetLength(),
                              ttstr(TJS_W("<octet>")))) {
                LOGGER->info("cannot load psb data from octet");
                loadSuccess = false;
            }
        } catch(const std::exception &e) {
            LOGGER->warn("PSBFile load octet error: {}", e.what());
            loadSuccess = false;
        } catch(...) {
            LOGGER->warn("PSBFile load octet unknown error");
            loadSuccess = false;
        }
    } else {
        LOGGER->warn("PSBFile.load invalid first argument type={} count={}",
                     static_cast<int>(p[0]->Type()), count);
        return TJS_E_INVALIDPARAM;
    }

    if(r != nullptr)
        *r = tTJSVariant(loadSuccess);
    return TJS_S_OK;
}

// 因为有两种版本的psbfile插件调用方式不一样
// TODO: 第一种（新) 实现有问题, 可能忽略了某些东西
// var psbfile = new PSBFile();
// psbfile.load("xxxx.PIMG");
// 第二种（旧)
// new PSBFile("xxxx.PIMG");

template <typename T>
class PSBFileConvertor {
    typedef ncbTypeConvertor::Stripper<PSBFile>::Type ClassT;
    typedef ncbInstanceAdaptor<ClassT> AdaptorT;

public:
    PSBFileConvertor() = default;
    virtual ~PSBFileConvertor() = default;

    virtual void operator()(T *&dst, const tTJSVariant &src) {
        if(src.Type() == tvtObject) {
            dst = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef());
        }
    }

    void operator()(tTJSVariant &dst, const T *&src) {
        if(src != nullptr) {
            if(iTJSDispatch2 *adpObj = AdaptorT::CreateAdaptor(src)) {
                dst = tTJSVariant(adpObj, adpObj);
                adpObj->Release();
            }
        } else {
            dst.Clear();
        }
    }
};

NCB_SET_CONVERTOR(PSBFile, PSBFileConvertor<PSBFile>);
NCB_SET_CONVERTOR(const PSBFile *, PSBFileConvertor<const PSBFile>);

static tjs_error PSBFileFactory(PSBFile **result, tjs_int count,
                                tTJSVariant **params, iTJSDispatch2 *_) {
    PSBFile *psbFile = nullptr;
    if(psbDebugEnabled()) {
        LOGGER->info("PSBFile factory enter count={} first={}", count,
                     count > 0 ? describeVariant(params[0])
                               : std::string("<none>"));
    }
    if(count == 0) {
        psbFile = new PSBFile();
    } else if(count >= 1 && params[0]->Type() == tvtString) {
        ttstr path{ *params[0] };
        psbFile = new PSBFile();
        try {
            if(psbFile->loadPSBFile(path)) {
                registerPsbResources(psbFile, path);
            } else {
                LOGGER->warn("Failed to load PSB file: {}", path.AsStdString());
            }
        } catch(const std::exception &e) {
            LOGGER->warn("PSBFile load error: {} ({})", e.what(), path.AsStdString());
        } catch(...) {
            LOGGER->warn("PSBFile load unknown error: {}", path.AsStdString());
        }
    } else if(count >= 1 && params[0]->Type() == tvtOctet) {
        auto *octet = params[0]->AsOctetNoAddRef();
        psbFile = new PSBFile();
        try {
            if(!octet || !psbFile->loadPSBData(
                              octet->GetData(), octet->GetLength(),
                              ttstr(TJS_W("<octet>")))) {
                LOGGER->warn("Failed to load PSB data from octet");
            }
        } catch(const std::exception &e) {
            LOGGER->warn("PSBFile load octet error: {}", e.what());
        } catch(...) {
            LOGGER->warn("PSBFile load octet unknown error");
        }
    } else {
        if(count > 0) {
            LOGGER->warn("PSBFile factory invalid first argument type={} count={}",
                         static_cast<int>(params[0]->Type()), count);
        }
        return TJS_E_INVALIDPARAM;
    }
    *result = psbFile;
    return TJS_S_OK;
}

NCB_REGISTER_CLASS(PSBFile) {
    Factory(PSBFileFactory);
    RawCallback(TJS_W("root"), &getRoot, 0, 0);
    RawCallback(TJS_W("load"), &load, 0);
}

NCB_PRE_REGIST_CALLBACK(initPsbFile);
NCB_POST_UNREGIST_CALLBACK(deInitPsbFile);
