#pragma once
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "SpeedController.h"

namespace UI {
    void Register();

    namespace SpeedConfig {
        void __stdcall RenderGeneral();
        void __stdcall Render();
        void __stdcall RenderAttack();
        void __stdcall RenderVitals();
        void __stdcall RenderLocations();
        void __stdcall RenderWeather();

        inline std::string saveIcon = FontAwesome::UnicodeToUtf8(0xf0c7) + " Save Settings";

        inline std::string smootingAccelerationHeader = FontAwesome::UnicodeToUtf8(0xf624) + " Smoothing / Acceleration";
        inline std::string fixesHeader = FontAwesome::UnicodeToUtf8(0xf54a) + " Fixes";
        inline std::string npcsHeader = FontAwesome::UnicodeToUtf8(0xf544) + " NPC Settings";

        inline std::string animationsHeader = FontAwesome::UnicodeToUtf8(0xf21d) + " Animations";
        inline std::string slopeTerrainHeader = FontAwesome::UnicodeToUtf8(0xe508) + " Slope / Terrain";
        inline std::string movementSpeedHeader = FontAwesome::UnicodeToUtf8(0xe552) + " Movement Speed Modifiers";
        inline std::string vitalsHeader = FontAwesome::UnicodeToUtf8(0xf21e) + " Vitals & Resources";
        inline std::string inputHeader = FontAwesome::UnicodeToUtf8(0xf11b) + " Input / Events";
        inline std::string armorHeader = FontAwesome::UnicodeToUtf8(0xf553) + " Armor Weight Scaling";

        inline std::string specificLocationHeader = FontAwesome::UnicodeToUtf8(0xf3c5) + " Specific Locations (BGSLocation)";
        inline std::string typesLocationHeader = FontAwesome::UnicodeToUtf8(0xf59f) + " Location Types (BGSKeyword e.g. LocType*)";
        inline std::string weatherListHeader = FontAwesome::UnicodeToUtf8(0xf743) + " Available Weather Presets";
    }
}