#pragma once
#include "backend.h"
#include "deferred_commands.h"
#include "../stereo.h"
namespace gfxvk {
extern int stereoEye;
extern Screen stereoRight;
bool stereo_light_atlas(uint32_t width, uint32_t height, uint32_t layers, bool depth);
decltype(Renderer::surfaces)& stereo_surface_cache(const SurfaceDesc& desc);
decltype(Renderer::surfaces)& stereo_copy_cache(uint32_t addr,uint32_t width,uint32_t height,uint32_t layers);
// While stereo is on, each eye records into its own command stream: eye 0 into the renderer's,
// eye 1 into a held one (eye_deferred_commands). Selecting an eye keeps both render passes
// open; on a tile-based GPU ending a pass stores and reloads its attachments, so ending it at
// every switch cost several times the frame (issue #248). Eye 1's commands are emitted as one
// run after eye 0's pass, at every submission and wherever the eyes must stay ordered
// (stereo_flush). The eyes share only buffers (never overwritten while recorded commands use
// them) and the sun atlas, which eye 0 writes and transitions first.
void stereo_select_eye(int eye);
void stereo_flush();
void stereo_invalidate(uint32_t flags, uint32_t addr, uint32_t size);
// Operations other than draws keep the earlier interleaved order: eye 1's held commands are
// emitted first, then each eye runs in turn in the renderer's stream, ending its pass.
struct StereoInOrder {
    StereoInOrder();
    ~StereoInOrder();
    StereoInOrder(const StereoInOrder&) = delete;
    StereoInOrder& operator=(const StereoInOrder&) = delete;
};
// Wrap complete guest operations, including clears/copies, so intermediate
// color, depth, history and alias images stay in the corresponding eye's graph.
template<class F> void stereo_pair(F&& f, bool force = false) {
    if (stereo::frame.mode == stereo::Off && !force) {
        stereo_select_eye(0);
        f();
        return;
    }
    // Clears and copies may reach the shared sun atlas (stereo_copy_cache).
    // Canonical guest reads explicitly select the left eye.
    StereoInOrder ordered;
    try {
        stereo_select_eye(0);
        f();
        stereo_select_eye(1);
        f();
        stereo_select_eye(0);
    } catch (...) { stereo_select_eye(0); throw; }
}
// Renderer-owned commands (command buffer begin, submission) go to the renderer's stream
// even while eye 1 is selected.
struct RendererCommands {
    DeferredCommands* eye = eye_deferred_commands;
    RendererCommands() { eye_deferred_commands = nullptr; }
    ~RendererCommands() { eye_deferred_commands = eye; }
    RendererCommands(const RendererCommands&) = delete;
    RendererCommands& operator=(const RendererCommands&) = delete;
};
void stereo_release_ao(); // retire only eye 1's scratch images
bool stereo_screen_target(uint32_t width, uint32_t height);
void stereo_latch();
void load_saved_stereo_hdr(); // start-up, both hosts: 3D and HDR settings saved by the overlay
}
