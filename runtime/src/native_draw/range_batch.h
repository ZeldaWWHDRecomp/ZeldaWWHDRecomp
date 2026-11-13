// Shader-rewrite range batches (decomp build, fast mode, WWHD_NATIVE_DRAW): included by gfx/vulkan/draw.cpp after
// its packet state (LoweredKind, PendingBatch), not compiled anywhere else.
//
// A kind whose instances each draw their own index range (a magma ball's strips) and whose blocks and vertex buffers
// change from object to object (one ball, then the next) gets one exact range batch per object today. A range batch
// of the shader-rewrite form takes every object's instances: the instances' entries go to the per-instance stream as
// in a stream batch (firstInstance: the instance), their index bytes back to back as in an exact range batch
// (firstIndex), and each object's vertex buffers (the instances with the same vertex buffer registers: a segment)
// copied as the exact range batch copies them, each segment at a vertex base of its own in every vertex buffer
// (vertexOffset). So every instance reads the same index values, vertices and entries as its own replay; the
// difference is the batch shaders (native_draw.h: the per-kind LOD-0 proof). Refused for a kind whose vertex shader
// may read the vertex index (vertexOffset is part of it), with an instance-rate or zero-stride vertex buffer.
#pragma once

struct RangeBatch {
  std::vector<uint8_t> index;                    // the instances' index bytes (the lowered index form)
  std::array<std::vector<uint8_t>, 16> vertex;   // per vertex buffer of the kind: the segments' copies
  uint32_t regs[16][2]{};                        // the open segment's vertex buffers (address and size words)
  bool open = false, extentValid = true;
  uint32_t base = 0, maxIndex = 0;               // the open segment's vertex base and largest index so far
  void clear() {
    index.clear();
    for (auto& v : vertex) v.clear();
    open = false;
  }
};

inline const char* range_batch_refusal(const PacketCapture& c) {
  if (const char* why = range_refusal(c)) return why;
  if (!c.vertexBindCount || c.vertexBindCount > 16) return "vertex buffer count";
  for (uint32_t i = 0; i < c.vertexBindCount; ++i)
    if (!c.vertexBinds[i].stride) return "zero stride";
  if (!c.vs || c.vs->source_text().empty()) return "the vertex shader text is not kept";
  if (vertex_index_live(c.vs)) return "the vertex shader may use the vertex index";
  return nullptr;
}

// one instance: its index bytes, its segment's vertex copies grown to the prefix its full draw needs, its command
inline void range_batch_append(RangeBatch& B, const LoweredKind& L, const native_draw::Mesh& m, const uint32_t* r,
                               std::vector<VkDrawIndexedIndirectCommand>& cmds, uint32_t instance) {
  const auto& cap = *L.cap;
  bool same = B.open;
  for (uint32_t i = 0; same && i < cap.vertexBindCount; ++i) {
    const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + cap.vertexBinds[i].binding * 7;
    same = a[0] == B.regs[i][0] && a[1] == B.regs[i][1];
  }
  if (!same) {  // a new segment: above every vertex buffer's copies so far
    uint32_t base = 0;
    for (uint32_t i = 0; i < cap.vertexBindCount; ++i) {
      const uint32_t stride = cap.vertexBinds[i].stride;
      base = std::max<uint32_t>(base, uint32_t((B.vertex[i].size() + stride - 1) / stride));
    }
    for (uint32_t i = 0; i < cap.vertexBindCount; ++i) {
      B.vertex[i].resize(size_t(base) * cap.vertexBinds[i].stride, 0);
      const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + cap.vertexBinds[i].binding * 7;
      B.regs[i][0] = a[0];
      B.regs[i][1] = a[1];
    }
    B.base = base;
    B.maxIndex = 0;
    B.extentValid = true;
    B.open = true;
  }
  const uint32_t width = cap.nativeIndexWidth ? cap.nativeIndexWidth : 4;
  const size_t at = B.index.size();
  size_t count = m.count;
  {
    rprof::UploadKind uploads(rprof::kUpIndex);
    if (cap.nativeIndexWidth) {
      B.index.resize(at + count * width);
      std::memcpy(B.index.data() + at, mem::ptr(m.indexAddr), count * width);
      rprof::guest_read(rprof::kUpIndex, m.indexAddr, count * width);
    } else {
      static thread_local std::vector<uint32_t> converted;
      vk::convert_indices(mem::ptr(m.indexAddr), m.prim, m.count, m.indexType, cap.stripRestart, cap.restartIndex,
                          converted);
      count = converted.size();
      B.index.resize(at + count * 4);
      std::memcpy(B.index.data() + at, converted.data(), count * 4);
      rprof::guest_read(rprof::kUpIndex, m.indexAddr, uint64_t(m.count) * (m.indexType == 4 || m.indexType == 0 ? 2 : 4));
    }
  }
  const VertexExtent e = indexed_vertex_extent(B.index.data() + at, count, width, cap.hostRestart, 0);
  if (!e.valid) B.extentValid = false;
  else B.maxIndex = std::max(B.maxIndex, e.maximum);
  for (uint32_t i = 0; i < cap.vertexBindCount; ++i) {
    const auto& v = cap.vertexBinds[i];
    const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
    const uint32_t addr = a[0], size = a[1] + 1;
    const uint32_t copied = v.supported ? vertex_prefix_size(size, v.stride, v.attributeEnd, {B.extentValid, B.maxIndex})
                                        : size;
    auto& out = B.vertex[i];
    const size_t from = out.size() - size_t(B.base) * v.stride;  // (this segment's bytes so far)
    if (copied <= from) continue;
    out.resize(out.size() + (copied - from));
    std::memcpy(out.data() + size_t(B.base) * v.stride + from, static_cast<const uint8_t*>(mem::ptr(addr)) + from,
                copied - from);
    rprof::guest_read(rprof::kUpVertex, addr + uint32_t(from), copied - from);
  }
  cmds.push_back({uint32_t(count), 1, uint32_t(at / width), int32_t(B.base), instance});
}

// the batch's index buffer and vertex buffers (the batch pipeline is bound)
inline void range_batch_bind(const RangeBatch& B, const LoweredKind& L, VkCommandBuffer cmd) {
  UploadSlice ix;
  {
    rprof::UploadKind uploads(rprof::kUpIndex);
    ix = snapshot(B.index.data(), B.index.size(), 4);
  }
  record::vkCmdBindIndexBuffer(cmd, ix.buffer, ix.offset, (*L.cap).indexType);
  for (uint32_t i = 0; i < (*L.cap).vertexBindCount; ++i) {
    UploadSlice v;
    {
      rprof::UploadKind uploads(rprof::kUpVertex);
      v = snapshot(B.vertex[i].data(), B.vertex[i].size(), 4);
    }
    record::vkCmdBindVertexBuffers(cmd, (*L.cap).vertexBinds[i].binding, 1, &v.buffer, &v.offset);
  }
}
