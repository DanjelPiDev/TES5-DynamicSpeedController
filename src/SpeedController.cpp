#include "SpeedController.h"

#include <chrono>
#include <cmath>

#include "SKSE/Logger.h"
#include "nlohmann/json.hpp"

#include "DynamicWetness_PublicAPI.h"
using nlohmann::json;

using namespace RE;

namespace SWE_Link {
    using namespace SWE::API;  // pull in CAT_*, FLAG_*, GetEnvMask(), etc.

    static constexpr const char* kSweatID = "speedctrl_sweat";
    static constexpr const char* kSweatBlockID = "speedctrl_sweat_block";

    inline void Init() {
        static bool tried = false;
        if (tried) return;
        tried = true;

        if (!SWE::API::Init()) {
            spdlog::info("[SWE_Link] DynamicWetness not found");
            spdlog::info("[SWE_Link] Download from https://www.nexusmods.com/skyrimspecialedition/mods/158207");
        } else {
            spdlog::info("[SWE_Link] DynamicWetness API initialized");
        }
    }

    inline bool IsAvailable() {
        Init();
        return SWE::API::IsAvailable();
    }

    inline bool IsWorldWet(RE::Actor* a) {
        Init();
        if (!a || !IsAvailable()) return false;

        // Prefer compact env mask + decode helper for consistent logic
        const unsigned m = SWE::API::GetEnvMask(a);
        const auto env = SWE::API::DecodeEnv(m);
        const bool exposedWetWx = env.wetWeather && env.exteriorOpen && !env.underRoof;
        return env.inWater || exposedWetWx;
    }

    inline void SetSweat(RE::Actor* a, float v, float ttlSec, bool envIsWet) {
        Init();
        if (!a || !IsAvailable()) return;

        v = std::clamp(v, 0.0f, 1.0f);

        // If world is wet, let DynamicWetness handle visuals, remove overlay
        if (envIsWet) {
            SWE::API::ClearExternalWetness(a, kSweatID);
            return;
        }

        // Skin only, additive after SWE’s own wetness, no extra flags
        const unsigned mask = SWE::API::CAT_SKIN_FACE | SWE::API::FLAG_PASSTHROUGH;
        SWE::API::SetExternalWetnessMask(a, kSweatID, v, ttlSec, mask);
    }

    inline void ClearSweat(RE::Actor* a) {
        Init();
        if (!a || !IsAvailable()) return;
        SWE::API::ClearExternalWetness(a, kSweatID);
    }
}



SpeedController* SpeedController::GetSingleton() {
    static SpeedController inst;
    return &inst;
}

void SpeedController::Install() {
    Settings::LoadFromJson(Settings::DefaultPath());
    LoadToggleBindingFromJson();
    lastApplyPlayerMs_ = NowMs();

    prevAffectNPCs_ = Settings::enableSpeedScalingForNPCs.load();

    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESCombatEvent>(this);
        holder->AddEventSink<RE::TESLoadGameEvent>(this);
        holder->AddEventSink<RE::TESEquipEvent>(this);
    }
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        pc->AddAnimationGraphEventSink(this);
    }
    if (auto* im = RE::BSInputDeviceManager::GetSingleton()) {
        im->AddEventSink(this);
    }

    StartHeartbeat();
    Apply();
}

template <class T>
static T* LookupForm(const std::string& plugin, std::uint32_t id) {
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return nullptr;
    return dh->LookupForm<T>(id, plugin);
}

static RE::BGSLocation* GetPlayerLocation(const RE::PlayerCharacter* pc) {
    if (!pc) return nullptr;

    if (auto* loc = pc->GetCurrentLocation()) return loc;

    if (auto* cell = pc->GetParentCell()) {
        if (auto* loc2 = cell->GetLocation()) return loc2;
    }
    return nullptr;
}

static std::optional<float> ComputeLocationValue(const RE::PlayerCharacter* pc) {
    auto* loc = GetPlayerLocation(pc);
    if (!loc) return std::nullopt;

    for (auto& fs : Settings::reduceInLocationSpecific) {
        if (auto* l = LookupForm<RE::BGSLocation>(fs.plugin, fs.id); l && l == loc) {
            return fs.value;
        }
    }

    if (auto* kwSet = loc->keywords) {
        for (auto& fs : Settings::reduceInLocationType) {
            if (auto* kw = LookupForm<RE::BGSKeyword>(fs.plugin, fs.id); kw) {
                if (loc->HasKeyword(kw)) {
                    return fs.value;
                }
            }
        }
    }
    return std::nullopt;
}

static bool RaceIs(const RE::Actor* a, std::string_view edid) {
    if (!a) return false;
    auto* r = a->GetRace();
    if (!r) return false;
    if (const char* id = r->GetFormEditorID()) {
        return _stricmp(id, edid.data()) == 0;
    }
    return false;
}

static bool TrySetAnyGraphVarFloat(RE::Actor* a, std::initializer_list<const char*> names, float v) {
    bool any = false;
    for (auto n : names) {
        any |= a->SetGraphVariableFloat(n, v);
    }
    return any;
}

static inline void ClampSpeedFloor(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;
    const float floor = Settings::minFinalSpeedMult.load();
    const float cur = avo->GetActorValue(RE::ActorValue::kSpeedMult);
    if (cur < floor) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, floor - cur);
    }
}

static RE::TESWeather* GetCurrentWeather() {
    if (auto* sky = RE::Sky::GetSingleton()) {
        return sky->currentWeather;
    }
    return nullptr;
}

static std::optional<float> ComputeWeatherValue(const RE::Actor* a) {
    if (!Settings::weatherEnabled.load()) return std::nullopt;
    if (Settings::weatherIgnoreInterior.load()) {
        const RE::TESObjectCELL* cell = a ? a->GetParentCell() : nullptr;
        if (cell && cell->IsInteriorCell()) return std::nullopt;
    }
    auto* cur = GetCurrentWeather();
    if (!cur) return std::nullopt;
    for (auto& fs : Settings::reduceInWeatherSpecific) {
        if (auto* w = LookupForm<RE::TESWeather>(fs.plugin, fs.id); w && w == cur) {
            return fs.value;
        }
    }
    return std::nullopt;
}
static float LinearVitalPenaltyPct(const RE::Actor* a, RE::ActorValue av, bool enabled, float thrPct, float reducePct,
                                   float smoothWidthPct) {
    if (!enabled || !a) return 0.0f;

    RE::Actor* ac = const_cast<RE::Actor*>(a);
    auto* avo = ac ? ac->AsActorValueOwner() : nullptr;
    if (!avo) return 0.0f;

    const float cur = avo->GetActorValue(av);

    float maxv = 0.0f;
    try {
        maxv = avo->GetPermanentActorValue(av);
    } catch (...) {
    }
    if (maxv <= 1e-3f) return 0.0f;

    const float pct = std::clamp(cur / maxv * 100.0f, 0.0f, 100.0f);
    const float w = std::max(0.0f, smoothWidthPct);

    float factor = 0.0f;
    if (pct <= thrPct - w)
        factor = 1.0f;
    else if (pct < thrPct && w > 0.f)
        factor = (thrPct - pct) / w;
    else
        factor = 0.0f;

    return -reducePct * std::clamp(factor, 0.0f, 1.0f);
}

namespace {
    inline bool IsWithinNPCProcRadius(const RE::Actor* a) {
        if (!a) return false;
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) return false;
        if (a == pc) return true;

        const int r = Settings::npcRadius.load();
        if (r <= 0) return true;

