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

#include "coop_block.h"
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


// ---- party members on the Sea Chart (the MAP screen's whole sea): a mark in each member's tunic colour and their
// name, where they are. Drawn over the picture like the radar, nothing of the game's chart touched. The chart area is
// the sea chart menu's main pane (hdSeaChartMenu_c +0x70: the 7 x 7 squares, square D4 at its origin, one square its
// size / 7, y up; story_chart.cpp); its global matrix (the layout's, +0x48) puts a point of it on the picture. Each
// time the game updates the chart (updateProgressIcons, hooked: it runs while the chart is open and settled, not while
// it opens, zooms or closes) the matrix and the members' places are taken; the marks show while that keeps coming.
// Party members only, at sea (the server's place report, once a second), never a blocked one; Settings > Online's
// radar switch turns them off too.
namespace {
struct ChartMark { float x, z; ImU32 col; std::string name; };
struct Chart { float m[12] = {}; float w = 0, h = 0; uint32_t at = 0; std::vector<ChartMark> marks; };
Chart g_chart;  // (g_mu)
ImU32 tunic_colour(int t) {
    static const ImU32 c[] = {IM_COL32(70, 200, 90, 255), IM_COL32(230, 70, 60, 255), IM_COL32(70, 130, 240, 255), IM_COL32(170, 90, 220, 255)};
    return c[(t % 4 + 4) % 4];
}
}  // namespace

void chart_updated(uint32_t menu) {
    const uint32_t main = menu ? ld32(menu + 0x70) : 0;
    if (!main || net::role() == net::Role::kOff || !sync::settings().radar) return;
    Chart c;
    for (int i = 0; i < 12; i++) c.m[i] = (float)ldf32(main + 0x48 + 4 * i);
    c.w = (float)ldf32(main + 0x3C), c.h = (float)ldf32(main + 0x40);
    c.at = net::now_ms();
    for (int id = 0; id < net::kMaxPlayers; id++) {
        net::Place pl;
        if (id == net::my_id() || !net::member(id) || block::blocked_id(id) || !net::member_place(id, pl) || !pl.at_sea || pl.age_ms > 5000) continue;
        c.marks.push_back({pl.x, pl.z, tunic_colour(net::tunic(id)), net::name(id)});
    }
    // test aid: WWHD_COOP_TEST_CHART_SELF=1 marks this game's own Link too (to line the marks up with the game's own
    // Link mark)
    if (static const bool self = getenv("WWHD_COOP_TEST_CHART_SELF"); self)
        if (const uint32_t link = ld32(GD(0x1046F0B0) + names::inf::kLink); link >= 0x10000000u)
            c.marks.push_back({(float)ldf32(link + names::ac::kPos), (float)ldf32(link + names::ac::kPos + 8), IM_COL32(255, 255, 255, 255), "(you)"});
    if (static uint32_t n = 0; ++n % 300 == 1)
        mods::trace("coop: sea chart open: %zu party marks (area %.0f x %.0f, at %.1f %.1f)", c.marks.size(), c.w, c.h, c.m[3], c.m[7]);
    std::lock_guard<std::mutex> l(g_mu);
    g_chart = std::move(c);
}

void register_chart_overlay() {
    overlay::ext::add_frame_layer({[](std::string&) {
                                       std::lock_guard<std::mutex> l(g_mu);
                                       return g_chart.at && net::now_ms() - g_chart.at < 200 && !g_chart.marks.empty();
                                   },
                                   [](float w, float h) {
                                       Chart c;
                                       {
                                           std::lock_guard<std::mutex> l(g_mu);
                                           c = g_chart;
                                       }
                                       const float a = aspect::game() > 0 ? aspect::game() : 16.f / 9.f;
                                       const float s = std::min(h / 720.f, w / (720.f * a));  // (layout units: the TV's 1280 x 720)
                                       ImDrawList* d = ImGui::GetForegroundDrawList();
                                       for (const ChartMark& k : c.marks) {
                                           // the point in the chart area (square D4 at its origin, y up), through its global matrix
                                           const float px = k.x / 100000.f * c.w / 7.f, py = -k.z / 100000.f * c.h / 7.f;
                                           const float gx = c.m[0] * px + c.m[1] * py + c.m[3], gy = c.m[4] * px + c.m[5] * py + c.m[7];
                                           const ImVec2 p(w / 2 + gx * s, h / 2 - gy * s);
                                           d->AddCircleFilled(p, 8 * s, k.col, 16);
                                           d->AddCircle(p, 8 * s, IM_COL32(255, 255, 255, 230), 16, 2 * s);
                                           const float size = 22 * s;  // (the name: about the chart's own small print)
                                           const ImVec2 t(p.x + 12 * s, p.y - size / 2);
                                           d->AddText(ImGui::GetFont(), size, ImVec2(t.x + 2 * s, t.y + 2 * s), IM_COL32(0, 0, 0, 200), k.name.c_str());
                                           d->AddText(ImGui::GetFont(), size, t, IM_COL32(255, 255, 255, 255), k.name.c_str());
                                       }
                                       return false;
                                   }});
}

}  // namespace mods::coop::radar

// hdSeaChartMenu_c::updateProgressIcons (02678B74): the game updating its sea chart, open and settled
extern "C" void f_02678B74_orig(Cpu* c);
extern "C" void hook_02678B74(Cpu* c) {
    const uint32_t menu = c->r[3];
    f_02678B74_orig(c);
    mods::coop::radar::chart_updated(menu);
}
