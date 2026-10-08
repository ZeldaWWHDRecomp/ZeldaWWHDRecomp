#include <thread>
#include <chrono>
#include <atomic>
#ifndef _WIN32  // the boot-crash debug aids below are POSIX-only (macOS, Linux)
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
// Test aids for the true 60 fps conversions (debug only; nothing here runs unless its env var is set).
//
//   WWHD_TEST_POKE=t:ADDR:HEX,...   at scenario time t (s, game time; see input.mm) writes the bytes
//                                   HEX to guest memory at ADDR. ADDR is hex, or *PTR+OFF (the word
//                                   at PTR plus OFF), e.g. *101F84DC+2E:38 equips the Hero's Sword.
//   WWHD_LINK_WARP=step:x:y:z:angle  once, before Link's full-pass execute at origin step + step;
//                                   updates the actor and the HD executable's retained position.
//   WWHD_SAVEINFO_DUMP=path         at the end of the scenario (WWHD_TEST_END) writes the save-info
//                                   block (dSv_info_c, *101F84DC, 0x12A0 bytes: what the game saves,
//                                   plus the current stage/zone memory) to path; also at the times
//                                   in WWHD_SAVEINFO_AT=t1,t2,... to path.t1, path.t2, ...
//   WWHD_ACTOR_DUMP=path:FN:SIZE,...  after every execute of a process whose execute function is FN
//                                   (hex; "link" for daPy_lk_c) appends a record to path: header
//                                   'ADMP', u64 logic step, u32 full pass, f32 dt, u32 process,
//                                   u32 size, then SIZE raw (big-endian guest) bytes of the process.
//                                   tools/true60/field_rates.py compares such dumps between a 30 fps
//                                   and a true 60 run to find per-step state that is not converted.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "guest_addr.h"
#include "runtime.h"
#include "true60.h"
#include "savestate.h"
#include "input.h"

namespace interp { uint64_t logic_steps(); bool hold_pass(); }

