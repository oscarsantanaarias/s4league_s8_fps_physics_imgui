#include "globals.h"

namespace Global {
    HANDLE hProcess = nullptr;
    DWORD processId = 0;
    uintptr_t baseAddress = 0;
    bool overlayVisible = true;
    nlohmann::json config;

    int fullscreenVal = 0;
    int gfxQuality = 2;
    int aspectRatioIndex = 1;

    const uintptr_t settingsBaseOffset = 0x01729488;
    const uintptr_t aspectRatioBaseOffset = 0x01728D60;

    const float aspectValues[5] = { 1.333f, 1.777f, 1.6f, 1.666f, 1.25f };
    const char* aspectLabels[5] = { "4:3", "16:9", "16:10", "5:3", "5:4" };
    const char* gfxOptions[3] = { "Low", "Medium", "High" };
}
