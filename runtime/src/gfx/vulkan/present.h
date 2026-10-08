#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "gfx/display.h"
struct ImDrawData;
namespace gfxvk {
struct Screen;
struct Surface;
struct Buffer;
// one picture (scaled into box) or filled rectangle of a window composition
struct ComposeQuad {
 Surface* image=nullptr; bool sourceLinear=false; gfx::Box box; float alpha=1;
 bool solid=false; float color[4]{};
};
// this frame's layout from gfx/display_modes.cpp, set by swap() (null: the picture scaled to fit)
void set_present_plan(const gfx::PresentPlan* plan);
std::vector<ComposeQuad> screen_quads(Screen& screen,VkExtent2D target,int& filter);
// the composition of a window into an offscreen image, read back as display-encoded RGBA8
std::vector<uint8_t> compose_offscreen(Screen& screen,uint32_t width,uint32_t height,bool srgb);
// screenshot.h: the picture of `screen` at its own size (as the window composition draws it: FXAA,
// display encoding; no overlay) into an 8-bit image of the window's swapchain format (its pipeline
// exists already; RGBA8 like the present dumps without a swapchain; `bgra` says which), copied into
// `buffer` (a new readback buffer) in the current command buffer; read it once that submission
// completed. False: nothing recorded
bool record_screenshot(Screen& screen,Buffer& buffer,uint32_t& width,uint32_t& height,bool& bgra);
// the climb mod's stamina wheel, drawn into the TV scan image (as mods/climb_hud.mm)
void draw_mod_overlay(Surface& scan);
// automatic GamePad overlay (display.mm): 32x18 signatures of the pictures (slot 0 GamePad, 1 TV)
bool record_signature(int slot,Surface& source,bool sourceLinear);
std::vector<float> read_signature(int slot);
void reset_signatures();
// capture.cpp: a colour surface read back as RGBA8 (encodeSrgb: linear values to display encoding)
std::vector<uint8_t> read_surface_rgba(Surface& source,bool encodeSrgb);
// Swapchain replacement calls reset only after the device is idle. Views and
// shared pipelines otherwise remain alive through submission completion.
void reset_present_screen(Screen& screen);
void prepare_present_screen(Screen& screen, bool colorAttachmentSupported, bool captureTransferSupported = false);
// Returns false when this swapchain requires the existing transfer-blit path.
bool draw_present_screen(Screen& screen, uint32_t imageIndex);
// Opt-in one-shot actual swap-image capture. Record before normal submit;
// finish only after that submission's fence has completed (no extra flush).
bool present_capture_requested();
void record_present_capture(Screen& screen, uint32_t imageIndex);
void finish_present_capture(Screen& screen);
void write_rgba_png(const std::string& path, uint32_t width, uint32_t height,
                    const std::vector<uint8_t>& rgba);
// settings overlay (overlay.cpp): this frame's Dear ImGui draw data, drawn on top of the TV window's
// composition (swap image and present dumps); null when the overlay shows nothing
void set_overlay_draw(ImDrawData* draw);
void overlay_renderer_init();
void overlay_prepare(ImDrawData* draw);  // texture uploads (outside rendering)
void overlay_draw(ImDrawData* draw,VkCommandBuffer cmd,VkFormat format,VkExtent2D extent,bool linear);
void reset_overlay_resources();
// Device shutdown/recreation only: drain submissions before destroying these.
void reset_present_resources();
}
