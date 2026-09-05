//
// Created by LiDon on 2025/9/15.
// Aligned to libkrkr2.so D3DEmotePlayer architecture:
// EmotePlayer is a thin shell delegating all animation logic to an owned Player.
// Binary: D3DEmotePlayerNativeInstance(24b) → EmoteObject(40b) → Player(1496b)
//

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

#include "D3DEmoteModule.h"
#include "EmotePlayer.h"
#include "RuntimeSupport.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmotePlayer::" #name "() stub called")

namespace {
    enum class Sdl3PlayMode {
        None,
        MotionKey,
        SingleCache,
        MultiCache
    };

    ttstr readMetadataBaseField(const tTJSVariant &module, const ttstr &field) {
        if(module.Type() != tvtObject) {
            return {};
        }
        iTJSDispatch2 *root = module.AsObjectNoAddRef();
        if(!root) {
            return {};
        }

        tTJSVariant metadata;
        if(TJS_FAILED(root->PropGet(0, TJS_W("metadata"), nullptr, &metadata, root)) ||
           metadata.Type() != tvtObject) {
            return {};
        }
        iTJSDispatch2 *metaObj = metadata.AsObjectNoAddRef();
        if(!metaObj) {
            return {};
        }

        tTJSVariant base;
        if(TJS_FAILED(metaObj->PropGet(0, TJS_W("base"), nullptr, &base, metaObj)) ||
           base.Type() != tvtObject) {
            // Standalone E-mote PSBs expose chara/motion directly in
            // metadata. Companion modules wrap the same dictionary in
            // metadata.base.
            base = metadata;
        }
        iTJSDispatch2 *baseObj = base.AsObjectNoAddRef();
        if(!baseObj) {
            return {};
        }

        tTJSVariant value;
        if(TJS_FAILED(baseObj->PropGet(0, field.c_str(), nullptr, &value, baseObj)) ||
           value.Type() == tvtVoid) {
            return {};
        }
        return ttstr(value);
    }

    bool isMotionModule(const tTJSVariant &module) {
        const auto snapshot = motion::detail::lookupModuleSnapshot(module);
        if(!snapshot || !snapshot->root) {
            return false;
        }
        const auto typeVal = (*snapshot->root)["type"];
        if(const auto num =
               std::dynamic_pointer_cast<const PSB::PSBNumber>(typeVal)) {
            return num->getValue<int>() == 0;
        }
        return false;
    }

    tTJSVariant boundModuleVariant(const motion::EmotePlayer &self) {
        const tTJSVariant module = self.getModule();
        if(module.Type() == tvtObject) {
            return module;
        }
        return self.getPlayer().getProject();
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

    std::vector<std::shared_ptr<motion::detail::MotionSnapshot>>
    orderedNativeObjectSnapshots(
        const std::vector<motion::ResourceManager::CachedModuleEntry>
            &cached) {
        std::vector<const motion::ResourceManager::CachedModuleEntry *>
            ordered;
        ordered.reserve(cached.size());
        for(const auto &entry : cached) {
            ordered.push_back(&entry);
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto *left,
                                                     const auto *right) {
            return left->loadGeneration < right->loadGeneration;
        });

        std::vector<std::shared_ptr<motion::detail::MotionSnapshot>> result;
        result.reserve(ordered.size());
        for(const auto *entry : ordered) {
            if(auto snapshot =
                   motion::detail::lookupModuleSnapshot(entry->module)) {
                result.push_back(std::move(snapshot));
            }
        }
        return result;
    }
} // namespace

namespace motion {

    EmotePlayer::EmotePlayer(ResourceManager rm) :
        _player(std::move(rm)) {
        // libartemis.so MOGLBase::BeginCreateMask/PrepareInnerMask use mode
        // 1 for the antialiased alpha-mask path. D3DEmoteModule exposes that
        // native default, while a bare Motion Player intentionally defaults
        // to stencil mode. Propagate the module setting when constructing the
        // owned player so a half-closed eyelid does not promote the final
        // translucent mask row to a fully visible strip of iris. The public
        // per-player maskMode setter can still override this afterwards.
        _player.setMaskMode(D3DEmoteModule::getMaskMode());
    }

