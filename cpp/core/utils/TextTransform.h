#pragma once

#include <string>

// Runtime-neutral text transformation hook.  Public runtimes only know that
// text may be transformed; the implementation (and any model state) lives in
// the optional private package.
using tTVPTextTransformCallback = bool (*)(const char *runtime_id,
                                           const std::string &input,
                                           std::string *output);
using tTVPTextPrefetchCallback = void (*)(const char *runtime_id,
                                          const std::string &input);

void TVPSetTextTransformCallback(tTVPTextTransformCallback callback);
void TVPSetTextPrefetchCallback(tTVPTextPrefetchCallback callback);
std::string TVPTransformText(const char *runtime_id,
                             const std::string &input);
void TVPPrefetchText(const char *runtime_id, const std::string &input);
