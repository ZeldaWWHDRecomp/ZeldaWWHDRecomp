// Signalling-NaN counter (local diagnostic): built only with the CMake option WWHD_SNAN_COUNT=ON, logs
// only with the environment variable WWHD_SNAN_COUNT=1. ppc.h calls wwhd_snan_hit() when a float load
// (lfs, psq_l) or store (stfs, psq_st) of the game code meets a signalling-NaN bit pattern (ppc.h moves
// it bit-exactly like the original hardware; a host f32 <-> f64 conversion would quiet it). Each call site
// (the game function and the host offset in it) is counted. Raw lfs -> stfs copies (tools/recomp/rawstore.py)
// load and store the word without these checks.
//
// captures/snan.log (next to wwhd.log; appended, one block per session):
//   # session <local time> build <version> <commit> pid <pid>
//   HIT <kind> <site> ea=<guest address> bits=<pattern> stage=<stage>   first time a site is seen
//   # snapshot <local time> hits=<total>  +  SNAP <kind> <site> count=<n>  every 30 s while sites are hit
//   SITE <kind> <site> count=<n>                                        every site at exit
//   # end <local time> sites=<n> hits=<total>
// tools/snan_merge.py combines all sessions into one table. Known, analysed sources (kKnown) are not logged.
// Diagnostic builds only: the option costs ~0.18 ms per frame even with logging off, so play builds leave it off.
#ifdef WWHD_SNAN_COUNT
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <string>
#include <unistd.h>

#include "build_info.h"
#include "crash_addr.h"
#include "guest_addr.h"
#include "runtime.h"

extern "C" int wwhd_snan_on = 0;

namespace {

const char* const kKind[] = {"lfs", "stfs", "psq_l", "psq_st"};

// call sites: a fixed open-addressing table, lock-free on the hot path (a site that is hit is usually hit
// every frame); the lock only guards the first sighting of a site and the log writes
constexpr size_t kSlots = 4096;
struct Slot {
    std::atomic<uintptr_t> key{0};  // (return address << 3) | (kind << 1) | 1, never 0
    std::atomic<int> kind{0};
    std::atomic<uintptr_t> ret{0};
    std::atomic<uint64_t> count{0};
};
Slot* g_slots;
std::atomic<uint64_t> g_total{0};
std::atomic<size_t> g_used{0};
std::mutex g_lock;
FILE* g_log;
time_t g_last_snapshot;

std::string now() {
    char buf[64];
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// "0200EDC8+0x1c": the compiled game function holding the call site (host offset), else the host address
std::string site_name(uintptr_t a) {
    char buf[512];
    int n = crash_addr::describe(buf, sizeof buf, a);
    std::string s(buf, n > 0 ? (size_t)n : 0);
    size_t g = s.find("[game function ");
    if (g != std::string::npos) {
        size_t e = s.find(']', g);
        return s.substr(g + 15, e == std::string::npos ? std::string::npos : e - g - 15);
    }
    char hex[32];
    snprintf(hex, sizeof hex, "host:%" PRIxPTR, a);
    return hex;
}

// the current stage (dStage_startStage_c name, 8 chars), as savestate.cpp reads it
std::string stage() {
    const char* p = (const char*)mem::ptr(GD(0x1046F0B0) + 0x5134);
    size_t n = strnlen(p, 8);
    for (size_t i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return "?";
    return n ? std::string(p, n) : "?";
}

// every site with its count so far (under g_lock); the last block of a session is its total, so a session
// that is killed still leaves counts (at most 30 s old)
void write_sites(const char* tag) {
    for (size_t i = 0; i < kSlots; i++) {
        uintptr_t k = g_slots[i].key.load(std::memory_order_acquire);
        if (!k || !g_slots[i].ret.load()) continue;
        fprintf(g_log, "%s %s %s count=%" PRIu64 "\n", tag, kKind[g_slots[i].kind.load() & 3], site_name(g_slots[i].ret.load()).c_str(),
                g_slots[i].count.load(std::memory_order_relaxed));
    }
}

void finish() {
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_log) return;
    write_sites("SITE");
    fprintf(g_log, "# end %s sites=%zu hits=%" PRIu64 "\n", now().c_str(), g_used.load(), g_total.load());
    fclose(g_log);
    g_log = nullptr;
}

struct Init {
    Init() {
        const char* e = getenv("WWHD_SNAN_COUNT");
        if (!e || strcmp(e, "1")) return;
        std::string dir = "captures";
        if (const char* lf = getenv("WWHD_LOG_FILE"); lf && *lf && strcmp(lf, "0") && strcmp(lf, "1")) {
            std::filesystem::path p(lf);
            dir = p.has_parent_path() ? p.parent_path().string() : ".";
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        g_log = fopen((dir + "/snan.log").c_str(), "a");
        if (!g_log) return;
        setvbuf(g_log, nullptr, _IOLBF, 0);
        fprintf(g_log, "# session %s build %s %s%s%s pid %d\n", now().c_str(), build::version(), build::commit(),
                *build::variant() ? " " : "", build::variant(), (int)getpid());
        g_slots = new Slot[kSlots];
        g_last_snapshot = time(nullptr);
        atexit(finish);
        wwhd_snan_on = 1;
    }
} g_init;

void first_sighting(Slot& s, uintptr_t ret, int kind, uint32_t ea, uint64_t bits) {
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_log) return;
    fprintf(g_log, "HIT %s %s ea=%08X bits=%0*" PRIX64 " stage=%s\n", kKind[kind & 3], site_name(ret).c_str(), ea,
            bits >> 32 ? 16 : 8, bits, stage().c_str());
}

void maybe_snapshot() {
    time_t t = time(nullptr);
    if (t - g_last_snapshot < 30) return;
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_log || t - g_last_snapshot < 30) return;
    g_last_snapshot = t;
    fprintf(g_log, "# snapshot %s hits=%" PRIu64 "\n", now().c_str(), g_total.load());
    write_sites("SNAP");
}

}  // namespace

