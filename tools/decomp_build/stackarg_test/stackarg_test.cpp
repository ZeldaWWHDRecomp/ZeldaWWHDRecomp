// stackarg_test: gabi.h GABI_STACKARG (the port's WWHD_DECOMP_FAST_STACKARG) with the real gabi.h and glue.cpp.
// A stack-argument reader made native for do_call_sa (NativeInfo bit 24) against the same call through do_call (the
// guest path; gmem_call below plays the callee's guest entry: it reads the f64 at r1 + 8 and writes it back as an
// f32, as cM3d_InclusionCheckPosIn3PosBox2d does, and returns the same result in r3):
//  1. for each of do_call's placements (no automatic frame; in the original's frame; below an active frame's Locals;
//     an active frame that is not the running one) the guest stack bytes afterwards, r1 and r3 are the same;
//  2. the native body really ran (its result), with r1 placed as do_call places it, and nothing else of the stack
//     changed;
//  3. caller_ok 0: do_call_sa takes the guest path (do_call).
#include <cstdint>
#define GABI_NATIVE_TABLE 1
namespace gabi_native {
// address, slot, flags: 1 callee=native, 2 caller=ok, 4 listed, 8 register-exact, 8 float registers, result r3, 24
inline constexpr uint32_t kTab[][3] = {
    {0x02000000u, 0u, 1u | 2u | 4u | 8u | (8u << 12) | (1u << 16) | (1u << 24)},
};
template <uint32_t A> struct Ne { static constexpr bool ok = false; };
extern "C" uint32_t wwhd_sa_02000000(double, double, double, double, double, double, double, double);
template <uint32_t A> struct Sa { static constexpr bool ok = false; };
template <> struct Sa<0x02000000u> { static constexpr bool ok = true; static constexpr auto fn = &wwhd_sa_02000000; };
}  // namespace gabi_native
#define GABI_TLS_OWNER 1
#define DECOMP_UNIT "stackarg_test"
#include "../glue.cpp"
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
extern "C" void decomp_register(const char*, uint32_t, const char*, void (*)(Cpu*), void (*)(Cpu*, void (*)(Cpu*))) {}
extern "C" void decomp_register_native(const char*, u32, void*, u32, u32) {}
extern "C" void ppc_trap(Cpu*, uint32_t) { __builtin_trap(); }
extern "C" const GabiPtrEntry* g_decomp_call_tab = nullptr;
extern "C" uint8_t* g_decomp_native_ok = nullptr;
extern "C" u32 g_decomp_ptr_mask = 0;
extern "C" uint8_t g_decomp_leaf_off = 0;
extern "C" uint8_t g_decomp_nocheck_off = 0;
void gmem_stack_args(int) {}
void gmem_call_top(uint32_t) {}
static int g_guest_calls = 0, g_native_runs = 0;
static u32 g_seen_r1 = 0;
// the callee's computation from its arguments and its stack word (both forms)
static s32 compute(f32 ax, f32 ay, f32 px, f32 margin) {
    u32 m;
    std::memcpy(&m, &margin, 4);
    return (s32)(m ^ (u32)(ax * 16.0f) ^ (u32)(ay * 256.0f) << 8 ^ (u32)(px * 4.0f) << 20);
}
void gmem_call(Cpu* c, uint32_t target, int, int, int) {  // the guest path: the callee's guest entry, as it behaves
    g_guest_calls++;
    if (target != 0x02000000u) __builtin_trap();
    g_seen_r1 = c->r[1];
    f32 margin = f32(gabi::load<f64>(c->r[1] + 8));
    gabi::store<f32>(c->r[1] + 8, margin);
    c->r[3] = (u32)compute(gabi::from_fpr<f32>(c->f[1].ps0), gabi::from_fpr<f32>(c->f[2].ps0), gabi::from_fpr<f32>(c->f[7].ps0), margin);
}
s32 incl(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy, f32 px, f32 py) {
    WWHD_GLUE_FUNC_R(0x02000000u, s32, ax, ay, bx, by, cx, cy, px, py);
    g_native_runs++;
    g_seen_r1 = gabi::cpu->r[1];
    f32 margin = f32(gabi::load<f64>(gabi::cpu->r[1] + 8));
    gabi::store<f32>(gabi::cpu->r[1] + 8, margin);
    return wwhd_native_scope_.ret<wwhd_ret_t_>(compute(ax, ay, px, margin));
}
extern "C" uint32_t wwhd_sa_02000000(double a, double b, double c, double d, double e, double f, double g, double h) {
    return gabi::ne_body<0x02000000u, incl, uint32_t>(a, b, c, d, e, f, g, h);
}

