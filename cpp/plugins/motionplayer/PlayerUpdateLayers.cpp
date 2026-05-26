// PlayerUpdateLayers.cpp — updateLayers dispatcher and native phase order
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "MotionTraceWeb.h"

using namespace motion::internal;

namespace motion {
    // --- updateLayers: 3-phase pipeline ---
    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
    // Operates on persistent MotionNode deque instead of re-walking PSB tree.
    void Player::updateLayers() {
        detail::motionTraceRecordUpdatePlayer(this);
        auto &nodes = _runtime->nodes;
        if (nodes.empty()) return;
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};
        const double currentTime = _clampedEvalTime;

        // Keep legacy diagnostic scratch in sync with node count. The native
        // node+8 path is parameterEntries; do not use this as player+384.
        if (_runtime->perNodeEvalData.size() != nodes.size()) {
            _runtime->perNodeEvalData.resize(nodes.size());
        }
        for (size_t ni = 0; ni < nodes.size(); ++ni) {
            _runtime->perNodeEvalData[ni].evalTime = _clampedEvalTime;
        }

        updateLayersPhase1_PreLoop(currentTime);
        updateLayersPhase2_MainLoop(currentTime);
        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
            const auto &root = nodes[0];
            detail::logoChainTraceLogf(
                motionPath, "updateLayers.phase1", "0x6BB33C", currentTime,
                "rootPos=({:.3f},{:.3f},{:.3f}) cameraVel=({:.3f},{:.3f},{:.3f}) damping={:.6f} variableCount={}",
                root.accumulated.posX, root.accumulated.posY,
                root.accumulated.posZ, _cameraVelocityX, _cameraVelocityY,
                _cameraVelocityZ, _cameraDamping, _variableValues.size());
            for(const auto &[label, value] : _variableValues) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase1.var", "0x6BB33C",
                    currentTime, "label={} value={:.6f}", label, value);
            }
            for(const auto &node : nodes) {
                const auto &ic = node.interpolatedCache;
                const auto &ac = node.accumulated;
                const auto &ls = node.localState;
                const bool hasParent = node.parentIndex >= 0
                    && node.parentIndex < static_cast<int>(nodes.size());
                const auto &pc = hasParent ? nodes[node.parentIndex].accumulated
                                           : nodes[0].accumulated;
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.node", "0x6BB33C",
                    currentTime,
                    "nodeIndex={} label={} type={} parent={} src={} inherit=0x{:x} indep={} interp[x={:.3f},y={:.3f},ox={:.3f},oy={:.3f},w={:.3f},h={:.3f},opacity={:.6f},angle={:.3f},scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),flip=({},{}) blend={}] local[pos=({:.3f},{:.3f},{:.3f}),angle={:.3f},scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),flip=({},{}) opacity={},blend={}] parentAccum[pos=({:.3f},{:.3f},{:.3f}),scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),matrix=({:.6f},{:.6f},{:.6f},{:.6f}),opacity={},blend={}] accum[pos=({:.3f},{:.3f},{:.3f}),scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),matrix=({:.6f},{:.6f},{:.6f},{:.6f}),opacity={},blend={},active={},visible={}]",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType, node.parentIndex,
                    ic.src.empty() ? std::string("<none>") : ic.src,
                    node.inheritFlags,
                    _independentLayerInherit ? 1 : 0,
                    ic.x, ic.y, ic.ox, ic.oy, ic.width, ic.height, ic.opacity,
                    ic.angle, ic.scaleX, ic.scaleY, ic.slantX, ic.slantY,
                    ic.flipX ? 1 : 0, ic.flipY ? 1 : 0, ic.blendMode,
                    ls.posX, ls.posY, ls.posZ, ls.angle, ls.scaleX, ls.scaleY,
                    ls.slantX, ls.slantY, ls.flipX ? 1 : 0, ls.flipY ? 1 : 0,
                    ls.opacity, ls.blendMode,
                    pc.posX, pc.posY, pc.posZ, pc.scaleX, pc.scaleY,
                    pc.slantX, pc.slantY, pc.m11, pc.m12, pc.m21, pc.m22,
                    pc.opacity, pc.blendMode,
                    ac.posX, ac.posY, ac.posZ, ac.scaleX, ac.scaleY,
                    ac.slantX, ac.slantY, ac.m11, ac.m12,
                    ac.m21, ac.m22, ac.opacity, ac.blendMode,
                    ac.active ? 1 : 0, ac.visible ? 1 : 0);
            }
        }

        // === PHASE 3: Post-loop processing ===
        // Call order matches libkrkr2.so Player_updateLayers (0x6BBC60..0x6BBCA8):
        // sub_6BC000 → sub_6BC4F0 → sub_6BD8DC → sub_6BDA28 →
        // sub_6BDCC0 → sub_6BDE94 → sub_6BE0C0 → sub_6BEDD0 →
        // sub_6BF0DC → sub_6C0528
        updateLayersPhase3_CameraConstraint();
        updateLayersPhase3_VertexComputation();
        updateLayersPhase3_Visibility();
        updateLayersPhase3_CameraNode();
        updateLayersPhase3_ShapeAABB();
        updateLayersPhase3_ShapeGeometry();
        updateLayersPhase3_MotionSubNode(currentTime);
        updateLayersPhase3_ParticleEmitter();
        updateLayersPhase3_ParticleSystem(currentTime);
        updateLayersPhase3_AnchorNode();

        // === Post-loop cleanup ===
        // Aligned to 0x6BBCB4..0x6BBE1C: clear per-node flags and timeline state.

        // Clear player+608 first-frame flag (0x6BBDF8: STRB WZR, [X19,#0x260]).
        _noUpdateYet = false;

        // Clear player+480 queuing flag (0x6BBDFC: STRB WZR, [X19,#0x1E0]).
        _queuing = false;

        // Clear node+44 (flags byte) and node+1504 (accumulated dirty) for
        // all non-root nodes (0x6BBCFC..0x6BBD40).
        for (size_t ci = 1; ci < nodes.size(); ++ci) {
            nodes[ci].flags &= ~0x01;           // node+44
            nodes[ci].accumulated.dirty = false; // node+1504
        }

        // Clear legacy local scratch flags.
        for (auto &evalData : _runtime->perNodeEvalData) {
            evalData.dirtyFlag = 0;
        }

    }

} // namespace motion
