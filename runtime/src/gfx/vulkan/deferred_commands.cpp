#include "deferred_commands.h"
#include "record_worker.h"
#include <array>
#include <cstdint>
#include <optional>
#include <limits>

namespace gfxvk {
namespace {
template<class T> std::vector<T> own(uint32_t count,const T* values) {
    if(!count)return {};
    return {values,values+count};
}
template<class T> const T* data(const std::vector<T>& values) { return values.empty()?nullptr:values.data(); }
template<class T> bool chains(uint32_t count,const T* values) {
    if(count&&!values)return false;
    for(uint32_t i=0;i<count;++i)if(values[i].pNext)return false;
    return true;
}
// Vulkan counts are uint32; account additions without size_t overflow on 32-bit hosts.
size_t bytes(uint32_t count,size_t element) {
    if(count>std::numeric_limits<size_t>::max()/element)return std::numeric_limits<size_t>::max();
    return size_t(count)*element;
}
size_t sum(size_t a,size_t b) { return b>std::numeric_limits<size_t>::max()-a?std::numeric_limits<size_t>::max():a+b; }
struct Rendering {
    VkRenderingInfo info{};
    std::vector<VkRenderingAttachmentInfo> colors;
    std::optional<VkRenderingAttachmentInfo> depth,stencil;
    explicit Rendering(const VkRenderingInfo& value):info(value),colors(own(value.colorAttachmentCount,value.pColorAttachments)) {
        if(value.pDepthAttachment)depth=*value.pDepthAttachment;
        if(value.pStencilAttachment)stencil=*value.pStencilAttachment;
        info.pColorAttachments=nullptr;info.pDepthAttachment=nullptr;info.pStencilAttachment=nullptr;
    }
    void emit(VkCommandBuffer cmd) const {
        auto value=info;value.pColorAttachments=data(colors);
        value.pDepthAttachment=depth?&*depth:nullptr;value.pStencilAttachment=stencil?&*stencil:nullptr;
        gfxvk::vkCmdBeginRendering(cmd,&value);
    }
};
}
void DeferredCommands::set_pod_commands(PodCommands* commands) {
    if (capturing_ || !commands_.empty()) throw std::logic_error("cannot switch an active deferred tail");
    if (active_deferred_commands && active_deferred_commands != this)
        throw std::logic_error("another command recorder is active");
    pod_ = commands;
    active_deferred_commands = commands ? this : nullptr;
}
void DeferredCommands::start_capture(std::function<void()> beforeExact) {
    if (pod_) throw std::logic_error("POD worker does not retain lambda capture");
    if(active_deferred_commands && active_deferred_commands!=this)throw std::logic_error("another deferred command recorder is active");
    if(capturing_ || !commands_.empty())throw std::logic_error("deferred command tail already active");
    beforeExact_=std::move(beforeExact);capturing_=true;active_deferred_commands=this;
}
void DeferredCommands::force_exact() {
    if (pod_) { drain_record_commands(); return; }
    capturing_=false;
    if(active_deferred_commands==this)active_deferred_commands=nullptr;
    // A throwing callback leaves the owned tail intact for explicit recovery.
    auto before=std::move(beforeExact_);
    if(before)try { before(); } catch(...) { beforeExact_=std::move(before);throw; }
    emit_exact();
}
void DeferredCommands::emit_exact() {
    if (pod_) { drain_record_commands(); return; }
    capturing_=false;
    if(active_deferred_commands==this)active_deferred_commands=nullptr;
    while(emitted_<commands_.size()) { commands_[emitted_]();++emitted_; }
    commands_.clear();payloadBytes_=0;emitted_=0;beforeExact_={};
}
void DeferredCommands::reset() {
    if (pod_) { drain_record_commands(); return; }
    if(!commands_.empty())throw std::logic_error("cannot discard deferred commands");
    capturing_=false;
    if(active_deferred_commands==this)active_deferred_commands=nullptr;
    payloadBytes_=0;beforeExact_={};
}
DeferredCommands& deferred_commands() { static DeferredCommands value;return value; }
void DeferredCommands::vkCmdBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipeline pipeline) {
    if(!pipeline)throw std::invalid_argument("cannot bind a null Vulkan pipeline");
    if (pod_) {
        if (!pod_->vkCmdBindPipeline(cmd, point, pipeline)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 1, pod_->failure_reason());
            gfxvk::vkCmdBindPipeline(cmd, point, pipeline);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdBindPipeline(cmd, point, pipeline); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
    if (pod_) {
        if (!pod_->vkCmdBindIndexBuffer(cmd, buffer, offset, type)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 2, pod_->failure_reason());
            gfxvk::vkCmdBindIndexBuffer(cmd, buffer, offset, type);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdBindIndexBuffer(cmd, buffer, offset, type); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdDraw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) {
    if (pod_) {
        if (!pod_->vkCmdDraw(cmd, vertices, instances, firstVertex, firstInstance)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 3, pod_->failure_reason());
            gfxvk::vkCmdDraw(cmd, vertices, instances, firstVertex, firstInstance);
        } else publish_record_commands_at_draw_boundary();
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdDraw(cmd, vertices, instances, firstVertex, firstInstance); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdDrawIndexed(VkCommandBuffer cmd, uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (pod_) {
        if (!pod_->vkCmdDrawIndexed(cmd, indices, instances, firstIndex, vertexOffset, firstInstance)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 4, pod_->failure_reason());
            gfxvk::vkCmdDrawIndexed(cmd, indices, instances, firstIndex, vertexOffset, firstInstance);
        } else publish_record_commands_at_draw_boundary();
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdDrawIndexed(cmd, indices, instances, firstIndex, vertexOffset, firstInstance); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) {
    if (pod_) {
        if (!pod_->vkCmdDispatch(cmd, x, y, z)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 5, pod_->failure_reason());
            gfxvk::vkCmdDispatch(cmd, x, y, z);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdDispatch(cmd, x, y, z); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdEndRendering(VkCommandBuffer cmd) {
    if (pod_) {
        if (!pod_->vkCmdEndRendering(cmd)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 6, pod_->failure_reason());
            gfxvk::vkCmdEndRendering(cmd);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdEndRendering(cmd); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdSetStencilReference(VkCommandBuffer cmd, VkStencilFaceFlags faces, uint32_t reference) {
    if (pod_) {
        if (!pod_->vkCmdSetStencilReference(cmd, faces, reference)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 7, pod_->failure_reason());
            gfxvk::vkCmdSetStencilReference(cmd, faces, reference);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdSetStencilReference(cmd, faces, reference); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdSetStencilWriteMask(VkCommandBuffer cmd, VkStencilFaceFlags faces, uint32_t mask) {
    if (pod_) {
        if (!pod_->vkCmdSetStencilWriteMask(cmd, faces, mask)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 8, pod_->failure_reason());
            gfxvk::vkCmdSetStencilWriteMask(cmd, faces, mask);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdSetStencilWriteMask(cmd, faces, mask); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdResetQueryPool(VkCommandBuffer cmd, VkQueryPool pool, uint32_t first, uint32_t count) {
    if (pod_) {
        if (!pod_->vkCmdResetQueryPool(cmd, pool, first, count)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 9, pod_->failure_reason());
            gfxvk::vkCmdResetQueryPool(cmd, pool, first, count);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdResetQueryPool(cmd, pool, first, count); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdWriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage, VkQueryPool pool, uint32_t query) {
    if (pod_) {
        if (!pod_->vkCmdWriteTimestamp(cmd, stage, pool, query)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 10, pod_->failure_reason());
            gfxvk::vkCmdWriteTimestamp(cmd, stage, pool, query);
        }
        return;
    }
    auto immediate=[=] { gfxvk::vkCmdWriteTimestamp(cmd, stage, pool, query); };
    record(0,true,[=] { return immediate; },immediate);
}
void DeferredCommands::vkCmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkViewport* values) {
    if (pod_) {
        if (!pod_->vkCmdSetViewport(cmd, first, count, values)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 11, pod_->failure_reason());
            gfxvk::vkCmdSetViewport(cmd, first, count, values);
        }
        return;
    }
    record(bytes(count,sizeof(VkViewport)),!count||values,[=] {
        return [=,copy=own(count,values)] { gfxvk::vkCmdSetViewport(cmd, first, count, data(copy)); };
    },[=] { gfxvk::vkCmdSetViewport(cmd, first, count, values); });
}
void DeferredCommands::vkCmdSetScissor(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkRect2D* values) {
    if (pod_) {
        if (!pod_->vkCmdSetScissor(cmd, first, count, values)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 12, pod_->failure_reason());
            gfxvk::vkCmdSetScissor(cmd, first, count, values);
        }
        return;
    }
    record(bytes(count,sizeof(VkRect2D)),!count||values,[=] {
        return [=,copy=own(count,values)] { gfxvk::vkCmdSetScissor(cmd, first, count, data(copy)); };
    },[=] { gfxvk::vkCmdSetScissor(cmd, first, count, values); });
}
void DeferredCommands::vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer source, VkBuffer destination, uint32_t count, const VkBufferCopy* regions) {
    if (pod_) {
        if (!pod_->vkCmdCopyBuffer(cmd, source, destination, count, regions)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 13, pod_->failure_reason());
            gfxvk::vkCmdCopyBuffer(cmd, source, destination, count, regions);
        }
        return;
    }
    record(bytes(count,sizeof(VkBufferCopy)),!count||regions,[=] {
        return [=,copy=own(count,regions)] { gfxvk::vkCmdCopyBuffer(cmd, source, destination, count, data(copy)); };
    },[=] { gfxvk::vkCmdCopyBuffer(cmd, source, destination, count, regions); });
}
void DeferredCommands::vkCmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer source, VkImage destination, VkImageLayout layout, uint32_t count, const VkBufferImageCopy* regions) {
    if (pod_) {
        if (!pod_->vkCmdCopyBufferToImage(cmd, source, destination, layout, count, regions)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 14, pod_->failure_reason());
            gfxvk::vkCmdCopyBufferToImage(cmd, source, destination, layout, count, regions);
        }
        return;
    }
    record(bytes(count,sizeof(VkBufferImageCopy)),!count||regions,[=] {
        return [=,copy=own(count,regions)] { gfxvk::vkCmdCopyBufferToImage(cmd, source, destination, layout, count, data(copy)); };
    },[=] { gfxvk::vkCmdCopyBufferToImage(cmd, source, destination, layout, count, regions); });
}
void DeferredCommands::vkCmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage source, VkImageLayout layout, VkBuffer destination, uint32_t count, const VkBufferImageCopy* regions) {
    if (pod_) {
        if (!pod_->vkCmdCopyImageToBuffer(cmd, source, layout, destination, count, regions)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 15, pod_->failure_reason());
            gfxvk::vkCmdCopyImageToBuffer(cmd, source, layout, destination, count, regions);
        }
        return;
    }
    record(bytes(count,sizeof(VkBufferImageCopy)),!count||regions,[=] {
        return [=,copy=own(count,regions)] { gfxvk::vkCmdCopyImageToBuffer(cmd, source, layout, destination, count, data(copy)); };
    },[=] { gfxvk::vkCmdCopyImageToBuffer(cmd, source, layout, destination, count, regions); });
}
void DeferredCommands::vkCmdCopyImage(VkCommandBuffer cmd, VkImage source, VkImageLayout sourceLayout, VkImage destination, VkImageLayout destinationLayout, uint32_t count, const VkImageCopy* regions) {
    if (pod_) {
        if (!pod_->vkCmdCopyImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 16, pod_->failure_reason());
            gfxvk::vkCmdCopyImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions);
        }
        return;
    }
    record(bytes(count,sizeof(VkImageCopy)),!count||regions,[=] {
        return [=,copy=own(count,regions)] { gfxvk::vkCmdCopyImage(cmd, source, sourceLayout, destination, destinationLayout, count, data(copy)); };
    },[=] { gfxvk::vkCmdCopyImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions); });
}
void DeferredCommands::vkCmdBlitImage(VkCommandBuffer cmd, VkImage source, VkImageLayout sourceLayout, VkImage destination, VkImageLayout destinationLayout, uint32_t count, const VkImageBlit* regions, VkFilter filter) {
    if (pod_) {
        if (!pod_->vkCmdBlitImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions, filter)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 17, pod_->failure_reason());
            gfxvk::vkCmdBlitImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions, filter);
        }
        return;
    }
    record(bytes(count,sizeof(VkImageBlit)),!count||regions,[=] {
        return [=,copy=own(count,regions)] { gfxvk::vkCmdBlitImage(cmd, source, sourceLayout, destination, destinationLayout, count, data(copy), filter); };
    },[=] { gfxvk::vkCmdBlitImage(cmd, source, sourceLayout, destination, destinationLayout, count, regions, filter); });
}
void DeferredCommands::vkCmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer* buffers, const VkDeviceSize* offsets) {
    if (pod_) {
        if (!pod_->vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 18, pod_->failure_reason());
            gfxvk::vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets);
        }
        return;
    }
    record(bytes(count,sizeof(VkBuffer)+sizeof(VkDeviceSize)),!count||(buffers&&offsets),[=] {
        return [=,b=own(count,buffers),o=own(count,offsets)] { gfxvk::vkCmdBindVertexBuffers(cmd,first,count,data(b),data(o)); };
    },[=] { gfxvk::vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets); });
}
void DeferredCommands::vkCmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet* sets, uint32_t offsetCount, const uint32_t* offsets) {
    if (pod_) {
        if (!pod_->vkCmdBindDescriptorSets(cmd, point, layout, first, count, sets, offsetCount, offsets)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 19, pod_->failure_reason());
            gfxvk::vkCmdBindDescriptorSets(cmd, point, layout, first, count, sets, offsetCount, offsets);
        }
        return;
    }
    record(sum(bytes(count,sizeof(VkDescriptorSet)),bytes(offsetCount,sizeof(uint32_t))),(!count||sets)&&(!offsetCount||offsets),[=] {
        return [=,s=own(count,sets),o=own(offsetCount,offsets)] { gfxvk::vkCmdBindDescriptorSets(cmd,point,layout,first,count,data(s),offsetCount,data(o)); };
    },[=] { gfxvk::vkCmdBindDescriptorSets(cmd, point, layout, first, count, sets, offsetCount, offsets); });
}
void DeferredCommands::vkCmdPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* values) {
    if (pod_) {
        if (!pod_->vkCmdPushConstants(cmd, layout, stages, offset, size, values)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 20, pod_->failure_reason());
            gfxvk::vkCmdPushConstants(cmd, layout, stages, offset, size, values);
        }
        return;
    }
    record(size,!size||values,[=] {
        return [=,v=own(size,static_cast<const uint8_t*>(values))] { gfxvk::vkCmdPushConstants(cmd,layout,stages,offset,size,data(v)); };
    },[=] { gfxvk::vkCmdPushConstants(cmd, layout, stages, offset, size, values); });
}
void DeferredCommands::vkCmdSetBlendConstants(VkCommandBuffer cmd, const float values[4]) {
    if (pod_) {
        if (!pod_->vkCmdSetBlendConstants(cmd, values)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 21, pod_->failure_reason());
            gfxvk::vkCmdSetBlendConstants(cmd, values);
        }
        return;
    }
    record(sizeof(float)*4,values,[=] {
        const std::array<float,4> copy{values[0],values[1],values[2],values[3]}; return [=] { gfxvk::vkCmdSetBlendConstants(cmd,copy.data()); };
    },[=] { gfxvk::vkCmdSetBlendConstants(cmd, values); });
}
void DeferredCommands::vkCmdClearColorImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, const VkClearColorValue* value, uint32_t count, const VkImageSubresourceRange* ranges) {
    if (pod_) {
        if (!pod_->vkCmdClearColorImage(cmd, image, layout, value, count, ranges)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 22, pod_->failure_reason());
            gfxvk::vkCmdClearColorImage(cmd, image, layout, value, count, ranges);
        }
        return;
    }
    record(sum(sizeof(VkClearColorValue),bytes(count,sizeof(VkImageSubresourceRange))),value&&(!count||ranges),[=] {
        const auto copy=*value;return [=,r=own(count,ranges)] { gfxvk::vkCmdClearColorImage(cmd,image,layout,&copy,count,data(r)); };
    },[=] { gfxvk::vkCmdClearColorImage(cmd, image, layout, value, count, ranges); });
}
void DeferredCommands::vkCmdClearDepthStencilImage(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, const VkClearDepthStencilValue* value, uint32_t count, const VkImageSubresourceRange* ranges) {
    if (pod_) {
        if (!pod_->vkCmdClearDepthStencilImage(cmd, image, layout, value, count, ranges)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 23, pod_->failure_reason());
            gfxvk::vkCmdClearDepthStencilImage(cmd, image, layout, value, count, ranges);
        }
        return;
    }
    record(sum(sizeof(VkClearDepthStencilValue),bytes(count,sizeof(VkImageSubresourceRange))),value&&(!count||ranges),[=] {
        const auto copy=*value;return [=,r=own(count,ranges)] { gfxvk::vkCmdClearDepthStencilImage(cmd,image,layout,&copy,count,data(r)); };
    },[=] { gfxvk::vkCmdClearDepthStencilImage(cmd, image, layout, value, count, ranges); });
}
void DeferredCommands::vkCmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info) {
    if (pod_) {
        if (!pod_->vkCmdBeginRendering(cmd, info)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 24, pod_->failure_reason());
            gfxvk::vkCmdBeginRendering(cmd, info);
        }
        return;
    }
    record(info?sum(sizeof(VkRenderingInfo)+2*sizeof(VkRenderingAttachmentInfo),bytes(info->colorAttachmentCount,sizeof(VkRenderingAttachmentInfo))):0,info&&!info->pNext&&chains(info->colorAttachmentCount,info->pColorAttachments)&&(!info->pDepthAttachment||!info->pDepthAttachment->pNext)&&(!info->pStencilAttachment||!info->pStencilAttachment->pNext),[=] {
        return [cmd,copy=Rendering(*info)] { copy.emit(cmd); };
    },[=] { gfxvk::vkCmdBeginRendering(cmd, info); });
}
void DeferredCommands::vkCmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags, uint32_t memoryCount, const VkMemoryBarrier* memory, uint32_t bufferCount, const VkBufferMemoryBarrier* buffers, uint32_t imageCount, const VkImageMemoryBarrier* images) {
    if (pod_) {
        if (!pod_->vkCmdPipelineBarrier(cmd, source, destination, flags, memoryCount, memory, bufferCount, buffers, imageCount, images)) {
            drain_record_commands(fallback_stats::Boundary::EncoderFallback, 25, pod_->failure_reason());
            gfxvk::vkCmdPipelineBarrier(cmd, source, destination, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
        }
        return;
    }
    record(sum(sum(bytes(memoryCount,sizeof(VkMemoryBarrier)),bytes(bufferCount,sizeof(VkBufferMemoryBarrier))),bytes(imageCount,sizeof(VkImageMemoryBarrier))),chains(memoryCount,memory)&&chains(bufferCount,buffers)&&chains(imageCount,images),[=] {
        return [=,m=own(memoryCount,memory),b=own(bufferCount,buffers),i=own(imageCount,images)] { gfxvk::vkCmdPipelineBarrier(cmd,source,destination,flags,memoryCount,data(m),bufferCount,data(b),imageCount,data(i)); };
    },[=] { gfxvk::vkCmdPipelineBarrier(cmd, source, destination, flags, memoryCount, memory, bufferCount, buffers, imageCount, images); });
}

} // namespace gfxvk

namespace gfxvk {
void DeferredCommands::vkCmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
    if (pod_) {
        if (pod_->vkCmdBeginRenderPass(cmd, info, contents)) return;
        drain_record_commands(fallback_stats::Boundary::EncoderFallback, 26, pod_->failure_reason());
    } else finish_exact();
    gfxvk::vkCmdBeginRenderPass(cmd, info, contents);
}
void DeferredCommands::vkCmdEndRenderPass(VkCommandBuffer cmd) {
    if (pod_) {
        if (pod_->vkCmdEndRenderPass(cmd)) return;
        drain_record_commands(fallback_stats::Boundary::EncoderFallback, 27, pod_->failure_reason());
    } else finish_exact();
    gfxvk::vkCmdEndRenderPass(cmd);
}
}
