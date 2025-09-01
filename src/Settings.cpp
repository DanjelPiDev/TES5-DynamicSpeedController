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
        id = static_cast<std::uint32_t>(std::stoul(idstr, nullptr, 0));  // 0 => 0x... or decimal
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

    j["kAttackSpeedEnabled"] = attackSpeedEnabled.load();
    j["kAttackOnlyWhenDrawn"] = attackOnlyWhenDrawn.load();
    j["kEnableSpeedScalingForNPCs"] = enableSpeedScalingForNPCs.load();
    j["kEnableDiagonalSpeedFix"] = enableDiagonalSpeedFix.load();
    j["kEnableDiagonalSpeedFixForNPCs"] = enableDiagonalSpeedFixForNPCs.load();
    j["kIgnoreBeastForms"] = ignoreBeastForms.load();
    j["kAttackBase"] = attackBase.load();
    j["kWeightPivot"] = weightPivot.load();
    j["kWeightSlope"] = weightSlope.load();
    j["kUsePlayerScale"] = usePlayerScale.load();
    j["kScaleSlope"] = scaleSlope.load();
    j["kMinAttackMult"] = minAttackMult.load();
    j["kMaxAttackMult"] = maxAttackMult.load();
    j["kSmoothingEnabled"] = smoothingEnabled.load();
    j["kSmoothingAffectsNPCs"] = smoothingAffectsNPCs.load();
    j["kSmoothingBypassOnStateChange"] = smoothingBypassOnStateChange.load();
    j["kSprintAffectsCombat"] = sprintAffectsCombat.load();

    switch (smoothingMode) {
        case SmoothingMode::Exponential:
            j["kSmoothingMode"] = "Exponential";
            break;
        case SmoothingMode::RateLimit:
            j["kSmoothingMode"] = "RateLimit";
            break;
        case SmoothingMode::ExpoThenRate:
            j["kSmoothingMode"] = "ExpoThenRate";
            break;
    }

    j["kSmoothingHalfLifeMs"] = smoothingHalfLifeMs.load();
    j["kSmoothingMaxChangePerSecond"] = smoothingMaxChangePerSecond.load();
    j["kSprintAnimOwnSmoothing"] = sprintAnimOwnSmoothing.load();
    j["kSprintAnimMode"] = sprintAnimSmoothingMode.load();  // 0=Expo, 1=Rate, 2=ExpoThenRate
    j["kSprintAnimTau"] = sprintAnimTau.load();
    j["kSprintAnimRatePerSec"] = sprintAnimRatePerSec.load();

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
    j["kLocationMode"] = (locationMode == LocationMode::Add) ? "add" : (locationMode == LocationMode::Replace) ? "replace" : "ignore";
    j["kMinFinalSpeedMult"] = minFinalSpeedMult.load();
    j["kSyncSprintAnimToSpeed"] = syncSprintAnimToSpeed.load();
    j["kOnlySlowDown"] = onlySlowDown.load();
    j["kSprintAnimMin"] = sprintAnimMin.load();
    j["kSprintAnimMax"] = sprintAnimMax.load();
    j["kEventDebounceMs"] = eventDebounceMs.load();

    j["kSlopeEnabled"] = slopeEnabled.load();
    j["kSlopeAffectsNPCs"] = slopeAffectsNPCs.load();
    j["kSlopeUphillPerDeg"] = slopeUphillPerDeg.load();
    j["kSlopeDownhillPerDeg"] = slopeDownhillPerDeg.load();
    j["kSlopeMaxAbs"] = slopeMaxAbs.load();
    j["kSlopeTau"] = slopeTau.load();
    j["kSlopeClampEnabled"] = slopeClampEnabled.load();
    j["kSlopeMinFinal"] = slopeMinFinal.load();
    j["kSlopeMaxFinal"] = slopeMaxFinal.load();
    j["kSlopeMethod"] = slopeMethod.load();
    j["kSlopeLookbackUnits"] = slopeLookbackUnits.load();
    j["kSlopeMaxHistorySec"] = slopeMaxHistorySec.load();
    j["kSlopeMinXYPerFrame"] = slopeMinXYPerFrame.load();
    j["kSlopeMedianN"] = slopeMedianN.load();

    j["kArmorAffectsMovement"] = armorAffectsMovement.load();
    j["kArmorAffectsAttackSpeed"] = armorAffectsAttackSpeed.load();
    j["kUseMaxArmorWeight"] = useMaxArmorWeight.load();
    j["kArmorWeightPivot"] = armorWeightPivot.load();
    j["kArmorWeightSlopeSM"] = armorWeightSlopeSM.load();
    j["kArmorMoveMin"] = armorMoveMin.load();
    j["kArmorMoveMax"] = armorMoveMax.load();
    j["kArmorWeightSlopeAtk"] = armorWeightSlopeAtk.load();
    j["kNpcRadius"] = npcRadius.load();
    j["kNpcPercentOfPlayer"] = npcPercentOfPlayer.load();
    j["kWeatherEnabled"] = weatherEnabled.load();
    j["kWeatherAffects"] = (weatherAffects == WeatherAffects::AllStates) ? "all" : "default";
    j["kWeatherMode"] = (weatherMode == WeatherMode::Add) ? "add" : "replace";
    j["kWeatherPresets"] = dumpList(reduceInWeatherSpecific);
    j["kWeatherIgnoreInterior"] = weatherIgnoreInterior.load();

    j["kHealthEnabled"] = healthEnabled.load();
    j["kHealthThresholdPct"] = healthThresholdPct.load();
    j["kHealthReducePct"] = healthReducePct.load();
    j["kHealthSmoothWidthPct"] = healthSmoothWidthPct.load();

    j["kStaminaEnabled"] = staminaEnabled.load();
    j["kStaminaThresholdPct"] = staminaThresholdPct.load();
    j["kStaminaReducePct"] = staminaReducePct.load();
    j["kStaminaSmoothWidthPct"] = staminaSmoothWidthPct.load();

    j["kMagickaEnabled"] = magickaEnabled.load();
    j["kMagickaThresholdPct"] = magickaThresholdPct.load();
    j["kMagickaReducePct"] = magickaReducePct.load();
    j["kMagickaSmoothWidthPct"] = magickaSmoothWidthPct.load();

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

    if (j.contains("kAttackSpeedEnabled")) {
        attackSpeedEnabled = j["kAttackSpeedEnabled"].get<bool>();
    }
    if (j.contains("kAttackOnlyWhenDrawn")) {
        attackOnlyWhenDrawn = j["kAttackOnlyWhenDrawn"].get<bool>();
    }
    if (j.contains("kEnableSpeedScalingForNPCs")) {
        enableSpeedScalingForNPCs = j["kEnableSpeedScalingForNPCs"].get<bool>();
    }
    if (j.contains("kIgnoreBeastForms")) {
        ignoreBeastForms = j["kIgnoreBeastForms"].get<bool>();
    }
    if (j.contains("kAttackBase")) {
        attackBase = j["kAttackBase"].get<float>();
    }
    if (j.contains("kWeightPivot")) {
        weightPivot = j["kWeightPivot"].get<float>();
    }
    if (j.contains("kWeightSlope")) {
        weightSlope = j["kWeightSlope"].get<float>();
    }
    if (j.contains("kUsePlayerScale")) {
        usePlayerScale = j["kUsePlayerScale"].get<bool>();
    }
    if (j.contains("kScaleSlope")) {
        scaleSlope = j["kScaleSlope"].get<float>();
    }
    if (j.contains("kMinAttackMult")) {
        minAttackMult = j["kMinAttackMult"].get<float>();
    }
    if (j.contains("kMaxAttackMult")) {
        maxAttackMult = j["kMaxAttackMult"].get<float>();
    }
    if (j.contains("kEnableDiagonalSpeedFix")) {
        enableDiagonalSpeedFix = j["kEnableDiagonalSpeedFix"].get<bool>();
    }
    if (j.contains("kEnableDiagonalSpeedFixForNPCs")) {
        enableDiagonalSpeedFixForNPCs = j["kEnableDiagonalSpeedFixForNPCs"].get<bool>();
    }
    if (j.contains("kSmoothingEnabled")) {
        smoothingEnabled = j["kSmoothingEnabled"].get<bool>();
    }
    if (j.contains("kSmoothingAffectsNPCs")) {
        smoothingAffectsNPCs = j["kSmoothingAffectsNPCs"].get<bool>();
    }
    if (j.contains("kSmoothingBypassOnStateChange")) {
        smoothingBypassOnStateChange = j["kSmoothingBypassOnStateChange"].get<bool>();
    }
    if (j.contains("kSmoothingHalfLifeMs")) {
        float v = j["kSmoothingHalfLifeMs"].get<float>();
        smoothingHalfLifeMs = std::clamp(v, 1.0f, 5000.0f);
    }
    if (j.contains("kSmoothingMaxChangePerSecond")) {
        float v = j["kSmoothingMaxChangePerSecond"].get<float>();
        smoothingMaxChangePerSecond = std::clamp(v, 1.0f, 1000.0f);
    }
    if (j.contains("kSmoothingMode")) {
        std::string m = j["kSmoothingMode"].get<std::string>();
        if (m == "Exponential")
            smoothingMode = SmoothingMode::Exponential;
        else if (m == "RateLimit")
            smoothingMode = SmoothingMode::RateLimit;
        else if (m == "ExpoThenRate")
            smoothingMode = SmoothingMode::ExpoThenRate;
    }
    if (j.contains("kMinFinalSpeedMult")) {
        float v = j["kMinFinalSpeedMult"].get<float>();
        minFinalSpeedMult = std::clamp(v, 0.0f, 100.0f);
    }
    if (j.contains("kSyncSprintAnimToSpeed")) {
        syncSprintAnimToSpeed = j["kSyncSprintAnimToSpeed"].get<bool>();
    }
    if (j.contains("kOnlySlowDown")) {
        onlySlowDown = j["kOnlySlowDown"].get<bool>();
    }
    if (j.contains("kSprintAnimMin")) {
        float v = j["kSprintAnimMin"].get<float>();
        sprintAnimMin = std::clamp(v, 0.1f, 10.0f);
    }
    if (j.contains("kSprintAnimMax")) {
        float v = j["kSprintAnimMax"].get<float>();
        sprintAnimMax = std::clamp(v, 0.1f, 10.0f);
    }
    if (j.contains("kSprintAffectsCombat")) {
        sprintAffectsCombat = j["kSprintAffectsCombat"].get<bool>();
    }
    if (j.contains("kSprintAnimOwnSmoothing")) {
        sprintAnimOwnSmoothing = j["kSprintAnimOwnSmoothing"].get<bool>();
    }
    if (j.contains("kSprintAnimMode")) {
        sprintAnimSmoothingMode = j["kSprintAnimMode"].get<int>();
    }
    if (j.contains("kSprintAnimTau")) {
        sprintAnimTau = j["kSprintAnimTau"].get<float>();
    }
    if (j.contains("kSprintAnimRatePerSec")) {
        sprintAnimRatePerSec = j["kSprintAnimRatePerSec"].get<float>();
    }
    if (j.contains("kEventDebounceMs")) {
        eventDebounceMs = j["kEventDebounceMs"].get<int>();
    }

    reduceInLocationType.clear();
    reduceInLocationSpecific.clear();
    reduceInWeatherSpecific.clear();

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
        if (s == "add") locationMode = LocationMode::Add;
        else if (s == "replace") locationMode = LocationMode::Replace;
        else if (s == "ignore") locationMode = LocationMode::Ignore;
    }
    if (j.contains("kSlopeEnabled")) {
        slopeEnabled = j["kSlopeEnabled"].get<bool>();
    }
    if (j.contains("kSlopeAffectsNPCs")) {
        slopeAffectsNPCs = j["kSlopeAffectsNPCs"].get<bool>();
    }
    if (j.contains("kSlopeUphillPerDeg")) {
        slopeUphillPerDeg = std::clamp(j["kSlopeUphillPerDeg"].get<float>(), 0.0f, 5.0f);
    }
    if (j.contains("kSlopeDownhillPerDeg")) {
        slopeDownhillPerDeg = std::clamp(j["kSlopeDownhillPerDeg"].get<float>(), 0.0f, 5.0f);
    }
    if (j.contains("kSlopeMaxAbs")) {
        slopeMaxAbs = std::clamp(j["kSlopeMaxAbs"].get<float>(), 0.0f, 100.0f);
    }
    if (j.contains("kSlopeTau")) {
        slopeTau = std::clamp(j["kSlopeTau"].get<float>(), 0.01f, 5.0f);
    }
    if (j.contains("kSlopeClampEnabled")) {
        slopeClampEnabled = j["kSlopeClampEnabled"].get<bool>();
    }
    if (j.contains("kSlopeMinFinal")) {
        slopeMinFinal = j["kSlopeMinFinal"].get<float>();
    }
    if (j.contains("kSlopeMaxFinal")) {
        slopeMaxFinal = j["kSlopeMaxFinal"].get<float>();
    }
    if (j.contains("kSlopeMethod")) {
        int m = j["kSlopeMethod"].get<int>();
        slopeMethod = std::clamp(m, 0, 1);
    }
    if (j.contains("kSlopeLookbackUnits")) {
        float v = j["kSlopeLookbackUnits"].get<float>();
        slopeLookbackUnits = std::max(0.0f, v);
    }
    if (j.contains("kSlopeMaxHistorySec")) {
        float v = j["kSlopeMaxHistorySec"].get<float>();
        slopeMaxHistorySec = std::max(0.01f, v);
    }
    if (j.contains("kSlopeMinXYPerFrame")) {
        float v = j["kSlopeMinXYPerFrame"].get<float>();
        slopeMinXYPerFrame = std::max(0.01f, v);
    }
    if (j.contains("kSlopeMedianN")) {
        int n = j["kSlopeMedianN"].get<int>();
        slopeMedianN = std::clamp(n, 1, 10);
    }
    if (j.contains("kArmorAffectsMovement")) {
        armorAffectsMovement = j["kArmorAffectsMovement"].get<bool>();
    }
    if (j.contains("kArmorAffectsAttackSpeed")) {
        armorAffectsAttackSpeed = j["kArmorAffectsAttackSpeed"].get<bool>();
    }
    if (j.contains("kUseMaxArmorWeight")) {
        useMaxArmorWeight = j["kUseMaxArmorWeight"].get<bool>();
    }
    if (j.contains("kArmorWeightPivot")) {
        float v = j["kArmorWeightPivot"].get<float>();
        armorWeightPivot = std::max(0.0f, v);
    }
    if (j.contains("kArmorWeightSlopeSM")) {
        float v = j["kArmorWeightSlopeSM"].get<float>();
        armorWeightSlopeSM = std::clamp(v, -10.0f, 10.0f);
    }
    if (j.contains("kArmorMoveMin")) {
        armorMoveMin = j["kArmorMoveMin"].get<float>();
    }
    if (j.contains("kArmorMoveMax")) {
        armorMoveMax = j["kArmorMoveMax"].get<float>();
    }
    if (armorMoveMin > armorMoveMax) {
        float a = armorMoveMin.load(), b = armorMoveMax.load();
        armorMoveMin = std::min(a, b);
        armorMoveMax = std::max(a, b);
    }
    if (j.contains("kArmorWeightSlopeAtk")) {
        float v = j["kArmorWeightSlopeAtk"].get<float>();
        armorWeightSlopeAtk = std::clamp(v, -1.0f, 1.0f);
    }
    if (j.contains("kNpcRadius")) {
        float r = j["kNpcRadius"].get<int>();
        // Maximum radius is 4096 * 4 = 16384 (max load distance for NPCs)
        npcRadius = static_cast<int>(std::clamp(r, 0.0f, 4096.0f * 4.0f));
    }
    if (j.contains("kNpcPercentOfPlayer")) {
        float v = j["kNpcPercentOfPlayer"].get<float>();
        npcPercentOfPlayer = clampf(v, 0.0f, 200.0f);
    }
    if (j.contains("kWeatherPresets")) {
        loadList(j["kWeatherPresets"], reduceInWeatherSpecific);
    }
    if (j.contains("kWeatherEnabled")) {
        weatherEnabled = j["kWeatherEnabled"].get<bool>();
    }
    if (j.contains("kWeatherAffects") && j["kWeatherAffects"].is_string()) {
        auto s = j["kWeatherAffects"].get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        weatherAffects = (s == "all") ? WeatherAffects::AllStates : WeatherAffects::DefaultOnly;
    }
    if (j.contains("kWeatherMode") && j["kWeatherMode"].is_string()) {
        auto s = j["kWeatherMode"].get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        weatherMode = (s == "add") ? WeatherMode::Add : WeatherMode::Replace;
    }
    if (j.contains("kWeatherIgnoreInterior")) {
        weatherIgnoreInterior = j["kWeatherIgnoreInterior"].get<bool>();
    }

    auto clamp01 = [](float v) { return std::max(0.0f, std::min(100.0f, v)); };
    if (j.contains("kHealthEnabled")) healthEnabled = j["kHealthEnabled"].get<bool>();
    if (j.contains("kHealthThresholdPct")) healthThresholdPct = clamp01(j["kHealthThresholdPct"].get<float>());
    if (j.contains("kHealthReducePct")) healthReducePct = clamp01(j["kHealthReducePct"].get<float>());
    if (j.contains("kHealthSmoothWidthPct")) healthSmoothWidthPct = clamp01(j["kHealthSmoothWidthPct"].get<float>());

    if (j.contains("kStaminaEnabled")) staminaEnabled = j["kStaminaEnabled"].get<bool>();
    if (j.contains("kStaminaThresholdPct")) staminaThresholdPct = clamp01(j["kStaminaThresholdPct"].get<float>());
    if (j.contains("kStaminaReducePct")) staminaReducePct = clamp01(j["kStaminaReducePct"].get<float>());
    if (j.contains("kStaminaSmoothWidthPct")) staminaSmoothWidthPct = clamp01(j["kStaminaSmoothWidthPct"].get<float>());

    if (j.contains("kMagickaEnabled")) magickaEnabled = j["kMagickaEnabled"].get<bool>();
    if (j.contains("kMagickaThresholdPct")) magickaThresholdPct = clamp01(j["kMagickaThresholdPct"].get<float>());
    if (j.contains("kMagickaReducePct")) magickaReducePct = clamp01(j["kMagickaReducePct"].get<float>());
    if (j.contains("kMagickaSmoothWidthPct")) magickaSmoothWidthPct = clamp01(j["kMagickaSmoothWidthPct"].get<float>());
    return true;
}
