// Internal runtime API shared by the loader, dispatcher, threads and HLE libraries.
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ppc.h"

// ---- guest memory layout ----
namespace mem {
constexpr uint32_t kMem2Start = 0x10000000;   // app data + heap (MEM2)
constexpr uint32_t kMem2End = 0x50000000;
constexpr uint32_t kRuntimeStart = 0x60000000; // runtime-owned guest objects (stacks, OS structs)
constexpr uint32_t kRuntimeEnd = 0x70000000;
constexpr uint32_t kHostStart = 0x68000000;    // ... of them host-only (not part of save states)
constexpr uint32_t kFixedStart = 0x6FFF0000;   // fixed slots for small buffers the guest may keep pointers to
constexpr uint32_t kFixedSize = 0x10000;
constexpr uint32_t kFgBucket = 0xE0000000;     // foreground bucket
constexpr uint32_t kFgBucketSize = 0x02800000;
constexpr uint32_t kMem1 = 0xF4000000;         // MEM1, 32 MiB
constexpr uint32_t kMem1Size = 0x02000000;
constexpr uint32_t kHleFuncBase = 0xC2000000;  // synthetic addresses of host functions

void init();
// bump allocator for runtime-owned guest memory (never freed). Guest-visible objects (the game may
// keep pointers to them) go through runtime_alloc and are part of save states; scratch memory that
// only host code uses (shader copies, service thread stacks) through host_alloc.
uint32_t runtime_alloc(uint32_t size, uint32_t align = 16);
uint32_t host_alloc(uint32_t size, uint32_t align = 16);
// fixed 0x100-byte guest buffer number `id` (lazily used features: same address in every session)
enum FixedSlot : uint32_t { kFixInterpEye = 0, kFixInterpMtx, kFixFxMidPos, kFixLinkScratch, kFixCount };
inline uint32_t fixed_slot(FixedSlot id) { return kFixedStart + 0x100 * id; }
uint32_t runtime_top();
void raise_runtime_top(uint32_t top);
struct AllocRec { uint32_t addr, size; uint64_t tag; };
std::vector<AllocRec> runtime_alloc_log();  // sorted by address
inline uint8_t* ptr(uint32_t ea) { return PPC_MEM_BASE + ea; }
inline uint32_t guest(const void* p) { return (uint32_t)((const uint8_t*)p - PPC_MEM_BASE); }
std::string read_cstr(uint32_t ea);
void write_cstr(uint32_t ea, const std::string& s, uint32_t max);
}  // namespace mem

// ---- loader ----
struct LoadedModule {
    uint32_t entry;
    uint32_t sda_base;   // r13
    uint32_t sda2_base;  // r2
    uint32_t stack_size;
    uint32_t data_end;   // end of .bss: first free MEM2 address
};
bool load_rpx(const std::string& path, LoadedModule& out);

// ---- dispatch ----
namespace dispatch {
void init();
// register a host function callable from guest code; returns its guest address
uint32_t register_host(PpcFunc fn, const char* name);
void set(uint32_t addr, PpcFunc fn);
PpcFunc lookup(uint32_t addr);
}  // namespace dispatch

// call a guest function from host code (on the current guest thread)
uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {});

// ---- threads ----
// Per-core scheduling: like the hardware, only one guest thread runs on each emulated core at a
// time, chosen by priority. Code that blocks the host thread (waits, sleeps, I/O) runs inside a
// BlockingScope so other threads on the core can run meanwhile.
namespace threads {
void block_begin();   // give up the core
void block_end();     // take it back (waits for a turn)
bool ensure_core();   // service threads entering guest code; true if the core was taken
void release_core();
void set_service_core(uint32_t core);
void report_sched();  // log per-thread core usage
}  // namespace threads
struct BlockingScope {
    BlockingScope() { threads::block_begin(); }
    ~BlockingScope() { threads::block_end(); }
    BlockingScope(const BlockingScope&) = delete;
};

