// SPDX-License-Identifier: MPL-2.0
// Host replacements of the game's compiler runtime (GHS C/C++ runtime) and nn static wrappers.
//
// Each function replaces one guest root (named by its canonical address) and is written from the C/C++
// standard and the host's own libraries; where the original's results go beyond the standard (NaNs,
// out-of-range conversions, division by zero, which FPR lane it writes), the behaviour was measured on
// the recompiled original (tools/host_runtime/difftest) and is stated here as a rule.
//
// Register contract (tools/host_runtime/callsite_regs.py): a replacement writes only its results (r3, r4,
// f1, f2 exactly as the original leaves them) and keeps every other register as it was on entry; no
// caller reads anything else the original leaves behind.
//
// These files depend only on ppc.h (guest memory and the Cpu state), so the difference tests compile
// them as they are. The hooks that route the game's calls here are in hostrt_hooks.cpp.
#pragma once
#include "ppc.h"

extern "C" void imp_coreinit_memset(Cpu* c);         // the port's memset (hle/coreinit_mem.cpp)
extern "C" void imp_coreinit___gh_set_errno(Cpu* c);  // the port's errno setter (hle/coreinit_misc.cpp)
// the port's coreinit memory and dynamic-load functions (hle/)
extern "C" void imp_coreinit_MEMAllocFromExpHeapEx(Cpu* c);
extern "C" void imp_coreinit_MEMDestroyExpHeap(Cpu* c);
extern "C" void imp_coreinit_OSDynLoad_Release(Cpu* c);
extern "C" void imp_coreinit_UCOpen(Cpu* c);
extern "C" void imp_coreinit_UCReadSysConfig(Cpu* c);
extern "C" void imp_coreinit_UCClose(Cpu* c);
extern "C" void imp_coreinit_OSReport(Cpu* c);
extern "C" void imp_coreinit_MEMCreateExpHeapEx(Cpu* c);
extern "C" void imp_coreinit_MEMGetAllocatableSizeForExpHeapEx(Cpu* c);
extern "C" void imp_coreinit_OSDynLoad_SetAllocator(Cpu* c);
extern "C" void imp_coreinit_OSDynLoad_Acquire(Cpu* c);
extern "C" void imp_coreinit_OSDynLoad_FindExport(Cpu* c);
extern "C" void imp_nn_act_GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc(Cpu* c);

namespace hostrt {

// ---- math (hostrt_math.cpp): only functions measured bit-exact over their full input sets
void floorf_028F4114(Cpu* c);
void ceilf_028F41B0(Cpu* c);
void floor_028F3B98(Cpu* c);

// ---- compiler helpers (hostrt_abi.cpp)
void cvt_u64_f_028F543C(Cpu* c);  // u64 (r3:r4) -> float (f1)
void cvt_f_s64_028F5680(Cpu* c);  // float (f1) -> s64 (r3:r4)
void cvt_f_u64_028F5678(Cpu* c);  // float (f1) -> u64 (r3:r4)
uint64_t cvt_f_s64_value(float x);
uint64_t cvt_f_u64_value(float x);
void umod64_028F597C(Cpu* c);     // u64 (r3:r4) % u64 (r5:r6) -> r3:r4

}  // namespace hostrt

