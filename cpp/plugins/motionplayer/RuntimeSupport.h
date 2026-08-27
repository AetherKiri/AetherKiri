//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>

#include "tjs.h"
#include "psbfile/PSBFile.h"
#include "MotionNode.h"

class tTVPBaseBitmap;

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

    // Motion-local variables drive the frame clock of layers marked with a
    // matching `parameterize` index. UI motions use these for states such as
    // select, disable, invisible, empty and notopen.
    struct MotionParameterInfo {
        std::string id;
        bool discretization = false;
        double rangeBegin = 0.0;
        double rangeEnd = 0.0;
        double division = 0.0;
    };

    struct MotionClip {
        std::string label;
        std::string owner;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop restart point
        double totalFrames = 0.0;
        double selfSyncTime = 0.0;
        double syncTime = 0.0;
        std::vector<std::string> layerNames;
        // PSB motion clips may contain several top-level layers with the same
        // label (for example one `alpha` group per title character). Keep the
        // authored list as the rendering source of truth; layersByName remains
        // the name-based lookup surface for script/query compatibility.
        std::vector<std::shared_ptr<const PSB::PSBDictionary>> orderedLayers;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBDictionary>>
            layersByName;
        std::vector<MotionParameterInfo> parameters;
        int defaultParameterIndex = -1;
        std::vector<std::string> sourceCandidates;
    };

    inline double parameterizedClipTime(const MotionClip &clip,
                                        const MotionParameterInfo &parameter,
                                        double value) {
        const double range = parameter.rangeEnd - parameter.rangeBegin;
        if(std::abs(range) <= 0.0000001) {
            return 0.0;
        }

        const double minimum =
            std::min(parameter.rangeBegin, parameter.rangeEnd);
        const double maximum =
            std::max(parameter.rangeBegin, parameter.rangeEnd);
        double normalized =
            (std::clamp(value, minimum, maximum) - parameter.rangeBegin) /
            range;

        // Discrete parameters select authored input values, while `division`
        // describes the motion timeline's subdivisions.  Those counts can be
        // different: eyechatch's `graffiti` selector has values 0..4 over a
        // 49-division timeline. Quantizing the input to 1/49 shifted values 1,
        // 3, and 4 off their exact selector frames and left every associated
        // child motion inactive. Integer selector ranges therefore quantize
        // by their own value count. Retain division as a fallback for the
        // uncommon case of a non-integral authored range.
        if(parameter.discretization) {
            const double rangeMagnitude = std::abs(range);
            const double integerSteps = std::round(rangeMagnitude);
            const double selectorSteps =
                integerSteps >= 1.0 &&
                        std::abs(rangeMagnitude - integerSteps) <= 0.0000001
                    ? integerSteps
                    : parameter.division;
            if(selectorSteps > 0.0) {
                normalized = std::round(normalized * selectorSteps) /
                             selectorSteps;
            }
        }
        normalized = std::clamp(normalized, 0.0, 1.0);

        // Parameterized motion samples span the authored frame axis. A
        // clip's selfSyncTime is only a playback/synchronization marker and
        // can be earlier than its last parameter frame. E-mote eye clips are
        // the concrete counterexample: both eyelid and iris use a 61-frame
        // [-10, 50] parameter axis, while only the eyelid declares
        // selfSyncTime=50. Using that marker maps face_eye_open=10 to frame
        // 16.67 for the lid but frame 20 for the iris, leaving the iris exposed
        // during a blink. Native parameter evaluation uses the complete axis.
        const double timelineEnd = clip.totalFrames > 0.0
            ? std::max(0.0, clip.totalFrames - 1.0)
            : std::max(0.0, parameter.division);
        return normalized * timelineEnd;
    }

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
        std::shared_ptr<const std::vector<std::uint8_t>> objectImage;
        std::shared_ptr<const PSB::PSBDictionary> root;
        // A Kiri ResourceManager can build one character from several PSBs
        // (for example head, costume/body, and timeline modules).  The
        // public evaluator keeps the selected timeline snapshot as the
        // primary motion, while a native backend needs the complete ordered
        // object set that was loaded into that ResourceManager.
        std::vector<std::shared_ptr<MotionSnapshot>> nativeObjectSnapshots;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBResource>>
            resourcesByPath;
        // Immutable PSB resources are shared by every Player bound to this
        // snapshot. Fingerprinting a multi-megabyte embedded image once per
        // Player made animated title scenes repeatedly rescan the same bytes.
        mutable std::mutex sourceFingerprintMutex;
        mutable std::unordered_map<
            const PSB::PSBResource *,
            std::pair<std::uint64_t, std::uint64_t>> sourceFingerprints;
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
        std::vector<std::string> layerNames;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBDictionary>> layersByName;
        std::vector<std::string> sourceCandidates;
        std::unordered_map<std::string, MotionClip> clipsByLabel;
        std::unordered_map<std::string,
            std::unordered_map<std::string, MotionClip>> clipsByOwnerAndLabel;
        std::unordered_map<std::string, TimelineControlBinding>
            timelineControlByLabel;
        // Opaque metadata owned by an optional motionplayer extension. The
        // public runtime never interprets vendor-specific controller payloads.
        std::shared_ptr<const void> extensionMetadata;
        std::vector<std::string> resourceAliases;
        double width = 0.0;
        double height = 0.0;
    };

    struct PlayerRuntime {
        std::unordered_map<std::string, std::shared_ptr<MotionSnapshot>> motionsByKey;
        std::unordered_map<std::string, tTJSVariant> sourcesByKey;
        // A PSB-backed E-mote layer first asks the legacy source loader for an
        // external bitmap, then falls back to the embedded PSB resource.  A
        // missing external source is a stable result for the lifetime of the
        // active motion; remember it so every animated layer does not repeat
        // storage-path resolution and directory scans on every frame.
        std::unordered_set<std::string> sourceLookupMisses;
        std::shared_ptr<MotionSnapshot> activeMotion;
        std::unordered_map<std::string, TimelineState> timelines;
        std::vector<std::string> playingTimelineLabels;
        std::string lastExplicitTimelineLabel;
        std::unordered_map<std::string, tjs_int> layerIdsByName;
        std::unordered_map<tjs_int, std::string> layerNamesById;
        std::vector<tTJSVariant> backgrounds;
        std::vector<tTJSVariant> captions;
        std::unordered_map<std::string, bool> disabledSelectorTargets;
        tTJSVariant lastCanvas;
        tTJSVariant lastViewParam;
        // Aligned to libkrkr2.so player+696: internal render layer consumed by
        // sub_6CE7D8 / sub_6CE938 style post-draw update.
        tTJSVariant internalRenderLayer;
        // D3DEmote exposes the completed full-canvas texture to a visible
        // KiriKiri layer by reference. Repainting that same texture on the
        // next tick forces a full-size copy-on-write clone. Keep two private
        // render surfaces per Player so one can remain visible while the
        // other is cleared and rebuilt.
        std::array<tTJSVariant, 2> d3dRenderLayers;
        std::size_t nextD3DRenderLayer = 0;
        std::size_t lastD3DRenderLayer = 0;
        std::uint64_t lastD3DRasterPublishUs = 0;
        // Stable visible endpoint for D3DAffineSourceMotion scripts that
        // present Player.draw() directly instead of calling captureCanvas().
        // AssignMotionImages swaps completed scratch textures into this layer
        // without copying or sharing the texture repainted on the next tick.
        tTJSVariant d3dPresentationLayer;
        // Visible presentation surface used by Yuzu/KAG no-separate title
        // motions when the scripted target has full-screen transition children.
        tTJSVariant presentationRenderLayer;
        // Reusable work layer for sub_6C4E28-style per-item local clipping.
        tTJSVariant scratchWorkLayer;
        // Stable ownerless framebuffer used by the private E-mote bridge's
        // RGBA export path. Creating and releasing a full-size Godot/Metal
        // texture on every frame leaves thousands of resources pending on
        // the render thread and can grow the process footprint by gigabytes
        // per minute. Keep one surface per Player and resize it only when the
        // requested export dimensions grow.
        tTJSVariant headlessRgbaRenderLayer;
        // Cropped Artemis E-mote exports use a separate surface. The first
        // frame establishes alpha bounds at full-stage size; keeping that
        // large backing texture would make every later cropped readback pay
        // for the original 1920x1080 allocation.
        tTJSVariant headlessRgbaRegionRenderLayer;
        tTJSVariant headlessRgbaRegionRenderLayer2;
        // The native backend imports its IOSurface/AHardwareBuffer as a Godot
        // texture. Keep the producer lifetime through the next composition so
        // queued KiriKiri draws never sample a recycled shared image.
        std::shared_ptr<void> nativeBackendGpuFrameLifetime;
        std::uint64_t nativeBackendGpuFrameCount = 0;
        // A provider can submit several independent E-mote surfaces before
        // synchronizing their GPU readbacks, matching Artemis' framebuffer
        // display pass instead of serially stalling after every player.
        bool headlessRgbaRenderPending = false;
        bool headlessRgbaPendingFullStage = false;
        int headlessRgbaPendingSlot = 0;
        int headlessRgbaPendingWidth = 0;
        int headlessRgbaPendingHeight = 0;
        std::array<uint64_t, 2> headlessRgbaReadbackRequests{};
        std::array<uint64_t, 2> headlessRgbaReadbackSequences{};
        std::array<bool, 2> headlessRgbaReadbackFullStage{};
        std::array<int, 2> headlessRgbaReadbackSlots{};
        std::array<int, 2> headlessRgbaReadbackWidths{};
        std::array<int, 2> headlessRgbaReadbackHeights{};
        uint64_t headlessRgbaNextReadbackSequence = 1;
        // Source bitmaps decoded for the active motion. Building these from PSB
        // resources is expensive enough to be visible during title animations.
        std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>>
            motionSourceBitmapCache;
        // A decoded atlas icon may retain a small filtering gutter around its
        // logical image.  Keep the sampling rectangle beside the cached
        // bitmap so prepared/tinted variants use the same coordinates.
        std::unordered_map<std::string, std::array<int, 4>>
            motionSourceBitmapRects;
        struct MotionSourceBitmapTraits {
            bool alphaOnlyKnown = false;
            bool alphaOnly = false;
            bool whiteMaskKnown = false;
            bool whiteMask = false;
            bool hasWhitePixelsKnown = false;
            bool hasWhitePixels = false;
        };
        // Source classification samples immutable pixels. Cache the result so
        // color animation does not resample the same bitmap for every tint.
        std::unordered_map<std::string, MotionSourceBitmapTraits>
            motionSourceBitmapTraits;
        struct MotionSourceMetadata {
            int width = 0;
            int height = 0;
            double originX = 0.0;
            double originY = 0.0;
        };
        // Width/height/origin are immutable PSB icon metadata. Layer
        // evaluation needs them every frame, but must not walk the PSB tree
        // (or decode the icon pixels) for every animated node every frame.
        std::unordered_map<std::string, MotionSourceMetadata>
            motionSourceMetadataCache;
        std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>>
            motionPreparedBitmapCache;
        // Only materialized (copied/tinted) prepared bitmaps are tracked here.
        // A motion can animate packed corner colors every frame, producing a
        // distinct cache key and a full-size bitmap for each value. Keep those
        // variants bounded per source while leaving cheap aliases to the base
        // source cache untouched.
        std::unordered_map<std::string, std::deque<std::string>>
            motionPreparedMaterializedKeysBySource;
        struct PresentationRenderCacheEntry {
            std::string motion;
            double frame = 0.0;
            tjs_int canvasWidth = 0;
            tjs_int canvasHeight = 0;
            std::size_t commandSignature = 0;
        };
        std::unordered_map<iTJSDispatch2 *, PresentationRenderCacheEntry>
            presentationRenderCache;
        int presentationRenderReuseSkips = 0;
        // E-mote can draw several characters through shared work layers. A
        // target-only cache cannot survive the next character overwriting
        // that layer, so retain each player's completed frame separately.
        // The bitmap keeps Godot-native GPU contents resident when available.
        struct EmoteRenderFrameCacheEntry {
            std::shared_ptr<tTVPBaseBitmap> bitmap;
            std::string motion;
            double frame = 0.0;
            tjs_int canvasWidth = 0;
            tjs_int canvasHeight = 0;
            std::size_t commandSignature = 0;
            std::uint64_t storedUs = 0;
        };
        EmoteRenderFrameCacheEntry emoteRenderFrameCache;
        int emoteRenderFrameReuseSkips = 0;
        // The D3D adaptor route clears to transparent and hands a private
        // scratch texture to captureCanvas. Keep it separate from the direct
        // layer route, whose authored target can have a different neutral
        // color and presentation lifetime.
        EmoteRenderFrameCacheEntry d3dEmoteRenderFrameCache;
        int d3dEmoteRenderFrameReuseSkips = 0;
        // E-mote rebuilds the render-command vector on every tick, but most
        // transformed leaves and composite subtrees are unchanged between
        // ticks. Keep their GPU-backed work layers alive across that rebuild
        // so animated parts can update without repainting all static parts.
        struct EmoteCommandOutputCacheEntry {
            std::size_t leafSignature = 0;
            std::size_t outputSignature = 0;
            std::size_t maskSignature = 0;
            tTJSVariant leafLayer;
            tTJSVariant composedLayer;
            tTJSVariant maskLayer;
            tTJSVariant unionMaskLayer;
            bool leafValid = false;
            bool outputValid = false;
            bool maskValid = false;
            bool leafBuilt = false;
            bool composedBuilt = false;
            std::uint64_t lastUseGeneration = 0;
        };
        std::unordered_map<std::string, EmoteCommandOutputCacheEntry>
            emoteCommandOutputCache;
        std::uint64_t emoteCommandOutputCacheGeneration = 0;
        std::uint64_t emoteCommandOutputCacheHits = 0;
        std::uint64_t emoteCommandLeafCacheHits = 0;
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
        // Persistent node tree for updateLayers pipeline
        std::vector<MotionNode> nodes;
        bool nodesBuilt = false;
        bool yuzuTitleFinalFrameRendered = false;
        bool yuzuPresentationCenteredOriginConfirmed = false;
        float yuzuPresentationTranslateX = 0.0f;
        float yuzuPresentationTranslateY = 0.0f;
        // Node label → index map. Aligned to binary's std::map<ttstr,int> at player+24.
        // Populated after buildNodeTree, queried by sub_6F2228 equivalent.
        std::map<std::string, int> nodeLabelMap;

        struct RenderAncestorReference {
            std::uintptr_t renderScopeId = 0;
            int scopedNodeIndex = -1;
        };

        struct PreparedRenderItem {
            int nodeIndex = 0;
            // Flattened child players remap nodeIndex into one numeric
            // namespace. Keep the original runtime-local identity as well so
            // cross-runtime parents can still be resolved after a containing
            // player is flattened again.
            std::uintptr_t renderScopeId = 0;
            int scopedNodeIndex = -1;
            std::uintptr_t parentRenderScopeId = 0;
            int scopedParentNodeIndex = -1;
            // A nested motion can be flattened through more than one Player.
            // Each reference is the next enclosing composite candidate after
            // the preceding runtime-local ancestor chain reaches its root.
            std::vector<RenderAncestorReference> outerRenderAncestorChain;
            std::string nodeLabel;
            tTJSVariant srcRef;
            std::string sourceKey;
            // Flattened motion children keep source keys such as
            // `src/tex/0001`, but those keys are local to the PSB that owns
            // the child. Retain that snapshot so the parent renderer does not
            // incorrectly resolve every child key against the timeline PSB.
            std::shared_ptr<MotionSnapshot> sourceMotion;
            bool hasOwnSource = false;
            bool groupOnly = false;
            // Legacy op-5 stencil containers can omit an explicit mask list.
            // Their first authored child branch is both visible artwork and
            // the alpha base for the remaining branches.
            bool implicitVisibleStencilGroup = false;
            bool implicitVisibleStencilBase = false;
            int implicitVisibleStencilGroupNodeIndex = -1;
            bool skipFlag0 = false;
            bool skipFlag1 = false;
            bool clipFlag = false;
            bool drawFlag = false;
            double sortKey = 0.0;
            int blendMode = 16;
            std::array<float, 8> corners{};
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            std::array<float, 4> paintBox{0.f, 0.f, 0.f, 0.f};
            std::array<float, 4> viewport{1.f, 1.f, -1.f, -1.f};
            bool hasViewport = false;
            // A flattened child may inherit a viewport from an enclosing
            // off-screen composite. That rectangle is already in the
            // containing player's world coordinates and must not be scaled
            // or translated with the child's authored geometry.
            bool viewportInheritedFromComposite = false;
            int opacity = 255;
            int updateCount = 0;
            int visibleAncestorIndex = -1;
            bool stencilMaskReferenced = false;
            std::vector<int> stencilMaskNodeIndices;
            int meshDivX = 0;
            int meshDivY = 0;
            int meshType = 0;
            std::vector<float> meshPoints;
            int layerId = 0;
        };
        struct RenderCommand {
            int nodeIndex = 0;
            std::uintptr_t renderScopeId = 0;
            int scopedNodeIndex = -1;
            std::uintptr_t parentRenderScopeId = 0;
            int scopedParentNodeIndex = -1;
            std::vector<RenderAncestorReference> outerRenderAncestorChain;
            std::string nodeLabel;
            tTJSVariant srcRef;
            std::string sourceKey;
            std::shared_ptr<MotionSnapshot> sourceMotion;
            bool hasOwnSource = false;
            bool groupOnly = false;
            bool implicitVisibleStencilGroup = false;
            bool implicitVisibleStencilBase = false;
            int implicitVisibleStencilGroupNodeIndex = -1;
            // Referenced stencil inputs must be materialized into an
            // off-screen layer even when they would otherwise qualify for the
            // direct-to-target fast path.  Composite groups read that layer's
            // source alpha later in the same render walk.
            bool stencilMaskReferenced = false;
            int blendMode = 16;
            int opacity = 255;
            int itemFlags = 0;
            int parentNodeIndex = -1;
            bool hasRenderParent = false;
            // A standalone flags-6 carrier without a concrete item+264
            // target contributes only alpha topology. Its descendants remain
            // ordinary render items and the carrier itself is never copied
            // to the colour target.
            bool alphaMaskOnly = false;
            // Parentless flags-6 carriers retain hidden mode-6 source layers
            // as alpha inputs. Ordinary drawable descendants reference the
            // carrier through differenceAlphaMaskGroupCommandIndices and are
            // cropped before reaching the presentation target.
            std::vector<int> differenceAlphaMaskSourceCommandIndices;
            std::vector<int> differenceAlphaMaskGroupCommandIndices;
            std::vector<int> differenceAlphaMaskInputCommandIndices;
            // An authored same-name item+304 pair uses the flags-6
            // difference operation. A base colour leaf without such a pair
            // consumes the carrier's combined group+324 alpha as an ordinary
            // crop instead.
            int differenceAlphaMaskOperation = 0;
            // A parent viewport can crop a drawable inside the final canvas.
            // Such items need the clip-sized scratch layer; the final target
            // only provides canvas-edge clipping.
            bool requiresLocalClip = false;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            int visibleAncestorIndex = -1;
            bool clearEnabled = false;
            std::array<int, 4> clipRect{0, 0, 0, 0};
            std::array<int, 4> dirtyRect{0, 0, 0, 0};
            std::array<float, 8> worldCorners{};
            std::array<float, 8> localCorners{};
            std::vector<float> worldMeshPoints;
            std::vector<float> localMeshPoints;
            int meshDivX = 0;
            int meshDivY = 0;
            int meshType = 0;
            int layerId = 0;
            std::vector<int> childCommandIndices;
            // A type-12 flags-6 group can modify an authored parent surface.
            // It is recorded here only when that parent has a concrete render
            // command. If the parent is merely a source-less transform, the
            // group remains an independent composite output; mode-6 child
            // bitmaps inside it are alpha-chain carriers rather than visible
            // RGB artwork.
            std::vector<int> stencilModifierCommandIndices;
            std::vector<int> stencilMaskNodeIndices;
            std::vector<int> stencilMaskCommandIndices;
            tTJSVariant leafLayer;
            tTJSVariant composedLayer;
            tTJSVariant maskLayer;
            tTJSVariant unionMaskLayer;
            std::array<int, 4> builtRect{0, 0, 0, 0};
            bool leafBuilt = false;
            bool composedBuilt = false;
            bool executedDirect = false;
        };
        std::vector<PreparedRenderItem> preparedRenderItems;  // player+936/944
        // Parent motion players propagate a large shared variable table to
        // every nested E-mote part. Remember the last inherited values so an
        // unchanged input does not dirty the entire child model again.
        std::unordered_map<std::string, double> inheritedVariableInputs;
        // Layer evaluation overlays persistent, evaluated and inherited
        // variables on every tick. Retain the union's nodes between ticks so
        // stable E-mote variable tables update values in place instead of
        // allocating and destroying two temporary unordered_maps per frame.
        // The generation excludes labels which disappeared this tick without
        // requiring the scratch map itself to be cleared.
        struct EffectiveVariableScratchEntry {
            double value = 0.0;
            std::uint64_t generation = 0;
            bool hasRoutingNode = false;
        };
        std::unordered_map<std::string, EffectiveVariableScratchEntry>
            effectiveVariableScratch;
        std::uint64_t effectiveVariableScratchGeneration = 0;

        std::uint64_t beginEffectiveVariableScratch() {
            ++effectiveVariableScratchGeneration;
            if(effectiveVariableScratchGeneration == 0) {
                effectiveVariableScratch.clear();
                effectiveVariableScratchGeneration = 1;
            }
            return effectiveVariableScratchGeneration;
        }

        void setEffectiveVariableScratch(
            const std::string &label, double value) {
            auto [it, inserted] = effectiveVariableScratch.try_emplace(label);
            (void)inserted;
            it->second.value = value;
            it->second.generation = effectiveVariableScratchGeneration;
            it->second.hasRoutingNode = false;
        }
        // Nested motion players can keep their flattened render description
        // until either their evaluated layer state or inherited draw affine
        // changes. Top-level players still rebuild every animated tick.
        std::uint64_t layerStateGeneration = 0;
        std::uint64_t preparedLayerStateGeneration = 0;
        std::array<double, 6> preparedDrawAffineMatrix{
            1.0, 0.0, 0.0, 1.0, 0.0, 0.0
        };
        bool preparedRenderItemsValid = false;
        std::vector<RenderCommand> renderCommands;

        // Per-node evaluation time array.
        // Aligned to libkrkr2.so player+384: 56-byte-per-node entries.
        // Each node's node+8 pointer points into this array.
        // Offset 40 within each entry stores the per-node eval time.
        // Offset 48 stores the per-node dirty flag (cleared in post-loop).
        struct PerNodeEvalData {
            double padding[5] = {};   // offsets 0-39 (unused in our current scope)
            double evalTime = 0.0;    // offset 40: per-node evaluation time
            int dirtyFlag = 0;        // offset 48: cleared in post-loop
        };
        std::vector<PerNodeEvalData> perNodeEvalData;  // player+384/392
        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // PSB root "type" field: 0=non-emote (motion), 1=emote
        bool isEmoteMode = false;

        void clearPresentationRenderReuse() {
            presentationRenderCache.clear();
            presentationRenderReuseSkips = 0;
        }

        void invalidatePresentationRenderTarget(iTJSDispatch2 *target) {
            if(target) {
                presentationRenderCache.erase(target);
            } else {
                clearPresentationRenderReuse();
            }
        }

        void clearMotionBitmapCaches() {
            motionSourceBitmapCache.clear();
            motionSourceBitmapRects.clear();
            motionSourceBitmapTraits.clear();
            motionSourceMetadataCache.clear();
            motionPreparedBitmapCache.clear();
            motionPreparedMaterializedKeysBySource.clear();
            emoteRenderFrameCache = {};
            emoteRenderFrameReuseSkips = 0;
            d3dEmoteRenderFrameCache = {};
            d3dEmoteRenderFrameReuseSkips = 0;
            emoteCommandOutputCache.clear();
            emoteCommandOutputCacheGeneration = 0;
            emoteCommandOutputCacheHits = 0;
            emoteCommandLeafCacheHits = 0;
            nextD3DRenderLayer = 0;
            lastD3DRenderLayer = 0;
            lastD3DRasterPublishUs = 0;
            nativeBackendGpuFrameLifetime.reset();
            nativeBackendGpuFrameCount = 0;
            inheritedVariableInputs.clear();
            effectiveVariableScratch.clear();
            effectiveVariableScratchGeneration = 0;
            preparedRenderItems.clear();
            preparedRenderItemsValid = false;
            clearPresentationRenderReuse();
        }
    };

    inline std::size_t nativeLayerBufferNodeIndex(
        std::size_t nodeCount,
        std::size_t bufferPosition) {
        (void)nodeCount;
        return bufferPosition;
    }

    inline bool preparedLocalNodeFollowsChildSlot(
        int localNodeIndex,
        int childParentNodeIndex) {
        return localNodeIndex > childParentNodeIndex;
    }

    inline bool preparedChildParentSlotLess(
        int lhsParentNodeIndex,
        int rhsParentNodeIndex) {
        return lhsParentNodeIndex < rhsParentNodeIndex;
    }

    inline bool tessellatePreparedItemForExternalMesh(
        PlayerRuntime::PreparedRenderItem &entry,
        double meshDivisionRatio,
        int meshDivision) {
        if(!entry.meshPoints.empty() || entry.meshType != 0) {
            return false;
        }

        float minX = entry.corners[0];
        float maxX = entry.corners[0];
        float minY = entry.corners[1];
        float maxY = entry.corners[1];
        for(std::size_t point = 2;
            point + 1 < entry.corners.size(); point += 2) {
            minX = std::min(minX, entry.corners[point]);
            maxX = std::max(maxX, entry.corners[point]);
            minY = std::min(minY, entry.corners[point + 1]);
            maxY = std::max(maxY, entry.corners[point + 1]);
        }
        if(!std::isfinite(minX) || !std::isfinite(maxX) ||
           !std::isfinite(minY) || !std::isfinite(maxY) ||
           maxX - minX <= 1e-5f || maxY - minY <= 1e-5f) {
            return false;
        }

        // libartemis MMotionPlayer::StepFrameMeshChain first expands an
        // affine child surface into a stable grid whenever a parent Bezier
        // patch exists. It then transforms every grid vertex through that
        // patch. Keeping this topology stable even while the patch happens to
        // be affine avoids a one-frame topology change at blink boundaries.
        const int divisionTotal = std::clamp(static_cast<int>(
            meshDivisionRatio * static_cast<double>(std::max(1, meshDivision))),
            2, 50);
        const double width = 0.5 * (
            std::hypot(entry.corners[2] - entry.corners[0],
                       entry.corners[3] - entry.corners[1]) +
            std::hypot(entry.corners[4] - entry.corners[6],
                       entry.corners[5] - entry.corners[7]));
        const double height = 0.5 * (
            std::hypot(entry.corners[6] - entry.corners[0],
                       entry.corners[7] - entry.corners[1]) +
            std::hypot(entry.corners[4] - entry.corners[2],
                       entry.corners[5] - entry.corners[3]));
        const double extent = width + height;
        int xSegments = extent > 0.0001
            ? static_cast<int>(
                  static_cast<double>(divisionTotal) * width / extent)
            : divisionTotal / 2;
        xSegments = std::clamp(xSegments, 1, divisionTotal - 1);
        const int ySegments = divisionTotal - xSegments;

        entry.meshDivX = xSegments + 1;
        entry.meshDivY = ySegments + 1;
        entry.meshType = 2;
        entry.meshPoints.resize(static_cast<std::size_t>(
            entry.meshDivX * entry.meshDivY * 2));
        for(int y = 0; y < entry.meshDivY; ++y) {
            const float v = static_cast<float>(y) /
                static_cast<float>(entry.meshDivY - 1);
            for(int x = 0; x < entry.meshDivX; ++x) {
                const float u = static_cast<float>(x) /
                    static_cast<float>(entry.meshDivX - 1);
                const float topX =
                    entry.corners[0] * (1.f - u) + entry.corners[2] * u;
                const float topY =
                    entry.corners[1] * (1.f - u) + entry.corners[3] * u;
                const float bottomX =
                    entry.corners[6] * (1.f - u) + entry.corners[4] * u;
                const float bottomY =
                    entry.corners[7] * (1.f - u) + entry.corners[5] * u;
                const std::size_t offset = static_cast<std::size_t>(
                    (y * entry.meshDivX + x) * 2);
                entry.meshPoints[offset] =
                    topX * (1.f - v) + bottomX * v;
                entry.meshPoints[offset + 1] =
                    topY * (1.f - v) + bottomY * v;
            }
        }
        return true;
    }

    std::shared_ptr<PlayerRuntime> makePlayerRuntime();
    const MotionClip *findMotionClip(const MotionSnapshot &snapshot,
                                     const std::string &owner,
                                     const std::string &label,
                                     bool allowLabelFallback = true);
    struct MotionCompositionEntryPoint {
        std::string owner;
        std::string label;
    };
    MotionCompositionEntryPoint resolveMotionCompositionEntryPoint(
        const MotionSnapshot &snapshot,
        const std::string &fallbackOwner,
        const std::string &fallbackLabel);

    std::string narrow(const ttstr &value);
    ttstr widen(const std::string &value);

    std::vector<ttstr> buildMotionLookupCandidates(const ttstr &name);
    bool resolveExistingPath(const std::vector<ttstr> &candidates, ttstr &resolved);
    void appendEmbeddedSourceCandidates(const MotionSnapshot &snapshot,
                                        const std::string &source,
                                        std::vector<ttstr> &candidates);

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(const ttstr &path,
                                                       tjs_int decryptSeed);
    tTJSVariant loadPSBVariant(
        const ttstr &path, tjs_int decryptSeed,
        std::shared_ptr<MotionSnapshot> *loadedSnapshot = nullptr);

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
