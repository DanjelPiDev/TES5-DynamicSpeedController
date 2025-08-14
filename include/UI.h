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
    }
}