namespace true60_test {
namespace {
struct Poke { double t; uint32_t ptr; bool deref; uint32_t off; std::vector<uint8_t> bytes; bool done = false; };
std::vector<Poke> parse_pokes() {
    std::vector<Poke> v;
    const char* e = getenv("WWHD_TEST_POKE");
    while (e && *e) {
        Poke p{};
        char* end;
        p.t = strtod(e, &end);
        if (*end != ':') break;
        e = end + 1;
        if (*e == '*') { p.deref = true; e++; }
        p.ptr = (uint32_t)strtoul(e, &end, 16);
        e = end;
        if (*e == '+') { p.off = (uint32_t)strtoul(e + 1, &end, 16); e = end; }
        if (*e != ':') break;
        e++;
        while (isxdigit((unsigned char)e[0]) && isxdigit((unsigned char)e[1])) {
            char b[3] = {e[0], e[1], 0};
            p.bytes.push_back((uint8_t)strtoul(b, nullptr, 16));
            e += 2;
        }
        v.push_back(p);
        if (*e != ',') break;
        e++;
    }
    return v;
}
const uint32_t kSaveInfoPtr = GD(0x101F84DC);  // dComIfGs save info (dSv_info_c), see dComIfGs_setSelectEquip 02522398
constexpr uint32_t kSaveInfoSize = 0x12A0;
void dump_saveinfo(const std::string& path) {
    uint32_t p = ld32(kSaveInfoPtr);
    if (p < mem::kMem2Start || p >= mem::kMem2End) return;
    if (FILE* f = fopen(path.c_str(), "wb")) {
        fwrite(mem::ptr(p), 1, kSaveInfoSize, f);
        fclose(f);
        LOG("[test] save info (%08X) written to %s", p, path.c_str());
    }
}
// Opt-in integration checks use the actual coreinit HLE on the guest main thread.
// Only host-created scratch objects and a host callback are used; no game data is logged.
std::atomic<unsigned> timing_callbacks{0};
uint32_t timing_event = 0;
void timing_callback(Cpu* c) {
    ++timing_callbacks;
    Cpu call = *c;
    call.r[3] = timing_event;
    hle_find("coreinit", "OSSignalEvent")(&call);
}
void timing_checks() {
    Cpu call = *threads::current();
    auto invoke = [&](const char* name) { hle_find("coreinit", name)(&call); };
    auto delay = [&](unsigned ms) { return timebase::kTicksPerSec * ms / 1000; };
    auto pair = [&](int r, uint64_t ticks) { call.r[r] = uint32_t(ticks >> 32); call.r[r + 1] = uint32_t(ticks); };
    auto check = [](const char* name, bool ok) { LOG("[test] timed HLE %s %s", name, ok ? "PASS" : "FAIL"); };
    auto sleep = [&](unsigned ms) { pair(3, delay(ms)); invoke("OSSleepTicks"); };
    auto start = std::chrono::steady_clock::now();
    sleep(2);
    check("OSSleepTicks", std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2));
    timing_event = mem::host_alloc(0x60, 0x20);
    call.r[3] = timing_event; call.r[4] = 0; call.r[5] = 0;
    invoke("OSInitEvent");
    auto wait_event = [&](unsigned ms) {
        call.r[3] = timing_event; pair(5, delay(ms)); invoke("OSWaitEventWithTimeout");
        return call.r[3] != 0;
    };
    start = std::chrono::steady_clock::now();
    bool result = wait_event(2);
    check("event timeout", !result && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2));
    call.r[3] = timing_event; invoke("OSSignalEvent");
    check("event signaled", wait_event(2));
    call.r[3] = timing_event; invoke("OSResetEvent");
    uint32_t alarm = mem::host_alloc(0x60, 0x20);
    uint32_t callback = dispatch::register_host(timing_callback, "timed-wait-test");
    call.r[3] = alarm; invoke("OSCreateAlarm");
    auto arm = [&](unsigned ms) {
        call.r[3] = alarm; pair(5, delay(ms)); call.r[7] = callback; invoke("OSSetAlarm");
    };
    arm(80); // the earlier replacement must interrupt the alarm thread's old deadline
    start = std::chrono::steady_clock::now();
    arm(3);
    result = wait_event(500);
    auto elapsed = std::chrono::steady_clock::now() - start;
    check("alarm rearm/event wake", result && timing_callbacks == 1 && elapsed >= std::chrono::milliseconds(3) &&
          elapsed < std::chrono::milliseconds(80));
    call.r[3] = alarm; pair(5, timebase::guest_now() + delay(2)); pair(7, delay(2)); call.r[9] = callback;
    invoke("OSSetPeriodicAlarm");
    sleep(12);
    check("periodic alarm", timing_callbacks >= 3);
    call.r[3] = alarm; invoke("OSCancelAlarm");
    sleep(5); // a callback already dispatched at cancellation may finish
    auto count = timing_callbacks.load();
    sleep(5);
    check("alarm cancellation", timing_callbacks == count);
}
}  // namespace