        const auto ap = a->GetPosition();
        const auto pp = pc->GetPosition();
        const float dx = ap.x - pp.x;
        const float dy = ap.y - pp.y;
        const float r2 = static_cast<float>(r) * static_cast<float>(r);
        return (dx * dx + dy * dy) <= r2;  // XY-Radius
    }
    inline float ExpoLerp(float prev, float target, float dt, float tau) {
        if (tau <= 1e-6f) return target;
        const float a = 1.0f - std::exp(-dt / std::max(1e-4f, tau));
        return prev + a * (target - prev);
    }
    inline float RateTowards(float prev, float target, float dt, float ratePerSec) {
        if (ratePerSec <= 0.f) return target;
        const float maxStep = ratePerSec * dt;
        const float d = target - prev;
        if (d > maxStep) return prev + maxStep;
        if (d < -maxStep) return prev - maxStep;
        return target;
    }
    inline float SmoothSprintAnim(float prev, float target, float dt) {
        if (!Settings::sprintAnimOwnSmoothing.load()) {
            return prev + (target - prev);
        }
        using SM = Settings::SmoothingMode;
        const auto mode = static_cast<SM>(Settings::sprintAnimSmoothingMode.load());
        switch (mode) {
            case SM::RateLimit:
                return RateTowards(prev, target, dt, Settings::sprintAnimRatePerSec.load());
            case SM::ExpoThenRate:
                return RateTowards(prev, ExpoLerp(prev, target, dt, Settings::sprintAnimTau.load()), dt,
                                   Settings::sprintAnimRatePerSec.load());
            case SM::Exponential:
            default:
                return ExpoLerp(prev, target, dt, Settings::sprintAnimTau.load());
        }
    }
    inline float PredictDiagonalPenalty(float curSM, float floor, float inX, float inY, bool sprinting) {
        // f = max(|x|,|y|)/sqrt(x^2+y^2) (<=1)
        const float ax = std::fabs(inX), ay = std::fabs(inY);
        const float mag = std::sqrt(inX * inX + inY * inY);
        const float maxc = std::max(ax, ay);
        if (mag <= 1e-4f || maxc <= 0.0f) return 0.0f;

        float f = std::min(1.0f, maxc / mag);
        float headroom = std::max(0.0f, curSM - floor);
        float penalty = headroom * (f - 1.0f);
        if (sprinting) penalty *= 0.5f;
        return penalty;
    }

}
void SpeedController::UpdateSprintAnimRate(RE::Actor* a) {
    if (!a) return;
    if (!Settings::syncSprintAnimToSpeed.load()) return;

    bool sprinting = IsSprintingByGraph(a);
    if (!sprinting) {
        if (a == RE::PlayerCharacter::GetSingleton()) {
            const uint64_t now = NowMs();
            const uint64_t last = lastSprintMs_.load(std::memory_order_relaxed);
            sprinting = (last != 0) && (now - last <= kSprintLatchMs);
        }
    }

    auto* avo = a->AsActorValueOwner();
    if (!avo) return;

    float target = 1.0f;
    if (sprinting) {
        float ratio = std::max(1.0f, avo->GetActorValue(RE::ActorValue::kSpeedMult)) / 100.0f;
        if (Settings::onlySlowDown.load()) ratio = std::min(ratio, 1.0f);
        target = std::clamp(ratio, Settings::sprintAnimMin.load(), Settings::sprintAnimMax.load());
    }

    const uint64_t nowMs = NowMs();
    static uint64_t lastMs = 0;
    const float dt = (lastMs == 0) ? (1.0f / 60.0f) : std::max(0.0f, (nowMs - lastMs) / 1000.0f);
    lastMs = nowMs;

    sprintAnimRate_ = SmoothSprintAnim(sprintAnimRate_, target, dt);

    TrySetAnyGraphVarFloat(a, {"fAnimSpeedMult", "AnimSpeedMult", "AnimSpeed", "fSprintSpeedMult"}, sprintAnimRate_);
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESEquipEvent* evn,
                                                       RE::BSTEventSource<RE::TESEquipEvent>*) {
    if (!evn) return RE::BSEventNotifyControl::kContinue;

    RE::TESObjectREFR* ref = evn->actor.get();
    RE::Actor* a = ref ? ref->As<RE::Actor>() : nullptr;
    if (!a) return RE::BSEventNotifyControl::kContinue;

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) {
        UpdateAttackSpeed(a);
    } else if (Settings::enableSpeedScalingForNPCs.load()) {
        if (IsWithinNPCProcRadius(a)) {
            UpdateAttackSpeed(a);
        } else {
            RevertDeltasFor(a);
            ClearNPCState(GetID(a));
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}

float& SpeedController::SlopeDeltaSlot(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return slopeDeltaPlayer_;
    return slopeDeltaNPC_[GetID(a)];
}

void SpeedController::ClearSlopeDeltaFor(RE::Actor* a) {
    float& slot = SlopeDeltaSlot(a);
    if (std::fabs(slot) > 1e-4f) {
        if (auto* avo = a->AsActorValueOwner()) {
            avo->ModActorValue(RE::ActorValue::kSpeedMult, -slot);
        }
        slot = 0.0f;
    }
    SlopeResidualSlot(a) = 0.0f;

    ClearPathFor(a);
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) {
        lastSlopePlayerMs_ = 0;
    } else {
        lastSlopeNPCMs_.erase(GetID(a));
    }
}

