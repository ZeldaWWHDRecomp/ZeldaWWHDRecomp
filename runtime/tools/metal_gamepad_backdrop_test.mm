// Offscreen Metal-only regression. Synthetic textures/materials; no game or
// window.
#include "../src/gfx/metal_backdrop_source.h"
#include "../src/gfx/metal_draw_replay.h"
#include "../src/gfx/metal_gamepad_backdrop.h"
#import <Metal/Metal.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace gfx;
int main() {
  @autoreleasepool {
    auto d = MTLCreateSystemDefaultDevice();
    if (!d) {
      puts("NO METAL DEVICE");
      return 77;
    }
    auto q = [d newCommandQueue];
    auto tex = [&](unsigned w, unsigned h, MTLPixelFormat format=MTLPixelFormatRGBA8Unorm) {
      auto td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:format
                                       width:w
                                      height:h
                                   mipmapped:NO];
      td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      td.storageMode = MTLStorageModeShared;
      return [d newTextureWithDescriptor:td];
    };
    auto scan = tex(16, 9), bg = tex(16, 9), out = tex(310, 270);
    std::vector<unsigned char> s(16 * 9 * 4, 255), b(16 * 9 * 4);
    for (unsigned i = 0; i < 16 * 9; i++) {
      s[i * 4] = 20;
      s[i * 4 + 1] = 40;
      s[i * 4 + 2] = 60;
      b[i * 4] = 100;
      b[i * 4 + 1] = 0;
      b[i * 4 + 2] = 0;
      b[i * 4 + 3] = 128;
    }
    [scan replaceRegion:MTLRegionMake2D(0, 0, 16, 9)
            mipmapLevel:0
              withBytes:s.data()
            bytesPerRow:64];
    [bg replaceRegion:MTLRegionMake2D(0, 0, 16, 9)
          mipmapLevel:0
            withBytes:b.data()
          bytesPerRow:64];
    auto run = [&](id<MTLTexture> backdrop, bool on,
                   gamepad_backdrop::Rect content) {
      gamepad_backdrop::enabled.store(on);
      auto cb = [q commandBuffer];
      auto rp = [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = out;
      rp.colorAttachments[0].loadAction = MTLLoadActionClear;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
      auto e = [cb renderCommandEncoderWithDescriptor:rp];
      gamepad_draw_bars(d, e, MTLPixelFormatRGBA8Unorm, 310, 270,
                        {0, 0, 310, 270}, content, scan, backdrop, false, 1);
      [e endEncoding];
      [cb commit];
      [cb waitUntilCompleted];
      if (cb.status != MTLCommandBufferStatusCompleted)
        exit(2);
      std::vector<unsigned char> p(310 * 270 * 4);
      [out getBytes:p.data()
          bytesPerRow:1240
           fromRegion:MTLRegionMake2D(0, 0, 310, 270)
          mipmapLevel:0];
      return p;
    };
    auto check = [](bool ok, const char *name) {
      if (!ok) {
        printf("FAIL %s\n", name);
        exit(1);
      }
      printf("PASS %s\n", name);
    };
    // Exercise the production capture-alpha policy with an actual Metal blend,
    // not only a texture that was premultiplied by the fixture.
    const char *captureShader = R"(
 #include <metal_stdlib>
 using namespace metal;
 vertex float4 capture_vs(uint i [[vertex_id]]) {return float4(i==2?3:-1,i==1?3:-1,0,1);}
 fragment float4 capture_fs(){return float4(.8,0,0,.5);}
 fragment float4 low_fs(){return float4(.8,0,0,.02);}
fragment float4 binding_fs(constant float4& bytes [[buffer(0)]], constant float4* values [[buffer(1)]],
 texture2d<float> image [[texture(0)]],sampler s [[sampler(0)]]) {
 return float4(bytes.r,values[0].g,image.sample(s,float2(.5)).b,1);
}
 )";
    NSError *error = nil;
    auto material = [d newLibraryWithSource:@(captureShader)
                                    options:nil
                                      error:&error];
    check(material != nil, "capture material compiled");
    auto capture = [&](bool opaque, bool policy, MTLPixelFormat format=MTLPixelFormatRGBA8Unorm, bool low=false) {
      auto image = tex(16, 9, format);
      auto pd = [MTLRenderPipelineDescriptor new];
      pd.vertexFunction = [material newFunctionWithName:@"capture_vs"];
      pd.fragmentFunction = [material newFunctionWithName:low ? @"low_fs" : @"capture_fs"];
      auto attachment = pd.colorAttachments[0];
      attachment.pixelFormat = format;
      attachment.blendingEnabled = !opaque;
      attachment.sourceRGBBlendFactor = attachment.sourceAlphaBlendFactor =
          MTLBlendFactorSourceAlpha;
      attachment.destinationRGBBlendFactor =
          attachment.destinationAlphaBlendFactor =
              MTLBlendFactorOneMinusSourceAlpha;
      if (policy)
        gamepad_capture_coverage(attachment, opaque);
      NSError *e = nil;
      auto pipeline = [d newRenderPipelineStateWithDescriptor:pd error:&e];
      check(pipeline != nil, "capture pipeline compiled");
      auto cb = [q commandBuffer];
      auto rp = [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = image;
      rp.colorAttachments[0].loadAction = MTLLoadActionClear;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      rp.colorAttachments[0].clearColor =
          MTLClearColorMake(0, 0, 0, opaque ? 1 : 0);
      auto encoder = [cb renderCommandEncoderWithDescriptor:rp];
      [encoder setRenderPipelineState:pipeline];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
      [encoder endEncoding];
      [cb commit];
      [cb waitUntilCompleted];
      check(cb.status == MTLCommandBufferStatusCompleted,
            "capture command completed");
      return image;
    };
    auto readPixel = [&](id<MTLTexture> texture) {
      std::vector<unsigned char> p(4);
      [texture getBytes:p.data()
            bytesPerRow:4
             fromRegion:MTLRegionMake2D(8, 4, 1, 1)
            mipmapLevel:0];
      return p;
    };
    auto baseline = readPixel(capture(false, false));
    check(abs(int(baseline[3]) - 64) <= 1,
          "negative control exposes alpha squared");
    auto captured = capture(false, true);
    auto coverage = readPixel(captured);
    check(abs(int(coverage[0]) - 102) <= 1 && abs(int(coverage[3]) - 128) <= 1,
          "production source-over stores half coverage not quarter");
    auto opaque = readPixel(capture(true, true));
    check(abs(int(opaque[0]) - 204) <= 1 && opaque[3] == 255,
          "opaque RGB capture keeps alpha one");
    // Native GamePad scanout is RGB10A2. Replaying its material into that
    // same format loses low coverage entirely; only the private layer promotes.
    auto native10 = capture(false, true, MTLPixelFormatRGB10A2Unorm, true);
    uint32_t nativeBefore=0,nativeAfter=0;
    [native10 getBytes:&nativeBefore bytesPerRow:4 fromRegion:MTLRegionMake2D(8,4,1,1) mipmapLevel:0];
    check((nativeBefore>>30)==0 && (nativeBefore&1023)>0,
          "negative control RGB10A2 loses low alpha while RGB remains nonzero");
    const auto promotedFormat=gamepad_capture_pixel_format(native10.pixelFormat);
    check(promotedFormat==MTLPixelFormatRGBA16Float,
          "actual RGB10A2 source selects high precision private capture");
    auto promoted=capture(false,true,promotedFormat,true);
    __fp16 lowPixel[4]{};
    [promoted getBytes:lowPixel bytesPerRow:8 fromRegion:MTLRegionMake2D(8,4,1,1) mipmapLevel:0];
    check(fabs(float(lowPixel[0])-.016f)<.0001f && fabs(float(lowPixel[3])-.02f)<.0001f,
          "promoted private layer retains low coverage and premultiplied RGB");
    auto halfPromoted=capture(false,true,promotedFormat);
    [halfPromoted getBytes:lowPixel bytesPerRow:8 fromRegion:MTLRegionMake2D(8,4,1,1) mipmapLevel:0];
    check(fabs(float(lowPixel[0])-.4f)<.001f && fabs(float(lowPixel[3])-.5f)<.001f,
          "promoted layer retains fractional coverage");
    [native10 getBytes:&nativeAfter bytesPerRow:4 fromRegion:MTLRegionMake2D(8,4,1,1) mipmapLevel:0];
    check(nativeBefore==nativeAfter && native10.pixelFormat==MTLPixelFormatRGB10A2Unorm,
          "private capture leaves native ten-bit scanout unchanged");
    gamepad_backdrop::Rect content{0, 48, 310, 174};
    auto lowBars=run(promoted,true,content);
    check(abs(int(lowBars[4*(10*310+155)])-4)<=1,
          "promoted low-alpha capture samples and composes without losing RGB");
    auto halfBars=run(halfPromoted,true,content);
    check(abs(int(halfBars[4*(10*310+155)])-102)<=1,
          "promoted fractional-alpha capture composes using original colour encoding");
    auto p = run(bg, true, content);
    check(abs(int(p[4 * (10 * 310 + 155)]) - 100) <= 1,
          "captured premultiplied coverage survives composition");
    check(p[4 * (135 * 310 + 155)] == 0, "content untouched");
    check(abs(int(p[4 * (260 * 310 + 155)]) - 100) <= 1, "bottom bar captured");
    p = run(captured, true, content);
    check(abs(int(p[4 * (10 * 310 + 155)]) - 102) <= 1,
          "actual captured half-alpha material composes once");
    p = run(nil, true, content);
    check(p[4 * (10 * 310 + 155)] == 20 && p[4 * (10 * 310 + 155) + 1] == 40,
          "edge-only fallback");
    p = run(bg, false, content);
    check(p[4 * (10 * 310 + 155)] == 0, "disabled unchanged");
    p = run(bg, true, {0, 0, 310, 270});
    check(p[4 * (10 * 310 + 155)] == 0, "no bars unchanged");
    // One draw exercises exact replay of copied inline bytes and strongly held
    // buffer/texture/sampler bindings after their caller-owned handles
    // disappear.
    MetalDrawBindings bindings;
    float inlineColor[4] = {.2f, 0, 0, 0}, bufferColor[4] = {0, .3f, 0, 0};
    auto buffer = [d newBufferWithBytes:bufferColor
                                 length:sizeof bufferColor
                                options:MTLResourceStorageModeShared];
    auto image = tex(16, 9);
    std::vector<unsigned char> blue(16 * 9 * 4, 0);
    for (unsigned i = 0; i < 16 * 9; ++i) {
      blue[i * 4 + 2] = 102;
      blue[i * 4 + 3] = 255;
    }
    [image replaceRegion:MTLRegionMake2D(0, 0, 16, 9)
             mipmapLevel:0
               withBytes:blue.data()
             bytesPerRow:64];
    auto sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    auto sample = [d newSamplerStateWithDescriptor:sd];
    bindings.bytes(false, inlineColor, sizeof inlineColor, 0);
    bindings.buffer(false, buffer, 0, 1);
    bindings.texture(false, image, 0);
    bindings.sampler(false, sample, 0);
    inlineColor[0] = .9f;
    buffer = nil;
    image = nil;
    sample = nil;
    auto pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [material newFunctionWithName:@"capture_vs"];
    pd.fragmentFunction = [material newFunctionWithName:@"binding_fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    NSError *pipelineError = nil;
    auto replayPipeline =
        [d newRenderPipelineStateWithDescriptor:pd error:&pipelineError];
    check(replayPipeline != nil, "binding replay pipeline compiled");
    auto replayTarget = tex(16, 9);
    auto cb = [q commandBuffer];
    auto rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = replayTarget;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto encoder = [cb renderCommandEncoderWithDescriptor:rp];
    [encoder setRenderPipelineState:replayPipeline];
    [encoder setFragmentTexture:scan
                        atIndex:0]; // A different existing binding must be
                                    // overwritten.
    bindings.apply(encoder);
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    [encoder endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    check(cb.status == MTLCommandBufferStatusCompleted,
          "binding replay command completed");
    auto result = readPixel(replayTarget);
    check(abs(int(result[0]) - 51) <= 1,
          "inline bytes survive source mutation after recording");
    check(abs(int(result[1]) - 77) <= 1,
          "bound buffer reused after caller release");
    check(result[2] == 102,
          "bound texture and sampler reused after caller release");
    // A Surface's texture replacement must not match the old pending pane.
    // The source holder pins the old actual object, preventing address reuse.
    MetalBackdropSource oldSource;
    auto originalSource = tex(16, 9);
    oldSource.texture = originalSource;
    const auto oldIdentity = oldSource.key();
    originalSource = nil;
    auto replacementSource = tex(16, 9);
    check(oldSource.texture != nil && oldSource.key() == oldIdentity,
          "pending capture retains actual source texture");
    check(!oldSource.matches(replacementSource) &&
              oldIdentity != metal_backdrop_source_key(replacementSource),
          "replaced texture cannot satisfy pending scan identity");
    check(!oldSource.clear(replacementSource) && oldSource.valid,
          "unrelated clear leaves capture valid");
    check(oldSource.clear(oldSource.texture) && !oldSource.valid &&
              !oldSource.matches(oldSource.texture),
          "clear invalidates active as well as pending source");
    reset_gamepad_bar_pipelines();
    puts("Metal offscreen checks passed");
  }
}
