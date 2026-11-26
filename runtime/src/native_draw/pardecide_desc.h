// pardecide S1 (WWHD_NATIVE_PAR_DECIDE >= 1, decomp build fast mode): a described J3D draw's record step as one POD
// extension command (native_draw/pardecide_ops.h) instead of 3-10 per-call POD commands. Included by j3d_desc_vk.h
// inside namespace gfxvk::j3ddescvk, after Bound and the vertex helpers.
//
// Exactness: every decision is the per-call path's, in its order: the bind cache (g_bound) compares and updates, the
// vertex slices (buffer-cache range or snapshot, same calls in the same order), g_packetDynamicDirty. Only the
// encoding changes: the emitter expands the command into the same Vulkan calls in the same order. Without the record
// worker's stream (immediate recording, a held eye stream) or when the stream rejects the command, the same calls are
// recorded one by one, as before.
#pragma once
#include "native_draw/pardecide_emit.h"  // (included by draw.cpp at file scope first)

namespace pdesc {
namespace pd = ::native_draw::pardecide;
// the record step of try_replay for a draw without the D2 depth proof; FAMILY: the depth family's single set
inline void record(VkCommandBuffer cmd, Bound& B, const State& s, VkPipeline pipe, VkPipelineLayout pipeLayout,
                   uint32_t setCount, const VkDescriptorSet* sets, uint32_t offsetCount, const uint32_t* offsets,
                   const uint32_t* vaddr, const uint32_t* vsize, const VertexExtent& extent, const UploadSlice& ib,
                   VkIndexType indexType, uint32_t drawCount) {
  alignas(8) std::byte buf[pd::kDescMaxBytes];
  const uint32_t code = 4;  // record_mt::Ext::PardecideDesc
  std::memcpy(buf, &code, sizeof code);
  pd::DescHead h{};
  h.cmd = cmd, h.pipeline = pipe, h.layout = pipeLayout;
  h.setCount = setCount, h.offsetCount = offsetCount, h.drawCount = drawCount;
  if (B.pipeline != pipe) {
    h.flags |= pd::kPipe;
    B.pipeline = pipe;
  }
  pd::DescVb vbs[pd::kDescMaxVb];
  uint32_t nvb = 0;
  for (size_t vi = 0; vi < s.vertex.size(); ++vi) {
    const auto& v = s.vertex[vi];
    const uint32_t copied = v.supported ? vertex_prefix_size(vsize[vi], v.stride, v.attributeEnd, extent) : vsize[vi];
    UploadSlice b;
    {
      rprof::UploadKind uploads(rprof::kUpVertex);
      if (!cached_guest_range(vaddr[vi], copied, rprof::kUpVertex, b)) b = snapshot(mem::ptr(vaddr[vi]), copied, 4);
      rprof::guest_read(rprof::kUpVertex, vaddr[vi], copied);
    }
    if (B.vb[v.binding] != b.buffer || B.vbo[v.binding] != b.offset) {
      if (nvb < pd::kDescMaxVb) vbs[nvb++] = {b.buffer, b.offset, v.binding, 0};
      B.vb[v.binding] = b.buffer;
      B.vbo[v.binding] = b.offset;
    }
  }
  h.vbCount = nvb;
  if (B.indexBuffer != ib.buffer || B.indexOffset != ib.offset || B.indexType != indexType) {
    h.flags |= pd::kIndex;
    h.indexBuffer = ib.buffer, h.indexOffset = ib.offset, h.indexType = uint32_t(indexType);
    B.indexBuffer = ib.buffer;
    B.indexOffset = ib.offset;
    B.indexType = indexType;
  }
  if (!B.dynamic || std::memcmp(&B.viewport, &s.viewport, sizeof s.viewport))
    h.flags |= pd::kViewport, h.viewport = s.viewport, B.viewport = s.viewport, g_packetDynamicDirty = true;
  if (!B.dynamic || std::memcmp(&B.scissor, &s.scissor, sizeof s.scissor))
    h.flags |= pd::kScissor, h.scissor = s.scissor, B.scissor = s.scissor, g_packetDynamicDirty = true;
  if (!B.dynamic || B.blend != s.blend) {
    h.flags |= pd::kBlend;
    std::memcpy(h.blend, s.blend.data(), sizeof h.blend);
    B.blend = s.blend;
    g_packetDynamicDirty = true;
  }
  if (!B.dynamic || B.stencilFront != s.stencilFront) {
    h.flags |= pd::kStencilF, h.stencilFront = s.stencilFront;
    B.stencilFront = s.stencilFront;
    g_packetDynamicDirty = true;
  }
  if (!B.dynamic || B.stencilBack != s.stencilBack) {
    h.flags |= pd::kStencilB, h.stencilBack = s.stencilBack;
    B.stencilBack = s.stencilBack;
    g_packetDynamicDirty = true;
  }
  B.dynamic = true;
  // one command into the worker's stream
  const bool fits = setCount <= pd::kDescMaxSets && offsetCount <= pd::kDescMaxOffsets;
  std::byte* p = buf + sizeof code;
  if (fits) {
  std::memcpy(p, &h, sizeof h), p += sizeof h;
  std::memcpy(p, sets, setCount * sizeof(VkDescriptorSet)), p += setCount * sizeof(VkDescriptorSet);
  std::memcpy(p, offsets, offsetCount * sizeof(uint32_t)), p += offsetCount * sizeof(uint32_t);
  std::memcpy(p, vbs, nvb * sizeof(pd::DescVb)), p += nvb * sizeof(pd::DescVb);
  if (pd::emit_ext(buf, size_t(p - buf))) {
    pd::counters().descCompact.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  }
  // the same calls, one by one (as the per-call path records them)
  pd::counters().descFallback.fetch_add(1, std::memory_order_relaxed);
  if (h.flags & pd::kPipe) record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, setCount, sets, offsetCount, offsets);
  for (uint32_t i = 0; i < nvb; ++i) record::vkCmdBindVertexBuffers(cmd, vbs[i].binding, 1, &vbs[i].buffer, &vbs[i].offset);
  if (h.flags & pd::kIndex) record::vkCmdBindIndexBuffer(cmd, ib.buffer, ib.offset, indexType);
  if (h.flags & pd::kViewport) record::vkCmdSetViewport(cmd, 0, 1, &s.viewport);
  if (h.flags & pd::kScissor) record::vkCmdSetScissor(cmd, 0, 1, &s.scissor);
  if (h.flags & pd::kBlend) record::vkCmdSetBlendConstants(cmd, reinterpret_cast<const float*>(s.blend.data()));
  if (h.flags & pd::kStencilF) record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, s.stencilFront);
  if (h.flags & pd::kStencilB) record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, s.stencilBack);
  record::vkCmdDrawIndexed(cmd, drawCount, 1, 0, 0, 0);
}
}  // namespace pdesc
