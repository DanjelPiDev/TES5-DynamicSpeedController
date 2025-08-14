#pragma once
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "SpeedController.h"

namespace UI {
    void Register();

    namespace SpeedConfig {
        void __stdcall Render();
        void __stdcall RenderLocations();
    }
}