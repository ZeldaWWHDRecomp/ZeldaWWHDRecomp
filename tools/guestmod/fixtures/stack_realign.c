/* Game-free reduction of the #117 heap-frame failure: ordinary C, a 0x220-byte
 * align16 local frame, and native callbacks that receive pointers into it.
 * No game code, executable bytes, assets or runtime workaround is involved. */
#include "wwhd_guest.h"
WWHD_GAME_FUNC(0x02000010, void, inspect_frame, (u32 frame, u32 mode, u32 output));

WWHD_REPLACE(0x02000000, u32, heap_frame, (u32 mode, u32 output)) {
    volatile u32 frame[0x220 / 4] __attribute__((aligned(16)));
    for (u32 i = 0; i < 0x220 / 4; ++i) frame[i] = (mode << 16) ^ i;
    inspect_frame((u32)frame, mode, output);
    return frame[0] ^ frame[0x70 / 4] ^ frame[0x130 / 4];
}

WWHD_REPLACE(0x02000004, u32, stack_arguments,
 (u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6,u32 a7,u32 pointer,u32 a9,u32 a10,u32 a11)) {
    volatile u32 aligned[8] __attribute__((aligned(32)));
    for (u32 i = 0; i < 8; ++i) aligned[i] = a0+a1+a2+a3+a4+a5+a6+a7+i;
    volatile u32* output = (volatile u32*)pointer;
    output[0] = (u32)aligned;
    output[1] = a0^a1^a2^a3^a4^a5^a6^a7^a9^a10^a11;
    return aligned[0]^aligned[7]^a9^a10^a11;
}
