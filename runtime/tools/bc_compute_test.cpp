// The Vulkan BC compute decoder (gfx/vulkan/bc_shader.h) on a real device, with two referees:
//  - the CPU decoder (bc_reference.h): byte-exact on the known-answer blocks (every selector in every
//    pixel position of both channel palette modes, signed and unsigned) and on random blocks;
//  - the device's own BC sampling where it has BC textures: native decoders interpolate with their
//    own precision (Apple GPUs use 1/64 weights), so it must agree within 3 steps, which still
//    catches a wrong palette mode, a dropped channel or a wrong selector.
// The shader always has to compile (glslang). Without a Vulkan device the GPU part is skipped (77).
// A copy of the shader with the Adreno 750 (Qualcomm v762) miscompilation put in by hand (selector 6 of
// the 6-value mode decoded as the 8-value interpolant) must fail the known-answer comparison.
#include "gfx/vulkan/bc_reference.h"
#include "gfx/vulkan/bc_shader.h"
#include <vulkan/vulkan.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace {
using namespace gfxvk;
std::vector<uint32_t> compile(const std::string& source) {
    glslang::TShader shader(EShLangCompute);
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    if (!shader.parse(GetDefaultResources(), 450, false, EShMsgDefault)) {
        std::fprintf(stderr, "compile: %s\n", shader.getInfoLog());
        return {};
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) { std::fprintf(stderr, "link: %s\n", program.getInfoLog()); return {}; }
    std::vector<uint32_t> words;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), words);
    return words;
}
const char* kFetch = R"(#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform sampler2D tex;
layout(set=0,binding=1,std430) writeonly buffer O { vec4 o[]; };
layout(push_constant) uniform P { uint w; uint h; } p;
void main() { uvec2 g = gl_GlobalInvocationID.xy; if (g.x < p.w && g.y < p.h) o[g.y * p.w + g.x] = texelFetch(tex, ivec2(g), 0); }
)";
#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { std::fprintf(stderr, "%s: %d\n", #x, int(r_)); std::exit(1); } } while (0)
struct Gpu {
    VkInstance instance = VK_NULL_HANDLE; VkPhysicalDevice physical = VK_NULL_HANDLE; VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE; VkCommandPool pool = VK_NULL_HANDLE; VkDescriptorPool descriptors = VK_NULL_HANDLE;
    bool bc = false;
    struct Buffer { VkBuffer buffer; VkDeviceMemory memory; void* mapped; VkDeviceSize size; };
    struct Pipeline { VkDescriptorSetLayout set; VkPipelineLayout layout; VkPipeline pipeline; };
    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties p; vkGetPhysicalDeviceMemoryProperties(physical, &p);
        for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
            if ((bits >> i & 1) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
        std::fprintf(stderr, "no memory type\n"); std::exit(1);
    }
    bool open() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
        uint32_t count = 0; vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count); vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        std::vector<const char*> extensions;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &app;
        for (auto& e : available) if (!std::strcmp(e.extensionName, "VK_KHR_portability_enumeration")) {
            extensions.push_back("VK_KHR_portability_enumeration"); ci.flags |= 1; } // ..._ENUMERATE_PORTABILITY_BIT_KHR
        ci.enabledExtensionCount = uint32_t(extensions.size()); ci.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) return false;
        count = 1;
        if (vkEnumeratePhysicalDevices(instance, &count, &physical) < 0 || !count) return false;
        VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
        VkPhysicalDeviceFeatures features; vkGetPhysicalDeviceFeatures(physical, &features);
        bc = features.textureCompressionBC;
        std::printf("device: %s (BC textures %s)\n", properties.deviceName, bc ? "yes" : "no");
        uint32_t families = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> family(families); vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, family.data());
        uint32_t index = 0; while (index < families && !(family[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++index;
        if (index == families) return false;
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = index; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures enable{}; enable.textureCompressionBC = features.textureCompressionBC;
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> deviceExtensions(count); vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, deviceExtensions.data());
        std::vector<const char*> enabled;
        for (auto& e : deviceExtensions) if (!std::strcmp(e.extensionName, "VK_KHR_portability_subset")) enabled.push_back("VK_KHR_portability_subset");
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &enable;
        di.enabledExtensionCount = uint32_t(enabled.size()); di.ppEnabledExtensionNames = enabled.data();
        if (vkCreateDevice(physical, &di, nullptr, &device) != VK_SUCCESS) return false;
        vkGetDeviceQueue(device, index, 0, &queue);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pi.queueFamilyIndex = index;
        CHECK(vkCreateCommandPool(device, &pi, nullptr, &pool));
        VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dp.maxSets = 4; dp.poolSizeCount = 2; dp.pPoolSizes = sizes;
        CHECK(vkCreateDescriptorPool(device, &dp, nullptr, &descriptors));
        return true;
    }
    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
        Buffer b{VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, size};
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = usage;
        CHECK(vkCreateBuffer(device, &ci, nullptr, &b.buffer));
        VkMemoryRequirements r; vkGetBufferMemoryRequirements(device, b.buffer, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = r.size;
        ai.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CHECK(vkAllocateMemory(device, &ai, nullptr, &b.memory)); CHECK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
        CHECK(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped));
        return b;
    }
    void release(Buffer& b) { vkUnmapMemory(device, b.memory); vkDestroyBuffer(device, b.buffer, nullptr); vkFreeMemory(device, b.memory, nullptr); }
    Pipeline pipeline(const std::vector<uint32_t>& code, std::vector<VkDescriptorType> types, uint32_t push) {
        Pipeline p{};
        std::vector<VkDescriptorSetLayoutBinding> bindings(types.size());
        for (uint32_t i = 0; i < types.size(); ++i) bindings[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        si.bindingCount = uint32_t(bindings.size()); si.pBindings = bindings.data();
        CHECK(vkCreateDescriptorSetLayout(device, &si, nullptr, &p.set));
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, push};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1; li.pSetLayouts = &p.set; li.pushConstantRangeCount = 1; li.pPushConstantRanges = &range;
        CHECK(vkCreatePipelineLayout(device, &li, nullptr, &p.layout));
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; mi.codeSize = code.size() * 4; mi.pCode = code.data();
        VkShaderModule module; CHECK(vkCreateShaderModule(device, &mi, nullptr, &module));
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.layout = p.layout;
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
        CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &p.pipeline));
        vkDestroyShaderModule(device, module, nullptr);
        return p;
    }
    void release(Pipeline& p) {
        vkDestroyPipeline(device, p.pipeline, nullptr); vkDestroyPipelineLayout(device, p.layout, nullptr);
        vkDestroyDescriptorSetLayout(device, p.set, nullptr);
    }
    VkDescriptorSet set(const Pipeline& p) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = descriptors; ai.descriptorSetCount = 1; ai.pSetLayouts = &p.set;
        VkDescriptorSet s; CHECK(vkAllocateDescriptorSets(device, &ai, &s)); return s;
    }
    template <class Record> void run(Record record) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(cmd, &bi));
        record(cmd);
        CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(device, pool, 1, &cmd);
    }
    // the decoder exactly as bc_decode.cpp binds and dispatches it
    std::vector<uint8_t> decode(const Pipeline& p, const std::vector<uint8_t>& blocks, uint32_t w, uint32_t h, uint32_t mode) {
        Buffer in = buffer(blocks.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT), out = buffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(in.mapped, blocks.data(), blocks.size()); std::memset(out.mapped, 0xcd, size_t(out.size));
        VkDescriptorSet s = set(p);
        VkDescriptorBufferInfo infos[2] = {{in.buffer, 0, in.size}, {out.buffer, 0, out.size}};
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[i].dstSet = s; writes[i].dstBinding = i;
            writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        run([&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &s, 0, nullptr);
            uint32_t params[4] = {w, h, 1, mode};
            vkCmdPushConstants(cmd, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof params, params);
            vkCmdDispatch(cmd, ((w + 3) / 4 + 7) / 8, ((h + 3) / 4 + 7) / 8, 1);
        });
        std::vector<uint8_t> result(static_cast<uint8_t*>(out.mapped), static_cast<uint8_t*>(out.mapped) + out.size);
        vkFreeDescriptorSets(device, descriptors, 1, &s); release(in); release(out);
        return result;
    }
    // the device's own BC decoding: upload into a BC image and texelFetch every texel
    std::vector<float> native(const Pipeline& p, const std::vector<uint8_t>& blocks, uint32_t w, uint32_t h, VkFormat format) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = format; ii.extent = {w, h, 1};
        ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkImage image; CHECK(vkCreateImage(device, &ii, nullptr, &image));
        VkMemoryRequirements r; vkGetImageMemoryRequirements(device, image, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = r.size; ai.memoryTypeIndex = memory_type(r.memoryTypeBits, 0);
        VkDeviceMemory memory; CHECK(vkAllocateMemory(device, &ai, nullptr, &memory)); CHECK(vkBindImageMemory(device, image, memory, 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view; CHECK(vkCreateImageView(device, &vi, nullptr, &view));
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; VkSampler sampler; CHECK(vkCreateSampler(device, &sci, nullptr, &sampler));
        Buffer upload = buffer(blocks.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT), out = buffer(VkDeviceSize(w) * h * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(upload.mapped, blocks.data(), blocks.size());
        VkDescriptorSet s = set(p);
        VkDescriptorImageInfo imageInfo{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo bufferInfo{out.buffer, 0, out.size};
        VkWriteDescriptorSet writes[2]{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[0].dstSet = s; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo = &imageInfo;
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[1].dstSet = s; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        run([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image = image; b.subresourceRange = vi.subresourceRange;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {w, h, 1};
            vkCmdCopyBufferToImage(cmd, upload.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            b.oldLayout = b.newLayout; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &s, 0, nullptr);
            uint32_t params[2] = {w, h};
            vkCmdPushConstants(cmd, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof params, params);
            vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
        });
        const float* f = static_cast<const float*>(out.mapped);
        std::vector<float> result(f, f + size_t(w) * h * 4);
        vkFreeDescriptorSets(device, descriptors, 1, &s); release(upload); release(out);
        vkDestroySampler(device, sampler, nullptr); vkDestroyImageView(device, view, nullptr);
        vkDestroyImage(device, image, nullptr); vkFreeMemory(device, memory, nullptr);
        return result;
    }
};
// known-answer blocks then random blocks, laid out 16 blocks wide
std::vector<uint8_t> test_blocks(unsigned type, uint32_t& w, uint32_t& h) {
    auto blocks = bc::known_answer_blocks(type, 256);
    uint32_t random = 77u + type;
    for (unsigned n = 0; n < 256 * bc::block_bytes(type); ++n) { random = random * 1664525u + 1013904223u; blocks.push_back(uint8_t(random >> 24)); }
    w = 16 * 4; h = uint32_t(blocks.size() / bc::block_bytes(type) / 16) * 4;
    return blocks;
}
}
int main() {
    glslang::InitializeProcess();
    const auto spirv = compile(gfxvk::bc::shader);
    if (spirv.empty()) { std::fprintf(stderr, "FAIL: the BC compute shader does not compile\n"); return 1; }
    // the Adreno 750 / Qualcomm v762 behaviour, put in by hand: selector 6 of the 6-value mode decoded as (2a + 5b) / 7
    std::string broken = gfxvk::bc::shader;
    const std::string six = "p.v6 = eight ? div7(2 * a + 5 * b) : (sign ? -127 : 0);";
    if (broken.find(six) == std::string::npos) { std::fprintf(stderr, "FAIL: fault injection point moved\n"); return 1; }
    broken.replace(broken.find(six), six.size(), "p.v6 = div7(2 * a + 5 * b);");
    const auto brokenSpirv = compile(broken);
    if (brokenSpirv.empty()) { std::fprintf(stderr, "FAIL: fault-injected shader does not compile\n"); return 1; }
    Gpu gpu;
    if (!gpu.open()) { std::printf("SKIP: no Vulkan device with a compute queue\n"); glslang::FinalizeProcess(); return 77; }
    auto decoder = gpu.pipeline(spirv, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, 16);
    auto brokenDecoder = gpu.pipeline(brokenSpirv, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, 16);
    auto fetch = gpu.pipeline(compile(kFetch), {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, 8);
    struct Format { const char* name; uint32_t mode; VkFormat native; };
    const Format formats[] = {{"BC1", 1, VK_FORMAT_BC1_RGBA_UNORM_BLOCK}, {"BC2", 2, VK_FORMAT_BC2_UNORM_BLOCK},
        {"BC3", 3, VK_FORMAT_BC3_UNORM_BLOCK}, {"BC4", 4, VK_FORMAT_BC4_UNORM_BLOCK}, {"BC4 signed", 4 | 256, VK_FORMAT_BC4_SNORM_BLOCK},
        {"BC5", 5, VK_FORMAT_BC5_UNORM_BLOCK}, {"BC5 signed", 5 | 256, VK_FORMAT_BC5_SNORM_BLOCK}};
    int failures = 0;
    for (const auto& f : formats) {
        const unsigned type = f.mode & 255; const bool sign = f.mode & 256;
        uint32_t w, h; const auto blocks = test_blocks(type, w, h);
        const auto cpu = gfxvk::bc::decode(blocks, w, h, 1, f.mode);
        const auto compute = gpu.decode(decoder, blocks, w, h, f.mode);
        size_t differ = 0;
        for (size_t n = 0; n < cpu.size(); ++n) if (cpu[n] != compute[n] && differ++ < 4)
            std::printf("  %s pixel %zu channel %zu: compute %u, CPU %u\n", f.name, n / 4, n % 4, compute[n], cpu[n]);
        // channels whose value the format defines (BC4: red, BC5: red and green)
        const unsigned channels = type == 4 ? 1 : type == 5 ? 2 : 4;
        double worst = 0; bool nativeChecked = false;
        VkFormatProperties properties; vkGetPhysicalDeviceFormatProperties(gpu.physical, f.native, &properties);
        if (gpu.bc && (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            nativeChecked = true;
            const auto native = gpu.native(fetch, blocks, w, h, f.native);
            for (size_t px = 0; px < size_t(w) * h; ++px) for (unsigned c = 0; c < channels; ++c) {
                const size_t n = px * 4 + c;
                const double ours = sign ? double(int8_t(cpu[n])) : double(cpu[n]);
                worst = std::max(worst, std::fabs(native[n] * (sign ? 127.0 : 255.0) - ours));
            }
        }
        const bool broken = (type >= 3) && (gpu.decode(brokenDecoder, blocks, w, h, f.mode) == cpu);
        std::printf("%-10s compute vs CPU: %zu bytes differ; native BC: %s; fault-injected shader %s\n", f.name, differ,
                    nativeChecked ? (std::to_string(worst).substr(0, 5) + " steps at most").c_str() : "not available",
                    type >= 3 ? (broken ? "NOT caught" : "caught") : "n/a");
        if (differ || worst > 3.0 || broken) ++failures;
    }
    gpu.release(decoder); gpu.release(brokenDecoder); gpu.release(fetch);
    vkDestroyDescriptorPool(gpu.device, gpu.descriptors, nullptr); vkDestroyCommandPool(gpu.device, gpu.pool, nullptr);
    vkDestroyDevice(gpu.device, nullptr); vkDestroyInstance(gpu.instance, nullptr);
    glslang::FinalizeProcess();
    if (failures) { std::printf("FAIL: %d formats\n", failures); return 1; }
    std::printf("PASS\n");
    return 0;
}
