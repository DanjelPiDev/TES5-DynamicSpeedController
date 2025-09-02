#include "UI.h"

#ifndef IM_ARRAYSIZE
    #define IM_ARRAYSIZE(_ARR) ((int)(sizeof(_ARR) / sizeof(*(_ARR))))
#endif


static int findIndex(const std::string& cur, const char* const* arr, int n) {
    for (int i = 0; i < n; ++i)
        if (cur == arr[i]) return i;
    return -1;
}

static bool MakeFormSpecFromForm(RE::TESForm* form, std::string& out) {
    if (!form) return false;
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return false;

    const std::uint32_t fid = form->GetFormID();
    const bool isLight = (fid & 0xFE000000) == 0xFE000000;

    std::string_view fname_sv;
    std::uint32_t localId = 0;

    if (isLight) {
        std::uint16_t lightIdx = static_cast<std::uint16_t>((fid & 0x00FFF000) >> 12);
        if (auto* f = dh->LookupLoadedLightModByIndex(lightIdx)) {
            fname_sv = f->GetFilename();
            localId = fid & 0x00000FFF;
        }
    } else {
        std::uint8_t modIdx = static_cast<std::uint8_t>(fid >> 24);
        if (auto* f = dh->LookupLoadedModByIndex(modIdx)) {
            fname_sv = f->GetFilename();
            localId = fid & 0x00FFFFFF;
        }
    }

    if (fname_sv.empty()) return false;

    char buf[300];
    std::snprintf(buf, sizeof(buf), "%.*s|0x%06X", static_cast<int>(fname_sv.size()), fname_sv.data(), localId);

    out.assign(buf);
    return true;
}

static bool GetCurrentLocationSpec(std::string& out) {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (!pc) return false;
    RE::BGSLocation* loc = nullptr;

    if (auto* l = pc->GetCurrentLocation())
        loc = l;
    else if (auto* cell = pc->GetParentCell())
        loc = cell->GetLocation();

    if (!loc) return false;
    return MakeFormSpecFromForm(loc, out);
}


static RE::BGSLocation* GetCurrentLocationPtr() {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (!pc) return nullptr;
    if (auto* l = pc->GetCurrentLocation()) return l;
    if (auto* cell = pc->GetParentCell()) return cell->GetLocation();
    return nullptr;
}

static void CollectKeywordsForLocation(RE::BGSLocation* loc,
                                       std::vector<std::pair<std::string, std::string>>& outNameSpec) {
    outNameSpec.clear();
    if (!loc) return;

    std::unordered_set<RE::FormID> seen;
    for (auto* p = loc; p; p = p->parentLoc) {
        const std::uint32_t n = p->GetNumKeywords();
        for (std::uint32_t i = 0; i < n; ++i) {
            auto kwOpt = p->GetKeywordAt(i);
            if (!kwOpt.has_value() || !kwOpt.value()) continue;
            auto* kw = kwOpt.value();
            if (!seen.insert(kw->GetFormID()).second) continue;

            std::string spec;
            if (!MakeFormSpecFromForm(kw, spec)) continue;

            const char* edid = kw->GetFormEditorID();
            std::string display = (edid && *edid) ? std::string(edid) : spec;
            outNameSpec.emplace_back(std::move(display), std::move(spec));
        }
    }

    std::sort(outNameSpec.begin(), outNameSpec.end(), [](auto& a, auto& b) { return a.first < b.first; });
}


void UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }
    SKSEMenuFramework::SetSection("Dynamic Speed Controller");
    SKSEMenuFramework::AddSectionItem("General Settings", SpeedConfig::RenderGeneral);
    SKSEMenuFramework::AddSectionItem("Speed Settings", SpeedConfig::Render);
    SKSEMenuFramework::AddSectionItem("Attack Settings", SpeedConfig::RenderAttack);
    SKSEMenuFramework::AddSectionItem("Vitals & Resources", SpeedConfig::RenderVitals);
    SKSEMenuFramework::AddSectionItem("Location Rules", SpeedConfig::RenderLocations);
    SKSEMenuFramework::AddSectionItem("Weather Presets", SpeedConfig::RenderWeather);
    SKSEMenuFramework::AddSectionItem("Add-ons", SpeedConfig::RenderAddons);
}

