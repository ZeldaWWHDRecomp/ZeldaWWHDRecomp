#pragma once
#include "deferred_commands.h"

// Immediate recording takes only a renderer-global null-pointer branch before the raw API.
// Capture is explicitly registered by DeferredCommands::start_capture().
namespace gfxvk::record {
template<class Action> inline void emit_owned(Action action) {
    if(auto* recorder=dispatch_recorder())recorder->emit_owned(std::move(action));
    else action();
}
inline void vkCmdBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipeline pipeline) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBindPipeline(cmd, point, pipeline);
    else gfxvk::vkCmdBindPipeline(cmd, point, pipeline);
}
inline void vkCmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBindIndexBuffer(cmd, buffer, offset, type);
    else gfxvk::vkCmdBindIndexBuffer(cmd, buffer, offset, type);
}
inline void vkCmdDraw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdDraw(cmd, vertices, instances, firstVertex, firstInstance);
    else gfxvk::vkCmdDraw(cmd, vertices, instances, firstVertex, firstInstance);
}
inline void vkCmdDrawIndexed(VkCommandBuffer cmd, uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdDrawIndexed(cmd, indices, instances, firstIndex, vertexOffset, firstInstance);
    else gfxvk::vkCmdDrawIndexed(cmd, indices, instances, firstIndex, vertexOffset, firstInstance);
}
inline void vkCmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdDispatch(cmd, x, y, z);
    else gfxvk::vkCmdDispatch(cmd, x, y, z);
}
inline void vkCmdEndRendering(VkCommandBuffer cmd) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdEndRendering(cmd);
    else gfxvk::vkCmdEndRendering(cmd);
}
inline void vkCmdSetStencilReference(VkCommandBuffer cmd, VkStencilFaceFlags faces, uint32_t reference) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdSetStencilReference(cmd, faces, reference);
    else gfxvk::vkCmdSetStencilReference(cmd, faces, reference);
}
inline void vkCmdSetStencilWriteMask(VkCommandBuffer cmd, VkStencilFaceFlags faces, uint32_t mask) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdSetStencilWriteMask(cmd, faces, mask);
    else gfxvk::vkCmdSetStencilWriteMask(cmd, faces, mask);
}
inline void vkCmdResetQueryPool(VkCommandBuffer cmd, VkQueryPool pool, uint32_t first, uint32_t count) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdResetQueryPool(cmd, pool, first, count);
    else gfxvk::vkCmdResetQueryPool(cmd, pool, first, count);
}
inline void vkCmdWriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage, VkQueryPool pool, uint32_t query) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdWriteTimestamp(cmd, stage, pool, query);
    else gfxvk::vkCmdWriteTimestamp(cmd, stage, pool, query);
}
inline void vkCmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkViewport* values) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdSetViewport(cmd, first, count, values);
    else gfxvk::vkCmdSetViewport(cmd, first, count, values);
}
inline void vkCmdSetScissor(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkRect2D* values) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdSetScissor(cmd, first, count, values);
    else gfxvk::vkCmdSetScissor(cmd, first, count, values);
}
inline void vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer source, VkBuffer destination, uint32_t count, const VkBufferCopy* regions) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdCopyBuffer(cmd, source, destination, count, regions);
    else gfxvk::vkCmdCopyBuffer(cmd, source, destination, count, regions);
}
inline void vkCmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer source, VkImage destination, VkImageLayout layout, uint32_t count, const VkBufferImageCopy* regions) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdCopyBufferToImage(cmd, source, destination, layout, count, regions);
    else gfxvk::vkCmdCopyBufferToImage(cmd, source, destination, layout, count, regions);
}
inline void vkCmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage source, VkImageLayout layout, VkBuffer destination, uint32_t count, const VkBufferImageCopy* regions) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdCopyImageToBuffer(cmd, source, layout, destination, count, regions);
    else gfxvk::vkCmdCopyImageToBuffer(cmd, source, layout, destination, count, regions);
}
inline void vkCmdCopyImage(VkCommandBuffer cmd, VkImage source, VkImageLayout sourceLayout, VkImage destination, VkImageLayout destinationLayout, uint32_t count, const VkImageCopy* regions) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdCopyImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions);
    else gfxvk::vkCmdCopyImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions);
}
inline void vkCmdBlitImage(VkCommandBuffer cmd, VkImage source, VkImageLayout sourceLayout, VkImage destination, VkImageLayout destinationLayout, uint32_t count, const VkImageBlit* regions, VkFilter filter) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBlitImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions, filter);
    else gfxvk::vkCmdBlitImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions, filter);
}
inline void vkCmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer* buffers, const VkDeviceSize* offsets) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets);
    else gfxvk::vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets);
}
inline void vkCmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet* sets, uint32_t offsetCount, const uint32_t* offsets) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBindDescriptorSets(cmd, point, layout, first, count, sets, offsetCount, offsets);
    else gfxvk::vkCmdBindDescriptorSets(cmd, point, layout, first, count, sets, offsetCount, offsets);
}
inline void vkCmdPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* values) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdPushConstants(cmd, layout, stages, offset, size, values);
    else gfxvk::vkCmdPushConstants(cmd, layout, stages, offset, size, values);
}
inline void vkCmdSetBlendConstants(VkCommandBuffer cmd, const float values[4]) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdSetBlendConstants(cmd, values);
    else gfxvk::vkCmdSetBlendConstants(cmd, values);
}
inline void vkCmdClearColorImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, const VkClearColorValue* value, uint32_t count, const VkImageSubresourceRange* ranges) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdClearColorImage(cmd, image, layout, value, count, ranges);
    else gfxvk::vkCmdClearColorImage(cmd, image, layout, value, count, ranges);
}
inline void vkCmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, const VkClearDepthStencilValue* value, uint32_t count, const VkImageSubresourceRange* ranges) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdClearDepthStencilImage(cmd, image, layout, value, count, ranges);
    else gfxvk::vkCmdClearDepthStencilImage(cmd, image, layout, value, count, ranges);
}
inline void vkCmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBeginRendering(cmd, info);
    else gfxvk::vkCmdBeginRendering(cmd, info);
}
inline void vkCmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags, uint32_t memoryCount, const VkMemoryBarrier* memory, uint32_t bufferCount, const VkBufferMemoryBarrier* buffers, uint32_t imageCount, const VkImageMemoryBarrier* images) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdPipelineBarrier(cmd, source, destination, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
    else gfxvk::vkCmdPipelineBarrier(cmd, source, destination, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
}
inline void vkCmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdBeginRenderPass(cmd, info, contents);
    else gfxvk::vkCmdBeginRenderPass(cmd, info, contents);
}
inline void vkCmdEndRenderPass(VkCommandBuffer cmd) {
    if(auto* recorder=dispatch_recorder())recorder->vkCmdEndRenderPass(cmd);
    else gfxvk::vkCmdEndRenderPass(cmd);
}
} // namespace gfxvk::record
