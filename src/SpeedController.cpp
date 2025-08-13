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

    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESCombatEvent>(this);
        holder->AddEventSink<RE::TESLoadGameEvent>(this);
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

    for (auto e = *evns; e; e = e->next) {
        if (e->eventType != RE::INPUT_EVENT_TYPE::kButton) continue;
        auto* be = static_cast<RE::ButtonEvent*>(e);

        const bool isDown = (be->value > 0.0f);
        const bool isPress = isDown && (be->heldDownSecs == 0.0f);  // erste Down-Frame

        const RE::BSFixedString evName = be->userEvent;
        if (!sprintUserEvent_.empty() && evName == RE::BSFixedString(sprintUserEvent_.c_str())) {
            if (be->value > 0.0f) {
                lastSprintMs_.store(NowMs(), std::memory_order_relaxed);
            } else {
                lastSprintMs_.store(0, std::memory_order_relaxed);
            }
        }

        if (loading_.load(std::memory_order_relaxed)) return RE::BSEventNotifyControl::kContinue;

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
                    const float before = CaseToDelta(ComputeCase(pc), pc);
                    joggingMode_ = !joggingMode_;
                    const float after = CaseToDelta(ComputeCase(pc), pc);
                    const float diff = after - before;

                    if (std::fabs(diff) > 0.01f) {
                        ModSpeedMult(pc, diff);
                    }
                    currentDelta = after;
                    ForceSpeedRefresh(pc);
                }
                lastToggle_ = now;
            }
            break;
        }
    }

    return RE::BSEventNotifyControl::kContinue;
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

void SpeedController::TryInitDrawnFromGraph() {
    if (initTried_) return;
    auto* pc = RE::PlayerCharacter::GetSingleton();
    (void)pc;
    initTried_ = true;
}

SpeedController::MoveCase SpeedController::ComputeCase(const RE::PlayerCharacter* pc) const {
    if (Settings::noReductionInCombat && pc->IsInCombat() && IsWeaponDrawnByState(pc)) {
        return MoveCase::Combat;
    }
    if (IsWeaponDrawnByState(pc)) {
        return MoveCase::Drawn;
    }
    if (pc->IsSneaking()) {
        return MoveCase::Sneak;
    }
    return MoveCase::Default;
}

void SpeedController::OnPreLoadGame() { loading_.store(true, std::memory_order_relaxed); }

void SpeedController::OnPostLoadGame() {
    SKSE::GetTaskInterface()->AddTask([this]() {
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            currentDelta = CaseToDelta(ComputeCase(pc), pc);
            ForceSpeedRefresh(pc);
        }
        loading_.store(false, std::memory_order_relaxed);
    });
}


float SpeedController::CaseToDelta(MoveCase c, const RE::PlayerCharacter* pc) const {
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

    const bool sprintActiveGraph = IsSprintingByGraph(pc);
    const uint64_t now = NowMs();
    const uint64_t last = lastSprintMs_.load(std::memory_order_relaxed);
    const bool sprintActiveLatch = (last != 0) && (now - last <= kSprintLatchMs);

    if (sprintActiveGraph || sprintActiveLatch) {
        base += Settings::increaseSprinting.load();
    }
    return base;
}

void SpeedController::Apply() {
    if (loading_.load(std::memory_order_relaxed)) return;

    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (!pc) return;

    const MoveCase mc = ComputeCase(pc);
    const float want = CaseToDelta(mc, pc);

    const float diff = want - currentDelta;

    if (std::fabs(diff) > 0.01f) {
        ModSpeedMult(pc, diff);
        currentDelta = want;
        ForceSpeedRefresh(pc);
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
