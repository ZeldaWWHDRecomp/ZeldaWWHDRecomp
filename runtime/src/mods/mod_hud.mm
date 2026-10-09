// HUDs for the gameplay mods, drawn by the renderer into the TV image right after the game's frame
// is copied to the scan buffer (so frame dumps show them too):
//   - the "climb any wall" mod's stamina wheel (climb.cpp): a ring to the upper right of the screen
//     centre, where the follow camera keeps Link, shown while climbing, refilling or fading out;
//   - the run/swim speed mod's boost bar (mods.cpp): a slim bar just under it, shown while boosting
//     or while the bar refills, green running, blue swimming, amber recharging.
// The Vulkan host draws the same two in gfx/vulkan/present.cpp (draw_mod_overlay).
#import <Metal/Metal.h>

#include <unordered_map>

#include "mods/climb.h"
#include "mods/mods.h"

namespace {
const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;
struct U { float4 rect; float stamina; float alpha; float exhausted; float pad; };
struct V { float4 pos [[position]]; float2 p; };
vertex V hud_vs(uint vid [[vertex_id]], constant U& u [[buffer(0)]]) {
    float2 q = float2(vid & 1, vid >> 1);
    V o;
    o.pos = float4(mix(u.rect.xy, u.rect.zw, q), 0, 1);
    o.p = q * 2.0 - 1.0;  // -1..1, +y up
    return o;
}
fragment float4 hud_fs(V in [[stage_in]], constant U& u [[buffer(0)]]) {
    float r = length(in.p);
    float aa = max(fwidth(r), 1e-4);
    // ring 0.58..0.92 (fill), outline 0.50..1.0 (dark)
    float ring = smoothstep(0.58 - aa, 0.58 + aa, r) * (1.0 - smoothstep(0.92 - aa, 0.92 + aa, r));
    float outline = smoothstep(0.50 - aa, 0.50 + aa, r) * (1.0 - smoothstep(1.0 - 2.0 * aa, 1.0, r));
    float ang = atan2(in.p.x, in.p.y);  // 0 at the top, clockwise
    float frac = (ang < 0.0 ? ang + 2.0 * M_PI_F : ang) / (2.0 * M_PI_F);
    float filled = 1.0 - smoothstep(u.stamina - 0.004, u.stamina + 0.004, frac);
    float3 green = float3(0.30, 0.90, 0.35), yellow = float3(1.0, 0.80, 0.15), red = float3(0.95, 0.20, 0.15);
    float3 full = u.exhausted > 0.5 ? red : (u.stamina < 0.3 ? mix(red, yellow, u.stamina / 0.3) : green);
    // the empty part: dark grey, dark red while exhausted (refilling, no climbing until full)
    float3 empty = u.exhausted > 0.5 ? float3(0.55, 0.08, 0.06) : float3(0.10, 0.10, 0.10);
    float3 col = mix(empty, full, filled);
    float fa = ring * mix(0.45, 0.95, filled);
    float oa = outline * 0.45;
    // fill over outline, premultiplied
    float a = fa + oa * (1.0 - fa);
    float3 c = col * fa;
    return float4(c, a) * u.alpha;
}
)";
// the run/swim boost's bar (mods.cpp move_hud): uv 0..1 across it, premultiplied; the same shader as
// gfx/vulkan/present.cpp's moveHudSource
const char* kMoveShader = R"(
#include <metal_stdlib>
using namespace metal;
struct MU { float4 rect; float stamina; float alpha; float state; float pad; };
struct MV { float4 pos [[position]]; float2 uv; };
vertex MV move_vs(uint vid [[vertex_id]], constant MU& u [[buffer(0)]]) {
    float2 q = float2(vid & 1, vid >> 1);
    MV o;
    o.pos = float4(mix(u.rect.xy, u.rect.zw, q), 0, 1);
    o.uv = q;
    return o;
}
fragment float4 move_fs(MV in [[stage_in]], constant MU& u [[buffer(0)]]) {
    float aa = max(fwidth(in.uv.x), 1e-4);
    float edge = smoothstep(0.0, aa * 2.0, min(min(in.uv.x, 1.0 - in.uv.x), min(in.uv.y, 1.0 - in.uv.y)));
    float fill = 1.0 - smoothstep(u.stamina - 0.004, u.stamina + 0.004, in.uv.x);
    // 0 running, 1 swimming, 2 recharging
    float3 full = u.state > 1.5 ? float3(0.95, 0.62, 0.18) : u.state > 0.5 ? float3(0.30, 0.62, 0.95) : float3(0.32, 0.88, 0.38);
    float3 col = mix(float3(0.07, 0.07, 0.08), full, fill);
    float a = edge * 0.9 * u.alpha;
    return float4(col * a, a);
}
)";

