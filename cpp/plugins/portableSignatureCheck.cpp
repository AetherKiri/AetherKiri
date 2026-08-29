#include "portableSignatureCheck.h"

#include "EventIntf.h"
#include "StorageIntf.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(AETHERKIRI_HAS_OPENSSL)
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#endif

namespace AetherKiri {

struct PortableSignatureCheck::Job {
    tjs_int handler = 0;
    ttstr filename;
    std::string publicKey;
    tTJSVariant info;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> stopped{false};
    std::atomic<bool> finished{false};
};

namespace {

constexpr std::size_t kMaxSignatureFileBytes = 1u * 1024u * 1024u;
constexpr std::size_t kMaxTargetFileBytes = 1024u * 1024u * 1024u;
constexpr std::size_t kReadChunkBytes = 64u * 1024u;

constexpr char kEmbeddedOptionsMark[] = " OPT_EMBED_AREA_";
constexpr char kEmbeddedCoreMark[] = " CORE_SIG_______";
constexpr char kEmbeddedReleaseMark[] = " RELEASE_SIG____";
constexpr std::array<std::uint8_t, 11> kXp3Mark = {
    static_cast<std::uint8_t>(' '), static_cast<std::uint8_t>('P'),
    static_cast<std::uint8_t>('3'), 0x0d, 0x0a, 0x20, 0x0a, 0x1a, 0x8b,
    0x67, 0x01};
constexpr char kSignaturePrefix[] = "-- SIGNATURE - SHA256/PSS/RSA --";

constexpr tjs_uint32 eventTagForHandler(tjs_int handler) {
    // A zero tag has special meaning to TVPCancelEvents (wildcard).  Keep the
    // public handler zero-based while using a stable non-zero queue tag.
    return handler < 0 ? 1u : static_cast<tjs_uint32>(handler) + 1u;
}

struct VerificationInput {
    std::vector<std::uint8_t> file;
    std::string signatureText;
    std::size_t skipBegin = std::numeric_limits<std::size_t>::max();
    std::size_t skipEnd = std::numeric_limits<std::size_t>::max();
};

std::string narrow(const ttstr &value) {
    const tjs_int length = TVPWideCharToUtf8String(value.c_str(), nullptr);
    if(length <= 0)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if(TVPWideCharToUtf8String(value.c_str(), result.data()) < 0)
        return {};
    if(!result.empty() && result.back() == '\0')
        result.pop_back();
    return result;
}

ttstr wide(const std::string &value) {
    if(value.empty())
        return ttstr();
    const tjs_int length = TVPUtf8ToWideCharString(
        value.data(), static_cast<tjs_uint>(std::min<std::size_t>(
            value.size(), std::numeric_limits<tjs_uint>::max())), nullptr);
    if(length <= 0)
        return ttstr();
    std::vector<tjs_char> buffer(static_cast<std::size_t>(length) + 1, 0);
    TVPUtf8ToWideCharString(value.data(), static_cast<tjs_uint>(std::min<std::size_t>(
                                  value.size(),
                                  std::numeric_limits<tjs_uint>::max())),
                            buffer.data());
    return ttstr(buffer.data(), length);
}

bool readStream(const ttstr &name, std::vector<std::uint8_t> &bytes,
                std::size_t limit, ttstr &error) {
    bytes.clear();
    try {
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name,
                                                                  TJS_BS_READ));
        if(!stream) {
            error = TJS_W("cannot open file");
            return false;
        }
        const tjs_uint64 declared = stream->GetSize();
        if(declared > static_cast<tjs_uint64>(limit)) {
            error = TJS_W("file is too large");
            return false;
        }
        if(declared > 0)
            bytes.reserve(static_cast<std::size_t>(declared));
        std::array<std::uint8_t, kReadChunkBytes> chunk{};
        while(bytes.size() < limit) {
            const tjs_uint got = stream->Read(chunk.data(),
                                              static_cast<tjs_uint>(chunk.size()));
            if(got == 0)
                break;
            if(static_cast<std::size_t>(got) > limit - bytes.size()) {
                error = TJS_W("file is too large");
                return false;
            }
            bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + got);
            if(declared != 0 && bytes.size() >= declared)
                break;
        }
        if(declared != 0 && bytes.size() != declared) {
            error = TJS_W("short read");
            return false;
        }
        return true;
    } catch(...) {
        error = TJS_W("exception while reading file");
        return false;
    }
}

