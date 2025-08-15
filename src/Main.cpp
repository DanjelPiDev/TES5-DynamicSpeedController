#include "Main.h"

#include <cmath>
#include <cstdint>

using namespace SKSE;

static constexpr std::uint32_t kSerVersion = 3;
static constexpr std::uint32_t kSerID = 'DSC1';

void OnSave(SKSE::SerializationInterface* intfc) {
    auto* sc = SpeedController::GetSingleton();
    bool jogging = sc->GetJoggingMode();
    float applied = sc->GetCurrentDelta();
    float diag = sc->GetDiagDelta();

    float baseSM = 100.0f;
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        if (auto* avo = pc->AsActorValueOwner()) {
            const float cur = avo->GetActorValue(RE::ActorValue::kSpeedMult);
            baseSM = cur - applied - diag;
        }
    }

    if (intfc->OpenRecord(kSerID, kSerVersion)) {
        intfc->WriteRecordData(&jogging, sizeof(jogging));
        intfc->WriteRecordData(&applied, sizeof(applied));
        intfc->WriteRecordData(&diag, sizeof(diag));
        intfc->WriteRecordData(&baseSM, sizeof(baseSM));
    }
}

void OnLoad(SKSE::SerializationInterface* intfc) {
    std::uint32_t type, version, length;
    while (intfc->GetNextRecordInfo(type, version, length)) {
        if (type == kSerID && (version >= 1 && version <= 3)) {
            bool jogging = false;
            float applied = 0.0f;
            float diag = 0.0f;
            float baseSM = NAN;

            std::size_t bytesRead = 0;

            intfc->ReadRecordData(&jogging, sizeof(jogging));
            bytesRead += sizeof(jogging);
            intfc->ReadRecordData(&applied, sizeof(applied));
            bytesRead += sizeof(applied);

            if (version >= 2 && length >= bytesRead + sizeof(diag)) {
                intfc->ReadRecordData(&diag, sizeof(diag));
                bytesRead += sizeof(diag);
            }

            if (length >= bytesRead + sizeof(baseSM)) {
                intfc->ReadRecordData(&baseSM, sizeof(baseSM));
                bytesRead += sizeof(baseSM);
            }

            if (length > bytesRead) {
                std::vector<char> skip(length - bytesRead);
                intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
            }

            SpeedController::GetSingleton()->SetSnapshot(jogging, applied, diag, baseSM);
            SKSE::GetTaskInterface()->AddTask([]() { SpeedController::GetSingleton()->DoPostLoadCleanup(); });
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
                UI::Register();
                SKSE::GetTaskInterface()->AddTask([]() { UI::Register(); });
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
                sc->OnPreLoadGame();
                SKSE::GetTaskInterface()->AddTask([]() { UI::Register(); });
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
