// PlayerCore.cpp — Constructor, setMotion, serialize, core properties
// Split from Player.cpp for maintainability.
//
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PlayerInternal.h"
#include "TickCount.h"
#include "tjsRandomGenerator.h"

using namespace motion::internal;

extern "C" void AetherKiriMotionEnsureCompactEventHook();

namespace {
    class MotionPlayerAutoProgressHook :
        public tTVPContinuousEventCallbackIntf {
    public:
        void OnContinuousCallback(tjs_uint64 tick) override;
    };

    class MotionPlayerPresentationHoldHook :
        public tTVPContinuousEventCallbackIntf {
    public:
        void OnContinuousCallback(tjs_uint64 tick) override;
    };

    std::mutex g_autoProgressMutex;
    std::vector<motion::Player *> g_autoProgressPlayers;
    MotionPlayerAutoProgressHook g_autoProgressHook;
    bool g_autoProgressHookRegistered = false;

    std::mutex g_presentationHoldMutex;
    std::vector<motion::Player *> g_presentationHoldPlayers;
    MotionPlayerPresentationHoldHook g_presentationHoldHook;
    bool g_presentationHoldHookRegistered = false;

    tTJSVariant createStandaloneRandomGenerator() {
        // Artemis embeds motionplayer without starting the KiriKiri script
        // host. Use the same TJS RandomGenerator native class directly so
        // particle selection and authored random motion retain libkrkr2's
        // semantics without requiring TVPScriptEngine.
        static auto *generatorClass = new TJS::tTJSNC_RandomGenerator();
        iTJSDispatch2 *instance = nullptr;
        if(TJS_FAILED(generatorClass->CreateNew(
               0, nullptr, nullptr, &instance, 0, nullptr,
               generatorClass)) || !instance) {
            return {};
        }
        tTJSVariant result(instance, instance);
        instance->Release();
        return result;
    }

