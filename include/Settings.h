#pragma once
#include <atomic>
#include <filesystem>
#include <optional>

// Get nlohmann/json from: https://github.com/nlohmann/json
#include "nlohmann/json.hpp"

struct Settings {
    enum class SmoothingMode { Exponential = 0, RateLimit = 1, ExpoThenRate = 2 };

    // Slopes
    static inline std::atomic<bool> slopeEnabled{false};
    static inline std::atomic<bool> slopeAffectsNPCs{true};

    static inline std::atomic<float> slopeUphillPerDeg{0.60f};    // per degree uphill (subtracted)
    static inline std::atomic<float> slopeDownhillPerDeg{0.30f};  // per degree downhill (added)
    static inline std::atomic<float> slopeMaxAbs{25.0f};          // max. |Delta| from slope (in SpeedMult)
    static inline std::atomic<float> slopeTau{0.25f};             // smoothing tau (in seconds)

    static inline std::atomic<int> slopeMethod{1};               // 0 = Instant (old), 1 = Path (new)
    static inline std::atomic<float> slopeLookbackUnits{96.0f};  // Distance (~1–1.5m in Skyrim Units)
    static inline std::atomic<float> slopeMaxHistorySec{1.0f};   // Hold path samples for this long (in seconds)
    static inline std::atomic<float> slopeMinXYPerFrame{0.25f};  // Ignore small movements (in Skyrim Units)
    static inline std::atomic<int> slopeMedianN{3};

    // Slope-spezific final values
    static inline std::atomic<bool> slopeClampEnabled{false};
    static inline std::atomic<float> slopeMinFinal{60.0f};
    static inline std::atomic<float> slopeMaxFinal{200.0f};

    static inline std::atomic<float> minFinalSpeedMult{10.0f};

    static inline std::atomic<bool> smoothingEnabled{true};
    static inline std::atomic<bool> smoothingAffectsNPCs{true};
    static inline std::atomic<bool> smoothingBypassOnStateChange{true};

    static inline SmoothingMode smoothingMode{SmoothingMode::ExpoThenRate};
    static inline std::atomic<float> smoothingHalfLifeMs{160.0f};
    static inline std::atomic<float> smoothingMaxChangePerSecond{120.0f};  // max delta per Second (SpeedMult-Points)

    static inline std::atomic<bool> enableSpeedScalingForNPCs{false};
    inline static std::atomic<bool> ignoreBeastForms{true};
    inline static std::atomic<bool> enableDiagonalSpeedFix{true};
    inline static std::atomic<bool> enableDiagonalSpeedFixForNPCs{false};

    static inline std::atomic<float> reduceOutOfCombat{45.0f};
    static inline std::atomic<float> reduceJoggingOutOfCombat{15.0f};
    static inline std::atomic<float> reduceDrawn{15.0f};
    static inline std::atomic<float> reduceSneak{20.0f};
    static inline std::atomic<float> increaseSprinting{25.0f};
    static inline std::atomic<bool> noReductionInCombat{true};
    static inline std::atomic<int> toggleSpeedKey{269};
    static inline std::string toggleSpeedEvent{"Shout"};
    static inline std::string sprintEventName{"Sprint"};

    // Attack speed scaling
    static inline std::atomic<bool> attackSpeedEnabled{true};
    static inline std::atomic<bool> attackOnlyWhenDrawn{true};
    static inline std::atomic<bool> sprintAffectsCombat{false};

    static inline std::atomic<float> attackBase{1.0f};
    static inline std::atomic<float> weightPivot{10.0f};   // reference weight in Skyrim units
    static inline std::atomic<float> weightSlope{-0.03f};  // per weight unit (negative slows heavy)
    static inline std::atomic<bool> usePlayerScale{false};
    static inline std::atomic<float> scaleSlope{0.25f};     // +0.25 per +1.0 scale

    static inline std::atomic<float> minAttackMult{0.6f};
    static inline std::atomic<float> maxAttackMult{1.8f};

    // Animation coupling
    static inline std::atomic<bool> syncSprintAnimToSpeed{true};  // Master toggle
    static inline std::atomic<bool> onlySlowDown{true};           // avoid >1x (No Cartoony-Speedup)
    static inline std::atomic<float> sprintAnimMin{0.50f};        // clamp
    static inline std::atomic<float> sprintAnimMax{1.25f};        // clamp

    static inline std::atomic<bool> sprintAnimOwnSmoothing{true};
    static inline std::atomic<int> sprintAnimSmoothingMode{static_cast<int>(SmoothingMode::ExpoThenRate)};
    static inline std::atomic<float> sprintAnimTau{0.10f};        // 100ms
    static inline std::atomic<float> sprintAnimRatePerSec{5.0f};  // max steps per second

    // Armor stuff
    static inline std::atomic<bool> armorAffectsMovement{false};
    static inline std::atomic<bool> armorAffectsAttackSpeed{false};
    static inline std::atomic<bool> useMaxArmorWeight{true};
    static inline std::atomic<float> armorWeightPivot{20.0f};
    static inline std::atomic<float> armorWeightSlopeSM{-1.0f};
    static inline std::atomic<float> armorMoveMin{-60.0f};
    static inline std::atomic<float> armorMoveMax{0.0f};
    static inline std::atomic<float> armorWeightSlopeAtk{-0.010f};

    static inline std::atomic<int> eventDebounceMs{10};
    static inline std::atomic<int> npcRadius{512};  // Max distance for NPCs is 16384, 0 = All NPCs (Disable radius check)
    static inline std::atomic<float> npcPercentOfPlayer{90.0f};  // NPCs move at least this percent of player speed, because NPCs are slower than players

    // Location stuff
    static bool ParseFormSpec(const std::string& spec, std::string& plugin, std::uint32_t& id);

    struct FormSpec {
        std::string plugin;
        std::uint32_t id = 0;
        float value = 0.f;
    };

    static inline std::vector<FormSpec> reduceInLocationType;      // BGSKeyword*
    static inline std::vector<FormSpec> reduceInLocationSpecific;  // BGSLocation*

    enum class LocationAffects { DefaultOnly, AllStates };
    enum class LocationMode { Replace, Add };

    static inline LocationAffects locationAffects{LocationAffects::DefaultOnly};
    static inline LocationMode locationMode{LocationMode::Replace};

    static bool SaveToJson(const std::filesystem::path& file);
    static bool LoadFromJson(const std::filesystem::path& file);

    static std::filesystem::path DefaultPath();
};
