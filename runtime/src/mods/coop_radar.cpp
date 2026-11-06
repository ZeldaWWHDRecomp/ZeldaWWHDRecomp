// SPDX-License-Identifier: MPL-2.0
// Online co-op: far players on a small sea radar (GAPS C13, the far tier: DESIGN.md's "L2" markers).
//
// Through a server, players in this sea square beyond the puppets' reach come as far poses (the far tier) or as the
// server's markers (the rest of the square). At sea they are dots on a round radar in the picture's lower left corner:
// this game's Link in the middle, the camera's view up, 25,000 units to the edge (a quarter of a square; dots further
// out sit on the rim). Party members are green. Nothing of the game's own map or chart is touched. Settings > Online
// turns it off.
#include "coop_radar.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "coop_names.h"
#include "coop_net.h"
#include "coop_sync.h"
#include "guest_addr.h"
#include "imgui.h"
#include "mods.h"
#include "runtime.h"
#include "../aspect.h"
#include "../interp.h"
#include "../overlay/extensions.h"

namespace mods::coop::radar {
namespace {
std::mutex g_mu;
std::vector<Dot> g_dots;  // the radar as the last step left it (relative, already turned)
std::vector<Ring> g_rings;  // (C28) the safe zones' borders
std::vector<Far> g_zone_at;  // (game thread) the zones as set
std::vector<float> g_zone_r;
std::atomic<bool> g_on{false};
}  // namespace

void set_zones(const std::vector<Far>& centres, const std::vector<float>& radii) { g_zone_at = centres, g_zone_r = radii; }

void step(uint32_t link, const std::vector<Far>& far) {
    const bool sea = link >= 0x10000000u && !strncmp((const char*)mem::ptr(GD(0x1046F0B0) + names::inf::kCurStage), "sea", 4);
    if (!sea || (far.empty() && g_zone_at.empty()) || !sync::settings().radar) { g_on = false; return; }
    const uint32_t at = link + names::ac::kPos;
    const float me[3] = {(float)ldf32(at), (float)ldf32(at + 4), (float)ldf32(at + 8)};
    float yaw = 0;  // the drawn camera's heading (eye to centre), else Link's
    if (const uint32_t cam = interp::last_camera(); cam >= 0x10000000u && cam < 0x50000000u) {
        const float fx = (float)ldf32(cam + 0xE8) - (float)ldf32(cam + 0xDC), fz = (float)ldf32(cam + 0xF0) - (float)ldf32(cam + 0xE4);
        yaw = std::atan2(fx, fz);
    } else {
        yaw = (int16_t)ld16(link + names::ac::kAngleY) * 3.14159265f / 32768;
    }
    std::lock_guard<std::mutex> l(g_mu);
    g_dots = place(me, yaw, far);
    g_rings = rings(me, yaw, g_zone_at, g_zone_r);
    g_on = !g_dots.empty() || !g_rings.empty();
    if (static uint32_t n = 0; ++n % 300 == 0) mods::trace("coop: radar: %zu far players", g_dots.size());
}

void register_overlay() {
    overlay::ext::add_frame_layer({[](std::string&) { return g_on.load(); },
                                   [](float w, float h) {
                                       std::vector<Dot> dots;
                                       std::vector<Ring> rings;
                                       {
                                           std::lock_guard<std::mutex> l(g_mu);
                                           dots = g_dots, rings = g_rings;
                                       }
                                       const float a = aspect::game() > 0 ? aspect::game() : 16.f / 9.f;
                                       const float s = std::min(h / 720.f, w / (720.f * a));  // (layout units)
                                       const float r = 70 * s, cx = w / 2 - (640.f - 110) * s, cy = h / 2 + (360.f - 210) * s;
                                       ImDrawList* d = ImGui::GetForegroundDrawList();
                                       d->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(20, 40, 60, 140), 48);
                                       d->AddCircle(ImVec2(cx, cy), r, IM_COL32(230, 230, 210, 200), 48, 2 * s);
                                       // (C28) safe zones: their borders, clipped to the radar (blue: no fighting inside)
                                       d->PushClipRect(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), true);
                                       for (const Ring& z : rings)
                                           d->AddCircle(ImVec2(cx + z.x * r, cy - z.y * r), z.r * r, IM_COL32(120, 190, 255, 220), 64, 2 * s);
                                       d->PopClipRect();
                                       d->AddTriangleFilled(ImVec2(cx, cy - 6 * s), ImVec2(cx - 4 * s, cy + 4 * s), ImVec2(cx + 4 * s, cy + 4 * s),
                                                            IM_COL32(255, 230, 120, 255));
                                       for (const Dot& o : dots) {
                                           const ImVec2 p(cx + o.x * r, cy - o.y * r);
                                           const ImU32 col = o.party ? IM_COL32(110, 230, 120, 255)
                                                         : o.pvp ? IM_COL32(240, 70, 60, o.rim ? 170 : 255)
                                                                 : IM_COL32(245, 245, 245, o.rim ? 150 : 255);
                                           d->AddCircleFilled(p, 3.5f * s, col, 12);
                                       }
                                       return false;
                                   }});
}

}  // namespace mods::coop::radar