// called with the scenario time on every controller read (input.mm)
void tick(double t, bool ended) {
    static bool timing_done = false;
    if (!timing_done && getenv("WWHD_TEST_TIMED_WAITS") && threads::current()) {
        timing_done = true;
        timing_checks();
    }

    // Host-only controller-mode regression script: t:mode (1 GamePad, 2 Pro).
    auto parse_modes = [](const char* name) {
        std::vector<std::pair<double, int>> modes;
        for (const char* e = getenv(name); e && *e;) {
            double at; int mode, n;
            if (sscanf(e, "%lf:%d%n", &at, &mode, &n) != 2 || (mode != 1 && mode != 2)) break;
            modes.emplace_back(at, mode);
            e += n;
            if (*e != ',') break;
            ++e;
        }
        return modes;
    };
    static auto controllers = parse_modes("WWHD_TEST_CONTROLLER");
    static auto checks = parse_modes("WWHD_TEST_CONTROLLER_CHECK");
    for (auto& [at, mode] : controllers) if (mode && t >= at) {
        input::set_pro_controller(mode == 2);
        LOG("[test] controller set %d at %.3f", mode, t);
        mode = 0;
    }
    for (auto& [at, mode] : checks) if (mode && t >= at) {
        LOG("[test] controller check %s expected %d actual %d at %.3f",
            input::pro_controller() == (mode == 2) ? "PASS" : "FAIL", mode, input::pro_controller() ? 2 : 1, t);
        mode = 0;
    }
    // WWHD_TEST_SAVE=t:slot,... uses the selected kind (WWHD_FULL_SAVE_STATES=0|1).
    static std::vector<std::pair<double, int>> saves = [] {
        std::vector<std::pair<double, int>> v;
        for (const char* e = getenv("WWHD_TEST_SAVE"); e && *e;) {
            double at; int slot, n;
            if (sscanf(e, "%lf:%d%n", &at, &slot, &n) != 2) break;
            v.emplace_back(at, slot);
            e += n;
            if (*e != ',') break;
            ++e;
        }
        return v;
    }();
    for (auto& [at, slot] : saves) if (slot > 0 && t >= at) {
        ss::request_save(slot);
        LOG("[test] t=%.3f save slot %d", t, slot);
        slot = 0;
    }
    // WWHD_TEST_LOAD=t:slot,... loads a save state at scenario time t (once each)
    static std::vector<std::pair<double, int>> loads = [] {
        std::vector<std::pair<double, int>> v;
        for (const char* e = getenv("WWHD_TEST_LOAD"); e && *e;) {
            double at; int slot, n;
            if (sscanf(e, "%lf:%d%n", &at, &slot, &n) != 2) break;
            v.push_back({at, slot});
            e += n;
            if (*e != ',') break;
            e++;
        }
        return v;
    }();
    for (auto& [at, slot] : loads)
        if (slot > 0 && t >= at) {
            LOG("[test] t=%.3f load slot %d", t, slot);
            ss::request_load(slot);
            slot = 0;
        }
    // WWHD_TEST_SCENECHANGE=t: at scenario time t the game is asked to enter its current stage again at
    // the start point (dComIfGp next stage := start stage, enabled): a scene change wherever Link is
    static double sc_t = getenv("WWHD_TEST_SCENECHANGE") ? atof(getenv("WWHD_TEST_SCENECHANGE")) : -1;
    if (sc_t >= 0 && t >= sc_t) {
        sc_t = -1;
        const uint32_t kPlay = GD(0x1046F0B0), kStart = kPlay + 0x5134, kNext = kPlay + 0x5140;
        for (uint32_t i = 0; i < 12; i++) st8(kNext + i, ld8(kStart + i));
        st8(kNext + 12, 1);  // enabled
        st8(kNext + 13, 0);  // wipe
        LOG("[test] t=%.3f scene change requested (%.8s)", t, (const char*)mem::ptr(kNext));
    }
    static std::vector<Poke> pokes = parse_pokes();
    for (auto& p : pokes) {
        if (p.done || t < p.t) continue;
        p.done = true;
        uint32_t a = (p.deref ? ld32(p.ptr) : p.ptr) + p.off;
        for (size_t i = 0; i < p.bytes.size(); i++) st8(a + (uint32_t)i, p.bytes[i]);
        LOG("[test] t=%.3f poke %08X: %zu bytes", t, a, p.bytes.size());
    }
    static const char* sd = getenv("WWHD_SAVEINFO_DUMP");
    if (!sd) return;
    static std::vector<double> at = [] {
        std::vector<double> v;
        for (const char* e = getenv("WWHD_SAVEINFO_AT"); e && *e;) {
            char* end;
            v.push_back(strtod(e, &end));
            if (*end != ',') break;
            e = end + 1;
        }
        return v;
    }();
    for (auto& x : at)
        if (x >= 0 && t >= x) {
            char buf[64];
            snprintf(buf, sizeof buf, ".%g", x);
            dump_saveinfo(std::string(sd) + buf);
            x = -1;
        }
    static bool end_done = false;
    if (ended && !end_done) {
        end_done = true;
        dump_saveinfo(sd);
    }
}