bool SpeedController::UpdateSlopePenalty(RE::Actor* a, float dt) {
    if (!a || dt <= 0.f) return false;
    if (!Settings::slopeEnabled.load()) return false;
    if (a != RE::PlayerCharacter::GetSingleton() && !Settings::slopeAffectsNPCs.load()) return false;

    const uint64_t nowMs = NowMs();
    const auto pos = a->GetPosition();

    PushPathSample(a, pos, nowMs);

    float slopeDeg = 0.0f;
    bool haveSlope = false;

    bool still = false;

    auto& q = PathBuf(a);
    if (q.size() >= 2) {
        const float movedXY = q.back().sxy - q[q.size() - 2].sxy;
        still = (std::fabs(movedXY) < Settings::slopeMinXYPerFrame.load());
    }

    if (Settings::slopeMethod.load() == 1) {
        haveSlope =
            ComputePathSlopeDeg(a, Settings::slopeLookbackUnits.load(), Settings::slopeMaxHistorySec.load(), slopeDeg);
        if (a == RE::PlayerCharacter::GetSingleton() 
            && Settings::dwEnabled.load() 
            && Settings::dwSlopeFeatureEnabled.load()) {
            static float s_dwIntensity = 0.0f;

            const float startDeg = std::max(0.0f, Settings::dwStartDeg.load());
            const float fullDeg = std::max(startDeg + 0.1f, Settings::dwFullDeg.load());
            const float span = std::max(0.1f, fullDeg - startDeg);

            float target = 0.0f;

            if (!still && haveSlope && slopeDeg > startDeg) {
                const float moveMag = std::clamp(std::sqrt(moveX_ * moveX_ + moveY_ * moveY_), 0.0f, 1.0f);
                const float slopePart = std::clamp((slopeDeg - startDeg) / span, 0.0f, 1.0f);
                target = std::clamp(slopePart * (0.50f + 0.50f * moveMag), 0.0f, 1.0f);
            }

            const float rateUp = std::max(0.0f, Settings::dwBuildUpPerSec.load());
            const float rateDown = std::max(0.0f, Settings::dwDryPerSec.load());
            const float rate = (target >= s_dwIntensity) ? rateUp : rateDown;

            s_dwIntensity = RateTowards(s_dwIntensity, target, dt, rate);

            const bool wetEnv = SWE_Link::IsWorldWet(a);

            constexpr float kHoldSec = 1.75f;
            constexpr float kEps = 1e-3f;

            static float s_lastSent = -1.0f;
            static uint64_t s_lastMs = 0;

            const uint64_t nowMs = NowMs();
            const float sendVal = (s_dwIntensity > kEps) ? s_dwIntensity : 0.0f;

            const uint64_t resendMs = static_cast<uint64_t>(kHoldSec * 1000.0f * 0.6f);
            bool needSend = std::fabs(sendVal - s_lastSent) > 0.01f || (nowMs - s_lastMs) > resendMs;

            if (needSend) {
                const bool wetEnv = SWE_Link::IsWorldWet(a);
                SWE_Link::SetSweat(a, sendVal, kHoldSec, wetEnv);
                s_lastSent = sendVal;
                s_lastMs = nowMs;
            }
        } else {
            SWE_Link::ClearSweat(a);
        }
    } else {
        haveSlope = false;
    }

    float want = 0.0f;
    if (Settings::slopeMethod.load() == 1) {
        if (!still) {
            bool haveSlope = ComputePathSlopeDeg(a, Settings::slopeLookbackUnits.load(),
                                                 Settings::slopeMaxHistorySec.load(), slopeDeg);
            if (haveSlope) {
                if (slopeDeg > 0.0f)
                    want -= Settings::slopeUphillPerDeg.load() * slopeDeg;
                else if (slopeDeg < 0.0f)
                    want += Settings::slopeDownhillPerDeg.load() * (-slopeDeg);
                want = std::clamp(want, -Settings::slopeMaxAbs.load(), Settings::slopeMaxAbs.load());
            }
        } else {
            want = 0.0f;
        }
    } else {
        want = 0.0f;
    }

    float& slot = SlopeDeltaSlot(a);
    const float tau = std::max(0.01f, Settings::slopeTau.load());
    const float alpha = 1.0f - std::exp(-dt / tau);
    const float newDelta = slot + alpha * (want - slot);

    float diff = newDelta - slot;
    float& acc = SlopeResidualSlot(a);
    acc += diff;

    static constexpr float kSlopeGran = 1e-4f;
    if (std::fabs(acc) >= kSlopeGran) {
        if (auto* avo = a->AsActorValueOwner()) {
            avo->ModActorValue(RE::ActorValue::kSpeedMult, acc);
            slot += acc;
            acc = 0.0f;
            return true;
        }
    }
    return false;
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESLoadGameEvent*,
                                                       RE::BSTEventSource<RE::TESLoadGameEvent>*) {
    loading_.store(true, std::memory_order_relaxed);
    Settings::LoadFromJson(Settings::DefaultPath());
    LoadToggleBindingFromJson();
    lastSprintMs_.store(0, std::memory_order_relaxed);

    // OnPostLoadGame();
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESCombatEvent* evn,
                                                       RE::BSTEventSource<RE::TESCombatEvent>*) {
    if (evn) {
        pendingRefresh_.store(true, std::memory_order_relaxed);
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::BSAnimationGraphEvent* evn,
                                                       RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!evn) return RE::BSEventNotifyControl::kContinue;

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (pc && evn->holder && evn->holder != pc) {
        return RE::BSEventNotifyControl::kContinue;
    }

    /*
    if (refreshGuard_.load(std::memory_order_relaxed)) {
        return RE::BSEventNotifyControl::kContinue;
    }*/

    pendingRefresh_.store(true, std::memory_order_relaxed);
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(RE::InputEvent* const* evns,
                                                       RE::BSTEventSource<RE::InputEvent*>*) {
    if (!evns) {
        return RE::BSEventNotifyControl::kContinue;
    }

    bool axisChanged = false;

    auto setAxis = [&](float& axis, float newVal) {
        if (std::fabs(axis - newVal) > 1e-3f) {
            axis = newVal;
            axisChanged = true;
        }
    };

    for (auto e = *evns; e; e = e->next) {
        // --- Button-Events (Keyboard/Buttons) ---
        if (e->eventType == RE::INPUT_EVENT_TYPE::kButton) {
            auto* be = static_cast<RE::ButtonEvent*>(e);
            const bool isDown = (be->value > 0.0f);
            const bool isPress = isDown && (be->heldDownSecs == 0.0f);
            const RE::BSFixedString evName = be->userEvent;

            if (!sprintUserEvent_.empty() && evName == RE::BSFixedString(sprintUserEvent_.c_str())) {
                if (be->value > 0.0f) {
                    lastSprintMs_.store(NowMs(), std::memory_order_relaxed);
                } else {
                    lastSprintMs_.store(0, std::memory_order_relaxed);
                }
            }

            if (evName == "Forward") {
                setAxis(moveY_, isDown ? +1.0f : (moveY_ > 0.0f ? 0.0f : moveY_));
            } else if (evName == "Back") {
                setAxis(moveY_, isDown ? -1.0f : (moveY_ < 0.0f ? 0.0f : moveY_));
            } else if (evName == "Strafe Left" || evName == "StrafeLeft" || evName == "MoveLeft") {
                setAxis(moveX_, isDown ? -1.0f : (moveX_ < 0.0f ? 0.0f : moveX_));
            } else if (evName == "Strafe Right" || evName == "StrafeRight" || evName == "MoveRight") {
                setAxis(moveX_, isDown ? +1.0f : (moveX_ > 0.0f ? 0.0f : moveX_));
            }

            if (loading_.load(std::memory_order_relaxed)) {
                continue;
            }

            // Jogging-Toggle
            bool matched = false;
            if (!toggleUserEvent_.empty() && evName == RE::BSFixedString(toggleUserEvent_.c_str())) {
                matched = isPress;
            }
            if (!matched && toggleKeyCode_ != 0 && isPress && be->idCode == toggleKeyCode_) {
                matched = true;
            }

            if (matched) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastToggle_ >= toggleCooldown_) {
                    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
                        ClearDiagDeltaFor(pc);

                        const float before = CaseToDelta(pc);
                        joggingMode_ = !joggingMode_;
                        const float after = CaseToDelta(pc);
                        const float diff = after - before;

                        if (std::fabs(diff) > 0.01f) {
                            ModSpeedMult(pc, diff);
                        }
                        currentDelta = after;

                        if (Settings::enableDiagonalSpeedFix.load()) {
                            UpdateDiagonalPenalty(pc);
                        }
                        ForceSpeedRefresh(pc);
                    }
                    lastToggle_ = now;
                }
            }
        }

        // --- Analog-Thumbstick (Controller) ---
        else if (e->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
            auto* te = static_cast<RE::ThumbstickEvent*>(e);

            if (te) {
                float nx = te->xValue;
                float ny = te->yValue;

                // Deadzone
                const float dead = 0.12f;
                if (std::fabs(nx) < dead) nx = 0.f;
                if (std::fabs(ny) < dead) ny = 0.f;

                setAxis(moveX_, nx);
                setAxis(moveY_, ny);
            }
        }
    }

    if (axisChanged) {
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            if (Settings::enableDiagonalSpeedFix.load()) {
                if (UpdateDiagonalPenalty(pc)) {
                    pendingRefresh_.store(true, std::memory_order_relaxed);
                }
            } else {
                ClearDiagDeltaFor(pc);
            }
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

void SpeedController::ClearNPCState(std::uint32_t id) {
    smVelNPC_.erase(id);
    wantFilteredNPC_.erase(id);
    lastApplyNPCMs_.erase(id);
    lastSlopeNPCMs_.erase(id);
    prevNPCSprinting_.erase(id);
    prevNPCSneak_.erase(id);
    prevNPCDrawn_.erase(id);
    slopeDeltaNPC_.erase(id);
    lastPosNPC_.erase(id);
    groundDeltaNPC_.erase(id);
    currentDeltaNPC_.erase(id);
    attackDeltaNPC_.erase(id);
    diagDeltaNPC_.erase(id);
    diagResidualNPC_.erase(id);
    slopeResidualNPC_.erase(id);
    scaleResidualNPC_.erase(id);
    scaleDeltaNPC_.erase(id);
    pathNPC_.erase(id);
}

float SpeedController::ComputeEquippedWeight(const RE::Actor* a) const {
    if (!a) return 0.0f;
    auto getWeight = [](RE::TESForm* f) -> float {
        if (!f) return 0.0f;
        if (auto* bo = f->As<RE::TESBoundObject>()) {
            return bo->GetWeight();
        }
        return 0.0f;
    };

    if (auto* r = a->GetEquippedObject(false)) {
        return getWeight(r);
    }
    if (auto* l = a->GetEquippedObject(true)) {
        return getWeight(l);
    }
    return 0.0f;
}

float SpeedController::GetPlayerScaleSafe(const RE::Actor* a) const {
    if (!a) return 1.0f;
    float s = 1.0f;
    try {
        s = a->GetScale();
    } catch (...) {
    }
    if (s <= 0.01f || s > 10.0f) s = 1.0f;
    return s;
}

void SpeedController::UpdateAttackSpeed(RE::Actor* actor) {
    if (!actor) return;

    float& myDelta = AttackDeltaSlot(actor);

    if (std::fabs(myDelta) > 1e-6f) {
        if (auto* avo = actor->AsActorValueOwner()) {
            avo->ModActorValue(RE::ActorValue::kWeaponSpeedMult, -myDelta);
        }
        myDelta = 0.0f;
    }

    if (Settings::ignoreBeastForms.load() && IsInBeastForm(actor)) return;
    if (!Settings::attackSpeedEnabled) return;
    if (Settings::attackOnlyWhenDrawn && !IsWeaponDrawnByState(actor)) return;

    const float w = ComputeEquippedWeight(actor);
    const float scale = Settings::usePlayerScale ? GetPlayerScaleSafe(actor) : 1.0f;

    float target = Settings::attackBase + Settings::weightSlope * (w - Settings::weightPivot) +
                   (Settings::usePlayerScale ? (Settings::scaleSlope * (scale - 1.0f)) : 0.0f);

    target = std::max(Settings::minAttackMult.load(), std::min(Settings::maxAttackMult.load(), target));

    if (Settings::armorAffectsAttackSpeed.load()) {
        const float aw = ComputeArmorWeight(actor);
        target += Settings::armorWeightSlopeAtk.load() * (aw - Settings::armorWeightPivot.load());
    }

    auto* avo = actor->AsActorValueOwner();
    if (!avo) return;

    const float cur = avo->GetActorValue(RE::ActorValue::kWeaponSpeedMult);
    const float delta = target - cur;

    if (std::fabs(delta) > 1e-4f) {
        avo->ModActorValue(RE::ActorValue::kWeaponSpeedMult, delta);
        myDelta = delta;
    }
}

void SpeedController::UpdateBindingsFromSettings() {
    toggleKeyCode_ = Settings::toggleSpeedKey.load();
    toggleUserEvent_ = Settings::toggleSpeedEvent;
    sprintUserEvent_ = Settings::sprintEventName;

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (pc) {
        this->Apply();
        this->ForceSpeedRefresh(pc);
    }
}

void SpeedController::LoadToggleBindingFromJson() {
    toggleKeyCode_ = 0;
    toggleUserEvent_.clear();
    sprintUserEvent_ = Settings::sprintEventName;

    std::ifstream in(Settings::DefaultPath());
    if (!in.is_open()) return;
    try {
        nlohmann::json j;
        in >> j;

        if (j.contains("kToggleSpeedKey") && j["kToggleSpeedKey"].is_number_integer()) {
            toggleKeyCode_ = static_cast<uint32_t>(j["kToggleSpeedKey"].get<int>());
        }
        if (j.contains("kToggleSpeedEvent") && j["kToggleSpeedEvent"].is_string()) {
            toggleUserEvent_ = j["kToggleSpeedEvent"].get<std::string>();
        }
        if (j.contains("kSprintEventName") && j["kSprintEventName"].is_string()) {
            sprintUserEvent_ = j["kSprintEventName"].get<std::string>();
        }
    } catch (...) {
    }
}

void SpeedController::StartHeartbeat() {
    if (loading_.load(std::memory_order_relaxed)) return;
    if (run_) return;

    run_ = true;
    th_ = std::thread([this]() {
        using namespace std::chrono_literals;
        bool prevSprint = false;
        while (run_) {
            std::this_thread::sleep_for(33ms);
            SKSE::GetTaskInterface()->AddTask([this, &prevSprint]() {
                // Bail out completely while loading to avoid races and stale writes
                if (loading_.load(std::memory_order_relaxed)) {
                    return;
                }

                this->Apply();

                if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
                    this->UpdateAttackSpeed(pc);
                    this->UpdateSprintAnimRate(pc);

                    int n = postLoadNudges_.load(std::memory_order_relaxed);
                    if (n > 0) {
                        const uint64_t now = NowMs();
                        if (now >= postLoadGraceUntilMs_.load(std::memory_order_relaxed)) {
                            ForceSpeedRefresh(pc);
                            postLoadNudges_.store(n - 1, std::memory_order_relaxed);
                        }
                    }

                    if (pendingRefresh_.load(std::memory_order_relaxed)) {
                        if (this->ForceSpeedRefresh(pc)) {
                            pendingRefresh_.store(false, std::memory_order_relaxed);
                        }
                    }

                    const bool curSprint = IsSprintingLatched(pc);
                    if (curSprint != prevSprint) {
                        if (!curSprint) {
                            pc->SetGraphVariableFloat("fAnimSpeedMult", 1.0f);
                            pc->SetGraphVariableFloat("AnimSpeedMult", 1.0f);
                            pc->SetGraphVariableFloat("AnimSpeed", 1.0f);
                            pc->SetGraphVariableFloat("fSprintSpeedMult", 1.0f);
                            sprintAnimRate_ = 1.0f;
                        }
                        ClearDiagDeltaFor(pc);
                        ForceSpeedRefresh(pc);
                        prevSprint = curSprint;
                    }
                }
            });
        }
    });
    th_.detach();
}

