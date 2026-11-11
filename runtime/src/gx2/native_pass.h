// Native pass and copy descriptions (native renderer, the pass description of PLAN §8/§9). Decomp build, fast mode,
// cmake WWHD_NATIVE_DRAW=ON only; nothing here exists in any other build.
//
// A decompiled site that starts rendering into a set of targets (agl's bindRenderTarget) describes the pass it begins
// (OP_PASS_BEGIN); a site that clears the pass's attachments describes that as the pass's load intent (OP_PASS_LOAD);
// a site that copies, expands or resolves a surface describes the copy (OP_PASS_COPY). The description is engine-level
// (attachments as images with format, size, mip and slices; load/store intents; copy kinds), not register words.
//
// Lowering today: the site still makes its own GX2 calls right after (or before) the description, so the register
// file, the full draw path and every port feature see exactly what they saw without descriptions; a description adds
// no register write and does not end a draw packet's lowered state. The render thread keeps the open pass and checks
// it against the registers at every draw (counters, and with WWHD_NATIVE_DRAW_VERIFY a mismatch is logged): this is
// what a later backend step needs before it may trust the open pass instead of deriving targets and pass per draw.
// A pass ends at the next OP_PASS_BEGIN, at a copy (store, then copy) and at the swap; OP_PASS_END is for sites that
// know their end and their store intent.
//
// Run-time switch: WWHD_NATIVE_PASS=1 (off by default; independent of WWHD_NATIVE_DRAW). The counter line
// ("[native-pass] ...", every 300 frames): passes, described draws (targets as described), draws outside a described
// pass, mismatches, load intents, copies by kind.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_pass {

// an attachment: the image as the target registers address it (colour: mip and swizzle applied, as GX2SetColorBuffer
// sets CB_COLOR*_BASE; depth: the image address, as DB_DEPTH_BASE), its format (GX2 surface format value), size of
// the bound mip, view (mip, first slice, slice count) and samples (1, 2, 4, 8)
struct Attachment {
    uint32_t image = 0, format = 0, width = 0, height = 0, mip = 0, firstSlice = 0, slices = 0, samples = 0;
};
constexpr uint32_t kMaxColors = 8;
// OP_PASS_BEGIN payload, word for word
struct Pass {
    uint32_t id = 0;         // running number (game thread)
    uint32_t colorMask = 0;  // bit i: colors[i] is an attachment
    uint32_t hasDepth = 0;
    Attachment colors[kMaxColors];
    Attachment depth;
};
constexpr uint32_t kPassWords = sizeof(Pass) / 4;
// OP_PASS_LOAD payload: the open pass's attachments the site clears before drawing (load intent "clear")
enum LoadBits : uint32_t { kLoadClearColor0 = 1, kLoadClearDepth = 2, kLoadClearStencil = 4 };
struct Load {
    uint32_t bits = 0, colorIndex = 0;
    uint32_t rgba[4] = {};  // float bits
    uint32_t depth = 0, stencil = 0;
};
constexpr uint32_t kLoadWords = sizeof(Load) / 4;
// OP_PASS_COPY payload
enum CopyKind : uint32_t {
    kCopyScanOut = 1,     // a colour image to the TV (dest 0) or GamePad (dest 1) scan buffer
    kCopyExpandDepth = 2, // a depth image's compression resolved in place (readable as a texture afterwards)
    kCopyExpandColor = 3, // a colour image's MSAA/compression resolved in place
    kCopySurface = 4,     // an image (mip, slice) copied to another image (mip, slice)
};
struct Copy {
    uint32_t kind = 0, dest = 0;  // dest: scan-out target (0 TV, 1 GamePad)
    Attachment src, dst;          // dst: kCopySurface only
};
constexpr uint32_t kCopyWords = sizeof(Copy) / 4;

bool on();                                      // WWHD_NATIVE_PASS=1
void begin(const uint32_t* p, uint32_t n);      // render thread: OP_PASS_BEGIN
void load(const uint32_t* p, uint32_t n);       // render thread: OP_PASS_LOAD
void end(const uint32_t* p, uint32_t n);        // render thread: OP_PASS_END
void copy(const uint32_t* p, uint32_t n);       // render thread: OP_PASS_COPY
void note_draw(const uint32_t* regs);           // render thread: a draw (full or packet) about to execute
void frame_end();                               // render thread: at each swap

}  // namespace native_pass
#endif
