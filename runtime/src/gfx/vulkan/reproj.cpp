// 60 fps (generated): the game runs at its own 30 frames a second, and after each real frame an
// in-between one is made on the GPU and shown half a frame later.
// From Nycolazs/WindWakerHD-Android-Plus (vk/reproj.cpp 84b0ff2) via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0: the shaders kVertex, kWarp and kHudMask, the generated camera (half the
// last rotation and movement), the cut rule and the scene copy at the first TV layout. The capture
// (framegen.cpp, OP_FRAMEGEN_CAMERA / OP_FRAMEGEN_LAYOUT) and the present step (backend.cpp
// present_generated) are this port's own; their present side rode on a Lossless Scaling present
// thread, which is not part of this port.
//
// The in-between frame is the real one reprojected with the scene's depth to the camera moved on by
// half its last step (eye and turn: the camera orbits Link, so a turn alone would move Link
// sideways). The HUD stays where it is: at the first HUD layout drawn on the TV picture the scene
// (3D and its post-processing) is copied aside, and pixels where the finished picture differs from it
// are HUD. Frame interpolation draws every frame twice on the game thread; this costs a GPU pass
// instead, so it holds 60 where the game thread can't.
//
// In this port the in-between picture is made in the TV picture's own space: the published TV scan
// is warped into a scan-sized image, which swap() presents through the normal window composition
// (scaling, FXAA, the settings overlay), held until half a real frame after the real one
// (backend.cpp present_generated, the held-present path of even frame delivery).
//
// Not here yet (the fork's later work): objects' own movement (kFlow / kFilter / kSelect / kSpread),
// the warped mesh (kGridVertex / kGridFragment / kCompose), the HUD's coverage from SPIR-V variants
// (hud_coverage.h, kWarpCov), and the depth of an earlier 3D pass (keep_depth / kDepthMerge). Objects
// therefore move with the camera only.
//
// Switches (read once): WWHD_FRAMEGEN=1 on (framegen.h); WWHD_FRAMEGEN_TINT=1 tints the in-between
// frames red; WWHD_FRAMEGEN_FOLLOW=0 moves what the camera follows with the camera too;
// WWHD_FRAMEGEN_HUD=x HUD difference threshold (default 3/255); WWHD_FRAMEGEN_DEBUG=1 shows the HUD
// mask (magenta), =2 the scene's depth (log scale) in the in-between frames.
#include "reproj.h"
#include "reproj_camera.h"

