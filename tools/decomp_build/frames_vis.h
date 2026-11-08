/* Pre-included into every decompiled game unit (CMakeLists.txt, WWHD_DECOMP): gabi.h's weak null
 * gmem_frame_info would otherwise be hidden and, after `ld -r`, local to each unit; with default
 * visibility it stays a global weak symbol that the runtime's definition (runtime/src/decomp/frames.cpp)
 * overrides. */
#pragma once
#include <cstdint>
#include "native_frames.h"
extern "C" __attribute__((visibility("default"))) const GabiFrameInfo* gmem_frame_info(uint32_t addr);

/* gabi.h's automatic frame (AutoFrame) in the game build: the original's frame of the running decompiled function
 * by slot (Cpu::decomp_running), made once at start-up from the same analysis gmem_frame_info answers from, with the
 * prologue stores in a form AutoFrame replays without a per-item switch. Per slot, in g_slots order; built by
 * runtime/src/decomp/frames.cpp (frames_init). Null: off (WWHD_DECOMP_AUTOFRAME_FAST=0, WWHD_DECOMP_FRAMES=0, or
 * before init); AutoFrame then asks gmem_frame_info as before. */
#define WWHD_DECOMP_FRAME_SLOTS 1
enum : uint8_t {
    DECOMP_FS_NONE = 0,   /* no frame (gmem_frame_info: null) */
    DECOMP_FS_TAIL = 1,   /* size 0, GABI_FF_TAIL */
    DECOMP_FS_FRAME = 2,  /* a frame whose prologue stores are the back chain, LR and GPRs only, none overlapping */
    DECOMP_FS_ITEMS = 3,  /* any other frame: replay fi's items in their order */
};
enum : uint8_t { DECOMP_FS_BACKCHAIN = 1, DECOMP_FS_LR = 2 };
struct DecompFrameSlot {
    uint32_t addr;            /* the slot's function */
    uint32_t size;            /* fi->size */
    uint8_t kind;             /* DECOMP_FS_* */
    uint8_t flags;            /* DECOMP_FS_FRAME: DECOMP_FS_BACKCHAIN / DECOMP_FS_LR */
    uint8_t ngpr;             /* DECOMP_FS_FRAME: GPR items at gpr */
    uint8_t reserved;
    uint16_t lr_off;          /* DECOMP_FS_FRAME with DECOMP_FS_LR: the LR save slot from sp */
    uint16_t hi_off, lo_off;  /* FrameCtx hi / lo from sp, as AutoFrame computes them from fi */
    uint16_t reserved2;
    const GabiFrameInfo* fi;  /* what gmem_frame_info(addr) returns (FrameCtx::fi) */
    const GabiFrameItem* gpr; /* DECOMP_FS_FRAME: the GPR items of fi */
};
extern "C" __attribute__((visibility("default"))) const DecompFrameSlot* g_decomp_frame_slots;
/* the entry for a function that is not the running slot's (AutoFrame of another address): never null; kind NONE
 * when gmem_frame_info(addr) is null */
extern "C" __attribute__((visibility("default"))) const DecompFrameSlot* decomp_frame_slot_find(uint32_t addr);
