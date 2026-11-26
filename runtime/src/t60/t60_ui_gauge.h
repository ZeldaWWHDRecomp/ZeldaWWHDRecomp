// The flyer's magic gauge per pass (2026-11-23). Included by
// t60_ui.cpp only (after the pane form, inside its anonymous namespace); draw only, no logic.
//
// Under Medli / Makar control the HUD's magic gauge moves to the flyer (decomp dMeter_flyGaugeMove 02592EE8: for 50
// frames it projects the partner's eyePos through the frame's exact camera into the HD HUD block 1047B03C +0xC / +0x10,
// the gauge's blend toward it at +0x30; hdHudMagicBar_c::updatePlacement 02674EF0 sets the MagicBar_00 layout's root
// N_All_00 to its translation + (point - origin + spacing x remaining) x blend on the HD UI's calc, the frame's first
// step). dMeter_Execute is unconverted (the tick30 step, dt 1) and the HD UI calcs once a frame: the point is the
// projection of the flyer at one 30 Hz frame end through that frame end's camera, while the camera and the flyer are drawn
// at the world time W of every pass. On every pass the gauge's root is drawn with the point replaced by the projection of
// the flyer's eyePos at W (its kept states blended at W: per step for a converted flyer, between its frame ends else)
// through the camera this pass draws (mDoLib_projectScreen 025F0EA4 reads the HD 3D camera camera_draw left), i.e.
// x += blend (point(W) - point used), with point / blend as the pane form shows them (the two calcs' words blended at the
// pass's fraction like the panes). Only while the partner is the controlled player on both calcs and the point still
// changes between them (the 50-frame transfer; afterwards the gauge rests as in the original) and the gauge root follows the point on
// both calcs (placed(): not in the bar's In state or its normal placement). The pane's translation is
// put back byte for byte before any logic (layout_exact).
//
// NC: WWHD_T60_FLYGAUGE_NEGCTRL=1 (drawn as before). debug: WWHD_T60_FLYGAUGE_LOG=path, one line per pass.
#pragma once

// (interp::t60_camera_world_time and wwhd_t60_proc_converted: declared by t60_ui.cpp before the include)