namespace {
struct DumpSpec { FILE* f; uint32_t fn; uint32_t size; bool link; };
std::vector<DumpSpec> parse_dumps() {
    std::vector<DumpSpec> v;
    const char* e = getenv("WWHD_ACTOR_DUMP");
    while (e && *e) {
        const char* c1 = strchr(e, ':');
        if (!c1) break;
        std::string path(e, c1 - e);
        DumpSpec d{};
        if (!strncmp(c1 + 1, "link", 4)) d.link = true;
        else d.fn = (uint32_t)strtoul(c1 + 1, nullptr, 16);
        const char* c2 = strchr(c1 + 1, ':');
        if (!c2) break;
        char* end;
        d.size = (uint32_t)strtoul(c2 + 1, &end, 16);
        d.f = fopen(path.c_str(), "wb");
        if (d.f) v.push_back(d);
        e = *end == ',' ? end + 1 : end + strlen(end);
    }
    return v;
}
}  // namespace

void rng_trace();
// after each process execute (true60.cpp, also with true 60 off); fn = its execute function
void after_execute(uint32_t proc, uint32_t fn, bool is_link, float dt) {
    static std::vector<DumpSpec> dumps = parse_dumps();
    if (is_link) rng_trace();
    if (dumps.empty()) return;
    for (auto& d : dumps) {
        if (!(d.link ? is_link : fn == d.fn)) continue;
        uint64_t step = interp::logic_steps();
        uint32_t full = interp::hold_pass() ? 0 : 1;
        fwrite("ADMP", 1, 4, d.f);
        fwrite(&step, 8, 1, d.f);
        fwrite(&full, 4, 1, d.f);
        fwrite(&dt, 4, 1, d.f);
        fwrite(&proc, 4, 1, d.f);
        fwrite(&d.size, 4, 1, d.f);
        fwrite(mem::ptr(proc), 1, d.size, d.f);
        static int n = 0;
        if (++n % 64 == 0) fflush(d.f);
    }
}
// WWHD_RNG_TRACE=path: before each full-pass execute of Link: logic step and cM_rnd's seeds
void rng_trace() {
    static FILE* f = getenv("WWHD_RNG_TRACE") ? fopen(getenv("WWHD_RNG_TRACE"), "w") : nullptr;
    if (!f || interp::hold_pass()) return;
    fprintf(f, "%llu %08X %08X %08X", (unsigned long long)interp::logic_steps(), ld32(GD(0x101FF9D4)), ld32(GD(0x101FF9D4) + 4), ld32(GD(0x101FF9D4) + 8));
    // WWHD_MEM_WATCH=addr:len,... adds those bytes (hex) to each line
    static std::vector<std::pair<uint32_t, uint32_t>> w = [] {
        std::vector<std::pair<uint32_t, uint32_t>> v;
        for (const char* e = getenv("WWHD_MEM_WATCH"); e && *e;) {
            char* end;
            uint32_t a = (uint32_t)strtoul(e, &end, 16), n = 4;
            if (*end == ':') n = (uint32_t)strtoul(end + 1, &end, 16);
            v.push_back({a, n});
            if (*end != ',') break;
            e = end + 1;
        }
        return v;
    }();
    for (auto& [a, n] : w) {
        fprintf(f, " ");
        for (uint32_t i = 0; i < n; i++) fprintf(f, "%02X", ld8(a + i));
    }
    fprintf(f, " c%08X\n", ld32(GD(0x101FF560)));
    static int n = 0;
    if (++n % 30 == 0) fflush(f);
}
bool dumping() {
    static const bool on = getenv("WWHD_ACTOR_DUMP") || getenv("WWHD_RNG_TRACE") || getenv("WWHD_MEM_DUMP") || getenv("WWHD_LINK_PRE") || getenv("WWHD_LINK_WARP");
    return on;
}
}  // namespace true60_test

