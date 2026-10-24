/* gabi: what decompiled WWHD source is written against.
 *
 * - Guest structures are described with their WWHD layout. Fields are `be<T>` (big-endian,
 *   accessed through the guest-memory functions below), pointers are `gptr<T>` (32-bit guest
 *   addresses). A `T*` in source is a token for a guest address (PPC_MEM_BASE + EA); it is never
 *   dereferenced directly, only through be<>/gptr<> members, so every access is visible to the
 *   harness.
 * - Calls to other guest functions go through `gabi::call<R>(addr, args...)` (PowerPC EABI:
 *   integers/pointers in r3..r10, floats in f1..f8, result in r3 or f1). Bindings with real
 *   names wrap these (see wwhd_src/include).
 * - Floating point follows the console as the recompiler models it: f32 arithmetic is IEEE
 *   single (exactly what fadds/fmuls/fdivs produce), and where GHS contracted a*b+c into
 *   fmadds the source must say so with gabi::fmadds() & co. (compile with -ffp-contract=off).
 *
 * The memory functions gmem_* are provided by the environment: the verification harness
 * (tools/verify/src/vm.cpp) or a native build.
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>

#include "ppc.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef float f32;
typedef double f64;
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

extern "C" {
uint8_t gmem_ld8(uint32_t ea);
uint16_t gmem_ld16(uint32_t ea);
uint32_t gmem_ld32(uint32_t ea);
uint64_t gmem_ld64(uint32_t ea);
void gmem_st8(uint32_t ea, uint8_t v);
void gmem_st16(uint32_t ea, uint16_t v);
void gmem_st32(uint32_t ea, uint32_t v);
void gmem_st64(uint32_t ea, uint64_t v);
/* kind: 0 direct, 1 through a function pointer; nint/nflt: argument registers set */
void gmem_call(Cpu* c, uint32_t target, int kind, int nint, int nflt);
}

