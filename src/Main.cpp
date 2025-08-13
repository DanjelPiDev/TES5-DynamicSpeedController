#include "Main.h"

#include <cmath>
#include <cstdint>

using namespace RE;
using namespace SKSE;

static constexpr std::uint32_t kSerVersion = 1;
static constexpr std::uint32_t kSerID = 'DSC1';

void OnSave(SKSE::SerializationInterface* intfc) {
    auto* sc = SpeedController::GetSingleton();
    bool jogging = sc->GetJoggingMode();
    float applied = sc->GetCurrentDelta();

    if (intfc->OpenRecord(kSerID, kSerVersion)) {
        intfc->WriteRecordData(&jogging, sizeof(jogging));
        intfc->WriteRecordData(&applied, sizeof(applied));
    }
}

void OnLoad(SKSE::SerializationInterface* intfc) {
    std::uint32_t type, version, length;
    while (intfc->GetNextRecordInfo(type, version, length)) {
        if (type == kSerID && version == kSerVersion) {
            bool jogging = false;
            float applied = 0.0f;
            intfc->ReadRecordData(&jogging, sizeof(jogging));
            intfc->ReadRecordData(&applied, sizeof(applied));

            auto* sc = SpeedController::GetSingleton();
            sc->SetJoggingMode(jogging);
            sc->SetCurrentDelta(applied);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    auto* ser = SKSE::GetSerializationInterface();
    ser->SetUniqueID(kSerID);
    ser->SetSaveCallback(OnSave);
    ser->SetLoadCallback(OnLoad);

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        auto* sc = SpeedController::GetSingleton();
        switch (message->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                sc->Install();
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
                sc->OnPreLoadGame();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                sc->OnPostLoadGame();
                break;
            default:
                break;
        }
    });

    return true;
}
