// The in-game UI's draw-path logic on a general step length (t60_ui.h). Decomp build only.
#include "t60/t60_ui.h"

#include <cstdlib>

#include "t60/t60.h"
#include "t60/t60_audio.h"

// the decompiled HUD's converted UI systems (d/d_meter_t60.h, game build only; the runtime-only decomp build has no
// game units: nothing is converted there)
extern "C" __attribute__((weak)) uint32_t wwhd_t60_ui_converted(void) { return 0; }

namespace t60::ui {
namespace {
const bool g_switch = [] {
    const char* e = getenv("WWHD_T60_UI");
    return !e || atoi(e) != 0;
}();
}  // namespace

uint32_t converted_mask() { return g_switch ? wwhd_t60_ui_converted() : 0u; }
bool steps(uint32_t sys) { return (converted_mask() & sys) != 0 && active() && (sub_step() || tick30()); }
float step_enter() {
    const float prev = wwhd_t60_dt;
    wwhd_t60_dt = 1.0f / (float)wwhd_t60_steps;
    audio::ui_step(true);  // (its sound starts on the frame's earlier steps are the logic's: t60_audio.h SE)
    return prev;
}
void step_leave(float prev) {
    audio::ui_step(false);
    wwhd_t60_dt = prev;
}
}  // namespace t60::ui

// ---- the HD layouts' picture between 30 Hz frames (t60_ui.h layout_present / layout_exact) ----
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime.h"
#include "ppc.h"
#include "guest_addr.h"

extern "C" uint32_t wwhd_t60_step_counter;  // (true60.h: one per logic step, advanced after fapGm_Execute)

namespace {
struct LytRec {
    uint32_t animator, vtable;
    float before, speed, after;
    uint32_t type, flags;
};
struct LytMtx {
    uint32_t layout, force;
    uint8_t context[0xC8];  // nw::lyt::LayoutDrawContext (LayoutDrawContext.h WWHD_SIZE), copied at the calc's call
};
struct LytFrame {
    std::vector<LytMtx> mtx;
    uint64_t id = ~0ull;  // the step counter (wwhd_t60_step_counter) of the frame's first step whose calc recorded it
    std::vector<LytRec> recs;
    std::unordered_map<uint32_t, size_t> index;
    void clear() { recs.clear(); index.clear(); mtx.clear(); id = ~0ull; }
};
LytFrame g_cur, g_prev;
bool g_shown = false;  // the panes show an interpolated picture (layout_exact has work to do)
bool g_replaying = false;  // our own guest calls (apply / matrices) are running: the decomp's records are not the calc's
const uint32_t kUpdateMatrices = GC(0x0287FBFCu);  // nw::lyt::Layout::updateMatrices(context, force)
constexpr uint32_t kFrame = 0x0C, kVtbl = 0x14, kAnimate = 0x2C;  // nw::lyt::Animator mFrame, vtable, apply (LYT_animframe.h)
float ldf(uint32_t ea) { uint32_t v = ld32(ea); float f; memcpy(&f, &v, 4); return f; }
void stf(uint32_t ea, float f) { uint32_t v; memcpy(&v, &f, 4); st32(ea, v); }
uint32_t bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
bool on() { return (t60::ui::converted_mask() & t60::ui::kHdLayout) != 0; }
// apply one animator to its panes at frame v, then put its frame back to `after`
void apply_at(Cpu* c, const LytRec& r, float v) {
    const uint32_t vt = ld32(r.animator + kVtbl);
    if (vt != r.vtable || vt < 0x10000000u) return;  // (no longer the animator it was)
    stf(r.animator + kFrame, v);
    g_replaying = true;
    guest_call(c, ld32(vt + kAnimate), {r.animator});
    g_replaying = false;
    stf(r.animator + kFrame, r.after);
}
// the picture frame at fraction t (0 < t < 1) of the frame from prev's applied frame to r's applied frame
float picture(const LytRec& r, const LytRec* p, float t) {
    if (!p || bits(p->after) != bits(r.before)) return r.before;  // (started / set this frame: no previous picture)
    constexpr uint32_t kEnded = 1, kFwd = 2, kBwd = 4;
    constexpr uint32_t kLoop = 1;
    if (p->flags & kEnded) return r.before;
    float v = r.before - p->speed * (1.0f - t);  // (the previous update's speed: p->before + p->speed (1 - wraps) = r.before)
    if (p->flags & (kFwd | kBwd)) {
        if (p->type != kLoop) return r.before;  // (Mirror reflection: no picture across the turn)
        const float count = (p->flags & kFwd) ? p->before + p->speed - p->after : p->after - (p->before + p->speed);
        if (!(count > 0.0f)) return r.before;
        if (v < 0.0f) v += count;
        if (v >= count) v -= count;
    }
    return v;
}
}  // namespace

