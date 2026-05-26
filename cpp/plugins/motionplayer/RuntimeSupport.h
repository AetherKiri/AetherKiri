//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>

#include "tjs.h"
#include "psbfile/PSBFile.h"
#include "MotionNode.h"

namespace motion {
    class SourceCache;
}

namespace motion::detail {

    struct VariableFrameInfo {
        std::string label;
        double value = 0.0;
    };

    struct VariableControllerBinding {
        int type = -1;
        int index = -1;
        std::string source;
        std::string role;
    };

    struct SelectorControlOption {
        std::string label;
        double offValue = 0.0;
        double onValue = 0.0;
    };

    struct SelectorControlBinding {
        std::string label;
        std::vector<SelectorControlOption> options;
    };

    struct FixedControllerOutputBinding {
        std::string label;
        int type = -1;
        int index = -1;
        std::string role;
    };

    struct ClampControlBinding {
        int type = 0;
        std::string varLr;
        std::string varUd;
        double minValue = 0.0;
        double maxValue = 0.0;
    };

    struct TimelineControlFrame {
        double time = 0.0;
        bool isTypeZero = true;
        float value = 0.0f;
        double easingWeight = 1.0;
    };

    struct TimelineControlTrack {
        std::string label;
        // Aligned to libkrkr2.so sub_66FC5C byte at track+8:
        // set when label is present in instantVariableList (player+0x4F8).
        bool instantVariable = false;
        std::vector<TimelineControlFrame> frames;
    };

    struct TimelineControlBinding {
        std::string label;
        double loopBegin = -1.0;
        double loopEnd = -1.0;
        double lastTime = -1.0;
        std::vector<TimelineControlTrack> tracks;
    };