void __stdcall UI::SpeedConfig::RenderGeneral() {
    ImGui::Text("General Speed Modifiers");

    bool ignoreBeast = Settings::ignoreBeastForms.load();
    if (ImGui::Checkbox("Ignore in Werewolf/Vampire form?", &ignoreBeast)) {
        Settings::ignoreBeastForms.store(ignoreBeast);
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(npcsHeader.c_str())) {
        bool affectNPCs = Settings::enableSpeedScalingForNPCs.load();
        if (ImGui::Checkbox("Affect NPCs too?", &affectNPCs)) {
            Settings::enableSpeedScalingForNPCs.store(affectNPCs);
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
        }

        int r = Settings::npcRadius.load();
        if (ImGui::SliderInt("NPC Radius (0 = All)", &r, 0, 16384)) {
            Settings::npcRadius.store(r);
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
                SpeedController::GetSingleton()->RefreshNow();
            }
        }
        ImGui::TextDisabled("Hint: NPCs outside the radius being automatically reverted to vanilla Skyrim movements.");

        float npcPct = Settings::npcPercentOfPlayer.load();
        if (ImGui::SliderFloat("NPC % of Player effect", &npcPct, 0.0f, 200.0f, "%.0f%%")) {
            Settings::npcPercentOfPlayer.store(npcPct);
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
                SpeedController::GetSingleton()->RefreshNow();
            }
        }
        ImGui::TextDisabled("NPCs apply only this percent of the player's movement modifiers.");
        ImGui::TextDisabled("This is to account for NPCs being generally slower than players.");
    }
    FontAwesome::Pop();

    ImGui::Separator();

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(fixesHeader.c_str())) {
        bool dfix = Settings::enableDiagonalSpeedFix.load();
        if (ImGui::Checkbox("Diagonal Speed Fix (Player)", &dfix)) {
            Settings::enableDiagonalSpeedFix.store(dfix);
        }

        bool affectNPCs = Settings::enableSpeedScalingForNPCs.load();
        bool dfixNPC = Settings::enableDiagonalSpeedFixForNPCs.load();
        if (!affectNPCs) {
            ImGui::TextDisabled("Enable 'Affect NPCs too?' in General Settings to modify NPC diagonal-fix settings.");
        } else {
            if (ImGui::Checkbox("Diagonal Speed Fix for NPCs", &dfixNPC)) {
                Settings::enableDiagonalSpeedFixForNPCs.store(dfixNPC);
            }
        }

        bool scEn = Settings::scaleCompEnabled.load();
        if (ImGui::Checkbox("Actor Scale Compensation (movement)", &scEn)) {
            Settings::scaleCompEnabled.store(scEn);
        }
        int scMode = static_cast<int>(Settings::scaleCompMode);
        const char* modes[] = {"Additive (per size diff)", "Inverse (divide by Scale)"};
        if (ImGui::Combo("Scale Compensation Mode", &scMode, modes, IM_ARRAYSIZE(modes))) {
            Settings::scaleCompMode = static_cast<Settings::ScaleCompMode>(scMode);
        }
        if (Settings::scaleCompMode == Settings::ScaleCompMode::Additive) {
            float scPer = Settings::scaleCompPerUnitSM.load();
            if (ImGui::SliderFloat("Per 1.0 size difference (SpeedMult)", &scPer, -200.0f, 200.0f, "%.1f")) {
                Settings::scaleCompPerUnitSM.store(scPer);
            }
            ImGui::TextDisabled("Delta SpeedMult = k * (1 − Scale).");
        } else {
            ImGui::TextDisabled("final SpeedMult is divided by Scale (* 1/Scale).");
        }
        bool belowOnly = Settings::scaleCompOnlyBelowOne.load();
        if (ImGui::Checkbox("Only compensate when Scale < 1.0", &belowOnly)) {
            Settings::scaleCompOnlyBelowOne.store(belowOnly);
        }
        ImGui::TextDisabled("NPCs inherit this via 'Affect NPCs too?' and NPC percentage of Player.");
    }
    FontAwesome::Pop();

    ImGui::Separator();

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(smootingAccelerationHeader.c_str())) {
        int eventDebounceMs = Settings::eventDebounceMs.load();
        if (ImGui::SliderInt("Event Debounce (ms)", &eventDebounceMs, 1, 100)) {
            Settings::eventDebounceMs.store(eventDebounceMs);
        }

        bool smooth = Settings::smoothingEnabled.load();
        if (ImGui::Checkbox("Enable smoothing", &smooth)) {
            Settings::smoothingEnabled.store(smooth);
        }

        bool smNpc = Settings::smoothingAffectsNPCs.load();
        if (ImGui::Checkbox("Affects NPCs too?", &smNpc)) {
            Settings::smoothingAffectsNPCs.store(smNpc);
        }
        ImGui::TextDisabled("This is separated from NPCs toggle because NPCs are less sensitive to sudden changes.");

        bool bypass = Settings::smoothingBypassOnStateChange.load();
        if (ImGui::Checkbox("Bypass on major state change (sprint/drawn/sneak)", &bypass)) {
            Settings::smoothingBypassOnStateChange.store(bypass);
        }

        int mode = static_cast<int>(Settings::smoothingMode);
        const char* modes[] = {"Exponential", "RateLimit", "ExpoThenRate"};
        if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
            Settings::smoothingMode = static_cast<Settings::SmoothingMode>(mode);
        }

        float hl = Settings::smoothingHalfLifeMs.load();
        if (ImGui::SliderFloat("Half-life (ms)", &hl, 1.0f, 2000.0f, "%.0f")) {
            Settings::smoothingHalfLifeMs.store(hl);
        }

        float maxrate = Settings::smoothingMaxChangePerSecond.load();
        if (ImGui::SliderFloat("Max Delta per second", &maxrate, 1.0f, 300.0f, "%.0f")) {
            Settings::smoothingMaxChangePerSecond.store(maxrate);
        }
    }
    FontAwesome::Pop();

    // kEventDebounceMs
    ImGui::Separator();

    FontAwesome::PushSolid();
    if (ImGui::Button(saveIcon.c_str())) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
    FontAwesome::Pop();
}