std::size_t findBytes(const std::vector<std::uint8_t> &haystack,
                      const std::uint8_t *needle, std::size_t needleSize) {
    if(!needle || needleSize == 0 || needleSize > haystack.size())
        return std::numeric_limits<std::size_t>::max();
    const auto first = haystack.begin();
    const auto last = haystack.end();
    const auto it = std::search(first, last, needle, needle + needleSize);
    return it == last ? std::numeric_limits<std::size_t>::max()
                      : static_cast<std::size_t>(it - first);
}

std::size_t findAscii(const std::vector<std::uint8_t> &haystack,
                      const char *needle) {
    return needle ? findBytes(
                        haystack,
                        reinterpret_cast<const std::uint8_t *>(needle),
                        std::strlen(needle))
                  : std::numeric_limits<std::size_t>::max();
}

bool readVerificationInput(const ttstr &filename, VerificationInput &input,
                           ttstr &error) {
    if(!readStream(filename, input.file, kMaxTargetFileBytes, error))
        return false;

    const std::size_t options = findAscii(input.file, kEmbeddedOptionsMark);
    const std::size_t release = findAscii(input.file, kEmbeddedReleaseMark);
    // CORE_SIG is not needed to hash/verify, but requiring it when present
    // catches malformed executable markers without changing normal-file
    // behaviour.  Old krkr2 images may omit it, so it is intentionally only
    // diagnostic and not a hard requirement.
    (void)findAscii(input.file, kEmbeddedCoreMark);

    if(options != std::numeric_limits<std::size_t>::max() &&
       release != std::numeric_limits<std::size_t>::max() && options > 0 &&
       release > 0) {
        const std::size_t signatureBegin =
            release + std::strlen(kEmbeddedReleaseMark) + 4u;
        const std::size_t xp3 = findBytes(input.file, kXp3Mark.data(),
                                          kXp3Mark.size());
        const std::size_t signatureEnd =
            xp3 != std::numeric_limits<std::size_t>::max() &&
                    xp3 > signatureBegin
                ? xp3
                : input.file.size();
        if(signatureBegin >= signatureEnd || signatureBegin > input.file.size()) {
            error = TJS_W("embedded signature area is invalid");
            return false;
        }
        input.signatureText.assign(
            reinterpret_cast<const char *>(input.file.data() + signatureBegin),
            signatureEnd - signatureBegin);
        input.skipBegin = options;
        input.skipEnd = xp3 != std::numeric_limits<std::size_t>::max() &&
                xp3 > options
            ? xp3
            : input.file.size();
        return true;
    }

    const ttstr sidecar = filename + TJS_W(".sig");
    if(!TVPIsExistentStorage(sidecar)) {
        error = TJS_W("signature file does not exist");
        return false;
    }
    std::vector<std::uint8_t> signature;
    if(!readStream(sidecar, signature, kMaxSignatureFileBytes, error))
        return false;
    input.signatureText.assign(reinterpret_cast<const char *>(signature.data()),
                               signature.size());
    return true;
}

