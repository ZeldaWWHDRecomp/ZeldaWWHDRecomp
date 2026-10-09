// The settings overlay's Dear ImGui draw data, drawn by the Vulkan renderer into the TV window's
// composition (present.cpp compose()). The renderer's own command buffer, upload arena, descriptor
// pool and pipeline cache are used, so the overlay needs no queue submissions of its own and works
// for every target format (swap images in any encoding, offscreen present dumps).
#include "../../exception_report.h"
#include "backend.h"
#include "present.h"
#include "shaders.h"
#include "imgui.h"
#include "overlay/guest_hud.h"
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace gfxvk {
namespace {

const char* kVertex = R"glsl(#version 450
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 col;
layout(push_constant) uniform Params { vec2 scale; vec2 translate; int linear; int linear_texture; } p;
layout(location=0) out vec4 color;
layout(location=1) out vec2 tc;
void main() {
 color=col;
 // sRGB targets: the UI's colours are display-encoded, the target encodes linear values
 if(p.linear!=0)color.rgb=mix(pow((color.rgb+0.055)/1.055,vec3(2.4)),color.rgb/12.92,lessThanEqual(color.rgb,vec3(0.04045)));
 tc=uv;
 gl_Position=vec4(pos*p.scale+p.translate,0.0,1.0);
}
)glsl";
const char* kFragment = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(location=0) in vec4 color;
layout(location=1) in vec2 tc;
layout(location=0) out vec4 result;
layout(push_constant) uniform Params { vec2 scale; vec2 translate; int linear; int linear_texture; } p;
void main() {
 vec4 sampleColor=texture(image,tc);
 if(p.linear_texture!=0)sampleColor.rgb=mix(pow((sampleColor.rgb+0.055)/1.055,vec3(2.4)),sampleColor.rgb/12.92,lessThanEqual(sampleColor.rgb,vec3(0.04045)));
 result=color*sampleColor;
}
)glsl";

struct Params { float scale[2], translate[2]; int32_t linear, linear_texture, pad[2]; };

struct Texture {
    ResourceUse use;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    int width = 0, height = 0;
};

struct Resources {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::unordered_map<int, VkPipeline> pipelines;  // by target format
};
Resources res;

VkShaderModule module(const char* source, bool vertex) {
    std::string error;
    auto words = vk::compile_glsl(source, vertex, &error);
    if (words.empty() || !error.empty()) exception_report::raise("Vulkan overlay shader: " + error);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule m;
    vk_check(vkCreateShaderModule(R.device, &ci, nullptr, &m), "overlay shader");
    return m;
}

void ensure_resources() {
    if (res.device && res.device != R.device) exception_report::raise("Vulkan overlay resources outlived their device");
    res.device = R.device;
    if (!res.descriptors) {
        VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 1;
        ci.pBindings = &b;
        vk_check(vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &res.descriptors), "overlay descriptors");
    }
    if (!res.layout) {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Params)};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &res.descriptors;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &push;
        vk_check(vkCreatePipelineLayout(R.device, &ci, nullptr, &res.layout), "overlay layout");
    }
    if (!res.sampler) {
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        ci.minFilter = ci.magFilter = VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU = ci.addressModeV = ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.maxLod = 0;
        vk_check(vkCreateSampler(R.device, &ci, nullptr, &res.sampler), "overlay sampler");
    }
}

VkPipeline pipeline(VkFormat format,bool additive=false) {
    int key=int(format)*2+int(additive);
    ensure_resources();
    if (auto it = res.pipelines.find(key); it != res.pipelines.end()) return it->second;
    VkShaderModule vs = module(kVertex, true), fs = VK_NULL_HANDLE;
    VkPipeline result = VK_NULL_HANDLE;
    try {
        fs = module(kFragment, false);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i = 0; i < 2; i++) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = i ? fs : vs;
            stages[i].pName = "main";
        }
        VkVertexInputBindingDescription binding{0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[3] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(ImDrawVert, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(ImDrawVert, uv)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(ImDrawVert, col)}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 15;
        blend.blendEnable = VK_TRUE;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = additive?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &blend;
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.pNext = &rendering;
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pColorBlendState = &bs;
        ci.pDynamicState = &ds;
        ci.layout = res.layout;
        vk_check(vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &result), "overlay pipeline");
    } catch (...) {
        vkDestroyShaderModule(R.device, vs, nullptr);
        if (fs) vkDestroyShaderModule(R.device, fs, nullptr);
        throw;
    }
    vkDestroyShaderModule(R.device, vs, nullptr);
    vkDestroyShaderModule(R.device, fs, nullptr);
    res.pipelines.emplace(key, result);
    return result;
}

