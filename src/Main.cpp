#include "Main.h"

#include <cmath>
#include <cstdint>

using namespace SKSE;

static constexpr std::uint32_t kSerVersion = 4;
static constexpr std::uint32_t kSerID = 'DSC1';

void OnSave(SKSE::SerializationInterface* intfc) {
    auto* sc = SpeedController::GetSingleton();
    bool jogging = sc->GetJoggingMode();
    float applied = sc->GetCurrentDelta();
    float diag = sc->GetDiagDelta();
    float slope = sc->GetSlopeDelta();

    float baseSM = 100.0f;
    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        if (auto* avo = pc->AsActorValueOwner()) {
            const float cur = avo->GetActorValue(RE::ActorValue::kSpeedMult);
            baseSM = cur - applied - diag - slope;
        }
    }

    if (intfc->OpenRecord(kSerID, kSerVersion)) {
        intfc->WriteRecordData(&jogging, sizeof(jogging));
        intfc->WriteRecordData(&applied, sizeof(applied));
        intfc->WriteRecordData(&diag, sizeof(diag));
        intfc->WriteRecordData(&baseSM, sizeof(baseSM));
        intfc->WriteRecordData(&slope, sizeof(slope));
    }
}

void OnLoad(SKSE::SerializationInterface* intfc) {
    std::uint32_t type, version, length;

    while (intfc->GetNextRecordInfo(type, version, length)) {
        if (type != kSerID) {
            std::vector<char> skip(length);
            if (length > 0) intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
            continue;
        }

        if (version < 1 || version > 4) {
            std::vector<char> skip(length);
            if (length > 0) intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
            continue;
        }

        bool jogging = false;
        float applied = 0.0f;
        float diag = 0.0f;
        float baseSM = NAN;
        float slope = 0.0f;  // v4

        std::size_t bytesRead = 0;

        auto try_read = [&](auto* ptr) {
            const std::size_t need = sizeof(*ptr);
            if (bytesRead + need > length) return false;
            intfc->ReadRecordData(ptr, static_cast<std::uint32_t>(need));
            bytesRead += need;
            return true;
        };

        (void)try_read(&jogging);
        (void)try_read(&applied);

        if (version >= 2) (void)try_read(&diag);

        (void)try_read(&baseSM);

        if (version >= 4) (void)try_read(&slope);

        if (length > bytesRead) {
            std::vector<char> skip(length - bytesRead);
            intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
        }

        auto* sc = SpeedController::GetSingleton();
        if (version >= 4) {
            sc->SetSnapshot(jogging, applied, diag, baseSM, slope);
        } else {
            sc->SetSnapshot(jogging, applied, diag, baseSM, 0);  // Slope = 0
        }

        SKSE::GetTaskInterface()->AddTask([]() { SpeedController::GetSingleton()->DoPostLoadCleanup(); });
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
