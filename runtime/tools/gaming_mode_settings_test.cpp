// Exercise the actual SDL host's settings migration and writes without game data.
#include "../src/gfx/vulkan/overlay_sdl.cpp"
#include "../src/render_prof.h"
#include "../src/background_shaders.h"
#include <cassert>
#include <cstdarg>
#include <unistd.h>
namespace gfxvk {
Renderer R;
SDL_Window* ensure_gamepad_window(bool) { return R.drc.window; }
bool gamepad_window_available() { return !getenv("WWHD_NO_GAMEPAD"); }
}
void log_msg(const char*, ...) {}
namespace input { void set_touch(bool, float, float) {} }
namespace aspect { void set_window_aspect(float) {} }
int main() {
    const auto file = std::filesystem::temp_directory_path() / ("wwhd-gaming-settings-" + std::to_string(getpid()) + ".ini");
    setenv("WWHD_SETTINGS", file.c_str(), 1);
    unsetenv("WAYLAND_DISPLAY"); unsetenv("GAMESCOPE_WAYLAND_DISPLAY");
    unsetenv("WINE_HOST_XDG_CURRENT_DESKTOP");
    setenv("XDG_CURRENT_DESKTOP", "gamescope", 1);
    unsetenv("WWHD_DRC_MODE"); unsetenv("WWHD_FULLSCREEN"); unsetenv("WWHD_TEST_DRC_MODE");
    unsetenv("WWHD_NO_GAMEPAD");
    auto reset = [&] {
        hostui::g_values.clear(); hostui::g_loaded = true;
        hostui::g_mode_chosen = false; gfx::g_mode = gfx::kDrcWindow;
    };
    reset();
    assert(hostui::startup_drc_mode() == gfx::kDrcPip);
    gfx::g_mode = gfx::kDrcPip;
    hostui::set_pip_size(.33f);
    assert(!hostui::g_values.count("drcMode")); // incidental save must not freeze the default
    assert(!hostui::g_values.count("tvFullScreen"));
    hostui::set_drc_mode(gfx::kDrcWindow);
    assert(hostui::g_values.at("drcMode") == "window");
    assert(hostui::startup_drc_mode() == gfx::kDrcWindow);
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(hostui::startup_drc_mode() == gfx::kDrcWindow); // reload actual settings.ini
    setenv("WWHD_DRC_MODE", "off", 1);
    assert(hostui::startup_drc_mode() == gfx::kDrcOff);
    gfx::g_mode = gfx::kDrcOff; hostui::set_pip_size(.25f);
    assert(hostui::g_values.at("drcMode") == "window"); // override not persisted
    unsetenv("WWHD_DRC_MODE");
    reset(); setenv("WWHD_NO_GAMEPAD", "1", 1);
    assert(hostui::startup_drc_mode() == gfx::kDrcWindow); // host's existing no-window fallback is Off
    unsetenv("WWHD_NO_GAMEPAD");
    reset(); setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    assert(hostui::startup_drc_mode() == gfx::kDrcWindow);
    // Explicitly choosing windowed counts even when the default is already windowed.
    assert(SDL_Init(SDL_INIT_VIDEO));
    gfxvk::R.tv.window = SDL_CreateWindow("settings test", 128, 128, SDL_WINDOW_HIDDEN);
    assert(gfxvk::R.tv.window);
    hostui::set_fullscreen(false);
    assert(hostui::g_values.at("tvFullScreen") == "0");
    // A hidden Gaming Mode startup issues the real SDL request. On the dummy
    // display, showing it proves fullscreen is queued, without a real display.
    reset(); setenv("XDG_CURRENT_DESKTOP", "gamescope", 1);
    setenv("WWHD_FULLSCREEN", "0", 1);
    assert(!hostui::apply_startup_fullscreen(true)); // environment wins
    unsetenv("WWHD_FULLSCREEN");
    assert(!hostui::apply_startup_fullscreen(false)); // saved windowed choice wins
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    assert(!hostui::apply_startup_fullscreen(true)); // Desktop hidden-window behavior stays
    setenv("XDG_CURRENT_DESKTOP", "gamescope", 1);
    assert(hostui::apply_startup_fullscreen(true));
    assert(SDL_GetWindowFlags(gfxvk::R.tv.window) & SDL_WINDOW_HIDDEN);
    assert(!hostui::g_values.count("tvFullScreen"));
    assert(SDL_ShowWindow(gfxvk::R.tv.window));
    assert(SDL_GetWindowFlags(gfxvk::R.tv.window) & SDL_WINDOW_FULLSCREEN);
    hostui::tv_fullscreen_changed();
    assert(!hostui::g_values.count("tvFullScreen")); // queued default isn't a choice
    hostui::set_fullscreen(false);
    assert(!(SDL_GetWindowFlags(gfxvk::R.tv.window) & SDL_WINDOW_FULLSCREEN));
    assert(hostui::g_values.at("tvFullScreen") == "0");
    SDL_DestroyWindow(gfxvk::R.tv.window); gfxvk::R.tv.window = nullptr;
    // Detailed performance profiling (render_prof.h): never saved means off; the choice survives a restart
    std::string profiling;
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(!hostui::get(rprof::kSettingKey, profiling));
    hostui::set(rprof::kSettingKey, "1");
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(hostui::get(rprof::kSettingKey, profiling) && profiling == "1");
    hostui::set(rprof::kSettingKey, "0");
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(hostui::get(rprof::kSettingKey, profiling) && profiling == "0");
    // Compile shaders in the background (background_shaders.h): never saved means off; the choice survives a restart
    std::string background;
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(!hostui::get(bgshaders::kSettingKey, background));
    hostui::set(bgshaders::kSettingKey, "fast");
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(hostui::get(bgshaders::kSettingKey, background) && background == "fast");
    hostui::set(bgshaders::kSettingKey, "0");
    hostui::g_loaded = false; hostui::g_values.clear();
    assert(hostui::get(bgshaders::kSettingKey, background) && background == "0");
    SDL_Quit(); std::filesystem::remove(file);
}
