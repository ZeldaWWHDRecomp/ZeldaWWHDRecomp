// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op: Link's pose and the puppets (coop_pose.h).
//
// Link's models: daPy_lk_c::createHeap (023D5548) makes them all with mDoExt_J3DModel__create (025E38E0). Each puppet
// gets its own copy of every one of them (its kit) the first time it is shown, on a heap of its own over memory the port
// gives (the puppet pool, GAPS C9/C11: not on Link's heap nor any of the game's, nothing made while nobody is shown, the
// number of puppets not bound by the game's memory); a kit is given back as Link is made again and as its player leaves. Link's draw
// (023D9820) enters them through entryDLSetLight (023D8DF0) / updateDLSetLight (023D8454): there, once a logic step,
// each model's world joint matrices (model +0x2C -> block +0x10, count block +0x2C) are taken relative to Link's place
// and heading. Right after each of his models is entered, each puppet's copy of it gets its matrices from the puppet's
// pose at its own place and is entered the same way (Link as the light's owner), the ones he did not draw after his
// draw: his sword and shield, and no calc (Link's joint callbacks take any model they run on for his). What he holds
// (his drawn sword, an item) is made as he takes it out (daPy_lk_c::initModel, 023D4BB8, a file of Link's archive): a
// pose names its file, and each puppet gets its own copy of that file's model the first time it holds it, on a heap
// of its own, given back as Link is made again.
#include "coop_pose.h"
#include "coop_emotes.h"
#include "coop_items.h"
#include "coop_names.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>

#include "coop_sync.h"
#include "link_masks.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"
#include "../savestate.h"
#include "../j3d_draw_guard.h"

namespace interp {
bool mtx_to_quat(const float* m, float* s, float* q);
void blend_joint(const float* a, const float* b, float* out, float t);
}

namespace mods::coop {
namespace {

using namespace names;  // (C18: the offsets by name, coop_names.h)
constexpr uint32_t kPos = ac::kPos, kRoom = ac::kRoom, kAngleY = ac::kAngleY, kCurProc = py::mCurProc, kEquip = py::mEquipItem;
constexpr uint8_t kFlying = 2, kHurt = 4;  // a pose's flags: Medli flying; Link flashing red (hurt)
constexpr uint16_t kShipFile = 0x8000;      // a held model's file is of the boat's archive, not Link's
constexpr uint32_t kMdlJoints = j3d::mpMtxBuffer, kJntMtx = j3d::mpAnmMtx, kJntNum = j3d::kJointCount, kMdlBase = j3d::mBaseTransformMtx;
constexpr int kMaxModels = 64, kMaxJoints = 128;
constexpr uint32_t kSwBlur = py::mpSwBlur, kTrailPts = 2 * 60 * 3;  // Link's sword trail; its points mTopPos, mBottomPos
const uint32_t kPlay = GD(0x1046F0B0);
const uint32_t kStage = kPlay + inf::kCurStage;

struct LinkModel { uint32_t model, data, flags, diff; uint32_t twin[kPuppets]; };
std::vector<LinkModel> g_models;  // Link's, in the order createHeap made them
struct Made { uint16_t file; uint32_t diff; };
std::map<uint32_t, Made> g_link_files;  // Link's models by initModel: their files
struct Held { uint16_t file; uint32_t model, heap; };
std::vector<Held> g_held[kPuppets];  // each puppet's copies of what its player held
std::unordered_map<uint32_t, int> g_wearer;  // Link's models (-1) and the puppets' copies (their slot): whose tunic
// each puppet's kit: its copies of Link's models (LinkModel::twin) on its pool heap (make_kit); tried once until Link is
// made again or it is let go (a game heap too full for it is not tried every step)
// A kit stays on its block once made and is shown again by the next player in that slot: its models are never
// destroyed, because the game's delete finds the heap of what it frees in its own heap tree (02755FEC from the root,
// 0x101F8B4C), where the pool's heaps are not (a destroyed kit crashed on a null heap). Kept across Link being made
// again while his models' data (his archive's, resident) are the same; data that changed leave the old kit unused.
// What a puppet holds and its rope are on the game heap as before, given back when it is let go (once not entered
// for drawing for a few steps: a model still entered in a draw buffer asserts as it is destroyed, J3D 027F13DC).
struct Kit {
    uint32_t heap = 0;
    bool tried = false, release = false;
    int hidden = 0;
    std::vector<std::pair<uint32_t, uint32_t>> models;  // (model data, its copy), in Link's creation order
} g_kit[kPuppets];
bool g_in_heap = false;
int g_body_k = -1;  // Link's body (mpCLModel) among his models

bool g_want = false;  // take a pose at Link's next draw
bool g_in_draw = false;
// Link's items (coop_items.h): their models as they draw them (fopAc_Draw), by their file of Link's archive
// (dComIfG_getObjectRes) and differed-list flags (mDoExt_J3DModel__create), into his next pose
uint32_t g_drawing = 0;
// the hookshot's chain and the grappling hook's rope as they drew (daHookshot_c, daHimo2), into his next pose
constexpr int16_t kHookshot = 0xA9, kHimo2 = 0x1BE;
constexpr uint32_t kRoot = py::mHookshotRootPos, kChainN = hookshot::mChainCnt, kChainAngle = ac::shape_angle,
                   kRopeLines = himo2::m1F30 + linemat::mpLines, kRopeN = himo2::m1F6C;
Pose g_lines;
uint32_t g_hook[kPuppets];  // each puppet's own hookshot (its process id), drawn as its player's chain
struct Rope { uint32_t mat = 0, heap = 0; } g_rope[kPuppets];  // each puppet's own rope line (mDoExt_3DlineMat1_c, 1
// line of 200 points) and its heap; mat 1: not made
std::unordered_map<uint32_t, uint16_t> g_link_res;  // model data: its file
std::unordered_map<uint32_t, uint32_t> g_diff_of;   // model data: the flags it was made with
std::vector<Model> g_item_models;
bool g_have = false;
Pose g_pose, g_taking;

struct Puppet {
    bool shown = false;
    bool see = false;  // drawn see-through (a player outside the party, the player's choice: GAPS X1b)
    int tunic = 0;
    Pose pose;
    float pos[3] = {};
    int16_t yaw = 0;
} g_puppet[kPuppets];

float f32(uint32_t a) { return (float)ldf32(a); }

// A see-through puppet's entries (X1b): into the translucent draw list (the current lists 104B4634 / 104B4638: opaque,
// translucent; both the translucent one around its entries, so it is drawn after the opaque world, sorted), and each
// packet entered marked (j3d_draw_guard): drawn at kSeeAlpha whatever blend its material sets
constexpr float kSeeAlpha = 0.5f;
struct SeeThrough {
    bool on;
    uint32_t was[2];
    explicit SeeThrough(int i) : on(i >= 0 && i < kPuppets && g_puppet[i].see) {
        if (!on) return;
        const uint32_t lists = GD(0x104B4634);
        was[0] = ld32(lists), was[1] = ld32(lists + 4);
        st32(lists, was[1]);
        float a = kSeeAlpha;
        uint32_t b;
        memcpy(&b, &a, 4);
        j3d_draw_guard::set_marking(b);
    }
    ~SeeThrough() {
        if (!on) return;
        const uint32_t lists = GD(0x104B4634);
        st32(lists, was[0]), st32(lists + 4, was[1]);
        j3d_draw_guard::set_marking(0);
    }
};
uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// a material's shown flag: the model data's (shared by Link's model and its copies), Link's draw sets it as he is
uint32_t shown_flag(uint32_t model, uint32_t i) {
    return ld32(ld32(ld32(model + j3d::mModelData) + j3d::mMaterials) + i * j3d::kMaterial + j3d::mShape) + j3d::mVisible;
}
uint32_t materials(uint32_t model) { return std::min<uint32_t>(ld32(ld32(model + j3d::mModelData) + j3d::mMaterialNum), 32); }
// a joint's materials (j3d::kJointMaterial, then each material's kMaterialNext) of `shown` (a material's index: its
// place in the model data's mMaterials) hidden around `entry`
template <typename F> void joint_without(uint32_t model, uint32_t joint, uint32_t shown, F entry) {
    const uint32_t mats = ld32(ld32(model + j3d::mModelData) + j3d::mMaterials);
    uint32_t off = 0;
    uint8_t was[32];
    for (uint32_t m = ld32(joint + j3d::kJointMaterial); m; m = ld32(m + j3d::kMaterialNext))
        if (const uint32_t i = (m - mats) / j3d::kMaterial; i < 32 && shown >> i & 1) was[i] = ld8(shown_flag(model, i)), st8(shown_flag(model, i), 0), off |= 1u << i;
    entry();
    for (uint32_t i = 0; i < 32; i++) if (off >> i & 1) st8(shown_flag(model, i), was[i]);
}

void rot_y(int16_t yaw, float& s, float& c) {
    const float a = yaw * (float)M_PI / 32768;
    s = std::sin(a), c = std::cos(a);
}

// a model's joints, relative to Link's place and heading (inverse of T(pos) * Ry(yaw), then the world matrix), into
// `into` (none if a joint is further than a pose holds: an arrow flown off)
void take(std::vector<Model>& into, int k, const Made* held, uint32_t model, uint32_t link) {
    const uint32_t jnt = ld32(model + kMdlJoints), mtx = jnt ? ld32(jnt + kJntMtx) : 0, n = jnt ? ld16(jnt + kJntNum) : 0;
    if (!mtx || !n || n > kMaxJoints) return;
    for (const Model& m : into) if (held ? m.k == kHeld && m.file == held->file : m.k == k) return;  // (entered twice in one draw)
    float s, c;
    rot_y((int16_t)ld16(link + kAngleY), s, c);
    const float p[3] = {f32(link + kPos), f32(link + kPos + 4), f32(link + kPos + 8)};
    Model out;
    out.k = held ? kHeld : (uint8_t)k;
    if (held) out.file = held->file, out.diff = held->diff;
    for (uint32_t i = 0; i < materials(model); i++) if (!ld8(shown_flag(model, i))) out.shown &= ~(1u << i);
    // (in first person Link hides his head: the others see it as it was before)
    static auto shown_before = [] { std::array<uint32_t, kMaxModels> a; a.fill(~0u); return a; }();
    if (k >= 0 && k < kMaxModels) mods::first_person() ? (void)(out.shown = shown_before[k]) : (void)(shown_before[k] = out.shown);
    out.j.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        float w[12], r[12];
        for (int e = 0; e < 12; e++) w[e] = f32(mtx + 48 * i + 4 * e);
        // R^-1 = [Ry^T | -Ry^T p]; Ry = [[c,0,s],[0,1,0],[-s,0,c]]
        for (int col = 0; col < 4; col++) {
            const float x = w[col] - (col == 3 ? p[0] : 0), y = w[4 + col] - (col == 3 ? p[1] : 0), z = w[8 + col] - (col == 3 ? p[2] : 0);
            r[col] = c * x - s * z, r[4 + col] = y, r[8 + col] = s * x + c * z;
        }
        float sc[3], q[4] = {1, 0, 0, 0};
        Joint& j = out.j[i];
        const bool ok = interp::mtx_to_quat(r, sc, q);
        const float us = ok ? (sc[0] + sc[1] + sc[2]) / 3 : 0;
        for (int e = 0; e < 4; e++) j.q[e] = (int16_t)std::lround(std::clamp(q[e], -1.f, 1.f) * 32767);
        for (int e = 0; e < 3; e++) {
            if (std::fabs(r[4 * e + 3] * 8) > 32767) return;
            j.t[e] = (int16_t)std::lround(r[4 * e + 3] * 8);
        }
        j.s = (uint8_t)std::clamp<long>(std::lround(us * 64), 0, 255);
    }
    into.push_back(std::move(out));
}

// a point relative to Link's place and heading (x4), and back at a place and heading
bool to_pose(uint32_t link, uint32_t w, int16_t* out) {
    float s, c;
    rot_y((int16_t)ld16(link + kAngleY), s, c);
    const float d[3] = {f32(w) - f32(link + kPos), f32(w + 4) - f32(link + kPos + 4), f32(w + 8) - f32(link + kPos + 8)};
    const float l[3] = {c * d[0] - s * d[2], d[1], s * d[0] + c * d[2]};
    bool in = true;
    for (int e = 0; e < 3; e++) in &= std::fabs(l[e] * 4) < 32767, out[e] = (int16_t)std::clamp<long>(std::lround(l[e] * 4), -32768, 32767);
    return in;
}
void from_pose(const int16_t* in, const float p[3], int16_t yaw, uint32_t w) {
    float s, c;
    rot_y(yaw, s, c);
    const float l[3] = {in[0] / 4.f, in[1] / 4.f, in[2] / 4.f};
    st32(w, bits(p[0] + c * l[0] + s * l[2])), st32(w + 4, bits(p[1] + l[1])), st32(w + 8, bits(p[2] - s * l[0] + c * l[2]));
}

// a joint back to a world matrix at a place and heading
void put(const Joint& j, const float p[3], float s, float c, uint32_t at) {
    float w = j.q[0] / 32767.f, x = j.q[1] / 32767.f, y = j.q[2] / 32767.f, z = j.q[3] / 32767.f;
    const float n = 1 / std::sqrt(std::max(1e-9f, w * w + x * x + y * y + z * z));
    w *= n, x *= n, y *= n, z *= n;
    const float sc = j.s / 64.f;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    float l[12];
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) l[4 * i + k] = r[i][k] * sc;
        l[4 * i + 3] = j.t[i] / 8.f;
    }
    // world = T(p) * Ry * local
    for (int col = 0; col < 4; col++) {
        const float x0 = l[col], y0 = l[4 + col], z0 = l[8 + col];
        st32(at + 4 * col, bits(c * x0 + s * z0 + (col == 3 ? p[0] : 0)));
        st32(at + 16 + 4 * col, bits(y0 + (col == 3 ? p[1] : 0)));
        st32(at + 32 + 4 * col, bits(-s * x0 + c * z0 + (col == 3 ? p[2] : 0)));
    }
}

int index_of(uint32_t model) {
    for (size_t k = 0; k < g_models.size(); k++)
        if (g_models[k].model == model) return (int)k;
    return -1;
}

// the test aids
FILE* g_rec = nullptr;
std::vector<Pose> g_ghost;
size_t g_ghost_at = 0;
float g_ghost_off[3] = {150, 0, 0};

