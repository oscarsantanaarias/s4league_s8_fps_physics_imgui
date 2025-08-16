#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx9.h>
#include <detours.h>
#include <intrin.h>
#include <stdint.h>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

#pragma intrinsic(_ReturnAddress)

static int max_framerate = 144;
static int field_of_view = 90;
static int center_field_of_view = 100;
static int sprint_field_of_view = 110;
//The below settings are not working yet
static int display_mode = 0;          
static int resolution_width = 1920;
static int resolution_height = 1080;
static int aspect_ratio = 1;         
static int graphic_quality = 2;       
//The Above settings are not working yet

float normal_jump_height_multiplier = 0.953f;

//Here you can edit the fly jump + jumping
float flight_multiplier = 1.0f;   
//HERE YOU CAN CHANGE THE EXO JUMP!
float exo_jump_multiplier = 2.0f;

static float frametime = 16.666666f;
static float speed_dampeners[9];
static float set_drop_val = 0.0f;

// ImGui-related globals
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef HRESULT(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
typedef HRESULT(__stdcall* Reset_t)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
static EndScene_t oEndScene = nullptr;
static Reset_t oReset = nullptr;
static HWND window = nullptr;
static WNDPROC oWndProc = nullptr;
static bool initialized = false;
static bool showMenu = false;
static D3DPRESENT_PARAMETERS g_d3dpp = {};

static const char* displayModes[] = { "Windowed", "Fullscreen", "Borderless" };
static const char* qualityLevels[] = { "Low", "Medium", "High" };
static const char* resolutionPresets[] = { "800x600", "1280x720", "1600x900", "1920x1080", "2560x1440", "3440x1440", "3840x2160" };
static const char* aspectRatios[] = { "4:3", "16:9", "16:10", "21:9" };

struct game_context {
    uint8_t unknown[0x48];
    uint8_t online_verbose_toggle;
    uint8_t fps_limiter_toggle;
};

typedef game_context* (__cdecl* fetch_game_context_t)(void);
static fetch_game_context_t fetch_game_context = (fetch_game_context_t)0x004ad790;

typedef void(__thiscall* game_tick_t)(void*);
typedef void(__thiscall* move_actor_by_t)(void*, float, float, float);
typedef void(__thiscall* fun_005e4020_t)(void*, uint32_t);

static game_tick_t orig_game_tick = nullptr;
static move_actor_by_t orig_move_actor_by = nullptr;
static fun_005e4020_t orig_fun_005e4020 = nullptr;

struct actor_ctx {
    uint8_t unknown[0xb0];
    uint8_t actor_state;
    uint8_t unknown_2[0x3];
    uint32_t actor_substate_1;
    uint32_t actor_substate_2;
};

struct ctx_fun_005e4020 {
    uint8_t unknown[0x2cc + 0x4];
    float set_drop_val;
};

static actor_ctx* fetch_actor_ctx() {
    typedef actor_ctx* (__cdecl* fetch_ctx_t)(void);
    static fetch_ctx_t fetch_ctx = (fetch_ctx_t)0x004ae0a0;
    return fetch_ctx();
}




//Fix FOV Start value
static float g_CustomFov = 110.0f;

bool IsValidPtr(void* ptr, size_t size = sizeof(void*))
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!ptr) return false;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)))
        return false;
    return !IsBadWritePtr(ptr, size);
}

void PatchOnlyNorRunFov()
{
    uint8_t** mgrPtr = reinterpret_cast<uint8_t**>(0x01644AC8);

    if (!IsValidPtr(mgrPtr, sizeof(void*)))
        return;

    uint8_t* mgr = *mgrPtr;
    if (!IsValidPtr(mgr, 0x30))
        return;

    uint8_t** camPtr = reinterpret_cast<uint8_t**>(mgr + 0x2C);
    if (!IsValidPtr(camPtr, sizeof(void*)))
        return;

    uint8_t* cam = *camPtr;
    if (!IsValidPtr(cam, 0x160))
        return;

    float* norFov = reinterpret_cast<float*>(cam + 0x154);
    float* runFov = reinterpret_cast<float*>(cam + 0x158);

  
    if (IsValidPtr(norFov, sizeof(float)) && IsValidPtr(runFov, sizeof(float)))
    {
        float curNor = *norFov;
        float curRun = *runFov;

        
        if ((curNor > 50.0f && curNor < 150.0f) &&
            (curRun > 50.0f && curRun < 150.0f) &&
            (curNor != g_CustomFov || curRun != g_CustomFov))
        {
            *norFov = g_CustomFov;
            *runFov = g_CustomFov;
        }
    }
}





