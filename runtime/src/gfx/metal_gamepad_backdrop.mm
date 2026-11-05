#include "metal_gamepad_backdrop.h"
#include <map>
#include <tuple>
namespace gfx {
namespace {
const char *source = R"(
#include <metal_stdlib>
using namespace metal;
struct V {float4 p [[position]];float2 uv;};
vertex V bg_vs(uint id [[vertex_id]],constant float4& rect [[buffer(0)]]) {
 float2 uv=float2(id&1,id>>1); V o;
 o.p=float4((rect.xy+uv*rect.zw)*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;
}
struct P {int conv;int edge;float opacity;float pad;};
float3 linearize(float3 c){return select(pow((c+.055)/1.055,2.4),c/12.92,c<=.04045);}
float3 encode(float3 c){return select(1.055*pow(c,1.0/2.4)-.055,c*12.92,c<=.0031308);}
fragment float4 bg_fs(V in [[stage_in]], texture2d<float> tex [[texture(0)]],
 sampler s [[sampler(0)]],constant P& p [[buffer(0)]]) {
 float4 c;
 if(p.edge<0) {c=tex.sample(s,in.uv);if(c.a>0)c.rgb/=c.a;else c.rgb=0;}
 else {
  uint2 size=uint2(tex.get_width(),tex.get_height());float3 sum=0;
  for(uint i=0;i<4;++i){uint u=min(uint((float(i)+.5)*float(p.edge<2?size.x:size.y)/4), (p.edge<2?size.x:size.y)-1);
   uint2 at=p.edge==0?uint2(u,0):p.edge==1?uint2(u,size.y-1):p.edge==2?uint2(0,u):uint2(size.x-1,u);
   sum+=tex.read(at).rgb;}
  c=float4(sum*.25,1);
 }
 if(p.conv==1)c.rgb=linearize(saturate(c.rgb));else if(p.conv==2)c.rgb=encode(saturate(c.rgb));
 c.a*=p.opacity;c.rgb*=c.a;return c;
}
)";
id<MTLDevice> owner;
id<MTLLibrary> library;
id<MTLSamplerState> sampler;
std::map<unsigned, id<MTLRenderPipelineState>> pipelines;
bool srgb(MTLPixelFormat f) {
  return f == MTLPixelFormatBGRA8Unorm_sRGB ||
         f == MTLPixelFormatRGBA8Unorm_sRGB;
}
} // namespace
void gamepad_capture_coverage(
    MTLRenderPipelineColorAttachmentDescriptor *attachment, bool opaque) {
  if (!attachment.blendingEnabled) {
    attachment.blendingEnabled = YES;
    attachment.rgbBlendOperation = MTLBlendOperationAdd;
    attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
    attachment.destinationRGBBlendFactor = MTLBlendFactorZero;
  }
  attachment.writeMask |= MTLColorWriteMaskAlpha;
  attachment.alphaBlendOperation = MTLBlendOperationAdd;
  attachment.sourceAlphaBlendFactor =
      opaque ? MTLBlendFactorZero : MTLBlendFactorOne;
  attachment.destinationAlphaBlendFactor =
      opaque ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
}
void reset_gamepad_bar_pipelines() {
  pipelines.clear();
  sampler = nil;
  library = nil;
  owner = nil;
}
void gamepad_draw_bars(id<MTLDevice> device,
                       id<MTLRenderCommandEncoder> encoder,
                       MTLPixelFormat format, unsigned width, unsigned height,
                       gamepad_backdrop::Rect viewport,
                       gamepad_backdrop::Rect content, id<MTLTexture> scan,
                       id<MTLTexture> backdrop, bool sourceLinear,
                       float opacity) {
  using namespace gamepad_backdrop;
  if (!device || !encoder || !scan || !width || !height ||
      !enabled.load(std::memory_order_relaxed) ||
      !has_bars(viewport.w, viewport.h, content) || !(opacity > 0))
    return;
  if (owner != device) {
    reset_gamepad_bar_pipelines();
    owner = device;
  }
  if (!library) {
    NSError *error = nil;
    library = [device newLibraryWithSource:@(source) options:nil error:&error];
    if (!library)
      return;
    MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
    d.minFilter = d.magFilter = MTLSamplerMinMagFilterLinear;
    d.sAddressMode = d.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler = [device newSamplerStateWithDescriptor:d];
  }
  auto &pipeline = pipelines[unsigned(format)];
  if (!pipeline) {
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [library newFunctionWithName:@"bg_vs"];
    d.fragmentFunction = [library newFunctionWithName:@"bg_fs"];
    auto a = d.colorAttachments[0];
    a.pixelFormat = format;
    a.blendingEnabled = YES;
    a.sourceRGBBlendFactor = a.sourceAlphaBlendFactor = MTLBlendFactorOne;
    a.destinationRGBBlendFactor = a.destinationAlphaBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    NSError *error = nil;
    pipeline = [device newRenderPipelineStateWithDescriptor:d error:&error];
    if (!pipeline)
      return;
  }
  const auto coverRect =
      cover(viewport.w, viewport.h, backdrop ? backdrop.width : scan.width,
            backdrop ? backdrop.height : scan.height);
  [encoder setRenderPipelineState:pipeline];
  [encoder setViewport:MTLViewport{0, 0, double(width), double(height), 0, 1}];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setFragmentTexture:backdrop ?: scan atIndex:0];
  [encoder setFragmentSamplerState:sampler atIndex:0];
  const auto rectangles = bars(viewport.w, viewport.h, content);
  for (unsigned i = 0; i < rectangles.size(); ++i) {
    const auto &bar = rectangles[i];
    if (!(bar.w > 0 && bar.h > 0))
      continue;
    const auto area = backdrop ? coverRect : bar;
    float rect[4] = {(viewport.x + area.x) / width,
                     (viewport.y + area.y) / height, area.w / width,
                     area.h / height};
    const unsigned x = unsigned(
        std::clamp<double>(std::ceil(viewport.x + bar.x), 0., double(width)));
    const unsigned y = unsigned(
        std::clamp<double>(std::ceil(viewport.y + bar.y), 0., double(height)));
    const unsigned right = unsigned(std::clamp<double>(
        std::floor(viewport.x + bar.x + bar.w), double(x), double(width)));
    const unsigned bottom = unsigned(std::clamp<double>(
        std::floor(viewport.y + bar.y + bar.h), double(y), double(height)));
    if (right == x || bottom == y)
      continue;
    struct {
      int conv, edge;
      float opacity, pad;
    } p{sourceLinear == srgb(format) ? 0
        : srgb(format)               ? 1
                                     : 2,
        backdrop ? -1 : int(i), std::clamp(opacity, 0.f, 1.f), 0};
    [encoder setScissorRect:MTLScissorRect{x, y, right - x, bottom - y}];
    [encoder setVertexBytes:rect length:sizeof rect atIndex:0];
    [encoder setFragmentBytes:&p length:sizeof p atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0
                vertexCount:4];
  }
  // The caller's ordinary presenter always rebinds its pipeline/rectangle.
  // Restore the full-output scissor so neither scanout nor overlays inherit bar
  // clipping.
  [encoder setScissorRect:MTLScissorRect{0, 0, width, height}];
}
} // namespace gfx
