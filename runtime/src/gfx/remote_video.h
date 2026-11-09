// The GamePad screen for the iPhone GamePad (remote_video.mm, remote_pad.h). Metal renderer only.
#pragma once
#import <Metal/Metal.h>

namespace remote_video {
// Render thread, once per present: a target to draw the GamePad picture into (as for the GamePad window),
// or nil (no phone wants it, the encoder is behind). drc_w/drc_h: the picture's nominal size.
id<MTLTexture> begin_frame(id<MTLDevice> device, bool srgb, float drc_w, float drc_h);
// after drawing into it, in the command buffer that draws it: encoded and sent once the GPU is done
void end_frame(id<MTLCommandBuffer> cb);
}
