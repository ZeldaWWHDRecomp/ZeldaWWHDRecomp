#pragma once
#include "gamepad_backdrop.h"
#import <Metal/Metal.h>
namespace gfx {
// Explicit inputs: independent of AppKit windows and the renderer singleton.
// content is local to viewport; normal scanout/touch geometry is never
// modified.
void gamepad_draw_bars(id<MTLDevice>, id<MTLRenderCommandEncoder>,
                       MTLPixelFormat, unsigned width, unsigned height,
                       gamepad_backdrop::Rect viewport,
                       gamepad_backdrop::Rect content, id<MTLTexture> scan,
                       id<MTLTexture> backdrop, bool sourceLinear,
                       float opacity);
// Capture pipeline only: keep RGB blend factors, store coverage alpha (opaque
// RGB retains A=1). Exposed here so offscreen tests exercise the actual policy.
void gamepad_capture_coverage(MTLRenderPipelineColorAttachmentDescriptor *,
                              bool opaque);
void reset_gamepad_bar_pipelines();
} // namespace gfx