    struct TimelineControlKeyframe {
        float value = 0.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    struct TimelineControlAnimatorState {
        std::deque<TimelineControlKeyframe> queue;
        bool active = false;
        float currentValue = 0.0f;
        float startValue = 0.0f;
        float targetValue = 0.0f;
        float progress = 1.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    // Runtime-owned parameter entry. Aligned to libkrkr2.so's 56-byte
    // Player+384 parameter table populated inside Player_initNonEmoteMotion
    // (0x6B365C) via sub_6B1718 / sub_6B202C.
    struct MotionParameterEntry {
        std::string id;
        bool discretization = false;
        double rangeBegin = 0.0;
        double rangeEnd = 0.0;
        double rangeScale = 1.0;
        double value = 0.0;
        int mode = 0;
    };

    struct MotionClip {
        std::string label;
        std::string owner;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop restart point
        double totalFrames = 0.0;
        // Primary layer storage — PSB array order, duplicates preserved.
        // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0) reading
        // "layer" from Player+528 as a TJS Array iterated by index.
        std::vector<std::shared_ptr<const PSB::PSBDictionary>> layerList;
        std::vector<std::string> sourceCandidates;
        // Raw PSB objects retained for Player_initNonEmoteMotion (0x6B365C).
        // The parameter table is intentionally not cached here; it is rebuilt
        // on each player init to mirror libkrkr2.so ownership/lifetime.
        std::shared_ptr<const PSB::PSBDictionary> motionObject;
        std::shared_ptr<const PSB::PSBDictionary> contentObject;
    };

    struct TimelineState {
        std::string label;
        int flags = 0;
        bool playing = false;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop, <0 means stop at end
        double totalFrames = 0.0;
        double currentTime = 0.0;
        double blendRatio = 1.0;
        bool wasPlaying = false;  // for edge detection in dispatchEvents
        bool controlInitialized = false;
        double controlLastAppliedTime = 0.0;
        std::vector<int> controlFrameCursor;
        std::vector<float> controlTrackValues;
        std::vector<TimelineControlAnimatorState> controlTrackAnimators;
        TimelineControlAnimatorState blendAnimator;
        bool blendAutoStop = false;
    };

    // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
    // type=0: onAction(param1, param2), type=1: onSync()
    struct MotionEvent {
        int type = 0;
        std::string param1;
        std::string param2;
    };

    struct MotionSnapshot {
        std::string path;
        std::shared_ptr<PSB::PSBFile> file;
        std::shared_ptr<const PSB::PSBDictionary> root;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBResource>>
            resourcesByPath;
        tTJSVariant moduleValue;
        std::vector<std::string> mainTimelineLabels;
        std::vector<std::string> diffTimelineLabels;
        std::vector<std::string> variableLabels;
        std::unordered_map<std::string, bool> loopTimelines;
        std::unordered_map<std::string, double> timelineLoopTimes;
        std::unordered_map<std::string, double> timelineTotalFrames;
        std::unordered_map<std::string, std::pair<double, double>> variableRanges;
        std::unordered_map<std::string, std::vector<VariableFrameInfo>> variableFrames;
        std::unordered_map<std::string, VariableControllerBinding> controllerBindings;
        std::unordered_set<std::string> instantVariableLabels;
        std::unordered_map<std::string, SelectorControlBinding> selectorControls;
        std::vector<FixedControllerOutputBinding> fixedControllerOutputs;
        std::vector<ClampControlBinding> clampControls;
        std::vector<std::string> mirrorVariableMatchList;
        // Primary layer storage — PSB array order, duplicates preserved.
        // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0) which reads
        // the "layer" TJS Array from Player+528 and iterates by index.
        std::vector<std::shared_ptr<const PSB::PSBDictionary>> layerList;
        std::vector<std::string> sourceCandidates;
        // Primary clip storage — PSB priority[] order preserved.
        // Aligned to libkrkr2.so Player+548 (motion.priority TJSArray stored at
        // 0x6B37D0) + Player+616 (priority[currentIndex].content at 0x6B38FC).
        // Duplicate clip labels are allowed (index-addressable) but the
        // auxiliary label→index map below resolves name-based lookups using
        // last-wins semantics to mirror Player+24 labelMap behaviour.
        std::vector<MotionClip> clipList;
        std::unordered_map<std::string, int> clipIndexByLabel;
        std::unordered_map<std::string, TimelineControlBinding>
            timelineControlByLabel;
        std::vector<std::string> resourceAliases;
        double width = 0.0;
        double height = 0.0;
    };

    // Aligned to libkrkr2.so Player+1296 std::vector<LabelEntry> written by
    // Player_initVariables (0x6CD750). Each entry is 160 bytes in the binary
    // with these observed writes (offsets relative to entry base):
    //   +0   ttstr name   — from entry["scope"], split by ':' and take the
    //                       right half; empty when no scope / no colon.
    //   +24  ttstr label  — from entry["label"].
    //   +68  u8  flag68=1 — observed default (semantics not yet reversed).
    //   +124 u8  flag124=1 — observed default (semantics not yet reversed).
    // Read paths in the binary have not been fully traced; the struct exists
    // so the eager initialisation sequence can land without drifting further.
    struct VariableLabelEntry {
        ttstr name;
        ttstr label;
        bool flag68 = true;
        bool flag124 = true;
    };

    struct PlayerRuntime {
        std::unordered_map<std::string, std::shared_ptr<MotionSnapshot>> motionsByKey;
        // Aligned to libkrkr2.so player+656: SourceCache object variant.
        motion::SourceCache *sourceCacheNative = nullptr;
        tTJSVariant sourceCacheObject;
        std::shared_ptr<MotionSnapshot> activeMotion;
        std::unordered_map<std::string, TimelineState> timelines;
        std::vector<std::string> playingTimelineLabels;
        std::unordered_map<std::string, tjs_int> layerIdsByName;
        std::unordered_map<tjs_int, std::string> layerNamesById;
        tjs_int nextLayerAbsolute = 1;
        struct LayerRenderState {
            tjs_int layerId = 0;
            bool clipEnabled = true;
            bool initialized = false;
            bool isDirty = false;
            tjs_int absolute = 0;
            tjs_int hitThreshold = 256;
            tTJSVariant layerObject;
            tTJSVariant layerGetter;
            std::array<float, 4> clipRect{0.f, 0.f, 0.f, 0.f};
            std::array<float, 4> worldRect{0.f, 0.f, 0.f, 0.f};
            std::array<float, 4> localRect{0.f, 0.f, 0.f, 0.f};
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
        };
        std::unordered_map<tjs_int, LayerRenderState> renderLayerStates;
        std::vector<tTJSVariant> backgrounds;
        std::vector<tTJSVariant> captions;
        std::unordered_map<std::string, bool> disabledSelectorTargets;
        tTJSVariant lastCanvas;
        tTJSVariant lastViewParam;
        // Aligned to libkrkr2.so player+696: internal render layer consumed by
        // sub_6CE7D8 / sub_6CE938 style post-draw update.
        tTJSVariant internalRenderLayer;
        // Reusable work layer for sub_6C4E28-style per-item local clipping.
        tTJSVariant scratchWorkLayer;
        std::array<double, 6> drawAffineMatrix{ 1.0, 0.0, 0.0,
                                                1.0, 0.0, 0.0 };
        tjs_int nextLayerId = 1;
        tjs_int clearColor = 0;
        tjs_int width = 0;
        tjs_int height = 0;
        int alphaOpCounter = 0;
        bool resizable = false;
        bool flip = false;
        bool visible = true;
        double opacity = 1.0;
        double slant = 0.0;
        double zoom = 1.0;
        std::vector<MotionEvent> pendingEvents;
        std::vector<MotionParameterEntry> parameterEntries;
        std::unordered_map<std::string, size_t> parameterEntryById;
        MotionParameterEntry defaultParameterEntry;
        MotionParameterEntry *defaultParameterEntryPtr = nullptr;
        int defaultParameterEntryIndex = -1;
        const MotionClip *activeClip = nullptr;
        // Persistent node tree for updateLayers pipeline. Aligned to
        // libkrkr2.so Player+200 (std::deque of MotionNode). The constructor
        // creates index 0 as the root node; loaded layer trees append real
        // nodes at indices [1,end) during Player_buildNodeTree (0x6B51F0).
        std::deque<MotionNode> nodes;
        // Aligned to libkrkr2.so Player+1296 std::vector<LabelEntry>.
        // Populated eagerly by Player_initVariables (0x6CD750) right after
        // buildNodeTree on the play / setMotion path.
        std::vector<VariableLabelEntry> variableLabelEntries;
        // Node label → index map. Aligned to binary's std::map<ttstr,int> at
        // player+24. Populated during recursive build with last-write-wins
        // assignment, queried by sub_6F2228 equivalent.
        std::map<std::string, int> nodeLabelMap;

        // Native render-item fields from the anonymous 0x1B0 item built by
        // libkrkr2.so 0x6C2334 and consumed in-place by 0x6C4E28 / 0x6C7440.
        // These fields intentionally keep the native write lifecycle: +21 and
        // +216..228 are not blanket-cleared every frame.
        struct NativeRenderItemFields {
            bool rawFlag16 = false; // original item +16 = node+201
            bool skipFlag0 = false; // original render item +17 (0x6C2334 / 0x6C7440)
            bool skipFlag1 = false; // original render item +18 (0x6C2334 / 0x6C7440)
            bool drawFlag = false;  // original render item +19
            bool rawFlag20 = false; // original item +20, set by sub_6C4E28 requireLayerId path
            bool rawFlag21 = false; // original item +21, drawable clip valid after sub_6C4E28
            std::uint8_t stencilMaskRef = 0; // original item +22
            std::uint8_t stencilWriteRef = 0; // original item +23
            std::array<float, 4> paintBox{0.f, 0.f, 0.f, 0.f}; // item+184..196
            std::array<float, 4> viewport{1.f, 1.f, -1.f, -1.f}; // item+200..212
            std::array<int, 4> clipRect{0, 0, 0, 0}; // item+216..228
            std::array<int, 4> dirtyRect{0, 0, 0, 0};
            int opacity = 255; // item+232
            // item+244 in libkrkr2.so sub_6C2334 @ 0x6C2A90 — stencil/composite
            // flags copied from node.stencilType; consumed by sub_6C7440 alpha
            // mask path `(item+244 & 4)` / `(item+244 & 3)==1`.
            int stencilComposite = 0;
        };

        struct RenderItemNativeFieldLifetime {
            bool rawFlag20 = false;
            bool rawFlag21 = false;
            std::array<int, 4> clipRect{0, 0, 0, 0};
            std::array<int, 4> dirtyRect{0, 0, 0, 0};
            std::array<float, 8> localCorners{};
            std::vector<float> localMeshPoints;
        };

        struct PreparedRenderItem : NativeRenderItemFields {
            int nodeIndex = 0;
            tTJSVariant srcRef;
            std::string sourceKey;
            bool hasOwnSource = false;
            bool groupOnly = false;
            bool topLevelList = true;
            bool groupList = false;
            bool selfSeedChildList = false;
            PlayerRuntime *nativeLifetimeOwner = nullptr;
            int nativeLifetimeKey = 0;
            double sortKey = 0.0;
            int blendMode = 16;
            tTJSVariant contextVariant; // original item +248 (player+1012 copy)
            std::array<float, 8> corners{};
            std::array<float, 8> localCorners{};
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            bool hasViewport = false;
            int coordinateMode = 0;
            int objTriPriority = 0;
            int visibleAncestorIndex = -1;
            int meshDivX = 0;
            int meshDivY = 0;
            int meshType = 0;
            std::vector<float> meshPoints;
            std::vector<float> localMeshPoints;
            int layerId = 0;
            int layerId2 = 0;
            PreparedRenderItem *parentItem = nullptr; // semantic mapping of item +264
            std::vector<PreparedRenderItem *> childItems; // semantic mapping of item +24
            tTJSVariant leafLayer;      // item+304 variant
            tTJSVariant composedLayer;  // item+324 variant
            std::array<int, 4> builtRect{0, 0, 0, 0};
            bool leafBuilt = false;
            bool composedBuilt = false;
            bool executedDirect = false;
        };
        std::vector<PreparedRenderItem> preparedRenderItems;  // player+936/944
        // Native-shaped a2/a3 split passed through sub_6C2334 -> sub_6C4E28
        // -> sub_6C7440. Both lists point directly into preparedRenderItems.
        std::vector<PreparedRenderItem *> preparedRenderItemsTopLevel;
        std::vector<PreparedRenderItem *> preparedRenderItemsGroup;
        std::unordered_map<int, RenderItemNativeFieldLifetime>
            renderItemNativeFieldLifetimeByNode;

        // Legacy local scratch for old diagnostics. libkrkr2.so player+384 is
        // the parameter table initialized by Player_initNonEmoteMotion
        // (0x6B365C), not per-node storage; node+8 resolves into
        // parameterEntries via MotionNode::parameterizeIndex.
        struct PerNodeEvalData {
            double padding[5] = {};   // offsets 0-39 (unused in our current scope)
            double evalTime = 0.0;
            int dirtyFlag = 0;
        };
        std::vector<PerNodeEvalData> perNodeEvalData;
        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // PSB root "type" field: 0=non-emote (motion), 1=emote
        bool isEmoteMode = false;
    };

    void ensureRootNodeLike_0x6CED30(PlayerRuntime &runtime);
    void resetNodeTreeKeepRootLike_0x6B56F8(PlayerRuntime &runtime);
    std::shared_ptr<PlayerRuntime> makePlayerRuntime();

    std::string narrow(const ttstr &value);
    ttstr widen(const std::string &value);

    std::vector<ttstr> buildMotionLookupCandidates(const ttstr &name);
    bool resolveExistingPath(const std::vector<ttstr> &candidates, ttstr &resolved);
    void appendEmbeddedSourceCandidates(const MotionSnapshot &snapshot,
                                        const std::string &source,
                                        std::vector<ttstr> &candidates);

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(const ttstr &path,
                                                       tjs_int decryptSeed);
    tTJSVariant loadPSBVariant(const ttstr &path, tjs_int decryptSeed);

    void registerModuleSnapshot(const tTJSVariant &module,
                                const std::shared_ptr<MotionSnapshot> &snapshot);
    std::shared_ptr<MotionSnapshot> lookupModuleSnapshot(const tTJSVariant &module);

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items);
    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries);
    std::vector<tTJSVariant> stringsToVariants(
        const std::vector<std::string> &values);