void load_ghost(const char* spec) {
    std::string path = spec;
    if (const size_t at = path.find('@'); at != std::string::npos) {
        sscanf(path.c_str() + at + 1, "%f,%f", &g_ghost_off[0], &g_ghost_off[2]);
        path.resize(at);
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { trace("coop: ghost %s not found", path.c_str()); return; }
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
    fclose(f);
    for (size_t o = 0; o < d.size();) {
        Pose p;
        size_t used = 0;
        if (!unpack(d.data() + o, d.size() - o, p, &used)) break;
        g_ghost.push_back(std::move(p)), o += used;
    }
    trace("coop: ghost %s, %zu steps, offset %.0f %.0f", path.c_str(), g_ghost.size(), g_ghost_off[0], g_ghost_off[2]);
}

// a solid heap's size (JKRSolidHeap's virtual getHeapSize)
uint32_t heap_size(Cpu* c, uint32_t heap) {
    const uint32_t r3 = c->r[3];
    const uint32_t n = guest_call(c, ld32(ld32(heap + sheap::kVtable) + sheap::kGetHeapSize), {heap});
    c->r[3] = r3;
    return n;
}
// a solid heap from the game heap for a few models, only with this much free (else 0: see make_kit)
uint32_t heap_for_models(Cpu* c, uint32_t need) {
    const uint32_t heap = guest_call(c, GC(0x025E3528), {0, 0});
    if (heap && heap_size(c, heap) < need) return guest_call(c, GC(0x025E3868), {heap}), 0;
    return heap;
}

// the heap's free bytes: its head to its tail, as its set-up places them
uint32_t heap_free(uint32_t heap) { return ld32(heap + sheap::kTail) - ld32(heap + sheap::kHead); }
// puppet i's copy of a held model: made from its file of Link's archive (dComIfG_getObjectRes, 026066C4, as initModel
// asks; the boat's archive for kShipFile) the first time, on a solid heap of its own from the game heap (a file that
// makes none is not tried again)
void make_held(Cpu* c, int i, const Model& m) {
    for (const Held& h : g_held[i]) if (h.file == m.file) return;
    static const uint32_t arc = [] {  // "Link", as initModel passes it (the text, then the string class's vtable); "Ship"
        const uint32_t b = mem::host_alloc(16);
        st32(b, GD(0x101CEB48)), st32(b + 4, GD(0x10034B24)), st32(b + 8, GD(0x101D034C)), st32(b + 12, GD(0x10034B24));
        return b;
    }();
    const uint32_t data = guest_call(c, GC(0x026066C4), {ld32(GD(0x101F4F28)), arc + (m.file & kShipFile ? 8u : 0u), (uint32_t)(m.file & ~kShipFile)});
    // (on the game heap, given back as Link is made again or the puppet is let go: a held file may be of an archive
    // that is not resident, the boat's)
    uint32_t heap = data ? heap_for_models(c, 0x40000) : 0, model = 0;  // mDoExt_createSolidHeapFromGame, 256 KB free
    if (heap) {
        const uint32_t was = guest_call(c, GC(0x027EC220), {heap});  // JKRHeap::becomeCurrentHeap
        model = guest_call(c, GC(0x025E38E0), {data, 0x80000, m.diff});
        guest_call(c, GC(0x027EC220), {was});
        guest_call(c, GC(0x025E3678), {heap});  // mDoExt_adjustSolidHeap
        if (!model) guest_call(c, GC(0x025E3868), {heap}), heap = 0;
    }
    g_held[i].push_back({m.file, model, heap});
    if (model) g_wearer[model] = i;
    trace("coop: puppet %d holds file %u: %s", i, m.file, model ? "its copy made" : "no copy");
}

// puppet i's kit made: a copy of each of Link's models, made with his data, flags and differed-list flags, on a heap
// of its own over memory the port gives (the puppet pool, GAPS C9/C11): none of the game's heaps is touched. Each slot
// gets its block (kSlotBytes from mem::host_alloc) the first time its puppet is shown and keeps it; a kit is a heap
// made on that block as JKRSolidHeap::create makes one (027EC464 -> sead's frame heap: its constructor 027549FC on the
// block, with no parent, then its set-up 02754AC4), kept (see Kit). A kit is ~441 KB (measured).
constexpr uint32_t kSlotBytes = 0xA0000;  // 640 KB
uint32_t g_slot_mem[kPuppets];
uint32_t pool_heap(Cpu* c, int i) {
    if (!g_slot_mem[i]) g_slot_mem[i] = mem::host_alloc(kSlotBytes, 0x40);
    static const uint32_t name = [] {  // the name JKRSolidHeap::create gives (its text, then the string class's vtable)
        const uint32_t b = mem::host_alloc(8);
        st32(b, GD(0x1016DCC8)), st32(b + 4, GD(0x1016DCB0));
        return b;
    }();
    const uint32_t blk = g_slot_mem[i];
    if (!blk) return 0;
    static uint32_t made_on[kPuppets];
    static uint32_t heap_of[kPuppets];
    if (made_on[i] == blk) return heap_of[i];  // (made on this block before, nothing made on it: still empty)
    made_on[i] = blk;
    const uint32_t r3 = c->r[3];
    // (this, name, parent, start, size, direction 1: from the front, no lock)
    const uint32_t heap = guest_call(c, GC(0x027549FC), {blk, name, 0, blk, kSlotBytes, 1, 0});
    if (heap) {
        guest_call(c, GC(0x02754AC4), {heap});
        st32(heap + sheap::kFlags, ld32(heap + sheap::kFlags) & ~4u);  // (as JKRSolidHeap::create leaves it)
    }
    c->r[3] = r3;
    heap_of[i] = heap;
    return heap;
}
void make_kit(Cpu* c, int i) {
    Kit& k = g_kit[i];
    if (k.heap || k.tried || g_models.empty()) return;
    k.tried = true;
    if (!k.models.empty()) {  // (made before: its copies put back, if Link's models are still of the same data)
        bool same = k.models.size() == g_models.size();
        for (size_t m = 0; same && m < g_models.size(); m++) same = k.models[m].first == g_models[m].data;
        if (same) {
            for (size_t m = 0; m < g_models.size(); m++) g_models[m].twin[i] = k.models[m].second, g_wearer[k.models[m].second] = i;
            k.heap = 1;  // (in use)
            trace("coop: puppet %d's kit: made before, shown again", i);
            return;
        }
        trace("coop: puppet %d's kit: Link's models changed, a new kit (the old one stays unused)", i);
        k.models.clear(), g_slot_mem[i] = 0;
    }
    const uint32_t r3 = c->r[3];
    const uint32_t heap = pool_heap(c, i);
    int made = 0, missed = 0;
    const uint32_t room = heap ? heap_free(heap) : 0;
    if (heap) {
        const uint32_t was = guest_call(c, GC(0x027EC220), {heap});  // JKRHeap::becomeCurrentHeap
        for (LinkModel& m : g_models) {
            // (a model made on a heap too small does not fail cleanly: stop well before that)
            if (heap_free(heap) < 0x20000) { missed++; continue; }
            (m.twin[i] = guest_call(c, GC(0x025E38E0), {m.data, m.flags, m.diff})) ? made++ : missed++;
        }
        guest_call(c, GC(0x027EC220), {was});
    }
    k.heap = made ? 1 : 0;  // (a heap with nothing made on it stays, empty, for the next try)
    if (made) for (const LinkModel& m : g_models) k.models.emplace_back(m.data, m.twin[i]);
    for (const LinkModel& m : g_models) if (m.twin[i]) g_wearer[m.twin[i]] = i;
    c->r[3] = r3;
    trace("coop: puppet %d's kit: %d of Link's models copied, %u KB of its %u KB block%s%s", i, made,
          heap ? (room - heap_free(heap)) >> 10 : 0, kSlotBytes >> 10, missed ? ", left out: " : "", missed ? std::to_string(missed).c_str() : "");
    // (its body's copy and that copy's joint block: interp.cpp's model trace names models by their joint block)
    if (g_body_k >= 0 && (size_t)g_body_k < g_models.size())
        if (const uint32_t body = g_models[g_body_k].twin[i])
            trace("coop: puppet %d's body: model %08X, joints %08X", i, body, ld32(body + kMdlJoints));
}
// ... let go: its kit kept on its block for the slot's next player, what it held and its rope given back
// (mDoExt_destroySolidHeap)
void free_kit(Cpu* c, int i) {
    const uint32_t r3 = c->r[3];
    for (LinkModel& m : g_models) if (m.twin[i]) g_wearer.erase(m.twin[i]), m.twin[i] = 0;
    if (g_kit[i].heap) trace("coop: puppet %d let go (its kit kept for the slot's next player)", i);
    g_kit[i].heap = 0, g_kit[i].tried = false, g_kit[i].release = false, g_kit[i].hidden = 0;
    for (const Held& h : g_held[i]) {
        g_wearer.erase(h.model);
        if (h.heap) guest_call(c, GC(0x025E3868), {h.heap});
    }
    g_held[i].clear();
    if (g_rope[i].heap) guest_call(c, GC(0x025E3868), {g_rope[i].heap});
    g_rope[i] = Rope{};
    c->r[3] = r3;
}

// a rope line as daHimo2 makes its own (mDoExt_3DlineMat1_c: ctor 025EB82C, init 025EBA58 with the "Always" archive's
// rope texture, file 0x7E), on a solid heap of its own; mat 1 if it was not made (not tried again)
Rope make_rope(Cpu* c) {
    static const uint32_t arc = [] {  // "Always", as daHimo2's create passes it (the text, then the string class's vtable)
        const uint32_t b = mem::host_alloc(8);
        st32(b, GD(0x10011004)), st32(b + 4, GD(0x10034B24));
        return b;
    }();
    const uint32_t tex = guest_call(c, GC(0x026066C4), {ld32(GD(0x101F4F28)), arc, 0x7E});
    const uint32_t heap = tex ? heap_for_models(c, 0x20000) : 0;  // mDoExt_createSolidHeapFromGame, 128 KB free
    uint32_t mat = 0;
    if (heap) {
        mat = mem::host_alloc(0x188, 0x20);
        const uint32_t was = guest_call(c, GC(0x027EC220), {heap});  // JKRHeap::becomeCurrentHeap
        guest_call(c, GC(0x025EB82C), {mat});
        if (!guest_call(c, GC(0x025EBA58), {mat, 1, 200, tex, 0})) mat = 0;
        guest_call(c, GC(0x027EC220), {was});
        guest_call(c, GC(0x025E3678), {heap});  // mDoExt_adjustSolidHeap
    }
    trace("coop: a rope line %s", mat ? "made" : "not made");
    if (!mat && heap) guest_call(c, GC(0x025E3868), {heap});
    return mat ? Rope{mat, heap} : Rope{1, 0};
}

// Playable characters (docs/multiplayer.md): Tetra, Medli or Princess Zelda in Link's place, on this screen and as
// the others see this player. The game's own model of her (its archive loaded with dComIfG_resLoad, 02520460, polled
// each step till done; the model file by name, 02606900, as Tetra's createHeap 02302B08 asks) made on a solid heap
// of its own, once a slot (0 this game, 1.. the puppets) and character. Each draw her bones take Link's: a bone
// kAlias names as one of his (or named as one) turns as his turned from his rest pose, the others keep their rest
// pose under their parent; each keeps its own length, so she keeps her proportions. (Copying his finished pose, not
// playing his animations on her: it carries what his code adds to them, the blends, look-at and foot placement.) Rest poses and names are read
// from the models' BFRES skeletons (FMDL +0xC: the FSKL, as 027F68FC finds it). Link's own models are drawn hidden.
extern "C" void f_023D8DF0_orig(Cpu* c);
// models: her body, then Medli's arms and wings (her own, hung from her body's locators as her draw's armNodeCallBack
// hangs them; the wings shown instead of the arms while she flies); idle: her NPC's wait clip for each (daNpc_Zl1's setAnm
// entry 1; Medli's l_anmTbl[0] and armAnmTbl[0]; Princess Zelda's modeWaitInit, 02455828: anm 1); fly: Medli's flying
// clips (daNpc_Md_c's anm 7: md_fly01, mdwing_fly01). Medli's are in her archive Md (Md_ship has no wings)
struct CharKind { const char *arc, *name, *file[3], *idle[3], *fly[3]; };
constexpr int kMedli = 2;
constexpr CharKind kChars[kCharacters] = {
    {"", "Link", {""}, {}, {}},
    {"Zl", "Tetra", {"zl.bdl"}, {"wait.bck"}, {}},
    {"Md", "Medli", {"md.bdl", "mdarm.bdl", "mdwing.bdl"}, {"md_wait01.bck", "mdarm_wait01.bck"}, {"md_fly01.bck", nullptr, "mdwing_fly01.bck"}},
    {"Pz", "Zelda", {"pz.bdl"}, {"wait02.bck"}, {}}};
// her bones (lower case) as Link's (cl.bdl's): the three skeletons are in docs/multiplayer.md
constexpr std::pair<const char*, const char*> kAlias[] = {
    {"world_root", "link_root"}, {"stomach", "stomach_jnt"}, {"chest", "chest_jnt"}, {"neck", "neck_jnt"},
    {"head", "head_jnt"}, {"chin", "chin_jnt"}, {"momi_l", "lmomi_jnt"}, {"momi_r", "rmomi_jnt"}, {"waist", "waist_jnt"},
    {"shoulder_l", "lshoulder_jnt"}, {"arm_l1", "larma_jnt"}, {"arm_l2", "larmb_jnt"}, {"zl_hand_l", "lhanda"},
    {"hand_l1", "lhanda"}, {"shoulder_r", "rshoulder_jnt"}, {"arm_r1", "rarma_jnt"}, {"arm_r2", "rarmb_jnt"},
    {"zl_hand_r", "rhanda"}, {"hand_r1", "rhanda"}, {"leg_l0", "lclotch_jnt"}, {"leg_l1", "llega_jnt"},
    {"leg_l2", "llegb_jnt"}, {"foot_l", "lfoot_jnt"}, {"leg_r0", "rclotch_jnt"}, {"leg_r1", "rlega_jnt"},
    {"leg_r2", "rlegb_jnt"}, {"foot_r", "rfoot_jnt"}, {"zl_eye", "eye"}, {"zl_mayu", "mayu"}, {"zl_nose", "hana"},
    {"zl_pod", "poda"},
    // Medli, and her arms (mdarm.bdl)
    {"backbone1", "stomach_jnt"}, {"backbone2", "chest_jnt"}, {"shoulderl", "lshoulder_jnt"}, {"arml", "larma_jnt"},
    {"shoulderr", "rshoulder_jnt"}, {"armr", "rarma_jnt"}, {"legl1", "llega_jnt"}, {"legl2", "llegb_jnt"},
    {"footl", "lfoot_jnt"}, {"legr1", "rlega_jnt"}, {"legr2", "rlegb_jnt"}, {"footr", "rfoot_jnt"},
    {"arml1", "larma_jnt"}, {"arml2", "larmb_jnt"}, {"handl", "lhanda"},
    {"armr1", "rarma_jnt"}, {"armr2", "rarmb_jnt"}, {"handr", "rhanda"}};
// Medli's arms (mdarm.bdl) and wings (mdwing.bdl) hang from these of her body's bones (her draw copies them in)
constexpr std::pair<const char*, const char*> kLocator[] = {{"armlloc", "arml"}, {"armrloc", "armr"}, {"winglloc", "arml"}, {"wingrloc", "armr"}};
// chains that hang (their rest pose sticks out; her animations let them fall)
constexpr const char* kHang[] = {"hair1"};
int g_own_char = 0;
float g_idle_w[1 + kPuppets];  // how far each slot (0 this game, 1.. the puppets) shows her own idle
float g_fly_w[1 + kPuppets];   // and Medli flying
uint32_t g_link_drawing = 0;  // Link in his draw now (his last draw's after it)
uint64_t g_body_parts = 0;  // his models a character replaces (bits by k): his body, hands, hair; not his gear
constexpr const char* kBodyParts[] = {"cl", "hands", "katsura", "yamu"};
bool body_part(int k) { return k >= 0 && k < 64 && (g_body_parts >> k & 1); }

using M34 = std::array<float, 12>;  // a 3x4 matrix, rows (rotation | translation)
void mul(const float* a, const float* b, float* o) {
    float r[12];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + (j == 3 ? a[i * 4 + 3] : 0);
    memcpy(o, r, sizeof r);
}
void rot_t(const float* a, float* o) {  // the rotation part transposed (its inverse), no translation
    const float r[12] = {a[0], a[4], a[8], 0, a[1], a[5], a[9], 0, a[2], a[6], a[10], 0};
    memcpy(o, r, sizeof r);
}

struct Bone {
    std::string key;  // the name, lower case
    int parent;
    M34 local{}, rest{};
    bool hang = false;
    float tip[3] = {};  // hang: towards the chain's end, in the bone's own frame
};
// the bones of a model file's skeleton with their rest poses (local, and in the model's space)
std::vector<Bone> skeleton(uint32_t data) {
    std::vector<Bone> out;
    using namespace bfres;
    const uint32_t fmdl = ld32(data), fskl = fmdl + fmdl_skeleton + ld32(fmdl + fmdl_skeleton);
    if (fskl == fmdl + fmdl_skeleton || ld32(fskl) != kFskl) return out;
    const uint32_t n = ld16(fskl + fskl_bone_count), bones = fskl + fskl_bones + ld32(fskl + fskl_bones);
    uint32_t stride = 0x40;
    while (n > 1 && stride < 0x80 && ld16(bones + stride + bone_index) != 1) stride += 0x10;  // (bone i's index is i)
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t b = bones + stride * i, flags = ld32(b + bone_flags);
        Bone o;
        for (uint32_t s = b + bone_name + ld32(b + bone_name); ld8(s); s++) o.key += (char)tolower(ld8(s));
        o.parent = (int16_t)ld16(b + bone_parent);
        if (o.parent >= (int)i) o.parent = -1;
        const uint32_t rot = b + bone_rotation, at = b + bone_translation;
        const float r[4] = {f32(rot), f32(rot + 4), f32(rot + 8), f32(rot + 12)};
        float* m = o.local.data();
        if ((flags >> 12 & 7) == 1) {  // Euler XYZ: Rz Ry Rx
            const float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), sy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
            const float e[12] = {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, 0, cy * sz, sx * sy * sz + cx * cz,
                                 cx * sy * sz - sx * cz, 0, -sy, sx * cy, cx * cy, 0};
            memcpy(m, e, sizeof e);
        } else {  // quaternion x y z w
            const float x = r[0], y = r[1], z = r[2], w = r[3];
            const float q[12] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0, 2 * (x * y + z * w),
                                 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0, 2 * (x * z - y * w), 2 * (y * z + x * w),
                                 1 - 2 * (x * x + y * y), 0};
            memcpy(m, q, sizeof q);
        }
        m[3] = f32(at), m[7] = f32(at + 4), m[11] = f32(at + 8);
        if (o.parent >= 0) mul(out[o.parent].rest.data(), m, o.rest.data());
        else o.rest = o.local;
        out.push_back(std::move(o));
    }
    return out;
}