    EmotePlayer::~EmotePlayer() = default;

    // --- Properties ---

    void EmotePlayer::setVisible(bool v) {
        _visible = v;
        _player.setVisible(v);
    }

    void EmotePlayer::setMeshDivisionRatio(double v) {
        _meshDivisionRatio = v;
        _player.setEmoteMeshDivisionRatio(v);
    }

    // Motion.EmotePlayer sub_67F34C and D3DEmotePlayer sub_5304BC both
    // ignore the assigned boolean and set Player+1161 to one. This switches
    // controller setters from restart semantics to native queued semantics.
    void EmotePlayer::setQueuing(bool) {
        _queuing = true;
        _player.enableEmoteAnimatorQueuing();
        if(emoteTraceEnabled()) {
            LOGGER->info("[EMOTE_TRACE] animator queuing enabled");
        }
    }

    bool EmotePlayer::getAnimating() const {
        return _player.getEmoteAnimating();
    }

    void EmotePlayer::setModule(tTJSVariant v) {
        _module = v;
        // Bridge loaded PSB snapshot into Player's animation pipeline.
        // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
        // After loading PSBs, the EmoteObject initializes its internal Player
        // with the loaded motion data.
        auto snapshot = detail::lookupModuleSnapshot(_module);
        if(snapshot) {
            _player.loadFromSnapshot(snapshot);
        }
    }

    tTJSVariant EmotePlayer::getModule() const { return _module; }

    void EmotePlayer::setMotionKey(ttstr v) {
        _storageKey = v;
        _player.bindMotionModuleKey(v);
        const auto loaded = _player.getProject();
        if(loaded.Type() == tvtObject) {
            _module = loaded;
        }
        _modified = true;
    }

    void EmotePlayer::setMotion(ttstr v) {
        _clipLabel = v;
        _modified = true;
    }

    ttstr EmotePlayer::getMotion() const {
        if(!_clipLabel.IsEmpty()) {
            return _clipLabel;
        }
        return _player.getMotion();
    }

    // --- Methods ---

    // Aligned to libkrkr2.so sub_52FD84: create() is actually "destroy/reset"
    void EmotePlayer::create() {
        _module.Clear();
        _storageKey.Clear();
        _clipLabel.Clear();
        _player.loadFromSnapshot(nullptr);
        _modified = true;
    }

    void EmotePlayer::load(tTJSVariant data) {
        _module = data;
        auto snapshot = detail::lookupModuleSnapshot(_module);
        if(snapshot) {
            _player.loadFromSnapshot(snapshot);
        }
        _modified = true;
    }

