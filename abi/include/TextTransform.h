#pragma once

#include "engine_api.h"

#include <string>

// Runtime-neutral text transformation hook.  Public runtimes only know that
// text may be transformed; the implementation (and any model state) lives in
// the optional private package.
using tTVPTextTransformCallback = bool (*)(const char *runtime_id,
                                            const std::string &input,
                                            std::string *output);
using tTVPTextPrefetchCallback = void (*)(const char *runtime_id,
                                           const std::string &input);

// The engine_api library owns the hook state; engine sources compiled into
// the runtime glue resolve these across the library boundary.
ENGINE_API_EXPORT void TVPSetTextTransformCallback(tTVPTextTransformCallback callback);
ENGINE_API_EXPORT void TVPSetTextPrefetchCallback(tTVPTextPrefetchCallback callback);
ENGINE_API_EXPORT std::string TVPTransformText(const char *runtime_id,
                                               const std::string &input);
ENGINE_API_EXPORT void TVPPrefetchText(const char *runtime_id,
                                        const std::string &input);