struct Hud {
    id<MTLDevice> device = nil;
    id<MTLLibrary> lib = nil;
    std::unordered_map<NSUInteger, id<MTLRenderPipelineState>> pipelines;      // stamina wheel
    std::unordered_map<NSUInteger, id<MTLRenderPipelineState>> movePipelines;  // boost bar
};
Hud& hud() {
    static Hud h;
    return h;
}

id<MTLRenderPipelineState> pipeline(id<MTLDevice> dev, MTLPixelFormat fmt, bool wheel) {
    Hud& h = hud();
    if (h.device != dev) {
        h = Hud{};
        h.device = dev;
        NSError* err = nil;
        NSString* source = [NSString stringWithFormat:@"%s\n%s", kShader, kMoveShader];
        h.lib = [dev newLibraryWithSource:source options:nil error:&err];
        if (!h.lib) {
            NSLog(@"[mods] HUD shaders: %@", err.localizedDescription);
            return nil;
        }
    }
    if (!h.lib) return nil;
    auto& cache = wheel ? h.pipelines : h.movePipelines;
    auto it = cache.find(fmt);
    if (it != cache.end()) return it->second;
    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [h.lib newFunctionWithName:wheel ? @"hud_vs" : @"move_vs"];
    d.fragmentFunction = [h.lib newFunctionWithName:wheel ? @"hud_fs" : @"move_fs"];
    d.colorAttachments[0].pixelFormat = fmt;
    d.colorAttachments[0].blendingEnabled = YES;
    d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero;
    d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;  // keep the image's alpha
    NSError* err = nil;
    id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p) NSLog(@"[mods] HUD pipeline: %@", err.localizedDescription);
    cache[fmt] = p;
    return p;
}
}  // namespace

namespace mods {
// called by the renderer after the TV image was copied to the scan texture
void draw_overlay(id<MTLCommandBuffer> cmd, id<MTLTexture> tex) {
    const ClimbHud wheel = climb_hud();
    const MoveHud bar = move_hud();
    const bool drawWheel = climb_enabled() && wheel.alpha > 0.0f, drawBar = bar.alpha > 0.0f;
    if ((!drawWheel && !drawBar) || !tex || !(tex.usage & MTLTextureUsageRenderTarget)) return;
    float w = tex.width, h = tex.height;
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = tex;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [cmd renderCommandEncoderWithDescriptor:rp];
    if (drawWheel) {
        id<MTLRenderPipelineState> p = pipeline(cmd.device, tex.pixelFormat, true);
        if (p) {
            float cx = 0.60f * w, cy = 0.38f * h, rad = 0.05f * h;  // pixels, from the top left
            struct { float rect[4]; float stamina, alpha, exhausted, pad; } u = {
                {(cx - rad) / w * 2 - 1, 1 - (cy + rad) / h * 2, (cx + rad) / w * 2 - 1, 1 - (cy - rad) / h * 2},
                wheel.stamina, wheel.alpha, wheel.exhausted ? 1.0f : 0.0f, 0};
            [e setRenderPipelineState:p];
            [e setVertexBytes:&u length:sizeof u atIndex:0];
            [e setFragmentBytes:&u length:sizeof u atIndex:0];
            [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        }
    }
    if (drawBar) {
        id<MTLRenderPipelineState> p = pipeline(cmd.device, tex.pixelFormat, false);
        if (p) {
            float bw = 0.12f * w, bh = 0.011f * h, cx = 0.60f * w, cy = 0.44f * h;
            struct { float rect[4]; float stamina, alpha, state, pad; } u = {
                {(cx - bw / 2) / w * 2 - 1, 1 - (cy + bh / 2) / h * 2, (cx + bw / 2) / w * 2 - 1, 1 - (cy - bh / 2) / h * 2},
                bar.stamina, bar.alpha, bar.exhausted ? 2.0f : bar.swimming ? 1.0f : 0.0f, 0};
            [e setRenderPipelineState:p];
            [e setVertexBytes:&u length:sizeof u atIndex:0];
            [e setFragmentBytes:&u length:sizeof u atIndex:0];
            [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        }
    }
    [e endEncoding];
}
}  // namespace mods