DWORD lastTime = GetTickCount();
float smoothedFPS = 0.0f;


void UpdateJumpMultiplierByFPS() {
  
    if (frametime > 0) {
        float fps = 1000.0f / frametime;
        smoothedFPS = (smoothedFPS * 0.9f) + (fps * 0.1f);

        //This here changes the jump value of character to fix at certains frames
        if (smoothedFPS < 90.0f) {
            normal_jump_height_multiplier = 1.0f;
        }
        else {
            normal_jump_height_multiplier = 0.953f;
        }
    }
}







LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (showMenu && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void ApplyGamerStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.TabRounding = 3.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.95f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.30f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.22f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.52f, 0.96f, 0.85f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.68f, 1.00f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.56f, 0.90f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.10f, 0.52f, 0.92f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.68f, 1.00f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.56f, 0.90f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.17f, 0.97f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.50f, 0.90f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.60f, 0.98f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.12f, 0.15f, 0.75f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.25f, 0.45f, 0.97f);
}

void RenderSettingsTab() {
    ImGui::Text("GAME SETTINGS");
    ImGui::Spacing();

    if (ImGui::SliderFloat("Field of View", &g_CustomFov, 50.0f, 120.0f, "%.0f°")) {
      
        field_of_view = static_cast<int>(g_CustomFov);
        center_field_of_view = static_cast<int>(g_CustomFov);
        sprint_field_of_view = static_cast<int>(g_CustomFov) + 10;

     
        auto mgr = *reinterpret_cast<uint8_t**>(0x01644AC8);
        if (mgr) {
            auto cam = *reinterpret_cast<uint8_t**>(mgr + 0x2C);
            if (cam) {
                *reinterpret_cast<float*>(cam + 0x154) = g_CustomFov; // NORFOV
                *reinterpret_cast<float*>(cam + 0x158) = g_CustomFov; // RUNFOV
            }
        }
    }

    ImGui::Text("Center Field of View: %d°", center_field_of_view);
    ImGui::Text("Sprint Field of View: %d°", sprint_field_of_view);

    ImGui::SliderInt("Max Framerate", &max_framerate, 30, 1000, "%d FPS");
    //Here you can enable a slider to edit the exo jump
    //  ImGui::SliderFloat("EXO Jump Multiplier", &exo_jump_multiplier, 0.1f, 5.0f);
}



void RenderGraphicSettingsTab() {
    ImGui::Text("DISPLAY SETTINGS");
    ImGui::Spacing();
    ImGui::Combo("Screen Type", &display_mode, displayModes, IM_ARRAYSIZE(displayModes));
    std::string currentRes = std::to_string(resolution_width) + "x" + std::to_string(resolution_height);
    if (ImGui::BeginCombo("Resolution", currentRes.c_str())) {
        for (int i = 0; i < IM_ARRAYSIZE(resolutionPresets); i++) {
            bool selected = (currentRes == resolutionPresets[i]);
            if (ImGui::Selectable(resolutionPresets[i], selected)) {
                size_t x = std::string(resolutionPresets[i]).find('x');
                resolution_width = std::stoi(std::string(resolutionPresets[i]).substr(0, x));
                resolution_height = std::stoi(std::string(resolutionPresets[i]).substr(x + 1));
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Combo("Aspect Ratio", &aspect_ratio, aspectRatios, IM_ARRAYSIZE(aspectRatios));
    ImGui::Separator();
    ImGui::Text("GRAPHICS QUALITY");
    ImGui::Spacing();
    ImGui::Combo("Quality Preset", &graphic_quality, qualityLevels, IM_ARRAYSIZE(qualityLevels));
}

HRESULT __stdcall hkReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    PatchOnlyNorRunFov();
    if (initialized) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    HRESULT hr = oReset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr) && initialized) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    if (FAILED(hr)) {

    }
    return hr;
}