void SpeedController::ForEachTargetActor(const std::function<void(RE::Actor*)>& fn) {
    if (!Settings::enableSpeedScalingForNPCs.load()) return;

    auto* pl = RE::ProcessLists::GetSingleton();
    if (!pl) return;

    for (auto& h : pl->highActorHandles) {
        RE::Actor* a = h.get().get();
        if (!a) continue;
        fn(a);
    }
}

void SpeedController::RevertAllNPCDeltas() {
    auto* pl = RE::ProcessLists::GetSingleton();
    if (!pl) {
        currentDeltaNPC_.clear();
        attackDeltaNPC_.clear();
        diagDeltaNPC_.clear();

        diagResidualNPC_.clear();
        slopeResidualNPC_.clear();
        diagResidualPlayer_ = 0.0f;
        slopeResidualPlayer_ = 0.0f;
        return;
    }

    for (auto& h : pl->highActorHandles) {
        RE::Actor* a = h.get().get();
        if (!a) continue;

        const auto id = GetID(a);
        auto* avo = a->AsActorValueOwner();
        if (!avo) continue;

        if (auto it = currentDeltaNPC_.find(id); it != currentDeltaNPC_.end()) {
            if (std::fabs(it->second) > 0.001f) {
                avo->ModActorValue(RE::ActorValue::kSpeedMult, -it->second);
                it->second = 0.0f;
            }
        }

        if (auto jt = attackDeltaNPC_.find(id); jt != attackDeltaNPC_.end()) {
            if (std::fabs(jt->second) > 1e-6f) {
                avo->ModActorValue(RE::ActorValue::kWeaponSpeedMult, -jt->second);
                jt->second = 0.0f;
            }
        }

        if (auto dt = diagDeltaNPC_.find(id); dt != diagDeltaNPC_.end()) {
            if (std::fabs(dt->second) > 0.001f) {
                avo->ModActorValue(RE::ActorValue::kSpeedMult, -dt->second);
                dt->second = 0.0f;
            }
        }

        if (auto st = scaleDeltaNPC_.find(id); st != scaleDeltaNPC_.end()) {
            if (std::fabs(st->second) > 0.001f) {
                avo->ModActorValue(RE::ActorValue::kSpeedMult, -st->second);
                st->second = 0.0f;
            }
        }

        ClearSlopeDeltaFor(a);
        ForceSpeedRefresh(a);
    }

    slopeDeltaNPC_.clear();
    lastPosNPC_.clear();
    currentDeltaNPC_.clear();
    attackDeltaNPC_.clear();
    diagDeltaNPC_.clear();
    scaleResidualNPC_.clear();
    scaleDeltaNPC_.clear();

    diagResidualNPC_.clear();
    slopeResidualNPC_.clear();
    diagResidualPlayer_ = 0.0f;
    slopeResidualPlayer_ = 0.0f;
}

float& SpeedController::AttackDeltaSlot(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return attackDelta_;
    return attackDeltaNPC_[GetID(a)];
}

float& SpeedController::CurrentDeltaSlot(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return currentDelta;
    return currentDeltaNPC_[GetID(a)];
}

void SpeedController::TryInitDrawnFromGraph() {
    if (initTried_) return;
    auto* pc = RE::PlayerCharacter::GetSingleton();
    (void)pc;
    initTried_ = true;
}

SpeedController::MoveCase SpeedController::ComputeCase(const RE::Actor* a) const {
    if (!a) return MoveCase::Default;
    if (Settings::noReductionInCombat && a->IsInCombat()) {
        return MoveCase::Combat;
    }
    if (a->IsSneaking()) {
        return MoveCase::Sneak;
    }
    if (IsWeaponDrawnByState(a)) {
        return MoveCase::Drawn;
    }
    return MoveCase::Default;
}