    tTJSVariant EmotePlayer::clone() {
        typedef ncbInstanceAdaptor<EmotePlayer> AdaptorT;

        auto *copy = new EmotePlayer(ResourceManager{});
        // Native EmoteObject::clone serializes the source Player and
        // unserializes it into the new Player. Loading only the PSB would
        // discard active timelines and variables during page cloning.
        const auto playerState = _player.serialize();
        // Copy EmotePlayer-specific state
        copy->_module = _module;
        copy->_storageKey = _storageKey;
        copy->_clipLabel = _clipLabel;
        copy->_useD3D = _useD3D;
        copy->_smoothing = _smoothing;
        copy->_meshDivisionRatio = _meshDivisionRatio;
        copy->_queuing = _queuing;
        copy->_hairScale = _hairScale;
        copy->_partsScale = _partsScale;
        copy->_bustScale = _bustScale;
        copy->_bodyScale = _bodyScale;
        copy->_progress = _progress;
        copy->_modified = _modified;
        copy->_drawVisible = _drawVisible;
        copy->_drawOpacity = _drawOpacity;
        copy->_opengl = _opengl;
        copy->_visible = _visible;
        copy->_playCallback = _playCallback;
        copy->_isSelfClear = _isSelfClear;
        copy->_baseScale = _baseScale;
        copy->_userScale = _userScale;
        copy->_rot = _rot;
        copy->_coordX = _coordX;
        copy->_coordY = _coordY;
        copy->_mirrorBase = _mirrorBase;
        copy->_mirrorRequested = _mirrorRequested;
        copy->_mirrorChanged = _mirrorChanged;
        copy->_color = _color;

        // Load the same snapshot into the cloned Player
        auto snapshot = detail::lookupModuleSnapshot(_module);
        if(snapshot) {
            copy->_player.loadFromSnapshot(snapshot);
        }
        copy->_player.unserialize(playerState);
        copy->_player.assignNativeBackendState(_player);

        // These wrapper properties are not part of Player::serialize().
        // Apply them after load/unserialize so model initialization cannot
        // overwrite the values restored by the outer E-mote wrapper.
        copy->_player.setVisible(_visible);
        copy->_player.setEmoteMeshDivisionRatio(_meshDivisionRatio);
        if(_queuing) {
            copy->_player.enableEmoteAnimatorQueuing();
        }
        copy->_player.setHairScale(_hairScale);
        copy->_player.setPartsScale(_partsScale);
        copy->_player.setBustScale(_bustScale);
        copy->_player.setBodyScale(_bodyScale);

        tTJSVariant result;
        if(iTJSDispatch2 *adaptor = AdaptorT::CreateAdaptor(copy)) {
            result = tTJSVariant(adaptor, adaptor);
            adaptor->Release();
        } else {
            delete copy;
        }
        return result;
    }

    void EmotePlayer::show() {
        _visible = true;
        _player.setVisible(true);
    }

    void EmotePlayer::hide() {
        _visible = false;
        _player.setVisible(false);
    }

    void EmotePlayer::assignState() { STUB_WARN(assignState); }
    void EmotePlayer::initPhysics() {
        _player.initPhysics();
        _modified = true;
    }

    tTJSVariant EmotePlayer::serialize() { return _player.serialize(); }

    void EmotePlayer::unserialize(tTJSVariant data) {
        _player.unserialize(data);
        _modified = true;
    }

    // Aligned to libkrkr2.so sub_5302E4: delegates to Player's rotAnimator
    void EmotePlayer::setRot(double rot, double transition, double ease) {
        const bool changed = std::fabs(_rot - rot) > 1e-9;
        _rot = rot;
        _player.setRotate(rot, transition, ease);
        if(changed && emoteAffineTraceEnabled()) {
            LOGGER->info(
                "[EMOTE_AFFINE] player={} motion={} rotRadians={:.6f} rotDegrees={:.3f} transition={:.3f} ease={:.3f}",
                static_cast<const void *>(this),
                detail::narrow(_player.getMotion()), rot,
                rot * 57.2957795130823208768, transition, ease);
        }
        _modified = true;
    }

