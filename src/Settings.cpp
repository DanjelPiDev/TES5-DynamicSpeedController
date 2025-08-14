#include "Settings.h"

#include <algorithm>
#include <fstream>
#include <string>

using nlohmann::json;

static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

std::filesystem::path Settings::DefaultPath() {
    return std::filesystem::path("Data") / "SKSE" / "Plugins" / "SpeedController.json";
}

bool Settings::ParseFormSpec(const std::string& spec, std::string& plugin, std::uint32_t& id) {
    // "Plugin.esm|0x123456"
    auto pos = spec.find('|');
    if (pos == std::string::npos) return false;
    plugin = spec.substr(0, pos);
    std::string idstr = spec.substr(pos + 1);
    try {
        id = static_cast<std::uint32_t>(std::stoul(idstr, nullptr, 0));  // 0 => 0x... or dezimal
        return true;
    } catch (...) {
        return false;
    }
}

bool Settings::SaveToJson(const std::filesystem::path& file) {
    nlohmann::json j;
    j["kReduceOutOfCombat"] = reduceOutOfCombat.load();
    j["kReduceJoggingOutOfCombat"] = reduceJoggingOutOfCombat.load();
    j["kReduceDrawn"] = reduceDrawn.load();
    j["kReduceSneak"] = reduceSneak.load();
    j["kIncreaseSprinting"] = increaseSprinting.load();
    j["kNoReductionInCombat"] = noReductionInCombat.load();
    j["kToggleSpeedKey"] = toggleSpeedKey.load();
    j["kToggleSpeedEvent"] = toggleSpeedEvent;
    j["kSprintEventName"] = sprintEventName;

    auto dumpList = [](const std::vector<FormSpec>& v) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& fs : v) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "0x%06X", fs.id);
            nlohmann::json e;
            e["form"] = fs.plugin + "|" + std::string(buf);
            e["value"] = fs.value;
            arr.push_back(std::move(e));
        }
        return arr;
    };
    j["kReduceInLocationType"] = dumpList(reduceInLocationType);
    j["kReduceInLocationSpecific"] = dumpList(reduceInLocationSpecific);

    j["kLocationAffects"] = (locationAffects == LocationAffects::AllStates) ? "all" : "default";
    j["kLocationMode"] = (locationMode == LocationMode::Add) ? "add" : "replace";

    std::ofstream out(file);
    if (!out.is_open()) return false;
    out << j.dump(4);
    return true;
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


    reduceInLocationType.clear();
    reduceInLocationSpecific.clear();

    auto loadList = [](const nlohmann::json& arr, std::vector<FormSpec>& out) {
        if (!arr.is_array()) return;
        for (auto& e : arr) {
            if (!e.is_object()) continue;
            FormSpec fs;
            if (!e.contains("form") || !e.contains("value")) continue;
            if (!e["form"].is_string() || !e["value"].is_number()) continue;
            std::string spec = e["form"].get<std::string>();
            if (!Settings::ParseFormSpec(spec, fs.plugin, fs.id)) continue;
            fs.value = clampf(e["value"].get<float>(), 0.f, 100.f);
            out.push_back(std::move(fs));
        }
    };

    if (j.contains("kReduceInLocationType")) {
        loadList(j["kReduceInLocationType"], reduceInLocationType);
    }
    if (j.contains("kReduceInLocationSpecific")) {
        loadList(j["kReduceInLocationSpecific"], reduceInLocationSpecific);
    }

    if (j.contains("kLocationAffects") && j["kLocationAffects"].is_string()) {
        auto s = j["kLocationAffects"].get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        locationAffects = (s == "all") ? LocationAffects::AllStates : LocationAffects::DefaultOnly;
    }
    if (j.contains("kLocationMode") && j["kLocationMode"].is_string()) {
        auto s = j["kLocationMode"].get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        locationMode = (s == "add") ? LocationMode::Add : LocationMode::Replace;
    }

    return true;
}