struct Part {  // one of her models (her body; Medli's arms)
    uint32_t data = 0;
    std::vector<Bone> bones;
    std::vector<int> link_of;  // Link's bone for each of hers (-1: none; -2 - i: her body's bone i, kLocator)
    // her eye and brow materials (bits), by kind: plain, "damA", "damB" (Link's mpZOffBlendShape, mpZOnShape, mpZOffNoneShape): drawn in
    // Link's face passes, not with her body
    uint32_t face = 0, group[3] = {}, cover = 0;  // cover: drawn over her eyes' place ("zhead", "shadow")
    std::vector<uint32_t> face_joints;  // her joints carrying any of them (a face pass enters a joint's materials)
    uint32_t model[1 + kPuppets] = {};
    uint32_t idle = 0;  // a morf of her model playing her idle (its model's joints: her idle pose, in her model's space)
    uint32_t fly = 0;   // the same, Medli flying
};
struct Kind {
    uint32_t phase = 0, arc = 0;
    int state = 0;             // 0 loading, 1 ready, 2 failed
    uint32_t mapped_for = 0;   // Link's body data the maps were made for
    Part part[3];
} g_kind[kCharacters];
std::vector<Bone> g_link_bones;
uint32_t g_link_bones_of = 0;

uint32_t guest_text(const char* s) {
    const uint32_t n = (uint32_t)strlen(s), b = mem::host_alloc(n + 1);
    for (uint32_t i = 0; i <= n; i++) st8(b + i, (uint8_t)s[i]);
    return b;
}
// her own idle: a morf of her model data (mDoExt_McaMorf, 025E4F64, as her NPC's createHeap makes it; on a heap of its
// own) with her wait clip set looping as her NPC sets it (dNpc_setAnmFNDirect, 0259D344: the clip by name from her
// archive). 0 if it was not made
uint32_t make_idle(Cpu* c, uint32_t data, const char* clip, uint32_t arc) {
    const uint32_t heap = guest_call(c, GC(0x025E3528), {0, 0});  // mDoExt_createSolidHeapFromGame
    if (!heap) return 0;
    const uint32_t was = guest_call(c, GC(0x027EC220), {heap});
    const uint32_t sp = c->r[1] - 0x40;  // (its arguments past the registers: no sound, no base anim, the model's flags)
    st32(sp + 8, 0), st32(sp + 0xC, 0), st32(sp + 0x10, 0x80000), st32(sp + 0x14, 0x11020222);
    c->f[1].ps0 = 1.0;
    const uint32_t morf = guest_call(c, GC(0x025E4F64), {0, data, 0, 0, 0, 0xFFFFFFFF, 0, 0xFFFFFFFF});
    if (morf && ld32(morf + mca::mpModel)) {
        c->f[1].ps0 = 0, c->f[2].ps0 = 1.0;  // (no morf in, speed 1)
        guest_call(c, GC(0x0259D344), {morf, 2, guest_text(clip), 0, arc});  // (loop)
    }
    guest_call(c, GC(0x027EC220), {was});
    guest_call(c, GC(0x025E3678), {heap});  // mDoExt_adjustSolidHeap
    trace("coop: idle %s: %s", clip, morf ? "made" : "not made");
    return morf && ld32(morf + mca::mpModel) ? morf : 0;
}

// character ch's archive loaded and her models made for slot (0 this game, 1.. puppet slot-1), a step at a time
void need_character(Cpu* c, int ch, int slot) {
    Kind& k = g_kind[ch];
    if (ch <= 0 || k.state == 2 || k.part[0].model[slot]) return;
    if (!k.phase) k.phase = mem::host_alloc(16), k.arc = guest_text(kChars[ch].arc);
    if (k.state == 0) {
        const uint32_t r = guest_call(c, GC(0x02520460), {k.phase, k.arc});
        if (r != 4) {  // (cPhs_COMPLEATE_e; 5 and on: failed)
            if (r >= 5) k.state = 2, trace("coop: %s's archive %s did not load (%u)", kChars[ch].name, kChars[ch].arc, r);
            return;
        }
        for (int i = 0; i < 3; i++) {
            const char* file = kChars[ch].file[i];
            if (!file) continue;
            const uint32_t s = mem::host_alloc(16);  // the archive and file names as string objects (text, vtable)
            st32(s, k.arc), st32(s + 4, GD(0x10023B10)), st32(s + 8, guest_text(file)), st32(s + 12, GD(0x10023B10));
            Part& pt = k.part[i];
            pt.data = guest_call(c, GC(0x02606900), {ld32(GD(0x101F4F28)), s, s + 8});
            pt.bones = pt.data ? skeleton(pt.data) : std::vector<Bone>{};
            if (pt.data) {  // her materials' names (the FMDL's material index group)
                using namespace bfres;
                const uint32_t fmdl = ld32(pt.data), md = fmdl + fmdl_materials + ld32(fmdl + fmdl_materials);
                for (uint32_t m = 0, n = std::min<uint32_t>(ld32(md + group_count), 32); m < n; m++) {
                    std::string name;
                    for (uint32_t e = md + group_entries + group_entry * (m + 1), t = e + entry_name + ld32(e + entry_name); ld8(t); t++)
                        name += (char)ld8(t);
                    if (name.find("zhead") != std::string::npos || name.find("shadow") != std::string::npos)
                        pt.cover |= 1u << m;  // (Tetra's zelda_zhead*, Zelda's m_pz_shadowL/R: found with the probe)
                    if (name.find("eye") == std::string::npos && name.find("mayu") == std::string::npos) continue;
                    pt.group[name.find("damA") != std::string::npos ? 1 : name.find("damB") != std::string::npos ? 2 : 0] |= 1u << m;
                }
                pt.face = pt.group[1] | pt.group[2] ? pt.group[0] | pt.group[1] | pt.group[2] : 0;  // (Medli's are plain)
            }
            trace("coop: %s's %s: %08X, %zu bones", kChars[ch].name, file, pt.data, pt.bones.size());
            if (pt.bones.empty()) { k.state = 2; return; }
            if (kChars[ch].idle[i]) pt.idle = make_idle(c, pt.data, kChars[ch].idle[i], k.arc);
            if (kChars[ch].fly[i]) pt.fly = make_idle(c, pt.data, kChars[ch].fly[i], k.arc);
        }
        k.state = 1;
    }
    for (Part& pt : k.part) {
        if (!pt.data) continue;
        uint32_t model = 0;
        if (const uint32_t heap = guest_call(c, GC(0x025E3528), {0, 0})) {  // mDoExt_createSolidHeapFromGame
            const uint32_t was = guest_call(c, GC(0x027EC220), {heap});
            model = guest_call(c, GC(0x025E38E0), {pt.data, 0x80000, 0x11020222});  // (Tetra's differed-list flags)
            guest_call(c, GC(0x027EC220), {was});
            guest_call(c, GC(0x025E3678), {heap});
            if (!model) guest_call(c, GC(0x025E3868), {heap});
        }
        const uint32_t jnt = model ? ld32(model + kMdlJoints) : 0;
        if (!jnt || ld16(jnt + kJntNum) != pt.bones.size()) {
            k.state = 2, trace("coop: %s's model was not made (%08X)", kChars[ch].name, model);
            return;
        }
        pt.model[slot] = model;
        const uint32_t md = ld32(model + j3d::mModelData), joints = ld32(md + j3d::mJointNodes), mats = ld32(md + j3d::mMaterials);
        pt.face_joints.clear();
        for (uint32_t j = 0; j < pt.bones.size(); j++) {
            bool any = false, all = true;
            for (uint32_t m = ld32(joints + j3d::kJoint * j + j3d::kJointMaterial); m; m = ld32(m + j3d::kMaterialNext)) {
                const uint32_t i = (m - mats) / j3d::kMaterial;
                any = true, all &= i < 32 && (pt.face >> i & 1);
            }
            if (any && all) pt.face_joints.push_back(joints + j3d::kJoint * j);
        }
        if (pt.face_joints.empty() && pt.face) pt.face_joints.push_back(joints);  // (Zelda's: all on her root)
    }
}

// bone matrix o turned so its chain (tip, in its own frame) hangs down and a little behind (base: her place)
void hang(float* o, const float* tip, const float* base) {
    float v[3], t[3] = {-0.4f * base[2], -1, -0.4f * base[10]};
    for (int r = 0; r < 3; r++) v[r] = o[r * 4] * tip[0] + o[r * 4 + 1] * tip[1] + o[r * 4 + 2] * tip[2];
    const float lv = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), lt = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (lv < 1e-4f) return;
    for (int r = 0; r < 3; r++) v[r] /= lv, t[r] /= lt;
    const float a[3] = {v[1] * t[2] - v[2] * t[1], v[2] * t[0] - v[0] * t[2], v[0] * t[1] - v[1] * t[0]};
    const float sn = a[0] * a[0] + a[1] * a[1] + a[2] * a[2], cs = v[0] * t[0] + v[1] * t[1] + v[2] * t[2];
    if (sn < 1e-8f) return;
    const float f = (1 - cs) / sn;  // Rodrigues: I + [a] + [a]^2 (1 - c) / s^2
    const float r[12] = {1 - f * (a[1] * a[1] + a[2] * a[2]), -a[2] + f * a[0] * a[1], a[1] + f * a[0] * a[2], 0,
                         a[2] + f * a[0] * a[1], 1 - f * (a[0] * a[0] + a[2] * a[2]), -a[0] + f * a[1] * a[2], 0,
                         -a[1] + f * a[0] * a[2], a[0] + f * a[1] * a[2], 1 - f * (a[0] * a[0] + a[1] * a[1]), 0};
    const float keep[3] = {o[3], o[7], o[11]};
    mul(r, o, o);
    o[3] = keep[0], o[7] = keep[1], o[11] = keep[2];
}

// her models for `slot` posed from Link's body model `src` (his or a puppet's copy, joint matrices written)
bool pose_character(int ch, int slot, uint32_t src) {
    Kind& k = g_kind[ch];
    if (ch <= 0 || !k.part[0].model[slot] || g_body_k < 0) return false;
    const uint32_t body_data = g_models[g_body_k].data;
    if (g_link_bones_of != body_data) g_link_bones = skeleton(body_data), g_link_bones_of = body_data;
    const uint32_t sj = ld32(src + kMdlJoints), smtx = sj ? ld32(sj + kJntMtx) : 0;
    if (!smtx || ld16(sj + kJntNum) != g_link_bones.size()) return false;
    if (k.mapped_for != body_data) {
        k.mapped_for = body_data;
        for (Part& pt : k.part) {
            pt.link_of.assign(pt.bones.size(), -1);
            for (size_t j = 0; j < pt.bones.size(); j++) {
                std::string his = pt.bones[j].key;
                for (const auto& [a, b] : kAlias) if (his == a) his = b;
                for (size_t l = 0; l < g_link_bones.size(); l++) if (g_link_bones[l].key == his) pt.link_of[j] = (int)l;
                for (const auto& [a, b] : kLocator)
                    for (size_t i = 0; &pt != &k.part[0] && i < k.part[0].bones.size(); i++)
                        if (pt.bones[j].key == a && k.part[0].bones[i].key == b) pt.link_of[j] = -2 - (int)i;
                Bone& h = pt.bones[j];
                // (an arm model's rest pose is at its origin: its real one hangs from her body's bone)
                if (pt.link_of[j] <= -2) h.rest = k.part[0].bones[-2 - pt.link_of[j]].rest;
                else if (h.parent >= 0) mul(pt.bones[h.parent].rest.data(), h.local.data(), h.rest.data());
                for (const char* n : kHang) h.hang |= h.key == n;
                if (!h.hang) continue;
                size_t e = j;  // the chain's last bone
                for (bool more = true; more;) {
                    more = false;
                    for (size_t i = e + 1; i < pt.bones.size(); i++) if (pt.bones[i].parent == (int)e) { e = i, more = true; break; }
                }
                const float d[3] = {pt.bones[e].rest[3] - h.rest[3], pt.bones[e].rest[7] - h.rest[7], pt.bones[e].rest[11] - h.rest[11]};
                for (int r = 0; r < 3; r++) h.tip[r] = h.rest[r] * d[0] + h.rest[4 + r] * d[1] + h.rest[8 + r] * d[2];
            }
        }
    }
    M34 base{};
    for (int e = 0; e < 12; e++) base[e] = f32(src + kMdlBase + 4 * e);
    std::vector<M34> body;  // her body's bones as posed (Medli's arms hang from them)
    for (Part& pt : k.part) {
        const uint32_t model = pt.model[slot];
        if (!model) continue;
        const uint32_t dmtx = ld32(ld32(model + kMdlJoints) + kJntMtx);
        for (int e = 0; e < 12; e++) st32(model + kMdlBase + 4 * e, bits(base[e]));
        const float x = g_fly_w[slot] > 0 ? g_fly_w[slot] : g_idle_w[slot], iw = x * x * (3 - 2 * x);  // (eased)
        const uint32_t clip = g_fly_w[slot] > 0 ? pt.fly : pt.idle;
        const uint32_t im = clip && iw > 0 ? ld32(clip + mca::mpModel) : 0, ij = im ? ld32(im + kMdlJoints) : 0;
        const uint32_t idle = ij && ld16(ij + kJntNum) == pt.bones.size() ? ld32(ij + kJntMtx) : 0;
        std::vector<M34> w(pt.bones.size());
        std::vector<int> loc(pt.bones.size(), -1);  // the arm model's bone each hangs under (Medli's: kLocator)
        for (size_t j = 0; j < pt.bones.size(); j++) {
            const Bone& b = pt.bones[j];
            const int l = pt.link_of[j], p = b.parent;
            loc[j] = l <= -2 ? (int)j : p >= 0 ? loc[p] : -1;
            float* o = w[j].data();
            const float* wp = p >= 0 ? w[p].data() : nullptr;
            if (l >= 0) {  // his turn from rest, on her rest: L * LR^-1 * HR (rotation)
                float lm[12], inv[12];
                for (int e = 0; e < 12; e++) lm[e] = f32(smtx + 48 * (uint32_t)l + 4 * e);
                rot_t(g_link_bones[l].rest.data(), inv);
                mul(lm, inv, o), mul(o, b.rest.data(), o);
                if (p >= 0 && (pt.link_of[p] <= -2 || (pt.link_of[p] >= 0 && g_link_bones[l].parent == pt.link_of[p]))) {  // her length
                    o[3] = wp[0] * b.local[3] + wp[1] * b.local[7] + wp[2] * b.local[11] + wp[3];
                    o[7] = wp[4] * b.local[3] + wp[5] * b.local[7] + wp[6] * b.local[11] + wp[7];
                    o[11] = wp[8] * b.local[3] + wp[9] * b.local[7] + wp[10] * b.local[11] + wp[11];
                } else {  // where his is, moved by her rest place from his (his hips' bob, crouch and jump)
                    const float d[3] = {b.rest[3] - g_link_bones[l].rest[3], b.rest[7] - g_link_bones[l].rest[7],
                                        b.rest[11] - g_link_bones[l].rest[11]};
                    for (int r = 0; r < 3; r++)
                        o[r * 4 + 3] = lm[r * 4 + 3] + base[r * 4] * d[0] + base[r * 4 + 1] * d[1] + base[r * 4 + 2] * d[2];
                }
            } else if (l <= -2) {
                w[j] = body[-2 - l];
            } else {
                mul(p >= 0 ? wp : base.data(), p >= 0 ? b.local.data() : b.rest.data(), o);
                if (b.hang) hang(o, b.tip, base.data());
            }
            if (idle && l > -2 && (&pt == &k.part[0] || loc[j] >= 0)) {  // towards her idle, placed where she stands
                float n[12];
                for (int e = 0; e < 12; e++) n[e] = f32(idle + 48 * (uint32_t)j + 4 * e);
                if (loc[j] < 0) mul(base.data(), n, n);
                else {  // an arm: its clip's place under its locator, on her body's bone (her draw's armNodeCallBack)
                    float il[12], inv[12];
                    for (int e = 0; e < 12; e++) il[e] = f32(idle + 48 * (uint32_t)loc[j] + 4 * e);
                    rot_t(il, inv);
                    for (int r = 0; r < 3; r++) inv[4 * r + 3] = -(inv[4 * r] * il[3] + inv[4 * r + 1] * il[7] + inv[4 * r + 2] * il[11]);
                    mul(w[loc[j]].data(), inv, inv), mul(inv, n, n);
                }
                interp::blend_joint(o, n, o, iw);
            }
            for (int e = 0; e < 12; e++) st32(dmtx + 48 * (uint32_t)j + 4 * e, bits(o[e]));
        }
        if (body.empty()) body = std::move(w);
    }
    return true;
}
// Her eyes and brows over her hair, as her own draw does it (Tetra's, daNpc_Zl1 02308090): four passes, each a
// draw state packet entered (027F0E04; two kinds, made with 027F1278 and the vtable 10058D10 or 10058D40, as her
// create 02303730 makes hers) and her eye and brow joints' materials entered (027F583C) with one kind of them shown:
// "damB"; the plain ones, then her cover materials ("zhead", "shadow") on her root joint (and not with her body: a
// material is entered once a frame); "damA"; none. The first two
// into the game's draw list mpOpaListZelda0, the others mpXluListZelda1 (the current lists: 104B4634, 104B4638); her
// body then into mpOpaListZelda1 / mpOpaListPlayerExtra. Her view is calculated first (once a draw: interp.cpp skips her entry's own, view_done).
extern "C" void f_027F583C_orig(Cpu* c);
constexpr uint32_t kPacketVtable = 0xC;  // (measured: where 027F1278 leaves the draw state packet's vtable)
uint32_t g_face_state[1 + kPuppets][4];  // each slot's own (a packet is entered once a frame: two Tetras on one screen)
std::vector<uint32_t> g_viewed;  // the characters' models whose view this draw calculated
bool g_viewing = false;
// a slot's draw state packets made: each slot its own, as a packet is entered once a frame (two Tetras on one screen,
// sharing them, halted the game: J3DDrawBuffer.cpp "p_pkt->getEntryPtr() == 0")
void face_states(Cpu* c, int slot) {
    for (int k = 0; k < 4; k++) {
        const uint32_t a = g_face_state[slot][k] = mem::host_alloc(0x98, 0x20);
        for (uint32_t o = 0; o < 0x98; o += 4) st32(a + o, 0);
        guest_call(c, GC(0x027F1278), {a});
        st32(a + kPacketVtable, k % 2 ? GD(0x10058D10) : GD(0x10058D40));
    }
}
void enter_face(Cpu* c, const Part& pt, uint32_t model, uint32_t src, int slot) {
    const uint32_t* const state = g_face_state[slot];
    if (!state[0]) face_states(c, slot);
    for (int e = 0; e < 12; e++) st32(model + kMdlBase + 4 * e, ld32(src + kMdlBase + 4 * e));
    g_viewing = true, guest_call(c, GC(0x027F55FC), {model}), g_viewing = false;  // J3DModel::viewCalc
    g_viewed.push_back(model);
    const uint32_t game = guest_call(c, GC(0x025200D4)), lists = GD(0x104B4634), n = materials(model);
    const uint32_t root = ld32(ld32(model + j3d::mModelData) + j3d::mJointNodes);  // (her joint 0)
    const auto show = [&](uint32_t bits) { for (uint32_t m = 0; m < n; m++) st8(shown_flag(model, m), bits >> m & 1); };
    const auto pass = [&](int k, uint32_t bits) {
        guest_call(c, GC(0x027F0E04), {ld32(lists), state[k], 0});
        show(bits);
        for (const uint32_t j : pt.face_joints) c->r[3] = model, c->r[4] = j, f_027F583C_orig(c);
    };
    const uint32_t dl = game + inf::kDrawlist;
    st32(lists, ld32(dl + dlst::mpOpaListZelda0)), st32(lists + 4, ld32(dl + dlst::mpOpaListZelda0));
    pass(0, pt.group[2]);
    pass(1, pt.group[0]);
    show(pt.cover), c->r[3] = model, c->r[4] = root, f_027F583C_orig(c);
    st32(lists, ld32(dl + dlst::mpXluListZelda1)), st32(lists + 4, ld32(dl + dlst::mpXluListZelda1));
    pass(2, pt.group[1]);
    guest_call(c, GC(0x027F0E04), {ld32(lists), state[3], 0});
    st32(lists, ld32(dl + dlst::mpOpaListZelda1)), st32(lists + 4, ld32(dl + dlst::mpOpaListPlayerExtra));
}
// ... and entered as his (lit as Link), her face as hers is (enter_face); once a draw (his body is reported twice:
// entryDLSetLight and the modelEntryDL in it)
uint32_t g_char_entered = 0;  // slots entered this draw
// A hurt player's red flash (daPy_lk_c::draw, 023D9820): while Link's damage timer (mDamageWaitTimer) runs, outside his
// electric shock (procedure 0x6C), his light's fog (tevStr's mFogColor: s16 x3, mFogStartZ, mFogEndZ) is
// red (0xFF, 0x3C, 0x3C) from 200 in front of him (less a pulse of up to 200) to 300 past that, and each model's
// entry (entryDLSetLight) copies it into its materials. The puppets are entered in Link's draw with his light: around
// a puppet's entries Link's fog is that red for its own player's hurt (its pose's kHurt), or as it was before his own
// red (kept from his last unhurt draw) for one not hurt while he is
bool flashing(uint32_t link) { return (int16_t)ld16(link + py::mDamageWaitTimer) > 0 && ld32(link + kCurProc) != 0x6C; }
struct Red {
    uint32_t link;
    uint8_t was[16];  // (mFogColor..mFogEndZ)
    bool on = false;
    Red(Cpu* c, uint32_t link, const Puppet* u) : link(link) {  // (u null: this game's own Link, red as he is)
        if (!u) return;
        static uint8_t plain[16];
        static bool have_plain = false;
        const bool mine = flashing(link), hurt = u->pose.flags & kHurt;
        if (!mine) memcpy(plain, mem::ptr(link + ac::kFogColor), 16), have_plain = true;
        if (!hurt && !(mine && have_plain)) return;
        on = true, memcpy(was, mem::ptr(link + ac::kFogColor), 16);
        if (!hurt) {
            memcpy(mem::ptr(link + ac::kFogColor), plain, 16);
            return;
        }
        static const uint32_t buf = mem::host_alloc(0x20);  // its place, then in the camera's frame (mDoLib_pos2camera)
        for (int e = 0; e < 3; e++) st32(buf + 4 * e, bits(u->pos[e]));
        uint32_t r[8];
        for (int k = 0; k < 8; k++) r[k] = c->r[3 + k];
        guest_call(c, GC(0x025F1108), {buf, buf + 0x10});
        for (int k = 0; k < 8; k++) c->r[3 + k] = r[k];
        const double turn = (uint16_t)(ld32(GD(0x101FF560)) * 0x800) * M_PI / 32768;  // (g_Counter.mTimer)
        const float start = -(float)ldf32(buf + 0x18) - 200 + 200 * (float)std::fabs(std::sin(turn));
        st16(link + ac::kFogColor, 0xFF), st16(link + ac::kFogColor + 2, 0x3C), st16(link + ac::kFogColor + 4, 0x3C);
        st32(link + ac::kFogStart, bits(start)), st32(link + ac::kFogEnd, bits(start + 300));
    }
    ~Red() { if (on) memcpy(mem::ptr(link + ac::kFogColor), was, 16); }
};

