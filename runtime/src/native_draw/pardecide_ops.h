// pardecide (MT recording stage 4, "decide serially, fill off the render thread"; decomp build fast mode only):
// WWHD_NATIVE_PAR_DECIDE=N (default 1 = S1 on in the decomp fast build; 0 off).
//
// This header holds the POD extension commands (record_mt.h, opcode 31) that carry a decided draw's work to the record
// worker ("emitter"): the render thread keeps every decision (which binds, which upload slices, in stream order) and
// writes one compact command; the emitter expands it into exactly the Vulkan calls the per-call encoding would have
// replayed, in the same order. Shared by the producer (native_draw/pardecide_desc.h) and the emitter's replay
// (record_mt.cpp replay_extension, fast build only).
#pragma once
#if defined(WWHD_DECOMP_FAST)
#include "../gfx/vulkan/loader.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace native_draw::pardecide {
// WWHD_NATIVE_PAR_DECIDE: 0 off; 1 (the default) compact described draws and packet replays, expanded on the emitter
// (S1). Read once. Phone cliff 2026-11-17 (with SUPPORT2): render thread -0.22 ms, record worker -0.45 ms per frame.
inline int level() {
  static const int v = [] {
    const char* e = std::getenv("WWHD_NATIVE_PAR_DECIDE");
    return e && *e ? std::atoi(e) : 1;
  }();
  return v;
}

// --- Ext::DescDraw: one described J3D draw (j3d_desc_vk.h try_replay's record step) ---
// Fixed head, then sets[setCount], offsets[offsetCount], vertex binds[vbCount]. Each flag bit names a call the
// per-call path records; the expansion order is the per-call order: pipeline, descriptor sets, vertex buffers (in
// the state's order), index buffer, viewport, scissor, blend constants, stencil front, stencil back, the draw.
enum DescFlags : uint32_t { kPipe = 1, kIndex = 2, kViewport = 4, kScissor = 8, kBlend = 16, kStencilF = 32, kStencilB = 64,
                           // a native packet's replay (draw.cpp packet_draw "same"): pipeline, index buffer, vertex
                           // buffers, descriptor sets, the draw (no dynamic state)
                           kPacketOrder = 128 };
struct DescHead {
  VkCommandBuffer cmd;
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkBuffer indexBuffer;
  VkDeviceSize indexOffset;
  VkViewport viewport;
  VkRect2D scissor;
  float blend[4];
  uint32_t flags, setCount, offsetCount, vbCount, indexType, stencilFront, stencilBack, drawCount;
};
struct DescVb {
  VkBuffer buffer;
  VkDeviceSize offset;
  uint32_t binding, pad;
};
constexpr size_t kDescMaxSets = 2, kDescMaxOffsets = 34, kDescMaxVb = 32;
constexpr size_t kDescMaxBytes = sizeof(uint32_t) + sizeof(DescHead) + kDescMaxSets * sizeof(VkDescriptorSet) +
                                 kDescMaxOffsets * sizeof(uint32_t) + kDescMaxVb * sizeof(DescVb) + 16;

struct Counters {
  std::atomic<uint64_t> descCompact{0}, descFallback{0}, emitterDesc{0}, packetCompact{0}, packetFallback{0}, emitterPacket{0};
};
inline Counters& counters() {
  static Counters c;
  return c;
}

