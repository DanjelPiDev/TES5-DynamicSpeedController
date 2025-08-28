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
    SKSEMenuFramework::AddSectionItem("Location Rules", SpeedConfig::RenderLocations);
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
        ImGui::Checkbox("Enable slope effect", (bool*)&Settings::slopeEnabled);
        bool affectNPCs = Settings::enableSpeedScalingForNPCs.load();
        if (!affectNPCs) {
            ImGui::TextDisabled("Enable 'Affect NPCs too?' in General Settings to modify NPC slope settings.");
        } else {
            ImGui::Checkbox("Affect NPCs too?", (bool*)&Settings::slopeAffectsNPCs);
        }

        ImGui::SliderFloat("Uphill per degree", (float*)&Settings::slopeUphillPerDeg, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Downhill per degree", (float*)&Settings::slopeDownhillPerDeg, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Max |slope delta|", (float*)&Settings::slopeMaxAbs, 0.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Smoothing tau (s)", (float*)&Settings::slopeTau, 0.01f, 5.0f, "%.2f");

        ImGui::Separator();
        ImGui::Checkbox("Clamp final SpeedMult while slope is active", (bool*)&Settings::slopeClampEnabled);
        ImGui::SliderFloat("Slope Min Final", (float*)&Settings::slopeMinFinal, 0.0f, 500.0f, "%.0f");
        ImGui::SliderFloat("Slope Max Final", (float*)&Settings::slopeMaxFinal, 0.0f, 500.0f, "%.0f");

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

        int mode = (Settings::locationMode == Settings::LocationMode::Add) ? 1 : 0;
        if (ImGui::RadioButton("Replace base reduction", mode == 0)) {
            Settings::locationMode = Settings::LocationMode::Replace;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Add to base reduction", mode == 1)) {
            Settings::locationMode = Settings::LocationMode::Add;
        }
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