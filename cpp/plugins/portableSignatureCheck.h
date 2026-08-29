#pragma once

// Portable implementation of the krkrz sigcheck.dll contract.  The public
// class deliberately exposes only TJS-facing operations; cryptographic
// provider types stay in the translation unit so an embedder without
// OpenSSL can still compile the compatibility surface and receive a
// fail-closed result.

#include "tp_stub.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace AetherKiri {

class PortableSignatureCheck final {
public:
    explicit PortableSignatureCheck(iTJSDispatch2 *owner);
    ~PortableSignatureCheck();

    PortableSignatureCheck(const PortableSignatureCheck &) = delete;
    PortableSignatureCheck &operator=(const PortableSignatureCheck &) = delete;

    tjs_int checkSignature(const tjs_char *filename, const tjs_char *publicKey,
                           const tTJSVariant &info);
    bool cancelCheckSignature(tjs_int handler);
    bool stopCheckSignature(tjs_int handler);

public:
    // Definitions stay private to the implementation file; the declarations
    // are public only so its bounded worker helpers can use shared ownership
    // without leaking any OpenSSL types through this header.
    struct Job;
    struct Worker;

private:
    void run(const std::shared_ptr<Job> &job);
    void reapFinished();
    void postProgress(const std::shared_ptr<Job> &job, tjs_int percent);
    void postDone(const std::shared_ptr<Job> &job, tjs_int status,
                  const ttstr &message);

    iTJSDispatch2 *owner_ = nullptr;
    std::mutex mutex_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::map<tjs_int, std::shared_ptr<Job>> jobs_;
    std::atomic<bool> shuttingDown_{false};
    // Match krkrz's zero-based handler allocation.  Event tags use a
    // separate non-zero encoding so handler 0 does not mean "all events".
    tjs_int nextHandler_ = 0;
};

} // namespace AetherKiri
