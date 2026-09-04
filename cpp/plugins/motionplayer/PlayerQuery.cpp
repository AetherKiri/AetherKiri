// PlayerQuery.cpp — Viewport, timeline/variable queries, selector, misc, compat
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"
#include "SourceCache.h"
#include "godot/GodotRenderManager.h"

#include <cstdlib>
#include <cstring>

using namespace motion::internal;

namespace {
    template<typename Shape>
    tTJSVariant makeNativeShapeVariant(Shape *native) {
        if(auto *dispatch = ncbInstanceAdaptor<Shape>::CreateAdaptor(native)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        delete native;
        return {};
    }

    bool motionDebugEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_DEBUG");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    bool motionHitDebugEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_HIT_DEBUG");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    bool emoteTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_EMOTE_TRACE");
            return value && *value && std::strcmp(value, "0") != 0;
        }();
        return enabled;
    }

    bool emoteAffineTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_EMOTE_AFFINE_TRACE");
            return value && *value && std::strcmp(value, "0") != 0;
        }();
        return enabled;
    }

    bool emoteTimelineTraceEnabled() {
        static const bool enabled = [] {
            const char *value =
                std::getenv("AETHERKIRI_EMOTE_TIMELINE_TRACE");
            return value && *value && std::strcmp(value, "0") != 0;
        }();
        return enabled;
    }

    std::string joinTimelineLabels(const std::vector<std::string> &labels) {
        std::string result;
        for(const auto &label : labels) {
            if(!result.empty()) {
                result += ',';
            }
            result += label;
        }
        return result;
    }

    std::string describeLayerForQueryDebug(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return "<null>";
        }
        const auto type = layer->GetType();
        const bool drawableType = type != ltBinder;
        tjs_int imageWidth = -1;
        tjs_int imageHeight = -1;
        bool hasImage = false;
        if(drawableType) {
            try {
                imageWidth = layer->GetImageWidth();
                imageHeight = layer->GetImageHeight();
                hasImage = layer->GetHasImage();
            } catch(...) {
                imageWidth = -1;
                imageHeight = -1;
                hasImage = false;
            }
        }
        std::ostringstream out;
        out << "ptr=" << static_cast<const void *>(layer)
            << ",name=" << motion::detail::narrow(layer->GetName())
            << ",primary=" << (layer->IsPrimary() ? 1 : 0)
            << ",visible=" << (layer->GetVisible() ? 1 : 0)
            << ",parentVisible=" << (layer->GetParentVisible() ? 1 : 0)
            << ",opacity=" << layer->GetOpacity()
            << ",order=" << layer->GetOrderIndex()
            << ",overall=" << layer->GetOverallOrderIndex()
            << ",rect=[" << layer->GetLeft() << "," << layer->GetTop()
            << "," << layer->GetWidth() << "x" << layer->GetHeight() << "]"
            << ",image=";
        if(drawableType && imageWidth >= 0 && imageHeight >= 0) {
            out << imageWidth << "x" << imageHeight;
        } else {
            out << "?x?";
        }
        out << ",hasImage=" << (hasImage ? 1 : 0)
            << ",type=" << static_cast<int>(type)
            << ",children=" << layer->GetCount();
        return out.str();
    }

    std::string describeLayerAncestryForQueryDebug(tTJSNI_BaseLayer *layer) {
        std::ostringstream out;
        int depth = 0;
        while(layer && depth < 12) {
            if(depth != 0) {
                out << " <- ";
            }
            out << "[" << depth << ":"
                << describeLayerForQueryDebug(layer) << "]";
            layer = layer->GetParent();
            ++depth;
        }
        if(layer) {
            out << " <- ...";
        }
        return out.str();
    }

    iTJSDispatch2 *selectVariantDispatchTarget(tTJSVariant *value) {
        if(!value || value->Type() != tvtObject) {
            return nullptr;
        }
        auto closure = value->AsObjectClosureNoAddRef();
        if(closure.ObjThis) {
            return closure.ObjThis;
        }
        if(closure.Object) {
            return closure.Object;
        }
        return value->AsObjectNoAddRef();
    }

    tTJSNI_BaseLayer *nativeLayerFromDispatch(iTJSDispatch2 *dispatch) {
        if(!dispatch) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(dispatch->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
            return nullptr;
        }
        return layer;
    }

    std::string describeDispatchLayerProbe(iTJSDispatch2 *dispatch) {
        if(!dispatch) {
            return "<null>";
        }
        tTJSNI_BaseLayer *layer = nullptr;
        const tjs_error er = dispatch->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            reinterpret_cast<iTJSNativeInstance **>(&layer));
        std::ostringstream out;
        out << "dispatch=" << static_cast<const void *>(dispatch)
            << ",nis=" << er
            << ",layer=[" << describeLayerForQueryDebug(layer) << "]";
        return out.str();
    }

    bool startsWith(const std::string &value, const std::string &prefix) {
        return value.size() >= prefix.size() &&
            value.compare(0, prefix.size(), prefix) == 0;
    }

    bool containsString(const std::vector<std::string> &values,
                        const std::string &needle) {
        return std::find(values.begin(), values.end(), needle) != values.end();
    }

    std::string joinStrings(const std::vector<std::string> &values,
                            const char *separator = ",") {
        std::ostringstream out;
        for(size_t index = 0; index < values.size(); ++index) {
            if(index != 0) {
                out << separator;
            }
            out << values[index];
        }
        return out.str();
    }

    std::string labelTail(std::string value) {
        const auto slash = value.find_last_of("/\\");
        if(slash != std::string::npos) {
            value = value.substr(slash + 1);
        }
        return value;
    }

    void appendYuzuShortMotionMatches(
        std::vector<std::string> &matches,
        const std::vector<std::string> &labels,
        const std::string &base,
        const std::string &suffix) {
        if(base.empty() || suffix.empty()) {
            return;
        }
        const auto motionPrefix = std::string("motion/") + base + "/" + suffix;
        for(const auto &label : labels) {
            const auto lowered = motion::internal::psbDebugLowercase(label);
            const auto tail = labelTail(lowered);
            if(startsWith(lowered, motionPrefix) || startsWith(tail, suffix)) {
                if(!containsString(matches, label)) {
                    matches.push_back(label);
                }
            }
        }
    }

    std::vector<std::string> resolveYuzuShortMotionLabels(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &requestKey,
        const std::string &charaKey) {
        std::vector<std::string> labels;
        const auto request = motion::internal::psbDebugLowercase(requestKey);
        if(request.empty()) {
            return labels;
        }

        std::vector<std::string> bases;
        const auto pathBase = motion::internal::psbDebugLowercase(
            motion::internal::basenameWithoutExtension(snapshot.path));
        if(!pathBase.empty()) {
            bases.push_back(pathBase);
        }
        const auto charaBase = motion::internal::psbDebugLowercase(
            motion::internal::basenameWithoutExtension(charaKey));
        if(!charaBase.empty() && !containsString(bases, charaBase)) {
            bases.push_back(charaBase);
        }

        std::vector<std::string> allLabels = snapshot.mainTimelineLabels;
        for(const auto &label : snapshot.diffTimelineLabels) {
            if(!containsString(allLabels, label)) {
                allLabels.push_back(label);
            }
        }
        for(const auto &[label, _] : snapshot.clipsByLabel) {
            if(!containsString(allLabels, label)) {
                allLabels.push_back(label);
            }
        }

        for(const auto &base : bases) {
            if(!startsWith(request, base) || request.size() <= base.size()) {
                continue;
            }
            const auto suffix = request.substr(base.size());
            appendYuzuShortMotionMatches(labels, allLabels, base, suffix);
            if(!labels.empty()) {
                break;
            }
        }
        return labels;
    }

    float variableEaseWeightLike_0x671228(double ease) {
        if(ease > 0.0) {
            return static_cast<float>(ease + 1.0);
        }
        if(ease < 0.0) {
            return static_cast<float>(1.0 / (1.0 - ease));
        }
        return 1.0f;
    }

    bool hitTestMotionNodeShape(const motion::detail::MotionNode &node,
                                double x, double y) {
        motion::detail::HitData hit{};
        hit.type = node.shapeGeomType;
        for(size_t i = 0; i < std::size(node.shapeVertices) &&
                          i < hit.values.size();
            ++i) {
            hit.values[i] = node.shapeVertices[i];
        }
        return motion::detail::hitTestHitData(hit, x, y);
    }
}

namespace motion {

    // --- Viewport/display ---
    void Player::setFlip(bool v) { _runtime->flip = v; }

    bool Player::shouldMirrorEvalLabelLike_0x67C6B0(const std::string &label) {
        if(!_mirrorEvalEnabled || label.empty() || !_runtime->activeMotion) {
            return false;
        }

        if(_mirrorPositiveCache.find(label) != _mirrorPositiveCache.end()) {
            return true;
        }
        if(_mirrorNegativeCache.find(label) != _mirrorNegativeCache.end()) {
            return false;
        }

        const auto &matchList = _runtime->activeMotion->mirrorVariableMatchList;
        const bool matched =
            std::find(matchList.begin(), matchList.end(), label) !=
            matchList.end();
        if(matched) {
            _mirrorPositiveCache.insert(label);
        } else {
            _mirrorNegativeCache.insert(label);
        }
        return matched;
    }

    double &Player::ensureEvalResultSlotLike_0x686944(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            return it->second->value;
        }