void __stdcall UI::SpeedConfig::Render() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(movementSpeedHeader.c_str())) {
        float minFinalSpeedMult = Settings::minFinalSpeedMult.load();
        if (ImGui::SliderFloat("Minimal Final SpeedMult", &minFinalSpeedMult, 0.0f, 100.0f, "%.1f")) {
            Settings::minFinalSpeedMult.store(minFinalSpeedMult);
        }

        float reduceOutOfCombatVal = Settings::reduceOutOfCombat.load();
        if (ImGui::SliderFloat("Reduce Out of Combat", &reduceOutOfCombatVal, 0.0f, 100.0f, "%.1f")) {
            Settings::reduceOutOfCombat.store(reduceOutOfCombatVal);
        }

        float reduceJoggingVal = Settings::reduceJoggingOutOfCombat.load();
        if (ImGui::SliderFloat("Reduce Jogging Out of Combat", &reduceJoggingVal, 0.0f, 100.0f, "%.1f")) {
            Settings::reduceJoggingOutOfCombat.store(reduceJoggingVal);
        }

        float reduceDrawnVal = Settings::reduceDrawn.load();
        if (ImGui::SliderFloat("Reduce Drawn", &reduceDrawnVal, 0.0f, 100.0f, "%.1f")) {
            Settings::reduceDrawn.store(reduceDrawnVal);
        }

        float reduceSneakVal = Settings::reduceSneak.load();
        if (ImGui::SliderFloat("Reduce Sneak", &reduceSneakVal, 0.0f, 100.0f, "%.1f")) {
            Settings::reduceSneak.store(reduceSneakVal);
        }

        float increaseSprintVal = Settings::increaseSprinting.load();
        if (ImGui::SliderFloat("Increase Sprinting", &increaseSprintVal, 0.0f, 100.0f, "%.1f")) {
            Settings::increaseSprinting.store(increaseSprintVal);
        }

        bool sprintAffectsCombat = Settings::sprintAffectsCombat.load();
        if (ImGui::Checkbox("Sprint Affects Combat", &sprintAffectsCombat)) {
            Settings::sprintAffectsCombat.store(sprintAffectsCombat);
        }

        bool noReductionInCombatVal = Settings::noReductionInCombat.load();
        if (ImGui::Checkbox("No Reduction In Combat", &noReductionInCombatVal)) {
            Settings::noReductionInCombat.store(noReductionInCombatVal);
        }
    }
    FontAwesome::Pop();

    ImGui::Separator();
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(armorHeader.c_str())) {
        bool m = Settings::armorAffectsMovement.load();
        if (ImGui::Checkbox("Affect Movement by Armor Weight", &m)) {
            Settings::armorAffectsMovement.store(m);
        }

        bool useMax = Settings::useMaxArmorWeight.load();
        if (ImGui::Checkbox("Use Max Equipped Armor Weight (vs Sum)", &useMax)) {
            Settings::useMaxArmorWeight.store(useMax);
        }

        float ap = Settings::armorWeightPivot.load();
        if (ImGui::SliderFloat("Armor Weight Pivot", &ap, 0.0f, 80.0f, "%.1f")) {
            Settings::armorWeightPivot.store(ap);
        }

        float smSlope = Settings::armorWeightSlopeSM.load();
        if (ImGui::SliderFloat("Armor -> SpeedMult Slope (per weight)", &smSlope, -5.0f, 5.0f, "%.2f")) {
            Settings::armorWeightSlopeSM.store(smSlope);
        }

        float lo = Settings::armorMoveMin.load();
        float hi = Settings::armorMoveMax.load();
        if (ImGui::DragFloat2("Clamp Movement Delta [min, max]", &lo, 0.1f, -200.0f, 200.0f, "%.1f")) {
            Settings::armorMoveMin.store(lo);
            Settings::armorMoveMax.store(hi);
        }

        ImGui::Separator();
        bool aatk = Settings::armorAffectsAttackSpeed.load();
        if (ImGui::Checkbox("Also affect Attack Speed", &aatk)) {
            Settings::armorAffectsAttackSpeed.store(aatk);
        }

        float atkSlope = Settings::armorWeightSlopeAtk.load();
        if (ImGui::SliderFloat("Armor -> Attack Slope (per weight)", &atkSlope, -0.20f, 0.20f, "%.3f")) {
            Settings::armorWeightSlopeAtk.store(atkSlope);
        }

        ImGui::TextDisabled("Hint: Negative slopes = heavier => slower.");
    }
    FontAwesome::Pop();

    ImGui::Separator();

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(slopeTerrainHeader.c_str())) {
        bool slopeEnabled = Settings::slopeEnabled.load();
        if (ImGui::Checkbox("Enable slope effect", &slopeEnabled)) {
            Settings::slopeEnabled.store(slopeEnabled);
        }
        bool affectNPCs = Settings::enableSpeedScalingForNPCs.load();
        if (!affectNPCs) {
            ImGui::TextDisabled("Enable 'Affect NPCs too?' in General Settings to modify NPC slope settings.");
        } else {
            bool slopeNPC = Settings::slopeAffectsNPCs.load();
            if (ImGui::Checkbox("Affect NPCs too?", &slopeNPC)) {
                Settings::slopeAffectsNPCs.store(slopeNPC);
            }
        }

        float slopeUphillPerDeg = Settings::slopeUphillPerDeg.load();
        ImGui::SliderFloat("Uphill per degree", &slopeUphillPerDeg, 0.0f, 5.0f, "%.2f");

        float slopeDownhillPerDeg = Settings::slopeDownhillPerDeg.load();
        ImGui::SliderFloat("Downhill per degree", &slopeDownhillPerDeg, 0.0f, 5.0f, "%.2f");

        float slopeMaxAbs = Settings::slopeMaxAbs.load();
        ImGui::SliderFloat("Max |slope delta|", &slopeMaxAbs, 0.0f, 100.0f, "%.1f");

        float slopeTau = Settings::slopeTau.load();
        ImGui::SliderFloat("Smoothing tau (s)", &slopeTau, 0.01f, 5.0f, "%.2f");

        ImGui::Separator();
        bool slopeClampEnabled = Settings::slopeClampEnabled.load();
        ImGui::Checkbox("Clamp final SpeedMult while slope is active", &slopeClampEnabled);

        float slopeMinFinal = Settings::slopeMinFinal.load();
        ImGui::SliderFloat("Slope Min Final", &slopeMinFinal, 0.0f, 500.0f, "%.0f");

        float slopeMaxFinal = Settings::slopeMaxFinal.load();
        ImGui::SliderFloat("Slope Max Final", &slopeMaxFinal, 0.0f, 500.0f, "%.0f");

        int slopeMethod = Settings::slopeMethod.load();
        const char* slopeMethods[] = {"Instant", "Path-based"};
        if (ImGui::Combo("Slope Method", &slopeMethod, slopeMethods, IM_ARRAYSIZE(slopeMethods))) {
            Settings::slopeMethod.store(slopeMethod);
        }
        float slopeLookbackUnits = Settings::slopeLookbackUnits.load();
        if (ImGui::SliderFloat("Slope Lookback Units", &slopeLookbackUnits, 0.0f, 200.0f, "%.1f")) {
            Settings::slopeLookbackUnits.store(slopeLookbackUnits);
        }
        float slopeMaxHistorySec = Settings::slopeMaxHistorySec.load();
        if (ImGui::SliderFloat("Slope Max History (sec)", &slopeMaxHistorySec, 0.0f, 5.0f, "%.2f")) {
            Settings::slopeMaxHistorySec.store(slopeMaxHistorySec);
        }
        float slopeMinXYPerFrame = Settings::slopeMinXYPerFrame.load();
        if (ImGui::SliderFloat("Slope Min XY Per Frame", &slopeMinXYPerFrame, 0.01f, 5.0f, "%.2f")) {
            Settings::slopeMinXYPerFrame.store(slopeMinXYPerFrame);
        }
        int slopeMedianN = Settings::slopeMedianN.load();
        if (ImGui::SliderInt("Slope Median N", &slopeMedianN, 1, 10)) {
            Settings::slopeMedianN.store(slopeMedianN);
        }
    }
    FontAwesome::Pop();

    ImGui::Separator();

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(animationsHeader.c_str())) {
        bool syncAnim = Settings::syncSprintAnimToSpeed.load();
        if (ImGui::Checkbox("Sync Sprint Animations to Speed", &syncAnim)) {
            Settings::syncSprintAnimToSpeed.store(syncAnim);
        }
        bool onlySlowDown = Settings::onlySlowDown.load();
        if (ImGui::Checkbox("Only Slow Down (No Speedup)", &onlySlowDown)) {
            Settings::onlySlowDown.store(onlySlowDown);
        }
        float sprintAnimMin = Settings::sprintAnimMin.load();
        if (ImGui::SliderFloat("Sprint Anim Min", &sprintAnimMin, 0.0f, 2.0f, "%.2f")) {
            Settings::sprintAnimMin.store(sprintAnimMin);
        }
        float sprintAnimMax = Settings::sprintAnimMax.load();
        if (ImGui::SliderFloat("Sprint Anim Max", &sprintAnimMax, 0.05f, 2.0f, "%.2f")) {
            Settings::sprintAnimMax.store(sprintAnimMax);
        }
        ImGui::Separator();
        ImGui::Text("Animation Smoothing");
        bool sprintAnimOwnSmoothing = Settings::sprintAnimOwnSmoothing.load();
        if (ImGui::Checkbox("Own Smoothing for Sprint Animations", &sprintAnimOwnSmoothing)) {
            Settings::sprintAnimOwnSmoothing.store(sprintAnimOwnSmoothing);
        }
        int sprintAnimMode = Settings::sprintAnimSmoothingMode.load();
        const char* sprintAnimModes[] = {"Exponential", "RateLimit", "ExpoThenRate"};
        if (ImGui::Combo("Smoothing Mode", &sprintAnimMode, sprintAnimModes, IM_ARRAYSIZE(sprintAnimModes))) {
            Settings::sprintAnimSmoothingMode.store(sprintAnimMode);
        }
        float sprintAnimTau = Settings::sprintAnimTau.load();
        if (ImGui::SliderFloat("Smoothing Tau (seconds)", &sprintAnimTau, 0.01f, 1.0f, "%.2f")) {
            Settings::sprintAnimTau.store(sprintAnimTau);
        }
        float sprintAnimRatePerSec = Settings::sprintAnimRatePerSec.load();
        if (ImGui::SliderFloat("Smoothing Rate (steps/sec)", &sprintAnimRatePerSec, 0.1f, 20.0f, "%.2f")) {
            Settings::sprintAnimRatePerSec.store(sprintAnimRatePerSec);
        }
    }
    FontAwesome::Pop();

    ImGui::Separator();

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(inputHeader.c_str())) {
        static const char* kUserEvents[] = {"Sprint",   "Sneak",        "Shout",    "Jump",
                                            "Activate", "Ready Weapon", "SwitchPOV"};
        constexpr int kEventsCount = sizeof(kUserEvents) / sizeof(kUserEvents[0]);
        constexpr int kCustomIdx = kEventsCount;

        std::string sprintEvt = Settings::sprintEventName;
        int sprintIdx = findIndex(sprintEvt, kUserEvents, kEventsCount);
        if (sprintIdx < 0) sprintIdx = kCustomIdx;

        if (ImGui::BeginCombo("Sprint Event", sprintIdx == kCustomIdx ? sprintEvt.c_str() : kUserEvents[sprintIdx])) {
            for (int i = 0; i < kEventsCount; ++i) {
                bool sel = (sprintIdx == i);
                if (ImGui::Selectable(kUserEvents[i], sel)) {
                    Settings::sprintEventName = kUserEvents[i];
                    SpeedController::GetSingleton()->UpdateBindingsFromSettings();
                }
            }
            bool selCustom = (sprintIdx == kCustomIdx);
            if (ImGui::Selectable("Custom...", selCustom)) {
            }
            ImGui::EndCombo();
        }

        static char sprintCustomBuf[64] = {};
        if (sprintIdx == kCustomIdx) {
            if (sprintCustomBuf[0] == '\0' && !sprintEvt.empty()) {
                std::snprintf(sprintCustomBuf, sizeof(sprintCustomBuf), "%s", sprintEvt.c_str());
            }
            if (ImGui::InputText("Custom Sprint Event", sprintCustomBuf, sizeof(sprintCustomBuf))) {
                Settings::sprintEventName = sprintCustomBuf;
                SpeedController::GetSingleton()->UpdateBindingsFromSettings();
            }
        }

        std::string toggleEvt = Settings::toggleSpeedEvent;
        int toggleIdx = findIndex(toggleEvt, kUserEvents, kEventsCount);
        if (toggleIdx < 0) toggleIdx = kCustomIdx;

        if (ImGui::BeginCombo("Toggle Event", toggleIdx == kCustomIdx ? toggleEvt.c_str() : kUserEvents[toggleIdx])) {
            for (int i = 0; i < kEventsCount; ++i) {
                bool sel = (toggleIdx == i);
                if (ImGui::Selectable(kUserEvents[i], sel)) {
                    Settings::toggleSpeedEvent = kUserEvents[i];
                    SpeedController::GetSingleton()->UpdateBindingsFromSettings();
                }
            }
            bool selCustom = (toggleIdx == kCustomIdx);
            if (ImGui::Selectable("Custom...", selCustom)) {
            }
            ImGui::EndCombo();
        }

        static char toggleCustomBuf[64] = {};
        if (toggleIdx == kCustomIdx) {
            if (toggleCustomBuf[0] == '\0' && !toggleEvt.empty()) {
                std::snprintf(toggleCustomBuf, sizeof(toggleCustomBuf), "%s", toggleEvt.c_str());
            }
            if (ImGui::InputText("Custom Toggle Event", toggleCustomBuf, sizeof(toggleCustomBuf))) {
                Settings::toggleSpeedEvent = toggleCustomBuf;
                SpeedController::GetSingleton()->UpdateBindingsFromSettings();
            }
        }

        int sc = Settings::toggleSpeedKey.load();
        if (ImGui::InputInt("Toggle Key (scancode)", &sc)) {
            Settings::toggleSpeedKey.store(sc);
            SpeedController::GetSingleton()->UpdateBindingsFromSettings();
        }
    }
    FontAwesome::Pop();

    FontAwesome::PushSolid();
    if (ImGui::Button(saveIcon.c_str())) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
    FontAwesome::Pop();
}