// known, analysed sources (not logged, so a diagnostic run shows only new ones)
struct Known {
    uint32_t ea;  // the address in this region's game (GD)
    uint64_t bits;
};
const Known* known_sources(size_t& n) {
    static const Known known[] = {
        {GD(0x101FCBC8), 0x7F800001},  // JStudio "unset" constant (TAdaptor_sound::do_prepare, 02848C04): harmless
    };
    n = sizeof known / sizeof known[0];
    return known;
}

extern "C" __attribute__((noinline)) void wwhd_snan_hit(int kind, uint32_t ea, uint64_t bits) {
    uintptr_t ret = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    if (!g_slots) return;
    size_t known_count = 0;
    const Known* known = known_sources(known_count);
    for (size_t i = 0; i < known_count; i++)
        if (known[i].ea == ea && known[i].bits == bits) return;
    uintptr_t key = (ret << 3) | ((uintptr_t)(kind & 3) << 1) | 1;
    size_t h = (size_t)((key * 0x9E3779B97F4A7C15ull) >> 52) & (kSlots - 1);
    for (size_t probe = 0; probe < kSlots; probe++, h = (h + 1) & (kSlots - 1)) {
        Slot& s = g_slots[h];
        uintptr_t k = s.key.load(std::memory_order_acquire);
        if (k == key) {
            s.count.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        if (k == 0) {
            uintptr_t expected = 0;
            if (s.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) {
                s.kind.store(kind);
                s.ret.store(ret);
                s.count.fetch_add(1, std::memory_order_relaxed);
                g_used.fetch_add(1);
                first_sighting(s, ret, kind, ea, bits);
                break;
            }
            if (expected == key) {
                s.count.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    }
    if ((g_total.fetch_add(1, std::memory_order_relaxed) & 0xFFFF) == 0) maybe_snapshot();
}
#endif