    tjs_error EmotePlayer::setRotCompat(tTJSVariant *, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setRot(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_53030C: binary returns hardcoded 0.0
    double EmotePlayer::getRot() { return 0.0; }

    // Aligned to libkrkr2.so sub_5301EC: delegates to Player's coordAnimator
    void EmotePlayer::setCoord(double x, double y, double transition,
                               double ease) {
        const bool changed = std::fabs(_coordX - x) > 1e-6 ||
            std::fabs(_coordY - y) > 1e-6;
        _coordX = x;
        _coordY = y;
        _player.setEmoteCoord(x, y, transition, ease);
        if(changed && emoteAffineTraceEnabled()) {
            LOGGER->info(
                "[EMOTE_AFFINE] player={} motion={} coord=({:.3f},{:.3f}) transition={:.3f} ease={:.3f}",
                static_cast<const void *>(this),
                detail::narrow(_player.getMotion()), x, y, transition, ease);
        }
        _modified = true;
    }

    tjs_error EmotePlayer::setCoordCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
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
        self->setCoord(param[0]->AsReal(), param[1]->AsReal(),
                       transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_530260: finalScale = baseScale * userScale
    void EmotePlayer::setScale(double s, double transition, double ease) {
        const bool changed = std::fabs(static_cast<double>(_userScale) - s) >
            1e-7;
        _userScale = static_cast<float>(s);
        const double finalScale =
            static_cast<double>(_baseScale) * static_cast<double>(_userScale);
        _player.setEmoteScale(finalScale, transition, ease);
        if(changed && emoteAffineTraceEnabled()) {
            LOGGER->info(
                "[EMOTE_AFFINE] player={} motion={} userScale={:.6f} baseScale={:.6f} finalScale={:.6f} transition={:.3f} ease={:.3f}",
                static_cast<const void *>(this),
                detail::narrow(_player.getMotion()), s,
                static_cast<double>(_baseScale), finalScale, transition, ease);
        }
        _modified = true;
    }

    tjs_error EmotePlayer::setScaleCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setScale(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_5302DC: binary returns hardcoded 1.0
    double EmotePlayer::getScale() { return 1.0; }

    void EmotePlayer::setMirror(bool mirror) {
        // Aligned to libkrkr2.so sub_671DB0:
        // wrapper stores requested mirror, derives a root-flip delta against a
        // baseline bit, forwards that effective flip to Player_setRootFlipX,
        // then triggers the large controller reset path.
        _mirrorRequested = mirror;
        _mirrorChanged = (_mirrorRequested != _mirrorBase);
        _player.setMirror(_mirrorChanged);
        _modified = true;
    }

    void EmotePlayer::setColor(tjs_int color, double transition, double ease) {
        _color = color;
        _player.setEmoteColor(static_cast<tjs_uint32>(color), transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setColorCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setColor(param[0]->AsInteger(), transition, ease);
        return TJS_S_OK;
    }
    // Aligned to libkrkr2.so sub_530320: binary returns hardcoded 0
    tjs_int EmotePlayer::getColor() { return 0; }

    // --- Variable system: delegates to Player ---
    // Aligned to libkrkr2.so sub_5305C8 → sub_671228:
    // wrapper forwards label/value/transition/ease into Player_setVariable.
    void EmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        _player.setVariable(label, value, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setVariableCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
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
        return TJS_S_OK;
    }

    double EmotePlayer::getVariable(ttstr label) {
        return _player.getVariable(label);
    }

    tTJSVariant EmotePlayer::getVariableFrameList(ttstr label) {
        return _player.getVariableFrameList(label);
    }

    tjs_int EmotePlayer::countVariables() {
        return _player.countVariables();
    }

    ttstr EmotePlayer::getVariableLabelAt(tjs_int idx) {
        return _player.getVariableLabelAt(idx);
    }

    tjs_int EmotePlayer::countVariableFrameAt(tjs_int idx) {
        return _player.countVariableFrameAt(idx);
    }

    ttstr EmotePlayer::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        return _player.getVariableFrameLabelAt(idx, frameIdx);
    }

    double EmotePlayer::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        return _player.getVariableFrameValueAt(idx, frameIdx);
    }

    // --- Wind/Force ---
    void EmotePlayer::startWind(double minAngle, double maxAngle,
                                double amplitude, double freqX,
                                double freqY) {
        _player.startWind(minAngle, maxAngle, amplitude, freqX, freqY);
        _modified = true;
    }

    tjs_error EmotePlayer::startWindCompat(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 5 || !param[0] || !param[1] || !param[2] ||
           !param[3] || !param[4]) {
            return TJS_E_INVALIDPARAM;
        }

        self->startWind(param[0]->AsReal(), param[1]->AsReal(),
                        param[2]->AsReal(), param[3]->AsReal(),
                        param[4]->AsReal());
        return TJS_S_OK;
    }

    void EmotePlayer::stopWind() {
        _player.stopWind();
        _modified = true;
    }

    tjs_error EmotePlayer::stopWindCompat(tTJSVariant *, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        self->stopWind();
        return TJS_S_OK;
    }

    // --- Timeline methods: delegate to Player ---

    tjs_int EmotePlayer::countMainTimelines() {
        return _player.countMainTimelines();
    }

    ttstr EmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        return _player.getMainTimelineLabelAt(idx);
    }

    tTJSVariant EmotePlayer::getMainTimelineLabelList() {
        return _player.getMainTimelineLabelList();
    }

    tjs_int EmotePlayer::countDiffTimelines() {
        return _player.countDiffTimelines();
    }

    ttstr EmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        return _player.getDiffTimelineLabelAt(idx);
    }

