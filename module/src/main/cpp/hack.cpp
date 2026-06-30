#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <dlfcn.h>
#include <string>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

#include "hack.h"
#include "log.h"
#include "game.h"
#include "utils.h"
#include "xdl.h"
#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "MemoryPatch.h"

// ====================================================================
// DAFTAR OFFSET MATANG ESP LINE (FREE FIRE MAX)
// ====================================================================
#define OFFSET_GET_MAIN                 0xa7ed6c0
#define OFFSET_WORLD_TO_SCREEN          0xa7ed344

#define OFFSET_GET_PLAYER_COUNT         0x645d5c4
#define OFFSET_GET_LOCAL_PLAYER         0x64cbde8
#define OFFSET_GET_PLAYER_BY_INDEX      0x7d3fb8c

#define OFFSET_GET_POSITION             0x8857b00
#define OFFSET_IS_DEAD                  0x76611dc
// ====================================================================

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };

static int                  g_GlHeight = 1080, g_GlWidth = 2340; // Resolusi cadangan standar
static bool                 g_IsSetup = false;
static std::string          g_IniFileName = "";
static utils::module_info   g_TargetModule{};

typedef void* (*_GetMainCamera)();
typedef int (*_GetPlayerCount)();
typedef void* (*_GetPlayerByIndex)(int index);
typedef void (*_GetPosition)(void* player, Vector3& pos);
typedef bool (*_IsDead)(void* player);

HOOKAF(void, Input, void *thiz, void *ex_ab, void *ex_ac) {
    origInput(thiz, ex_ab, ex_ac);
    ImGui_ImplAndroid_HandleInputEvent((AInputEvent *)thiz);
    return;
}

void SetupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    io.IniFilename = g_IniFileName.c_str();
    io.DisplaySize = ImVec2((float)g_GlWidth, (float)g_GlHeight);

    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImGui::StyleColorsDark();

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 24.0f;
    io.Fonts->AddFontDefault(&font_cfg);

    ImGui::GetStyle().ScaleAllSizes(3.0f);
}

uintptr_t get_absolute_address(uintptr_t offset) {
    if (g_TargetModule.start_address == 0) return 0;
    return (uintptr_t)g_TargetModule.start_address + offset;
}

void DrawESP() {
    // Teks indikator besar di layar agar mudah terlihat saat tes pertama berhasil
    ImGui::GetForegroundDrawList()->AddText(ImVec2(100, 200), ImColor(0, 255, 0), "=== ESP SYSTEM: ACTIVE ===");

    if (g_TargetModule.start_address == 0) {
        ImGui::GetForegroundDrawList()->AddText(ImVec2(100, 250), ImColor(255, 0, 0), "Menunggu Modul Game Dimuat...");
        return;
    }

    _GetMainCamera GetMainCamera = (_GetMainCamera)get_absolute_address(OFFSET_GET_MAIN);
    _GetPlayerCount GetPlayerCount = (_GetPlayerCount)get_absolute_address(OFFSET_GET_PLAYER_COUNT);
    _GetPlayerByIndex GetPlayerByIndex = (_GetPlayerByIndex)get_absolute_address(OFFSET_GET_PLAYER_BY_INDEX);
    _GetPosition GetPosition = (_GetPosition)get_absolute_address(OFFSET_GET_POSITION);
    _IsDead IsDead = (_IsDead)get_absolute_address(OFFSET_IS_DEAD);

    if (!GetMainCamera || !GetPlayerCount || !GetPlayerByIndex || !GetPosition || !IsDead) return;

    void* camera = GetMainCamera();
    if (!camera) return;

    int total_players = GetPlayerCount();
    if (total_players <= 0 || total_players > 100) return;

    for (int i = 0; i < total_players; i++) {
        void* player = GetPlayerByIndex(i);
        if (!player) continue;
        if (IsDead(player)) continue;

        Vector3 enemy_world_pos{0, 0, 0};
        GetPosition(player, enemy_world_pos);

        Vector3 enemy_screen_pos{0, 0, 0};
        using _W2S_Func = void(*)(void*, Vector3, Vector3&, int);
        _W2S_Func target_w2s = (_W2S_Func)get_absolute_address(OFFSET_WORLD_TO_SCREEN);
        if (target_w2s) {
            target_w2s(camera, enemy_world_pos, enemy_screen_pos, 2);
        }

        if (enemy_screen_pos.z < 0.0f) continue;

        ImVec2 line_start = ImVec2((float)g_GlWidth / 2.0f, (float)g_GlHeight);
        ImVec2 line_end = ImVec2(enemy_screen_pos.x, (float)g_GlHeight - enemy_screen_pos.y);

        ImGui::GetForegroundDrawList()->AddLine(line_start, line_end, ImColor(255, 0, 0, 255), 2.5f);
    }
}

