// SPDX-License-Identifier: MPL-2.0
// Contract test of the replacements that write guest memory and call game code (runtime/src/hostrt):
// the recompiled original and the replacement run from the same CPU state and the same memory, with the
// game functions they call (constructors, destructors, operator new/delete) modelled by deterministic
// stand-ins that write memory and clobber volatile registers. They must agree on
// - every call made: target, r3..r10, the stack (SP offset, back chain, stack-argument words), LR;
// - the memory they and their callees wrote (a scratch region, and the caller's frame above SP);
// - the result registers callers read; the replacement must keep every other register.
// A replacement may hand an input class back to the original (it then runs the original itself).
//
// usage: calltest IMAGE [ROOT|all] [count]
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "hostrt_impl.h"
#include "rt.h"

using namespace difftest;

// threads per run: HOSTRT_TEST_THREADS (default 4; the machine-wide budget is 4 across all runs)
static int test_threads() {
    const char* e = getenv("HOSTRT_TEST_THREADS");
    int n = e ? atoi(e) : 4;
    return n < 1 ? 1 : n > 4 ? 4 : n;
}

extern "C" {
void f_028EFFD0(Cpu*);
void f_028EA09C(Cpu*);
void f_028F0294(Cpu*);
void f_028EFF8C(Cpu*);
void f_028F0020(Cpu*);
void f_0286CA30(Cpu*);
void f_0286CA40(Cpu*);
void f_0286CB2C(Cpu*);
void f_0286CB3C(Cpu*);
void f_0286CB4C(Cpu*);
void f_0286CB5C(Cpu*);
void f_0286CB6C(Cpu*);
void f_0286CBB4(Cpu*);
void f_0286D270(Cpu*);
void f_0286D2D4(Cpu*);
void f_0286D2E4(Cpu*);
void f_0286D3C0(Cpu*);
void f_0286D3D0(Cpu*);
void f_0286D3E0(Cpu*);
void f_0286D3F0(Cpu*);
void f_0286D400(Cpu*);
void f_0286CA50(Cpu*);
void f_0286CAA4(Cpu*);
void f_0286CAE8(Cpu*);
void f_0286D280(Cpu*);
void f_0286D2F4(Cpu*);
void f_0286D338(Cpu*);
void f_0286CB7C(Cpu*);
void f_0286C590(Cpu*);
void f_0286C9CC(Cpu*);
void f_0286D37C(Cpu*);
void f_0286D620(Cpu*);
void f_0286D938(Cpu*);
void f_0286D9E0(Cpu*);
void f_0286C5F8(Cpu*);
void f_0286CD7C(Cpu*);
void f_0286DA70(Cpu*);
void f_028F5C88(Cpu*);
void f_028F1748(Cpu*);
void f_028F17A8(Cpu*);
void f_028F186C(Cpu*);
void f_0286DA9C(Cpu*);
void f_0286DACC(Cpu*);
void f_0286DB18(Cpu*);
void f_028F0164(Cpu*);
void f_028F026C(Cpu*);
void f_028F521C(Cpu*);
void f_028F5914(Cpu*);
void f_028F040C(Cpu*);
void f_028EA038(Cpu*);
void f_028F2EE8(Cpu*);
void f_028F3090(Cpu*);
}