bool decodeBase64(const std::string &input, std::vector<std::uint8_t> &output) {
    output.clear();
    std::string compact;
    compact.reserve(input.size());
    for(unsigned char c : input) {
        if(std::isspace(c))
            continue;
        if(c == '\0' || (std::isalnum(c) == 0 && c != '+' && c != '/' &&
                          c != '='))
            return false;
        compact.push_back(static_cast<char>(c));
    }
    if(compact.empty())
        return false;
    const std::size_t remainder = compact.size() & 3u;
    if(remainder == 1)
        return false;
    if(remainder != 0)
        compact.append(4u - remainder, '=');

    auto value = [](char c) -> int {
        if(c >= 'A' && c <= 'Z') return c - 'A';
        if(c >= 'a' && c <= 'z') return c - 'a' + 26;
        if(c >= '0' && c <= '9') return c - '0' + 52;
        if(c == '+') return 62;
        if(c == '/') return 63;
        return -1;
    };

    output.reserve((compact.size() / 4u) * 3u);
    for(std::size_t i = 0; i < compact.size(); i += 4) {
        const char a = compact[i];
        const char b = compact[i + 1];
        const char c = compact[i + 2];
        const char d = compact[i + 3];
        const int va = value(a);
        const int vb = value(b);
        const bool padC = c == '=';
        const bool padD = d == '=';
        const int vc = padC ? 0 : value(c);
        const int vd = padD ? 0 : value(d);
        if(va < 0 || vb < 0 || vc < 0 || vd < 0 ||
           (padC && !padD) ||
           ((padC || padD) && i + 4u != compact.size()))
            return false;
        output.push_back(static_cast<std::uint8_t>((va << 2) | (vb >> 4)));
        if(!padC)
            output.push_back(static_cast<std::uint8_t>((vb << 4) | (vc >> 2)));
        if(!padD)
            output.push_back(static_cast<std::uint8_t>((vc << 6) | vd));
    }
    return !output.empty();
}

bool isCancelled(const std::shared_ptr<PortableSignatureCheck::Job> &job) {
    return !job || job->cancelled.load(std::memory_order_acquire);
}

#if defined(AETHERKIRI_HAS_OPENSSL)
ttstr opensslError(const tjs_char *fallback) {
    const unsigned long code = ERR_get_error();
    if(code == 0)
        return ttstr(fallback);
    char buffer[256] = {};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return wide(buffer);
}
#endif

