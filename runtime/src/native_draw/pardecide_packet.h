// pardecide S1 (WWHD_NATIVE_PAR_DECIDE >= 1, decomp build fast mode): a native packet's replay (draw.cpp packet_draw,
// "same": the lowered kind drawn again) recorded as one POD extension command (native_draw/pardecide_ops.h,
// kPacketOrder) instead of 3-6 per-call POD commands. Included by draw.cpp before packet_draw.
//
// Exactness: the per-call path's decisions in its order (kind change, the kind's own buffer or the instance's stream
// through the buffer cache / a snapshot, same calls in the same order); only the encoding changes. Without the record
// worker's stream, or when the stream rejects the command, the same calls are recorded one by one.
#pragma once
#include "native_draw/pardecide_emit.h"  // (included by draw.cpp at file scope first)

namespace pdpacket {
namespace pd = ::native_draw::pardecide;
template <class Cap>
inline void record(VkCommandBuffer cmd, bool kindChange, bool sameVertexInput, const Cap& cap, const uint32_t* r,
                   const VkDescriptorSet* sets, const uint32_t* offsets) {
  alignas(8) std::byte buf[pd::kDescMaxBytes];
  const uint32_t code = 4;  // record_mt::Ext::PardecideDesc
  std::memcpy(buf, &code, sizeof code);
  pd::DescHead h{};
  h.cmd = cmd, h.pipeline = cap.vkPipeline, h.layout = cap.layout;
  h.flags = pd::kPacketOrder;
  if (kindChange) {
    h.flags |= pd::kPipe | pd::kIndex;
    h.indexBuffer = cap.indexBuffer, h.indexOffset = cap.indexOffset, h.indexType = uint32_t(cap.indexType);
  }
  h.setCount = 2, h.offsetCount = cap.offsetCount, h.drawCount = cap.drawCount;
  pd::DescVb vbs[pd::kDescMaxVb];
  uint32_t nvb = 0;
  bool fits = cap.offsetCount <= pd::kDescMaxOffsets && cap.vertexBindCount <= pd::kDescMaxVb;
  for (uint32_t i = 0; i < cap.vertexBindCount; ++i) {
    const auto& v = cap.vertexBinds[i];
    const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
    if (a[0] == v.regs[0] && a[1] == v.regs[1]) {  // the kind's own buffer
      if ((kindChange || !sameVertexInput) && nvb < pd::kDescMaxVb) vbs[nvb++] = {v.buffer, v.offset, v.binding, 0};
      continue;
    }
    const uint32_t addr = a[0], size = a[1] + 1;
    UploadSlice b;
    {
      rprof::UploadKind uploads(rprof::kUpVertex);
      if (!buffer_cache_enabled() || !cached_guest_range(addr, size, rprof::kUpVertex, b))
        b = snapshot(mem::ptr(addr), size, 4);
      rprof::guest_read(rprof::kUpVertex, addr, size);
    }
    if (nvb < pd::kDescMaxVb) vbs[nvb++] = {b.buffer, b.offset, v.binding, 0};
  }
  h.vbCount = nvb;
  if (fits) {
    std::byte* p = buf + sizeof code;
    std::memcpy(p, &h, sizeof h), p += sizeof h;
    std::memcpy(p, sets, 2 * sizeof(VkDescriptorSet)), p += 2 * sizeof(VkDescriptorSet);
    std::memcpy(p, offsets, cap.offsetCount * sizeof(uint32_t)), p += cap.offsetCount * sizeof(uint32_t);
    std::memcpy(p, vbs, nvb * sizeof(pd::DescVb)), p += nvb * sizeof(pd::DescVb);
    if (pd::emit_ext(buf, size_t(p - buf))) {
      pd::counters().packetCompact.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  pd::counters().packetFallback.fetch_add(1, std::memory_order_relaxed);
  if (kindChange) {
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cap.vkPipeline);
    record::vkCmdBindIndexBuffer(cmd, cap.indexBuffer, cap.indexOffset, cap.indexType);
  }
  for (uint32_t i = 0; i < nvb; ++i) record::vkCmdBindVertexBuffers(cmd, vbs[i].binding, 1, &vbs[i].buffer, &vbs[i].offset);
  record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cap.layout, 0, 2, sets, cap.offsetCount, offsets);
  record::vkCmdDrawIndexed(cmd, cap.drawCount, 1, 0, 0, 0);
}
}  // namespace pdpacket
