/* SPDX-License-Identifier: MPL-2.0
 *
 * lfs / stfs / psq_l / psq_st (float GQR type) must move single-precision values bit for bit like the
 * console's PowerPC (ppc.h ppc_lfs_value / ppc_stfs_bits). The reference conversions below are written
 * out from the documents, independently of ppc.h:
 *   load:  PowerPC User Instruction Set Architecture Book I v2.01, 4.6.2, p. 96;
 *   store: Book I v2.01, 4.6.3, p. 100, and the IBM 750CL User's Manual Table 2-44, p. 105
 *          (exponent < 874: "denormalize and store" / "store zero" -> signed zero).
 * Checked: named values (signalling and quiet NaNs of both signs, +-0, denormals, infinities, a double
 * that is not a single and rounds differently on the host, too large and too small doubles), random
 * doubles across every store case, every one of the 2^32 single words on load (--exhaustive; else a
 * stride), and that a load -> store round trip through guest memory is bit-identical for every class,
 * with the value passed directly and through a Cpu FPR slot, and that for every value a single-precision
 * operation can produce (any non-signalling-NaN single) the host conversion gives the stfs bits
 * (tools/recomp/rawstore.py relies on both). Build it at -O0 and -O2
 * (tools/recomp/test_ppc_f32_exact.py does both); the host conversions it replaced fail here. */
/* -std=c11 hides mmap's MAP_ANONYMOUS on glibc unless a POSIX/default source level is asked for. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include "ppc.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

static int fails;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            if (fails++ < 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
        }                                                 \
    } while (0)

/* ---- reference, literally from the pseudocode (bit 0 = most significant) ---- */
static uint64_t ref_lfs(uint32_t w) {
    uint32_t e = (w >> 23) & 0xFF, f = w & 0x7FFFFF;
    uint64_t b1 = (w >> 30) & 1;
    if (e > 0 && e < 255) { /* normalized: FRT2:4 = not WORD1 */
        uint64_t n = b1 ^ 1;
        return ((uint64_t)(w & 0xC0000000u) << 32) | (n << 61) | (n << 60) | (n << 59) |
               ((uint64_t)(w & 0x3FFFFFFFu) << 29);
    }
    if (e == 0 && f != 0) { /* denormalized: normalize */
        uint64_t sign = w >> 31;
        int exp = -126;
        uint64_t frac = (uint64_t)f << 29; /* frac0:52 = 0b0 || WORD9:31 || 29 zeros, bit 52 = frac0 */
        while (!(frac & (1ull << 52))) { frac <<= 1; exp--; }
        return (sign << 63) | ((uint64_t)(exp + 1023) << 52) | (frac & 0x000FFFFFFFFFFFFFull);
    }
    /* zero / infinity / NaN: FRT2:4 = WORD1 */
    return ((uint64_t)(w & 0xC0000000u) << 32) | (b1 << 61) | (b1 << 60) | (b1 << 59) |
           ((uint64_t)(w & 0x3FFFFFFFu) << 29);
}
static uint32_t ref_stfs(uint64_t b) {
    uint32_t e = (b >> 52) & 0x7FF;
    if (e > 896 || (b << 1) == 0) /* no denormalization required: WORD0:1 = FRS0:1, WORD2:31 = FRS5:34 */
        return (uint32_t)(b >> 63) << 31 | (uint32_t)((b >> 62) & 1) << 30 | (uint32_t)((b >> 29) & 0x3FFFFFFF);
    uint32_t sign = (uint32_t)(b >> 63) << 31;
    if (e < 874) return sign; /* 750CL: denormalize (everything shifts out) / store zero */
    int exp = (int)e - 1023;
    uint64_t frac = (1ull << 52) | (b & 0x000FFFFFFFFFFFFFull); /* 0b1 || FRS12:63 */
    while (exp < -126) { frac >>= 1; exp++; }
    return sign | (uint32_t)((frac >> 29) & 0x7FFFFF); /* WORD9:31 = frac1:23 */
}

