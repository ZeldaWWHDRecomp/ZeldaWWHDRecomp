// Sky passes of the cloud work on Vulkan (cloud_proto.cpp has the game side, gfx/metal_clouds.mm the
// Metal side). For now: the horizon-haze grade (Graphics > Horizon haze), drawn into the half-resolution
// sky buffer when the game starts drawing its sky clouds, after the sky dome and its haze band.
#include "backend.h"
#include "deferred_dispatch.h"
#include "pass.h"
#include "shaders.h"
#include "stereo.h"
#include "../../cloud_proto.h"
#include "../../cloud_noise.h"
#include "../../cloud_scene.h"
#include "../../exception_report.h"
#include "gfx/renderer.h"
#include "runtime.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfxvk {
namespace {
void check_vk(VkResult result, const char* what) {
    if (result != VK_SUCCESS) exception_report::raise(std::string(what) + " failed (" + std::to_string(int(result)) + ")");
}
VkShaderModule sky_module(const char* source, bool vertex) {
    std::string error;
    auto words = vk::compile_glsl(source, vertex, &error);
    if (words.empty() || !error.empty()) exception_report::raise("Vulkan sky shader: " + error);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check_vk(vkCreateShaderModule(R.device, &ci, nullptr, &module), "create sky shader");
    return module;
}

const char* kSkyVertex = R"glsl(#version 450
layout(location=0) out vec2 ndc;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    ndc = vec2(p.x, -p.y);  // Vulkan's y points down; the shader works in GL-style NDC (y up)
    gl_Position = vec4(p, 0.5, 1.0);
}
)glsl";
// the same grade as metal_clouds.mm grade_fs: pull the horizon band back toward the sky colour, keeping
// a warm glow around a low sun
const char* kGradeFragment = R"glsl(#version 450
layout(push_constant) uniform Sky {
    vec4 view0, view1, view2, proj, sun, lit, sky, params;
} g;
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
void main() {
    mat3 view = mat3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
    vec3 dc = normalize(vec3(ndc.x / g.proj.x, ndc.y / g.proj.y, -1.0));
    vec3 rd = transpose(view) * dc;
    float band = (1.0 - smoothstep(0.0, 0.32, rd.y)) * step(-0.02, rd.y);
    vec3 skyc = g.sky.rgb * mix(0.95, 1.12, clamp(rd.y * 4.0, 0.0, 1.0));
    float aroundSun = pow(clamp(dot(rd, g.sun.xyz), 0.0, 1.0), 6.0) * g.lit.w;
    float a = clamp(g.params.z, 0.0, 1.0) * band * (1.0 - aroundSun);
    color = vec4(skyc * a, a);
}
)glsl";

struct Resources {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> pipelines;  // by the pass's attachment formats
} res;

VkPipeline grade_pipeline() {
    if (res.device && res.device != R.device) res = {};
    res.device = R.device;
    if (!res.layout) {
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cloud_proto::SkyFrame)};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &push;
        check_vk(vkCreatePipelineLayout(R.device, &ci, nullptr, &res.layout), "create sky grade layout");
    }
    std::string key = "grade";
    uint32_t ncolor = 0;
    VkFormat colorFormats[8]{};
    for (uint32_t i = 0; i < 8; i++)
        if (R.passColors[i]) { colorFormats[i] = R.passColors[i]->fmt.pixel; ncolor = i + 1; }
    for (uint32_t i = 0; i < ncolor; i++) key += "/" + std::to_string(int(colorFormats[i]));
    if (R.passDepth) key += "/d" + std::to_string(int(R.passDepth->fmt.pixel)) + (R.passDepth->fmt.stencil ? "s" : "");
    if (auto it = res.pipelines.find(key); it != res.pipelines.end()) return it->second;
    VkShaderModule vs = sky_module(kSkyVertex, true), fs = sky_module(kGradeFragment, false);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend[8]{};
    for (uint32_t i = 0; i < ncolor; i++) blend[i].colorWriteMask = 0;
    blend[0].blendEnable = VK_TRUE;  // premultiplied: rgb + dst * (1 - a); destination alpha kept
    blend[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend[0].colorBlendOp = VK_BLEND_OP_ADD;
    blend[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].alphaBlendOp = VK_BLEND_OP_ADD;
    blend[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = ncolor;
    bs.pAttachments = blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = ncolor;
    rendering.pColorAttachmentFormats = colorFormats;
    if (R.passDepth) {
        rendering.depthAttachmentFormat = R.passDepth->fmt.pixel;
        rendering.stencilAttachmentFormat = R.passDepth->fmt.stencil ? R.passDepth->fmt.pixel : VK_FORMAT_UNDEFINED;
    }
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_targets(ci, rendering);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &dss;
    ci.pColorBlendState = &bs;
    ci.pDynamicState = &ds;
    ci.layout = res.layout;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe);
    vkDestroyShaderModule(R.device, vs, nullptr);
    vkDestroyShaderModule(R.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        LOG("[sky] Vulkan grade pipeline %s failed (%d)", key.c_str(), int(r));
        pipe = VK_NULL_HANDLE;
    } else {
        LOG("[sky] Vulkan grade pipeline %s", key.c_str());
    }
    res.pipelines.emplace(key, pipe);
    return pipe;
}

}  // namespace

