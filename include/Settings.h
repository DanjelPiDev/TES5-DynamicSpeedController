#pragma once
#include <atomic>
#include <filesystem>
#include <optional>

// Get nlohmann/json from: https://github.com/nlohmann/json
#include "nlohmann/json.hpp"

struct Settings {
    enum class SmoothingMode { Exponential = 0, RateLimit = 1, ExpoThenRate = 2 };

    // Slope / Treppen
    static inline std::atomic<bool> slopeEnabled{false};
    static inline std::atomic<bool> slopeAffectsNPCs{true};

    static inline std::atomic<float> slopeUphillPerDeg{0.60f};    // pro Grad bergauf (wird abgezogen)
    static inline std::atomic<float> slopeDownhillPerDeg{0.30f};  // pro Grad bergab (wird addiert)
    static inline std::atomic<float> slopeMaxAbs{25.0f};          // max. |Delta| durch Slope
    static inline std::atomic<float> slopeTau{0.25f};             // Glättung in Sekunden

    static inline std::atomic<int> slopeMethod{1};               // 0 = Instant (alt), 1 = Path (neu)
    static inline std::atomic<float> slopeLookbackUnits{96.0f};  // Distanzfenster (ca. ~1–1.5 m in Skyrim Units)
    static inline std::atomic<float> slopeMaxHistorySec{1.0f};   // Positionen so lange halten
    static inline std::atomic<float> slopeMinXYPerFrame{0.25f};  // Ignoriere zu kleine XY-Schritte
    static inline std::atomic<int> slopeMedianN{3};              // optional: Median über N Fenster

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

    static inline std::atomic<int> eventDebounceMs{10};

    // Location stuff
    static bool ParseFormSpec(const std::string& spec, std::string& plugin, std::uint32_t& id);

    struct FormSpec {
        std::string plugin;
        std::uint32_t id = 0;
        float value = 0.f;
    };

    static inline std::vector<FormSpec> reduceInLocationType;      // BGSKeyword*
    static inline std::vector<FormSpec> reduceInLocationSpecific;  // BGSLocation*

    static inline std::atomic<bool> groundEnabled{false};
    static inline std::atomic<bool> groundAffectsNPCs{true};
    static inline std::atomic<float> groundTau{0.10f};
    static inline std::atomic<bool> groundClampEnabled{false};
    static inline std::atomic<float> groundMinFinal{60.0f};
    static inline std::atomic<float> groundMaxFinal{200.0f};

    struct GroundRule {
        std::string name;
        float value = 0.f;
    };

    static inline std::vector<GroundRule> groundRules;

    enum class LocationAffects { DefaultOnly, AllStates };
    enum class LocationMode { Replace, Add };

    static inline LocationAffects locationAffects{LocationAffects::DefaultOnly};
    static inline LocationMode locationMode{LocationMode::Replace};

    static bool SaveToJson(const std::filesystem::path& file);
    static bool LoadFromJson(const std::filesystem::path& file);

    static std::filesystem::path DefaultPath();
};