// Jalur Rendering Umum (OpenGL)
EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH, &g_GlWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &g_GlHeight);

    if (!g_IsSetup) { SetupImGui(); g_IsSetup = true; }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
    ImGui::NewFrame();

    DrawESP();

    ImGui::EndFrame();
    ImGui::Render();
    glViewport(0, 0, g_GlWidth, g_GlHeight);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return old_eglSwapBuffers(dpy, surface);
}

// JALUR CADANGAN UNIVERSAL (Bisa tembus jika game menggunakan Vulkan)
int (*old_ANativeWindow_unlockAndPost)(ANativeWindow* window);
int hook_ANativeWindow_unlockAndPost(ANativeWindow* window) {
    int width = ANativeWindow_getWidth(window);
    int height = ANativeWindow_getHeight(window);
    if (width > 0 && height > 0) {
        g_GlWidth = width;
        g_GlHeight = height;
    }

    if (!g_IsSetup) { SetupImGui(); g_IsSetup = true; }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
    ImGui::NewFrame();

    DrawESP();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return old_ANativeWindow_unlockAndPost(window);
}

void hack_start(const char *_game_data_dir) {
    LOGI("hack start | %s", _game_data_dir);
    // Jalankan pencarian modul di latar belakang tanpa mengunci sistem utama
    g_TargetModule = utils::find_module(TargetLibName);
}

void hack_prepare(const char *_game_data_dir) {
    LOGI("hack thread: %d", gettid());
    g_IniFileName = std::string(_game_data_dir) + "/files/imgui.ini";
    sleep(4);

    // Kaitan Input Sentuhan Layar
    void *sym_input = DobbySymbolResolver("/system/lib/libinput.so", "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
    if (NULL != sym_input){
        DobbyHook((void *)sym_input, (dobby_dummy_func_t) Input, (dobby_dummy_func_t*)&origInput);
    }
    
    // Kaitan Jalur Grafik 1: OpenGL
    void *egl_handle = xdl_open("libEGL.so", 0);
    void *eglSwapBuffers = xdl_sym(egl_handle, "eglSwapBuffers", nullptr);
    if (NULL != eglSwapBuffers) {
        utils::hook((void*)eglSwapBuffers, (func_t)hook_eglSwapBuffers, (func_t*)&old_eglSwapBuffers);
    }
    xdl_close(egl_handle);

    // Kaitan Jalur Grafik 2: Jalur Sistem Layar Android (Cadangan Vulkan)
    void *android_handle = xdl_open("libandroid.so", 0);
    void *unlockAndPost = xdl_sym(android_handle, "ANativeWindow_unlockAndPost", nullptr);
    if (NULL != unlockAndPost) {
        utils::hook((void*)unlockAndPost, (func_t)hook_ANativeWindow_unlockAndPost, (func_t*)&old_ANativeWindow_unlockAndPost);
    }
    xdl_close(android_handle);

    hack_start(_game_data_dir);
}
