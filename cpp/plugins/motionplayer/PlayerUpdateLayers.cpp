// PlayerUpdateLayers.cpp — updateLayers 3-phase pipeline + extracted sub-phase methods
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "ncbind.hpp"    // ncbInstanceAdaptor<Player>::CreateAdaptor for TJS bridge
#include "tjsArray.h"    // TJSCreateArrayObject, TJSGetArrayElementCount
#include <algorithm>
#include <cctype>
#include <cstdlib>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AETHERKIRI_MOTION_HAS_ARM_NEON 1
#else
#define AETHERKIRI_MOTION_HAS_ARM_NEON 0
#endif
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif

using namespace motion::internal;

namespace {
    inline void evaluateMotionBezierPatch(const float *mesh, float u, float v,
                                          float &outX, float &outY) {
        const float su = 1.0f - u;
        const float sv = 1.0f - v;
        const float bu[4] = {
            su * su * su,
            3.0f * su * su * u,
            3.0f * su * u * u,
            u * u * u,
        };
        const float bv[4] = {
            sv * sv * sv,
            3.0f * sv * sv * v,
            3.0f * sv * v * v,
            v * v * v,
        };

        // A bicubic Bernstein patch is separable. Evaluate the four rows
        // first and then blend those results vertically. This is algebraically
        // identical to the former 16-control-point loop but avoids forming
        // and applying 16 two-dimensional weights for every E-mote mesh
        // vertex and every flattened child corner.
        float rowX[4];
        float rowY[4];
        for(int row = 0; row < 4; ++row) {
            const float *points = mesh + row * 8;
            rowX[row] =
                points[0] * bu[0] + points[2] * bu[1] +
                points[4] * bu[2] + points[6] * bu[3];
            rowY[row] =
                points[1] * bu[0] + points[3] * bu[1] +
                points[5] * bu[2] + points[7] * bu[3];
        }
        outX =
            rowX[0] * bv[0] + rowX[1] * bv[1] +
            rowX[2] * bv[2] + rowX[3] * bv[3];
        outY =
            rowY[0] * bv[0] + rowY[1] * bv[1] +
            rowY[2] * bv[2] + rowY[3] * bv[3];
    }

    struct ExternalMeshTransform {
        const float *controlPoints = nullptr;
        double invM11 = 0.0;
        double invM12 = 0.0;
        double invM21 = 0.0;
        double invM22 = 0.0;
        float invOffX = 0.0f;
        float invOffY = 0.0f;
    };

#if AETHERKIRI_MOTION_HAS_ARM_NEON
    __attribute__((always_inline)) inline void evaluateMotionBezierPatch4(
        const float *mesh,
        float32x4_t u,
        float32x4_t v,
        float32x4_t &outX,
        float32x4_t &outY) {
        const auto one = vdupq_n_f32(1.0f);
        const auto su = vsubq_f32(one, u);
        const auto sv = vsubq_f32(one, v);
        const auto su2 = vmulq_f32(su, su);
        const auto u2 = vmulq_f32(u, u);
        const auto sv2 = vmulq_f32(sv, sv);
        const auto v2 = vmulq_f32(v, v);
        const auto bu0 = vmulq_f32(su2, su);
        const auto bu1 = vmulq_n_f32(vmulq_f32(su2, u), 3.0f);
        const auto bu2 = vmulq_n_f32(vmulq_f32(su, u2), 3.0f);
        const auto bu3 = vmulq_f32(u2, u);
        const auto bv0 = vmulq_f32(sv2, sv);
        const auto bv1 = vmulq_n_f32(vmulq_f32(sv2, v), 3.0f);
        const auto bv2 = vmulq_n_f32(vmulq_f32(sv, v2), 3.0f);
        const auto bv3 = vmulq_f32(v2, v);

        float32x4_t rowX = vmulq_n_f32(bu0, mesh[0]);
        rowX = vmlaq_n_f32(rowX, bu1, mesh[2]);
        rowX = vmlaq_n_f32(rowX, bu2, mesh[4]);
        rowX = vmlaq_n_f32(rowX, bu3, mesh[6]);
        float32x4_t rowY = vmulq_n_f32(bu0, mesh[1]);
        rowY = vmlaq_n_f32(rowY, bu1, mesh[3]);
        rowY = vmlaq_n_f32(rowY, bu2, mesh[5]);
        rowY = vmlaq_n_f32(rowY, bu3, mesh[7]);
        outX = vmulq_f32(rowX, bv0);
        outY = vmulq_f32(rowY, bv0);

        rowX = vmulq_n_f32(bu0, mesh[8]);
        rowX = vmlaq_n_f32(rowX, bu1, mesh[10]);
        rowX = vmlaq_n_f32(rowX, bu2, mesh[12]);
        rowX = vmlaq_n_f32(rowX, bu3, mesh[14]);
        rowY = vmulq_n_f32(bu0, mesh[9]);
        rowY = vmlaq_n_f32(rowY, bu1, mesh[11]);
        rowY = vmlaq_n_f32(rowY, bu2, mesh[13]);
        rowY = vmlaq_n_f32(rowY, bu3, mesh[15]);
        outX = vmlaq_f32(outX, rowX, bv1);
        outY = vmlaq_f32(outY, rowY, bv1);

        rowX = vmulq_n_f32(bu0, mesh[16]);
        rowX = vmlaq_n_f32(rowX, bu1, mesh[18]);
        rowX = vmlaq_n_f32(rowX, bu2, mesh[20]);
        rowX = vmlaq_n_f32(rowX, bu3, mesh[22]);
        rowY = vmulq_n_f32(bu0, mesh[17]);
        rowY = vmlaq_n_f32(rowY, bu1, mesh[19]);
        rowY = vmlaq_n_f32(rowY, bu2, mesh[21]);
        rowY = vmlaq_n_f32(rowY, bu3, mesh[23]);
        outX = vmlaq_f32(outX, rowX, bv2);
        outY = vmlaq_f32(outY, rowY, bv2);

        rowX = vmulq_n_f32(bu0, mesh[24]);
        rowX = vmlaq_n_f32(rowX, bu1, mesh[26]);
        rowX = vmlaq_n_f32(rowX, bu2, mesh[28]);
        rowX = vmlaq_n_f32(rowX, bu3, mesh[30]);
        rowY = vmulq_n_f32(bu0, mesh[25]);
        rowY = vmlaq_n_f32(rowY, bu1, mesh[27]);
        rowY = vmlaq_n_f32(rowY, bu2, mesh[29]);
        rowY = vmlaq_n_f32(rowY, bu3, mesh[31]);
        outX = vmlaq_f32(outX, rowX, bv3);
        outY = vmlaq_f32(outY, rowY, bv3);
    }
#endif

    inline void deformExternalMeshPoint(
        float &displayX,
        float &displayY,
        const double *drawAffine,
        double inverseDeterminant,
        const ExternalMeshTransform *meshChain,
        std::size_t meshChainSize) {
        const double translatedX =
            static_cast<double>(displayX) - drawAffine[4];
        const double translatedY =
            static_cast<double>(displayY) - drawAffine[5];
        float modelX = static_cast<float>(
            (drawAffine[3] * translatedX -
             drawAffine[2] * translatedY) * inverseDeterminant);
        float modelY = static_cast<float>(
            (-drawAffine[1] * translatedX +
             drawAffine[0] * translatedY) * inverseDeterminant);
        for(std::size_t chainIndex = 0;
            chainIndex < meshChainSize; ++chainIndex) {
            const auto &transform = meshChain[chainIndex];
            const float x = modelX + transform.invOffX;
            const float y = modelY + transform.invOffY;
            const float u = static_cast<float>(
                transform.invM11 * x + transform.invM12 * y);
            const float v = static_cast<float>(
                transform.invM21 * x + transform.invM22 * y);
            evaluateMotionBezierPatch(
                transform.controlPoints, u, v, modelX, modelY);
        }
        displayX = static_cast<float>(
            drawAffine[0] * modelX + drawAffine[2] * modelY +
            drawAffine[4]);
        displayY = static_cast<float>(
            drawAffine[1] * modelX + drawAffine[3] * modelY +
            drawAffine[5]);
    }

    std::size_t deformExternalMeshPoints(
        float *interleavedPoints,
        std::size_t pointCount,
        const double *drawAffine,
        double inverseDeterminant,
        const ExternalMeshTransform *meshChain,
        std::size_t meshChainSize,
        bool allowArmNeon) {
        if(!interleavedPoints || !drawAffine || !meshChain ||
           meshChainSize == 0 || pointCount == 0) {
            return 0;
        }

        std::size_t pointIndex = 0;
        std::size_t vectorBatchCount = 0;
#if AETHERKIRI_MOTION_HAS_ARM_NEON
        if(allowArmNeon) {
            alignas(16) float modelX[4];
            alignas(16) float modelY[4];
            alignas(16) float patchU[4];
            alignas(16) float patchV[4];
            for(; pointIndex + 4 <= pointCount; pointIndex += 4) {
                for(std::size_t lane = 0; lane < 4; ++lane) {
                    const std::size_t offset = (pointIndex + lane) * 2;
                    const double translatedX =
                        static_cast<double>(interleavedPoints[offset]) -
                        drawAffine[4];
                    const double translatedY =
                        static_cast<double>(interleavedPoints[offset + 1]) -
                        drawAffine[5];
                    modelX[lane] = static_cast<float>(
                        (drawAffine[3] * translatedX -
                         drawAffine[2] * translatedY) *
                        inverseDeterminant);
                    modelY[lane] = static_cast<float>(
                        (-drawAffine[1] * translatedX +
                         drawAffine[0] * translatedY) *
                        inverseDeterminant);
                }

                for(std::size_t chainIndex = 0;
                    chainIndex < meshChainSize; ++chainIndex) {
                    const auto &transform = meshChain[chainIndex];
                    for(std::size_t lane = 0; lane < 4; ++lane) {
                        const float x = modelX[lane] + transform.invOffX;
                        const float y = modelY[lane] + transform.invOffY;
                        patchU[lane] = static_cast<float>(
                            transform.invM11 * x +
                            transform.invM12 * y);
                        patchV[lane] = static_cast<float>(
                            transform.invM21 * x +
                            transform.invM22 * y);
                    }
                    float32x4_t nextModelX;
                    float32x4_t nextModelY;
                    evaluateMotionBezierPatch4(
                        transform.controlPoints,
                        vld1q_f32(patchU), vld1q_f32(patchV),
                        nextModelX, nextModelY);
                    vst1q_f32(modelX, nextModelX);
                    vst1q_f32(modelY, nextModelY);
                }

                for(std::size_t lane = 0; lane < 4; ++lane) {
                    const std::size_t offset = (pointIndex + lane) * 2;
                    interleavedPoints[offset] = static_cast<float>(
                        drawAffine[0] * modelX[lane] +
                        drawAffine[2] * modelY[lane] + drawAffine[4]);
                    interleavedPoints[offset + 1] = static_cast<float>(
                        drawAffine[1] * modelX[lane] +
                        drawAffine[3] * modelY[lane] + drawAffine[5]);
                }
                ++vectorBatchCount;
            }
        }
#else
        (void)allowArmNeon;
#endif
        for(; pointIndex < pointCount; ++pointIndex) {
            const std::size_t offset = pointIndex * 2;
            deformExternalMeshPoint(
                interleavedPoints[offset], interleavedPoints[offset + 1],
                drawAffine, inverseDeterminant,
                meshChain, meshChainSize);
        }
        return vectorBatchCount;
    }

    inline bool motionUpdateDebugEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_DEBUG");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    inline bool emoteRootTraceEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_EMOTE_ROOT_TRACE");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    inline bool markMotionUpdateDebugLogged(const std::string &key) {
        static std::unordered_set<std::string> loggedKeys;
        return loggedKeys.insert(key).second;
    }

    inline void copyPackedColorsToBytes(
        uint8_t (&colorBytes)[16],
        const std::array<std::uint32_t, 4> &packedColors) {
        std::memcpy(colorBytes, packedColors.data(), sizeof(std::uint32_t) * 4u);
    }

    inline std::array<std::uint32_t, 4> copyPackedColorsFromBytes(
        const uint8_t (&colorBytes)[16]) {
        std::array<std::uint32_t, 4> packedColors{};
        std::memcpy(packedColors.data(), colorBytes,
                    sizeof(std::uint32_t) * packedColors.size());
        return packedColors;
    }

    inline std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    inline motion::detail::PlayerRuntime::MotionSourceMetadata
    resolveMotionSourceMetadata(
        motion::detail::PlayerRuntime &runtime,
        const motion::detail::MotionSnapshot &motion,
        const std::string &source) {
        const auto cacheKey = motion.path + '\n' + source;
        if(const auto it = runtime.motionSourceMetadataCache.find(cacheKey);
           it != runtime.motionSourceMetadataCache.end()) {
            return it->second;
        }

        motion::detail::PlayerRuntime::MotionSourceMetadata metadata;
        std::vector<std::uint8_t> unusedPixels;
        findPSBResourceBySourceName(
            motion, source, metadata.width, metadata.height, unusedPixels,
            metadata.originX, metadata.originY, nullptr, false);
        runtime.motionSourceMetadataCache.emplace(cacheKey, metadata);
        return metadata;
    }

    template <typename StateT>
    inline void populateTransformStateFromFrameState(
        StateT &localState,
        const motion::internal::FrameContentState &state) {
        localState.visible = state.visible;
        localState.active = state.visible;
        localState.flipX = state.flipX;
        localState.flipY = state.flipY;
        localState.posX = state.x;
        localState.posY = state.y;
        localState.posZ = state.z;
        localState.angle = state.angle;
        localState.scaleX = state.scaleX;
        localState.scaleY = state.scaleY;
        localState.slantX = state.slantX;
        localState.slantY = state.slantY;
        localState.opacity = static_cast<int>(
            std::clamp(state.opacity * 255.0, 0.0, 255.0));
        localState.blendMode = state.blendMode;
    }

    // Populate a ClipSlot from a FrameContentState.
    // Cannot be a ClipSlot method because FrameContentState is defined in
    // PlayerInternal.h (motion::internal namespace) which MotionNode.h cannot include.
    inline void populateSlotFromState(
        motion::detail::MotionNode::ClipSlot &slot,
        const motion::internal::FrameContentState &s) {
        slot.done = !s.visible;
        slot.src = s.src;
        slot.motionIcon = s.motionIcon;
        slot.srcList = s.srcList;
        slot.x = s.x; slot.y = s.y; slot.z = s.z;
        slot.ox = s.ox; slot.oy = s.oy;
        slot.width = s.width; slot.height = s.height;
        slot.opacity = s.opacity; slot.angle = s.angle;
        slot.scaleX = s.scaleX; slot.scaleY = s.scaleY;
        slot.slantX = s.slantX; slot.slantY = s.slantY;
        slot.flipX = s.flipX; slot.flipY = s.flipY;
        slot.blendMode = s.blendMode;
        slot.packedColors = s.packedColors;
        slot.ccc.x = s.ccc.x; slot.ccc.y = s.ccc.y;
        slot.acc.x = s.acc.x; slot.acc.y = s.acc.y;
        slot.zcc.x = s.zcc.x; slot.zcc.y = s.zcc.y;
        slot.scc.x = s.scc.x; slot.scc.y = s.scc.y;
        slot.occ.x = s.occ.x; slot.occ.y = s.occ.y;
        slot.cc.x = s.cc.x; slot.cc.y = s.cc.y;
        slot.cp.x = s.cp.x; slot.cp.y = s.cp.y; slot.cp.t = s.cp.t;
        slot.hasCpRotation = !s.cp.empty();
        slot.clipStartTime = s.clipStartTime;
        slot.motionDt = s.motionDt; slot.motionFlags = s.motionFlags;
        slot.motionDofst = s.motionDofst; slot.motionDocmpl = s.motionDocmpl;
        slot.motionTimeOffset = s.motionTimeOffset; slot.motionDtgt = s.motionDtgt;
        slot.prtTrigger = s.prtTrigger;
        slot.prtFmin = s.prtFmin; slot.prtF = s.prtF;
        slot.prtVmin = s.prtVmin; slot.prtV = s.prtV;
        slot.prtAmin = s.prtAmin; slot.prtA = s.prtA;
        slot.prtZmin = s.prtZmin; slot.prtZ = s.prtZ;
        slot.prtRange = s.prtRange;
        slot.hasTransformOrder = s.hasTransformOrder;
        std::copy(s.transformOrder, s.transformOrder + 4, slot.transformOrder);
        slot.action = s.action; slot.hasSync = s.hasSync;
        // hasEasing derived from acc curve presence
        slot.hasEasing = !s.acc.empty();
    }

    inline bool needsSlotRebind(
        const motion::detail::MotionNode::ClipSlot &slot,
        const motion::internal::FrameContentState &state,
        int nodeType) {
        const bool newDone = !state.visible;
        if(slot.done != newDone) {
            return true;
        }
        if(slot.src != state.src || slot.motionIcon != state.motionIcon ||
           slot.srcList != state.srcList) {
            return true;
        }

        if(nodeType == 3 || nodeType == 6) {
            return slot.motionDt != state.motionDt ||
                slot.motionFlags != state.motionFlags ||
                slot.motionDofst != state.motionDofst ||
                slot.motionDocmpl != state.motionDocmpl ||
                slot.motionTimeOffset != state.motionTimeOffset ||
                slot.motionDtgt != state.motionDtgt;
        }

        if(nodeType == 4) {
            return slot.prtTrigger != state.prtTrigger ||
                slot.prtFmin != state.prtFmin ||
                slot.prtF != state.prtF ||
                slot.prtVmin != state.prtVmin ||
                slot.prtV != state.prtV ||
                slot.prtAmin != state.prtAmin ||
                slot.prtA != state.prtA ||
                slot.prtZmin != state.prtZmin ||
                slot.prtZ != state.prtZ ||
                slot.prtRange != state.prtRange;
        }

        return false;
    }
} // anonymous namespace

namespace motion {

    namespace detail {
        std::size_t deformExternalMeshPointsForTesting(
            float *interleavedPoints,
            std::size_t pointCount,
            const double *drawAffine,
            const double *meshInverseMatrices,
            const float *meshInverseOffsets,
            const float *meshControlPoints,
            std::size_t meshChainSize,
            bool allowArmNeon) {
            if(!interleavedPoints || !drawAffine ||
               !meshInverseMatrices || !meshInverseOffsets ||
               !meshControlPoints || meshChainSize == 0) {
                return 0;
            }
            const double determinant =
                drawAffine[0] * drawAffine[3] -
                drawAffine[2] * drawAffine[1];
            if(std::fabs(determinant) <= 1e-12) {
                return 0;
            }

            std::vector<ExternalMeshTransform> transforms;
            transforms.reserve(meshChainSize);
            for(std::size_t index = 0; index < meshChainSize; ++index) {
                const double *matrix = meshInverseMatrices + index * 4;
                const float *offset = meshInverseOffsets + index * 2;
                transforms.push_back({
                    meshControlPoints + index * 32,
                    matrix[0], matrix[1], matrix[2], matrix[3],
                    offset[0], offset[1]
                });
            }
            return deformExternalMeshPoints(
                interleavedPoints, pointCount, drawAffine,
                1.0 / determinant, transforms.data(), transforms.size(),
                allowArmNeon);
        }
    }

    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        // === PHASE 1: Pre-loop setup ===

        // Camera velocity → root node position (0x6BB360..0x6BB42C)
        // In libkrkr2.so this modifies root node+1592/1600/1608 (posX/Y/Z) before
        // prevPos save. Applied here to root accumulated state.
        {
            auto &rootNode = nodes[0];
            if (_cameraVelocityX != 0.0)
                rootNode.localState.posX += _frameLastTime * _cameraVelocityX;
            if (_cameraVelocityY != 0.0)
                rootNode.localState.posY += _frameLastTime * _cameraVelocityY;
            if (_cameraVelocityZ != 0.0)
                rootNode.localState.posZ += _frameLastTime * _cameraVelocityZ;
            // Camera friction (0x6BB3E0..0x6BB428)
            if (_cameraDamping != 1.0 && _frameLastTime > 0.0) {
                const double dampFactor = std::pow(_cameraDamping,
                                                    _frameLastTime / 60.0);
                _cameraVelocityX *= dampFactor;
                _cameraVelocityY *= dampFactor;
                _cameraVelocityZ *= dampFactor;
            }
        }

        // Step 1: Save previous positions for delta calculation
        for (auto &n : nodes) {
            n.prevPosX = n.accumulated.posX;
            n.prevPosY = n.accumulated.posY;
            n.prevPosZ = n.accumulated.posZ;
        }

        // Step 2: Evaluate root node (index 0)
        auto &root = nodes[0];
        {
            FrameContentState rootState;
            if (root.psbNode) {
                rootState = evaluateLayerContent(root.psbNode, currentTime,
                                                 root.nodeType);
            } else {
                // Aligned to Player_buildNodeTree (0x6B51F0): node 0 is a
                // synthetic root, so it keeps the Player-set transform and a
                // neutral visible state instead of evaluating a PSB frameList.
                rootState.visible = true;
                rootState.opacity = 1.0;
                rootState.scaleX = 1.0;
                rootState.scaleY = 1.0;
                rootState.blendMode = 16;
            }
            // Populate root active clip slot
            populateSlotFromState(root.activeSlot(), rootState);
            root.currentFrameType = rootState.frameType;
            root.stencilType = root.stencilTypeBase;
            const double sourcePosX = root.localState.posX;
            const double sourcePosY = root.localState.posY;
            const double sourcePosZ = root.localState.posZ;
            const bool sourceFlipX = root.localState.flipX;
            populateTransformStateFromFrameState(root.localState, rootState);
            // Aligned to Player_updateLayers (0x6BB33C): root working state is
            // rebuilt by memcpy(root+0x5E0, root+0x630, 0x50), so preserve the
            // setter/camera-authored source block before refreshing defaults.
            root.localState.posX = sourcePosX;
            root.localState.posY = sourcePosY;
            root.localState.posZ = sourcePosZ;
            root.localState.flipX = sourceFlipX;
            // D3DEmotePlayer does not apply its outer scale/rotation in the
            // script-side affine matrix.  libgame's sub_673AC0 forwards the
            // scale animator to sub_6BE334 (synthetic root scale) and converts
            // the rotation from radians to degrees before writing the same
            // root source block.  Keep those wrapper controls on the outer
            // E-mote Player only; nested Motion players inherit the result.
            if(_runtime->isEmoteMode && !_motionParentPlayer) {
                root.localState.scaleX *= _emoteScaleState.value;
                root.localState.scaleY *= _emoteScaleState.value;
                root.localState.angle +=
                    _emoteRotState.value * 57.2957795130823208768;
            }
            root.localState.dirty = true;
            root.accumulated.visible = root.localState.visible;
            root.accumulated.flipX = root.localState.flipX;
            root.accumulated.flipY = root.localState.flipY;
            root.accumulated.posX = root.localState.posX;
            root.accumulated.posY = root.localState.posY;
            root.accumulated.posZ = root.localState.posZ;
            root.accumulated.angle = root.localState.angle;
            root.accumulated.scaleX = root.localState.scaleX;
            root.accumulated.scaleY = root.localState.scaleY;
            root.accumulated.slantX = root.localState.slantX;
            root.accumulated.slantY = root.localState.slantY;
            root.accumulated.opacity = root.localState.opacity;
            root.accumulated.blendMode = root.localState.blendMode;
            root.accumulated.active = root.localState.active;
            // Cache interpolated data for rendering
            root.interpolatedCache.src = rootState.src;
            root.interpolatedCache.width = rootState.width;
            root.interpolatedCache.height = rootState.height;
            root.interpolatedCache.opacity = rootState.opacity;
            root.interpolatedCache.x = rootState.x;
            root.interpolatedCache.y = rootState.y;
            root.interpolatedCache.z = rootState.z;
            root.interpolatedCache.ox = rootState.ox;
            root.interpolatedCache.oy = rootState.oy;
            root.interpolatedCache.angle = rootState.angle;
            root.interpolatedCache.scaleX = rootState.scaleX;
            root.interpolatedCache.scaleY = rootState.scaleY;
            root.interpolatedCache.slantX = rootState.slantX;
            root.interpolatedCache.slantY = rootState.slantY;
            root.interpolatedCache.flipX = rootState.flipX ^ _rootFlipX;
            root.interpolatedCache.flipY = rootState.flipY;
            root.interpolatedCache.blendMode = rootState.blendMode;
            root.interpolatedCache.packedColors = rootState.packedColors;
            copyPackedColorsToBytes(root.colorBytes, rootState.packedColors);
            root.interpolatedCache.hasTransformOrder = rootState.hasTransformOrder;
            if (rootState.hasTransformOrder) {
                std::copy(std::begin(rootState.transformOrder),
                          std::end(rootState.transformOrder),
                          root.interpolatedCache.transformOrder);
            }
            root.interpolatedCache.action = rootState.action;
            root.interpolatedCache.hasSync = rootState.hasSync;
            root.interpolatedCache.prtTrigger = rootState.prtTrigger;
            root.interpolatedCache.prtF = rootState.prtF;
            root.interpolatedCache.prtV = rootState.prtV;
            root.interpolatedCache.prtA = rootState.prtA;
            root.interpolatedCache.prtZ = rootState.prtZ;
            root.interpolatedCache.prtRange = rootState.prtRange;

            // Populate root clipW/clipH/originX/originY from PSB icon.
            // Aligned to sub_6BC4F0: node+232/240 = PSB icon pixel dimensions.
            if (!rootState.src.empty() && _runtime->activeMotion) {
                const auto metadata = resolveMotionSourceMetadata(
                    *_runtime, *_runtime->activeMotion, rootState.src);
                root.clipW = metadata.width;
                root.clipH = metadata.height;
                root.originX = metadata.originX;
                root.originY = metadata.originY;
            }

            // Step 3: Build root local 2x2 matrix via sub_699940
            // Reuse applyLocalTransform logic but on raw 2x2
            Affine2x3 rootAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            applyLocalTransform(rootAffine, root);
            root.accumulated.m11 = rootAffine[0];
            root.accumulated.m21 = rootAffine[1];
            root.accumulated.m12 = rootAffine[2];
            root.accumulated.m22 = rootAffine[3];
        }

