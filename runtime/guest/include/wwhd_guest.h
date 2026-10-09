/* Wind Waker HD recomp: guest mod SDK (Mod SDK v2, prototype). See docs/mod-sdk-v2.md.
 *
 * Guest mods are C compiled for the console CPU (32-bit big-endian PowerPC, the game's ABI) with a
 * freely available clang:
 *
 *   clang --target=powerpc-unknown-eabi -mcpu=750 -O2 -G0 -ffreestanding -fno-builtin -nostdlib
 *         -fno-jump-tables -ffunction-sections -fdata-sections -I<sdk>/include -c mod.c -o mod.o
 *   ld.lld -m elf32ppc -r mod.o [more.o ...] -o mod.elf          (one relocatable ELF per mod)
 *
 * The player's installation translates mod.elf to C and compiles it with its local compiler
 * (tools/guestmod/build_guest_mod.py). Nothing from the game is part of this header: declare the game
 * functions and data you use by address (WWHD_GAME_FUNC / WWHD_GAME_DATA), e.g. from the public
 * GameCube decompilation (zeldaret/tww, CC0) plus the HD addresses.
 */
#pragma once

/* Old SDKs embedded data pointers without relocations and cannot safely run on EU. */
static const unsigned int wwhd_address_format __attribute__((used, section(".wwhd_addresses"))) = 1;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef float f32;
typedef double f64;

#define WWHD_GUEST_API_VERSION 1

/* ---- hooks: one descriptor per hook in section .wwhd_hooks (read by the translator) ---- */
typedef struct { u32 kind, target; void* func; u32 flags; } wwhd_hook_desc;
#define WWHD_KIND_REPLACE 1
#define WWHD_KIND_ENTRY 2
#define WWHD_KIND_RETURN 3
#define WWHD__CAT2(a, b) a##b
#define WWHD__CAT(a, b) WWHD__CAT2(a, b)
#define WWHD__DESC(kind, addr, fn)                                                                   \
    __attribute__((section(".wwhd_hooks"), used)) static const wwhd_hook_desc WWHD__CAT(wwhd__h_, fn) = \
        {kind, addr, (void*)&fn, 0}

/* Replace the game function at `addr` completely (one mod per function):
 *   WWHD_REPLACE(0x0200ED84, void, my_addcalc2, (f32* v, f32 target, f32 scale, f32 max_step)) { ... } */
#define WWHD_REPLACE(addr, ret, name, params) \
    static ret name params;                   \
    WWHD__DESC(WWHD_KIND_REPLACE, addr, name); \
    __attribute__((noinline, used)) static ret name params

/* Run before the function (gets its arguments; changes to argument registers are discarded, write
 * through pointers instead) or after it (gets the arguments again; the return value is kept). */
#define WWHD_HOOK(addr, name, params) \
    static void name params;          \
    WWHD__DESC(WWHD_KIND_ENTRY, addr, name); \
    __attribute__((noinline, used)) static void name params
#define WWHD_HOOK_RETURN(addr, name, params) \
    static void name params;                 \
    WWHD__DESC(WWHD_KIND_RETURN, addr, name); \
    __attribute__((noinline, used)) static void name params

/* ---- the game ---- */
#define WWHD__STR2(x) #x
#define WWHD__STR(x) WWHD__STR2(x)
/* call a game function (goes through the game's dispatch, so other mods' hooks apply) */
#define WWHD_GAME_FUNC(addr, ret, name, params) ret name params __asm__("__wwhd_game_" WWHD__STR(addr))
/* call the game's own code of a function, below every mod's hook/replacement */
#define WWHD_GAME_ORIGINAL(addr, ret, name, params) ret name params __asm__("__wwhd_orig_" WWHD__STR(addr))
/* a game variable at a fixed address (MEM2 data of the USA game) */
#define WWHD_GAME_DATA(addr, type) (*({ \
    extern u8 WWHD__CAT(wwhd_game_data_, addr)[] __asm__("__wwhd_gdata_" WWHD__STR(addr)); \
    (type volatile*)WWHD__CAT(wwhd_game_data_, addr); }))

/* ---- host services (resolved by name on install; missing ones are an install error) ---- */
void wwhd_log(const char* message);
void wwhd_log_int(const char* label, int value);
void wwhd_log_hex(const char* label, u32 value);
void wwhd_log_float(const char* label, double value);
/* integer option `id` of this mod, or `fallback` */
int wwhd_config_int(const char* id, int fallback);
/* Other typed manager options (values are frozen until restart). Strings include enum options.
 * config_string copies at most capacity-1 bytes, terminates them and returns bytes copied;
 * a missing/wrong-type option returns zero without modifying the buffer. */
int wwhd_config_bool(const char* id, int fallback);
double wwhd_config_float(const char* id, double fallback);
u32 wwhd_config_string(const char* id, char* buffer, u32 capacity);
/* Per-mod heap: 16-byte aligned, default 256 KiB (manifest guest.heap_size changes it).
 * malloc returns null on exhaustion; free accepts null. Heap/data are part of full states. */
void* wwhd_malloc(u32 size);
void wwhd_free(void* pointer);
/* Current input, in VPAD button bits and normalized sticks/touch coordinates. */
typedef struct {
    u32 buttons;
    f32 lx, ly, rx, ry;
    u32 touch;
    f32 tx, ty;
} wwhd_input_state;
void wwhd_input_read(wwhd_input_state* state);
/* Flat filenames in this mod's Data/<id> folder; no paths or symlinks. At most 1 MiB per call.
 * read returns bytes read (zero at EOF), write replaces the file; -1 indicates failure. */
s32 wwhd_file_read(const char* filename, void* buffer, u32 size);
s32 wwhd_file_write(const char* filename, const void* buffer, u32 size);
/* Seconds in the current logic step (including true-60 scaling), and full-step count. */
double wwhd_logic_dt(void);
unsigned long long wwhd_logic_step(void);
void* memcpy(void* dst, const void* src, unsigned long n);
void* memmove(void* dst, const void* src, unsigned long n);
void* memset(void* dst, int v, unsigned long n);