// WWHD_LIGHT_TRACE=path: point-light registrations (dKy_plight_set/priority_set/cut,
// dKy_efplight_set/cut) with logic step, full pass and caller (true60 comparisons)
namespace {
void light_trace(int fn, Cpu* c) {
    static FILE* f = getenv("WWHD_LIGHT_TRACE") ? fopen(getenv("WWHD_LIGHT_TRACE"), "w") : nullptr;
    if (!f) return;
    fprintf(f, "%llu %d %d %08X %08X\n", (unsigned long long)interp::logic_steps(), interp::hold_pass() ? 0 : 1, fn, c->r[3], c->lr);
    fflush(f);
}
}  // namespace
extern "C" {
void f_025564B4_orig(Cpu* c); void f_0255A2B8_orig(Cpu* c); void f_0255A374_orig(Cpu* c); void f_0255B9C8_orig(Cpu* c); void f_0255BA9C_orig(Cpu* c);
void hook_025564B4(Cpu* c) { light_trace(0, c); f_025564B4_orig(c); }
void hook_0255A2B8(Cpu* c) { light_trace(1, c); f_0255A2B8_orig(c); }
void hook_0255A374(Cpu* c) { light_trace(2, c); f_0255A374_orig(c); }
void hook_0255B9C8(Cpu* c) { light_trace(3, c); f_0255B9C8_orig(c); }
void hook_0255BA9C(Cpu* c) { light_trace(4, c); f_0255BA9C_orig(c); }
}

namespace true60_test {
// WWHD_LINK_PRE=path: Link's process (0x8284 bytes) right before each full-pass execute (same record format)
uint64_t origin_step();
void before_execute_link(uint32_t proc) {
    // Visual comparison fixture: apply at an actor boundary, outside controller-read call stacks.
    static bool warped = false;
    if (!warped && !interp::hold_pass() && origin_step()) {
        if (const char* e = getenv("WWHD_LINK_WARP")) {
            unsigned long long step; float x,y,z; int angle;
            if (sscanf(e,"%llu:%f:%f:%f:%d",&step,&x,&y,&z,&angle)==5 && interp::logic_steps() >= origin_step()+step) {
                LOG("[test] warp Link %08X from %.1f %.1f %.1f to %.1f %.1f %.1f",proc,(float)ldf32(proc+0x314),(float)ldf32(proc+0x318),(float)ldf32(proc+0x31C),x,y,z);
                for (uint32_t off : {0x2ECu,0x300u,0x314u}) { stf32(proc+off,x); stf32(proc+off+4,y); stf32(proc+off+8,z); }
                st16(proc+0x322,(uint16_t)angle);st16(proc+0x32A,(uint16_t)angle);
                // The HD executable restores these retained debug values at 0240D130 each update.
                stf32(GD(0x1046CD48),x);stf32(GD(0x1046CD4C),y);stf32(GD(0x1046CD50),z);
                st16(GD(0x1046CD12),(uint16_t)angle);st16(GD(0x1046CD0A),(uint16_t)angle);
                warped=true;
            }
        }
    }
    // WWHD_MEM_DUMP=path:addr:size: that memory before each full-pass execute of Link (same record format)
    static FILE* md = nullptr;
    static uint32_t md_a = 0, md_n = 0;
    static bool md_init = false;
    if (!md_init) {
        md_init = true;
        if (const char* e = getenv("WWHD_MEM_DUMP")) {
            const char* c1 = strchr(e, ':');
            if (c1) {
                md = fopen(std::string(e, c1 - e).c_str(), "wb");
                char* end;
                md_a = (uint32_t)strtoul(c1 + 1, &end, 16);
                if (*end == ':') md_n = (uint32_t)strtoul(end + 1, nullptr, 16);
            }
        }
    }
    if (md && md_n && !interp::hold_pass()) {
        uint64_t step = interp::logic_steps();
        uint32_t full = 1;
        float dt = 1.0f;
        fwrite("ADMP", 1, 4, md); fwrite(&step, 8, 1, md); fwrite(&full, 4, 1, md); fwrite(&dt, 4, 1, md);
        fwrite(&md_a, 4, 1, md); fwrite(&md_n, 4, 1, md); fwrite(mem::ptr(md_a), 1, md_n, md);
        fflush(md);
    }
    static FILE* f = getenv("WWHD_LINK_PRE") ? fopen(getenv("WWHD_LINK_PRE"), "wb") : nullptr;
    if (!f || interp::hold_pass()) return;
    uint64_t step = interp::logic_steps();
    uint32_t full = 1, size = 0x8284;
    float dt = 1.0f;
    fwrite("ADMP", 1, 4, f); fwrite(&step, 8, 1, f); fwrite(&full, 4, 1, f); fwrite(&dt, 4, 1, f);
    fwrite(&proc, 4, 1, f); fwrite(&size, 4, 1, f); fwrite(mem::ptr(proc), 1, size, f);
    fflush(f);
}
}

