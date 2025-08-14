#pragma once

#include <atomic>
#include <thread>
#include "Settings.h"

class SpeedController : public RE::BSTEventSink<RE::TESCombatEvent>,
                        public RE::BSTEventSink<RE::TESLoadGameEvent>,
                        public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
                        public RE::BSTEventSink<RE::InputEvent*> {
public:
    static SpeedController* GetSingleton();

    void Install();

    virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent*, RE::BSTEventSource<RE::TESCombatEvent>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent*, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
    virtual RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* evns, RE::BSTEventSource<RE::InputEvent*>*) override;

    void OnPostLoadGame();
    void OnPreLoadGame();

    void RefreshNow();
    void UpdateBindingsFromSettings();

    bool GetJoggingMode() const;
    void SetJoggingMode(bool b);

    float GetCurrentDelta() const;
    void SetCurrentDelta(float d);

private:
    enum class MoveCase : std::uint8_t { Combat, Drawn, Sneak, Default };

    // Movement speed (To fix the diagonal speed issue of skyrim)
    float moveX_ = 0.0f;  // -1 ... +1  (left/right)
    float moveY_ = 0.0f;  // -1 ... +1  (forward/backward)
    float diagDelta_ = 0.0f;

    float currentDelta = 0.0f;
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

    void LoadToggleBindingFromJson();

    void StartHeartbeat();
    void TryInitDrawnFromGraph();
    MoveCase ComputeCase(const RE::PlayerCharacter* pc) const;
    float CaseToDelta(MoveCase c, const RE::PlayerCharacter* pc) const;
    void Apply();
    bool UpdateDiagonalPenalty(RE::Actor* a);

    static void ModSpeedMult(RE::Actor* actor, float delta);
    static void ForceSpeedRefresh(RE::Actor* actor);
    static bool IsWeaponDrawnByState(const RE::Actor* a);
    static bool IsSprintingByGraph(const RE::Actor* a);

    static inline uint64_t NowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
};