void SpeedController::OnPreLoadGame() {
    loading_.store(true, std::memory_order_relaxed);
    postLoadCleaned_.store(false, std::memory_order_relaxed);
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        SWE_Link::ClearSweat(pc);
    }
}

void SpeedController::OnPostLoadGame() { DoPostLoadCleanup(); }

void SpeedController::DoPostLoadCleanup() {
    if (postLoadCleaned_.exchange(true)) return;

    SKSE::GetTaskInterface()->AddTask([this]() {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        auto* avo = pc ? pc->AsActorValueOwner() : nullptr;

        loading_.store(true, std::memory_order_relaxed);

        if (pc && avo) {
            if (snapshotLoaded_.load(std::memory_order_relaxed)) {
                const float snapCur = currentDelta;
                const float snapDiag = diagDelta_;
                const bool snapJog = joggingMode_;
                const float snapSlope = slopeDeltaPlayer_;

                ClearSlopeDeltaFor(pc);
                RevertDeltasFor(pc);

                float base = savedBaselineSM_;
                if (!std::isfinite(base)) {
                    const float curAfterRevert = avo->GetActorValue(RE::ActorValue::kSpeedMult);
                    base = curAfterRevert;
                }
                {
                    const float cur = avo->GetActorValue(RE::ActorValue::kSpeedMult);
                    avo->ModActorValue(RE::ActorValue::kSpeedMult, base - cur);
                }
                if (std::fabs(snapSlope) > 1e-6f) {
                    avo->ModActorValue(RE::ActorValue::kSpeedMult, snapSlope);
                    slopeDeltaPlayer_ = snapSlope;
                }

                if (std::fabs(snapCur) > 1e-6f) {
                    avo->ModActorValue(RE::ActorValue::kSpeedMult, snapCur);
                    currentDelta = snapCur;
                }
                if (std::fabs(snapDiag) > 1e-6f) {
                    avo->ModActorValue(RE::ActorValue::kSpeedMult, snapDiag);
                    diagDelta_ = snapDiag;
                }
                joggingMode_ = snapJog;

                snapshotLoaded_.store(false, std::memory_order_relaxed);

                ForceSpeedRefresh(pc);
            } else {
                RevertDeltasFor(pc);
                currentDelta = 0.0f;
                diagDelta_ = 0.0f;
                attackDelta_ = 0.0f;
            }

            moveX_ = 0.0f;
            moveY_ = 0.0f;
            lastApplyPlayerMs_ = NowMs();
        }

        currentDeltaNPC_.clear();
        attackDeltaNPC_.clear();
        diagDeltaNPC_.clear();

        postLoadGraceUntilMs_.store(NowMs() + 800, std::memory_order_relaxed);
        postLoadNudges_.store(3, std::memory_order_relaxed);
        pendingRefresh_.store(true, std::memory_order_relaxed);

        loading_.store(false, std::memory_order_relaxed);
    });
}

bool SpeedController::UpdateDiagonalPenalty(RE::Actor* a) {
    if (!a) return false;

    float x = 0.f, y = 0.f;
    if (a == RE::PlayerCharacter::GetSingleton()) {
        x = moveX_;
        y = moveY_;
    } else {
        (void)TryGetMoveAxesFromGraph(a, x, y);
    }
    return UpdateDiagonalPenalty(a, x, y);
}

bool SpeedController::UpdateDiagonalPenalty(RE::Actor* a, float inX, float inY) {
    if (!a) return false;

    // f = max(|x|,|y|) / sqrt(x^2 + y^2)   (<= 1)
    const float ax = std::fabs(inX);
    const float ay = std::fabs(inY);
    const float mag = std::sqrt(inX * inX + inY * inY);
    const float maxc = std::max(ax, ay);

    float f = 1.0f;
    if (mag > 1e-4f && maxc > 0.0f) f = std::min(1.0f, maxc / mag);

    auto* avo = a->AsActorValueOwner();
    if (!avo) return false;

    float& slot = DiagDeltaSlot(a);

    const float curSM = avo->GetActorValue(RE::ActorValue::kSpeedMult);
    const float floor = Settings::minFinalSpeedMult.load();
    const bool sprinting = IsSprintingLatched(a);

    const float curNoDiag = curSM - slot;
    float headroom = std::max(0.0f, curNoDiag - floor);

    float newDiag = 0.0f;
    if (f < 0.999f) {
        newDiag = headroom * (f - 1.0f);  // <= 0
        if (sprinting) newDiag *= 0.5f;
    } else {
        newDiag = 0.0f;
    }

    const float delta = newDiag - slot;

    float& acc = DiagResidualSlot(a);
    acc += delta;

    static constexpr float kDiagGran = 5e-4f;
    if (std::fabs(acc) >= kDiagGran) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, acc);
        slot += acc;
        acc = 0.0f;
        return true;
    }
    return false;
}

float SpeedController::CaseToDelta(const RE::Actor* a) const {
    const MoveCase c = ComputeCase(a);
    float base = 0.0f;
    switch (c) {
        case MoveCase::Combat:
            base = 0.0f;
            break;
        case MoveCase::Drawn:
            base = -Settings::reduceDrawn.load();
            break;
        case MoveCase::Sneak:
            base = -Settings::reduceSneak.load();
            break;
        default:
            base = -(joggingMode_ ? Settings::reduceJoggingOutOfCombat.load() : Settings::reduceOutOfCombat.load());
            break;
    }

    if (Settings::locationMode != Settings::LocationMode::Ignore &&
        (Settings::locationAffects == Settings::LocationAffects::AllStates ||
         (Settings::locationAffects == Settings::LocationAffects::DefaultOnly && (c == MoveCase::Default)))) {
        RE::BGSLocation* loc = nullptr;
        if (a) {
            if (auto* l = a->GetCurrentLocation())
                loc = l;
            else if (auto* cell = a->GetParentCell())
                loc = cell->GetLocation();
        }
        if (loc) {
            // Specific
            for (auto& fs : Settings::reduceInLocationSpecific) {
                if (auto* l = LookupForm<RE::BGSLocation>(fs.plugin, fs.id); l && l == loc) {
                    base = (Settings::locationMode == Settings::LocationMode::Replace) ? -fs.value : base - fs.value;
                    goto done_loc;
                }
            }
            // Types
            if (auto* kwSet = loc->keywords) {
                for (auto& fs : Settings::reduceInLocationType) {
                    if (auto* kw = LookupForm<RE::BGSKeyword>(fs.plugin, fs.id); kw) {
                        if (loc->HasKeyword(kw)) {
                            base = (Settings::locationMode == Settings::LocationMode::Replace) ? -fs.value
                                                                                               : base - fs.value;
                            break;
                        }
                    }
                }
            }
        }
    }
done_loc:;
    if (Settings::weatherEnabled.load() &&
        (Settings::weatherAffects == Settings::WeatherAffects::AllStates ||
         (Settings::weatherAffects == Settings::WeatherAffects::DefaultOnly && (c == MoveCase::Default)))) {
        if (auto w = ComputeWeatherValue(a)) {
            base = (Settings::weatherMode == Settings::WeatherMode::Replace) ? -(*w) : base - (*w);
        }
    }

    bool sprint = IsSprintingLatched(a);
    if (sprint) {
        const MoveCase c = ComputeCase(a);
        if (c != MoveCase::Combat || Settings::sprintAffectsCombat.load()) {
            base += Settings::increaseSprinting.load();
        }
    }
    if (Settings::armorAffectsMovement.load()) {
        const float aw = ComputeArmorWeight(a);
        float armDelta = Settings::armorWeightSlopeSM.load() * (aw - Settings::armorWeightPivot.load());
        const float lo = Settings::armorMoveMin.load();
        const float hi = Settings::armorMoveMax.load();
        if (lo <= hi) {
            armDelta = std::clamp(armDelta, lo, hi);
        } else {
            armDelta = std::clamp(armDelta, hi, lo);
        }
        base += armDelta;
    }

    {
        float vit = 0.0f;
        vit += LinearVitalPenaltyPct(a, RE::ActorValue::kHealth, Settings::healthEnabled.load(),
                                     Settings::healthThresholdPct.load(), Settings::healthReducePct.load(),
                                     Settings::healthSmoothWidthPct.load());
        vit += LinearVitalPenaltyPct(a, RE::ActorValue::kStamina, Settings::staminaEnabled.load(),
                                     Settings::staminaThresholdPct.load(), Settings::staminaReducePct.load(),
                                     Settings::staminaSmoothWidthPct.load());
        vit += LinearVitalPenaltyPct(a, RE::ActorValue::kMagicka, Settings::magickaEnabled.load(),
                                     Settings::magickaThresholdPct.load(), Settings::magickaReducePct.load(),
                                     Settings::magickaSmoothWidthPct.load());
        base += vit;
    }

    if (Settings::scaleCompEnabled.load() && a && Settings::scaleCompMode == Settings::ScaleCompMode::Additive) {
        const float s = GetPlayerScaleSafe(a);
        if (!Settings::scaleCompOnlyBelowOne.load() || s < 1.0f) {
            base += Settings::scaleCompPerUnitSM.load() * (1.0f - s);
        }
    }
    return base;
}