HRESULT __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    UpdateJumpMultiplierByFPS();

    HRESULT hr = pDevice->TestCooperativeLevel();
    if (hr == D3DERR_DEVICELOST) {
        Sleep(50);
        return oEndScene(pDevice);
    }
    else if (hr == D3DERR_DEVICENOTRESET) {
        HRESULT resetHr = pDevice->Reset(&g_d3dpp);
        if (FAILED(resetHr)) {
            return oEndScene(pDevice);
        }
    }

    if (!initialized) {
        window = FindWindowA("S4_Client", nullptr);
        if (!window)
            return oEndScene(pDevice);

        ImGui::CreateContext();
        ApplyGamerStyle();
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX9_Init(pDevice);
        oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
        initialized = true;
    }

    static bool lastDeleteState = false;
    bool currentDeleteState = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (currentDeleteState && !lastDeleteState)
        showMenu = !showMenu;
    lastDeleteState = currentDeleteState;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (showMenu) {
       
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImVec2 windowSize = ImVec2(600, 350);
        ImVec2 windowPos = ImVec2(
            (displaySize.x - windowSize.x) * 0.5f,
            (displaySize.y - windowSize.y) * 0.5f
        );

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

      
        ImGui::Begin("Gamer Config", &showMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Settings")) {
                RenderSettingsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Graphics")) {
                RenderGraphicSettingsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();

        float btnWidth = 140.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btnWidth * 2 - ImGui::GetStyle().ItemSpacing.x) / 2);
        if (ImGui::Button("Close Menu", ImVec2(btnWidth, 35)))
            showMenu = false;

        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    return oEndScene(pDevice);
}




DWORD WINAPI InitHook(LPVOID) {
    while (!(window = FindWindowA("S4_Client", nullptr)))
        Sleep(100);

    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 0;

    D3DPRESENT_PARAMETERS d3dpp{};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = window;
    g_d3dpp = d3dpp;

    IDirect3DDevice9* pDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice))) {
        pD3D->Release();
        return 0;
    }

    void** vTable = *reinterpret_cast<void***>(pDevice);
    oEndScene = (EndScene_t)vTable[42];
    oReset = (Reset_t)vTable[16];

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oEndScene, hkEndScene);
    DetourAttach(&(PVOID&)oReset, hkReset);
    DetourTransactionCommit();

    pDevice->Release();
    pD3D->Release();
    return 0;
}

void __fastcall patched_fun_005e4020(void* ecx, void* edx, uint32_t param_1) {
    ctx_fun_005e4020* ctx = (ctx_fun_005e4020*)ecx;
    orig_fun_005e4020(ctx, param_1);
    if (_ReturnAddress() == (void*)0x0051f508) {
        set_drop_val = ctx->set_drop_val;
    }
}




void __fastcall patched_move_actor_by(void* ecx, void* edx, float param_1, float param_2, float param_3) {
    PatchOnlyNorRunFov();
    actor_ctx* actx = fetch_actor_ctx();
    float y = param_2;
    static float scythe_time = 0.0f;
    static bool was_flying = false;
    static bool first_ps_drop_frame = true;
    void* ret_addr = _ReturnAddress();

    if (ret_addr == (void*)0x00527467) {
        bool flying = false;
        if (actx->actor_state == 31) flying = true;
        if ((actx->actor_state == 39 || actx->actor_state == 25) && (actx->actor_substate_2 & 0xffff) == 0x02ff) flying = true;
        if (actx->actor_state == 4 && was_flying) flying = true;
        was_flying = flying;

       
        if (flying && param_2 > 0.0001f) {
            float orig_fixed_frametime = 16.666666f;
            float modifier = (orig_fixed_frametime / frametime);
            if (frametime < orig_fixed_frametime) {
                float frametime_diff_ratio = (orig_fixed_frametime - frametime) / orig_fixed_frametime;
                modifier = modifier * (1.0f - 0.4f * frametime_diff_ratio);
            }
            y = param_2 * modifier;
        }

       //EXO JUMP
        if (actx->actor_state == 63 && param_2 > 0.0f) {
           

            float frametime_ratio = 17.0f / frametime;
            if (frametime <= 13.0f) {
                float est = frametime_ratio / (1.0f / 3.75f);
                y = param_2 / est * frametime_ratio;
            }
            else if (frametime >= 33.0f) {
                float est = frametime_ratio / 4.0f;
                y = param_2 / est * frametime_ratio;
            }
            else if (frametime >= 28.0f) {
                float est = frametime_ratio / 3.0f;
                y = param_2 / est * frametime_ratio;
            }
            else if (frametime >= 25.0f) {
                float est = frametime_ratio / 2.25f;
                y = param_2 / est * frametime_ratio;
            }
            else if (frametime >= 22.0f) {
                float est = frametime_ratio / 2.0f;
                y = param_2 / est * frametime_ratio;
            }
            else if (frametime >= 19.0f) {
                float est = frametime_ratio / 1.75f;
                y = param_2 / est * frametime_ratio;
            }
            else if (frametime >= 18.0f) {
                float est = frametime_ratio / 1.5f;
                y = param_2 / est * frametime_ratio;
            }
            scythe_time += frametime;

           //EXTRA MULTIPLIER FOR EXO
       
            y *= exo_jump_multiplier;
        }
        else {
            scythe_time = 0.0f;
        }

        // Drop handling (state 45)
        if (actx->actor_state == 45 && set_drop_val == -50000.0f) {
            float drop_cutoff = (-750.0f) * (frametime / 16.666666f);
            if (param_2 < drop_cutoff) {
                if (first_ps_drop_frame) {
                    y = -850.0f;
                    first_ps_drop_frame = false;
                }
                else {
                    y = 0.0f;
                }
            }
        }
        else {
            first_ps_drop_frame = true;
        }

        // NORMAL JUMP
        if (!flying && actx->actor_state != 63 && actx->actor_state != 45 && param_2 > 0.0001f) {
            y = param_2 * normal_jump_height_multiplier;
        }
    }
    else {
        scythe_time = 0.0f;
    }

    orig_move_actor_by(ecx, param_1, y, param_3);
}