void __stdcall UI::SpeedConfig::RenderAttack() {
    ImGui::Text("Attack Speed");
    ImGui::Separator();

    bool atkEnabled = Settings::attackSpeedEnabled.load();
    if (ImGui::Checkbox("Enable Attack Speed Scaling", &atkEnabled)) {
        Settings::attackSpeedEnabled.store(atkEnabled);
    }

    bool drawnOnly = Settings::attackOnlyWhenDrawn.load();
    if (ImGui::Checkbox("Only apply when weapon drawn", &drawnOnly)) {
        Settings::attackOnlyWhenDrawn.store(drawnOnly);
    }

    float base = Settings::attackBase.load();
    if (ImGui::SliderFloat("Base Multiplier", &base, 0.3f, 20.0f, "%.2f")) {
        Settings::attackBase.store(base);
    }

    float pivot = Settings::weightPivot.load();
    if (ImGui::SliderFloat("Weight Pivot", &pivot, 0.0f, 50.0f, "%.1f")) {
        Settings::weightPivot.store(pivot);
    }

    float slope = Settings::weightSlope.load();
    if (ImGui::SliderFloat("Weight Slope (per weight unit)", &slope, -1.00f, 1.00f, "%.3f")) {
        Settings::weightSlope.store(slope);
    }

    bool useScale = Settings::usePlayerScale.load();
    if (ImGui::Checkbox("Scale attackspeed by Player Size", &useScale)) {
        Settings::usePlayerScale.store(useScale);
    }

    float ss = Settings::scaleSlope.load();
    if (ImGui::SliderFloat("Scale Slope (per +1.0 size)", &ss, -1.0f, 1.0f, "%.2f")) {
        Settings::scaleSlope.store(ss);
    }

    float minMul = Settings::minAttackMult.load();
    float maxMul = Settings::maxAttackMult.load();
    if (ImGui::DragFloat2("Clamp [min, max]", &minMul, 0.01f, 0.1f, 10.0f, "%.2f")) {
        minMul = std::max(0.1f, std::min(10.0f, minMul));
        Settings::minAttackMult.store(minMul);
    }
    if (ImGui::DragFloat("Max Attack Multiplier", &maxMul, 0.01f, 0.1f, 10.0f, "%.2f")) {
        maxMul = std::max(0.1f, std::min(10.0f, maxMul));
        Settings::maxAttackMult.store(maxMul);
    }

    FontAwesome::PushSolid();
    if (ImGui::Button(saveIcon.c_str())) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
    FontAwesome::Pop();
}

