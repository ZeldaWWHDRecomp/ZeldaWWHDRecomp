// accessor_test: gabi.h GABI_FAST_ACCESSOR (the port's WWHD_DECOMP_FAST_ACCESSOR) with the real gabi.h and glue.cpp.
// A lazy accessor written as kankyo_getEnvlight is (guard word, construction, the object's address):
//  1. its native entry (AccEntry: as NativeReg registers it and ne_body exports it) returns what the body returns, the
//     object's guest address, for the first call (guard 0: the body runs and constructs, once) and for every later one;
//  2. once the guard is set the body does not run again through the entry, and the call state is left as it was;
//  3. a guard word set by someone else (any non-zero value) counts as set, as in the body's test.
#include <cstdint>
#define GABI_NATIVE_TABLE 1
namespace gabi_native {
// address, slot, flags: 1 callee=native, 2 caller=ok, 4 listed, 8 register-exact, result r3
inline constexpr uint32_t kTab[][3] = {
    {0x02000000u, 0u, 1u | 2u | 4u | 8u | (1u << 16)},
};
template <uint32_t A> struct Ne { static constexpr bool ok = false; };
template <uint32_t A> struct Acc { static constexpr bool ok = false; };
template <> struct Acc<0x02000000u> { static constexpr bool ok = true; static constexpr uint32_t guard = 0x10010000u, value = 0x10010100u; };
}  // namespace gabi_native
#define GABI_TLS_OWNER 1
#define DECOMP_UNIT "accessor_test"
#include "../glue.cpp"
#include <cstdio>
#include <sys/mman.h>
extern "C" void decomp_register(const char*, uint32_t, const char*, void (*)(Cpu*), void (*)(Cpu*, void (*)(Cpu*))) {}
static void* g_registered = nullptr;
extern "C" void decomp_register_native(const char*, u32, void* fn, u32, u32) { g_registered = fn; }
extern "C" void ppc_trap(Cpu*, uint32_t) { __builtin_trap(); }
extern "C" const GabiPtrEntry* g_decomp_call_tab = nullptr;
extern "C" uint8_t* g_decomp_native_ok = nullptr;
extern "C" u32 g_decomp_ptr_mask = 0;
extern "C" uint8_t g_decomp_leaf_off = 0;
extern "C" uint8_t g_decomp_nocheck_off = 0;
struct Guard { gabi::be<u32> value; };
struct Obj { gabi::be<u32> a, b; void construct() { a = 0x11u; b = 0x22u; } };
static Guard& guard = gabi::object_at<Guard>(0x10010000u);
static Obj& obj = gabi::object_at<Obj>(0x10010100u);
static int g_body_runs = 0, g_constructs = 0;
Obj* getObj() {
    WWHD_GLUE_FUNC_R(0x02000000u, Obj*);
    g_body_runs++;
    if (guard.value == 0) {
        guard.value = 1;
        obj.construct();
        g_constructs++;
    }
    return wwhd_native_scope_.ret<wwhd_ret_t_>((&obj));
}
static gabi::NativeReg<0x02000000u, getObj> reg;
extern "C" uint32_t wwhd_ne_02000000(void) { return gabi::ne_body<0x02000000u, getObj, uint32_t>(); }

int main() {
    void* want = PPC_MEM_BASE + 0x10000000u;
    if (mmap(want, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != want) { std::printf("mmap failed\n"); return 2; }
    gabi::ThreadState& t = *gabi::gabi_ts_pass();
    static Cpu cpu{};
    t.cpu = &cpu;
    cpu.r[1] = 0x10018000u;
    t.depth = 2;
    t.caller_ok = 1;
    t.current = 0x02345678u;
    int bad = 0;
    using Fn = u32 (*)();
    if (g_registered != (void*)&gabi::AccEntry<0x02000000u, getObj>::run) { std::printf("NativeReg: not AccEntry\n"); bad++; }
    const Fn entries[2] = {(Fn)g_registered, &wwhd_ne_02000000};
    for (int e = 0; e < 2; e++) {
        guard.value = 0;
        g_body_runs = g_constructs = 0;
        u32 r[4];
        for (int i = 0; i < 4; i++) r[i] = entries[e]();
        const bool ok = r[0] == 0x10010100u && r[1] == r[0] && r[3] == r[0] && g_body_runs == 1 && g_constructs == 1 &&
                        (u32)guard.value == 1u && (u32)obj.a == 0x11u;
        if (!ok) bad++;
        guard.value = 0x80000000u;  // 3. any non-zero guard word
        const int b0 = g_body_runs;
        const u32 r4 = entries[e]();
        if (r4 != 0x10010100u || g_body_runs != b0) bad++;
        if (t.depth != 2 || t.current != 0x02345678u || t.caller_ok != 1 || cpu.r[1] != 0x10018000u) bad++;
        std::printf("accessor_test: %s: results %08X %08X %08X %08X, body runs %d, constructions %d -> %s\n",
                    e ? "wwhd_ne_ (ne_body)" : "registered entry", r[0], r[1], r[2], r[3], g_body_runs, g_constructs,
                    ok ? "ok" : "WRONG");
    }
    std::printf("accessor_test: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