int verifyInput(const VerificationInput &input, const std::string &publicKey,
                const std::shared_ptr<PortableSignatureCheck::Job> &job,
                const std::function<void(tjs_int)> &progress, ttstr &error) {
    if(publicKey.empty()) {
        error = TJS_W("public key is empty");
        return -2;
    }
    std::string encoded = input.signatureText;
    const std::size_t prefixLength = std::strlen(kSignaturePrefix);
    if(encoded.size() < prefixLength ||
       encoded.compare(0, prefixLength, kSignaturePrefix) != 0) {
        error = TJS_W("invalid signature file format");
        return -2;
    }
    encoded.erase(0, prefixLength);
    std::vector<std::uint8_t> signature;
    if(!decodeBase64(encoded, signature)) {
        error = TJS_W("invalid base64 signature");
        return -2;
    }

#if !defined(AETHERKIRI_HAS_OPENSSL)
    (void)input;
    (void)job;
    (void)progress;
    (void)signature;
    error = TJS_W("portable signature verifier is unavailable");
    return -2;
#else
    BIO *bio = BIO_new_mem_buf(publicKey.data(),
                               static_cast<int>(std::min<std::size_t>(
                                   publicKey.size(),
                                   static_cast<std::size_t>(
                                       std::numeric_limits<int>::max()))));
    if(!bio) {
        error = opensslError(TJS_W("cannot allocate public-key parser"));
        return -2;
    }
    EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if(!key) {
        error = opensslError(TJS_W("cannot parse public key"));
        return -2;
    }
    const int keyType = EVP_PKEY_base_id(key);
    if(keyType != EVP_PKEY_RSA && keyType != EVP_PKEY_RSA_PSS) {
        EVP_PKEY_free(key);
        error = TJS_W("public key is not RSA");
        return -2;
    }

    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if(!context) {
        EVP_PKEY_free(key);
        error = TJS_W("cannot allocate signature verifier");
        return -2;
    }
    EVP_PKEY_CTX *keyContext = nullptr;
    int status = -2;
    do {
        if(EVP_DigestVerifyInit(context, &keyContext, EVP_sha256(), nullptr,
                                key) <= 0)
            break;
        if(!keyContext || EVP_PKEY_CTX_set_rsa_padding(
                              keyContext, RSA_PKCS1_PSS_PADDING) <= 0 ||
           EVP_PKEY_CTX_set_rsa_pss_saltlen(keyContext,
                                            RSA_PSS_SALTLEN_DIGEST) <= 0)
            break;

        const std::size_t skipBegin = input.skipBegin;
        const std::size_t skipEnd = input.skipEnd;
        const bool skip = skipBegin != std::numeric_limits<std::size_t>::max() &&
                          skipEnd != std::numeric_limits<std::size_t>::max() &&
                          skipBegin < skipEnd && skipEnd <= input.file.size();
        const std::size_t total = skip
            ? input.file.size() - (skipEnd - skipBegin)
            : input.file.size();
        std::size_t processed = 0;
        tjs_int lastPercent = -1;
        auto update = [&](std::size_t amount) -> bool {
            processed += amount;
            if(isCancelled(job))
                return false;
            const tjs_int percent = total == 0
                ? 100
                : static_cast<tjs_int>(std::min<std::uint64_t>(
                      100u,
                      (static_cast<std::uint64_t>(processed) * 100u) /
                          static_cast<std::uint64_t>(total)));
            if(percent != lastPercent) {
                lastPercent = percent;
                if(progress)
                    progress(percent);
            }
            return true;
        };
        auto feed = [&](std::size_t begin, std::size_t end) -> bool {
            while(begin < end) {
                const std::size_t amount = std::min(kReadChunkBytes, end - begin);
                if(EVP_DigestVerifyUpdate(context, input.file.data() + begin,
                                          amount) <= 0)
                    return false;
                begin += amount;
                if(!update(amount))
                    return false;
            }
            return true;
        };
        if((skip && !feed(0, skipBegin)) ||
           (!skip && !feed(0, input.file.size())) ||
           (skip && !feed(skipEnd, input.file.size()))) {
            if(isCancelled(job))
                status = -1;
            break;
        }
        if(progress)
            progress(100);
        const int verified = EVP_DigestVerifyFinal(
            context, signature.data(), signature.size());
        if(verified == 1) {
            status = 1;
        } else if(verified == 0) {
            status = 0;
            error = TJS_W("signature verification failed");
        }
    } while(false);
    if(status == -2 && error.IsEmpty())
        error = opensslError(TJS_W("signature verification failed"));
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return status;
#endif
}

} // namespace

struct PortableSignatureCheck::Worker {
    explicit Worker(std::shared_ptr<Job> value) : job(std::move(value)) {}
    std::shared_ptr<Job> job;
    std::thread thread;
};

PortableSignatureCheck::PortableSignatureCheck(iTJSDispatch2 *owner)
    : owner_(owner) {
    if(owner_)
        owner_->AddRef();
}

PortableSignatureCheck::~PortableSignatureCheck() {
    shuttingDown_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto &pair : jobs_) {
            pair.second->cancelled.store(true, std::memory_order_release);
            pair.second->stopped.store(true, std::memory_order_release);
        }
    }
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto &worker : workers_)
            if(worker && worker->thread.joinable())
                threads.push_back(std::move(worker->thread));
        workers_.clear();
        jobs_.clear();
    }
    for(auto &thread : threads)
        if(thread.joinable())
            thread.join();
    if(owner_) {
        TVPCancelSourceEvents(owner_);
        owner_->Release();
        owner_ = nullptr;
    }
}

tjs_int PortableSignatureCheck::checkSignature(const tjs_char *filename,
                                               const tjs_char *publicKey,
                                               const tTJSVariant &info) {
    reapFinished();
    auto job = std::make_shared<Job>();
    job->filename = filename ? filename : TJS_W("");
    job->publicKey = publicKey ? narrow(ttstr(publicKey)) : std::string();
    job->info = info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(shuttingDown_.load(std::memory_order_acquire))
            return -1;
        const tjs_int firstCandidate = nextHandler_;
        do {
            job->handler = nextHandler_;
            if(nextHandler_ == std::numeric_limits<tjs_int>::max())
                nextHandler_ = 0;
            else
                ++nextHandler_;
            if(jobs_.find(job->handler) == jobs_.end())
                break;
        } while(nextHandler_ != firstCandidate);
        if(jobs_.find(job->handler) != jobs_.end())
            return -1;
        jobs_[job->handler] = job;
        auto worker = std::make_unique<Worker>(job);
        worker->thread = std::thread([this, job] { run(job); });
        workers_.push_back(std::move(worker));
    }
    return job->handler;
}

