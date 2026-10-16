// Renders the volumetric clouds (gfx/clouds.h) over a simple sky and sea to a PNG, without the game
// or a window: a headless Vulkan device draws one fullscreen pass, the image is read back and saved.
// Used to tune the cloud look (cel bands, coverage, colours) and as a test that the shader compiles
// and runs on the device.
//
//   cloud_preview out.png [--size WxH] [--preset noon|morning|sunset|storm] [--cel 0..1]
//                         [--coverage 0..1] [--yaw deg] [--pitch deg] [--fov deg] [--time s]
//                         [--steps n] [--light-steps n] [--bands n]
#include <vulkan/vulkan.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <zlib.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "gfx/clouds.h"

namespace {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string(r) + ")");
}

const char* kVertex = R"glsl(#version 450
layout(location=0) out vec2 ndc;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    ndc = p * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)glsl";

// preview scene: sky gradient with a sun, a flat sea with a sky reflection, clouds over both
const char* kPreviewMain = R"glsl(
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 result;
vec3 to_srgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
vec3 sky(vec3 rd) {
    float up = clamp(rd.y, 0.0, 1.0);
    vec3 c = mix(P.skyHorizon.rgb, P.skyTop.rgb, pow(up, 0.45));
    float s = max(dot(rd, normalize(P.sunDir.xyz)), 0.0);
    c += P.sunColor.rgb * (pow(s, 900.0) * 6.0 + pow(s, 12.0) * 0.12);
    return c;
}
void main() {
    vec4 nearP = P.invViewProj * vec4(ndc, 0.0, 1.0);
    vec4 farP = P.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = P.camPos.xyz;
    vec3 rd = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
    vec3 bg;
    float maxDist = 1e30;
    if (rd.y < 0.0) {
        float t = -ro.y / rd.y;
        maxDist = t;
        vec3 deep = vec3(0.0, 0.16, 0.42), shallow = vec3(0.02, 0.38, 0.75);
        float fres = pow(1.0 - max(-rd.y, 0.0), 5.0);
        vec3 refl = sky(reflect(rd, vec3(0.0, 1.0, 0.0)));
        bg = mix(mix(shallow, deep, clamp(-rd.y * 3.0, 0.0, 1.0)), refl, fres * 0.8);
        bg = mix(P.skyHorizon.rgb, bg, exp(-t / P.shadowTint.w * 4.0));
    } else {
        bg = sky(rd);
    }
    vec4 c = wwhd_clouds(ro, rd, maxDist, gl_FragCoord.xy);
    vec3 color = bg * (1.0 - c.a) + c.rgb;
    result = vec4(to_srgb(clamp(color, 0.0, 1.0)), 1.0);
}
)glsl";

std::vector<uint32_t> compile(const std::string& src, EShLanguage stage) {
    glslang::TShader shader(stage);
    const char* text = src.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        throw std::runtime_error(std::string("GLSL: ") + shader.getInfoLog() + shader.getInfoDebugLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) throw std::runtime_error(std::string("GLSL link: ") + program.getInfoLog());
    std::vector<uint32_t> words;
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &options);
    return words;
}

void be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }
void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    be32(png, uint32_t(data.size()));
    size_t begin = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    be32(png, uint32_t(crc32(crc32(0, Z_NULL, 0), png.data() + begin, uInt(data.size() + 4))));
}
void write_png(const std::string& path, uint32_t w, uint32_t h, const uint8_t* rgba) {
    std::vector<uint8_t> raw;
    for (uint32_t y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + size_t(y) * w * 4, rgba + size_t(y + 1) * w * 4);
    }
    uLongf size = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(size);
    if (compress2(z.data(), &size, raw.data(), uLong(raw.size()), 6) != Z_OK) throw std::runtime_error("zlib");
    z.resize(size);
    std::vector<uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10}, ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    FILE* f = fopen(path.c_str(), "wb");
    if (!f || fwrite(png.data(), 1, png.size(), f) != png.size() || fclose(f)) throw std::runtime_error("cannot write " + path);
}