namespace {

constexpr uint32_t kScratch = 0xE0000000u, kScratchSize = 0x40000;  // per slot: + slot * 0x100000
constexpr uint32_t kFakeBase = 0x02100000u;                          // fake constructor/destructor addresses

uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

thread_local uint32_t t_heap;
thread_local Cpu t_input;  // the input being tested (for reports)

// deterministic stand-ins for the port's coreinit functions the nn wrappers call
bool import_model(Cpu* c, const char* name) {
    if (!strcmp(name, "coreinit_MEMAllocFromExpHeapEx")) {
        c->r[3] = (mix(c->r[4] ^ c->r[5] ^ t_heap) & 7) ? t_heap : 0;
        if (c->r[3]) t_heap += (c->r[4] + 15) & ~15u;
        return true;
    }
    const uint32_t h = mix(c->r[3] ^ mix(c->r[4] ^ mix(c->r[5])));
    if (!strcmp(name, "coreinit_MEMCreateExpHeapEx")) {
        c->r[3] = (h & 7) ? 0xE0F00000u | (h & 0xFFF0) : 0;
        return true;
    }
    if (!strcmp(name, "coreinit_MEMGetAllocatableSizeForExpHeapEx")) {
        static const uint32_t avail[] = {0x01600000u, 0x01500000u, 0x01100000u, 0x01900000u, 0x01700000u};
        c->r[3] = avail[h % 5];  // both sides of the 0x380000 check for both loaders
        return true;
    }
    if (!strcmp(name, "coreinit_OSDynLoad_SetAllocator")) {
        c->r[3] = 0;
        return true;
    }
    if (!strcmp(name, "coreinit_OSDynLoad_Acquire")) {
        st32(c->r[4], 0x70000000u | (h & 0xFFFF));
        c->r[3] = (h & 7) ? 0 : 0xFFFFFFF1u;
        return true;
    }
    if (!strcmp(name, "coreinit_OSDynLoad_FindExport")) {
        st32(c->r[6], kFakeBase + (h & 0xFFFC));
        c->r[3] = 0;
        return true;
    }
    if (!strcmp(name, "coreinit_UCOpen")) {
        c->r[3] = (h & 7) ? h & 0xFF : 0xFFFFFFF0u | (h & 0xF);
        return true;
    }
    if (!strcmp(name, "coreinit_UCReadSysConfig")) {
        const uint32_t req = c->r[5], size = ld32(req + 0x4C), dst = ld32(req + 0x50);
        for (uint32_t i = 0; i < size && i < 16; i++) st8(dst + i, (uint8_t)mix(h + i));
        static const uint32_t errs[] = {0, 0, 0, 0xFFDFFFF7u, 0xFFDFFFEBu, 0xFFDFFF00u};
        c->r[3] = errs[h % 6];
        return true;
    }
    if (!strcmp(name, "coreinit_UCClose") || !strcmp(name, "coreinit_OSReport")) {
        c->r[3] = 0;
        return true;
    }
    if (!strncmp(name, "nn_act_GetParentalControlSlotNoEx", 33)) {
        st8(c->r[3], (uint8_t)(h >> 8));
        static const uint32_t res[] = {0, 0, 0, 0xA070F480u, 0xC070FA80u, 0xC0712C80u, 0x80000001u};
        c->r[3] = res[h % 7];
        return true;
    }
    if (!strcmp(name, "coreinit_MEMDestroyExpHeap") || !strcmp(name, "coreinit_OSDynLoad_Release")) {
        c->r[3] = mix(c->r[3]);
        return true;
    }
    return false;
}

bool import_report(Cpu* c, const char* name) {
    if (import_model(c, name)) return true;
    fprintf(stderr, "calltest: import %s (lr %08X) for input r3 %08X r4 %08X r5 %08X r6 %08X r7 %08X r8 %08X\n", name, c->lr,
            t_input.r[3], t_input.r[4], t_input.r[5], t_input.r[6], t_input.r[7], t_input.r[8]);
    return false;
}  // the bump pointer of the operator new stand-in (reset before each run)

// stand-ins for the game functions the runtime calls: operator new / new[] (bump allocation in the
// scratch region, every 16th request fails), operator delete / delete[], and fake constructors and
// destructors (they mark the object and clobber the volatile registers)
bool model(Cpu* c, uint32_t addr) {
    if (addr == 0x0273AD10u || addr == 0x0273ADACu) {
        const uint32_t n = c->r[3];
        c->r[3] = (mix(n ^ t_heap) & 15) ? t_heap : 0;
        if (c->r[3]) t_heap += (n + 15) & ~15u;
        return true;
    }
    if (addr == 0x0273AF40u || addr == 0x0273AFC8u) return true;
    if ((addr & 0xFFF00000u) != kFakeBase) return false;
    const uint32_t h = mix(c->r[3] ^ addr);
    if (c->r[3] >= 0xE0000000u) st32(c->r[3], h);
    c->r[0] = mix(h + 1);
    for (int i = 4; i <= 12; i++) c->r[i] = mix(h + i);
    for (int i = 0; i <= 13; i++) c->f[i].ps0 = u64_as_f64((uint64_t)mix(h + 100 + i) << 32 | mix(h + 200 + i));
    c->ctr = mix(h + 300);
    for (int i = 0; i < 8; i++) c->cr[i] = mix(h + 400 + i) & 1;
    c->r[3] = (h & 3) ? mix(h + 3) : 0;  // results are 0 now and then (paths that test them)
    return true;
}

enum Out { R3 = 1, R4 = 2, TAIL = 0x100 };  // TAIL: a tail call, every register must end as the original's

struct Root {
    const char* name;
    void (*orig)(Cpu*);
    bool (*repl)(Cpu*);
    std::function<void(Cpu&, std::mt19937_64&, uint32_t scratch)> gen;
    int outs;
    std::vector<uint32_t> globals = {};  // guest words outside the scratch region the function may write
};

template <void (*F)(Cpu*)>
bool always(Cpu* c) {
    F(c);
    return true;
}

// a list {count, sentinel{next, prev}} with k nodes in a ring through the sentinel, in the scratch region,
// and the arguments for one list operation (op 1 insert, 2 erase, 3 erase range, 4 clear)
void list_args(Cpu& c, std::mt19937_64& r, uint32_t s, int op) {
    const uint32_t list = s + 0x100, sent = list + 4;
    const int k = (int)(r() % 7);
    std::vector<uint32_t> ring{sent};
    for (int i = 0; i < k; i++) ring.push_back(s + 0x400 + 0x40 * (uint32_t)i + ((uint32_t)r() & 0x38));
    for (size_t i = 0; i < ring.size(); i++) {
        st32(ring[i], ring[(i + 1) % ring.size()]);
        st32(ring[i] + 4, ring[(i + ring.size() - 1) % ring.size()]);
    }
    st32(list, (uint32_t)k);
    auto it = [&](uint32_t node) {  // an iterator: a word holding a node pointer
        const uint32_t w = s + 0x2000 + ((uint32_t)r() & 0xFF0);
        st32(w, node);
        return w;
    };
    const uint32_t a = (uint32_t)(r() % ring.size());
    c.r[3] = list;
    if (op == 1) c.r[4] = it(ring[a]), c.r[5] = s + 0x3000 + ((uint32_t)r() & 0xFF8);
    if (op == 2) c.r[4] = k ? ring[1 + r() % k] : sent;
    if (op == 3) {
        const uint32_t b = a + (uint32_t)(r() % (ring.size() - a));  // end at or after first, on the ring
        c.r[4] = it(ring[a]), c.r[5] = it(b == 0 && a == 0 ? sent : ring[b]);
        if (a == 0 && b != 0) c.r[4] = it(ring[1 % ring.size()]);
    }
    if (op == 4) c.r[4] = it(ring[a]);
}

// a random printf format (mostly integer, string and char conversions; some float ones, which the
// replacement hands back) and a va_list over random arguments, in the scratch region
uint32_t random_format(std::mt19937_64& r, uint32_t at) {
    static const char* flags[] = {"", "", "", "-", "+", " ", "#", "0", "-0", "+ ", "#0", "-#"};
    static const char* lens[] = {"", "", "", "h", "hh", "l", "ll"};
    static const char* convs = "diuoxXcs%";
    std::string f;
    const int parts = 1 + (int)(r() % 4);
    for (int i = 0; i < parts; i++) {
        if (r() & 1) f += "ab/:"[r() % 4], f += "xyz.%"[r() % 4];
        f += '%';
        f += flags[r() % 12];
        const int w = (int)(r() % 6);
        if (w == 1) f += std::to_string(r() % 20);
        if (w == 2) f += '*';
        const int pr = (int)(r() % 6);
        if (pr == 1) f += "." + std::to_string(r() % 12);
        if (pr == 2) f += ".*";
        if (pr == 3) f += ".";
        f += lens[r() % 7];
        f += (r() % 40) ? convs[r() % 9] : "fegp"[r() % 4];
    }
    if (getenv("CALLTEST_SHOW_FORMAT")) fprintf(stderr, "format: \"%s\"\n", f.c_str());
    for (size_t i = 0; i < f.size(); i++) st8(at + (uint32_t)i, (uint8_t)f[i]);
    st8(at + (uint32_t)f.size(), 0);
    return at;
}

uint32_t random_va(std::mt19937_64& r, uint32_t s) {
    const uint32_t ap = s + 0x100, save = s + 0x200, ovf = s + 0x400 + ((uint32_t)r() & 4);
    st8(ap, (uint8_t)(r() % 10)), st8(ap + 1, (uint8_t)(r() % 9)), st16(ap + 2, (uint16_t)r());
    st32(ap + 4, ovf), st32(ap + 8, save);
    // argument words: small numbers, extremes, random bits, and pointers to strings
    for (uint32_t i = 0; i < 0x60; i++) {
        const uint32_t m = (uint32_t)(r() % 6), at = (i < 8 ? save : ovf - 0x20) + 4 * i;
        uint32_t v = m == 0 ? (uint32_t)(r() % 100) : m == 1 ? (uint32_t)-(int)(r() % 100) : m == 2 ? 0x80000000u
                   : m == 3 ? (uint32_t)r() & 0x0FFFFFFF : s + 0x1000 + 0x40 * (uint32_t)(r() % 16);
        st32(at, v);
    }
    for (uint32_t k = 0; k < 16; k++) {  // the strings
        const uint32_t p = s + 0x1000 + 0x40 * k, n = (uint32_t)(r() % 40);
        for (uint32_t i = 0; i < n; i++) st8(p + i, (uint8_t)(0x20 + r() % 0x5F));
        st8(p + n, 0);
    }
    return ap;
}

// variadic arguments in r<first>..r10, f1..f8 (CR1.eq says whether floats were passed) and the stack
void varargs_regs(Cpu& c, std::mt19937_64& r, uint32_t s, int first) {
    for (int i = first; i <= 10; i++) {
        const uint32_t m = (uint32_t)(r() % 5);
        c.r[i] = m == 0 ? (uint32_t)(r() % 100) : m == 1 ? (uint32_t)-(int)(r() % 100) : m == 2 ? (uint32_t)r() & 0x0FFFFFFF
                                                                                       : s + 0x1000 + 0x40 * (uint32_t)(r() % 16);
    }
    for (uint32_t k = 0; k < 16; k++) {
        const uint32_t p = s + 0x1000 + 0x40 * k, n = (uint32_t)(r() % 40);
        for (uint32_t i = 0; i < n; i++) st8(p + i, (uint8_t)(0x20 + r() % 0x5F));
        st8(p + n, 0);
    }
    c.cr[6] = r() & 1;
    for (uint32_t i = 0; i < 24; i++) st32(c.r[1] + 8 + 4 * i, (uint32_t)(r() % 1000));  // stack arguments
}
bool snprintf_wrap(Cpu* c) { return hostrt::snprintf_028F17A8(c); }
bool sprintf_wrap(Cpu* c) { return hostrt::sprintf_028F186C(c); }

// arguments of the array-new family: p (0: allocate), n, size, ctor, dtor
void array_args(Cpu& c, std::mt19937_64& r, uint32_t s) {
    static const uint32_t sizes[] = {1, 4, 8, 12, 0x2C, 0x12C};
    c.r[3] = (r() & 3) ? s + 0x108 + ((uint32_t)r() & 0xFF8) : 0;
    c.r[4] = (r() & 15) ? (uint32_t)(r() % 7) : (uint32_t)(r() % 60);
    c.r[5] = sizes[r() % 6];
    c.r[6] = (r() & 7) ? kFakeBase + ((uint32_t)r() & 0xFFFC) : 0;
    c.r[7] = (r() & 1) ? kFakeBase + ((uint32_t)r() & 0xFFFC) : 0;
    st32(0x101FCD08u, (r() % 8) ? 8 : (uint32_t)(r() % 4) * 8);  // the header size (not 8: handed back)
}

// a number string (white space, sign, prefix, digits of random bases, trailing junk), endptr, base
void strto_args(Cpu& c, std::mt19937_64& r, uint32_t s) {
    std::string t;
    static const char* ws = " \t\n\b\r\v\f";
    for (int i = (int)(r() % 3); i > 0; i--) t += ws[r() % 7];
    if (r() % 3 == 0) t += "+-"[r() % 2];
    if (r() % 4 == 0) t += (r() & 1) ? "0x" : "0X";
    else if (r() % 4 == 0) t += "0";
    static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFXZ";
    for (int i = (int)(r() % ((r() & 3) ? 8 : 24)); i > 0; i--) t += digits[r() % 44];
    if (r() & 1) t += "_ -"[r() % 3];
    const uint32_t str = s + 0x1000;
    for (size_t i = 0; i < t.size(); i++) st8(str + (uint32_t)i, (uint8_t)t[i]);
    st8(str + (uint32_t)t.size(), 0);
    c.r[3] = str, c.r[4] = (r() % 5) ? s + 0x2000 : 0;
    static const uint32_t bases[] = {0, 10, 16, 8, 2, 36, 1, 37, 0xFFFFFFFF, 7};
    c.r[5] = (r() & 1) ? bases[r() % 10] : (uint32_t)(r() % 40);
}

// the wrappers' tables (filled by the generator after the globals were randomised) and arguments
void nn_args(Cpu& c, std::mt19937_64& r, uint32_t s) {
    for (uint32_t a = 0x104B6740u; a < 0x104B6840u; a += 4) st32(a, kFakeBase + ((uint32_t)r() & 0xFFFC));
    st32(0x104B6798u, s + 0x7000), st32(0x104B682Cu, s + 0x7100);  // heap handles
    st8(0x101FC898u, (uint8_t)(r() & 1));
    c.r[3] = s + 0x100 + ((uint32_t)r() & 0xFF0);
    if (r() & 1) c.r[3] = (uint32_t)r() % 0x200;  // sizes, for the allocator
    c.r[4] = (uint32_t)r() % 64, c.r[5] = s + 0x200 + ((uint32_t)r() & 0xFF0);
}

void sci_args(Cpu& c, std::mt19937_64& r, uint32_t s) {
    c.r[3] = s + 0x100 + ((uint32_t)r() & 0xFF0);  // the value's destination
    c.r[4] = (uint32_t)r() % 16;                    // the account
}

// a CreateArg {buffer, region, language, FSClient} with now and then a null buffer or client
void loader_args(Cpu& c, std::mt19937_64& r, uint32_t s) {
    nn_args(c, r, s);
    const uint32_t arg = s + 0x100;
    st32(arg, (r() % 8) ? s + 0x10000 : 0), st32(arg + 4, (uint32_t)r() % 8), st32(arg + 8, (uint32_t)r() % 16);
    st32(arg + 0xC, (r() % 8) ? s + 0x8000 : 0);
    c.r[3] = arg;
}

std::vector<Root> roots() {
    return {
        {"construct_array", f_028EFFD0, hostrt::construct_array_028EFFD0,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             static const uint32_t sizes[] = {1, 4, 8, 12, 0x2C, 0x12C};
             c.r[3] = (r() & 7) ? s + 0x100 + ((uint32_t)r() & 0xFFC) : 0;
             c.r[4] = (r() & 15) ? (uint32_t)(r() % 7) : (uint32_t)(r() % 60);
             c.r[5] = sizes[r() % 6];
             c.r[6] = (r() & 7) ? kFakeBase + ((uint32_t)r() & 0xFFFC) : 0;
             st32(0x101FCD08u, (r() % 8) ? 8 : (uint32_t)(r() % 4) * 8);  // the header size (not 8: handed back)
         },
         R3 | R4, {0x101FCD08u}},
        {"vec_new", f_028EFF8C, hostrt::vec_new_028EFF8C, array_args, R3 | R4, {0x101FCD08u}},
        {"vec_new_zero", f_028F0020, hostrt::vec_new_zero_028F0020, array_args, R3 | R4, {0x101FCD08u}},
        {"construct_array_arg", f_028F0294, always<hostrt::construct_array_arg_028F0294>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             array_args(c, r, s);
             if (!c.r[3]) c.r[3] = s + 0x108;
             c.r[7] = s + 0x2000 + ((uint32_t)r() & 0xFF8);
         },
         R3 | R4},
        {"static_init", f_028EA09C, always<hostrt::static_init_028EA09C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t) {
             const uint32_t n = (uint32_t)(r() % 12);
             for (uint32_t i = 0; i < n; i++) st32(0x1018AC20u + 4 * i, kFakeBase + ((uint32_t)r() & 0xFFFC) + 4);
             st32(0x1018AC20u + 4 * n, 0);
             st32(0x101FCBB8u, (r() % 4) ? 0 : 1);
         },
         R3 | R4, {0x101FCBB8u, 0x1018AC20u, 0x1018AC24u, 0x1018AC28u, 0x1018AC2Cu, 0x1018AC30u, 0x1018AC34u, 0x1018AC38u, 0x1018AC3Cu,
                   0x1018AC40u, 0x1018AC44u, 0x1018AC48u, 0x1018AC4Cu, 0x1018AC50u}},
        {"destroy_arr", f_028F0164, hostrt::destroy_arr_028F0164,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             static const uint32_t sizes[] = {1, 4, 8, 12, 0x2C, 0x12C};
             const uint32_t size = sizes[r() % 6], n = (r() & 15) ? (uint32_t)(r() % 7) : (uint32_t)(r() % 40);
             const uint32_t p = (r() & 15) ? s + 0x108 + ((uint32_t)r() & 0xFF8) : 0;
             const bool header = p && (r() & 1);
             if (header) st32(p - 8, (r() & 15) ? n * size : (uint32_t)r() % 64), st32(p - 4, ~n);
             c.r[3] = p, c.r[4] = header ? 0xFFFFFFFFu : n, c.r[5] = size;
             c.r[6] = (r() & 7) ? kFakeBase + ((uint32_t)r() & 0xFFFC) : 0;
             c.r[7] = (r() & 1) ? 0 : (uint32_t)(r() % 4);
             c.r[8] = (uint32_t)r();
             st32(0x101FCD08u, (r() % 8) ? 8 : (uint32_t)(r() % 4) * 8);
         },
         R3 | R4, {0x101FCD08u}},
        {"list_insert", f_0286DA70, always<hostrt::list_insert_0286DA70>, [](Cpu& c, std::mt19937_64& r, uint32_t s) { list_args(c, r, s, 1); }, R3 | R4},
        {"list_erase", f_0286DA9C, always<hostrt::list_erase_0286DA9C>, [](Cpu& c, std::mt19937_64& r, uint32_t s) { list_args(c, r, s, 2); }, R3 | R4},
        {"list_erase_range", f_0286DACC, always<hostrt::list_erase_range_0286DACC>, [](Cpu& c, std::mt19937_64& r, uint32_t s) { list_args(c, r, s, 3); }, R3 | R4},
        {"list_clear", f_0286DB18, always<hostrt::list_clear_0286DB18>, [](Cpu& c, std::mt19937_64& r, uint32_t s) { list_args(c, r, s, 4); }, R3 | R4},
        {"thunk_0286CA30", f_0286CA30, always<hostrt::export_thunk_0286CA30>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6774u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6774u}},
        {"thunk_0286CA40", f_0286CA40, always<hostrt::export_thunk_0286CA40>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6790u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6790u}},
        {"thunk_0286CB2C", f_0286CB2C, always<hostrt::export_thunk_0286CB2C>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6748u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6748u}},
        {"thunk_0286CB3C", f_0286CB3C, always<hostrt::export_thunk_0286CB3C>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6764u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6764u}},
        {"thunk_0286CB4C", f_0286CB4C, always<hostrt::export_thunk_0286CB4C>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6784u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6784u}},
        {"thunk_0286CB5C", f_0286CB5C, always<hostrt::export_thunk_0286CB5C>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6780u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6780u}},
        {"thunk_0286CB6C", f_0286CB6C, always<hostrt::export_thunk_0286CB6C>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6778u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6778u}},
        {"thunk_0286CBB4", f_0286CBB4, always<hostrt::export_thunk_0286CBB4>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B6770u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B6770u}},
        {"thunk_0286D270", f_0286D270, always<hostrt::export_thunk_0286D270>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67E4u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67E4u}},
        {"thunk_0286D2D4", f_0286D2D4, always<hostrt::export_thunk_0286D2D4>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67F8u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67F8u}},
        {"thunk_0286D2E4", f_0286D2E4, always<hostrt::export_thunk_0286D2E4>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67A8u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67A8u}},
        {"thunk_0286D3C0", f_0286D3C0, always<hostrt::export_thunk_0286D3C0>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67ECu, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67ECu}},
        {"thunk_0286D3D0", f_0286D3D0, always<hostrt::export_thunk_0286D3D0>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67F0u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67F0u}},
        {"thunk_0286D3E0", f_0286D3E0, always<hostrt::export_thunk_0286D3E0>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67A4u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67A4u}},
        {"thunk_0286D3F0", f_0286D3F0, always<hostrt::export_thunk_0286D3F0>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67C4u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67C4u}},
        {"thunk_0286D400", f_0286D400, always<hostrt::export_thunk_0286D400>, [](Cpu& c, std::mt19937_64& r, uint32_t) { st32(0x104B67D8u, kFakeBase + ((uint32_t)r() & 0xFFFC)); }, TAIL, {0x104B67D8u}},
        {"va_arg", f_028F5C88, always<hostrt::va_arg_028F5C88>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             const uint32_t ap = s + 0x100 + ((uint32_t)r() & 0xF0);
             st8(ap, (r() & 3) ? (uint8_t)(r() % 10) : (uint8_t)r());
             st8(ap + 1, (r() & 3) ? (uint8_t)(r() % 10) : (uint8_t)r());
             st32(ap + 4, s + 0x1000 + ((uint32_t)r() & 0xFFF));  // overflow area, any alignment
             st32(ap + 8, s + 0x3000 + ((uint32_t)r() & 0xFF8));  // register save area
             c.r[3] = ap, c.r[4] = (r() & 3) ? 1 + (uint32_t)(r() % 3) : (uint32_t)(r() % 16);
         },
         R3},
        {"vsnprintf", f_028F1748, hostrt::vsnprintf_028F1748,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + 0x2000, c.r[4] = (r() & 3) ? 0x200 : (uint32_t)(r() % 20);
             c.r[5] = random_format(r, s + 0x3000), c.r[6] = random_va(r, s);
         },
         R3 | R4},
        {"snprintf", f_028F17A8, snprintf_wrap,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + 0x2000, c.r[4] = (r() & 3) ? 0x200 : (uint32_t)(r() % 20);
             c.r[5] = random_format(r, s + 0x3000);
             varargs_regs(c, r, s, 6);
         },
         R3 | R4},
        {"sprintf", f_028F186C, sprintf_wrap,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + 0x2000, c.r[4] = random_format(r, s + 0x3000);
             varargs_regs(c, r, s, 5);
         },
         R3 | R4},
        {"memmove", f_028EA038, always<hostrt::memmove_028EA038>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + 0x1000 + ((uint32_t)r() & 0x3FF), c.r[4] = s + 0x1000 + ((uint32_t)r() & 0x3FF);
             c.r[5] = (r() & 3) ? (uint32_t)(r() % 64) : (uint32_t)(r() % 0x800);
         },
         0},
        {"strtoul", f_028F2EE8, always<hostrt::strtoul_028F2EE8>, strto_args, R3 | R4},
        {"strtol", f_028F3090, always<hostrt::strtol_028F3090>, strto_args, R3 | R4},
        {"wrap_0286CA50", f_0286CA50, always<hostrt::wrap_0286CA50>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"wrap_0286CAA4", f_0286CAA4, always<hostrt::wrap_0286CAA4>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"wrap_0286CAE8", f_0286CAE8, always<hostrt::wrap_0286CAE8>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"wrap_0286D280", f_0286D280, always<hostrt::wrap_0286D280>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"wrap_0286D2F4", f_0286D2F4, always<hostrt::wrap_0286D2F4>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"wrap_0286D338", f_0286D338, always<hostrt::wrap_0286D338>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"wrap_0286CB7C", f_0286CB7C, always<hostrt::wrap_0286CB7C>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"heap_0286C590", f_0286C590, always<hostrt::heap_alloc_0286C590>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"erreula_0286C9CC", f_0286C9CC, always<hostrt::erreula_destroy_0286C9CC>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"swkbd_0286D37C", f_0286D37C, always<hostrt::swkbd_destroy_0286D37C>, nn_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"sci_0286D620", f_0286D620, always<hostrt::sci_language_0286D620>, sci_args, R3 | R4},
        {"sci_0286D938", f_0286D938, always<hostrt::sci_parental_0286D938>, sci_args, R3 | R4},
        {"sci_0286D9E0", f_0286D9E0, always<hostrt::sci_parental_0286D9E0>, sci_args, R3 | R4},
        {"erreula_create", f_0286C5F8, always<hostrt::erreula_create_0286C5F8>, loader_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"swkbd_create", f_0286CD7C, always<hostrt::swkbd_create_0286CD7C>, loader_args, R3 | R4, {0x104B6740u, 0x104B6744u, 0x104B6748u, 0x104B674Cu, 0x104B6750u, 0x104B6754u, 0x104B6758u, 0x104B675Cu, 0x104B6760u, 0x104B6764u, 0x104B6768u, 0x104B676Cu, 0x104B6770u, 0x104B6774u, 0x104B6778u, 0x104B677Cu, 0x104B6780u, 0x104B6784u, 0x104B6788u, 0x104B678Cu, 0x104B6790u, 0x104B6794u, 0x104B6798u, 0x104B679Cu, 0x104B67A0u, 0x104B67A4u, 0x104B67A8u, 0x104B67ACu, 0x104B67B0u, 0x104B67B4u, 0x104B67B8u, 0x104B67BCu, 0x104B67C0u, 0x104B67C4u, 0x104B67C8u, 0x104B67CCu, 0x104B67D0u, 0x104B67D4u, 0x104B67D8u, 0x104B67DCu, 0x104B67E0u, 0x104B67E4u, 0x104B67E8u, 0x104B67ECu, 0x104B67F0u, 0x104B67F4u, 0x104B67F8u, 0x104B67FCu, 0x104B6800u, 0x104B6804u, 0x104B6808u, 0x104B680Cu, 0x104B6810u, 0x104B6814u, 0x104B6818u, 0x104B681Cu, 0x104B6820u, 0x104B6824u, 0x104B6828u, 0x104B682Cu, 0x104B6830u, 0x104B6834u, 0x104B6838u, 0x104B683Cu, 0x101FC898u}},
        {"register_global", f_028F026C, always<hostrt::register_global_object_028F026C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             const uint32_t rec = s + 0x100 + ((uint32_t)r() & 0xFF0);
             c.r[3] = rec;
             if (r() & 1) st32(rec, 0);
             const uint32_t m = r() % 4;
             st32(0x104DA130u, m == 0 ? rec : m == 1 ? 0 : s + ((uint32_t)r() & 0xFFF0));
         },
         R3 | R4, {0x104DA130u}},
        {"bzero", f_028F521C, always<hostrt::bzero_028F521C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + ((uint32_t)r() & 0x3FFF);
             c.r[4] = (r() & 3) ? (uint32_t)(r() % 0x200) : (uint32_t)(r() % 0x8000);
         },
         R3 | R4},
        {"zero_words", f_028F5914, always<hostrt::zero_words_028F5914>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + ((uint32_t)r() & 0x3FFC);
             c.r[4] = (r() & 3) ? (uint32_t)(r() % 40) : (uint32_t)(r() % 0x2000);
         },
         R3 | R4},
        {"strcpy", f_028F040C, always<hostrt::strcpy_028F040C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             const uint32_t src = s + 0x8000 + ((uint32_t)r() & 0xFFF), len = (uint32_t)(r() % ((r() & 3) ? 40 : 0x400));
             for (uint32_t i = 0; i < len; i++) st8(src + i, (uint8_t)(1 + r() % 255));
             st8(src + len, 0);
             // destination: disjoint, or overlapping at or below the source (a forward copy ahead of it never ends)
             c.r[3] = (r() & 3) ? s + ((uint32_t)r() & 0x3FFF) : src - (uint32_t)(r() % 64);
             c.r[4] = src;
         },
         R3 | R4},
    };
}