void enter_character(Cpu* c, uint32_t link, int ch, int slot, uint32_t src) {
    Red red(c, link, slot ? &g_puppet[slot - 1] : nullptr);
    if (g_char_entered >> slot & 1 || !pose_character(ch, slot, src)) return;
    g_char_entered |= 1u << slot;
    const uint32_t lists = GD(0x104B4634), was_lists[2] = {ld32(lists), ld32(lists + 4)};
    const bool flying = g_fly_w[slot] >= 0.5f;
    for (Part& pt : g_kind[ch].part) {
        const uint32_t model = pt.model[slot], n = model ? materials(model) : 0;
        const int i = (int)(&pt - g_kind[ch].part);
        if (!model || (i == 1 && flying && g_kind[ch].part[2].model[slot]) || (i == 2 && !flying)) continue;  // (arms or wings)
        uint8_t was[32];
        for (uint32_t m = 0; m < n; m++) was[m] = ld8(shown_flag(model, m));
        const uint32_t r3 = c->r[3], r4 = c->r[4], r5 = c->r[5];
        if (pt.face) enter_face(c, pt, model, src, slot);
        for (uint32_t m = 0; m < n; m++) st8(shown_flag(model, m), was[m] & !((pt.face | pt.cover) >> m & 1));
        c->r[3] = link, c->r[4] = model, c->r[5] = 0;
        {
            SeeThrough see(slot - 1);  // (slot 0: this game's own)
            f_023D8DF0_orig(c);  // entryDLSetLight
        }
        c->r[3] = r3, c->r[4] = r4, c->r[5] = r5;
        for (uint32_t m = 0; m < n; m++) st8(shown_flag(model, m), was[m]);
        st32(lists, was_lists[0]), st32(lists + 4, was_lists[1]);
    }
}

// Link's model entered hidden while this game plays a character: its materials' shown flags off around `entry`
template <typename F> void hidden(uint32_t model, F entry) {
    const uint32_t n = materials(model);
    uint8_t was[32];
    for (uint32_t k = 0; k < n; k++) was[k] = ld8(shown_flag(model, k)), st8(shown_flag(model, k), 0);
    entry();
    for (uint32_t k = 0; k < n; k++) st8(shown_flag(model, k), was[k]);
}

// The tunics (docs/online-coop.md): Link's body texture (hero's clothes, linktexS3TC, or his island clothes,
// linktexbci4, which the game swaps in for it: before he has the hero's clothes and in a second quest) recoloured where
// it is the tunic's and hat's green (the island shirt's blue), one copy a colour, made the first time it is worn; a
// model's texture list (written by viewCalc, 027F50F8) then points at its wearer's. Which colour blocks are the tunic's is found at run time from the
// player's own loaded texture (no mask ships with the port): an endpoint (RGB565) of the base green's hue and
// saturation (tunic_green()). Each such endpoint takes the wanted colour's hue, its saturation and value moved as much
// as the base colour's are; a BC1 block whose endpoints swap order has its index 3 (transparent) made 2.
int g_own_tunic = -1;
struct Clothes {
    uint32_t base;  // the tunic's own colour, 0xRRGGBB
    uint32_t fmt, image_bytes;  // its GX2 format (without the sRGB bit) and level-0 size (640x384)
    bool hue_test;  // the hue test below may stand in until the computed mask is ready (BC1 green only)
    uint32_t tex = 0;            // Link's texture object (its GX2Texture at +4)
    uint32_t worn[4] = {};       // its copies, by tunic (0: none), each in memory of its own for good
    uint32_t img[4] = {}, mip[4] = {};
    bool made[4] = {};
    bool guessed[4] = {};        // made by the hue test (the computed mask was not ready): made again once it is
    uint32_t retry[4] = {};
    uint32_t made_from = 0;      // the texture they are copies of
    bool fresh = false;          // to be made again (another texture)
} g_clothes[2] = {{0x5AB24A, 0x31, 640 * 384 / 2, true},    // (BC1: HD Link's hero's clothes)
                  {0x4A75AD, 0x33, 640 * 384, false}};       // (BC3: his island clothes; the shirt's blue)
constexpr uint32_t kTunicRgb[4] = {0x5AB24A, 0xE52B36, 0x698DB9, 0x9A4F9F};  // green; the randomizer's Red/Blue/Purple Link

void hsv(uint16_t c, double& h, double& s, double& v) {
    const double r = (c >> 11) / 31.0, g = (c >> 5 & 63) / 63.0, b = (c & 31) / 31.0;
    const double mx = std::max({r, g, b}), d = mx - std::min({r, g, b});
    v = mx, s = mx > 0 ? d / mx : 0, h = 0;
    if (d < 0.001) return;
    h = 60 * (r >= mx ? (g - b) / d : g >= mx ? 2 + (b - r) / d : 4 + (r - g) / d);
    if (h < 0) h += 360;
}
uint16_t rgb565(double h, double s, double v) {
    double r = v, g = v, b = v;
    if (s > 0) {
        const double hh = (h >= 360 ? 0 : h) / 60, f = hh - (int)hh, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch ((int)hh) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
        }
    }
    return (uint16_t)(std::lround(r * 31) << 11 | std::lround(g * 63) << 5 | std::lround(b * 31));
}
uint16_t rgb565(uint32_t rgb) {
    return (uint16_t)(std::lround((rgb >> 16) / 255.0 * 31) << 11 | std::lround((rgb >> 8 & 255) / 255.0 * 63) << 5 | std::lround((rgb & 255) / 255.0 * 31));
}
// an endpoint that is the tunic's green (the hat and tunic; not his skin, hair, belt, boots or eyes): a hue around
// the base green's (about 110 degrees), clearly coloured and not too dark
bool tunic_green(uint16_t c) {
    double h, s, v;
    hsv(c, h, s, v);
    return h >= 80 && h <= 150 && s >= 0.3 && v >= 0.12;
}
uint16_t exchange(uint16_t base, uint16_t want, uint16_t cur) {
    double bh, bs, bv, wh, ws, wv, ch, cs, cv;
    hsv(base, bh, bs, bv), hsv(want, wh, ws, wv), hsv(cur, ch, cs, cv);
    if (cs == 0) cs = bs;
    return rgb565(wh, std::clamp(cs + ws - bs, 0.0, 1.0), std::clamp(cv + wv - bv, 0.0, 1.0));
}

// Link's texture object of these clothes as tunic t (0: his own), the copy made the first time
uint32_t worn(Clothes& k, int t) {
    if (!k.tex || kTunicRgb[t] == k.base) return k.tex;  // (the clothes' own colour: as they are)
    if (k.fresh) k.fresh = false, std::fill(std::begin(k.made), std::end(k.made), false), std::fill(std::begin(k.guessed), std::end(k.guessed), false);
    using namespace gx2;
    const uint32_t gx = k.tex + kTexSurface;
    if (k.made[t] && k.guessed[t] && ++k.retry[t] % 30 == 0 &&
        link_masks::find(mem::ptr(ld32(gx + image)), ld32(gx + imageSize), ld32(gx + mipmapSize) ? mem::ptr(ld32(gx + mipmaps)) : nullptr, ld32(gx + mipmapSize)))
        k.made[t] = false;  // (the mask is ready now: made again from it)
    if (k.made[t]) return k.worn[t];
    // (no hue test for these clothes: as they are until the mask is ready, asked again every 30 draws)
    if (!k.hue_test && (++k.retry[t] % 30 != 1 ||
                        !link_masks::find(mem::ptr(ld32(gx + image)), ld32(gx + imageSize), ld32(gx + mipmapSize) ? mem::ptr(ld32(gx + mipmaps)) : nullptr, ld32(gx + mipmapSize))))
        return k.tex;
    const uint32_t size = ld32(gx + imageSize), mips = ld32(gx + mipmapSize), align = std::max<uint32_t>(ld32(gx + alignment), 0x100);
    if (!k.worn[t]) k.worn[t] = mem::host_alloc(kTexObject, 0x20), k.img[t] = mem::host_alloc(size, align), k.mip[t] = mips ? mem::host_alloc(mips, align) : 0;
    k.made[t] = true;
    const uint32_t obj = k.worn[t], img = k.img[t], mip = k.mip[t];
    memcpy(mem::ptr(obj), mem::ptr(k.tex), kTexObject);
    memcpy(mem::ptr(img), mem::ptr(ld32(gx + image)), size);
    if (mip) memcpy(mem::ptr(mip), mem::ptr(ld32(gx + mipmaps)), mips);
    st32(obj + kTexSurface + image, img), st32(obj + kTexSurface + mipmaps, mip);
    // the tunic's mask computed from the player's own game (mods/link_masks.h): its tint; until it is ready (or for a
    // texture it does not know), the hue test below
    if (const link_masks::Mask* m = link_masks::find(mem::ptr(ld32(gx + image)), size, mips ? mem::ptr(ld32(gx + mipmaps)) : nullptr, mips)) {
        link_masks::tint(*m, link_masks::Part::kTunic, kTunicRgb[t], mem::ptr(img), mip ? mem::ptr(mip) : nullptr);
        k.guessed[t] = false;
        trace("coop: Link's clothes (%06X) in tunic %d made from the computed mask", k.base, t);
        return obj;
    }
    k.guessed[t] = true;
    const uint16_t base = rgb565(k.base), want = rgb565(kTunicRgb[t]);
    const uint32_t block = 8;  // (BC1)
    for (uint32_t n = 0, at = 0; at < size + mips; n++, at += block) {
        uint8_t* b = mem::ptr(at < size ? img + at : mip + at - size) + block - 8;
        uint16_t c[2];
        memcpy(c, b, 4);  // (little-endian: the GPU's)
        const uint8_t bits = (uint8_t)(tunic_green(c[0]) | tunic_green(c[1]) << 1);
        if (!bits) continue;
        const bool before = c[0] > c[1];
        for (int e = 0; e < 2; e++) if (bits >> e & 1) c[e] = exchange(base, want, c[e]);
        memcpy(b, c, 4);
        if (before && c[0] <= c[1]) {  // (BC1: no transparent texels where there were none)
            uint32_t idx;
            memcpy(&idx, b + 4, 4);
            for (int i = 0; i < 32; i += 2) if ((idx >> i & 3) == 3) idx &= ~(1u << i);
            memcpy(b + 4, &idx, 4);
        }
    }
    trace("coop: Link's clothes (%06X) in tunic %d made", k.base, t);
    return obj;
}

