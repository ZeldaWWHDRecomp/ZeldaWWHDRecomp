// Same-state draw runs drawn as one instanced draw (on by default in the normal build; WWHD_VK_BATCH_RUNS=0 turns it off): the renderer's side of
// gx2/draw_runs.h. Included by draw.cpp only, inside namespace gfxvk: part 1 (BatchDraw, the per-piece uniform bytes)
// before bind_stage, part 2 (batch_draw_part2.h: the variant, the acceptance, the chunked draw) before draw_stereo.
// PROVENANCE: follows VR-WakerHD's batch_* hooks in its gfx/vulkan/draw.cpp (commit e06fb7b99cbb, MPL-2.0 like every
// changed ZeldaWWHDRecomp file; discussion #535), without the VR parts (eyes, drawing threads, sun atlas thread);
// "draw batching from VR-WakerHD (thanks @mkooner)".
//
// A held run (draw_batch::Batch: pieces that differ only in vertex-shader uniform block addresses) is drawn as one
// draw of `count` instances with the vertex shader's batched variant (batch_glsl.h): each batched block becomes an
// array of the pieces' blocks indexed by gl_InstanceIndex, and the support block (Cemu's helper uniforms, which carry
// the remapped entries of guest blocks) an array of per-piece copies when a batched block's entries are remapped into
// it; the guest's instance ID reads 0, as in each piece's own draw. Every other operation of the shader is as
// translated, so instance k computes what piece k's draw did. Each piece's support copy is piece 0's final bytes with
// its own remapped entries, copied as pack_uniforms_into copies them.
// The renderer refuses (gx2 then draws the pieces one by one, each with its own addresses, as before): stereo, the
// GamePad backdrop capture, 60 fps (generated)'s HUD coverage, a packet capture, a geometry shader, instance-rate
// vertex input, the AO / sun-shadow / depth-downsample programs (which change their uniforms in place), the tall grass
// while Grass draw distance culls tufts by their block, a zero block address, a variant still compiling or failed,
// and a pipeline of the variant not ready yet (made in the background lane, never on the render thread).
#pragma once
// (draw.cpp includes ../draw_batch.h, batch_glsl.h and shader_compiler.h at its top)

struct BatchDraw {
  const draw_batch::Batch* run = nullptr;
  vk::Shader* base = nullptr;     // the run's vertex shader
  vk::Shader* variant = nullptr;  // its batched variant
  const vk::batch::Variant* layout = nullptr;
  uint32_t first = 0, count = 0;  // the chunk's pieces
  bool declined = false;          // (draw: the variant's pipeline is not ready; nothing was drawn)
};
// (draw threads T1: the calling thread's, DRAW_THREAD_STATIC; set around draw() by batch_draw_run, read by draw and
// bind_stage)
inline BatchDraw*& batch_draw() {
  DRAW_THREAD_STATIC(batch, BatchDraw*);
  return batch;
}
// the chunk's pieces of batched block i, one after another at the variant's stride (zero after each guest block),
// read as bind_stage reads a block: the bytes as submitted, else guest memory
inline void batch_gather(const BatchDraw& b, const uint32_t* r, uint32_t i, std::vector<uint8_t>& out) {
  const uint32_t stride = b.layout->vec4s[i] * 16;
  const uint32_t size = std::min<uint32_t>(r[mmSQ_VTX_UNIFORM_BLOCK_START + i * 7 + 1] + 1, 0x10000);
  const uint32_t copy = std::min(size, stride);
  out.assign(size_t(b.count) * stride, 0);
  for (uint32_t k = 0; k < b.count; ++k) {
    const uint32_t addr = b.run->addrs[b.first + k][i];
    const uint8_t* submitted = addr ? gx2::submitted_uniform_block(addr, size) : nullptr;
    const uint8_t* source = submitted ? submitted : static_cast<const uint8_t*>(addr ? mem::ptr(addr) : nullptr);
    if (source) std::memcpy(out.data() + size_t(k) * stride, source, copy);
  }
}
// the chunk's support blocks: piece 0's final bytes (support), each with its own remapped entries of the batched blocks
inline void batch_support(const BatchDraw& b, const uint32_t* r, const vk::Shader& sh, const std::vector<uint8_t>& support,
                          std::vector<uint8_t>& out) {
  const uint32_t stride = b.layout->supportBytes;
  const int remapped = sh.uniforms.offset_remapped;
  out.resize(size_t(b.count) * stride);
  for (uint32_t k = 0; k < b.count; ++k) {
    uint8_t* piece = out.data() + size_t(k) * stride;
    const size_t base = std::min<size_t>(stride, support.size());
    std::memcpy(piece, support.data(), base);
    std::memset(piece + base, 0, stride - base);
    const uint32_t* addrs = b.run->addrs[b.first + k];
    for (const auto& group : sh.dec->list_remappedUniformEntries_bufferGroups) {
      const uint32_t i = group.kcacheBankIdOffset / 4 / 7;
      if (i >= 16 || !(b.run->blocks >> i & 1)) continue;
      const uint32_t address = addrs[i];
      if (!address) continue;  // (refused: batch_accept)
      const size_t blockSize = std::min<uint32_t>(r[mmSQ_VTX_UNIFORM_BLOCK_START + group.kcacheBankIdOffset / 4 + 1] + 1, 0x10000);
      const uint8_t* submitted = gx2::submitted_uniform_block(address, uint32_t(blockSize));
      for (const auto& entry : group.entries) {
        const int at = remapped + int(entry.mappedIndexOffset);
        if (remapped < 0 || at < 0 || size_t(at) + 16 > stride) continue;
        const void* src = submitted && entry.indexOffset + 16 <= blockSize
            ? static_cast<const void*>(submitted + entry.indexOffset) : ppc_ptr(address + entry.indexOffset);
        std::memcpy(piece + at, src, 16);
      }
    }
  }
}