// render thread, every 300 frames (j3ddesc d1 frame_end): "[pardecide] per frame" line
inline void report(double frames) {
  auto take = [](std::atomic<uint64_t>& v) { return double(v.exchange(0, std::memory_order_relaxed)); };
  const double c = take(counters().descCompact), f = take(counters().descFallback), e = take(counters().emitterDesc);
  const double pc = take(counters().packetCompact), pf = take(counters().packetFallback), pe = take(counters().emitterPacket);
  std::fprintf(stderr, "[pardecide-s1] per frame (level %d): described draws compact %.1f, per-call fallback %.2f, emitter "
               "expansions %.1f; packet replays compact %.1f, per-call fallback %.2f, emitter expansions %.1f\n", level(),
               c / frames, f / frames, e / frames, pc / frames, pf / frames, pe / frames);
}
// emitter: the replay of one DescDraw (BYTES after the sub-opcode); EXECUTE false only validates
inline void replay_desc(const std::byte* bytes, size_t size, bool execute) {
  DescHead h;
  if (size < sizeof h) throw std::runtime_error("truncated pardecide draw");
  std::memcpy(&h, bytes, sizeof h);
  if (h.setCount > kDescMaxSets || h.offsetCount > kDescMaxOffsets || h.vbCount > kDescMaxVb)
    throw std::runtime_error("bad pardecide draw");
  const size_t need = sizeof h + h.setCount * sizeof(VkDescriptorSet) + h.offsetCount * sizeof(uint32_t) +
                      h.vbCount * sizeof(DescVb);
  if (size != need) throw std::runtime_error("bad pardecide draw size");
  if (!execute) return;
  const std::byte* p = bytes + sizeof h;
  VkDescriptorSet sets[kDescMaxSets];
  uint32_t offsets[kDescMaxOffsets];
  std::memcpy(sets, p, h.setCount * sizeof(VkDescriptorSet));
  p += h.setCount * sizeof(VkDescriptorSet);
  std::memcpy(offsets, p, h.offsetCount * sizeof(uint32_t));
  p += h.offsetCount * sizeof(uint32_t);
  if (h.flags & kPacketOrder) {
    if (h.flags & kPipe) gfxvk::vkCmdBindPipeline(h.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, h.pipeline);
    if (h.flags & kIndex) gfxvk::vkCmdBindIndexBuffer(h.cmd, h.indexBuffer, h.indexOffset, VkIndexType(h.indexType));
    for (uint32_t i = 0; i < h.vbCount; ++i) {
      DescVb v;
      std::memcpy(&v, p + i * sizeof v, sizeof v);
      gfxvk::vkCmdBindVertexBuffers(h.cmd, v.binding, 1, &v.buffer, &v.offset);
    }
    gfxvk::vkCmdBindDescriptorSets(h.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, h.layout, 0, h.setCount, sets, h.offsetCount,
                                   offsets);
    gfxvk::vkCmdDrawIndexed(h.cmd, h.drawCount, 1, 0, 0, 0);
    counters().emitterPacket.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (h.flags & kPipe) gfxvk::vkCmdBindPipeline(h.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, h.pipeline);
  gfxvk::vkCmdBindDescriptorSets(h.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, h.layout, 0, h.setCount, sets, h.offsetCount,
                                 offsets);
  for (uint32_t i = 0; i < h.vbCount; ++i) {
    DescVb v;
    std::memcpy(&v, p + i * sizeof v, sizeof v);
    gfxvk::vkCmdBindVertexBuffers(h.cmd, v.binding, 1, &v.buffer, &v.offset);
  }
  if (h.flags & kIndex) gfxvk::vkCmdBindIndexBuffer(h.cmd, h.indexBuffer, h.indexOffset, VkIndexType(h.indexType));
  if (h.flags & kViewport) gfxvk::vkCmdSetViewport(h.cmd, 0, 1, &h.viewport);
  if (h.flags & kScissor) gfxvk::vkCmdSetScissor(h.cmd, 0, 1, &h.scissor);
  if (h.flags & kBlend) gfxvk::vkCmdSetBlendConstants(h.cmd, h.blend);
  if (h.flags & kStencilF) gfxvk::vkCmdSetStencilReference(h.cmd, VK_STENCIL_FACE_FRONT_BIT, h.stencilFront);
  if (h.flags & kStencilB) gfxvk::vkCmdSetStencilReference(h.cmd, VK_STENCIL_FACE_BACK_BIT, h.stencilBack);
  gfxvk::vkCmdDrawIndexed(h.cmd, h.drawCount, 1, 0, 0, 0);
  counters().emitterDesc.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace native_draw::pardecide
#endif
