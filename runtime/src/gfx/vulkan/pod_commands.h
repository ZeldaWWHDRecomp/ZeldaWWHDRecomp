#pragma once
#include "loader.h"
#include "pod_failure_reason.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>
namespace gfxvk {
// CPU argument storage only. Submission owner retains all referenced Vulkan
// resources. Append failure is atomic: caller drains then uses its existing
// immediate fallback.
class PodCommands {
public:
  struct Limits {
    size_t maxCommands = 65536, maxPayloadBytes = 4 * 1024 * 1024;
  };
  PodCommands() = default;
  explicit PodCommands(Limits limits) : limits_(limits) {}
  // Producer startup option; does not change serialized bytes or replay.
  void set_direct_scalars(bool enabled) { directScalars_ = enabled; }
#ifdef WWHD_POD_SCALAR_TEST
  void fail_next_direct_growth_for_test() { failDirectGrowth_ = true; }
#endif
  size_t command_count() const { return commands_; }
  size_t payload_bytes() const { return bytes_.size(); }
  PodFailureReason failure_reason() const { return failureReason_; }
  bool empty() const { return bytes_.empty(); }
  void clear() {
    bytes_.clear();
    commands_ = 0;
  }
  std::vector<std::byte> snapshot() const { return bytes_; }
  std::vector<std::byte> take() {
    auto result = std::move(bytes_);
    bytes_.clear();
    commands_ = 0;
    return result;
  }
  static void replay(const std::vector<std::byte> &bytes);
  bool beginCommandBuffer(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags);
  bool submit(VkDevice device, VkQueue queue, VkCommandBuffer cmd,
              VkFence fence, VkSemaphore wait = VK_NULL_HANDLE,
              VkSemaphore signal = VK_NULL_HANDLE,
              VkSemaphore extraWait = VK_NULL_HANDLE,
              VkSemaphore extraSignal = VK_NULL_HANDLE);
  bool vkCmdBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint point,
                         VkPipeline pipeline);
  bool vkCmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer,
                            VkDeviceSize offset, VkIndexType type);
  bool vkCmdDraw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances,
                 uint32_t firstVertex, uint32_t firstInstance);
  bool vkCmdDrawIndexed(VkCommandBuffer cmd, uint32_t indices,
                        uint32_t instances, uint32_t firstIndex,
                        int32_t vertexOffset, uint32_t firstInstance);
  bool vkCmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z);
  bool vkCmdEndRendering(VkCommandBuffer cmd);
  bool vkCmdSetStencilReference(VkCommandBuffer cmd, VkStencilFaceFlags faces,
                                uint32_t reference);
  bool vkCmdSetStencilWriteMask(VkCommandBuffer cmd, VkStencilFaceFlags faces,
                                uint32_t mask);
  bool vkCmdResetQueryPool(VkCommandBuffer cmd, VkQueryPool pool,
                           uint32_t first, uint32_t count);
  bool vkCmdWriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage,
                           VkQueryPool pool, uint32_t query);
  bool vkCmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                        const VkViewport *values);
  bool vkCmdSetScissor(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                       const VkRect2D *values);
  bool vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer source,
                       VkBuffer destination, uint32_t count,
                       const VkBufferCopy *regions);
  bool vkCmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer source,
                              VkImage destination, VkImageLayout layout,
                              uint32_t count, const VkBufferImageCopy *regions);
  bool vkCmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage source,
                              VkImageLayout layout, VkBuffer destination,
                              uint32_t count, const VkBufferImageCopy *regions);
  bool vkCmdCopyImage(VkCommandBuffer cmd, VkImage source,
                      VkImageLayout sourceLayout, VkImage destination,
                      VkImageLayout destinationLayout, uint32_t count,
                      const VkImageCopy *regions);
  bool vkCmdBlitImage(VkCommandBuffer cmd, VkImage source,
                      VkImageLayout sourceLayout, VkImage destination,
                      VkImageLayout destinationLayout, uint32_t count,
                      const VkImageBlit *regions, VkFilter filter);
  bool vkCmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first,
                              uint32_t count, const VkBuffer *buffers,
                              const VkDeviceSize *offsets);
  bool vkCmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint point,
                               VkPipelineLayout layout, uint32_t first,
                               uint32_t count, const VkDescriptorSet *sets,
                               uint32_t offsetCount, const uint32_t *offsets);
  bool vkCmdPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                          VkShaderStageFlags stages, uint32_t offset,
                          uint32_t size, const void *values);
  bool vkCmdSetBlendConstants(VkCommandBuffer cmd, const float values[4]);
  bool vkCmdClearColorImage(VkCommandBuffer cmd, VkImage image,
                            VkImageLayout layout,
                            const VkClearColorValue *value, uint32_t count,
                            const VkImageSubresourceRange *ranges);
  bool vkCmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout layout,
                                   const VkClearDepthStencilValue *value,
                                   uint32_t count,
                                   const VkImageSubresourceRange *ranges);
  bool vkCmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo *info);
  bool vkCmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags source,
                            VkPipelineStageFlags destination,
                            VkDependencyFlags flags, uint32_t memoryCount,
                            const VkMemoryBarrier *memory, uint32_t bufferCount,
                            const VkBufferMemoryBarrier *buffers,
                            uint32_t imageCount,
                            const VkImageMemoryBarrier *images);
  bool vkCmdBeginRenderPass(VkCommandBuffer cmd,
                            const VkRenderPassBeginInfo *info,
                            VkSubpassContents contents);
  bool vkCmdEndRenderPass(VkCommandBuffer cmd);

private:
  // Written only by a rejected append/encoder. Every false exit records a
  // fresh cause; successful calls neither reset nor expose this diagnostic.
  PodFailureReason failureReason_ = PodFailureReason::None;
  bool reject(PodFailureReason reason) { failureReason_ = reason; return false; }
  bool directScalars_ = false;
#ifdef WWHD_POD_SCALAR_TEST
  bool failDirectGrowth_ = false;
#endif
  template<class... T> bool append_scalars(uint32_t opcode, const T&... values);
  Limits limits_{};
  size_t commands_ = 0;
  std::vector<std::byte> bytes_;
  bool append(uint32_t opcode, std::span<const std::byte> payload);
};
} // namespace gfxvk
