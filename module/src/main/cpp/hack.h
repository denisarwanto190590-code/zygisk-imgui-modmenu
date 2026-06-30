#ifndef ZYGISK_IMGUI_MODMENU_HACK_H
#define ZYGISK_IMGUI_MODMENU_HACK_H

// Perbaikan makro Hook agar sesuai dengan pemanggilan fungsi di hack.cpp
#define HOOKAF(ret, func, ...) \
    ret (*orig##func)(__VA_ARGS__); \
    ret func(__VA_ARGS__)

void hack_prepare(const char *game_data_dir);

#endif //ZYGISK_IMGUI_MODMENU_HACK_H