namespace gauge {
const bool g_nc = [] {
    const char* e = getenv("WWHD_T60_FLYGAUGE_NEGCTRL");
    return e && atoi(e) != 0;
}();
FILE* const g_log = getenv("WWHD_T60_FLYGAUGE_LOG") ? fopen(getenv("WWHD_T60_FLYGAUGE_LOG"), "w") : nullptr;
const uint32_t kPlay = GD(0x1046F0B0);                // g_dComIfG_gameInfo.play
const uint32_t kPlayer0 = GI(0x5B2C), kPartner = GI(0x5B38);  // mPlayerInfo[0].mpPlayer, mpPlayerPtr[1]
constexpr uint32_t kEyePos = 0x37C;                    // fopAc_ac_c::eyePos
const uint32_t kHudBlock = GD(0x1047B03Cu);            // sHeartTimers / sMagicPlacement (d_meter_00 / hd_screen_magic_bar)
constexpr uint32_t kPointX = 0xC, kPointY = 0x10, kBlend = 0x30;
const uint32_t kProjectScreen = GC(0x025F0EA4u);        // mDoLib_projectScreen(cXyz* src, cXyz* dst)
constexpr uint32_t kProfName = 0xE, kProcId = 0x4;

uint32_t partner() {
    const uint32_t p = ld32(kPlay + kPartner);
    return p && p == ld32(kPlay + kPlayer0) ? p : 0;
}
// the words a calc used (read on the frame's first step, where the HD UI calc ran; dMeter writes them on tick30 only)
struct Calc {
    uint64_t id = ~0ull;
    uint32_t flyer = 0;
    float x = 0, y = 0, b = 0;
    bool has_tr = false, special = false;  // the gauge root's translation the calc left (tx / ty); special: see placed()
    float tx = 0, ty = 0;
};
Calc g_calc_prev, g_calc_cur;
// the flyer's eyePos per kept state, stamped with the time of the state (see note_step)
struct Eye { double t; float p[3]; };
std::vector<Eye> g_eye;
uint32_t g_eye_proc = 0, g_eye_id = 0;
float g_chg_tick = 0.0f, g_chg_early = 0.0f;  // (recent eye changes on tick30 / earlier steps, decaying)
// the shown pane and its translation as the logic left it (put back by restore())
uint32_t g_pane = 0, g_pane_layout = 0;
uint8_t g_saved[12];
bool g_applied = false;

bool name_is(uint32_t pane, const char* s) {
    for (uint32_t j = 0;; j++) {
        const uint8_t ch = ld8(pane + 0x80 + j);
        if (ch != (uint8_t)s[j]) return false;
        if (!ch) return true;
        if (j > 24) return false;
    }
}
// N_All_00 of the MagicBar_00 part (the HUD layout holds several parts, each with a root N_All_00): the last N_All_00
// before P_MagicBase_00 in the pane form's live snapshot (depth first: the part's root comes before its panes)
uint32_t find_pane(const PaneFrame& f) {
    for (const LayoutSnap& l : f.layouts) {
        uint32_t all = 0;
        for (const PaneSnap& s : l.panes) {
            if (name_is(s.pane, "N_All_00")) all = s.pane;
            else if (name_is(s.pane, "P_MagicBase_00")) {
                if (all) return all;
                break;
            }
        }
    }
    return 0;
}

void note_step() {  // every pass of a step: the calc words (first step) and the flyer's state
    const uint32_t fl = partner();
    if (g_cur.id != g_calc_cur.id && t60::first_step()) {
        g_calc_prev = g_calc_cur;
        g_calc_cur.id = g_cur.id;
        g_calc_cur.flyer = fl;
        g_calc_cur.has_tr = g_calc_cur.special = false;
        g_calc_cur.x = ldf(kHudBlock + kPointX), g_calc_cur.y = ldf(kHudBlock + kPointY), g_calc_cur.b = ldf(kHudBlock + kBlend);
    }
    if (!fl) { g_eye.clear(); g_eye_proc = 0; return; }
    const uint32_t id = ld32(fl + kProcId);
    if (fl != g_eye_proc || id != g_eye_id) g_eye.clear(), g_eye_proc = fl, g_eye_id = id, g_chg_tick = g_chg_early = 0.0f;
    const double st = t60::step_time();
    Eye e{st, {ldf(fl + kEyePos), ldf(fl + kEyePos + 4), ldf(fl + kEyePos + 8)}};
    // which steps change the flyer's eye (its execute: every step, the tick30 step, or the frame work on the frame's
    // first step), from its changes over the last frames; a state is stamped with the time it belongs to
    const bool changed = g_eye.empty() || std::memcmp(g_eye.back().p, e.p, sizeof e.p) != 0;
    const bool tick = t60::tick30();
    g_chg_tick = g_chg_tick * 0.9f + (changed && tick ? 1.0f : 0.0f);
    g_chg_early = g_chg_early * 0.9f + (changed && !tick ? 1.0f : 0.0f);
    if (g_chg_early > 0.5f && g_chg_tick < 0.05f) {  // (the frame's first step: its state is the frame's end)
        if (tick && !changed) return;
        e.t = std::ceil(st - 1e-6);
    } else if (g_chg_tick > 0.5f && g_chg_early < 0.05f) {  // (the tick30 step: its earlier steps show the last end)
        if (!tick) return;
    }
    if (!g_eye.empty() && std::fabs(g_eye.back().t - e.t) < 1e-6) g_eye.back() = e;
    else {
        if (!g_eye.empty() && g_eye.back().t > e.t) g_eye.clear();
        g_eye.push_back(e);
    }
    while (g_eye.size() > 16) g_eye.erase(g_eye.begin());
}
bool eye_at(double w, float* p) {
    const Eye* a = nullptr;
    const Eye* b = nullptr;
    for (const Eye& e : g_eye) {
        if (e.t <= w + 1e-6) a = &e;
        if (!b && e.t >= w - 1e-6) b = &e;
    }
    if (!a || !b) return false;
    const double f = b->t - a->t > 1e-6 ? (w - a->t) / (b->t - a->t) : 0.0;
    for (int i = 0; i < 3; i++) p[i] = (float)(a->p[i] + f * (b->p[i] - a->p[i]));
    return true;
}
bool project(Cpu* c, const float* p, float* out) {
    const Cpu saved = *c;
    const uint32_t sp = c->r[1];
    c->r[1] = (sp - 0x100u) & ~0xFu;
    const uint32_t src = c->r[1] + 0x10u, dst = c->r[1] + 0x20u;
    for (int i = 0; i < 3; i++) stf(src + 4 * i, p[i]), stf(dst + 4 * i, 0.0f);
    guest_call(c, kProjectScreen, {src, dst});
    out[0] = ldf(dst), out[1] = ldf(dst + 4);
    *c = saved;
    return out[0] == out[0] && out[1] == out[1];
}
// the gauge root's translation in a pane form snapshot (the calc's logic value)
bool snap_tr(const PaneFrame& f, uint32_t pane, float* tr) {
    for (const LayoutSnap& l : f.layouts)
        for (const PaneSnap& p : l.panes)
            if (p.pane == pane) {
                tr[0] = sf(p.state, 0), tr[1] = sf(p.state, 4);
                return true;
            }
    return false;
}
// does this calc's gauge root follow the point (updatePlacement's special placement: translation = T + (point - origin +
// spacing x remaining) x blend)? Told from two calcs with the same blend: the translation moved by blend x the point's
// move (the bar's In state, or the normal placement, does not follow it). Kept while the blend changes (the move back)
bool placed(const Calc& p, const Calc& q) {
    if (!q.flyer || q.flyer != p.flyer || !p.has_tr || !q.has_tr || !(q.b > 0.0f)) return false;
    if (bits(p.b) != bits(q.b)) return p.special;
    const float dx = q.x - p.x, dy = q.y - p.y;
    if (dx == 0.0f && dy == 0.0f) return p.special;
    return std::fabs((q.tx - p.tx) - q.b * dx) < 0.05f && std::fabs((q.ty - p.ty) - q.b * dy) < 0.05f;
}
// the shown gauge root at the pass's fraction t of the frame (1: the tick30 pass, the live calc's panes)
void present(Cpu* c, float t, const PaneFrame& live) {
    const double w = interp::t60_camera_world_time();  // (the camera's draw time on this pass: the actor draws')
    float pw[3] = {0, 0, 0}, s[2] = {0, 0};
    bool on = (!g_nc || g_log) && g_calc_cur.id == g_cur.id && g_calc_prev.id != ~0ull && g_calc_cur.id - g_calc_prev.id == wwhd_t60_steps &&
              g_calc_cur.flyer && g_calc_cur.flyer == g_calc_prev.flyer;
    uint32_t pane = 0;
    if (on) {
        pane = g_pane && g_pane_layout == g_cur.id ? g_pane : find_pane(live);
        on = pane != 0;
    }
    if (on) {
        g_pane = pane, g_pane_layout = g_cur.id;
        if (!g_calc_cur.has_tr) {
            float tr[2];
            if (snap_tr(live, pane, tr)) {
                g_calc_cur.tx = tr[0], g_calc_cur.ty = tr[1], g_calc_cur.has_tr = true;
                g_calc_cur.special = placed(g_calc_prev, g_calc_cur);
            }
        }
        on = g_calc_cur.special && g_calc_prev.special &&
             (bits(g_calc_cur.x) != bits(g_calc_prev.x) || bits(g_calc_cur.y) != bits(g_calc_prev.y));
    }
    const float x = g_calc_prev.x + (g_calc_cur.x - g_calc_prev.x) * t, y = g_calc_prev.y + (g_calc_cur.y - g_calc_prev.y) * t;
    const float b = g_calc_prev.b + (g_calc_cur.b - g_calc_prev.b) * t;
    on = on && eye_at(w, pw) && project(c, pw, s);
    float tx0 = 0, ty0 = 0, tx = 0, ty = 0;
    if (on && g_nc) {  // (debug log of the NC: the shown values only)
        tx = tx0 = ldf(pane + 0x1C), ty = ty0 = ldf(pane + 0x20);
    } else if (on) {
        if (!g_applied)
            for (int i = 0; i < 12; i++) g_saved[i] = ld8(pane + 0x1C + i);
        tx0 = ldf(pane + 0x1C), ty0 = ldf(pane + 0x20);
        tx = tx0 + b * (s[0] - x), ty = ty0 + b * (s[1] - y);
        stf(pane + 0x1C, tx), stf(pane + 0x20, ty);
        st8(pane + 0x44, ld8(pane + 0x44) | 0x10);  // (matrix dirty)
        g_applied = true;
    }
    if (g_log)
        fprintf(g_log, "%.4f %.4f k %u on %d fl %08X pt %.3f %.3f b %.4f eye %.2f %.2f %.2f scr %.3f %.3f pane %.3f %.3f -> %.3f %.3f\n",
                t60::step_time(), w, wwhd_t60_stepk, on ? 1 : 0, g_calc_cur.flyer, x, y, b, pw[0], pw[1], pw[2], s[0], s[1], tx0, ty0, tx, ty);
}
void restore() {  // the logic's translation, byte for byte
    if (!g_applied) return;
    g_applied = false;
    for (int i = 0; i < 12; i++) st8(g_pane + 0x1C + i, g_saved[i]);
}
}  // namespace gauge
