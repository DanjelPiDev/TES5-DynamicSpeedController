#include "UI.h"

static int findIndex(const std::string& cur, const char* const* arr, int n) {
    for (int i = 0; i < n; ++i)
        if (cur == arr[i]) return i;
    return -1;
}


void UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }
    SKSEMenuFramework::SetSection("Dynamic Speed Controller");
    SKSEMenuFramework::AddSectionItem("Speed Settings", SpeedConfig::Render);
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
    }
}
