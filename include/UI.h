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
        void __stdcall RenderLocations();

        inline std::string saveIcon = FontAwesome::UnicodeToUtf8(0xf0c7) + " Save Settings";

        inline std::string generalItem = FontAwesome::UnicodeToUtf8(0xf013) + " General Settings";
        inline std::string speedItem = FontAwesome::UnicodeToUtf8(0xf554) + " Speed Settings";
        inline std::string attackItem = FontAwesome::UnicodeToUtf8(0xf3ed) + " Attack Speed Settings";
        inline std::string locationsItem = FontAwesome::UnicodeToUtf8(0xf57d) + " Location Rules";

        inline std::string animationsHeader = FontAwesome::UnicodeToUtf8(0xf21d) + " Animations";
        inline std::string slopeTerrainHeader = FontAwesome::UnicodeToUtf8(0xe508) + " Slope / Terrain";
        inline std::string groundMaterialsHeader = FontAwesome::UnicodeToUtf8(0xe538) + " Ground Materials ";
        inline std::string movementSpeedHeader = FontAwesome::UnicodeToUtf8(0xe552) + " Movement Speed Modifiers";
        inline std::string inputHeader = FontAwesome::UnicodeToUtf8(0xf11b) + " Input / Events";

        inline std::string specificLocationHeader = FontAwesome::UnicodeToUtf8(0xf3c5) + " Specific Locations (BGSLocation)";
        inline std::string typesLocationHeader = FontAwesome::UnicodeToUtf8(0xf59f) + " Location Types (BGSKeyword e.g. LocType*)";
    }
}