// OP_CLOUDS_BEGIN (renderer table): the sky buffer's pass is the open one
static uint32_t g_skyAddr = 0, g_skySeq = 0, g_cloudsWanted = 0;
static uint64_t g_skyFrame = ~0ull;
void sky_clouds_begin(const uint32_t* regs, uint32_t seq) {
    (void)regs;
    static uint64_t begins = 0, opened = 0;
    begins++;
    if (R.rendering && R.passColors[0]) g_skyAddr = R.passColors[0]->addr, g_skySeq = seq, g_skyFrame = R.frame, opened++;
    if (cloud_proto::mode() == cloud_proto::kSdf) g_cloudsWanted = seq;
    if (begins % 300 == 0 && render::cloud_shadows())
        LOG("[shadows] Vulkan: sky begins %llu, with an open pass %llu", (unsigned long long)begins, (unsigned long long)opened);
    const float reduce = 1.0f - render::horizon_haze();
    if (reduce <= 0.0f || stereo::frame.mode != stereo::Off) return;  // stereo 3D: the original sky (limitation)
    static uint64_t missed = 0;
    if (!R.rendering || !R.passTracked || !R.passColors[0] || !R.passWidth || !R.passHeight) {
        if (missed++ % 600 == 0) LOG("[sky] horizon haze: no open sky pass (%llu times)", (unsigned long long)missed);
        return;
    }
    VkPipeline pipe = grade_pipeline();
    if (!pipe) return;
    cloud_proto::SkyFrame g;
    cloud_proto::sky_frame(*cloud_proto::snapshot(seq), float(R.passWidth) / float(R.passHeight), reduce, g);
    VkCommandBuffer cmd = command_buffer();
    VkViewport viewport{0, 0, float(R.passWidth), float(R.passHeight), 0, 1};
    VkRect2D area{{0, 0}, {R.passWidth, R.passHeight}};
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &area);
    record::vkCmdPushConstants(cmd, res.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof g, &g);
    record::vkCmdDraw(cmd, 3, 1, 0, 0);
    // the game's next draw must not trust its cached pipeline / dynamic state: start a fresh pass
    end_encoder();
}


// ---- cloud shadows (Graphics > Cloud shadows), as in metal_clouds.mm: after the draw that composites
// the sky buffer, the picture and its depth buffer are remembered; right before the picture is first
// read (the post effects) or a depth-only draw rewrites its depth, a full-screen pass reconstructs each
// pixel's world position from depth, follows the light up to the cloud layer and darkens by the cloud
// coverage there (the game's clouds anchored in the world), in two toon steps
namespace {
const char* kShadowFragment = R"glsl(#version 450
layout(push_constant) uniform G { vec4 view0, view1, view2, proj, sun, layer, look; } g;
layout(set=0, binding=0) uniform sampler2D depthTex;
layout(set=0, binding=1) uniform Clouds { vec4 c[100]; } cl;
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (d >= 0.99999) discard;
    if (int(g.look.z + 0.5) == 10) { color = vec4(1, 0, 1, 1); return; }  // debug: geometry in magenta
    if (int(g.look.z + 0.5) == 12) { color = vec4(clamp(cl.c[0].w, 0.0, 1.0), g.layer.w / 100.0, clamp(cl.c[0].z / 3000.0, 0.0, 1.0), 1); return; }
    float n = g.proj.z, f = g.proj.w;
    float zlin = n * f / (f - d * (f - n));  // GX2 depth is 0..1
    vec3 dc = vec3(ndc.x / g.proj.x, ndc.y / g.proj.y, -1.0);
    mat3 view = mat3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
    vec3 p = transpose(view) * (dc * zlin);
    float wy = p.y + g.layer.z;
    vec3 L = g.sun.xyz;
    float t = (g.layer.x - wy) / max(L.y, 0.05);
    vec2 q = p.xz + L.xz * t;
    if (int(g.look.z + 0.5) == 13) { color = vec4(fract(q.x / 3000.0), fract(q.y / 3000.0), clamp(wy / 200.0, 0.0, 1.0), 1); return; }
    float cov = 0.0;
    for (int k = 0; k < int(g.layer.w); k++) {
        vec4 c = cl.c[k];
        vec2 dd = (q - c.xy) / c.z;
        float r2 = dot(dd, dd);
        if (r2 < 1.0) cov = max(cov, clamp(c.w * 1.3, 0.0, 1.0) * (1.0 - r2));
    }
    float nz = vnoise(q * 0.0011) * 0.6 + vnoise(q * 0.0034) * 0.4;
    cov = clamp((cov - 0.18 - (0.55 - nz) * 0.45) * 3.0, 0.0, 1.0);
    if (int(g.look.z + 0.5) == 4) { color = vec4(1.0, 1.0 - 0.8 * cov, 1.0 - 0.8 * cov, 1.0); return; }  // debug: coverage
    float sw = g.look.y;
    float shade = 0.45 * smoothstep(0.25 - sw, 0.25 + sw, cov) + 0.55 * smoothstep(0.6 - sw, 0.6 + sw, cov);
    float fade = 1.0 - smoothstep(12000.0, 22000.0, length(p.xz));
    float k = 1.0 - g.look.x * shade * fade * g.sun.w;
    if (k > 0.999) discard;
    color = vec4(k, k, k, 1.0);
}
)glsl";
struct ShadowGlobals { float view[12], proj[4], sun[4], layer[4], look[4]; };
struct ShadowResources {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::unordered_map<int, VkPipeline> pipelines;  // by colour format
} sres;
uint32_t g_pictureAddr = 0, g_pictureDepthAddr = 0, g_shadowSeq = 0;
Surface *g_picture = nullptr, *g_pictureDepth = nullptr;
uint32_t g_pictureSlice = 0;
uint64_t g_shadowFrame = ~0ull;