// a texture object that is these clothes: 640x384 in their format and size
bool is_clothes(const Clothes& k, uint32_t tex) {
    const uint32_t s = tex + gx2::kTexSurface;
    return tex >= mem::kMem2Start && tex < mem::kMem2End && ld32(s + gx2::width) == 640 && ld32(s + gx2::height) == 384 &&
           (ld32(s + gx2::format) & 0x3F) == k.fmt && ld32(s + gx2::imageSize) == k.image_bytes;
}
// the clothes a model's texture list entry is, or null: a known texture object, or (the game swaps the island clothes'
// texture into the list in viewCalc, so they need not be in a model's table) a new one taken on (its copies made again)
Clothes* clothes_of(uint32_t tex) {
    for (Clothes& k : g_clothes) {
        if (!tex || tex == k.tex) return tex ? &k : nullptr;
        for (uint32_t w : k.worn) if (w && tex == w) return nullptr;  // (one of the copies)
    }
    for (Clothes& k : g_clothes)
        if (is_clothes(k, tex)) {
            k.tex = tex;
            if (k.tex != k.made_from) k.made_from = k.tex, k.fresh = true;
            trace("coop: Link's clothes (%06X): %08X", k.base, tex);
            return &k;
        }
    return nullptr;
}

// Link's body textures, found among his models' (each model data's texture table: j3d::kTexTable), by size and format
void find_clothes() {
    for (Clothes& k : g_clothes) k.tex = 0;
    for (const LinkModel& lm : g_models) {
        const uint32_t table = ld32(ld32(lm.model + j3d::mModelData) + j3d::kTexTable), n = table ? std::min<uint32_t>(ld16(table), 64) : 0;
        for (uint32_t j = 0; j < n; j++)
            if (const uint32_t tex = ld32(ld32(table + j3d::kTexTableEntries) + j * j3d::kTexEntry + j3d::kTexEntryObj))
                for (Clothes& k : g_clothes)
                    if (!k.tex && is_clothes(k, tex)) k.tex = tex;
    }
    for (Clothes& k : g_clothes)  // (its copies made from another texture: made again, in the same memory)
        if (k.tex != k.made_from) k.made_from = k.tex, k.fresh = true;
    trace("coop: Link's clothes: %08X, island clothes %08X", g_clothes[0].tex, g_clothes[1].tex);
}

}  // namespace

void own_tunic(int t) { g_own_tunic = t; }
void own_character(int ch) { g_own_char = ch & 3; }
int own_char() { return g_own_char; }

// Link's voice variants (linkVoiceStart's table 0x1018D99C: voiceStart's id to its variants) by what he does, with
// Medli's and Zelda's own sounds for it (the decomp's JAZelAudio_SE.h; Tetra has no voice of her own in the sound
// table, and she is Zelda): attacks, hurt, jumps and rolls, crashes and falls, lifting and throwing, pushing,
// surprise, hanging, calling. The rest (breath, swimming, drinking...) none
uint32_t char_voice(uint32_t id, int ch) {
    if (!ch || id < 0x1800 || id > 0x184D) return id;
    struct Say { uint8_t lo, hi; uint16_t md, zl; };
    static constexpr Say kSay[] = {
        {0x00, 0x06, 0x48A6, 0x4989},  // slashes (MD_THROW, ZL_D24_ATTACK)
        {0x07, 0x10, 0x48AA, 0x4960},  // hurt, burnt (MD_DAMAGE, ZL_GN_DAMAGE)
        {0x11, 0x13, 0x48A7, 0},       // a side step (MD_LANDING)
        {0x14, 0x16, 0x48A8, 0},       // a jump (MD_FLY_UP)
        {0x17, 0x19, 0x48A7, 0},       // a roll, a landing
        {0x1A, 0x1C, 0x48A9, 0},       // rolled into a wall (MD_CRASH)
        {0x22, 0x22, 0x48A9, 0x484A},  // a fall (ZL_FDEMO_FALL)
        {0x23, 0x23, 0x48A9, 0x484B},  // a hard landing (ZL_FDEMO_LANDING)
        {0x25, 0x2C, 0x48A5, 0x4847},  // lifting, throwing (MD_LIFT_UP, ZL_FDEMO_JITA_1)
        {0x2D, 0x2D, 0x48A5, 0x4848},  // pushing, pulling
        {0x31, 0x32, 0x48A9, 0x484A},  // falling down (dead)
        {0x37, 0x38, 0x48AB, 0x4980},  // surprise (MD_CALL, ZL_D25_SURPRISED)
        {0x3B, 0x3C, 0, 0x498A},       // hanging, climbing up (ZL_D24_NECK_HANG)
        {0x44, 0x44, 0x48AB, 0},       // a call
        {0x46, 0x46, 0x48AA, 0x4960},  // an electric shock
        {0x4D, 0x4D, 0x48AB, 0x4980},  // surprise
    };
    const uint32_t v = id - 0x1800;
    for (const Say& s : kSay) if (v >= s.lo && v <= s.hi) return ch == kMedli ? s.md : s.zl;
    return 0;
}
bool view_done(uint32_t model) {
    return !g_viewing && std::find(g_viewed.begin(), g_viewed.end(), model) != g_viewed.end();
}

// Link's particles: each one dPa_control_c::set (025A847C: r4 group, r5 id, r6 pos, r7 angle) starts while he executes,
// relative to him, for the others to start at his puppet; theirs started the same way (group 0, alpha 0xFF, no callback)
static uint32_t g_exec_link = 0;
static std::vector<Effect> g_effects;
void link_executes(uint32_t link) { g_exec_link = link; }
uint32_t executing() { return g_exec_link; }
void particle(Cpu* c) {
    const uint32_t link = g_exec_link, pos = c->r[6], angle = c->r[7];
    if (!link || !g_want || !pos || g_effects.size() >= 32) return;
    float s, co;
    rot_y((int16_t)ld16(link + kAngleY), s, co);
    const float d[3] = {f32(pos) - f32(link + kPos), f32(pos + 4) - f32(link + kPos + 4), f32(pos + 8) - f32(link + kPos + 8)};
    Effect e{(uint16_t)c->r[5], {}, {}};
    const float l[3] = {co * d[0] - s * d[2], d[1], s * d[0] + co * d[2]};
    for (int i = 0; i < 3; i++) e.at[i] = (int16_t)std::clamp<long>(std::lround(l[i] * 4), -32768, 32767);
    if (angle) for (int i = 0; i < 3; i++) e.angle[i] = (int16_t)ld16(angle + 2 * i);
    e.angle[1] = (int16_t)(e.angle[1] - (int16_t)ld16(link + kAngleY));
    g_effects.push_back(e);
}
std::vector<Effect> take_effects() { return std::move(g_effects); }
void effect_at(Cpu* c, const Effect& e, const float pos[3], int16_t yaw) {
    static const uint32_t buf = mem::host_alloc(0x20);  // pos +0, angle +0xC
    float s, co;
    rot_y(yaw, s, co);
    const float l[3] = {e.at[0] / 4.f, e.at[1] / 4.f, e.at[2] / 4.f};
    const float w[3] = {pos[0] + co * l[0] + s * l[2], pos[1] + l[1], pos[2] - s * l[0] + co * l[2]};
    for (int i = 0; i < 3; i++) st32(buf + 4 * i, bits(w[i]));
    st16(buf + 0xC, (uint16_t)e.angle[0]), st16(buf + 0xE, (uint16_t)(e.angle[1] + yaw)), st16(buf + 0x10, (uint16_t)e.angle[2]);
    st32(c->r[1] - 0x40 + 8, 0xFFFFFFFF), st32(c->r[1] - 0x40 + 0xC, 0), st32(c->r[1] - 0x40 + 0x10, 0), st32(c->r[1] - 0x40 + 0x14, 0);
    guest_call(c, GC(0x025A847C), {ld32(kPlay + inf::kParticle), 0, e.id, buf, buf + 0xC, 0, 0xFF, 0});
}

// Compact joints (GAPS C10, the pose's flags bit 7): a model's joints in 8 bytes each instead of 15: the rotation as
// the smallest three of its quaternion (2 bits which is left out, 3 x 10 bits of the others, sign made positive for
// the one left out) and the translation relative to Link as 3 x 10 bits at half a unit (within 256 of him); a
// model's scales only when not all 1, its shown materials only when not all. A model with a joint further away (an
// arrow flown off) keeps the full form. A model's mode byte: bit 0 all shown, bit 1 compact, bit 2 every scale 1.
constexpr uint8_t kCompact = 0x80;
uint32_t q_pack(const int16_t q[4]) {
    float f[4];
    for (int e = 0; e < 4; e++) f[e] = q[e] / 32767.f;
    int big = 0;
    for (int e = 1; e < 4; e++) if (std::fabs(f[e]) > std::fabs(f[big])) big = e;
    const float sign = f[big] < 0 ? -1.f : 1.f;
    uint32_t out = (uint32_t)big << 30;
    for (int e = 0, k = 0; e < 4; e++) {
        if (e == big) continue;
        const float v = std::clamp(f[e] * sign * (float)M_SQRT2, -1.f, 1.f);  // (the others lie within +-1/sqrt 2)
        out |= (uint32_t)std::lround((v + 1) * 511.5f) << (20 - 10 * k++);
    }
    return out;
}
void q_unpack(uint32_t w, int16_t q[4]) {
    const int big = (int)(w >> 30);
    float f[4], sum = 0;
    for (int e = 0, k = 0; e < 4; e++) {
        if (e == big) continue;
        f[e] = ((float)((w >> (20 - 10 * k++)) & 1023) / 511.5f - 1) / (float)M_SQRT2;
        sum += f[e] * f[e];
    }
    f[big] = std::sqrt(std::max(0.f, 1 - sum));
    for (int e = 0; e < 4; e++) q[e] = (int16_t)std::lround(std::clamp(f[e], -1.f, 1.f) * 32767);
}
// a translation (x8 units) as 3 x 10 bits at half a unit; false when it is not within 255.5 units
bool t_pack(const int16_t t[3], uint32_t& out) {
    out = 0;
    for (int e = 0; e < 3; e++) {
        const long v = std::lround(t[e] / 4.0) + 512;  // (x8 -> x2)
        if (v < 0 || v > 1023) return false;
        out |= (uint32_t)v << (20 - 10 * e);
    }
    return true;
}
void t_unpack(uint32_t w, int16_t t[3]) {
    for (int e = 0; e < 3; e++) t[e] = (int16_t)((((int)(w >> (20 - 10 * e)) & 1023) - 512) * 4);
}

// Local joints (mode bit 3): each joint of one of Link's models as its rotation under its parent (the model's
// skeleton, FSKL: the same game data on every game) in 32 bits, and a translation only where it is not the
// skeleton's own (the root's, relative to Link; one an animation moves), in 32 bits. Both games build the joints the
// same way from those numbers, the sender from what it sent (so no error adds up down the chain), into the pose's
// joints as before (relative to Link). For a model of a unit scale whose skeleton has its joints' count.
void jmtx(const Joint& j, float m[12]) {
    float w = j.q[0] / 32767.f, x = j.q[1] / 32767.f, y = j.q[2] / 32767.f, z = j.q[3] / 32767.f;
    const float n = 1 / std::sqrt(std::max(1e-9f, w * w + x * x + y * y + z * z));
    w *= n, x *= n, y *= n, z *= n;
    const float r[12] = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), j.t[0] / 8.f,
                         2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), j.t[1] / 8.f,
                         2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), j.t[2] / 8.f};
    memcpy(m, r, sizeof r);
}
Joint jof(const float m[12]) {
    Joint j{};
    float sc[3], q[4] = {1, 0, 0, 0};
    interp::mtx_to_quat(m, sc, q);
    for (int e = 0; e < 4; e++) j.q[e] = (int16_t)std::lround(std::clamp(q[e], -1.f, 1.f) * 32767);
    for (int e = 0; e < 3; e++) j.t[e] = (int16_t)std::clamp<long>(std::lround(m[4 * e + 3] * 8), -32768, 32767);
    j.s = 64;
    return j;
}
void inv_rigid(const float a[12], float o[12]) {  // [R | t]^-1 = [R^T | -R^T t]
    rot_t(a, o);
    for (int i = 0; i < 3; i++) o[4 * i + 3] = -(o[4 * i] * a[3] + o[4 * i + 1] * a[7] + o[4 * i + 2] * a[11]);
}
const std::vector<Bone>* skel_for(uint8_t k, size_t joints) {
    if (k >= g_models.size()) return nullptr;
    static std::unordered_map<uint32_t, std::vector<Bone>> cache;
    const uint32_t data = g_models[k].data;
    auto it = cache.find(data);
    if (it == cache.end()) it = cache.emplace(data, skeleton(data)).first;
    return it->second.size() == joints && joints ? &it->second : nullptr;
}
// one joint's local transform as sent: its rotation (32 bits) and, if it has one, translation (32 bits); out: the
// matrix both games use
void local_of(uint32_t rot, const uint32_t* t, const Bone& b, float out[12]) {
    Joint j{};
    q_unpack(rot, j.q);
    if (t) t_unpack(*t, j.t);
    jmtx(j, out);
    if (!t) out[3] = b.local[3], out[7] = b.local[7], out[11] = b.local[11];
}
// The smaller local form (mode bit 4, GAPS C10 step 3): a rotation only where it is not the skeleton's own (within
// rest_deg(), 0.3 degrees: 197 bytes on average, 0.30 degrees off at worst, 238 bytes before; a second mask); a joint without one takes the skeleton's. (24-bit rotations were tried:
// 1.7 degrees off at worst, and larger poses, as fewer joints then stay at rest.)
float rest_deg() {
    static const float d = getenv("WWHD_COOP_REST_DEG") ? (float)atof(getenv("WWHD_COOP_REST_DEG")) : 0.3f;  // (tests)
    return d;
}
// as local_of, the rotation possibly none (the skeleton's own)
void local_of2(const uint32_t* rot, const uint32_t* t, const Bone& b, float out[12]) {
    if (rot) {
        Joint j{};
        q_unpack(*rot, j.q);
        jmtx(j, out);
    } else {
        for (int r = 0; r < 3; r++) for (int col = 0; col < 3; col++) out[4 * r + col] = b.local[4 * r + col];
    }
    if (t) {
        int16_t tt[3];
        t_unpack(*t, tt);
        out[3] = tt[0] / 8.f, out[7] = tt[1] / 8.f, out[11] = tt[2] / 8.f;
    } else out[3] = b.local[3], out[7] = b.local[7], out[11] = b.local[11];
}
double q_angle_deg(const int16_t a[4], const float m[12]) {  // between a rotation and a matrix's
    float sc[3], q[4] = {1, 0, 0, 0};
    interp::mtx_to_quat(m, sc, q);
    double dot = 0, na = 0;
    for (int e = 0; e < 4; e++) dot += a[e] / 32767.0 * q[e], na += (a[e] / 32767.0) * (a[e] / 32767.0);
    return 2 * std::acos(std::min(1.0, std::fabs(dot) / std::sqrt(std::max(1e-12, na)))) * 180 / M_PI;
}

