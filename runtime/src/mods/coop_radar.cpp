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
#include <cfloat>
#include <memory>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "coop_block.h"
#include "game_look.h"
#include "coop_names.h"
#include "coop_net.h"
#include "coop_sync.h"
#include "guest_addr.h"
#include "imgui.h"
#include "imgui_internal.h"
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

void step(uint32_t link, const std::vector<Far>& far_players) {
    chart_step();
    const bool sea = link >= 0x10000000u && !strncmp((const char*)mem::ptr(GD(0x1046F0B0) + names::inf::kCurStage), "sea", 4);
    if (!sea || (far_players.empty() && g_zone_at.empty()) || !sync::settings().radar) { g_on = false; return; }
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
    g_dots = place(me, yaw, far_players);
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


// ---- players on the Sea Chart (the menu's MAP page: its 7 x 7 grid of the Great Sea): each party member at sea as the
// chart's own Link icon (its picture read from this game's menu, the hat in the member's tunic colour) with their name
// over it; every other player at sea the server shows (coop_net's sea_players) as a small grey diamond with their name.
// Drawn over the picture as the radar is, nothing of the game's chart touched; this game's own Link is the game's.
// Only while the chart is on the TV, open and settled: the in-game menu open (hdIngameUi_c's state, +0x0C) on its MAP
// page (mMenuSelection +0x224 == 1), and the chart updated (updateProgressIcons, hooked: not while it opens, zooms or
// closes) in the last 200 ms. Never in play, on the HUD's mini-map or the GamePad's own chart (the GamePad screen is
// not where the overlay draws). The chart area is the menu's main pane (+0x70: the 7 x 7 squares, square D4 at its
// origin, a square its size / 7, y up); its global matrix (+0x48) puts a sea place on the picture. Settings > Online:
// a switch for party members, one for the others; blocked players are never sent or shown.
namespace {
struct ChartMark { float x, z; int tunic; std::string name; bool party; };
struct Chart { float m[12] = {}; float w = 0, h = 0, icon = 0; uint32_t at = 0; std::vector<ChartMark> marks; };
Chart g_chart;                   // (g_mu)
game_glyph::Bitmap g_face;        // (g_mu) the chart's Link icon (P_MapLinkFace_00), read once
std::unique_ptr<ImTextureData> g_faces[4];  // (render thread) the icon in each tunic colour
const uint32_t kIngameUi = GD(0x101F8344);
const ImU32 kTunicRgb[] = {IM_COL32(70, 200, 90, 255), IM_COL32(230, 70, 60, 255), IM_COL32(70, 130, 240, 255), IM_COL32(170, 90, 220, 255)};

// the in-game menu open on its MAP page (game thread)
bool menu_on_map() {
    const uint32_t ui = ld32(kIngameUi);
    return ui >= 0x10000000u && ui < 0x50000000u && ld32(ui + 0x0C) == GD(0x1049ECB0) && ld32(ui + 0x224) == 1;
}
// the icon with its green (the hat) turned to a tunic's colour, each pixel's brightness kept
std::unique_ptr<ImTextureData> tinted(const game_glyph::Bitmap& b, int tunic) {
    auto t = std::make_unique<ImTextureData>();
    t->Create(ImTextureFormat_RGBA32, b.width, b.height);
    t->UseColors = true;
    uint8_t* px = (uint8_t*)t->GetPixels();
    memcpy(px, b.rgba.data(), b.rgba.size());
    const ImU32 c = kTunicRgb[tunic & 3];
    const float tr = (c & 0xFF) / 255.f, tg = (c >> 8 & 0xFF) / 255.f, tb = (c >> 16 & 0xFF) / 255.f;
    for (size_t i = 0; i + 3 < b.rgba.size(); i += 4) {
        const int r = px[i], g = px[i + 1], bl = px[i + 2];
        if (tunic == 0 || !(g > r + 25 && g > bl + 25)) continue;  // (green, his own tunic: as the game's)
        const float l = std::min(1.3f, g / 180.f);
        px[i] = (uint8_t)std::min(255.f, tr * 255 * l), px[i + 1] = (uint8_t)std::min(255.f, tg * 255 * l), px[i + 2] = (uint8_t)std::min(255.f, tb * 255 * l);
    }
    ImGui::RegisterUserTexture(t.get());
    return t;
}
}  // namespace

void chart_updated(uint32_t menu) {
    const uint32_t main = menu ? ld32(menu + 0x70) : 0;
    const sync::Settings set = sync::settings();
    if (!main || net::role() == net::Role::kOff || !menu_on_map() || !(set.chart_party || set.chart_others)) {
        std::lock_guard<std::mutex> l(g_mu);
        g_chart.at = 0;
        return;
    }
    Chart c;
    for (int i = 0; i < 12; i++) c.m[i] = (float)ldf32(main + 0x48 + 4 * i);
    c.w = (float)ldf32(main + 0x3C), c.h = (float)ldf32(main + 0x40);
    c.at = net::now_ms();
    // the game's own Link icon: its picture (once) and its size on the chart (its pane's size through its global matrix)
    const uint32_t face = game_look::find_pane(game_look::layout_root(menu), "P_MapLinkFace_00");
    if (face) c.icon = (float)ldf32(face + 0x3C) * std::hypot((float)ldf32(face + 0x48), (float)ldf32(face + 0x48 + 16));
    bool need_face = false;
    {
        std::lock_guard<std::mutex> l(g_mu);
        need_face = g_face.width == 0;
    }
    if (need_face && face) {
        game_glyph::Bitmap b;
        if (game_look::texture(face, {0xF4, 0x28}, b) && b.width > 0) {
            std::lock_guard<std::mutex> l(g_mu);
            g_face = std::move(b);
            mods::trace("coop: sea chart: the game's Link icon read (%dx%d)", g_face.width, g_face.height);
        }
    }
    if (set.chart_party)
        for (int id = 0; id < net::kMaxPlayers; id++) {
            net::Place pl;
            if (id == net::my_id() || !net::member(id) || block::blocked_id(id) || !net::member_place(id, pl) || !pl.at_sea || pl.age_ms > 5000) continue;
            c.marks.push_back({pl.x, pl.z, net::tunic(id), net::name(id), true});
        }
    if (set.chart_others)
        for (const net::SeaMark& o : net::sea_players()) c.marks.push_back({o.x, o.z, -1, o.name, false});
    // test aid: WWHD_COOP_TEST_CHART_SELF=1 marks this game's own Link too, as a party member in red (to line the marks
    // up with the game's own Link icon)
    if (static const bool self = getenv("WWHD_COOP_TEST_CHART_SELF"); self)
        if (const uint32_t link = ld32(GD(0x1046F0B0) + names::inf::kLink); link >= 0x10000000u)
            c.marks.push_back({(float)ldf32(link + names::ac::kPos), (float)ldf32(link + names::ac::kPos + 8), 1, "(you)", true});
    if (static uint32_t n = 0; ++n % 300 == 1) {
        size_t party = 0;
        for (const ChartMark& k : c.marks) party += k.party;
        mods::trace("coop: sea chart open: %zu party marks, %zu others", party, c.marks.size() - party);
    }
    std::lock_guard<std::mutex> l(g_mu);
    g_chart = std::move(c);
}

void chart_step() {  // (each step: the chart gone from the screen, its marks at once)
    if (menu_on_map()) return;
    std::lock_guard<std::mutex> l(g_mu);
    g_chart.at = 0;
}

void chart_backend_destroyed() {
    for (auto& t : g_faces)
        if (t) {
            if (ImGui::GetCurrentContext()) ImGui::GetPlatformIO().Textures.find_erase(t.get()), ImGui::UnregisterUserTexture(t.get());
            t.reset();
        }
}
bool chart_texture(uint64_t id) {
    for (const auto& t : g_faces)
        if (t && uint64_t(t->GetTexID()) == id) return true;
    return false;
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
                                           if (g_face.width && !g_faces[0])
                                               for (int t = 0; t < 4; t++) g_faces[t] = tinted(g_face, t);
                                       }
                                       const float a = aspect::game() > 0 ? aspect::game() : 16.f / 9.f;
                                       const float s = std::min(h / 720.f, w / (720.f * a));  // (layout units: the TV's 1280 x 720)
                                       ImDrawList* d = ImGui::GetForegroundDrawList();
                                       ImFont* font = ImGui::GetFont();
                                       const float icon = (c.icon > 8 && c.icon < 200 ? c.icon : 48) * s;
                                       auto label = [&](ImVec2 at, float size, const std::string& t) {  // (centred on at.x)
                                           const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, t.c_str());
                                           const ImVec2 p(at.x - sz.x / 2, at.y);
                                           d->AddText(font, size, ImVec2(p.x + 2 * s, p.y + 2 * s), IM_COL32(0, 0, 0, 190), t.c_str());
                                           d->AddText(font, size, p, IM_COL32(255, 255, 255, 255), t.c_str());
                                       };
                                       auto diamond = [&](ImVec2 p, float r) {
                                           const ImVec2 q[4] = {{p.x, p.y - r}, {p.x + r, p.y}, {p.x, p.y + r}, {p.x - r, p.y}};
                                           d->AddConvexPolyFilled(q, 4, IM_COL32(215, 215, 205, 240));
                                           d->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(60, 60, 60, 230), 2 * s);
                                       };
                                       bool any_party = false, any_other = false;
                                       for (const ChartMark& k : c.marks) {  // (the others first: party members over them)
                                           if (k.party) { any_party = true; continue; }
                                           any_other = true;
                                           const float px = k.x / 100000.f * c.w / 7.f, py = -k.z / 100000.f * c.h / 7.f;
                                           const ImVec2 p(w / 2 + (c.m[0] * px + c.m[1] * py + c.m[3]) * s, h / 2 - (c.m[4] * px + c.m[5] * py + c.m[7]) * s);
                                           diamond(p, 7 * s);
                                           label(ImVec2(p.x, p.y + 9 * s), 18 * s, k.name);
                                       }
                                       for (const ChartMark& k : c.marks) {
                                           if (!k.party) continue;
                                           const float px = k.x / 100000.f * c.w / 7.f, py = -k.z / 100000.f * c.h / 7.f;
                                           const ImVec2 p(w / 2 + (c.m[0] * px + c.m[1] * py + c.m[3]) * s, h / 2 - (c.m[4] * px + c.m[5] * py + c.m[7]) * s);
                                           if (ImTextureData* t = g_faces[k.tunic & 3].get())
                                               d->AddImage(t->GetTexRef(), ImVec2(p.x - icon / 2, p.y - icon / 2), ImVec2(p.x + icon / 2, p.y + icon / 2));
                                           else
                                               d->AddCircleFilled(p, icon / 3, kTunicRgb[k.tunic & 3], 16);
                                           label(ImVec2(p.x, p.y - icon / 2 - 22 * s), 22 * s, k.name);
                                       }
                                       // the legend, under the chart's bottom left corner
                                       const float lx = w / 2 + (c.m[0] * (-c.w / 2) + c.m[3]) * s, ly = h / 2 - (c.m[5] * (-c.h / 2) + c.m[7]) * s + 6 * s;
                                       float x = lx;
                                       if (any_party) {
                                           if (ImTextureData* t = g_faces[1].get()) d->AddImage(t->GetTexRef(), ImVec2(x, ly), ImVec2(x + 22 * s, ly + 22 * s));
                                           d->AddText(font, 18 * s, ImVec2(x + 26 * s, ly + 2 * s), IM_COL32(60, 45, 30, 255), "party");
                                           x += 90 * s;
                                       }
                                       if (any_other) {
                                           diamond(ImVec2(x + 8 * s, ly + 11 * s), 7 * s);
                                           d->AddText(font, 18 * s, ImVec2(x + 22 * s, ly + 2 * s), IM_COL32(60, 45, 30, 255), "other players");
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
