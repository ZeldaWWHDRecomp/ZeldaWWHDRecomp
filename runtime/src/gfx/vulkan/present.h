#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <vulkan/vulkan.h>
#include "gfx/display.h"
struct ImDrawData;
namespace gfxvk {
struct Screen;
struct Surface;
struct Buffer;
Screen& presentation_source_screen(Screen& physical);
// one picture (scaled into box) or filled rectangle of a window composition
struct ComposeQuad {
 Surface* image=nullptr; bool sourceLinear=false; gfx::Box box; float alpha=1;
 bool solid=false; float color[4]{};
};
// this frame's layout from gfx/display_modes.cpp, set by swap() (null: the picture scaled to fit)
void set_present_plan(const gfx::PresentPlan* plan);
// Internal hidden-only bounded diagnostic; never reads back or presents its reused target.
void compose_headless_diagnostic(Screen& screen);
std::vector<ComposeQuad> screen_quads(Screen& screen,VkExtent2D target,int& filter);
// the composition of a window into an offscreen image, read back as display-encoded RGBA8
std::vector<uint8_t> compose_offscreen(Screen& screen,uint32_t width,uint32_t height,bool srgb);
// screenshot.h: the picture of `screen` at its own size (as the window composition draws it: FXAA,
// display encoding; no overlay) into an 8-bit image of the window's swapchain format (its pipeline
// exists already; RGBA8 like the present dumps without a swapchain; `bgra` says which), copied into
// `buffer` (a new readback buffer) in the current command buffer; read it once that submission
// completed. False: nothing recorded
bool record_screenshot(Screen& screen,Buffer& buffer,uint32_t& width,uint32_t& height,bool& bgra);
// the gameplay mod HUDs (climb's stamina wheel, the run/swim boost's bar), drawn into the TV scan
// image (as mods/mod_hud.mm does on Metal)
void draw_mod_overlay(Surface& scan);
// automatic GamePad overlay (display.mm): 32x18 signatures of the pictures (slot 0 GamePad, 1 TV)
bool record_signature(int slot,Surface& source,bool sourceLinear);
std::vector<float> read_signature(int slot);
void reset_signatures();
// capture.cpp: a colour surface read back as RGBA8 (encodeSrgb: linear values to display encoding)
std::vector<uint8_t> read_surface_rgba(Surface& source,bool encodeSrgb);
// Swapchain replacement calls reset only after the device is idle. Views and
// shared pipelines otherwise remain alive through submission completion.
// Detach old views without destroying them; the caller retains them until GPU completion.
std::vector<VkImageView> take_present_screen_views(Screen& screen);
void reset_present_screen(Screen& screen);
void prepare_present_screen(Screen& screen, bool colorAttachmentSupported, bool captureTransferSupported = false);
// Returns false when this swapchain requires the existing transfer-blit path.
bool draw_present_screen(Screen& screen, uint32_t imageIndex);
struct PresentImageParams {
 int32_t aa=0,conv=0;
 float sharp=1,foot=1,alpha=1,pad[3]{};
};
// Prepared on the renderer thread. Recording reads only these copied handles
// and values; the caller retains all referenced objects through GPU completion.
struct PresentImageDraw {
 VkDevice device=VK_NULL_HANDLE;
 VkPipeline pipeline=VK_NULL_HANDLE;
 VkPipelineLayout layout=VK_NULL_HANDLE;
 VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
 VkSampler sampler=VK_NULL_HANDLE;
 VkImageView source=VK_NULL_HANDLE,targetView=VK_NULL_HANDLE;
 VkImage target=VK_NULL_HANDLE;
 // Caller supplies the actual sampled layout; external images may stay GENERAL.
 VkImageLayout sourceLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 VkImageLayout oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
 VkImageLayout finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
 VkExtent2D extent{};
 VkViewport viewport{};
 PresentImageParams params;
};
std::optional<PresentImageDraw> prepare_present_image(Screen&,uint32_t imageIndex,
    const Surface& snapshot,bool sourceLinear,int filter,bool fxaa);
// Uses an independently owned command buffer and descriptor pool. Does not
// access renderer submission state, mutable resource caches or screen objects.
void record_present_image(const PresentImageDraw&,VkCommandBuffer,VkDescriptorPool);
// Presentation resources and dispatch belonging to a separate logical device.
// Device must enable dynamic rendering; source is a linearly sampleable RGBA8
// view with caller-owned layout/ownership synchronization. Caller owns target
// views, command/descriptor pools, and must drain all draws before destruction.
// Prepare and record are serialized by the owner, never mutate global dispatch.
class IndependentPresenter {
public:
 IndependentPresenter(VkDevice,bool dynamicRenderingKHR);
 ~IndependentPresenter();
 IndependentPresenter(const IndependentPresenter&)=delete;
 IndependentPresenter& operator=(const IndependentPresenter&)=delete;
 PresentImageDraw prepare(VkImageView source,VkExtent2D sourceExtent,bool sourceLinear,
     VkImage target,VkImageView targetView,VkFormat targetFormat,VkExtent2D targetExtent,
     VkImageLayout oldLayout,int filter,bool fxaa,
     VkImageLayout finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
 void record(const PresentImageDraw&,VkCommandBuffer,VkDescriptorPool) const;
private:
 struct Impl;
 std::unique_ptr<Impl> impl;
};
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
