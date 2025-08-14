#pragma once
#include <atomic>
#include <filesystem>
#include <optional>

// Get nlohmann/json from: https://github.com/nlohmann/json
#include "nlohmann/json.hpp"

struct Settings {
    static inline std::atomic<float> reduceOutOfCombat{45.0f};
    static inline std::atomic<float> reduceJoggingOutOfCombat{15.0f};
    static inline std::atomic<float> reduceDrawn{15.0f};
    static inline std::atomic<float> reduceSneak{20.0f};
    static inline std::atomic<float> increaseSprinting{25.0f};
    static inline std::atomic<bool> noReductionInCombat{true};
    static inline std::atomic<int> toggleSpeedKey{269};
    static inline std::string toggleSpeedEvent{"Shout"};
    static inline std::string sprintEventName{"Sprint"};

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