namespace {
void begin_frame() {
    const uint64_t first = wwhd_t60_step_counter;
    if (g_cur.id == first) return;
    // a new frame's first step: this frame's records become the previous frame's
    const bool next = g_cur.id != ~0ull && first - g_cur.id == wwhd_t60_steps;
    std::swap(g_prev, g_cur);
    if (!next) g_prev.clear();
    g_cur.clear();
    g_cur.id = first;
}
// redo the recorded pane matrix updates (a copy of each draw context on the guest stack below the caller's frame)
void redo_matrices(Cpu* c) {
    for (const LytMtx& m : g_cur.mtx) {
        const uint32_t sp = c->r[1];
        c->r[1] = (sp - 0x100u) & ~0xFu;
        const uint32_t buf = c->r[1] + 0x10u;
        for (uint32_t i = 0; i < sizeof m.context; i++) st8(buf + i, m.context[i]);
        g_replaying = true;
        guest_call(c, kUpdateMatrices, {m.layout, buf, m.force});
        g_replaying = false;
        c->r[1] = sp;
    }
}
}  // namespace

extern "C" __attribute__((visibility("default"))) void wwhd_t60_lyt_matrices(uint32_t layout, uint32_t context, uint32_t force) {
    if (g_replaying) return;
    begin_frame();
    for (LytMtx& m : g_cur.mtx)
        if (m.layout == layout) { m.force = force; for (uint32_t i = 0; i < sizeof m.context; i++) m.context[i] = ld8(context + i); return; }
    g_cur.mtx.push_back({layout, force, {}});
    for (uint32_t i = 0; i < sizeof g_cur.mtx.back().context; i++) g_cur.mtx.back().context[i] = ld8(context + i);
}

extern "C" __attribute__((visibility("default"))) void wwhd_t60_lyt_record(uint32_t animator, float before, float speed,
                                                                         float after, uint32_t type, uint32_t flags) {
    if (g_replaying) return;
    begin_frame();
    auto it = g_cur.index.find(animator);
    if (it != g_cur.index.end()) {  // (a second update in the same calc: keep the first start, the last end)
        LytRec& r = g_cur.recs[it->second];
        r.after = after, r.flags = flags, r.type = type;
        return;
    }
    g_cur.index[animator] = g_cur.recs.size();
    g_cur.recs.push_back({animator, ld32(animator + kVtbl), before, speed, after, type, flags});
}
extern "C" __attribute__((visibility("default"))) void wwhd_t60_lyt_forget(uint32_t animator) {
    for (LytFrame* f : {&g_cur, &g_prev}) {
        auto it = f->index.find(animator);
        if (it == f->index.end()) continue;
        f->recs[it->second].animator = 0;  // (kept in place: the record order is the apply order)
        f->index.erase(it);
    }
}

