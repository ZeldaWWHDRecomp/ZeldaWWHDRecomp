// Switches of the draw threads (multithreaded draw recording, based on VR-WakerHD's design): each off unless its
// variable is exactly "1". Each constant reads the environment itself: an inline constant built from other inline
// constants may be initialised before them (seen on Windows). No draw thread is started yet: the thread switches only
// select per-thread state (draw_thread_state.h); the cache locks (draw_thread_locks.h) stay idle unless
// WWHD_VK_LOCK_ALWAYS=1 is set with one of them.
#pragma once
#include <cstdlib>
#include <cstring>

namespace gfxvk::draw_switches {
inline bool env_on(const char* name) {
    const char* e = std::getenv(name);
    return e && !std::strcmp(e, "1");
}
// the sun shadow atlas's draws may record on a draw thread of their own
inline const bool kAtlasThread = env_on("WWHD_VK_ATLAS_THREAD");
// the scene's draws may record on draw threads
inline const bool kSceneThread = env_on("WWHD_VK_SCENE_THREAD");
// (diagnostics) the cache locks taken with a single recording thread too
inline const bool kLockAlways = env_on("WWHD_VK_LOCK_ALWAYS");
// a draw thread may run this session: draw_thread_state.h keeps per-thread state in thread_locals only then
inline bool any_thread() {
    return env_on("WWHD_VK_ATLAS_THREAD") || env_on("WWHD_VK_SCENE_THREAD") || env_on("WWHD_VK_PER_THREAD");
}
}  // namespace gfxvk::draw_switches