    tTJSVariant EmotePlayer::getDiffTimelineLabelList() {
        return _player.getDiffTimelineLabelList();
    }

    tjs_int EmotePlayer::countPlayingTimelines() {
        return _player.countPlayingTimelines();
    }

    ttstr EmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        return _player.getPlayingTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        return _player.getPlayingTimelineFlagsAt(idx);
    }

    bool EmotePlayer::isLoopTimeline(ttstr label) {
        return _player.getLoopTimeline(label);
    }

    bool EmotePlayer::getLoopTimeline(ttstr label) {
        return isLoopTimeline(label);
    }

    tjs_int EmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return _player.getTimelineTotalFrameCount(label);
    }

    void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        _player.playTimeline(label, flags);
        _modified = true;
    }

    bool EmotePlayer::isTimelinePlaying(ttstr label) {
        return _player.getTimelinePlaying(label);
    }

    bool EmotePlayer::getTimelinePlaying(ttstr label) {
        return isTimelinePlaying(label);
    }

    void EmotePlayer::stopTimeline(ttstr label) {
        _player.stopTimeline(label);
        _modified = true;
    }

    void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        _player.setTimelineBlendRatio(label, ratio);
        _modified = true;
    }

    double EmotePlayer::getTimelineBlendRatio(ttstr label) {
        return _player.getTimelineBlendRatio(label);
    }

    void EmotePlayer::fadeInTimeline(ttstr label, double duration,
                                     tjs_int flags) {
        if(duration <= 0.0) {
            playTimeline(label, flags);
            return;
        }
        _player.fadeInTimeline(label, duration, flags);
        _modified = true;
    }

    void EmotePlayer::fadeOutTimeline(ttstr label, double duration,
                                      tjs_int flags) {
        if(duration <= 0.0) {
            stopTimeline(label);
            return;
        }
        _player.fadeOutTimeline(label, duration, flags);
        _modified = true;
    }

    tTJSVariant EmotePlayer::getPlayingTimelineInfoList() {
        return _player.getPlayingTimelineInfoList();
    }

    void EmotePlayer::setTimeline(ttstr label, bool loop) {
        // Player doesn't have an exact equivalent; use playTimeline + loop flag
        _player.playTimeline(label, 0);
    }

    bool EmotePlayer::play(ttstr label, tjs_int flags) {
        if(label.IsEmpty()) {
            label = _clipLabel;
        }

        Sdl3PlayMode mode = Sdl3PlayMode::None;
        ttstr clipLookupLabel;
        bool selfClear = true;
        auto &rm = _player.getResourceManagerNative();
        const tTJSVariant module = boundModuleVariant(*this);

        if(_player.hasActiveMotion() || module.Type() == tvtObject) {
            if(!_player.hasActiveMotion() && module.Type() == tvtObject) {
                if(const auto snapshot = detail::lookupModuleSnapshot(module)) {
                    _player.loadFromSnapshot(snapshot);
                }
            }
            if(_player.hasActiveMotion()) {
                mode = Sdl3PlayMode::MotionKey;
                const ttstr metaChara =
                    readMetadataBaseField(module, TJS_W("chara"));
                const ttstr metaMotion =
                    readMetadataBaseField(module, TJS_W("motion"));
                if(!metaChara.IsEmpty()) {
                    _player.setChara(metaChara);
                }
                clipLookupLabel = !metaMotion.IsEmpty() ? metaMotion : label;
                LOGGER->debug(
                    "EmotePlayer::play mode=MotionKey storageKey={} clipLookup={} playLabel={}",
                    _storageKey.AsStdString(), clipLookupLabel.AsStdString(),
                    label.AsStdString());
            }
        }

        if(mode == Sdl3PlayMode::None) {
            const auto cached = rm.uniqueCachedModules();
            if(cached.size() == 1 && isMotionModule(cached.front().module)) {
                mode = Sdl3PlayMode::SingleCache;
                const auto &entry = cached.front();
                if(!_player.hasActiveMotion()) {
                    _player.bindMotionModuleKey(ttstr(entry.key.c_str()));
                    _storageKey = ttstr(entry.key.c_str());
                    _module = entry.module;
                }
                const ttstr metaChara =
                    readMetadataBaseField(entry.module, TJS_W("chara"));
                const ttstr metaMotion =
                    readMetadataBaseField(entry.module, TJS_W("motion"));
                if(!metaChara.IsEmpty()) {
                    _player.setChara(metaChara);
                }
                clipLookupLabel = !metaMotion.IsEmpty() ? metaMotion : label;
                LOGGER->debug(
                    "EmotePlayer::play mode=SingleCache key={} chara={} clipLookup={}",
                    entry.key, _player.getChara().AsStdString(),
                    clipLookupLabel.AsStdString());
            } else if(!cached.empty()) {
                mode = Sdl3PlayMode::MultiCache;
                selfClear = false;
                const ttstr requestedChara = _player.getChara();
                const auto requestedCharaString =
                    requestedChara.AsStdString();
                const auto requestedMotionString = label.AsStdString();
                const tTJSVariant mostRecentlyLoaded =
                    rm.getLastLoadedModule();
                iTJSDispatch2 *mostRecentlyLoadedObject =
                    mostRecentlyLoaded.Type() == tvtObject
                    ? mostRecentlyLoaded.AsObjectNoAddRef()
                    : nullptr;
                const ResourceManager::CachedModuleEntry *bestEntry = nullptr;
                int bestScore = -1;
                std::uint64_t bestGeneration = 0;
                for(const auto &entry : cached) {
                    const ttstr metaChara =
                        readMetadataBaseField(entry.module, TJS_W("chara"));
                    const ttstr metaMotion =
                        readMetadataBaseField(entry.module, TJS_W("motion"));
                    if(metaChara.IsEmpty() || metaMotion.IsEmpty()) {
                        continue;
                    }

                    const auto metaCharaString = metaChara.AsStdString();
                    const auto metaMotionString = metaMotion.AsStdString();
                    int score = 0;
                    if(!requestedCharaString.empty()) {
                        if(metaCharaString != requestedCharaString) {
                            continue;
                        }
                        score += 4;
                    }
                    if(!requestedMotionString.empty()) {
                        if(metaMotionString != requestedMotionString) {
                            continue;
                        }
                        score += 8;
                    }
                    if(entry.key.find(metaMotionString) != std::string::npos) {
                        score += 1;
                    }
                    // Several resolution variants intentionally carry the
                    // same chara/motion metadata. MotionAffineSourceLayer
                    // loads the variant selected for the current zoom and
                    // immediately constructs a fresh EmotePlayer from this
                    // shared ResourceManager. Native binds that just-loaded
                    // object. Iterating the unordered cache instead could
                    // bind an older half-resolution PSB while the script
                    // applies the full-resolution scale, making one actor
                    // suddenly shrink after an affine/zoom transition.
                    if(entry.module.AsObjectNoAddRef() ==
                       mostRecentlyLoadedObject) {
                        score += 16;
                    }
                    // Resolve equal-metadata resolution variants by their
                    // own load order.  The global last-loaded object can be
                    // another actor when a scene constructs Chocola and
                    // Vanilla back-to-back; in that case unordered_map order
                    // used to select an old half-resolution model and made
                    // only one actor suddenly shrink.
                    if(score > bestScore ||
                       (score == bestScore &&
                        entry.loadGeneration > bestGeneration)) {
                        bestScore = score;
                        bestGeneration = entry.loadGeneration;
                        bestEntry = &entry;
                    }
                }

                if(bestEntry != nullptr) {
                    const ttstr metaChara = readMetadataBaseField(
                        bestEntry->module, TJS_W("chara"));
                    const ttstr metaMotion = readMetadataBaseField(
                        bestEntry->module, TJS_W("motion"));
                    const auto selectedSnapshot =
                        detail::lookupModuleSnapshot(bestEntry->module);
                    if(selectedSnapshot) {
                        _player.setProject(bestEntry->module);
                        _player.loadFromSnapshot(
                            selectedSnapshot,
                            orderedNativeObjectSnapshots(cached));
                    } else {
                        _player.bindMotionModuleKey(
                            ttstr(bestEntry->key.c_str()));
                    }
                    _storageKey = ttstr(bestEntry->key.c_str());
                    _module = bestEntry->module;
                    _player.setChara(metaChara);
                    clipLookupLabel = metaMotion;
                    LOGGER->debug(
                        "EmotePlayer::play mode=MultiCache key={} chara={} clipLookup={}: selected most-recent exact metadata match",
                        bestEntry->key, metaChara.AsStdString(),
                        metaMotion.AsStdString());
                }
            }
        }

        if(clipLookupLabel.IsEmpty()) {
            if(label.IsEmpty()) {
                LOGGER->error(
                    "EmotePlayer::play(): no module/motion resolved in any SDL3 play mode");
                throw std::runtime_error(
                    "motionplayer: EmotePlayer.play() could not resolve motion module");
            }
            clipLookupLabel = label;
            LOGGER->warn("EmotePlayer::play: fallback to playLabel={}",
                         label.AsStdString());
        }

        _clipLabel = label;
        _progress = 0.0;
        _isSelfClear = selfClear;
        _player.setTickCount(0.0);
        _player.setFrameLoopTime(0.0);
        _player.setSpeed(true);

        const bool started =
            _player.playMotionLike_0x6B2284(clipLookupLabel, flags);
        if(!started && !label.IsEmpty() && clipLookupLabel != label) {
            LOGGER->debug(
                "EmotePlayer::play: clipLookup={} failed; retry playLabel={}",
                clipLookupLabel.AsStdString(), label.AsStdString());
            const bool retryStarted =
                _player.playMotionLike_0x6B2284(label, flags);
            _player.setAllplaying(true);
            _modified = true;
            return retryStarted;
        }

        _player.setAllplaying(true);
        _modified = true;
        return started;
    }

    void EmotePlayer::addPlayCallback() {
        _playCallback = true;
    }

    void EmotePlayer::skip() {
        skipToSync();
    }

    void EmotePlayer::skipToSync() {
        _player.skipToSync();
        _modified = true;
    }

    // Aligned to libgame.so sub_530E30 → sub_67A100. pass() flushes the
    // remaining control frames of non-looping timelines; it is deliberately
    // separate from progress(dt).
    void EmotePlayer::pass() {
        _player.passTimelinesLike_0x67A100();
        _modified = true;
    }

    // Shared full animation advance.  sub_530E3C enters Player_progress once
    // with the original delta.  Player_progress internally chunks controller
    // animators to 1.1 frames, but evaluates the physics controllers once with
    // the unmodified delta after those chunks.
    void EmotePlayer::progressFrames(double dtFrames) {
        _progress += dtFrames;
        // D3DEmotePlayer is advanced explicitly by the game's render tick.
        // Record that manual advance on the shared Player so its compatibility
        // continuous callback cannot apply the same wall-clock interval a
        // second time.
        const double beforeTick = _player.getFrameTickCount();
        _player.frameProgressManually(dtFrames);
        if(emoteTraceEnabled()) {
            static std::unordered_map<const EmotePlayer *, uint64_t> counts;
            const uint64_t count = ++counts[this];
            if(count <= 12 || count % 60 == 0 || dtFrames > 2.0) {
                LOGGER->info(
                    "[EMOTE_TRACE] progress player={} call={} dtFrames={:.4f} "
                    "frameTick={:.4f}->{:.4f} speed={} animating={} allplaying={}",
                    static_cast<const void *>(this), count, dtFrames,
                    beforeTick, _player.getFrameTickCount(),
                    _player.getSpeed() ? 1 : 0,
                    _player.getEmoteAnimating() ? 1 : 0,
                    _player.getAllplaying() ? 1 : 0);
            }
        }
        _modified = true;
    }

    // Motion.EmotePlayer: libgame sub_67EC94 converts the TVP millisecond
    // interval to 60 Hz frame units before entering the common player update.
    void EmotePlayer::progress(double dtMilliseconds) {
        progressFrames(dtMilliseconds * 60.0 / 1000.0);
    }

    // D3DEmotePlayer: libgame sub_530E3C receives frame units directly.
    void D3DEmotePlayer::progress(double dtFrames) {
        progressFrames(dtFrames);
    }

    // Aligned to libkrkr2.so sub_672D58: routes by label to bust/h/parts
    void EmotePlayer::setOuterForce(double x, double y) {
        setOuterForce(TJS_W("bust"), x, y, 0.0, 0.0);
    }

    void EmotePlayer::setOuterForce(ttstr label, double x, double y,
                                    double transition, double ease) {
        _player.setOuterForce(label, x, y, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setOuterForceCompat(tTJSVariant *, tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 3 || !param[0] || !param[1] || !param[2]) {
            return TJS_E_INVALIDPARAM;
        }

        const ttstr label(*param[0]);
        const double transition =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        const double ease =
            (numparams >= 5 && param[4]) ? param[4]->AsReal() : 0.0;
        self->setOuterForce(label, param[1]->AsReal(), param[2]->AsReal(),
                            transition, ease);
        return TJS_S_OK;
    }

    tTJSVariant EmotePlayer::getOuterForce() {
        STUB_WARN(getOuterForce);
        return tTJSVariant();
    }

    bool EmotePlayer::contains(double x, double y) {
        if(!_visible) {
            return false;
        }

        // Use local coordinate state for AABB test.
        // Aligned to libkrkr2.so sub_690DF0: supports circle/rect/quad;
        // we use AABB approximation for now.
        const double scale = static_cast<double>(_baseScale * _userScale);
        const double width = _player.getActiveMotionWidth();
        const double height = _player.getActiveMotionHeight();
        if(width <= 0.0 || height <= 0.0) {
            return false;
        }

        const auto scaledWidth = width * scale;
        const auto scaledHeight = height * scale;
        return x >= _coordX && x <= (_coordX + scaledWidth) &&
               y >= _coordY && y <= (_coordY + scaledHeight);
    }

    bool EmotePlayer::contains(ttstr label, double x, double y) {
        if(!_visible || label.IsEmpty()) {
            return false;
        }
        return _player.hitTestLayer(label, x, y);
    }

    tjs_error EmotePlayer::containsCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(!result) {
            return TJS_E_INVALIDPARAM;
        }

        if(numparams >= 3 && param[0] && param[1] && param[2]) {
            *result = tTJSVariant(
                self->contains(ttstr(*param[0]),
                               param[1]->AsReal(),
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

    tjs_error EmotePlayer::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        return self ? Player::setDrawAffineTranslateMatrixCompat(
                          result, numparams, param, &self->_player)
                    : TJS_E_INVALIDOBJECT;
    }

    tjs_error EmotePlayer::clearCompat(tTJSVariant *result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        return self ? Player::clearCompatForNative(
                          result, numparams, param, &self->_player)
                    : TJS_E_INVALIDOBJECT;
    }

    tjs_error EmotePlayer::drawCompat(tTJSVariant *result,
                                      tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        return self ? Player::drawCompatForNative(
                          result, numparams, param, &self->_player)
                    : TJS_E_INVALIDOBJECT;
    }

} // namespace motion