// ---- pane form: the panes the first-step calc sets by hand ----
// The HD screens' code writes pane values itself once a frame (magic bar placement, compass needle, map pan / zoom /
// rotation, ...). Every pane of the layouts whose matrices the calc updated (g_cur.mtx) is snapshotted: its state block
// (LayoutPane +0x1C..+0x78: translation, rotation, scale, size, flags, alpha, inherited alpha, matrix flag, matrix) and
// its materials' two colour words. At the tick30 step (A's frame end, after layout_exact) the snapshot is kept as the
// previous frame end; on a later frame's earlier step k the panes are shown at prev + (live - prev) k/N, only when both
// frame ends have the same pane set (addresses, types, visibility) per layout; a rotation that jumps more than 180 deg is
// not interpolated; text, animator frames and every other field are left alone (discrete). The recorded animators are
// then applied over it (their own bound fields win) and the matrices redone. layout_exact puts the live snapshot back
// byte for byte after the animators' exact apply: A's panes before any logic. WWHD_T60_UI=0: off (on()).
namespace {
constexpr uint32_t kPaneState = 0x1C, kPaneStateLen = 0x5C;  // LayoutPane: content +0x10 .. matrix end (PaneState.h)
constexpr uint32_t kPaneChildren = 0x14;                     // content()->children.root (the sentinel HeapLink)
constexpr uint32_t kPaneFlags = 0x44, kPaneAlpha = 0x45, kPaneVtbl = 0x08;
constexpr uint32_t kMaterialAt = 0x4C, kMaterialCount = 0x54;  // pane method table (PaneText.h PaneMethods)
constexpr uint32_t kLayoutRoot = 0x0C;                         // nw::lyt::Layout::mRoot (LYT_animframe.h)
constexpr uint32_t kMaxPanes = 4096, kMaxMaterials = 8;
uint32_t pmix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
bool pobj(uint32_t a, uint32_t size) { return a >= 0x10000000u && a + size < 0x50000000u; }
struct PaneSnap {
    uint32_t pane, vtbl;
    uint8_t state[kPaneStateLen];
    uint32_t nmat;
    uint32_t mat[kMaxMaterials], color[kMaxMaterials][2];
};
struct LayoutSnap {
    uint32_t layout;
    std::vector<PaneSnap> panes;
};
struct PaneFrame {
    uint64_t id = ~0ull;  // g_cur.id of the frame it was taken in
    std::vector<LayoutSnap> layouts;
    void clear() { layouts.clear(); id = ~0ull; }
};
PaneFrame g_pane_prev;  // the previous frame's end (tick30 step, A's panes)
PaneFrame g_pane_live;  // this step's logic values, put back by layout_exact
bool g_pane_shown = false;
uint32_t g_pane_moved = 0;  // (trace: panes shown between the frame ends on this step)
// (trace only: what show_pane wrote on this step, to tell the hand-set fields the animators' apply left standing)
struct PaneWrite { uint32_t pane, ea, value; uint8_t size; char kind; };
std::vector<PaneWrite> g_pane_writes;
bool g_pane_record = false;
void rec_write(uint32_t pane, uint32_t ea, uint32_t value, uint8_t size, char kind) {
    if (g_pane_record) g_pane_writes.push_back({pane, ea, value, size, kind});
}

void snap_pane(Cpu* c, uint32_t pane, PaneSnap& s) {
    s.pane = pane;
    s.vtbl = ld32(pane + kPaneVtbl);
    for (uint32_t i = 0; i < kPaneStateLen; i++) s.state[i] = ld8(pane + kPaneState + i);
    s.nmat = 0;
    if (s.vtbl < 0x10000000u) return;
    g_replaying = true;
    uint32_t n = guest_call(c, ld32(s.vtbl + kMaterialCount), {pane});
    if (n > kMaxMaterials) n = kMaxMaterials;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t m = guest_call(c, ld32(s.vtbl + kMaterialAt), {pane, i});
        if (!pobj(m, 0x3C)) continue;
        s.mat[s.nmat] = m, s.color[s.nmat][0] = ld32(m), s.color[s.nmat][1] = ld32(m + 4);
        s.nmat++;
    }
    g_replaying = false;
}
void snap_tree(Cpu* c, uint32_t pane, std::vector<PaneSnap>& out, uint32_t depth) {
    if (!pobj(pane, 0xA4) || out.size() >= kMaxPanes || depth > 64) return;
    out.emplace_back();
    snap_pane(c, pane, out.back());
    const uint32_t sentinel = pane + kPaneChildren;
    uint32_t child = ld32(sentinel);
    for (uint32_t guard = 0; child != sentinel && guard < kMaxPanes; guard++) {
        if (!pobj(child, 0xA4)) break;
        snap_tree(c, child, out, depth + 1);
        child = ld32(child);  // (HeapLink next at the pane's start)
    }
}
void snap_frame(Cpu* c, PaneFrame& f) {
    const Cpu saved = *c;
    f.layouts.clear();
    f.id = g_cur.id;
    for (const LytMtx& m : g_cur.mtx) {
        f.layouts.push_back({m.layout, {}});
        snap_tree(c, ld32(m.layout + kLayoutRoot), f.layouts.back().panes, 0);
    }
    *c = saved;
}
bool same_set(const LayoutSnap& a, const LayoutSnap& b) {
    if (a.layout != b.layout || a.panes.size() != b.panes.size()) return false;
    for (size_t i = 0; i < a.panes.size(); i++) {
        const PaneSnap &p = a.panes[i], &q = b.panes[i];
        if (p.pane != q.pane || p.vtbl != q.vtbl || p.nmat != q.nmat) return false;
        if ((p.state[kPaneFlags - kPaneState] ^ q.state[kPaneFlags - kPaneState]) & 1) return false;  // (visibility)
        for (uint32_t m = 0; m < p.nmat; m++) if (p.mat[m] != q.mat[m]) return false;
    }
    return true;
}
float sf(const uint8_t* s, uint32_t off) { uint32_t v = (uint32_t)s[off] << 24 | (uint32_t)s[off + 1] << 16 | (uint32_t)s[off + 2] << 8 | s[off + 3]; float f; memcpy(&f, &v, 4); return f; }
uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float x = (float)((a >> sh) & 0xFF), y = (float)((b >> sh) & 0xFF);
        r |= (uint32_t)(int)(x + (y - x) * t + 0.5f) << sh;
    }
    return r;
}
// show one pane between the previous frame end p and this step's logic value q at fraction t; true if it changed
bool show_pane(const PaneSnap& p, const PaneSnap& q, float t) {
    bool moved = false;
    for (uint32_t off = 0x1C; off < 0x44; off += 4) {  // translation[3], rotation[3], scale[2], width, height
        const float a = sf(p.state, off - kPaneState), b = sf(q.state, off - kPaneState);
        if (bits(a) == bits(b) || !(a == a) || !(b == b)) continue;
        if (off >= 0x28 && off < 0x34 && std::fabs(b - a) > 180.0f) continue;  // (rotation wrap / jump: no picture)
        stf(q.pane + off, a + (b - a) * t);
        rec_write(q.pane, q.pane + off, ld32(q.pane + off), 4, off < 0x28 ? 'T' : off < 0x34 ? 'R' : off < 0x3C ? 'S' : 'W');
        moved = true;
    }
    const uint8_t a = p.state[kPaneAlpha - kPaneState], b = q.state[kPaneAlpha - kPaneState];
    if (a != b) { st8(q.pane + kPaneAlpha, (uint8_t)(int)((float)a + ((float)b - (float)a) * t + 0.5f)); rec_write(q.pane, q.pane + kPaneAlpha, ld8(q.pane + kPaneAlpha), 1, 'A'); moved = true; }
    for (uint32_t m = 0; m < q.nmat; m++)
        for (int w = 0; w < 2; w++)
            if (p.color[m][w] != q.color[m][w]) { st32(q.mat[m] + 4 * w, lerp_color(p.color[m][w], q.color[m][w], t)); rec_write(q.pane, q.mat[m] + 4 * w, ld32(q.mat[m] + 4 * w), 4, 'C'); moved = true; }
    if (moved) st8(q.pane + kPaneFlags, ld8(q.pane + kPaneFlags) | 0x10);  // (matrix dirty: redo_matrices recomputes it)
    return moved;
}
void restore_panes() {
    for (const LayoutSnap& l : g_pane_live.layouts)
        for (const PaneSnap& s : l.panes) {
            for (uint32_t i = 0; i < kPaneStateLen; i++) st8(s.pane + kPaneState + i, s.state[i]);
            for (uint32_t m = 0; m < s.nmat; m++) st32(s.mat[m], s.color[m][0]), st32(s.mat[m] + 4, s.color[m][1]);
        }
}
uint32_t pane_hash(const PaneFrame& f, uint32_t* count) {
    uint32_t h = 2166136261u, n = 0;
    for (const LayoutSnap& l : f.layouts)
        for (const PaneSnap& s : l.panes) {
            n++;
            h = pmix(h, s.pane);
            for (uint32_t i = 0; i < kPaneStateLen; i++) h = pmix(h, s.state[i]);
            for (uint32_t m = 0; m < s.nmat; m++) h = pmix(pmix(h, s.color[m][0]), s.color[m][1]);
        }
    *count = n;
    return h;
}
}  // namespace