void __fastcall patched_game_tick(void* ecx, void* edx) {
    PatchOnlyNorRunFov();
    game_context* ctx = fetch_game_context();
    if (!ctx) return;

    bool should_limit = ctx->fps_limiter_toggle != 0;

    if (should_limit && max_framerate > 0) {
        static LARGE_INTEGER freq = { 0 };
        static LARGE_INTEGER last_tick = { 0 };
        if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        if (last_tick.QuadPart != 0) {
            double target_frametime_sec = 1.0 / max_framerate;
            double elapsed_sec = double(now.QuadPart - last_tick.QuadPart) / freq.QuadPart;
            while (elapsed_sec < target_frametime_sec) {
                QueryPerformanceCounter(&now);
                elapsed_sec = double(now.QuadPart - last_tick.QuadPart) / freq.QuadPart;
            }
        }
        last_tick = now;
    }

    uint8_t saved_fps_limiter = ctx->fps_limiter_toggle;
    ctx->fps_limiter_toggle = 0;
    orig_game_tick(ecx);
    ctx->fps_limiter_toggle = saved_fps_limiter;

    static LARGE_INTEGER freq = { 0 };
    static LARGE_INTEGER last_frame = { 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (last_frame.QuadPart != 0) {
        frametime = (float)(now.QuadPart - last_frame.QuadPart) * 1000.0f / (float)freq.QuadPart;
    }
    last_frame = now;

    const float orig_speed_dampener = 0.015f;
    const float orig_fixed_frametime = 16.666666f;
    float new_speed_dampener = frametime * orig_speed_dampener / orig_fixed_frametime;
    speed_dampeners[3] = new_speed_dampener;
    speed_dampeners[4] = new_speed_dampener;
    speed_dampeners[8] = new_speed_dampener;
}

void hook_move_actor_by() {
    PatchOnlyNorRunFov();
    uint8_t* target_addr = (uint8_t*)0x0051c2f0;
    const int patch_size = 10;
    uint8_t* trampoline = (uint8_t*)VirtualAlloc(NULL, patch_size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return;
    memcpy(trampoline, target_addr, patch_size);
    uintptr_t jump_back_addr = (uintptr_t)(target_addr + patch_size);
    intptr_t rel_jump_back = (intptr_t)(jump_back_addr - (uintptr_t)(trampoline + patch_size + 5));
    trampoline[patch_size] = 0xE9;
    *(int32_t*)(trampoline + patch_size + 1) = (int32_t)rel_jump_back;
    orig_move_actor_by = (move_actor_by_t)trampoline;
    DWORD oldProtect;
    VirtualProtect(target_addr, patch_size, PAGE_EXECUTE_READWRITE, &oldProtect);
    intptr_t rel_hook = (intptr_t)((uintptr_t)patched_move_actor_by - (uintptr_t)(target_addr + 5));
    uint8_t patch[patch_size];
    patch[0] = 0xE9;
    *(int32_t*)(patch + 1) = (int32_t)rel_hook;
    for (int i = 5; i < patch_size; i++) patch[i] = 0x90;
    memcpy(target_addr, patch, patch_size);
    VirtualProtect(target_addr, patch_size, oldProtect, &oldProtect);
}

void hook_game_tick() {
    PatchOnlyNorRunFov();
    uint8_t* target_addr = (uint8_t*)0x00871970;
    const int patch_size = 9;
    uint8_t* trampoline = (uint8_t*)VirtualAlloc(NULL, patch_size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return;
    memcpy(trampoline, target_addr, patch_size);
    uintptr_t jump_back_addr = (uintptr_t)(target_addr + patch_size);
    intptr_t rel_jump_back = (intptr_t)(jump_back_addr - (uintptr_t)(trampoline + patch_size + 5));
    trampoline[patch_size] = 0xE9;
    *(int32_t*)(trampoline + patch_size + 1) = (int32_t)rel_jump_back;
    orig_game_tick = (game_tick_t)trampoline;
    DWORD oldProtect;
    VirtualProtect(target_addr, patch_size, PAGE_EXECUTE_READWRITE, &oldProtect);
    intptr_t rel_hook = (intptr_t)((uintptr_t)patched_game_tick - (uintptr_t)(target_addr + 5));
    uint8_t patch[patch_size];
    patch[0] = 0xE9;
    *(int32_t*)(patch + 1) = (int32_t)rel_hook;
    for (int i = 5; i < patch_size; i++) patch[i] = 0x90;
    memcpy(target_addr, patch, patch_size);
    VirtualProtect(target_addr, patch_size, oldProtect, &oldProtect);
}

void hook_fun_005e4020() {

    uint8_t* target_addr = (uint8_t*)0x005e4020;
    const int patch_size = 10;
    uint8_t* trampoline = (uint8_t*)VirtualAlloc(NULL, patch_size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return;
    memcpy(trampoline, target_addr, patch_size);
    uintptr_t jump_back_addr = (uintptr_t)(target_addr + patch_size);
    intptr_t rel_jump_back = (intptr_t)(jump_back_addr - (uintptr_t)(trampoline + patch_size + 5));
    trampoline[patch_size] = 0xE9;
    *(int32_t*)(trampoline + patch_size + 1) = (int32_t)rel_jump_back;
    orig_fun_005e4020 = (fun_005e4020_t)trampoline;
    DWORD oldProtect;
    VirtualProtect(target_addr, patch_size, PAGE_EXECUTE_READWRITE, &oldProtect);
    intptr_t rel_hook = (intptr_t)((uintptr_t)patched_fun_005e4020 - (uintptr_t)(target_addr + 5));
    uint8_t patch[patch_size];
    patch[0] = 0xE9;
    *(int32_t*)(patch + 1) = (int32_t)rel_hook;
    for (int i = 5; i < patch_size; i++) patch[i] = 0x90;
    memcpy(target_addr, patch, patch_size);
    VirtualProtect(target_addr, patch_size, oldProtect, &oldProtect);
}

void patch_min_frametime(double min_frametime) {
    PatchOnlyNorRunFov();
    double* min_frametime_const = (double*)0x013d33a0;
    DWORD oldProtect;
    VirtualProtect(min_frametime_const, sizeof(double), PAGE_EXECUTE_READWRITE, &oldProtect);
    *min_frametime_const = min_frametime;
    VirtualProtect(min_frametime_const, sizeof(double), oldProtect, &oldProtect);
}

void redirect_speed_dampeners() {
    PatchOnlyNorRunFov();
    const uint8_t value[] = { 0x8f, 0xc2, 0x75, 0x3c };
    for (int i = 0; i < 9; i++) {
        memcpy(&speed_dampeners[i], value, sizeof(value));
    }
    uint32_t* patch_location = nullptr;
    patch_location = (uint32_t*)0x00563c0e; *patch_location = (uint32_t)&speed_dampeners[0];
    patch_location = (uint32_t*)0x007b063d; *patch_location = (uint32_t)&speed_dampeners[1];
    patch_location = (uint32_t*)0x007b06aa; *patch_location = (uint32_t)&speed_dampeners[2];
    patch_location = (uint32_t*)0x007b1204; *patch_location = (uint32_t)&speed_dampeners[3];
    patch_location = (uint32_t*)0x007b120c; *patch_location = (uint32_t)&speed_dampeners[4];
    patch_location = (uint32_t*)0x007b1973; *patch_location = (uint32_t)&speed_dampeners[5];
    patch_location = (uint32_t*)0x007b19a9; *patch_location = (uint32_t)&speed_dampeners[6];
    patch_location = (uint32_t*)0x007b1edc; *patch_location = (uint32_t)&speed_dampeners[7];
    patch_location = (uint32_t*)0x007b2363; *patch_location = (uint32_t)&speed_dampeners[8];
}

DWORD WINAPI main_thread(LPVOID) {
    patch_min_frametime(1.0 / max_framerate);
    redirect_speed_dampeners();
    hook_game_tick();
    hook_move_actor_by();
    hook_fun_005e4020();

    while (true)
    {
        PatchOnlyNorRunFov(); 
        Sleep(100);          
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, main_thread, NULL, 0, NULL);
        CreateThread(nullptr, 0, InitHook, nullptr, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH && initialized) {
        if (oWndProc) SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)oEndScene, hkEndScene);
        DetourDetach(&(PVOID&)oReset, hkReset);
        DetourTransactionCommit();
    }
    return TRUE;
}