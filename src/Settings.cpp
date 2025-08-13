#include "Settings.h"

#include <algorithm>
#include <fstream>
#include <string>

using nlohmann::json;

static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

std::filesystem::path Settings::DefaultPath() {
    return std::filesystem::path("Data") / "SKSE" / "Plugins" / "SpeedController.json";
}

bool Settings::LoadFromJson(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in.is_open()) {
        return false;
    }
    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }

    if (j.contains("kReduceOutOfCombat")) {
        float v = j["kReduceOutOfCombat"].get<float>();
        reduceOutOfCombat = clampf(v, 0.0f, 100.0f);
    }
    if (j.contains("kReduceJoggingOutOfCombat")) {
        float v = j["kReduceJoggingOutOfCombat"].get<float>();
        reduceJoggingOutOfCombat = clampf(v, 0.0f, 100.0f);
    }
    if (j.contains("kReduceDrawn")) {
        float v = j["kReduceDrawn"].get<float>();
        reduceDrawn = clampf(v, 0.0f, 100.0f);
    }
    if (j.contains("kReduceSneak")) {
        float v = j["kReduceSneak"].get<float>();
        reduceSneak = clampf(v, 0.0f, 100.0f);
    }
    if (j.contains("kIncreaseSprinting")) {
        float v = j["kIncreaseSprinting"].get<float>();
        increaseSprinting = clampf(v, 0.0f, 100.0f);
    }
    if (j.contains("kNoReductionInCombat")) {
        bool v = j["kNoReductionInCombat"].get<bool>();
        noReductionInCombat = v;
    }
    if (j.contains("kToggleSpeedKey")) {
        int v = j["kToggleSpeedKey"].get<int>();
        toggleSpeedKey = v;
    }
    if (j.contains("kToggleSpeedEvent")) {
        std::string v = j["kToggleSpeedEvent"].get<std::string>();
        toggleSpeedEvent = v;
    }
    if (j.contains("kSprintEventName")) {
        std::string v = j["kSprintEventName"].get<std::string>();
        sprintEventName = v;
    }
    return true;
}
