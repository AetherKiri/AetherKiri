#pragma once

// Include Aether's thread ABI before the upstream declaration.  The krkrz
// header uses the same ThreadIntfH guard, so this prevents its standalone
// thread implementation from becoming the base class of the bridge.
#include "ThreadIntf.h"

// The transport declaration/implementation still comes from the pinned
// checkout; the bridge adapts only the small constructor/startup difference
// (Aether starts suspended and resumes after derived members are initialized).
#include "../../../third_party/krkrz_dev/src/core/common/utils/DAPServer.h"
