#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <dlfcn.h>
#include <string>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

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

// STRUKTUR DATA (SDK DASAR)
struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };
struct Matrix4x4 { float m[16]; };

static int                  g_GlHeight, g_GlWidth;
static bool                 g_IsSetup = false;
static std::string          g_IniFileName = "";
static utils::module_info   g_TargetModule{};

// Deklarasi fungsi il2cpp agar bisa dipanggil di C++
typedef void* (*_GetMainCamera)();
typedef void (*_WorldToScreen)(void* camera, Vector3 world_pos, Vector3& screen_pos, int eye);
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
    ImGui::StyleColorsDark(); // Mengubah ke tema gelap agar menu lebih rapi

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 22.0f;
    io.Fonts->AddFontDefault(&font_cfg);

    ImGui::GetStyle().ScaleAllSizes(3.0f);
}

uintptr_t get_absolute_address(uintptr_t offset) {
    if (g_TargetModule.start_address == 0) return 0;
    return (uintptr_t)g_TargetModule.start_address + offset;
}

// LOGIKA UTAMA MENGGAMBAR ESP LINE
void DrawESP() {
    if (g_TargetModule.start_address == 0) return;

    // Teks indikator sistem aktif di pojok layar
    ImGui::GetForegroundDrawList()->AddText(ImVec2(50, 80), ImColor(0, 255, 0), "ESP System: ACTIVE");

    // 1. Ambil fungsi-fungsi berdasarkan offset Anda
    _GetMainCamera GetMainCamera = (_GetMainCamera)get_absolute_address(OFFSET_GET_MAIN);
    _GetPlayerCount GetPlayerCount = (_GetPlayerCount)get_absolute_address(OFFSET_GET_PLAYER_COUNT);
    _GetPlayerByIndex GetPlayerByIndex = (_GetPlayerByIndex)get_absolute_address(OFFSET_GET_PLAYER_BY_INDEX);
    _GetPosition GetPosition = (_GetPosition)get_absolute_address(OFFSET_GET_POSITION);
    _IsDead IsDead = (_IsDead)get_absolute_address(OFFSET_IS_DEAD);

    // Keamanan dasar agar game tidak crash jika fungsi gagal dibaca
    if (!GetMainCamera || !GetPlayerCount || !GetPlayerByIndex || !GetPosition || !IsDead) return;

    // 2. Dapatkan objek Kamera Utama game
    void* camera = GetMainCamera();
    if (!camera) return;

    // 3. Baca jumlah total pemain di sekitar
    int total_players = GetPlayerCount();
    if (total_players <= 0 || total_players > 100) return; // Batasi angka tidak wajar

    // 4. Lakukan Perulangan untuk mengecek setiap musuh
    for (int i = 0; i < total_players; i++) {
        void* player = GetPlayerByIndex(i);
        if (!player) continue;

        // Validasi: Lewati jika pemain tersebut sudah mati
        if (IsDead(player)) continue;

        // 5. Ambil koordinat 3D posisi musuh
        Vector3 enemy_world_pos{0, 0, 0};
        GetPosition(player, enemy_world_pos);

        // 6. Konversi koordinat 3D game ke posisi Layar 2D menggunakan fungsi bawaan engine game
        Vector3 enemy_screen_pos{0, 0, 0};
        
        // Panggil fungsi Unity WorldToScreen internal via offset Anda
        // Parameter ke-4 angka 2 melambangkan target mata orientasi layar lebar/ponsel
        using _W2S_Func = void(*)(void*, Vector3, Vector3&, int);
        _W2S_Func target_w2s = (_W2S_Func)get_absolute_address(OFFSET_WORLD_TO_SCREEN);
        if (target_w2s) {
            target_w2s(camera, enemy_world_pos, enemy_screen_pos, 2);
        }

        // Jika musuh berada di belakang kamera atau jaraknya tidak valid, lewati
        if (enemy_screen_pos.z < 0.0f) continue;

        // 7. GAMBAR GARIS ESP LINE
        // Menarik garis dari tengah bawah layar HP menuju kaki musuh
        ImVec2 line_start = ImVec2((float)g_GlWidth / 2.0f, (float)g_GlHeight);
        ImVec2 line_end = ImVec2(enemy_screen_pos.x, (float)g_GlHeight - enemy_screen_pos.y); // Balik koordinat Y Unity ke sistem layar Android

        ImGui::GetForegroundDrawList()->AddLine(
            line_start, 
            line_end, 
            ImColor(255, 0, 0, 255), // Warna Merah Terang
            2.0f                     // Ketebalan garis 2 piksel
        );
    }
}

EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH, &g_GlWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &g_GlHeight);

    if (!g_IsSetup) {
      SetupImGui();
      g_IsSetup = true;
    }

    ImGuiIO &io = ImGui::GetIO();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
    ImGui::NewFrame();

    // Memanggil fungsi utama gambar ESP Line kita
    DrawESP();

    ImGui::EndFrame();
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return old_eglSwapBuffers(dpy, surface);
}

void hack_start(const char *_game_data_dir) {
    LOGI("hack start | %s", _game_data_dir);
    do {
        sleep(1);
        g_TargetModule = utils::find_module(TargetLibName);
    } while (g_TargetModule.size <= 0);
    LOGI("%s: %p - %p",TargetLibName, g_TargetModule.start_address, g_TargetModule.end_address);
}

void hack_prepare(const char *_game_data_dir) {
    LOGI("hack thread: %d", gettid());
    int api_level = utils::get_android_api_level();
    LOGI("api level: %d", api_level);
    g_IniFileName = std::string(_game_data_dir) + "/files/imgui.ini";
    sleep(5);

    void *sym_input = DobbySymbolResolver("/system/lib/libinput.so", "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
    if (NULL != sym_input){
        DobbyHook((void *)sym_input, (dobby_dummy_func_t) Input, (dobby_dummy_func_t*)&origInput);
    }
    
    void *egl_handle = xdl_open("libEGL.so", 0);
    void *eglSwapBuffers = xdl_sym(egl_handle, "eglSwapBuffers", nullptr);
    if (NULL != eglSwapBuffers) {
        utils::hook((void*)eglSwapBuffers, (func_t)hook_eglSwapBuffers, (func_t*)&old_eglSwapBuffers);
    }
    xdl_close(egl_handle);

    hack_start(_game_data_dir);
}
