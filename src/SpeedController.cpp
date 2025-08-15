#include "SpeedController.h"

#include <chrono>
#include <cmath>

#include "SKSE/Logger.h"

#include "nlohmann/json.hpp"
using nlohmann::json;

using namespace RE;

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


namespace {
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
    if (!IsSprintingByGraph(a)) {
        TrySetAnyGraphVarFloat(a, {"fAnimSpeedMult", "AnimSpeedMult", "AnimSpeed", "fSprintSpeedMult"}, 1.0f);
        sprintAnimRate_ = 1.0f;
        return;
    }

    auto* avo = a->AsActorValueOwner();
    if (!avo) return;

    const float curSM = std::max(1.0f, avo->GetActorValue(RE::ActorValue::kSpeedMult));
    float ratio = curSM / 100.0f;

    if (Settings::onlySlowDown.load()) ratio = std::min(ratio, 1.0f);

    ratio = std::clamp(ratio, Settings::sprintAnimMin.load(), Settings::sprintAnimMax.load());

    const uint64_t nowMs = NowMs();
    static uint64_t lastMs = nowMs;
    const float dt = (lastMs == 0) ? (1.0f / 60.0f) : std::max(0.0f, (nowMs - lastMs) / 1000.0f);
    lastMs = nowMs;

    sprintAnimRate_ = SmoothSprintAnim(sprintAnimRate_, ratio, dt);

    TrySetAnyGraphVarFloat(a,
                           {
                               "fAnimSpeedMult",
                               "AnimSpeedMult", "AnimSpeed",
                               "fSprintSpeedMult"
                           },
                           sprintAnimRate_);
}


RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESEquipEvent* evn,
                                                       RE::BSTEventSource<RE::TESEquipEvent>*) {
    if (!evn) return RE::BSEventNotifyControl::kContinue;

    RE::TESObjectREFR* ref = evn->actor.get();
    RE::Actor* a = ref ? ref->As<RE::Actor>() : nullptr;
    if (!a) return RE::BSEventNotifyControl::kContinue;

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc || Settings::enableSpeedScalingForNPCs.load()) {
        UpdateAttackSpeed(a);
    }
    return RE::BSEventNotifyControl::kContinue;
}

float& SpeedController::SlopeDeltaSlot(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return slopeDeltaPlayer_;
    return slopeDeltaNPC_[a->GetFormID()];
}

void SpeedController::ClearSlopeDeltaFor(RE::Actor* a) {
    float& slot = SlopeDeltaSlot(a);
    if (std::fabs(slot) > 1e-4f) {
        if (auto* avo = a->AsActorValueOwner()) {
            avo->ModActorValue(RE::ActorValue::kSpeedMult, -slot);
        }
        slot = 0.0f;
    }
    ClearPathFor(a);
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) {
        lastSlopePlayerMs_ = 0;
    } else {
        lastSlopeNPCMs_.erase(a->GetFormID());
    }
}


bool SpeedController::UpdateSlopePenalty(RE::Actor* a, float dt) {
    if (!a || dt <= 0.f) return false;
    if (!Settings::slopeEnabled.load()) return false;
    if (a != RE::PlayerCharacter::GetSingleton() && !Settings::slopeAffectsNPCs.load()) return false;

    const uint64_t nowMs = NowMs();
    const auto pos = a->GetPosition();

    // Pfad aktualisieren
    PushPathSample(a, pos, nowMs);

    float slopeDeg = 0.0f;
    bool haveSlope = false;

    if (Settings::slopeMethod.load() == 1) {
        // Weg-basierte Steigung
        haveSlope =
            ComputePathSlopeDeg(a, Settings::slopeLookbackUnits.load(), Settings::slopeMaxHistorySec.load(), slopeDeg);
    } else {
        // Fallback: Instant (dein alter Ansatz)
        // -> optional: du kannst hier lastPosPlayer_/NPC_ weiterverwenden,
        //    aber mit Pfad brauchst du die alten lastPos* nicht mehr.
        haveSlope = false;
    }

    float want = 0.0f;
    if (haveSlope) {
        if (slopeDeg > 0.0f) {
            want -= Settings::slopeUphillPerDeg.load() * slopeDeg;
        } else if (slopeDeg < 0.0f) {
            want += Settings::slopeDownhillPerDeg.load() * (-slopeDeg);
        }
        const float absMax = Settings::slopeMaxAbs.load();
        want = std::clamp(want, -absMax, absMax);
    }

    // Exponential Smoothing (behältst du, aber du kannst 'tau' für snappigere Treppen auf 0.12–0.2 senken)
    float& slot = SlopeDeltaSlot(a);
    const float tau = std::max(0.01f, Settings::slopeTau.load());
    const float alpha = 1.0f - std::exp(-dt / tau);
    const float newDelta = slot + alpha * (want - slot);

    if (std::fabs(newDelta - slot) > 1e-4f) {
        if (auto* avo = a->AsActorValueOwner()) {
            avo->ModActorValue(RE::ActorValue::kSpeedMult, newDelta - slot);
            slot = newDelta;
            return true;
        }
    }
    return false;
}


RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) {
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

    if (refreshGuard_.load(std::memory_order_relaxed)) {
        return RE::BSEventNotifyControl::kContinue;
    }

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
    } catch (...) {}
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

                    if (pendingRefresh_.exchange(false)) {
                        this->ForceSpeedRefresh(pc);
                    }

                    const bool curSprint = IsSprintingByGraph(pc);
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

        ClearSlopeDeltaFor(a);
        ForceSpeedRefresh(a);
    }

    slopeDeltaNPC_.clear();
    lastPosNPC_.clear();
    currentDeltaNPC_.clear();
    attackDeltaNPC_.clear();
    diagDeltaNPC_.clear();
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
    if (std::fabs(slot) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -slot);
        slot = 0.0f;
    }

    if (f >= 0.999f) return false;

    const float curSM = avo->GetActorValue(RE::ActorValue::kSpeedMult);
    const float floor = Settings::minFinalSpeedMult.load();
    const float headroom = std::max(0.0f, curSM - floor);

    float newDiag = headroom * (f - 1.0f);  // <= 0

    if (IsSprintingByGraph(a)) {
        // Make diagonal penalty less severe when sprinting
        newDiag *= 0.5f;
    }

    if (std::fabs(newDiag) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, newDiag);
        slot = newDiag;
        return true;
    }
    return false;
}