static uint64_t bits_of(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double dbl_of(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

enum { kSrc = 0x10000000u, kDst = 0x10000100u };

/* the shapes the recompiler emits: lfs f; stfs f (value in a local) */
__attribute__((noinline)) static void copy_local(uint32_t s, uint32_t d) {
    double v = ldf32(s);
    stf32(d, v);
}
/* lfs into the Cpu FPR slot (ps0 and ps1, paired-single mode duplicates), stfs from it */
__attribute__((noinline)) static void copy_fpr(Cpu* c, uint32_t s, uint32_t d) {
    { double v = ldf32(s); c->f[5].ps0 = v; c->f[5].ps1 = v; }
    stf32(d, c->f[5].ps0);
}
/* psq_l / psq_st with GQR0 (float) */
__attribute__((noinline)) static void copy_psq(Cpu* c, uint32_t s, uint32_t d) {
    psq_load(c, 6, s, 0, 0);
    psq_store(c, 6, d, 0, 0);
}
/* quantized slow path with a float type in a GQR the code does not treat as static */
__attribute__((noinline)) static void copy_psq_gqr(Cpu* c, uint32_t s, uint32_t d) {
    psq_load(c, 7, s, 0, 7);
    psq_store(c, 7, d, 0, 7);
}

static void round_trip(Cpu* c, uint32_t w) {
    uint32_t r;
    st32(kSrc, w); st32(kSrc + 4, w ^ 0x80000000u);
    copy_local(kSrc, kDst); r = ld32(kDst);
    CHECK(r == w, "lfs->stfs local %08" PRIX32 " -> %08" PRIX32, w, r);
    copy_fpr(c, kSrc, kDst); r = ld32(kDst);
    CHECK(r == w, "lfs->stfs fpr %08" PRIX32 " -> %08" PRIX32, w, r);
    CHECK(bits_of(c->f[5].ps0) == ref_lfs(w), "lfs FPR image %08" PRIX32 ": %016" PRIX64, w, bits_of(c->f[5].ps0));
    copy_psq(c, kSrc, kDst);
    CHECK(ld32(kDst) == w && ld32(kDst + 4) == (w ^ 0x80000000u), "psq_l->psq_st %08" PRIX32, w);
    CHECK(bits_of(c->f[6].ps0) == ref_lfs(w) && bits_of(c->f[6].ps1) == ref_lfs(w ^ 0x80000000u),
          "psq_l FPR image %08" PRIX32, w);
    copy_psq_gqr(c, kSrc, kDst);
    CHECK(ld32(kDst) == w && ld32(kDst + 4) == (w ^ 0x80000000u), "psq_l->psq_st (GQR7 float) %08" PRIX32, w);
}

int main(int argc, char** argv) {
    int exhaustive = argc > 1 && !strcmp(argv[1], "--exhaustive");
    void* page = PPC_MEM_BASE + kSrc;  /* the guest page the round trips use, at its guest address */
#ifdef _WIN32
    if (VirtualAlloc(page, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != page) {
        fprintf(stderr, "ppc_f32_exact_test: VirtualAlloc guest page failed (%lu)\n", (unsigned long)GetLastError());
        return 1;
    }
#else
    if (mmap(page, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != page) {
        perror("ppc_f32_exact_test: mmap guest page");
        return 1;
    }
#endif
    static Cpu cpu;
    Cpu* c = &cpu;
    memset(c, 0, sizeof *c);
    c->gqr[7] = 0x00010001u; /* type 1 (reserved float type) on load and store, not a static GQR */

    /* ---- named values ---- */
    static const struct { uint32_t w; uint64_t d; const char* what; } named[] = {
        {0x7F800059u, 0x7FF0000B20000000ull, "SNaN +"},
        {0xFF812345u, 0xFFF02468A0000000ull, "SNaN -"},
        {0x7F800001u, 0x7FF0000020000000ull, "SNaN smallest payload"},
        {0x7FC00059u, 0x7FF8000B20000000ull, "QNaN +"},
        {0xFFFFFFFFu, 0xFFFFFFFFE0000000ull, "QNaN - all ones"},
        {0x00000000u, 0x0000000000000000ull, "+0"},
        {0x80000000u, 0x8000000000000000ull, "-0"},
        {0x00000001u, 0x36A0000000000000ull, "smallest denormal"},
        {0x807FFFFFu, 0xB80FFFFFC0000000ull, "largest denormal -"},
        {0x00400000u, 0x3800000000000000ull, "denormal 2^-127"},
        {0x7F800000u, 0x7FF0000000000000ull, "+inf"},
        {0xFF800000u, 0xFFF0000000000000ull, "-inf"},
        {0x3F800000u, 0x3FF0000000000000ull, "1.0"},
        {0x00800000u, 0x3810000000000000ull, "smallest normal"},
        {0x7F7FFFFFu, 0x47EFFFFFE0000000ull, "largest normal"},
    };
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++) {
        uint32_t w = named[i].w;
        uint64_t got = bits_of(ppc_lfs_value(w));
        CHECK(got == named[i].d, "lfs %s %08" PRIX32 ": %016" PRIX64 " want %016" PRIX64, named[i].what, w, got,
              named[i].d);
        CHECK(ref_lfs(w) == named[i].d, "reference lfs %s", named[i].what);
        uint32_t back = ppc_stfs_bits(dbl_of(named[i].d));
        CHECK(back == w, "stfs %s: %08" PRIX32 " want %08" PRIX32, named[i].what, back, w);
        round_trip(c, w);
    }

    /* ---- stfs of doubles that are not singles ---- */
    static const struct { uint64_t d; uint32_t w; const char* what; } st[] = {
        /* 1 + 0.75 * 2^-23: the host rounds up to 3F800001, stfs drops the low 29 bits */
        {0x3FF0000018000000ull, 0x3F800000u, "1+0.75ulp truncated"},
        {0xBFF0000018000000ull, 0xBF800000u, "-(1+0.75ulp) truncated"},
        {0x3FF000001FFFFFFFull, 0x3F800000u, "just below 1+ulp"},
        {0x3FD5555555555555ull, 0x3EAAAAAAu, "1/3 (host: 3EAAAAAB)"},
        /* too large for single: the bit selection, not infinity (Book I: well-defined, not equal) */
        {0x4810000000000000ull, 0x40800000u, "2^130"},
        {0x7FEFFFFFFFFFFFFFull, 0x7F7FFFFFu, "DBL_MAX (FLT_MAX bits)"},
        /* a NaN whose payload is only in the low 29 bits: the bit selection gives infinity */
        {0x7FF0000000000001ull, 0x7F800000u, "SNaN low payload"},
        {0x7FF8000000000001ull, 0x7FC00000u, "QNaN low payload"},
        /* denormalization: exponents 874..896, truncated */
        {0x3800000000000000ull, 0x00400000u, "2^-127"},
        {0x3800000010000000ull, 0x00400000u, "2^-127 (1+2^-24) truncated"},
        {0x36A0000000000000ull, 0x00000001u, "2^-149"},
        {0x36AFFFFFFFFFFFFFull, 0x00000001u, "just below 2^-148"},
        {0xB6A8000000000000ull, 0x80000001u, "-1.5 * 2^-149"},
        /* below the smallest single denormal (exponent < 874) and double denormals: signed zero */
        {0x3690000000000000ull, 0x00000000u, "2^-150"},
        {0xB690000000000000ull, 0x80000000u, "-2^-150"},
        {0x0000000000000001ull, 0x00000000u, "double denormal"},
        {0x8000000000000001ull, 0x80000000u, "-double denormal"},
        {0x0010000000000000ull, 0x00000000u, "DBL_MIN"},
    };
    for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
        uint32_t got = ppc_stfs_bits(dbl_of(st[i].d));
        CHECK(got == st[i].w, "stfs %s %016" PRIX64 ": %08" PRIX32 " want %08" PRIX32, st[i].what, st[i].d, got,
              st[i].w);
        CHECK(ref_stfs(st[i].d) == st[i].w, "reference stfs %s", st[i].what);
        stf32(kDst, dbl_of(st[i].d));
        CHECK(ld32(kDst) == st[i].w, "stf32 %s", st[i].what);
        c->f[8].ps0 = c->f[8].ps1 = dbl_of(st[i].d);
        psq_store(c, 8, kDst, 0, 0);
        CHECK(ld32(kDst) == st[i].w && ld32(kDst + 4) == st[i].w, "psq_st %s", st[i].what);
    }

    /* ---- random doubles across all store cases ---- */
    for (int i = 0; i < 4000000; i++) {
        uint64_t r = next();
        uint64_t e = (i & 3) == 0 ? (r >> 52) & 0x7FF                 /* any exponent */
                   : (i & 3) == 1 ? 860 + (next() % 50)               /* around the denormal range */
                   : (i & 3) == 2 ? 1000 + (next() % 60)              /* ordinary values */
                   : 1140 + (next() % 30);                            /* around the single maximum */
        uint64_t f = (i & 4) ? (r & 0x000FFFFFFFFFFFFFull) : (r & 0x000FFFFFE0000000ull); /* half are singles */
        uint64_t b = (r & 0x8000000000000000ull) | e << 52 | f;
        uint32_t got = ppc_stfs_bits(dbl_of(b)), want = ref_stfs(b);
        CHECK(got == want, "stfs random %016" PRIX64 ": %08" PRIX32 " want %08" PRIX32, b, got, want);
    }

    /* ---- every single word (or a stride of them) on load, and the round trip ---- */
    uint64_t step = exhaustive ? 1 : 4099;
    for (uint64_t w = 0; w <= 0xFFFFFFFFull; w += step) {
        uint32_t x = (uint32_t)w;
        uint64_t got = bits_of(ppc_lfs_value(x));
        CHECK(got == ref_lfs(x), "lfs %08" PRIX32 ": %016" PRIX64 " want %016" PRIX64, x, got, ref_lfs(x));
        uint32_t back = ppc_stfs_bits(dbl_of(got));
        CHECK(back == x, "lfs->stfs %08" PRIX32 " -> %08" PRIX32, x, back);
        /* a single-precision result (never a signalling NaN) stores like the host conversion: what
           tools/recomp/rawstore.py emits for an stfs of such a register */
        if (!((x & 0x7FC00000u) == 0x7F800000u && (x & 0x003FFFFFu)))
            CHECK(f32_as_u32((float)dbl_of(got)) == back, "single-result store %08" PRIX32, x);
        if (!exhaustive || (x & 0xFFF) == 0) round_trip(c, x);
    }
    /* every NaN class boundary through guest memory */
    static const uint32_t edges[] = {0x7F800001u, 0x7FBFFFFFu, 0x7FC00000u, 0x7FFFFFFFu, 0xFF800001u, 0xFFBFFFFFu,
                                     0xFFC00000u, 0xFFFFFFFFu, 0x007FFFFFu, 0x80000001u};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) round_trip(c, edges[i]);

    if (fails) { printf("ppc_f32_exact_test: %d failure(s)\n", fails); return 1; }
    printf("ppc_f32_exact_test: ok%s\n", exhaustive ? " (exhaustive load)" : "");
    return 0;
}