namespace true60_test {
uint64_t g_origin_step = 0;
// Called at the actual logic-step boundary, including frames that do not poll input.
void logic_step(uint64_t step) {
    static FILE* timeline = [] {
        const char* path = getenv("WWHD_LOGIC_TIMELINE");
        return path ? fopen(path, "w") : nullptr;
    }();
    if (!timeline || !g_origin_step) return;
    // Only step numbers and clocks: no guest memory or save data.
    fprintf(timeline, "%llu %.9f %.9f\n", (unsigned long long)step, (step - g_origin_step) / 30.0,
            timebase::now() / double(timebase::kTicksPerSec));
    fflush(timeline);
}
void set_origin_step(uint64_t s) { g_origin_step = s; logic_step(s); }
uint64_t origin_step() { return g_origin_step; }
}

// debug: WWHD_TEVLOG=path: dScnKy_env_light_c::settingTevStruct (025626A4) calls: step, full, type (r4), tevStr (r6)
extern "C" void f_025626A4_orig(Cpu* c);
extern "C" void hook_025626A4(Cpu* c) {
    static FILE* f = getenv("WWHD_TEVLOG") ? fopen(getenv("WWHD_TEVLOG"), "w") : nullptr;
    if (f) fprintf(f, "%llu %d %d %08X %08X %08X\n", (unsigned long long)interp::logic_steps(), interp::hold_pass() ? 0 : 1, (int)c->r[4], c->r[5], c->r[6], c->lr);
    f_025626A4_orig(c);
}
// debug: WWHD_CULLLOG=path: fopAcM_cullingCheck (025D6CE8): step, full, actor, result
extern "C" void f_025D6CE8_orig(Cpu* c);
namespace true60_test { uint64_t origin_step(); }
extern "C" void hook_025D6CE8(Cpu* c) {
    // debug: WWHD_T60_PAGEHASH=step:actor:path writes a hash per 4 KB page of 10000000..4A000000 at that
    // actor's culling check on the full pass of that scenario step (to diff two runs)
    static const char* ph = getenv("WWHD_T60_PAGEHASH");
    if (ph && true60_test::origin_step() && !interp::hold_pass()) {
        static uint64_t st = strtoull(ph, nullptr, 10);
        static uint32_t act = (uint32_t)strtoul(strchr(ph, ':') + 1, nullptr, 16);
        static bool done = false;
        if (!done && c->r[3] == act && interp::logic_steps() == true60_test::origin_step() + st) {
            done = true;
            if (FILE* f = fopen(strchr(strchr(ph, ':') + 1, ':') + 1, "w")) {
                for (uint32_t a = 0x10000000; a < 0x4A000000; a += 0x1000) {
                    const uint8_t* p = mem::ptr(a);
                    uint64_t h = 1469598103934665603ull;
                    for (int i = 0; i < 0x1000; i += 8) { uint64_t w; memcpy(&w, p + i, 8); h = (h ^ w) * 1099511628211ull; }
                    fprintf(f, "%08X %016llX\n", a, (unsigned long long)h);
                }
                fclose(f);
            }
        }
    }
    static FILE* f = getenv("WWHD_CULLLOG") ? fopen(getenv("WWHD_CULLLOG"), "w") : nullptr;
    uint32_t a = c->r[3];
    std::string ctx;
    if (f) {
        char t[16];
        for (uint32_t o = 0; o < 0x30; o += 4) { snprintf(t, sizeof t, " %08X", ld32(GD(0x104B45F8) + o)); ctx += t; }
        ctx += " | cull";
        uint32_t m = ld32(a + 0x348);
        for (uint32_t o = 0; o < 0x30 && m; o += 4) { snprintf(t, sizeof t, " %08X", ld32(m + o)); ctx += t; }
        uint32_t vo = ld32(ld32(ld32(GD(0x101F95D0)) + 0x1024));
        snprintf(t, sizeof t, " | vis %08X", vo); ctx += t;
        for (uint32_t o = 0x600; o < 0x800 && vo; o += 4) { snprintf(t, sizeof t, " %08X", ld32(vo + o)); ctx += t; }
        ctx += " | box";
        for (uint32_t o = 0x34C; o < 0x370; o += 4) { snprintf(t, sizeof t, " %08X", ld32(a + o)); ctx += t; }
    }
    f_025D6CE8_orig(c);
    if (f) fprintf(f, "%llu %d %08X %d%s\n", (unsigned long long)interp::logic_steps(), interp::hold_pass() ? 0 : 1, a, (int)c->r[3], ctx.c_str());
}
// debug: WWHD_BOOTDBG=1 logs agl shader program archive setup (02786520 / 027B8904): boot crash hunt
extern "C" void f_02786520_orig(Cpu* c);
extern "C" void hook_02786520(Cpu* c) {
    static const bool on = getenv("WWHD_BOOTDBG") != nullptr;
    if (on) {
        uint32_t ar = c->r[4];
        LOG("[bootdbg] 02786520 this=%08X archive=%08X vt=%08X getFile=%08X", c->r[3], ar, ar ? ld32(ar + 0x10) : 0,
            ar && ld32(ar + 0x10) ? ld32(ld32(ar + 0x10) + 0x3C) : 0);
    }
    f_02786520_orig(c);
}
namespace { void wp_arm(uint32_t guest_lo, uint32_t size); }
extern "C" void f_027B8904_orig(Cpu* c);
extern "C" void hook_027B8904(Cpu* c) {
    static const bool on = getenv("WWHD_BOOTDBG") != nullptr;
    if (on)
        LOG("[bootdbg] 027B8904 obj=%08X fb=%08X sharc=%08X flags=%X", c->r[3], ld32(c->r[4]), ld32(c->r[5]), c->r[6]);
    uint32_t obj = c->r[3];
    // WWHD_BOOTDBG_SLOW=ms: stall each archive init (a slow machine) to make boot timing races reproducible
    static const int slow = getenv("WWHD_BOOTDBG_SLOW") ? atoi(getenv("WWHD_BOOTDBG_SLOW")) : 0;
    if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(slow));
    f_027B8904_orig(c);
    if (on) {
        uint32_t a = ld32(obj + 0x20);
        LOG("[bootdbg] 027B8904 done obj=%08X n=%u arr=%08X e0.7c=%08X", obj, ld32(obj + 0x1c), a, a ? ld32(a + 0x7c) : 0);
        static bool armed = false;
        if (!armed && getenv("WWHD_BOOTDBG_PROT") && a) { armed = true; wp_arm(a, ld32(obj + 0x1c) * 0x84); }
    }
}
// WWHD_BOOTDBG_PROT=1: after the first agl program archive (obj 226FE868) is set up, its program
// array's host page is write-protected; every write fault logs the writing thread and backtrace (the
// page is re-protected 0.2 ms later), to find who corrupts it
namespace {
#ifdef _WIN32
void wp_arm(uint32_t, uint32_t) { LOG("[wp] WWHD_BOOTDBG_PROT is not available on Windows"); }
#else
std::atomic<uintptr_t> g_wp_lo{0}, g_wp_hi{0};
std::atomic<bool> g_wp_armed{false};
struct sigaction g_wp_old_segv, g_wp_old_bus;
void wp_handler(int sig, siginfo_t* si, void* uc) {
    uintptr_t a = (uintptr_t)si->si_addr;
    if (a >= g_wp_lo && a < g_wp_hi) {
        char name[64] = "";
        pthread_getname_np(pthread_self(), name, sizeof name);
        Cpu* c = threads::current();
        char buf[200];
        int n = snprintf(buf, sizeof buf, "[wp] write %08X by \"%s\" guest lr=%08X\n", (unsigned)(a - (uintptr_t)PPC_MEM_BASE), name,
                         c ? c->lr : 0);
        write(2, buf, n);
        void* fr[24];
        int nf = backtrace(fr, 24);
        backtrace_symbols_fd(fr, nf, 2);
        mprotect((void*)g_wp_lo.load(), g_wp_hi - g_wp_lo, PROT_READ | PROT_WRITE);
        g_wp_armed = false;
        return;
    }
    struct sigaction& o = sig == SIGBUS ? g_wp_old_bus : g_wp_old_segv;
    if (o.sa_flags & SA_SIGINFO) o.sa_sigaction(sig, si, uc);
    else if (o.sa_handler != SIG_DFL && o.sa_handler != SIG_IGN) o.sa_handler(sig);
    else { signal(sig, SIG_DFL); raise(sig); }
}
void wp_arm(uint32_t guest_lo, uint32_t size) {
    uintptr_t pg = (uintptr_t)getpagesize();
    uintptr_t lo = ((uintptr_t)PPC_MEM_BASE + guest_lo) & ~(pg - 1);
    uintptr_t hi = ((uintptr_t)PPC_MEM_BASE + guest_lo + size + pg - 1) & ~(pg - 1);
    g_wp_lo = lo;
    g_wp_hi = hi;
    struct sigaction sa{};
    sa.sa_sigaction = wp_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, &g_wp_old_segv);
    sigaction(SIGBUS, &sa, &g_wp_old_bus);
    mprotect((void*)lo, hi - lo, PROT_READ);
    g_wp_armed = true;
    std::thread([] {
        for (int i = 0; i < 200000; i++) {  // 40 s
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            if (!g_wp_armed) { mprotect((void*)g_wp_lo.load(), g_wp_hi - g_wp_lo, PROT_READ); g_wp_armed = true; }
        }
        mprotect((void*)g_wp_lo.load(), g_wp_hi - g_wp_lo, PROT_READ | PROT_WRITE);
    }).detach();
    LOG("[wp] armed %08X..%08X", (unsigned)(lo - (uintptr_t)PPC_MEM_BASE), (unsigned)(hi - (uintptr_t)PPC_MEM_BASE));
}
#endif
}  // namespace
extern "C" void f_027B82B8_orig(Cpu* c);
extern "C" void hook_027B82B8(Cpu* c) {
    static const bool on = getenv("WWHD_BOOTDBG") != nullptr;
    uint32_t obj = c->r[3];
    auto st = [&](const char* w) {
        uint32_t a = ld32(obj + 0x20);
        LOG("[bootdbg] 027B82B8 %s obj=%08X n=%u arr=%08X e0.7c=%08X lr=%08X", w, obj, ld32(obj + 0x1c), a, a ? ld32(a + 0x7c) : 0, c->lr);
    };
    if (on) st("in ");
    f_027B82B8_orig(c);
    if (on) st("out");
}
// debug: WWHD_HEAPLOG=1 logs each new (heap, thread) pair of sead::ExpHeap::alloc (02753D6C) with the
// heap's lock flag (+0x90 bit 0)
#include <mutex>
#include <set>
extern "C" void f_02753D6C_orig(Cpu* c);
extern "C" void hook_02753D6C(Cpu* c) {
    static const bool on = getenv("WWHD_HEAPLOG") != nullptr;
    if (on) {
        static std::mutex m;
        static std::set<std::pair<uint32_t, uint32_t>> seen;
        uint32_t h = c->r[3], t = threads::current_thread();
        std::lock_guard<std::mutex> lk(m);
        if (seen.insert({h, t}).second) {
            char name[64] = "";
#ifndef _WIN32
            pthread_getname_np(pthread_self(), name, sizeof name);
#endif
            LOG("[heaplog] heap %08X (flags %08X, %08X..%08X) first alloc by \"%s\" (%08X) size %X lr %08X", h, ld32(h + 0x90),
                ld32(h + 0x20), ld32(h + 0x20) + ld32(h + 0x24), name, t, c->r[4], c->lr);
        }
    }
    f_02753D6C_orig(c);
}