    void primeTimelineStates(std::unordered_map<std::string, TimelineState> &states,
                             const MotionSnapshot &snapshot);
    void stepTimelines(std::unordered_map<std::string, TimelineState> &states,
                       double dt,
                       std::vector<MotionEvent> *events = nullptr);

    bool logoChainTraceEnabled();
    bool logoChainTraceEnabledForPath(const std::string &motionPath);
    bool logoChainTraceEnabled(const std::shared_ptr<MotionSnapshot> &snapshot);
    bool logoSnapshotMarkEnabled();
    bool logoSnapshotMarkEnabledForPath(const std::string &motionPath);
    void resetLogoChainTraceSession(const std::string &motionPath);
    void logoChainTraceLog(const std::string &motionPath,
                           const char *stage,
                           const char *func,
                           double frameTime,
                           const std::string &message);
    void logoChainTraceCheck(const std::string &motionPath,
                             const char *stage,
                             const char *func,
                             double frameTime,
                             const std::string &expected,
                             const std::string &actual,
                             bool ok,
                             const std::string &likelyRootCause = {});
    void logoChainTraceSummary(const std::string &motionPath,
                               const char *func,
                               double frameTime,
                               const std::string &note = {});

    template <typename... Args>
    inline void logoChainTraceLogf(const std::string &motionPath,
                                   const char *stage,
                                   const char *func,
                                   double frameTime,
                                   fmt::format_string<Args...> format,
                                   Args &&...args) {
        if(!logoChainTraceEnabledForPath(motionPath)) {
            return;
        }
        logoChainTraceLog(motionPath, stage, func, frameTime,
                          fmt::format(format, std::forward<Args>(args)...));
    }

    // Scan PSB layer tree for action/sync events between prevTime and newTime.
    // Aligned to libkrkr2.so: updateLayers queues events during tree evaluation.
    void scanLayerActions(const MotionSnapshot &snapshot,
                          double prevTime, double newTime,
                          std::vector<MotionEvent> &events);

} // namespace motion::detail