void __stdcall UI::SpeedConfig::RenderVitals() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(vitalsHeader.c_str())) {
        ImGui::TextDisabled("SpeedMult reduction when vital resources are low. Values are in SpeedMult points (%%).");
        auto drawBlock = [](const char* id, const char* labelEnabled, bool& en, float& thr, float& red, float& sw) {
            ImGui::PushID(id);
            if (ImGui::Checkbox(labelEnabled, &en)) {
                if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
            }
            ImGui::BeginDisabled(!en);
            ImGui::SliderFloat("Threshold (%)", &thr, 0.f, 100.f, "%.0f");
            ImGui::TextDisabled("At or below this percent, reduction starts.");
            ImGui::SliderFloat("Reduce (SpeedMult %pts)", &red, 0.f, 100.f, "%.0f");
            ImGui::TextDisabled("How many SpeedMult points to subtract at full effect.");
            ImGui::SliderFloat("Smoothing width (%)", &sw, 0.f, 100.f, "%.0f");
            ImGui::TextDisabled("Linear ramp. 0 = hard cutoff, larger = gentler blend below threshold.");
            ImGui::EndDisabled();
            ImGui::Separator();

            ImGui::PopID();
        };

        // Health
        bool hEn = Settings::healthEnabled.load();
        float hThr = Settings::healthThresholdPct.load();
        float hRed = Settings::healthReducePct.load();
        float hSw = Settings::healthSmoothWidthPct.load();
        drawBlock("Health", "Health affects speed", hEn, hThr, hRed, hSw);
        Settings::healthEnabled.store(hEn);
        Settings::healthThresholdPct.store(hThr);
        Settings::healthReducePct.store(hRed);
        Settings::healthSmoothWidthPct.store(hSw);

        // Stamina
        bool sEn = Settings::staminaEnabled.load();
        float sThr = Settings::staminaThresholdPct.load();
        float sRed = Settings::staminaReducePct.load();
        float sSw = Settings::staminaSmoothWidthPct.load();
        drawBlock("Stamina", "Stamina affects speed", sEn, sThr, sRed, sSw);
        Settings::staminaEnabled.store(sEn);
        Settings::staminaThresholdPct.store(sThr);
        Settings::staminaReducePct.store(sRed);
        Settings::staminaSmoothWidthPct.store(sSw);

        // Magicka
        bool mEn = Settings::magickaEnabled.load();
        float mThr = Settings::magickaThresholdPct.load();
        float mRed = Settings::magickaReducePct.load();
        float mSw = Settings::magickaSmoothWidthPct.load();
        drawBlock("Magicka", "Magicka affects speed", mEn, mThr, mRed, mSw);
        Settings::magickaEnabled.store(mEn);
        Settings::magickaThresholdPct.store(mThr);
        Settings::magickaReducePct.store(mRed);
        Settings::magickaSmoothWidthPct.store(mSw);

        FontAwesome::PushSolid();
        if (ImGui::Button(saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
        }
        FontAwesome::Pop();
    }
    FontAwesome::Pop();
}