void barrier(VkCommandBuffer cmd, Texture& t, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags from_stage,
             VkPipelineStageFlags to_stage) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = t.layout;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto dependency = derive_dependency(t.use, to_stage, dst, t.layout != to);
    b.srcAccessMask = narrow_barriers() ? dependency.sourceAccess : src;
    b.dstAccessMask = dst;
    vkCmdPipelineBarrier(cmd, narrow_barriers() ? dependency.source : from_stage, to_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    t.layout = to;
}

void destroy(Texture* t) {
    if (!t) return;
    if (t->image) defer_surface_image(t->image, t->memory, t->view ? std::vector<VkImageView>{t->view} : std::vector<VkImageView>{});
    delete t;
}

// ImGui 1.92 texture protocol: create, update (whole texture) and destroy font atlas pages
void update_texture(VkCommandBuffer cmd, ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        destroy((Texture*)(uintptr_t)tex->GetTexID());
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
        return;
    }
    if (tex->Status != ImTextureStatus_WantCreate && tex->Status != ImTextureStatus_WantUpdates) return;
    if (tex->Format != ImTextureFormat_RGBA32) exception_report::raise("Vulkan overlay: unexpected texture format");
    Texture* t = (Texture*)(uintptr_t)tex->GetTexID();
    if (tex->Status == ImTextureStatus_WantCreate || !t || t->width != tex->Width || t->height != tex->Height) {
        destroy(t);
        t = new Texture;
        t->width = tex->Width;
        t->height = tex->Height;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {uint32_t(tex->Width), uint32_t(tex->Height), 1};
        ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_check(vkCreateImage(R.device, &ci, nullptr, &t->image), "overlay texture");
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(R.device, t->image, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(R.device, &ai, nullptr, &t->memory), "overlay texture memory");
        vk_check(vkBindImageMemory(R.device, t->image, t->memory, 0), "overlay texture bind");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t->image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk_check(vkCreateImageView(R.device, &vi, nullptr, &t->view), "overlay texture view");
        tex->SetTexID((ImTextureID)(uintptr_t)t);
    }
    const size_t pitch = size_t(tex->Width) * 4, bytes = pitch * tex->Height;
    UploadSlice staging = allocate_upload(bytes, 16);
    memcpy(staging.mapped, tex->GetPixels(), bytes);
    barrier(cmd, *t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.bufferOffset = staging.offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(tex->Width), uint32_t(tex->Height), 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(cmd, *t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    tex->SetStatus(ImTextureStatus_OK);
}

}  // namespace

void overlay_renderer_init() {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "wwhd_vulkan";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
}

void overlay_prepare(ImDrawData* d) {
    if (!d || !d->Textures) return;
    VkCommandBuffer cmd = command_buffer();
    for (ImTextureData* tex : *d->Textures)
        if (tex->Status != ImTextureStatus_OK) update_texture(cmd, tex);
}