namespace hostrt {

// ---- C++ runtime (hostrt_cxx.cpp)
// array construction: __construct_array(p, n, size, ctor), __vec_new(p, n, size, ctor, dtor) and the
// zero-filling form; false hands the call back to the original (an allocator hook is set)
bool construct_array_028EFFD0(Cpu* c);
bool vec_new_028EFF8C(Cpu* c);
bool vec_new_zero_028F0020(Cpu* c);
void construct_array_arg_028F0294(Cpu* c);
void static_init_028EA09C(Cpu* c);  // runs the static initializers (once)  // (p, n, size, ctor, arg): ctor(p + i*size, arg + i*size)
void register_global_object_028F026C(Cpu* c);
// __destroy_arr(p, n, size, dtor, flags, extra): destroys n elements (n == -1: the count from the array's
// header) in reverse order and, when flags != 0, frees the array
bool destroy_arr_028F0164(Cpu* c);  // (record) links a destructor record into the global chain

// ---- nn static wrappers (hostrt_nn.cpp)
void list_insert_0286DA70(Cpu* c);       // (list, pos iterator, node) -> node
void list_erase_0286DA9C(Cpu* c);        // (list, node) -> next
void list_erase_range_0286DACC(Cpu* c);  // (list, first iterator, end iterator) -> end
void list_clear_0286DB18(Cpu* c);        // (list, iterator)
// erreula/swkbd export thunks: jump to the export the loader stored in the wrapper's table
void export_thunk_0286CA30(Cpu* c);
void export_thunk_0286CA40(Cpu* c);
void export_thunk_0286CB2C(Cpu* c);
void export_thunk_0286CB3C(Cpu* c);
void export_thunk_0286CB4C(Cpu* c);
void export_thunk_0286CB5C(Cpu* c);
void export_thunk_0286CB6C(Cpu* c);
void export_thunk_0286CBB4(Cpu* c);
void export_thunk_0286D270(Cpu* c);
void export_thunk_0286D2D4(Cpu* c);
void export_thunk_0286D2E4(Cpu* c);
void export_thunk_0286D3C0(Cpu* c);
void export_thunk_0286D3D0(Cpu* c);
void export_thunk_0286D3E0(Cpu* c);
void export_thunk_0286D3F0(Cpu* c);
void export_thunk_0286D400(Cpu* c);
void work_size_0286CBC4(Cpu* c);
void work_size_0286CD54(Cpu* c);         // swkbd work memory size for the option flags in r3
void heap_alloc_0286C590(Cpu* c);        // (size, align, out): erreula's RPL allocator callback
void erreula_destroy_0286C9CC(Cpu* c);
void swkbd_destroy_0286D37C(Cpu* c);
void wrap_0286CA50(Cpu* c);              // the wrappers that check two exports, then call a third
void wrap_0286CAA4(Cpu* c);
void wrap_0286CAE8(Cpu* c);
void wrap_0286D280(Cpu* c);
void wrap_0286D2F4(Cpu* c);
void wrap_0286D338(Cpu* c);
void wrap_0286CB7C(Cpu* c);              // calls an export with a copy of *r3
void erreula_create_0286C5F8(Cpu* c);
void swkbd_create_0286CD7C(Cpu* c);     // (CreateArg*): load swkbd.rpl, its heap and exports, create   // (CreateArg*): load erreula.rpl, its heap and exports, create
void sci_language_0286D620(Cpu* c);      // (out): system config "cafe.language"
void sci_parental_0286D938(Cpu* c);      // (out, account): "p_acct%d.net_communication_on_game"
void sci_parental_0286D9E0(Cpu* c);      // (out, account): "p_acct%d.network_launcher"  // returns the work memory size (0x01600000)
void empty_0286D5F0(Cpu* c);      // does nothing

// ---- C library (hostrt_libc.cpp)
void va_arg_028F5C88(Cpu* c);

// ---- printf family (hostrt_printf.cpp): false hands the call back (floating-point conversions)
bool vsnprintf_028F1748(Cpu* c);  // (buf, n, fmt, va_list)
bool snprintf_028F17A8(Cpu* c);   // (buf, n, fmt, ...)
bool sprintf_028F186C(Cpu* c);    // (buf, fmt, ...)      // __va_arg(va_list, type class) -> address of the next argument
void bzero_028F521C(Cpu* c);       // (p, n): memset(p, 0, n) through the port's memset
void zero_words_028F5914(Cpu* c);  // (p, n): n zero words at p
void strcpy_028F040C(Cpu* c);      // (dst, src)
void memmove_028EA038(Cpu* c);     // (dst, src, n), used once by the start-up code
void div_028F35A8(Cpu* c);         // div(a, b): r3 = quotient, r4 = remainder
void strtoul_028F2EE8(Cpu* c);     // (str, endptr, base)
void strtol_028F3090(Cpu* c);      // (str, endptr, base)

}  // namespace hostrt