void SpeedController::RefreshNow() {
    Apply();
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        ForceSpeedRefresh(pc);
    }
}

void SpeedController::Apply() {
    if (NowMs() < postLoadGraceUntilMs_.load(std::memory_order_relaxed)) return;
    if (loading_.load(std::memory_order_relaxed)) return;
    if (refreshGuard_.load(std::memory_order_relaxed)) return;

    const bool cur = Settings::enableSpeedScalingForNPCs.load();
    if (prevAffectNPCs_ && !cur) {
        RevertAllNPCDeltas();
    }
    prevAffectNPCs_ = cur;

    RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
    if (!pc) return;

    ApplyFor(pc);

    static uint64_t lastNpcApplyMs = 0;
    const uint64_t now = NowMs();
    const int gapMs = std::max(0, Settings::eventDebounceMs.load());
    const bool npcThrottled = (gapMs > 0 && lastNpcApplyMs != 0 && (now - lastNpcApplyMs) < (uint64_t)gapMs);

    if (npcThrottled) {
        UpdateSlopeTickNPCsOnly();
        return;
    }
    lastNpcApplyMs = now;

    ForEachTargetActor([&](RE::Actor* a) {
        if (a != pc) ApplyFor(a);
    });
}

void SpeedController::RevertMovementDeltasFor(RE::Actor* a, bool clearSlope) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;

    // Movement-Delta
    float& moveDelta = CurrentDeltaSlot(a);
    if (std::fabs(moveDelta) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -moveDelta);
        moveDelta = 0.0f;
    }

    // Diagonal-Delta
    float& diag = DiagDeltaSlot(a);
    if (std::fabs(diag) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -diag);
        diag = 0.0f;
    }

    // Slope-Delta
    if (clearSlope) {
        ClearSlopeDeltaFor(a);
    }

    ForceSpeedRefresh(a);
}

void SpeedController::ApplyFor(RE::Actor* a) {
    if (!a) return;

    if (Settings::ignoreBeastForms.load() && IsInBeastForm(a)) {
        RevertDeltasFor(a);
        return;
    }

    const bool isPlayer = (a == RE::PlayerCharacter::GetSingleton());
    if (!isPlayer) {
        if (!IsWithinNPCProcRadius(a)) {
            RevertDeltasFor(a);
            ClearNPCState(GetID(a));
            return;
        }
    }

    const auto id = GetID(a);
    float want = CaseToDelta(a);

    if (!isPlayer) {
        const float pct = std::clamp(Settings::npcPercentOfPlayer.load(), 0.0f, 200.0f) * 0.01f;
        want *= pct;
    }

    float& cur = isPlayer ? currentDelta : currentDeltaNPC_[id];
    uint64_t& t = isPlayer ? lastApplyPlayerMs_ : lastApplyNPCMs_[id];

    uint64_t now = NowMs();
    float dt = 0.0f;
    if (t == 0) {
        dt = 1.0f / 60.0f;
    } else {
        dt = std::max(0.0f, (now - t) / 1000.0f);
    }
    t = now;

    bool smoothing = Settings::smoothingEnabled.load() && (isPlayer || Settings::smoothingAffectsNPCs.load());

    // Flip-Logic: Always invalidate diagonal penalty
    const bool curSprint = IsSprintingLatched(a);
    const bool curSneak = a->IsSneaking();
    const bool curDrawn = IsWeaponDrawnByState(a);
    bool flip = false;

    if (isPlayer) {
        flip = (curSprint != prevPlayerSprinting_) || (curSneak != prevPlayerSneak_) || (curDrawn != prevPlayerDrawn_);
        prevPlayerSprinting_ = curSprint;
        prevPlayerSneak_ = curSneak;
        prevPlayerDrawn_ = curDrawn;
    } else {
        auto id = GetID(a);
        bool& pS = prevNPCSprinting_[id];
        bool& pN = prevNPCSneak_[id];
        bool& pD = prevNPCDrawn_[id];
        flip = (curSprint != pS) || (curSneak != pN) || (curDrawn != pD);
        pS = curSprint;
        pN = curSneak;
        pD = curDrawn;
    }

    if (flip) {
        // immediately reject Diagonal-Delta, so Headroom/Clamp fits exactly
        ClearDiagDeltaFor(const_cast<RE::Actor*>(a));
        if (Settings::smoothingBypassOnStateChange.load() && smoothing) {
            smoothing = false;
            RevertMovementDeltasFor(a, false);
        }
        ForceSpeedRefresh(a);
    }

    float newDelta = want;
    if (smoothing) {
        newDelta = SmoothCombined(cur, want, dt);
    }

    float diff = newDelta - cur;

    if (auto* avo = a->AsActorValueOwner()) {
        const float floor = Settings::minFinalSpeedMult.load();

        float& curSlot = isPlayer ? currentDelta : currentDeltaNPC_[id];
        float& diagSlot = DiagDeltaSlot(a);
        float& slopeSlot = SlopeDeltaSlot(a);
        float& scaleSlot = ScaleDeltaSlot(a);

        const float curSM = avo->GetActorValue(RE::ActorValue::kSpeedMult);
        const float baseNoUs = curSM - curSlot - diagSlot - slopeSlot;

        bool wantDiag =
            isPlayer ? Settings::enableDiagonalSpeedFix.load() : Settings::enableDiagonalSpeedFixForNPCs.load();

        float predictedDiag = 0.0f;
        if (wantDiag) {
            float x = 0.f, y = 0.f;
            const bool sprinting = IsSprintingLatched(a);
            if (isPlayer) {
                x = moveX_;
                y = moveY_;
            } else {
                (void)TryGetMoveAxesFromGraph(a, x, y);
            }
            predictedDiag = PredictDiagonalPenalty(baseNoUs + newDelta, floor, x, y, sprinting);
        }

        float expectedNoScaleFinal = baseNoUs + newDelta + predictedDiag + slopeSlot;

        float sFactor = 1.0f;
        if (Settings::scaleCompEnabled.load() && Settings::scaleCompMode == Settings::ScaleCompMode::Inverse) {
            const float s = GetPlayerScaleSafe(a);
            if (!Settings::scaleCompOnlyBelowOne.load() || s < 1.0f) sFactor = 1.0f / s;
        }

        float expectedFinal = expectedNoScaleFinal * sFactor;


        if (Settings::slopeClampEnabled.load()) {
            const float lo = Settings::slopeMinFinal.load();
            const float hi = Settings::slopeMaxFinal.load();
            if (expectedFinal < lo) {
                newDelta += (lo - expectedFinal);
                expectedFinal = lo;
            } else if (expectedFinal > hi) {
                newDelta -= (expectedFinal - hi);
                expectedFinal = hi;
            }
        }

        if (expectedFinal < floor) {
            const float needed = floor - expectedFinal;
            newDelta += needed;
            expectedNoScaleFinal += needed;
            expectedFinal = expectedNoScaleFinal * sFactor;
        }

        diff = newDelta - curSlot;
    }

    bool moveChanged = false;
    if (std::fabs(diff) > 0.0001f) {
        ModSpeedMult(a, diff);
        cur = newDelta;
        moveChanged = true;
    }

    const bool wantDiag =
        (isPlayer ? Settings::enableDiagonalSpeedFix.load() : Settings::enableDiagonalSpeedFixForNPCs.load());

    bool diagChanged = false;
    if (wantDiag) {
        diagChanged = UpdateDiagonalPenalty(a);
    } else {
        ClearDiagDeltaFor(a);
    }

    const bool slopeChanged = UpdateSlopePenalty(a, dt);

    bool scaleChanged = false;
    if (Settings::scaleCompEnabled.load() && Settings::scaleCompMode == Settings::ScaleCompMode::Inverse) {
        if (auto* avo2 = a->AsActorValueOwner()) {
            float x = 0.f, y = 0.f;
            const bool sprinting = IsSprintingLatched(a);
            if (isPlayer) {
                x = moveX_;
                y = moveY_;
            } else {
                (void)TryGetMoveAxesFromGraph(a, x, y);
            }

            const float floor = Settings::minFinalSpeedMult.load();

            float& curSlot2 = isPlayer ? currentDelta : currentDeltaNPC_[id];
            float& diagSlot2 = DiagDeltaSlot(a);
            float& slopeSlot2 = SlopeDeltaSlot(a);

            const float curSM2 = avo2->GetActorValue(RE::ActorValue::kSpeedMult);
            const float baseNoUs2 = curSM2 - curSlot2 - diagSlot2 - slopeSlot2;
            const float predictedDiag2 = PredictDiagonalPenalty(baseNoUs2 + curSlot2, floor, x, y, sprinting);
            const float noScaleFinalPreview = baseNoUs2 + curSlot2 + predictedDiag2 + slopeSlot2;

            scaleChanged = UpdateScaleCompDelta(a, noScaleFinalPreview);
        }
    } else {
        ClearScaleDeltaFor(a);
    }

    if (isPlayer) {
        if (slopeChanged) {
            pendingRefresh_.store(true, std::memory_order_relaxed);
        }
    } else {
        if (moveChanged || diagChanged || slopeChanged || scaleChanged) {
            ForceSpeedRefresh(a);
        }
    }

    ClampSpeedFloorTracked(a);
}

