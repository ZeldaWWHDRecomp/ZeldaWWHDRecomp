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

/* Read-only port settings v1. Stable types; unknown keys and wrong types return 0.
 * Successful reads return bytes including the string terminator; short buffers
 * are unchanged. A null buffer with capacity 0 queries the required byte count.
 * Buffers and key strings must belong to this mod's code/data/heap region. */
#define WWHD_SETTING_API_VERSION 1
enum { WWHD_SETTING_STRING=1, WWHD_SETTING_BOOL=2, WWHD_SETTING_U32=3, WWHD_SETTING_F64=4 };
u32 wwhd_setting_get(const char* key,u32 type,void* buffer,u32 capacity);
/* Per-key observed revision, initially 1; 0 means absent. Compare for inequality. */
unsigned long long wwhd_setting_changed(const char* key);

/* HUD v1. Register from a game hook; callback receives a recording-list handle once
 * per logic step. Its immutable output is held for every presentation until the next
 * step. TV=0 (1280x720), DRC=1 (854x480), both=2 (TV coordinates scaled to each screen).
 * Element/text/path buffers must be static or allocated in this mod's heap. */
#define WWHD_HUD_API_VERSION 1
enum { WWHD_HUD_TV=0,WWHD_HUD_DRC=1,WWHD_HUD_BOTH=2 };
enum { WWHD_HUD_RECT=0,WWHD_HUD_TEXT=1,WWHD_HUD_IMAGE=2,WWHD_HUD_RECT_OUTLINE=3,
       WWHD_HUD_CIRCLE=4,WWHD_HUD_CIRCLE_OUTLINE=5,WWHD_HUD_LINE=6 };
enum { WWHD_HUD_CENTER=0,WWHD_HUD_TOP_LEFT=1,WWHD_HUD_TOP=2,WWHD_HUD_TOP_RIGHT=3,
       WWHD_HUD_LEFT=4,WWHD_HUD_RIGHT=5,WWHD_HUD_BOTTOM_LEFT=6,WWHD_HUD_BOTTOM=7,WWHD_HUD_BOTTOM_RIGHT=8 };
enum { WWHD_HUD_ALPHA=0,WWHD_HUD_ADDITIVE=1 };
enum { WWHD_HUD_PACKAGE=0,WWHD_HUD_DATA=1 };
typedef struct {
    u32 kind,anchor,blend;
    f32 x,y,w,h,size,thickness,rotation;
    f32 u0,v0,u1,v1;
    u32 rgba,image;
    const char* text;
    u32 text_bytes;
} wwhd_hud_element;
/* x/y is the top-left except circles (center). w/h is extent, or signed line delta.
 * size is circle radius / text height. Rotation is radians about image center.
 * UVs are normalized subrect coordinates. rgba is RRGGBBAA. Set thickness>0. */
u32 wwhd_hud_register(void (*callback)(u32 list),u32 screen);
u32 wwhd_hud_emit(u32 list,const wwhd_hud_element* element);
/* PNG path relative to this package's assets/ or textures/, or this mod's Data folder.
 * Returns an owned handle, zero on failure. Reuse handles between logic steps. */
u32 wwhd_hud_texture(u32 source,const char* path);
u32 wwhd_hud_release(u32 image);
/* Changes after full state load; old handles are invalid. Reload PNGs on a change. */
unsigned long long wwhd_hud_epoch(void);