VkPipeline shadow_pipeline(VkFormat format) {
    if (sres.device && sres.device != R.device) sres = {};
    sres.device = R.device;
    if (!sres.set) {
        VkDescriptorSetLayoutBinding b[2]{{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                          {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 2;
        ci.pBindings = b;
        check_vk(vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &sres.set), "create cloud shadow descriptors");
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShadowGlobals)};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1;
        li.pSetLayouts = &sres.set;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &push;
        check_vk(vkCreatePipelineLayout(R.device, &li, nullptr, &sres.layout), "create cloud shadow layout");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.minFilter = si.magFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check_vk(vkCreateSampler(R.device, &si, nullptr, &sres.sampler), "create cloud shadow sampler");
    }
    if (auto it = sres.pipelines.find(int(format)); it != sres.pipelines.end()) return it->second;
    VkShaderModule vs = sky_module(kSkyVertex, true), fs = sky_module(kShadowFragment, false);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;  // multiply: dst * k
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
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
    pipeline_targets(ci, rendering);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &dss;
    ci.pColorBlendState = &bs;
    ci.pDynamicState = &ds;
    ci.layout = sres.layout;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe);
    vkDestroyShaderModule(R.device, vs, nullptr);
    vkDestroyShaderModule(R.device, fs, nullptr);
    if (r != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    LOG("[shadows] Vulkan pipeline for format %d%s", int(format), pipe ? "" : " failed");
    sres.pipelines.emplace(int(format), pipe);
    return pipe;
}

VkImageView depth_view(Surface* s, uint32_t layer) {
    const uint32_t key = 0x9E000000u | layer;  // freed with the surface's other sampled views
    if (auto it = s->sampledViews.find(key); it != s->sampledViews.end()) return it->second;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = s->image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = s->fmt.pixel;
    info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
    VkImageView view = VK_NULL_HANDLE;
    check_vk(vkCreateImageView(R.device, &info, nullptr, &view), "create cloud shadow depth view");
    s->sampledViews.emplace(key, view);
    return view;
}

