#pragma once

#include "tjs.h"

#include <string>

namespace AetherKiri {

// A small persistent replacement for the Windows registry subset used by
// legacy KiriKiri scripts.  The store deliberately accepts only value types
// that have a stable, process-independent representation; object/closure
// values are rejected instead of being silently stringified.
class PortableRegistryStore {
public:
    static PortableRegistryStore &Instance();

    bool Write(const ttstr &key, const tTJSVariant &value);
    bool Read(const ttstr &key, tTJSVariant &value);
    bool DeleteValue(const ttstr &key);
    bool DeleteKey(const ttstr &key);

    // Flush is normally called by every mutation.  It is public so host
    // shutdown code and focused contract tests can force the durable write.
    bool Flush();

private:
    PortableRegistryStore() = default;
    PortableRegistryStore(const PortableRegistryStore &) = delete;
    PortableRegistryStore &operator=(const PortableRegistryStore &) = delete;
};

} // namespace AetherKiri
