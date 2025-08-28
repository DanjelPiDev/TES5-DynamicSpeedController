#pragma once

#include <atomic>
#include <thread>
#include "Settings.h"

struct PathSample {
    float x, y, z;
    float sxy;     // Cumulative distance in XY plane
    uint64_t tMs;  // Timestamp in milliseconds
};

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

    float GetSlopeDelta() const { return slopeDeltaPlayer_; }

    void SetSnapshot(bool jogging, float curDelta, float diag, float baseSM, float slope) {
        joggingMode_ = jogging;
        currentDelta = curDelta;
        diagDelta_ = diag;
        savedBaselineSM_ = baseSM;
        slopeDeltaPlayer_ = slope;
        snapshotLoaded_.store(true, std::memory_order_relaxed);
    }

    std::deque<PathSample> pathPlayer_;
    std::unordered_map<std::uint32_t, std::deque<PathSample>> pathNPC_;

    std::deque<PathSample>& PathBuf(RE::Actor* a);
    void ClearPathFor(RE::Actor* a);
    void PushPathSample(RE::Actor* a, const RE::NiPoint3& pos, uint64_t nowMs);
    bool ComputePathSlopeDeg(RE::Actor* a, float lookbackUnits, float maxAgeSec, float& outDeg);

    float diagResidualPlayer_ = 0.0f;
    std::unordered_map<std::uint32_t, float> diagResidualNPC_;

    float slopeResidualPlayer_ = 0.0f;
    std::unordered_map<std::uint32_t, float> slopeResidualNPC_;

    inline float& DiagResidualSlot(RE::Actor* a) {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (a == pc) return diagResidualPlayer_;
        return diagResidualNPC_[a->GetFormID()];
    }
    inline float& SlopeResidualSlot(RE::Actor* a) {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (a == pc) return slopeResidualPlayer_;
        return slopeResidualNPC_[a->GetFormID()];
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

    uint64_t lastSlopePlayerMs_ = 0;
    std::unordered_map<uint32_t, uint64_t> lastSlopeNPCMs_;

    bool prevPlayerSprinting_ = false;
    bool prevPlayerSneak_ = false;
    bool prevPlayerDrawn_ = false;

    std::unordered_map<std::uint32_t, bool> prevNPCSprinting_;
    std::unordered_map<std::uint32_t, bool> prevNPCSneak_;
    std::unordered_map<std::uint32_t, bool> prevNPCDrawn_;

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

    std::atomic<uint64_t> lastRefreshPlayerMs_{0};
    std::unordered_map<RE::FormID, uint64_t> lastRefreshNPCMs_;

    std::chrono::steady_clock::time_point lastToggle_{};
    std::chrono::milliseconds toggleCooldown_{150};

    float slopeDeltaPlayer_ = 0.0f;
    std::unordered_map<std::uint32_t, float> slopeDeltaNPC_;

    RE::NiPoint3 lastPosPlayer_{};
    std::unordered_map<std::uint32_t, RE::NiPoint3> lastPosNPC_;

    // Ground delta-Slots (analog to slope/diag)
    float groundDeltaPlayer_ = 0.0f;
    std::unordered_map<std::uint32_t, float> groundDeltaNPC_;

    void ClampSpeedFloorTracked(RE::Actor* a);
    void RevertMovementDeltasFor(RE::Actor* a, bool clearSlope = true);
    float& SlopeDeltaSlot(RE::Actor* a);
    void ClearSlopeDeltaFor(RE::Actor* a);
    void ClearNPCState(std::uint32_t id);
    bool UpdateSlopePenalty(RE::Actor* a, float dt);
    void UpdateSlopeTickNPCsOnly();
    void UpdateSlopeTickOnly();

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
    bool ForceSpeedRefresh(RE::Actor* actor);
    static bool IsWeaponDrawnByState(const RE::Actor* a);
    static bool IsSprintingByGraph(const RE::Actor* a);
    bool IsSprintingLatched(const RE::Actor* a) const;

    float& DiagDeltaSlot(RE::Actor* a);
    void ClearDiagDeltaFor(RE::Actor* a);

    bool TryGetMoveAxesFromGraph(const RE::Actor* a, float& outX, float& outY) const;

    bool UpdateDiagonalPenalty(RE::Actor* a, float inX, float inY);
    bool UpdateDiagonalPenalty(RE::Actor* a);

    float ComputeArmorWeight(const RE::Actor* a) const;

    static inline uint64_t NowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
};
