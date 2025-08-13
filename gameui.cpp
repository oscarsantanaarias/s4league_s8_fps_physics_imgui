#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <fstream>
#include <cmath>
#include "imgui.h"
#include "json.hpp"
#include "globals.h"

using json = nlohmann::json;

// === Offsets ===
constexpr uintptr_t settingsBaseOffset = 0;
constexpr uintptr_t aspectRatioBaseOffset = 0;

// === Runtime Variables ===
HANDLE hProcess = nullptr;
DWORD processId = 0;
uintptr_t baseAddress = 0;

bool overlayVisible = true; // extern sichtbar

static int fullscreenVal = 0;
static int gfxQuality = 2;
static int aspectRatioIndex = 1;

const float aspectValues[] = { 1.333f, 1.777f, 1.6f, 1.666f, 1.25f };
const char* aspectLabels[] = { "4:3", "16:9", "16:10", "5:3", "5:4" };
const char* gfxOptions[] = { "Low", "Medium", "High" };

json config;

// === Module base address helper ===
uintptr_t GetModuleBaseAddress(DWORD pid, const wchar_t* modName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W mod;
    mod.dwSize = sizeof(mod);
    if (Module32FirstW(snapshot, &mod)) {
        do {
            if (_wcsicmp(mod.szModule, modName) == 0) {
                CloseHandle(snapshot);
                return (uintptr_t)mod.modBaseAddr;
            }
        } while (Module32NextW(snapshot, &mod));
    }
    CloseHandle(snapshot);
    return 0;
}

// === Open process ===
bool OpenGameProcess() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"S4Client.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (pid == 0) return false;

    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;

    processId = pid;
    baseAddress = GetModuleBaseAddress(pid, L"S4Client.exe");
    return baseAddress != 0;
}

// === Memory read/write with pointer chain ===
template<typename T>
bool ReadMem(uintptr_t baseOffset, uintptr_t subOffset, T& out) {
    if (!hProcess) return false;
    uintptr_t ptr = 0;
    SIZE_T br;
    if (!ReadProcessMemory(hProcess, (LPCVOID)(baseAddress + baseOffset), &ptr, sizeof(ptr), &br) || br != sizeof(ptr))
        return false;
    return ReadProcessMemory(hProcess, (LPCVOID)(ptr + subOffset), &out, sizeof(T), &br) && br == sizeof(T);
}

template<typename T>
bool WriteMem(uintptr_t baseOffset, uintptr_t subOffset, T val) {
    if (!hProcess) return false;
    uintptr_t ptr = 0;
    SIZE_T br;
    if (!ReadProcessMemory(hProcess, (LPCVOID)(baseAddress + baseOffset), &ptr, sizeof(ptr), &br) || br != sizeof(ptr))
        return false;
    SIZE_T wr;
    return WriteProcessMemory(hProcess, (LPVOID)(ptr + subOffset), &val, sizeof(T), &wr) && wr == sizeof(T);
}

// === Save config ===
void SaveConfig() {
    std::ofstream out("netsphere.json");
    if (out.is_open()) {
        out << config.dump(4);
        out.close();
    }

    WriteMem<BYTE>(settingsBaseOffset, 0x2C, (BYTE)fullscreenVal);
    WriteMem<BYTE>(settingsBaseOffset, 0x38, (BYTE)gfxQuality);
    WriteMem<float>(aspectRatioBaseOffset, 0x1C, aspectValues[aspectRatioIndex]);
}

// === Load config from JSON ===
void LoadConfigJson() {
    std::ifstream in("netsphere.json");
    if (in.is_open()) {
        try {
            in >> config;
        }
        catch (...) {
            config.clear();
        }
        in.close();
    }

    if (config.empty()) {
        config = {
            {"max_framerate", 300},
            {"field_of_view", 60},
            {"center_field_of_view", 66},
            {"sprint_field_of_view", 80}
        };
        SaveConfig();
    }
}

// === Load live game values ===
bool LoadLiveGameSettings() {
    BYTE fs, gfx;
    float ratio;

    bool ok = ReadMem<BYTE>(settingsBaseOffset, 0x2C, fs)
        && ReadMem<BYTE>(settingsBaseOffset, 0x38, gfx)
        && ReadMem<float>(aspectRatioBaseOffset, 0x1C, ratio);
    if (!ok) return false;

    fullscreenVal = (fs == 1) ? 1 : 0;
    gfxQuality = (gfx <= 2) ? gfx : 2;

    float minDiff = FLT_MAX;
    for (int i = 0; i < IM_ARRAYSIZE(aspectValues); ++i) {
        float diff = std::fabs(ratio - aspectValues[i]);
        if (diff < minDiff) {
            minDiff = diff;
            aspectRatioIndex = i;
        }
    }
    return true;
}