void SpeedController::ModSpeedMult(RE::Actor* actor, float delta) {
    if (!actor) return;
    RE::ActorValueOwner* avo = actor->AsActorValueOwner();
    avo->ModActorValue(RE::ActorValue::kSpeedMult, delta);
}

bool SpeedController::ForceSpeedRefresh(RE::Actor* actor) {
    if (!actor) return false;
    if (loading_.load(std::memory_order_relaxed)) return false;

    const uint64_t now = NowMs();
    if (now < postLoadGraceUntilMs_.load(std::memory_order_relaxed)) {
        pendingRefresh_.store(true, std::memory_order_relaxed);
        return false;
    }

    const bool isPlayer = (actor == RE::PlayerCharacter::GetSingleton());
    uint64_t prev =
        isPlayer ? lastRefreshPlayerMs_.load(std::memory_order_relaxed) : lastRefreshNPCMs_[actor->GetFormID()];

    if (prev != 0 && (now - prev) < 25) {
        return false;
    }

    if (isPlayer) {
        lastRefreshPlayerMs_.store(now, std::memory_order_relaxed);
    } else {
        lastRefreshNPCMs_[actor->GetFormID()] = now;
    }

    if (auto* avo = actor->AsActorValueOwner()) {
        const float before = avo->GetActorValue(RE::ActorValue::kCarryWeight);

        refreshGuard_.store(true, std::memory_order_relaxed);
        constexpr float eps = kRefreshEps;
        avo->ModActorValue(RE::ActorValue::kCarryWeight, +eps);
        avo->ModActorValue(RE::ActorValue::kCarryWeight, -eps);
        refreshGuard_.store(false, std::memory_order_relaxed);

        const float after = avo->GetActorValue(RE::ActorValue::kCarryWeight);
        const float diff = after - before;
        const bool looksLikeOurNudge = std::fabs(std::fabs(diff) - eps) < 0.005f;
        const bool suspiciousZero = (after <= 0.01f && before > 0.01f);
        if ((looksLikeOurNudge || suspiciousZero) && std::fabs(diff) > 1e-6f) {
            avo->ModActorValue(RE::ActorValue::kCarryWeight, -diff);
        }
    }
    return true;
}

bool SpeedController::IsWeaponDrawnByState(const RE::Actor* a) {
    if (!a) return false;
    const RE::ActorState* st = a->AsActorState();
    if (!st) return false;

    const RE::WEAPON_STATE ws = st->actorState2.weaponState;
    switch (ws) {
        case RE::WEAPON_STATE::kDrawn:
        case RE::WEAPON_STATE::kDrawing:
        case RE::WEAPON_STATE::kWantToDraw:
            return true;
        case RE::WEAPON_STATE::kSheathed:
        case RE::WEAPON_STATE::kSheathing:
        case RE::WEAPON_STATE::kWantToSheathe:
            return false;
        default:
            break;
    }
    auto right = a->GetEquippedObject(false);
    auto left = a->GetEquippedObject(true);
    const bool any =
        (right && (right->IsWeapon() || right->Is(RE::FormType::Light) || right->Is(RE::FormType::Scroll))) ||
        (left && (left->IsWeapon() || left->Is(RE::FormType::Light) || left->Is(RE::FormType::Scroll)));
    return any;
}

bool SpeedController::IsSprintingByGraph(const RE::Actor* a) {
    if (!a) return false;
    bool b = false;
    if (a->GetGraphVariableBool("IsSprinting", b) && b) return true;
    if (a->GetGraphVariableBool("bIsSprinting", b) && b) return true;
    if (a->GetGraphVariableBool("bSprint", b) && b) return true;
    return false;
}

bool SpeedController::IsInBeastForm(const RE::Actor* a) {
    if (!a) return false;

    if (RaceIs(a, "WerewolfBeastRace")) return true;
    if (RaceIs(a, "DLC1VampireBeastRace")) return true;

    bool b = false;
    if ((a->GetGraphVariableBool("IsWerewolf", b) && b) || (a->GetGraphVariableBool("bIsWerewolf", b) && b) ||
        (a->GetGraphVariableBool("IsVampireLord", b) && b) || (a->GetGraphVariableBool("bIsVampireLord", b) && b)) {
        return true;
    }
    return false;
}

void SpeedController::UpdateSlopeTickOnly() {
    // Player
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        const uint64_t now = NowMs();
        float dt = (lastSlopePlayerMs_ == 0) ? (1.0f / 60.0f) : std::max(0.0f, (now - lastSlopePlayerMs_) / 1000.0f);
        lastSlopePlayerMs_ = now;

        bool pChanged = UpdateSlopePenalty(pc, dt);
        if (pChanged) {
            pendingRefresh_.store(true, std::memory_order_relaxed);
        }
        ClampSpeedFloorTracked(pc);
    }

    // NPCs
    if (Settings::enableSpeedScalingForNPCs.load()) {
        auto* pl = RE::ProcessLists::GetSingleton();
        if (pl) {
            for (auto& h : pl->highActorHandles) {
                RE::Actor* a = h.get().get();
                if (!a) continue;
                const auto id = GetID(a);

                if (!IsWithinNPCProcRadius(a)) {
                    RevertDeltasFor(a);
                    ClearNPCState(id);
                    continue;
                }

                const uint64_t now = NowMs();
                uint64_t& t = lastSlopeNPCMs_[id];
                float dt = (t == 0) ? (1.0f / 60.0f) : std::max(0.0f, (now - t) / 1000.0f);
                t = now;

                bool aChanged = UpdateSlopePenalty(a, dt);
                if (aChanged) {
                    ForceSpeedRefresh(a);
                }
                ClampSpeedFloorTracked(a);
            }
        }
    }
}

void SpeedController::UpdateSlopeTickNPCsOnly() {
    if (!Settings::enableSpeedScalingForNPCs.load()) return;
    auto* pl = RE::ProcessLists::GetSingleton();
    if (!pl) return;

    for (auto& h : pl->highActorHandles) {
        RE::Actor* a = h.get().get();
        if (!a) continue;

        const auto id = GetID(a);

        if (!IsWithinNPCProcRadius(a)) {
            RevertDeltasFor(a);
            ClearNPCState(id);
            continue;
        }

        const uint64_t now = NowMs();
        uint64_t& t = lastSlopeNPCMs_[id];
        float dt = (t == 0) ? (1.0f / 60.0f) : std::max(0.0f, (now - t) / 1000.0f);
        t = now;

        bool changed = UpdateSlopePenalty(a, dt);
        if (changed) {
            ForceSpeedRefresh(a);
        }
        ClampSpeedFloorTracked(a);
    }
}