void pack(const Pose& p, std::string& out) {
    static const bool full = getenv("WWHD_COOP_FULL_POSES") != nullptr;  // (test aid: the fork's form, to compare)
    auto put_bytes = [&](const void* v, size_t n) { out.append((const char*)v, n); };
    const size_t start = out.size();
    uint32_t size = 0;
    put_bytes(&size, 4);  // filled in below
    const uint8_t flags = (uint8_t)((p.flags & ~kCompact) | (full ? 0 : kCompact));
    put_bytes(p.stage, 8), put_bytes(&p.room, 1), put_bytes(&flags, 1), put_bytes(p.pos, 12), put_bytes(&p.yaw, 2);
    put_bytes(&p.proc, 2), put_bytes(&p.equip, 2);
    const uint8_t nm = (uint8_t)p.models.size();
    put_bytes(&nm, 1);
    for (const Model& m : p.models) {
        const uint8_t nj = (uint8_t)m.j.size();
        if (full) {
            put_bytes(&m.k, 1), put_bytes(&m.shown, 4), put_bytes(&nj, 1);
            if (m.k == kHeld) put_bytes(&m.file, 2), put_bytes(&m.diff, 4), put_bytes(&m.brk, 4);
            for (const Joint& j : m.j) put_bytes(j.q, 8), put_bytes(j.t, 6), put_bytes(&j.s, 1);
            continue;
        }
        std::vector<uint32_t> ts(m.j.size());
        bool compact = true, unit = true;
        for (size_t i = 0; i < m.j.size(); i++) compact &= t_pack(m.j[i].t, ts[i]), unit &= m.j[i].s == 64;
        if (const std::vector<Bone>* sk = unit && m.k != kHeld && !getenv("WWHD_COOP_NO_LOCAL") ? skel_for(m.k, m.j.size()) : nullptr) {
            // local: each joint under the parent as rebuilt (the receiver's), its translation only if not the skeleton's
            std::vector<std::array<float, 12>> built(m.j.size());
            std::vector<uint32_t> rots, trs;
            std::vector<uint8_t> mask((m.j.size() + 7) / 8, 0), rmask((m.j.size() + 7) / 8, 0);
            bool ok = true;
            for (size_t i = 0; i < m.j.size() && ok; i++) {
                const Bone& b = (*sk)[i];
                float w[12], l[12], ip[12], bl[12];
                jmtx(m.j[i], w);
                if (b.parent >= 0) inv_rigid(built[b.parent].data(), ip), mul(ip, w, l);
                else memcpy(l, w, sizeof l);
                const Joint lj = jof(l);
                for (int e = 0; e < 12; e++) bl[e] = b.local[e];
                const bool turned = b.parent < 0 || q_angle_deg(lj.q, bl) > rest_deg();
                uint32_t rw = 0;
                if (turned) rw = q_pack(lj.q), rmask[i / 8] |= (uint8_t)(1 << (i % 8)), rots.push_back(rw);
                const bool own = b.parent < 0 || std::fabs(l[3] - b.local[3]) > 0.3f || std::fabs(l[7] - b.local[7]) > 0.3f ||
                                 std::fabs(l[11] - b.local[11]) > 0.3f;
                uint32_t tw = 0;
                if (own) ok = t_pack(lj.t, tw), mask[i / 8] |= (uint8_t)(1 << (i % 8)), trs.push_back(tw);
                float lq[12];
                local_of2(turned ? &rw : nullptr, own ? &tw : nullptr, b, lq);
                if (b.parent >= 0) mul(built[b.parent].data(), lq, built[i].data());
                else memcpy(built[i].data(), lq, sizeof lq);
            }
            if (ok) {
                const uint8_t mode = (uint8_t)((m.shown == ~0u ? 1 : 0) | 8 | 0x10);
                put_bytes(&m.k, 1), put_bytes(&mode, 1);
                if (!(mode & 1)) put_bytes(&m.shown, 4);
                put_bytes(&nj, 1), put_bytes(mask.data(), mask.size()), put_bytes(rmask.data(), rmask.size());
                put_bytes(rots.data(), 4 * rots.size());
                put_bytes(trs.data(), 4 * trs.size());
                continue;
            }
        }
        const uint8_t mode = (uint8_t)((m.shown == ~0u ? 1 : 0) | (compact ? 2 : 0) | (unit ? 4 : 0));
        put_bytes(&m.k, 1), put_bytes(&mode, 1);
        if (!(mode & 1)) put_bytes(&m.shown, 4);
        put_bytes(&nj, 1);
        if (m.k == kHeld) put_bytes(&m.file, 2), put_bytes(&m.diff, 4), put_bytes(&m.brk, 4);
        for (size_t i = 0; i < m.j.size(); i++) {
            const Joint& j = m.j[i];
            if (compact) {
                const uint32_t q = q_pack(j.q);
                put_bytes(&q, 4), put_bytes(&ts[i], 4);
            } else {
                put_bytes(j.q, 8), put_bytes(j.t, 6);
            }
            if (!unit) put_bytes(&j.s, 1);
        }
    }
    put_bytes(&p.trail, 4);
    if (p.trail > 0) {
        put_bytes(&p.trail_idx, 4), put_bytes(&p.trail_color, 4), put_bytes(&p.trail_top, 4);
        put_bytes(p.trail_pts.data(), 2 * p.trail_pts.size());
    }
    put_bytes(&p.chain, 2);
    if (p.chain) put_bytes(p.chain_root, 6), put_bytes(p.chain_angle, 4);
    const uint8_t nr = (uint8_t)(p.rope_pts.size() / 3);
    put_bytes(&nr, 1);
    if (nr) put_bytes(&p.rope_width, 4), put_bytes(p.rope_pts.data(), 6 * nr);
    if (p.flags & kSailing) put_bytes(p.boat, 12), put_bytes(&p.boat_yaw, 2);  // (C16: his boat, for a friend's deck)
    if (p.flags & kOnFriend) put_bytes(&p.deck_of, 1);
    size = (uint32_t)(out.size() - start);
    memcpy(&out[start], &size, 4);
    // test aid WWHD_COOP_POSE_CHECK=1: each compact pose read back and compared with what was packed (largest joint
    // rotation error in degrees, largest translation error in units, the sizes), traced every 300 poses
    if (static const bool check = getenv("WWHD_COOP_POSE_CHECK") != nullptr; check && !full) {
        static double worst_deg = 0, worst_t = 0, sum_size = 0;
        static uint32_t n = 0;
        Pose back;
        if (unpack((const uint8_t*)out.data() + start, size, back)) {
            for (size_t mi = 0; mi < p.models.size() && mi < back.models.size(); mi++)
                for (size_t ji = 0; ji < p.models[mi].j.size() && ji < back.models[mi].j.size(); ji++) {
                    const Joint &a = p.models[mi].j[ji], &b = back.models[mi].j[ji];
                    double dot = 0, na = 0, nb = 0;
                    for (int e = 0; e < 4; e++) dot += (double)a.q[e] * b.q[e], na += (double)a.q[e] * a.q[e], nb += (double)b.q[e] * b.q[e];
                    if (na > 0 && nb > 0) worst_deg = std::max(worst_deg, 2 * std::acos(std::min(1.0, std::fabs(dot) / std::sqrt(na * nb))) * 180 / M_PI);
                    for (int e = 0; e < 3; e++) worst_t = std::max(worst_t, std::fabs(a.t[e] - b.t[e]) / 8.0);
                }
        } else worst_deg = 999;
        sum_size += size;
        if (++n % 300 == 0) {
            trace("coop: pose check: %u poses, %.0f bytes on average (this one %u), worst joint error %.2f degrees, %.2f units", n,
                  sum_size / n, size, worst_deg, worst_t);
            worst_deg = worst_t = 0;
        }
    }
}

bool unpack(const uint8_t* d, size_t n, Pose& p, size_t* used) {
    size_t o = 0;
    auto get = [&](void* v, size_t k) {
        if (o + k > n) return false;
        memcpy(v, d + o, k), o += k;
        return true;
    };
    uint32_t size = 0;
    if (!get(&size, 4) || size > n || size < 35) return false;
    n = size;
    uint8_t nm = 0;
    if (!get(p.stage, 8) || !get(&p.room, 1) || !get(&p.flags, 1) || !get(p.pos, 12) || !get(&p.yaw, 2) ||
        !get(&p.proc, 2) || !get(&p.equip, 2) || !get(&nm, 1))
        return false;
    const bool compact_form = p.flags & kCompact;
    p.flags &= ~kCompact;
    p.models.resize(nm);
    for (Model& m : p.models) {
        uint8_t nj = 0, mode = 0;
        if (!get(&m.k, 1)) return false;
        if (compact_form && !get(&mode, 1)) return false;
        if (compact_form && (mode & 1)) m.shown = ~0u;
        else if (!get(&m.shown, 4)) return false;
        if (!get(&nj, 1)) return false;
        if (m.k == kHeld && (!get(&m.file, 2) || !get(&m.diff, 4) || !get(&m.brk, 4))) return false;
        m.j.resize(nj);
        if (compact_form && (mode & 8)) {  // local joints: rebuilt as the sender did
            const std::vector<Bone>* sk = skel_for(m.k, nj);
            std::vector<uint8_t> mask((nj + 7) / 8), rmask((nj + 7) / 8, 0xFF);
            if (!sk || !get(mask.data(), mask.size())) return false;
            const bool masked = mode & 0x10;
            if (masked && !get(rmask.data(), rmask.size())) return false;
            std::vector<uint32_t> rots(nj, 0);
            for (size_t i = 0; i < nj; i++)
                if (rmask[i / 8] >> (i % 8) & 1)
                    if (!get(&rots[i], 4)) return false;
            std::vector<std::array<float, 12>> built(nj);
            for (size_t i = 0; i < nj; i++) {
                const Bone& b = (*sk)[i];
                uint32_t tw = 0;
                const bool own = mask[i / 8] >> (i % 8) & 1, turned = rmask[i / 8] >> (i % 8) & 1;
                if (own && !get(&tw, 4)) return false;
                float lq[12];
                if (masked) local_of2(turned ? &rots[i] : nullptr, own ? &tw : nullptr, b, lq);
                else local_of(rots[i], own ? &tw : nullptr, b, lq);
                if (b.parent >= 0) mul(built[b.parent].data(), lq, built[i].data());
                else memcpy(built[i].data(), lq, sizeof lq);
                m.j[i] = jof(built[i].data());
            }
            continue;
        }
        for (Joint& j : m.j) {
            if (!compact_form) {
                if (!get(j.q, 8) || !get(j.t, 6) || !get(&j.s, 1)) return false;
                continue;
            }
            if (mode & 2) {
                uint32_t q, t;
                if (!get(&q, 4) || !get(&t, 4)) return false;
                q_unpack(q, j.q), t_unpack(t, j.t);
            } else if (!get(j.q, 8) || !get(j.t, 6)) return false;
            j.s = 64;
            if (!(mode & 4) && !get(&j.s, 1)) return false;
        }
    }
    if (!get(&p.trail, 4)) return false;
    if (p.trail > 0) {
        p.trail_pts.resize(kTrailPts);
        if (!get(&p.trail_idx, 4) || !get(&p.trail_color, 4) || !get(&p.trail_top, 4) || !get(p.trail_pts.data(), 2 * kTrailPts)) return false;
    }
    uint8_t nr = 0;
    if (!get(&p.chain, 2) || (p.chain && (!get(p.chain_root, 6) || !get(p.chain_angle, 4))) || !get(&nr, 1)) return false;
    p.rope_pts.resize(3 * nr);
    if (nr && (!get(&p.rope_width, 4) || !get(p.rope_pts.data(), 6 * nr))) return false;
    if ((p.flags & kSailing) && (!get(p.boat, 12) || !get(&p.boat_yaw, 2))) return false;
    if ((p.flags & kOnFriend) && !get(&p.deck_of, 1)) return false;
    if (used) *used = size;
    if (o != size) return false;
    // (another game's data, written into this game's objects as it is drawn: what the game itself could not have made
    // is refused: a place off any stage, numbers that are no numbers, a trail index, chain or rope past their arrays)
    auto finite = [](float v, float most) { return std::isfinite(v) && std::fabs(v) <= most; };
    for (float v : p.pos) if (!finite(v, 1e6f)) return false;
    for (float v : p.boat) if (!finite(v, 1e6f)) return false;
    if ((p.flags & kOnFriend) && (p.deck_of >= 16 || !finite(p.pos[0], 1000) || !finite(p.pos[1], 1000) || !finite(p.pos[2], 1000)))
        return false;  // (a friend's deck: a party member's (up to 16, deck::kDecks), a few steps across)
    if (p.trail > 0 && (p.trail_idx < 0 || p.trail_idx >= 60 || !finite(p.trail_top, 1e4f))) return false;
    if (p.chain > 300 || p.rope_pts.size() > 3 * 200 || (nr && !finite(p.rope_width, 1e3f))) return false;
    for (const Model& m : p.models) if (m.k == kHeld && !finite(m.brk, 1e5f)) return false;
    return true;
}

void blend(const Pose& a, const Pose& b, float t, Pose& out) {
    out = b;
    // (a place in another frame, a world one and a deck's or two decks': the newer as it is)
    const bool frame = !((a.flags ^ b.flags) & (kOnDeck | kOnFriend)) && a.deck_of == b.deck_of;
    for (int i = 0; i < 3 && frame; i++) out.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * t;
    if (frame) out.yaw = (int16_t)(a.yaw + (int16_t)(b.yaw - a.yaw) * t);
    if (a.flags & b.flags & kSailing) {
        for (int i = 0; i < 3; i++) out.boat[i] = a.boat[i] + (b.boat[i] - a.boat[i]) * t;
        out.boat_yaw = (int16_t)(a.boat_yaw + (int16_t)(b.boat_yaw - a.boat_yaw) * t);
    }
    for (Model& m : out.models)
        for (const Model& n : a.models) {
            if (n.k != m.k || n.file != m.file || n.j.size() != m.j.size()) continue;
            for (size_t i = 0; i < m.j.size(); i++) {
                const Joint &x = n.j[i], &y = m.j[i];
                const int dot = x.q[0] * y.q[0] + x.q[1] * y.q[1] + x.q[2] * y.q[2] + x.q[3] * y.q[3], sign = dot < 0 ? -1 : 1;
                Joint& o = m.j[i];
                for (int e = 0; e < 4; e++) o.q[e] = (int16_t)std::lround(sign * x.q[e] + (y.q[e] - sign * x.q[e]) * t);  // (put() normalises)
                for (int e = 0; e < 3; e++) o.t[e] = (int16_t)std::lround(x.t[e] + (y.t[e] - x.t[e]) * t);
                o.s = (uint8_t)std::lround(x.s + (y.s - x.s) * t);
            }
            break;
        }
}

bool own_pose(Pose& out) {
    if (g_have) out = g_pose;
    return g_have;
}

void state_loaded() {
    // A full state put the game's memory back as it was (the port's own, where the kits are, is not in it): Link's
    // models of then are not known here, and what the puppets held and their ropes were on game heaps that may not be
    // there now. Those are forgotten, not destroyed (destroying them would free into the state's heaps); the puppets are
    // not drawn until Link is made again (the next place), when their kits are found again by his models' data.
    for (int i = 0; i < kPuppets; i++) {
        g_held[i].clear(), g_rope[i] = Rope{}, g_hook[i] = 0;
        g_kit[i].heap = 0, g_kit[i].tried = false, g_kit[i].release = false, g_kit[i].hidden = 0;
    }
    // Link as he was made before the load, if the state has the same one (a state saved since he was made, here: his
    // body model where it was and each of his models still made of the same data): his models stay known, and the
    // puppets' kits are put back at their next show
    bool same = !g_models.empty() && g_link_drawing && g_body_k >= 0 && (size_t)g_body_k < g_models.size() &&
                ld32(g_link_drawing + py::mpCLModel) == g_models[g_body_k].model;
    for (const LinkModel& m : g_models) same = same && m.model >= mem::kMem2Start && m.model < mem::kMem2End && ld32(m.model + j3d::mModelData) == m.data;
    g_wearer.clear(), g_have = false;
    if (same) {
        for (LinkModel& m : g_models) {
            for (uint32_t& t : m.twin) t = 0;
            g_wearer[m.model] = -1;
        }
        trace("coop: a full state was loaded: Link is the one made before; the puppets are shown again");
        return;
    }
    g_models.clear(), g_link_files.clear();
    trace("coop: a full state was loaded: the puppets wait for Link to be made again");
}

int body_model() { return g_body_k; }

void release(int i) {
    if (i >= 0 && i < kPuppets) g_puppet[i].shown = false, g_kit[i].release = true, emotes::stop(i);
}

void show(int i, const Pose* p, const float pos[3], int16_t yaw, int tunic, bool see_through) {
    Puppet& u = g_puppet[i];
    u.shown = p != nullptr;
    if (!p) return;
    u.see = see_through;
    u.tunic = tunic & 3;
    u.pose = *p;
    memcpy(u.pos, pos, sizeof u.pos), u.yaw = yaw;
    // test aid: WWHD_COOP_TEST_PUPPET_AT=<forward>,<turn degrees>[,<right>[,<speed>]]: every puppet shown that far ahead
    // along its heading (and to its right) and turned; with a speed (units a second) it also walks ahead, from where it is
    // to 4 s on and back again (C23: a puppet moving every step at 60 fps) (two games started on one save stand on one spot: one faces the other's camera)
    static float ahead = 0, turn = 0, right = 0, speed = 0;
    static const bool moved = [] { const char* e = getenv("WWHD_COOP_TEST_PUPPET_AT"); return e && sscanf(e, "%f,%f,%f,%f", &ahead, &turn, &right, &speed) >= 1; }();
    if (moved) {
        float s, c;
        rot_y(yaw, s, c);
        const float walked = speed * (float)(net::now_ms() % 4000) / 1000.f;
        u.pos[0] += s * (ahead + walked) - c * right, u.pos[2] += c * (ahead + walked) + s * right, u.yaw = (int16_t)(yaw + (int)(turn * 32768 / 180));
    }
    emotes::pose(i, u.pose, u.pos, g_body_k);  // (C22: an emote its player played)
}

