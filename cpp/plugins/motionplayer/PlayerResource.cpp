// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "SourceCache.h"

using namespace motion::internal;

namespace motion {

    // --- Resource management ---
    void Player::unload(ttstr name) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return;
        }

        for(auto it = _runtime->motionsByKey.begin();
            it != _runtime->motionsByKey.end();) {
            if(it->first == key || it->second->path == key) {
                if(_runtime->activeMotion == it->second) {
                    _runtime->activeMotion.reset();
                    _runtime->timelines.clear();
                    _runtime->playingTimelineLabels.clear();
                }
                it = _runtime->motionsByKey.erase(it);
            } else {
                ++it;
            }
        }

        if(_runtime->sourceCacheNative) {
            _runtime->sourceCacheNative->eraseSource(name);
        }
    }

    void Player::unloadAll() {
        _runtime->motionsByKey.clear();
        if(_runtime->sourceCacheNative) {
            _runtime->sourceCacheNative->clearCache();
        }
        _runtime->activeMotion.reset();
        _runtime->timelines.clear();
        _runtime->playingTimelineLabels.clear();
        _runtime->layerIdsByName.clear();
        _runtime->layerNamesById.clear();
        _resourceManagerNative.clearCache();
        _runtime->lastCanvas.Clear();
        _runtime->lastViewParam.Clear();
        _runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        _variableValues.clear();
        _variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _mirrorPositiveCache.clear();
        _mirrorNegativeCache.clear();
        _motionKey.Clear();
    }

    bool Player::isExistMotion(ttstr name) {
        return static_cast<bool>(
            resolveMotion(*_runtime, name, &_resourceManagerNative));
    }

    tTJSVariant Player::findMotion(ttstr name) {
        const auto snapshot =
            resolveMotion(*_runtime, name, &_resourceManagerNative);
        if(!snapshot) {
            return {};
        }

        activateMotion(*_runtime, snapshot);
        _motionKey = name;
        syncVariableKeysFromActiveMotion();
        return snapshot->moduleValue;
    }

    tjs_int Player::requireLayerId(ttstr name) {
        const auto key = detail::narrow(name);
        // Aligned to libkrkr2.so: after eager Player_buildNodeTree, the
        // label map is authoritative for any loaded motion; an empty
        // nodeLabelMap simply means no motion is loaded yet.
        if(_runtime) {
            if(const auto it = _runtime->nodeLabelMap.find(key);
               it != _runtime->nodeLabelMap.end()) {
                const auto nodeIndex = it->second;
                if(nodeIndex >= 0 &&
                   nodeIndex < static_cast<int>(_runtime->nodes.size()) &&
                   _runtime->nodes[nodeIndex].layerId1 != 0) {
                    return _runtime->nodes[nodeIndex].layerId1;
                }
            }
        }
        return _resourceManagerNative.requireLayerIdForName(name);
    }

    void Player::releaseLayerId(tjs_int id) {
        _resourceManagerNative.releaseLayerId(id);
    }


} // namespace motion
