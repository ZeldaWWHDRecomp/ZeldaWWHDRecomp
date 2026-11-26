#include "pass_cache.h"
#include "gfx/vulkan/draw_thread_locks.h"
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>

namespace gfxvk {
namespace {
template<class T> void mix(size_t& hash, T value) {
    hash ^= std::hash<T>{}(value) + size_t(0x9e3779b9u) + (hash << 6) + (hash >> 2);
}
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}
void validate_formats(const PassFormats& formats) {
    if (formats.colorCount > formats.colors.size() ||
        (formats.stencil && formats.depth == VK_FORMAT_UNDEFINED))
        throw std::invalid_argument("invalid render-pass formats");
    for (size_t i = formats.colorCount; i < formats.colors.size(); ++i)
        if (formats.colors[i] != VK_FORMAT_UNDEFINED)
            throw std::invalid_argument("noncanonical render-pass colour formats");
}
}
size_t RenderPassKeyHash::operator()(const RenderPassKey& key) const {
    size_t hash = 0;
    mix(hash, key.formats.colorCount);
    for (auto format : key.formats.colors) mix(hash, uint32_t(format));
    mix(hash, uint32_t(key.formats.depth)); mix(hash, key.formats.stencil);
    mix(hash, uint32_t(key.colorLoad));
    return hash;
}
size_t FramebufferKeyHash::operator()(const FramebufferKey& key) const {
    size_t hash = 0;
    mix(hash, key.compatiblePass);
    for (auto view : key.views) mix(hash, view);
    mix(hash, key.width); mix(hash, key.height);
    return hash;
}
PassCaches::~PassCaches() { reset(); }
VkRenderPass PassCaches::render_pass(const PassFormats& formats, VkAttachmentLoadOp colorLoad) {
  draw_locks::Guard locked(draw_locks::kSurfaces);  // (draw threads: a shared cache)
    validate_formats(formats);
    if (colorLoad != VK_ATTACHMENT_LOAD_OP_LOAD && colorLoad != VK_ATTACHMENT_LOAD_OP_CLEAR)
        throw std::invalid_argument("unsupported render-pass colour load operation");
    std::lock_guard lock(renderPassMutex_);
    const RenderPassKey key{formats, colorLoad};
    if (auto found = renderPasses_.find(key); found != renderPasses_.end()) return found->second;
    std::array<VkAttachmentDescription, 9> attachments{};
    std::array<VkAttachmentReference, 8> colors{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < formats.colorCount; ++i) {
        colors[i] = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        if (formats.colors[i] == VK_FORMAT_UNDEFINED) continue;
        colors[i].attachment = count;
        auto& attachment = attachments[count++];
        attachment.format = formats.colors[i]; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = colorLoad; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkAttachmentReference depth{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (formats.depth != VK_FORMAT_UNDEFINED) {
        depth.attachment = count;
        auto& attachment = attachments[count++];
        attachment.format = formats.depth; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = formats.stencil ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = formats.stencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = formats.colorCount; subpass.pColorAttachments = colors.data();
    subpass.pDepthStencilAttachment = formats.depth == VK_FORMAT_UNDEFINED ? nullptr : &depth;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = count; info.pAttachments = attachments.data();
    info.subpassCount = 1; info.pSubpasses = &subpass;
    // Explicit barriers outside the pass own all synchronization and layout
    // changes. Initial, subpass and final layouts intentionally agree.
    VkRenderPass pass;
    check(api_.createRenderPass(device_, &info, nullptr, &pass), "create render pass");
    try { renderPasses_.emplace(key, pass); }
    catch (...) { api_.destroyRenderPass(device_, pass, nullptr); throw; }
    return pass;
}
VkFramebuffer PassCaches::framebuffer(const PassFormats& formats, const FramebufferKey& key) {
  draw_locks::Guard locked(draw_locks::kSurfaces);  // (draw threads: a shared cache)
    validate_formats(formats);
    if (!key.compatiblePass || !key.width || !key.height)
        throw std::invalid_argument("invalid framebuffer key");
    if (auto found = framebuffers_.find(key); found != framebuffers_.end()) return found->second;
    std::array<VkImageView, 9> views{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        const bool used = i < formats.colorCount && formats.colors[i] != VK_FORMAT_UNDEFINED;
        if (used != bool(key.views[i])) throw std::invalid_argument("framebuffer colour view/format mismatch");
        if (used) views[count++] = key.views[i];
    }
    if ((formats.depth != VK_FORMAT_UNDEFINED) != bool(key.views[8]))
        throw std::invalid_argument("framebuffer depth view/format mismatch");
    if (key.views[8]) views[count++] = key.views[8];
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = key.compatiblePass; info.attachmentCount = count; info.pAttachments = views.data();
    info.width = key.width; info.height = key.height; info.layers = 1;
    VkFramebuffer framebuffer;
    check(api_.createFramebuffer(device_, &info, nullptr, &framebuffer), "create framebuffer");
    try {
        framebuffers_.emplace(key, framebuffer);
        for (auto view : key.views) if (view) byView_[view].insert(key);
    } catch (...) {
        // Roll back every reverse entry as well as the owning map on allocation
        // failure; no cached handle can survive with a partial reverse index.
        for (auto view : key.views) if (auto found = byView_.find(view); found != byView_.end()) {
            found->second.erase(key);
            if (found->second.empty()) byView_.erase(found);
        }
        framebuffers_.erase(key); api_.destroyFramebuffer(device_, framebuffer, nullptr); throw;
    }
    ++created_;
    return framebuffer;
}
std::vector<VkFramebuffer> PassCaches::forget_views(const VkImageView* views, size_t count) {
  draw_locks::Guard locked(draw_locks::kSurfaces);  // (draw threads: a shared cache)
    std::unordered_set<FramebufferKey, FramebufferKeyHash> keys;
    for (size_t i = 0; i < count; ++i)
        if (auto found = byView_.find(views[i]); found != byView_.end())
            keys.insert(found->second.begin(), found->second.end());
    std::vector<VkFramebuffer> retired;
    retired.reserve(keys.size()); // Allocate before mutating the lookup tables.
    for (const auto& key : keys) {
        auto found = framebuffers_.find(key); assert(found != framebuffers_.end());
        retired.push_back(found->second); framebuffers_.erase(found);
        for (auto view : key.views) if (auto reverse = byView_.find(view); reverse != byView_.end()) {
            reverse->second.erase(key);
            if (reverse->second.empty()) byView_.erase(reverse);
        }
    }
    return retired;
}
void PassCaches::destroy_framebuffers(const std::vector<VkFramebuffer>& framebuffers) {
  draw_locks::Guard locked(draw_locks::kSurfaces);  // (draw threads: a shared cache)
    for (auto framebuffer : framebuffers) api_.destroyFramebuffer(device_, framebuffer, nullptr);
}
void PassCaches::reset() {
  draw_locks::Guard locked(draw_locks::kSurfaces);  // (draw threads: a shared cache)
    for (const auto& [key, framebuffer] : framebuffers_)
        api_.destroyFramebuffer(device_, framebuffer, nullptr);
    framebuffers_.clear(); byView_.clear();
    std::lock_guard lock(renderPassMutex_);
    for (const auto& [key, pass] : renderPasses_) api_.destroyRenderPass(device_, pass, nullptr);
    renderPasses_.clear();
}
} // namespace gfxvk