// Medli flies (Asad's rule: as in the game; daNpc_Md_c::flyPlayerAction, 02294160, when one plays her): in the air
// (Link's mModeFlg bit 2, in an auto jump or a fall: procedures 0x24, 0x27, 0x29) A takes off, up at 20; then she
// glides down at most 10 a step (gravity at -1, maxFallSpeed, the fall's floor, at -10); each A, or every 10 steps it is
// held, flaps her up (the floor at +10 for 5 steps). 450 steps (15 s), 70 without A, the ground or anything else Link
// does ends it, as 1500 over where she took off is as high as she goes. No fall damage: the fall's start (m35F0,
// m35F4, m3688.y, as procAutoJump_init sets them) kept at her height
struct Flight { int left = 0, flap = 0, idle = 0; float top = 0; uint32_t gravity = 0, floor = 0; } g_fly;
void fly(uint32_t link) {
    Flight& f = g_fly;
    const uint32_t proc = ld32(link + kCurProc);
    const bool air = ld32(link + py::mModeFlg) & 2, may = g_own_char == kMedli && air && (proc == 0x24 || proc == 0x27 || proc == 0x29);
    const bool a = ld8(link + py::mItemTrigger) & 1, held = ld8(link + py::mItemButton) & 1;
    if (!f.left) {
        if (!may || !a) return;
        f = {450, 0, 70, f32(link + kPos + 4) + 1500, ld32(link + ac::gravity), ld32(link + ac::maxFallSpeed)};
        st32(link + ac::kSpeedY, bits(20.f));
        trace("coop: Medli takes off");
    } else if (a || (held && f.left % 10 == 0)) f.flap = 5, f.idle = 70;
    else if (f.flap) f.flap--;
    if (!may || --f.left <= 0 || --f.idle <= 0) {
        st32(link + ac::gravity, f.gravity), st32(link + ac::maxFallSpeed, f.floor), f.left = 0;
        return trace("coop: Medli lands");
    }
    st32(link + ac::gravity, bits(-1.f)), st32(link + ac::maxFallSpeed, bits(f.flap ? 10.f : -10.f));
    if (f32(link + kPos + 4) > f.top) st32(link + kPos + 4, bits(f.top)), st32(link + ac::kSpeedY, bits(0.f));
    for (uint32_t o : {py::m35F0, py::m35F4, py::kFallStartY}) st32(link + o, ld32(link + kPos + 4));
}

void step(Cpu* c, uint32_t link, bool on) {
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* r = getenv("WWHD_COOP_RECORD")) g_rec = fopen(r, "wb"), trace("coop: recording Link to %s", r);
        if (const char* g = getenv("WWHD_COOP_GHOST")) load_ghost(g);
    }
    if (link) {
        need_character(c, g_own_char, 0);
        for (int i = 0; i < kPuppets; i++) if (g_puppet[i].shown) need_character(c, g_puppet[i].pose.flags >> 4 & 3, i + 1);
        // her idle while Link waits (procedures 3-5: waiting, his idle fidget), eased in and out over 0.3 s; each clip
        // played a step, its pose worked out (its model at the origin: her idle in her model's space)
        // Medli flying the same, over 0.2 s
        fly(link);
        bool playing[kCharacters] = {}, flying[kCharacters] = {};
        for (int s = 0; s <= kPuppets; s++) {
            const bool shown = s == 0 || g_puppet[s - 1].shown;
            const int ch = s == 0 ? g_own_char : g_puppet[s - 1].pose.flags >> 4 & 3;
            const uint32_t proc = s == 0 ? ld32(link + kCurProc) : g_puppet[s - 1].pose.proc;
            const bool fl = s == 0 ? g_fly.left > 0 : g_puppet[s - 1].pose.flags & kFlying;
            g_idle_w[s] = std::clamp(g_idle_w[s] + (shown && ch && proc >= 3 && proc <= 5 ? 1 : -1) / 9.f, 0.f, 1.f);
            g_fly_w[s] = std::clamp(g_fly_w[s] + (shown && ch == kMedli && fl ? 1 : -1) / 6.f, 0.f, 1.f);
            playing[ch] |= shown && g_idle_w[s] > 0, flying[ch] |= shown && g_fly_w[s] > 0;
        }
        auto play = [&](uint32_t morf) {
            guest_call(c, GC(0x025E535C), {morf, 0, 0, 0});  // mDoExt_McaMorf::play (no sound)
            const uint32_t m = ld32(morf + mca::mpModel);
            for (int e = 0; e < 12; e++) st32(m + kMdlBase + 4 * e, bits(e % 5 == 0 ? 1.f : 0.f));
            guest_call(c, GC(0x025E55A0), {morf});  // its calc
        };
        for (int ch = 1; ch < kCharacters; ch++)
            for (Part& pt : g_kind[ch].part) {
                if (playing[ch] && pt.idle) play(pt.idle);
                if (flying[ch] && pt.fly) play(pt.fly);
            }
    }
    g_want = on && link;
    if (!on) {
        for (Puppet& u : g_puppet) u.shown = false;
        return;
    }
    if (g_rec && g_have) {
        std::string b;
        pack(g_pose, b);
        fwrite(b.data(), 1, b.size(), g_rec), fflush(g_rec);
    }
    // test aid: WWHD_COOP_TEST_STATE=<save step>,<load step>: a portable state saved into slot 5, then loaded, that many
    // steps after the puppets' step started (save states with puppets shown)
    // (a third number 1: a full state)
    if (static int t_save = -1, t_load = -1, t_full = 0, t_n = 0; getenv("WWHD_COOP_TEST_STATE")) {
        if (t_save < 0 && sscanf(getenv("WWHD_COOP_TEST_STATE"), "%d,%d,%d", &t_save, &t_load, &t_full) < 2) t_save = t_load = 1 << 30;
        if (++t_n == t_save) (t_full ? ss::request_save_full(5) : ss::request_save_portable(5)), trace("coop: state test: saving");
        if (t_n == t_load) ss::request_load(5), trace("coop: state test: loading");
    }
    if (!g_ghost.empty()) {  // the recording, step by step beside where it was made, then round again
        const Pose& p = g_ghost[g_ghost_at++ % g_ghost.size()];
        const float at[3] = {p.pos[0] + g_ghost_off[0], p.pos[1] + g_ghost_off[1], p.pos[2] + g_ghost_off[2]};
        const bool here = strncmp(p.stage, (const char*)mem::ptr(kStage), 8) == 0;
        static const int tunic = getenv("WWHD_COOP_GHOST_TUNIC") ? atoi(getenv("WWHD_COOP_GHOST_TUNIC")) : 1;
        // WWHD_COOP_GHOSTS=<n>: n of them (1..kPuppets), in a ring around that place, each a little further on in it
        static const int n_ghosts = std::clamp(getenv("WWHD_COOP_GHOSTS") ? atoi(getenv("WWHD_COOP_GHOSTS")) : 1, 1, kPuppets);
        // WWHD_COOP_GHOST_CYCLE=<steps>: every so many steps they all leave (their kits given back) and come again
        static const int cycle = getenv("WWHD_COOP_GHOST_CYCLE") ? atoi(getenv("WWHD_COOP_GHOST_CYCLE")) : 0;
        static int cycled = 0;
        if (cycle > 0 && ++cycled % cycle < 10) {  // (gone for 10 steps)
            for (int g = 0; g < n_ghosts; g++) release(g);
        } else
        for (int g = 0; g < n_ghosts; g++) {
            const Pose& q = g ? g_ghost[(g_ghost_at + 37 * g) % g_ghost.size()] : p;
            const float turn = 6.2831853f * g / n_ghosts, r = g ? 220.f : 0.f;
            const float gat[3] = {q.pos[0] + g_ghost_off[0] + r * std::cos(turn), q.pos[1] + g_ghost_off[1], q.pos[2] + g_ghost_off[2] + r * std::sin(turn)};
            // (WWHD_COOP_GHOST_SEE=1: every other one see-through, from the second)
            static const bool see = getenv("WWHD_COOP_GHOST_SEE") != nullptr;
            show(g, here ? &q : nullptr, gat, q.yaw, (tunic + g) & 3, see && (g & 1));
        }
        static int n = 0;
        // test aid: WWHD_COOP_HEAP_PROBE=1 traces the game heap's free bytes every 300 steps (a solid heap of all that
        // is free made and given back at once), to show the puppets take none of it
        if (static const bool probe = getenv("WWHD_COOP_HEAP_PROBE"); probe && n % 300 == 0) {
            const uint32_t r3 = c->r[3];
            if (const uint32_t h = guest_call(c, GC(0x025E3528), {0, 0})) {
                trace("coop: game heap free %u KB", heap_size(c, h) >> 10);
                guest_call(c, GC(0x025E3868), {h});
            }
            c->r[3] = r3;
        }
        if (++n % 150 == 0)
            trace("coop: ghost step %zu at %.0f %.0f %.0f, %zu models, %s", g_ghost_at, at[0], at[1], at[2], p.models.size(),
                  here ? "shown" : "another stage");
    }
    for (int i = 0; i < kPuppets; i++) {
        const Puppet& u = g_puppet[i];
        if (u.shown) g_kit[i].hidden = 0;
        else if (g_kit[i].release && ++g_kit[i].hidden >= 3) free_kit(c, i);
        if (!u.shown) continue;
        if (link) make_kit(c, i);
        for (const Model& m : u.pose.models) if (m.k == kHeld) make_held(c, i, m);
        // its chain: a hookshot of our own, made as its player's flies, kept (its execute waits, as Link's own
        // hookshot does in his hand: it draws nothing of itself), drawn as theirs (hook_025D4654)
        if (u.pose.chain && link && !(g_hook[i] && guest_call(c, GC(0x025DE50C), {g_hook[i]}))) {  // (fopAcM_SearchByID)
            const uint32_t a = items::spawn(c, link, kHookshot, 0, u.pos);
            g_hook[i] = a ? ld32(a + 4) : 0;
            trace("coop: puppet %d's hookshot %s", i, a ? "made" : "not made");
        }
        if (!u.pose.rope_pts.empty() && !g_rope[i].mat) g_rope[i] = make_rope(c);
    }
}

}  // namespace mods::coop

using namespace mods::coop;

// daPy_lk_c::createHeap: Link's models, then a copy of each for every puppet on his heap
extern "C" void f_023D5548_orig(Cpu* c);
// what the puppets hold and their ropes given back (mDoExt_destroySolidHeap) as Link is made again; his this (r3) kept
// around it (guest_call leaves its own result there: his createHeap then ran on that, a crash after a reload)
static void free_held(Cpu* c) {
    for (int i = 0; i < kPuppets; i++) free_kit(c, i);  // (keeps r3: Link's this)
}
extern "C" void hook_023D5548(Cpu* c) {
    free_held(c);
    g_link_files.clear();
    g_models.clear();
    g_have = false;
    g_in_heap = true;
    f_023D5548_orig(c);
    g_in_heap = false;
    g_wearer.clear();
    for (const LinkModel& m : g_models) g_wearer[m.model] = -1;
    find_clothes();
    g_body_parts = 0;
    for (size_t k = 0; k < g_models.size() && k < 64; k++) {  // (by his model files' names: cl, hands, katsura, ...)
        const uint32_t fmdl = ld32(g_models[k].data);
        std::string name;
        for (uint32_t t = fmdl + 4 + ld32(fmdl + 4); ld8(t); t++) name += (char)ld8(t);
        for (const char* n : kBodyParts) if (name == n) g_body_parts |= 1ull << k;
    }
    mods::trace("coop: Link made %zu models (the puppets' copies: their kits, as they are shown)", g_models.size());
}

// mDoExt_J3DModel__create(data, flags, diff flags): the ones Link's createHeap makes
extern "C" void f_025E38E0_orig(Cpu* c);
extern "C" void hook_025E38E0(Cpu* c) {
    const uint32_t data = c->r[3], flags = c->r[4], diff = c->r[5];
    f_025E38E0_orig(c);
    if (g_in_heap && c->r[3] && g_models.size() < kMaxModels) g_models.push_back({c->r[3], data, flags, diff, {}});
    if (c->r[3]) g_diff_of[data] = diff;
}

// daPy_lk_c::initModel(this, J3DModel**, file, differed-list flags): which file each of Link's models is
extern "C" void f_023D4BB8_orig(Cpu* c);
extern "C" void hook_023D4BB8(Cpu* c) {
    const uint32_t out = c->r[4], file = c->r[5], diff = c->r[6];
    f_023D4BB8_orig(c);
    if (const uint32_t model = ld32(out)) g_link_files[model] = {(uint16_t)file, diff};
}

extern "C" void f_023D8DF0_orig(Cpu* c);
extern "C" void f_025E2E5C_orig(Cpu* c);

// puppet i's copy of Link's model k, posed and entered as Link's own, each once a draw
static uint64_t g_entered[kPuppets];  // models entered this draw, by bit k
static void enter(Cpu* c, uint32_t link, int i, const Model& m) {
    const Puppet& u = g_puppet[i];
    uint32_t twin = 0;
    if (m.k == kHeld) {
        for (const Held& h : g_held[i]) if (h.file == m.file) twin = h.model;
    } else if (m.k < g_models.size() && m.k < 64 && !(g_entered[i] >> m.k & 1)) {
        g_entered[i] |= 1ull << m.k, twin = g_models[m.k].twin[i];
    }
    const uint32_t jnt = twin ? ld32(twin + kMdlJoints) : 0, mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    if (!mtx || ld16(jnt + kJntNum) != m.j.size()) return;
    float s, co;
    rot_y(u.yaw, s, co);
    for (size_t j = 0; j < m.j.size(); j++) put(m.j[j], u.pos, s, co, mtx + 48 * (uint32_t)j);
    // its base transform: T(pos) * Ry
    const float base[12] = {co, 0, s, u.pos[0], 0, 1, 0, u.pos[1], -s, 0, co, u.pos[2]};
    for (int e = 0; e < 12; e++) st32(twin + kMdlBase + 4 * e, bits(base[e]));
    // its packets' draw flags as Link's model has them (027F596C: model j3d::kDiffFlag into each shape packet)
    if (m.k != kHeld) {
        const uint32_t fl = ld32(g_models[m.k].model + j3d::kDiffFlag);
        st32(twin + j3d::kDiffFlag, fl), guest_call(c, GC(0x027F596C), {twin, fl});
    }
    // a bomb's fuse warning (its red): Link's bomb brk (py::kBombBrk, on the bomb's model data, which its copies share) at the
    // frame that player's bomb drew with, as daBomb_c::draw_norm (020C4398) sets it (mDoExt_brkAnm::entry 025E83FC)
    if (m.k == kHeld && m.brk >= 0) {
        const uint32_t r3 = c->r[3];
        c->f[1].ps0 = m.brk;
        guest_call(c, GC(0x025E83FC), {link + py::kBombBrk, ld32(twin + j3d::mModelData)});
        c->r[3] = r3;
    }
    // its materials shown as that player's were (his sword drawn: the blade), Link's put back after
    const uint32_t n = materials(twin);
    uint8_t was[32];
    const int ch = u.pose.flags >> 4 & 3;  // (that player's character: Link's models hidden)
    const uint32_t shown = ch && m.k != kHeld && body_part(m.k) ? 0 : m.shown;
    for (uint32_t k = 0; k < n; k++) was[k] = ld8(shown_flag(twin, k)), st8(shown_flag(twin, k), shown >> k & 1);
    // entryDLSetLight(Link, twin, 0): lit as Link, entered into the draw buffers current now, Link's. (Its other way,
    // 0259130C, enters into the actors' buffers, drawn after the sea and in the sea's state: the copies drew in one blue.)
    Red red(c, link, &u);
    const uint32_t r3 = c->r[3], r4 = c->r[4], r5 = c->r[5];
    c->r[3] = link, c->r[4] = twin, c->r[5] = 0;
    {
        SeeThrough see(i);
        f_023D8DF0_orig(c);
    }
    c->r[3] = r3, c->r[4] = r4, c->r[5] = r5;
    for (uint32_t k = 0; k < n; k++) st8(shown_flag(twin, k), was[k]);
    if (ch && m.k == g_body_k) enter_character(c, link, ch, i + 1, twin);
}
// Link's model just entered in his draw: taken into the pose, and each puppet's copy of it entered right after it
static void entered(Cpu* c, uint32_t link, uint32_t model) {
    const int k = index_of(model);
    if (k < 0) {
        if (const auto it = g_link_files.find(model); g_want && it != g_link_files.end()) take(g_taking.models, -1, &it->second, model, link);
        return;
    }
    if (g_want) take(g_taking.models, k, nullptr, model, link);
    if (k == g_body_k) enter_character(c, link, g_own_char, 0, model);
    for (int i = 0; i < kPuppets; i++)
        if (g_puppet[i].shown)
            for (const Model& m : g_puppet[i].pose.models) if (m.k == k) enter(c, link, i, m);
}
extern "C" void hook_023D8DF0(Cpu* c) {
    const uint32_t link = c->r[3], model = c->r[4];
    if (g_in_draw && g_own_char && body_part(index_of(model))) hidden(model, [&] { f_023D8DF0_orig(c); });
    else f_023D8DF0_orig(c);
    if (g_in_draw) entered(c, link, model);
}
extern "C" void f_023D8454_orig(Cpu* c);
extern "C" void hook_023D8454(Cpu* c) {
    const uint32_t link = c->r[3], model = c->r[4];
    if (g_in_draw && g_own_char && body_part(index_of(model))) hidden(model, [&] { f_023D8454_orig(c); });
    else f_023D8454_orig(c);
    if (g_in_draw) entered(c, link, model);
}