namespace threads {
void init(const LoadedModule& m);
void run_main(const LoadedModule& m, int argc, uint32_t argv);  // does not return until the game exits
Cpu* current();          // Cpu of the calling host thread (null if not a guest thread)
uint32_t current_thread();  // guest OSThread* of the calling thread
// a Cpu + guest stack for host-created threads that need to call guest code (alarms, audio)
Cpu* make_service_cpu(const char* name, uint32_t stack_size = 0x10000);
// service threads bracket their guest work (callbacks, guest memory access) with these, so a save
// state can wait until they are idle; service_begin blocks while the game is frozen
void service_begin();
void service_end();
// a blocking sleep that counts as parked for save states (OSSleepTicks, GX2WaitForVsync)
void park_sleep_until(std::chrono::steady_clock::time_point t,
                      bool precise = false, void (*before_resume)() = nullptr);

// ---- save states (savestate.cpp) ----
// Freeze every other guest thread at a parked point; the caller (the game's main thread, between
// frames) gives up its core meanwhile. False (and `busy` names the culprits) if they did not all
// park within the timeout; thaw() must be called either way.
// entry_mode: threads still running after entry_after_ms park at a guest function entry
// (1: any such thread, 2: only at the place a loaded snapshot has them)
bool quiesce(int timeout_ms, std::string& busy, int entry_mode = 0, int entry_after_ms = 0);
void thaw();
}  // namespace threads

// ---- time ----
namespace timebase {
constexpr uint64_t kTicksPerSec = 62156250ull;  // Espresso bus clock / 4
uint64_t now();  // host ticks since boot (monotonic; host-side timing)
// guest-visible time (OSGetTime, mftb, alarms): host time plus an offset that a loaded save state
// sets so the guest's clock continues from the moment it was saved
uint64_t guest_now();
uint64_t to_guest(uint64_t host_ticks);
uint64_t to_host(uint64_t guest_ticks);
void set_guest_now(uint64_t guest_ticks);
}

// ---- HLE registration ----
struct HleReg {
    const char* lib;
    const char* name;
    PpcFunc fn;
    HleReg(const char* l, const char* n, PpcFunc f);
};
PpcFunc hle_find(const char* lib, const char* name);  // null if not implemented
PpcFunc hle_find_any(const char* name);

#define HLE(lib, name)                                                        \
    extern "C" void imp_##lib##_##name(Cpu* c);                                \
    static HleReg hle_reg_##lib##_##name(#lib, #name, imp_##lib##_##name);   \
    extern "C" void imp_##lib##_##name(Cpu* c)

// argument helpers (PPC SysV ABI)
inline uint32_t arg(Cpu* c, int i) { return c->r[3 + i]; }
inline uint64_t arg64(Cpu* c, int reg) { return ((uint64_t)c->r[reg] << 32) | c->r[reg + 1]; }  // reg = first of pair
inline void ret(Cpu* c, uint32_t v) { c->r[3] = v; }
inline void ret64(Cpu* c, uint64_t v) { c->r[3] = (uint32_t)(v >> 32); c->r[4] = (uint32_t)v; }

// ---- logging ----
extern bool g_trace_hle;
void log_msg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) log_msg(__VA_ARGS__)
// every later log line also goes to `sink` (one line without '\n'); nullptr stops (main.cpp: log file)
void log_set_sink(void (*sink)(const char*, size_t));
void log_ring_write(int fd, void (*out)(int, const char*, size_t));  // the recent lines, oldest first
// writes the last ~200 log lines through out(fd, text, len) (used by crash logs; no locking)
void log_ring_write(int fd, void (*out)(int, const char*, size_t));
#define TRACE(...) do { if (g_trace_hle) log_msg(__VA_ARGS__); } while (0)

// ---- configuration ----
namespace config {
extern std::string game_dir;   // extracted game root (contains code/, content/, meta/)
extern std::string save_dir;   // host directory for save data
}
