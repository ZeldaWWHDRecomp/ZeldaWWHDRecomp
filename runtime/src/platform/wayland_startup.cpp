#include "wayland_startup.h"
#include "wayland_policy.h"
#include "../runtime.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>
#if defined(__linux__) && !defined(__ANDROID__)
#include <wayland-client.h>
#endif

namespace wayland_startup {
#if defined(__linux__) && !defined(__ANDROID__)
namespace {
struct RegistryInfo { bool syncobj = false; };
void global(void* data, wl_registry*, uint32_t, const char* interface, uint32_t) {
    if (std::strcmp(interface, "wp_linux_drm_syncobj_manager_v1") == 0)
        static_cast<RegistryInfo*>(data)->syncobj = true;
}
void removed(void*, wl_registry*, uint32_t) {}
const wl_registry_listener listener{global, removed};
const char* syncobj_support() {
    auto* display = static_cast<wl_display*>(SDL_GetPointerProperty(
        SDL_GetGlobalProperties(), SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, nullptr));
    if (!display) return "unknown";
    // Private queue: the roundtrip must not dispatch SDL's listeners reentrantly. This runs
    // before any Vulkan surface or render thread exists. Never bind the syncobj manager.
    auto* queue = wl_display_create_queue(display);
    if (!queue) return "unknown";
    auto* registry = wl_display_get_registry(display);
    if (!registry) { wl_event_queue_destroy(queue); return "unknown"; }
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(registry), queue);
    RegistryInfo info;
    wl_registry_add_listener(registry, &listener, &info);
    const int result = wl_display_roundtrip_queue(display, queue);
    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);
    return result < 0 ? "unknown" : info.syncobj ? "yes" : "no";
}
}
#endif

void configure() {
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool wayland = driver && std::strcmp(driver, "wayland") == 0;
    const char* syncobj = "not applicable";
#if defined(__linux__) && !defined(__ANDROID__)
    if (wayland) syncobj = syncobj_support();
#endif
    LOG("[display] SDL %d.%d.%d video driver=%s; wp_linux_drm_syncobj_manager_v1=%s",
        SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
        SDL_VERSIONNUM_MICRO(SDL_GetVersion()), driver ? driver : "unknown", syncobj);
#if defined(__linux__) && !defined(__ANDROID__)
    if (wayland_policy::disable_nvidia_explicit_sync(wayland, std::getenv("__NV_DISABLE_EXPLICIT_SYNC") != nullptr)) {
        if (SDL_setenv_unsafe("__NV_DISABLE_EXPLICIT_SYNC", "1", 0) == 0)
            LOG("[display] Wayland: setting __NV_DISABLE_EXPLICIT_SYNC=1 before Vulkan load to avoid NVIDIA surface-commit disconnects (requires NVIDIA 575.51.02+); if it still closes, use SDL_VIDEO_DRIVER=x11");
        else
            LOG("[display] Wayland: could not disable NVIDIA explicit sync; if the window closes, use SDL_VIDEO_DRIVER=x11");
    } else if (wayland) {
        LOG("[display] Wayland: preserving __NV_DISABLE_EXPLICIT_SYNC=%s; if NVIDIA closes the window with an explicit-sync error, use __NV_DISABLE_EXPLICIT_SYNC=1 (575.51.02+) or SDL_VIDEO_DRIVER=x11",
            std::getenv("__NV_DISABLE_EXPLICIT_SYNC"));
    }
#endif
}
}  // namespace wayland_startup