namespace gabi {

extern thread_local Cpu* cpu;

/* ---- addresses ---- */
inline u32 ea(const void* p) { return p ? (u32)((const uint8_t*)p - PPC_MEM_BASE) : 0u; }
template <class T> inline T* at(u32 a) { return a ? (T*)(PPC_MEM_BASE + a) : nullptr; }

/* f32 values move like lfs / stfs on the console: bit for bit, a signalling NaN stays signalling
 * (ppc.h ppc_lfs_value / ppc_stfs_bits, PowerPC Book I 4.6.2 / 4.6.3). The recompiled reference
 * (vm_gen.h) does the same, so a typed f32 copy compares equal to the game's lfs/stfs pair. */
inline f32 f32_from_bits(u32 v) {
    f32 r;
    memcpy(&r, &v, 4);
    return r;
}
inline u32 f32_bits(f32 f) {
    u32 v;
    memcpy(&v, &f, 4);
    return v;
}
/* f32 <-> FPR (argument and result registers): an f32 in an FPR is the lfs image of its bits; an
 * FPR read as f32 keeps a NaN's bits as stfs would, other values round like frsp (unchanged). */
inline f64 fpr_from_f32(f32 f) { return ppc_lfs_value(f32_bits(f)); }
inline f32 f32_from_fpr(f64 d) { return d != d ? f32_from_bits(ppc_stfs_bits(d)) : (f32)d; }
template <class T> inline f64 fpr_from(T a) {
    if constexpr (std::is_same_v<T, f32>) return fpr_from_f32(a);
    else return (f64)a;
}
template <class T> inline T from_fpr(f64 d) {
    if constexpr (std::is_same_v<T, f32>) return f32_from_fpr(d);
    else return (T)d;
}

template <class T> inline T load(u32 a) {
    static_assert(!std::is_pointer_v<T>, "guest pointers are 32-bit: load<u32> and at<T>()");
    if constexpr (std::is_same_v<T, f32>) return f32_from_bits(gmem_ld32(a));
    else if constexpr (sizeof(T) == 1) { u8 v = gmem_ld8(a); T r; memcpy(&r, &v, 1); return r; }
    else if constexpr (sizeof(T) == 2) { u16 v = gmem_ld16(a); T r; memcpy(&r, &v, 2); return r; }
    else if constexpr (sizeof(T) == 4) { u32 v = gmem_ld32(a); T r; memcpy(&r, &v, 4); return r; }
    else { static_assert(sizeof(T) == 8); u64 v = gmem_ld64(a); T r; memcpy(&r, &v, 8); return r; }
}
template <class T> inline void store(u32 a, T x) {
    static_assert(!std::is_pointer_v<T>, "guest pointers are 32-bit: store<u32>(a, ea(p))");
    if constexpr (sizeof(T) == 1) { u8 v; memcpy(&v, &x, 1); gmem_st8(a, v); }
    else if constexpr (sizeof(T) == 2) { u16 v; memcpy(&v, &x, 2); gmem_st16(a, v); }
    else if constexpr (sizeof(T) == 4) { u32 v; memcpy(&v, &x, 4); gmem_st32(a, v); }
    else { static_assert(sizeof(T) == 8); u64 v; memcpy(&v, &x, 8); gmem_st64(a, v); }
}

/* ---- big-endian guest field ---- */
template <class T> struct be {
    uint8_t _raw[sizeof(T)];
    u32 addr() const { return ea(this); }
    T get() const { return load<T>(addr()); }
    void set(T v) { store<T>(addr(), v); }
    operator T() const { return get(); }
    be& operator=(T v) { set(v); return *this; }
    be& operator=(const be& o) { set(o.get()); return *this; }
    be() = default;
    be(const be&) = delete; /* guest fields live in guest memory: never copied on the host */
    template <class U> be& operator+=(const U& v) { set((T)(get() + v)); return *this; }
    template <class U> be& operator-=(const U& v) { set((T)(get() - v)); return *this; }
    template <class U> be& operator*=(const U& v) { set((T)(get() * v)); return *this; }
    template <class U> be& operator/=(const U& v) { set((T)(get() / v)); return *this; }
    template <class U> be& operator|=(const U& v) { set((T)(get() | v)); return *this; }
    template <class U> be& operator&=(const U& v) { set((T)(get() & v)); return *this; }
    template <class U> be& operator^=(const U& v) { set((T)(get() ^ v)); return *this; }
    template <class U> be& operator<<=(const U& v) { set((T)(get() << v)); return *this; }
    template <class U> be& operator>>=(const U& v) { set((T)(get() >> v)); return *this; }
    T operator++(int) { T o = get(); set((T)(o + 1)); return o; }
    T operator--(int) { T o = get(); set((T)(o - 1)); return o; }
    be& operator++() { set((T)(get() + 1)); return *this; }
    be& operator--() { set((T)(get() - 1)); return *this; }
};

/* ---- guest pointer field ---- */
template <class T> struct gptr {
    be<u32> v;
    operator T*() const { return at<T>(v.get()); }
    T* operator->() const { return at<T>(v.get()); }
    T* get() const { return at<T>(v.get()); }
    gptr& operator=(T* p) { v.set(ea(p)); return *this; }
    gptr& operator=(const gptr& o) { v.set(o.v.get()); return *this; }
};

/* function pointer stored in guest memory (an address of guest code) */
struct gfn {
    be<u32> v;
    u32 get() const { return v.get(); }
};

/* ---- guest calls ---- */
struct ArgPack {
    Cpu* c;
    int gi = 3, fi = 1;
    int ns = 0;
    u32 stk[8];
    void put_int(u32 v) {
        if (gi <= 10) c->r[gi] = v;
        else stk[ns++] = v; /* beyond r10: parameter area of the caller's frame */
        gi++;
    }
    template <class A> void put(A a) {
        if constexpr (std::is_floating_point_v<A>) {
            c->f[fi].ps0 = c->f[fi].ps1 = fpr_from(a);
            fi++;
        } else if constexpr (std::is_pointer_v<A>) {
            put_int(ea(a));
        } else if constexpr (std::is_same_v<A, std::nullptr_t>) {
            put_int(0);
        } else if constexpr (std::is_enum_v<A>) {
            put_int((u32)(s32)a);
        } else if constexpr (std::is_same_v<A, bool>) {
            put_int(a ? 1u : 0u);
        } else if constexpr (std::is_integral_v<A> && std::is_signed_v<A>) {
            put_int((u32)(s32)a);
        } else if constexpr (std::is_integral_v<A>) {
            put_int((u32)a);
        } else {
            static_assert(sizeof(A) == 0, "unsupported argument type");
        }
    }
};

template <class R> inline R result(Cpu* c) {
    if constexpr (std::is_void_v<R>) return;
    else if constexpr (std::is_same_v<R, bool>) return c->r[3] != 0; /* GHS tests bool results with cmpwi */
    else if constexpr (std::is_floating_point_v<R>) return from_fpr<R>(c->f[1].ps0);
    else if constexpr (std::is_pointer_v<R>) return at<std::remove_pointer_t<R>>(c->r[3]);
    else return (R)c->r[3];
}

/* stack arguments go to a temporary outgoing-argument frame (back chain at +0, args from +8) */
inline void do_call(Cpu* c, ArgPack& p, u32 addr, int kind) {
    if (p.ns) {
        u32 size = (8 + 4 * p.ns + 15) & ~15u;
        u32 sp = c->r[1];
        c->r[1] = sp - size;
        store<u32>(c->r[1], sp);
        for (int i = 0; i < p.ns; i++) store<u32>(c->r[1] + 8 + 4 * i, p.stk[i]);
        gmem_call(c, addr, kind, p.gi - 3, p.fi - 1);
        c->r[1] = sp;
    } else {
        gmem_call(c, addr, kind, p.gi - 3, p.fi - 1);
    }
}

template <class R = void, class... A> inline R call(u32 addr, A... a) {
    Cpu* c = cpu;
    ArgPack p{c};
    (p.put(a), ...);
    do_call(c, p, addr, 0);
    return result<R>(c);
}
template <class R = void, class... A> inline R call_ptr(u32 fn, A... a) {
    Cpu* c = cpu;
    ArgPack p{c};
    (p.put(a), ...);
    c->pc = fn;
    do_call(c, p, fn, 1);
    return result<R>(c);
}

/* ---- guest stack temporaries (for locals whose address is passed to guest code) ---- */
template <class T> struct Local {
    static constexpr u32 kSize = (sizeof(T) + 15) & ~15u;
    u32 a;
    Local() { cpu->r[1] -= kSize; a = cpu->r[1]; }
    ~Local() { cpu->r[1] += kSize; }
    Local(const Local&) = delete;
    T* get() const { return at<T>(a); }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    operator T*() const { return get(); }
};

/* ---- floating point as the console computes it (recompiler semantics, ppc2c.py) ---- */
inline f32 fmadds(f32 a, f32 c, f32 b) { return (f32)((f64)a * (f64)c + (f64)b); }   /* a*c+b */
inline f32 fmsubs(f32 a, f32 c, f32 b) { return (f32)((f64)a * (f64)c - (f64)b); }   /* a*c-b */
inline f32 fnmadds(f32 a, f32 c, f32 b) { return (f32)(-((f64)a * (f64)c + (f64)b)); }
inline f32 fnmsubs(f32 a, f32 c, f32 b) { return (f32)(-((f64)a * (f64)c - (f64)b)); } /* b-a*c */
inline f64 fmadd(f64 a, f64 c, f64 b) { return std::fma(a, c, b); }
inline f64 fmsub(f64 a, f64 c, f64 b) { return std::fma(a, c, -b); }
inline f64 fnmsub(f64 a, f64 c, f64 b) { return -std::fma(a, c, -b); }
/* float -> s32 conversion (fctiwz: truncating, saturating, NaN -> INT_MIN) */
inline s32 ftoi(f64 d) { return (s32)(u32)ppc_fctiwz(d); }
/* paired-single and sqrt estimates */
inline f64 fres(f64 x) { return ppc_fres(x); }
inline f64 frsqrte(f64 x) { return ppc_frsqrte(x); }

/* ---- candidate registration (harness entry adapters) ---- */
typedef void (*EntryFn)(Cpu*);
enum RetKind { RET_VOID, RET_INT1, RET_INT2, RET_INT4, RET_FLOAT };
struct Candidate {
    u32 addr;
    const char* name;
    EntryFn fn;
    RetKind ret;
    Candidate* next;
    Candidate(u32 a, const char* n, EntryFn f, RetKind r);
};

template <class T> struct ArgFrom {
    static T get(Cpu* c, int& gi, int& fi) {
        if constexpr (std::is_floating_point_v<T>) return from_fpr<T>(c->f[fi++].ps0);
        else if constexpr (std::is_pointer_v<T>) return at<std::remove_pointer_t<T>>(c->r[gi++]);
        else if constexpr (std::is_same_v<T, bool>) return (c->r[gi++] & 0xFF) != 0;
        else return (T)c->r[gi++];
    }
};

template <class R> constexpr RetKind ret_kind() {
    if constexpr (std::is_void_v<R>) return RET_VOID;
    else if constexpr (std::is_floating_point_v<R>) return RET_FLOAT;
    else if constexpr (sizeof(R) == 1) return RET_INT1;
    else if constexpr (sizeof(R) == 2) return RET_INT2;
    else return RET_INT4;
}

/* Activation: a decompiled function runs its body only when entered from guest code. When
 * another decompiled function calls it directly (natural C++ call), WWHD_FUNC turns that call
 * into a guest call to its address, so every function is tested in isolation and a native
 * build can route the call wherever that address is implemented. */
struct Activation {
    static inline thread_local int depth = 0;
    Activation() { depth++; }
    ~Activation() { depth--; }
    static bool nested() { return depth > 0; }
};

template <class R> inline void store_ret(Cpu* c, R r) {
    if constexpr (std::is_floating_point_v<R>) c->f[1].ps0 = c->f[1].ps1 = fpr_from(r);
    else if constexpr (std::is_pointer_v<R>) c->r[3] = ea(r);
    else if constexpr (std::is_same_v<R, bool>) c->r[3] = r ? 1 : 0;
    else if constexpr (std::is_signed_v<R>) c->r[3] = (u32)(s32)r;
    else c->r[3] = (u32)r;
}

template <class R, class... A> struct Entry {
    template <R (*F)(A...)> static void run(Cpu* c) {
        cpu = c;
        Activation::depth = 0;
        int gi = 3, fi = 1;
        /* braced initialisation evaluates the arguments in order */
        std::tuple<A...> args{ArgFrom<A>::get(c, gi, fi)...};
        if constexpr (std::is_void_v<R>) {
            std::apply(F, args);
        } else {
            R r = std::apply(F, args);
            store_ret<R>(c, r);
        }
    }
};
template <class C, class R, class... A> struct MEntry {
    template <R (C::*F)(A...)> static void run(Cpu* c) {
        cpu = c;
        Activation::depth = 0;
        int gi = 4, fi = 1;
        C* self = at<C>(c->r[3]);
        std::tuple<A...> args{ArgFrom<A>::get(c, gi, fi)...};
        auto inv = [self](A... a) { return (self->*F)(a...); };
        if constexpr (std::is_void_v<R>) {
            std::apply(inv, args);
        } else {
            R r = std::apply(inv, args);
            store_ret<R>(c, r);
        }
    }
};
template <class R, class... A> constexpr auto entry_of(R (*)(A...)) { return Entry<R, A...>{}; }
template <class C, class R, class... A> constexpr auto entry_of(R (C::*)(A...)) { return MEntry<C, R, A...>{}; }
template <class R, class... A> constexpr RetKind ret_of(R (*)(A...)) { return ret_kind<R>(); }
template <class C, class R, class... A> constexpr RetKind ret_of(R (C::*)(A...)) { return ret_kind<R>(); }

}  // namespace gabi

#define GABI_CAT2(a, b) a##b
#define GABI_CAT(a, b) GABI_CAT2(a, b)

/* First statement of every decompiled function: its WWHD address, return type and arguments
 * (with `this` first for methods). */
#define WWHD_FUNC(addr, R, ...)                                                  \
    if (gabi::Activation::nested()) return gabi::call<R>(addr, __VA_ARGS__);   \
    gabi::Activation wwhd_activation_

/* VERIFY(0x021E01B8, daMtoge_actionUp) / VERIFY(0x021DFF30, &daMtoge_c::calcMtx): this source
 * function implements the WWHD function at that address (registers it with the harness). */
#define VERIFY(addr, fn)                                                                        \
    static gabi::Candidate GABI_CAT(wwhd_verify_, __LINE__)(addr, #fn,                         \
        &decltype(gabi::entry_of(fn))::template run<fn>, gabi::ret_of(fn))
