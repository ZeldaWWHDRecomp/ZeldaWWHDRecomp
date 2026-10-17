#include "pass.h"
#include "pipeline_worker.h"
#include "pipeline_demand.h"
#include <cassert>

namespace gfxvk {
// Explicit lifetime, like Renderer::device. A static destructor could enter a
// validation layer after dyld has already finalized that layer's mutexes.
namespace { PassCaches* caches = nullptr; }
void init_pass_caches() {
    if (R.renderPasses) caches = new PassCaches(R.device, PassApi{
        vkCreateRenderPass, vkDestroyRenderPass, vkCreateFramebuffer, vkDestroyFramebuffer,
        vkCmdBeginRenderPass, vkCmdEndRenderPass});
}
PassCaches& pass_caches() {
    if (!caches) throw std::logic_error("render-pass cache is not initialized");
    return *caches;
}
PreparedPass prepare_pass(PassCaches& cache, const VkRenderingInfo& info, const PassBegin& pass) {
    assert(info.layerCount == 1 && !info.viewMask && !info.flags && !info.pNext);
    assert(info.colorAttachmentCount == pass.formats.colorCount);
    PreparedPass prepared;
    FramebufferKey key;
    key.width = info.renderArea.offset.x + info.renderArea.extent.width;
    key.height = info.renderArea.offset.y + info.renderArea.extent.height;
    VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_LOAD;
    bool haveLoad = false;
    uint32_t clearCount = 0;
    for (uint32_t i = 0; i < info.colorAttachmentCount; ++i) {
        const auto& attachment = info.pColorAttachments[i];
        key.views[i] = attachment.imageView;
        assert(bool(attachment.imageView) == (pass.formats.colors[i] != VK_FORMAT_UNDEFINED));
        if (!attachment.imageView) continue;
        assert(attachment.imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        assert(pass.layouts[i] == attachment.imageLayout);
        assert(attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE && !attachment.resolveImageView && !attachment.pNext);
        if (haveLoad && load != attachment.loadOp) throw std::invalid_argument("mixed pass colour load operations");
        load = attachment.loadOp; haveLoad = true;
        prepared.clears[clearCount++] = attachment.clearValue;
    }
    if (info.pDepthAttachment) {
        const auto& attachment = *info.pDepthAttachment;
        key.views[8] = attachment.imageView;
        assert(attachment.imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        assert(pass.layouts[8] == attachment.imageLayout);
        // The newer drawn depth-copy path completely overwrites its render
        // area and requests DONT_CARE. LOAD is a valid stronger operation there;
        // keep a single compatible depth variant and preserve the outside area.
        assert((attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD || attachment.loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE) &&
               attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
        assert(!attachment.resolveImageView && !attachment.pNext);
    }
    assert(bool(info.pDepthAttachment) == (pass.formats.depth != VK_FORMAT_UNDEFINED));
    assert(bool(info.pStencilAttachment) == pass.formats.stencil);
    assert(!info.pStencilAttachment || info.pStencilAttachment->imageView == key.views[8]);
    key.compatiblePass = cache.render_pass(pass.formats, VK_ATTACHMENT_LOAD_OP_LOAD);
    prepared.info.renderPass = load == VK_ATTACHMENT_LOAD_OP_LOAD ? key.compatiblePass : cache.render_pass(pass.formats, load);
    prepared.info.framebuffer = cache.framebuffer(pass.formats, key);
    prepared.info.renderArea = info.renderArea;
    prepared.info.clearValueCount = load == VK_ATTACHMENT_LOAD_OP_CLEAR ? clearCount : 0;
    return prepared;
}
void begin_render_pass(VkCommandBuffer cmd, const VkRenderingInfo& info, const PassBegin& pass) {
    // Resolve handles NOW, before deferred recording can seal this submission or
    // defer a view. The emitted command owns values only, never a cache lookup.
    auto prepared = prepare_pass(pass_caches(), info, pass);
    if (auto* recorder = dispatch_recorder(); recorder && recorder->pod_mode()) {
        auto info = prepared.info; info.pClearValues = prepared.clears.data();
        record::vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    } else record::emit_owned([cmd, prepared] { prepared.begin(cmd, pass_caches().api()); });
}
void end_render_pass(VkCommandBuffer cmd) {
    if (auto* recorder = dispatch_recorder(); recorder && recorder->pod_mode())
        record::vkCmdEndRenderPass(cmd);
    else record::emit_owned([cmd] { vkCmdEndRenderPass(cmd); });
}
void render_pass_pipeline_targets(VkGraphicsPipelineCreateInfo& info, const VkPipelineRenderingCreateInfo& targets) {
    info.pNext = nullptr; info.subpass = 0;
    info.renderPass = pass_caches().render_pass(pass_formats(targets), VK_ATTACHMENT_LOAD_OP_LOAD);
}
void forget_framebuffers(const VkImageView* views, size_t count, bool deferred) {
    if (!caches) return;
    if (deferred) R.garbageFramebuffers.reserve(R.garbageFramebuffers.size() + caches->framebuffer_count());
    auto retired = caches->forget_views(views, count);
    if (deferred) R.garbageFramebuffers.insert(R.garbageFramebuffers.end(), retired.begin(), retired.end());
    else caches->destroy_framebuffers(retired);
}
void reset_pass_caches() {
    if (!caches) return;
    // Persistence deliberately catches errors. Even if an earlier pipeline
    // drain/save failed, no compiler may still borrow these render-pass handles
    // when they are destroyed. Both stops are idempotent after a normal save.
    save_pipeline_recipes();
    finish_pipeline_demand(VK_NULL_HANDLE);
    delete caches; caches = nullptr;
}
} // namespace gfxvk