static void item_drawn(uint32_t model);
// mDoExt_modelEntryDL (025E2E5C) / modelUpdateDL (025E2DE0): Link's draw enters his body (mpCLModel) and a few others
// straight through these, without entryDLSetLight
extern "C" void hook_025E2E5C(Cpu* c) {
    const uint32_t model = c->r[3];
    if (g_in_draw && g_own_char && body_part(index_of(model))) hidden(model, [&] { f_025E2E5C_orig(c); });
    else f_025E2E5C_orig(c);
    if (g_in_draw) entered(c, g_link_drawing, model);
    else item_drawn(model);
}
extern "C" void f_025E2DE0_orig(Cpu* c);
extern "C" void hook_025E2DE0(Cpu* c) {
    const uint32_t model = c->r[3];
    if (g_in_draw && g_own_char && body_part(index_of(model))) hidden(model, [&] { f_025E2DE0_orig(c); });
    else f_025E2DE0_orig(c);
    if (g_in_draw) entered(c, g_link_drawing, model);
    else item_drawn(model);
}

// J3DModel's texture list of each material (027F50F8, in viewCalc): a puppet's copies in its player's tunic, Link's
// in this game's while a session is on
extern "C" void f_027F50F8_orig(Cpu* c);
extern "C" void hook_027F50F8(Cpu* c) {
    const uint32_t model = c->r[3];
    f_027F50F8_orig(c);
    const auto it = g_wearer.find(model);
    if (it == g_wearer.end()) return;
    const int t = it->second < 0 ? g_own_tunic : g_puppet[it->second].tunic;
    if (t < 0) return;
    using namespace j3d;
    for (uint32_t i = 0, n = ld16(model + kMatPacketNum); i < n; i++) {
        const uint32_t mi = ld32(model + kMatPackets) + i * kMatPacket, list = ld32(mi + kMatPacketTex), count = list ? ld8(ld32(mi) + kMatTexNum) : 0;
        for (uint32_t j = 0; j < count; j++)
            if (Clothes* k = clothes_of(ld32(list + 4 * j))) st32(list + 4 * j, worn(*k, t));
    }
}

// J3DModel's entry of one joint's materials (027F583C, r4 the joint): Link's draw enters his face (eyes, brows, mouth over
// his hair) this way (before his body), into its own buffers, and his arms as he aims the hookshot; each puppet's copy of
// that model the same, but without the joint's materials its whole entry shows (a packet entered twice in one draw breaks
// the draw lists; leaving out the whole joint for one of them left the puppets without faces)
extern "C" void hook_027F583C(Cpu* c) {
    const uint32_t model = c->r[3], joint = c->r[4];
    const int k = g_in_draw ? index_of(model) : -1;
    if (!g_own_char || !body_part(k)) f_027F583C_orig(c);
    if (k < 0 || k >= 64) return;
    const uint32_t r3 = c->r[3], r4 = c->r[4];
    for (int i = 0; i < kPuppets; i++)
        if (g_puppet[i].shown && g_models[k].twin[i] && !((g_puppet[i].pose.flags >> 4 & 3) && body_part(k)))
            for (const Model& m : g_puppet[i].pose.models)
                if (m.k == k) joint_without(model, joint, m.shown, [&] { SeeThrough see(i); c->r[3] = g_models[k].twin[i], c->r[4] = joint, f_027F583C_orig(c); });
    c->r[3] = r3, c->r[4] = r4;
}

// daPy_lk_c::draw: the pose taken; the puppets' models Link did not draw himself this time entered after him
extern "C" void f_023D9820_orig(Cpu* c);
extern "C" void hook_023D9820(Cpu* c) {
    const uint32_t link = c->r[3];
    if (g_want) g_taking = Pose{};
    for (uint64_t& e : g_entered) e = 0;
    g_in_draw = true, g_link_drawing = link, g_body_k = index_of(ld32(link + py::mpCLModel)), g_char_entered = 0;
    j3d_draw_guard::clear_marks();  // (this frame's see-through entries follow)
    g_viewed.clear();
    f_023D9820_orig(c);
    g_in_draw = false;
    const uint32_t ret = c->r[3];
    if (g_want && !g_taking.models.empty()) {
        memcpy(g_taking.stage, mem::ptr(kStage), 8);
        g_taking.room = (int8_t)ld8(link + kRoom);
        for (int i = 0; i < 3; i++) g_taking.pos[i] = f32(link + kPos + 4 * i);
        g_taking.yaw = (int16_t)ld16(link + kAngleY);
        g_taking.proc = (uint16_t)ld32(link + kCurProc), g_taking.equip = ld16(link + kEquip);
        using namespace swblur;
        if (const uint32_t blur = ld32(link + kSwBlur); blur && (int32_t)ld32(blur + mPointCount) > 0) {
            g_taking.trail = (int32_t)ld32(blur + mPointCount), g_taking.trail_idx = (int32_t)ld32(blur + mSampleIndex);
            g_taking.trail_color = (int32_t)ld32(blur + mBlurColorType), g_taking.trail_top = f32(blur + mBlurTopRate);
            float s, co;
            rot_y((int16_t)ld16(link + kAngleY), s, co);
            g_taking.trail_pts.resize(kTrailPts);
            for (uint32_t i = 0; i < kTrailPts / 3; i++) {
                const uint32_t v = blur + mTopPos + 12 * i;  // (mTopPos 0..59, then mBottomPos 0..59 right after)
                const float d[3] = {f32(v) - g_taking.pos[0], f32(v + 4) - g_taking.pos[1], f32(v + 8) - g_taking.pos[2]};
                const float l[3] = {co * d[0] - s * d[2], d[1], s * d[0] + co * d[2]};
                for (int e = 0; e < 3; e++) g_taking.trail_pts[3 * i + e] = (int16_t)std::clamp<long>(std::lround(l[e] * 4), -32768, 32767);
            }
        }
        g_taking.flags = (uint8_t)(g_own_char << 4 | (g_fly.left ? kFlying : 0) | (flashing(link) ? kHurt : 0));
        // in first person Link draws no head (nor what his head holds): the others see the models he drew before, as
        // they are now (their joints are worked out all the same)
        static uint64_t drawn_before = 0;
        uint64_t drawn = 0;
        for (const Model& m : g_taking.models) if (m.k < 64) drawn |= 1ull << m.k;
        if (!mods::first_person()) drawn_before = drawn;
        else
            for (size_t k = 0; k < g_models.size() && k < 64; k++)
                if ((drawn_before & ~drawn) >> k & 1) take(g_taking.models, (int)k, nullptr, g_models[k].model, link);
        for (Model& m : g_item_models)  // (not one he draws himself: a puppet's copy of a file is entered once a draw)
            if (std::none_of(g_taking.models.begin(), g_taking.models.end(), [&](const Model& t) { return t.k == kHeld && t.file == m.file; }))
                g_taking.models.push_back(std::move(m));
        g_item_models.clear();
        g_taking.chain = g_lines.chain, g_taking.rope_width = g_lines.rope_width, g_taking.rope_pts = std::move(g_lines.rope_pts);
        memcpy(g_taking.chain_root, g_lines.chain_root, 6), memcpy(g_taking.chain_angle, g_lines.chain_angle, 4);
        g_lines = Pose{};
        g_pose = std::move(g_taking), g_have = true, g_want = false;
    }
    for (int i = 0; i < kPuppets; i++)
        if (g_puppet[i].shown)
            for (const Model& m : g_puppet[i].pose.models) enter(c, link, i, m);
    // their sword trails: each puppet's own trail object (made once as the game makes Link's: 0240F564 on 0x1243C bytes),
    // its points from the pose, entered as Link's draw enters his (0252F3B0 into the z-sorted translucent list)
    static uint32_t blurs[kPuppets];
    for (int i = 0; i < kPuppets; i++) {
        const Puppet& u = g_puppet[i];
        const uint32_t mine = ld32(link + kSwBlur);
        if (!u.shown || u.pose.trail <= 0 || u.pose.trail_pts.size() != kTrailPts || !mine) continue;
        uint32_t& b = blurs[i];
        using namespace swblur;
        if (!b) b = mem::host_alloc(swblur::kSize, 0x20), guest_call(c, GC(0x0240F564), {b});
        st32(b + mPointCount, (uint32_t)u.pose.trail), st32(b + mSampleIndex, (uint32_t)u.pose.trail_idx), st32(b + mBlurColorType, (uint32_t)u.pose.trail_color);
        st32(b + mBlurTopRate, bits(u.pose.trail_top)), st32(b + mpPosBuffer, ld32(mine + mpPosBuffer));
        float s, co;
        rot_y(u.yaw, s, co);
        for (uint32_t k = 0; k < kTrailPts / 3; k++) {
            const float l[3] = {u.pose.trail_pts[3 * k] / 4.f, u.pose.trail_pts[3 * k + 1] / 4.f, u.pose.trail_pts[3 * k + 2] / 4.f};
            const float w[3] = {u.pos[0] + co * l[0] + s * l[2], u.pos[1] + l[1], u.pos[2] - s * l[0] + co * l[2]};
            for (int e = 0; e < 3; e++) st32(b + mTopPos + 12 * k + 4 * e, bits(w[e]));
        }
        const uint32_t gi = guest_call(c, GC(0x025200D4));
        guest_call(c, GC(0x0252F3B0), {gi + inf::kDrawlist, ld32(gi + inf::kDrawlist + dlst::mpXluList), b, b + mBottomPos});
    }
    // their ropes: each puppet's own line, its points from the pose, updated and set as daHimo2's draw does (025EC62C
    // with the rope's colour and Link's light; 025EDD04 into the line packet, entered into the actors' list once a draw)
    for (int i = 0; i < kPuppets; i++) {
        const Puppet& u = g_puppet[i];
        const uint32_t mat = g_rope[i].mat, n = (uint32_t)u.pose.rope_pts.size() / 3;
        if (!u.shown || n < 2 || mat <= 1) continue;
        const uint32_t pts = ld32(ld32(mat + linemat::mpLines));
        for (uint32_t k = 0; k < n; k++) from_pose(&u.pose.rope_pts[3 * k], u.pos, u.yaw, pts + 12 * k);
        c->f[1].ps0 = u.pose.rope_width;
        guest_call(c, GC(0x025EC62C), {mat, n, GD(0x101B6E14), 0, link + ac::tevStr});
        const uint32_t gi = guest_call(c, GC(0x025200D4)), dl = gi + inf::kDrawlist, lists = GD(0x104B4634);
        const uint32_t packet = dl + dlst::m3DlineMatSortPacket + dlst::kLinePacket;
        bool in = false;  // (entered twice, a line would follow itself: the packet's draw would never end)
        for (uint32_t m = ld32(packet + dlst::kLinePacketFirst); m && !in; m = ld32(m)) in = m == mat;
        if (in) continue;
        const uint32_t was[2] = {ld32(lists), ld32(lists + 4)};
        st32(lists, ld32(dl + dlst::mpOpaListP1)), st32(lists + 4, ld32(dl + dlst::mpOpaListPlayerExtra));  // (into the actors' lists, as daHimo2 puts it)
        guest_call(c, GC(0x025EDD04), {packet, mat});
        st32(lists, was[0]), st32(lists + 4, was[1]);
    }
    c->r[3] = ret;
}

// one of Link's items' models entered as it draws (coop_items.h): into his next pose, as a model he holds
// the boat he rides (daPy_lk_c procedures SHIP_READY..SHIP_RESTART but its getting off, as extras.cpp's riding): its
// models go with his pose as an item's, so each player sails their own boat on the others' screens too
static bool his_boat(uint32_t a) {
    const uint32_t link = ld32(kPlay + inf::kLink), proc = link ? ld32(link + kCurProc) : 0;
    return a && a == ld32(kPlay + inf::kShip) && proc >= 0x86 && proc <= 0x91 && proc != 0x90;
}
static void item_drawn(uint32_t model) {
    if (!g_drawing || (!items::mine(g_drawing) && !his_boat(g_drawing))) return;  // (items draw after Link: kept for his next pose)
    const uint32_t data = ld32(model + j3d::mModelData), link = ld32(kPlay + inf::kLink);
    const auto file = g_link_res.find(data);
    const auto diff = g_diff_of.find(data);
    if (file == g_link_res.end() || diff == g_diff_of.end() || !link) return;
    const Made made{file->second, diff->second};
    std::erase_if(g_item_models, [&](const Model& m) { return m.file == made.file; });
    take(g_item_models, -1, &made, model, link);
    // a bomb (not a Bomb Flower: daBomb_c mType 1) as its draw_norm left Link's bomb brk: its fuse warning
    if (items::mine(g_drawing) == 0x126 && ld32(g_drawing + bomb::mType) != 1 && !g_item_models.empty() && g_item_models.back().file == made.file)
        g_item_models.back().brk = f32(link + py::kBombBrk + 4);  // (its frame)
}
extern "C" void f_025D4654_orig(Cpu* c);
// one of Link's items drawn: the hookshot's chain (its links mChainCnt, angles shape_angle, from his hand: Link's
// mHookshotRootPos) and the grappling hook's rope (its points as its draw left them: count m1F6C, line m1F30) into his
// next pose
static void item_lines(uint32_t a, int16_t item, uint32_t n_was) {
    const uint32_t link = ld32(kPlay + inf::kLink);
    if (item == kHookshot && link) {
        g_lines.chain = (uint16_t)std::clamp<int32_t>((int32_t)ld32(a + kChainN), 0, 300);
        to_pose(link, link + kRoot, g_lines.chain_root);
        g_lines.chain_angle[0] = (int16_t)ld16(a + kChainAngle);
        g_lines.chain_angle[1] = (int16_t)(ld16(a + kChainAngle + 2) - ld16(link + kAngleY));
    } else if (item == kHimo2) {
        const uint32_t n = ld32(a + kRopeN);
        if (n == ~0u) return st32(a + kRopeN, n_was);  // (not drawn)
        g_lines.rope_pts.resize(3 * std::min<uint32_t>(n, 200));
        bool in = link;  // (as it is made its points are far off: no rope)
        for (uint32_t k = 0, pts = ld32(ld32(a + kRopeLines)); in && k < g_lines.rope_pts.size() / 3; k++)
            in = to_pose(link, pts + 12 * k, &g_lines.rope_pts[3 * k]);
        if (!in) g_lines.rope_pts.clear();
        g_lines.rope_width = f32(GD(0x10464674));
    }
}
// puppet i's hookshot drawn as its player's chain: its links and angles, and Link's hand put where theirs is for it
static void draw_chain(Cpu* c, int i, uint32_t a) {
    const Puppet& u = g_puppet[i];
    const uint32_t link = ld32(kPlay + inf::kLink);
    if (!u.shown || !u.pose.chain || !link) { c->r[3] = 1; return; }
    st32(a + kChainN, u.pose.chain);  // (its list draw reads them again: left as they are)
    st16(a + kChainAngle, (uint16_t)u.pose.chain_angle[0]), st16(a + kChainAngle + 2, (uint16_t)(u.pose.chain_angle[1] + u.yaw));
    uint32_t hand[3];
    for (int e = 0; e < 3; e++) hand[e] = ld32(link + kRoot + 4 * e);
    from_pose(u.pose.chain_root, u.pos, u.yaw, link + kRoot);
    for (int e = 0; e < 3; e++) st32(a + kPos + 4 * e, ld32(link + kRoot + 4 * e));  // (its light)
    f_025D4654_orig(c);
    for (int e = 0; e < 3; e++) st32(link + kRoot + 4 * e, hand[e]);
}
void mods::coop::actor_draw(Cpu* c) {  // fopAc_Draw(actor), from interp.cpp's hook
    const uint32_t a = c->r[3];
    for (int i = 0; i < kPuppets; i++) if (g_hook[i] && ld32(a + 4) == g_hook[i]) return draw_chain(c, i, a);
    const int16_t item = items::mine(a);
    const uint32_t n_was = item == kHimo2 ? ld32(a + kRopeN) : 0;
    if (item == kHimo2) st32(a + kRopeN, ~0u);  // (its draw sets it, if it draws)
    g_drawing = a;
    f_025D4654_orig(c);
    g_drawing = 0;
    if (item) item_lines(a, item, n_was);
}
// dComIfG_getObjectRes(res control, archive name (a string object: its text first), file): Link's archive's files, and
// his boat's (the King of Red Lions, "Ship": kShipFile on)
extern "C" void f_026066C4_orig(Cpu* c);
extern "C" void hook_026066C4(Cpu* c) {
    const uint32_t text = ld32(c->r[4]), file = c->r[5];
    const bool link = text && !memcmp(mem::ptr(text), "Link", 5), ship = text && !memcmp(mem::ptr(text), "Ship", 5);
    f_026066C4_orig(c);
    if ((link || ship) && c->r[3]) g_link_res[c->r[3]] = (uint16_t)(file | (ship ? kShipFile : 0));
}
