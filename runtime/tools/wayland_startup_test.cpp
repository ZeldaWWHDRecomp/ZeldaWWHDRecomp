// Exercise the real SDL/Wayland startup path without creating a window or Vulkan device.
#include "platform/wayland_startup.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<std::string> logs;
void log_msg(const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    logs.emplace_back(text);
    puts(text);
}

int main(int argc, char** argv) {
    assert(argc >= 2);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
        return 1;
    }
    assert(std::strcmp(SDL_GetCurrentVideoDriver(), argv[1]) == 0);
    const bool wayland = std::strcmp(argv[1], "wayland") == 0;
#if defined(__linux__) && !defined(__ANDROID__)
    const bool applies = wayland;
#else
    const bool applies = false;
#endif
    // A missing value enables the NVIDIA workaround only on native Linux Wayland.
    SDL_unsetenv_unsafe("__NV_DISABLE_EXPLICIT_SYNC");
    wayland_startup::configure();
    const char* value = std::getenv("__NV_DISABLE_EXPLICIT_SYNC");
    assert(applies ? value && std::strcmp(value, "1") == 0 : value == nullptr);
    assert(!logs.empty());
    assert(logs.front().find(std::string("video driver=") + argv[1]) != std::string::npos);
    if (argc >= 3)
        assert(logs.front().find(std::string("wp_linux_drm_syncobj_manager_v1=") + argv[2]) != std::string::npos);
    if (wayland)
        assert(logs.front().find("=unknown") == std::string::npos);

    // Preserve explicitly disabled, enabled, empty and arbitrary inherited values verbatim.
    for (const char* override_value : {"0", "1", "", "custom"}) {
        assert(SDL_setenv_unsafe("__NV_DISABLE_EXPLICIT_SYNC", override_value, 1) == 0);
        wayland_startup::configure();
        value = std::getenv("__NV_DISABLE_EXPLICIT_SYNC");
        assert(value && std::strcmp(value, override_value) == 0);
    }
    SDL_Quit();
    puts("wayland_startup_test: driver, protocol probe, native default and overrides passed");
}