void __stdcall UI::SpeedConfig::RenderLocations() {
    ImGui::Text("Location-based Modifiers");
    ImGui::Separator();

    {
        int aff = (Settings::locationAffects == Settings::LocationAffects::AllStates) ? 1 : 0;
        if (ImGui::RadioButton("Affect Default/Out-of-combat only", aff == 0)) {
            Settings::locationAffects = Settings::LocationAffects::DefaultOnly;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Affect All Movement States", aff == 1)) {
            Settings::locationAffects = Settings::LocationAffects::AllStates;
        }

        int mode = (Settings::locationMode == Settings::LocationMode::Replace) ? 0
                   : (Settings::locationMode == Settings::LocationMode::Add)   ? 1
                                                                               : 2;
        if (ImGui::RadioButton("Replace base reduction", mode == 0)) {
            Settings::locationMode = Settings::LocationMode::Replace;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Add to base reduction", mode == 1)) {
            Settings::locationMode = Settings::LocationMode::Add;
        }
        /*
        ImGui::SameLine();
        if (ImGui::RadioButton("Ignore location rules", mode == 2)) {
            Settings::locationMode = Settings::LocationMode::Ignore;
        }*/
    }

    ImGui::Spacing();

    // Specific Locations (BGSLocation)
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(specificLocationHeader.c_str())) {
        static char specBuf[256] = {};
        static float specVal = 30.0f;

        ImGui::InputText("Form (Plugin|0xID)", specBuf, sizeof(specBuf));
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Current Location")) {
            std::string cur;
            if (GetCurrentLocationSpec(cur)) {
                std::snprintf(specBuf, sizeof(specBuf), "%s", cur.c_str());
            }
        }
        ImGui::SliderFloat("Value (reduce)", &specVal, 0.f, 100.f, "%.1f");

        if (ImGui::Button("Add Specific")) {
            Settings::FormSpec fs;
            if (Settings::ParseFormSpec(specBuf, fs.plugin, fs.id)) {
                fs.value = std::max(0.f, std::min(100.f, specVal));
                Settings::reduceInLocationSpecific.push_back(std::move(fs));
                specBuf[0] = '\0';
            }
        }

        if (ImGui::BeginTable("specLocTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Form");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Remove");
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)Settings::reduceInLocationSpecific.size(); ++i) {
                auto& fs = Settings::reduceInLocationSpecific[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s|0x%06X", fs.plugin.c_str(), fs.id);
                ImGui::TableSetColumnIndex(1);
                float v = fs.value;
                if (ImGui::DragFloat(("##specv" + std::to_string(i)).c_str(), &v, 0.1f, 0.f, 100.f, "%.1f")) {
                    fs.value = std::max(0.f, std::min(100.f, v));
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton(("X##spec" + std::to_string(i)).c_str())) {
                    Settings::reduceInLocationSpecific.erase(Settings::reduceInLocationSpecific.begin() + i);
                    --i;
                }
            }
            ImGui::EndTable();
        }
    }
    FontAwesome::Pop();

    ImGui::Spacing();

    FontAwesome::PushSolid();
    // Location Types (BGSKeyword)
    if (ImGui::CollapsingHeader(typesLocationHeader.c_str())) {
        static char typeBuf[256] = {};
        static float typeVal = 45.0f;
        static std::vector<std::pair<std::string, std::string>> s_curLocKw;
        static int s_curLocKwIdx = -1;

        ImGui::InputText("Keyword (Plugin|0xID)", typeBuf, sizeof(typeBuf));
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Current Location")) {
            s_curLocKw.clear();
            s_curLocKwIdx = -1;
            if (auto* loc = GetCurrentLocationPtr()) {
                CollectKeywordsForLocation(loc, s_curLocKw);
            }
        }

        if (!s_curLocKw.empty()) {
            std::vector<const char*> items;
            items.reserve(s_curLocKw.size());
            for (auto& kv : s_curLocKw) items.push_back(kv.first.c_str());

            if (ImGui::Combo("Pick keyword from current location", &s_curLocKwIdx, items.data(),
                             static_cast<int>(items.size()))) {
                if (s_curLocKwIdx >= 0 && s_curLocKwIdx < static_cast<int>(s_curLocKw.size())) {
                    std::snprintf(typeBuf, sizeof(typeBuf), "%s", s_curLocKw[s_curLocKwIdx].second.c_str());
                }
            }
        } else {
            ImGui::TextDisabled("Hint: Click 'Use Current Location' to list its keywords (LocType*, etc.).");
        }

        ImGui::SliderFloat("Value (reduce)", &typeVal, 0.f, 100.f, "%.1f");

        auto alreadyInList = [](const Settings::FormSpec& needle) {
            for (auto& fs : Settings::reduceInLocationType) {
                if (fs.id == needle.id && fs.plugin == needle.plugin) return true;
            }
            return false;
        };

        if (ImGui::Button("Add Type")) {
            Settings::FormSpec fs;
            if (Settings::ParseFormSpec(typeBuf, fs.plugin, fs.id)) {
                fs.value = std::max(0.f, std::min(100.f, typeVal));
                if (!alreadyInList(fs)) {
                    Settings::reduceInLocationType.push_back(std::move(fs));
                }
                typeBuf[0] = '\0';
            }
        }

        if (ImGui::BeginTable("typeLocTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Keyword");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Remove");
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)Settings::reduceInLocationType.size(); ++i) {
                auto& fs = Settings::reduceInLocationType[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s|0x%06X", fs.plugin.c_str(), fs.id);
                ImGui::TableSetColumnIndex(1);
                float v = fs.value;
                if (ImGui::DragFloat(("##typev" + std::to_string(i)).c_str(), &v, 0.1f, 0.f, 100.f, "%.1f")) {
                    fs.value = std::max(0.f, std::min(100.f, v));
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton(("X##type" + std::to_string(i)).c_str())) {
                    Settings::reduceInLocationType.erase(Settings::reduceInLocationType.begin() + i);
                    --i;
                }
            }
            ImGui::EndTable();
        }
    }
    FontAwesome::Pop();

    ImGui::Separator();
    FontAwesome::PushSolid();
    if (ImGui::Button(saveIcon.c_str())) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
    FontAwesome::Pop();
}

