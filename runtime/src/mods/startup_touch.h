// SPDX-License-Identifier: MPL-2.0
// Startup menus use the GamePad's real touchscreen; title taps are queued as a short A press.
// State comes from the loaded stage, not elapsed time or a guess from the visible buttons.
#pragma once
#include <cstdint>
#include <cstring>

namespace mods::startup_touch {
enum Scene : int32_t { kUnknown = 0, kBoot = 1, kTitle = 2, kFileSelect = 3, kGameplay = 4 };
constexpr int32_t kPackMagic = 0x57570100;  // WW, contract 1, scene in the low byte
constexpr uint32_t kConfirm = 0x8000;
inline Scene scene(bool in_game, const char* stage) {
    if (in_game) return kGameplay;
    if (stage && !std::strcmp(stage, "sea_T")) return kTitle;
    if (stage && !std::strcmp(stage, "Name")) return kFileSelect;
    return kBoot;
}
inline int32_t pack(Scene scene) { return scene == kUnknown ? 0 : kPackMagic | int32_t(scene); }

class TitlePress {
public:
    uint32_t filter(uint32_t buttons, Scene scene, bool tapped, uint64_t step) {
        if (scene != kTitle) { until_ = 0; return buttons; }
        if (tapped) until_ = step + 2;
        return buttons | (step < until_ ? kConfirm : 0);
    }
private:
    uint64_t until_ = 0;
};
}  // namespace mods::startup_touch