namespace interp { double t60_camera_world_time(); }  // (the flyer's gauge, interp.cpp)
extern "C" int wwhd_t60_proc_converted(int);  // (the flyer's gauge, f_pc_executor.cpp; weak default in t60_draw.cpp)
namespace {
#include "t60/t60_ui_gauge.h"  // (the flyer's magic gauge)
}  // namespace

namespace t60::ui {
void layout_exact(Cpu* c) {
    gauge::restore();  // (the flyer's gauge: before the pane form's own put-back)
    if (!g_shown) return;
    g_shown = false;
    const Cpu saved = *c;
    for (const LytRec& r : g_cur.recs)
        if (r.animator && bits(ldf(r.animator + kFrame)) == bits(r.after)) apply_at(c, r, r.before);
    redo_matrices(c);
    *c = saved;
    if (g_pane_shown) { g_pane_shown = false; restore_panes(); }  // (byte for byte: the logic's panes, A's)
}
// test aid WWHD_T60_LYT_TRACE=path: per 30 Hz frame (its 30 Hz counter mCounter0) the recorded layout animators' frames
// (a hash of every record's frame before / after) and a hash of the panes and materials they bind, as the frame ends
// (N 1: after the calc; N > 1: on the tick30 step after A's panes are back): "F ctr records frames_hash panes_hash";
// per earlier step "S ctr k n records shown moving" (moving: shown strictly between the previous and this frame's
// applied frame). Compare an N 1 run with an N run: the F lines must be equal.
namespace {
FILE* trace_file() {
    static FILE* f = getenv("WWHD_T60_LYT_TRACE") ? fopen(getenv("WWHD_T60_LYT_TRACE"), "w") : nullptr;
    return f;
}
uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
bool guest_obj(uint32_t a, uint32_t size) { return a >= 0x10000000u && a + size < 0x50000000u; }
void trace_frame() {
    FILE* f = trace_file();
    if (!f) return;
    uint32_t hf = 2166136261u, hp = 2166136261u, n = 0;
    for (const LytRec& r : g_cur.recs) {
        if (!r.animator || ld32(r.animator + kVtbl) != r.vtable) continue;
        n++;
        hf = mix(mix(hf, bits(r.before)), bits(r.after));
        const uint32_t bind = ld32(r.animator + 0x1C), count = ld16(r.animator + 0x20);  // (BoundAnimation bindings)
        if (!guest_obj(bind, count * 8u)) continue;
        for (uint32_t i = 0; i < count && i < 256; i++) {
            const uint32_t target = ld32(bind + 8 * i), track = ld32(bind + 8 * i + 4);
            if (!guest_obj(track, 0x20)) continue;
            const uint32_t size = ld8(track + 0x1D) ? 0x3Cu : 0xA4u;  // (LayoutMaterial / LayoutPane base)
            if (!guest_obj(target, size)) continue;
            for (uint32_t b = 0; b < size; b += 4) hp = mix(hp, ld32(target + b));
        }
    }
    fprintf(f, "F %u %u %08X %08X  (recs %zu id %llu ctr %u k %u n %u first %u)\n", ld32(GD(0x101FF558)), n, hf, hp,
            g_cur.recs.size(), (unsigned long long)g_cur.id, wwhd_t60_step_counter, wwhd_t60_stepk, wwhd_t60_steps, wwhd_t60_first);
}
uint64_t g_traced = ~0ull;
// "Q ctr panes hash": the frame end's hand-set pane state (every pane of the layouts the calc updated: state block and
// material colours; N 1 after the calc, N > 1 on the tick30 step after A's panes are back). A and B must be equal.
void trace_panes(const PaneFrame& q) {
    FILE* f = trace_file();
    if (!f) return;
    uint32_t count = 0;
    const uint32_t h = pane_hash(q, &count);
    fprintf(f, "Q %u %u %08X  (layouts %zu)\n", ld32(GD(0x101FF558)), count, h, q.layouts.size());
    // "W ctr layout pane name tx ty rz sx sy w h alpha": the frame end's values of the watched panes (the hand-set panes
    // of the HUD placement checks: compass needle / clock hand, magic bar, sea chart Link marker; more names in WWHD_T60_LYT_TRACE_NAMES,
    // comma separated), so a run shows whether they are in the snapshot and whether they change between frame ends
    static const std::string names = std::string(",P_Compass_00,P_Clock_00,P_MagicBase_00,P_MagicBase_01,L_LinkPos_00,N_Rotate_00,") +
        (getenv("WWHD_T60_LYT_TRACE_NAMES") ? getenv("WWHD_T60_LYT_TRACE_NAMES") : "") + ",";
    for (const LayoutSnap& l : q.layouts)
        for (const PaneSnap& s : l.panes) {
            char name[26] = {};
            for (uint32_t j = 0; j < 25; j++) { const uint8_t ch = ld8(s.pane + 0x80 + j); if (!ch || ch <= ' ' || ch > '~') break; name[j] = ch; }
            if (!name[0] || names.find("," + std::string(name) + ",") == std::string::npos) continue;
            fprintf(f, "W %u %08X %08X %s %g %g %g %g %g %g %g %u\n", ld32(GD(0x101FF558)), l.layout, s.pane, name, sf(s.state, 0),
                    sf(s.state, 4), sf(s.state, 0x14), sf(s.state, 0x18), sf(s.state, 0x1C), sf(s.state, 0x20), sf(s.state, 0x24),
                    s.state[kPaneAlpha - kPaneState]);
        }
}
// "H ctr k n pane name fields" per earlier step: a pane the pane form showed between the frame ends whose lerped field(s)
// the animators' apply left standing (moved by the pane form, not by an animator): T translation, R rotation, S scale,
// W size, A alpha, C material colour (one letter per field word). Returns the number of such panes (S line field 8).
uint32_t trace_hand(uint32_t k, uint32_t n) {
    FILE* f = trace_file();
    g_pane_record = false;
    if (!f) return 0;
    uint32_t panes = 0;
    for (size_t i = 0; i < g_pane_writes.size();) {
        const uint32_t pane = g_pane_writes[i].pane;
        char fields[64];
        size_t nf = 0;
        for (; i < g_pane_writes.size() && g_pane_writes[i].pane == pane; i++) {
            const PaneWrite& w = g_pane_writes[i];
            const uint32_t now = w.size == 1 ? ld8(w.ea) : ld32(w.ea);
            if (now == w.value && nf + 1 < sizeof fields) fields[nf++] = w.kind;
        }
        fields[nf] = 0;
        if (!nf) continue;
        char name[26];
        for (uint32_t j = 0; j < 25; j++) {
            const uint8_t ch = ld8(pane + 0x80 + j);  // (PaneContent name at pane +0xC +0x74)
            name[j] = ch;
            if (!ch) break;
            if (ch <= ' ' || ch > '~') name[j] = '?';
        }
        name[25] = 0;
        fprintf(f, "H %u %u %u %08X %s %s\n", ld32(GD(0x101FF558)), k, n, pane, name[0] ? name : "-", fields);
        panes++;
    }
    g_pane_writes.clear();
    return panes;
}
}  // namespace

void layout_present(Cpu* c) {
    const bool calc_frame = g_cur.id != ~0ull && (uint64_t)wwhd_t60_step_counter - g_cur.id < wwhd_t60_steps;
    if (!on() || !active() || wwhd_t60_steps < 2) {
        layout_exact(c);
        g_pane_prev.clear();
        if (calc_frame && g_traced != g_cur.id && (wwhd_t60_steps < 2 || tick30())) {
            g_traced = g_cur.id;
            trace_frame();
            if (trace_file()) { PaneFrame q; snap_frame(c, q); trace_panes(q); }
        }
        return;
    }
    if (!(first_step() || sub_step() || tick30())) return;  // (a plain interpolation pass keeps the step's picture)
    if (g_cur.id == ~0ull || (uint64_t)wwhd_t60_step_counter - g_cur.id >= wwhd_t60_steps) { layout_exact(c); g_pane_prev.clear(); return; }  // (no calc this frame)
    const uint32_t k = wwhd_t60_stepk, n = wwhd_t60_steps;
    gauge::note_step();  // (the flyer's gauge)
    if (k >= n) {  // the tick30 step: A's panes; they are the next frame's previous frame end
        layout_exact(c);
        snap_frame(c, g_pane_prev);
        if (g_traced != g_cur.id) { g_traced = g_cur.id; trace_frame(); trace_panes(g_pane_prev); }
        {  // (the flyer's gauge: the gauge's root at this pass's camera; put back by layout_exact)
            const Cpu saved = *c;
            gauge::present(c, 1.0f, g_pane_prev);
            if (gauge::g_applied) redo_matrices(c);
            *c = saved;
        }
        return;
    }
    uint32_t shown = 0, moving = 0;
    const float t = (float)k / (float)n;
    const Cpu saved = *c;
    // the hand-set panes first (the animators' own bound fields are applied over them below)
    snap_frame(c, g_pane_live);
    g_pane_shown = true;
    g_pane_moved = 0;
    g_pane_writes.clear();
    g_pane_record = trace_file() != nullptr;
    if (g_pane_prev.id != ~0ull && g_cur.id - g_pane_prev.id == n)
        for (const LayoutSnap& l : g_pane_live.layouts)
            for (const LayoutSnap& pl : g_pane_prev.layouts) {
                if (pl.layout != l.layout) continue;
                if (same_set(pl, l))
                    for (size_t i = 0; i < l.panes.size(); i++) g_pane_moved += show_pane(pl.panes[i], l.panes[i], t) ? 1 : 0;
                break;
            }
    for (const LytRec& r : g_cur.recs) {
        if (!r.animator || bits(ldf(r.animator + kFrame)) != bits(r.after)) continue;  // (gone or set again since)
        auto it = g_prev.index.find(r.animator);
        const LytRec* p = it == g_prev.index.end() ? nullptr : &g_prev.recs[it->second];
        const float v = picture(r, p, t);
        apply_at(c, r, v);
        shown++;
        if (p && bits(v) != bits(r.before)) moving++;
    }
    const uint32_t hand = trace_hand(k, n);
    gauge::present(c, t, g_pane_live);  // (the flyer's gauge)
    redo_matrices(c);
    *c = saved;
    g_shown = true;
    if (FILE* f = trace_file()) fprintf(f, "S %u %u %u %u %u %u %u %u  (id %llu ctr %u prev %zu prev_id %llu)\n", ld32(GD(0x101FF558)), k, n, (uint32_t)g_cur.recs.size(), shown, moving, g_pane_moved, hand,
                                     (unsigned long long)g_cur.id, wwhd_t60_step_counter, g_prev.recs.size(), (unsigned long long)g_prev.id);
}
}  // namespace t60::ui