constexpr u32 STK = 0x10080000u, LEN = 0x400u;  // the guest stack window the cases use: [STK - LEN, STK)
static void call(bool sa) {
    Cpu* c = gabi::cpu;
    gabi::ArgPack args{c};
    for (int i = 0; i < 8; i++) args.put((f32)(1.5f * i + 0.25f));
    f64 m = 0.75;
    u64 bits;
    std::memcpy(&bits, &m, 8);
    args.ns = 2;
    args.stk[0] = u32(bits >> 32);
    args.stk[1] = u32(bits);
    if (sa) gabi::do_call_sa<0x02000000u>(c, args, 0);
    else gabi::do_call(c, args, 0x02000000u, 0);
}
int main() {
    void* want = PPC_MEM_BASE + (STK - 0x10000u);
    if (mmap(want, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != want) { std::printf("mmap failed\n"); return 2; }
    gabi::ThreadState& t = *gabi::gabi_ts_pass();
    static Cpu cpu{};
    t.cpu = &cpu;
    int bad = 0;
    struct Case { const char* name; bool fc_on, active, at_entry; u32 fsp, lo, cur, hi; } cases[] = {
        {"no automatic frame", false, false, false, 0, 0, 0, 0},
        {"in the original's frame", true, true, true, STK - 0x40, STK - 0x30, STK - 0x30, STK - 0x10},
        {"below the frame's Locals (no room)", true, true, true, STK - 0x40, STK - 0x3C, STK - 0x38, STK - 0x10},
        {"an active frame that is not the running one", true, true, false, STK - 0x40, STK - 0x30, STK - 0x30, STK - 0x10},
        {"an inactive frame", true, false, true, STK - 0x40, STK - 0x30, STK - 0x30, STK - 0x10},
    };
    for (const Case& k : cases) {
        static gabi::FrameCtx fc;
        fc = gabi::FrameCtx{};
        fc.active = k.active;
        fc.entry = STK;
        fc.sp = k.fsp;
        fc.lo = k.lo;
        fc.cur = k.cur;
        fc.hi = k.hi;
        u8 mem[2][LEN];
        u32 r1[2], r3[2], seen[2];
        for (int sa = 0; sa < 2; sa++) {
            for (u32 i = 0; i < LEN; i++) gabi::store<u8>(STK - LEN + i, (u8)(i * 37 + 11));  // stale bytes
            cpu = Cpu{};
            cpu.r[1] = k.at_entry ? STK : STK - 0x80;
            cpu.r[3] = 0xDEADBEEFu;
            t.frame_ctx = k.fc_on ? &fc : nullptr;
            t.depth = 2;
            t.caller_ok = 1;
            t.current = 0x02345678u;
            g_seen_r1 = 0;
            const int g0 = g_guest_calls, n0 = g_native_runs;
            call(sa);
            if (sa ? (g_native_runs != n0 + 1 || g_guest_calls != g0) : (g_guest_calls != g0 + 1)) {
                std::printf("%s: %s path not taken\n", k.name, sa ? "native" : "guest");
                bad++;
            }
            std::memcpy(mem[sa], gabi::at<u8>(STK - LEN), LEN);
            r1[sa] = cpu.r[1];
            r3[sa] = cpu.r[3];
            seen[sa] = g_seen_r1;
            if (t.depth != 2 || t.current != 0x02345678u || t.caller_ok != 1 || t.frame_ctx != (k.fc_on ? &fc : nullptr)) {
                std::printf("%s: call state changed\n", k.name);
                bad++;
            }
        }
        const bool same = !std::memcmp(mem[0], mem[1], LEN) && r1[0] == r1[1] && r3[0] == r3[1] && seen[0] == seen[1];
        if (r3[1] != (u32)compute(0.25f, 1.75f, 9.25f, 0.75f)) { std::printf("%s: wrong result\n", k.name); bad++; }
        std::printf("stackarg_test: %-45s callee r1 %08X/%08X r3 %08X/%08X stack %s\n", k.name, seen[0], seen[1], r3[0], r3[1],
                    same ? "same" : "DIFFERENT");
        if (!same) bad++;
    }
    // 3. caller_ok 0: the guest path
    t.frame_ctx = nullptr;
    t.depth = 2;
    t.caller_ok = 0;
    cpu = Cpu{};
    cpu.r[1] = STK;
    const int g0 = g_guest_calls, n0 = g_native_runs;
    call(true);
    if (g_guest_calls != g0 + 1 || g_native_runs != n0) { std::printf("caller_ok 0: not the guest path\n"); bad++; }
    std::printf("stackarg_test: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
