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



RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) {
    Settings::LoadFromJson(Settings::DefaultPath());

    Settings::LoadFromJson(Settings::DefaultPath());
    LoadToggleBindingFromJson();
    lastSprintMs_.store(0, std::memory_order_relaxed);

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::TESCombatEvent* evn,
                                                       RE::BSTEventSource<RE::TESCombatEvent>*) {
    if (evn) {
        Apply();
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl SpeedController::ProcessEvent(const RE::BSAnimationGraphEvent* evn,
                                                       RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!evn) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (pc && evn->holder && evn->holder != pc) {
        return RE::BSEventNotifyControl::kContinue;
    }

    Apply();
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
                        const float before = CaseToDelta(pc);
                        joggingMode_ = !joggingMode_;
                        const float after = CaseToDelta(pc);
                        const float diff = after - before;

                        if (std::fabs(diff) > 0.01f) {
                            ModSpeedMult(pc, diff);
                        }
                        currentDelta = after;
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
            if (UpdateDiagonalPenalty(pc)) {
                ForceSpeedRefresh(pc);
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
    sprintUserEvent_ = "Shout";

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
            std::this_thread::sleep_for(750ms);
            SKSE::GetTaskInterface()->AddTask([this, &prevSprint]() {
                this->Apply();
                if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
                    this->UpdateAttackSpeed(pc);

                    const bool curSprint = IsSprintingByGraph(pc);
                    if (curSprint != prevSprint) {
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
            }
        }

        if (auto jt = attackDeltaNPC_.find(id); jt != attackDeltaNPC_.end()) {
            if (std::fabs(jt->second) > 1e-6f) {
                avo->ModActorValue(RE::ActorValue::kWeaponSpeedMult, -jt->second);
            }
        }

        ForceSpeedRefresh(a);
    }

    currentDeltaNPC_.clear();
    attackDeltaNPC_.clear();
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
    if (Settings::noReductionInCombat && a->IsInCombat() && IsWeaponDrawnByState(a)) {
        return MoveCase::Combat;
    }
    if (IsWeaponDrawnByState(a)) {
        return MoveCase::Drawn;
    }
    if (a->IsSneaking()) {
        return MoveCase::Sneak;
    }
    return MoveCase::Default;
}

void SpeedController::OnPreLoadGame() { loading_.store(true, std::memory_order_relaxed); }

void SpeedController::OnPostLoadGame() {
    SKSE::GetTaskInterface()->AddTask([this]() {
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            currentDelta = CaseToDelta(pc);
            ForceSpeedRefresh(pc);
        }
        loading_.store(false, std::memory_order_relaxed);
    });
}

bool SpeedController::UpdateDiagonalPenalty(RE::Actor* a) {
    if (!a) return false;

    const float ax = std::fabs(moveX_);
    const float ay = std::fabs(moveY_);
    const float mag = std::sqrt(moveX_ * moveX_ + moveY_ * moveY_);
    const float maxc = std::max(ax, ay);

    float f = 1.0f;
    if (mag > 1e-4f && maxc > 0.0f) {
        f = std::min(1.0f, maxc / mag);
    }

    auto* avo = a->AsActorValueOwner();
    if (!avo) return false;

    if (std::fabs(diagDelta_) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, -diagDelta_);
        diagDelta_ = 0.0f;
    }

    if (f >= 0.999f) {
        return false;
    }

    const float curSM = avo->GetActorValue(RE::ActorValue::kSpeedMult);

    const float newDiag = curSM * (f - 1.0f);

    if (std::fabs(newDiag) > 0.001f) {
        avo->ModActorValue(RE::ActorValue::kSpeedMult, newDiag);
        diagDelta_ = newDiag;
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
    if (sprint) base += Settings::increaseSprinting.load();
    return base;
}

void SpeedController::RefreshNow() {
    Apply();
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        ForceSpeedRefresh(pc);
    }
}

void SpeedController::Apply() {
    if (loading_.load(std::memory_order_relaxed)) return;

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

    const float want = CaseToDelta(a);
    float& cur = CurrentDeltaSlot(a);
    const float diff = want - cur;

    bool changed = false;
    if (std::fabs(diff) > 0.01f) {
        ModSpeedMult(a, diff);
        cur = want;
        changed = true;
    }

    if (a == RE::PlayerCharacter::GetSingleton()) {
        if (UpdateDiagonalPenalty(a)) {
            changed = true;
        }
    }
    UpdateAttackSpeed(a);

    if (changed) {
        ForceSpeedRefresh(a);
    }
}


void SpeedController::ModSpeedMult(RE::Actor* actor, float delta) {
    if (!actor) return;
    RE::ActorValueOwner* avo = actor->AsActorValueOwner();
    avo->ModActorValue(RE::ActorValue::kSpeedMult, delta);
}

void SpeedController::ForceSpeedRefresh(RE::Actor* actor) {
    if (!actor) return;

    if (auto* avo = actor->AsActorValueOwner()) {
        avo->ModActorValue(RE::ActorValue::kCarryWeight, 0.1f);
        avo->ModActorValue(RE::ActorValue::kCarryWeight, -0.1f);
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

bool SpeedController::GetJoggingMode() const { return joggingMode_; }
void SpeedController::SetJoggingMode(bool b) { joggingMode_ = b; }

float SpeedController::GetCurrentDelta() const { return currentDelta; }
void SpeedController::SetCurrentDelta(float d) { currentDelta = d; }