#include "backend.h"
#include "deferred_dispatch.h"
#include "pass.h"
#include "shaders.h"
#include "stereo.h"
#include "framegen.h"
#include "runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace gfxvk::reproj {
namespace {

const char* kVertex = R"glsl(#version 450
layout(location=0) out vec2 uv;
void main() {
 vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
 uv = p;
 gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

// TV picture uv -> the generated TV picture. p.rect: where the TV picture lies in the target (uv; here
// always the whole of it); t: tanX, tanY, HUD difference threshold, tint; z: near, far, depth usable,
// objects' movement (off: no flow pass yet); r0..r2: rows of H (the generated camera's axes in the real
// camera's space), .w: its eye there; a: distance to the point the camera looks at, follows on, unused,
// debug view (1: HUD mask, 2: depth)
const char* kWarp = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D comp;      // the finished TV picture (the published scan)
layout(set=0,binding=1) uniform sampler2D depthImg;  // the scene's depth (TV picture space)
layout(set=0,binding=2) uniform sampler2D hudMask;   // where the HUD is, a quarter of the TV picture's size (kHudMask)
layout(set=0,binding=3) uniform sampler2D unused3;
layout(set=0,binding=4) uniform sampler2D flowImg;   // objects' own movement per 16x16 block (kFlow; not made yet)
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
// the 2x2 depth texels around q, as textureGather would return them, read one by one: a gather on
// the game's depth-stencil image froze an Adreno 650 (Galaxy S20 FE) until it restarted and gave
// stray lines on others (LASRecomp-Android-Plus 08bbaef); plain fetches work on every driver
vec4 gatherDepth(vec2 q) {
 ivec2 sz = textureSize(depthImg, 0), b = ivec2(floor(q * vec2(sz) - 0.5));
 ivec2 lo = ivec2(0), hi = sz - 1;
 return vec4(texelFetch(depthImg, clamp(b + ivec2(0, 1), lo, hi), 0).r, texelFetch(depthImg, clamp(b + ivec2(1, 1), lo, hi), 0).r,
             texelFetch(depthImg, clamp(b + ivec2(1, 0), lo, hi), 0).r, texelFetch(depthImg, clamp(b, lo, hi), 0).r);
}
// the nearest depth of the 2x2 pixels around q: the game's outlines around characters lie on the
// background's depth and would otherwise move with the background (depth range [-1,1] as GX2)
float viewDepth(vec2 q) {
 vec4 m = gatherDepth(q);
 float d = min(min(m.x, m.y), min(m.z, m.w)), n = p.z.x, f = p.z.y;
 return 2.0 * n * f / max((f + n) - (d * 2.0 - 1.0) * (f - n), 1e-6);
}
// how much the pixel at q moves with the camera, 0..1: what lies about as far as the point the
// camera looks at (a.x; a.y: on). The camera follows Link (and the title's boat): they and what is
// near them keep their place on the screen while the camera moves, so moving them with the camera's
// movement shook them back and forth every frame; the background, further away, is reprojected.
// (z: the view depth at q, read once per step of the motion)
float follows(float z, vec2 q) {
 if (p.a.y < 0.5 || p.a.x <= 0.0) return 0.0;
 float r = abs(log(max(z, 1e-3) / p.a.x));
 // only around the point the camera looks at (the screen's centre): the world at Link's distance
 // elsewhere (a boat beside him, a wall) moves with the camera like the rest
 vec2 d = (q - 0.5) * vec2(p.t.x / p.t.y, 1.0);
 return (1.0 - smoothstep(0.35, 0.75, r)) * (1.0 - smoothstep(0.12, 0.24, length(d)));
}
// where the real picture's pixel q (TV uv), at view depth z, is in the generated picture
vec2 toGenCam(vec2 q, float z);
vec2 toGen(vec2 q, float z, float fw) { return mix(toGenCam(q, z), q, fw); }
vec2 toGenCam(vec2 q, float z) {
 vec3 P = vec3((q.x * 2.0 - 1.0) * p.t.x, (1.0 - q.y * 2.0) * p.t.y, -1.0) * z - vec3(p.r0.w, p.r1.w, p.r2.w);
 vec3 g = p.r0.xyz * P.x + p.r1.xyz * P.y + p.r2.xyz * P.z;
 if (g.z > -1e-4) return q;
 return vec2(g.x / (-g.z) / p.t.x * 0.5 + 0.5, 0.5 - g.y / (-g.z) / p.t.y * 0.5);
}
// half the movement of the object at q beyond the camera's (TV uv), where q's depth is its block's
// (characters and enemies: with the camera's movement alone they stood still in the in-between frame,
// then jumped: 30 fps motion)
vec2 objectMove(vec2 q, float z, float fw) {
 if (p.z.w < 0.5) return vec2(0.0);
 vec4 f = textureLod(flowImg, q, 0.0);
 if (f.w < 0.5) return vec2(0.0);
 float bz = p.z.x * exp(f.z * log(p.z.y / p.z.x));
 if (abs(z - bz) > 0.15 * bz) return vec2(0.0);
 return f.xy * 0.5 * (1.0 - fw);  // (what follows the camera already keeps its place)
}
// the HUD and a few pixels around it (glows, outlines and their soft edges): the mask's texels cover
// 4x4 TV pixels, its bilinear tap anything within one texel of them
bool hud(vec2 q) { return textureLod(hudMask, q, 0.0).r > 0.01; }
// the picture's pixel nearest to q, unfiltered: bilinear resampling softened fine detail (water,
// grass, distant edges) in every other frame, which shimmered; the movement is rounded to whole pixels
vec4 window(vec2 q) {
 ivec2 sz = textureSize(comp, 0);
 return texelFetch(comp, clamp(ivec2((p.rect.xy + q * p.rect.zw) * vec2(sz)), ivec2(0), sz - 1), 0);
}
void main() {
 vec2 tv = (uv - p.rect.xy) / p.rect.zw;
 if (p.a.w > 1.5) {  // debug (WWHD_FRAMEGEN_DEBUG=2): the scene's view depth, log scale from near to far
  float z = viewDepth(tv);
  result = vec4(vec3(clamp(log(max(z, p.z.x) / p.z.x) / log(p.z.y / p.z.x), 0.0, 1.0)), 1.0);
  return;
 }
 if (p.z.z < 0.5 || any(lessThan(tv, vec2(0.0))) || any(greaterThan(tv, vec2(1.0))) || hud(tv)) {
  result = textureLod(comp, uv, 0.0);  // outside the TV picture, without a depth buffer, or the HUD: unmoved
  if (p.a.w > 0.5 && hud(tv)) result.rgb = mix(result.rgb, vec3(1.0, 0.0, 1.0), 0.5);  // (debug 1: the HUD mask)
  return;
 }
 vec2 q = tv;  // the source pixel that lands here: a few fixed-point steps of the motion
 vec2 raw = tv;
 for (int i = 0; i < 3; i++) {
  float z = viewDepth(q), fw = follows(z, q);
  raw = tv - (toGen(q, z, fw) - q) - objectMove(q, z, fw);
  q = clamp(raw, vec2(0.0), vec2(1.0));
 }
 // it would come from outside the real picture (the edge while the camera turns): the edge of the
 // picture along the movement (q is clamped to it). The real picture's own pixel there, as before,
 // showed what lies next to the edge twice (a boat, a sign) in a band as wide as half the camera's
 // step: up to a tenth of the screen in a fast turn
 if (hud(q)) {  // the scene there is hidden under the HUD: the nearest visible scene along the motion
  vec2 px = 1.0 / vec2(textureSize(depthImg, 0));
  vec2 d = q - tv;
  float l = length(d / px);
  d = l > 0.5 ? d / l : vec2(px.x, 0.0);
  for (int k = 1; k <= 8; k++) {
   vec2 a = clamp(q + d * float(k * 12), vec2(0.0), vec2(1.0));
   if (!hud(a)) { q = a; break; }
   a = clamp(q - d * float(k * 12), vec2(0.0), vec2(1.0));
   if (!hud(a)) { q = a; break; }
  }
 }
 result = window(q);
 if (p.t.w > 0.5) result.rgb *= vec3(1.0, 0.6, 0.6);  // (debug: WWHD_FRAMEGEN_TINT=1) generated frames tinted
}
)glsl";

// where the HUD is (WWHD: the finished TV picture differs from the scene there), one texel per 4x4 TV
// pixels: 1 if any of them differs by more than t.z
const char* kHudMask = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D scanImg;
layout(set=0,binding=1) uniform sampler2D sceneImg;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
void main() {
 ivec2 sz = textureSize(scanImg, 0), b = ivec2(uv * vec2(sz) / 4.0) * 4;
 float m = 0.0;
 for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
  ivec2 c = min(b + ivec2(x, y), sz - 1);
  vec3 d = abs(texelFetch(scanImg, c, 0).rgb - texelFetch(sceneImg, c, 0).rgb);
  m = max(m, max(max(d.r, d.g), d.b));
 }
 result = vec4(m > p.t.z ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
)glsl";

struct Push { float r0[4], r1[4], r2[4], t[4], z[4], rect[4], a[4]; };
static_assert(sizeof(Push) == 112, "push constants: 7 vec4");

struct Cam {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // camera-to-world: columns right, up, back (the camera looks along -z)
    float eye[3] = {0, 0, 0};
    float target = 0;  // distance from the eye to the point the camera looks at (Link, the title's boat)
    float tanY = 0, znear = 1, zfar = 100000;
    uint32_t flags = 0;
    uint64_t frame = ~0ull;
    bool valid = false;
};

struct State {
    Cam cur, prev;
    // the TV picture before the HUD, and the scene's depth buffer, of this frame
    std::unique_ptr<Surface> scene;
    uint64_t sceneFrame = ~0ull;
    uint32_t depthAddr = 0;
    // the in-between picture (a scan of the TV picture's size and format) and the HUD mask
    std::unique_ptr<Surface> gen, mask;
    // pipelines: the generation passes' layout (5 sampled images, push constants), by fragment shader and format
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE, linear = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkPipeline> pipelines;
    bool failed = false;  // a pipeline failed to build: no more attempts (logged once)
    // the real frames' interval (ns): exponential average of the time between presented real frames
    int64_t lastReal = 0;
    double interval = 0;
    // counters: since the last perf line (window) and in all
    uint64_t generated = 0, early = 0, skipped[kSkipCount] = {};
    uint64_t totalGenerated = 0, totalEarly = 0, totalSkipped[kSkipCount] = {}, swaps = 0;
} S;

const char* const kSkipNames[kSkipCount] = {"other 60 fps mode", "stereo", "present path", "not shown", "fast", "no camera",
                                             "no scene", "no depth", "size", "cut", "still", "failed"};

void count_skip(Skip why) {
    if (why < 0 || why >= kSkipCount) return;
    S.skipped[why]++;
    S.totalSkipped[why]++;
}

// a game surface (no lookup side effects: find_or_create_surface would claim sizes and adopt aliases):
// 2D, one level and layer, of guest size w x h (0: any); the newest written
Surface* find_surface(uint32_t addr, uint32_t w, uint32_t h, bool depth) {
    if (!addr) return nullptr;
    Surface* best = nullptr;
    auto range = R.surfaces.equal_range(addr);
    for (auto it = range.first; it != range.second; ++it) {
        Surface* s = it->second.get();
        if (s->isDepth != depth || !s->image || !s->view || s->evicted || s->imageType != VK_IMAGE_TYPE_2D || s->mips != 1 || s->arrayLayers != 1)
            continue;
        if (w && (s->width != w || s->height != h)) continue;
        if (!best || s->writeSeq > best->writeSeq) best = s;
    }
    return best;
}

bool same_extent(const Surface* a, const Surface* b) {
    return a && b && a->extent.width == b->extent.width && a->extent.height == b->extent.height;
}

// one of this file's own images (not a game surface: never in R.surfaces), made again when the size or
// format changes (the old one is retired once the GPU is done with it)
bool ensure_image(std::unique_ptr<Surface>& s, uint32_t format, const FormatInfo& fmt, uint32_t w, uint32_t h, VkImageUsageFlags need) {
    if (s && s->image && s->fmt.pixel == fmt.pixel && s->extent.width == w && s->extent.height == h) return (s->usage & need) == need;
    if (s) destroy_surface_image(s.get());
    s = std::make_unique<Surface>();
    s->width = w;
    s->height = h;
    s->format = format;
    s->fmt = fmt;
    s->scale = 1;
    try {
        create_surface_image(s.get(), false, {w, h, 1});
    } catch (const std::exception& e) {
        LOG("[framegen] image %ux%u format %d failed: %s", w, h, int(fmt.pixel), e.what());
        s.reset();
        return false;
    }
    return (s->usage & need) == need;
}

VkShaderModule shader_module(const char* source, bool vertex) {
    std::string error;
    auto words = vk::compile_glsl(source, vertex, &error);
    if (words.empty() || !error.empty()) {
        LOG("[framegen] shader failed: %s", error.c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(R.device, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

bool make_layout() {
    if (S.device && S.device != R.device) {  // (a new device: the old objects went with it)
        S.dsl = VK_NULL_HANDLE;
        S.layout = VK_NULL_HANDLE;
        S.nearest = S.linear = VK_NULL_HANDLE;
        S.pipelines.clear();
    }
    S.device = R.device;
    if (S.layout) return true;
    VkDescriptorSetLayoutBinding b[5];
    for (uint32_t i = 0; i < 5; i++) b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.bindingCount = 5;
    dci.pBindings = b;
    if (vkCreateDescriptorSetLayout(R.device, &dci, nullptr, &S.dsl) != VK_SUCCESS) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &S.dsl;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(R.device, &lci, nullptr, &S.layout) != VK_SUCCESS) return false;
    for (VkFilter filter : {VK_FILTER_NEAREST, VK_FILTER_LINEAR}) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = filter;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0;
        if (vkCreateSampler(R.device, &si, nullptr, filter == VK_FILTER_LINEAR ? &S.linear : &S.nearest) != VK_SUCCESS) return false;
    }
    return true;
}

// a full-screen pass's pipeline for fragment shader `src` drawing into `format`
VkPipeline pipeline_for(const char* src, VkFormat format) {
    const uint64_t key = uint64_t(uintptr_t(src)) * 1000003u ^ uint64_t(format);
    if (auto it = S.pipelines.find(key); it != S.pipelines.end()) return it->second;
    VkShaderModule vs = shader_module(kVertex, true), fs = shader_module(src, false);
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vs && fs) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i = 0; i < 2; i++) {
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
        blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &blend;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_targets(ci, rendering);  // (a render pass on devices without dynamic rendering)
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
        ci.layout = S.layout;
        if (vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe) != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    }
    if (vs) vkDestroyShaderModule(R.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(R.device, fs, nullptr);
    if (!pipe) {
        LOG("[framegen] pipeline for %s (format %d) failed: no in-between frames", src == kWarp ? "the warp" : "the HUD mask", int(format));
        S.failed = true;
        return VK_NULL_HANDLE;
    }
    LOG("[framegen] pipeline for %s (format %d)", src == kWarp ? "the warp" : "the HUD mask", int(format));
    S.pipelines.emplace(key, pipe);
    return pipe;
}

struct Input {
    Surface* image;
    VkSampler sampler;
};
// one full-screen pass of `pipe` into `target`; up to 5 sampled inputs (unused bindings get the first),
// each already in SHADER_READ_ONLY_OPTIMAL
void run_pass(Surface& target, VkPipeline pipe, const Input* in, int n, const Push& pc) {
    transition_image(&target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkCommandBuffer cmd = command_buffer();
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = R.descriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &S.dsl;
    vk_check(vkAllocateDescriptorSets(R.device, &allocation, &set), "framegen descriptor set");
    VkDescriptorImageInfo ii[5];
    VkWriteDescriptorSet wr[5]{};
    for (int k = 0; k < 5; k++) {
        const Input& x = in[k < n ? k : 0];
        ii[k] = {x.sampler, x.image->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        wr[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[k].dstSet = set;
        wr[k].dstBinding = uint32_t(k);
        wr[k].descriptorCount = 1;
        wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[k].pImageInfo = &ii[k];
    }
    vkUpdateDescriptorSets(R.device, 5, wr, 0, nullptr);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = target.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // (every pixel is written)
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkExtent2D extent{target.extent.width, target.extent.height};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    begin_pass(cmd, rendering, [&] { return color_pass(target.fmt.pixel, target.layout); });
    R.rendering = true;
    R.passTracked = false;
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &scissor);
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, S.layout, 0, 1, &set, 0, nullptr);
    record::vkCmdPushConstants(cmd, S.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    record::vkCmdDraw(cmd, 3, 1, 0, 0);
    end_pass(cmd);
    R.rendering = false;
    R.passTracked = false;
}

void sampled(Surface* s) {
    transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// every 600 swaps: the counters so far
void log_totals() {
    if (++S.swaps % 600) return;
    std::string why;
    for (int k = 0; k < kSkipCount; k++)
        if (S.totalSkipped[k]) why += std::string(why.empty() ? "" : ", ") + kSkipNames[k] + " " + std::to_string(S.totalSkipped[k]);
    LOG("[framegen] %llu in-between frames generated (%llu presented early), %llu real frames shown alone (%s); real frames every %.2f ms",
        (unsigned long long)S.totalGenerated, (unsigned long long)S.totalEarly, (unsigned long long)S.swaps - S.totalGenerated,
        why.empty() ? "-" : why.c_str(), S.interval / 1e6);
}

}  // namespace

bool enabled() { return framegen::requested(); }

// render thread (OP_FRAMEGEN_CAMERA): the 3D camera of the frame being drawn
void note_camera(const uint32_t* words, uint32_t n) {
    if (n < framegen::kCameraWords) return;
    float v[framegen::kCameraWords - 1];
    memcpy(v, words, sizeof v);
    const float *eye = v, *center = v + 3, *up = v + 6;
    static const bool camLog = getenv("WWHD_CAM_LOG") != nullptr;
    if (camLog)
        LOG("[framegen] cam frame %llu eye %.1f %.1f %.1f center %.1f %.1f %.1f fovy %.2f tan %.4f near %.2f far %.0f flags %u",
            (unsigned long long)R.frame, eye[0], eye[1], eye[2], center[0], center[1], center[2], v[9], v[12], v[10], v[11],
            words[framegen::kCameraWords - 1]);
    if (S.cur.frame != R.frame) S.prev = S.cur;  // the frame's last camera counts (others: 2D, menus)
    basis(eye, center, up, S.cur.m);
    memcpy(S.cur.eye, eye, sizeof S.cur.eye);
    S.cur.target = std::sqrt((center[0] - eye[0]) * (center[0] - eye[0]) + (center[1] - eye[1]) * (center[1] - eye[1]) +
                             (center[2] - eye[2]) * (center[2] - eye[2]));
    S.cur.tanY = v[12];
    S.cur.znear = v[10];
    S.cur.zfar = v[11];
    S.cur.flags = words[framegen::kCameraWords - 1];
    S.cur.frame = R.frame;
    S.cur.valid = true;
    static int logged = 0;
    if (logged < 2) {
        logged++;
        LOG("[framegen] camera (frame %llu): fovy %.1f (tan %.4f as drawn) near %.2f far %.1f", (unsigned long long)R.frame, v[9], v[12],
            v[10], v[11]);
    }
}

// render thread (OP_FRAMEGEN_LAYOUT): a layout is about to be drawn into the target bound now. At the
// first one on the TV picture this frame, the scene is complete: copy it aside, and note its depth buffer.
void layout(uint32_t colorBase, uint32_t width, uint32_t height, uint32_t depthBase) {
    if (S.sceneFrame == R.frame || stereo::frame.mode != stereo::Off) return;
    // (no copy while another 60 fps mode or fast forward runs: no in-between frame follows)
    if (S.cur.valid && S.cur.frame == R.frame && (S.cur.flags & (framegen::kInterp | framegen::kTrue60 | framegen::kFastForward))) return;
    // the TV picture: 16:9 at the console's size (the GamePad's 854x480 family is not it)
    if (height < 360 || height == 480 || std::abs((float)width * 9.0f / ((float)height * 16.0f) - 1.0f) > 0.03f) return;
    Surface* color = find_surface(colorBase, width, height, false);
    if (!color || color->fmt.depth || color->fmt.kind != FormatInfo::FLOAT) return;
    S.sceneFrame = R.frame;
    if (!ensure_image(S.scene, color->format, color->fmt, color->extent.width, color->extent.height,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        S.sceneFrame = ~0ull;
        return;
    }
    transition_image(color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(S.scene.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy r{};
    r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.extent = {color->extent.width, color->extent.height, 1};
    record::vkCmdCopyImage(command_buffer(), color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, S.scene->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    // the scene's depth: the TV-sized depth buffer bound for the HUD, else the main 3D pass's
    S.depthAddr = 0;
    Surface* d = find_surface(depthBase, 0, 0, true);
    if (!same_extent(d, color)) d = find_surface(R.mainDepthAddr, 0, 0, true);
    if (same_extent(d, color)) S.depthAddr = d->addr;
    static int logged = 0;
    if (logged < 2) {
        logged++;
        LOG("[framegen] scene %08X %ux%u (%ux%u) format %d, depth %08X format %d", color->addr, width, height, color->extent.width,
            color->extent.height, int(color->fmt.pixel), S.depthAddr, d && S.depthAddr ? int(d->fmt.pixel) : 0);
    }
}

int64_t real_presented(bool shown, int64_t now) {
    if (!shown) {
        S.lastReal = 0;
        return 0;
    }
    if (S.lastReal) {
        const double d = double(now - S.lastReal);
        if (d > 5e6 && d < 1e8) S.interval = S.interval > 0 ? S.interval * 0.9 + d * 0.1 : d;
    }
    S.lastReal = now;
    // (the real frames come faster than 40 a second: no room for an in-between frame)
    if (S.interval < 25e6) return 0;
    return int64_t(std::min(S.interval, 50e6) * 0.5);
}

Surface* generate(Surface* scan, Skip skip) {
    log_totals();
    const Cam& cur = S.cur;
    const Cam& prev = S.prev;
    if (skip == kNone) {
        if (!cur.valid || cur.frame != R.frame) skip = kNoCamera;
        else if (cur.flags & (framegen::kInterp | framegen::kTrue60 | framegen::kFastForward)) skip = kOtherMode;
        else if (S.sceneFrame != R.frame || !S.scene || !S.scene->image) skip = kNoScene;
        else if (!scan || !scan->image || !scan->view || !same_extent(scan, S.scene.get()) || scan->fmt.pixel != S.scene->fmt.pixel)
            skip = kSize;
        else if (!(cur.znear > 0) || !(cur.zfar > cur.znear) || !(cur.tanY > 0)) skip = kNoCamera;
    }
    Surface* depth = nullptr;
    if (skip == kNone) {
        depth = find_surface(S.depthAddr, 0, 0, true);
        if (!same_extent(depth, S.scene.get())) skip = kNoDepth;
    }
    // the generated camera: half the last rotation (D = Cprev^T Ccur, D^(1/2)) and half the last
    // movement, in the real camera's space; none across a cut or without a camera of the frame before
    float half[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, move[3] = {0, 0, 0};
    if (skip == kNone) {
        if (!prev.valid || prev.frame + 1 != cur.frame) skip = kCut;
    }
    if (skip == kNone) {
        const Step step = half_step(prev.m, prev.eye, cur.m, cur.eye, half, move);
        if (step == Step::Cut) skip = kCut;
        else if (step == Step::Still) skip = kStill;
    }
    // the images and pipelines
    VkPipeline warpPipe = VK_NULL_HANDLE, maskPipe = VK_NULL_HANDLE;
    if (skip == kNone) {
        const FormatInfo rgba8 = format_info(0x1A, false);  // GX2 R8G8B8A8_UNORM
        const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (S.failed || !make_layout() ||
            !ensure_image(S.gen, scan->format, scan->fmt, scan->extent.width, scan->extent.height, target | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            !ensure_image(S.mask, 0x1A, rgba8, std::max(1u, (scan->extent.width + 3) / 4), std::max(1u, (scan->extent.height + 3) / 4), target) ||
            !(warpPipe = pipeline_for(kWarp, scan->fmt.pixel)) || !(maskPipe = pipeline_for(kHudMask, rgba8.pixel)))
            skip = kFailed;
    }
    if (skip != kNone) {
        count_skip(skip);
        static int logged = 0;
        if (logged < 4 && skip != kStill && skip != kNotShown) {
            logged++;
            LOG("[framegen] frame %llu not generated (%s): camera frame %lld, scene frame %lld, depth %08X", (unsigned long long)R.frame,
                kSkipNames[skip], cur.valid ? (long long)cur.frame : -1ll, S.scene ? (long long)S.sceneFrame : -1ll, S.depthAddr);
        }
        return nullptr;
    }
    // the TV picture's size relative to its own height: its projection's aspect (the game widens it)
    const float tanY = cur.tanY, tanX = tanY * float(S.scene->extent.width) / float(S.scene->extent.height);
    static const float hudThreshold = getenv("WWHD_FRAMEGEN_HUD") ? (float)atof(getenv("WWHD_FRAMEGEN_HUD")) : 3.0f / 255.0f;
    static const bool tint = getenv("WWHD_FRAMEGEN_TINT") && atoi(getenv("WWHD_FRAMEGEN_TINT")) != 0;
    static const bool followOn = !getenv("WWHD_FRAMEGEN_FOLLOW") || atoi(getenv("WWHD_FRAMEGEN_FOLLOW")) != 0;
    static const float debugView = getenv("WWHD_FRAMEGEN_DEBUG") ? (float)atof(getenv("WWHD_FRAMEGEN_DEBUG")) : 0.0f;
    const Push pc{{half[0], half[1], half[2], move[0]}, {half[3], half[4], half[5], move[1]}, {half[6], half[7], half[8], move[2]},
                  {tanX, tanY, hudThreshold, tint ? 1.0f : 0.0f}, {cur.znear, cur.zfar, 1, 0}, {0, 0, 1, 1},
                  {cur.target, followOn ? 1.0f : 0.0f, 0, debugView}};
    end_encoder();
    sampled(scan);
    sampled(S.scene.get());
    sampled(depth);
    {  // where the HUD is: the finished picture against the scene before the HUD
        const Input in[2] = {{scan, S.nearest}, {S.scene.get(), S.nearest}};
        run_pass(*S.mask, maskPipe, in, 2, pc);
    }
    sampled(S.mask.get());
    {
        const Input in[5] = {{scan, S.nearest}, {depth, S.nearest}, {S.mask.get(), S.linear}, {S.scene.get(), S.nearest}, {S.scene.get(), S.nearest}};
        run_pass(*S.gen, warpPipe, in, 5, pc);
    }
    S.gen->dyn = scan->dyn;  // (the size the picture stands for: as the real scan's)
    S.generated++;
    S.totalGenerated++;
    static bool first = true;
    if (first) {
        first = false;
        LOG("[framegen] first in-between frame (frame %llu): %ux%u format %d, tan %.4f x %.4f, step %.2f %.2f %.2f%s%s",
            (unsigned long long)R.frame, S.gen->extent.width, S.gen->extent.height, int(S.gen->fmt.pixel), tanX, tanY, move[0] * 2, move[1] * 2,
            move[2] * 2, tint ? ", tinted" : "", followOn ? "" : ", follow off");
    }
    return S.gen.get();
}

std::unique_ptr<Surface>& generated_scan() { return S.gen; }

void presented(bool early) {
    if (early) {
        S.early++;
        S.totalEarly++;
    }
}
void not_presented() {
    S.generated--;
    S.totalGenerated--;
    count_skip(kNotShown);
}

std::string perf_line() {
    char buf[96];
    snprintf(buf, sizeof buf, "generated %llu (presented early %llu), skipped", (unsigned long long)S.generated, (unsigned long long)S.early);
    std::string out = buf;
    uint64_t skipped = 0;
    for (int k = 0; k < kSkipCount; k++) skipped += S.skipped[k];
    out += " " + std::to_string(skipped);
    for (int k = 0; k < kSkipCount; k++)
        if (S.skipped[k]) out += std::string(", ") + kSkipNames[k] + " " + std::to_string(S.skipped[k]);
    S.generated = S.early = 0;
    for (auto& k : S.skipped) k = 0;
    return out;
}

}  // namespace gfxvk::reproj