void shadows_now() {
    const uint32_t seq = g_shadowSeq;
    g_shadowSeq = 0;
    Surface* color = g_picture;
    Surface* depth = g_pictureDepth;
    // several surfaces can share an address: take the last pass's, where the later scene draws went
    if (R.passColors[0] && R.passColors[0]->addr == g_pictureAddr) color = R.passColors[0];
    if (R.passDepth && R.passDepth->addr == g_pictureDepthAddr) depth = R.passDepth;
    if (!color || !depth || !depth->fmt.depth) return;
    VkPipeline pipe = shadow_pipeline(color->fmt.pixel);
    if (!pipe) return;
    const cloud_proto::Snapshot& s = *cloud_proto::snapshot(seq);
    ShadowGlobals g{};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) g.view[c * 4 + r] = s.viewRot[r * 4 + c];
    const float aspect = float(color->extent.width) / float(color->extent.height);
    const float f = 1.0f / std::tan(s.fovy * 0.5f * 3.14159265f / 180.0f);
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = s.viewNear, g.proj[3] = s.viewFar;
    cloud_proto::SkyFrame sf;
    cloud_proto::sky_frame(s, aspect, 0, sf);
    for (int i = 0; i < 4; i++) g.sun[i] = sf.sun[i];
    g.layer[0] = 6000.0f, g.layer[1] = 24000.0f, g.layer[2] = s.eye[1], g.layer[3] = float(s.nShadow);
    auto envf = [](const char* n, float d) { const char* e = getenv(n); return e && *e ? (float)atof(e) : d; };
    static const float dark = envf("WWHD_CLOUD_SHADOW_DARKNESS", 0.35f), debug = envf("WWHD_CLOUD_SHADOWS_DEBUG", 0.0f);
    g.look[0] = dark * (1.0f - 0.7f * sf.lit[3]) * std::clamp(1.0f - 0.6f * s.strength, 0.3f, 1.0f);
    g.look[1] = 0.04f, g.look[2] = debug;
    UploadSlice clouds = allocate_upload(sizeof(float) * 4 * 100, 256);
    std::memset(clouds.mapped, 0, sizeof(float) * 4 * 100);
    std::memcpy(clouds.mapped, s.shadow, sizeof(float) * 4 * size_t(std::max(0, s.nShadow)));
    end_encoder();
    transition_image(depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    transition_image(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptor_pool();
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &sres.set;
    check_vk(vkAllocateDescriptorSets(R.device, &alloc, &set), "allocate cloud shadow descriptors");
    VkDescriptorImageInfo image{sres.sampler, depth_view(depth, R.passDepth == depth ? R.passDepthSlice : 0),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo buffer{clouds.buffer, clouds.offset, sizeof(float) * 4 * 100};
    VkWriteDescriptorSet w[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstSet = w[1].dstSet = set;
    w[0].dstBinding = 0, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[0].pImageInfo = &image;
    w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, w[1].pBufferInfo = &buffer;
    vkUpdateDescriptorSets(R.device, 2, w, 0, nullptr);
    VkCommandBuffer cmd = command_buffer();
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = layer_view(color, g_pictureSlice);
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = {color->extent.width, color->extent.height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    begin_pass(cmd, ri, [&] {
        PassBegin pass;
        pass.formats.colorCount = 1;
        pass.formats.colors[0] = color->fmt.pixel;
        pass.layouts[0] = color->layout;
        return pass;
    });
    ++R.renderPassCount;
    VkViewport viewport{0, 0, float(color->extent.width), float(color->extent.height), 0, 1};
    VkRect2D area{{0, 0}, {color->extent.width, color->extent.height}};
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &area);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sres.layout, 0, 1, &set, 0, nullptr);
    record::vkCmdPushConstants(cmd, sres.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof g, &g);
    record::vkCmdDraw(cmd, 3, 1, 0, 0);
    end_pass(cmd);
}
}  // namespace

// ---- toon clouds (Graphics > Cloud style), the Vulkan side of metal_clouds.mm's impostors: the game's
// own cloud cards are skipped in the sky buffer (draw.cpp, clouds_skip_card) and the impostors are drawn
// into the full-resolution picture right after the draw that composites the sky buffer, with a "sky only"
// depth test (in front of or at the far plane, no depth writes)
namespace {
const char* kCloudVertex = R"glsl(#version 450
layout(set=0, binding=0) uniform Globals {
    vec4 view0, view1, view2, proj, sun, lit, sky, params, tune, extra;
} g;
struct Inst { vec4 center, shape, base; };
layout(set=0, binding=1) uniform Instances { Inst i[320]; } inst;
layout(location=0) out vec3 vWorld;
layout(location=1) flat out int vInst;
void main() {
    Inst c = inst.i[gl_InstanceIndex];
    vec2 corner = vec2((gl_VertexIndex & 1) != 0 ? 1.0 : -1.0, (gl_VertexIndex & 2) != 0 ? 1.0 : -1.0);
    float r = c.center.w * max(1.0, c.shape.x) * 1.6;
    vec3 mid = c.center.xyz + vec3(0, c.center.w * c.shape.x * 0.6, 0);
    mat3 view = mat3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up = vec3(view[0][1], view[1][1], view[2][1]);
    vec3 p = mid + (corner.x * right + corner.y * up) * r;
    vec3 q = view * p;
    float w = -q.z;
    gl_Position = vec4(q.x * g.proj.x, -q.y * g.proj.y, g.extra.z * w, w);  // Vulkan clip y points down
    vWorld = p;
    vInst = gl_InstanceIndex;
}
)glsl";
const char* kCloudFragment = R"glsl(#version 450
layout(set=0, binding=0) uniform Globals {
    vec4 view0, view1, view2, proj, sun, lit, sky, params, tune, extra;
} g;
struct Inst { vec4 center, shape, base; };
layout(set=0, binding=1) uniform Instances { Inst i[320]; } inst;
layout(set=0, binding=2) uniform sampler3D noiseTex;
layout(location=0) in vec3 vWorld;
layout(location=1) flat in int vInst;
layout(location=0) out vec4 outColor;
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }
float hash1(float n) { return fract(sin(n) * 43758.5453); }
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// ---- shared toon shading: 3 hard bands, warm lit / cool shadow with a height-gradient ambient
// (sky above, sea bounce below: Cumulus' ambient idea), sun-side rim and a forward-scatter silver lining
struct Shade { vec3 lit, mid, shadow, rim; };
Shade palette(vec3 base, float far) {
    Shade s;
    float storm = g.sky.w, warm = g.lit.w;
    float luma = dot(base, vec3(0.3, 0.59, 0.11));
    // whiter at noon, the palette's own colour (gold, pink) when the sun is low
    float sat = mix(g.tune.y, 1.0, warm);
    vec3 lit = mix(vec3(luma), base, sat);
    // full brightness by day; at night the palette's own (dark) level
    float mx = max(max(base.r, max(base.g, base.b)), 1e-3);
    lit = lit / max(max(lit.r, max(lit.g, lit.b)), 1e-3) * mix(min(mx * 1.15, 1.0), 1.0, g.sun.w);
    lit *= 1.0 - 0.45 * storm;
    vec3 skyAmb = mix(g.sky.rgb, vec3(1.0), 0.42 * g.sun.w);  // no white ambient at night
    s.lit = lit;
    s.shadow = mix(lit * g.tune.z, skyAmb * mix(0.95, 0.55, storm), 0.48);
    s.mid = mix(lit * g.tune.w, skyAmb, 0.10);
    // low sun: golden-pink lit side, lavender shadows (warm/cool contrast)
    s.lit = mix(s.lit, s.lit * vec3(1.0, 0.82, 0.62) + vec3(0.0, 0.02, 0.04), warm * 0.8);
    s.shadow = mix(s.shadow, s.shadow * vec3(0.80, 0.66, 0.92), warm);
    s.mid = mix(s.mid, s.mid * vec3(0.95, 0.80, 0.82), warm * 0.5);
    s.mid = mix(s.mid, s.mid * vec3(1.0, 0.86, 0.80), warm * 0.8);
    s.rim = min(mix(lit * 1.08 + 0.04, vec3(1.0, 0.86, 0.55), warm), vec3(1.0));
    vec3 haze = mix(g.sky.rgb, vec3(1.0), 0.55 * g.sun.w);
    float f = far * 0.32;
    s.lit = mix(s.lit, haze, f), s.mid = mix(s.mid, haze, f), s.shadow = mix(s.shadow, haze, f), s.rim = mix(s.rim, haze, f * 0.5);
    return s;
}
vec3 toon(Shade c, vec3 n, vec3 nDetail, vec3 rd, float height, float shadowTerm) {
    vec3 L = g.sun.xyz;
    float ndl = dot(n, L) * 0.5 + 0.5;
    float hw = 0.4 * (1.0 - 0.75 * g.lit.w);  // a low sun lights the sides it faces, not every top
    float lightv = (ndl * (1.0 - hw) + height * hw) * mix(0.45, 1.0, shadowTerm);
    float sw = g.params.x;
    vec3 col = mix(c.shadow, c.mid, smoothstep(0.26 - sw, 0.26 + sw, lightv));
    col = mix(col, c.lit, smoothstep(0.40 - sw, 0.40 + sw, lightv));
    // sea bounce under the base: cooler, a little lighter than the side shadow
    float down = smoothstep(-0.3, -0.8, n.y);
    col = mix(col, c.shadow * vec3(0.94, 1.0, 1.06), down * 0.7);
    // rim and silver lining: silhouette x light side; much stronger looking toward a low sun
    // silhouette from the smooth normal (outer edges only), nudged by the detail normal for a frayed line
    float facing = saturate(dot(normalize(mix(n, nDetail, 0.25)), -rd));
    float toward = saturate(dot(rd, L));
    float fwd = 0.5 + 3.0 * pow(toward, 2.0) * (0.3 + 0.7 * g.lit.w);
    float rim = pow(1.0 - facing, 2.5) * saturate(dot(n, L) + 0.55) * fwd * g.sun.w;
    col = mix(col, c.rim, smoothstep(0.32 - sw, 0.32 + sw, rim));
    return col;
}

