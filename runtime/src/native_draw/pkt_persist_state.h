// native_draw/pkt_persist_state.h: the data a persistent packet state carries (WWHD_NATIVE_PKT_PERSIST, decomp build
// fast mode; the logic is native_draw/pkt_persist.h). Included by gfx/vulkan/draw.cpp inside namespace gfxvk before
// LoweredKind, which holds one Extra per lowered state (moved with the state when it is swapped).
#pragma once

namespace pkpstate {
// the registers a lowering read (j3ddescvk::read_regs of its programs, the packet's compared checked ranges) without the
// instance's own (blocks' address and size words, vertex attribute address and size words, the fetch shader words,
// the declared instance register ranges): per (programs, packet)
struct ReadSet {
  std::vector<uint32_t> regs;
  struct Run { uint32_t first, count; };
  std::vector<Run> runs;
};
struct Extra {
  const ReadSet* set = nullptr;  // null: the state does not persist (why)
  const char* why = "not made";
  std::vector<uint32_t> values;  // the read set's values at the lowering
  uint64_t hash = 0;
  // the pass the lowering drew in (draw()'s reusePass key)
  std::array<Surface*, 8> colors{};
  std::array<uint32_t, 8> slices{};
  Surface* depth = nullptr;
  uint32_t depthSlice = 0, width = 0, height = 0;
  float targetSx = 1, targetSy = 1;  // the first target's scale (the support uniforms' sx, sy)
  uint64_t structGen = 0;   // g_surface_struct_gen at the lowering (surfaces made or dropped since: not persisted)
  uint64_t builtFor = 0;    // the submission whose slices and sets the state's capture holds
  uint64_t setsFor = ~0ull; // dynamic stages: the submission their descriptor sets were made for
  std::vector<uint8_t> indexBytes;  // the guest index bytes the lowering drew (content check within a submission)
  uint32_t indexAddr = 0;
  // per stage texture (ContinuedStage order): looked up again at each take (D1's rule: GPU-written with mips, depth, or
  // an address with several surfaces), the surface's scale the support bytes were packed with
  struct Tex { bool unstable = false; float sx = 1, sy = 1; };
  std::array<std::array<Tex, LATTE_NUM_MAX_TEX_UNITS>, 2> tex{};
  // per stage descriptor write: its source (-1 an image, 0..5 an instance block, 16 the support uniforms)
  std::array<std::array<int8_t, 17 + LATTE_NUM_MAX_TEX_UNITS>, 2> src{};
  UploadSlice sup[2]{};     // non-patched support uniforms of the submission builtFor
  bool supUploaded[2]{};
};
}  // namespace pkpstate
