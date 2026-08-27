#pragma once

#include <memory>
#include <vector>

#include "tjs.h"
#include "PSBFile.h"

namespace PSB {
    // AetherKiri owns PSB media lifecycle in psbfile/main.cpp.
    // These wrappers are kept so motionplayer can share the same API surface
    // as kirikiriroid2-web.
    void initPSBMedia();
    void deInitPSBMedia();

    void registerRootResources(const ttstr &container,
                               const std::shared_ptr<const PSBDictionary> &root);
    void registerRootResources(const std::vector<ttstr> &containers,
                               const std::shared_ptr<const PSBDictionary> &root);

    void registerRootResources(const ttstr &container, const PSBFile &file);
    void registerRootResources(const std::vector<ttstr> &containers,
                               const PSBFile &file);

    // Lazy motion loading already registers the generic PSB object/resource
    // tree in PSBMedia.  This narrow hook adds only the authored slice-set
    // mapping, avoiding a second full registration pass.
    void registerMotionSliceResources(const ttstr &container,
                                      const PSBFile &file);
    void registerMotionSliceResources(const std::vector<ttstr> &containers,
                                      const PSBFile &file);
} // namespace PSB
