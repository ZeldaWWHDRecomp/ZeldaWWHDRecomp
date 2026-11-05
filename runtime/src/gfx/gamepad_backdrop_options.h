#pragma once
#include "gamepad_backdrop.h"
#include "../overlay/hostui.h"
#include <string>

namespace gfx::gamepad_backdrop {
inline constexpr const char* kSettingKey = "gamepad-bar-background";
inline void load_options() {
    std::string saved;
    enabled.store(!hostui::get(kSettingKey, saved) || saved != "0", std::memory_order_relaxed);
}
inline void set_option(bool on) {
    enabled.store(on, std::memory_order_relaxed);
    hostui::set(kSettingKey, on ? "1" : "0");
}
} // namespace gfx::gamepad_backdrop
