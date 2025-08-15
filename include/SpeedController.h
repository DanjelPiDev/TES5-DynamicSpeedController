#pragma once

#include <atomic>
#include <thread>
#include "Settings.h"

class SpeedController : public RE::BSTEventSink<RE::TESCombatEvent>,
                        public RE::BSTEventSink<RE::TESLoadGameEvent>,
                        public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
                        public RE::BSTEventSink<RE::InputEvent*>,
                        public RE::BSTEventSink<RE::TESEquipEvent> {
public:
    static SpeedController* GetSingleton();

    void Install();

    virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent*, RE::BSTEventSource<RE::TESCombatEvent>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent*, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* evns, RE::BSTEventSource<RE::InputEvent*>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent*, RE::BSTEventSource<RE::TESEquipEvent>*) override;

    void OnPostLoadGame();
    void OnPreLoadGame();
    void DoPostLoadCleanup();

    void RefreshNow();
    void UpdateBindingsFromSettings();

    bool GetJoggingMode() const;
    void SetJoggingMode(bool b);

    float GetCurrentDelta() const;
    void SetCurrentDelta(float d);

    float GetDiagDelta() const { return diagDelta_; }
    void SetDiagDelta(float d) { diagDelta_ = d; }

    void SetSnapshot(bool jogging, float curDelta, float diag, float baseSM) {
        joggingMode_ = jogging;
        currentDelta = curDelta;
        diagDelta_ = diag;
        savedBaselineSM_ = baseSM;
        snapshotLoaded_.store(true, std::memory_order_relaxed);
    }

private:
    enum class MoveCase : std::uint8_t { Combat, Drawn, Sneak, Default };
    float SmoothExpo(float prev, float target, float dtSec) const {
        // alpha = 1 - exp(-dt / tau); tau = hl/ln2
        const float hlSec = Settings::smoothingHalfLifeMs.load() / 1000.0f;
        const float tau = std::max(hlSec / 0.69314718056f, 1e-4f);
        const float a = 1.0f - std::exp(-dtSec / tau);
        return prev + (target - prev) * std::clamp(a, 0.0f, 1.0f);
    }
    float SmoothRate(float prev, float target, float dtSec) const {
        const float maxPerSec = Settings::smoothingMaxChangePerSecond.load();
        const float maxStep = std::max(0.0f, maxPerSec) * dtSec;
        float d = target - prev;
        if (d > maxStep) d = maxStep;
        if (d < -maxStep) d = -maxStep;
        return prev + d;
    }
    float SmoothCombined(float prev, float target, float dtSec) const {
        using SM = Settings::SmoothingMode;
        switch (Settings::smoothingMode) {
            case SM::Exponential:
                return SmoothExpo(prev, target, dtSec);
            case SM::RateLimit:
                return SmoothRate(prev, target, dtSec);
            case SM::ExpoThenRate:
                return SmoothRate(prev, SmoothExpo(prev, target, dtSec), dtSec);
        }
        return target;
    }
    std::atomic<bool> pendingRefresh_{false};
    std::atomic<bool> refreshGuard_{false};
    std::atomic<uint64_t> lastRefreshMs_{0};
    std::atomic<int> postLoadNudges_{0};
    std::atomic<uint64_t> postLoadGraceUntilMs_{0};
    std::atomic<bool> postLoadCleaned_{false};
    std::atomic<bool> snapshotLoaded_{false};
    float savedBaselineSM_ = NAN;

    static constexpr float kRefreshEps = 0.10f;

    float sprintAnimRate_ = 1.0f;

    float smVelPlayer_ = 0.0f;
    std::unordered_map<std::uint32_t, float> smVelNPC_;

    float wantFilteredPlayer_ = 0.0f;
    std::unordered_map<std::uint32_t, float> wantFilteredNPC_;

    uint64_t lastApplyPlayerMs_ = 0;
    std::unordered_map<std::uint32_t, uint64_t> lastApplyNPCMs_;

    bool prevPlayerSprinting_ = false;
    bool prevPlayerSneak_ = false;
    bool prevPlayerDrawn_ = false;

    // Movement speed (To fix the diagonal speed issue of skyrim)
    float moveX_ = 0.0f;  // -1 ... +1  (left/right)
    float moveY_ = 0.0f;  // -1 ... +1  (forward/backward)
    float diagDelta_ = 0.0f;
    std::unordered_map<std::uint32_t, float> diagDeltaNPC_;

    // Deltas: Player vs. NPCs
    float currentDelta = 0.0f;
    float attackDelta_ = 0.0f;
    std::unordered_map<std::uint32_t, float> currentDeltaNPC_;
    std::unordered_map<std::uint32_t, float> attackDeltaNPC_;
    bool prevAffectNPCs_ = false;

    bool initTried_ = false;
    std::atomic<bool> run_ = false;
    std::atomic<bool> loading_{false};
    std::thread th_;

    bool joggingMode_ = false;    // false=OutOfCombat normal, true=Jogging
    uint32_t toggleKeyCode_ = 0;
    std::string toggleUserEvent_;

    std::atomic<uint64_t> lastSprintMs_{0};
    static constexpr uint64_t kSprintLatchMs = 150;
    std::string sprintUserEvent_ = "Sprint";

    std::chrono::steady_clock::time_point lastToggle_{};
    std::chrono::milliseconds toggleCooldown_{150};

    void UpdateSprintAnimRate(RE::Actor* a);

    void LoadToggleBindingFromJson();

    void StartHeartbeat();
    void TryInitDrawnFromGraph();

    MoveCase ComputeCase(const RE::Actor* pc) const;
    float CaseToDelta(const RE::Actor* pc) const;

    void Apply();
    void ApplyFor(RE::Actor* a);

    void UpdateAttackSpeed(RE::Actor* actor);
    float ComputeEquippedWeight(const RE::Actor* a) const;
    float GetPlayerScaleSafe(const RE::Actor* a) const;

    static std::uint32_t GetID(const RE::Actor* a) { return a ? a->GetFormID() : 0; }
    float& AttackDeltaSlot(RE::Actor* a);
    float& CurrentDeltaSlot(RE::Actor* a);

    void ForEachTargetActor(const std::function<void(RE::Actor*)>& fn);
    void RevertDeltasFor(RE::Actor* a);
    void RevertAllNPCDeltas(); 

    static bool IsInBeastForm(const RE::Actor* a);
    static void ModSpeedMult(RE::Actor* actor, float delta);
    void ForceSpeedRefresh(RE::Actor* actor);
    static bool IsWeaponDrawnByState(const RE::Actor* a);
    static bool IsSprintingByGraph(const RE::Actor* a);

    float& DiagDeltaSlot(RE::Actor* a);
    void ClearDiagDeltaFor(RE::Actor* a);

    bool TryGetMoveAxesFromGraph(const RE::Actor* a, float& outX, float& outY) const;

    bool UpdateDiagonalPenalty(RE::Actor* a, float inX, float inY);
    bool UpdateDiagonalPenalty(RE::Actor* a); 

    static inline uint64_t NowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
};
