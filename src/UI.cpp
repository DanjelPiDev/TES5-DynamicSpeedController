#include "UI.h"

static int findIndex(const std::string& cur, const char* const* arr, int n) {
    for (int i = 0; i < n; ++i)
        if (cur == arr[i]) return i;
    return -1;
}

static bool MakeFormSpecFromForm(RE::TESForm* form, std::string& out) {
    if (!form) return false;
    auto* file = form->GetFile(0);
    if (!file) return false;

    std::string_view fname = file->GetFilename();
    std::uint32_t localId = form->GetLocalFormID();

    char buf[260];
    std::snprintf(buf, sizeof(buf), "%.*s|0x%06X", static_cast<int>(fname.size()), fname.data(), localId);

    out = buf;
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
    ImGui::Text("Movement Speed Modifiers");

    bool affectNPCs = Settings::enableSpeedScalingForNPCs.load();
    if (ImGui::Checkbox("Affect NPCs too?", &affectNPCs)) {
        Settings::enableSpeedScalingForNPCs.store(affectNPCs);
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) SpeedController::GetSingleton()->RefreshNow();
    }

    if (ImGui::Button("Save Settings")) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
}

void __stdcall UI::SpeedConfig::Render() {
    ImGui::Text("Movement Speed Modifiers");

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

    bool noReductionInCombatVal = Settings::noReductionInCombat.load();
    if (ImGui::Checkbox("No Reduction In Combat", &noReductionInCombatVal)) {
        Settings::noReductionInCombat.store(noReductionInCombatVal);
    }


    ImGui::Separator();
    ImGui::Text("Input / Events");

    static const char* kUserEvents[] = {"Sprint",       "Sneak",     "Shout",     "Jump",     "Activate",
                                        "Ready Weapon", "SwitchPOV"};
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
        if (ImGui::Selectable("Custom...", selCustom)) {}
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


    if (ImGui::Button("Save Settings")) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
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
    if (ImGui::DragFloat("##MaxAttack", &maxMul, 0.01f, 0.1f, 10.0f, "%.2f")) {
        maxMul = std::max(0.1f, std::min(10.0f, maxMul));
        Settings::maxAttackMult.store(maxMul);
    }

    if (ImGui::Button("Save Settings")) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
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

    // --- Specific Locations (BGSLocation) ---
    if (ImGui::CollapsingHeader("Specific Locations (BGSLocation)", ImGuiTreeNodeFlags_DefaultOpen)) {
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

        // List
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

    ImGui::Spacing();

    // --- Location Types (BGSKeyword) ---
    if (ImGui::CollapsingHeader("Location Types (BGSKeyword e.g. LocType*)", ImGuiTreeNodeFlags_DefaultOpen)) {
        static char typeBuf[256] = {};
        static float typeVal = 45.0f;

        ImGui::InputText("Keyword (Plugin|0xID)", typeBuf, sizeof(typeBuf));
        ImGui::SliderFloat("Value (reduce)", &typeVal, 0.f, 100.f, "%.1f");

        if (ImGui::Button("Add Type")) {
            Settings::FormSpec fs;
            if (Settings::ParseFormSpec(typeBuf, fs.plugin, fs.id)) {
                fs.value = std::max(0.f, std::min(100.f, typeVal));
                Settings::reduceInLocationType.push_back(std::move(fs));
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

    ImGui::Separator();
    if (ImGui::Button("Save Location Rules")) {
        Settings::SaveToJson(Settings::DefaultPath());
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            SpeedController::GetSingleton()->RefreshNow();
        }
    }
}