void SpeedController::RevertDeltasFor(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;

    float& moveDelta = CurrentDeltaSlot(a);
    if (std::fabs(moveDelta) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -moveDelta);
        moveDelta = 0.0f;
    }

    float& atkDelta = AttackDeltaSlot(a);
    if (std::fabs(atkDelta) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kWeaponSpeedMult, -atkDelta);
        atkDelta = 0.0f;
    }

    float& diag = DiagDeltaSlot(a);
    if (std::fabs(diag) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -diag);
        diag = 0.0f;
    }

    float& sc = ScaleDeltaSlot(a);
    if (std::fabs(sc) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -sc);
        sc = 0.0f;
    }

    ClearSlopeDeltaFor(a);
    ForceSpeedRefresh(a);

    DiagResidualSlot(a) = 0.0f;
    SlopeResidualSlot(a) = 0.0f;
    ScaleResidualSlot(a) = 0.0f;
}

bool SpeedController::TryGetMoveAxesFromGraph(const RE::Actor* a, float& outX, float& outY) const {
    if (!a) return false;
    float x = 0.0f, y = 0.0f;
    bool ok = false;

    if (a->GetGraphVariableFloat("MoveX", x)) ok = true;
    if (a->GetGraphVariableFloat("MoveY", y)) ok = true;

    float t = 0.0f;
    if (!ok) {
        if (a->GetGraphVariableFloat("SpeedSide", x)) ok = true;
        if (a->GetGraphVariableFloat("SpeedForward", y)) ok = true;
    }
    if (!ok) {
        if (a->GetGraphVariableFloat("Strafe", x)) ok = true;
        if (a->GetGraphVariableFloat("Forward", y)) ok = true;
    }

    if (!ok) return false;
    outX = x;
    outY = y;
    return (std::fabs(outX) > 1e-4f) || (std::fabs(outY) > 1e-4f);
}

float& SpeedController::DiagDeltaSlot(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return diagDelta_;
    return diagDeltaNPC_[GetID(a)];
}

float& SpeedController::ScaleDeltaSlot(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return scaleDeltaPlayer_;
    return scaleDeltaNPC_[GetID(a)];
}

void SpeedController::ClearScaleDeltaFor(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;
    float& slot = ScaleDeltaSlot(a);
    if (std::fabs(slot) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -slot);
        slot = 0.0f;
    }
    ScaleResidualSlot(a) = 0.0f;
}

bool SpeedController::UpdateScaleCompDelta(RE::Actor* a, float predictedNoScaleFinal) {
    if (!a) return false;
    if (!Settings::scaleCompEnabled.load()) {
        ClearScaleDeltaFor(a);
        return false;
    }
    if (Settings::scaleCompMode != Settings::ScaleCompMode::Inverse) {
        ClearScaleDeltaFor(a);
        return false;
    }

    const float s = GetPlayerScaleSafe(a);
    if (Settings::scaleCompOnlyBelowOne.load() && s >= 1.0f) {
        // No inverse correction above 1.0
        ClearScaleDeltaFor(a);
        return false;
    }
    const float factor = 1.0f / std::max(0.01f, s);
    const float K = factor - 1.0f;

    auto* avo = a->AsActorValueOwner();
    if (!avo) return false;

    float& slot = ScaleDeltaSlot(a);
    const float target = K * predictedNoScaleFinal;  // make final = noScale * factor
    const float delta = target - slot;

    float& acc = ScaleResidualSlot(a);
    acc += delta;

    static constexpr float kGran = 5e-4f;
    if (std::fabs(acc) >= kGran) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, acc);
        slot += acc;
        acc = 0.0f;
        return true;
    }
    return false;
}

void SpeedController::ClearDiagDeltaFor(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;
    float& slot = DiagDeltaSlot(a);
    if (std::fabs(slot) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -slot);
        slot = 0.0f;
    }
    DiagResidualSlot(a) = 0.0f;
}

std::deque<PathSample>& SpeedController::PathBuf(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return pathPlayer_;
    return pathNPC_[GetID(a)];
}

void SpeedController::ClearPathFor(RE::Actor* a) {
    auto& q = PathBuf(a);
    q.clear();
}

bool SpeedController::IsSprintingLatched(const RE::Actor* a) const {
    if (!a) return false;
    if (IsSprintingByGraph(a)) return true;
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) {
        const uint64_t now = NowMs();
        const uint64_t last = lastSprintMs_.load(std::memory_order_relaxed);
        if (last != 0 && (now - last) <= kSprintLatchMs) return true;
    }
    return false;
}

void SpeedController::PushPathSample(RE::Actor* a, const RE::NiPoint3& pos, uint64_t nowMs) {
    auto& q = PathBuf(a);
    float sxy = 0.0f;
    if (!q.empty()) {
        const auto& last = q.back();
        const float dx = pos.x - last.x;
        const float dy = pos.y - last.y;
        const float dxy = std::sqrt(dx * dx + dy * dy);
        if (dxy < Settings::slopeMinXYPerFrame.load()) {
            sxy = last.sxy;
        } else {
            sxy = last.sxy + dxy;
        }
    }
    q.push_back(PathSample{pos.x, pos.y, pos.z, sxy, nowMs});

    const uint64_t maxAgeMs = static_cast<uint64_t>(std::max(0.f, Settings::slopeMaxHistorySec.load()) * 1000.f);
    while (!q.empty() && (nowMs - q.front().tMs) > maxAgeMs) {
        q.pop_front();
    }
}

void SpeedController::ClampSpeedFloorTracked(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;

    const float floor = Settings::minFinalSpeedMult.load();

    float& moveSlot = CurrentDeltaSlot(a);
    float& diagSlot = DiagDeltaSlot(a);
    float& slopeSlot = SlopeDeltaSlot(a);

    const float cur = avo->GetActorValue(RE::ActorValue::kSpeedMult);

    const float eps = 1e-4f;
    if (cur < floor - eps) {
        const float need = floor - cur;
        avo->ModActorValue(RE::ActorValue::kSpeedMult, need);
        moveSlot += need;
    }
}

bool SpeedController::ComputePathSlopeDeg(RE::Actor* a, float lookbackUnits, float maxAgeSec, float& outDeg) {
    auto& q = PathBuf(a);
    if (q.size() < 2) return false;

    const auto& cur = q.back();
    const float wantSxy = std::max(0.f, cur.sxy - std::max(lookbackUnits, 0.0f));

    const PathSample* ref = nullptr;
    for (int i = static_cast<int>(q.size()) - 1; i >= 0; --i) {
        if (q[static_cast<size_t>(i)].sxy <= wantSxy) {
            ref = &q[static_cast<size_t>(i)];
            break;
        }
    }
    if (!ref) ref = &q.front();

    const float dxy = std::max(1e-3f, cur.sxy - ref->sxy);
    const float dz = cur.z - ref->z;
    outDeg = std::clamp(std::atan2(dz, dxy) * 57.29578f, -85.0f, 85.0f);
    return true;
}

float SpeedController::ComputeArmorWeight(const RE::Actor* a) const {
    if (!a) return 0.0f;

    float maxW = 0.0f, sumW = 0.0f;

    auto* ac = const_cast<RE::Actor*>(a);

    std::unordered_set<RE::FormID> seen;

    for (std::uint32_t i = 0; i <= 31; ++i) {
        const auto slot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(i);
        RE::TESObjectARMO* armo = ac->GetWornArmor(slot, true);
        if (!armo) continue;

        const RE::FormID fid = armo->GetFormID();
        if (!seen.insert(fid).second) continue;

        const RE::TESBoundObject* bo = armo;
        const float w = bo ? bo->GetWeight() : 0.0f;

        sumW += w;
        if (w > maxW) maxW = w;
    }

    return Settings::useMaxArmorWeight.load() ? maxW : sumW;
}

bool SpeedController::GetJoggingMode() const { return joggingMode_; }
void SpeedController::SetJoggingMode(bool b) { joggingMode_ = b; }

float SpeedController::GetCurrentDelta() const { return currentDelta; }
void SpeedController::SetCurrentDelta(float d) { currentDelta = d; }