        _evalResultList.push_back(EvalResultEntry{label, 0.0});
        auto it = _evalResultList.end();
        --it;
        _evalResultListIndex[label] = it;
        return it->value;
    }

    void Player::removeEvalResultSlotLike_Reset(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            _evalResultList.erase(it->second);
            _evalResultListIndex.erase(it);
        }
    }

    void Player::writeEvalResultValueLike_0x6C4668(const std::string &label,
                                                   double value) {
        if(label.empty()) {
            return;
        }
        ensureEvalResultSlotLike_0x686944(label) = value;
        _evalResultValues[label] = value;
    }

    void Player::setOpacity(double v) { _runtime->opacity = v; }

    void Player::setVisible(bool v) {
        _runtime->visible = v;
        invokeNativeBackend(v ? "show" : "hide");
    }

    void Player::setSlant(double v) { _runtime->slant = v; }

    void Player::setZoom(double v) { _runtime->zoom = v; }

    tTJSVariant Player::getLayerNames() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(activeLayerNames()));
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::calcViewParam() {
        _runtime->lastViewParam = detail::makeDictionary({
            { "flip", _runtime->flip },
            { "opacity", _runtime->opacity },
            { "visible", _runtime->visible },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "zFactor", _zFactor },
            { "colorWeight", getColorWeight() },
        });
    }

    Player *Player::findLayerNodeForQuery(const std::string &key,
                                          int &nodeIndex) {
        nodeIndex = -1;
        if(key.empty()) {
            return nullptr;
        }

        // A composed motion is normally a tree, but malformed assets or a
        // reused child object must not turn a layer query into unbounded C++
        // recursion.  Iterative DFS also preserves the native first-match
        // behavior without consuming the call stack on deeply nested UIs.
        std::vector<Player *> pending{this};
        std::unordered_set<Player *> visited;
        while(!pending.empty()) {
            Player *current = pending.back();
            pending.pop_back();
            if(!current || !visited.insert(current).second) {
                continue;
            }

            current->ensureMotionLoaded();
            current->ensureNodeTreeBuilt();
            if(!current->_runtime) {
                continue;
            }
            if((current->_layersDirty || current->_emoteDirty) &&
               !current->_runtime->nodes.empty()) {
                current->updateLayers();
            }

            if(const auto found = current->_runtime->nodeLabelMap.find(key);
               found != current->_runtime->nodeLabelMap.end() &&
               found->second >= 0 &&
               found->second <
                   static_cast<int>(current->_runtime->nodes.size())) {
                nodeIndex = found->second;
                return current;
            }

            // Push in reverse so the traversal order matches the previous
            // recursive, node-order DFS implementation.
            for(auto it = current->_runtime->nodes.rbegin();
                it != current->_runtime->nodes.rend(); ++it) {
                if(it->nodeType == 3) {
                    if(auto *child = it->getChildPlayer()) {
                        if(visited.find(child) == visited.end()) {
                            pending.push_back(child);
                        }
                    }
                } else if(it->nodeType == 4) {
                    for(int index = it->getParticleCount() - 1;
                        index >= 0; --index) {
                        if(auto *child = it->getParticleChild(index)) {
                            if(visited.find(child) == visited.end()) {
                                pending.push_back(child);
                            }
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime) {
            return {};
        }

        // Motion buttons are queried between draws. Keep their child players
        // synchronized with the parent node before returning the object used
        // by script-side contains(x, y).
        if((_layersDirty || _emoteDirty) && !_runtime->nodes.empty()) {
            updateLayers();
        }

        const auto key = detail::narrow(name);
        int nodeIndex = -1;
        auto *owner = findLayerNodeForQuery(key, nodeIndex);
        if(!owner || !owner->_runtime) {
            return {};
        }

        const auto &node =
            owner->_runtime->nodes[static_cast<size_t>(nodeIndex)];
        if(node.nodeType == 3 && node.childPlayerVar.Type() == tvtObject) {
            if(auto *child = node.getChildPlayer()) {
                if(LOGGER && motionHitDebugEnabled()) {
                    LOGGER->info(
                        "motion hit child query: parentMotion={} label={} parentPos=({:.2f},{:.2f}) childMotion={} childLabel={}",
                        owner->_runtime->activeMotion
                            ? owner->_runtime->activeMotion->path
                            : std::string("<none>"),
                        key, node.accumulated.posX, node.accumulated.posY,
                        child->_runtime && child->_runtime->activeMotion
                            ? child->_runtime->activeMotion->path
                            : std::string("<none>"),
                        detail::narrow(child->_motionKey));
                }
            }
            return node.childPlayerVar;
        }

        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime) {
            return {};
        }

        if((_layersDirty || _emoteDirty) && !_runtime->nodes.empty()) {
            updateLayers();
        }

        const auto key = detail::narrow(name);
        int nodeIndex = -1;
        auto *owner = findLayerNodeForQuery(key, nodeIndex);
        if(!owner || !owner->_runtime) {
            return {};
        }

        const auto &node =
            owner->_runtime->nodes[static_cast<size_t>(nodeIndex)];
        tTJSVariant motion;
        if(node.nodeType == 3 && node.childPlayerVar.Type() == tvtObject) {
            motion = node.childPlayerVar;
        }

        tTJSVariant shape;
        switch(node.shapeGeomType) {
            case ShapeTypePoint: {
                auto *point = new Point();
                point->x = node.shapeVertices[0];
                point->y = node.shapeVertices[1];
                shape = makeNativeShapeVariant(point);
                break;
            }
            case ShapeTypeCircle: {
                auto *circle = new Circle();
                circle->x = node.shapeVertices[0];
                circle->y = node.shapeVertices[1];
                circle->r = node.shapeVertices[2];
                shape = makeNativeShapeVariant(circle);
                break;
            }
            case ShapeTypeRect: {
                auto *rect = new Rect();
                rect->l = node.shapeVertices[3];
                rect->t = node.shapeVertices[4];
                rect->w = node.shapeVertices[5] - node.shapeVertices[3];
                rect->h = node.shapeVertices[6] - node.shapeVertices[4];
                shape = makeNativeShapeVariant(rect);
                break;
            }
            case ShapeTypeQuad: {
                auto *quad = new Quad();
                for(size_t index = 0; index < 8; ++index) {
                    quad->verts[index] = node.shapeVertices[index + 7];
                }
                shape = makeNativeShapeVariant(quad);
                break;
            }
            default:
                break;
        }

        if(LOGGER && motionHitDebugEnabled()) {
            const auto &root = owner->_runtime->nodes.front();
            LOGGER->info(
                "motion layer getter: motion={} chara={} motionKey={} layer={} type={} geom={} rootPos=({:.2f},{:.2f}) nodePos=({:.2f},{:.2f}) nodeScale=({:.4f},{:.4f}) rect=({:.2f},{:.2f},{:.2f},{:.2f})",
                owner->_runtime->activeMotion
                    ? owner->_runtime->activeMotion->path
                    : std::string("<none>"),
                detail::narrow(owner->_chara),
                detail::narrow(owner->_motionKey), key,
                node.nodeType, node.shapeGeomType,
                root.accumulated.posX, root.accumulated.posY,
                node.accumulated.posX, node.accumulated.posY,
                node.accumulated.scaleX, node.accumulated.scaleY,
                node.shapeVertices[3], node.shapeVertices[4],
                node.shapeVertices[5] - node.shapeVertices[3],
                node.shapeVertices[6] - node.shapeVertices[4]);
        }

        const auto layerId = owner->requireLayerId(name);
        return detail::makeDictionary({
            { "name", name },
            { "label", name },
            { "id", layerId },
            { "type", node.nodeType },
            { "visible", node.accumulated.visible },
            { "branchVisible", node.accumulated.active },
            { "layerVisible", node.drawFlag },
            { "x", node.accumulated.posX },
            { "y", node.accumulated.posY },
            { "left", node.accumulated.posX },
            { "top", node.accumulated.posY },
            { "flipX", node.accumulated.flipX },
            { "flipY", node.accumulated.flipY },
            { "zoomX", node.accumulated.scaleX },
            { "zoomY", node.accumulated.scaleY },
            { "angleDeg", node.accumulated.angle },
            { "angleRad", node.accumulated.angle * 3.14159265358979323846 /
                              180.0 },
            { "slantX", node.accumulated.slantX },
            { "slantY", node.accumulated.slantY },
            { "opacity", node.accumulated.opacity },
            { "shape", shape },
            { "motion", motion },
        });
    }

    tTJSVariant Player::getLayerGetterList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        for(const auto &layerName : activeLayerNames()) {
            const auto getter = getLayerGetter(detail::widen(layerName));
            if(getter.Type() != tvtVoid) {
                items.push_back(getter);
            }
        }
        return detail::makeArray(items);
    }

    void Player::skipToSync() {
        invokeNativeBackend("skip");
        _layersDirty = true;
        for(auto &[label, state] : _runtime->timelines) {
            const auto controlIt = _runtime->activeMotion
                ? _runtime->activeMotion->timelineControlByLabel.find(label)
                : decltype(_runtime->activeMotion->timelineControlByLabel.find(
                      label)){};
            const bool controlLoop =
                _runtime->activeMotion &&
                controlIt !=
                    _runtime->activeMotion->timelineControlByLabel.end() &&
                controlIt->second.loopBegin >= 0.0 &&
                controlIt->second.loopEnd > controlIt->second.loopBegin;

            // Native pass/step consumes one-shot controls, but it must leave
            // persistent controller loops (for example `ポーズA_通常待機`)
            // running.  These loops are described by TimelineControlBinding,
            // not MotionClip::loop, so consulting state.loop alone stops all
            // authored head/body/hair idle motion at the first scenario
            // `em:step()` call.
            if(controlLoop || state.loop) {
                continue;
            }
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            state.playing = false;
        }
        if(const auto it = std::remove_if(_runtime->playingTimelineLabels.begin(),
                                          _runtime->playingTimelineLabels.end(),
                                          [this](const std::string &label) {
                                              const auto found =
                                                  _runtime->timelines.find(label);
                                              return found ==
                                                      _runtime->timelines.end() ||
                                                  !found->second.playing;
                                          });
           it != _runtime->playingTimelineLabels.end()) {
            _runtime->playingTimelineLabels.erase(
                it, _runtime->playingTimelineLabels.end());
        }
        _syncWaiting = false;
        _syncActive = false;
        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(!_allplaying) {
            disableAutoProgress();
        }

        // MEmotePlayer::Skip/Step do not stop at timeline control.  The
        // native routines also call epSkip on every eye, eyebrow, mouth,
        // selector, transition, and generic variable controller.  In
        // particular, EPSelectorControl::epSkip resolves the final queued
        // selector and then skips each option's transition controller.  If we
        // leave those queues live, a model prepared offscreen by `em:step()`
        // becomes visible in its neutral selector pose and visibly morphs to
        // the requested pose (for example, a raised hand flashes before the
        // authored arm_type=0 pose settles).
        const auto skipAnimator =
            [this](const std::string &label, VariableAnimatorState &state) {
                float value = state.targetValue;
                if(!state.queue.empty()) {
                    value = state.queue.back().value;
                }
                state.queue.clear();
                state.active = false;
                state.currentValue = value;
                state.startValue = value;
                state.targetValue = value;
                state.progress = 1.0f;
                state.duration = 0.0f;
                _variableValues[label] = value;
                ensureEvalResultSlotLike_0x686944(label) = value;
                _evalResultValues[label] = value;
            };
        const auto skipBucket = [&skipAnimator](auto &bucket) {
            for(auto &[label, state] : bucket) {
                skipAnimator(label, state);
            }
        };
        skipBucket(_type4ControllerAnimators);
        skipBucket(_type5ControllerAnimators);
        skipBucket(_type6ControllerAnimators);
        skipBucket(_type8ControllerAnimators);
        skipBucket(_type7ControllerAnimators);
        skipBucket(_variableAnimators);
        _emoteDirty = true;
    }

    void Player::setStereovisionCameraPosition(double x, double y, double z) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant vx = x;
        tTJSVariant vy = y;
        tTJSVariant vz = z;
        static tjs_uint addHint = 0;
        tTJSVariant *argsX[] = { &vx };
        tTJSVariant *argsY[] = { &vy };
        tTJSVariant *argsZ[] = { &vz };
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsX, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsY, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsZ, array);
        _cameraPosition = tTJSVariant(array, array);
        array->Release();
    }

    // --- Timeline/variable queries ---
    void Player::setVariableResolvedWeightLike_0x671228(
        const std::string &key, double value, double transition,
        double easeWeight) {
        _layersDirty = true;
        const auto *activeMotion = _runtime->activeMotion.get();
        const auto bindingIt = activeMotion
            ? activeMotion->controllerBindings.find(key)
            : decltype(activeMotion->controllerBindings.find(key)){};
        const bool hasBinding =
            activeMotion && bindingIt != activeMotion->controllerBindings.end();

        if(emoteTraceEnabled() && LOGGER) {
            LOGGER->info(
                "[EMOTE_TRACE] setVariable motion={} key={} binding={} value={} transition={} easeWeight={} queuing={}",
                activeMotion ? activeMotion->path : std::string{}, key,
                hasBinding ? bindingIt->second.type : -1, value, transition,
                easeWeight, _emoteAnimatorFlag ? 1 : 0);
        }

        const auto queueControllerStateLikeBinary =
            [&](const std::string &targetKey,
                VariableAnimatorState &state,
                double currentValueInput,
                double requestedValue,
                double requestedTransition,
                double requestedEaseWeight) {
                const auto currentValue =
                    static_cast<float>(currentValueInput);
                const auto targetValue =
                    static_cast<float>(requestedValue);
                if(requestedTransition <= 0.0) {
                    state.queue.clear();
                    state.active = false;
                    state.currentValue = targetValue;
                    state.startValue = targetValue;
                    state.targetValue = targetValue;
                    state.progress = 1.0f;
                    state.duration = 0.0f;
                    state.weight =
                        static_cast<float>(requestedEaseWeight);
                    _variableValues[targetKey] = requestedValue;
                    ensureEvalResultSlotLike_0x686944(targetKey) =
                        requestedValue;
                    _evalResultValues[targetKey] = requestedValue;
                    return;
                }

                if(!_emoteAnimatorFlag) {
                    state.queue.clear();
                    state.active = false;
                    state.currentValue = currentValue;
                    state.startValue = currentValue;
                    state.targetValue = currentValue;
                    state.progress = 1.0f;
                    state.duration = 0.0f;
                }

                state.queue.push_back(VariableKeyframe{
                    targetValue,
                    static_cast<float>(requestedTransition),
                    static_cast<float>(requestedEaseWeight),
                });
                _variableValues[targetKey] = state.currentValue;
                ensureEvalResultSlotLike_0x686944(targetKey) =
                    state.currentValue;
                _evalResultValues[targetKey] = state.currentValue;
            };

        if(hasBinding) {
            const auto queueControllerLikeBinary =
                [&](VariableAnimatorState &state,
                    double requestedValue,
                    double requestedTransition,
                    double requestedEaseWeight) {
                    queueControllerStateLikeBinary(
                        key, state,
                        _variableValues.count(key) ? _variableValues[key]
                                                   : getVariable(detail::widen(key)),
                        requestedValue, requestedTransition,
                        requestedEaseWeight);
                };

            switch(bindingIt->second.type) {
                case 0:
                case 1:
                case 2:
                    // Aligned to 0x671228 cases 0/1/2:
                    // these labels are routed to physics control groups, not to
                    // the generic eval-result map / animator sink.
                    _emoteDirty = true;
                    return;
                case 3:
                    // Aligned to 0x671228 default route for loopControl-built
                    // entries: no generic eval-result write happens here.
                    _emoteDirty = true;
                    return;
                case 4:
                case 5:
                case 7:
                case 8: {
                    if(bindingIt->second.type == 8 && activeMotion) {
                        const auto selectorIt =
                            activeMotion->selectorControls.find(key);
                        if(selectorIt != activeMotion->selectorControls.end()) {
                            const int selectedIndex =
                                static_cast<int>(value);
                            eraseControllerAnimatorStateLike_0x671228(key);
                            _variableValues[key] =
                                static_cast<double>(selectedIndex);
                            ensureEvalResultSlotLike_0x686944(key) =
                                static_cast<double>(selectedIndex);
                            _evalResultValues[key] =
                                static_cast<double>(selectedIndex);

                            const double resolvedEaseWeight = easeWeight;
                            int optionIndex = 0;
                            for(const auto &option : selectorIt->second.options) {
                                if(option.label.empty()) {
                                    ++optionIndex;
                                    continue;
                                }
                                const double targetValue =
                                    optionIndex == selectedIndex
                                        ? option.onValue
                                        : option.offValue;
                                const auto currentIt =
                                    _evalResultValues.find(option.label);
                                const double currentValue =
                                    currentIt != _evalResultValues.end()
                                        ? currentIt->second
                                        : (_variableValues.count(option.label)
                                               ? _variableValues[option.label]
                                               : getVariable(
                                                     detail::widen(option.label)));
                                const double range =
                                    std::abs(option.onValue - option.offValue);
                                const double scaledTransition =
                                    transition > 0.0 && range > 0.0000001
                                        ? std::abs(targetValue - currentValue) /
                                              range * transition
                                        : 0.0;
                                auto &optionState =
                                    _type8ControllerAnimators[option.label];
                                queueControllerStateLikeBinary(
                                    option.label, optionState, currentValue,
                                    targetValue, scaledTransition,
                                    resolvedEaseWeight);
                                ++optionIndex;
                            }
                            _emoteDirty = true;
                            return;
                        }
                    }
                    auto *bucket =
                        controllerAnimatorBucketLike_0x671228(
                            bindingIt->second.type);
                    if(!bucket) {
                        _emoteDirty = true;
                        return;
                    }
                    auto &state = (*bucket)[key];
                    ensureEvalResultSlotLike_0x686944(key);
                    queueControllerLikeBinary(state, value, transition,
                                              easeWeight);
                    _emoteDirty = true;
                    return;
                }
                case 6: {
                    if(bindingIt->second.role == "label") {
                        eraseControllerAnimatorStateLike_0x671228(key);
                        const double directValue =
                            static_cast<double>(static_cast<int>(value));
                        _variableValues[key] = directValue;
                        ensureEvalResultSlotLike_0x686944(key) = directValue;
                        _evalResultValues[key] = directValue;
                        _emoteDirty = true;
                        return;
                    }
                    auto &state = _type6ControllerAnimators[key];
                    ensureEvalResultSlotLike_0x686944(key);
                    queueControllerLikeBinary(state, value, transition,
                                              easeWeight);
                    _emoteDirty = true;
                    return;
                }
                default:
                    _emoteDirty = true;
                    return;
            }
        }

        // Labels without a fixed controller binding use the final generic
        // animator bucket stepped by Player_progress.  Timeline controls such
        // as body_UD/body_LR are intentionally sparse and depend on this
        // transition to turn authored keyframes into continuous idle motion.
        const auto evalIt = _evalResultValues.find(key);
        const double currentValue = evalIt != _evalResultValues.end()
            ? evalIt->second
            : (_variableValues.count(key)
                   ? _variableValues[key]
                   : getVariable(detail::widen(key)));
        auto [stateIt, inserted] = _variableAnimators.try_emplace(key);
        if(inserted) {
            stateIt->second.currentValue =
                static_cast<float>(currentValue);
            stateIt->second.startValue =
                static_cast<float>(currentValue);
            stateIt->second.targetValue =
                static_cast<float>(currentValue);
        }
        queueControllerStateLikeBinary(
            key, stateIt->second, currentValue, value, transition,
            easeWeight);
        _emoteDirty = true;
    }

    void Player::setVariable(ttstr label, double value, double transition,
                             double ease) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return;
        }

        setVariableResolvedWeightLike_0x671228(
            key, value, transition, variableEaseWeightLike_0x671228(ease));
        invokeNativeBackend(
            "setvariable", { MotionBackendValue::String(key),
                              MotionBackendValue::Number(value),
                              MotionBackendValue::Number(transition),
                              MotionBackendValue::Number(ease) });
    }

    double Player::getVariable(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return 0.0;
        }

        if(const auto it = _variableValues.find(key); it != _variableValues.end()) {
            return it->second;
        }

        if(!_runtime->activeMotion) {
            return 0.0;
        }

        // E-mote's variableFrames/variableRanges tables describe the authored
        // deformation domain, not an initial pose.  A fresh controller starts
        // at the neutral scalar 0.  Returning the first range entry here (for
        // example body_UD=-30) lets fixed-controller initialization persist an
        // extreme pose before updateLayers has a chance to seed its neutral
        // values.  Waiting-loop output is then added to that bad base forever,
        // which tilts the model and clamps hair/tail anchor motion.
        if(_runtime->isEmoteMode) {
            return 0.0;
        }

        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it != _runtime->activeMotion->variableFrames.end() &&
           !it->second.empty()) {
            return it->second.front().value;
        }

        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return it->second.first;
        }

        return 0.0;
    }

    tjs_int Player::countVariables() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->variableLabels.size())
            : 0;
    }

    ttstr Player::getVariableLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >= _runtime->activeMotion->variableLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->variableLabels[idx]);
    }

    tjs_int Player::countVariableFrameAt(tjs_int idx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0;
        }
        const auto frames = getVariableFrameList(label);
        return getObjectCount(frames);
    }

    ttstr Player::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return {};
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return {};
        }
        return detail::widen(it->second[frameIdx].label);
    }

    double Player::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0.0;
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return 0.0;
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return 0.0;
        }
        return it->second[frameIdx].value;
    }

    bool Player::getTimelinePlaying(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return detail::makeArray(
                { tTJSVariant(it->second.first), tTJSVariant(it->second.second) });
        }
        return {};
    }

    tTJSVariant Player::getVariableFrameList(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it == _runtime->activeMotion->variableFrames.end()) {
            return detail::makeArray({});
        } else {
            std::vector<tTJSVariant> frames;
            for(const auto &frame : it->second) {
                frames.push_back(detail::makeDictionary({
                    { "label", detail::widen(frame.label) },
                    { "frame", frame.value },
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
    }

    bool Player::hitTestLayer(ttstr name, double x, double y) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }

        if((_layersDirty || _emoteDirty) && !_runtime->nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        const auto key = detail::narrow(name);
        if(key.empty()) {
            return false;
        }

        int nodeIndex = -1;
        if(auto *owner = findLayerNodeForQuery(key, nodeIndex);
           owner && owner->_runtime) {
            const auto *node = &owner->_runtime->nodes[
                static_cast<size_t>(nodeIndex)];
            return hitTestMotionNodeShape(*node, x, y);
        }
        return false;
    }

    bool Player::contains(double x, double y) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }

        if(_motionParentPlayer && _motionParentPlayer->_runtime) {
            _motionParentPlayer->ensureNodeTreeBuilt();
            if((_motionParentPlayer->_layersDirty ||
                _motionParentPlayer->_emoteDirty) &&
               !_motionParentPlayer->_runtime->nodes.empty()) {
                _motionParentPlayer->updateLayers();
            }
        }

        if((_layersDirty || _emoteDirty) && !_runtime->nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        bool hasExplicitShape = false;
        bool shapeHit = false;
        for(const auto &node : _runtime->nodes) {
            if(node.nodeType != 1 || !node.accumulated.active ||
               node.activeSlot().done || node.shapeGeomType == ShapeTypePoint) {
                continue;
            }
            hasExplicitShape = true;
            if(hitTestMotionNodeShape(node, x, y)) {
                shapeHit = true;
                break;
            }
        }

        const bool boundsHit =
            _boundsMinX <= x && x < _boundsMaxX &&
            _boundsMinY <= y && y < _boundsMaxY;
        const bool hit = hasExplicitShape ? shapeHit : boundsHit;
        if(LOGGER && motionHitDebugEnabled()) {
            LOGGER->info(
                "motion contains: motion={} label={} point=({:.2f},{:.2f}) shapes={} shapeHit={} bounds=({:.2f},{:.2f},{:.2f},{:.2f}) boundsHit={} result={}",
                _runtime->activeMotion->path, detail::narrow(_motionKey), x, y,
                hasExplicitShape ? 1 : 0, shapeHit ? 1 : 0,
                _boundsMinX, _boundsMinY, _boundsMaxX, _boundsMaxY,
                boundsHit ? 1 : 0, hit ? 1 : 0);
        }
        return hit;
    }

    tjs_error Player::containsCompatMethod(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(!result) {
            return TJS_E_INVALIDPARAM;
        }

        if(numparams >= 3 && param[0] && param[1] && param[2]) {
            *result = tTJSVariant(
                self->hitTestLayer(ttstr(*param[0]), param[1]->AsReal(),
                                   param[2]->AsReal()));
            return TJS_S_OK;
        }
        if(numparams >= 2 && param[0] && param[1]) {
            *result = tTJSVariant(
                self->contains(param[0]->AsReal(), param[1]->AsReal()));
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    tjs_int Player::countMainTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->mainTimelineLabels.size())
            : 0;
    }

    ttstr Player::getMainTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->mainTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->mainTimelineLabels[idx]);
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->mainTimelineLabels));
    }

    tjs_int Player::countDiffTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->diffTimelineLabels.size())
            : 0;
    }

    ttstr Player::getDiffTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->diffTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->diffTimelineLabels[idx]);
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->loopTimelines.find(key);
           it != _runtime->activeMotion->loopTimelines.end()) {
            // Some E-mote PSBs expose a generic timeline dictionary whose
            // optional `loop` field defaults to false, while the authoritative
            // timelineControl entry carries a valid loop interval.  A false
            // value here therefore cannot rule out a controller-defined loop.
            if(it->second) {
                return true;
            }
        }
        if(const auto it =
               _runtime->activeMotion->timelineControlByLabel.find(key);
           it != _runtime->activeMotion->timelineControlByLabel.end()) {
            return it->second.loopBegin >= 0.0 &&
                   it->second.loopEnd > it->second.loopBegin;
        }
        return false;
    }

    tjs_int Player::countPlayingTimelines() {
        ensureMotionLoaded();
        return static_cast<tjs_int>(_runtime->playingTimelineLabels.size());
    }

    ttstr Player::getPlayingTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _runtime->playingTimelineLabels.size()) {
            return detail::widen(_runtime->playingTimelineLabels[idx]);
        }
        return {};
    }

    tjs_int Player::getPlayingTimelineFlagsAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _runtime->playingTimelineLabels.size()) {
            const auto &label = _runtime->playingTimelineLabels[idx];
            if(const auto it = _runtime->timelines.find(label);
               it != _runtime->timelines.end()) {
                return it->second.flags;
            }
        }
        return 0;
    }

    tjs_int Player::getTimelineTotalFrameCount(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return static_cast<tjs_int>(it->second.totalFrames);
        }
        if(_runtime->activeMotion) {
            if(const auto it = _runtime->activeMotion->timelineTotalFrames.find(key);
               it != _runtime->activeMotion->timelineTotalFrames.end()) {
                return static_cast<tjs_int>(it->second);
            }
        }
        return 0;
    }

    void Player::playTimeline(ttstr label, tjs_int flags) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return;
        }
        if(_runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }
        _layersDirty = true;

        const auto key = detail::narrow(label);
        if(emoteTraceEnabled() && LOGGER) {
            LOGGER->info(
                "[EMOTE_TRACE] playTimeline motion={} label={} flags={} queuing={}",
                _runtime->activeMotion ? _runtime->activeMotion->path
                                       : std::string{},
                key, flags, _emoteAnimatorFlag ? 1 : 0);
        }
        auto it = _runtime->timelines.find(key);
        if(it == _runtime->timelines.end()) {
            return;
        }

        if(emoteTimelineTraceEnabled() && LOGGER) {
            const auto controlIt =
                _runtime->activeMotion->timelineControlByLabel.find(key);
            const auto *control =
                controlIt != _runtime->activeMotion->timelineControlByLabel.end()
                ? &controlIt->second
                : nullptr;
            std::ostringstream trackDescription;
            if(control) {
                for(size_t trackIndex = 0;
                    trackIndex < control->tracks.size(); ++trackIndex) {
                    if(trackIndex != 0) {
                        trackDescription << ';';
                    }
                    const auto &track = control->tracks[trackIndex];
                    trackDescription << track.label << "(instant="
                                     << (track.instantVariable ? 1 : 0)
                                     << ",frames=" << track.frames.size();
                    if(!track.frames.empty()) {
                        trackDescription << ",first="
                                         << track.frames.front().time
                                         << ",last="
                                         << track.frames.back().time;
                    }
                    trackDescription << ')';
                }
            }
            LOGGER->info(
                "[EMOTE_TIMELINE] play motion={} label={} flags={} "
                "stateLoop={} loopTime={:.3f} total={:.3f} "
                "control={} loopBegin={:.3f} loopEnd={:.3f} "
                "lastTime={:.3f} tracks={} trackDetail=[{}] before=[{}]",
                _runtime->activeMotion->path, key, flags,
                it->second.loop ? 1 : 0, it->second.loopTime,
                it->second.totalFrames, control ? 1 : 0,
                control ? control->loopBegin : -1.0,
                control ? control->loopEnd : -1.0,
                control ? control->lastTime : -1.0,
                control ? control->tracks.size() : 0,
                trackDescription.str(),
                joinTimelineLabels(_runtime->playingTimelineLabels));
        }

        // TimelinePlayFlagParallel is bit 0. Native E-mote only replaces the
        // current list when that bit is absent; flag 3 (parallel + diff) must
        // keep the persistent model timeline and any other parallel controls.
        // Clearing on a SET bit made Nekopara's waiting_loop replace
        // `全体構造`, so hair/tail motion ran once against an incomplete base
        // controller state and then settled permanently.
        if((flags & 1) == 0) {
            stopTimeline(TJS_W(""));
        }

        if(!label.IsEmpty()) {
            if(std::find(_runtime->playingTimelineLabels.begin(),
                         _runtime->playingTimelineLabels.end(),
                         key) == _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.push_back(key);
            }
            _runtime->lastExplicitTimelineLabel = key;
        }

        if(const auto *clip = detail::findMotionClip(
               *_runtime->activeMotion, detail::narrow(_chara), key,
               false)) {
            it->second.totalFrames = clip->totalFrames;
            it->second.loop = clip->loop;
            it->second.loopTime = clip->loopTime;
        }
        it->second.flags = flags;
        it->second.playing = true;
        it->second.currentTime = 0.0;
        it->second.blendRatio = 1.0;
        it->second.blendAnimator = {};
        it->second.blendAnimator.currentValue = 1.0f;
        it->second.blendAnimator.startValue = 1.0f;
        it->second.blendAnimator.targetValue = 1.0f;
        it->second.blendAutoStop = false;
        it->second.controlInitialized = false;
        it->second.controlLastAppliedTime = 0.0;
        it->second.controlFrameCursor.clear();
        it->second.controlTrackValues.clear();
        it->second.controlTrackAnimators.clear();
        if(const auto controlIt =
               _runtime->activeMotion->timelineControlByLabel.find(key);
           controlIt != _runtime->activeMotion->timelineControlByLabel.end()) {
            seekTimelineControlStateLike_0x66EE30(
                it->second, controlIt->second, 0.0);
        }
        _allplaying = !_runtime->playingTimelineLabels.empty();
        invokeNativeBackend(
            "playtimeline", { MotionBackendValue::String(key),
                               MotionBackendValue::Number(flags) });
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    void Player::stopTimeline(ttstr label) {
        _layersDirty = true;
        const auto key = detail::narrow(label);
        if(label.IsEmpty()) {
            for(auto &[_, state] : _runtime->timelines) {
                state.playing = false;
                state.blendRatio = 1.0;
                state.blendAnimator = {};
                state.blendAutoStop = false;
                state.controlInitialized = false;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            _runtime->playingTimelineLabels.clear();
        } else {
            if(const auto it = _runtime->timelines.find(key);
               it != _runtime->timelines.end()) {
                it->second.playing = false;
                it->second.blendRatio = 1.0;
                it->second.blendAnimator = {};
                it->second.blendAutoStop = false;
                it->second.controlInitialized = false;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();
            }
            if(const auto it = std::remove(_runtime->playingTimelineLabels.begin(),
                                           _runtime->playingTimelineLabels.end(),
                                           key);
               it != _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.erase(
                    it, _runtime->playingTimelineLabels.end());
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
        invokeNativeBackend(
            "stoptimeline", { MotionBackendValue::String(key) });
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    void Player::setTimelineBlendRatio(ttstr label, double ratio) {
        ensureMotionLoaded();
        _layersDirty = true;
        if(_runtime->timelines.empty() && _runtime->activeMotion) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto &state = _runtime->timelines[key];
        state.label = key;
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_TIMELINE_TRACE")) {
            LOGGER->info(
                "[EMOTE_TIMELINE] blend motion={} label={} {:.4f}->{:.4f}",
                _runtime && _runtime->activeMotion
                    ? _runtime->activeMotion->path
                    : std::string{},
                key, state.blendRatio, ratio);
        }
        state.blendRatio = ratio;
        // The internal difference-timeline route steps this animator on every
        // progress call, even when it has no queued transition.  Resetting it
        // with the zero-valued default object made the next frame overwrite
        // the just-assigned ratio (normally 1) with 0, permanently muting
        // waiting_loop while its track clock kept running.
        state.blendAnimator = {};
        state.blendAnimator.currentValue = static_cast<float>(ratio);
        state.blendAnimator.startValue = static_cast<float>(ratio);
        state.blendAnimator.targetValue = static_cast<float>(ratio);
        state.blendAutoStop = false;
        invokeNativeBackend(
            "settimelineblendratio",
            { MotionBackendValue::String(key),
              MotionBackendValue::Number(ratio),
              MotionBackendValue::Number(0.0),
              MotionBackendValue::Number(0.0),
              MotionBackendValue::Boolean(false) });
    }

    double Player::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.blendRatio;
        }
        return 1.0;
    }

    void Player::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
        const auto key = detail::narrow(label);
        const bool alreadyPlaying =
            std::find(_runtime->playingTimelineLabels.begin(),
                      _runtime->playingTimelineLabels.end(),
                      key) != _runtime->playingTimelineLabels.end();
        if(!alreadyPlaying) {
            playTimeline(label, 3);
            setTimelineBlendLike_0x6735AC(key, false, 0.0, 0.0, 0.0);
        }
        setTimelineBlendLike_0x6735AC(key, false, 1.0, duration, 0.0);
        invokeNativeBackend(
            "fadeintimeline", { MotionBackendValue::String(key),
                                 MotionBackendValue::Number(duration),
                                 MotionBackendValue::Number(0.0) });
    }

    void Player::fadeOutTimeline(ttstr label, double duration, tjs_int) {
        setTimelineBlendLike_0x6735AC(detail::narrow(label), true, 0.0,
                                      duration, 0.0);
        invokeNativeBackend(
            "fadeouttimeline",
            { MotionBackendValue::String(detail::narrow(label)),
              MotionBackendValue::Number(duration),
              MotionBackendValue::Number(0.0) });
    }

    tTJSVariant Player::getPlayingTimelineInfoList() {
        ensureMotionLoaded();
        return detail::makeArray(timelineInfoVariants(*_runtime));
    }

    bool Player::playMotionLike_0x6B2284(ttstr label, tjs_int flags) {
        if(!_runtime->activeMotion && _project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
            }
        }

        ensureMotionLoaded();
        if(_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !_runtime->activeMotion) {
            setMotion(label);
            ensureMotionLoaded();
            if(_runtime->activeMotion && _runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
        }

        if(!_runtime->activeMotion) {
            return false;
        }

        if((flags & PlayFlagForce) != 0) {
            stopTimeline(TJS_W(""));
        }

        const bool chainMode = (flags & PlayFlagChain) != 0;
        const auto playOne = [&](const std::string &timelineLabel,
                                 const bool rememberExplicit) {
            auto &state = _runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            if(const auto *clip = detail::findMotionClip(
                   *_runtime->activeMotion, detail::narrow(_chara),
                   timelineLabel, false)) {
                state.totalFrames = clip->totalFrames;
                state.loop = clip->loop;
                state.loopTime = clip->loopTime;
            }
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            if(!chainMode) {
                state.currentTime = 0.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = 0.0;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            if(std::find(_runtime->playingTimelineLabels.begin(),
                         _runtime->playingTimelineLabels.end(),
                         timelineLabel) == _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.push_back(timelineLabel);
            }
            if(rememberExplicit) {
                _runtime->lastExplicitTimelineLabel = timelineLabel;
            }
            if(state.totalFrames <= 0.0 && _runtime->activeMotion) {
                const auto it =
                    _runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != _runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }

            // Mirror only an exact public native timeline. Kiri's retained root
            // clips (`全体構造` / `タイムライン構造`) are bookkeeping entry
            // points, not native timelines. Mapping one to the first available
            // main timeline used to select `sample_全自動_test`, which is an
            // authored demonstration sequence that intentionally cycles every
            // expression. Blink and physics are independent native controllers
            // and do not require that demo timeline to run.
            if(_nativeBackend) {
                const auto nativeTimelineLabels = [&](const char *countMethod,
                                                      const char *labelMethod) {
                    std::vector<MotionBackendValue> countResults;
                    if(!invokeNativeBackend(countMethod, {}, &countResults) ||
                       countResults.empty() ||
                       countResults.front().type !=
                           MotionBackendValue::Type::Number) {
                        return std::vector<std::string>{};
                    }
                    const int count = std::max(
                        0, static_cast<int>(countResults.front().number));
                    std::vector<std::string> labels;
                    labels.reserve(static_cast<std::size_t>(count));
                    for(int index = 0; index < count; ++index) {
                        std::vector<MotionBackendValue> labelResults;
                        if(invokeNativeBackend(
                               labelMethod,
                               {MotionBackendValue::Number(index)},
                               &labelResults) &&
                           !labelResults.empty() &&
                           labelResults.front().type ==
                               MotionBackendValue::Type::String) {
                            labels.push_back(labelResults.front().string);
                        }
                    }
                    return labels;
                };

                const auto mainLabels = nativeTimelineLabels(
                    "countmaintimelines", "getmaintimelinelabelat");
                const auto diffLabels = nativeTimelineLabels(
                    "countdifftimelines", "getdifftimelinelabelat");
                int nativeFlags = exactMotionBackendTimelineFlags(
                    timelineLabel, mainLabels, diffLabels);
                std::string nativeLabel = timelineLabel;
                bool startedAmbientTimeline = false;
                if(nativeFlags == 0) {
                    nativeLabel = preferredMotionBackendIdleTimeline(diffLabels);
                    if(!nativeLabel.empty()) {
                        nativeFlags = 3;
                        startedAmbientTimeline = true;
                    }
                }
                const bool nativeStarted = nativeFlags != 0 &&
                    invokeNativeBackend(
                        "playtimeline",
                        { MotionBackendValue::String(nativeLabel),
                          MotionBackendValue::Number(nativeFlags) });
                if(nativeStarted && startedAmbientTimeline) {
                    auto &ambientState = _runtime->timelines[nativeLabel];
                    ambientState.label = nativeLabel;
                    ambientState.flags = nativeFlags;
                    ambientState.blendRatio = 1.0;
                    ambientState.playing = true;
                    if(std::find(_runtime->playingTimelineLabels.begin(),
                                 _runtime->playingTimelineLabels.end(),
                                 nativeLabel) ==
                       _runtime->playingTimelineLabels.end()) {
                        _runtime->playingTimelineLabels.push_back(nativeLabel);
                    }
                }
                if(emoteTimelineTraceEnabled() && LOGGER) {
                    LOGGER->info(
                        "[EMOTE_TIMELINE] native play motion={} requested={} native={} mirrored={} ambient={} flags={} main=[{}] diff=[{}]",
                        _runtime->activeMotion
                            ? _runtime->activeMotion->path
                            : std::string{},
                        timelineLabel, nativeLabel,
                        nativeStarted ? 1 : 0,
                        startedAmbientTimeline ? 1 : 0, nativeFlags,
                        joinStrings(mainLabels), joinStrings(diffLabels));
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            // E-mote metadata starts the retained model clip (for example
            // `全体構造`) through Player.play(), even though that clip is not
            // one of the public main/difference control timelines.  Native
            // Player creates a playback entry for that exact clip.  Falling
            // through to the public main-timeline list starts every mutually
            // exclusive expression/action at once; a later pass() then
            // flushes all of them into the controller animators together.
            const bool hasExactTimeline =
                _runtime->timelines.find(key) != _runtime->timelines.end();
            const bool hasExactClip = detail::findMotionClip(
                                          *_runtime->activeMotion,
                                          detail::narrow(_chara), key, false) !=
                nullptr;
            if(hasExactTimeline || hasExactClip) {
                playOne(key, true);
                started = true;
            }
        }

        if(!started) {
            const auto &primary =
                !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel, false);
                started = true;
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(started && !label.IsEmpty()) {
            _motionKey = label;
        }
        return started;
    }

    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return false;
        }
        const auto key = detail::narrow(name);
        return layers->find(key) != layers->end() &&
            _runtime->disabledSelectorTargets.find(key) ==
                _runtime->disabledSelectorTargets.end();
    }

    void Player::deactivateSelectorTarget(ttstr name) {
        _runtime->disabledSelectorTargets[detail::narrow(name)] = true;
    }

    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        // AnimKAGLayer compares this value with its previous result and only
        // invalidates the Layer when it changes.  Expose the progress
        // ping-pong token expected by that protocol.  Returning the static
        // source-path list leaves the first transparent frame of fade-in
        // motions resident forever because that list does not change while
        // opacity, transforms and nested timelines advance.
        return detail::makeArray(
            {tTJSVariant(static_cast<tjs_int>(_commandListPulse ? 1 : 0))});
    }

    bool Player::getD3DAvailable() { return true; }

    void Player::doAlphaMaskOperation() {}

    // Aligned to libkrkr2.so Player_playImpl (0x6B21E8):
    // Called from sub_6BE0C0 at 0x6BE46C with flags = motionFlags | v12.
    // flags: PlayFlagForce(1)=force reload, PlayFlagStealth(16)=set stealth fields only.
    void Player::onFindMotion(ttstr name, int flags) {
        // PlayFlagStealth (0x10): store as stealth motion, don't load
        // Binary: if ((flags & 0x10) && !player->project) { player->motionKey = name; return; }
        if ((flags & PlayFlagStealth) && _project.Type() == tvtVoid) {
            _stealthMotion = name;
            return;
        }

        // PlayFlagForce (0x01): force reload even if same motion is loaded
        // Binary: Player_setMotionImpl skips reload guard when force flag set
        if ((flags & PlayFlagForce) && _motionKey == name) {
            _motionKey = ttstr();  // clear to bypass same-motion guard in findMotion
        }

        // Load the motion (equivalent to Player_setMotionImpl → loadMotion)
        (void)findMotion(name);

        // After loading, prime timelines and start playback
        // (aligned to Player_setMotionImpl post-load behavior)
        if (_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        // Start all timelines playing (equivalent to playCompat's playOne loop)
        if (_runtime->activeMotion && !_runtime->timelines.empty()) {
            double maxTF = 0.0;
            _runtime->playingTimelineLabels.clear();
            const auto &primary =
                !_runtime->activeMotion->mainTimelineLabels.empty()
                    ? _runtime->activeMotion->mainTimelineLabels
                    : _runtime->activeMotion->diffTimelineLabels;
            for (const auto &timelineLabel : primary) {
                auto &state = _runtime->timelines[timelineLabel];
                state.flags = flags & ~PlayFlagStealth;  // pass flags minus stealth
                state.playing = true;
                state.blendRatio = 1.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = state.currentTime;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
                _runtime->playingTimelineLabels.push_back(timelineLabel);
                if (state.totalFrames > maxTF) maxTF = state.totalFrames;
            }
            _cachedTotalFrames = maxTF;  // player+1128 cached value
            _allplaying = !_runtime->playingTimelineLabels.empty();
        }

        // Handle pending stealth motion (0x6B226C..0x6B2280)
        if (!_stealthMotion.IsEmpty()) {
            _stealthChara = _chara;
            // stealthMotion is consumed — binary nulls it after use
            _stealthMotion = ttstr();
        }
    }

    tjs_error Player::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        std::array<double, 6> matrix{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        if(numparams >= 6) {
            for(size_t index = 0; index < matrix.size(); ++index) {
                if(!param[index] || param[index]->Type() == tvtVoid) {
                    return TJS_E_INVALIDPARAM;
                }
                matrix[index] = param[index]->AsReal();
            }
        } else if(numparams == 1 && param[0] && param[0]->Type() == tvtObject &&
                  param[0]->AsObjectNoAddRef() != nullptr) {
            const auto object = *param[0];
            tTJSVariant value;
            if(getObjectProperty(object, TJS_W("m11"), value) &&
               value.Type() != tvtVoid) {
                matrix[0] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m21"), value) &&
               value.Type() != tvtVoid) {
                matrix[1] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m12"), value) &&
               value.Type() != tvtVoid) {
                matrix[2] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m22"), value) &&
               value.Type() != tvtVoid) {
                matrix[3] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m14"), value) &&
               value.Type() != tvtVoid) {
                matrix[4] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m24"), value) &&
               value.Type() != tvtVoid) {
                matrix[5] = value.AsReal();
            }
        } else {
            return TJS_E_BADPARAMCOUNT;
        }

        const auto previousMatrix = nativeInstance->_runtime->drawAffineMatrix;
        bool affineChanged = false;
        for(size_t index = 0; index < matrix.size(); ++index) {
            if(std::fabs(previousMatrix[index] - matrix[index]) > 1e-7) {
                affineChanged = true;
                break;
            }
        }
        nativeInstance->_runtime->drawAffineMatrix = matrix;
        if(affineChanged) {
            // The affine matrix is part of every prepared vertex position.
            // A title/scene source can set it after the child motion has
            // already prepared its first frame; keep that stale identity
            // frame from being reused and make updateLayers propagate the
            // new matrix through nested motion players before drawing.
            nativeInstance->_runtime->preparedRenderItemsValid = false;
            nativeInstance->_runtime->clearPresentationRenderReuse();
            nativeInstance->_layersDirty = true;
        }
        nativeInstance->invokeNativeBackend(
            "setpresentationaffine",
            {MotionBackendValue::Number(matrix[0]),
             MotionBackendValue::Number(matrix[1]),
             MotionBackendValue::Number(matrix[2]),
             MotionBackendValue::Number(matrix[3]),
             MotionBackendValue::Number(matrix[4]),
             MotionBackendValue::Number(matrix[5])});
        const auto motionPath =
            nativeInstance->_runtime && nativeInstance->_runtime->activeMotion
                ? nativeInstance->_runtime->activeMotion->path
                : std::string{};
        const bool isIdentity =
            matrix[0] == 1.0 && matrix[1] == 0.0 && matrix[2] == 0.0 &&
            matrix[3] == 1.0 && matrix[4] == 0.0 && matrix[5] == 0.0;
        detail::logoChainTraceLogf(
            motionPath, "setDrawAffine", "0x6D4F14",
            nativeInstance->_clampedEvalTime,
            "numparams={} matrix=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] nonIdentityFlag={} routeSource={}",
            numparams, matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
            matrix[5], isIdentity ? 0 : 1,
            (numparams >= 6) ? "six-params"
                             : ((numparams == 1) ? "matrix-object" : "invalid"));
        if(emoteAffineTraceEnabled()) {
            bool changed = false;
            for(size_t index = 0; index < matrix.size(); ++index) {
                changed = changed ||
                    std::fabs(previousMatrix[index] - matrix[index]) > 1e-7;
            }
            if(changed) {
                LOGGER->info(
                    "[EMOTE_AFFINE] player={} motion={} drawMatrix=[{:.6f},{:.6f},{:.6f},{:.6f},{:.3f},{:.3f}]",
                    static_cast<const void *>(nativeInstance), motionPath,
                    matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                    matrix[5]);
            }
        }
        return TJS_S_OK;
    }

    tjs_error Player::captureCanvasCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            if(nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
                if(result) {
                    *result = *param[0];
                }
                return TJS_S_OK;
            }
        }

        if(result) {
            *result = nativeInstance->captureCanvas();
        }
        return TJS_S_OK;
    }

    tjs_error Player::clearCompatMethod(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        return clearCompatForNative(result, numparams, param, self);
    }

    tjs_error Player::clearCompatForNative(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           Player *self) {
        if(result) {
            result->Clear();
        }
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param || !param[0] ||
           param[0]->Type() != tvtObject) {
            return TJS_E_INVALIDPARAM;
        }

        iTJSDispatch2 *target = selectVariantDispatchTarget(param[0]);
        if(auto *adaptor =
               ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                   target, false)) {
            target = adaptor->getPrivateRenderTargetObject();
            if(!target) {
                return TJS_S_OK;
            }
        } else if(auto *resolved = tryResolveLayerDispatch(*param[0])) {
            target = resolved;
        }

        tTJSNI_BaseLayer *layer = nullptr;
        if(!target || TJS_FAILED(target->NativeInstanceSupport(
                          TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                          reinterpret_cast<iTJSNativeInstance **>(&layer))) ||
           !layer) {
            return TJS_S_OK;
        }

        auto *bitmap = layer->GetMainImage();
        if(!bitmap || bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) {
            return TJS_S_OK;
        }
        const auto &cachedEmoteMotion =
            self->_runtime->emoteRenderFrameCache.motion;
        // The dx_*_timeline players use one invisible full-window work layer:
        // clear(), draw(), then assignImages() the result to the character
        // layer. Once draw() has a GPU-resident frame cache, the intermediate
        // clear forces a copy-on-write texture change; assigning the same
        // cached frame back then dirties and recomposes the entire scene.
        // Cached draw() will either alias the frame unchanged or perform its
        // own prepareLayerForRender() clear before a real refresh, so this
        // paired clear is redundant. Restrict the shortcut to in-game dynamic
        // timelines: title/load-menu E-mote layers rely on standalone clear()
        // notifications for their state transitions.
        const bool skipCachedDynamicEmoteWorkLayerClear =
            self->_runtime->isEmoteMode &&
            self->_runtime->emoteRenderFrameCache.bitmap &&
            (cachedEmoteMotion.find("タイムライン.psb") !=
                 std::string::npos ||
             cachedEmoteMotion.find("_timeline.psb") !=
                 std::string::npos);
        if(skipCachedDynamicEmoteWorkLayerClear) {
            return TJS_S_OK;
        }
        const tjs_uint32 color =
            numparams >= 2 && param[1]
                ? static_cast<tjs_uint32>(param[1]->AsInteger())
                : 0;
        const tTVPRect clearRect(
            0, 0, static_cast<tjs_int>(bitmap->GetWidth()),
            static_cast<tjs_int>(bitmap->GetHeight()));
        if(!TVPGodotClearMotionScratchInPlace(
               bitmap, clearRect, color)) {
            bitmap->Fill(clearRect, color);
        }
        layer->Update(false);
        self->_runtime->clearPresentationRenderReuse();
        return TJS_S_OK;
    }

    // drawCompat — aligned to libkrkr2.so sub_6D5FB8 / Player_drawD3D (0x6D5B90).
    // Logic:
    //   1. param is D3DAdaptor → set _d3dDrawMode and render via D3D path immediately
    //   2. param is SLA → route to SLA target
    //   3. param is Layer → if _d3dDrawMode, render via shared D3DAdaptor+captureCanvas;
    //      else render directly to Layer
    tjs_error Player::drawCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *nativeInstance = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        return drawCompatForNative(result, numparams, param, nativeInstance);
    }

    tjs_error Player::drawCompatForNative(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(LOGGER && motionDebugEnabled()) {
            LOGGER->info(
                "motion drawCompat entered: native={} numparams={} firstArgType={}",
                static_cast<const void *>(nativeInstance), numparams,
                (numparams > 0 && param && param[0])
                    ? static_cast<int>(param[0]->Type())
                    : -1);
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }
        auto releaseDeferredEndedTimelineHold = [&]() {
            nativeInstance->releaseDeferredEndedTimelineRenderHoldAfterDraw();
        };

        const auto motionPath =
            nativeInstance->_runtime && nativeInstance->_runtime->activeMotion
                ? nativeInstance->_runtime->activeMotion->path
                : std::string{};
        tTJSVariant *arg = (numparams > 0 && param) ? param[0] : nullptr;
        iTJSDispatch2 *paramObj = selectVariantDispatchTarget(arg);
        tTJSNI_BaseLayer *argLayer = nullptr;
        const bool argIsLayer = arg && tryGetLayerObject(*arg, argLayer);
        iTJSDispatch2 *resolvedLayerObject =
            arg ? tryResolveLayerDispatch(*arg) : nullptr;
        if(LOGGER && motionDebugEnabled()) {
            static int probeCount = 0;
            const bool titleProbe =
                motionPath.find("title") != std::string::npos;
            if(!argIsLayer && (titleProbe || probeCount < 32)) {
                ++probeCount;
                tTJSVariantClosure closure;
                if(arg && arg->Type() == tvtObject) {
                    closure = arg->AsObjectClosureNoAddRef();
                } else {
                    closure.Object = nullptr;
                    closure.ObjThis = nullptr;
                }
                LOGGER->info(
                    "motion drawCompat target probe: motion={} argType={} object=[{}] objThis=[{}] selected=[{}] valueLayer=[{}] resolvedObject={}",
                    motionPath, arg ? static_cast<int>(arg->Type()) : -1,
                    describeDispatchLayerProbe(closure.Object),
                    describeDispatchLayerProbe(closure.ObjThis),
                    describeDispatchLayerProbe(paramObj),
                    describeLayerForQueryDebug(argLayer),
                    static_cast<const void *>(resolvedLayerObject));
            }
        }

        if(!paramObj) {
            iTJSDispatch2 *targetLayerObject =
                tryResolveLayerDispatch(nativeInstance->_targetLayer);
            if(targetLayerObject) {
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} route=stored-target target={}",
                        motionPath,
                        static_cast<const void *>(targetLayerObject));
                }
                if(nativeInstance->_d3dDrawMode) {
                    nativeInstance->renderViaSharedD3DAdaptor(targetLayerObject);
                } else {
                    nativeInstance->renderToLayer(targetLayerObject);
                }
                if(result) {
                    *result = tTJSVariant(targetLayerObject, targetLayerObject);
                }
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.dispatch", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "route=no-param drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f})",
                nativeInstance->_runtime->drawAffineMatrix[0],
                nativeInstance->_runtime->drawAffineMatrix[1],
                nativeInstance->_runtime->drawAffineMatrix[2],
                nativeInstance->_runtime->drawAffineMatrix[3],
                nativeInstance->_runtime->drawAffineMatrix[4],
                nativeInstance->_runtime->drawAffineMatrix[5],
                nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
            if(result) {
                *result = nativeInstance->_runtime->lastCanvas;
            }
            releaseDeferredEndedTimelineHold();
            return TJS_S_OK;
        }

        // Direct Layer fast path. TJS may pass Layer as a closure object that
        // also confuses unrelated ncbind native-instance probes, so resolve it
        // before checking D3DAdaptor/SLA wrappers.
        if(argIsLayer || resolvedLayerObject == paramObj) {
            auto *drawTargetObject =
                resolvedLayerObject ? resolvedLayerObject : paramObj;
            if(!argLayer && resolvedLayerObject) {
                tTJSVariant resolvedVar(resolvedLayerObject, resolvedLayerObject);
                tryGetLayerObject(resolvedVar, argLayer);
            }
            if(LOGGER && motionDebugEnabled()) {
                LOGGER->info(
                    "motion drawCompat route: motion={} object={} resolvedObject={} route={} d3dDrawModeBefore={} targetLayer={} drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                    motionPath, static_cast<const void *>(paramObj),
                    static_cast<const void *>(drawTargetObject),
                    nativeInstance->_d3dDrawMode ? "layer-via-d3d" : "layer",
                    nativeInstance->_d3dDrawMode ? 1 : 0,
                    static_cast<const void *>(argLayer),
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5]);
            }
            detail::logoChainTraceCheck(
                motionPath, "drawCompat.dispatch", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "Layer -> renderToLayer/renderViaSharedD3DAdaptor",
                nativeInstance->_d3dDrawMode
                    ? "Layer -> renderViaSharedD3DAdaptor"
                    : "Layer -> renderToLayer",
                true, "drawCompat Layer routing mismatch");
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.matrix", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "route={} drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                nativeInstance->_d3dDrawMode ? "layer-via-d3d" : "layer",
                nativeInstance->_runtime->drawAffineMatrix[0],
                nativeInstance->_runtime->drawAffineMatrix[1],
                nativeInstance->_runtime->drawAffineMatrix[2],
                nativeInstance->_runtime->drawAffineMatrix[3],
                nativeInstance->_runtime->drawAffineMatrix[4],
                nativeInstance->_runtime->drawAffineMatrix[5],
                nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
            if(nativeInstance->_d3dDrawMode) {
                nativeInstance->renderViaSharedD3DAdaptor(drawTargetObject);
            } else {
                nativeInstance->renderToLayer(drawTargetObject);
            }
            if(result) *result = *arg;
            releaseDeferredEndedTimelineHold();
            return TJS_S_OK;
        }

        // Step 1: Check if param is D3DAdaptor (libkrkr2.so checks NIS with
        // D3DAdaptor classID). If so, set _d3dDrawMode and render immediately.
        {
            auto *d3dAdaptor =
                ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(paramObj, false);
            if(d3dAdaptor) {
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} object={} route=d3d d3dDrawModeBefore={}",
                        motionPath, static_cast<const void *>(paramObj),
                        nativeInstance->_d3dDrawMode ? 1 : 0);
                }
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "D3DAdaptor -> Player_drawD3D",
                    "D3DAdaptor -> Player_drawD3D", true,
                    "drawCompat D3D routing mismatch");
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.matrix", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "route=d3d drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5],
                    nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
                nativeInstance->_d3dDrawMode = true;
                nativeInstance->renderToD3DAdaptor(d3dAdaptor);
                if(result && arg) *result = *arg;
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
        }

        // Step 2: Check if param is SLA.
        // Aligned to libkrkr2.so Player_drawCompat (0x6D5FB8):
        // the native code only checks the SeparateLayerAdaptor class ID here.
        // It does not route plain Layer objects through the SLA backend just
        // because they resolve to an owner/target layer.
        {
            auto *sla =
                ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    paramObj, false);
            if(sla) {
                tTJSNI_BaseLayer *ownerLayer = nullptr;
                if(auto *owner = sla->getOwner()) {
                    tTJSVariant ownerVar(owner, owner);
                    tryGetLayerObject(ownerVar, ownerLayer);
                }
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} object={} route=sla owner=[{}] ancestry=[{}]",
                        motionPath, static_cast<const void *>(paramObj),
                        describeLayerForQueryDebug(ownerLayer),
                        describeLayerAncestryForQueryDebug(ownerLayer));
                }
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "SeparateLayerAdaptor -> Player_DrawSLA",
                    "SeparateLayerAdaptor -> Player_DrawSLA", true,
                    "drawCompat SLA routing mismatch");
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.matrix", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "route=sla drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5],
                    nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
                nativeInstance->renderToSeparateLayerAdaptor(paramObj);
                if(result && arg) {
                    *result = *arg;
                }
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
        }

        // Step 4: param resolves to a Layer via property chain
        {
            iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*arg);
            if(resolved) {
                tTJSNI_BaseLayer *resolvedLayer = nullptr;
                tTJSVariant resolvedVar(resolved, resolved);
                tryGetLayerObject(resolvedVar, resolvedLayer);
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} object={} resolvedObject={} route={} d3dDrawModeBefore={} target=[{}] ancestry=[{}]",
                        motionPath, static_cast<const void *>(paramObj),
                        static_cast<const void *>(resolved),
                        nativeInstance->_d3dDrawMode
                            ? "resolved-layer-via-d3d"
                            : "resolved-layer",
                        nativeInstance->_d3dDrawMode ? 1 : 0,
                        describeLayerForQueryDebug(resolvedLayer),
                        describeLayerAncestryForQueryDebug(resolvedLayer));
                }
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "Resolved owner Layer -> renderToLayer/renderViaSharedD3DAdaptor",
                    nativeInstance->_d3dDrawMode
                        ? "Resolved owner Layer -> renderViaSharedD3DAdaptor"
                        : "Resolved owner Layer -> renderToLayer",
                    true, "drawCompat owner-layer routing mismatch");
                if(nativeInstance->_d3dDrawMode) {
                    nativeInstance->renderViaSharedD3DAdaptor(resolved);
                } else {
                    nativeInstance->renderToLayer(resolved);
                }
                if(result) *result = tTJSVariant(resolved, resolved);
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
        }

        // Fallback: no SLA/Layer match
        if(LOGGER && motionDebugEnabled()) {
            LOGGER->info(
                "motion drawCompat unresolved target: motion={} selected=[{}]",
                motionPath, describeDispatchLayerProbe(paramObj));
        }
        detail::logoChainTraceCheck(
            motionPath, "drawCompat.dispatch", "0x6D5FB8",
            nativeInstance->_clampedEvalTime,
            "D3DAdaptor | SeparateLayerAdaptor | Layer",
            "unresolved target", false,
            "drawCompat could not classify the target object");
        if(result) {
            *result = nativeInstance->_runtime->lastCanvas;
        }
        releaseDeferredEndedTimelineHold();
        return TJS_S_OK;
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        self->releaseDeferredEndedTimelineRenderHoldAfterDraw(true);

        ttstr label;
        tjs_int flags = 0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            if(param[0]->Type() == tvtInteger || param[0]->Type() == tvtReal) {
                flags = param[0]->AsInteger();
            } else {
                label = *param[0];
            }
        }
        if(numparams > 1 && param[1] && param[1]->Type() != tvtVoid) {
            flags = param[1]->AsInteger();
        }

        if(!self->_runtime->activeMotion && self->_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(self->_project)) {
                activateMotion(*self->_runtime, snapshot);
                self->syncVariableKeysFromActiveMotion();
            }
        }

        self->ensureMotionLoaded();
        if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
            detail::primeTimelineStates(self->_runtime->timelines,
                                        *self->_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !self->_runtime->activeMotion) {
            self->setMotion(label);
            self->ensureMotionLoaded();
            if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
                detail::primeTimelineStates(self->_runtime->timelines,
                                            *self->_runtime->activeMotion);
            }
        }

        if(!self->_runtime->activeMotion) {
            if(result) {
                *result = tTJSVariant(false);
            }
            return TJS_S_OK;
        }

        if(motionDebugEnabled() && LOGGER) {
            LOGGER->info(
                "motion play request: motion={} label={} flags={} chara={} timelines={} activeLabels=[{}]",
                self->_runtime->activeMotion->path, detail::narrow(label), flags,
                detail::narrow(self->_chara), self->_runtime->timelines.size(),
                joinStrings(self->_runtime->activeMotion->mainTimelineLabels));
        }

        if((flags & PlayFlagForce) != 0) {
            self->stopTimeline(TJS_W(""));
        }

        const auto playOne = [&](const std::string &timelineLabel,
                                 const bool rememberExplicit) {
            auto &state = self->_runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            if(const auto *clip = detail::findMotionClip(
                   *self->_runtime->activeMotion,
                   detail::narrow(self->_chara), timelineLabel, false)) {
                // Motion labels are reused by many objects in one PSB. Use
                // the selected object's timing instead of the flattened
                // label table, whose last writer may be a different object.
                state.totalFrames = clip->totalFrames;
                state.loop = clip->loop;
                state.loopTime = clip->loopTime;
            }
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            state.currentTime = 0.0;
            state.controlInitialized = false;
            state.controlLastAppliedTime = 0.0;
            state.controlFrameCursor.clear();
            state.controlTrackValues.clear();
            state.controlTrackAnimators.clear();
            if(std::find(self->_runtime->playingTimelineLabels.begin(),
                         self->_runtime->playingTimelineLabels.end(),
                         timelineLabel) ==
               self->_runtime->playingTimelineLabels.end()) {
                self->_runtime->playingTimelineLabels.push_back(timelineLabel);
            }
            if(rememberExplicit) {
                self->_runtime->lastExplicitTimelineLabel = timelineLabel;
            }
            // Ensure totalFrames is set (may be 0 if timeline wasn't primed)
            if(state.totalFrames <= 0.0 && self->_runtime->activeMotion) {
                auto it = self->_runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != self->_runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(self->_runtime->timelines.find(key) != self->_runtime->timelines.end()) {
                playOne(key, true);
                started = true;
                if(motionDebugEnabled() && LOGGER) {
                    LOGGER->info("motion play exact match: request={} source={}",
                                 key, self->_runtime->activeMotion->path);
                }
            } else if(self->_runtime->activeMotion) {
                const auto aliases = resolveYuzuShortMotionLabels(
                    *self->_runtime->activeMotion, key,
                    detail::narrow(self->_chara));
                for(const auto &alias : aliases) {
                    if(self->_runtime->timelines.find(alias) ==
                       self->_runtime->timelines.end()) {
                        continue;
                    }
                    playOne(alias, true);
                    started = true;
                }
                if(started && motionDebugEnabled() && LOGGER) {
                    LOGGER->info(
                        "motion play yuzu short alias: request={} chara={} source={} resolved=[{}]",
                        key, detail::narrow(self->_chara),
                        self->_runtime->activeMotion->path,
                        joinStrings(aliases));
                }
            }
        }

        if(!started) {
            const auto &primary = !self->_runtime->activeMotion->mainTimelineLabels.empty()
                ? self->_runtime->activeMotion->mainTimelineLabels
                : self->_runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel, false);
                started = true;
            }
        }

        self->_allplaying = !self->_runtime->playingTimelineLabels.empty();
        if(started) {
            // Motion.Player.motion is the currently selected timeline label,
            // not the PSB storage name.  AnimKAGLayer reads it when the
            // timeline starts and stops (for example to distinguish `show`
            // from `hide`).  Keeping the old/empty value lets the visual
            // transition finish while silently skipping the script-side
            // completion action that rebuilds a page or closes a dialog.
            if(!label.IsEmpty()) {
                self->_motionKey = label;
            }
            // A newly selected clip starts with its own playback clock.  The
            // accumulated player clock may still point at the end of the
            // previously selected clip; retaining it makes the first
            // zero-delta refresh immediately finish a shorter replacement
            // clip before it can render or advance.
            self->_frameLastTime = 0.0;
            self->_frameLoopTime = 0.0;
            self->_loopTime = 0.0;
            self->_clampedEvalTime = 0.0;
            self->_frameTickCount = 0.0;
            self->_runtime->nodes.clear();
            self->_runtime->nodesBuilt = false;
            self->_runtime->nodeLabelMap.clear();
        }
        if(motionDebugEnabled() && LOGGER) {
            std::string controlSummary = "<none>";
            if(!label.IsEmpty()) {
                const auto controlIt =
                    self->_runtime->activeMotion->timelineControlByLabel.find(
                        detail::narrow(label));
                if(controlIt != self->_runtime->activeMotion
                                    ->timelineControlByLabel.end()) {
                    controlSummary = fmt::format(
                        "last={:.3f},loop=[{:.3f},{:.3f}],tracks={}",
                        controlIt->second.lastTime,
                        controlIt->second.loopBegin,
                        controlIt->second.loopEnd,
                        controlIt->second.tracks.size());
                }
            }
            LOGGER->info(
                "motion play result: player={} motion={} label={} started={} speed={} control={} playing=[{}]",
                static_cast<const void *>(self),
                self->_runtime->activeMotion->path, detail::narrow(label),
                started ? 1 : 0, self->_speed ? 1 : 0, controlSummary,
                joinStrings(self->_runtime->playingTimelineLabels));
        }
        if(self->_allplaying) {
            self->enableAutoProgress(objthis);
        } else {
            self->disableAutoProgress();
        }

        if(result) {
            *result = tTJSVariant(started);
        }
        return TJS_S_OK;
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();
        self->noteManualProgress();

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }

        self->_runtime->pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);
        const auto motionPath =
            self->_runtime && self->_runtime->activeMotion
                ? self->_runtime->activeMotion->path
                : std::string{};
        detail::logoChainTraceCheck(
            motionPath, "progressCompat.dt", "0x6D2A98",
            self->_clampedEvalTime,
            fmt::format("dt_ms*60/1000={:.6f}", delta * kMotionFramesPerMillisecond),
            fmt::format("dt_frames={:.6f}", self->_frameLastTime),
            std::fabs(self->_frameLastTime - delta * kMotionFramesPerMillisecond) <
                0.000001,
            "progressCompat dt(ms)->frame conversion diverged from 0x6D2A98");
        const std::string renderHoldLabel =
            self->beginEndedTimelineRenderHold();

        // Aligned to libkrkr2.so Player_progressCompat (0x6D2A98):
        // progress_inner -> updateLayers -> calcBounds -> dispatchEvents.
        self->ensureNodeTreeBuilt();
        if(!self->_runtime->nodes.empty()) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.update", "0x6D2A98",
                self->_clampedEvalTime,
                "timelineCurrentTime={:.3f} pendingEvents={} nodes={}",
                self->_clampedEvalTime, self->_runtime->pendingEvents.size(),
                self->_runtime->nodes.size());
            self->updateLayers();
        }
        self->calcBounds();

        // Match krkrsdl3: progress() advances/evaluates state only.  The TJS
        // MotionAffineSourceLayer immediately follows it with draw(), which
        // selects Layer, D3DAdaptor or SeparateLayerAdaptor.  Presenting here
        // as well creates a second frame that bypasses the authored affine
        // owner and is visible for one host frame at a different origin.

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 30.0 && self->_clampedEvalTime <= 40.0) {
            std::fprintf(stderr, "SHOTMARK motion=%s frame=%.3f\n",
                         motionPath.c_str(), self->_clampedEvalTime);
        }

        if(!self->deferEndedTimelineRenderHoldUntilDraw(renderHoldLabel)) {
            self->endEndedTimelineRenderHold(renderHoldLabel);
        }
        self->dispatchPendingEvents(objthis);
        if(!self->_allplaying && self->_runtime->playingTimelineLabels.empty()) {
            self->disableAutoProgress();
        }

        if(result) {
            *result = tTJSVariant(self->getProgressCompat());
        }
        return TJS_S_OK;
    }

    tjs_error Player::setVariableCompatMethod(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setVariable(ttstr(*param[0]), param[1]->AsReal(), transition,
                          ease);
        // Gallery/layout variables are intentionally coalesced until the next
        // draw or layer query: a page initializes dozens of them in one TJS
        // callback. Interactive `select` controllers are different. KAG does
        // not call progress(0) after the pointer leaves every button, so their
        // leave state must be committed here or the last hover frame remains
        // cached indefinitely.
        const auto variableKey = detail::narrow(ttstr(*param[0]));
        const double selectorValue = param[1]->AsReal();
        const bool interactiveSelectKey =
            variableKey == "select" ||
            (variableKey.size() > 7 &&
             variableKey.compare(variableKey.size() - 7, 7, "/select") == 0);
        // `select=0` is the bulk initialization/default state and is normally
        // followed by one layout draw.  Repainting the whole retained canvas
        // for every such assignment interrupts menu construction.  Non-zero
        // values are the actual pointer/key transitions that need an
        // immediate commit.
        const bool interactiveSelect =
            interactiveSelectKey && std::abs(selectorValue) > 0.0000001;
        if(interactiveSelect && transition <= 0.0 && self->_runtime &&
           self->_runtime->activeMotion) {
            self->ensureNodeTreeBuilt();
            if(!self->_runtime->nodes.empty()) {
                self->updateLayers();
                self->calcBounds();
            }

            // Updating the selector tree only changes the native node cache.
            // Classic KAG menu scripts do not consistently issue progress(0)
            // after onLeave, so commit the new selector frame during this
            // input event. Render the owning root UI, not the isolated button:
            // selector sprites are alpha-composited, and drawing only the
            // child leaves the previous hover pixels underneath it.
            Player *renderOwner = self;
            std::unordered_set<Player *> ancestry;
            while(renderOwner->_motionParentPlayer &&
                  ancestry.insert(renderOwner).second) {
                renderOwner = renderOwner->_motionParentPlayer;
            }
            if(!renderOwner->_autoProgressRendering &&
               !renderOwner->_presentationHoldRendering) {
                iTJSDispatch2 *target = nullptr;
                if(renderOwner->_targetLayer.Type() == tvtObject) {
                    tTJSVariant targetValue = renderOwner->_targetLayer;
                    target = tryResolveLayerDispatch(targetValue);
                }
                if(!target && renderOwner->_runtime &&
                   renderOwner->_runtime->lastCanvas.Type() == tvtObject) {
                    target = tryResolveLayerDispatch(
                        renderOwner->_runtime->lastCanvas);
                }
                if(!target && self != renderOwner &&
                   self->_targetLayer.Type() == tvtObject) {
                    tTJSVariant targetValue = self->_targetLayer;
                    target = tryResolveLayerDispatch(targetValue);
                }
                if(target) {
                    renderOwner->_autoProgressRendering = true;
                    try {
                        // The normal selector frame may be translucent. Clear
                        // the retained root canvas before drawing the complete
                        // UI, otherwise each visited hover remains blended
                        // beneath subsequent frames.
                        if(auto *targetLayer = nativeLayerFromDispatch(target)) {
                            if(auto *image = targetLayer->GetMainImage()) {
                                const tTVPRect clearRect(
                                    0, 0,
                                    std::max<tjs_int>(
                                        targetLayer->GetImageWidth(), 0),
                                    std::max<tjs_int>(
                                        targetLayer->GetImageHeight(), 0));
                                image->Fill(clearRect, 0x00000000);
                            }
                        }
                        if(renderOwner->_d3dDrawMode) {
                            renderOwner->renderViaSharedD3DAdaptor(target);
                        } else {
                            renderOwner->renderToLayer(target);
                        }
                    } catch(const std::exception &e) {
                        if(LOGGER && motionDebugEnabled()) {
                            LOGGER->warn(
                                "motion selector render failed: motion={} variable={} error={}",
                                renderOwner->_runtime &&
                                        renderOwner->_runtime->activeMotion
                                    ? renderOwner->_runtime->activeMotion->path
                                    : std::string("<none>"),
                                variableKey, e.what());
                        }
                    } catch(...) {
                        if(LOGGER && motionDebugEnabled()) {
                            LOGGER->warn(
                                "motion selector render failed: motion={} variable={} error=<unknown>",
                                renderOwner->_runtime &&
                                        renderOwner->_runtime->activeMotion
                                    ? renderOwner->_runtime->activeMotion->path
                                    : std::string("<none>"),
                                variableKey);
                        }
                    }
                    renderOwner->_autoProgressRendering = false;
                }
            }
            // Parameter-selected one-shot children (notably button `out`
            // transitions) can outlive the already-finished root timeline.
            // Keep driving the owning UI until those child timelines finish.
            if(renderOwner == self &&
               (renderOwner->_allplaying ||
                renderOwner->hasPlayingChildPlayers())) {
                renderOwner->_allplaying = true;
                renderOwner->enableAutoProgress(objthis);
            }
        }
        return TJS_S_OK;
    }

    tjs_error Player::setCoordCompatMethod(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setEmoteCoord(param[0]->AsReal(), param[1]->AsReal(), transition,
                            ease);
        return TJS_S_OK;
    }

    tjs_error Player::isPlayingCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // `playing` describes the selected/root timeline only. `allplaying`
        // is maintained separately by updateLayers and also includes nested
        // child players. Querying one property must not clear the other: the
        // KAG continuous handler reads `playing` after progress(), then relies
        // on `allplaying` on the next tick to finish longer child motions.
        const bool playing = self->getPlaying();
        if(result) {
            *result = tTJSVariant(playing);
        }
        return TJS_S_OK;
    }

    tjs_error Player::stopCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Aligned to libkrkr2.so Player_stop (0x6D9A30):
        // Binary simply clears the Player-level playing flag (player+1099).
        // Timeline state is left intact; TJS polls `playing` for edge-triggered
        // stop detection and may still inspect the final motion pose afterward.
        self->_allplaying = false;
        self->disableAutoProgress();

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    tTJSVariant Player::motionList() {
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for(const auto &[_, snapshot] : _runtime->motionsByKey) {
            if(snapshot && seen.insert(snapshot->path).second) {
                paths.push_back(snapshot->path);
            }
        }
        return detail::makeArray(detail::stringsToVariants(paths));
    }

    void Player::emoteEdit(tTJSVariant args) {
        _directEdit = true;
        _tags = args;
    }

} // namespace motion
