// pass_ts_test: gabi.h GABI_PASS_TS (the port's WWHD_DECOMP_FAST_PASS_TS) with the real gabi.h and glue.cpp's table, miss
// path and gabi_ts_pass (unit 0's definitions, GABI_TLS_OWNER):
//  1. every thread (short-lived ones too, thread pointers reused, signals during lookups) gets its own block from
//     gabi_ts_pass, and a function that uses the block many times through gabi.h's views (gabi::cpu, frame_ctx,
//     Activation::depth / current) sees its own thread's values on every use;
//  2. (run.sh) the compiler keeps one gabi_ts_pass call per function: body() below, with eight uses of the block,
//     calls it once (objdump count).
#define GABI_TLS_OWNER 1
#define DECOMP_UNIT "pass_ts_test"
#include "../glue.cpp"
#include <pthread.h>
#include <csignal>
#include <cstdio>
#include <atomic>
#include <vector>
extern "C" void decomp_register(const char*, uint32_t, const char*, void (*)(Cpu*), void (*)(Cpu*, void (*)(Cpu*))) {}
extern "C" void decomp_register_native(const char*, u32, void*, u32, u32) {}
extern "C" void ppc_trap(Cpu*, uint32_t) { __builtin_trap(); } /* gmem_trap (never reached) */
static std::atomic<long> bad{0}, uses{0}, sig_uses{0};
/* eight uses of the block (each a separate lookup without GABI_PASS_TS) */
extern "C" [[gnu::noinline]] int body(Cpu* want, int round) {
    int ok = 1;
    gabi::cpu = want;
    gabi::Activation::depth = round;
    ok &= (Cpu*)gabi::cpu == want;
    gabi::Activation::current = 0x02000000u + (u32)round;
    ok &= (int)gabi::Activation::depth == round;
    ok &= (u32)gabi::Activation::current == 0x02000000u + (u32)round;
    ok &= &GABI_TLS_ == &gabi::tls;
    ok &= gabi::frame_ctx == (gabi::FrameCtx*)nullptr;
    ok &= gabi::gabi_ts_pass() == &gabi::tls;
    return ok;
}
static void on_sig(int) { sig_uses++; if (gabi::gabi_ts_pass() != &gabi::tls) bad++; }
static void* worker(void* a) {
    Cpu c{};
    for (int i = 0; i < (int)(intptr_t)a; i++) { uses++; if (!body(&c, i)) bad++; }
    gabi::cpu = nullptr;
    return nullptr;
}
int main(int argc, char** argv) {
    signal(SIGUSR1, on_sig);
    const int rounds = argc > 1 ? atoi(argv[1]) : 300;
    for (int r = 0; r < rounds; r++) {
        std::vector<pthread_t> t(24);
        for (int i = 0; i < 24; i++) {
            pthread_create(&t[i], nullptr, worker, (void*)(intptr_t)(100 + (i * 37 + r) % 300));
            if ((r + i) % 5 == 0) pthread_kill(t[i], SIGUSR1);
        }
        for (auto& x : t) pthread_join(x, nullptr);
        Cpu c{};
        if (!body(&c, r)) bad++;
    }
    std::printf("pass_ts_test: %ld bodies on %d threads, %ld lookups in signal handlers, %ld wrong blocks\n", uses.load(),
                rounds * 24, sig_uses.load(), bad.load());
    std::printf("pass_ts_test: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
