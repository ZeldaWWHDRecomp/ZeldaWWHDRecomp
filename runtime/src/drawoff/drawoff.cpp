// drawoff: the game thread's DrawDone wait off the critical path (decomp build, fast mode only). See drawoff.h.
#include "drawoff/drawoff.h"
#include "cheapib/render_replay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define WWHD_DRAWOFF_BACKTRACE 1
#endif
#endif

#include "fixed_seed.h"
#include "gfx/renderer.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include "write_watch.h"
#include "cheapib/cheapib_s2.h"  // (c2 with cheapib stage 2: a replayed pass's vertex writes in the copies)
#define XXH_INLINE_ALL
#include "../third_party/xxhash/xxhash.h"

namespace interp {
uint64_t pass_count();
bool hold_pass();
}  // namespace interp

#ifdef WWHD_NATIVE_DRAW
namespace native_draw::j3ddesc { extern thread_local bool t_capturing; bool game_draw_open(); }
#endif
namespace drawoff {
extern bool g_vertex_on;
uint32_t attrib_lr(uint64_t key);  // (below)
std::atomic<uint64_t> g_attrib_described{0};
extern std::atomic<uint64_t> g_attrib_calls[2], g_attrib_bytes[2];
namespace {

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int read_mode() {
#ifdef WWHD_DECOMP_FAST
    const char* e = cheapib::render_replay::env("WWHD_DRAWOFF");  // (the setting's set when not set: cheapib/render_replay.h)
    const int m = e ? atoi(e) : 0;
    if (m == 1) {
        LOG("[drawoff] WWHD_DRAWOFF=1 (c1): GX2DrawDone queues a fence, the game waits for it before the next painter");
        return m;
    }
    if (m == 2) {
        if (!render::vulkan()) {
            LOG("[drawoff] WWHD_DRAWOFF=2 (c2): off, the Vulkan renderer only (its vertex reads take the submitted copies)");
            return 0;
        }
        g_vertex_on = true;
        LOG("[drawoff] WWHD_DRAWOFF=2 (c2): one pass in flight (the flips may be one swap behind; the first GX2DrawDone "
            "of a pass waits for the previous pass's fence); dynamic vertex ranges copied at each list call");
        return m;
    }
    if (m) LOG("[drawoff] WWHD_DRAWOFF=%d: unknown mode, off", m);
#endif
    return 0;
}

bool readable(uint32_t addr, uint64_t size) {
    const uint64_t end = uint64_t(addr) + size;
    auto in = [&](uint64_t lo, uint64_t hi) { return addr >= lo && end <= hi; };
    return size && size <= (64u << 20) &&
           (in(mem::kMem2Start, mem::kMem2End) || in(mem::kRuntimeStart, mem::kRuntimeEnd) ||
            in(mem::kMem1, uint64_t(mem::kMem1) + mem::kMem1Size) || in(mem::kFgBucket, uint64_t(mem::kFgBucket) + mem::kFgBucketSize));
}
uint64_t content(uint32_t addr, uint64_t size) {
    return readable(addr, size) ? XXH3_64bits(mem::ptr(addr), size_t(size)) : 1;
}

// ---------------------------------------------------------------- c1 state (game thread)
struct C1 {
    uint64_t pending = 0;          // fence of the last deferred DrawDone not waited for yet
    uint64_t pending_pass = 0;     // the pass that issued it
    uint64_t linear_pass = 0;      // pass of the last linear-aligned colour buffer + 1 (0: none)
    bool in_frame = false;
    // counters (per report)
    uint64_t deferred = 0, full = 0, full_linear = 0, full_uncovered = 0, waits = 0, waited_done = 0, chained = 0;
    int64_t wait_ns = 0;
    uint64_t passes = 0, painter_points = 0;
};
C1 g;
std::atomic<uint64_t> g_linear_set{0};  // passes (+1) in which a linear target was set (any thread)
std::atomic<uint64_t> g_pass{0};        // interp's pass count, for the other threads (set at the pass entry)

// ---------------------------------------------------------------- vertex copies (any recording thread)
// The census's hazard class: vertex data the game double-buffers per pass (3D line packets, see vertex_source). With
// c1 the calc two passes later rewrites the half the render thread may still read, so the bytes the list is recorded
// with are copied into a ring of host-only guest memory and the command reads the copy. A pass's copies go into
// slice pass % 3; c1 waits for list P before painter P + 2, so the slice is free again at the entry of pass P + 3.
struct Ring {
    static constexpr uint32_t kSlice = 4u << 20, kSlices = 3;
    std::once_flag once;
    uint32_t base = 0;
    std::atomic<uint32_t> used[kSlices] = {};
    std::atomic<uint64_t> copies{0}, bytes{0}, overflow{0}, refused{0}, reused{0};
    std::mutex seen_mu;  // this pass's copies by (addr, size): a buffer bound again reuses its copy
    uint64_t seen_pass = ~0ull;
    std::unordered_map<uint64_t, uint32_t> seen;
    struct Made { uint32_t from, size; uint64_t pass, hash; };
    std::unordered_map<uint32_t, Made> made_at;  // census: ring address -> its copy (render-side check)
    std::atomic<uint64_t> ring_reads{0}, ring_bad{0}, ring_unknown{0};
    std::atomic<uint64_t> uncovered_pass{0};  // a pass (+1) whose copy did not fit: its next DrawDone drains
    // verify (WWHD_DRAWOFF_COPY_VERIFY=1, with the drain): the copies of a pass against guest memory once its list is done
    std::mutex mu;
    struct Copy { uint32_t from, to, size; uint64_t swap; };
    std::vector<Copy> made;
    uint64_t checked = 0, mismatched = 0;
};
Ring& ring() { static Ring r; return r; }
int read_copy_mode() {
#ifdef WWHD_DECOMP_FAST
    // WWHD_DRAWOFF_COPY: 1 copies with or without c1 (test: the census and verify see the copies with the drain), 0 never;
    // default: with c1
    if (const char* e = getenv("WWHD_DRAWOFF_COPY")) return atoi(e) ? 1 : 0;
    return -1;
#else
    return 0;
#endif
}
int copy_what() {  // debug: WWHD_DRAWOFF_COPY_WHAT bits: 1 GX2SetAttribBuffer, 2 packet instances (default 3)
    static const int w = [] { const char* e = getenv("WWHD_DRAWOFF_COPY_WHAT"); return e ? atoi(e) : 3; }();
    return w;
}
bool copy_verify() {
    static const bool on = [] { const char* e = getenv("WWHD_DRAWOFF_COPY_VERIFY"); return e && atoi(e) != 0; }();
    return on;
}

// ---------------------------------------------------------------- trace
struct Trace {
    uint64_t from = 0, to = 0;
    std::mutex mu;
    std::vector<std::string> rows;
    int64_t t0 = 0;
};
Trace& tr() { static Trace t; return t; }
bool tracing() {
    if (!g_trace) return false;
    const uint64_t p = interp::pass_count();
    return p >= tr().from && p < tr().to;
}
uint32_t thread_tag() {
    static std::atomic<uint32_t> next{1};
    static thread_local uint32_t tag = next.fetch_add(1);
    return tag;
}
void event(const char* what, uint32_t a = 0, uint32_t b = 0) {
    if (!tracing()) return;
    Trace& t = tr();
    char buf[160];
    const int64_t now = now_ns();
    std::lock_guard<std::mutex> lk(t.mu);
    snprintf(buf, sizeof buf, "%9.3f t%u %s %08X %X", (now - t.t0) / 1e6, thread_tag(), what, a, b);
    t.rows.push_back(buf);
}
void flush_trace(uint64_t pass) {
    Trace& t = tr();
    std::vector<std::string> rows;
    {
        std::lock_guard<std::mutex> lk(t.mu);
        rows.swap(t.rows);
        t.t0 = now_ns();
    }
    for (auto& r : rows) LOG("[drawoff trace] p%llu %s", (unsigned long long)pass, r.c_str());
}

// ---------------------------------------------------------------- census
struct Read { int kind; uint32_t tag; uint64_t hash; bool covered = false; };  // covered: by a c2 copy
struct Frame {
    uint64_t id = 0;
    std::unordered_map<uint64_t, Read> reads;  // (addr << 32 | size)
};
struct Census {
    std::mutex mu;
    Frame current;                      // render thread
    Frame done;                         // last completed (render thread writes, game thread takes)
    uint64_t frames = 0;
    // game thread
    bool armed = false, armed_c = false;  // (armed_c: point (c), the first DrawDone of the next pass: c2's window)
    uint64_t checked_c = 0, no_point_c = 0, last_dd_pass = 0;
    uint64_t hazcN[kKinds] = {}, hazcB[kKinds] = {};    // changed up to point (c), not covered by a c2 copy
    uint64_t covN[kKinds] = {}, covB[kKinds] = {};      // changed up to point (c), covered by a c2 copy
    std::unordered_map<uint64_t, uint64_t> hazards_c;   // key -> times (uncovered, all run)
    std::unordered_map<uint32_t, uint64_t> by_tag_c[kKinds];  // tag -> uncovered changed ranges (all run)
    Frame checking;                     // the frame checked this pass
    std::vector<uint64_t> hashA;        // its hashes at the pass entry
    std::vector<uint64_t> keys;
    uint64_t every = 300, sample = 1, passes = 0, checked = 0, stale = 0, no_point = 0;
    uint64_t readN[kKinds] = {}, readB[kKinds] = {};
    uint64_t preN[kKinds] = {}, preB[kKinds] = {};    // changed between the render read and the pass entry
    uint64_t hazN[kKinds] = {}, hazB[kKinds] = {};    // changed between the pass entry and the painter point
    std::unordered_map<uint64_t, uint64_t> hazards;  // key -> times (all run)
    std::unordered_map<uint64_t, uint32_t> hazard_kind;  // key -> kind | tag << 8
    std::unordered_map<uint64_t, uint64_t> by_tag[kKinds];  // tag -> hazard ranges (all run)
    uint64_t last_done = 0;
    int64_t ns = 0;
};
Census& cs() { static Census c; return c; }
const char* kind_name(int k) {
    switch (k) {
    case kVertex: return "vertex";
    case kIndex: return "index";
    case kUbo: return "ubo";
    case kTexture: return "texture";
    case kList: return "list";
    default: return "other";
    }
}
// the command that read a range: a packet draw's site (native_draw.cpp kSiteNames), else the op
std::string tag_name(uint32_t tag) {
    static const char* const sites[] = {"", "grass", "tree", "wood", "flower", "magma", "star", "housi", "wave", "moya",
                                        "vrkumo", "rain", "sea", "line", "jpa", "layout", "text", "post", "shadow",
                                        "aglquad", "aglpair", "sun"};
    char buf[32];
    if ((tag & 0xFF) == gx2::OP_PACKET_DRAW && (tag >> 8) < sizeof sites / sizeof *sites) snprintf(buf, sizeof buf, "packet-%s", sites[tag >> 8]);
    else snprintf(buf, sizeof buf, "op%X", tag & 0xFF);
    return buf;
}
std::string per_kind(const uint64_t* n, const uint64_t* b, uint64_t div) {
    std::string s;
    char buf[96];
    for (int k = 0; k < kKinds; ++k)
        if (n[k]) {
            snprintf(buf, sizeof buf, " %s %.1f/%.1f KB", kind_name(k), double(n[k]) / std::max<uint64_t>(div, 1),
                     double(b[k]) / 1024 / std::max<uint64_t>(div, 1));
            s += buf;
        }
    return s.empty() ? " none" : s;
}
void census_report() {
    Census& c = cs();
    LOG("[drawoff] census per %llu passes: lists checked %llu (stale %llu, no painter point %llu, %.2f ms per check); "
        "live reads per list (ranges/KB):%s; changed after the render read, before the pass entry:%s; HAZARD (written by the "
        "calc c1 overlaps):%s",
        (unsigned long long)c.passes, (unsigned long long)c.checked, (unsigned long long)c.stale, (unsigned long long)c.no_point,
        c.checked ? c.ns / 1e6 / double(c.checked) : 0.0, per_kind(c.readN, c.readB, c.checked).c_str(),
        per_kind(c.preN, c.preB, c.checked).c_str(), per_kind(c.hazN, c.hazB, c.checked).c_str());
    LOG("[drawoff] census c2 window (to the next pass's first DrawDone): lists checked %llu (no point %llu); changed and "
        "read from a c2 copy:%s; HAZARD for c2 (changed, read live):%s",
        (unsigned long long)c.checked_c, (unsigned long long)c.no_point_c, per_kind(c.covN, c.covB, c.checked_c).c_str(),
        per_kind(c.hazcN, c.hazcB, c.checked_c).c_str());
    if (!c.hazards_c.empty()) {
        std::vector<std::pair<uint64_t, uint64_t>> v(c.hazards_c.begin(), c.hazards_c.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::string s;
        char buf[96];
        for (size_t i = 0; i < v.size() && i < 24; ++i) {
            const uint32_t kt = c.hazard_kind[v[i].first];
            snprintf(buf, sizeof buf, " %s:%08X+%X@%s x%llu", kind_name(kt & 0xFF), uint32_t(v[i].first >> 32),
                     uint32_t(v[i].first), tag_name(kt >> 8).c_str(), (unsigned long long)v[i].second);
            s += buf;
        }
        LOG("[drawoff] census c2 hazard ranges (all run, %zu distinct):%s", v.size(), s.c_str());
        s.clear();
        for (int k = 0; k < kKinds; ++k)
            for (auto& [tag, n] : c.by_tag_c[k]) {
                snprintf(buf, sizeof buf, " %s@%s %llu", kind_name(k), tag_name(tag).c_str(), (unsigned long long)n);
                s += buf;
            }
        LOG("[drawoff] census c2 hazards by command (ranges, all run):%s", s.c_str());
    }
    c.checked_c = c.no_point_c = 0;
    memset(c.hazcN, 0, sizeof c.hazcN); memset(c.hazcB, 0, sizeof c.hazcB);
    memset(c.covN, 0, sizeof c.covN); memset(c.covB, 0, sizeof c.covB);
    if (!c.hazards.empty()) {
        std::vector<std::pair<uint64_t, uint64_t>> v(c.hazards.begin(), c.hazards.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::string s;
        char buf[80];
        for (size_t i = 0; i < v.size() && i < 24; ++i) {
            const uint32_t kt = c.hazard_kind[v[i].first];
            snprintf(buf, sizeof buf, " %s:%08X+%X@%s(lr %08X) x%llu", kind_name(kt & 0xFF), uint32_t(v[i].first >> 32),
                     uint32_t(v[i].first), tag_name(kt >> 8).c_str(), attrib_lr(v[i].first), (unsigned long long)v[i].second);
            s += buf;
        }
        LOG("[drawoff] census hazard ranges (all run, %zu distinct):%s", v.size(), s.c_str());
        s.clear();
        for (int k = 0; k < kKinds; ++k)
            for (auto& [tag, n] : c.by_tag[k]) {
                snprintf(buf, sizeof buf, " %s@%s %llu", kind_name(k), tag_name(tag).c_str(), (unsigned long long)n);
                s += buf;
            }
        LOG("[drawoff] census hazards by command (ranges, all run):%s", s.c_str());
    }
    LOG("[drawoff] census GX2SetAttribBuffer per pass: top level %.1f calls %.1f KB, recording %.1f calls %.1f KB",
        g_attrib_calls[0].exchange(0) / double(c.passes), g_attrib_bytes[0].exchange(0) / 1024.0 / double(c.passes),
        g_attrib_calls[1].exchange(0) / double(c.passes), g_attrib_bytes[1].exchange(0) / 1024.0 / double(c.passes));
    c.passes = c.checked = c.stale = c.no_point = 0;
    c.ns = 0;
    memset(c.readN, 0, sizeof c.readN); memset(c.readB, 0, sizeof c.readB);
    memset(c.preN, 0, sizeof c.preN); memset(c.preB, 0, sizeof c.preB);
    memset(c.hazN, 0, sizeof c.hazN); memset(c.hazB, 0, sizeof c.hazB);
}
std::atomic<uint64_t> g_game_swaps{0};
// point (a), a DrawDone's return once the render thread's frames have caught up with the game's swaps: with the drain,
// the pass's second DrawDone (after its swap), where the list called at the end of the pass before is done (the frame
// its swap ended). c1 lets the calc after this point overlap that list.
void census_arm() {
    Census& c = cs();
    const int64_t t0 = now_ns();
    {
        std::lock_guard<std::mutex> lk(c.mu);
        if (c.done.id == 0 || c.done.id == c.last_done || c.done.id != g_game_swaps.load()) return;
        if (c.sample > 1 && c.done.id % c.sample) { c.last_done = c.done.id; return; }
        c.last_done = c.done.id;
        c.checking.id = c.done.id;
        c.checking.reads.swap(c.done.reads);
        c.done.reads.clear();
    }
    c.keys.clear();
    c.hashA.clear();
    for (auto& [key, r] : c.checking.reads) {
        const uint32_t addr = uint32_t(key >> 32), size = uint32_t(key);
        const uint64_t h = content(addr, size);
        c.keys.push_back(key);
        c.hashA.push_back(h);
        c.readN[r.kind]++;
        c.readB[r.kind] += size;
        if (h != r.hash) { c.preN[r.kind]++; c.preB[r.kind] += size; }
    }
    c.armed = true;
    c.armed_c = true;
    c.ns += now_ns() - t0;
}
// point (b), where c1 waits
void census_pass() {
    Census& c = cs();
    if (++c.passes >= c.every) census_report();
}
// point (c): the first DrawDone of a pass, before its wait; with c2 the list checked since point (a) may still be read
// by the render thread up to here
void census_point_c() {
    Census& c = cs();
    const uint64_t pass = interp::pass_count() + 1;
    if (c.last_dd_pass == pass) return;
    c.last_dd_pass = pass;
    if (!c.armed_c) { c.no_point_c++; return; }
    c.armed_c = false;
    const int64_t t0 = now_ns();
    for (size_t i = 0; i < c.keys.size(); ++i) {
        const uint64_t key = c.keys[i];
        const uint32_t addr = uint32_t(key >> 32), size = uint32_t(key);
        if (content(addr, size) == c.hashA[i]) continue;
        const Read& r = c.checking.reads[key];
        if (r.covered) { c.covN[r.kind]++; c.covB[r.kind] += size; continue; }
        c.hazcN[r.kind]++;
        c.hazcB[r.kind] += size;
        c.hazards_c[key]++;
        c.by_tag_c[r.kind][r.tag]++;
        c.hazard_kind[key] = uint32_t(r.kind) | (r.tag << 8);
    }
    c.checked_c++;
    c.ns += now_ns() - t0;
}
void census_point() {
    Census& c = cs();
    if (!c.armed) { c.no_point++; return; }
    c.armed = false;
    const int64_t t0 = now_ns();
    for (size_t i = 0; i < c.keys.size(); ++i) {
        const uint64_t key = c.keys[i];
        const uint32_t addr = uint32_t(key >> 32), size = uint32_t(key);
        if (content(addr, size) == c.hashA[i]) continue;
        const Read& r = c.checking.reads[key];
        const int kind = r.kind;
        c.hazN[kind]++;
        c.hazB[kind] += size;
        c.hazards[key]++;
        c.hazard_kind[key] = uint32_t(kind) | (r.tag << 8);
        c.by_tag[kind][r.tag]++;
    }
    c.checked++;
    c.ns += now_ns() - t0;
}

}  // namespace

thread_local uint32_t g_tag = 0;
bool g_census = [] {
#ifdef WWHD_DECOMP_FAST
    const char* e = getenv("WWHD_DRAWOFF_CENSUS");
    if (e && atoi(e)) {
        if (const char* n = getenv("WWHD_DRAWOFF_CENSUS_EVERY")) cs().every = std::max(1, atoi(n));
        cs().sample = std::max(1, atoi(e));
        LOG("[drawoff] hazard census on (every %llu. list, report every %llu passes)", (unsigned long long)cs().sample,
            (unsigned long long)cs().every);
        return true;
    }
#endif
    return false;
}();
bool g_trace = [] {
#ifdef WWHD_DECOMP_FAST
    const char* e = getenv("WWHD_DRAWOFF_TRACE");
    unsigned long long a = 0, n = 0;
    if (e && sscanf(e, "%llu:%llu", &a, &n) == 2 && n) {
        tr().from = a;
        tr().to = a + n;
        return true;
    }
#endif
    return false;
}();

int mode() {
    static const int m = read_mode();
    return m;
}
bool copies_on() {
    static const bool on = [] {
        const int c = read_copy_mode();
        return c < 0 ? mode() == 1 : c != 0;
    }();
    return on;
}


// ---------------------------------------------------------------- c2: vertex ranges per list, copied at the call
namespace {
struct VRange { uint32_t addr, size; };
// what each recorded list reads of dynamic vertex data (its own binds + the lists it calls), by its start address
struct VIndex {
    std::mutex mu;
    struct List { uint32_t size = 0; std::vector<VRange> ranges; std::vector<uint32_t> calls; };
    std::unordered_map<uint32_t, List> lists;
};
VIndex& vindex() { static VIndex v; return v; }
thread_local std::vector<VRange> t_vranges;  // the list this thread records: its dynamic vertex binds
thread_local std::vector<uint32_t> t_vcalls;  // ... and the lists it calls
// A call's copies: sorted ranges (merged where they overlap) and their bytes
struct VSnap {
    std::vector<uint32_t> addr, end, off;
    std::vector<uint8_t> bytes;
    void clear() { addr.clear(); end.clear(); off.clear(); bytes.clear(); }
    const uint8_t* find(uint32_t a, uint32_t size) const {
        auto it = std::upper_bound(addr.begin(), addr.end(), a);
        if (it == addr.begin()) return nullptr;
        const size_t i = size_t(it - addr.begin()) - 1;
        if (uint64_t(a) + size > end[i]) return nullptr;
        return bytes.data() + off[i] + (a - addr[i]);
    }
};
std::mutex g_vfree_mu;
std::vector<VSnap*> g_vfree;
VSnap* vsnap_new() {
    {
        std::lock_guard<std::mutex> lk(g_vfree_mu);
        if (!g_vfree.empty()) { VSnap* v = g_vfree.back(); g_vfree.pop_back(); v->clear(); return v; }
    }
    return new VSnap;
}
void vsnap_free(VSnap* v) {
    if (!v) return;
    std::lock_guard<std::mutex> lk(g_vfree_mu);
    g_vfree.push_back(v);
}
// copies the ranges (any order, overlaps merged) as they are now
VSnap* vsnap_build(std::vector<VRange>& r);
std::atomic<uint64_t> g_v_calls{0}, g_v_snaps{0}, g_v_bytes{0}, g_v_ranges{0}, g_v_now{0}, g_v_now_bytes{0}, g_v_unread{0};
std::atomic<uint64_t> g_v_hits{0}, g_v_misses{0};
constexpr uint32_t kVMax = 8u << 20;  // a larger range stays live (counted as unread)
bool vreadable(uint32_t addr, uint64_t size) {
    const uint64_t end = uint64_t(addr) + size;
    auto in = [&](uint64_t lo, uint64_t hi) { return addr >= lo && end <= hi; };
    return size && size <= kVMax &&
           (in(mem::kMem2Start, mem::kMem2End) || in(mem::kRuntimeStart, mem::kRuntimeEnd) ||
            in(mem::kMem1, uint64_t(mem::kMem1) + mem::kMem1Size) || in(mem::kFgBucket, uint64_t(mem::kFgBucket) + mem::kFgBucketSize));
}
VSnap* vsnap_build(std::vector<VRange>& r) {
    std::sort(r.begin(), r.end(), [](const VRange& a, const VRange& b) { return a.addr != b.addr ? a.addr < b.addr : a.size > b.size; });
    VSnap* v = vsnap_new();
    for (const VRange& x : r) {
        if (!vreadable(x.addr, x.size)) { g_v_unread++; continue; }
        const uint64_t e = uint64_t(x.addr) + x.size;
        if (!v->addr.empty() && x.addr <= v->end.back()) {  // overlaps (or touches) the previous: one range
            if (e > v->end.back()) v->end.back() = uint32_t(e);
            continue;
        }
        v->addr.push_back(x.addr);
        v->end.push_back(uint32_t(e));
    }
    size_t total = 0;
    v->off.resize(v->addr.size());
    for (size_t i = 0; i < v->addr.size(); ++i) { v->off[i] = uint32_t(total); total += (v->end[i] - v->addr[i] + 15) & ~15u; }
    v->bytes.resize(total + 16);
    for (size_t i = 0; i < v->addr.size(); ++i) {
        std::memcpy(v->bytes.data() + v->off[i], mem::ptr(v->addr[i]), v->end[i] - v->addr[i]);
        if (g_vertex_on) cheapib::c2_vtx_patch(v->addr[i], v->end[i], v->bytes.data() + v->off[i]);  // (cheapib's replayed writes)
    }
    g_v_ranges += v->addr.size();
    g_v_bytes += total;
    return v;
}
// render thread
VSnap* g_v_active = nullptr;              // the call being executed
std::vector<VSnap*> g_v_overlay;          // copies of binds outside lists, this frame (newest last)
// c2 state (game thread)
struct C2 {
    uint64_t last_fence = 0, last_pass = 0;
    uint64_t dd = 0, capped = 0, cap_done = 0, full = 0, vsync_free = 0, status_ahead = 0;
    int64_t cap_ns = 0;
};
C2 g2;
}  // namespace
bool g_vertex_on = false;
// the census (with copies off) notes and copies like c2 without reading the copies: its reads are classified by them
bool c2_notes() { return c2() || (g_census && !copies_on()); }
bool lists_on() { return copies_on() || c2_notes(); }
void* vertex_snapshot(uint32_t list, uint32_t size) {
    if (!c2_notes()) return nullptr;
    g_v_calls++;
    static thread_local std::vector<VRange> ranges;
    static thread_local std::vector<uint32_t> stack;
    static thread_local std::unordered_map<uint32_t, bool> seen;
    ranges.clear(); stack.clear(); seen.clear();
    {
        VIndex& ix = vindex();
        std::lock_guard<std::mutex> lk(ix.mu);
        auto top = ix.lists.find(list);
        if (top == ix.lists.end() || size > top->second.size) return nullptr;
        stack.push_back(list);
        while (!stack.empty()) {  // the list and every list it calls, to any depth
            const uint32_t l = stack.back();
            stack.pop_back();
            if (!seen.emplace(l, true).second) continue;
            auto it = ix.lists.find(l);
            if (it == ix.lists.end()) continue;
            // the list's body too: the game reuses a list's memory for a later pass's recording (agl's command
            // rings, sead's per-frame lists), which with c2 can come before the render thread has executed it
            if (c2_notes() && it->second.size) ranges.push_back({l, it->second.size});
            ranges.insert(ranges.end(), it->second.ranges.begin(), it->second.ranges.end());
            for (uint32_t c : it->second.calls) stack.push_back(c);
        }
    }
    if (ranges.empty()) return nullptr;
    g_v_snaps++;
    return vsnap_build(ranges);
}
void* vertex_snapshot_now(uint32_t addr, uint32_t size) {
    if (!c2_notes() || !addr || !size) return nullptr;
    std::vector<VRange> r{{addr, size}};
    g_v_now++;
    g_v_now_bytes += size;
    return vsnap_build(r);
}
void nested_call(uint32_t list) {
    if (c2_notes() && t_vcalls.size() < 4096) t_vcalls.push_back(list);
}
void render_vertex_snapshot(void* s) {
    vsnap_free(g_v_active);
    g_v_active = static_cast<VSnap*>(s);
}
void render_vertex_overlay(void* s) {
    if (s) g_v_overlay.push_back(static_cast<VSnap*>(s));
}
const uint8_t* vertex_bytes(uint32_t addr, uint32_t size) {
    if (g_v_active)
        if (const uint8_t* b = g_v_active->find(addr, size)) { g_v_hits.fetch_add(1, std::memory_order_relaxed); return b; }
    for (size_t i = g_v_overlay.size(); i-- > 0;)
        if (const uint8_t* b = g_v_overlay[i]->find(addr, size)) { g_v_hits.fetch_add(1, std::memory_order_relaxed); return b; }
    return nullptr;
}
bool vertex_addr_of(const void* p, uint32_t size, uint32_t& addr) {
    const uint8_t* q = static_cast<const uint8_t*>(p);
    auto in = [&](const VSnap* v) {
        if (!v || v->bytes.empty() || q < v->bytes.data() || q + size > v->bytes.data() + v->bytes.size()) return false;
        const uint32_t o = uint32_t(q - v->bytes.data());
        auto it = std::upper_bound(v->off.begin(), v->off.end(), o);
        if (it == v->off.begin()) return false;
        const size_t i = size_t(it - v->off.begin()) - 1;
        const uint64_t a = uint64_t(v->addr[i]) + (o - v->off[i]);
        if (a + size > v->end[i]) return false;
        addr = uint32_t(a);
        return true;
    };
    if (in(g_v_active)) return true;
    for (size_t i = g_v_overlay.size(); i-- > 0;)
        if (in(g_v_overlay[i])) return true;
    return false;
}
static void c2_render_swap() {  // the frame's binds outside lists are done
    for (VSnap* v : g_v_overlay) vsnap_free(v);
    g_v_overlay.clear();
}
// a dynamic vertex range read by the list this thread records (c2)
static void note_vertex(uint32_t addr, uint32_t size) {
    if (t_vranges.size() < 65536) t_vranges.push_back({addr, size});
}

namespace {
thread_local uint32_t t_list = 0;     // the display list this thread records
thread_local bool t_list_copies = false;
std::mutex g_lists_mu;
std::unordered_map<uint32_t, uint64_t> g_list_pass;  // list start -> the pass that recorded it with copies
std::atomic<uint64_t> g_recalled{0};
}  // namespace
std::atomic<uint64_t> g_game_swaps_all{0};
uint32_t vertex_copy(uint32_t addr, uint32_t size) {
    Ring& r = ring();
    std::call_once(r.once, [&] {
        r.base = mem::host_alloc(Ring::kSlice * Ring::kSlices, 0x100);
        LOG("[drawoff] vertex copies: %u x %u KB of host-only guest memory at %08X", Ring::kSlices, Ring::kSlice >> 10, r.base);
    });
    if (!readable(addr, size) || size > Ring::kSlice / 4) { r.refused++; return addr; }
    const uint64_t pass = g_pass.load(std::memory_order_relaxed);
    const uint32_t slice = uint32_t(pass % Ring::kSlices), need = (size + 0xFF) & ~0xFFu;
    const uint64_t key = (uint64_t(addr) << 32) | size;
    {
        std::lock_guard<std::mutex> lk(r.seen_mu);
        if (r.seen_pass != pass) { r.seen.clear(); r.seen_pass = pass; }
        auto it = r.seen.find(key);
        if (it != r.seen.end()) { r.reused++; t_list_copies = true; return it->second; }
    }
    const uint32_t at = r.used[slice].fetch_add(need, std::memory_order_relaxed);
    if (at + need > Ring::kSlice) {
        r.overflow++;
        r.uncovered_pass.store(pass + 1, std::memory_order_relaxed);
        return addr;
    }
    const uint32_t to = r.base + slice * Ring::kSlice + at;
    {
        // a host write into guest memory: the renderers' caches (textures, the buffer cache) key ranges by address and
        // learn of writes through the write tracker; the ring's addresses repeat every three passes
        wwatch::HostWrite hw(to, size);
        std::memcpy(mem::ptr(to), mem::ptr(addr), size);
    }
    t_list_copies = true;
    {
        std::lock_guard<std::mutex> lk(r.seen_mu);
        if (r.seen_pass == pass) r.seen.emplace(key, to);
        if (g_census) r.made_at[to] = {addr, size, pass, XXH3_64bits(mem::ptr(to), size)};
    }
    r.copies++;
    r.bytes += size;
    if (copy_verify()) {
        std::lock_guard<std::mutex> lk(r.mu);
        r.made.push_back({addr, to, size, g_game_swaps_all.load() + 1});
    }
    return to;
}
// the verify: a copy made before swap S is checked against guest memory once the list called after swap S is done
// (with the drain: at the second DrawDone of the next pass, census point (a)); 0 mismatches = the bytes the list was
// recorded with are the bytes the render thread would have read
void copy_check(uint64_t done_swap) {
    Ring& r = ring();
    std::lock_guard<std::mutex> lk(r.mu);
    size_t keep = 0;
    for (auto& c : r.made) {
        if (c.swap + 1 > done_swap) { r.made[keep++] = c; continue; }
        if (c.swap + 1 == done_swap) {
            r.checked++;
            if (std::memcmp(mem::ptr(c.from), mem::ptr(c.to), c.size)) r.mismatched++;
        }
    }
    r.made.resize(keep);
}
uint32_t debug_addr() {
    static const uint32_t a = [] { const char* e = getenv("WWHD_DRAWOFF_DEBUG_ADDR"); return e ? uint32_t(strtoul(e, nullptr, 16)) : 0u; }();
    return a;
}
void debug_seen(const char* where, uint32_t cls, uint32_t addr, uint32_t size) {
    if (!debug_addr() || addr != debug_addr()) return;
    static std::atomic<int> left{6};
    if (left.fetch_sub(1) > 0)
        LOG("[drawoff] debug %s: class/lr %08X addr %08X size %X pass %llu recording %d", where, cls, addr, size,
            (unsigned long long)interp::pass_count(), int(gx2::recording_display_list()));
}
// the census: which guest caller set a vertex buffer (full draws), for its hazard report
std::mutex g_attrib_mu;
std::unordered_map<uint64_t, uint32_t> g_attrib_lr;  // (addr << 32 | size) -> lr
uint32_t attrib_lr(uint64_t key) {
    std::lock_guard<std::mutex> lk(g_attrib_mu);
    auto it = g_attrib_lr.find(key);
    return it == g_attrib_lr.end() ? 0xFFFFFFFFu : it->second;
}
std::atomic<uint64_t> g_attrib_calls[2], g_attrib_bytes[2];  // census: GX2SetAttribBuffer calls (top level, recording)
uint32_t attrib_source(uint32_t lr, uint32_t addr, uint32_t size) {
    debug_seen("GX2SetAttribBuffer", lr, addr, size);
    if (g_census) {
        std::lock_guard<std::mutex> lk(g_attrib_mu);
        g_attrib_lr[(uint64_t(addr) << 32) | size] = lr;
        const int rec = gx2::recording_display_list() ? 1 : 0;
        g_attrib_calls[rec]++;
        g_attrib_bytes[rec] += size;
    }
    if (c2_notes()) {  // (a bind outside lists: gx2_resources.cpp takes its copy now)
        if (!addr || !size || !gx2::recording_display_list()) return addr;
#ifdef WWHD_NATIVE_DRAW
        if (native_draw::j3ddesc::t_capturing || native_draw::j3ddesc::game_draw_open()) { g_attrib_described++; return addr; }
#endif
        note_vertex(addr, size);
        return addr;
    }
    if (!copies_on() || !addr || !size || !gx2::recording_display_list() || !(copy_what() & 1)) return addr;
#ifdef WWHD_NATIVE_DRAW
    // a J3D draw's bind (a description being defined, or any draw at a J3D site) stays: described draws keep their
    // register state across passes, so a ring address would outlive its slice (static model data; the census found
    // no J3D hazard)
    if (native_draw::j3ddesc::t_capturing || native_draw::j3ddesc::game_draw_open()) { g_attrib_described++; return addr; }
#endif
    return vertex_copy(addr, size);
}
// what is read from a copy while a display list is recorded: every buffer bound through GX2SetAttribBuffer (actor
// packets: one generic guest caller, J3DDrawBuffer::drawHead, so no finer rule) and the 3D line and sea packet
// instances (and text, seen once); the census found the game double-buffering these per pass. A buffer bound again in the same pass reuses
// its copy.
uint32_t vertex_source(uint32_t cls, uint32_t addr, uint32_t size) {
    debug_seen("packet instance", cls, addr, size);
    if (c2_notes()) {
        // every packet site's instance buffer (the census found sea, line and the cloud packets' (vrkumo) rewritten
        // per pass; the others are static and small)
        (void)cls;
        if (!addr || !size) return addr;
        if (gx2::recording_display_list()) note_vertex(addr, size);
        else gx2::drawoff_vertex_overlay(vertex_snapshot_now(addr, size));  // (packets drawn outside lists: the clouds)
        return addr;
    }
    if (!copies_on() || !addr || !size || !gx2::recording_display_list() || !(copy_what() & 2)) return addr;
    // packet instances: the sites the census found (3D lines, the sea, text: once at the Picto snap); the other packets'
    // instance buffers are static
    if (cls != kClassLine && cls != kClassSea && cls != kClassText) return addr;
    return vertex_copy(addr, size);
}
void list_begin(uint32_t start) {
    t_list = start;
    t_list_copies = false;
    t_vranges.clear();
    t_vcalls.clear();
}
void list_end(uint32_t size) {
    if (!t_list) return;
    if (c2_notes()) {  // the list's dynamic vertex ranges (and the lists it calls), by its start
        VIndex& ix = vindex();
        std::lock_guard<std::mutex> lk(ix.mu);
        auto& l = ix.lists[t_list];
        l.size = size;
        l.ranges.assign(t_vranges.begin(), t_vranges.end());
        l.calls.assign(t_vcalls.begin(), t_vcalls.end());
        t_vranges.clear();
        t_vcalls.clear();
        t_list = 0;
        return;
    }
    std::lock_guard<std::mutex> lk(g_lists_mu);
    if (t_list_copies) g_list_pass[t_list] = g_pass.load(std::memory_order_relaxed);
    else g_list_pass.erase(t_list);
    t_list = 0;
}
void list_call(uint32_t list) {
    if (!copies_on()) return;
    uint64_t pass;
    {
        std::lock_guard<std::mutex> lk(g_lists_mu);
        auto it = g_list_pass.find(list);
        if (it == g_list_pass.end()) return;
        pass = it->second;
    }
    const uint64_t now = g_pass.load(std::memory_order_relaxed);
    if (pass == now) return;
    g_recalled++;
    ring().uncovered_pass.store(now + 1, std::memory_order_relaxed);
}

static void c2_report();
void pass_entry() {
    const uint64_t pass = interp::pass_count();  // (the previous pass's number: interp counts after this point)
    if (copies_on()) {  // this pass's copy slice: free (see Ring)
        g_pass.store(pass + 1, std::memory_order_relaxed);
        ring().used[(pass + 1) % Ring::kSlices].store(0, std::memory_order_relaxed);
    }
    if (g_trace && pass >= tr().from && pass <= tr().to) flush_trace(pass);
    if (g_census) census_pass();
    if (copies_on() && (pass + 1) % 300 == 0) {
        Ring& r = ring();
        LOG("[drawoff] vertex copies per 300 passes: %llu (%.1f KB per pass), overflow %llu, refused %llu; verify %llu checked, "
            "%llu mismatched; lists called again in a later pass %llu; reused %llu; J3D description binds kept %llu",
            (unsigned long long)r.copies.exchange(0),
            r.bytes.exchange(0) / 1024.0 / 300, (unsigned long long)r.overflow.exchange(0), (unsigned long long)r.refused.exchange(0),
            (unsigned long long)r.checked, (unsigned long long)r.mismatched, (unsigned long long)g_recalled.exchange(0),
            (unsigned long long)r.reused.exchange(0), (unsigned long long)g_attrib_described.exchange(0));
        if (g_census) LOG("[drawoff] ring reads (census) %llu, bad %llu, no copy there %llu", (unsigned long long)r.ring_reads.load(),
                          (unsigned long long)r.ring_bad.load(), (unsigned long long)r.ring_unknown.load());
    }
    if (!on()) return;
    g.passes++;
    if (c2()) {
        if (g.passes % 300 == 0) c2_report();
        return;
    }
    if (g.passes % 300 == 0) {
        LOG("[drawoff] c1 per 300 passes: DrawDone deferred %llu, full %llu (linear target %llu, copy overflow %llu); painter "
            "points %llu, waits %llu (fence already done %llu, chained %llu), %.3f ms waited per pass",
            (unsigned long long)g.deferred, (unsigned long long)g.full, (unsigned long long)g.full_linear,
            (unsigned long long)g.full_uncovered,
            (unsigned long long)g.painter_points, (unsigned long long)g.waits, (unsigned long long)g.waited_done,
            (unsigned long long)g.chained, g.wait_ns / 1e6 / 300);
        g.full_uncovered = 0;
        g.deferred = g.full = g.full_linear = g.waits = g.waited_done = g.chained = g.painter_points = 0;
        g.wait_ns = 0;
    }
}
void painter_point(const char* why);
void frame_begin(bool hold) {
    g.in_frame = true;
    event(hold ? "frame-begin hold" : "frame-begin logic");
}
void frame_end() {
    g.in_frame = false;
    event("frame-end");
    painter_point("painter point (frame end)");  // (the sead render follows the per-frame function)
}
bool in_frame() { return g.in_frame; }

static void wait_pending(bool chained) {
    if (!g.pending) return;
    const uint64_t id = g.pending;
    g.pending = 0;
    g.waits++;
    if (chained) g.chained++;
    if (gx2::drawoff_fence_done(id)) { g.waited_done++; return; }
    const int64_t t0 = now_ns();
    event("c1-wait", uint32_t(id));
    gx2::drawoff_wait(id);
    event("c1-wait-done", uint32_t(id));
    g.wait_ns += now_ns() - t0;
}

static bool c2_draw_done();
bool defer_draw_done(uint32_t lr) {
    event("DrawDone", lr);
    if (!on()) return false;
    if (c2()) return c2_draw_done();
    // a linear-aligned colour buffer set this pass or the one before: the CPU may read it back after this DrawDone
    const uint64_t pass = interp::pass_count() + 1;
    const uint64_t lin = g_linear_set.load(std::memory_order_relaxed);
    if (lin && pass <= lin + 1) {
        wait_pending(true);
        g.full++;
        g.full_linear++;
        return false;
    }
    // a copy that did not fit (this pass or the one before): drained as today
    const uint64_t unc = ring().uncovered_pass.load(std::memory_order_relaxed);
    if (unc && pass <= unc + 1) {
        wait_pending(true);
        g.full++;
        g.full_uncovered++;
        return false;
    }
    // the pass's second DrawDone (after the swap) replaces its first: fences complete in order. One from an earlier
    // pass that no painter point waited for is waited for now (at most one pass's DrawDones outstanding).
    if (g.pending && g.pending_pass != pass) wait_pending(true);
    g.pending = gx2::drawoff_fence();
    g.pending_pass = pass;
    g.deferred++;
    return true;
}
void painter_point(const char* why) {
    event(why);
    if (g_census) census_point();
    if (!on() || c2()) return;  // (c2: no wait here)
    g.painter_points++;
    wait_pending(false);
}
// c2: the full waits of c1 (a linear target set this pass or the one before, a copy that did not fit), else the cap:
// the first DrawDone of a pass waits for the last fence of the pass before (the render thread at most one pass behind)
static bool c2_full_due(uint64_t pass) {
    const uint64_t lin = g_linear_set.load(std::memory_order_relaxed);
    return lin && pass <= lin + 1;
}
static void c2_wait(uint64_t id) {
    if (!id) return;
    g2.capped++;
    if (gx2::drawoff_fence_done(id)) { g2.cap_done++; return; }
    const int64_t t0 = now_ns();
    event("c2-cap-wait", uint32_t(id));
    gx2::drawoff_wait(id);
    event("c2-cap-done", uint32_t(id));
    g2.cap_ns += now_ns() - t0;
}
static bool c2_draw_done() {
    const uint64_t pass = interp::pass_count() + 1;
    g2.dd++;
    if (c2_full_due(pass)) {  // as today: the caller drains everything (render_sync)
        g2.full++;
        g2.last_fence = 0;
        return false;
    }
    if (g2.last_fence && g2.last_pass != pass) c2_wait(g2.last_fence);
    g2.last_fence = gx2::drawoff_fence();
    g2.last_pass = pass;
    return true;
}
uint32_t swaps_ahead() {
    if (!c2()) return 0;
    const uint64_t pass = interp::pass_count() + 1;
    return c2_full_due(pass) ? 0 : 1;
}
namespace {
int dump_by_swap_mode() {  // 0 off, 1 the swap's script frame, 2 that x 10 + the swap's index in it (each pass of a step),
                          // 3 the game's world clock (mTimer) at the swap x 10 + the swap's index in it
    static const int m = [] { const char* e = getenv("WWHD_DRAWOFF_DUMP_BY_SWAP"); return e ? atoi(e) : 0; }();
    return m;
}
bool dump_by_swap() { return dump_by_swap_mode() != 0; }
struct SwapScript { std::atomic<uint64_t> swap{0}, script{0}; };
SwapScript g_swap_script[256];  // (c2 keeps the render thread at most a pass or two behind)
}  // namespace
void note_swap_script(uint64_t swap, uint32_t timer) {
    if (!dump_by_swap()) return;
    SwapScript& e = g_swap_script[swap % 256];
    uint64_t script = dump_by_swap_mode() == 3 ? timer : fixed_seed::script_frame(swap);
    if (dump_by_swap_mode() >= 2) {
        static uint64_t last = ~0ull, index = 0;
        index = script == last ? index + 1 : 0;
        last = script;
        script = script * 10 + std::min<uint64_t>(index, 9);
    }
    e.script.store(script, std::memory_order_relaxed);
    e.swap.store(swap, std::memory_order_release);
}
uint64_t dump_script_frame(uint64_t frame, uint64_t live) {
    if (!dump_by_swap()) return live;
    const SwapScript& e = g_swap_script[frame % 256];
    if (e.swap.load(std::memory_order_acquire) != frame) return live;
    return e.script.load(std::memory_order_relaxed);
}
int frames_in_flight() { return c2() ? 1 : 0; }
int grid_keep_frames() {
    static const int n = [] {
        const char* e = getenv("WWHD_DRAWOFF_GRID_KEEP");
        const bool on = !e || atoi(e) != 0;
        if (c2()) LOG("[drawoff] c2 flips: %s (WWHD_DRAWOFF_GRID_KEEP=%d)", on ? "a flip up to a frame later than a vsync keeps the grid" : "one vsync late moves the grid (as before)", on ? 1 : 0);
        return on ? frames_in_flight() : 0;
    }();
    return n;
}
bool flip_time() {
    static const bool on = [] {
        const char* e = getenv("WWHD_DRAWOFF_FLIP_TIME");
        const bool v = c2() && (!e || atoi(e) != 0);
        if (c2()) LOG("[drawoff] c2 flip times: %s (WWHD_DRAWOFF_FLIP_TIME=%d)", v ? "a flip counts when its frame was rendered (or due), not when the game next looks" : "when the game next looks (as before)", v ? 1 : 0);
        return v;
    }();
    return on;
}
int64_t step_slot(int64_t slot, int64_t last, int64_t lead, int64_t step) {
    static const bool on = [] {
        const char* e = getenv("WWHD_DRAWOFF_SLOT_AHEAD");
        const bool v = c2() && (!e || atoi(e) != 0);
        if (c2()) LOG("[drawoff] c2 step slots: %s (WWHD_DRAWOFF_SLOT_AHEAD=%d)", v ? "a slot the last logic pass already used counts as the next one" : "as gx2 has it (as before)", v ? 1 : 0);
        return v;
    }();
    if (!on || last <= 0 || step <= 0) return slot;
    static uint64_t moved = 0;
    const int64_t s0 = slot;
    while (slot - lead < last + step / 2) slot += step;
    if (slot != s0 && (++moved == 1 || moved % 1000 == 0)) LOG("[drawoff] c2 step slot moved on by %.1f ms (%llu times)", (slot - s0) / 1e6, (unsigned long long)moved);
    return slot;
}
bool render_cost() {
    static const bool on = [] {
        const char* e = getenv("WWHD_DRAWOFF_RENDER_COST");
        const bool v = !e || atoi(e) != 0;
        if (c2()) LOG("[drawoff] c2 pass cost: %s (WWHD_DRAWOFF_RENDER_COST=%d)", v ? "waits for the render thread count, the in-between decision counts the pass" : "all flip waits left out (as before)", v ? 1 : 0);
        return v;
    }();
    return c2() && on;
}
static int64_t g_flip_wait_ns = 0;  // (game thread)
void add_flip_wait_ns(int64_t ns) { g_flip_wait_ns += ns; }
int64_t take_flip_wait_ns() {
    const int64_t v = g_flip_wait_ns;
    g_flip_wait_ns = 0;
    return c2() ? v : 0;
}
void c2_note(int what) {  // gx2_core: 0 a vsync wait skipped, 1 a swap status counted one in flight as flipped
    if (what == 0) g2.vsync_free++;
    else g2.status_ahead++;
}
static void c2_report() {
    const double n = 300;
    LOG("[drawoff] c2 per 300 passes: DrawDone %llu (full %llu), cap waits %llu (fence already done %llu), %.3f ms "
        "capped per pass; vsync waits skipped %llu, swap status one ahead %llu; vertex copies: %llu calls, %llu with "
        "copies, %.1f ranges and %.1f KB per pass, binds outside lists %llu (%.1f KB per pass), too large %llu; "
        "render reads from copies %llu",
        (unsigned long long)g2.dd, (unsigned long long)g2.full, (unsigned long long)g2.capped, (unsigned long long)g2.cap_done,
        g2.cap_ns / 1e6 / n, (unsigned long long)g2.vsync_free, (unsigned long long)g2.status_ahead,
        (unsigned long long)g_v_calls.exchange(0), (unsigned long long)g_v_snaps.exchange(0), g_v_ranges.exchange(0) / n,
        g_v_bytes.exchange(0) / 1024.0 / n, (unsigned long long)g_v_now.exchange(0), g_v_now_bytes.exchange(0) / 1024.0 / n,
        (unsigned long long)g_v_unread.exchange(0), (unsigned long long)g_v_hits.exchange(0));
    g2.dd = g2.full = g2.capped = g2.cap_done = g2.vsync_free = g2.status_ahead = 0;
    g2.cap_ns = 0;
}
void linear_target() {
    const uint64_t pass = interp::pass_count() + 1;
    if (g_linear_set.load(std::memory_order_relaxed) != pass) {
        g_linear_set.store(pass, std::memory_order_relaxed);
        event("linear-target");
    }
}
void note_call(uint32_t list, uint32_t size) { event("call", list, size); }
void note_begin_list(uint32_t lr) { event("begin-list", lr); }
void note_swap() {
    event("swap");
    g_game_swaps.fetch_add(1);
    g_game_swaps_all.fetch_add(1);
}
void census_dd() { census_point_c(); }
void draw_done_returned() {
    if (!g_census) return;
    const uint64_t before = cs().last_done;
    census_arm();
    if (copy_verify() && copies_on() && cs().last_done != before) copy_check(cs().last_done);  // (needs the census)
}

void read_slow(int kind, uint32_t addr, uint64_t size) {
    if (!addr || !size || size > (64u << 20)) return;
    if (ring().base && addr >= ring().base && addr < ring().base + Ring::kSlice * Ring::kSlices) {  // a copy read
        Ring& r = ring();
        std::lock_guard<std::mutex> lk(r.seen_mu);
        r.ring_reads++;
        auto it = r.made_at.find(addr);
        if (it == r.made_at.end()) { if (r.ring_unknown++ < 5) LOG("[drawoff] ring read %08X+%llX tag %X: no copy starts there", addr, (unsigned long long)size, g_tag); }
        else if (size > it->second.size || XXH3_64bits(mem::ptr(addr), it->second.size) != it->second.hash) {
            if (r.ring_bad++ < 8)
                LOG("[drawoff] ring read %08X+%llX tag %X: copy of %08X+%X from pass %llu (now %llu) %s", addr, (unsigned long long)size,
                    g_tag, it->second.from, it->second.size, (unsigned long long)it->second.pass,
                    (unsigned long long)g_pass.load(), size > it->second.size ? "read longer than the copy" : "overwritten");
        }
    }
    static const uint32_t debug_addr = [] { const char* e = getenv("WWHD_DRAWOFF_DEBUG_ADDR"); return e ? uint32_t(strtoul(e, nullptr, 16)) : 0u; }();
    if (debug_addr && addr == debug_addr) {  // debug: who reads this range (host stack, a few times)
        static int left = 3;
        if (left > 0) {
            left--;
#ifdef WWHD_DRAWOFF_BACKTRACE
            void* frames[24];
            const int n = backtrace(frames, 24);
            char** names = backtrace_symbols(frames, n);
            for (int i = 0; i < n; ++i) LOG("[drawoff] read %08X+%llX tag %X: %s", addr, (unsigned long long)size, g_tag, names ? names[i] : "?");
            free(names);
#else
            LOG("[drawoff] read %08X+%llX tag %X (no host backtrace on this platform)", addr, (unsigned long long)size, g_tag);
#endif
        }
    }
    if (kind == kUbo && gx2::submitted_uniform_covers(addr, uint32_t(size))) return;  // (served from the snapshot)
    Census& c = cs();
    const uint64_t key = (uint64_t(addr) << 32) | uint32_t(size);
    auto it = c.current.reads.find(key);
    if (it != c.current.reads.end()) return;  // (first read in the frame: the content the render thread used)
    c.current.reads.emplace(key, Read{kind, g_tag, content(addr, size), vertex_bytes(addr, uint32_t(size)) != nullptr});
}
void render_call(uint32_t list, uint32_t size, bool begin, int depth) {
    if (g_census && begin) read_slow(kList, list, size);
    if (depth == 0) event(begin ? "R call" : "R call-end", list, size);
}
void render_swap() {
    if (g_vertex_on || g_census) c2_render_swap();
    event("R swap");
    if (!g_census) return;
    Census& c = cs();
    std::lock_guard<std::mutex> lk(c.mu);
    c.current.id = ++c.frames;
    c.done.id = c.current.id;
    c.done.reads.swap(c.current.reads);
    c.current.reads.clear();
}
void render_fence(uint64_t id) { event("R fence", uint32_t(id)); }

}  // namespace drawoff
