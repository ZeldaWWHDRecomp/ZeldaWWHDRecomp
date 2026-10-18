#include "pod_commands.h"
#include "handoff_trace.h"
#include "render_prof.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <type_traits>
namespace gfxvk {
std::mutex& graphics_queue_mutex() {
  static std::mutex m;
  return m;
}
namespace {
struct Header {
  uint32_t opcode;
  uint32_t bytes;
};
static_assert(std::is_trivially_copyable_v<Header>);
struct Writer {
  // Most draw/state packets fit here; avoid transient heap storage and growth.
  std::array<std::byte, 256> inline_bytes;
  std::vector<std::byte> overflow;
  size_t used = 0, limit;
  bool valid = true;
  PodFailureReason failure; // assigned before every transition to invalid
  explicit Writer(size_t n) : limit(n) {}
  std::span<const std::byte> view() const {
    return {overflow.empty() ? inline_bytes.data() : overflow.data(), used};
  }
  void raw(const void *p, size_t count, size_t size) {
    if (!valid) return;
    if ((count && !p) || size == 0) {
      failure = PodFailureReason::InvalidInput; valid = false; return;
    }
    if (count > (limit - used) / size) {
      failure = PodFailureReason::ByteBudgetOrOverflow; valid = false; return;
    }
    const size_t n = count * size;
    if (!n) return;
    const size_t next = used + n;
    if (overflow.empty() && next <= inline_bytes.size()) {
      std::memcpy(inline_bytes.data() + used, p, n);
    } else {
      const bool spilling = overflow.empty();
      overflow.resize(next);
      if (spilling && used) std::memcpy(overflow.data(), inline_bytes.data(), used);
      std::memcpy(overflow.data() + used, p, n);
    }
    used = next;
  }
  template <class T> void put(const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    raw(&value, 1, sizeof(T));
  }
  template <class T> void array(uint32_t count, const T *p) {
    raw(p, count, sizeof(T));
  }
};
struct Reader {
  const std::byte *bytes;
  size_t remaining;
  bool materialize = true;
  template <class T> T get() {
    static_assert(std::is_trivially_copyable_v<T>);
    if (sizeof(T) > remaining)
      throw std::runtime_error("truncated POD command");
    T value{};
    std::memcpy(&value, bytes, sizeof(T));
    bytes += sizeof(T);
    remaining -= sizeof(T);
    return value;
  }
  template <class T> std::vector<T> array(uint32_t count) {
    if (count > remaining / sizeof(T))
      throw std::runtime_error("truncated POD array");
    const size_t n = size_t(count) * sizeof(T);
    std::vector<T> value;
    if (materialize) {
      value.resize(count);
      if (n) std::memcpy(value.data(), bytes, n);
    } else if constexpr (requires(const T &v) { v.pNext; }) {
      // Preserve semantic chain validation during the allocation-free dry pass.
      // Typed memcpy decoding is still required: packet arrays may be unaligned.
      for (uint32_t i = 0; i < count; ++i) {
        T item;
        std::memcpy(&item, bytes + size_t(i) * sizeof(T), sizeof(T));
        if (item.pNext) throw std::runtime_error("invalid POD array chain");
      }
    }
    bytes += n;
    remaining -= n;
    return value;
  }
};
template <class T> const T *data(const std::vector<T> &v) {
  return v.empty() ? nullptr : v.data();
}
template <class T> bool chains(uint32_t n, const T *p, PodFailureReason& reason) {
  if (n && !p) { reason = PodFailureReason::InvalidInput; return false; }
  for (uint32_t i = 0; i < n; ++i)
    if (p[i].pNext) { reason = PodFailureReason::UnsupportedPNext; return false; }
  return true;
}
template <class T> bool chains(uint32_t n, const T* p) {
  PodFailureReason reason;
  return chains(n, p, reason); // Replay validation preserves its fatal behavior.
}
void checked(VkResult r) {
  if (r != VK_SUCCESS)
    throw std::runtime_error("POD Vulkan operation failed: " +
                             std::to_string(int(r)));
}
} // namespace
bool PodCommands::append(uint32_t opcode,
                         std::span<const std::byte> payload) {
  if (commands_ >= limits_.maxCommands)
    return reject(PodFailureReason::CommandLimit);
  if (bytes_.size() > limits_.maxPayloadBytes || payload.size() > UINT32_MAX ||
      sizeof(Header) > limits_.maxPayloadBytes - bytes_.size() ||
      payload.size() > limits_.maxPayloadBytes - bytes_.size() - sizeof(Header))
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  const Header h{opcode, uint32_t(payload.size())};
  try {
    auto old = bytes_.size();
    bytes_.resize(old + sizeof h + payload.size());
    std::memcpy(bytes_.data() + old, &h, sizeof h);
    if (!payload.empty())
      std::memcpy(bytes_.data() + old + sizeof h, payload.data(),
                  payload.size());
    ++commands_;
    return true;
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
// Fixed scalar packets only: no padded aggregate, borrowed array or pointer dereference.
template<class... T>
bool PodCommands::append_scalars(uint32_t opcode, const T&... values) {
  static_assert((std::is_trivially_copyable_v<T> && ...));
  constexpr size_t payload = (sizeof(T) + ... + 0);
  if (payload > limits_.maxPayloadBytes) return reject(PodFailureReason::ByteBudgetOrOverflow);
  if (commands_ >= limits_.maxCommands) return reject(PodFailureReason::CommandLimit);
  if (bytes_.size() > limits_.maxPayloadBytes || payload > UINT32_MAX ||
      sizeof(Header) > limits_.maxPayloadBytes - bytes_.size() ||
      payload > limits_.maxPayloadBytes - bytes_.size() - sizeof(Header))
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  try {
#ifdef WWHD_POD_SCALAR_TEST
    if (failDirectGrowth_) { failDirectGrowth_ = false; throw std::bad_alloc(); }
#endif
    const size_t old = bytes_.size();
    bytes_.resize(old + sizeof(Header) + payload); // Strong guarantee before writes.
    const Header header{opcode, uint32_t(payload)};
    std::memcpy(bytes_.data() + old, &header, sizeof(header));
    size_t cursor = old + sizeof(header);
    auto put = [&](const auto& value) {
      std::memcpy(bytes_.data() + cursor, &value, sizeof(value)); cursor += sizeof(value);
    };
    (put(values), ...); // Only nonthrowing field copies after successful growth.
    ++commands_; return true;
  } catch (const std::bad_alloc&) { return reject(PodFailureReason::AllocationFailure); }
}
bool PodCommands::vkCmdBindPipeline(VkCommandBuffer cmd,
                                    VkPipelineBindPoint point,
                                    VkPipeline pipeline) {
  if (directScalars_) return append_scalars(1, cmd, point, pipeline);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(point);
    w.put(pipeline);
    if (!w.valid) return reject(w.failure);
    return append(1, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer,
                                       VkDeviceSize offset, VkIndexType type) {
  if (directScalars_) return append_scalars(2, cmd, buffer, offset, type);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(buffer);
    w.put(offset);
    w.put(type);
    if (!w.valid) return reject(w.failure);
    return append(2, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdDraw(VkCommandBuffer cmd, uint32_t vertices,
                            uint32_t instances, uint32_t firstVertex,
                            uint32_t firstInstance) {
  if (directScalars_) return append_scalars(3, cmd, vertices, instances, firstVertex, firstInstance);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(vertices);
    w.put(instances);
    w.put(firstVertex);
    w.put(firstInstance);
    if (!w.valid) return reject(w.failure);
    return append(3, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdDrawIndexed(VkCommandBuffer cmd, uint32_t indices,
                                   uint32_t instances, uint32_t firstIndex,
                                   int32_t vertexOffset,
                                   uint32_t firstInstance) {
  if (directScalars_) return append_scalars(4, cmd, indices, instances, firstIndex, vertexOffset, firstInstance);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(indices);
    w.put(instances);
    w.put(firstIndex);
    w.put(vertexOffset);
    w.put(firstInstance);
    if (!w.valid) return reject(w.failure);
    return append(4, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y,
                                uint32_t z) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(x);
    w.put(y);
    w.put(z);
    if (!w.valid) return reject(w.failure);
    return append(5, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdEndRendering(VkCommandBuffer cmd) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    if (!w.valid) return reject(w.failure);
    return append(6, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdSetStencilReference(VkCommandBuffer cmd,
                                           VkStencilFaceFlags faces,
                                           uint32_t reference) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(faces);
    w.put(reference);
    if (!w.valid) return reject(w.failure);
    return append(7, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdSetStencilWriteMask(VkCommandBuffer cmd,
                                           VkStencilFaceFlags faces,
                                           uint32_t mask) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(faces);
    w.put(mask);
    if (!w.valid) return reject(w.failure);
    return append(8, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdResetQueryPool(VkCommandBuffer cmd, VkQueryPool pool,
                                      uint32_t first, uint32_t count) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(pool);
    w.put(first);
    w.put(count);
    if (!w.valid) return reject(w.failure);
    return append(9, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdWriteTimestamp(VkCommandBuffer cmd,
                                      VkPipelineStageFlagBits stage,
                                      VkQueryPool pool, uint32_t query) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(stage);
    w.put(pool);
    w.put(query);
    if (!w.valid) return reject(w.failure);
    return append(10, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdSetViewport(VkCommandBuffer cmd, uint32_t first,
                                   uint32_t count, const VkViewport *values) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(first);
    w.put(count);
    w.array(count, static_cast<const VkViewport *>(values));
    if (!w.valid) return reject(w.failure);
    return append(11, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdSetScissor(VkCommandBuffer cmd, uint32_t first,
                                  uint32_t count, const VkRect2D *values) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(first);
    w.put(count);
    w.array(count, static_cast<const VkRect2D *>(values));
    if (!w.valid) return reject(w.failure);
    return append(12, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer source,
                                  VkBuffer destination, uint32_t count,
                                  const VkBufferCopy *regions) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(source);
    w.put(destination);
    w.put(count);
    w.array(count, static_cast<const VkBufferCopy *>(regions));
    if (!w.valid) return reject(w.failure);
    return append(13, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer source,
                                         VkImage destination,
                                         VkImageLayout layout, uint32_t count,
                                         const VkBufferImageCopy *regions) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(source);
    w.put(destination);
    w.put(layout);
    w.put(count);
    w.array(count, static_cast<const VkBufferImageCopy *>(regions));
    if (!w.valid) return reject(w.failure);
    return append(14, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage source,
                                         VkImageLayout layout,
                                         VkBuffer destination, uint32_t count,
                                         const VkBufferImageCopy *regions) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(source);
    w.put(layout);
    w.put(destination);
    w.put(count);
    w.array(count, static_cast<const VkBufferImageCopy *>(regions));
    if (!w.valid) return reject(w.failure);
    return append(15, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdCopyImage(VkCommandBuffer cmd, VkImage source,
                                 VkImageLayout sourceLayout,
                                 VkImage destination,
                                 VkImageLayout destinationLayout,
                                 uint32_t count, const VkImageCopy *regions) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(source);
    w.put(sourceLayout);
    w.put(destination);
    w.put(destinationLayout);
    w.put(count);
    w.array(count, static_cast<const VkImageCopy *>(regions));
    if (!w.valid) return reject(w.failure);
    return append(16, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdBlitImage(VkCommandBuffer cmd, VkImage source,
                                 VkImageLayout sourceLayout,
                                 VkImage destination,
                                 VkImageLayout destinationLayout,
                                 uint32_t count, const VkImageBlit *regions,
                                 VkFilter filter) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(source);
    w.put(sourceLayout);
    w.put(destination);
    w.put(destinationLayout);
    w.put(count);
    w.array(count, static_cast<const VkImageBlit *>(regions));
    w.put(filter);
    if (!w.valid) return reject(w.failure);
    return append(17, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first,
                                         uint32_t count,
                                         const VkBuffer *buffers,
                                         const VkDeviceSize *offsets) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(first);
    w.put(count);
    w.array(count, static_cast<const VkBuffer *>(buffers));
    w.array(count, static_cast<const VkDeviceSize *>(offsets));
    if (!w.valid) return reject(w.failure);
    return append(18, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdBindDescriptorSets(
    VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout,
    uint32_t first, uint32_t count, const VkDescriptorSet *sets,
    uint32_t offsetCount, const uint32_t *offsets) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(point);
    w.put(layout);
    w.put(first);
    w.put(count);
    w.array(count, static_cast<const VkDescriptorSet *>(sets));
    w.put(offsetCount);
    w.array(offsetCount, static_cast<const uint32_t *>(offsets));
    if (!w.valid) return reject(w.failure);
    return append(19, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdPushConstants(VkCommandBuffer cmd,
                                     VkPipelineLayout layout,
                                     VkShaderStageFlags stages, uint32_t offset,
                                     uint32_t size, const void *values) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(layout);
    w.put(stages);
    w.put(offset);
    w.put(size);
    w.array(size, static_cast<const std::byte *>(values));
    if (!w.valid) return reject(w.failure);
    return append(20, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdSetBlendConstants(VkCommandBuffer cmd,
                                         const float values[4]) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.array(4, static_cast<const float *>(values));
    if (!w.valid) return reject(w.failure);
    return append(21, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdClearColorImage(VkCommandBuffer cmd, VkImage image,
                                       VkImageLayout layout,
                                       const VkClearColorValue *value,
                                       uint32_t count,
                                       const VkImageSubresourceRange *ranges) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(image);
    w.put(layout);
    w.array(1, static_cast<const VkClearColorValue *>(value));
    w.put(count);
    w.array(count, static_cast<const VkImageSubresourceRange *>(ranges));
    if (!w.valid) return reject(w.failure);
    return append(22, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdClearDepthStencilImage(
    VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
    const VkClearDepthStencilValue *value, uint32_t count,
    const VkImageSubresourceRange *ranges) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(image);
    w.put(layout);
    w.array(1, static_cast<const VkClearDepthStencilValue *>(value));
    w.put(count);
    w.array(count, static_cast<const VkImageSubresourceRange *>(ranges));
    if (!w.valid) return reject(w.failure);
    return append(23, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdBeginRendering(VkCommandBuffer cmd,
                                      const VkRenderingInfo *info) {
  // Reject the complete packet before reading any nested caller array.
  if (commands_ >= limits_.maxCommands) return reject(PodFailureReason::CommandLimit);
  if (bytes_.size() > limits_.maxPayloadBytes ||
      sizeof(Header) > limits_.maxPayloadBytes - bytes_.size())
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  size_t remaining = std::min<size_t>(
      UINT32_MAX, limits_.maxPayloadBytes - bytes_.size() - sizeof(Header));
  const auto consume = [&remaining](size_t count, size_t element) {
    if (count > remaining / element)
      return false;
    remaining -= count * element;
    return true;
  };
  if (!consume(1, sizeof(cmd) + sizeof(VkRenderingInfo) + sizeof(uint32_t)))
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  if (!info) return reject(PodFailureReason::InvalidInput);
  if (info->pNext) return reject(PodFailureReason::UnsupportedPNext);
  if (!consume(info->colorAttachmentCount, sizeof(VkRenderingAttachmentInfo)) ||
      !consume(info->pDepthAttachment ? 1 : 0,
               sizeof(VkRenderingAttachmentInfo)) ||
      !consume(info->pStencilAttachment ? 1 : 0,
               sizeof(VkRenderingAttachmentInfo)))
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  PodFailureReason reason;
  if (!chains(info->colorAttachmentCount, info->pColorAttachments, reason)) return reject(reason);
  if ((info->pDepthAttachment && info->pDepthAttachment->pNext) ||
      (info->pStencilAttachment && info->pStencilAttachment->pNext))
    return reject(PodFailureReason::UnsupportedPNext);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    auto copy = *info;
    copy.pColorAttachments = nullptr;
    copy.pDepthAttachment = nullptr;
    copy.pStencilAttachment = nullptr;
    w.put(copy);
    uint32_t mask = (info->pDepthAttachment ? 1u : 0u) |
                    (info->pStencilAttachment ? 2u : 0u) |
                    ((info->pDepthAttachment &&
                      info->pDepthAttachment == info->pStencilAttachment)
                         ? 4u
                         : 0u);
    w.put(mask);
    w.array(info->colorAttachmentCount, info->pColorAttachments);
    if (mask & 1)
      w.put(*info->pDepthAttachment);
    if (mask & 2)
      w.put(*info->pStencilAttachment);
    if (!w.valid) return reject(w.failure);
    return append(24, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdPipelineBarrier(
    VkCommandBuffer cmd, VkPipelineStageFlags source,
    VkPipelineStageFlags destination, VkDependencyFlags flags,
    uint32_t memoryCount, const VkMemoryBarrier *memory, uint32_t bufferCount,
    const VkBufferMemoryBarrier *buffers, uint32_t imageCount,
    const VkImageMemoryBarrier *images) {
  // Counts are budgeted together before chains() dereferences any array.
  if (commands_ >= limits_.maxCommands) return reject(PodFailureReason::CommandLimit);
  if (bytes_.size() > limits_.maxPayloadBytes ||
      sizeof(Header) > limits_.maxPayloadBytes - bytes_.size())
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  size_t remaining = std::min<size_t>(
      UINT32_MAX, limits_.maxPayloadBytes - bytes_.size() - sizeof(Header));
  const auto consume = [&remaining](size_t count, size_t element) {
    if (count > remaining / element)
      return false;
    remaining -= count * element;
    return true;
  };
  if (!consume(1, sizeof(cmd) + sizeof(source) + sizeof(destination) +
                      sizeof(flags) + sizeof(memoryCount) +
                      sizeof(bufferCount) + sizeof(imageCount)) ||
      !consume(memoryCount, sizeof(VkMemoryBarrier)) ||
      !consume(bufferCount, sizeof(VkBufferMemoryBarrier)) ||
      !consume(imageCount, sizeof(VkImageMemoryBarrier)))
    return reject(PodFailureReason::ByteBudgetOrOverflow);
  PodFailureReason reason;
  if (!chains(memoryCount, memory, reason)) return reject(reason);
  if (!chains(bufferCount, buffers, reason)) return reject(reason);
  if (!chains(imageCount, images, reason)) return reject(reason);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(source);
    w.put(destination);
    w.put(flags);
    w.put(memoryCount);
    w.array(memoryCount, static_cast<const VkMemoryBarrier *>(memory));
    w.put(bufferCount);
    w.array(bufferCount, static_cast<const VkBufferMemoryBarrier *>(buffers));
    w.put(imageCount);
    w.array(imageCount, static_cast<const VkImageMemoryBarrier *>(images));
    if (!w.valid) return reject(w.failure);
    return append(25, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdBeginRenderPass(VkCommandBuffer cmd,
                                       const VkRenderPassBeginInfo *info,
                                       VkSubpassContents contents) {
  if (!info) return reject(PodFailureReason::InvalidInput);
  if (info->pNext) return reject(PodFailureReason::UnsupportedPNext);
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    auto copy = *info;
    copy.pClearValues = nullptr;
    w.put(copy);
    w.array(info->clearValueCount, info->pClearValues);
    w.put(contents);
    if (!w.valid) return reject(w.failure);
    return append(26, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::vkCmdEndRenderPass(VkCommandBuffer cmd) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    if (!w.valid) return reject(w.failure);
    return append(27, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::beginCommandBuffer(VkCommandBuffer cmd,
                                     VkCommandBufferUsageFlags flags) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(cmd);
    w.put(flags);
    if (!w.valid) return reject(w.failure);
    return append(28, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
bool PodCommands::submit(VkDevice device, VkQueue queue, VkCommandBuffer cmd,
                         VkFence fence, VkSemaphore wait, VkSemaphore signal,
                         VkSemaphore extraWait, VkSemaphore extraSignal) {
  try {
    Writer w(limits_.maxPayloadBytes);
    w.put(device);
    w.put(queue);
    w.put(cmd);
    w.put(fence);
    w.put(wait);
    w.put(signal);
    w.put(extraWait);
    w.put(extraSignal);
    if (!w.valid) return reject(w.failure);
    return append(29, w.view());
  } catch (const std::bad_alloc &) {
    return reject(PodFailureReason::AllocationFailure);
  }
}
void PodCommands::replay(const std::vector<std::byte> &bytes) {
  const bool smallDiagnostic = handoff_trace::enabled() && bytes.size() <= 2048;
  std::array<uint32_t, 30> opcodeCounts;
  if (smallDiagnostic) opcodeCounts.fill(0);
  // Validate the entire stream before emitting its first Vulkan command.
  for (bool execute : {false, true}) {
    Reader stream{bytes.data(), bytes.size(), execute};
    while (stream.remaining) {
      auto h = stream.get<Header>();
      if (h.bytes > stream.remaining)
        throw std::runtime_error("truncated POD payload");
      Reader r{stream.bytes, h.bytes, execute};
      const bool timed = smallDiagnostic && execute;
      const uint64_t wallStart = timed ? handoff_trace::native_now_ns() : 0;
      const uint64_t cpuStart = timed ? rprof::thread_cpu_ns() : 0;
      switch (h.opcode) {
      case 1: {
        auto cmd = r.get<VkCommandBuffer>();
        auto point = r.get<VkPipelineBindPoint>();
        auto pipeline = r.get<VkPipeline>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBindPipeline(cmd, point, pipeline);
        break;
      }
      case 2: {
        auto cmd = r.get<VkCommandBuffer>();
        auto buffer = r.get<VkBuffer>();
        auto offset = r.get<VkDeviceSize>();
        auto type = r.get<VkIndexType>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBindIndexBuffer(cmd, buffer, offset, type);
        break;
      }
      case 3: {
        auto cmd = r.get<VkCommandBuffer>();
        auto vertices = r.get<uint32_t>();
        auto instances = r.get<uint32_t>();
        auto firstVertex = r.get<uint32_t>();
        auto firstInstance = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdDraw(cmd, vertices, instances, firstVertex,
                           firstInstance);
        break;
      }
      case 4: {
        auto cmd = r.get<VkCommandBuffer>();
        auto indices = r.get<uint32_t>();
        auto instances = r.get<uint32_t>();
        auto firstIndex = r.get<uint32_t>();
        auto vertexOffset = r.get<int32_t>();
        auto firstInstance = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdDrawIndexed(cmd, indices, instances, firstIndex,
                                  vertexOffset, firstInstance);
        break;
      }
      case 5: {
        auto cmd = r.get<VkCommandBuffer>();
        auto x = r.get<uint32_t>();
        auto y = r.get<uint32_t>();
        auto z = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdDispatch(cmd, x, y, z);
        break;
      }
      case 6: {
        auto cmd = r.get<VkCommandBuffer>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdEndRendering(cmd);
        break;
      }
      case 7: {
        auto cmd = r.get<VkCommandBuffer>();
        auto faces = r.get<VkStencilFaceFlags>();
        auto reference = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdSetStencilReference(cmd, faces, reference);
        break;
      }
      case 8: {
        auto cmd = r.get<VkCommandBuffer>();
        auto faces = r.get<VkStencilFaceFlags>();
        auto mask = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdSetStencilWriteMask(cmd, faces, mask);
        break;
      }
      case 9: {
        auto cmd = r.get<VkCommandBuffer>();
        auto pool = r.get<VkQueryPool>();
        auto first = r.get<uint32_t>();
        auto count = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdResetQueryPool(cmd, pool, first, count);
        break;
      }
      case 10: {
        auto cmd = r.get<VkCommandBuffer>();
        auto stage = r.get<VkPipelineStageFlagBits>();
        auto pool = r.get<VkQueryPool>();
        auto query = r.get<uint32_t>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdWriteTimestamp(cmd, stage, pool, query);
        break;
      }
      case 11: {
        auto cmd = r.get<VkCommandBuffer>();
        auto first = r.get<uint32_t>();
        auto count = r.get<uint32_t>();
        auto values = r.array<VkViewport>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdSetViewport(cmd, first, count, data(values));
        break;
      }
      case 12: {
        auto cmd = r.get<VkCommandBuffer>();
        auto first = r.get<uint32_t>();
        auto count = r.get<uint32_t>();
        auto values = r.array<VkRect2D>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdSetScissor(cmd, first, count, data(values));
        break;
      }
      case 13: {
        auto cmd = r.get<VkCommandBuffer>();
        auto source = r.get<VkBuffer>();
        auto destination = r.get<VkBuffer>();
        auto count = r.get<uint32_t>();
        auto regions = r.array<VkBufferCopy>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdCopyBuffer(cmd, source, destination, count,
                                 data(regions));
        break;
      }
      case 14: {
        auto cmd = r.get<VkCommandBuffer>();
        auto source = r.get<VkBuffer>();
        auto destination = r.get<VkImage>();
        auto layout = r.get<VkImageLayout>();
        auto count = r.get<uint32_t>();
        auto regions = r.array<VkBufferImageCopy>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdCopyBufferToImage(cmd, source, destination, layout, count,
                                        data(regions));
        break;
      }
      case 15: {
        auto cmd = r.get<VkCommandBuffer>();
        auto source = r.get<VkImage>();
        auto layout = r.get<VkImageLayout>();
        auto destination = r.get<VkBuffer>();
        auto count = r.get<uint32_t>();
        auto regions = r.array<VkBufferImageCopy>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdCopyImageToBuffer(cmd, source, layout, destination, count,
                                        data(regions));
        break;
      }
      case 16: {
        auto cmd = r.get<VkCommandBuffer>();
        auto source = r.get<VkImage>();
        auto sourceLayout = r.get<VkImageLayout>();
        auto destination = r.get<VkImage>();
        auto destinationLayout = r.get<VkImageLayout>();
        auto count = r.get<uint32_t>();
        auto regions = r.array<VkImageCopy>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdCopyImage(cmd, source, sourceLayout, destination,
                                destinationLayout, count, data(regions));
        break;
      }
      case 17: {
        auto cmd = r.get<VkCommandBuffer>();
        auto source = r.get<VkImage>();
        auto sourceLayout = r.get<VkImageLayout>();
        auto destination = r.get<VkImage>();
        auto destinationLayout = r.get<VkImageLayout>();
        auto count = r.get<uint32_t>();
        auto regions = r.array<VkImageBlit>(count);
        auto filter = r.get<VkFilter>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBlitImage(cmd, source, sourceLayout, destination,
                                destinationLayout, count, data(regions),
                                filter);
        break;
      }
      case 18: {
        auto cmd = r.get<VkCommandBuffer>();
        auto first = r.get<uint32_t>();
        auto count = r.get<uint32_t>();
        auto buffers = r.array<VkBuffer>(count);
        auto offsets = r.array<VkDeviceSize>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBindVertexBuffers(cmd, first, count, data(buffers),
                                        data(offsets));
        break;
      }
      case 19: {
        auto cmd = r.get<VkCommandBuffer>();
        auto point = r.get<VkPipelineBindPoint>();
        auto layout = r.get<VkPipelineLayout>();
        auto first = r.get<uint32_t>();
        auto count = r.get<uint32_t>();
        auto sets = r.array<VkDescriptorSet>(count);
        auto offsetCount = r.get<uint32_t>();
        auto offsets = r.array<uint32_t>(offsetCount);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBindDescriptorSets(cmd, point, layout, first, count,
                                         data(sets), offsetCount,
                                         data(offsets));
        break;
      }
      case 20: {
        auto cmd = r.get<VkCommandBuffer>();
        auto layout = r.get<VkPipelineLayout>();
        auto stages = r.get<VkShaderStageFlags>();
        auto offset = r.get<uint32_t>();
        auto size = r.get<uint32_t>();
        auto values = r.array<std::byte>(size);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdPushConstants(cmd, layout, stages, offset, size,
                                    data(values));
        break;
      }
      case 21: {
        auto cmd = r.get<VkCommandBuffer>();
        auto values = r.array<float>(4);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdSetBlendConstants(cmd, data(values));
        break;
      }
      case 22: {
        auto cmd = r.get<VkCommandBuffer>();
        auto image = r.get<VkImage>();
        auto layout = r.get<VkImageLayout>();
        auto value = r.array<VkClearColorValue>(1);
        auto count = r.get<uint32_t>();
        auto ranges = r.array<VkImageSubresourceRange>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdClearColorImage(cmd, image, layout, data(value), count,
                                      data(ranges));
        break;
      }
      case 23: {
        auto cmd = r.get<VkCommandBuffer>();
        auto image = r.get<VkImage>();
        auto layout = r.get<VkImageLayout>();
        auto value = r.array<VkClearDepthStencilValue>(1);
        auto count = r.get<uint32_t>();
        auto ranges = r.array<VkImageSubresourceRange>(count);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdClearDepthStencilImage(cmd, image, layout, data(value),
                                             count, data(ranges));
        break;
      }
      case 24: {
        auto cmd = r.get<VkCommandBuffer>();
        auto info = r.get<VkRenderingInfo>();
        auto mask = r.get<uint32_t>();
        if (info.pNext || info.pColorAttachments || info.pDepthAttachment ||
            info.pStencilAttachment || mask > 7 ||
            ((mask & 4) && ((mask & 3) != 3)))
          throw std::runtime_error("invalid rendering payload");
        auto colors =
            r.array<VkRenderingAttachmentInfo>(info.colorAttachmentCount);
        VkRenderingAttachmentInfo depth{}, stencil{};
        if (mask & 1)
          depth = r.get<VkRenderingAttachmentInfo>();
        if (mask & 2)
          stencil = r.get<VkRenderingAttachmentInfo>();
        if ((execute && !chains(info.colorAttachmentCount, data(colors))) ||
            ((mask & 1) && depth.pNext) || ((mask & 2) && stencil.pNext))
          throw std::runtime_error("invalid attachment chain");
        info.pColorAttachments = data(colors);
        info.pDepthAttachment = (mask & 1) ? &depth : nullptr;
        info.pStencilAttachment =
            (mask & 4) ? &depth : ((mask & 2) ? &stencil : nullptr);
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBeginRendering(cmd, &info);
        break;
      }
      case 25: {
        auto cmd = r.get<VkCommandBuffer>();
        auto source = r.get<VkPipelineStageFlags>();
        auto destination = r.get<VkPipelineStageFlags>();
        auto flags = r.get<VkDependencyFlags>();
        auto memoryCount = r.get<uint32_t>();
        auto memory = r.array<VkMemoryBarrier>(memoryCount);
        if (execute && !chains(memoryCount, data(memory)))
          throw std::runtime_error("invalid barrier chain");
        auto bufferCount = r.get<uint32_t>();
        auto buffers = r.array<VkBufferMemoryBarrier>(bufferCount);
        if (execute && !chains(bufferCount, data(buffers)))
          throw std::runtime_error("invalid barrier chain");
        auto imageCount = r.get<uint32_t>();
        auto images = r.array<VkImageMemoryBarrier>(imageCount);
        if (execute && !chains(imageCount, data(images)))
          throw std::runtime_error("invalid barrier chain");
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdPipelineBarrier(cmd, source, destination, flags,
                                      memoryCount, data(memory), bufferCount,
                                      data(buffers), imageCount, data(images));
        break;
      }
      case 26: {
        auto cmd = r.get<VkCommandBuffer>();
        auto info = r.get<VkRenderPassBeginInfo>();
        if (info.pNext || info.pClearValues)
          throw std::runtime_error("invalid renderpass payload");
        auto clears = r.array<VkClearValue>(info.clearValueCount);
        info.pClearValues = data(clears);
        auto contents = r.get<VkSubpassContents>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdBeginRenderPass(cmd, &info, contents);
        break;
      }
      case 27: {
        auto cmd = r.get<VkCommandBuffer>();
        if (r.remaining)
          throw std::runtime_error("trailing POD payload");
        if (execute)
          gfxvk::vkCmdEndRenderPass(cmd);
        break;
      }
      case 28: {
        auto cmd = r.get<VkCommandBuffer>();
        auto flags = r.get<VkCommandBufferUsageFlags>();
        if (r.remaining)
          throw std::runtime_error("trailing begin payload");
        if (execute) {
          VkCommandBufferBeginInfo info{
              VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
          info.flags = flags;
          checked(gfxvk::vkBeginCommandBuffer(cmd, &info));
        }
        break;
      }
      case 29: {
        auto device = r.get<VkDevice>();
        auto queue = r.get<VkQueue>();
        auto cmd = r.get<VkCommandBuffer>();
        auto fence = r.get<VkFence>();
        auto wait = r.get<VkSemaphore>();
        auto signal = r.get<VkSemaphore>();
        auto extraWait = r.get<VkSemaphore>();
        auto extraSignal = r.get<VkSemaphore>();
        if (r.remaining)
          throw std::runtime_error("trailing submit payload");
        if (execute) {
          checked(gfxvk::vkEndCommandBuffer(cmd));
          checked(gfxvk::vkResetFences(device, 1, &fence));
          std::array<VkSemaphore, 2> waits{}, signals{};
          std::array<VkPipelineStageFlags, 2> stages{
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
          VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
          info.commandBufferCount = 1;
          info.pCommandBuffers = &cmd;
          if (wait)
            waits[info.waitSemaphoreCount++] = wait;
          if (extraWait)
            waits[info.waitSemaphoreCount++] = extraWait;
          if (signal)
            signals[info.signalSemaphoreCount++] = signal;
          if (extraSignal)
            signals[info.signalSemaphoreCount++] = extraSignal;
          info.pWaitSemaphores = waits.data();
          info.pSignalSemaphores = signals.data();
          info.pWaitDstStageMask = stages.data();
          if (handoff_trace::enabled()) handoff_trace::emit_worker(handoff_trace::Kind::QueueSubmitBegin);
          VkResult result;
          {
            // a held TV present (even frame delivery) may be issued by the producer meanwhile
            std::lock_guard<std::mutex> queueLock(graphics_queue_mutex());
            result = gfxvk::vkQueueSubmit(queue, 1, &info, fence);
          }
          if (handoff_trace::enabled()) handoff_trace::emit_worker(handoff_trace::Kind::QueueSubmitEnd, uint64_t(int64_t(result)));
          checked(result);
        }
        break;
      }
      default:
        throw std::runtime_error("unknown POD opcode");
      }
      if (timed) {
        const uint64_t cpuEnd = rprof::thread_cpu_ns();
        const uint64_t wallEnd = handoff_trace::native_now_ns();
        ++opcodeCounts[h.opcode];
        handoff_trace::slow_opcode(h.opcode, wallStart, wallEnd, cpuStart, cpuEnd);
      }
      stream.bytes += h.bytes;
      stream.remaining -= h.bytes;
    }
  }
  if (smallDiagnostic)
    for (uint32_t opcode = 1; opcode < opcodeCounts.size(); ++opcode)
      if (opcodeCounts[opcode]) handoff_trace::emit_worker(handoff_trace::Kind::OpcodeInventory, opcode, opcodeCounts[opcode]);
}
} // namespace gfxvk