// ---- C: one impostor per cloud, raymarched against smooth-unioned spheres
float blobs(vec3 p, float hs, float seed) {
    float d = 1e9;
    for (int i = 0; i < 4; i++) {  // base row
        float fi = float(i);
        float x = -0.85 + fi * 0.57 + (hash1(seed + fi) - 0.5) * 0.25;
        float r = 0.42 + hash1(seed + fi + 7.0) * 0.16;
        float z = (hash1(seed + fi + 13.0) - 0.5) * 0.35;
        d = smin(d, length(p - vec3(x, r * 0.75 * hs, z)) - r, 0.12);
    }
    for (int i = 0; i < 3; i++) {  // middle
        float fi = float(i);
        float x = -0.5 + fi * 0.5 + (hash1(seed + fi + 21.0) - 0.5) * 0.3;
        float r = 0.36 + hash1(seed + fi + 29.0) * 0.18;
        float y = (0.55 + hash1(seed + fi + 37.0) * 0.25) * hs;
        d = smin(d, length(p - vec3(x, y, (hash1(seed + fi + 41.0) - 0.5) * 0.3)) - r, 0.1);
    }
    for (int i = 0; i < 2; i++) {  // crown
        float fi = float(i);
        float cx = (hash1(seed + 53.0 + fi) - 0.5) * 0.7;
        d = smin(d, length(p - vec3(cx, (0.95 + hash1(seed + 59.0 + fi) * 0.25) * hs, 0.0)) -
                    (0.26 + hash1(seed + 61.0 + fi) * 0.12), 0.1);
    }
    return max(d, -(p.y - 0.06));  // flat base
}
float sdf(vec3 p, float hs, float seed, float amp, float ero) {
    float d = blobs(p, hs, seed) + ero;
    if (d > 0.35) return d;
    vec3 np = p * 0.8 + vec3(seed * 0.37, 0.0, seed * 0.11);
    float n = texture(noiseTex, np).g * 0.6 + texture(noiseTex, np * 2.7).r * 0.4;
    return d + (0.5 - n) * amp;
}
void main() {
    Inst c = inst.i[vInst];
    float hs = c.shape.x, seed = c.shape.y, amp = g.params.y;
    float ero = (1.0 - saturate(c.shape.z * 1.25)) * 0.55;  // the game's fade erodes the shape
    vec3 rd = normalize(vWorld);
    vec3 ro = -c.center.xyz / c.center.w;
    vec3 bc = vec3(0, 0.6 * hs, 0);
    float br = 1.5 * max(1.0, hs);
    vec3 oc = ro - bc;
    float b = dot(oc, rd), cc = dot(oc, oc) - br * br, h = b * b - cc;
    if (h < 0.0) discard;
    h = sqrt(h);
    float t = max(-b - h, 0.0), t1 = -b + h;
    float dmin = 1e9, tmin = t;
    bool hit = false;
    vec3 p;
    float eps = 0.0015 * t;
    for (int i = 0; i < int(g.extra.x) && t < t1; i++) {
        p = ro + rd * t;
        float d = sdf(p, hs, seed, amp, ero);
        if (d < dmin) { dmin = d; tmin = t; }
        if (d < eps) { hit = true; break; }
        t += max(d * 0.8, 0.008);
    }
    Shade pal = palette(c.base.rgb, c.shape.w);
        if (!hit) {  // frayed fringe just outside the surface
        float fw = g.tune.x;
        if (dmin > fw) discard;
        vec3 q = ro + rd * tmin;
        float n = texture(noiseTex, q * 1.9 + seed).g;
        float a = (1.0 - dmin / fw) * smoothstep(0.4, 0.7, n);
        if (a < 0.05) discard;
        a = saturate(a * 2.0) * saturate(c.shape.z * 4.0);
        vec3 col = mix(pal.mid, pal.lit, saturate(q.y / (1.2 * hs)));
        outColor = vec4(col * a, a);
        return;
    }
    vec3 nb = normalize(vec3( 1, -1, -1) * blobs(p + vec3( 1, -1, -1) * 0.06, hs, seed) +
                          vec3(-1, -1,  1) * blobs(p + vec3(-1, -1,  1) * 0.06, hs, seed) +
                          vec3(-1,  1, -1) * blobs(p + vec3(-1,  1, -1) * 0.06, hs, seed) +
                          vec3( 1,  1,  1) * blobs(p + vec3( 1,  1,  1) * 0.06, hs, seed));
    vec3 nd = nb;
    if (g.extra.y < 0.5) {
        const float e = 0.01;
        nd = normalize(vec3( 1, -1, -1) * sdf(p + vec3( 1, -1, -1) * e, hs, seed, amp, ero) +
                       vec3(-1, -1,  1) * sdf(p + vec3(-1, -1,  1) * e, hs, seed, amp, ero) +
                       vec3(-1,  1, -1) * sdf(p + vec3(-1,  1, -1) * e, hs, seed, amp, ero) +
                       vec3( 1,  1,  1) * sdf(p + vec3( 1,  1,  1) * e, hs, seed, amp, ero));
    }
    vec3 n = normalize(mix(nb, nd, 0.15));
    float sh = 1.0;
    if (g.params.w > 0.0) {  // self-shadow: soft-shadow march toward the light through the blob field
        float ts = 0.06;
        for (int i = 0; i < int(g.params.w); i++) {
            float dd = blobs(p + g.sun.xyz * ts, hs, seed);
            sh = min(sh, saturate(6.0 * dd / ts + 0.5));
            ts += max(dd, 0.08);
        }
    }
    vec3 col = toon(pal, n, nd, rd, saturate(p.y / (1.1 * hs)), sh);
    float a = saturate(c.shape.z * 6.0);
    outColor = vec4(col * a, a);
}

)glsl";
constexpr uint32_t kMaxInstances = 320;  // a 16 KiB uniform block
struct CloudResources {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImage noise = VK_NULL_HANDLE;
    VkDeviceMemory noiseMemory = VK_NULL_HANDLE;
    VkImageView noiseView = VK_NULL_HANDLE;
    bool noiseReady = false;
    std::unordered_map<std::string, VkPipeline> pipelines;
} cres;
uint32_t g_cloudsSeq = 0;      // the composite was seen: draw the clouds before the next draw
uint32_t g_cloudsPicture = 0;  // ... into this picture (address)