// === Quick Settings window ===
void RenderQuickSettings() {
    static bool quickVisible = true;
    ImGui::SetNextWindowSize(ImVec2(280, 160), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);

    if (!overlayVisible)
        return;

    if (ImGui::Begin("Quick Settings", &quickVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool isFullscreen = fullscreenVal != 0;
        if (ImGui::Checkbox("Fullscreen", &isFullscreen)) {
            fullscreenVal = isFullscreen ? 1 : 0;
            WriteMem<BYTE>(settingsBaseOffset, 0x2C, (BYTE)fullscreenVal);
        }

        if (ImGui::Combo("Graphics Quality", &gfxQuality, gfxOptions, IM_ARRAYSIZE(gfxOptions))) {
            WriteMem<BYTE>(settingsBaseOffset, 0x38, (BYTE)gfxQuality);
        }

        if (ImGui::Combo("Aspect Ratio", &aspectRatioIndex, aspectLabels, IM_ARRAYSIZE(aspectLabels))) {
            WriteMem<float>(aspectRatioBaseOffset, 0x1C, aspectValues[aspectRatioIndex]);
        }

        if (ImGui::Button("Apply")) {
            SaveConfig();
        }

        ImGui::End();
    }
}

// === Main Overlay GUI ===
void RenderGUI() {
    static bool loaded = false;
    static bool deletePressedLast = false;
    static bool showError = true;

    // Toggle overlay with DELETE key
    bool deletePressedNow = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (deletePressedNow && !deletePressedLast)
        overlayVisible = !overlayVisible;
    deletePressedLast = deletePressedNow;

    if (!overlayVisible)
        return;

    // Open process and load settings
    if (!loaded || !hProcess) {
        if (OpenGameProcess()) {
            LoadConfigJson();
            LoadLiveGameSettings();
            loaded = true;
            showError = false;
        }
        else {
            showError = true;
        }
    }

    if (showError) {
        ImGui::Begin("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("S4Client not found.");
        if (ImGui::Button("Retry")) {
            loaded = OpenGameProcess();
            showError = !loaded;
        }
        if (ImGui::Button("Close Overlay")) {
            overlayVisible = false;
        }
        ImGui::End();
        return;
    }

    // Load settings from JSON config
    static int max_framerate = config.value("max_framerate", 300);
    static int field_of_view = config.value("field_of_view", 60);
    static int center_field_of_view = config.value("center_field_of_view", 66);
    static int sprint_field_of_view = config.value("sprint_field_of_view", 80);

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);

    if (ImGui::Begin("Netsphere Settings", &overlayVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTabBar("MainTabs")) {
            // Tab 1: Game Settings
            if (ImGui::BeginTabItem("Game Settings")) {
                ImGui::SliderInt("Max FPS", &max_framerate, 30, 1000);
                ImGui::SliderInt("Base FOV", &field_of_view, 50, 120);
                ImGui::SliderInt("Center FOV", &center_field_of_view, 50, 120);
                ImGui::SliderInt("Sprint FOV", &sprint_field_of_view, 50, 120);

                if (ImGui::Button("Apply and Save")) {
                    config["max_framerate"] = max_framerate;
                    config["field_of_view"] = field_of_view;
                    config["center_field_of_view"] = center_field_of_view;
                    config["sprint_field_of_view"] = sprint_field_of_view;
                    SaveConfig();
                }
                ImGui::EndTabItem();
            }

            // Tab 2: Graphics
            if (ImGui::BeginTabItem("Graphics")) {
                bool isFullscreen = fullscreenVal != 0;
                if (ImGui::Checkbox("Fullscreen", &isFullscreen)) {
                    fullscreenVal = isFullscreen ? 1 : 0;
                    WriteMem<BYTE>(settingsBaseOffset, 0x2C, (BYTE)fullscreenVal);
                }

                if (ImGui::Combo("Graphics Quality", &gfxQuality, gfxOptions, IM_ARRAYSIZE(gfxOptions))) {
                    WriteMem<BYTE>(settingsBaseOffset, 0x38, (BYTE)gfxQuality);
                }

                if (ImGui::Combo("Aspect Ratio", &aspectRatioIndex, aspectLabels, IM_ARRAYSIZE(aspectLabels))) {
                    WriteMem<float>(aspectRatioBaseOffset, 0x1C, aspectValues[aspectRatioIndex]);
                }
                ImGui::EndTabItem();
            }

            // Tab 3: Diagnostics
            if (ImGui::BeginTabItem("Diagnostics")) {
                ImGui::Text("PID: %d", processId);
                ImGui::Text("Base Address: 0x%p", (void*)baseAddress);
                ImGui::Text("Handle: 0x%p", hProcess);
                ImGui::Text("Overlay: %s", overlayVisible ? "Visible" : "Hidden");

                if (ImGui::Button("Reload Game Values")) {
                    LoadLiveGameSettings();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // Call quick settings window
    RenderQuickSettings();
}