void __stdcall UI::SpeedConfig::RenderWeather() {
    ImGui::Text("Weather-based Modifiers");
    ImGui::Separator();

    // Master toggle
    bool wen = Settings::weatherEnabled.load();
    if (ImGui::Checkbox("Enable weather-based speed modifier", &wen)) {
        Settings::weatherEnabled.store(wen);
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
    }

    bool win = Settings::weatherIgnoreInterior.load();
    if (ImGui::Checkbox("Ignore interior cells", &win)) {
        Settings::weatherIgnoreInterior.store(win);
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
    }

    {
        int aff = (Settings::weatherAffects == Settings::WeatherAffects::AllStates) ? 1 : 0;
        if (ImGui::RadioButton("Affect Default/Out-of-combat only", aff == 0)) {
            Settings::weatherAffects = Settings::WeatherAffects::DefaultOnly;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Affect All Movement States", aff == 1)) {
            Settings::weatherAffects = Settings::WeatherAffects::AllStates;
        }

        int mode = (Settings::weatherMode == Settings::WeatherMode::Add) ? 1 : 0;
        if (ImGui::RadioButton("Replace base reduction", mode == 0)) {
            Settings::weatherMode = Settings::WeatherMode::Replace;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Add to base reduction", mode == 1)) {
            Settings::weatherMode = Settings::WeatherMode::Add;
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Hint: Values are 'reduce' amounts (0...100). Higher = slower movement.");
    ImGui::Spacing();

    static char specBuf[256] = {};
    static float specVal = 20.0f;

    ImGui::InputText("Weather (Plugin|0xID)", specBuf, sizeof(specBuf));
    ImGui::SameLine();
    if (ImGui::SmallButton("Use Current Weather")) {
        if (auto* sky = RE::Sky::GetSingleton()) {
            if (auto* w = sky->currentWeather) {
                std::string spec;
                if (MakeFormSpecFromForm(w, spec)) {
                    std::snprintf(specBuf, sizeof(specBuf), "%s", spec.c_str());
                }
            }
        }
    }
    ImGui::SliderFloat("Value (reduce)", &specVal, 0.f, 100.f, "%.1f");
    if (ImGui::Button("Add / Update")) {
        Settings::FormSpec fs;
        if (Settings::ParseFormSpec(specBuf, fs.plugin, fs.id)) {
            fs.value = std::max(0.f, std::min(100.f, specVal));
            bool replaced = false;
            for (auto& e : Settings::reduceInWeatherSpecific) {
                if (e.plugin == fs.plugin && e.id == fs.id) {
                    e.value = fs.value;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) Settings::reduceInWeatherSpecific.push_back(std::move(fs));
            specBuf[0] = '\0';
        }
    }

    ImGui::Separator();
    // Full list of all Weathers, editable values + highlight current
    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* sky = RE::Sky::GetSingleton();
    RE::TESWeather* cur = sky ? sky->currentWeather : nullptr;

    if (dh) {
        const ImGuiTableFlags tblFlags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame;

        FontAwesome::PushSolid();
        if (ImGui::CollapsingHeader(weatherListHeader.c_str())) {
            if (ImGui::BeginTable("weatherTable", 3, tblFlags)) {
                ImGui::TableSetupColumn("Weather");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Remove");
                ImGui::TableHeadersRow();

                const auto& arr = dh->GetFormArray<RE::TESWeather>();
                for (auto* w : arr) {
                    if (!w) continue;

                    const bool isCurrent = (cur && w == cur);
                    const ImVec4 hl = ImVec4(0.80f, 0.90f, 1.0f, 1.0f);

                    ImGui::TableNextRow();

                    // Col 1: Plugin|0xID
                    ImGui::TableSetColumnIndex(0);
                    std::string spec;
                    if (MakeFormSpecFromForm(w, spec)) {
                        if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, hl);
                        ImGui::Text("%s%s", (isCurrent ? ">> " : ""), spec.c_str());
                        if (isCurrent) ImGui::PopStyleColor();
                    } else {
                        if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, hl);
                        ImGui::Text("%s0x%08X", (isCurrent ? ">> " : ""), w->GetFormID());
                        if (isCurrent) ImGui::PopStyleColor();
                    }

                    int idx = -1;
                    for (int i = 0; i < static_cast<int>(Settings::reduceInWeatherSpecific.size()); ++i) {
                        auto& e = Settings::reduceInWeatherSpecific[i];
                        auto* lw = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESWeather>(e.id, e.plugin);
                        if (lw == w) {
                            idx = i;
                            break;
                        }
                    }
                    float curVal = (idx >= 0) ? Settings::reduceInWeatherSpecific[idx].value : 0.0f;

                    // Col 2: Value editor
                    ImGui::TableSetColumnIndex(1);
                    float tmp = curVal;
                    std::string idVal = "##wval" + std::to_string(w->GetFormID());
                    if (ImGui::DragFloat(idVal.c_str(), &tmp, 0.1f, 0.f, 100.f, "%.1f")) {
                        tmp = std::max(0.f, std::min(100.f, tmp));
                        if (idx >= 0) {
                            Settings::reduceInWeatherSpecific[idx].value = tmp;
                        } else {
                            Settings::FormSpec fs;
                            if (MakeFormSpecFromForm(w, spec) && Settings::ParseFormSpec(spec, fs.plugin, fs.id)) {
                                fs.value = tmp;
                                Settings::reduceInWeatherSpecific.push_back(std::move(fs));
                            }
                        }
                        if (auto* pc = RE::PlayerCharacter::GetSingleton())
                            SpeedController::GetSingleton()->RefreshNow();
                    }

                    // Col 3: Remove
                    ImGui::TableSetColumnIndex(2);
                    std::string idRem = "X##wrem" + std::to_string(w->GetFormID());
                    if (idx >= 0) {
                        if (ImGui::SmallButton(idRem.c_str())) {
                            Settings::reduceInWeatherSpecific.erase(Settings::reduceInWeatherSpecific.begin() + idx);
                        }
                    } else {
                        ImGui::BeginDisabled();
                        ImGui::SmallButton(idRem.c_str());
                        ImGui::EndDisabled();
                    }
                }
                ImGui::EndTable();
            }
        }
        FontAwesome::Pop();
    } else {
        ImGui::TextDisabled("TESDataHandler not available.");
    }

    ImGui::Separator();
    FontAwesome::PushSolid();
    if (ImGui::Button(saveIcon.c_str())) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
    FontAwesome::Pop();
}

void __stdcall UI::SpeedConfig::RenderAddons() {
    ImGui::Text("Add-ons");
    ImGui::Separator();

    auto isDWInstalled = []() -> bool {
        HMODULE h = GetModuleHandleA("DynamicWetness.dll");
        if (!h) h = GetModuleHandleA("DynamicWetness");
        if (!h) return false;
        return GetProcAddress(h, "SWE_SetExternalWetnessMask") != nullptr;
    }();
    const bool dwInstalled = isDWInstalled;

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader("Dynamic Wetness")) {
        bool dwEnabled = Settings::dwEnabled.load();
        if (dwEnabled && !dwInstalled) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "DynamicWetness not detected! Please install the mod first. (Requires DW v1.3.0+)");
            ImGui::Separator();
            dwEnabled = false;
        }
        if (ImGui::Checkbox("Enable DynamicWetness support", &dwEnabled)) {
            Settings::dwEnabled.store(dwEnabled);
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
        }

        ImGui::BeginDisabled(!(dwInstalled && Settings::dwEnabled.load()));
        bool dwSlopeFeatureEnabled = Settings::dwSlopeFeatureEnabled.load();
        if (ImGui::Checkbox("Enable DW Slope-Feature", &dwSlopeFeatureEnabled)) {
            Settings::dwSlopeFeatureEnabled.store(dwSlopeFeatureEnabled);
            if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
        }

        float startDeg = Settings::dwStartDeg.load();
        float fullDeg = Settings::dwFullDeg.load();

        if (ImGui::SliderFloat("Uphill start angle (deg)", &startDeg, 0.0f, 30.0f, "%.1f")) {
            const float guard = std::max(0.1f, std::min(fullDeg - 0.1f, startDeg));
            Settings::dwStartDeg.store(guard);
        }
        if (ImGui::SliderFloat("Uphill full angle (deg)", &fullDeg, 1.0f, 45.0f, "%.1f")) {
            const float guard = std::max(Settings::dwStartDeg.load() + 0.1f, fullDeg);
            Settings::dwFullDeg.store(guard);
        }
        ImGui::TextDisabled("Wetness intensity ramps linearly from start to full angle.");

        float bu = Settings::dwBuildUpPerSec.load();
        if (ImGui::SliderFloat("Wetness build-up rate (Delta/s)", &bu, 0.0f, 10.0f, "%.2f")) {
            Settings::dwBuildUpPerSec.store(bu);
        }
        ImGui::TextDisabled("0 = instant; larger = faster build-up.");

        float dr = Settings::dwDryPerSec.load();
        if (ImGui::SliderFloat("Dry rate (Delta/s)", &dr, 0.0f, 10.0f, "%.2f")) {
            Settings::dwDryPerSec.store(dr);
        }
        ImGui::TextDisabled("0 = instant; larger = faster drying.");

        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::Button(saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
        }
    }
    FontAwesome::Pop();
}