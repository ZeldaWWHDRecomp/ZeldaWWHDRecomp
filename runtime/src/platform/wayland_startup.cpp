#include "wayland_startup.h"
#include "wayland_policy.h"
#include "../runtime.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace wayland_startup {
#if defined(__linux__) && !defined(__ANDROID__)
namespace {
// libwayland-client is loaded at run time, like SDL does, instead of linked: players' setup
// links the game without it, and X11-only systems may not have it at all.
struct wl_proxy;
struct wl_display;
struct wl_event_queue;
struct wl_interface;
using MarshalFlags = wl_proxy* (*)(wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, ...);
struct WaylandClient {
    MarshalFlags marshal_flags;
    uint32_t (*get_version)(wl_proxy*);
    int (*add_listener)(wl_proxy*, void (**)(void), void*);
    void (*destroy)(wl_proxy*);
    void (*set_queue)(wl_proxy*, wl_event_queue*);
    wl_event_queue* (*create_queue)(wl_display*);
    void (*queue_destroy)(wl_event_queue*);
    int (*roundtrip_queue)(wl_display*, wl_event_queue*);
    const wl_interface* registry_interface;
};
bool load(WaylandClient& c) {
    SDL_SharedObject* lib = SDL_LoadObject("libwayland-client.so.0");
    if (!lib) return false;  // stays loaded: SDL's Wayland driver holds the same library
    auto sym = [&](const char* name) { return reinterpret_cast<void*>(SDL_LoadFunction(lib, name)); };
    c.marshal_flags = reinterpret_cast<MarshalFlags>(sym("wl_proxy_marshal_flags"));
    c.get_version = reinterpret_cast<uint32_t (*)(wl_proxy*)>(sym("wl_proxy_get_version"));
    c.add_listener = reinterpret_cast<int (*)(wl_proxy*, void (**)(void), void*)>(sym("wl_proxy_add_listener"));
    c.destroy = reinterpret_cast<void (*)(wl_proxy*)>(sym("wl_proxy_destroy"));
    c.set_queue = reinterpret_cast<void (*)(wl_proxy*, wl_event_queue*)>(sym("wl_proxy_set_queue"));
    c.create_queue = reinterpret_cast<wl_event_queue* (*)(wl_display*)>(sym("wl_display_create_queue"));
    c.queue_destroy = reinterpret_cast<void (*)(wl_event_queue*)>(sym("wl_event_queue_destroy"));
    c.roundtrip_queue = reinterpret_cast<int (*)(wl_display*, wl_event_queue*)>(sym("wl_display_roundtrip_queue"));
    c.registry_interface = static_cast<const wl_interface*>(sym("wl_registry_interface"));
    return c.marshal_flags && c.get_version && c.add_listener && c.destroy && c.set_queue &&
           c.create_queue && c.queue_destroy && c.roundtrip_queue && c.registry_interface;
}
struct RegistryInfo { bool syncobj = false; };
void global(void* data, wl_proxy*, uint32_t, const char* interface, uint32_t) {
    if (std::strcmp(interface, "wp_linux_drm_syncobj_manager_v1") == 0)
        static_cast<RegistryInfo*>(data)->syncobj = true;
}
void removed(void*, wl_proxy*, uint32_t) {}
// Same layout as wl_registry_listener.
struct RegistryListener {
    void (*global)(void*, wl_proxy*, uint32_t, const char*, uint32_t);
    void (*global_remove)(void*, wl_proxy*, uint32_t);
};
const RegistryListener listener{global, removed};
const char* syncobj_support() {
    auto* display = static_cast<wl_display*>(SDL_GetPointerProperty(
        SDL_GetGlobalProperties(), SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, nullptr));
    if (!display) return "unknown";
    WaylandClient c{};
    if (!load(c)) return "unknown";
    // Private queue: the roundtrip must not dispatch SDL's listeners reentrantly. This runs
    // before any Vulkan surface or render thread exists. Never bind the syncobj manager.
    auto* queue = c.create_queue(display);
    if (!queue) return "unknown";
    // wl_display_get_registry(), as inlined from wayland-client-protocol.h (opcode 1).
    auto* display_proxy = reinterpret_cast<wl_proxy*>(display);
    wl_proxy* registry = c.marshal_flags(display_proxy, 1, c.registry_interface,
                                         c.get_version(display_proxy), 0, nullptr);
    if (!registry) { c.queue_destroy(queue); return "unknown"; }
    c.set_queue(registry, queue);
    RegistryInfo info;
    c.add_listener(registry, reinterpret_cast<void (**)(void)>(const_cast<RegistryListener*>(&listener)), &info);
    const int result = c.roundtrip_queue(display, queue);
    c.destroy(registry);
    c.queue_destroy(queue);
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