        // --- sub_6BBE20: Variable interpolation (pre-loop) ---
        // Aligned to 0x6BBE20. Interpolates variable values and binds to nodes.
        // In libkrkr2.so this operates on a 160-byte item deque (player+1312).
        // Each variable is interpolated then bound to nodes via sub_6C4668.
        //
        // sub_6C4668 binding: resolves variable name to a source entry in
        // player+264 map, then updates child Player timeline parameters for
        // nodeType=3 and nodeType=4 nodes. In our architecture, variable values
        // are stored in _variableValues and exposed via getVariable()/setVariable()
        // TJS API. The binding to child Players happens implicitly when child
        // Players re-evaluate their timelines.
        if (_runtime->activeMotion) {
            const auto &varFrames = _runtime->activeMotion->variableFrames;
            for (const auto &[label, frames] : varFrames) {
                if (frames.empty()) continue;
                // User-set value takes precedence
                if (_variableValues.find(label) != _variableValues.end()) continue;
                // E-mote's variableList is a labelled range table, not an
                // initial-pose list.  Controllers start from the neutral
                // scalar (0); for example head_UD is authored as -30,0,30
                // and choosing the first entry tears the character into its
                // extreme deformation layers.  Ordinary Motion selector
                // parameters keep their authored first-frame default.
                _variableValues[label] = _runtime->isEmoteMode
                    ? 0.0
                    : frames.front().value;
            }
            // Controller state and its evaluated output are distinct in
            // libgame. Auto blink, clamp and timeline blending write the
            // latter without changing the expression animator's base value.
            // Pass the evaluated value down the motion ownership tree for
            // this frame, falling back to the persistent base variable.
            const auto effectiveVariableGeneration =
                _runtime->beginEffectiveVariableScratch();
            for(const auto &[label, value] : _variableValues) {
                _runtime->setEffectiveVariableScratch(label, value);
            }
            for(const auto &[label, value] : _evalResultValues) {
                _runtime->setEffectiveVariableScratch(label, value);
            }
            // sub_6C4668 forwards the owning Player's already-evaluated
            // controller output into nested Motion Players.  It does not call
            // Player::setVariable on the child: doing so routes hair/bust/
            // parts bindings (types 0..2) back into their controller groups,
            // where the generic value is intentionally ignored.  Keep the
            // inherited value as a distinct, per-frame input and let it win
            // over the child's neutral controller state.  This is required
            // for E-mote mesh physics, waiting-loop body motion, and
            // voice-driven face_talk to reach the image leaves.
            for(const auto &[label, value] :
                _runtime->inheritedVariableInputs) {
                _runtime->setEffectiveVariableScratch(label, value);
            }
            const auto propagateInheritedVariable =
                [](Player *child, const std::string &label, double value) {
                    if(!child || !child->_runtime) {
                        return;
                    }
                    auto [it, inserted] =
                        child->_runtime->inheritedVariableInputs.try_emplace(
                            label, value);
                    if(!inserted && it->second == value) {
                        return;
                    }
                    it->second = value;
                    child->_layersDirty = true;
                    child->_emoteDirty = true;
                };
            // Bind path-qualified variables to child Players (sub_6C4668
            // equivalent).  Yuzu addresses nested selectors with labels such
            // as `bt_start/select` and `slot01/base/disable`.  Each path
            // segment names a motion node; transparent wrapper motions keep
            // the full path until the player containing that segment is
            // reached, then only the remaining suffix belongs to the selected
            // child. This mirrors the native recursive parameter binder while
            // preserving path scope between sibling controls.
            for(auto &[label, scratch] :
                _runtime->effectiveVariableScratch) {
                if(scratch.generation != effectiveVariableGeneration) {
                    continue;
                }
                const auto slash = label.find('/');
                if(slash == std::string::npos) {
                    continue;
                }
                bool found = false;
                for(const auto &candidate : nodes) {
                    if(candidate.nodeType == 3 &&
                       candidate.layerName.size() == slash &&
                       label.compare(0, slash, candidate.layerName) == 0) {
                        found = true;
                        break;
                    }
                }
                scratch.hasRoutingNode = found;
            }
            for (auto &vn : nodes) {
                if (vn.nodeType == 3) {
                    if (auto *cp = vn.getChildPlayer()) {
                        const auto prefixSize = vn.layerName.size();
                        for(const auto &[label, scratch] :
                            _runtime->effectiveVariableScratch) {
                            if(scratch.generation !=
                               effectiveVariableGeneration) {
                                continue;
                            }
                            const auto value = scratch.value;
                            if(!vn.layerName.empty() &&
                               label.size() > prefixSize + 1 &&
                               label.compare(0, prefixSize,
                                             vn.layerName) == 0 &&
                               label[prefixSize] == '/') {
                                propagateInheritedVariable(
                                    cp, label.substr(prefixSize + 1), value);
                                continue;
                            }

                            if(label.find('/') == std::string::npos) {
                                propagateInheritedVariable(cp, label, value);
                            } else if(!scratch.hasRoutingNode) {
                                propagateInheritedVariable(cp, label, value);
                            }
                        }
                    }
                } else if (vn.nodeType == 4) {
                    for (int pi2 = 0; pi2 < vn.getParticleCount(); ++pi2) {
                        if (auto *cp = vn.getParticleChild(pi2)) {
                            for(const auto &[label, scratch] :
                                _runtime->effectiveVariableScratch) {
                                if(scratch.generation ==
                                   effectiveVariableGeneration) {
                                    propagateInheritedVariable(
                                        cp, label, scratch.value);
                                }
                            }
                        }
                    }
                }
            }
        }

    }

    // Phase 2: Main node evaluation loop (non-root nodes)
    void Player::updateLayersPhase2_MainLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        const std::string motionPath = _runtime->activeMotion
            ? _runtime->activeMotion->path
            : std::string();
        const auto *activeClip = selectActiveClip();
        const bool traceFrameSelection =
            detail::logoChainTraceEnabled(_runtime->activeMotion);
        const auto resolveMotionVariable =
            [this](const std::string &label, double fallback) {
                size_t ownerDepth = 0;
                for(const Player *owner = this;
                    owner && ownerDepth++ < 32;
                    owner = owner->_motionParentPlayer) {
                    if(owner->_runtime) {
                        if(const auto it =
                               owner->_runtime->inheritedVariableInputs.find(
                                   label);
                           it != owner->_runtime
                                     ->inheritedVariableInputs.end()) {
                            return it->second;
                        }
                    }
                    if(const auto it = owner->_evalResultValues.find(label);
                       it != owner->_evalResultValues.end()) {
                        return it->second;
                    }
                    if(const auto it = owner->_variableValues.find(label);
                       it != owner->_variableValues.end()) {
                        return it->second;
                    }
                }
                return fallback;
            };
        // === PHASE 2: Main loop — evaluate non-root nodes ===
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find parent node — walk parentIndex chain while the parent's
            // inheritMask carries the byte(node+42)&0x40 gate used by
            // Player_updateLayers at 0x6BB598..0x6BB5BC.
            int parentIdx = node.parentIndex;
            while (parentIdx > 0 && parentIdx < static_cast<int>(nodes.size())) {
                if ((nodes[parentIdx].inheritFlags & 0x00400000) == 0) break;
                parentIdx = nodes[parentIdx].parentIndex;
            }
            if (parentIdx < 0 || parentIdx >= static_cast<int>(nodes.size()))
                parentIdx = 0;
            const auto &parent = nodes[parentIdx];

            double nodeTime = currentTime;
            int parameterIndex = node.parameterizeIndex;
            if(parameterIndex < 0 && activeClip) {
                parameterIndex = activeClip->defaultParameterIndex;
            }
            if(parameterIndex >= 0) {
                if(activeClip && static_cast<size_t>(parameterIndex) <
                               activeClip->parameters.size()) {
                    const auto &parameter =
                        activeClip->parameters[
                            static_cast<size_t>(parameterIndex)];
                    // Motion nodes form nested Players, while scripts set
                    // variables on the owning root Player. The native binder
                    // resolves an unqualified parameter through that owner
                    // chain. Child Players can be instantiated after the
                    // parent's variable propagation pass, so relying only on
                    // a copied child map leaves their parameter at rangeBegin
                    // (for example `chapter=45` rendered as 0-0). Resolve the
                    // nearest authored value directly through the ancestry as
                    // well; path-qualified sibling selectors remain isolated
                    // because their leaf parameter name does not match at the
                    // root.
                    const double rawValue = resolveMotionVariable(
                        parameter.id, parameter.rangeBegin);
                    nodeTime = detail::parameterizedClipTime(
                        *activeClip, parameter, rawValue);
                }
            }

            // Evaluate this node's interpolated state.
            auto state = evaluateLayerContent(node.psbNode, nodeTime,
                                              node.nodeType,
                                              traceFrameSelection);
            if(!node.meshCombinators.empty()) {
                if(evaluateMeshCombinators(
                       node.meshCombinators, resolveMotionVariable,
                       node.meshControlPoints)) {
                    state.hasMeshPayload = true;
                    state.meshControlPoints = node.meshControlPoints;
                }
            }
            if(traceFrameSelection && state.debugEvaluated) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.framesel", "0x6926B4/0x699AE4",
                    nodeTime,
                    "nodeIndex={} label={} type={} activeIndex={} nextIndex={} frameA[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] frameB[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] t={:.6f} interpolated={} final[src={},opacity={:.6f},scale=({:.6f},{:.6f})]",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType,
                    state.debugActiveIndex,
                    state.debugNextIndex,
                    state.debugFrameATime,
                    state.debugFrameAType,
                    state.debugFrameAInvisible ? 1 : 0,
                    state.debugFrameASrc.empty() ? std::string("<none>")
                                                : state.debugFrameASrc,
                    state.debugFrameAOpacity,
                    state.debugFrameAScaleX,
                    state.debugFrameAScaleY,
                    state.debugFrameBTime,
                    state.debugFrameBType,
                    state.debugFrameBInvisible ? 1 : 0,
                    state.debugFrameBSrc.empty() ? std::string("<none>")
                                                : state.debugFrameBSrc,
                    state.debugFrameBOpacity,
                    state.debugFrameBScaleX,
                    state.debugFrameBScaleY,
                    state.debugInterpT,
                    state.debugInterpolated ? 1 : 0,
                    state.src.empty() ? std::string("<none>") : state.src,
                    state.opacity,
                    state.scaleX,
                    state.scaleY);
            }
            node.currentFrameType = state.frameType;
            // sub_6B1058 initializes node+52 from the authored stencilType,
            // and sub_6BF714 copies it unchanged to render-item+244.  The
            // frame-list type belongs to the active slot; mixing it into the
            // stencil operation changes normal eye cropping (1) into reverse
            // cropping (2).
            node.stencilType = node.stencilTypeBase;

            // Cache interpolated data for rendering
            const bool sourceChanged =
                node.interpolatedCache.src != state.src;
            node.interpolatedCache.src = state.src;
            node.interpolatedCache.srcList = state.srcList;
            node.interpolatedCache.width = state.width;
            node.interpolatedCache.height = state.height;
            node.interpolatedCache.opacity = state.opacity;
            node.interpolatedCache.x = state.x;
            node.interpolatedCache.y = state.y;
            node.interpolatedCache.z = state.z;
            node.interpolatedCache.ox = state.ox;
            node.interpolatedCache.oy = state.oy;
            node.interpolatedCache.angle = state.angle;
            node.interpolatedCache.scaleX = state.scaleX;
            node.interpolatedCache.scaleY = state.scaleY;
            node.interpolatedCache.slantX = state.slantX;
            node.interpolatedCache.slantY = state.slantY;
            node.interpolatedCache.flipX = state.flipX;
            node.interpolatedCache.flipY = state.flipY;
            node.interpolatedCache.blendMode = state.blendMode;
            node.interpolatedCache.packedColors = state.packedColors;
            copyPackedColorsToBytes(node.colorBytes, state.packedColors);
            node.interpolatedCache.hasTransformOrder = state.hasTransformOrder;
            if (state.hasTransformOrder) {
                std::copy(std::begin(state.transformOrder),
                          std::end(state.transformOrder),
                          node.interpolatedCache.transformOrder);
            }
            node.interpolatedCache.action = state.action;
            node.interpolatedCache.hasSync = state.hasSync;
            // Motion sub-object data from FrameContentState (mask 0x80000)
            node.interpolatedCache.motionDt = state.motionDt;
            node.interpolatedCache.motionFlags = state.motionFlags;
            node.interpolatedCache.motionDofst = state.motionDofst;
            node.interpolatedCache.motionDocmpl = state.motionDocmpl;
            node.interpolatedCache.motionTimeOffset = state.motionTimeOffset;
            node.interpolatedCache.clipStartTime = state.clipStartTime;
            node.interpolatedCache.motionDtgt = state.motionDtgt;
            // Particle data from FrameContentState (mask 0x100000)
            node.interpolatedCache.prtTrigger = state.prtTrigger;
            node.interpolatedCache.prtF = state.prtF;
            node.interpolatedCache.prtV = state.prtV;
            node.interpolatedCache.prtA = state.prtA;
            node.interpolatedCache.prtZ = state.prtZ;
            node.interpolatedCache.prtRange = state.prtRange;
            node.prtTrigger = state.prtTrigger;
            if(node.meshType == 1 && state.visible) {
                if(state.meshControlPoints.size() == 32) {
                    node.meshControlPoints = state.meshControlPoints;
                } else {
                    const auto &identity =
                        motion::internal::identityMeshControlPoints();
                    node.meshControlPoints.assign(identity.begin(),
                                                  identity.end());
                }
            } else {
                node.meshControlPoints.clear();
                node.meshRenderPoints.clear();
                node.meshWorldControlPoints.clear();
            }
            // Crossfade easing now stored in ClipSlot via populateSlotFromState.
            // Position easing (ccc) and rotation (cp) for sub_69A4D4 context
            node.interpolatedCache.ccc_x = state.ccc.x;
            node.interpolatedCache.ccc_y = state.ccc.y;
            node.interpolatedCache.cp_x = state.cp.x;
            node.interpolatedCache.cp_y = state.cp.y;
            node.interpolatedCache.cp_t = state.cp.t;
            node.interpolatedCache.hasCpRotation = !state.cp.empty();


            // Populate clipW/clipH and originX/originY from PSB source icon.
            // Aligned to sub_6BC4F0 at 0x6BCB14: node+232/240 = PSB icon
            // pixel dimensions (not state.width/height which are unused).
            // findPSBResourceBySourceName navigates source/<group>/icon/<name>
            // and reads width, height, originX, originY from the icon node.
            if (!state.src.empty() && _runtime->activeMotion &&
                (sourceChanged || node.clipW <= 0.0 || node.clipH <= 0.0)) {
                const auto metadata = resolveMotionSourceMetadata(
                    *_runtime, *_runtime->activeMotion, state.src);
                node.clipW = metadata.width;
                node.clipH = metadata.height;
                node.originX = metadata.originX;
                node.originY = metadata.originY;
            }

            // Populate active clip slot from evaluated state
            if(needsSlotRebind(node.activeSlot(), state, node.nodeType)) {
                node.flags |= 0x01;
            }
            populateSlotFromState(node.activeSlot(), state);
            populateTransformStateFromFrameState(node.accumulated, state);
            node.accumulated.dirty = true;

            // Type 2 is a structural transform group. Several classic KAG
            // motions deliberately give the group itself only type=0 frames
            // while animating its children (for example Extra/CG's content
            // grid). The native player still carries the parent transform
            // through that group; skipping here leaves every child in the
            // motion's local coordinate space.
            const bool structuralTransformGroup =
                !state.visible && node.nodeType == 2;
            if (!state.visible && !structuralTransformGroup) {
                node.accumulated.visible = false;
                node.accumulated.active = false;
                node.accumulated.opacity = 0;
                node.drawFlag = false;
                continue;
            }

            // === Inheritance from parent ===
            // Aligned to libkrkr2.so 0x6BB630..0x6BBB6C (Player_updateLayers main loop)
            // Full inheritFlags system with 3-phase independentLayerInherit support.
            node.accumulated.visible = true;
            // Native node activity is hierarchical.  A visible keyframe does
            // not reactivate a descendant whose parameterized parent selected
            // an invisible branch.  This distinction is essential for
            // E-mote: its multidimensional deformation tree contains many
            // copies of the same artwork below mutually-exclusive parameter
            // branches.  Treating every visible leaf as independently active
            // draws all samples on top of each other (dark clothing) and lets
            // later body samples cover the head entirely.
            node.accumulated.active = parent.accumulated.active;
            // First-stage composition uses the node's own override/source block
            // (+0x630..+0x678) to modify the evaluated working block
            // (+0x5E0..+0x628), matching 0x6BB630..0x6BB700.
            node.accumulated.flipX ^= node.localState.flipX;
            node.accumulated.flipY ^= node.localState.flipY;
            node.accumulated.angle += node.localState.angle;
            node.accumulated.scaleX *= node.localState.scaleX;
            node.accumulated.scaleY *= node.localState.scaleY;
            node.accumulated.slantX += node.localState.slantX;
            node.accumulated.slantY += node.localState.slantY;
            node.accumulated.opacity =
                node.accumulated.opacity * node.localState.opacity / 255;
            node.accumulated.posX += node.localState.posX;
            node.accumulated.posY += node.localState.posY;
            node.accumulated.posZ += node.localState.posZ;

            // sub_69AE74: Mesh position deformation (0x6BB714)
            // Aligned to 0x69AE74. Called when parent.meshType != 0.
            // Deforms child position based on parent mesh surface.
            // Condition: parent.meshType==1 && (parent.meshFlags & 1) &&
            //            child.active && child.hasSource && parent has mesh vertices.
            if (parent.meshType == 1 && (parent.meshFlags & 1) != 0
                && node.accumulated.active && node.hasSource) {
                // Normalize child position by parent clip dimensions (0x69AF24..0x69AF50)
                const double pw = parent.clipW > 0.0 ? parent.clipW : 1.0;
                const double ph = parent.clipH > 0.0 ? parent.clipH : 1.0;
                const double normX = (node.accumulated.posX + parent.originX) / pw;
                const double normY = (node.accumulated.posY + parent.originY) / ph;

                // Evaluate at normalized coordinates
                float defX = static_cast<float>(normX);
                float defY = static_cast<float>(normY);
                // Evaluate mesh at normalized coordinates using parent's mesh data.
                // parent.meshControlPoints populated by sub_6BC4F0 vertex computation.
                if (parent.meshControlPoints.size() >= 32) {
                    // 16-point Bezier patch: evaluate via sub_6990A0
                    evaluateMotionBezierPatch(parent.meshControlPoints.data(),
                                              defX, defY, defX, defY);
                }
                node.accumulated.posX = static_cast<double>(defX) * pw - parent.originX;
                node.accumulated.posY = static_cast<double>(defY) * ph - parent.originY;

                // Angle deformation from mesh gradient (0x69AFB4..0x69B0EC)
                if ((parent.meshFlags & 2) != 0
                    && (node.inheritFlags & 0x10) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    // Sample at 4 nearby points (0x69B030..0x69B094)
                    evaluateMotionBezierPatch(
                        mp, defX - eps, defY, x1, y1);
                    evaluateMotionBezierPatch(
                        mp, defX + eps, defY, x2, y2);
                    evaluateMotionBezierPatch(
                        mp, defX, defY - eps, x3, y3);
                    evaluateMotionBezierPatch(
                        mp, defX, defY + eps, x4, y4);
                    // Average of two orthogonal gradients (0x69B0AC..0x69B0EC)
                    double a1 = std::atan2(
                        static_cast<double>(y3 - y4),
                        static_cast<double>(x4 - x3));
                    double a2 = std::atan2(
                        static_cast<double>(x2 - x1),
                        static_cast<double>(y2 - y1));
                    node.accumulated.angle += (a1 + a2) * 0.5 * 360.0 / 6.28318531;
                }

                // Scale deformation from mesh jacobian (0x69B11C..0x69B1A8)
                if ((parent.meshFlags & 4) != 0
                    && (node.inheritFlags & 0x60) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    evaluateMotionBezierPatch(
                        mp, defX - eps, defY, x1, y1);
                    evaluateMotionBezierPatch(
                        mp, defX + eps, defY, x2, y2);
                    evaluateMotionBezierPatch(
                        mp, defX, defY - eps, x3, y3);
                    evaluateMotionBezierPatch(
                        mp, defX, defY + eps, x4, y4);
                    // Jacobian area from cross product (0x69B154..0x69B188)
                    double dx1 = x2 - x1, dy1 = y2 - y1;
                    double dx2 = x3 - x4, dy2 = y3 - y4;
                    double area1 = std::fabs(dx1 * (y4 - y1) - dy1 * (x4 - x1)) * 0.5;
                    double area2 = std::fabs(dx1 * (y3 - y1) - dy1 * (x3 - x1)) * 0.5;
                    double scaleFactor = std::sqrt(area1 + area2 + area2 + area1) / 0.0002;
                    if (node.inheritFlags & 0x020)
                        node.accumulated.scaleX *= scaleFactor;
                    if (node.inheritFlags & 0x040)
                        node.accumulated.scaleY *= scaleFactor;
                }
            }

            // Position transform happens after the parent-local pre-add and mesh
            // deformation. The branch key is parent.coordinateMode (0x6BB718).
            {
                const double localX = node.accumulated.posX;
                const double localY = node.accumulated.posY;
                const double localZ = node.accumulated.posZ;
                if (parent.coordinateMode != 0) {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localZ;
                    const double worldZ = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localZ;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = localY + parent.accumulated.posY;
                    node.accumulated.posZ = worldZ + parent.accumulated.posZ;
                } else {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localY;
                    const double worldY = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localY;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = worldY + parent.accumulated.posY;
                    node.accumulated.posZ = localZ + parent.accumulated.posZ;
                }
            }

            // sub_6BAA10: Ground correction TJS callback (0x6BB7F8)
            // Aligned to 0x6BAA10. Called when node+47 (groundCorrection) set.
            // Invokes TJS onGroundCorrection(parentPos, childPos) callback on
            // the node's TJS object. The callback can modify child position.
            // In libkrkr2.so, the TJS object is at *(node+0)+16 (the layer's
            // iTJSDispatch2 reference). In our architecture, MotionNode doesn't
            // hold a TJS dispatch pointer. This callback is used for specialized
            // ground-plane correction in E-mote animations.
            if (node.groundCorrection && node.tjsLayerObject) {
                auto *tjsObj = static_cast<iTJSDispatch2 *>(node.tjsLayerObject);
                // Aligned to sub_6BAA10 (0x6BAA10): invoke TJS onGroundCorrection.
                // Push parent pos [posX,posY,posZ] and child pos as TJS arrays,
                // call onGroundCorrection, read back corrected child position.
                try {
                    // Create parent position array
                    iTJSDispatch2 *parentArr = TJSCreateArrayObject();
                    tTJSVariant pxv(parent.accumulated.posX);
                    tTJSVariant pyv(parent.accumulated.posY);
                    tTJSVariant pzv(parent.accumulated.posZ);
                    tTJSVariant *pargs[] = { &pxv };
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pyv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pzv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);

                    // Create child position array
                    iTJSDispatch2 *childArr = TJSCreateArrayObject();
                    tTJSVariant cxv(node.accumulated.posX);
                    tTJSVariant cyv(node.accumulated.posY);
                    tTJSVariant czv(node.accumulated.posZ);
                    tTJSVariant *cargs[] = { &cxv };
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &cyv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &czv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);

                    // Call onGroundCorrection(parentPos, childPos)
                    tTJSVariant parentVar(parentArr, parentArr);
                    tTJSVariant childVar(childArr, childArr);
                    tTJSVariant *callArgs[] = { &parentVar, &childVar };
                    tTJSVariant result;
                    tjsObj->FuncCall(0, TJS_W("onGroundCorrection"),
                        nullptr, &result, 2, callArgs, tjsObj);

                    // Read back corrected position from result (0x6BAD48..0x6BAE00)
                    if (result.Type() == tvtObject) {
                        iTJSDispatch2 *resObj = result.AsObjectNoAddRef();
                        if (resObj) {
                            tTJSVariant rv;
                            tTJSVariant idx;
                            idx = 0; resObj->PropGetByNum(0, 0, &rv, resObj);
                            node.accumulated.posX = static_cast<double>(rv);
                            idx = 1; resObj->PropGetByNum(0, 1, &rv, resObj);
                            node.accumulated.posY = static_cast<double>(rv);
                            idx = 2; resObj->PropGetByNum(0, 2, &rv, resObj);
                            node.accumulated.posZ = static_cast<double>(rv);
                        }
                    }
                    parentArr->Release();
                    childArr->Release();
                } catch (...) {
                    // TJS callback failure — silently ignore
                }
            }

            // Opacity conditional second multiply (0x6BB808..0x6BB830):
            // Decompilation: if ((v46 & 0x400) != 0 || (v47 = v3, !*(a1+1097)))
            //   node.opacity = v47.opacity * node.opacity / 255
            // v47 = parent when 0x400 set; v47 = root (v3) when !independentLayerInherit
            {
                const auto *opaNode = &parent;
                if ((node.inheritFlags & 0x400) == 0 && _independentLayerInherit) {
                    // Neither 0x400 set nor independentLayerInherit=false: skip
                    // (no second multiply in this case)
                } else {
                    if ((node.inheritFlags & 0x400) != 0)
                        opaNode = &parent;
                    else
                        opaNode = &nodes[0];  // root
                    node.accumulated.opacity = opaNode->accumulated.opacity
                        * node.accumulated.opacity / 255;
                }
            }

            // === inheritFlags per-property control (0x6BB83C) ===
            // Decompilation evidence: Player_updateLayers 0x6BB83C..0x6BBB6C
            //   if ((~v46 & 0x1FC) == 0) → all bits set, simple path
            //   else:
            //     per-property inherit from parent for SET bits
            //     if (player+1097) → LABEL_68: sub_699940 only, NO matrix multiply
            //     else → LABEL_76: root undo → sub_699940 → root re-apply → matrix multiply
            const int flags = node.inheritFlags;
            const bool allInheritBitsSet = (~flags & 0x1FC) == 0;

            if (allInheritBitsSet) {
                // All bits set → simple path (0x6BB848): inherit from parent,
                // sub_699940, matrix multiply. Already inherited above.
                Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(localAffine, node);
                const double lm11 = localAffine[0], lm21 = localAffine[1];
                const double lm12 = localAffine[2], lm22 = localAffine[3];
                node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
            } else {
                // Some bits NOT set: per-property inherit from parent for SET bits only
                // (0x6BB8F4..0x6BB918)
                if (flags & 0x004) node.accumulated.flipX = state.flipX ^ parent.accumulated.flipX;
                else               node.accumulated.flipX = state.flipX;
                if (flags & 0x008) node.accumulated.flipY = state.flipY ^ parent.accumulated.flipY;
                else               node.accumulated.flipY = state.flipY;
                if (flags & 0x010) node.accumulated.angle = state.angle + parent.accumulated.angle;
                else               node.accumulated.angle = state.angle;
                if (flags & 0x020) node.accumulated.scaleX = state.scaleX * parent.accumulated.scaleX;
                else               node.accumulated.scaleX = state.scaleX;
                if (flags & 0x040) node.accumulated.scaleY = state.scaleY * parent.accumulated.scaleY;
                else               node.accumulated.scaleY = state.scaleY;
                if (flags & 0x080) node.accumulated.slantX = state.slantX + parent.accumulated.slantX;
                else               node.accumulated.slantX = state.slantX;
                if (flags & 0x100) node.accumulated.slantY = state.slantY + parent.accumulated.slantY;
                else               node.accumulated.slantY = state.slantY;

                if (_independentLayerInherit) {
                    // LABEL_68 (0x6BB918): independentLayerInherit=TRUE
                    // Only sub_699940, NO matrix multiply with parent.
                    // Node's matrix stays as its own local matrix (independent of parent).
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);
                    node.accumulated.m11 = localAffine[0];
                    node.accumulated.m21 = localAffine[1];
                    node.accumulated.m12 = localAffine[2];
                    node.accumulated.m22 = localAffine[3];
                } else {
                    // LABEL_76 (0x6BB9BC..0x6BBB6C): independentLayerInherit=FALSE
                    // 4-phase: undo root → sub_699940 → re-apply root → matrix multiply
                    const auto &rootNode = nodes[0];

                    // Phase A: For SET bits, UNDO root contribution (0x6BB9BC)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle -= rootNode.accumulated.angle;
                    if (flags & 0x020 && rootNode.accumulated.scaleX != 0.0)
                        node.accumulated.scaleX /= rootNode.accumulated.scaleX;
                    if (flags & 0x040 && rootNode.accumulated.scaleY != 0.0)
                        node.accumulated.scaleY /= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX -= rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY -= rootNode.accumulated.slantY;

                    // Phase B: sub_699940 (0x6BB9E8)
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);

                    // Phase C: For SET bits, RE-APPLY root contribution (0x6BBA04)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle += rootNode.accumulated.angle;
                    if (flags & 0x020) node.accumulated.scaleX *= rootNode.accumulated.scaleX;
                    if (flags & 0x040) node.accumulated.scaleY *= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX += rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY += rootNode.accumulated.slantY;

                    // Phase D: Matrix multiply parent × local (0x6BBA24).
                    // M2's glyph nodes intentionally omit both scale bits:
                    // their positions follow the 2x parent transform, but
                    // their image geometry stays at its authored size. The
                    // position was already transformed above, so remove only
                    // the omitted orthogonal scale from the geometry matrix.
                    Affine2x3 parentGeometryAffine = {
                        parent.accumulated.m11,
                        parent.accumulated.m21,
                        parent.accumulated.m12,
                        parent.accumulated.m22,
                        0.0,
                        0.0
                    };
                    parentGeometryAffine =
                        startupLogoGeometryParentTransform(
                            motionPath, parentGeometryAffine, flags);
                    const double lm11 = localAffine[0], lm21 = localAffine[1];
                    const double lm12 = localAffine[2], lm22 = localAffine[3];
                    node.accumulated.m11 =
                        parentGeometryAffine[0] * lm11 +
                        parentGeometryAffine[2] * lm21;
                    node.accumulated.m21 =
                        parentGeometryAffine[1] * lm11 +
                        parentGeometryAffine[3] * lm21;
                    node.accumulated.m12 =
                        parentGeometryAffine[0] * lm12 +
                        parentGeometryAffine[2] * lm22;
                    node.accumulated.m22 =
                        parentGeometryAffine[1] * lm12 +
                        parentGeometryAffine[3] * lm22;
                }
            }
        }

    }

    void Player::updateLayersPhase3_CameraConstraint() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC000: Camera constraint (nodeType=9) ---
        // Aligned to 0x6BC000..0x6BC4EC. Only when !isEmoteMode.
        // 9 cases at 0x6BC1B0..0x6BC358 based on flipX/flipY + constraintType (node+2376).
        if (!_runtime->isEmoteMode && nodes.size() >= 2) {
            double offsetX = 0, offsetY = 0, offsetZ = 0;
            // Track which axes have constraints and their types
            bool hasMinX = false, hasMaxX = false, hasTrackX = false;
            bool hasMinY = false, hasMaxY = false, hasTrackY = false;
            bool hasMinZ = false, hasMaxZ = false, hasTrackZ = false;
            double minX = 3.4e38, maxX = -3.4e38, trackX = 0;
            double minY = 3.4e38, maxY = -3.4e38, trackY = 0;
            double minZ = 3.4e38, maxZ = -3.4e38, trackZ = 0;

            for (size_t ci = 1; ci < nodes.size(); ++ci) {
                auto &cn = nodes[ci];
                if (cn.nodeType != 9 || cn.activeSlot().done || !cn.accumulated.active) continue;

                // Target node: root (node 0). Full impl would look up dtgt.
                const auto &target = nodes[0];

                // Compute constraintType with flip adjustment (0x6BC1B0..0x6BC1FC)
                int ctype = cn.cameraConstraintType;
                if (cn.accumulated.flipX) {
                    if (ctype == 0) ctype = 2;
                    else if (ctype == 2) ctype = 0;
                }
                if (cn.accumulated.flipY) {
                    if (ctype == 3) ctype = 5;
                    else if (ctype == 5) ctype = 3;
                }

                // 9 cases (0x6BC224..0x6BC358)
                switch (ctype) {
                    case 0: { // X min constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d < 0 && d < minX) { minX = d; hasMinX = true; }
                        break;
                    }
                    case 1: { // X direct track
                        trackX = target.accumulated.posX - cn.accumulated.posX;
                        hasTrackX = true;
                        break;
                    }
                    case 2: { // X max constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d > 0 && d > maxX) { maxX = d; hasMaxX = true; }
                        break;
                    }
                    case 3: { // Y min constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d < 0 && d < minY) { minY = d; hasMinY = true; }
                        break;
                    }
                    case 4: { // Y direct track
                        trackY = target.accumulated.posY - cn.accumulated.posY;
                        hasTrackY = true;
                        break;
                    }
                    case 5: { // Y max constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d > 0 && d > maxY) { maxY = d; hasMaxY = true; }
                        break;
                    }
                    case 6: { // Z min constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d < 0 && d < minZ) { minZ = d; hasMinZ = true; }
                        break;
                    }
                    case 7: { // Z direct track
                        trackZ = target.accumulated.posZ - cn.accumulated.posZ;
                        hasTrackZ = true;
                        break;
                    }
                    case 8: { // Z max constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d > 0 && d > maxZ) { maxZ = d; hasMaxZ = true; }
                        break;
                    }
                    default: break;
                }
            }
            // Resolve final offset per axis (0x6BC398..0x6BC410)
            // Priority: track > max > min > 0
            if (hasTrackX) offsetX = trackX;
            else if (hasMaxX) offsetX = maxX;
            else if (hasMinX) offsetX = minX;
            if (hasTrackY) offsetY = trackY;
            else if (hasMaxY) offsetY = maxY;
            else if (hasMinY) offsetY = minY;
            if (hasTrackZ) offsetZ = trackZ;
            else if (hasMaxZ) offsetZ = maxZ;
            else if (hasMinZ) offsetZ = minZ;

            // Apply offset to all nodes (0x6BC450..0x6BC4BC)
            if (offsetX != 0 || offsetY != 0 || offsetZ != 0) {
                for (size_t ci = 1; ci < nodes.size(); ++ci) {
                    nodes[ci].accumulated.posX += offsetX;
                    nodes[ci].accumulated.posY += offsetY;
                    nodes[ci].accumulated.posZ += offsetZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_VertexComputation() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC4F0: Vertex computation ---
        // Aligned to 0x6BC4F0. Full implementation matching decompilation.
        for (size_t vi = 1; vi < nodes.size(); ++vi) {
            auto &vn = nodes[vi];
            const int parentIdx = vn.parentIndex >= 0 ? vn.parentIndex : 0;
            auto &parentNode = nodes[parentIdx];
            const int slotIdx = 0;  // current slot index

            // sub_6BC648 consumes the immutable emoteEdit.priorDraw value only
            // while forceVisible is active. NodeTree cached the raw integer so
            // this hot loop does not repeatedly walk PSB dictionaries or use
            // dynamic_pointer_cast for hundreds of E-mote nodes.
            vn.priorDraw = vn.forceVisible ? vn.authoredPriorDraw : 0;

            // sub_6B98D0 stores a mesh-chain pointer at node+1968.  It is not
            // the node+1936 shape/stencil clip parent.  A node inherits the
            // closest parent that either contributes mesh data (+1962) or is
            // a mesh-combine boundary (+1963), otherwise it skips directly to
            // the parent's inherited mesh ancestor.
            if(parentIdx > 0) {
                vn.meshAncestorIndex =
                    (parentNode.hasMeshData || parentNode.meshCombineEnabled)
                    ? parentIdx : parentNode.meshAncestorIndex;
            } else {
                vn.meshAncestorIndex = -1;
            }

            // Native +1962 is deliberately narrower than "has a mesh": the
            // current mesh slot must be active and meshSyncChildMask bit 3
            // must opt this node into deforming descendants.  +1963 records
            // whether the inherited chain may be combined through this node;
            // inheritMask bit 0x02000000 disables that combination.
            vn.hasMeshData =
                !vn.activeSlot().done && vn.accumulated.active &&
                vn.meshType != 0 && vn.meshControlPoints.size() == 32 &&
                (vn.meshFlags & 8) != 0;
            vn.meshCombineEnabled =
                vn.meshAncestorIndex >= 0 &&
                (vn.inheritFlags & 0x02000000) == 0;

            // Check visible (0x6BC700..0x6BC74C)
            if (!vn.accumulated.visible) {
                // Walk parent for mesh flag
                goto bc4f0_next;
            }

            // Propagate clip origin
            vn.clipOriginX = vn.interpolatedCache.ox;
            vn.clipOriginY = vn.interpolatedCache.oy;

            // nodeType 1/5 special position via parent mesh chain (0x6BC828..0x6BC8D4)
            // if ((1 << nodeType) & 0x22) != 0 → nodeType 1 (shape) or 5 (camera)
            if (((1 << vn.nodeType) & 0x22) != 0) {
                double px = vn.accumulated.posX;
                double py = vn.accumulated.posY;
                // Walk parent clip chain, evaluate through each mesh (0x6BC838..0x6BC8B0)
                int clipWalk = vn.meshAncestorIndex;
                size_t clipDepth = 0;
                while (clipWalk >= 0 &&
                       clipWalk < static_cast<int>(nodes.size()) &&
                       clipDepth++ < nodes.size()) {
                    auto &cn = nodes[clipWalk];
                    if (cn.meshWorldControlPoints.size() >= 32) {
                        // Apply inverse matrix to get normalized coords (0x6BC858..0x6BC87C)
                        float tx = static_cast<float>(px) + cn.meshInvOffX;
                        float ty = static_cast<float>(py) + cn.meshInvOffY;
                        float ix = static_cast<float>(
                            cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                        float iy = static_cast<float>(
                            cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                        // Evaluate bezier patch at normalized coords
                        // (sub_69B1E8).
                        float ox = 0.0f;
                        float oy = 0.0f;
                        evaluateMotionBezierPatch(
                            cn.meshWorldControlPoints.data(), ix, iy, ox, oy);
                        px = ox;
                        py = oy;
                    }
                    clipWalk = cn.meshAncestorIndex;
                }
                vn.vertexPosX = px;
                vn.vertexPosY = py;
                vn.vertexPosZ = vn.accumulated.posZ;
            }

            // Non slot-done path: vertex computation (0x6BC8DC..0x6BD730)
            if (!vn.activeSlot().done) {
                // Second visibility bitmask check (0x6BCE2C..0x6BCE40)
                // Non-emote: 7233 = 0x1C41, Emote: 7241 = 0x1C49
                const int vbm = _runtime->isEmoteMode ? 7241 : 7233;
                const bool vertexEligible = vn.forceVisible
                    || ((vbm & (1 << vn.nodeType)) != 0);

                if (vertexEligible && vn.hasSource) {
                    const double m11 = vn.accumulated.m11, m12 = vn.accumulated.m12;
                    const double m21 = vn.accumulated.m21, m22 = vn.accumulated.m22;
                    const double posX = vn.accumulated.posX;
                    const double posY = vn.accumulated.posY
                        + vn.accumulated.posZ * _zFactor;

                    // Origin offset (0x6BCB58..0x6BCBA4)
                    const double totalOX = vn.originX + vn.clipOriginX;
                    const double totalOY = vn.originY + vn.clipOriginY;
                    const double orgX = posX - (m12 * totalOY + totalOX * m11);
                    const double orgY = posY - (totalOY * m22 + totalOX * m21);
                    vn.vertexPosX = orgX;
                    vn.vertexPosY = orgY;
                    vn.vertexPosZ = vn.accumulated.posZ;

                    const double cw = vn.clipW;
                    const double ch = vn.clipH;

                    // Mesh vertex construction (sub_6B98D0, 0x6B9F68..0x6BA760).
                    // The node's authored patch is always converted to world
                    // control points so descendants can inherit it.  A render
                    // grid, however, is allocated only when node+1968 points
                    // at an inherited mesh chain.
                    const bool ownMesh =
                        vn.meshType == 1 && vn.meshControlPoints.size() == 32 &&
                        cw > 0.0 && ch > 0.0;
                    const double mw11 = m11 * cw, mw12 = m12 * ch;
                    const double mw21 = m21 * cw, mw22 = m22 * ch;
                    if(ownMesh) {
                        const double det = mw11 * mw22 - mw12 * mw21;
                        if(std::fabs(det) > 1e-10) {
                            vn.meshInvM11 = mw22 / det;
                            vn.meshInvM12 = -mw12 / det;
                            vn.meshInvM21 = -mw21 / det;
                            vn.meshInvM22 = mw11 / det;
                            vn.meshInvOffX = -static_cast<float>(orgX);
                            vn.meshInvOffY = -static_cast<float>(orgY);
                        }
                        vn.meshWorldControlPoints.resize(32);
                        for(int point = 0; point < 16; ++point) {
                            const double u = vn.meshControlPoints[point * 2];
                            const double v = vn.meshControlPoints[point * 2 + 1];
                            vn.meshWorldControlPoints[point * 2] =
                                static_cast<float>(orgX + u * mw11 + v * mw12);
                            vn.meshWorldControlPoints[point * 2 + 1] =
                                static_cast<float>(orgY + u * mw21 + v * mw22);
                        }
                    } else {
                        vn.meshWorldControlPoints.clear();
                    }

                    if(vn.meshAncestorIndex >= 0 && cw > 0.0 && ch > 0.0) {
                        int divisionSource = ownMesh
                            ? static_cast<int>(vi) : vn.meshAncestorIndex;
                        while(!ownMesh && divisionSource >= 0 &&
                              divisionSource < static_cast<int>(nodes.size()) &&
                              !nodes[divisionSource].hasMeshData) {
                            divisionSource = nodes[divisionSource].meshAncestorIndex;
                        }
                        int divTotal = 1;
                        if(divisionSource >= 0 &&
                           divisionSource < static_cast<int>(nodes.size())) {
                            const auto &sourceNode = nodes[divisionSource];
                            divTotal = static_cast<int>(
                                _emoteMeshDivisionRatio *
                                static_cast<double>(sourceNode.meshDivision));
                            divTotal = std::clamp(divTotal, 1, 50);
                            if(!ownMesh) {
                                const double sourceExtent =
                                    sourceNode.clipW + sourceNode.clipH;
                                if(sourceExtent > 0.0) {
                                    divTotal = std::clamp(static_cast<int>(
                                        static_cast<double>(divTotal) *
                                        (cw + ch) / sourceExtent), 1, 50);
                                }
                            }
                        }

                        const int xSegments = std::clamp(static_cast<int>(
                            static_cast<double>(divTotal) * cw / (cw + ch)),
                            0, divTotal);
                        vn.meshDivX = xSegments + 1;
                        vn.meshDivY = divTotal - xSegments + 1;
                        const int numPts = vn.meshDivX * vn.meshDivY;
                        vn.meshRenderPoints.resize(numPts * 2);

                        // sub_6B8348 builds either an identity UV grid for an
                        // authored patch or a bilinear grid over the image quad
                        // for an ordinary source inherited by a mesh parent.
                        for(int gy = 0; gy < vn.meshDivY; ++gy) {
                            const float v = vn.meshDivY > 1
                                ? static_cast<float>(gy) / (vn.meshDivY - 1)
                                : 0.f;
                            for(int gx = 0; gx < vn.meshDivX; ++gx) {
                                const float u = vn.meshDivX > 1
                                    ? static_cast<float>(gx) / (vn.meshDivX - 1)
                                    : 0.f;
                                float px = static_cast<float>(
                                    orgX + u * mw11 + v * mw12);
                                float py = static_cast<float>(
                                    orgY + u * mw21 + v * mw22);
                                if(ownMesh) {
                                    evaluateMotionBezierPatch(
                                        vn.meshWorldControlPoints.data(),
                                        u, v, px, py);
                                }
                                const size_t point = static_cast<size_t>(
                                    gy * vn.meshDivX + gx) * 2;
                                vn.meshRenderPoints[point] = px;
                                vn.meshRenderPoints[point + 1] = py;
                            }
                        }

                        // Evaluate both grid and origin through every active
                        // inherited world patch.  Since all grid points take
                        // the complete chain here, adding the origin delta a
                        // second time (the previous implementation did this)
                        // would duplicate the deformation.
                        int meshWalk = vn.meshAncestorIndex;
                        size_t meshDepth = 0;
                        double cascadeOrgX = orgX;
                        double cascadeOrgY = orgY;
                        while(meshWalk >= 0 &&
                              meshWalk < static_cast<int>(nodes.size()) &&
                              meshDepth++ < nodes.size()) {
                            const auto &ancestor = nodes[meshWalk];
                            if(ancestor.hasMeshData &&
                               ancestor.meshWorldControlPoints.size() == 32) {
                                for(size_t point = 0;
                                    point < vn.meshRenderPoints.size() / 2;
                                    ++point) {
                                    const float x =
                                        vn.meshRenderPoints[point * 2] +
                                        ancestor.meshInvOffX;
                                    const float y =
                                        vn.meshRenderPoints[point * 2 + 1] +
                                        ancestor.meshInvOffY;
                                    const float u = static_cast<float>(
                                        ancestor.meshInvM11 * x +
                                        ancestor.meshInvM12 * y);
                                    const float v = static_cast<float>(
                                        ancestor.meshInvM21 * x +
                                        ancestor.meshInvM22 * y);
                                    evaluateMotionBezierPatch(
                                        ancestor.meshWorldControlPoints.data(),
                                        u, v,
                                        vn.meshRenderPoints[point * 2],
                                        vn.meshRenderPoints[point * 2 + 1]);
                                }
                                const float ox = static_cast<float>(cascadeOrgX) +
                                    ancestor.meshInvOffX;
                                const float oy = static_cast<float>(cascadeOrgY) +
                                    ancestor.meshInvOffY;
                                const float ou = static_cast<float>(
                                    ancestor.meshInvM11 * ox +
                                    ancestor.meshInvM12 * oy);
                                const float ov = static_cast<float>(
                                    ancestor.meshInvM21 * ox +
                                    ancestor.meshInvM22 * oy);
                                float resultX = 0.f, resultY = 0.f;
                                evaluateMotionBezierPatch(
                                    ancestor.meshWorldControlPoints.data(),
                                    ou, ov, resultX, resultY);
                                cascadeOrgX = resultX;
                                cascadeOrgY = resultY;
                                _processedMeshVerticesNum += numPts + 1;
                            }
                            meshWalk = ancestor.meshAncestorIndex;
                        }
                        vn.vertexPosX = cascadeOrgX;
                        vn.vertexPosY = cascadeOrgY;
                    } else {
                        vn.meshRenderPoints.clear();
                        vn.meshDivX = 0;
                        vn.meshDivY = 0;
                    }

                    // 4-corner vertex output (0x6BCE44..0x6BCEC0)
                    {
                        const double fx = vn.vertexPosX;
                        const double fy = vn.vertexPosY;
                        vn.vertices[0] = static_cast<float>(fx);
                        vn.vertices[1] = static_cast<float>(fy);
                        vn.vertices[2] = static_cast<float>(fx + m11*cw);
                        vn.vertices[3] = static_cast<float>(fy + m21*cw);
                        vn.vertices[4] = static_cast<float>(fx + m11*cw + m12*ch);
                        vn.vertices[5] = static_cast<float>(fy + m21*cw + m22*ch);
                        vn.vertices[6] = static_cast<float>(fx + m12*ch);
                        vn.vertices[7] = static_cast<float>(fy + m22*ch);
                        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                            const auto motionPath = _runtime->activeMotion->path;
                            const std::array<float, 8> expectedVertices = {
                                static_cast<float>(fx),
                                static_cast<float>(fy),
                                static_cast<float>(fx + m11 * cw),
                                static_cast<float>(fy + m21 * cw),
                                static_cast<float>(fx + m11 * cw + m12 * ch),
                                static_cast<float>(fy + m21 * cw + m22 * ch),
                                static_cast<float>(fx + m12 * ch),
                                static_cast<float>(fy + m22 * ch)
                            };
                            bool ok = true;
                            for(size_t vi = 0; vi < expectedVertices.size(); ++vi) {
                                if(std::fabs(vn.vertices[vi] - expectedVertices[vi]) >
                                   0.01f) {
                                    ok = false;
                                    break;
                                }
                            }
                            detail::logoChainTraceCheck(
                                motionPath, "updateLayers.phase3.vertices",
                                "0x6BC4F0", _clampedEvalTime,
                                fmt::format(
                                    "pos=({:.3f},{:.3f}) clip=({:.3f},{:.3f}) m=({:.6f},{:.6f},{:.6f},{:.6f}) exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                                    fx, fy, cw, ch, m11, m12, m21, m22,
                                    expectedVertices[0], expectedVertices[1],
                                    expectedVertices[2], expectedVertices[3],
                                    expectedVertices[4], expectedVertices[5],
                                    expectedVertices[6], expectedVertices[7]),
                                fmt::format(
                                    "src={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                                    vn.interpolatedCache.src.empty()
                                        ? std::string("<none>")
                                        : vn.interpolatedCache.src,
                                    vn.vertices[0], vn.vertices[1],
                                    vn.vertices[2], vn.vertices[3],
                                    vn.vertices[4], vn.vertices[5],
                                    vn.vertices[6], vn.vertices[7]),
                                ok,
                                "sub_6BC4F0 vertex output diverged from expected corners");
                        }
                    }

                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // When node+1996 (forceVisible) is set, write node properties
                    // to a TJS dictionary for sub-motion evaluation.
                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // Write node properties to TJS dict for sub-motion evaluation.
                    if (vn.forceVisible && vn.tjsLayerObject) {
                        auto *tjsObj = static_cast<iTJSDispatch2 *>(vn.tjsLayerObject);
                        try {
                            // "c" array: [posX, posY] (0x6BD480..0x6BD494)
                            tTJSVariant posXv(vn.vertexPosX);
                            tTJSVariant posYv(vn.vertexPosY);
                            // "mtx" array: [m11,m12,m21,m22] (0x6BD534..0x6BD570)
                            tTJSVariant m11v(m11), m12v(m12), m21v(m21), m22v(m22);
                            // "width" (0x6BD590)
                            tTJSVariant wv(cw);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("width"),
                                nullptr, &wv, tjsObj);
                            // "height" (0x6BD5B0)
                            tTJSVariant hv(ch);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("height"),
                                nullptr, &hv, tjsObj);
                            // "originX" (0x6BD5E4)
                            tTJSVariant oxv(totalOX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originX"),
                                nullptr, &oxv, tjsObj);
                            // "originY" (0x6BD618)
                            tTJSVariant oyv(totalOY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originY"),
                                nullptr, &oyv, tjsObj);
                            // "flipX" (0x6BD638)
                            tTJSVariant fxv(static_cast<tjs_int>(vn.accumulated.flipX));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipX"),
                                nullptr, &fxv, tjsObj);
                            // "flipY" (0x6BD658)
                            tTJSVariant fyv(static_cast<tjs_int>(vn.accumulated.flipY));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipY"),
                                nullptr, &fyv, tjsObj);
                            // "zoomX" (0x6BD678)
                            tTJSVariant zxv(vn.accumulated.scaleX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomX"),
                                nullptr, &zxv, tjsObj);
                            // "zoomY" (0x6BD698)
                            tTJSVariant zyv(vn.accumulated.scaleY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomY"),
                                nullptr, &zyv, tjsObj);
                            // "slantX" (0x6BD6B8)
                            tTJSVariant sxv(vn.accumulated.slantX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("slantX"),
                                nullptr, &sxv, tjsObj);
                            // "angle" (0x6BD6D8)
                            tTJSVariant av(vn.accumulated.angle);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("angle"),
                                nullptr, &av, tjsObj);
                        } catch (...) {}
                    }
                }
            }
            bc4f0_next:;
        }

        // Delta position computation (0x6BBB74..0x6BBC54)
        // if playing (player+480): delta = 0; else: delta = currentPos - prevPos
        {
            bool anyPlaying = std::any_of(
                _runtime->timelines.begin(), _runtime->timelines.end(),
                [](const auto &e) { return e.second.playing; });
            for (size_t di = 1; di < nodes.size(); ++di) {
                auto &dn = nodes[di];
                if (anyPlaying) {
                    dn.deltaPosX = 0; dn.deltaPosY = 0; dn.deltaPosZ = 0;
                } else {
                    dn.deltaPosX = dn.accumulated.posX - dn.prevPosX;
                    dn.deltaPosY = dn.accumulated.posY - dn.prevPosY;
                    dn.deltaPosZ = dn.accumulated.posZ - dn.prevPosZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_Visibility() {
        auto &nodes = _runtime->nodes;
        // Visibility flags — aligned to sub_6BD8DC at 0x6BD8DC.
        // Root node (index 0) is always visible.
        if (!nodes.empty()) {
            nodes[0].visibleAncestorIndex = -1;
            nodes[0].drawFlag = nodes[0].accumulated.visible && nodes[0].hasSource;
        }
        // Visibility bitmask: which nodeTypes can render
        // Non-emote: 6145 = 0x1801 → nodeTypes 0, 11, 12
        // Emote:     6153 = 0x1809 → nodeTypes 0, 3, 11, 12
        // Aligned to sub_6BD8DC (0x6BD8DC): visibility bitmask depends on emote mode.
        const int visBitmask = _runtime->isEmoteMode ? 6153 : 6145;
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];
            node.visibleAncestorIndex = -1;

            // Find visible ancestor (walk parent chain, 0x6BD9D8)
            int pIdx = node.parentIndex;
            if (pIdx >= 0 && pIdx < static_cast<int>(nodes.size())) {
                if (!nodes[pIdx].drawFlag) {
                    node.visibleAncestorIndex = nodes[pIdx].visibleAncestorIndex;
                } else {
                    node.visibleAncestorIndex = pIdx;
                }
            }

            // Visibility is driven by the active frame slot and accumulated
            // state. stencilType is an alpha-composite operation code, not a
            // general visibility gate: ordinary image nodes legitimately use
            // stencilType==0.
            if (node.activeSlot().done) {
                node.drawFlag = false;
            } else if (!node.accumulated.active) {
                node.drawFlag = false;
            } else if (node.forceVisible
                       || (visBitmask & (1 << node.nodeType)) != 0) {
                node.drawFlag = node.hasSource;
            } else {
                // Active node, not in renderable bitmask, not forceVisible:
                // v9 stays as active (non-zero) → drawFlag = true
                node.drawFlag = true;
            }
        }

    }

    void Player::updateLayersPhase3_CameraNode() {
        auto &nodes = _runtime->nodes;
        // Camera node processing — aligned to sub_6BDA28 (0x6BDA28).
        // Find first nodeType=5 (camera) that is active, compute cameraOffset.
        _hasCamera = false;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto &camNode = nodes[i];
            if (camNode.nodeType != 5 || !camNode.accumulated.active) continue;
            _hasCamera = true;

            // Compute delta from root node position
            const auto &rootAcc = nodes[0].accumulated;
            const double dx = -(camNode.accumulated.posX - rootAcc.posX);
            const double dy = -(camNode.accumulated.posY * _zFactor
                + camNode.accumulated.posZ
                - (rootAcc.posY * _zFactor + rootAcc.posZ));

            // Transform by drawAffineMatrix (player+808..832)
            const auto &dam = _runtime->drawAffineMatrix;
            _cameraOffsetX = static_cast<float>(
                static_cast<int>(dam[0] * dx + dam[2] * dy + 0.5));
            _cameraOffsetY = static_cast<float>(
                static_cast<int>(dam[1] * dx + dam[3] * dy + 0.5));

            // Camera-to-target angle (0x6BDC04..0x6BDCB0)
            // When stereovisionActive (a1+1094): compute camera angle for 3D effect.
            if (_stereovisionActive) {
                // Store camera/target positions (a1+72..112)
                _cameraPosX = camNode.accumulated.posX;
                _cameraPosY = camNode.accumulated.posY;
                _cameraPosZ = camNode.accumulated.posZ;
                // Look up target node via clip slot action path
                // For now, target defaults to previous positions
                // Compute angle: atan2(camPosZ - targetZ, camPosX - targetX)
                double angleRad = std::atan2(
                    camNode.accumulated.posZ - _cameraTargetZ,
                    camNode.accumulated.posX - _cameraTargetX);
                double angleDeg = angleRad * -57.2957795 + 90.0;
                while (angleDeg < 0.0) angleDeg += 360.0;
                while (angleDeg >= 360.0) angleDeg -= 360.0;
                _cameraAngle = angleDeg;  // a1+472
                _cameraTargetX = _cameraPosX;
                _cameraTargetY = _cameraPosY;
                _cameraTargetZ = _cameraPosZ;
            }
            break;  // only first camera node
        }

    }

    void Player::updateLayersPhase3_ShapeAABB() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDCC0: Shape AABB computation (nodeType=7) ---
        // Aligned to 0x6BDCC0. For nodeType=7 active nodes, compute AABB
        // from 2x2 matrix × 16-unit extent, origin offset, parent clip clamping.
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            // Propagate parent clip region (node+1936)
            if (sn.parentIndex >= 0 && sn.parentIndex < static_cast<int>(nodes.size())) {
                sn.parentClipIndex = nodes[sn.parentIndex].parentClipIndex;
            }
            if (sn.nodeType != 7 || !sn.accumulated.active) continue;

            const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
            const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
            const double px = sn.accumulated.posX, py = sn.accumulated.posY;
            const double pzs = sn.accumulated.posZ * _zFactor + py;
            const double ox = sn.clipOriginX, oy = sn.clipOriginY;
            const double oox = ox * m11 + oy * m12;
            const double ooy = ox * m21 + oy * m22;
            // Extent = matrix × 16
            const double ex1 = m11 * 16.0, ex2 = m12 * 16.0;
            const double ey1 = m21 * 16.0, ey2 = m22 * 16.0;
            double xMin = px - ex1 - ex2 - oox;
            double xMax = px + ex1 + ex2 - oox;
            double yMin = pzs - ey1 - ey2 - ooy;
            double yMax = pzs + ey1 + ey2 - ooy;
            if (xMin > xMax) std::swap(xMin, xMax);
            if (yMin > yMax) std::swap(yMin, yMax);
            sn.shapeAABB[0] = static_cast<float>(xMin);
            sn.shapeAABB[1] = static_cast<float>(yMin);
            sn.shapeAABB[2] = static_cast<float>(xMax);
            sn.shapeAABB[3] = static_cast<float>(yMax);
            // Clamp to parent clip (0x6BDE40..0x6BDE80)
            if (sn.parentClipIndex >= 0 &&
                sn.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &pc = nodes[sn.parentClipIndex];
                if (pc.shapeAABB[0] > sn.shapeAABB[0]) sn.shapeAABB[0] = pc.shapeAABB[0];
                if (pc.shapeAABB[1] > sn.shapeAABB[1]) sn.shapeAABB[1] = pc.shapeAABB[1];
                if (pc.shapeAABB[2] < sn.shapeAABB[2]) sn.shapeAABB[2] = pc.shapeAABB[2];
                if (pc.shapeAABB[3] < sn.shapeAABB[3]) sn.shapeAABB[3] = pc.shapeAABB[3];
            }
            sn.parentClipIndex = static_cast<int>(si);
        }

    }

    void Player::updateLayersPhase3_ShapeGeometry() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDE94: Shape geometry computation (nodeType=1) ---
        // Aligned to 0x6BDE94. For nodeType=1 nodes with active slot,
        // compute shape vertices based on shapeType (0=point,1=circle,2=rect,3=quad).
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            if (sn.nodeType != 1 || sn.activeSlot().done) continue;
            sn.shapeGeomType = sn.shapeType;
            switch (sn.shapeType) {
                case 0: // point (0x6BDF40)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    break;
                case 1: { // circle (0x6BDF50)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    sn.shapeVertices[2] = sn.accumulated.scaleX * 16.0 * 0.5;
                    break;
                }
                case 2: { // rect (0x6BDF70)
                    const double hw = sn.accumulated.scaleX * 16.0 * 0.5;
                    const double hh = sn.accumulated.scaleY * 16.0 * 0.5;
                    sn.shapeVertices[3] = sn.vertexPosX - hw;
                    sn.shapeVertices[4] = sn.vertexPosY - hh;
                    sn.shapeVertices[5] = sn.vertexPosX + hw;
                    sn.shapeVertices[6] = sn.vertexPosY + hh;
                    break;
                }
                case 3: { // quad (0x6BDFA8)
                    const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
                    const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
                    const double ox = sn.clipOriginX, oy = sn.clipOriginY;
                    const double oox = ox * m11 + oy * m12;
                    const double ooy = ox * m21 + oy * m22;
                    const double px = sn.vertexPosX, py = sn.vertexPosY;
                    const double ax = m11 * -8.0, bx = m12 * -8.0;
                    const double cx = m11 * 8.0,  dx = m12 * 8.0;
                    const double ay = m21 * -8.0, by = m22 * -8.0;
                    const double cy = m21 * 8.0,  dy = m22 * 8.0;
                    sn.shapeVertices[7]  = px + ax + bx - oox;
                    sn.shapeVertices[8]  = py + ay + by - ooy;
                    sn.shapeVertices[9]  = px + cx + bx - oox;
                    sn.shapeVertices[10] = py + cy + by - ooy;
                    sn.shapeVertices[11] = px + cx + dx - oox;
                    sn.shapeVertices[12] = py + cy + dy - ooy;
                    sn.shapeVertices[13] = px + ax + dx - oox;
                    sn.shapeVertices[14] = py + ay + dy - ooy;
                    break;
                }
                default: break;
            }
        }

    }

    // Helper: find node by label in the node tree (sub_6F2228 equivalent)
    // Aligned to sub_6F2228: std::map<ttstr,int> lookup at player+24.
    // Binary uses red-black tree traversal with wcscmp; we use std::map::find.
    static int findNodeByLabel(const std::map<std::string, int> &labelMap,
                               const std::string &label) {
        auto it = labelMap.find(label);
        return (it != labelMap.end()) ? it->second : -1;
    }

    void Player::updateLayersPhase3_MotionSubNode(double currentTime) {
        auto &nodes = _runtime->nodes;
        const std::string motionPath = _runtime->activeMotion
            ? _runtime->activeMotion->path
            : std::string();
        // Motion sub-node processing — aligned to sub_6BE0C0 (0x6BE0C0).
        // For each nodeType=3 (Motion) node, create/manage child Player instance.
        // libkrkr2's generic updater branches away here for E-mote because
        // libgame owns an equivalent object-composition pass.  AetherKiri
        // hosts both formats in this Player, so E-mote must continue through
        // the shared pass: all_parts/全体構造 is only a skeleton whose visible
        // artwork lives in same-PSB body_parts/head_parts/face_parts motions.

        struct MotionSubNodeRootState {
            double posX = 0.0;
            double posY = 0.0;
            double posZ = 0.0;
        };

        auto computeMotionSubNodeRootState =
            [](const detail::MotionNode &parentNode) {
                MotionSubNodeRootState state;
                state.posX = parentNode.accumulated.posX;
                state.posY = parentNode.accumulated.posY;
                state.posZ = parentNode.accumulated.posZ;

                const double originX = parentNode.activeSlot().ox;
                const double originY = parentNode.activeSlot().oy;
                if (originX != 0.0 || originY != 0.0) {
                    const double negOY = -originY;
                    const double vx =
                        parentNode.accumulated.m12 * negOY -
                        originX * parentNode.accumulated.m11;
                    const double vy =
                        parentNode.accumulated.m22 * negOY -
                        originX * parentNode.accumulated.m21;
                    if (parentNode.coordinateMode == 1) {
                        state.posX += vx;
                        state.posZ += vy;
                    } else {
                        state.posX += vx;
                        state.posY += vy;
                    }
                }

                return state;
            };

        auto applyMotionSubNodeRootState =
            [&](Player &child,
                const detail::MotionNode &parentNode,
                const MotionSubNodeRootState &rootState,
                bool hasAngle,
                double computedAngle) -> bool {
                if (!child._runtime) {
                    return false;
                }

                bool changed = false;
                const auto assignIfChanged =
                    [&](auto &target, const auto &value) {
                        if(target == value) {
                            return false;
                        }
                        target = value;
                        changed = true;
                        return true;
                    };
                assignIfChanged(child._pendingRootX, rootState.posX);
                assignIfChanged(child._pendingRootY, rootState.posY);
                assignIfChanged(child._pendingRootZ, rootState.posZ);
                assignIfChanged(child._hasPendingRootPos, true);
                assignIfChanged(child._zFactor, _zFactor);
                assignIfChanged(child._runtime->drawAffineMatrix,
                                _runtime->drawAffineMatrix);
                uint32_t packed;
                std::memcpy(&packed, &parentNode.colorBytes[0],
                            sizeof(uint32_t));
                assignIfChanged(child._colorWeightPacked, packed);

                if (child._runtime->nodes.empty()) {
                    if(changed) {
                        child._layersDirty = true;
                    }
                    return changed;
                }

                auto &cr = child._runtime->nodes[0];
                bool localChanged = false;
                const auto assignLocal = [&](auto &target, const auto &value) {
                    const bool valueChanged = assignIfChanged(target, value);
                    localChanged = localChanged || valueChanged;
                };
                assignLocal(cr.localState.posX, rootState.posX);
                assignLocal(cr.localState.posY, rootState.posY);
                assignLocal(cr.localState.posZ, rootState.posZ);
                assignLocal(cr.localState.flipX,
                            parentNode.accumulated.flipX);
                assignLocal(cr.localState.flipY,
                            parentNode.accumulated.flipY);
                assignLocal(cr.localState.scaleX,
                            parentNode.accumulated.scaleX);
                assignLocal(cr.localState.scaleY,
                            parentNode.accumulated.scaleY);
                assignLocal(cr.localState.slantX,
                            parentNode.accumulated.slantX);
                assignLocal(cr.localState.slantY,
                            parentNode.accumulated.slantY);
                assignLocal(cr.localState.opacity,
                            parentNode.accumulated.opacity);
                assignLocal(cr.localState.active,
                            parentNode.accumulated.active);
                assignLocal(cr.localState.visible,
                            parentNode.accumulated.visible);
                if(localChanged) {
                    cr.localState.dirty = true;
                }

                assignIfChanged(cr.accumulated.posX, rootState.posX);
                assignIfChanged(cr.accumulated.posY, rootState.posY);
                assignIfChanged(cr.accumulated.posZ, rootState.posZ);
                assignIfChanged(cr.accumulated.flipX,
                                parentNode.accumulated.flipX);
                assignIfChanged(cr.accumulated.flipY,
                                parentNode.accumulated.flipY);
                assignIfChanged(cr.accumulated.scaleX,
                                parentNode.accumulated.scaleX);
                assignIfChanged(cr.accumulated.scaleY,
                                parentNode.accumulated.scaleY);
                assignIfChanged(cr.accumulated.slantX,
                                parentNode.accumulated.slantX);
                assignIfChanged(cr.accumulated.slantY,
                                parentNode.accumulated.slantY);
                assignIfChanged(cr.accumulated.opacity,
                                parentNode.accumulated.opacity);
                assignIfChanged(cr.accumulated.active,
                                parentNode.accumulated.active);

                // === Angle -> child (0x6BEAA8..0x6BEB08) ===
                if (hasAngle) {
                    if (child._runtime->isEmoteMode) {
                        double k = computedAngle;
                        while (k < 0.0) k += 360.0;
                        while (k >= 360.0) k -= 360.0;
                    } else {
                        assignIfChanged(cr.accumulated.angle, computedAngle);
                    }
                }

                // === Matrix propagation (0x6BEB9C..0x6BEC4C) ===
                double m11 = parentNode.accumulated.m11;
                double m12 = parentNode.accumulated.m12;
                double m21 = parentNode.accumulated.m21;
                double m22 = parentNode.accumulated.m22;
                if (hasAngle ||
                    computedAngle == parentNode.accumulated.angle ||
                    child._directEdit) {
                } else {
                    double delta =
                        (computedAngle - parentNode.accumulated.angle) *
                        3.14159265 * 2.0 / 360.0;
                    if (parentNode.accumulated.flipX !=
                        parentNode.accumulated.flipY) {
                        delta = -delta;
                    }
                    const double c = std::cos(delta);
                    const double s = std::sin(delta);
                    m11 =
                        c * parentNode.accumulated.m11 +
                        s * parentNode.accumulated.m12;
                    m12 =
                        c * parentNode.accumulated.m12 -
                        parentNode.accumulated.m11 * s;
                    m21 =
                        c * parentNode.accumulated.m21 +
                        s * parentNode.accumulated.m22;
                    m22 =
                        c * parentNode.accumulated.m22 -
                        parentNode.accumulated.m21 * s;
                }
                assignIfChanged(cr.accumulated.m11, m11);
                assignIfChanged(cr.accumulated.m12, m12);
                assignIfChanged(cr.accumulated.m21, m21);
                assignIfChanged(cr.accumulated.m22, m22);
                if(changed) {
                    cr.accumulated.dirty = true;
                    child._layersDirty = true;
                }
                return changed;
            };

        std::vector<Player *> childPlayersNeedingUpdate;
        childPlayersNeedingUpdate.reserve(nodes.size());
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &mn = nodes[i];
            if (mn.nodeType != 3) continue;

            // Get parent's priorDraw flag as play trigger (v12, 0x6BE204..0x6BE214)
            // In libkrkr2.so: v12 = *(int*)(parentObj+48) where parentObj = node+8 or player+47*8
            int v12 = 0;
            if (mn.tjsLayerObject) {
                v12 = mn.priorDraw;  // keep raw int value, don't truncate to 0/1
            } else {
                v12 = _priorDraw;    // keep raw int value
            }
            bool childRootStateValid = false;
            MotionSubNodeRootState childRootState;
            bool childRootHasAngle = false;
            double childRootComputedAngle = 0.0;

            // Get child Player via TJS dispatch (0x6BE220..0x6BE260)
            // Aligned to binary: node+1912 → NativeInstanceSupport → native Player*
            // Child Player is pre-created in buildNodeTree (sub_6B3C78 case 3).
            {
            Player *childPtr = mn.getChildPlayer();
            if (!childPtr) {
                goto label_18;
            }
            Player &child = *childPtr;

            // If no v12 flags and not visible → skip to LABEL_18 (0x6BE270)
            if (!v12 && !mn.accumulated.visible) {
                goto label_18;
            }

            // Check slotDone → clear child (0x6BE31C..0x6BE354)
            // Binary: calls cleanup (sub_6C0DE8, sub_6B56F8), releases TJS variants,
            // then goes to LABEL_3 (next loop iteration), SKIPPING frameProgress/updateLayers.
            if (mn.activeSlot().done) {
                const bool retainTitlePresentationChild =
                    isYuzuTitlePresentationMotionPath(motionPath) &&
                    child._runtime &&
                    child._runtime->activeMotion &&
                    child._runtime->nodesBuilt &&
                    child._runtime->nodes.size() > 1;
                if(retainTitlePresentationChild) {
                    child._allplaying = false;
                    child._queuing = false;
                    if(LOGGER && std::getenv("AETHERKIRI_MOTION_DEBUG")) {
                        LOGGER->info(
                            "motion child retain final title frame: parent={} node={} childMotion={} nodes={}",
                            motionPath, mn.layerName,
                            child._runtime->activeMotion
                                ? child._runtime->activeMotion->path
                                : std::string("<none>"),
                            child._runtime->nodes.size());
                    }
                    continue;
                }
                // Binary cleanup at 0x6BE328..0x6BE354:
                // 1. child._allplaying = false (player+1099)
                // 2. sub_6C0DE8(child+1296) — resets timeline keyframe cache
                // 3. sub_6B56F8(child) — releases layer IDs for all non-root nodes,
                //    clears nodes (except root), resets label map
                // 4. Release TJS variants at child+984 and child+976
                child._allplaying = false;
                if (child._runtime) {
                    // sub_6C0DE8: reset timeline keyframe cache
                    child._runtime->timelines.clear();
                    // sub_6B56F8: release layer IDs for non-root nodes, then clear
                    child._runtime->layerIdsByName.clear();
                    child._runtime->layerNamesById.clear();
                    child._runtime->nodeLabelMap.clear();
                    // Keep root node but clear the rest (sub_6B56F8 at 0x6B59E0)
                    if (child._runtime->nodes.size() > 1) {
                        child._runtime->nodes.resize(1);
                    }
                    child._runtime->nodesBuilt = false;
                }
                continue;  // skip to next iteration — binary goes to LABEL_3, not LABEL_18
            }

            {
                // Get motion source from clip slot (0x6BE364)
                const auto &src = mn.activeSlot().src;
                if (!src.empty()) {
                    // Re-init gate: (v12 & 5) != 0 || mn.flags (0x6BE37C)
                    if ((v12 & 5) != 0 || (mn.flags & 0x01)) {
                        mn.flags |= 0x01; // mark as initialized (0x6BE388)

                        // Binary does NOT flip activeSlotIndex here (0x6BE21C reads it
                        // once and uses it unchanged throughout). Slot flip is managed
                        // elsewhere in the clip evaluation pipeline.

                        // Resolve motion and play (0x6BE3B4..0x6BE46C).
                        // Yuzu motion sources commonly use "motion/chara/clip",
                        // where the child player should reuse the current PSB
                        // snapshot and play the named clip instead of looking
                        // for a separate "clip.mtn" storage.
                        {
                            std::string motionRef = src;
                            if(motionRef.rfind("motion/", 0) == 0) {
                                motionRef = motionRef.substr(7);
                            }
                            const auto slashPos = motionRef.find_last_of("/\\");
                            std::string charaPart;
                            std::string motionPart;
                            if (slashPos == std::string::npos) {
                                charaPart = motionRef;
                                motionPart =
                                    mn.activeSlot().motionIcon.empty()
                                    ? motionRef
                                    : mn.activeSlot().motionIcon;
                            } else {
                                charaPart = motionRef.substr(0, slashPos);
                                motionPart =
                                    mn.activeSlot().motionIcon.empty()
                                    ? motionRef.substr(slashPos + 1)
                                    : mn.activeSlot().motionIcon;
                            }

                            const int playFlags =
                                mn.activeSlot().motionFlags | v12;
                            const bool canReuseCurrentSnapshot =
                                _runtime && _runtime->activeMotion &&
                                detail::findMotionClip(
                                    *_runtime->activeMotion, charaPart,
                                    motionPart, false) != nullptr;
                            if(canReuseCurrentSnapshot) {
                                // Valid group/icon children remain part of
                                // the same E-mote project and need its cached
                                // head/body/timeline modules. Do not share the
                                // manager for unresolved legacy `src` slots:
                                // ensureMotionLoaded() would otherwise mistake
                                // the project's last module for that missing
                                // child and recursively build the whole PSB.
                                child._resourceManagerNative =
                                    _resourceManagerNative;
                                const bool sameChara =
                                    detail::narrow(child._chara) == charaPart;
                                child.setChara(detail::widen(charaPart));
                                const auto motionKey = detail::widen(motionPart);
                                const bool sameSnapshot =
                                    child._runtime &&
                                    child._runtime->activeMotion ==
                                        _runtime->activeMotion;
                                const bool sameMotion =
                                    detail::narrow(child._motionKey) ==
                                    motionPart;
                                const bool hasTimeline =
                                    child._runtime &&
                                    child._runtime->timelines.find(motionPart) !=
                                        child._runtime->timelines.end();
                                const bool hasBuiltNodes =
                                    child._runtime &&
                                    child._runtime->nodesBuilt &&
                                    child._runtime->nodes.size() > 1;
                                if(!sameSnapshot || !sameChara || !sameMotion ||
                                   !hasTimeline || !hasBuiltNodes) {
                                    child._motionKey = motionKey;
                                    child.loadFromSnapshot(_runtime->activeMotion);
                                    child.playMotionLike_0x6B2284(
                                        motionKey, playFlags);
                                }
                            } else if(
                                shouldSearchCachedMotionComposition(
                                    motionRef,
                                    mn.activeSlot().motionIcon)) {
                                // The group may live in another PSB already
                                // loaded into this E-mote project (timeline →
                                // body, body → head). Select the cached module
                                // that owns this exact group/motion pair. A
                                // hierarchical src is authoritative even when
                                // motionIcon is empty; split CG timelines use
                                // motion/all_parts/全体構造 in exactly that form.
                                std::shared_ptr<detail::MotionSnapshot>
                                    composedSnapshot =
                                        child._runtime &&
                                        child._runtime->activeMotion &&
                                        detail::findMotionClip(
                                            *child._runtime->activeMotion,
                                            charaPart, motionPart, false)
                                        ? child._runtime->activeMotion
                                        : nullptr;
                                std::uint64_t composedGeneration = 0;
                                if(!composedSnapshot) {
                                    for(const auto &entry :
                                        _resourceManagerNative
                                            .uniqueCachedModules()) {
                                        const auto candidate =
                                            detail::lookupModuleSnapshot(
                                                entry.module);
                                        if(!candidate ||
                                           !detail::findMotionClip(
                                               *candidate, charaPart,
                                               motionPart, false)) {
                                            continue;
                                        }
                                        if(!composedSnapshot ||
                                           entry.loadGeneration >
                                               composedGeneration) {
                                            composedSnapshot = candidate;
                                            composedGeneration =
                                                entry.loadGeneration;
                                        }
                                    }
                                }
                                if(composedSnapshot) {
                                    const auto entryPoint =
                                        detail::
                                            resolveMotionCompositionEntryPoint(
                                                *composedSnapshot, charaPart,
                                                motionPart);
                                    if(LOGGER &&
                                       std::getenv(
                                           "AETHERKIRI_MOTION_DEBUG")) {
                                        LOGGER->info(
                                            "motion child bind cached composition: parent={} source={} owner={} clip={} module={}",
                                            motionPath, src,
                                            entryPoint.owner,
                                            entryPoint.label,
                                            composedSnapshot->path);
                                    }
                                    child._resourceManagerNative =
                                        _resourceManagerNative;
                                    const auto motionKey =
                                        detail::widen(entryPoint.label);
                                    const bool sameChara =
                                        detail::narrow(child._chara) ==
                                        entryPoint.owner;
                                    const bool sameSnapshot =
                                        child._runtime &&
                                        child._runtime->activeMotion ==
                                            composedSnapshot;
                                    const bool sameMotion =
                                        detail::narrow(child._motionKey) ==
                                        entryPoint.label;
                                    const bool hasTimeline =
                                        child._runtime &&
                                        child._runtime->timelines.find(
                                            entryPoint.label) !=
                                            child._runtime->timelines.end();
                                    const bool hasBuiltNodes =
                                        child._runtime &&
                                        child._runtime->nodesBuilt &&
                                        child._runtime->nodes.size() > 1;
                                    if(!sameSnapshot || !sameChara ||
                                       !sameMotion || !hasTimeline ||
                                       !hasBuiltNodes) {
                                        child.setChara(
                                            detail::widen(entryPoint.owner));
                                        child._motionKey = motionKey;
                                        child.loadFromSnapshot(
                                            composedSnapshot);
                                        child.playMotionLike_0x6B2284(
                                            motionKey, playFlags);
                                    }
                                } else {
                                    child.setChara(
                                        detail::widen(charaPart));
                                    child.onFindMotion(
                                        detail::widen(motionPart),
                                        playFlags);
                                }
                            } else {
                                // Single segment: binary sets chara to src itself
                                // then Player_play with raw src (no "/" prefix)
                                child.setChara(detail::widen(motionRef));
                                child.onFindMotion(detail::widen(motionRef),
                                                   playFlags);
                            }
                        }
                        // Stealth motion (0x6BE41C..0x6BE44C): binary reads from
                        // CHILD player+776, plays with flag 16, then clears child+776.
                        if (!child._stealthMotion.IsEmpty()) {
                            child.onFindMotion(child._stealthMotion, PlayFlagStealth);
                            child._stealthMotion.Clear();
                        }


                        // Time sync from parent loop time (0x6BE478..0x6BE4E8)
                        // Binary checks both _allplaying && _queuing (0x6BE478)
                        if (child._allplaying && child._queuing) {
                            // Binary at 0x6BE49C: childTime = player+1120 - slot+8 + slot+376
                            // = _frameLoopTime - clipStartTime + motionTimeOffset
                            double childTime = _frameLoopTime
                                - mn.activeSlot().clipStartTime
                                + mn.activeSlot().motionTimeOffset;
                            if (_frameLastTime < 0.0) {
                                // Backward play: handle loop wrapping
                                // Binary reads child+1136 (_loopTime) and child+1128 (_cachedTotalFrames)
                                double loopEnd = child._loopTime;
                                if (loopEnd >= 0.0) {
                                    double totalFrames = child._cachedTotalFrames;
                                    while (childTime >= totalFrames)
                                        childTime = childTime - totalFrames + loopEnd;
                                }
                            }
                            // Binary reads player+1128 directly (0x6BE4CC)
                            double totalFrames = child._cachedTotalFrames;
                            childTime = std::max(childTime, 0.0);
                            // Binary: writes unclamped time to player+1120 (0x6BE4D4)
                            child._frameLoopTime = childTime;
                            if (childTime > totalFrames) childTime = totalFrames;
                            // Binary: writes clamped time to player+456 (0x6BE4E4)
                            child._clampedEvalTime = childTime;
                            // Binary at 0x6BE4E8: writes word 0x0101 to child+480,
                            // setting both _queuing (byte+480) and _allplaying (byte+481)
                            // simultaneously. Does NOT iterate timelines.
                            child._allplaying = true;
                            child._queuing = true;
                            // Binary: if (!*(byte*)(v4 + 480)) — checks _queuing (0x6BE4EC)
                            if (!_queuing) {
                                child._needsInternalAssignImages = true;
                            }
                        }
                    }
                }

                // Binary at 0x6BE534 unconditionally proceeds to angle/state
                // propagation (no activeMotion guard). Only guard for null runtime.
                if (!child._runtime) goto label_18;

                // === Angle interpolation (0x6BE534..0x6BEC9C) ===
                int angleMode = mn.activeSlot().motionDt;
                bool hasAngle = false;
                double computedAngle = 0.0;
                const double dofst = mn.activeSlot().motionDofst;

                // Dual-slot crossfade angle interpolation (0x6BE85C..0x6BEC9C)
                // When crossfading between two clip slots, blend dofst (v37) between
                // old and new slot values using time-based ratio.
                double v37 = dofst;
                if (mn.activeSlot().motionDocmpl
                    && mn.activeSlot().crossfading
                    && !mn.otherSlot().done
                    && mn.otherSlot().motionDt != 0) {
                    // Binary at 0x6BE864: uses node+8+40 (per-node eval time) if
                    // available, else player+456 (_clampedEvalTime). NOT _frameLoopTime.
                    // Binary: *(node+8+40) — per-node eval time from player+384 array.
                    // Falls back to player+456 (_clampedEvalTime) if node+8 is null.
                    double parentTime = (i < _runtime->perNodeEvalData.size())
                        ? _runtime->perNodeEvalData[i].evalTime : _clampedEvalTime;
                    double currentStart = mn.activeSlot().clipStartTime;
                    double otherStart = mn.otherSlot().clipStartTime;
                    double denom = otherStart - currentStart;
                    // Binary divides directly without denom guard (0x6BEC6C)
                    double ratio = (parentTime - currentStart) / denom;
                    // Binary at 0x6BEC74: only checks hasEasing (slot+544).
                    if (mn.activeSlot().hasEasing) {
                        ratio = evaluateBezierCurve(mn.activeSlot().acc, ratio);
                    }
                    // Binary does NOT clamp ratio to [0,1] (0x6BEC9C).
                    double otherDofst = mn.otherSlot().motionDofst;
                    // Wrap angle difference > 180 degrees for shortest-path interpolation
                    if (dofst >= otherDofst) {
                        if (dofst - otherDofst > 180.0) otherDofst += 360.0;
                    } else {
                        if (otherDofst - dofst > 180.0) otherDofst -= 360.0;
                    }
                    v37 = otherDofst * ratio + dofst * (1.0 - ratio);
                    // Normalize to [0, 360)
                    if (v37 < 0.0) v37 += 360.0;
                    if (v37 >= 360.0) v37 -= 360.0;
                }

                if (angleMode != 0) {
                    // Case 2→3 fallthrough: binary at 0x6BE664 checks child player+608
                    // (_noUpdateYet). If set, case 2 falls through to LABEL_83 (case 3
                    // logic) because on the first frame there's no delta position yet.
                    int effectiveMode = angleMode;
                    if (angleMode == 2 && child._noUpdateYet) {
                        effectiveMode = 3;  // fallthrough to case 3 (0x6BE664→0x6BE668)
                    }

                    switch (effectiveMode) {
                    case 1: // Direct angle (0x6BE5BC)
                        // Binary does NOT normalize case 1 to [0,360).
                        computedAngle = dofst + mn.accumulated.angle;
                        hasAngle = true;
                        break;
                    case 2: { // atan2 from delta position (0x6BE8C4)
                        // Binary uses v37 (potentially interpolated) not raw dofst
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = mn.deltaPosZ; // node+192
                            dx_comp = mn.deltaPosX; // node+176
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = mn.deltaPosY; // node+184
                            dx_comp = mn.deltaPosX; // node+176
                        } else {
                            // Binary: non-0/non-1 coordinateMode → LABEL_129
                            hasAngle = true;
                            break;
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 3: { // Interpolated atan2 (LABEL_83: 0x6BE668..0x6BE79C)
                        // Binary: guard: crossfading && !otherSlotDone (0x6BE680).
                        // If guard fails → hasAngle=false (LABEL_119).
                        // Otherwise: compute ratio from parent time, call sub_69A4D4
                        // twice (at t and t+0.0001) for finite-difference derivative,
                        // then atan2 on delta based on coordinateMode.
                        if (!mn.activeSlot().crossfading
                            || mn.otherSlot().done) {
                            // Guard fails → LABEL_119: hasAngle=false
                            break;
                        }
                        // Parent time (0x6BE688..0x6BE6B0): node+8 ? *(node+8)+40 : player+456
                        // Binary: *(node+8+40) — per-node eval time from player+384 array.
                    // Falls back to player+456 (_clampedEvalTime) if node+8 is null.
                    double parentTime = (i < _runtime->perNodeEvalData.size())
                        ? _runtime->perNodeEvalData[i].evalTime : _clampedEvalTime;
                        double currentStart = mn.activeSlot().clipStartTime;
                        double otherStart = mn.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Binary divides directly without zero guard (0x6BE6D0)
                        double ratio = (parentTime - currentStart) / denom;
                        double t2 = ratio + 0.0001;
                        if (t2 >= 1.0) ratio = 0.9999;
                        t2 = std::min(t2, 1.0);
                        // sub_69A4D4: interpolate between slot positions.
                        // src = currentSlot+96 = current evaluated position
                        // dst = otherSlot+96 = position from before crossfade
                        const auto &slot = mn.activeSlot();
                        BezierCurve cccCurve;
                        cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                        ControlPointCurve cpCurve;
                        if (slot.hasCpRotation) {
                            cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                            cpCurve.t = slot.cp.t;
                        }
                        // Use crossfade slot positions: src=current, dst=other (saved at flip)
                        // Binary reads full {x,y,z} from active slot (a3+96..112).
                        double src[3] = {slot.x, slot.y, mn.activeSlot().z};
                        double dst[3] = {mn.otherSlot().x, mn.otherSlot().y, mn.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        interpolatePosition69A4D4(cccCurve, dst, src, out1, mn.coordinateMode, cpCurve, ratio);
                        interpolatePosition69A4D4(cccCurve, dst, src, out2, mn.coordinateMode, cpCurve, t2);
                        // Pick dx/dy based on coordinateMode (0x6BE72C..0x6BE740)
                        double dx_comp, dy_comp;
                        if (mn.coordinateMode == 1) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[2] - out1[2];
                        } else if (mn.coordinateMode == 0) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[1] - out1[1];
                        } else {
                            hasAngle = true;
                            break; // LABEL_129
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 4: { // Target node lookup (0x6BE7B4)
                        // Binary: hasAngle is only set to true when target found
                        // and angle computed. LABEL_119 sets hasAngle=false.
                        const auto &dtgt = mn.activeSlot().motionDtgt;
                        if (dtgt.empty()) break; // LABEL_119: hasAngle=false
                        int targetIdx = findNodeByLabel(_runtime->nodeLabelMap, dtgt);
                        if (targetIdx < 0) break; // LABEL_119: hasAngle=false
                        const auto &target = nodes[targetIdx];
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = target.accumulated.posZ - mn.accumulated.posZ;
                            dx_comp = target.accumulated.posX - mn.accumulated.posX;
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = target.accumulated.posY - mn.accumulated.posY;
                            dx_comp = target.accumulated.posX - mn.accumulated.posX;
                        } else {
                            hasAngle = true; // LABEL_129
                            break;
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    default: break; // LABEL_119: hasAngle=false
                    }
                    // Binary normalizes per-case (cases 2,3,4 each have inline loops).
                    // Case 1 does NOT normalize. Skip normalization for case 1.
                    if (effectiveMode != 1) {
                        while (computedAngle < 0.0) computedAngle += 360.0;
                        while (computedAngle >= 360.0) computedAngle -= 360.0;
                    }
                }

                // === State propagation to child root node (0x6BEA18..0x6BEB74) ===
                childRootState = computeMotionSubNodeRootState(mn);
                childRootHasAngle = hasAngle;
                childRootComputedAngle = computedAngle;
                childRootStateValid = true;
                const bool inheritedRootChanged = applyMotionSubNodeRootState(
                    child, mn, childRootState, childRootHasAngle,
                    childRootComputedAngle);
                if(inheritedRootChanged && LOGGER &&
                   motionUpdateDebugEnabled()) {
                    LOGGER->info(
                        "motion child inherited root: parent={} node={} child={} key={} parentPos=({:.3f},{:.3f},{:.3f}) parentMatrix=({:.6f},{:.6f},{:.6f},{:.6f}) origin=({:.3f},{:.3f}) childRoot=({:.3f},{:.3f},{:.3f}) childNodes={} firstUpdate={}",
                        motionPath, mn.layerName,
                        child._runtime && child._runtime->activeMotion
                            ? child._runtime->activeMotion->path
                            : std::string("<none>"),
                        detail::narrow(child._motionKey),
                        mn.accumulated.posX, mn.accumulated.posY,
                        mn.accumulated.posZ, mn.accumulated.m11,
                        mn.accumulated.m12, mn.accumulated.m21,
                        mn.accumulated.m22, mn.activeSlot().ox,
                        mn.activeSlot().oy, childRootState.posX,
                        childRootState.posY, childRootState.posZ,
                        child._runtime ? child._runtime->nodes.size() : 0,
                        child._noUpdateYet ? 1 : 0);
                }
                // Note: clip chain propagation is done in label_18 below,
                // which ALL paths (active + inactive) fall through to.

            }
            } // end childPtr scope — goto label_18 can jump here
            // Fall through to label_18 (matches binary: active path → LABEL_18)

        label_18:
            // LABEL_18: shared exit for ALL paths (0x6BE278..0x6BE2F8).
            // Binary always calls frameProgress + updateLayers on child,
            // even for inactive/non-visible nodes.
            if (auto *childP = mn.getChildPlayer()) {
                auto &child = *childP;
                if (!childRootStateValid && child._runtime) {
                    childRootState = computeMotionSubNodeRootState(mn);
                    childRootHasAngle = false;
                    childRootComputedAngle = mn.accumulated.angle;
                    childRootStateValid = true;
                }
                if (childRootStateValid) {
                    applyMotionSubNodeRootState(
                        child, mn, childRootState, childRootHasAngle,
                        childRootComputedAngle);
                }
                // A non-looping motion referenced by a motion node is sampled
                // from its parent's local time.  It is not an independent
                // clock: the slot's start/offset preserves staggered entrance
                // timing, while path variables select exact states such as
                // `select=2` or `page=1`.  Advancing these children on their
                // own made button states cycle, page numbers count forever,
                // and one-frame save cards disappear at their empty end time.
                const auto &motionSource = mn.activeSlot().src;
                const auto *childClip = child.selectActiveClip();
                const bool isTitleCastEntrance =
                    motionSource == "motion/char/show" &&
                    motionPath.find("title.psb") != std::string::npos;
                // A parameterized clip is a selector timeline: its parameter
                // chooses which nested motion is active, but it does not scrub
                // the selected motion at that selector value. Once selected,
                // the child must advance on its own clock until its one-shot
                // animation completes. This applies to ordinary UI `select`
                // clips as well as authored selectors such as `ch`, `page`,
                // and `state`.
                const auto *parentClip = selectActiveClip();
                int motionParameterIndex = mn.parameterizeIndex;
                if(motionParameterIndex < 0 && parentClip) {
                    motionParameterIndex = parentClip->defaultParameterIndex;
                }
                const bool parameterizedSelectorChild =
                    parentClip && motionParameterIndex >= 0 &&
                    static_cast<size_t>(motionParameterIndex) <
                        parentClip->parameters.size();
                const bool parentDrivenChild =
                    !motionSource.empty() && childClip && !childClip->loop &&
                    !isTitleCastEntrance && !parameterizedSelectorChild;
                if(parentDrivenChild) {
                    auto sourceLeaf = motionSource;
                    if(const auto slash = sourceLeaf.find_last_of("/\\");
                       slash != std::string::npos) {
                        sourceLeaf = sourceLeaf.substr(slash + 1);
                    }

                    std::optional<double> selectedTime;
                    // A child clip can itself be a parameterized selector.
                    // Resolve its authored parameter before falling back to
                    // the parent animation clock. The value may already have
                    // been copied into the child by phase 1, or still live on
                    // an ancestor when the child was just instantiated.
                    if(childClip->defaultParameterIndex >= 0 &&
                       static_cast<size_t>(childClip->defaultParameterIndex) <
                           childClip->parameters.size()) {
                        const auto &parameter = childClip->parameters[
                            static_cast<size_t>(
                                childClip->defaultParameterIndex)];
                        double rawValue = parameter.rangeBegin;
                        std::unordered_set<const Player *> variableOwners;
                        for(const Player *owner = &child;
                            owner && variableOwners.insert(owner).second;
                            owner = owner->_motionParentPlayer) {
                            if(owner->_runtime) {
                                if(const auto it = owner->_runtime
                                                       ->inheritedVariableInputs
                                                       .find(parameter.id);
                                   it != owner->_runtime
                                             ->inheritedVariableInputs.end()) {
                                    rawValue = it->second;
                                    break;
                                }
                            }
                            if(const auto it =
                                   owner->_evalResultValues.find(parameter.id);
                               it != owner->_evalResultValues.end()) {
                                rawValue = it->second;
                                break;
                            }
                            if(const auto it =
                                   owner->_variableValues.find(parameter.id);
                               it != owner->_variableValues.end()) {
                                rawValue = it->second;
                                break;
                            }
                        }
                        // The native selector defaults to its range beginning
                        // even before a script explicitly binds the variable.
                        // Keeping that default prevents selector clips from
                        // accidentally advancing as ordinary animations.
                        selectedTime = detail::parameterizedClipTime(
                            *childClip, parameter, rawValue);
                    }
                    if(!mn.layerName.empty()) {
                        if(!selectedTime) {
                            if(const auto it =
                                   _variableValues.find(mn.layerName);
                           it != _variableValues.end()) {
                                selectedTime = it->second;
                            }
                        }
                    }
                    if(!selectedTime && !sourceLeaf.empty()) {
                        if(const auto it = _variableValues.find(sourceLeaf);
                           it != _variableValues.end()) {
                            selectedTime = it->second;
                        }
                    }

                    double childTime = selectedTime.value_or(
                        currentTime - mn.activeSlot().clipStartTime +
                        mn.activeSlot().motionTimeOffset);
                    childTime = std::max(0.0, childTime);
                    const double totalFrames = childClip->totalFrames > 0.0
                        ? childClip->totalFrames
                        : child._cachedTotalFrames;
                    child._frameLoopTime = childTime;
                    child._loopTime = childTime;

                    double renderTime = childTime;
                    if(totalFrames > 0.0 && renderTime >= totalFrames) {
                        renderTime = std::max(
                            0.0, std::nextafter(totalFrames, 0.0));
                    }

                    // The numbered title character clips contain an authored
                    // transition-out tail after frame 70.  The title's parent
                    // keeps those cards on screen, so retain the fully visible
                    // pose while still honoring each card's staggered start.
                    if(childClip->label == "charmove" &&
                       child._motionParentPlayer &&
                       detail::narrow(child._motionParentPlayer->_chara)
                               .rfind("title_bg", 0) == 0) {
                        renderTime = std::min(renderTime, 70.0);
                    }

                    bool sampledStateChanged =
                        child._clampedEvalTime != renderTime;
                    child._clampedEvalTime = renderTime;
                    if(child._runtime) {
                        if(auto stateIt = child._runtime->timelines.find(
                               childClip->label);
                           stateIt != child._runtime->timelines.end()) {
                            auto &state = stateIt->second;
                            sampledStateChanged =
                                sampledStateChanged ||
                                state.currentTime != renderTime ||
                                state.playing || state.wasPlaying;
                            state.currentTime = renderTime;
                            state.playing = false;
                            state.wasPlaying = false;
                        }
                        const auto playingIt = std::remove(
                            child._runtime->playingTimelineLabels.begin(),
                            child._runtime->playingTimelineLabels.end(),
                            childClip->label);
                        sampledStateChanged =
                            sampledStateChanged ||
                            playingIt !=
                                child._runtime->playingTimelineLabels.end();
                        child._runtime->playingTimelineLabels.erase(
                            playingIt,
                            child._runtime->playingTimelineLabels.end());
                    }
                    child._queuing = true;
                    child._allplaying = child.hasPlayingChildPlayers();
                    // The sampled child itself remains pinned to the parent,
                    // but independently playing descendants still need the
                    // root tick's delta (button -> selected over/out motion).
                    child._frameLastTime = _frameLastTime;
                    if(sampledStateChanged) {
                        child._emoteDirty = true;
                    }
                } else {
                    // Looping/standalone children keep their own clock.
                    child.frameProgress(_frameLastTime);
                }
                child.ensureNodeTreeBuilt();
                if (childRootStateValid) {
                    applyMotionSubNodeRootState(
                        child, mn, childRootState, childRootHasAngle,
                        childRootComputedAngle);
                }
                if (child._runtime && !child._runtime->nodes.empty()) {
                    auto &cr = child._runtime->nodes[0];
                    // Clip chain propagation (0x6BE278..0x6BE29C)
                    // Binary: v17+1936 = v10+1936 (parentClipIndex)
                    //         v18 = v10; if (!node+1963) v18 = *(v10+1968)
                    //         v17+1968 = v18 (visibleAncestor with conditional)
                    //         v17+1952 = v10+1952 (third field — not mapped in our arch)
                    // parentClipIndex is local to a Player node array. The
                    // binary stores a node pointer here, but copying our
                    // integer into a child array can alias an unrelated child
                    // node and create a cycle. Parent transforms are already
                    // propagated through the child root state above.
                    cr.parentClipIndex = -1;
                    // Native stores an ancestor pointer. Our integer indices are
                    // local to each Player, so parent-runtime indices must not be
                    // copied into the child node array. The external ancestor is
                    // attached when child render items are flattened below.
                    cr.visibleAncestorIndex = -1;
                    // Binary 0x6BE29C: propagates node+1952 (forceVisible) to child root
                    cr.forceVisible = mn.forceVisible;
                }

                // A Motion node is an ownership edge in the native player
                // tree, not a general graph edge.  Malformed/compatibility
                // selector data can otherwise resolve a descendant back to
                // the same snapshot + clip pair as one of its ancestors.
                // Recursing through that pair creates Players forever and
                // eventually exhausts memory.  Keep the resolved child (so
                // queries retain the same shape), but do not traverse a
                // cyclic ownership edge.
                bool cyclicMotionOwnership = false;
                int ownershipDepth = 0;
                for(const Player *ancestor = child._motionParentPlayer;
                    ancestor; ancestor = ancestor->_motionParentPlayer) {
                    ++ownershipDepth;
                    if(child._runtime && ancestor->_runtime &&
                       child._runtime->activeMotion &&
                       ancestor->_runtime->activeMotion &&
                       sameMotionOwnershipIdentity(
                           child._runtime->activeMotion->path,
                           child._chara, child._motionKey,
                           ancestor->_runtime->activeMotion->path,
                           ancestor->_chara, ancestor->_motionKey)) {
                        cyclicMotionOwnership = true;
                        break;
                    }
                    // Native E-mote/motion assets are shallow ownership
                    // trees.  A dozen nested Players already exceeds every
                    // shipped KamiGAL/Nekopara hierarchy and is therefore a
                    // malformed selector cycle even when each lookup made a
                    // fresh snapshot object.
                    if(ownershipDepth >= 12) {
                        cyclicMotionOwnership = true;
                        break;
                    }
                }
                if(cyclicMotionOwnership) {
                    child._allplaying = false;
                    child._queuing = false;
                    if(LOGGER && std::getenv("AETHERKIRI_MOTION_DEBUG")) {
                        LOGGER->warn(
                            "motion child cycle suppressed: parent={} node={} child={} chara={} motion={} depth={}",
                            motionPath, mn.layerName,
                            child._runtime && child._runtime->activeMotion
                                ? child._runtime->activeMotion->path
                                : std::string("<none>"),
                            detail::narrow(child._chara),
                            detail::narrow(child._motionKey),
                            ownershipDepth);
                    }
                    continue;
                }
                const bool childNeedsUpdate =
                    child._noUpdateYet || child._layersDirty ||
                    child._emoteDirty || child._allplaying ||
                    child.hasPlayingChildPlayers();
                if(childNeedsUpdate) {
                    childPlayersNeedingUpdate.push_back(&child);
                }
            }
        }

        if(childPlayersNeedingUpdate.empty()) {
            return;
        }

        int ownershipDepth = 0;
        for(const Player *ancestor = _motionParentPlayer;
            ancestor; ancestor = ancestor->_motionParentPlayer) {
            ++ownershipDepth;
        }
        const bool warmedChildren = std::all_of(
            childPlayersNeedingUpdate.begin(),
            childPlayersNeedingUpdate.end(),
            [](const Player *child) {
                return child && !child->_noUpdateYet && child->_runtime &&
                    child->_runtime->nodesBuilt &&
                    !child->_runtime->nodes.empty();
            });
        const bool canParallelizeSiblings =
            _runtime->isEmoteMode && ownershipDepth == 1 &&
            childPlayersNeedingUpdate.size() >= 2 && warmedChildren &&
            !detail::logoChainTraceEnabled(_runtime->activeMotion) &&
            !motionUpdateDebugEnabled();
        if(canParallelizeSiblings) {
            const auto childCount =
                static_cast<std::ptrdiff_t>(
                    childPlayersNeedingUpdate.size());
#pragma omp parallel for schedule(static)
            for(std::ptrdiff_t childIndex = 0;
                childIndex < childCount; ++childIndex) {
                childPlayersNeedingUpdate[
                    static_cast<size_t>(childIndex)]->updateLayers();
            }
        } else {
            for(Player *child : childPlayersNeedingUpdate) {
                child->updateLayers();
            }
        }

    }

    bool Player::applyMotionParentRootStateForRender() {
        if(!_runtime || !_motionParentPlayer ||
           !_motionParentPlayer->_runtime ||
           _motionParentNodeIndex < 0 ||
           _motionParentNodeIndex >=
               static_cast<int>(_motionParentPlayer->_runtime->nodes.size())) {
            return false;
        }

        const auto &parentNode =
            _motionParentPlayer->_runtime->nodes[_motionParentNodeIndex];

        double posX = parentNode.accumulated.posX;
        double posY = parentNode.accumulated.posY;
        double posZ = parentNode.accumulated.posZ;

        const double originX = parentNode.activeSlot().ox;
        const double originY = parentNode.activeSlot().oy;
        if(originX != 0.0 || originY != 0.0) {
            const double negOY = -originY;
            const double vx =
                parentNode.accumulated.m12 * negOY -
                originX * parentNode.accumulated.m11;
            const double vy =
                parentNode.accumulated.m22 * negOY -
                originX * parentNode.accumulated.m21;
            if(parentNode.coordinateMode == 1) {
                posX += vx;
                posZ += vy;
            } else {
                posX += vx;
                posY += vy;
            }
        }

        _pendingRootX = posX;
        _pendingRootY = posY;
        _pendingRootZ = posZ;
        _hasPendingRootPos = true;
        _zFactor = _motionParentPlayer->_zFactor;
        _runtime->drawAffineMatrix =
            _motionParentPlayer->_runtime->drawAffineMatrix;
        {
            uint32_t packed;
            std::memcpy(&packed, &parentNode.colorBytes[0],
                        sizeof(uint32_t));
            _colorWeightPacked = packed;
        }

        if(_runtime->nodes.empty()) {
            return true;
        }

        auto &root = _runtime->nodes[0];
        root.localState.posX = posX;
        root.localState.posY = posY;
        root.localState.posZ = posZ;
        root.localState.flipX = parentNode.accumulated.flipX;
        root.localState.flipY = parentNode.accumulated.flipY;
        root.localState.scaleX = parentNode.accumulated.scaleX;
        root.localState.scaleY = parentNode.accumulated.scaleY;
        root.localState.slantX = parentNode.accumulated.slantX;
        root.localState.slantY = parentNode.accumulated.slantY;
        root.localState.opacity = parentNode.accumulated.opacity;
        root.localState.active = parentNode.accumulated.active;
        root.localState.visible = parentNode.accumulated.visible;
        root.localState.dirty = true;

        root.accumulated.posX = posX;
        root.accumulated.posY = posY;
        root.accumulated.posZ = posZ;
        root.accumulated.flipX = parentNode.accumulated.flipX;
        root.accumulated.flipY = parentNode.accumulated.flipY;
        root.accumulated.scaleX = parentNode.accumulated.scaleX;
        root.accumulated.scaleY = parentNode.accumulated.scaleY;
        root.accumulated.slantX = parentNode.accumulated.slantX;
        root.accumulated.slantY = parentNode.accumulated.slantY;
        root.accumulated.opacity = parentNode.accumulated.opacity;
        root.accumulated.active = parentNode.accumulated.active;
        root.accumulated.visible = parentNode.accumulated.visible;
        root.accumulated.m11 = parentNode.accumulated.m11;
        root.accumulated.m12 = parentNode.accumulated.m12;
        root.accumulated.m21 = parentNode.accumulated.m21;
        root.accumulated.m22 = parentNode.accumulated.m22;
        root.accumulated.dirty = true;

        return true;
    }

    void Player::updateLayersPhase3_ParticleEmitter() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BEDD0: Particle emitter state (nodeType=6) ---
        // Aligned to 0x6BEDD0. Only when !isEmoteMode.
        if (_runtime->isEmoteMode) return;

        for (size_t ei = 1; ei < nodes.size(); ++ei) {
            auto &en = nodes[ei];
            if (en.nodeType != 6) continue;

            // Active/slotDone guard (0x6BEE90..0x6BEEC4)
            if (!en.accumulated.active || en.activeSlot().done) {
                en.emitterActive = false;
                en.emitterDtgt.clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // dtgt from clip slot (node+536*slot+356, our activeSlot().src)
            const std::string &dtgt = en.activeSlot().src;
            if (dtgt.empty()) {
                en.emitterActive = false;
                en.emitterDtgt.clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // Flags gate + re-resolve logic (0x6BEED8..0x6BEF9C)
            // Binary checks whole byte at node+44: LDRB W9,[X21,#0x2C]; CBZ W9
            // If flags==0: always LABEL_27 (continue, just accumulate timer).
            // If flags!=0: check emitterActive + dtgt comparison.
            bool doAccumulate; // true=LABEL_27 (timer += dt), false=LABEL_21 (re-resolve)

            if (!en.flags) {
                // node+44 flags == 0: skip re-resolve → LABEL_27 (0x6BEEE0)
                doAccumulate = true;
            } else if (!en.emitterActive) {
                // First init → LABEL_21 (0x6BEEFC)
                doAccumulate = false;
            } else if (en.emitterDtgt == dtgt) {
                // Same dtgt (pointer or string compare) → LABEL_27 (0x6BEEF8)
                doAccumulate = true;
            } else {
                // dtgt changed → LABEL_21
                doAccumulate = false;
            }

            if (doAccumulate) {
                // LABEL_27 (0x6BEF88): emitterTimer = _frameLastTime + emitterTimer
                en.emitterTimer += _frameLastTime;
            } else {
                // LABEL_21 (0x6BEF48): re-resolve dtgt, compute time offset.
                // Binary does NOT flip activeSlotIndex here (v10 is read once at
                // 0x6BEE9C and never modified). No crossfading flag is set either.
                en.emitterActive = true;
                en.emitterDtgt = dtgt;
                // Timer = (parentTime - clipSlot.startTime) + clipSlot.timeOffset
                // Aligned to 0x6BEF74..0x6BEFA8:
                //   parentTime = node+8 ? *(node+8+40) : player+1120 (_frameLoopTime)
                // Binary: node+8 is per-node eval data pointer. Offset 40 = evalTime.
                // Falls back to player+1120 (_frameLoopTime) if null.
                double parentTime = (ei < _runtime->perNodeEvalData.size())
                    ? _runtime->perNodeEvalData[ei].evalTime : _frameLoopTime;
                double startTime = en.activeSlot().clipStartTime;
                double timeOffset = en.activeSlot().motionTimeOffset;
                en.emitterTimer = (parentTime - startTime) + timeOffset;
            }

            // Binary: emitterOffsetActive = false AFTER branch convergence (0x6BEFB0)
            en.emitterOffsetActive = false;

            // Trigger type handling (0x6BEFC4..0x6BF0B8)
            // triggerType from clipSlot (node+536*slot+708)
            const int triggerType = en.activeSlot().prtTrigger;

            switch (triggerType) {
            case 4: {
                // Target position offset (0x6BF048..0x6BF0B8)
                // sub_6F2228 resolves target node by name from slot+712 (motionDtgt).
                // Compute position difference: target.pos - emitter.pos
                int targetIdx = findNodeByLabel(_runtime->nodeLabelMap, en.activeSlot().motionDtgt);
                if (targetIdx >= 0 && targetIdx < static_cast<int>(nodes.size())) {
                    auto &target = nodes[targetIdx];
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = target.accumulated.posX - en.accumulated.posX;
                    en.emitterOffsetY = target.accumulated.posY - en.accumulated.posY;
                    en.emitterOffsetZ = target.accumulated.posZ - en.accumulated.posZ;
                }
                break;
            }
            case 3: {
                // LABEL_36 (0x6BF028): sub_6C1540 equivalent.
                // sub_6C1540 guard at 0x6C1574: *(a3+25) [crossfading] && !*(a4+24) [otherSlotDone].
                // ratio at 0x6C15A8: (player+456 - currentSlot.startTime) / (otherSlot.startTime - currentSlot.startTime)
                // src = currentSlot+96 (current evaluated position), dst = otherSlot+96 (saved at crossfade start).
                if (en.activeSlot().crossfading && !en.otherSlot().done) {
                    const auto &slot = en.activeSlot();
                    double currentStart = slot.clipStartTime;
                    double otherStart = en.otherSlot().clipStartTime;
                    double denom = otherStart - currentStart;
                    // Binary divides directly without denom!=0 guard (0x6C15A8)
                    constexpr double epsilon = 0.0001;
                    double ratio = (_clampedEvalTime - currentStart) / denom;
                    double t2 = ratio + epsilon;
                    if (t2 >= 1.0) ratio = 0.9999;
                    t2 = std::min(t2, 1.0);
                    BezierCurve cccCurve;
                    cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                    ControlPointCurve cpCurve;
                    if (slot.hasCpRotation) {
                        cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                        cpCurve.t = slot.cp.t;
                    }
                    // src = current slot position, dst = other slot position (saved at flip)
                    // Binary reads full {x,y,z} from active slot (a3+96..112).
                    double src[3] = {slot.x, slot.y, en.activeSlot().z};
                    double dst[3] = {en.otherSlot().x, en.otherSlot().y, en.otherSlot().z};
                    double out1[3] = {}, out2[3] = {};
                    interpolatePosition69A4D4(cccCurve, dst, src, out1,
                        en.coordinateMode, cpCurve, ratio);
                    interpolatePosition69A4D4(cccCurve, dst, src, out2,
                        en.coordinateMode, cpCurve, t2);
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = out2[0] - out1[0];
                    en.emitterOffsetY = out2[1] - out1[1];
                    en.emitterOffsetZ = out2[2] - out1[2];
                }
                break;
            }
            case 2: {
                // (0x6BEFF0..0x6BF020)
                // Binary checks player+608 (_noUpdateYet) OR emitterTimer==0 (0x6BEFF4)
                if (_noUpdateYet || en.emitterTimer == 0.0) {
                    // Queuing or zero timer → same as case 3: sub_6C1540
                    // sub_6C1540 guard: crossfading && !otherSlotDone (0x6C1574)
                    if (en.activeSlot().crossfading && !en.otherSlot().done) {
                        const auto &slot = en.activeSlot();
                        double currentStart = slot.clipStartTime;
                        double otherStart = en.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Binary divides directly without denom!=0 guard (0x6C15A8)
                        constexpr double epsilon = 0.0001;
                        double ratio = (_clampedEvalTime - currentStart) / denom;
                        double t2 = ratio + epsilon;
                        if (t2 >= 1.0) ratio = 0.9999;
                        t2 = std::min(t2, 1.0);
                        BezierCurve cccCurve;
                        cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                        ControlPointCurve cpCurve;
                        if (slot.hasCpRotation) {
                            cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                            cpCurve.t = slot.cp.t;
                        }
                        double src[3] = {slot.x, slot.y, en.activeSlot().z};
                        double dst[3] = {en.otherSlot().x, en.otherSlot().y, en.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        interpolatePosition69A4D4(cccCurve, dst, src, out1,
                            en.coordinateMode, cpCurve, ratio);
                        interpolatePosition69A4D4(cccCurve, dst, src, out2,
                            en.coordinateMode, cpCurve, t2);
                        en.emitterOffsetActive = true;
                        en.emitterOffsetX = out2[0] - out1[0];
                        en.emitterOffsetY = out2[1] - out1[1];
                        en.emitterOffsetZ = out2[2] - out1[2];
                    }
                } else {
                    // Non-queuing, timer running: binary reads node+176/184/192
                    // directly (0x6BF004..0x6BF020), which ARE deltaPosX/Y/Z.
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = en.deltaPosX;
                    en.emitterOffsetY = en.deltaPosY;
                    en.emitterOffsetZ = en.deltaPosZ;
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void Player::updateLayersPhase3_ParticleSystem(double currentTime) {
        // --- sub_6BF0DC: Particle system (nodeType=4) ---
        // Fully aligned to libkrkr2.so 0x6BF0DC (~800 lines decompiled).
        // Velocity stored on child Player _cameraVelocityX/Y/Z (player+784/792/800).
        // frameProgress + updateLayersPhase1_PreLoop auto-applies velocity+damping.
        if (_runtime->isEmoteMode) return;
        auto &nodes = _runtime->nodes;
        const double dt = _frameLastTime;
        constexpr double PI = 3.14159265358979323846;

        for (size_t pi = 1; pi < nodes.size(); ++pi) {
            auto &pn = nodes[pi];
            if (pn.nodeType != 4) continue;

            // Binary flow: BLOCK 1 (child position update) runs BEFORE the LABEL_64
            // activity check. The activity check only gates BLOCK 2 (emission control).
            // Existing particles ALWAYS get position updates even when inactive/done.

            const int childCount = pn.getParticleCount();

            // ====== BLOCK 1: Existing particle update (0x6BF310..0x6BF668) ======
            // Binary guard: particleInheritVelocity==2 gates ALL child position updates (0x6BF304).
            // If != 2: goto LABEL_64 (skip ALL child position updates).
            // If == 2: check !slotDone && particleInheritAngle for full matrix update;
            // otherwise just add deltaPos to existing children (0x6BF32C..0x6BF384).
            if (pn.particleInheritVelocity == 2 && childCount >= 1 && !pn.activeSlot().done && pn.particleInheritAngle) {
                const double curM11 = pn.accumulated.m11, curM21 = pn.accumulated.m21;
                const double curM12 = pn.accumulated.m12, curM22 = pn.accumulated.m22;

                const bool matrixChanged =
                    (curM11 != pn.prevM11 || curM21 != pn.prevM21 ||
                     curM12 != pn.prevM12 || curM22 != pn.prevM22);

                if (matrixChanged) {
                    // Compute inv(prev) * cur (0x6BF458..0x6BF49C)
                    // Binary divides each element by det WITHOUT negation,
                    // then computes the product as subtraction pairs.
                    // This is inv(prev) * cur, NOT cur * inv(prev).
                    const double det = pn.prevM11 * pn.prevM22 - pn.prevM12 * pn.prevM21;
                    {
                        const double id = 1.0 / det;
                        const double id_m22 = pn.prevM22 * id;  // v34
                        const double id_m21 = pn.prevM21 * id;  // v35 (no negation)
                        const double id_m12 = pn.prevM12 * id;  // v36 (no negation)
                        const double id_m11 = pn.prevM11 * id;  // v37
                        // inv(prev) * cur coefficients (0x6BF490..0x6BF49C)
                        const double t11 = curM11 * id_m22 - curM21 * id_m12;  // v39
                        const double t12 = curM21 * id_m11 - curM11 * id_m21;  // v40
                        const double t21 = curM12 * id_m22 - curM22 * id_m12;  // v41
                        const double t22 = curM22 * id_m11 - curM12 * id_m21;  // v42

                        // Angle delta (0x6BF404..0x6BF43C)
                        // Binary reads node+1536 = accumulated.angle, not interpolated
                        const double curAngle = pn.accumulated.angle;
                        double angleDelta = curAngle - pn.prevParticleAngle;
                        if (pn.accumulated.flipX == pn.accumulated.flipY)
                            angleDelta = curAngle - pn.prevParticleAngle;
                        else
                            angleDelta = -(curAngle - pn.prevParticleAngle);
                        pn.prevParticleAngle = curAngle;

                        const double posXref = pn.accumulated.posX;
                        const double posYref = pn.accumulated.posY;
                        const double posZref = pn.accumulated.posZ;
                        const double dPosX = pn.deltaPosX, dPosY = pn.deltaPosY;
                        const double dPosZ = pn.deltaPosZ;

                        for (int ci = 0; ci < childCount; ++ci) {
                            auto *child = pn.getParticleChild(ci);
                            if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                            auto &cr = child->_runtime->nodes[0];

                            // Rotate child angle (0x6BF4C4..0x6BF528)
                            // Binary checks child._directEdit (player+482) for emote path.
                            // If _directEdit: writes to player+464 and calls initEmoteMotion.
                            // If not: writes to root node accumulated.angle.
                            if (child->_directEdit) {
                                // Emote angle path — not applicable in web port
                                // player+464 = emote angle, Player_initEmoteMotion(child, 2)
                            } else {
                                double cAngle = cr.accumulated.angle + angleDelta;
                                while (cAngle < 0.0) cAngle += 360.0;
                                while (cAngle >= 360.0) cAngle -= 360.0;
                                cr.accumulated.angle = cAngle;
                            }

                            // Transform position (0x6BF54C..0x6BF620)
                            const int coordMode = pn.coordinateMode;
                            if (coordMode == 1) {
                                // 3D: X/Z through matrix, Y pass-through
                                double px = cr.accumulated.posX - posXref + dPosX;
                                double pz = cr.accumulated.posZ - posZref + dPosZ;
                                cr.accumulated.posX = posXref + t11 * px + t12 * pz;
                                cr.accumulated.posZ = posZref + t21 * px + t22 * pz;
                                cr.accumulated.posY += dPosY;
                            } else {
                                // 2D: Binary swaps X↔Z (0x6BF5D0..0x6BF620):
                                //   newPosX = oldPosZ + deltaPosZ
                                //   newPosY = posYref + t21*px + t22*py
                                //   newPosZ = posXref + t11*px + t12*py
                                double px = cr.accumulated.posX - posXref + dPosX;
                                double py = cr.accumulated.posY - posYref + dPosY;
                                cr.accumulated.posX = cr.accumulated.posZ + dPosZ;
                                cr.accumulated.posY = posYref + t21 * px + t22 * py;
                                cr.accumulated.posZ = posXref + t11 * px + t12 * py;
                            }

                            // Transform velocity (0x6BF628..0x6BF64C)
                            double vx = child->_cameraVelocityX;
                            double vy = (coordMode == 1) ? child->_cameraVelocityZ
                                                         : child->_cameraVelocityY;
                            double nvx = t11 * vx + t12 * vy;
                            double nvy = t21 * vx + t22 * vy;
                            child->_cameraVelocityX = nvx;
                            if (coordMode == 1) child->_cameraVelocityZ = nvy;
                            else child->_cameraVelocityY = nvy;
                        }
                    }
                    pn.prevM11 = curM11; pn.prevM21 = curM21;
                    pn.prevM12 = curM12; pn.prevM22 = curM22;
                } else {
                    // Matrix unchanged: just add delta position (0x6BF348..0x6BF384)
                    for (int ci = 0; ci < childCount; ++ci) {
                        auto *child = pn.getParticleChild(ci);
                        if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                        auto &cr = child->_runtime->nodes[0];
                        cr.accumulated.posX += pn.deltaPosX;
                        cr.accumulated.posY += pn.deltaPosY;
                        cr.accumulated.posZ += pn.deltaPosZ;
                    }
                }
            } else if (pn.particleInheritVelocity == 2 && childCount >= 1) {
                // Missing path from binary (0x6BF32C..0x6BF384):
                // When particleInheritVelocity==2 but (slotDone || !particleInheritAngle),
                // still add deltaPos to existing children's positions.
                for (int ci = 0; ci < childCount; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                    auto &cr = child->_runtime->nodes[0];
                    cr.accumulated.posX += pn.deltaPosX;
                    cr.accumulated.posY += pn.deltaPosY;
                    cr.accumulated.posZ += pn.deltaPosZ;
                }
            }
            // Binary: when particleInheritVelocity != 2, goto LABEL_64 (0x6BF314)
            // skips ALL child position updates — no deltaPos addition.

            // ====== LABEL_64: Activity check (0x6BF668..0x6BF710) ======
            // Binary: only !accumulated.active sets particleEmitterFlagActive=false.
            // slotDone alone does NOT reset the flag — it just skips emission.
            // emitCount declared here so goto doesn't cross initialization.
            {
            int emitCount = 0;
            if (!pn.accumulated.active) {
                pn.particleEmitterFlagActive = false;
                goto physics_step;
            }

            // ====== BLOCK 2: Emission control (0x6BF668..0x6BF810) ======
            // Binary: slotDone skips emission but does NOT reset particleEmitterFlagActive.
            if (pn.activeSlot().done) goto physics_step;
            {
                const double prtFmin = pn.activeSlot().prtFmin;
                const double prtF = pn.activeSlot().prtF;
                const int prtTrigger = pn.activeSlot().prtTrigger;

                if (prtTrigger == 0 && prtFmin == 0.0) goto physics_step;

                const bool wasActive = pn.particleEmitterFlagActive;
                pn.particleEmitterFlagActive = true;

                // Read trigger type from slot (0x6BF680..0x6BF690)
                const int triggerType = pn.prtTrigger;

                if (triggerType == 0) {
                    // Frequency mode (0x6BF690..0x6BF7F4)
                    if (!wasActive) {
                        // First frame: initialize timer (0x6BF7BC..0x6BF7EC)
                        // Binary interpolates in frequency domain: lerp(60/prtFmin, 60/prtF, r)
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum = freq0;
                    }
                    // Timer loop (0x6BF698..0x6BF6F8)
                    pn.emitterTimerAccum -= dt;
                    while (pn.emitterTimerAccum <= 0.0) {
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum += freq0;
                        ++emitCount;
                    }
                    // LABEL_85 timer clamp (0x6BF780..0x6BF7B8)
                    // Only for frequency mode (triggerType==0).
                    // Clamps timer to min(60/prtFmin, currentTimer).
                    if (prtFmin > 0.0) {
                        double maxTimer = 60.0 / prtFmin;
                        if (maxTimer > pn.emitterTimerAccum)
                            maxTimer = pn.emitterTimerAccum;
                        pn.emitterTimerAccum = maxTimer;
                        if (emitCount <= 0) goto physics_step;
                    }
                } else if (triggerType == 1) {
                    // Count mode (0x6BF734..0x6BF804)
                    // Binary checks node+44 (flags byte, v173) not particleInheritAngle.
                    if (pn.flags) {
                        double r = random();
                        emitCount = static_cast<int>(prtFmin + (prtF - prtFmin) * r);
                    }
                    // Timer clamp is NOT applied for triggerType==1 in binary.
                    // LABEL_85 (0x6BF780) is only reachable from the frequency mode path.
                    if (emitCount <= 0) goto physics_step;
                }
            }

            // ====== BLOCK 3: Particle creation (0x6BF810..0x6C02DC) ======
            // Binary creates exactly 1 particle per frame per node.
            // When srcList is empty (v85==0), skip creation and run ONE physics step (0x6C02D0).
            // When emitCount > 1, physics is skipped; next frame creates another particle.
            if (emitCount > 0) {
                // 3a. Resolve srcList count (0x6BF810..0x6BF87C)
                const auto &srcList = pn.activeSlot().srcList;
                const int srcListCount = static_cast<int>(srcList.size());

                // Binary: if srcList count==0, skip creation entirely (0x6C02D0).
                // The binary decrements emitCount to 0 (a no-op loop), then runs
                // ONE physics step. No particles are created.
                if (srcListCount == 0) {
                    goto physics_step;
                }

                // Binary creates exactly 1 particle per frame per node (0x6BF810..0x6C02DC).
                // emitCount > 1 just means "skip physics this frame" (0x6C0270).
                {

                // Random selection from srcList (0x6BF87C: v86 = random() * v85)
                int idx = static_cast<int>(random() * srcListCount);
                if (idx >= srcListCount) idx = srcListCount - 1;
                const std::string &selectedSrc = srcList[idx];
                if (selectedSrc.empty()) goto physics_step;

                // Handle "chara/motion" format (binary: sub_697D34 splits by "/")
                std::string particleChara;
                std::string motionPath;
                auto slashPos = selectedSrc.find('/');
                if (slashPos != std::string::npos) {
                    particleChara = selectedSrc.substr(0, slashPos);
                    motionPath = selectedSrc.substr(slashPos + 1);
                } else {
                    motionPath = selectedSrc;
                }

                // 3b. Create child Player via TJS dispatch (0x6BF93C..0x6BFA00)
                // Aligned to binary: new Player → CreateAdaptor → Array.add
                using PlayerAdaptor = ncbInstanceAdaptor<Player>;
                auto *childRaw = new Player(_resourceManagerNative);
                childRaw->_tjsRandomGenerator = _tjsRandomGenerator;
                iTJSDispatch2 *childDisp = PlayerAdaptor::CreateAdaptor(childRaw);
                if (!childDisp) { delete childRaw; goto physics_step; }
                tTJSVariant childVar(childDisp, childDisp);
                childDisp->Release();
                auto *child = childRaw;  // native pointer for subsequent use
                // Binary: chara comes from the split path, not parent chara
                child->setChara(particleChara.empty() ? _chara : detail::widen(particleChara));
                child->onFindMotion(detail::widen(motionPath));
                // Stealth motion (0x6BFA08..0x6BFA40): binary checks child+776
                // (stealth motion path) and plays it with flag 0x10. In our arch,
                // propagate parent stealth fields so child resolves stealth if set.
                child->_stealthChara = _stealthChara;
                child->_stealthMotion = _stealthMotion;
                if (!_stealthMotion.IsEmpty()) {
                    child->onFindMotion(_stealthMotion, PlayFlagStealth);
                }
                // colorWeight propagation (player+1156, 0x6BF9B4)
                {
                    uint32_t packed;
                    std::memcpy(&packed, &pn.colorBytes[0], sizeof(uint32_t));
                    child->_colorWeightPacked = packed;
                }
                // emoteEdit propagation (0x6BF9C0..0x6BF9D4)
                child->_emoteEditVariant = _emoteEditVariant;
                child->_zFactor = _zFactor;
                child->_independentLayerInherit = _independentLayerInherit;

                // Set blendMode on child root node accumulated state (0x6BFAA8..0x6BFAC4)
                // Binary writes to *(v99+1656) = root node accumulated blendMode, not activeSlot.
                if (child->_runtime && !child->_runtime->nodes.empty()) {
                    auto &cr = child->_runtime->nodes[0];
                    auto blendVal = pn.activeSlot().blendMode;
                    if (cr.accumulated.blendMode != blendVal) {
                        cr.accumulated.dirty = true;
                        cr.accumulated.blendMode = blendVal;
                    }
                }

                // Stealth motion play (0x6BFA08..0x6BFA40)
                // Binary: if child+776 (stealth path) exists, play it with flags=16
                // In our architecture, stealth is stored at player level.
                // The binary copies the stealth path from the resource manager state.
                // For web port, stealth motion is rarely used; skip for now.

                // 3c. Position based on flyDirection (0x6BFAC8..0x6BFC88)
                double offX = 0, offY = 0, offZ = 0;
                // Binary uses "particle" field (node+2164, PSB key "particle") for fly
                // direction, NOT particleFlyDirection (node+2180). 0x6BFAC8.
                const int flyDir = pn.particleType;
                // Binary uses node+2189 (particleTriVolume, PSB key), not coordinateMode.
                const bool has3D = pn.particleTriVolume;

                if (flyDir == 2) {
                    // Uniform box (0x6BFB88..0x6BFBCC)
                    // Binary RNG order: r1→offX (v110→v168), r2→offY (v167) (0x6BFB88)
                    double r1 = random();
                    offY = random() * 32.0 - 16.0;  // r2→offY (v167)
                    offX = r1 * 32.0 - 16.0;         // r1→offX (v168)
                    if (has3D) offZ = random() * 32.0 - 16.0;
                } else if (flyDir == 1) {
                    // 3D sphere (0x6BFAE4..0x6BFB78)
                    if (has3D) {
                        double r1 = random(), r2 = random(), r3 = random();
                        double phi = r2 * 2.0 * PI;
                        double theta = r1 * 2.0 * PI;
                        double radius = std::cbrt(r3) * 16.0;
                        double cosPhi = std::cos(phi);
                        offX = cosPhi * (radius * std::cos(theta));
                        offY = radius * (cosPhi * std::sin(theta));
                        offZ = radius * std::sin(phi);
                    } else {
                        // 2D disk (0x6BFC14..0x6BFC48)
                        double angle2d = random() * 2.0 * PI;
                        double radius = std::sqrt(random()) * 16.0;
                        offX = std::cos(angle2d) * radius;
                        offY = radius * std::sin(angle2d);
                    }
                } else {
                    // flyDir == 0 or other: offX=offY=0 (0x6BFBD8)
                    offX = 0.0;
                    offY = 0.0;
                }

                // Z component scale by sqrt(det(matrix)) (0x6BFC64..0x6BFC88)
                // Binary does sqrt(det) without abs — NaN for negative det.
                if (offZ != 0.0) {
                    const double det = pn.accumulated.m11 * pn.accumulated.m22
                                     - pn.accumulated.m12 * pn.accumulated.m21;
                    offZ *= std::sqrt(det);
                }

                // Transform offset through parent matrix (0x6BFCE0..0x6BFCE8)
                const double m11 = pn.accumulated.m11, m21 = pn.accumulated.m21;
                const double m12 = pn.accumulated.m12, m22 = pn.accumulated.m22;
                const double clipOX = pn.clipOriginX, clipOY = pn.clipOriginY;
                const double txOff = m11 * (offX - clipOX) + m12 * (offY - clipOY);
                const double tyOff = m21 * (offX - clipOX) + m22 * (offY - clipOY);

                // 3d. Speed = lerp(prtVmin, prtV, random()) (0x6BFC94..0x6BFCBC)
                // Binary only calls random() when min != max to preserve RNG sequence.
                double speed = pn.activeSlot().prtVmin;
                if (speed != pn.activeSlot().prtV)
                    speed = speed + (pn.activeSlot().prtV - speed) * random();

                // 3e. Direction based on particleFlyDirection (0x6BFCEC..0x6BFDE8)
                // Binary uses node+2180 (particleFlyDirection) for direction mode,
                // NOT node+2176 (particleInheritVelocity). 0x6BFCC4.
                double direction = 0.0;
                const int inhVel = pn.particleFlyDirection;

                if (inhVel == 2) {
                    // Exponential decay (0x6BFD58..0x6BFDE8)
                    double dist = std::sqrt(txOff * txOff + tyOff * tyOff + offZ * offZ);
                    double dirAngle = std::atan2(tyOff, txOff) * 360.0;
                    double decay = pn.particleAccelRatio;
                    // Binary reads cached player+1128 directly (0x6BFD88)
                    double childTotalTime = child->_cachedTotalFrames;
                    double dtNorm = childTotalTime / 60.0;
                    if (decay == 1.0) {
                        speed = (dtNorm > 0) ? dist / dtNorm : 0.0;
                    } else if (decay > 0.0 && dtNorm > 0.0) {
                        speed = dist * std::log(decay) / (std::pow(decay, dtNorm) - 1.0);
                    }
                    direction = dirAngle / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0; // convert to radians
                    speed /= 60.0;
                } else if (inhVel == 1) {
                    // Offset direction (0x6BFCF4..0x6BFD18)
                    direction = std::atan2(tyOff, txOff) * 360.0 / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0;
                } else {
                    // Matrix angle (0x6BFDAC): atan2(m12, m11) — node+136, node+120.
                    direction = std::atan2(pn.accumulated.m12, pn.accumulated.m11) * 360.0 / (2.0 * PI);
                    direction = direction * PI / 180.0;
                }

                // Angle spread (0x6BFDEC..0x6BFE34)
                double range = pn.activeSlot().prtRange;
                double spreadRandom = -range;
                if (range != -range) spreadRandom = (range + range) * random() - range;
                double totalAngle = direction + spreadRandom * PI / 180.0;
                double dirRad = totalAngle;

                // 3f. particleApplyZoomToVelocity (0x6BFE38..0x6BFEA0)
                double zoomScale = 1.0;
                if (inhVel >= 1 && inhVel <= 2) {
                    if (txOff != 0.0 || tyOff != 0.0) {
                        if (offZ != 0.0) {
                            double xyLen = std::sqrt(txOff * txOff + tyOff * tyOff);
                            zoomScale = xyLen / std::sqrt(offZ * offZ + xyLen * xyLen);
                        }
                    }
                }

                // Compute velocity + set position (0x6BFEC0..0x6BFF70)
                // Binary branches on coordinateMode (node+24), not inhVel.
                double velX = 0.0, velY = 0.0, velZ = 0.0;

                if (child->_runtime && !child->_runtime->nodes.empty()) {
                    auto &cr = child->_runtime->nodes[0];
                    if (pn.coordinateMode == 1) {
                        // 3D mode (0x6BFEB4..0x6BFEDC)
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = offZ + pn.accumulated.posY;
                        cr.accumulated.posZ = tyOff + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = speed * 0.0;
                        velZ = zoomScale * speed * std::sin(dirRad);
                    } else if (pn.coordinateMode == 0) {
                        // 2D mode (0x6BFF14..0x6BFF3C)
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = tyOff + pn.accumulated.posY;
                        cr.accumulated.posZ = offZ + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = zoomScale * speed * std::sin(dirRad);
                        velZ = speed * 0.0;
                    }

                    // 3h. Set flipX/Y (0x6BFF74..0x6BFFA4)
                    // Binary only writes + sets dirty when values differ.
                    if (cr.accumulated.flipX != pn.accumulated.flipX ||
                        cr.accumulated.flipY != pn.accumulated.flipY) {
                        cr.accumulated.flipX = pn.accumulated.flipX;
                        cr.accumulated.flipY = pn.accumulated.flipY;
                        cr.accumulated.dirty = true;
                    }

                    // 3i. Angle from prtA lerp — BEFORE zoom (0x6BFFA8..0x6C00AC)
                    // Binary order: angle lerp → angle computation → zoom lerp.
                    // Both call random(), so order matters for RNG sequence.
                    double aMin = pn.activeSlot().prtAmin;
                    double aMax = pn.activeSlot().prtA;
                    double prtAngle = aMin;
                    if (aMin != aMax) prtAngle = aMin + (aMax - aMin) * random();
                    // Binary uses PARENT flipX/Y for sign (0x6BFFD8..0x6BFFE0)
                    double childAngle = -prtAngle;
                    if (pn.accumulated.flipX == pn.accumulated.flipY) childAngle = prtAngle;

                    if (pn.particleInheritAngle) {
                        // Binary: v154 = dirRad + PI; if(!flipX) v154 = dirRad;
                        // then childAngle += v154 * 360 / (2*PI) (0x6BFFEC..0x6C0008)
                        double v154 = dirRad + PI;
                        if (!pn.accumulated.flipX) v154 = dirRad;
                        childAngle += v154 * 360.0 / (2.0 * PI);
                    }
                    while (childAngle < 0.0) childAngle += 360.0;
                    while (childAngle >= 360.0) childAngle -= 360.0;

                    // _directEdit check (0x6C0058): binary writes to player+464 and
                    // calls Player_initEmoteMotion if child._directEdit is true
                    if (child->_directEdit) {
                        // Emote mode angle path (0x6C0088..0x6C00AC)
                        double k = childAngle;
                        while (k < 0.0) k += 360.0;
                        while (k >= 360.0) k -= 360.0;
                        // player+464 = emote angle — not mapped in web port
                        // Player_initEmoteMotion(child, 2) — N/A for web
                    } else {
                        // Normal angle path (0x6C0060..0x6C0078)
                        if (cr.accumulated.angle != childAngle) {
                            cr.accumulated.dirty = true;
                            cr.accumulated.angle = childAngle;
                        }
                    }

                    // 3j. Zoom lerp — AFTER angle (0x6C00B0..0x6C00D8)
                    double zoom = pn.activeSlot().prtZmin;
                    if (zoom != pn.activeSlot().prtZ)
                        zoom = zoom + (pn.activeSlot().prtZ - zoom) * random();
                    if (cr.accumulated.scaleX != zoom || cr.accumulated.scaleY != zoom) {
                        cr.accumulated.dirty = true;
                        cr.accumulated.scaleX = zoom;
                        cr.accumulated.scaleY = zoom;
                    }

                    // 3j. particleApplyZoomToVelocity on child velocity (0x6C0110..0x6C0168)
                    // Binary gate: particleFlyDirection != 2 (0x6C0110)
                    if (pn.particleFlyDirection != 2) {
                        if (pn.particleApplyZoomToVelocity == 1) {
                            velX *= zoom; velY *= zoom; velZ *= zoom;
                        } else if (pn.particleApplyZoomToVelocity == 2 && zoom != 0.0) {
                            velX /= zoom; velY /= zoom; velZ /= zoom;
                        }
                    }
                }

                // 3k. Store velocity on child (0x6BFEF8..0x6BFF70)
                child->_cameraVelocityX = velX;
                child->_cameraVelocityY = velY;
                child->_cameraVelocityZ = velZ;

                // 3l. particleInheritVelocity==1: add parent delta/dt (0x6C0174..0x6C01AC)
                // Binary checks node+2176 (particleInheritVelocity), not particleFlyDirection.
                // Binary at 0x6C0178: checks dt != 0.0 (not dt > 0.0)
                if (pn.particleInheritVelocity == 1 && dt != 0.0) {
                    child->_cameraVelocityX += pn.deltaPosX / dt;
                    child->_cameraVelocityY += pn.deltaPosY / dt;
                    child->_cameraVelocityZ += pn.deltaPosZ / dt;
                }

                // 3m. Set cameraDamping (0x6C01B4)
                // Binary: node+2192 is one field for both decay and damping
                child->_cameraDamping = pn.particleAccelRatio;

                pn.addParticleChild(childVar);

                // Enforce maxNum per-particle (0x6C0218..0x6C0268)
                // Binary: signed comparison count > maxNum. When maxNum==0, ALL particles
                // are removed (size > 0 is always true). Only removes ONE per emission.
                if (pn.getParticleCount() > pn.particleMaxNum) {
                    pn.eraseParticleChild(0);
                }

                // Physics only when emitCount <= 1 (0x6C026C: CMP W20, #1; B.GT)
                if (emitCount <= 1) goto physics_step;
                // emitCount > 1: skip physics this frame, advance to next node.
                // Next frame will create another particle.
                continue;
                } // end creation block
            }
            } // end outer emitCount scope

        physics_step:
            // ====== sub_6C17A4: Physics stepping ======
            // Pass 1: Delete particles (0x6C1858..0x6C1950)
            // Binary uses TJS Array.erase with index-based iteration.
            // When erasing, count decreases and index stays (--i after erase).
            {
                int pCount = pn.getParticleCount();
                for (int ci = 0; ci < pCount; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    bool shouldErase = false;
                    if (!child || !child->_runtime || child->_runtime->nodes.empty()) {
                        shouldErase = true;
                    } else if (child->_allplaying) {
                        // Playing: only check bounds if particleDeleteOutside (0x6C1888)
                        if (pn.particleDeleteOutside) {
                            const double bMinX = child->_boundsMinX;
                            const double bMinY = child->_boundsMinY;
                            const double bMaxX = child->_boundsMaxX;
                            const double bMaxY = child->_boundsMaxY;
                            if (bMaxX >= bMinX && bMaxY >= bMinY) {
                                const double sw = static_cast<double>(_runtime->width);
                                const double sh = static_cast<double>(_runtime->height);
                                if (!(bMaxY > 0.0 && bMinX < sw && bMaxX > 0.0 && bMinY < sh)) {
                                    shouldErase = true;
                                }
                            }
                        }
                    } else {
                        // Not playing: always delete (0x6C1880)
                        shouldErase = true;
                    }
                    if (shouldErase) {
                        // Aligned to sub_6C17A4 (0x6C1930): TJS Array.erase(index)
                        pn.eraseParticleChild(ci);
                        --ci;
                        pCount = pn.getParticleCount();
                    }
                }
            }

            // Pass 2: Step each remaining child (0x6C1984..0x6C1A3C)
            // Binary at 0x6C1960: mesh combine parent propagation.
            {
                const int pCount2 = pn.getParticleCount();
                for (int ci = 0; ci < pCount2; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    if (!child || !child->_runtime) continue;
                    child->_zFactor = _zFactor;
                    if (!child->_runtime->nodes.empty()) {
                        auto &cr = child->_runtime->nodes[0];
                        cr.parentClipIndex = -1;
                        // Parent and child node indices are separate namespaces.
                        // Attach the external ancestor during render-item merge.
                        cr.visibleAncestorIndex = -1;
                        cr.forceVisible = pn.forceVisible;
                    }
                    child->frameProgress(_frameLastTime);
                    child->ensureNodeTreeBuilt();
                    if (!child->_runtime->nodes.empty()) {
                        child->updateLayers();
                    }
                }
            }
        } // for each nodeType==4
    }

    void Player::updateLayersPhase3_AnchorNode() {
        auto &nodes = _runtime->nodes;
        // --- sub_6C0528: Anchor node processing (nodeType=10) ---
        // Aligned to 0x6C0528. For each nodeType=10 active node,
        // apply exponential damping toward root node values.
        for (size_t ai = 1; ai < nodes.size(); ++ai) {
            auto &an = nodes[ai];
            if (an.nodeType != 10 || !an.accumulated.active) continue;
            _needsInternalAssignImages = true;
            if (_frameLastTime == 0.0) {
                an.anchorEnabled = false;
                continue;
            }
            an.anchorEnabled = true;
            // Read width/height (0x6C0790..0x6C0848)
            double cw = an.interpolatedCache.width;
            double ch = an.interpolatedCache.height;
            if (cw <= 0.0) cw = 32.0;
            if (ch <= 0.0) ch = 32.0;
            an.clipW = cw;
            an.clipH = ch;
            an.originX = cw * 0.5;
            an.originY = ch * 0.5;

            // Damping exponent (0x6C088C..0x6C08B8)
            // From decompilation: v28 = dt * (v27*dt/v27) / v27 / 60 / damping
            // where v27 = dt/fps. Simplifies to dt*fps/60/damping for dt~1 frame.
            const double dampPow = std::abs(_frameLastTime) / 60.0
                / std::max(an.anchorDamping, 0.001);

            // Angle damping (0x6C08C0..0x6C08E0)
            double angle = an.accumulated.angle;
            if (angle >= 180.0)
                angle = 360.0 - (360.0 - angle) * dampPow;
            else
                angle = angle * dampPow;
            an.accumulated.angle = angle;

            // Scale damping (0x6C08E0..0x6C0924)
            an.accumulated.scaleX = std::pow(
                an.accumulated.scaleX * 32.0 / cw, dampPow);
            an.accumulated.scaleY = std::pow(
                an.accumulated.scaleY * 32.0 / ch, dampPow);

            // Slant damping (0x6C0924..0x6C0938)
            an.accumulated.slantX *= dampPow;
            an.accumulated.slantY *= dampPow;

            // Rebuild local matrix via sub_699940 (0x6C0944)
            {
                Affine2x3 la = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(la, an);
                an.accumulated.m11 = la[0]; an.accumulated.m21 = la[1];
                an.accumulated.m12 = la[2]; an.accumulated.m22 = la[3];
            }

            // If !independentLayerInherit: multiply with root (0x6C094C)
            if (!_independentLayerInherit && !nodes.empty()) {
                const auto &rn = nodes[0];
                const double nm11 = an.accumulated.m11, nm12 = an.accumulated.m12;
                const double nm21 = an.accumulated.m21, nm22 = an.accumulated.m22;
                an.accumulated.m11 = rn.accumulated.m11*nm11 + rn.accumulated.m12*nm21;
                an.accumulated.m21 = rn.accumulated.m21*nm11 + rn.accumulated.m22*nm21;
                an.accumulated.m12 = rn.accumulated.m11*nm12 + rn.accumulated.m12*nm22;
                an.accumulated.m22 = rn.accumulated.m21*nm12 + rn.accumulated.m22*nm22;
            }

            // Opacity damping (0x6C0994..0x6C09F8)
            {
                int opa = an.accumulated.opacity;
                double opaF = static_cast<double>(opa) / 255.0;
                if (opa == 0) opaF = 1.0 / 255.0;
                double newOpa = std::pow(opaF, dampPow) * 255.0 * an.anchorOpaScale;
                newOpa = std::clamp(newOpa, 0.0, 255.0);
                an.accumulated.opacity = static_cast<int>(newOpa);
                double denom = newOpa;
                if (static_cast<int>(newOpa) < 0) denom += 4294967296.0;
                if (denom != 0.0) an.anchorOpaScale = newOpa / denom;
            }

            // Position lerp toward root (0x6C0A04..0x6C0A4C)
            if (!nodes.empty()) {
                const auto &rn = nodes[0];
                an.accumulated.posX = rn.accumulated.posX
                    + dampPow * (an.accumulated.posX - rn.accumulated.posX);
                an.accumulated.posY = rn.accumulated.posY
                    + dampPow * (an.accumulated.posY - rn.accumulated.posY);
                an.accumulated.posZ = rn.accumulated.posZ
                    + dampPow * (an.accumulated.posZ - rn.accumulated.posZ);
            }

            // Color damping (0x6C0A68..0x6C0C58)
            // Per-channel pow(channel/base, dampPow)*base*colorScale
            {
                const bool isDefaultBlend =
                    (an.interpolatedCache.blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 255.0 : 255.0;
                const auto packedColors = copyPackedColorsFromBytes(an.colorBytes);
                const bool allEqual =
                    packedColors[0] == packedColors[1]
                    && packedColors[1] == packedColors[2]
                    && packedColors[2] == packedColors[3];
                if (!(allEqual && packedColors[0] == 0xFF808080u)) {
                    int iters = (allEqual) ? 1 : 4;
                    for (int ci = 0; ci < iters && ci < 4; ++ci) {
                        for (int ch = 0; ch < 3; ++ch) {
                            double v = static_cast<double>(an.colorBytes[ci*4+ch]);
                            if (v == 0.0) v = 1.0;
                            double res = base * std::pow(v / base, dampPow)
                                * an.anchorColorScale[ci*4+ch];
                            res = std::clamp(res, 0.0, 255.0);
                            int ir = static_cast<int>(res);
                            double dr = static_cast<double>(ir);
                            if (dr != 0.0) an.anchorColorScale[ci*4+ch] = res / dr;
                            an.colorBytes[ci*4+ch] = static_cast<uint8_t>(ir);
                        }
                        // Alpha channel (0x6C0BA8..0x6C0BE0)
                        double av = static_cast<double>(an.colorBytes[ci*4+3]) / 255.0;
                        if (av == 0.0) av = 1.0 / 255.0;
                        double ares = std::pow(av, dampPow) * 255.0
                            * an.anchorColorScale[ci*4+3];
                        ares = std::clamp(ares, 0.0, 255.0);
                        int iar = static_cast<int>(ares);
                        double dar = static_cast<double>(iar);
                        if (dar != 0.0) an.anchorColorScale[ci*4+3] = ares / dar;
                        an.colorBytes[ci*4+3] = static_cast<uint8_t>(iar);
                    }
                    if (allEqual) {
                        std::memcpy(&an.colorBytes[4], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[8], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[12], &an.colorBytes[0], 4);
                    }
                }
            }
        }

    }

    // --- updateLayers: 3-phase pipeline ---
    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
    // Operates on persistent MotionNode vector instead of re-walking PSB tree.
    void Player::updateLayers() {
        auto &nodes = _runtime->nodes;
        if (nodes.empty()) return;
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};
        // E-mote separates its persistent model from its controller clocks.
        // The selected base.motion clip is a multidimensional geometry table:
        // unparameterized nodes describe the neutral model at frame 0, while
        // parameterized nodes below are sampled from _variableValues.  Letting
        // the ordinary timeline clock scrub this table reaches the authored
        // invisible sentinel at frame 61 and makes the entire character blink
        // out periodically.  Blink/bust/hair timelines continue advancing in
        // frameProgress(); only the model evaluation clock is pinned here.
        const double currentTime = _runtime->isEmoteMode
            ? 0.0
            : _clampedEvalTime;

        // Ensure per-node eval data array matches node count (player+384).
        // Binary allocates this as a fixed-size array during Player construction;
        // we resize dynamically to match node count.
        if (_runtime->perNodeEvalData.size() != nodes.size()) {
            _runtime->perNodeEvalData.resize(nodes.size());
        }
        // Binary writes the active model evaluation time into the per-node
        // records during the main loop (0x6BB4E0 area).
        for (size_t ni = 0; ni < nodes.size(); ++ni) {
            _runtime->perNodeEvalData[ni].evalTime = currentTime;
        }

        updateLayersPhase1_PreLoop(currentTime);
        // Phase 1 evaluates the child motion's local root and therefore resets
        // it to the PSB-local origin.  Restore the containing motion node before
        // phase 2 accumulates descendants and phase 3 computes their vertices.
        if(_motionParentPlayer) {
            applyMotionParentRootStateForRender();
        }
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

        if(LOGGER && emoteRootTraceEnabled() && _runtime->isEmoteMode &&
           !_motionParentPlayer && !_runtime->nodes.empty()) {
            const auto &traceRoot = _runtime->nodes.front();
            const auto traceKey = motionPath + ":" +
                std::to_string(traceRoot.accumulated.scaleX) + ":" +
                std::to_string(traceRoot.accumulated.scaleY);
            if(markMotionUpdateDebugLogged(traceKey)) {
                size_t visibleCount = 0;
                size_t unscaledMatrixCount = 0;
                std::ostringstream sample;
                for(size_t traceIndex = 1; traceIndex < nodes.size();
                    ++traceIndex) {
                    const auto &traceNode = nodes[traceIndex];
                    if(!traceNode.accumulated.active ||
                       !traceNode.accumulated.visible ||
                       traceNode.interpolatedCache.src.empty()) {
                        continue;
                    }
                    ++visibleCount;
                    const double matrixScaleX = std::hypot(
                        traceNode.accumulated.m11,
                        traceNode.accumulated.m21);
                    const double matrixScaleY = std::hypot(
                        traceNode.accumulated.m12,
                        traceNode.accumulated.m22);
                    if(matrixScaleX < traceRoot.accumulated.scaleX * 0.75 ||
                       matrixScaleY < traceRoot.accumulated.scaleY * 0.75) {
                        ++unscaledMatrixCount;
                    }
                    if(visibleCount <= 8) {
                        if(sample.tellp() > 0) {
                            sample << ';';
                        }
                        sample << traceNode.layerName << "(s="
                               << traceNode.accumulated.scaleX << ','
                               << traceNode.accumulated.scaleY << ",m="
                               << matrixScaleX << ',' << matrixScaleY << ')';
                    }
                }
                LOGGER->info(
                    "[EMOTE_ROOT] motion={} rootScale=({:.3f},{:.3f}) "
                    "rootMatrix=({:.3f},{:.3f},{:.3f},{:.3f}) "
                    "visible={} unscaledMatrices={} sample=[{}]",
                    motionPath, traceRoot.accumulated.scaleX,
                    traceRoot.accumulated.scaleY,
                    traceRoot.accumulated.m11,
                    traceRoot.accumulated.m12,
                    traceRoot.accumulated.m21,
                    traceRoot.accumulated.m22, visibleCount,
                    unscaledMatrixCount, sample.str());
            }
        }

        // === Post-loop cleanup ===
        // Aligned to 0x6BBCB4..0x6BBE1C: clear per-node flags and timeline state.

        // Clear player+608 first-frame flag (0x6BBDF8: STRB WZR, [X19,#0x260]).
        _noUpdateYet = false;

        // Clear player+480 queuing flag (0x6BBDFC: STRB WZR, [X19,#0x1E0]).
        _queuing = false;

        // Clear node+44 (flags byte) and node+1504 (accumulated visible)
        // for all non-root nodes (0x6BBCFC..0x6BBD40).
        for (size_t ci = 1; ci < nodes.size(); ++ci) {
            nodes[ci].flags &= ~0x01;           // node+44
            nodes[ci].accumulated.visible = false; // node+1504
        }

        // Clear per-node eval data dirty flags (0x6BBD44..0x6BBDF4).
        // Binary: *(v98+48) = 0 for each entry in player+384 array.
        for (auto &evalData : _runtime->perNodeEvalData) {
            evalData.dirtyFlag = 0;
        }

        // A motion child evaluates its own root at local (0, 0).  The parent
        // transform is applied before the child update above, so phase 1 can
        // overwrite it while rebuilding the child's accumulated state.  Keep
        // the inherited root as the final state consumed by recursive render
        // collection and hit testing.  Without this, nested button/slot clips
        // leak into the top-left corner and every save card is rendered at the
        // origin instead of at its parent slot position.
        if(_motionParentPlayer) {
            applyMotionParentRootStateForRender();
        }

        _allplaying = !_runtime->playingTimelineLabels.empty() ||
            shouldReportPlayingChildPlayers();
        ++_runtime->layerStateGeneration;
        _emoteDirty = false;
        _layersDirty = false;

    }

    bool Player::hasPlayingChildPlayers() const {
        if(!_runtime) {
            return false;
        }

        const auto isPlaying = [](const Player *child) {
            return child &&
                (child->_allplaying ||
                 (child->_runtime &&
                  !child->_runtime->playingTimelineLabels.empty()) ||
                 child->hasPlayingChildPlayers());
        };

        for(const auto &node : _runtime->nodes) {
            if(node.nodeType == 3) {
                if(isPlaying(node.getChildPlayer())) {
                    return true;
                }
                continue;
            }
            if(node.nodeType != 4) {
                continue;
            }
            const int childCount = node.getParticleCount();
            for(int index = 0; index < childCount; ++index) {
                if(isPlaying(node.getParticleChild(index))) {
                    return true;
                }
            }
        }
        return false;
    }

    bool Player::shouldReportPlayingChildPlayers() const {
        // `allplaying` is the aggregate state used by AnimKAGLayer to keep
        // calling progress() after the selected/root timeline has stopped.
        // Nested motion clips are not limited to SD presentations: classic
        // title and Extra PSBs also use short outer clips that launch longer
        // child animations.  Restricting this state to SD presentations
        // freezes those children at the outer clip's final frame.
        return !_motionParentPlayer && _runtime && _runtime->activeMotion &&
            hasPlayingChildPlayers();
    }

    void Player::calcBounds() {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        _boundsMinX = 1e308;
        _boundsMinY = 1e308;
        _boundsMaxX = -1e308;
        _boundsMaxY = -1e308;

        bool haveBounds = false;
        auto mergeBounds = [&](double minX, double minY, double maxX, double maxY) {
            if(minX > maxX || minY > maxY) {
                return;
            }
            if(!haveBounds) {
                _boundsMinX = minX;
                _boundsMinY = minY;
                _boundsMaxX = maxX;
                _boundsMaxY = maxY;
                haveBounds = true;
                return;
            }
            if(minX < _boundsMinX) _boundsMinX = minX;
            if(minY < _boundsMinY) _boundsMinY = minY;
            if(maxX > _boundsMaxX) _boundsMaxX = maxX;
            if(maxY > _boundsMaxY) _boundsMaxY = maxY;
        };

        for(auto &node : _runtime->nodes) {
            node.bounds[0] = 1.0f;
            node.bounds[1] = 1.0f;
            node.bounds[2] = -1.0f;
            node.bounds[3] = -1.0f;

            if(!node.accumulated.active || !node.hasSource || !node.drawFlag) {
                continue;
            }

            // Motion and particle nodes are structural containers. Their
            // source is another Player (or a particle collection), so the
            // container's own vertex is not rendered geometry. Including its
            // default (0, 0) point expands child-only button bounds to the
            // stage origin and makes unrelated clicks hit the button. Native
            // Motion.Player_calcBounds obtains their extent exclusively from
            // the child-player merge below.
            if(node.nodeType == LayerTypeMotion ||
               node.nodeType == LayerTypeParticle) {
                continue;
            }

            bool haveNodeBounds = false;
            double minX = 0.0;
            double minY = 0.0;
            double maxX = 0.0;
            double maxY = 0.0;
            auto extendPoint = [&](double x, double y) {
                if(!haveNodeBounds) {
                    minX = maxX = x;
                    minY = maxY = y;
                    haveNodeBounds = true;
                    return;
                }
                if(x < minX) minX = x;
                if(y < minY) minY = y;
                if(x > maxX) maxX = x;
                if(y > maxY) maxY = y;
            };

            if(!node.meshRenderPoints.empty()) {
                for(size_t pi = 0; pi + 1 < node.meshRenderPoints.size(); pi += 2) {
                    extendPoint(node.meshRenderPoints[pi],
                                node.meshRenderPoints[pi + 1]);
                }
            } else if(node.clipW > 0.0 || node.clipH > 0.0) {
                for(int ci = 0; ci < 4; ++ci) {
                    extendPoint(node.vertices[ci * 2], node.vertices[ci * 2 + 1]);
                }
            } else {
                extendPoint(node.vertexPosX, node.vertexPosY);
            }

            if(!haveNodeBounds) {
                continue;
            }

            const std::array<float, 4> expectedBounds = {
                static_cast<float>(std::floor(minX)),
                static_cast<float>(std::floor(minY)),
                static_cast<float>(std::ceil(maxX)),
                static_cast<float>(std::ceil(maxY))
            };
            node.bounds[0] = expectedBounds[0];
            node.bounds[1] = expectedBounds[1];
            node.bounds[2] = expectedBounds[2];
            node.bounds[3] = expectedBounds[3];
            mergeBounds(node.bounds[0], node.bounds[1],
                        node.bounds[2], node.bounds[3]);
            if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                const std::array<float, 4> actualBounds = {
                    node.bounds[0], node.bounds[1], node.bounds[2], node.bounds[3]
                };
                bool ok = true;
                for(size_t bi = 0; bi < expectedBounds.size(); ++bi) {
                    if(std::fabs(expectedBounds[bi] - actualBounds[bi]) > 0.01f) {
                        ok = false;
                        break;
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "calcBounds.node", "0x6C3D04",
                    _clampedEvalTime,
                    fmt::format(
                        "from=minmax({:.3f},{:.3f},{:.3f},{:.3f}) exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        minX, minY, maxX, maxY,
                        expectedBounds[0], expectedBounds[1],
                        expectedBounds[2], expectedBounds[3]),
                    fmt::format(
                        "nodeIndex={} label={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        node.index,
                        node.layerName.empty() ? std::string("<root>")
                                               : node.layerName,
                        actualBounds[0], actualBounds[1],
                        actualBounds[2], actualBounds[3]),
                    ok,
                    "Player_calcBounds produced an unexpected node AABB");
            }
        }

        for(size_t ni = 1; ni < _runtime->nodes.size(); ++ni) {
            auto &node = _runtime->nodes[ni];
            if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    child->calcBounds();
                    mergeBounds(child->_boundsMinX, child->_boundsMinY,
                                child->_boundsMaxX, child->_boundsMaxY);
                }
            } else if(node.nodeType == 4) {
                const int particleCount = node.getParticleCount();
                for(int pi = 0; pi < particleCount; ++pi) {
                    if(auto *child = node.getParticleChild(pi)) {
                        child->calcBounds();
                        mergeBounds(child->_boundsMinX, child->_boundsMinY,
                                    child->_boundsMaxX, child->_boundsMaxY);
                    }
                }
            }
        }

        if(!haveBounds) {
            _boundsMinX = 0.0;
            _boundsMinY = 0.0;
            _boundsMaxX = 0.0;
            _boundsMaxY = 0.0;
        }
        detail::logoChainTraceLogf(
            motionPath, "calcBounds.player", "0x6C3D04", _clampedEvalTime,
            "playerBounds=({:.3f},{:.3f},{:.3f},{:.3f}) haveBounds={}",
            _boundsMinX, _boundsMinY, _boundsMaxX, _boundsMaxY,
            haveBounds ? 1 : 0);
    }

    void Player::appendPreparedRenderItems() {
        if(!_runtime || !_runtime->activeMotion) {
            return;
        }

        auto &entries = _runtime->preparedRenderItems;
        const auto &nodes = _runtime->nodes;
        const int bitmask = _runtime->isEmoteMode ? 5193 : 5185;
        const auto &dam = _runtime->drawAffineMatrix;
        // Node indices are dense and local to this Player. Reusing compact
        // marker arrays avoids constructing thousands of short-lived hash
        // tables while six or more E-mote characters are prepared each frame.
        std::vector<std::uint8_t> requiredGroupNodeIndices(nodes.size(), 0);
        std::vector<std::uint32_t> ancestorVisitMarks(nodes.size(), 0);
        std::uint32_t ancestorVisitToken = 0;
        int skipInactive = 0;
        int skipType = 0;
        int skipNoRenderableSource = 0;

        auto transformPoint = [&](float x, float y) -> tTVPPointD {
            return {
                dam[0] * static_cast<double>(x) + dam[2] * static_cast<double>(y) + dam[4],
                dam[1] * static_cast<double>(x) + dam[3] * static_cast<double>(y) + dam[5]
            };
        };

        constexpr std::array<float, 4> kInvalidPreparedPaintBox = {
            1.0f, 1.0f, -1.0f, -1.0f
        };

        auto updatePaintBox = [](detail::PlayerRuntime::PreparedRenderItem &entry,
                                 double x, double y, bool firstPoint) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            if(firstPoint) {
                entry.paintBox = { fx, fy, fx, fy };
                return;
            }
            if(fx < entry.paintBox[0]) entry.paintBox[0] = fx;
            if(fy < entry.paintBox[1]) entry.paintBox[1] = fy;
            if(fx > entry.paintBox[2]) entry.paintBox[2] = fx;
            if(fy > entry.paintBox[3]) entry.paintBox[3] = fy;
        };

        for(size_t i = 0; i < nodes.size(); ++i) {
            const auto &node = nodes[i];
            if(!node.accumulated.active) continue;
            if(!node.forceVisible && (((1 << node.nodeType) & bitmask) == 0)) {
                continue;
            }
            if(!node.hasSource || node.interpolatedCache.src.empty()) continue;

            if(++ancestorVisitToken == 0) {
                std::fill(ancestorVisitMarks.begin(),
                          ancestorVisitMarks.end(), 0);
                ++ancestorVisitToken;
            }
            for(int ancestorIndex = node.visibleAncestorIndex;
                ancestorIndex >= 0 &&
                ancestorIndex < static_cast<int>(nodes.size()); ) {
                if(ancestorVisitMarks[ancestorIndex] ==
                   ancestorVisitToken) {
                    if(LOGGER && std::getenv("AETHERKIRI_MOTION_DEBUG")) {
                        LOGGER->warn(
                            "motion prepare ancestor cycle skipped: motion={} node={} ancestor={}",
                            _runtime->activeMotion
                                ? _runtime->activeMotion->path
                                : std::string("<none>"),
                            node.layerName, ancestorIndex);
                    }
                    break;
                }
                ancestorVisitMarks[ancestorIndex] = ancestorVisitToken;
                const auto &ancestor = nodes[ancestorIndex];
                // Type 12 is an off-screen/composite container even when its
                // own stencil mode is 1 rather than 4.  Its source is the
                // group's base image, not an independent full-screen sibling.
                // Keeping only mode-4 containers here flattened mode-1 child
                // motions directly into the target and let their base images
                // overwrite previously rendered nested artwork.
                const bool isSpecialCompositeParent =
                    ancestor.nodeType == 12 || ancestor.nodeType == 7;
                const bool inserted =
                    isSpecialCompositeParent &&
                    !requiredGroupNodeIndices[ancestorIndex];
                if(inserted) {
                    requiredGroupNodeIndices[ancestorIndex] = 1;
                }
                const int nextAncestorIndex = ancestor.visibleAncestorIndex;
                if(!inserted || nextAncestorIndex == ancestorIndex) {
                    if(!isSpecialCompositeParent && nextAncestorIndex != ancestorIndex) {
                        ancestorIndex = nextAncestorIndex;
                        continue;
                    }
                    break;
                }
                ancestorIndex = nextAncestorIndex;
            }
        }

        // The runtime node vector is already stored in the authored buffer
        // order expected by the render manager. Reversing the complete flat
        // array also reverses unrelated equal-Z surfaces, allowing an opaque
        // background leaf to cover the SD CG assembled before it.
        for(size_t bufferPosition = 0;
            bufferPosition < nodes.size(); ++bufferPosition) {
            const size_t i = detail::nativeLayerBufferNodeIndex(
                nodes.size(), bufferPosition);
            const auto &node = nodes[i];
            if(!node.accumulated.active) {
                ++skipInactive;
                continue;
            }
            const bool hasOwnSource =
                node.hasSource && !node.interpolatedCache.src.empty();
            const bool needsGroupEntry =
                requiredGroupNodeIndices[i] != 0;
            if(!needsGroupEntry &&
               !node.forceVisible &&
               (((1 << node.nodeType) & bitmask) == 0)) {
                ++skipType;
                continue;
            }
            if(!hasOwnSource && !needsGroupEntry) {
                ++skipNoRenderableSource;
                continue;
            }

            detail::PlayerRuntime::PreparedRenderItem entry;
            entry.nodeIndex = static_cast<int>(i);
            entry.nodeLabel = node.layerName;
            entry.hasOwnSource = hasOwnSource;
            entry.groupOnly = needsGroupEntry;
            entry.implicitVisibleStencilGroup =
                node.implicitVisibleStencilGroup;
            entry.implicitVisibleStencilBase =
                node.implicitVisibleStencilBase;
            entry.implicitVisibleStencilGroupNodeIndex =
                node.implicitVisibleStencilGroupNodeIndex;
            entry.sourceMotion = _runtime->activeMotion;
            if(hasOwnSource) {
                entry.sourceKey = node.interpolatedCache.src;
                entry.srcRef = findSource(detail::widen(entry.sourceKey));
            }
            // Aligned to sub_6D5164 -> sub_6C2334:
            // top-level build uses arg4=0, so render-item draw flag becomes
            // node+1960 ? 1 : node+1961. node+1961 is the post-build
            // stencilComposite mask-layer reference flag.
            entry.drawFlag =
                node.drawFlag || node.stencilCompositeMaskReferenced ||
                needsGroupEntry;
            // sub_6BF714 stores node+1528 (the accumulated Z coordinate) in
            // render-item+64. sub_6D2544 then stable-sorts those items with
            // sub_6D22E0, whose sole comparison is item+64 ascending.
            entry.sortKey = node.accumulated.posZ;
            entry.blendMode = node.accumulated.blendMode;
            entry.packedColors = copyPackedColorsFromBytes(node.colorBytes);
            entry.opacity = node.accumulated.opacity;
            entry.updateCount = node.stencilType;
            entry.visibleAncestorIndex = node.visibleAncestorIndex;
            entry.stencilMaskReferenced =
                node.stencilCompositeMaskReferenced;
            entry.stencilMaskNodeIndices =
                node.stencilCompositeMaskNodeIndices;
            // Ordinary image nodes acquire a tessellated grid when they are
            // below an E-mote mesh ancestor even though their authored
            // meshTransform is zero.  Render those as an explicit point mesh;
            // otherwise the renderer would ignore the computed deformation
            // and fall back to the undeformed quad.
            entry.meshType = node.meshRenderPoints.empty()
                ? 0 : (node.meshType == 1 ? 1 : 2);
            entry.meshDivX = node.meshDivX;
            entry.meshDivY = node.meshDivY;

            bool havePaintBox = false;
            if(hasOwnSource && node.clipW > 0.0 && node.clipH > 0.0) {
                for(int ci = 0; ci < 4; ++ci) {
                    const auto pt = transformPoint(node.vertices[ci * 2],
                                                   node.vertices[ci * 2 + 1]);
                    entry.corners[ci * 2] = static_cast<float>(pt.x);
                    entry.corners[ci * 2 + 1] = static_cast<float>(pt.y);
                    updatePaintBox(entry, pt.x, pt.y, !havePaintBox);
                    havePaintBox = true;
                }
            }

            if(hasOwnSource && !node.meshRenderPoints.empty()) {
                entry.meshPoints.resize(node.meshRenderPoints.size());
                for(size_t pi = 0; pi + 1 < node.meshRenderPoints.size(); pi += 2) {
                    const auto pt = transformPoint(node.meshRenderPoints[pi],
                                                   node.meshRenderPoints[pi + 1]);
                    entry.meshPoints[pi] = static_cast<float>(pt.x);
                    entry.meshPoints[pi + 1] = static_cast<float>(pt.y);
                    updatePaintBox(entry, pt.x, pt.y, !havePaintBox);
                    havePaintBox = true;
                }
            }

            if(!havePaintBox
               && hasOwnSource
               && node.bounds[2] >= node.bounds[0]
               && node.bounds[3] >= node.bounds[1]) {
                const auto p0 = transformPoint(node.bounds[0], node.bounds[1]);
                const auto p1 = transformPoint(node.bounds[2], node.bounds[1]);
                const auto p2 = transformPoint(node.bounds[2], node.bounds[3]);
                const auto p3 = transformPoint(node.bounds[0], node.bounds[3]);
                entry.paintBox = {
                    static_cast<float>(std::floor(std::min(std::min(p0.x, p1.x),
                                                          std::min(p2.x, p3.x)))),
                    static_cast<float>(std::floor(std::min(std::min(p0.y, p1.y),
                                                          std::min(p2.y, p3.y)))),
                    static_cast<float>(std::ceil(std::max(std::max(p0.x, p1.x),
                                                         std::max(p2.x, p3.x)))),
                    static_cast<float>(std::ceil(std::max(std::max(p0.y, p1.y),
                                                         std::max(p2.y, p3.y))))
                };
                havePaintBox = true;
            }

            if(!havePaintBox) {
                // libkrkr2.so sub_6C2334 initializes item+200..212 from
                // node+1936 when present, otherwise from xmmword_14D7C60.
                // That default is {1,1,-1,-1}, i.e. an invalid rect sentinel,
                // not a point box at vertexPos. Group-only items rely on this
                // invalid sentinel so the later child-union pass can replace
                // the parent paintBox with the first real child bounds instead
                // of being permanently anchored at (0,0).
                entry.paintBox = kInvalidPreparedPaintBox;
            } else {
                entry.paintBox = {
                    static_cast<float>(std::floor(entry.paintBox[0])),
                    static_cast<float>(std::floor(entry.paintBox[1])),
                    static_cast<float>(std::ceil(entry.paintBox[2])),
                    static_cast<float>(std::ceil(entry.paintBox[3]))
                };
            }

            if(node.parentClipIndex >= 0
               && node.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &clipNode = nodes[node.parentClipIndex];
                if(clipNode.shapeAABB[2] >= clipNode.shapeAABB[0]
                   && clipNode.shapeAABB[3] >= clipNode.shapeAABB[1]) {
                    const auto p0 = transformPoint(clipNode.shapeAABB[0], clipNode.shapeAABB[1]);
                    const auto p1 = transformPoint(clipNode.shapeAABB[2], clipNode.shapeAABB[1]);
                    const auto p2 = transformPoint(clipNode.shapeAABB[2], clipNode.shapeAABB[3]);
                    const auto p3 = transformPoint(clipNode.shapeAABB[0], clipNode.shapeAABB[3]);
                    entry.viewport = {
                        static_cast<float>(std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x))),
                        static_cast<float>(std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y))),
                        static_cast<float>(std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x))),
                        static_cast<float>(std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y)))
                    };
                    entry.hasViewport = true;
                }
            }

            if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                const auto motionPath = _runtime->activeMotion->path;
                const std::array<float, 8> expectedCorners = {
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[0]) +
                                       dam[2] * static_cast<double>(node.vertices[1]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[0]) +
                                       dam[3] * static_cast<double>(node.vertices[1]) +
                                       dam[5]),
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[2]) +
                                       dam[2] * static_cast<double>(node.vertices[3]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[2]) +
                                       dam[3] * static_cast<double>(node.vertices[3]) +
                                       dam[5]),
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[4]) +
                                       dam[2] * static_cast<double>(node.vertices[5]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[4]) +
                                       dam[3] * static_cast<double>(node.vertices[5]) +
                                       dam[5]),
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[6]) +
                                       dam[2] * static_cast<double>(node.vertices[7]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[6]) +
                                       dam[3] * static_cast<double>(node.vertices[7]) +
                                       dam[5])
                };
                const auto effectiveColor = unpackPackedRgba(entry.packedColors[0]);
                detail::logoChainTraceLogf(
                    motionPath, "prepare.item", "0x6C2334",
                    _clampedEvalTime,
                    "nodeIndex={} src={} blend={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] meshType={} meshDiv=({},{}) sortKey={:.3f} nodeDrawFlag={} maskRef={} itemDrawFlag={} visibleAncestorIndex={} slotDone={} frameType={} stencilBase={} stencilType={}",
                    entry.nodeIndex,
                    entry.sourceKey.empty() ? std::string("<none>")
                                            : entry.sourceKey,
                    entry.blendMode, entry.opacity,
                    entry.packedColors[0], entry.packedColors[1],
                    entry.packedColors[2], entry.packedColors[3],
                    effectiveColor[0], effectiveColor[1],
                    effectiveColor[2], effectiveColor[3],
                    entry.meshType, entry.meshDivX,
                    entry.meshDivY, entry.sortKey,
                    node.drawFlag ? 1 : 0,
                    node.stencilCompositeMaskReferenced ? 1 : 0,
                    entry.drawFlag ? 1 : 0,
                    entry.visibleAncestorIndex,
                    node.activeSlot().done ? 1 : 0,
                    node.currentFrameType,
                    node.stencilTypeBase,
                    node.stencilType);
                bool cornersOk = node.clipW <= 0.0 && node.clipH <= 0.0;
                if(!cornersOk) {
                    cornersOk = true;
                    for(size_t ci = 0; ci < expectedCorners.size(); ++ci) {
                        if(std::fabs(entry.corners[ci] - expectedCorners[ci]) >
                           0.01f) {
                            cornersOk = false;
                            break;
                        }
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "prepare.corners", "0x6C2334",
                    _clampedEvalTime,
                    fmt::format(
                        "drawAffine*vertices exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        expectedCorners[0], expectedCorners[1],
                        expectedCorners[2], expectedCorners[3],
                        expectedCorners[4], expectedCorners[5],
                        expectedCorners[6], expectedCorners[7]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, entry.corners[0], entry.corners[1],
                        entry.corners[2], entry.corners[3], entry.corners[4],
                        entry.corners[5], entry.corners[6], entry.corners[7]),
                    cornersOk,
                    "PreparedRenderItem corners diverged from drawAffineMatrix * node.vertices");
                detail::logoChainTraceCheck(
                    motionPath, "prepare.paintBox", "0x6C2334",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox from transformed geometry exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    true,
                    "PreparedRenderItem paintBox diverged from transformed geometry");
                detail::logoChainTraceCheck(
                    motionPath, "prepare.viewport", "0x6C2334",
                    _clampedEvalTime,
                    entry.hasViewport
                        ? fmt::format("parent shapeAABB chain exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.viewport[0], entry.viewport[1],
                                      entry.viewport[2], entry.viewport[3])
                        : std::string("parent shapeAABB chain exp=<invalid default>"),
                    entry.hasViewport
                        ? fmt::format("nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.nodeIndex, entry.viewport[0],
                                      entry.viewport[1], entry.viewport[2],
                                      entry.viewport[3])
                        : fmt::format("nodeIndex={} act=<invalid default>",
                                      entry.nodeIndex),
                    true,
                    "PreparedRenderItem viewport propagation diverged from parent clip chain");
            }

            entries.push_back(std::move(entry));
        }

        if(entries.empty()) {
            return;
        }

        std::vector<std::ptrdiff_t> entryIndexByNode(nodes.size(), -1);
        for(size_t i = 0; i < entries.size(); ++i) {
            if(entries[i].nodeIndex >= 0 &&
               entries[i].nodeIndex < static_cast<int>(nodes.size())) {
                entryIndexByNode[entries[i].nodeIndex] =
                    static_cast<std::ptrdiff_t>(i);
            }
        }

        // visibleAncestorIndex follows every active structural node in the
        // native tree, but transform-only nodes do not produce render items.
        // Collapse those gaps while the local node array is still available
        // so the later flattened command walk reaches the actual composite
        // ancestor instead of stopping at a missing command.
        for(auto &entry : entries) {
            int ancestorIndex = entry.visibleAncestorIndex;
            if(++ancestorVisitToken == 0) {
                std::fill(ancestorVisitMarks.begin(),
                          ancestorVisitMarks.end(), 0);
                ++ancestorVisitToken;
            }
            while(ancestorIndex >= 0 &&
                  ancestorIndex < static_cast<int>(nodes.size()) &&
                  entryIndexByNode[ancestorIndex] < 0 &&
                  ancestorVisitMarks[ancestorIndex] !=
                      ancestorVisitToken) {
                ancestorVisitMarks[ancestorIndex] = ancestorVisitToken;
                ancestorIndex = nodes[ancestorIndex].visibleAncestorIndex;
            }
            entry.visibleAncestorIndex =
                ancestorIndex >= 0 &&
                        ancestorIndex < static_cast<int>(nodes.size()) &&
                        entryIndexByNode[ancestorIndex] >= 0
                    ? ancestorIndex
                    : -1;
        }

        auto unionPaintBox =
            [](detail::PlayerRuntime::PreparedRenderItem &parent,
               const detail::PlayerRuntime::PreparedRenderItem &child) {
                if(child.paintBox[2] < child.paintBox[0] ||
                   child.paintBox[3] < child.paintBox[1]) {
                    return;
                }
                if(parent.paintBox[2] < parent.paintBox[0] ||
                   parent.paintBox[3] < parent.paintBox[1]) {
                    parent.paintBox = child.paintBox;
                    return;
                }
                parent.paintBox[0] = std::min(parent.paintBox[0], child.paintBox[0]);
                parent.paintBox[1] = std::min(parent.paintBox[1], child.paintBox[1]);
                parent.paintBox[2] = std::max(parent.paintBox[2], child.paintBox[2]);
                parent.paintBox[3] = std::max(parent.paintBox[3], child.paintBox[3]);
            };

        for(const auto &childEntry : entries) {
            if(++ancestorVisitToken == 0) {
                std::fill(ancestorVisitMarks.begin(),
                          ancestorVisitMarks.end(), 0);
                ++ancestorVisitToken;
            }
            for(int ancestorIndex = childEntry.visibleAncestorIndex;
                ancestorIndex >= 0 &&
                ancestorIndex < static_cast<int>(nodes.size()); ) {
                if(ancestorVisitMarks[ancestorIndex] ==
                   ancestorVisitToken) {
                    if(LOGGER && std::getenv("AETHERKIRI_MOTION_DEBUG")) {
                        LOGGER->warn(
                            "motion prepare paintBox ancestor cycle skipped: motion={} childNode={} ancestor={}",
                            _runtime->activeMotion
                                ? _runtime->activeMotion->path
                                : std::string("<none>"),
                            childEntry.nodeLabel, ancestorIndex);
                    }
                    break;
                }
                ancestorVisitMarks[ancestorIndex] = ancestorVisitToken;
                const auto parentIndex = entryIndexByNode[ancestorIndex];
                if(parentIndex < 0) {
                    break;
                }
                auto &parentEntry =
                    entries[static_cast<size_t>(parentIndex)];
                const auto &ancestorNode = nodes[parentEntry.nodeIndex];
                unionPaintBox(parentEntry, childEntry);
                const int nextAncestorIndex = ancestorNode.visibleAncestorIndex;
                if(nextAncestorIndex == ancestorIndex) {
                    break;
                }
                ancestorIndex = nextAncestorIndex;
            }
        }
    }

    bool Player::prepareRenderItems() {
        if(!_runtime) {
            return false;
        }

        static thread_local std::vector<const Player *> s_prepareStack;
        for(const Player *preparing : s_prepareStack) {
            if(preparing == this) {
                if(LOGGER && std::getenv("AETHERKIRI_MOTION_DEBUG")) {
                    LOGGER->warn(
                        "motion prepareRenderItems recursive player skipped: player={} motion={} depth={}",
                        static_cast<const void *>(this),
                        _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string("<none>"),
                        s_prepareStack.size());
                }
                _runtime->preparedRenderItems.clear();
                _runtime->preparedRenderItemsValid = false;
                return false;
            }
        }
        if(s_prepareStack.size() >= 32) {
            if(LOGGER && std::getenv("AETHERKIRI_MOTION_DEBUG")) {
                LOGGER->warn(
                    "motion prepareRenderItems depth limit skipped: player={} motion={} depth={}",
                    static_cast<const void *>(this),
                    _runtime->activeMotion ? _runtime->activeMotion->path
                                           : std::string("<none>"),
                    s_prepareStack.size());
            }
            _runtime->preparedRenderItems.clear();
            _runtime->preparedRenderItemsValid = false;
            return false;
        }
        if(_motionParentPlayer &&
           _runtime->preparedRenderItemsValid &&
           _runtime->preparedLayerStateGeneration ==
               _runtime->layerStateGeneration &&
           _runtime->preparedDrawAffineMatrix ==
               _runtime->drawAffineMatrix) {
            return !_runtime->preparedRenderItems.empty();
        }
        s_prepareStack.push_back(this);
        struct PrepareStackGuard {
            std::vector<const Player *> &stack;
            ~PrepareStackGuard() { stack.pop_back(); }
        } prepareStackGuard{ s_prepareStack };

        _runtime->preparedRenderItemsValid = false;
        _runtime->preparedRenderItems.clear();
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};

        struct PendingChildRenderItems {
            int parentNodeIndex = -1;
            int externalAncestorNodeIndex = -1;
            int externalMeshAncestorIndex = -1;
            // A flattened child still belongs to the nearest type-12
            // off-screen composite. Keep that boundary separately from the
            // visible-ancestor link used for draw ordering.
            int externalCompositeClipNodeIndex = -1;
            bool forceExternalAncestorForRoot = false;
            std::string childMotionPath;
            std::vector<detail::PlayerRuntime::PreparedRenderItem> entries;
        };
        std::vector<PendingChildRenderItems> pendingChildItems;
        pendingChildItems.reserve(_runtime->nodes.size());
        std::vector<std::uint32_t> prepareVisitMarks(
            _runtime->nodes.size(), 0);
        std::uint32_t prepareVisitToken = 0;
        auto acquirePrepareVisitToken = [&]() {
            if(++prepareVisitToken == 0) {
                std::fill(prepareVisitMarks.begin(),
                          prepareVisitMarks.end(), 0);
                ++prepareVisitToken;
            }
            return prepareVisitToken;
        };

        auto collectChildEntries = [&](int parentNodeIndex, Player *child) {
            if(!child || !child->_runtime) {
                return;
            }
            if(!child->prepareRenderItems()) {
                return;
            }
            auto &childEntries = child->_runtime->preparedRenderItems;
            if(childEntries.empty()) {
                return;
            }
            PendingChildRenderItems pending;
            pending.parentNodeIndex = parentNodeIndex;
            if(parentNodeIndex >= 0 &&
               parentNodeIndex < static_cast<int>(_runtime->nodes.size())) {
                const auto &parentNode = _runtime->nodes[parentNodeIndex];
                int stencilCompositeAncestorIndex = -1;
                const auto visitToken = acquirePrepareVisitToken();
                for(int ancestorIndex = parentNode.visibleAncestorIndex;
                    ancestorIndex >= 0 &&
                    ancestorIndex < static_cast<int>(_runtime->nodes.size()); ) {
                    if(prepareVisitMarks[ancestorIndex] == visitToken) {
                        break;
                    }
                    prepareVisitMarks[ancestorIndex] = visitToken;
                    const auto &ancestor = _runtime->nodes[ancestorIndex];
                    if(ancestor.nodeType == 12) {
                        stencilCompositeAncestorIndex = ancestorIndex;
                        break;
                    }
                    const int nextAncestorIndex =
                        ancestor.visibleAncestorIndex;
                    if(nextAncestorIndex == ancestorIndex) {
                        break;
                    }
                    ancestorIndex = nextAncestorIndex;
                }
                const bool insideStencilComposite =
                    stencilCompositeAncestorIndex >= 0;
                pending.forceExternalAncestorForRoot =
                    parentNode.meshCombineEnabled || insideStencilComposite;
                // Native sub-motion roots keep a node pointer in their mesh
                // ancestor slot.  Indices cannot cross our Player runtimes,
                // so remember the equivalent external chain and apply it to
                // the flattened child geometry below.
                pending.externalMeshAncestorIndex =
                    parentNode.meshCombineEnabled
                        ? parentNodeIndex
                        : parentNode.meshAncestorIndex;
                pending.externalAncestorNodeIndex =
                    parentNode.meshCombineEnabled
                        ? parentNodeIndex
                        : (insideStencilComposite
                               ? stencilCompositeAncestorIndex
                               : parentNode.visibleAncestorIndex);
                pending.externalCompositeClipNodeIndex =
                    stencilCompositeAncestorIndex;
            }
            pending.childMotionPath = child->_runtime->activeMotion
                ? child->_runtime->activeMotion->path
                : std::string("<none>");
            // A nested Player is flattened into this Player's sole output
            // surface. Transfer the prepared entries instead of deep-copying
            // every mesh/corner/stencil vector at each ownership level. The
            // child cache cannot be reused after its entries have moved, so
            // invalidate it explicitly; a later independent render will
            // rebuild it from the unchanged layer state.
            pending.entries = std::move(childEntries);
            child->_runtime->preparedRenderItemsValid = false;
            detail::logoChainTraceLogf(
                motionPath, "prepare.childCollect", "0x6F363C",
                _clampedEvalTime,
                "parentNodeIndex={} childMotionPath={} collected={}",
                parentNodeIndex, pending.childMotionPath, pending.entries.size());
            pendingChildItems.push_back(std::move(pending));
        };

        // The native player can keep preview child players on separate output
        // surfaces. AetherKiri flattens a motion into one KiriKiri layer, so an
        // active nested motion must be collected here as well. Otherwise a CG
        // viewer preview silently drops any artwork authored in nodeType 3/4
        // children (for example the centre of eyechatch.mtn). Do not infer this
        // from the motion filename: ordinary gallery composites use the same
        // nesting as the previously handled sd* presentations.
        const bool expandChildRenderItems = true;
        if(expandChildRenderItems) {
            for(size_t ni = 1; ni < _runtime->nodes.size(); ++ni) {
                auto &node = _runtime->nodes[ni];
                if(node.nodeType == 3) {
                    collectChildEntries(static_cast<int>(ni),
                                        node.getChildPlayer());
                } else if(node.nodeType == 4) {
                    const int particleCount = node.getParticleCount();
                    for(int pi = 0; pi < particleCount; ++pi) {
                        collectChildEntries(static_cast<int>(ni),
                                            node.getParticleChild(pi));
                    }
                }
            }
        }

        appendPreparedRenderItems();
        const auto localRenderScopeId = reinterpret_cast<std::uintptr_t>(
            _runtime.get());
        for(auto &entry : _runtime->preparedRenderItems) {
            entry.renderScopeId = localRenderScopeId;
            entry.scopedNodeIndex = entry.nodeIndex;
            if(entry.visibleAncestorIndex >= 0) {
                entry.parentRenderScopeId = localRenderScopeId;
                entry.scopedParentNodeIndex =
                    entry.visibleAncestorIndex;
            }
        }

        auto isValidPreparedPaintBox =
            [](const std::array<float, 4> &box) {
                return std::isfinite(box[0]) && std::isfinite(box[1]) &&
                    std::isfinite(box[2]) && std::isfinite(box[3]) &&
                    box[2] >= box[0] && box[3] >= box[1];
            };

        auto unionPreparedPaintBox =
            [&](std::array<float, 4> &bounds,
                const std::array<float, 4> &box,
                bool &haveBounds) {
                if(!isValidPreparedPaintBox(box)) {
                    return;
                }
                if(!haveBounds) {
                    bounds = box;
                    haveBounds = true;
                    return;
                }
                bounds[0] = std::min(bounds[0], box[0]);
                bounds[1] = std::min(bounds[1], box[1]);
                bounds[2] = std::max(bounds[2], box[2]);
                bounds[3] = std::max(bounds[3], box[3]);
            };

        auto applyExternalMeshChain =
            [&](PendingChildRenderItems &pending) {
                int meshWalk = pending.externalMeshAncestorIndex;
                if(meshWalk < 0 ||
                   meshWalk >= static_cast<int>(_runtime->nodes.size())) {
                    return;
                }

                std::vector<int> meshChain;
                meshChain.reserve(8);
                const auto visitToken = acquirePrepareVisitToken();
                while(meshWalk >= 0 &&
                      meshWalk < static_cast<int>(_runtime->nodes.size())) {
                    if(prepareVisitMarks[meshWalk] == visitToken) {
                        break;
                    }
                    prepareVisitMarks[meshWalk] = visitToken;
                    const auto &ancestor = _runtime->nodes[meshWalk];
                    if(ancestor.hasMeshData &&
                       ancestor.meshWorldControlPoints.size() == 32) {
                        meshChain.push_back(meshWalk);
                    }
                    meshWalk = ancestor.meshAncestorIndex;
                }
                if(meshChain.empty()) {
                    return;
                }

                const auto &dam = _runtime->drawAffineMatrix;
                const double det = dam[0] * dam[3] - dam[2] * dam[1];
                if(std::fabs(det) <= 1e-12) {
                    return;
                }
                const double inverseDeterminant = 1.0 / det;
                std::vector<ExternalMeshTransform> meshTransforms;
                meshTransforms.reserve(meshChain.size());
                for(const int ancestorIndex : meshChain) {
                    const auto &ancestor =
                        _runtime->nodes[ancestorIndex];
                    meshTransforms.push_back({
                        ancestor.meshWorldControlPoints.data(),
                        ancestor.meshInvM11, ancestor.meshInvM12,
                        ancestor.meshInvM21, ancestor.meshInvM22,
                        ancestor.meshInvOffX, ancestor.meshInvOffY
                    });
                }

                size_t deformedEntries = 0;
                for(auto &entry : pending.entries) {
                    if(!entry.hasOwnSource) {
                        continue;
                    }
                    // A native nodeType-3 render item owns the child Player's
                    // completed output surface.  Our single-surface renderer
                    // flattens that child instead, so the corresponding item
                    // can have a source selector but no image-sized geometry
                    // of its own (all four default corners are {0,0}).  Do not
                    // feed that placeholder through the inherited mesh chain:
                    // the native StepFrameMotionLayer path applies the chain
                    // to the child before BuildLayerFrameInfo, never to a
                    // synthetic zero-sized bitmap.
                    float cornerMinX = entry.corners[0];
                    float cornerMaxX = entry.corners[0];
                    float cornerMinY = entry.corners[1];
                    float cornerMaxY = entry.corners[1];
                    for(size_t point = 2;
                        point + 1 < entry.corners.size(); point += 2) {
                        cornerMinX = std::min(cornerMinX,
                                              entry.corners[point]);
                        cornerMaxX = std::max(cornerMaxX,
                                              entry.corners[point]);
                        cornerMinY = std::min(cornerMinY,
                                              entry.corners[point + 1]);
                        cornerMaxY = std::max(cornerMaxY,
                                              entry.corners[point + 1]);
                    }
                    const bool hasCornerGeometry =
                        std::isfinite(cornerMinX) &&
                        std::isfinite(cornerMaxX) &&
                        std::isfinite(cornerMinY) &&
                        std::isfinite(cornerMaxY) &&
                        cornerMaxX - cornerMinX > 1e-5f &&
                        cornerMaxY - cornerMinY > 1e-5f;
                    const bool hasMeshGeometry =
                        entry.meshPoints.size() >= 6;
                    if(!hasCornerGeometry && !hasMeshGeometry) {
                        continue;
                    }

                    // Native StepFrameMeshChain keeps an affine child Player
                    // surface tessellated for the entire lifetime of an
                    // inherited Bezier patch, then deforms every vertex. Four
                    // corners cannot carry the eyelid curve to nested iris and
                    // eye-white layers, and changing topology only after the
                    // curve becomes nonlinear causes a blink-boundary flash.
                    const auto &divisionNode =
                        _runtime->nodes[meshChain.front()];
                    detail::tessellatePreparedItemForExternalMesh(
                        entry, _emoteMeshDivisionRatio,
                        divisionNode.meshDivision);

                    // Preserve authored child tessellation when present and
                    // the compatibility grid manufactured above when the
                    // external patch cannot be represented by four corners.
                    deformExternalMeshPoints(
                        entry.meshPoints.data(),
                        entry.meshPoints.size() / 2,
                        dam.data(), inverseDeterminant,
                        meshTransforms.data(), meshTransforms.size(), true);
                    if(hasCornerGeometry) {
                        deformExternalMeshPoints(
                            entry.corners.data(), entry.corners.size() / 2,
                            dam.data(), inverseDeterminant,
                            meshTransforms.data(), meshTransforms.size(), true);
                    }

                    bool haveBounds = false;
                    auto includePoint = [&](float x, float y) {
                        if(!haveBounds) {
                            entry.paintBox = {x, y, x, y};
                            haveBounds = true;
                            return;
                        }
                        entry.paintBox[0] = std::min(entry.paintBox[0], x);
                        entry.paintBox[1] = std::min(entry.paintBox[1], y);
                        entry.paintBox[2] = std::max(entry.paintBox[2], x);
                        entry.paintBox[3] = std::max(entry.paintBox[3], y);
                    };
                    for(size_t point = 0;
                        point + 1 < entry.meshPoints.size(); point += 2) {
                        includePoint(entry.meshPoints[point],
                                     entry.meshPoints[point + 1]);
                    }
                    if(!haveBounds && hasCornerGeometry) {
                        for(size_t point = 0;
                            point + 1 < entry.corners.size(); point += 2) {
                            includePoint(entry.corners[point],
                                         entry.corners[point + 1]);
                        }
                    }
                    if(haveBounds) {
                        entry.paintBox[0] = std::floor(entry.paintBox[0]);
                        entry.paintBox[1] = std::floor(entry.paintBox[1]);
                        entry.paintBox[2] = std::ceil(entry.paintBox[2]);
                        entry.paintBox[3] = std::ceil(entry.paintBox[3]);
                    }
                    ++deformedEntries;
                }

                if(LOGGER &&
                   std::getenv("AETHERKIRI_EMOTE_MESH_TRACE") &&
                   deformedEntries > 0) {
                    LOGGER->info(
                        "[EMOTE_MESH] child external mesh applied: motion={} parentNode={} parentLabel={} childMotion={} chainDepth={} chainFirst={} entries={}",
                        motionPath, pending.parentNodeIndex,
                        pending.parentNodeIndex >= 0 &&
                                pending.parentNodeIndex <
                                    static_cast<int>(_runtime->nodes.size())
                            ? _runtime->nodes[pending.parentNodeIndex].layerName
                            : std::string("<invalid>"),
                        pending.childMotionPath, meshChain.size(),
                        meshChain.empty() ? -1 : meshChain.front(),
                        deformedEntries);
                }
            };

        auto inheritParentClipViewport =
            [&](PendingChildRenderItems &pending) {
                if(pending.parentNodeIndex < 0 ||
                   pending.parentNodeIndex >=
                       static_cast<int>(_runtime->nodes.size())) {
                    return;
                }

                const auto &parentNode =
                    _runtime->nodes[pending.parentNodeIndex];
                const auto &dam = _runtime->drawAffineMatrix;
                auto transformClipPoint = [&](float x, float y) {
                    return std::array<float, 2>{
                        static_cast<float>(dam[0] * x + dam[2] * y + dam[4]),
                        static_cast<float>(dam[1] * x + dam[3] * y + dam[5])
                    };
                };

                std::array<float, 4> inheritedViewport{
                    1.0f, 1.0f, -1.0f, -1.0f
                };
                bool haveInheritedViewport = false;
                bool inheritedFromComposite = false;
                auto includeViewport = [&](const std::array<float, 4> &box) {
                    if(!std::isfinite(box[0]) || !std::isfinite(box[1]) ||
                       !std::isfinite(box[2]) || !std::isfinite(box[3]) ||
                       box[2] < box[0] || box[3] < box[1]) {
                        return;
                    }
                    if(!haveInheritedViewport) {
                        inheritedViewport = box;
                        haveInheritedViewport = true;
                        return;
                    }
                    inheritedViewport[0] = std::max(
                        inheritedViewport[0], box[0]);
                    inheritedViewport[1] = std::max(
                        inheritedViewport[1], box[1]);
                    inheritedViewport[2] = std::min(
                        inheritedViewport[2], box[2]);
                    inheritedViewport[3] = std::min(
                        inheritedViewport[3], box[3]);
                };

                // Preserve the existing type-7 shape-chain behavior.
                const int clipIndex = parentNode.parentClipIndex;
                if(clipIndex >= 0 &&
                   clipIndex < static_cast<int>(_runtime->nodes.size())) {
                    const auto &clipNode = _runtime->nodes[clipIndex];
                    if(clipNode.shapeAABB[2] >= clipNode.shapeAABB[0] &&
                       clipNode.shapeAABB[3] >= clipNode.shapeAABB[1]) {
                        const auto p0 = transformClipPoint(
                            clipNode.shapeAABB[0], clipNode.shapeAABB[1]);
                        const auto p1 = transformClipPoint(
                            clipNode.shapeAABB[2], clipNode.shapeAABB[1]);
                        const auto p2 = transformClipPoint(
                            clipNode.shapeAABB[2], clipNode.shapeAABB[3]);
                        const auto p3 = transformClipPoint(
                            clipNode.shapeAABB[0], clipNode.shapeAABB[3]);
                        includeViewport({
                            std::min(std::min(p0[0], p1[0]),
                                     std::min(p2[0], p3[0])),
                            std::min(std::min(p0[1], p1[1]),
                                     std::min(p2[1], p3[1])),
                            std::max(std::max(p0[0], p1[0]),
                                     std::max(p2[0], p3[0])),
                            std::max(std::max(p0[1], p1[1]),
                                     std::max(p2[1], p3[1]))
                        });
                    }
                }

                // A type-12 node is rendered by krkrsdl3 into an off-screen
                // target before that target is composited.  Its final
                // paintBox is expanded by descendants, so it is not a valid
                // boundary.  Carry the node's own quad instead; this keeps a
                // child authored at x<0 invisible until it enters the SD
                // surface, then reveals only the intersecting part.
                const int compositeIndex =
                    pending.externalCompositeClipNodeIndex;
                if(compositeIndex >= 0 &&
                   compositeIndex < static_cast<int>(_runtime->nodes.size())) {
                    const auto &compositeNode = _runtime->nodes[compositeIndex];
                    std::array<float, 8> compositeCorners{};
                    bool haveCompositeGeometry = false;
                    for(const auto &localEntry :
                        _runtime->preparedRenderItems) {
                        if(localEntry.nodeIndex != compositeIndex ||
                           !localEntry.hasOwnSource) {
                            continue;
                        }
                        bool finiteCorners = true;
                        for(const float value : localEntry.corners) {
                            if(!std::isfinite(value)) {
                                finiteCorners = false;
                                break;
                            }
                        }
                        if(finiteCorners) {
                            float minX = localEntry.corners[0];
                            float minY = localEntry.corners[1];
                            float maxX = minX;
                            float maxY = minY;
                            for(int ci = 1; ci < 4; ++ci) {
                                minX = std::min(minX,
                                                localEntry.corners[ci * 2]);
                                minY = std::min(minY,
                                                localEntry.corners[ci * 2 + 1]);
                                maxX = std::max(maxX,
                                                localEntry.corners[ci * 2]);
                                maxY = std::max(maxY,
                                                localEntry.corners[ci * 2 + 1]);
                            }
                            if(maxX - minX > 1e-5f &&
                               maxY - minY > 1e-5f) {
                                compositeCorners = localEntry.corners;
                                haveCompositeGeometry = true;
                            }
                        }
                        break;
                    }
                    if(!haveCompositeGeometry &&
                       compositeNode.clipW > 0.0 &&
                       compositeNode.clipH > 0.0) {
                        for(int ci = 0; ci < 4; ++ci) {
                            const auto point = transformClipPoint(
                                compositeNode.vertices[ci * 2],
                                compositeNode.vertices[ci * 2 + 1]);
                            compositeCorners[ci * 2] = point[0];
                            compositeCorners[ci * 2 + 1] = point[1];
                        }
                        haveCompositeGeometry = true;
                    }
                    if(!haveCompositeGeometry &&
                       compositeNode.bounds[2] >= compositeNode.bounds[0] &&
                       compositeNode.bounds[3] >= compositeNode.bounds[1]) {
                        const auto p0 = transformClipPoint(
                            compositeNode.bounds[0], compositeNode.bounds[1]);
                        const auto p1 = transformClipPoint(
                            compositeNode.bounds[2], compositeNode.bounds[1]);
                        const auto p2 = transformClipPoint(
                            compositeNode.bounds[2], compositeNode.bounds[3]);
                        const auto p3 = transformClipPoint(
                            compositeNode.bounds[0], compositeNode.bounds[3]);
                        compositeCorners = {
                            p0[0], p0[1], p1[0], p1[1],
                            p2[0], p2[1], p3[0], p3[1]
                        };
                        haveCompositeGeometry = true;
                    }
                    if(haveCompositeGeometry) {
                        std::array<float, 4> compositeViewport{
                            compositeCorners[0], compositeCorners[1],
                            compositeCorners[0], compositeCorners[1]
                        };
                        for(int ci = 1; ci < 4; ++ci) {
                            compositeViewport[0] = std::min(
                                compositeViewport[0], compositeCorners[ci * 2]);
                            compositeViewport[1] = std::min(
                                compositeViewport[1], compositeCorners[ci * 2 + 1]);
                            compositeViewport[2] = std::max(
                                compositeViewport[2], compositeCorners[ci * 2]);
                            compositeViewport[3] = std::max(
                                compositeViewport[3], compositeCorners[ci * 2 + 1]);
                        }
                        includeViewport(compositeViewport);
                        inheritedFromComposite = true;
                    }
                }

                if(!haveInheritedViewport) {
                    return;
                }
                for(auto &entry : pending.entries) {
                    entry.viewportInheritedFromComposite =
                        entry.viewportInheritedFromComposite ||
                        inheritedFromComposite;
                    if(entry.hasViewport &&
                       entry.viewport[2] >= entry.viewport[0] &&
                       entry.viewport[3] >= entry.viewport[1]) {
                        entry.viewport[0] = std::max(
                            entry.viewport[0], inheritedViewport[0]);
                        entry.viewport[1] = std::max(
                            entry.viewport[1], inheritedViewport[1]);
                        entry.viewport[2] = std::min(
                            entry.viewport[2], inheritedViewport[2]);
                        entry.viewport[3] = std::min(
                            entry.viewport[3], inheritedViewport[3]);
                    } else {
                        entry.viewport = inheritedViewport;
                        entry.hasViewport = true;
                    }
                }
            };

        // The local node buffer and ordinary equal-Z leaves both preserve
        // authored order. Emit sibling child buffers by that same parent-slot
        // order before inserting each child at its slot. Reversing only the
        // child list puts later-authored background motions over earlier
        // button/character motions even though their local leaves are no
        // longer reversed.
        std::stable_sort(
            pendingChildItems.begin(), pendingChildItems.end(),
            [](const PendingChildRenderItems &lhs,
               const PendingChildRenderItems &rhs) {
                return detail::preparedChildParentSlotLess(
                    lhs.parentNodeIndex, rhs.parentNodeIndex);
            });

        int nextMergedNodeIndex = static_cast<int>(_runtime->nodes.size());
        for(auto &pending : pendingChildItems) {
            if(pending.entries.empty()) {
                continue;
            }
            applyExternalMeshChain(pending);
            // A child Player owns a separate node array, so its local
            // parentClipIndex cannot point back into the containing motion.
            // Carry the nearest type-7 clip into the flattened child items.
            inheritParentClipViewport(pending);

            // Child players use node indices local to their own runtime. Once
            // flattened, isolate those namespaces so render-parent lookup
            // cannot bind to an unrelated command with the same local index.
            int maxChildNodeIndex = -1;
            for(const auto &entry : pending.entries) {
                maxChildNodeIndex = std::max(maxChildNodeIndex,
                                             entry.nodeIndex);
                maxChildNodeIndex = std::max(maxChildNodeIndex,
                                             entry.visibleAncestorIndex);
                for(const int maskNodeIndex : entry.stencilMaskNodeIndices) {
                    maxChildNodeIndex = std::max(maxChildNodeIndex,
                                                 maskNodeIndex);
                }
            }
            if(maxChildNodeIndex >= 0) {
                const int nodeIndexOffset = nextMergedNodeIndex;
                for(auto &entry : pending.entries) {
                    const bool hadScopedRenderParent =
                        entry.parentRenderScopeId != 0 &&
                        entry.scopedParentNodeIndex >= 0;
                    if(entry.nodeIndex >= 0) {
                        entry.nodeIndex += nodeIndexOffset;
                    }
                    if(entry.visibleAncestorIndex >= 0) {
                        entry.visibleAncestorIndex += nodeIndexOffset;
                    } else if(pending.externalAncestorNodeIndex >= 0 &&
                              (entry.groupOnly ||
                               pending.forceExternalAncestorForRoot)) {
                        // Composite roots remain inside their containing
                        // group. A plain bitmap root is already a complete
                        // colour draw, though: attaching it to the nearest
                        // type-12 ancestor makes later nested groups cover it
                        // (gallery sticker/icon variants), whereas multi-layer
                        // variants stay independent through their local parent
                        // indices. Mesh-combined roots are the explicit
                        // exception and must retain the external parent.
                        entry.visibleAncestorIndex =
                            pending.externalAncestorNodeIndex;
                        entry.parentRenderScopeId = localRenderScopeId;
                        entry.scopedParentNodeIndex =
                            pending.externalAncestorNodeIndex;
                    }
                    if(hadScopedRenderParent &&
                       pending.externalAncestorNodeIndex >= 0) {
                        const detail::PlayerRuntime::
                            RenderAncestorReference outerAncestor{
                                localRenderScopeId,
                                pending.externalAncestorNodeIndex};
                        if(entry.outerRenderAncestorChain.empty() ||
                           entry.outerRenderAncestorChain.back()
                                   .renderScopeId !=
                               outerAncestor.renderScopeId ||
                           entry.outerRenderAncestorChain.back()
                                   .scopedNodeIndex !=
                               outerAncestor.scopedNodeIndex) {
                            entry.outerRenderAncestorChain.push_back(
                                outerAncestor);
                        }
                    }
                    for(int &maskNodeIndex : entry.stencilMaskNodeIndices) {
                        if(maskNodeIndex >= 0) {
                            maskNodeIndex += nodeIndexOffset;
                        }
                    }
                    if(entry.implicitVisibleStencilGroupNodeIndex >= 0) {
                        entry.implicitVisibleStencilGroupNodeIndex +=
                            nodeIndexOffset;
                    }
                }
                nextMergedNodeIndex += maxChildNodeIndex + 1;
            }
            auto insertPos = _runtime->preparedRenderItems.end();
            for(auto it = _runtime->preparedRenderItems.begin();
                it != _runtime->preparedRenderItems.end(); ++it) {
                // Child node indices are remapped into the containing numeric
                // namespace. Only a local-scope item can delimit the native
                // parent slot; otherwise an already-inserted child's large
                // index would move the next sibling to the wrong side. The
                // local buffer preserves authored order, so the child is
                // emitted at its parent slot: after earlier backdrop/mask
                // nodes and before the next later local node.
                if(it->renderScopeId == localRenderScopeId &&
                   detail::preparedLocalNodeFollowsChildSlot(
                       it->nodeIndex, pending.parentNodeIndex)) {
                    insertPos = it;
                    break;
                }
            }
            const auto insertedCount = pending.entries.size();
            _runtime->preparedRenderItems.insert(
                insertPos,
                std::make_move_iterator(pending.entries.begin()),
                std::make_move_iterator(pending.entries.end()));
            detail::logoChainTraceLogf(
                motionPath, "prepare.childMerge", "0x6F363C",
                _clampedEvalTime,
                "parentNodeIndex={} childMotionPath={} inserted={} parentTotalAfterInsert={}",
                pending.parentNodeIndex, pending.childMotionPath, insertedCount,
                _runtime->preparedRenderItems.size());
        }

        // Child-player items are merged after appendPreparedRenderItems() has
        // already propagated local paint boxes. Re-run the direct parent
        // union now so a type-12 off-screen group covers the complete nested
        // iris/highlight surface instead of retaining its 16x16 placeholder
        // bitmap bounds.
        int maxMergedNodeIndex = -1;
        for(const auto &entry : _runtime->preparedRenderItems) {
            maxMergedNodeIndex =
                std::max(maxMergedNodeIndex, entry.nodeIndex);
        }
        std::vector<std::ptrdiff_t> mergedEntryIndexByNode(
            static_cast<size_t>(maxMergedNodeIndex + 1), -1);
        for(size_t i = 0; i < _runtime->preparedRenderItems.size(); ++i) {
            const int nodeIndex =
                _runtime->preparedRenderItems[i].nodeIndex;
            if(nodeIndex >= 0 &&
               mergedEntryIndexByNode[static_cast<size_t>(nodeIndex)] < 0) {
                mergedEntryIndexByNode[static_cast<size_t>(nodeIndex)] =
                    static_cast<std::ptrdiff_t>(i);
            }
        }
        for(const auto &childEntry : _runtime->preparedRenderItems) {
            if(childEntry.visibleAncestorIndex < 0 ||
               childEntry.visibleAncestorIndex > maxMergedNodeIndex) {
                continue;
            }
            const auto parentIndex = mergedEntryIndexByNode[
                static_cast<size_t>(childEntry.visibleAncestorIndex)];
            if(parentIndex < 0) {
                continue;
            }
            auto &parentEntry =
                _runtime->preparedRenderItems[
                    static_cast<size_t>(parentIndex)];
            if(!parentEntry.groupOnly ||
               childEntry.paintBox[2] < childEntry.paintBox[0] ||
               childEntry.paintBox[3] < childEntry.paintBox[1]) {
                continue;
            }
            if(parentEntry.paintBox[2] < parentEntry.paintBox[0] ||
               parentEntry.paintBox[3] < parentEntry.paintBox[1]) {
                parentEntry.paintBox = childEntry.paintBox;
            } else {
                parentEntry.paintBox[0] = std::min(
                    parentEntry.paintBox[0], childEntry.paintBox[0]);
                parentEntry.paintBox[1] = std::min(
                    parentEntry.paintBox[1], childEntry.paintBox[1]);
                parentEntry.paintBox[2] = std::max(
                    parentEntry.paintBox[2], childEntry.paintBox[2]);
                parentEntry.paintBox[3] = std::max(
                    parentEntry.paintBox[3], childEntry.paintBox[3]);
            }
        }
        const bool tracePrepareSort =
            detail::logoChainTraceEnabled(_runtime->activeMotion);
        std::vector<double> beforeSortKeys;
        if(tracePrepareSort) {
            beforeSortKeys.reserve(_runtime->preparedRenderItems.size());
            for(const auto &item : _runtime->preparedRenderItems) {
                beforeSortKeys.push_back(item.sortKey);
            }
        }
        // Aligned to sub_6D4F00 (0x6D4F00): compare render-item sort key.
        const auto renderItemLess =
            [](const detail::PlayerRuntime::PreparedRenderItem &lhs,
               const detail::PlayerRuntime::PreparedRenderItem &rhs) {
                return lhs.sortKey < rhs.sortKey;
            };
        if(!std::is_sorted(_runtime->preparedRenderItems.begin(),
                           _runtime->preparedRenderItems.end(),
                           renderItemLess)) {
            std::stable_sort(_runtime->preparedRenderItems.begin(),
                _runtime->preparedRenderItems.end(), renderItemLess);
        }
        if(tracePrepareSort) {
            std::ostringstream beforeSort;
            std::ostringstream afterSort;
            for(size_t i = 0; i < beforeSortKeys.size(); ++i) {
                if(i) beforeSort << ",";
                beforeSort << beforeSortKeys[i];
            }
            for(size_t i = 0; i < _runtime->preparedRenderItems.size(); ++i) {
                if(i) afterSort << ",";
                afterSort << _runtime->preparedRenderItems[i].sortKey;
            }
            detail::logoChainTraceLogf(
                motionPath, "prepare.sort", "0x6D5164/0x6D4F00",
                _clampedEvalTime,
                "itemCount={} sortKeysBefore=[{}] sortKeysAfter=[{}]",
                _runtime->preparedRenderItems.size(), beforeSort.str(),
                afterSort.str());
        }
        _runtime->preparedLayerStateGeneration =
            _runtime->layerStateGeneration;
        _runtime->preparedDrawAffineMatrix =
            _runtime->drawAffineMatrix;
        _runtime->preparedRenderItemsValid = true;
        return !_runtime->preparedRenderItems.empty();
    }

    void Player::applyPreparedRenderItemTranslateOffsets() {
        if(!_runtime) {
            return;
        }

        // Aligned to libkrkr2.so Player_applyTranslateOffset (0x6D5264):
        // normal path adds cameraOffset to prepared render items here.
        // Root position is already baked into node state during updateLayers.
        const double ofsX = static_cast<double>(_cameraOffsetX);
        const double ofsY = static_cast<double>(_cameraOffsetY);
        const bool traceTranslate =
            detail::logoChainTraceEnabled(_runtime->activeMotion);
        if(ofsX == 0.0 && ofsY == 0.0 && !traceTranslate) {
            return;
        }
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};
        for(auto &entry : _runtime->preparedRenderItems) {
            std::array<float, 8> beforeCorners{};
            std::array<float, 4> beforePaintBox{};
            std::array<float, 4> beforeViewport{};
            std::vector<float> beforeMeshPoints;
            if(traceTranslate) {
                beforeCorners = entry.corners;
                beforePaintBox = entry.paintBox;
                beforeViewport = entry.viewport;
                beforeMeshPoints = entry.meshPoints;
            }
            for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                entry.corners[ci] =
                    static_cast<float>(static_cast<double>(entry.corners[ci]) + ofsX);
                entry.corners[ci + 1] =
                    static_cast<float>(static_cast<double>(entry.corners[ci + 1]) + ofsY);
            }
            entry.paintBox[0] = static_cast<float>(static_cast<double>(entry.paintBox[0]) + ofsX);
            entry.paintBox[1] = static_cast<float>(static_cast<double>(entry.paintBox[1]) + ofsY);
            entry.paintBox[2] = static_cast<float>(static_cast<double>(entry.paintBox[2]) + ofsX);
            entry.paintBox[3] = static_cast<float>(static_cast<double>(entry.paintBox[3]) + ofsY);
            if(entry.hasViewport) {
                entry.viewport[0] =
                    static_cast<float>(static_cast<double>(entry.viewport[0]) + ofsX);
                entry.viewport[1] =
                    static_cast<float>(static_cast<double>(entry.viewport[1]) + ofsY);
                entry.viewport[2] =
                    static_cast<float>(static_cast<double>(entry.viewport[2]) + ofsX);
                entry.viewport[3] =
                    static_cast<float>(static_cast<double>(entry.viewport[3]) + ofsY);
            }
            for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                entry.meshPoints[pi] =
                    static_cast<float>(static_cast<double>(entry.meshPoints[pi]) + ofsX);
                entry.meshPoints[pi + 1] =
                    static_cast<float>(static_cast<double>(entry.meshPoints[pi + 1]) + ofsY);
            }
            if(traceTranslate) {
                bool ok = true;
                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    if(std::fabs((entry.corners[ci] - beforeCorners[ci]) -
                                 static_cast<float>(ofsX)) > 0.01f ||
                       std::fabs((entry.corners[ci + 1] - beforeCorners[ci + 1]) -
                                 static_cast<float>(ofsY)) > 0.01f) {
                        ok = false;
                        break;
                    }
                }
                if(ok && entry.hasViewport) {
                    for(size_t vi = 0; vi < entry.viewport.size(); vi += 2) {
                        if(std::fabs((entry.viewport[vi] - beforeViewport[vi]) -
                                     static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs((entry.viewport[vi + 1] -
                                      beforeViewport[vi + 1]) -
                                     static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                if(ok) {
                    for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                        if(std::fabs((entry.meshPoints[pi] - beforeMeshPoints[pi]) -
                                     static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs((entry.meshPoints[pi + 1] -
                                      beforeMeshPoints[pi + 1]) -
                                     static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "prepare.translate", "0x6D5264",
                    _clampedEvalTime,
                    fmt::format("cameraOffset=({:.3f},{:.3f}) applied to corners/paintBox/viewport/mesh", ofsX, ofsY),
                    fmt::format(
                        "nodeIndex={} beforeCorner0=({:.3f},{:.3f}) afterCorner0=({:.3f},{:.3f}) beforePaintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] afterPaintBox=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, beforeCorners[0], beforeCorners[1],
                        entry.corners[0], entry.corners[1],
                        beforePaintBox[0], beforePaintBox[1], beforePaintBox[2],
                        beforePaintBox[3], entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    ok,
                    "Player_applyTranslateOffset added more than cameraOffset");
            }
        }
    }

} // namespace motion