void overlay_draw(ImDrawData* d, VkCommandBuffer cmd, VkFormat format, VkExtent2D extent, bool linear) {
    if (!d || d->TotalVtxCount <= 0 || d->DisplaySize.x <= 0 || d->DisplaySize.y <= 0) return;
    // the draw data is laid out for the TV window; a target of another size (present dump) gets it scaled
    const float sx = extent.width / d->DisplaySize.x, sy = extent.height / d->DisplaySize.y;
    UploadSlice vtx = allocate_upload(size_t(d->TotalVtxCount) * sizeof(ImDrawVert), 16);
    UploadSlice idx = allocate_upload(size_t(d->TotalIdxCount) * sizeof(ImDrawIdx), 16);
    auto* vp = (ImDrawVert*)vtx.mapped;
    auto* ip = (ImDrawIdx*)idx.mapped;
    for (const ImDrawList* l : d->CmdLists) {
        memcpy(vp, l->VtxBuffer.Data, l->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(ip, l->IdxBuffer.Data, l->IdxBuffer.Size * sizeof(ImDrawIdx));
        vp += l->VtxBuffer.Size;
        ip += l->IdxBuffer.Size;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(format));
    vkCmdBindVertexBuffers(cmd, 0, 1, &vtx.buffer, &vtx.offset);
    vkCmdBindIndexBuffer(cmd, idx.buffer, idx.offset, sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    Params p{};
    p.scale[0] = 2.0f / d->DisplaySize.x;
    p.scale[1] = 2.0f / d->DisplaySize.y;
    p.translate[0] = -1.0f - d->DisplayPos.x * p.scale[0];
    p.translate[1] = -1.0f - d->DisplayPos.y * p.scale[1];
    p.linear = linear ? 1 : 0;
    vkCmdPushConstants(cmd, res.layout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
    std::unordered_map<Texture*, VkDescriptorSet> sets;
    VkDescriptorSet bound = VK_NULL_HANDLE;
    int vtx_base = 0, idx_base = 0;
    for (const ImDrawList* l : d->CmdLists) {
        for (const ImDrawCmd& c : l->CmdBuffer) {
            if (c.UserCallback) {
                if(c.UserCallback==overlay::guesthud::blend_callback)
                    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(format,c.UserCallbackData!=nullptr));
                continue;
            }
            float x0 = (c.ClipRect.x - d->DisplayPos.x) * sx, y0 = (c.ClipRect.y - d->DisplayPos.y) * sy;
            float x1 = (c.ClipRect.z - d->DisplayPos.x) * sx, y1 = (c.ClipRect.w - d->DisplayPos.y) * sy;
            x0 = std::max(x0, 0.0f);
            y0 = std::max(y0, 0.0f);
            x1 = std::min(x1, float(extent.width));
            y1 = std::min(y1, float(extent.height));
            if (x1 <= x0 || y1 <= y0) continue;
            Texture* t = (Texture*)(uintptr_t)c.GetTexID();
            if (!t || !t->view) continue;
            int linear_texture=linear&&overlay::guesthud::image_texture(uint64_t(c.GetTexID()));
            if(p.linear_texture!=linear_texture) {
                p.linear_texture=linear_texture;
                vkCmdPushConstants(cmd,res.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof p,&p);
            }
            VkDescriptorSet& set = sets[t];
            if (!set) {
                VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                a.descriptorPool = R.descriptorPool;
                a.descriptorSetCount = 1;
                a.pSetLayouts = &res.descriptors;
                vk_check(vkAllocateDescriptorSets(R.device, &a, &set), "overlay descriptor set");
                VkDescriptorImageInfo info{res.sampler, t->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                w.dstSet = set;
                w.descriptorCount = 1;
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &info;
                vkUpdateDescriptorSets(R.device, 1, &w, 0, nullptr);
            }
            if (set != bound) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res.layout, 0, 1, &set, 0, nullptr);
                bound = set;
            }
            VkRect2D scissor{{int32_t(x0), int32_t(y0)}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdDrawIndexed(cmd, c.ElemCount, 1, c.IdxOffset + idx_base, int32_t(c.VtxOffset + vtx_base), 0);
        }
        vtx_base += l->VtxBuffer.Size;
        idx_base += l->IdxBuffer.Size;
    }
}

void reset_overlay_resources() {
    if(!res.device)return;
    if(ImGui::GetCurrentContext())for(auto* tex:ImGui::GetPlatformIO().Textures) {
        auto* t=reinterpret_cast<Texture*>(uintptr_t(tex->GetTexID()));
        if(t) {
            if(t->view)vkDestroyImageView(res.device,t->view,nullptr);
            if(t->image)vkDestroyImage(res.device,t->image,nullptr);
            if(t->memory)vkFreeMemory(res.device,t->memory,nullptr);
            delete t;
        }
        tex->SetTexID(ImTextureID_Invalid);tex->SetStatus(ImTextureStatus_Destroyed);
    }
    overlay::guesthud::backend_destroyed();
    for (auto [f, p] : res.pipelines) vkDestroyPipeline(res.device, p, nullptr);
    if (res.sampler) vkDestroySampler(res.device, res.sampler, nullptr);
    if (res.layout) vkDestroyPipelineLayout(res.device, res.layout, nullptr);
    if (res.descriptors) vkDestroyDescriptorSetLayout(res.device, res.descriptors, nullptr);
    res = {};
}

}  // namespace gfxvk