bool PortableSignatureCheck::cancelCheckSignature(tjs_int handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(handler);
    if(it == jobs_.end())
        return false;
    it->second->cancelled.store(true, std::memory_order_release);
    return true;
}

bool PortableSignatureCheck::stopCheckSignature(tjs_int handler) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(handler);
        if(it == jobs_.end())
            return false;
        job = it->second;
        job->cancelled.store(true, std::memory_order_release);
        job->stopped.store(true, std::memory_order_release);
    }
    if(owner_) {
        static ttstr progressName(TJS_W("onCheckSignatureProgress"));
        static ttstr doneName(TJS_W("onCheckSignatureDone"));
        const tjs_uint32 tag = eventTagForHandler(handler);
        TVPCancelEvents(owner_, owner_, progressName, tag);
        TVPCancelEvents(owner_, owner_, doneName, tag);
    }
    return true;
}

void PortableSignatureCheck::reapFinished() {
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto it = workers_.begin(); it != workers_.end();) {
            if(!*it || !(*it)->job->finished.load(std::memory_order_acquire)) {
                ++it;
                continue;
            }
            if((*it)->thread.joinable())
                threads.push_back(std::move((*it)->thread));
            jobs_.erase((*it)->job->handler);
            it = workers_.erase(it);
        }
    }
    for(auto &thread : threads)
        if(thread.joinable())
            thread.join();
}

void PortableSignatureCheck::run(const std::shared_ptr<Job> &job) {
    int status = -2;
    ttstr error;
    try {
        VerificationInput input;
        if(!readVerificationInput(job->filename, input, error)) {
            status = -2;
        } else {
            postProgress(job, 0);
            status = verifyInput(
                input, job->publicKey, job,
                [this, job](tjs_int percent) { postProgress(job, percent); },
                error);
        }
    } catch(...) {
        status = -2;
        error = TJS_W("exception while checking signature");
    }
    if(job->cancelled.load(std::memory_order_acquire) && status != 1)
        status = -1;
    if(!job->stopped.load(std::memory_order_acquire) &&
       !shuttingDown_.load(std::memory_order_acquire))
        postDone(job, status, error);
    job->finished.store(true, std::memory_order_release);
}

void PortableSignatureCheck::postProgress(const std::shared_ptr<Job> &job,
                                          tjs_int percent) {
    if(!owner_ || !job || job->stopped.load(std::memory_order_acquire) ||
       shuttingDown_.load(std::memory_order_acquire))
        return;
    static ttstr eventName(TJS_W("onCheckSignatureProgress"));
    tTJSVariant params[] = {
        tTJSVariant(job->handler), tTJSVariant(job->info),
        tTJSVariant(std::clamp(percent, 0, 100))};
    TVPPostEvent(owner_, owner_, eventName, eventTagForHandler(job->handler),
                 TVP_EPT_POST, 3, params);
}

void PortableSignatureCheck::postDone(const std::shared_ptr<Job> &job,
                                      tjs_int status, const ttstr &message) {
    if(!owner_ || !job || job->stopped.load(std::memory_order_acquire) ||
       shuttingDown_.load(std::memory_order_acquire))
        return;
    static ttstr eventName(TJS_W("onCheckSignatureDone"));
    tTJSVariant params[] = {tTJSVariant(job->handler), tTJSVariant(job->info),
                            tTJSVariant(status), tTJSVariant(message)};
    TVPPostEvent(owner_, owner_, eventName, eventTagForHandler(job->handler),
                 TVP_EPT_POST, 4, params);
}

} // namespace AetherKiri