    void registerAutoProgressPlayer(motion::Player *player) {
        if(!player) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_autoProgressMutex);
        if(std::find(g_autoProgressPlayers.begin(), g_autoProgressPlayers.end(),
                     player) == g_autoProgressPlayers.end()) {
            g_autoProgressPlayers.push_back(player);
        }
        if(!g_autoProgressHookRegistered) {
            TVPAddContinuousEventHook(&g_autoProgressHook);
            g_autoProgressHookRegistered = true;
        }
    }

    void unregisterAutoProgressPlayer(motion::Player *player) {
        std::lock_guard<std::mutex> lock(g_autoProgressMutex);
        g_autoProgressPlayers.erase(
            std::remove(g_autoProgressPlayers.begin(), g_autoProgressPlayers.end(),
                        player),
            g_autoProgressPlayers.end());
        if(g_autoProgressHookRegistered && g_autoProgressPlayers.empty()) {
            TVPRemoveContinuousEventHook(&g_autoProgressHook);
            g_autoProgressHookRegistered = false;
        }
    }

    void registerPresentationHoldPlayer(motion::Player *player) {
        if(!player) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_presentationHoldMutex);
        if(std::find(g_presentationHoldPlayers.begin(),
                     g_presentationHoldPlayers.end(),
                     player) == g_presentationHoldPlayers.end()) {
            g_presentationHoldPlayers.push_back(player);
        }
        if(!g_presentationHoldHookRegistered) {
            TVPAddContinuousEventHook(&g_presentationHoldHook);
            g_presentationHoldHookRegistered = true;
        }
    }

    void unregisterPresentationHoldPlayer(motion::Player *player) {
        std::lock_guard<std::mutex> lock(g_presentationHoldMutex);
        g_presentationHoldPlayers.erase(
            std::remove(g_presentationHoldPlayers.begin(),
                        g_presentationHoldPlayers.end(), player),
            g_presentationHoldPlayers.end());
        if(g_presentationHoldHookRegistered &&
           g_presentationHoldPlayers.empty()) {
            TVPRemoveContinuousEventHook(&g_presentationHoldHook);
            g_presentationHoldHookRegistered = false;
        }
    }

    void MotionPlayerAutoProgressHook::OnContinuousCallback(tjs_uint64 tick) {
        std::vector<motion::Player *> players;
        {
            std::lock_guard<std::mutex> lock(g_autoProgressMutex);
            players = g_autoProgressPlayers;
        }

        for(auto *player : players) {
            if(player) {
                player->autoProgressFromContinuousTick(tick);
            }
        }
    }

    void MotionPlayerPresentationHoldHook::OnContinuousCallback(
        tjs_uint64 tick) {
        std::vector<motion::Player *> players;
        {
            std::lock_guard<std::mutex> lock(g_presentationHoldMutex);
            players = g_presentationHoldPlayers;
        }

        for(auto *player : players) {
            if(player) {
                player->presentationHoldFromContinuousTick(tick);
            }
        }
    }

    tTJSNI_BaseLayer *resolvePresentationHoldLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    bool layerBelongsToCgViewPresentation(tTJSNI_BaseLayer *layer) {
        for(auto *current = layer; current; current = current->GetParent()) {
            auto name = motion::detail::narrow(current->GetName());
            for(char &ch : name) {
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            }
            if(name.find("cg view layer") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::string lowerAscii(std::string value) {
        for(char &ch : value) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    std::uint32_t swapPackedRbLike_0x6CD710(std::uint32_t packedColor) {
        return (packedColor & 0xFF00FF00u) |
            ((packedColor >> 16) & 0xFFu) |
            ((packedColor & 0xFFu) << 16);
    }
}

extern "C" void AetherKiriMotionPlayerCoreResetForGameSession() {
    {
        std::lock_guard<std::mutex> lock(g_autoProgressMutex);
        if(g_autoProgressHookRegistered)
            TVPRemoveContinuousEventHook(&g_autoProgressHook);
        g_autoProgressPlayers.clear();
        g_autoProgressHookRegistered = false;
    }
    {
        std::lock_guard<std::mutex> lock(g_presentationHoldMutex);
        if(g_presentationHoldHookRegistered)
            TVPRemoveContinuousEventHook(&g_presentationHoldHook);
        g_presentationHoldPlayers.clear();
        g_presentationHoldHookRegistered = false;
    }
    motion::ResourceManager::resetStaticStateForHostSession();
}

namespace motion {

    std::vector<tTJSVariant> SnapshotAutoProgressPlayerDispatchesForCompat() {
        std::vector<tTJSVariant> snapshot;
        std::lock_guard<std::mutex> lock(g_autoProgressMutex);
        snapshot.reserve(g_autoProgressPlayers.size());
        for(auto *player : g_autoProgressPlayers) {
            if(!player) {
                continue;
            }
            iTJSDispatch2 *dispatch =
                player->getAutoProgressDispatchForCompat();
            if(dispatch) {
                snapshot.emplace_back(dispatch, dispatch);
            }
        }
        return snapshot;
    }

    std::unordered_map<std::string, Player::VariableAnimatorState> *
    Player::controllerAnimatorBucketLike_0x671228(int type) {
        switch(type) {
            case 4:
                return &_type4ControllerAnimators;
            case 5:
                return &_type5ControllerAnimators;
            case 6:
                return &_type6ControllerAnimators;
            case 7:
                return &_type7ControllerAnimators;
            case 8:
                return &_type8ControllerAnimators;
            default:
                return nullptr;
        }
    }

    const std::unordered_map<std::string, Player::VariableAnimatorState> *
    Player::controllerAnimatorBucketLike_0x671228(int type) const {
        switch(type) {
            case 4:
                return &_type4ControllerAnimators;
            case 5:
                return &_type5ControllerAnimators;
            case 6:
                return &_type6ControllerAnimators;
            case 7:
                return &_type7ControllerAnimators;
            case 8:
                return &_type8ControllerAnimators;
            default:
                return nullptr;
        }
    }

    Player::VariableAnimatorState *
    Player::findControllerAnimatorStateLike_0x671228(const std::string &label) {
        const auto findInBucket =
            [&label](auto &bucket) -> VariableAnimatorState * {
            if(const auto it = bucket.find(label); it != bucket.end()) {
                return &it->second;
            }
            return nullptr;
        };

        if(auto *state = findInBucket(_type4ControllerAnimators)) {
            return state;
        }
        if(auto *state = findInBucket(_type5ControllerAnimators)) {
            return state;
        }
        if(auto *state = findInBucket(_type6ControllerAnimators)) {
            return state;
        }
        if(auto *state = findInBucket(_type8ControllerAnimators)) {
            return state;
        }
        return findInBucket(_type7ControllerAnimators);
    }

    const Player::VariableAnimatorState *
    Player::findControllerAnimatorStateLike_0x671228(
        const std::string &label) const {
        const auto findInBucket =
            [&label](const auto &bucket) -> const VariableAnimatorState * {
            if(const auto it = bucket.find(label); it != bucket.end()) {
                return &it->second;
            }
            return nullptr;
        };

        if(const auto *state = findInBucket(_type4ControllerAnimators)) {
            return state;
        }
        if(const auto *state = findInBucket(_type5ControllerAnimators)) {
            return state;
        }
        if(const auto *state = findInBucket(_type6ControllerAnimators)) {
            return state;
        }
        if(const auto *state = findInBucket(_type8ControllerAnimators)) {
            return state;
        }
        return findInBucket(_type7ControllerAnimators);
    }

    void Player::eraseControllerAnimatorStateLike_0x671228(
        const std::string &label) {
        _type4ControllerAnimators.erase(label);
        _type5ControllerAnimators.erase(label);
        _type6ControllerAnimators.erase(label);
        _type7ControllerAnimators.erase(label);
        _type8ControllerAnimators.erase(label);
    }

    void Player::clearControllerAnimatorStateLike_0x671228() {
        _type4ControllerAnimators.clear();
        _type5ControllerAnimators.clear();
        _type6ControllerAnimators.clear();
        _type7ControllerAnimators.clear();
        _type8ControllerAnimators.clear();
    }

    void Player::setSelectorEnabled(bool v) {
        if(_selectorEnabled == v) {
            return;
        }
        _selectorEnabled = v;
        syncSelectorControlsLike_0x670D1C();
    }

    tjs_int Player::getColorWeight() const {
        return static_cast<tjs_int>(
            swapPackedRbLike_0x6CD710(_colorWeightPacked));
    }

    void Player::setColorWeight(tjs_int v) {
        _colorWeightPacked = swapPackedRbLike_0x6CD710(
            static_cast<std::uint32_t>(v));
    }

    tjs_int Player::getMaskMode() const {
        return _maskMode;
    }

    void Player::setMaskMode(tjs_int v) {
        _maskMode = v;
    }

    void Player::setIndependentLayerInherit(bool v) {
        if(_independentLayerInherit == v) {
            return;
        }

        _independentLayerInherit = v;
        if(!_runtime) {
            return;
        }

        // libkrkr2.so 0x6CC9D4 compares player+1097 and marks node+1584 dirty.
        for(auto &node : _runtime->nodes) {
            node.accumulated.dirty = true;
        }
    }

    Player::Player(ResourceManager rm) :
        _runtime(detail::makePlayerRuntime()),
        _resourceManagerNative(std::move(rm)) {
        AetherKiriMotionEnsureCompactEventHook();
        // Aligned to sub_6A88CC (0x6A8988): create TJS Math.RandomGenerator
        // and store at player+992. Child Players inherit via sub_6CED30.
        try {
            TVPExecuteExpression(
                TJS_W("new Math.RandomGenerator()"),
                &_tjsRandomGenerator);
        } catch(...) {
            _tjsRandomGenerator = createStandaloneRandomGenerator();
        }
        if(_tjsRandomGenerator.Type() != tvtObject) {
            _tjsRandomGenerator = createStandaloneRandomGenerator();
        }
        if(_tjsRandomGenerator.Type() != tvtObject) {
            LOGGER->warn("Player: failed to create Math.RandomGenerator");
        }
    }

    Player::~Player() {
        discardRenderToRgbaReadback();
        disableAutoProgress();
        disablePresentationHold();
    }

    bool Player::invokeNativeBackend(
        const std::string &method,
        const std::vector<MotionBackendValue> &arguments,
        std::vector<MotionBackendValue> *results) {
        if(!_nativeBackend) {
            return false;
        }
        std::string error;
        if(_nativeBackend->invoke(method, arguments, results, &error)) {
            return true;
        }
        if(LOGGER) {
            LOGGER->warn("native motion backend {} failed: {}", method,
                         error);
        }
        return false;
    }

    bool Player::assignNativeBackendState(const Player &source) {
        if(!_nativeBackend || !source._nativeBackend) {
            return false;
        }
        std::string error;
        if(_nativeBackend->assignState(*source._nativeBackend, &error)) {
            return true;
        }
        if(LOGGER) {
            LOGGER->warn("native E-mote assignState failed: {}", error);
        }
        return false;
    }

    void Player::enableEmoteAnimatorQueuing() {
        _emoteAnimatorFlag = true;
        invokeNativeBackend(
            "setqueuing", { MotionBackendValue::Boolean(true) });
    }

    void Player::setAllplaying(bool v) {
        _allplaying = v;
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    bool Player::getPlaying() const {
        // `playing` is the state of this Player's selected/root timeline.
        // `allplaying` additionally keeps independently timed descendants
        // advancing. AnimKAGLayer deliberately uses the former to unlock UI
        // input once the owning transition ends, while continuing to tick the
        // latter for nested effects. Returning the aggregate state here makes
        // a looping child permanently lock every motion-backed button.
        return _runtime && !_runtime->playingTimelineLabels.empty();
    }

    bool Player::getEmoteAnimating() const {
        // libgame.so sub_671378 does not read Player+0x44b (allplaying) and it
        // does not treat a playing timeline as blocking by itself. It first
        // collects the controller tracks owned by active timelines, then skips
        // matching controller animators while testing the five controller
        // buckets. Those tracks include perpetual blink/breathing/idle motion;
        // reporting them as `animating` makes MotionAffineSourceLayer wait at
        // every KAG line end forever.
        const auto animatorActive = [](const auto &state) {
            return state.active || !state.queue.empty();
        };

        std::unordered_set<std::string> timelineControlledLabels;
        if(_runtime && _runtime->activeMotion) {
            for(const auto &timelineLabel :
                _runtime->playingTimelineLabels) {
                const auto stateIt =
                    _runtime->timelines.find(timelineLabel);
                if(stateIt == _runtime->timelines.end() ||
                   !stateIt->second.playing) {
                    continue;
                }
                const auto controlIt = _runtime->activeMotion
                    ->timelineControlByLabel.find(timelineLabel);
                if(controlIt == _runtime->activeMotion
                                    ->timelineControlByLabel.end()) {
                    continue;
                }
                for(const auto &track : controlIt->second.tracks) {
                    if(!track.label.empty()) {
                        timelineControlledLabels.insert(track.label);
                    }
                }
            }
        }

        const auto bucketBlocks = [&](const auto &bucket) {
            for(const auto &[label, state] : bucket) {
                if(animatorActive(state) &&
                   timelineControlledLabels.find(label) ==
                       timelineControlledLabels.end()) {
                    return true;
                }
            }
            return false;
        };

        return bucketBlocks(_type4ControllerAnimators) |
            bucketBlocks(_type5ControllerAnimators) |
            bucketBlocks(_type6ControllerAnimators) |
            bucketBlocks(_type7ControllerAnimators) |
            bucketBlocks(_type8ControllerAnimators);
    }

    void Player::enableAutoProgress(iTJSDispatch2 *objthis) {
        if(!objthis) {
            return;
        }

        if(_autoProgressDispatch != objthis) {
            objthis->AddRef();
            auto *oldDispatch = _autoProgressDispatch;
            _autoProgressDispatch = objthis;
            if(oldDispatch) {
                oldDispatch->Release();
            }
        }

        _autoProgressLastTick = 0;
        _autoProgressHasLastTick = false;
        _manualProgressLastTick = 0;
        if(!_autoProgressRegistered) {
            registerAutoProgressPlayer(this);
            _autoProgressRegistered = true;
        }
    }

    void Player::disableAutoProgress() {
        if(_autoProgressRegistered) {
            unregisterAutoProgressPlayer(this);
            _autoProgressRegistered = false;
        }

        _autoProgressLastTick = 0;
        _autoProgressHasLastTick = false;

        auto *dispatch = _autoProgressDispatch;
        _autoProgressDispatch = nullptr;
        if(dispatch) {
            dispatch->Release();
        }
    }

    void Player::enablePresentationHold(iTJSDispatch2 *targetLayerObject,
                                        tjs_uint64 durationMs) {
        if(!targetLayerObject || durationMs == 0) {
            return;
        }

        const tjs_uint64 now = TVPGetTickCount();
        _presentationHoldLayer =
            tTJSVariant(targetLayerObject, targetLayerObject);
        _presentationHoldUntilTick =
            std::max(_presentationHoldUntilTick, now + durationMs);

        if(!_presentationHoldRegistered) {
            registerPresentationHoldPlayer(this);
            _presentationHoldRegistered = true;
            _presentationHoldLastTick = 0;
        }
    }

    void Player::disablePresentationHold() {
        if(_presentationHoldRegistered) {
            unregisterPresentationHoldPlayer(this);
            _presentationHoldRegistered = false;
        }

        _presentationHoldLayer.Clear();
        _presentationHoldUntilTick = 0;
        _presentationHoldLastTick = 0;
        _presentationHoldRendering = false;
    }

    void Player::presentationHoldFromContinuousTick(tjs_uint64 tick) {
        if(!_runtime || _presentationHoldRendering ||
           _presentationHoldLayer.Type() != tvtObject) {
            disablePresentationHold();
            return;
        }

        if(tick > _presentationHoldUntilTick) {
            disablePresentationHold();
            return;
        }
        if(_presentationHoldLastTick != 0 &&
           tick < _presentationHoldLastTick + 33) {
            return;
        }

        iTJSDispatch2 *target = _presentationHoldLayer.AsObjectNoAddRef();
        auto *layer = resolvePresentationHoldLayer(target);
        if(!layer || !layer->GetVisible() || !layer->GetParentVisible() ||
           layer->GetOpacity() <= 0) {
            disablePresentationHold();
            return;
        }

        _presentationHoldLastTick = tick;
        _presentationHoldRendering = true;
        bool rendered = false;
        try {
            rendered = renderToLayer(target);
        } catch(const std::exception &e) {
            if(LOGGER) {
                LOGGER->warn(
                    "motion presentation hold render failed: error={}",
                    e.what());
            }
        } catch(...) {
            if(LOGGER) {
                LOGGER->warn(
                    "motion presentation hold render failed: error=<unknown>");
            }
        }
        _presentationHoldRendering = false;

        if(!rendered) {
            disablePresentationHold();
        }
    }

    void Player::noteManualProgress() {
        _manualProgressLastTick = TVPGetTickCount();
        // A script-owned Motion.Player clock and the continuous callback must
        // never advance the same clip.  The old 120 ms grace period let the
        // automatic clock take over during a slow render, then the script's
        // next wall-clock delta counted that same interval again.  That made
        // title animations jump for one frame whenever loading exceeded the
        // grace period.  A later play() call can explicitly opt the player
        // back into automatic progression via enableAutoProgress().
        disableAutoProgress();
    }

    std::string Player::beginEndedTimelineRenderHold() {
        if(!_runtime || !_runtime->activeMotion) {
            return {};
        }

        const std::string &label = _runtime->lastExplicitTimelineLabel;
        if(label.empty()) {
            return {};
        }

        if(std::find(_runtime->playingTimelineLabels.begin(),
                     _runtime->playingTimelineLabels.end(),
                     label) != _runtime->playingTimelineLabels.end()) {
            return {};
        }

        if(_runtime->activeMotion->clipsByLabel.find(label) ==
           _runtime->activeMotion->clipsByLabel.end()) {
            return {};
        }

        const auto stateIt = _runtime->timelines.find(label);
        if(stateIt == _runtime->timelines.end()) {
            return {};
        }

        auto &state = stateIt->second;
        // A completed one-shot has already committed its final frame.
        // Re-entering the hold while an unrelated descendant keeps the owner
        // ticking rebuilds retained UI state and creates duplicate frames.
        if(_completedEndedTimelineRenderHoldLabel == label) {
            return {};
        }
        if(state.playing || state.totalFrames <= 0.0 ||
           state.currentTime + 0.0001 < state.totalFrames) {
            return {};
        }

        state.playing = true;
        _runtime->playingTimelineLabels.push_back(label);
        _allplaying = true;
        _syncActive = _syncWaiting;
        _runtime->nodesBuilt = false;
        _emoteDirty = true;
        if(LOGGER) {
            const char *debug = std::getenv("AETHERKIRI_MOTION_DEBUG");
            if(debug && *debug && std::strcmp(debug, "0") != 0) {
                LOGGER->info(
                    "motion hold ended timeline for render: motion={} label={} time={:.3f}/{:.3f}",
                    _runtime->activeMotion->path, label, state.currentTime,
                    state.totalFrames);
            }
        }
        return label;
    }

    void Player::endEndedTimelineRenderHold(const std::string &label) {
        if(label.empty() || !_runtime) {
            return;
        }

        _runtime->playingTimelineLabels.erase(
            std::remove(_runtime->playingTimelineLabels.begin(),
                        _runtime->playingTimelineLabels.end(), label),
            _runtime->playingTimelineLabels.end());
        if(const auto stateIt = _runtime->timelines.find(label);
           stateIt != _runtime->timelines.end() &&
           stateIt->second.totalFrames > 0.0 &&
           stateIt->second.currentTime + 0.0001 >=
               stateIt->second.totalFrames) {
            stateIt->second.playing = false;
            stateIt->second.wasPlaying = false;
            _completedEndedTimelineRenderHoldLabel = label;
        }
        if(_endedTimelineRenderHoldHasRestore &&
           _endedTimelineRenderHoldRestoreLabel == label) {
            if(auto stateIt = _runtime->timelines.find(label);
               stateIt != _runtime->timelines.end()) {
                stateIt->second.currentTime =
                    _endedTimelineRenderHoldRestoreTime;
                stateIt->second.playing = false;
                stateIt->second.wasPlaying = false;
            }
            _clampedEvalTime = _endedTimelineRenderHoldRestoreEvalTime;
            _endedTimelineRenderHoldRestoreLabel.clear();
            _endedTimelineRenderHoldRestoreTime = 0.0;
            _endedTimelineRenderHoldRestoreEvalTime = 0.0;
            _endedTimelineRenderHoldHasRestore = false;
        }
        _allplaying = !_runtime->playingTimelineLabels.empty() ||
            shouldReportPlayingChildPlayers();
        _syncActive = _syncWaiting && _allplaying;
    }

    bool Player::deferEndedTimelineRenderHoldUntilDraw(
        const std::string &label) {
        (void)label;
        return false;
    }

    void Player::releaseDeferredEndedTimelineRenderHoldAfterDraw(bool force) {
        (void)force;
        if(_deferredEndedTimelineRenderHoldLabel.empty()) {
            return;
        }

        const auto label = _deferredEndedTimelineRenderHoldLabel;
        _deferredEndedTimelineRenderHoldLabel.clear();
        _completedEndedTimelineRenderHoldLabel = label;
        endEndedTimelineRenderHold(label);
    }

    void Player::dispatchPendingEvents(iTJSDispatch2 *objthis) {
        if(!_runtime || _runtime->pendingEvents.empty()) {
            return;
        }

        const auto pendingEvents = _runtime->pendingEvents;
        _runtime->pendingEvents.clear();
        if(!objthis) {
            return;
        }

        for(const auto &ev : pendingEvents) {
            try {
                if(ev.type == 0) {
                    tTJSVariant p1(detail::widen(ev.param1));
                    tTJSVariant p2(detail::widen(ev.param2));
                    tTJSVariant *args[] = { &p1, &p2 };
                    objthis->FuncCall(0, TJS_W("onAction"), nullptr, nullptr,
                                      2, args, objthis);
                } else if(ev.type == 1) {
                    objthis->FuncCall(0, TJS_W("onSync"), nullptr, nullptr, 0,
                                      nullptr, objthis);
                }
            } catch(...) {
            }
        }
    }

    void Player::autoProgressFromContinuousTick(tjs_uint64 tick) {
        if(!_runtime) {
            disableAutoProgress();
            return;
        }

        iTJSDispatch2 *dispatch = _autoProgressDispatch;
        if(dispatch) {
            dispatch->AddRef();
        }

        const auto releaseDispatch = [&]() {
            if(dispatch) {
                dispatch->Release();
                dispatch = nullptr;
            }
        };

        const bool playing =
            _allplaying || !_runtime->playingTimelineLabels.empty();
        if(!playing || !_speed) {
            _autoProgressLastTick = tick;
            _autoProgressHasLastTick = true;
            if(!playing) {
                disableAutoProgress();
            }
            releaseDispatch();
            return;
        }

        if(_manualProgressLastTick != 0 &&
           tick <= _manualProgressLastTick + 120) {
            _autoProgressLastTick = tick;
            _autoProgressHasLastTick = true;
            releaseDispatch();
            return;
        }

        double deltaMs = 1000.0 / 60.0;
        if(_autoProgressHasLastTick) {
            deltaMs = tick >= _autoProgressLastTick
                ? static_cast<double>(tick - _autoProgressLastTick)
                : 0.0;
        }
        _autoProgressLastTick = tick;
        _autoProgressHasLastTick = true;

        if(deltaMs <= 0.0) {
            releaseDispatch();
            return;
        }
        deltaMs = std::clamp(deltaMs, 0.0, 100.0);

        _runtime->pendingEvents.clear();
        frameProgress(deltaMs * kMotionFramesPerMillisecond);
        ensureNodeTreeBuilt();
        if(!_runtime->nodes.empty()) {
            updateLayers();
        }
        calcBounds();
        // Match krkrsdl3: progress evaluates motion state only. Presentation
        // belongs exclusively to draw(), which receives the authored Layer,
        // D3DAdaptor or SeparateLayerAdaptor from the script.
        dispatchPendingEvents(dispatch);

        if(!_allplaying && _runtime->playingTimelineLabels.empty()) {
            disableAutoProgress();
        }
        releaseDispatch();
    }

    // Aligned to libkrkr2.so Player_getRootX (0x6D98A8) / Player_setRootX (0x6CD028)
    double Player::getX() const {
        if (_runtime && !_runtime->nodes.empty())
            return _runtime->nodes[0].localState.posX;
        return _hasPendingRootPos ? _pendingRootX : 0.0;
    }
    void Player::setX(double v) {
        _pendingRootX = v;
        _hasPendingRootPos = true;
        _layersDirty = true;
        if (_runtime && !_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            if (root.localState.posX != v) {
                root.localState.posX = v;
                root.localState.dirty = true;
                root.accumulated.dirty = true;
            }
        }
    }
    // Aligned to libkrkr2.so Player_getRootY (0x6D98B4) / Player_setRootY (0x6CD048)
    double Player::getY() const {
        if (_runtime && !_runtime->nodes.empty())
            return _runtime->nodes[0].localState.posY;
        return _hasPendingRootPos ? _pendingRootY : 0.0;
    }
    void Player::setY(double v) {
        _pendingRootY = v;
        _hasPendingRootPos = true;
        _layersDirty = true;
        if (_runtime && !_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            if (root.localState.posY != v) {
                root.localState.posY = v;
                root.localState.dirty = true;
                root.accumulated.dirty = true;
            }
        }
    }

    // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
    // Sets activeMotion directly from a pre-loaded snapshot, bypassing file I/O.
    // Used by EmotePlayer.setModule() to bridge loaded PSB data into the Player pipeline.
    void Player::loadFromSnapshot(
        std::shared_ptr<detail::MotionSnapshot> snapshot,
        std::vector<std::shared_ptr<detail::MotionSnapshot>>
            nativeObjectSnapshots) {
        std::string nativeBackendSourcePath = snapshot ? snapshot->path
                                                       : std::string{};
        if(!nativeObjectSnapshots.empty()) {
            nativeBackendSourcePath.clear();
            for(const auto &objectSnapshot : nativeObjectSnapshots) {
                if(!objectSnapshot || objectSnapshot->path.empty()) {
                    continue;
                }
                if(!nativeBackendSourcePath.empty()) {
                    nativeBackendSourcePath += ':';
                }
                nativeBackendSourcePath += objectSnapshot->path;
            }
            if(nativeBackendSourcePath.empty() && snapshot) {
                nativeBackendSourcePath = snapshot->path;
            }
        }
        const bool reuseNativeBackend =
            snapshot && _nativeBackend &&
            _nativeBackendSourcePath == nativeBackendSourcePath;
        // A newly bound motion starts on its own local timeline.  Motion
        // sub-nodes reuse Player instances while switching between clips
        // such as `select`, `over`, and `out`; carrying the previous clip's
        // accumulated time makes the replacement one-shot end on its first
        // zero-delta evaluation.
        _frameLastTime = 0.0;
        _frameLoopTime = 0.0;
        _loopTime = 0.0;
        _clampedEvalTime = 0.0;
        _frameTickCount = 0.0;
        _commandListPulse = false;
        _runtime->activeMotion.reset();
        _runtime->clearMotionBitmapCaches();
        _runtime->timelines.clear();
        _runtime->playingTimelineLabels.clear();
        _runtime->lastExplicitTimelineLabel.clear();
        _runtime->yuzuPresentationCenteredOriginConfirmed = false;
        _runtime->yuzuPresentationTranslateX = 0.0f;
        _runtime->yuzuPresentationTranslateY = 0.0f;
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
        _motionExtensionState.reset();
        if(!reuseNativeBackend) {
            _nativeBackend.reset();
            _nativeBackendSourcePath.clear();
            _nativeBackendPresentationReady = snapshot == nullptr;
            _nativeBackendPresentationHoldLogged = false;
        }
        disableAutoProgress();

        if(snapshot) {
            if(!snapshot->path.empty()) {
                _resourceManagerNative.rememberLoadedModule(
                    detail::widen(snapshot->path), snapshot->moduleValue);
            }
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
            if(!reuseNativeBackend) {
                // A native player renders the complete model from
                // the root. Motion child Players are only an implementation
                // detail of the public compatibility renderer; attaching a
                // second full native model to every child multiplies startup and
                // frame work by the number of authored motion nodes.
                if(!_motionParentPlayer) {
                    const auto *extension = motionPlayerExtension();
                    if(extension && extension->createNativePlayer &&
                       snapshot->objectImage &&
                       !snapshot->objectImage->empty()) {
                        std::string error;
                        detail::MotionSnapshot nativeRequest;
                        nativeRequest.path = snapshot->path;
                        nativeRequest.file = snapshot->file;
                        nativeRequest.objectImage = snapshot->objectImage;
                        nativeRequest.root = snapshot->root;
                        nativeRequest.width = snapshot->width;
                        nativeRequest.height = snapshot->height;
                        nativeRequest.nativeObjectSnapshots =
                            std::move(nativeObjectSnapshots);
                        _nativeBackend = extension->createNativePlayer(
                            nativeRequest, &error);
                        _nativeBackendPresentationReady = !_nativeBackend;
                        if(LOGGER) {
                            if(_nativeBackend) {
                                _nativeBackendSourcePath =
                                    nativeBackendSourcePath;
                                LOGGER->info(
                                    "Kiri native motion backend active ({})",
                                    snapshot->path);
                            } else {
                                LOGGER->warn(
                                    "Kiri native motion backend unavailable for {}: {}",
                                    snapshot->path, error);
                            }
                        }
                    }
                }
            }
        }
    }

    bool Player::hasActiveMotion() const {
        return _runtime && _runtime->activeMotion != nullptr;
    }

    void Player::bindMotionModuleKey(ttstr storageKey) {
        const auto loaded = _resourceManagerNative.findLoadedModule(storageKey);
        if(loaded.Type() != tvtObject) {
            LOGGER->warn(
                "Player::bindMotionModuleKey({}): module not in "
                "ResourceManager cache; call ResourceManager.load() first",
                storageKey.AsStdString());
            return;
        }

        _project = loaded;
        if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
            loadFromSnapshot(snapshot);
            LOGGER->debug(
                "Player::bindMotionModuleKey({}): bound snapshot path={}",
                storageKey.AsStdString(), snapshot->path);
            return;
        }

        if(const auto snapshot =
               resolveMotion(*_runtime, storageKey, &_resourceManagerNative)) {
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
            LOGGER->debug(
                "Player::bindMotionModuleKey({}): resolved snapshot path={}",
                storageKey.AsStdString(), snapshot->path);
            return;
        }

        LOGGER->error(
            "Player::bindMotionModuleKey({}): loaded object has no motion "
            "snapshot",
            storageKey.AsStdString());
        throw std::runtime_error(
            "motionplayer: motionKey module has no parseable motion snapshot");
    }

    double Player::getActiveMotionWidth() const {
        return _runtime->activeMotion ? _runtime->activeMotion->width : 0.0;
    }

    double Player::getActiveMotionHeight() const {
        return _runtime->activeMotion ? _runtime->activeMotion->height : 0.0;
    }

    void Player::setMotion(ttstr v) {
        if(_motionKey == v) {
            return;
        }
        _motionKey = v;
        _layersDirty = true;
        _runtime->activeMotion.reset();
        _runtime->clearMotionBitmapCaches();
        _runtime->timelines.clear();
        _runtime->playingTimelineLabels.clear();
        _runtime->lastExplicitTimelineLabel.clear();
        _runtime->yuzuPresentationCenteredOriginConfirmed = false;
        _runtime->yuzuPresentationTranslateX = 0.0f;
        _runtime->yuzuPresentationTranslateY = 0.0f;
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
        ensureMotionLoaded();
    }

    // Aligned to libkrkr2.so 0x681CAC → 0x6B0F10:
    // motion setter calls objthis.onFindMotion({chara, motion}) to let
    // TJS participate in path resolution before loading the PSB.
    tjs_error Player::setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;

        ttstr motionValue;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            motionValue = *param[0];
        }

        if(self->_motionKey == motionValue) {
            return TJS_S_OK;
        }

        // Build dict {chara, motion} and call objthis.onFindMotion(dict)
        // Aligned to libkrkr2.so Player_loadMotion_guess (0x6B0F10)
        tTJSVariant dictVar = detail::makeDictionary({
            {"chara", tTJSVariant(self->_chara)},
            {"motion", tTJSVariant(motionValue)}
        });
        tTJSVariant onFindResult;
        tTJSVariant *args[] = { &dictVar };
        tjs_error hr = objthis->FuncCall(0, TJS_W("onFindMotion"),
                                          nullptr, &onFindResult, 1, args, objthis);

        // Read back (possibly modified) chara and motion from result
        if(TJS_SUCCEEDED(hr) && onFindResult.Type() == tvtObject) {
            iTJSDispatch2 *resObj = onFindResult.AsObjectNoAddRef();
            if(resObj) {
                tTJSVariant charaVal, motionVal;
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("chara"), nullptr, &charaVal, resObj))
                    && charaVal.Type() != tvtVoid) {
                    self->_chara = ttstr(charaVal);
                }
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("motion"), nullptr, &motionVal, resObj))
                    && motionVal.Type() != tvtVoid) {
                    motionValue = ttstr(motionVal);
                }
            }
        }

        // Reset state and load
        self->_motionKey = motionValue;
        self->_runtime->activeMotion.reset();
        self->_runtime->clearMotionBitmapCaches();
        self->_runtime->timelines.clear();
        self->_runtime->playingTimelineLabels.clear();
        self->_runtime->lastExplicitTimelineLabel.clear();
        self->_runtime->yuzuPresentationCenteredOriginConfirmed = false;
        self->_runtime->yuzuPresentationTranslateX = 0.0f;
        self->_runtime->yuzuPresentationTranslateY = 0.0f;
        self->_runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        self->_variableKeys.Clear();
        self->_variableValues.clear();
        self->ensureMotionLoaded();

        return TJS_S_OK;
    }

    tjs_error Player::getMotionCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;
        if(result) *result = tTJSVariant(self->_motionKey);
        return TJS_S_OK;
    }

    bool Player::ensureMotionLoaded() {
        if(_runtime->activeMotion) {
            return true;
        }

        const auto motionKey = detail::narrow(_motionKey);
        const bool motionKeyLooksLikeStorage =
            motionKey.find('/') != std::string::npos ||
            motionKey.find('\\') != std::string::npos ||
            motionKey.find('.') != std::string::npos;

        if(_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(motionKeyLooksLikeStorage) {
            if(const auto snapshot =
                   resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(const auto loaded = _resourceManagerNative.getLastLoadedModule();
           loaded.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(_motionKey.IsEmpty()) {
            return false;
        }

        if(const auto snapshot =
               resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
            return true;
        }

        return false;
    }

    void Player::syncVariableKeysFromActiveMotion() {
        if(!_runtime->activeMotion) {
            _variableKeys = detail::makeArray({});
            return;
        }

        _variableKeys = detail::makeArray(
            detail::stringsToVariants(_runtime->activeMotion->variableLabels));
        syncSelectorControlsLike_0x670D1C();
    }

    void Player::syncSelectorControlsLike_0x670D1C() {
        const auto *activeMotion = _runtime->activeMotion.get();
        if(!activeMotion) {
            return;
        }

        const auto removeRuntimeState =
            [this](const std::string &label) {
                if(label.empty()) {
                    return;
                }
                _variableAnimators.erase(label);
                eraseControllerAnimatorStateLike_0x671228(label);
                _variableValues.erase(label);
                _evalResultValues.erase(label);
                removeEvalResultSlotLike_Reset(label);
            };

        for(const auto &[selectorLabel, binding] : activeMotion->selectorControls) {
            removeRuntimeState(selectorLabel);
            for(const auto &option : binding.options) {
                removeRuntimeState(option.label);
            }

            if(!_selectorEnabled) {
                continue;
            }

            // Aligned to libkrkr2.so sub_670D1C:
            // selector-enabled path resets each selector controller and
            // immediately applies sub_6680B0(..., index=0, transition=0, ease=0).
            setVariable(detail::widen(selectorLabel), 0.0, 0.0, 0.0);
        }

        _emoteDirty = true;
    }

    const detail::TimelineState *Player::primaryTimelineStateLike_0x66F80C() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        const auto &primaryLabels =
            !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
        for(const auto &label : primaryLabels) {
            if(const auto it = _runtime->timelines.find(label);
               it != _runtime->timelines.end()) {
                return &it->second;
            }
        }

        if(!_motionKey.IsEmpty()) {
            if(const auto it = _runtime->timelines.find(detail::narrow(_motionKey));
               it != _runtime->timelines.end()) {
                return &it->second;
            }
        }

        return !_runtime->timelines.empty()
            ? &(_runtime->timelines.begin()->second)
            : nullptr;
    }

    void Player::resetControllerStateLike_0x66EB8C() {
        // Aligned to libkrkr2.so sub_66EB8C:
        // the binary performs a broad controller/reset sweep after wrapper-side
        // setMirror(). Keep the local reset focused on runtime controller state,
        // eval sinks, and root-node dirty propagation.
        _variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _mirrorPositiveCache.clear();
        _mirrorNegativeCache.clear();

        if(_runtime && !_runtime->nodes.empty()) {
            auto &root = _runtime->nodes.front();
            root.localState.flipX = _rootFlipX;
            root.localState.dirty = true;
            root.accumulated.dirty = true;
            root.interpolatedCache.flipX = _rootFlipX;
        }

        if(_selectorEnabled) {
            syncSelectorControlsLike_0x670D1C();
        }
        _emoteDirty = true;
    }

    const detail::MotionClip *Player::selectActiveClip() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        const auto selectByLabel =
            [this](const std::string &label) -> const detail::MotionClip * {
                if(label.empty()) {
                    return nullptr;
                }
                return detail::findMotionClip(
                    *_runtime->activeMotion, detail::narrow(_chara), label);
            };

        // An E-mote module separates the persistent model motion (for
        // example metadata base.motion = "全体構造") from the independently
        // playing main/difference timelines ("waiting_loop", expressions,
        // and so on).  Those controller labels do not select the geometry
        // object.  The native Emote wrapper keeps the metadata motion bound
        // while timelines only drive variables.  Falling through to the
        // playing label here made clip lookup fail and build the snapshot's
        // union of head/body/face object trees as unrelated root layers.
        if(_runtime->isEmoteMode && !_motionKey.IsEmpty()) {
            if(const auto *clip = selectByLabel(detail::narrow(_motionKey))) {
                return clip;
            }
        }

        for(const auto &label : _runtime->playingTimelineLabels) {
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        if(const auto *clip = selectByLabel(_runtime->lastExplicitTimelineLabel)) {
            return clip;
        }

        // Yuzu title motion sources are cloned into an affine render layer.
        // The render-side clone intentionally has no playback controller of
        // its own, while the authored `title_trial` clip starts fully
        // transparent.  Prefer the resource's explicit static state only for
        // this distinctive title asset shape; other multi-clip resources keep
        // their authored primary-label order.
        const auto &motionPath = _runtime->activeMotion->path;
        const auto slash = motionPath.find_last_of("/\\");
        std::string motionName =
            slash == std::string::npos ? motionPath : motionPath.substr(slash + 1);
        std::transform(motionName.begin(), motionName.end(), motionName.begin(),
                       [](const unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        if(!_runtime->isEmoteMode && motionName == "title_bg.psb" &&
           _runtime->activeMotion->clipsByLabel.find("title_trial") !=
               _runtime->activeMotion->clipsByLabel.end() &&
           _runtime->activeMotion->clipsByLabel.find("title") !=
               _runtime->activeMotion->clipsByLabel.end()) {
            if(const auto *clip = selectByLabel("normal")) {
                return clip;
            }
        }

        const auto &primaryLabels =
            !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
        for(const auto &label : primaryLabels) {
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        if(_runtime->activeMotion->clipsByLabel.size() == 1) {
            return &_runtime->activeMotion->clipsByLabel.begin()->second;
        }

        return nullptr;
    }

    const std::vector<std::string> &Player::activeLayerNames() const {
        static const std::vector<std::string> empty;
        if(!_runtime->activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->layerNames.empty()) {
            return clip->layerNames;
        }

        return _runtime->activeMotion->layerNames;
    }

    const std::unordered_map<
        std::string, std::shared_ptr<const PSB::PSBDictionary>> *
    Player::activeLayersByName() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->layersByName.empty()) {
            return &clip->layersByName;
        }

        return &_runtime->activeMotion->layersByName;
    }

    const std::vector<std::string> &Player::activeSourceCandidates() const {
        static const std::vector<std::string> empty;
        if(!_runtime->activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->sourceCandidates.empty()) {
            return clip->sourceCandidates;
        }

        return _runtime->activeMotion->sourceCandidates;
    }

    tTJSVariant Player::getVariableKeys() {
        ensureMotionLoaded();
        if(_variableKeys.Type() == tvtVoid) {
            return detail::makeArray({});
        }
        return _variableKeys;
    }

    void Player::setProgressCompat(double v) {
        ensureMotionLoaded();
        _layersDirty = true;
        const auto progress = std::clamp(v, 0.0, 1.0);
        _runtime->playingTimelineLabels.clear();
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames * progress;
            } else {
                state.currentTime = progress;
            }
            if(progress >= 1.0 && !state.loop) {
                state.playing = false;
            }
            state.controlInitialized = false;
            state.controlLastAppliedTime = state.currentTime;
            state.controlFrameCursor.clear();
            state.controlTrackValues.clear();
            state.controlTrackAnimators.clear();
            if(state.playing) {
                _runtime->playingTimelineLabels.push_back(state.label);
            }
        }
        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    double Player::getProgressCompat() const {
        bool sawTimeline = false;
        bool anyPlaying = false;
        double progress = 0.0;

        for(const auto &[_, state] : _runtime->timelines) {
            sawTimeline = true;
            anyPlaying = anyPlaying || state.playing;
            if(state.totalFrames > 0.0) {
                progress = std::max(
                    progress,
                    std::clamp(state.currentTime / state.totalFrames, 0.0, 1.0));
            } else if(!state.playing) {
                progress = std::max(progress, 1.0);
            }
        }

        if(!sawTimeline) {
            return _allplaying ? 0.0 : 1.0;
        }
        if(!anyPlaying) {
            return 1.0;
        }
        return progress;
    }

    // --- Core methods ---
    // Aligned to libkrkr2.so sub_6BA7B8 at 0x6BA7B8:
    // 1. sub_A0F5E0(v9, a1+992) — read TJS dispatch from player+992
    // 2. FuncCall(obj, 0, L"random", ...) — call "random" method
    // 3. Convert result variant to double (case 2→real, case 4→int→double, case 5→raw)
    //
    // player+992 is initialized once via "new Math.RandomGenerator()" (sub_6A88CC at 0x6A8988).
    // Child Players inherit the same object from parent (sub_6CED30 at 0x6CED30: a1+992 = a2).
    double Player::random() {
        if (_tjsRandomGenerator.Type() == tvtObject) {
            iTJSDispatch2 *obj = _tjsRandomGenerator.AsObjectNoAddRef();
            if (obj) {
                tTJSVariant result;
                static tjs_uint32 hint = 0;
                tjs_error hr = obj->FuncCall(0, TJS_W("random"), &hint,
                                             &result, 0, nullptr, obj);
                if (TJS_SUCCEEDED(hr))
                    return static_cast<double>(result);
            }
        }
        return 0.0;
    }

    // E-mote scripts pass the metadata object to initPhysics; the native
    // Player has already retained the equivalent PSB controller payloads.
    // Rebuild every state object so loading a save never carries momentum or
    // a half-finished blink from the previous character module.
    void Player::initPhysics() {
        ensureMotionLoaded();
        _motionExtensionState.reset();
        ensureEmoteControlStateInitialized();
        _emoteDirty = true;
        _layersDirty = true;
    }
    tTJSVariant Player::serialize() {
        ensureMotionLoaded();
        ensureEmoteControlStateInitialized();

        const auto serializeAnimator = [](const auto &state) {
            std::vector<tTJSVariant> queue;
            queue.reserve(state.queue.size());
            for(const auto &keyframe : state.queue) {
                queue.push_back(detail::makeDictionary({
                    { "value", static_cast<double>(keyframe.value) },
                    { "duration", static_cast<double>(keyframe.duration) },
                    { "weight", static_cast<double>(keyframe.weight) },
                }));
            }
            return detail::makeDictionary({
                { "value", static_cast<double>(state.currentValue) },
                { "current", static_cast<double>(state.currentValue) },
                { "start", static_cast<double>(state.startValue) },
                { "target", static_cast<double>(state.targetValue) },
                { "progress", static_cast<double>(state.progress) },
                { "duration", static_cast<double>(state.duration) },
                { "weight", static_cast<double>(state.weight) },
                { "active", state.active },
                // Native controller serializers call this request queue `rq`.
                { "rq", detail::makeArray(queue) },
            });
        };

        const auto serializeAnimatorBucket =
            [&serializeAnimator](const auto &bucket) {
                std::vector<std::string> labels;
                labels.reserve(bucket.size());
                for(const auto &[label, state] : bucket) {
                    labels.push_back(label);
                }
                std::sort(labels.begin(), labels.end());

                std::vector<tTJSVariant> result;
                result.reserve(labels.size());
                for(const auto &label : labels) {
                    const auto &state = bucket.at(label);
                    result.push_back(detail::makeDictionary({
                        { "label", detail::widen(label) },
                        { "value", static_cast<double>(state.currentValue) },
                        { "phase", state.active ? 1 : 0 },
                        { "speed", static_cast<double>(state.duration) },
                        { "tick", static_cast<double>(state.progress) },
                        { "animator", serializeAnimator(state) },
                    }));
                }
                return detail::makeArray(result);
            };

        const auto serializeOuterForce = [](const OuterForceState &state) {
            return detail::makeDictionary({
                { "active", state.active },
                { "x", state.x },
                { "y", state.y },
                { "transition", state.transition },
                { "ease", state.ease },
            });
        };

        std::vector<std::pair<std::string, tTJSVariant>> variables;
        std::unordered_set<std::string> authoredVariables;
        if(_runtime->activeMotion) {
            const auto &motion = *_runtime->activeMotion;
            const auto remember = [&authoredVariables](const std::string &label) {
                if(!label.empty()) {
                    authoredVariables.insert(label);
                }
            };

            for(const auto &label : motion.variableLabels) {
                remember(label);
            }
            for(const auto &[label, _] : motion.variableRanges) {
                remember(label);
            }
            for(const auto &[label, _] : motion.variableFrames) {
                remember(label);
            }
            for(const auto &[label, binding] : motion.controllerBindings) {
                (void)binding;
                remember(label);
            }
            for(const auto &[timelineLabel, binding] :
                motion.timelineControlByLabel) {
                (void)timelineLabel;
                for(const auto &track : binding.tracks) {
                    remember(track.label);
                }
            }
            const auto rememberClipParameters = [&remember](
                                                    const detail::MotionClip &clip) {
                for(const auto &parameter : clip.parameters) {
                    remember(parameter.id);
                }
            };
            for(const auto &[label, clip] : motion.clipsByLabel) {
                (void)label;
                rememberClipParameters(clip);
            }
            for(const auto &[owner, clips] : motion.clipsByOwnerAndLabel) {
                (void)owner;
                for(const auto &[label, clip] : clips) {
                    (void)label;
                    rememberClipParameters(clip);
                }
            }
        }
        for(const auto &[label, value] : _variableValues) {
            // Native Player::serialize (libgame.so sub_673220) retains
            // authored E-mote controls through its typed controller buckets
            // and timeline/physics state.  It does not enumerate the PSB's
            // variable range table.  Doing that here used getVariable's
            // range fallback and serialized values such as body_slant=-30
            // even when the current neutral value was zero.  Restoring that
            // dictionary after a resolution/model switch permanently tilted
            // the character and displaced the physics anchors.
            //
            // Keep the Aether-only dictionary solely for genuinely dynamic
            // script variables which are not authored by the active motion.
            if(authoredVariables.find(label) == authoredVariables.end()) {
                variables.emplace_back(label, value);
            }
        }

        std::vector<tTJSVariant> richTimelines;
        for(const auto &label : _runtime->playingTimelineLabels) {
            const auto it = _runtime->timelines.find(label);
            if(it == _runtime->timelines.end() || !it->second.playing) {
                continue;
            }
            const auto &state = it->second;
            richTimelines.push_back(detail::makeDictionary({
                { "label", detail::widen(label) },
                { "flags", static_cast<tjs_int>(state.flags | 1) },
                { "curTime", state.currentTime },
                { "currentTime", state.currentTime },
                { "blendRatio", state.blendRatio },
                { "blendRatioCtrl", serializeAnimator(state.blendAnimator) },
                { "s", state.blendAutoStop },
            }));
        }

        const auto base = detail::makeDictionary({
            { "c", detail::makeDictionary({
                  { "x", _emoteCoordState.x },
                  { "y", _emoteCoordState.y },
                  { "transition", _emoteCoordState.transition },
                  { "ease", _emoteCoordState.ease },
              }) },
            { "scale", detail::makeDictionary({
                  { "value", _emoteScaleState.value },
                  { "transition", _emoteScaleState.transition },
                  { "ease", _emoteScaleState.ease },
              }) },
            { "color", detail::makeDictionary({
                  { "value", static_cast<tjs_int64>(_emoteColorState.packed) },
                  { "transition", _emoteColorState.transition },
                  { "ease", _emoteColorState.ease },
                  { "set", _emoteColorState.explicitlySet },
              }) },
            { "r", detail::makeDictionary({
                  { "value", _emoteRotState.value },
                  { "transition", _emoteRotState.transition },
                  { "ease", _emoteRotState.ease },
              }) },
        });
        const auto outerForces = detail::makeDictionary({
            { "bust", serializeOuterForce(_bustOuterForce) },
            { "h", serializeOuterForce(_hairOuterForce) },
            { "parts", serializeOuterForce(_partsOuterForce) },
        });
        tTJSVariant eye = detail::makeArray({});
        tTJSVariant bust = detail::makeArray({});
        tTJSVariant hair = detail::makeArray({});
        tTJSVariant parts = detail::makeArray({});
        if(const auto *extension = motionPlayerExtension();
           extension && extension->serializeControlState) {
            extension->serializeControlState(
                *this, eye, bust, hair, parts);
        }
        const auto physics = detail::makeDictionary({
            { "bust", bust },
            { "h", hair },
            { "parts", parts },
            { "wind", detail::makeDictionary({
                  { "active", _windState.active },
                  { "minAngle", _windState.minAngle },
                  { "maxAngle", _windState.maxAngle },
                  { "amplitude", _windState.amplitude },
                  { "freqX", _windState.freqX },
                  { "freqY", _windState.freqY },
                  { "phase", _windState.phase },
                  { "prevPhase", _windState.prevPhase },
                  { "scaledAmplitude", _windState.scaledAmplitude },
                  { "counter", _windState.counter },
              }) },
        });

        const auto legacyTimelines = getPlayingTimelineInfoList();

        return detail::makeDictionary({
            // Native libgame.so Player::serialize keys (sub_673220).
            { "t", detail::makeArray(richTimelines) },
            { "eye", eye },
            { "eyebrow", serializeAnimatorBucket(_type5ControllerAnimators) },
            { "m", serializeAnimatorBucket(_type6ControllerAnimators) },
            { "transition", serializeAnimatorBucket(_type7ControllerAnimators) },
            { "s", serializeAnimatorBucket(_type8ControllerAnimators) },
            { "base", base },
            { "o", outerForces },
            // Aether extension: native serialization does not retain every
            // particle coordinate, but doing so prevents a visible dynamics
            // pop on save/load.
            { "physics", physics },
            { "chara", _chara },
            { "motion", _motionKey },
            { "resolution", _resolution },
            { "tickcount", getTickCount() },
            { "speed", _speed },
            { "outline", tTJSVariant(_outline) },
            { "variables", detail::makeDictionary(variables) },
            { "timelines", legacyTimelines },
        });
    }

    void Player::unserialize(tTJSVariant data) {
        if(data.Type() != tvtObject || data.AsObjectNoAddRef() == nullptr) {
            return;
        }

        tTJSVariant value;
        if(getObjectProperty(data, TJS_W("chara"), value) &&
           value.Type() != tvtVoid) {
            _chara = value;
        }

        if(getObjectProperty(data, TJS_W("motion"), value) &&
           value.Type() != tvtVoid) {
            _motionKey = value;
            ensureMotionLoaded();
        }

        if(getObjectProperty(data, TJS_W("resolution"), value) &&
           value.Type() != tvtVoid) {
            _resolution = value.AsReal();
        }

        if(getObjectProperty(data, TJS_W("tickcount"), value) &&
           value.Type() != tvtVoid) {
            setTickCount(value.AsReal());
        }

        if(getObjectProperty(data, TJS_W("speed"), value) &&
           value.Type() != tvtVoid) {
            _speed = value.AsReal();
        }

        if(getObjectProperty(data, TJS_W("outline"), value) &&
           value.Type() != tvtVoid) {
            _outline = ttstr(value);
        }

        ensureMotionLoaded();
        ensureEmoteControlStateInitialized();

        const auto readNumber = [](const tTJSVariant &object,
                                   const tjs_char *name,
                                   double &target) {
            tTJSVariant stored;
            if(getObjectProperty(object, name, stored) &&
               stored.Type() != tvtVoid) {
                target = stored.AsReal();
                return true;
            }
            return false;
        };
        const auto readBool = [](const tTJSVariant &object,
                                 const tjs_char *name,
                                 bool &target) {
            tTJSVariant stored;
            if(getObjectProperty(object, name, stored) &&
               stored.Type() != tvtVoid) {
                target = stored.AsInteger() != 0;
                return true;
            }
            return false;
        };

        const auto restoreVariableAnimator = [&readNumber, &readBool](
                                                   const tTJSVariant &object,
                                                   VariableAnimatorState &state) {
            if(object.Type() != tvtObject ||
               object.AsObjectNoAddRef() == nullptr) {
                return;
            }
            double number = 0.0;
            if(readNumber(object, TJS_W("current"), number) ||
               readNumber(object, TJS_W("value"), number)) {
                state.currentValue = static_cast<float>(number);
            }
            if(readNumber(object, TJS_W("start"), number)) {
                state.startValue = static_cast<float>(number);
            }
            if(readNumber(object, TJS_W("target"), number)) {
                state.targetValue = static_cast<float>(number);
            }
            if(readNumber(object, TJS_W("progress"), number) ||
               readNumber(object, TJS_W("tick"), number)) {
                state.progress = static_cast<float>(number);
            }
            if(readNumber(object, TJS_W("duration"), number) ||
               readNumber(object, TJS_W("speed"), number)) {
                state.duration = static_cast<float>(number);
            }
            if(readNumber(object, TJS_W("weight"), number)) {
                state.weight = static_cast<float>(number);
            }
            readBool(object, TJS_W("active"), state.active);

            tTJSVariant queue;
            if(getObjectProperty(object, TJS_W("rq"), queue) &&
               queue.Type() == tvtObject &&
               queue.AsObjectNoAddRef() != nullptr) {
                state.queue.clear();
                const auto count = getObjectCount(queue);
                for(tjs_int index = 0; index < count; ++index) {
                    tTJSVariant item;
                    if(!getArrayItem(queue, index, item) ||
                       item.Type() != tvtObject) {
                        continue;
                    }
                    VariableKeyframe keyframe;
                    if(readNumber(item, TJS_W("value"), number)) {
                        keyframe.value = static_cast<float>(number);
                    }
                    if(readNumber(item, TJS_W("duration"), number)) {
                        keyframe.duration = static_cast<float>(number);
                    }
                    if(readNumber(item, TJS_W("weight"), number)) {
                        keyframe.weight = static_cast<float>(number);
                    }
                    state.queue.push_back(keyframe);
                }
            }
        };

        const auto restoreAnimatorBucket = [&restoreVariableAnimator](
                                               const tTJSVariant &array,
                                               auto &bucket) {
            if(array.Type() != tvtObject ||
               array.AsObjectNoAddRef() == nullptr) {
                return;
            }
            bucket.clear();
            const auto count = getObjectCount(array);
            for(tjs_int index = 0; index < count; ++index) {
                tTJSVariant item;
                if(!getArrayItem(array, index, item) ||
                   item.Type() != tvtObject) {
                    continue;
                }
                tTJSVariant labelValue;
                if(!getObjectProperty(item, TJS_W("label"), labelValue) ||
                   labelValue.Type() == tvtVoid) {
                    continue;
                }
                const auto label = detail::narrow(labelValue);
                tTJSVariant animator;
                if(!getObjectProperty(item, TJS_W("animator"), animator) ||
                   animator.Type() != tvtObject) {
                    animator = item;
                }
                restoreVariableAnimator(animator, bucket[label]);
            }
        };

        if(getObjectProperty(data, TJS_W("variables"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            DictionaryEnumerator callback;
            tTJSVariantClosure closure(&callback, nullptr);
            value.AsObjectNoAddRef()->EnumMembers(TJS_IGNOREPROP, &closure,
                                                  value.AsObjectNoAddRef());
            for(const auto &[label, stored] : callback.entries) {
                if(stored.Type() != tvtVoid) {
                    setVariable(label, stored.AsReal());
                }
            }
        }

        for(const auto &[name, bucket] : std::initializer_list<
                std::pair<const tjs_char *,
                          std::unordered_map<std::string,
                                             VariableAnimatorState> *>>{
                { TJS_W("eyebrow"), &_type5ControllerAnimators },
                { TJS_W("m"), &_type6ControllerAnimators },
                { TJS_W("transition"), &_type7ControllerAnimators },
                { TJS_W("s"), &_type8ControllerAnimators },
            }) {
            if(getObjectProperty(data, name, value) &&
               value.Type() == tvtObject) {
                restoreAnimatorBucket(value, *bucket);
            }
        }

        tTJSVariant serializedEye;
        getObjectProperty(data, TJS_W("eye"), serializedEye);

        if(getObjectProperty(data, TJS_W("base"), value) &&
           value.Type() == tvtObject) {
            tTJSVariant component;
            if(getObjectProperty(value, TJS_W("c"), component) &&
               component.Type() == tvtObject) {
                readNumber(component, TJS_W("x"), _emoteCoordState.x);
                readNumber(component, TJS_W("y"), _emoteCoordState.y);
                readNumber(component, TJS_W("transition"),
                           _emoteCoordState.transition);
                readNumber(component, TJS_W("ease"), _emoteCoordState.ease);
                setX(_emoteCoordState.x);
                setY(_emoteCoordState.y);
            }
            if(getObjectProperty(value, TJS_W("scale"), component) &&
               component.Type() == tvtObject) {
                const double previousScale = _emoteScaleState.value;
                readNumber(component, TJS_W("value"), _emoteScaleState.value);
                readNumber(component, TJS_W("transition"),
                           _emoteScaleState.transition);
                readNumber(component, TJS_W("ease"), _emoteScaleState.ease);
                if(LOGGER &&
                   std::getenv("AETHERKIRI_EMOTE_AFFINE_TRACE") &&
                   std::fabs(previousScale - _emoteScaleState.value) > 1e-7) {
                    LOGGER->info(
                        "[EMOTE_AFFINE] player={} motion={} unserializeScale={:.6f}->{:.6f}",
                        static_cast<const void *>(this),
                        _runtime && _runtime->activeMotion
                            ? _runtime->activeMotion->path
                            : std::string{},
                        previousScale, _emoteScaleState.value);
                }
            }
            if(getObjectProperty(value, TJS_W("r"), component) &&
               component.Type() == tvtObject) {
                readNumber(component, TJS_W("value"), _emoteRotState.value);
                readNumber(component, TJS_W("transition"),
                           _emoteRotState.transition);
                readNumber(component, TJS_W("ease"), _emoteRotState.ease);
                _rotateAngle = _emoteRotState.value;
            }
            if(getObjectProperty(value, TJS_W("color"), component) &&
               component.Type() == tvtObject) {
                double packed = static_cast<double>(_emoteColorState.packed);
                bool hasExplicitFlag = readBool(
                    component, TJS_W("set"),
                    _emoteColorState.explicitlySet);
                if(readNumber(component, TJS_W("value"), packed)) {
                    _emoteColorState.packed = static_cast<tjs_uint32>(packed);
                    const auto color = _emoteColorState.packed;
                    _emoteColorState.rgbaBytes[0] = static_cast<float>(
                        static_cast<std::uint8_t>(color >> 16));
                    _emoteColorState.rgbaBytes[1] = static_cast<float>(
                        static_cast<std::uint8_t>(color >> 8));
                    _emoteColorState.rgbaBytes[2] = static_cast<float>(
                        static_cast<std::uint8_t>(color));
                    _emoteColorState.rgbaBytes[3] = static_cast<float>(
                        static_cast<std::uint8_t>(color >> 24));
                }
                if(!hasExplicitFlag) {
                    // Older serialized players did not distinguish the
                    // untouched default value (zero) from an explicit color.
                    // Treat a non-zero legacy value as authored, while keeping
                    // the native backend's visible default for untouched players.
                    _emoteColorState.explicitlySet =
                        _emoteColorState.packed != 0;
                }
                readNumber(component, TJS_W("transition"),
                           _emoteColorState.transition);
                readNumber(component, TJS_W("ease"), _emoteColorState.ease);
            }
        }

        const auto restoreOuterForce = [&readNumber, &readBool](
                                           const tTJSVariant &object,
                                           OuterForceState &state) {
            if(object.Type() != tvtObject) {
                return;
            }
            readBool(object, TJS_W("active"), state.active);
            readNumber(object, TJS_W("x"), state.x);
            readNumber(object, TJS_W("y"), state.y);
            readNumber(object, TJS_W("transition"), state.transition);
            readNumber(object, TJS_W("ease"), state.ease);
        };
        if(getObjectProperty(data, TJS_W("o"), value) &&
           value.Type() == tvtObject) {
            tTJSVariant force;
            if(getObjectProperty(value, TJS_W("bust"), force)) {
                restoreOuterForce(force, _bustOuterForce);
            }
            if(getObjectProperty(value, TJS_W("h"), force)) {
                restoreOuterForce(force, _hairOuterForce);
            }
            if(getObjectProperty(value, TJS_W("parts"), force)) {
                restoreOuterForce(force, _partsOuterForce);
            }
        }

        tTJSVariant serializedBust;
        tTJSVariant serializedHair;
        tTJSVariant serializedParts;
        if(getObjectProperty(data, TJS_W("physics"), value) &&
           value.Type() == tvtObject) {
            tTJSVariant component;
            getObjectProperty(value, TJS_W("bust"), serializedBust);
            getObjectProperty(value, TJS_W("h"), serializedHair);
            getObjectProperty(value, TJS_W("parts"), serializedParts);
            if(getObjectProperty(value, TJS_W("wind"), component) &&
               component.Type() == tvtObject) {
                readBool(component, TJS_W("active"), _windState.active);
                readNumber(component, TJS_W("minAngle"), _windState.minAngle);
                readNumber(component, TJS_W("maxAngle"), _windState.maxAngle);
                readNumber(component, TJS_W("amplitude"), _windState.amplitude);
                readNumber(component, TJS_W("freqX"), _windState.freqX);
                readNumber(component, TJS_W("freqY"), _windState.freqY);
                readNumber(component, TJS_W("phase"), _windState.phase);
                readNumber(component, TJS_W("prevPhase"), _windState.prevPhase);
                readNumber(component, TJS_W("scaledAmplitude"),
                           _windState.scaledAmplitude);
                double counter = _windState.counter;
                if(readNumber(component, TJS_W("counter"), counter)) {
                    _windState.counter = static_cast<int>(counter);
                }
            }
        }
        if(const auto *extension = motionPlayerExtension();
           extension && extension->unserializeControlState) {
            extension->unserializeControlState(
                *this, serializedEye, serializedBust,
                serializedHair, serializedParts);
        }

        bool restoredTimelines = false;
        tTJSVariant serializedTimelines;
        bool hasSerializedTimelines =
            getObjectProperty(data, TJS_W("t"), serializedTimelines) &&
            serializedTimelines.Type() == tvtObject &&
            serializedTimelines.AsObjectNoAddRef() != nullptr;
        if(!hasSerializedTimelines) {
            hasSerializedTimelines =
                getObjectProperty(data, TJS_W("timelines"),
                                  serializedTimelines) &&
                serializedTimelines.Type() == tvtObject &&
                serializedTimelines.AsObjectNoAddRef() != nullptr;
        }
        if(hasSerializedTimelines) {
            ensureMotionLoaded();
            if(_runtime->activeMotion && _runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
            _runtime->playingTimelineLabels.clear();

            const auto count = getObjectCount(serializedTimelines);
            for(tjs_int index = 0; index < count; ++index) {
                tTJSVariant item;
                if(!getArrayItem(serializedTimelines, index, item) ||
                   item.Type() != tvtObject ||
                   item.AsObjectNoAddRef() == nullptr) {
                    continue;
                }

                tTJSVariant labelValue;
                if(!getObjectProperty(item, TJS_W("label"), labelValue) ||
                   labelValue.Type() == tvtVoid) {
                    continue;
                }

                const auto key = detail::narrow(labelValue);
                auto it = _runtime->timelines.find(key);
                if(it == _runtime->timelines.end()) {
                    continue;
                }

                restoredTimelines = true;
                it->second.playing = true;
                _runtime->playingTimelineLabels.push_back(key);
                it->second.controlInitialized = false;
                it->second.controlLastAppliedTime = it->second.currentTime;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();

                tTJSVariant flagsValue;
                if(getObjectProperty(item, TJS_W("flags"), flagsValue) &&
                   flagsValue.Type() != tvtVoid) {
                    it->second.flags = flagsValue.AsInteger();
                }

                tTJSVariant currentTimeValue;
                if((getObjectProperty(item, TJS_W("currentTime"),
                                      currentTimeValue) ||
                    getObjectProperty(item, TJS_W("curTime"),
                                      currentTimeValue)) &&
                   currentTimeValue.Type() != tvtVoid) {
                    it->second.currentTime = currentTimeValue.AsReal();
                }

                tTJSVariant blendRatioValue;
                if(getObjectProperty(item, TJS_W("blendRatio"), blendRatioValue) &&
                   blendRatioValue.Type() != tvtVoid) {
                    it->second.blendRatio = blendRatioValue.AsReal();
                }

                tTJSVariant blendAnimatorValue;
                if(getObjectProperty(item, TJS_W("blendRatioCtrl"),
                                     blendAnimatorValue) &&
                   blendAnimatorValue.Type() == tvtObject) {
                    auto &animator = it->second.blendAnimator;
                    double number = 0.0;
                    if(readNumber(blendAnimatorValue, TJS_W("current"),
                                  number) ||
                       readNumber(blendAnimatorValue, TJS_W("value"), number)) {
                        animator.currentValue = static_cast<float>(number);
                    }
                    if(readNumber(blendAnimatorValue, TJS_W("start"), number)) {
                        animator.startValue = static_cast<float>(number);
                    }
                    if(readNumber(blendAnimatorValue, TJS_W("target"), number)) {
                        animator.targetValue = static_cast<float>(number);
                    }
                    if(readNumber(blendAnimatorValue, TJS_W("progress"), number)) {
                        animator.progress = static_cast<float>(number);
                    }
                    if(readNumber(blendAnimatorValue, TJS_W("duration"), number)) {
                        animator.duration = static_cast<float>(number);
                    }
                    if(readNumber(blendAnimatorValue, TJS_W("weight"), number)) {
                        animator.weight = static_cast<float>(number);
                    }
                    readBool(blendAnimatorValue, TJS_W("active"),
                             animator.active);
                    tTJSVariant queue;
                    if(getObjectProperty(blendAnimatorValue, TJS_W("rq"), queue) &&
                       queue.Type() == tvtObject) {
                        animator.queue.clear();
                        const auto queueCount = getObjectCount(queue);
                        for(tjs_int queueIndex = 0; queueIndex < queueCount;
                            ++queueIndex) {
                            tTJSVariant queued;
                            if(!getArrayItem(queue, queueIndex, queued) ||
                               queued.Type() != tvtObject) {
                                continue;
                            }
                            detail::TimelineControlKeyframe keyframe;
                            if(readNumber(queued, TJS_W("value"), number)) {
                                keyframe.value = static_cast<float>(number);
                            }
                            if(readNumber(queued, TJS_W("duration"), number)) {
                                keyframe.duration = static_cast<float>(number);
                            }
                            if(readNumber(queued, TJS_W("weight"), number)) {
                                keyframe.weight = static_cast<float>(number);
                            }
                            animator.queue.push_back(keyframe);
                        }
                    }
                }
                readBool(item, TJS_W("s"), it->second.blendAutoStop);
            }
        }

        if(!restoredTimelines && ensureMotionLoaded()) {
            if(_runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
            const auto &primary = !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
            for(const auto &label : primary) {
                playTimeline(detail::widen(label), PlayFlagForce);
            }
        }

        if(restoredTimelines && _nativeBackend && _runtime->activeMotion) {
            const auto nativeTimelineLabels = [this](
                const char *countMethod, const char *labelMethod) {
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
            struct NativeTimelineRestore {
                std::string label;
                int flags = 0;
                double blendRatio = 1.0;
            };
            std::vector<NativeTimelineRestore> nativeTimelines;
            for(const auto &label : _runtime->playingTimelineLabels) {
                const auto stateIt = _runtime->timelines.find(label);
                if(stateIt == _runtime->timelines.end() ||
                   !stateIt->second.playing) {
                    continue;
                }
                const int nativeFlags = exactMotionBackendTimelineFlags(
                    label, mainLabels, diffLabels);
                if(nativeFlags == 0) {
                    continue;
                }
                const auto duplicate = std::find_if(
                    nativeTimelines.begin(), nativeTimelines.end(),
                    [&label](const auto &entry) {
                        return entry.label == label;
                    });
                if(duplicate == nativeTimelines.end()) {
                    nativeTimelines.push_back(NativeTimelineRestore{
                        label, (stateIt->second.flags & ~3) | nativeFlags,
                        stateIt->second.blendRatio});
                }
            }

            // D3DEmote.tjs changes model resolution by serializing the old
            // player, creating a fresh one, and unserializing into it.  The
            // compatibility state above made getTimelinePlaying() report the
            // restored waiting loop, but the fresh native player had
            // never received PlayTimeline.  The script therefore skipped its
            // own restart and the visible model froze after its initial pose.
            // Rebuild the native timeline set from restored state atomically.
            if(!nativeTimelines.empty()) {
                invokeNativeBackend(
                    "stoptimeline", {MotionBackendValue::String({})});
                for(const auto &timeline : nativeTimelines) {
                    invokeNativeBackend(
                        "playtimeline",
                        {MotionBackendValue::String(timeline.label),
                         MotionBackendValue::Number(timeline.flags)});
                    invokeNativeBackend(
                        "settimelineblendratio",
                        {MotionBackendValue::String(timeline.label),
                         MotionBackendValue::Number(timeline.blendRatio),
                         MotionBackendValue::Number(0.0),
                         MotionBackendValue::Number(0.0),
                         MotionBackendValue::Boolean(false)});
                }
            }

            invokeNativeBackend(
                "setcoord", {MotionBackendValue::Number(_emoteCoordState.x),
                              MotionBackendValue::Number(_emoteCoordState.y),
                              MotionBackendValue::Number(0.0),
                              MotionBackendValue::Number(0.0)});
            invokeNativeBackend(
                "setscale", {MotionBackendValue::Number(_emoteScaleState.value),
                              MotionBackendValue::Number(0.0),
                              MotionBackendValue::Number(0.0)});
            invokeNativeBackend(
                "setrot", {MotionBackendValue::Number(_emoteRotState.value),
                            MotionBackendValue::Number(0.0),
                            MotionBackendValue::Number(0.0)});
            if(_emoteColorState.explicitlySet) {
                invokeNativeBackend(
                    "setcolor",
                    {MotionBackendValue::Number(_emoteColorState.packed),
                     MotionBackendValue::Number(0.0),
                     MotionBackendValue::Number(0.0)});
            }
            invokeNativeBackend(
                "setmeshdivisionratio",
                {MotionBackendValue::Number(_emoteMeshDivisionRatio)});
            invokeNativeBackend(
                "sethairscale", {MotionBackendValue::Number(_hairScale)});
            invokeNativeBackend(
                "setpartsscale", {MotionBackendValue::Number(_partsScale)});
            invokeNativeBackend(
                "setbustscale", {MotionBackendValue::Number(_bustScale)});
            if(_emoteAnimatorFlag) {
                invokeNativeBackend(
                    "setqueuing", {MotionBackendValue::Boolean(true)});
            }
            if(_windState.active) {
                invokeNativeBackend(
                    "startwind",
                    {MotionBackendValue::Number(_windState.minAngle),
                     MotionBackendValue::Number(_windState.maxAngle),
                     MotionBackendValue::Number(_windState.amplitude),
                     MotionBackendValue::Number(_windState.freqX),
                     MotionBackendValue::Number(_windState.freqY)});
            }
            for(const auto &[label, force] :
                std::initializer_list<std::pair<const char *,
                                                const OuterForceState &>>{
                    {"bust", _bustOuterForce},
                    {"h", _hairOuterForce},
                    {"parts", _partsOuterForce},
                }) {
                if(force.active) {
                    invokeNativeBackend(
                        "setouterforce",
                        {MotionBackendValue::String(label),
                         MotionBackendValue::Number(force.x),
                         MotionBackendValue::Number(force.y),
                         MotionBackendValue::Number(0.0),
                         MotionBackendValue::Number(0.0)});
                }
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
        _layersDirty = true;
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setCoord (0x5301EC):
    // store the coord animator payload on Player and keep root x/y in sync.
    void Player::setEmoteCoord(double x, double y, double transition,
                               double ease) {
        _emoteCoordState.x = x;
        _emoteCoordState.y = y;
        _emoteCoordState.transition = transition;
        _emoteCoordState.ease = ease;
        setX(x);
        setY(y);
        invokeNativeBackend(
            "setcoord", { MotionBackendValue::Number(x),
                           MotionBackendValue::Number(y),
                           MotionBackendValue::Number(transition),
                           MotionBackendValue::Number(ease) });
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setScale (0x530260):
    // the wrapper multiplies baseScale * userScale, then forwards the final
    // scalar plus transition/ease to the inner Player scale animator.
    void Player::setEmoteScale(double scale, double transition, double ease) {
        const double previousScale = _emoteScaleState.value;
        _emoteScaleState.value = scale;
        _emoteScaleState.transition = transition;
        _emoteScaleState.ease = ease;
        _emoteDirty = true;
        _layersDirty = true;
        invokeNativeBackend(
            "setscale", { MotionBackendValue::Number(scale),
                           MotionBackendValue::Number(transition),
                           MotionBackendValue::Number(ease) });
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_AFFINE_TRACE") &&
           std::fabs(previousScale - scale) > 1e-7) {
            LOGGER->info(
                "[EMOTE_AFFINE] player={} motion={} innerScale={:.6f}->{:.6f}",
                static_cast<const void *>(this),
                _runtime && _runtime->activeMotion
                    ? _runtime->activeMotion->path
                    : std::string{},
                previousScale, scale);
        }
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setRot (0x5302E4):
    // read player+1161, set player+1162=1, then forward rot/transition/ease
    // to the Player rot animator sink.
    void Player::setRotate(double rot, double transition, double ease) {
        _rotateAngle = rot;
        _emoteRotState.value = rot;
        _emoteRotState.transition = transition;
        _emoteRotState.ease = ease;
        _emoteDirty = true;
        _layersDirty = true;
        invokeNativeBackend(
            "setrot", { MotionBackendValue::Number(rot),
                         MotionBackendValue::Number(transition),
                         MotionBackendValue::Number(ease) });
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setColor (0x530314):
    // unpack AARRGGBB into four float byte values and forward them to the
    // Player color animator sink together with transition/ease.
    void Player::setEmoteColor(tjs_uint32 color, double transition,
                               double ease) {
        _emoteColorState.packed = color;
        _emoteColorState.explicitlySet = true;
        _emoteColorState.rgbaBytes[0] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 16));
        _emoteColorState.rgbaBytes[1] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 8));
        _emoteColorState.rgbaBytes[2] =
            static_cast<float>(static_cast<std::uint8_t>(color));
        _emoteColorState.rgbaBytes[3] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 24));
        _emoteColorState.transition = transition;
        _emoteColorState.ease = ease;
        _colorWeightPacked = swapPackedRbLike_0x6CD710(color);
        _emoteDirty = true;
        _layersDirty = true;
        invokeNativeBackend(
            "setcolor", { MotionBackendValue::Number(color),
                           MotionBackendValue::Number(transition),
                           MotionBackendValue::Number(ease) });
    }

    void Player::setMirror(bool mirror) {
        // Aligned to libkrkr2.so Player_setRootFlipX (0x6CD068):
        // update the synthetic root node's flipX flag and mark it dirty.
        if(_rootFlipX == mirror && _mirrorEvalEnabled == mirror) {
            return;
        }

        _rootFlipX = mirror;
        _mirrorEvalEnabled = mirror;
        resetControllerStateLike_0x66EB8C();
    }

    void Player::setEmoteMeshDivisionRatio(double v) {
        _emoteMeshDivisionRatio = v;
        invokeNativeBackend(
            "setmeshdivisionratio", { MotionBackendValue::Number(v) });
    }

    // Aligned to libkrkr2.so:
    // sub_681F20: player+1184 = a2
    void Player::setHairScale(double s) {
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_CONTROL_TRACE") &&
           _hairScale != s) {
            LOGGER->info("[EMOTE_CONTROL] set hairScale {:.6f} -> {:.6f}",
                         _hairScale, s);
        }
        _hairScale = s;
        invokeNativeBackend("sethairscale",
                            { MotionBackendValue::Number(s) });
    }
    // sub_681F28: player+1192 = a2
    void Player::setPartsScale(double s) {
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_CONTROL_TRACE") &&
           _partsScale != s) {
            LOGGER->info("[EMOTE_CONTROL] set partsScale {:.6f} -> {:.6f}",
                         _partsScale, s);
        }
        _partsScale = s;
        invokeNativeBackend("setpartsscale",
                            { MotionBackendValue::Number(s) });
    }
    // sub_681F30: player+1200 = a2
    void Player::setBustScale(double s) {
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_CONTROL_TRACE") &&
           _bustScale != s) {
            LOGGER->info("[EMOTE_CONTROL] set bustScale {:.6f} -> {:.6f}",
                         _bustScale, s);
        }
        _bustScale = s;
        invokeNativeBackend("setbustscale",
                            { MotionBackendValue::Number(s) });
    }
    // player+1176, consumed by sub_678D50 while interpolating the authored
    // point-shape anchor against the camera origin.
    void Player::setBodyScale(double s) {
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_CONTROL_TRACE") &&
           _bodyScale != s) {
            LOGGER->info("[EMOTE_CONTROL] set bodyScale {:.6f} -> {:.6f}",
                         _bodyScale, s);
        }
        _bodyScale = s;
    }

    // Aligned to D3DEmotePlayer_startWind (0x530A60) -> sub_66DD8C:
    // normalize amplitude, optionally destroy/rebuild wind simulator state, then
    // store min/max/amplitude/freq and reset the active counter.
    void Player::startWind(double minAngle, double maxAngle, double amplitude,
                           double freqX, double freqY) {
        if(LOGGER && std::getenv("AETHERKIRI_EMOTE_CONTROL_TRACE")) {
            LOGGER->info(
                "[EMOTE_CONTROL] startWind args=[{:.6f},{:.6f},{:.6f},{:.6f},{:.6f}] meshDivision={:.6f}",
                minAngle, maxAngle, amplitude, freqX, freqY,
                _emoteMeshDivisionRatio);
        }
        const double absAmplitude = std::abs(amplitude);
        const double normalizedMin = amplitude >= 0.0 ? minAngle : maxAngle;
        const double normalizedMax = amplitude >= 0.0 ? maxAngle : minAngle;

        if(absAmplitude == 0.0 || normalizedMin == normalizedMax ||
           (freqX == 0.0 && freqY == 0.0)) {
            stopWind();
            return;
        }

        const bool rebuild = !_windState.active ||
            _windState.minAngle != normalizedMin ||
            _windState.maxAngle != normalizedMax;
        if(rebuild) {
            _windState = {};
            _windState.active = true;
        }

        _windState.active = true;
        _windState.minAngle = normalizedMin;
        _windState.maxAngle = normalizedMax;
        _windState.amplitude = absAmplitude;
        _windState.freqX = freqX;
        _windState.freqY = freqY;
        const double direction = _windState.prevPhase > _windState.phase
            ? -1.0
            : 1.0;
        const double ratio = _emoteMeshDivisionRatio != 0.0
            ? _emoteMeshDivisionRatio
            : 1.0;
        _windState.scaledAmplitude = direction * (absAmplitude / ratio);
        _windState.counter = 0;
        invokeNativeBackend(
            "startwind", { MotionBackendValue::Number(minAngle),
                            MotionBackendValue::Number(maxAngle),
                            MotionBackendValue::Number(amplitude),
                            MotionBackendValue::Number(freqX),
                            MotionBackendValue::Number(freqY) });
        _emoteDirty = true;
    }

    // Aligned to sub_681A38: delete wind simulator and clear player+1128.
    void Player::stopWind() {
        _windState = {};
        invokeNativeBackend("stopwind");
        _emoteDirty = true;
    }

    // Aligned to D3DEmotePlayer_setOuterForce (0x530A8C) ->
    // Player_setOuterForce (0x672D58): case-insensitive label dispatch for
    // "bust", "h", and "parts", carrying transition/ease through the sink.
    void Player::setOuterForce(ttstr label, double x, double y,
                               double transition, double ease) {
        const auto key = lowerAscii(detail::narrow(label));
        OuterForceState *target = nullptr;
        if(key == "bust") {
            target = &_bustOuterForce;
        } else if(key == "h") {
            target = &_hairOuterForce;
        } else if(key == "parts") {
            target = &_partsOuterForce;
        } else {
            return;
        }

        target->active = true;
        target->x = x;
        target->y = y;
        target->transition = transition;
        target->ease = ease;
        invokeNativeBackend(
            "setouterforce", { MotionBackendValue::String(key),
                                MotionBackendValue::Number(x),
                                MotionBackendValue::Number(y),
                                MotionBackendValue::Number(transition),
                                MotionBackendValue::Number(ease) });
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so sub_681EF8 at 0x681EF8:
    // Stores translate (x,y) to runtime+144/148 (cameraOffsetX/Y).
    // The full 6-param matrix version is handled by setDrawAffineTranslateMatrixCompat.
    void Player::setDrawAffineTranslateMatrix(tTJSVariant) {
        // Single-param variant: compat handler does the real work via NCB_METHOD_RAW
    }

    tTJSVariant Player::getCameraOffset() { return _cameraPosition; }

    void Player::setCameraOffset(tTJSVariant offset) {
        _cameraPosition = offset;
        // Aligned to libkrkr2.so sub_6D9A38: setCameraOffset(x, y)
        // Stores as float at Player+144/148. NCB passes a Point with x,y.
        if(offset.Type() == tvtObject) {
            auto *obj = offset.AsObjectNoAddRef();
            if(obj) {
                tTJSVariant xv, yv;
                if(obj->PropGet(0, TJS_W("x"), nullptr, &xv, obj) == TJS_S_OK)
                    _cameraOffsetX = static_cast<float>(xv.AsReal());
                if(obj->PropGet(0, TJS_W("y"), nullptr, &yv, obj) == TJS_S_OK)
                    _cameraOffsetY = static_cast<float>(yv.AsReal());
            }
        }
    }

    void Player::modifyRoot(tTJSVariant data) { _project = data; }

    void Player::debugPrint() {
        LOGGER->info("motionKey={}, motions={}, sources={}, timelines={}",
                     _motionKey.AsStdString(), _runtime->motionsByKey.size(),
                     _runtime->sourcesByKey.size(), _runtime->timelines.size());
    }


} // namespace motion