bool SpeedController::UpdateDiagonalPenalty(RE::Actor* a) {
    if (!a) return false;

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) {
        return UpdateDiagonalPenalty(a, moveX_, moveY_);
    }

    float x = 0.f, y = 0.f;
    if (TryGetMoveAxesFromGraph(a, x, y)) {
        return UpdateDiagonalPenalty(a, x, y);
    }

    ClearDiagDeltaFor(a);
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

    if (Settings::locationAffects == Settings::LocationAffects::AllStates ||
        (Settings::locationAffects == Settings::LocationAffects::DefaultOnly && (c == MoveCase::Default))) {
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

    bool sprint = IsSprintingByGraph(a);
    if (!sprint) {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (a == pc) {
            const uint64_t now = NowMs();
            const uint64_t last = lastSprintMs_.load(std::memory_order_relaxed);
            sprint = (last != 0) && (now - last <= kSprintLatchMs);
        }
    }
    if (sprint) {
        const MoveCase c = ComputeCase(a);
        if (c != MoveCase::Combat || Settings::sprintAffectsCombat.load()) {
            base += Settings::increaseSprinting.load();
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
    {
        static uint64_t lastApplyMainMs = 0;
        const uint64_t now = NowMs();
        const int gap = std::max(0, Settings::eventDebounceMs.load());
        const bool throttled = (gap > 0 && lastApplyMainMs != 0 && (now - lastApplyMainMs) < (uint64_t)gap);

        if (throttled) {
            UpdateSlopeTickOnly();
            return;
        }
        lastApplyMainMs = now;
    }
    if (NowMs() < postLoadGraceUntilMs_.load(std::memory_order_relaxed)) {
        return;
    }

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

    ForEachTargetActor([&](RE::Actor* a) {
        if (a != pc) ApplyFor(a);
    });
}

void SpeedController::ApplyFor(RE::Actor* a) {
    if (!a) return;

    if (Settings::ignoreBeastForms.load() && IsInBeastForm(a)) {
        RevertDeltasFor(a);
        return;
    }

    const float want = CaseToDelta(a);
    const bool isPlayer = (a == RE::PlayerCharacter::GetSingleton());
    float& cur = isPlayer ? currentDelta : currentDeltaNPC_[a->formID];
    uint64_t& t = isPlayer ? lastApplyPlayerMs_ : lastApplyNPCMs_[a->formID];

    uint64_t now = NowMs();
    float dt = 0.0f;
    if (t == 0) {
        dt = 1.0f / 60.0f;
    } else {
        dt = std::max(0.0f, (now - t) / 1000.0f);
    }
    t = now;

    bool smoothing = Settings::smoothingEnabled.load() && (isPlayer || Settings::smoothingAffectsNPCs.load());

    if (smoothing && isPlayer && Settings::smoothingBypassOnStateChange.load()) {
        const bool sprinting = IsSprintingByGraph(a);
        const bool sneaking = a->IsSneaking();
        const bool drawn = IsWeaponDrawnByState(a);

        const bool flip =
            (sprinting != prevPlayerSprinting_) || (sneaking != prevPlayerSneak_) || (drawn != prevPlayerDrawn_);
        prevPlayerSprinting_ = sprinting;
        prevPlayerSneak_ = sneaking;
        prevPlayerDrawn_ = drawn;

        if (flip) {
            smoothing = false;
            ForceSpeedRefresh(a);
        }
    }

    float newDelta = want;
    if (smoothing) {
        newDelta = SmoothCombined(cur, want, dt);
    }

    float diff = newDelta - cur;

    if (auto* avo = a->AsActorValueOwner()) {
        const float floor = Settings::minFinalSpeedMult.load();

        float& curSlot = isPlayer ? currentDelta : currentDeltaNPC_[a->formID];
        float& diagSlot = DiagDeltaSlot(a);
        float& slopeSlot = SlopeDeltaSlot(a);

        const float curSM = avo->GetActorValue(RE::ActorValue::kSpeedMult);
        const float baseNoUs = curSM - curSlot - diagSlot - slopeSlot;

        bool wantDiag =
            isPlayer ? Settings::enableDiagonalSpeedFix.load() : Settings::enableDiagonalSpeedFixForNPCs.load();

        float predictedDiag = 0.0f;
        if (wantDiag) {
            float x = 0.f, y = 0.f;
            bool sprinting = IsSprintingByGraph(a);
            if (isPlayer) {
                x = moveX_;
                y = moveY_;
            } else {
                (void)TryGetMoveAxesFromGraph(a, x, y);
            }
            predictedDiag = PredictDiagonalPenalty(baseNoUs + newDelta, floor, x, y, sprinting);
        }

        float expectedFinal = baseNoUs + newDelta + predictedDiag + slopeSlot;

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
        }

        diff = newDelta - curSlot;
    }

    if (std::fabs(diff) > 0.0001f) {
        ModSpeedMult(a, diff);
        cur = newDelta;
    }

    const bool wantDiag = (isPlayer ? Settings::enableDiagonalSpeedFix.load() : Settings::enableDiagonalSpeedFixForNPCs.load());

    if (wantDiag) {
        UpdateDiagonalPenalty(a);
    } else {
        ClearDiagDeltaFor(a);
    }
    UpdateSlopePenalty(a, dt);
    ClampSpeedFloor(a);
}


void SpeedController::ModSpeedMult(RE::Actor* actor, float delta) {
    if (!actor) return;
    RE::ActorValueOwner* avo = actor->AsActorValueOwner();
    avo->ModActorValue(RE::ActorValue::kSpeedMult, delta);
}

void SpeedController::ForceSpeedRefresh(RE::Actor* actor) {
    if (!actor) return;
    if (loading_.load(std::memory_order_relaxed)) return;

    const uint64_t now = NowMs();
    if (now < postLoadGraceUntilMs_.load(std::memory_order_relaxed)) {
        pendingRefresh_.store(true, std::memory_order_relaxed);
        return;
    }

    uint64_t prev = lastRefreshMs_.load(std::memory_order_relaxed);
    if (prev != 0 && (now - prev) < 25) return;
    lastRefreshMs_.store(now, std::memory_order_relaxed);

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

        UpdateSlopePenalty(pc, dt);
        ClampSpeedFloor(pc);
    }

    // NPCs, wenn aktiv
    if (Settings::enableSpeedScalingForNPCs.load()) {
        auto* pl = RE::ProcessLists::GetSingleton();
        if (pl) {
            for (auto& h : pl->highActorHandles) {
                RE::Actor* a = h.get().get();
                if (!a) continue;
                const auto id = GetID(a);

                const uint64_t now = NowMs();
                uint64_t& t = lastSlopeNPCMs_[id];
                float dt = (t == 0) ? (1.0f / 60.0f) : std::max(0.0f, (now - t) / 1000.0f);
                t = now;

                UpdateSlopePenalty(a, dt);
                ClampSpeedFloor(a);
            }
        }
    }
}