void randomise(Cpu& c, std::mt19937_64& r, int slot) {
    reset(&c, slot);
    for (int i = 0; i < 32; i++)
        if (i != 1) c.r[i] = (uint32_t)r();
    for (int i = 0; i < 32; i++) c.f[i].ps0 = u64_as_f64(r()), c.f[i].ps1 = u64_as_f64(r());
    for (int i = 0; i < 32; i++) c.cr[i] = r() & 1;
    c.ctr = (uint32_t)r(), c.lr = 0x02000000u | ((uint32_t)r() & 0xFFFFFC);
    c.xer_ca = r() & 1, c.xer_so = r() & 1, c.xer_ov = r() & 1;
}

bool kept(const Cpu& e, const Cpu& b, int outs) {
    for (int i = 0; i < 32; i++) {
        if ((i == 3 && (outs & R3)) || (i == 4 && (outs & R4))) continue;
        if (e.r[i] != b.r[i]) return false;
    }
    return !memcmp(e.f, b.f, sizeof e.f) && !memcmp(e.cr, b.cr, sizeof e.cr) && e.ctr == b.ctr && e.lr == b.lr &&
           e.xer_ca == b.xer_ca && e.xer_so == b.xer_so && e.xer_ov == b.xer_ov;
}

bool same_call(const Call& a, const Call& b) {
    if (a.addr != b.addr || (a.import == nullptr) != (b.import == nullptr)) return false;
    if (a.import && strcmp(a.import, b.import)) return false;
    if (a.import) {  // the port's host functions: only their arguments matter, not where on the stack
        static const std::pair<const char*, int> counts[] = {
            {"coreinit___gh_set_errno", 1}, {"coreinit_memset", 3}, {"coreinit_UCOpen", 1}, {"coreinit_UCClose", 1},
            {"coreinit_UCReadSysConfig", 3}, {"coreinit_OSReport", 4}, {"nn_act_GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc", 2},
            {"coreinit_MEMAllocFromExpHeapEx", 3}, {"coreinit_MEMCreateExpHeapEx", 3},
            {"coreinit_MEMGetAllocatableSizeForExpHeapEx", 2}, {"coreinit_OSDynLoad_SetAllocator", 2}, {"coreinit_OSDynLoad_Acquire", 2},
            {"coreinit_OSDynLoad_FindExport", 4}, {"coreinit_MEMDestroyExpHeap", 1}, {"coreinit_OSDynLoad_Release", 1}};
        int nargs = 8;
        for (auto& [n, k] : counts)
            if (!strcmp(a.import, n)) nargs = k;
        if (!strcmp(a.import, "coreinit_OSReport")) {  // the format and the arguments it uses
            nargs = 1;
            for (uint32_t i = a.r[3]; ld8(i); i++)
                if (ld8(i) == '%') nargs++;
        }
        for (int i = 3; i < 3 + nargs; i++)
            if (a.r[i] != b.r[i]) return false;
        return true;
    }
    for (int i = 1; i <= 10; i++)
        if (i != 2 && a.r[i] != b.r[i]) return false;
    // the stack below the entry SP is the functions' scratch (as in tools/verify): compared are the back
    // chain and the first stack-argument word (SP+8), which the originals write for some callees
    return a.stack[0] == b.stack[0] && a.lr == b.lr && a.back == b.back;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: calltest IMAGE [ROOT|all] [count]\n");
        return 2;
    }
    load_image(argv[1]);
    outside_hook = model;
    import_hook = import_report;
    const std::string which = argc > 2 ? argv[2] : "all";
    const uint64_t count = argc > 3 ? strtoull(argv[3], nullptr, 0) : 1000000ull;
    int bad = 0;
    for (const Root& rt : roots()) {
        if (which != "all" && which != rt.name) continue;
        const int nth = rt.globals.empty() ? test_threads() : 1;  // globals are shared guest memory: one thread
        std::vector<uint64_t> fails(nth), handed(nth), calls(nth);
        std::vector<std::thread> th;
        for (int k = 0; k < nth; k++)
            th.emplace_back([&, k] {
                std::mt19937_64 r(0xCA11000u + k);
                const uint32_t s = kScratch + 0x100000u * k;
                std::vector<uint8_t> init(kScratchSize), after_a(kScratchSize);
                Cpu e, a, b;
                CallLog la, lb;
                for (uint64_t i = k; i < count; i += nth) {
                    for (uint32_t j = 0; j < kScratchSize; j += 4) {
                        uint32_t w = (uint32_t)r();
                        memcpy(&init[j], &w, 4);
                    }
                    randomise(e, r, k);
                    memcpy(PPC_MEM_BASE + s, init.data(), kScratchSize);  // the generator may plant data in it
                    std::vector<uint32_t> g_init;
                    for (uint32_t g : rt.globals) st32(g, (uint32_t)r());
                    rt.gen(e, r, s);
                    memcpy(init.data(), PPC_MEM_BASE + s, kScratchSize);
                    for (uint32_t g : rt.globals) g_init.push_back(ld32(g));
                    const uint32_t sp = e.r[1];
                    t_input = e;
                    uint8_t frame_a[0x20], frame_init[0x20], below[0x400];
                    for (int j = 0; j < 0x20; j++) frame_init[j] = (uint8_t)r();
                    // the stack below SP starts with the same garbage for both runs, so stack words a
                    // replacement fails to write (or writes but the original does not) show in the calls
                    for (int j = 0; j < 0x400; j++) below[j] = (uint8_t)r();
                    // replacement
                    memcpy(PPC_MEM_BASE + s, init.data(), kScratchSize);
                    memcpy(PPC_MEM_BASE + sp, frame_init, 0x20);
                    memcpy(PPC_MEM_BASE + sp - 0x400, below, 0x400);
                    for (size_t g = 0; g < rt.globals.size(); g++) st32(rt.globals[g], g_init[g]);
                    b = e, t_entry_sp = sp, lb.clear(), t_log = &lb, t_heap = s + 0x20000;
                    if (!rt.repl(&b)) {  // handed back: the hook runs the original itself, nothing to compare
                        handed[k]++;
                        // ... provided the replacement changed nothing before handing back
                        if ((!lb.empty() || memcmp(init.data(), PPC_MEM_BASE + s, kScratchSize) || memcmp(frame_init, PPC_MEM_BASE + sp, 0x20) ||
                             !kept(e, b, 0)) && fails[k]++ < 3)
                            printf("  %s FAIL: changed state before handing back (r3 %08X r4 %08X)\n", rt.name, e.r[3], e.r[4]);
                        continue;
                    }
                    std::vector<uint8_t> after_b(PPC_MEM_BASE + s, PPC_MEM_BASE + s + kScratchSize);
                    uint8_t frame_b[0x20];
                    memcpy(frame_b, PPC_MEM_BASE + sp, 0x20);
                    std::vector<uint32_t> g_b;
                    for (uint32_t g : rt.globals) g_b.push_back(ld32(g));
                    // original
                    memcpy(PPC_MEM_BASE + s, init.data(), kScratchSize);
                    memcpy(PPC_MEM_BASE + sp, frame_init, 0x20);
                    memcpy(PPC_MEM_BASE + sp - 0x400, below, 0x400);
                    for (size_t g = 0; g < rt.globals.size(); g++) st32(rt.globals[g], g_init[g]);
                    a = e, t_entry_sp = sp, la.clear(), t_log = &la, t_heap = s + 0x20000;
                    rt.orig(&a);
                    memcpy(after_a.data(), PPC_MEM_BASE + s, kScratchSize);
                    memcpy(frame_a, PPC_MEM_BASE + sp, 0x20);
                    std::vector<uint32_t> g_a;
                    for (uint32_t g : rt.globals) g_a.push_back(ld32(g));
                    calls[k] += lb.size();
                    std::string why;
                    if (la.size() != lb.size()) why = "call count";
                    else
                        for (size_t j = 0; j < la.size() && why.empty(); j++)
                            if (!same_call(la[j], lb[j])) why = "call " + std::to_string(j);
                    if (why.empty() && memcmp(after_a.data(), after_b.data(), kScratchSize)) why = "memory";
                    if (why.empty() && memcmp(frame_a, frame_b, 0x20)) why = "caller frame";
                    for (size_t g = 0; g < rt.globals.size() && why.empty(); g++)
                        if (g_b[g] != g_a[g]) why = "global";
                    if (why.empty() && (((rt.outs & R3) && a.r[3] != b.r[3]) || ((rt.outs & R4) && a.r[4] != b.r[4]))) why = "result";
                    if (why.empty() && (rt.outs & TAIL) && (memcmp(a.r, b.r, sizeof a.r) || memcmp(a.f, b.f, sizeof a.f) ||
                                                             memcmp(a.cr, b.cr, sizeof a.cr) || a.ctr != b.ctr || a.lr != b.lr))
                        why = "registers (tail call)";
                    if (why.empty() && !(rt.outs & TAIL) && !kept(e, b, rt.outs)) why = "registers kept";
                    if (!why.empty() && why.rfind("call ", 0) == 0 && isdigit((unsigned char)why[5]) && fails[k] < 3) {
                        const size_t j = std::stoul(why.substr(5));
                        for (const Call* q : {&la[j], &lb[j]}) {
                            printf("    %s %08X sp-%X back sp-%X lr %08X r:", q == &la[j] ? "orig" : "repl", q->addr, q->r[1], q->back, q->lr);
                            for (int x = 3; x <= 10; x++) printf(" %08X", q->r[x]);
                            printf(" stk %08X %08X %08X %08X\n", q->stack[0], q->stack[1], q->stack[2], q->stack[3]);
                        }
                    }
                    if (!why.empty() && why == "memory" && getenv("CALLTEST_SHOW_TEXT") && fails[k] < 12) {
                        // printf tests: the format (scratch + 0x3000) and both outputs (scratch + 0x2000)
                        auto str = [&](const uint8_t* m, uint32_t off) {
                            std::string t;
                            for (uint32_t i = off; i < off + 200 && m[i]; i++) t += (char)m[i];
                            return t;
                        };
                        printf("    format \"%s\"\n    orig   \"%s\"\n    repl   \"%s\"\n", str(init.data(), 0x3000).c_str(),
                               str(after_a.data(), 0x2000).c_str(), str(after_b.data(), 0x2000).c_str());
                    }
                    if (!why.empty() && fails[k]++ < 3)
                        printf("  %s FAIL (%s): r3 %08X r4 %08X r5 %08X r6 %08X r7 %08X r8 %08X; calls %zu/%zu; result %08X %08X / %08X %08X\n",
                               rt.name, why.c_str(), e.r[3], e.r[4], e.r[5], e.r[6], e.r[7], e.r[8], la.size(), lb.size(), a.r[3], a.r[4],
                               b.r[3], b.r[4]);
                }
            });
        for (auto& t : th) t.join();
        uint64_t f = 0, h = 0, n = 0;
        for (int k = 0; k < nth; k++) f += fails[k], h += handed[k], n += calls[k];
        printf("%-16s inputs %10" PRIu64 "  handed to the original %8" PRIu64 "  guest calls compared %10" PRIu64 "  failures %" PRIu64 "%s\n",
               rt.name, count, h, n, f, f ? "" : "  OK");
        fflush(stdout);
        bad += f != 0;
    }
    return bad ? 1 : 0;
}
