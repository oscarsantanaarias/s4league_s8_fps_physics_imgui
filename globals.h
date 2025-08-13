#pragma once
#include <windows.h>
#include "json.hpp"

extern bool overlayVisible;

namespace Global {
    extern HANDLE hProcess;
    extern DWORD processId;
    extern uintptr_t baseAddress;
    extern bool overlayVisible;
    extern nlohmann::json config;

    extern int fullscreenVal;
    extern int gfxQuality;
    extern int aspectRatioIndex;

    inline constexpr uintptr_t settingsBaseOffset = 0x01729488;
    inline constexpr uintptr_t aspectRatioBaseOffset = 0x01728D60;

    inline constexpr float aspectValues[5] = { 1.333f, 1.777f, 1.6f, 1.666f, 1.25f };
    inline constexpr const char* aspectLabels[5] = { "4:3", "16:9", "16:10", "5:3", "5:4" };
    inline constexpr const char* gfxOptions[3] = { "Low", "Medium", "High" };
}