void SpeedController::RevertDeltasFor(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;

    float& moveDelta = CurrentDeltaSlot(a);
    if (std::fabs(moveDelta) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -moveDelta);
        moveDelta = 0.0f;
    }

    float& atkDelta = AttackDeltaSlot(a);
    if (std::fabs(atkDelta) > 1e-6f) {
        avo->ModActorValue(RE::ActorValue::kWeaponSpeedMult, -atkDelta);
        atkDelta = 0.0f;
    }

    float& diag = DiagDeltaSlot(a);
    if (std::fabs(diag) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -diag);
        diag = 0.0f;
    }
    ClearSlopeDeltaFor(a);
    ForceSpeedRefresh(a);
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

void SpeedController::ClearDiagDeltaFor(RE::Actor* a) {
    if (!a) return;
    auto* avo = a->AsActorValueOwner();
    if (!avo) return;
    float& slot = DiagDeltaSlot(a);
    if (std::fabs(slot) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -slot);
        slot = 0.0f;
    }
}

std::deque<PathSample>& SpeedController::PathBuf(RE::Actor* a) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (a == pc) return pathPlayer_;
    return pathNPC_[a->GetFormID()];
}

void SpeedController::ClearPathFor(RE::Actor* a) {
    auto& q = PathBuf(a);
    q.clear();
}

void SpeedController::PushPathSample(RE::Actor* a, const RE::NiPoint3& pos, uint64_t nowMs) {
    auto& q = PathBuf(a);
    float sxy = 0.0f;
    if (!q.empty()) {
        const auto& last = q.back();
        const float dx = pos.x - last.x;
        const float dy = pos.y - last.y;
        const float dxy = std::sqrt(dx * dx + dy * dy);
        // Mini-Filter: sehr kleine Schritte ignorieren, damit keine „zitternden“ In-Place-Messungen entstehen
        if (dxy < Settings::slopeMinXYPerFrame.load()) {
            // trotzdem eine Zeitschranke pflegen, damit wir purgen können
            sxy = last.sxy;
        } else {
            sxy = last.sxy + dxy;
        }
    }
    q.push_back(PathSample{pos.x, pos.y, pos.z, sxy, nowMs});

    // Alte Samples rausschmeißen (Zeitfenster)
    const uint64_t maxAgeMs = static_cast<uint64_t>(std::max(0.f, Settings::slopeMaxHistorySec.load()) * 1000.f);
    while (!q.empty() && (nowMs - q.front().tMs) > maxAgeMs) {
        q.pop_front();
    }
}

bool SpeedController::ComputePathSlopeDeg(RE::Actor* a, float lookbackUnits, float maxAgeSec, float& outDeg) {
    auto& q = PathBuf(a);
    if (q.size() < 2) return false;

    const auto& cur = q.back();
    const float wantSxy = std::max(0.f, cur.sxy - std::max(lookbackUnits, 0.0f));

    // Finde Sample, das mindestens lookbackUnits hinter uns liegt
    const PathSample* ref = nullptr;
    for (int i = static_cast<int>(q.size()) - 1; i >= 0; --i) {
        if (q[static_cast<size_t>(i)].sxy <= wantSxy) {
            ref = &q[static_cast<size_t>(i)];
            break;
        }
    }
    if (!ref) ref = &q.front();  // notfalls ältestes

    const float dxy = std::max(1e-3f, cur.sxy - ref->sxy);
    const float dz = cur.z - ref->z;
    outDeg = std::clamp(std::atan2(dz, dxy) * 57.29578f, -85.0f, 85.0f);
    return true;
}


bool SpeedController::GetJoggingMode() const { return joggingMode_; }
void SpeedController::SetJoggingMode(bool b) { joggingMode_ = b; }

float SpeedController::GetCurrentDelta() const { return currentDelta; }
void SpeedController::SetCurrentDelta(float d) { currentDelta = d; }
