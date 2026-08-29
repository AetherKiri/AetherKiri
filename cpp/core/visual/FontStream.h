#pragma once

#include "tjs.h"

#include <cstddef>

// Return a read-only stream backed by the shared font-byte cache.  Ownership
// of `source` is transferred to this function in all cases: callers should
// never delete it after calling.  If the stream is too large or cannot be
// read completely, the original stream is returned so legacy behavior is
// preserved.
tTJSBinaryStream *TVPCreateCachedFontStream(tTJSBinaryStream *source,
                                             const ttstr &cacheKey);

// Clear cached font bytes during font-library teardown. Existing streams keep
// their shared buffer alive until they are destroyed.
void TVPClearFontStreamCache();

// Tune the bounded cache for hosts with a smaller memory budget. The default
// is intentionally conservative for mobile/embedded builds.
void TVPSetFontStreamCacheLimits(std::size_t maxBytes,
                                 std::size_t maxEntries);