// ---- column-major 4x4 matrices (GLSL convention)
struct Mat { float m[16]; };
Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++) r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
    return r;
}
Mat inverse(const Mat& a) {
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    Mat r;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] / det;
    return r;
}
// view looking along (yaw, pitch) from eye; Vulkan clip space (y down, depth 0..1)
Mat view_proj(const float eye[3], float yawDeg, float pitchDeg, float fovDeg, float aspect) {
    const float d2r = 3.14159265f / 180.0f;
    float cy = std::cos(yawDeg * d2r), sy = std::sin(yawDeg * d2r), cp = std::cos(pitchDeg * d2r), sp = std::sin(pitchDeg * d2r);
    float f[3] = {sy * cp, sp, cy * cp};  // forward
    float r[3] = {cy, 0.0f, -sy};         // right
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    if (u[1] < 0) for (float& x : u) x = -x;
    Mat v{};
    for (int i = 0; i < 3; i++) { v.m[i * 4 + 0] = r[i]; v.m[i * 4 + 1] = -u[i]; v.m[i * 4 + 2] = f[i]; }
    v.m[12] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]);
    v.m[13] = (u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    v.m[14] = -(f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    v.m[15] = 1.0f;
    float n = 10.0f, fa = 1.0e8f, t = 1.0f / std::tan(fovDeg * d2r * 0.5f);
    Mat p{};
    p.m[0] = t / aspect; p.m[5] = t; p.m[10] = fa / (fa - n); p.m[11] = 1.0f; p.m[14] = -n * fa / (fa - n);
    return mul(p, v);
}

void srgb_color(float* dst, float r, float g, float b) {
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    dst[0] = lin(r); dst[1] = lin(g); dst[2] = lin(b);
}
void set_sun(clouds::Params& p, float elevDeg, float azimDeg, float intensity) {
    const float d2r = 3.14159265f / 180.0f;
    p.sunDir[0] = std::cos(elevDeg * d2r) * std::sin(azimDeg * d2r);
    p.sunDir[1] = std::sin(elevDeg * d2r);
    p.sunDir[2] = std::cos(elevDeg * d2r) * std::cos(azimDeg * d2r);
    p.sunDir[3] = intensity;
}
void apply_preset(clouds::Params& p, const std::string& name) {
    if (name == "noon") {
        set_sun(p, 55, 25, 1.0f);
        srgb_color(p.sunColor, 1.0f, 0.98f, 0.94f);
        srgb_color(p.skyTop, 0.10f, 0.42f, 0.92f);
        srgb_color(p.skyHorizon, 0.62f, 0.86f, 1.0f);
        srgb_color(p.shadowTint, 0.62f, 0.72f, 0.90f);
    } else if (name == "morning") {
        set_sun(p, 15, 60, 0.9f);
        srgb_color(p.sunColor, 1.0f, 0.85f, 0.65f);
        srgb_color(p.skyTop, 0.25f, 0.50f, 0.85f);
        srgb_color(p.skyHorizon, 0.92f, 0.85f, 0.78f);
        srgb_color(p.shadowTint, 0.72f, 0.72f, 0.92f);
    } else if (name == "sunset") {
        set_sun(p, 4, 15, 0.85f);
        srgb_color(p.sunColor, 1.0f, 0.55f, 0.25f);
        srgb_color(p.skyTop, 0.24f, 0.28f, 0.55f);
        srgb_color(p.skyHorizon, 1.0f, 0.62f, 0.40f);
        srgb_color(p.shadowTint, 0.62f, 0.50f, 0.78f);
    } else if (name == "storm") {
        set_sun(p, 40, 25, 0.35f);
        srgb_color(p.sunColor, 0.80f, 0.82f, 0.86f);
        srgb_color(p.skyTop, 0.30f, 0.33f, 0.40f);
        srgb_color(p.skyHorizon, 0.48f, 0.52f, 0.58f);
        srgb_color(p.shadowTint, 0.45f, 0.48f, 0.58f);
        p.skyTop[3] = 0.85f;
    } else {
        throw std::runtime_error("unknown preset " + name);
    }
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc < 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: cloud_preview out.png [--size WxH] [--preset noon|morning|sunset|storm] [--cel 0..1]\n"
                        "       [--coverage 0..1] [--yaw deg] [--pitch deg] [--fov deg] [--time s] [--steps n]\n"
                        "       [--light-steps n] [--bands n]\n");
        return 2;
    }
    std::string out = argv[1];
    uint32_t W = 960, H = 540;
    float yaw = 20, pitch = 8, fov = 60, time = 0;
    clouds::Params P = clouds::default_params();
    std::string preset = "noon";
    std::vector<std::pair<std::string, std::string>> overrides;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--size") { if (sscanf(v.c_str(), "%ux%u", &W, &H) != 2) throw std::runtime_error("--size WxH"); }
        else if (k == "--preset") preset = v;
        else if (k == "--yaw") yaw = std::stof(v);
        else if (k == "--pitch") pitch = std::stof(v);
        else if (k == "--fov") fov = std::stof(v);
        else if (k == "--time") time = std::stof(v);
        else overrides.emplace_back(k, v);
    }
    apply_preset(P, preset);
    for (auto& [k, v] : overrides) {
        float x = std::stof(v);
        if (k == "--cel") P.style[0] = x;
        else if (k == "--coverage") P.skyTop[3] = x;
        else if (k == "--steps") P.march[0] = x;
        else if (k == "--light-steps") P.march[1] = x;
        else if (k == "--bands") P.style[1] = x;
        else throw std::runtime_error("unknown option " + k);
    }
    float eye[3] = {0.0f, 800.0f, 0.0f};
    Mat inv = inverse(view_proj(eye, yaw, pitch, fov, float(W) / float(H)));
    memcpy(P.invViewProj, inv.m, sizeof inv.m);
    P.camPos[0] = eye[0]; P.camPos[1] = eye[1]; P.camPos[2] = eye[2]; P.camPos[3] = time;

    // ---- shaders
    glslang::InitializeProcess();
    auto vs = compile(kVertex, EShLangVertex);
    auto fs = compile(std::string("#version 450\n") + clouds::kCloudsGlsl + kPreviewMain, EShLangFragment);
    glslang::FinalizeProcess();

    // ---- device
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "cloud_preview";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    check(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (!n) throw std::runtime_error("no Vulkan device");
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(instance, &n, gpus.data());
    VkPhysicalDevice gpu = gpus[0];
    for (auto g : gpus) {  // prefer a real GPU over a software one
        VkPhysicalDeviceProperties pr;
        vkGetPhysicalDeviceProperties(g, &pr);
        if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) { gpu = g; break; }
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, families.data());
    uint32_t qf = UINT32_MAX;
    for (uint32_t i = 0; i < n; i++) if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qf = i; break; }
    if (qf == UINT32_MAX) throw std::runtime_error("no graphics queue");
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qf; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VkDevice dev;
    check(vkCreateDevice(gpu, &dci, nullptr, &dev), "vkCreateDevice");
    VkQueue queue;
    vkGetDeviceQueue(dev, qf, 0, &queue);
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mem);
    auto memType = [&](uint32_t bits, VkMemoryPropertyFlags want) {
        for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want) return i;
        throw std::runtime_error("no suitable memory type");
    };
    auto buffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceMemory& m) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size; bci.usage = usage;
        VkBuffer b;
        check(vkCreateBuffer(dev, &bci, nullptr, &b), "vkCreateBuffer");
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, b, &req);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(dev, &mai, nullptr, &m), "vkAllocateMemory");
        vkBindBufferMemory(dev, b, m, 0);
        return b;
    };

    // ---- render target, uniform and readback buffers
    VkImageCreateInfo imci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imci.imageType = VK_IMAGE_TYPE_2D; imci.format = VK_FORMAT_R8G8B8A8_UNORM; imci.extent = {W, H, 1};
    imci.mipLevels = 1; imci.arrayLayers = 1; imci.samples = VK_SAMPLE_COUNT_1_BIT; imci.tiling = VK_IMAGE_TILING_OPTIMAL;
    imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image;
    check(vkCreateImage(dev, &imci, nullptr, &image), "vkCreateImage");
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(dev, image, &ireq);
    VkMemoryAllocateInfo imai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imai.allocationSize = ireq.size;
    imai.memoryTypeIndex = memType(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imageMem;
    check(vkAllocateMemory(dev, &imai, nullptr, &imageMem), "vkAllocateMemory image");
    vkBindImageMemory(dev, image, imageMem, 0);
    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = image; ivci.viewType = VK_IMAGE_VIEW_TYPE_2D; ivci.format = imci.format;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    check(vkCreateImageView(dev, &ivci, nullptr, &view), "vkCreateImageView");

    VkDeviceMemory uboMem, readMem;
    VkBuffer ubo = buffer(sizeof P, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMem);
    void* mapped;
    vkMapMemory(dev, uboMem, 0, sizeof P, 0, &mapped);
    memcpy(mapped, &P, sizeof P);
    vkUnmapMemory(dev, uboMem);
    VkBuffer readback = buffer(VkDeviceSize(W) * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readMem);

    // ---- pipeline
    VkAttachmentDescription att{};
    att.format = imci.format; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &sub;
    VkRenderPass pass;
    check(vkCreateRenderPass(dev, &rpci, nullptr, &pass), "vkCreateRenderPass");
    VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass = pass; fbci.attachmentCount = 1; fbci.pAttachments = &view; fbci.width = W; fbci.height = H; fbci.layers = 1;
    VkFramebuffer fb;
    check(vkCreateFramebuffer(dev, &fbci, nullptr, &fb), "vkCreateFramebuffer");

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1; dslci.pBindings = &binding;
    VkDescriptorSetLayout dsl;
    check(vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &dsl), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
    VkPipelineLayout layout;
    check(vkCreatePipelineLayout(dev, &plci, nullptr, &layout), "vkCreatePipelineLayout");
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &ps;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(dev, &dpci, nullptr, &pool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
    VkDescriptorSet set;
    check(vkAllocateDescriptorSets(dev, &dsai, &set), "vkAllocateDescriptorSets");
    VkDescriptorBufferInfo dbi{ubo, 0, sizeof P};
    VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wds.dstSet = set; wds.descriptorCount = 1; wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev, 1, &wds, 0, nullptr);

    auto shaderModule = [&](const std::vector<uint32_t>& words) {
        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = words.size() * 4; smci.pCode = words.data();
        VkShaderModule m;
        check(vkCreateShaderModule(dev, &smci, nullptr, &m), "vkCreateShaderModule");
        return m;
    };
    VkPipelineShaderStageCreateInfo stages[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = shaderModule(vs); stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = shaderModule(fs); stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{0, 0, float(W), float(H), 0, 1};
    VkRect2D sc{{0, 0}, {W, H}};
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2; gpci.pStages = stages; gpci.pVertexInputState = &vi; gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps; gpci.pRasterizationState = &rs; gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb;
    gpci.layout = layout; gpci.renderPass = pass;
    VkPipeline pipe;
    check(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe), "vkCreateGraphicsPipelines");

    // ---- record, submit, read back
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = qf;
    VkCommandPool cmdPool;
    check(vkCreateCommandPool(dev, &cpci, nullptr, &cmdPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    check(vkAllocateCommandBuffers(dev, &cbai, &cmd), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = pass; rpbi.framebuffer = fb; rpbi.renderArea = sc;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    VkMemoryBarrier hostRead{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hostRead, 0, nullptr, 0, nullptr);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    check(vkCreateFence(dev, &fci, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    auto start = std::chrono::steady_clock::now();
    check(vkQueueSubmit(queue, 1, &si, fence), "vkQueueSubmit");
    check(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    double ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    void* pixels;
    vkMapMemory(dev, readMem, 0, VK_WHOLE_SIZE, 0, &pixels);
    write_png(out, W, H, static_cast<const uint8_t*>(pixels));
    vkUnmapMemory(dev, readMem);
    printf("cloud_preview: %s (%ux%u, preset %s, cel %.2f) on %s in %.0f ms\n", out.c_str(), W, H, preset.c_str(),
           P.style[0], props.deviceName, ms_);
    vkDeviceWaitIdle(dev);
    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
} catch (const std::exception& e) {
    fprintf(stderr, "cloud_preview: %s\n", e.what());
    return 1;
}