void cloud_resources() {
    if (cres.device && cres.device != R.device) cres = {};
    cres.device = R.device;
    if (!cres.set) {
        VkDescriptorSetLayoutBinding b[3]{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 3;
        ci.pBindings = b;
        check_vk(vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &cres.set), "create cloud descriptors");
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1;
        li.pSetLayouts = &cres.set;
        check_vk(vkCreatePipelineLayout(R.device, &li, nullptr, &cres.layout), "create cloud layout");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.minFilter = si.magFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        check_vk(vkCreateSampler(R.device, &si, nullptr, &cres.sampler), "create cloud sampler");
    }
    if (!cres.noise) {
        const uint32_t N = cloud_noise::kSize;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_3D;
        ci.format = VK_FORMAT_R8G8_UNORM;
        ci.extent = {N, N, N};
        ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check_vk(vkCreateImage(R.device, &ci, nullptr, &cres.noise), "create cloud noise");
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(R.device, cres.noise, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check_vk(vkAllocateMemory(R.device, &ai, nullptr, &cres.noiseMemory), "cloud noise memory");
        check_vk(vkBindImageMemory(R.device, cres.noise, cres.noiseMemory, 0), "bind cloud noise");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = cres.noise;
        vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
        vi.format = ci.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check_vk(vkCreateImageView(R.device, &vi, nullptr, &cres.noiseView), "cloud noise view");
    }
    if (!cres.noiseReady) {  // upload once, outside any pass
        const uint32_t N = cloud_noise::kSize;
        std::vector<uint8_t> data = cloud_noise::rg8();
        end_encoder();
        VkCommandBuffer cmd = command_buffer();
        UploadSlice staging = allocate_upload(data.size(), 16);
        std::memcpy(staging.mapped, data.data(), data.size());
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = cres.noise;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy copy{};
        copy.bufferOffset = staging.offset;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {N, N, N};
        record::vkCmdCopyBufferToImage(cmd, staging.buffer, cres.noise, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        cres.noiseReady = true;
        LOG("[clouds] Vulkan: noise uploaded");
    }
}

// a pipeline for the open pass's attachments (the composite's picture, its other targets, its depth)
VkPipeline cloud_pipeline(bool depthTested) {
    std::string key = depthTested ? "c" : "cs";
    uint32_t ncolor = 0;
    VkFormat colorFormats[8]{};
    for (uint32_t i = 0; i < 8; i++)
        if (R.passColors[i]) { colorFormats[i] = R.passColors[i]->fmt.pixel; ncolor = i + 1; }
    for (uint32_t i = 0; i < ncolor; i++) key += "/" + std::to_string(int(colorFormats[i]));
    Surface* depth = R.passDepth;
    if (depth) key += "/d" + std::to_string(int(depth->fmt.pixel)) + (depth->fmt.stencil ? "s" : "");
    if (auto it = cres.pipelines.find(key); it != cres.pipelines.end()) return it->second;
    VkShaderModule vs = sky_module(kCloudVertex, true), fs = sky_module(kCloudFragment, false);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = depth && depthTested ? VK_TRUE : VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend[8]{};
    blend[0].blendEnable = VK_TRUE;  // premultiplied
    blend[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend[0].colorBlendOp = VK_BLEND_OP_ADD;
    blend[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].alphaBlendOp = VK_BLEND_OP_ADD;
    blend[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = ncolor;
    bs.pAttachments = blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = ncolor;
    rendering.pColorAttachmentFormats = colorFormats;
    if (depth) {
        rendering.depthAttachmentFormat = depth->fmt.pixel;
        rendering.stencilAttachmentFormat = depth->fmt.stencil ? depth->fmt.pixel : VK_FORMAT_UNDEFINED;
    }
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_targets(ci, rendering);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &dss;
    ci.pColorBlendState = &bs;
    ci.pDynamicState = &ds;
    ci.layout = cres.layout;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe);
    vkDestroyShaderModule(R.device, vs, nullptr);
    vkDestroyShaderModule(R.device, fs, nullptr);
    if (r != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    LOG("[clouds] Vulkan pipeline %s%s", key.c_str(), pipe ? "" : " failed");
    cres.pipelines.emplace(key, pipe);
    return pipe;
}

// into the open pass (the composite's), then a fresh pass for the game's next draw
// `target`: the address the open pass must be drawing to; `depthTested`: the full-resolution picture
// (sky-only test) rather than the half-resolution sky buffer (Lite)
void draw_clouds(uint32_t seq, uint32_t target, bool depthTested) {
    cloud_resources();  // may end the pass for the one-time noise upload
    if (!R.rendering || !R.passTracked || !R.passColors[0] || R.passColors[0]->addr != target) {
        static uint64_t missed = 0;
        if (missed++ % 600 == 0) LOG("[clouds] Vulkan: the composite pass is closed (%llu times)", (unsigned long long)missed);
        return;
    }
    VkPipeline pipe = cloud_pipeline(depthTested);
    if (!pipe) return;
    const uint32_t w = R.passWidth, h = R.passHeight;
    cloud_scene::Globals g;
    std::vector<cloud_scene::Inst> inst;
    cloud_scene::build(*cloud_proto::snapshot(seq), float(w) / float(h), cloud_proto::mode(), depthTested && R.passDepth, R.frame, g, inst);
    if (inst.empty()) return;
    if (inst.size() > kMaxInstances) inst.erase(inst.begin(), inst.end() - kMaxInstances);  // keep the nearest
    UploadSlice gu = allocate_upload(sizeof g, 256), iu = allocate_upload(sizeof(cloud_scene::Inst) * kMaxInstances, 256);
    std::memcpy(gu.mapped, &g, sizeof g);
    std::memcpy(iu.mapped, inst.data(), sizeof(cloud_scene::Inst) * inst.size());
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptor_pool();
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &cres.set;
    check_vk(vkAllocateDescriptorSets(R.device, &alloc, &set), "allocate cloud descriptors");
    VkDescriptorBufferInfo b0{gu.buffer, gu.offset, sizeof g}, b1{iu.buffer, iu.offset, sizeof(cloud_scene::Inst) * kMaxInstances};
    VkDescriptorImageInfo img{cres.sampler, cres.noiseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr[3]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    for (int i = 0; i < 3; i++) wr[i].dstSet = set, wr[i].dstBinding = i, wr[i].descriptorCount = 1;
    wr[0].descriptorType = wr[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr[0].pBufferInfo = &b0, wr[1].pBufferInfo = &b1;
    wr[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, wr[2].pImageInfo = &img;
    vkUpdateDescriptorSets(R.device, 3, wr, 0, nullptr);
    VkCommandBuffer cmd = command_buffer();
    VkViewport viewport{0, 0, float(w), float(h), 0, 1};
    VkRect2D area{{0, 0}, {w, h}};
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &area);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cres.layout, 0, 1, &set, 0, nullptr);
    static const int repeat = getenv("WWHD_CLOUDS_REPEAT") ? std::max(1, atoi(getenv("WWHD_CLOUDS_REPEAT"))) : 1;
    for (int r = 0; r < repeat; r++) record::vkCmdDraw(cmd, 4, uint32_t(inst.size()), 0, 0);
    end_encoder();  // the game's next draw must not trust its cached pipeline / dynamic state
}
}  // namespace

// draw.cpp, first thing: the game's own cloud cards are skipped while toon clouds replace them
bool clouds_skip_card() {
    if (stereo::frame.mode != stereo::Off) return false;  // stereo 3D keeps the game's cards (see clouds_before_draw)
    const int m = cloud_proto::mode();
    if (!cloud_proto::g_active_seq || (m != cloud_proto::kSdf && m != cloud_proto::kHide)) return false;
    // Lite: the clouds go into the half-resolution sky buffer in place of the cards (a quarter of the pixels);
    // High: into the full-resolution picture after the composite (clouds_before_draw)
    if (m == cloud_proto::kSdf && cloud_proto::tier() == 1 && g_cloudsWanted && R.passColors[0] && R.passColors[0]->addr == g_skyAddr) {
        const uint32_t seq = g_cloudsWanted;
        g_cloudsWanted = 0;
        draw_clouds(seq, g_skyAddr, false);
    }
    return true;
}


// draw.cpp, once a draw's targets are known and before its textures are bound: notices the sky
// composite (it samples the sky buffer), draws the toon clouds right after it, and applies the pending
// cloud shadows before the picture is read
void clouds_before_draw(const uint32_t* r, const vk::Shader* ps, Surface* color0, uint32_t slice0, Surface* depth) {
    // stereo 3D draws every pass once per eye; the toon clouds and shadows are mono for now (limitation)
    if (g_skyFrame != R.frame || stereo::frame.mode != stereo::Off) return;
    if (g_cloudsSeq) {  // the previous draw was the composite: its pass is still open
        uint32_t seq = g_cloudsSeq;
        g_cloudsSeq = 0;
        draw_clouds(seq, g_cloudsPicture, true);
    }
    const bool shadows = render::cloud_shadows();
    if ((!shadows && !g_cloudsWanted) || !ps || !ps->dec) return;
    bool samplesSky = false, readsPicture = false;
    for (int u = 0; u < ps->dec->textureUnitListCount; u++) {
        const uint32_t unit = ps->dec->textureUnitList[u];
        const uint32_t addr = r[Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + unit * 7 + 2] << 8;
        if (addr && addr == g_skyAddr) samplesSky = true;
        if (g_shadowSeq && addr && addr == g_pictureAddr) readsPicture = true;
    }
    if (g_shadowSeq && !color0 && depth && depth->addr == g_pictureDepthAddr) readsPicture = true;
    static uint64_t composites = 0, applied = 0;
    if (readsPicture && g_shadowFrame == R.frame) shadows_now(), applied++;
    if (samplesSky) composites++;
    if (samplesSky && composites % 300 == 0 && shadows)
        LOG("[shadows] Vulkan: composites %llu, applied %llu", (unsigned long long)composites, (unsigned long long)applied);
    if (samplesSky && color0 && g_cloudsWanted) {
        g_cloudsSeq = g_cloudsWanted, g_cloudsPicture = color0->addr;
        g_cloudsWanted = 0;
    }
    if (samplesSky && shadows && color0 && depth && g_skySeq) {
        g_picture = color0, g_pictureAddr = color0->addr, g_pictureSlice = slice0;
        g_pictureDepth = depth, g_pictureDepthAddr = depth->addr;
        g_shadowSeq = g_skySeq, g_shadowFrame = R.frame;
        g_skySeq = 0;
    }
}

}  // namespace gfxvk
