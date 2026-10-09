// Pure pane policy shared by the guest hooks and their synthetic tests.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace aspect::panes {
enum class Role { Content, Fill, Hud, Projected };
struct Transform { float x, y, sx, sy; };
inline Transform transform(Role role, float x, float y, float sx, float sy, float kx, float ky) {
    Transform t{x,y,sx,sy};
    if (role == Role::Fill) { t.sx *= kx; t.sy *= ky; }
    if (role == Role::Projected) { t.x *= kx; t.y *= ky; }
    if (role == Role::Hud) {
        if (x <= -300) t.x -= 640*(kx-1);
        if (x >= 300) t.x += 640*(kx-1);
        if (y <= -200) t.y -= 360*(ky-1);
        if (y >= 200) t.y += 360*(ky-1);
    }
    return t;
}
inline bool fill_name(std::string_view name) {
    // Pause pictures contain the captured game/filter, not the menu frame (PR #16).
    // The other names describe authored backgrounds and transition wipes.
    constexpr std::string_view names[] = {
        "PF_PauseTV_00", "PF_PauseDRC_00", "BootBase_00", "BootDeco_00",
        "P_Base_00", "P_Base_01", "PF_BG_00", "P_BG_00", "W_BG_00",
        "P_NoDataBG_00", "P_Dark_00", "P_Dark_01", "P_Light_00", "P_Light_01",
        "P_Wave00", "P_Wave_01", "P_OpeningPic_00", "W_Wipe_00",
        "P_BgPattern_00", "W_Shutter_00", "W_FadeWipe_00", "W_FadeWipe_01", "P_FadeWipe_00"};
    for (auto n : names) if (name == n) return true;
    return false;
}
inline bool fill(std::string_view name, bool leaf, float x, float y, float w, float h) {
    return leaf && fill_name(name) && std::fabs(x) < 8 && std::fabs(y) < 8 && w >= 1270 && h >= 710;
}
inline bool parked_hud(float x, float y) {
    return std::fabs(x) > 672 || std::fabs(y) > 392;
}
inline bool projected_root_name(std::string_view name) {
    // These layouts are placed at a 3D actor's projected position. A translated root alone
    // is not evidence of that role: selectors and independently drawn map floors translate too.
    return name == "N_EnemyHP_00" || name == "T_CommandA_00" || name == "P_SetSeatASpecial_00";
}
// Intersect the guest scissor with the centred native layout region. Call only for TV content;
// render-target aspect factors exclude resolution scaling, so this also works at 2x/3x.
inline void clip(uint32_t w, uint32_t h, float kx, float ky,
                 uint32_t& x, uint32_t& y, uint32_t& ex, uint32_t& ey) {
    if (kx == 1 && ky == 1) return;
    // Match the renderer's scissor rounding tolerance: e.g. 800/(10/9) can be just below
    // 720 in float, which must not trim an extra pixel from both edges at 16:10.
    uint32_t left = uint32_t(std::ceil((w - w / kx) * .5f - .01f));
    uint32_t top = uint32_t(std::ceil((h - h / ky) * .5f - .01f));
    x = std::max(x, left); y = std::max(y, top);
    ex = std::min(ex, w - left); ey = std::min(ey, h - top);
}
}
