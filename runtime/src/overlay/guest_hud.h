#pragma once
#include <cstdint>
struct ImDrawData;
struct ImDrawList;
struct ImDrawCmd;
namespace overlay::guesthud {
bool active(); // includes renderer texture retirement work
void set_tv_region(float x,float y,float width,float height,bool visible);
void blend_callback(const ImDrawList*,const ImDrawCmd*); // backend-recognized alpha/additive marker
bool image_texture(uint64_t id); // PNG samples are display-encoded
void backend_destroyed(); // after the backend releases registered GPU textures
void frame();  // after ImGui::NewFrame(), before ImGui::Render()
ImDrawData* gamepad_frame(float width,float height); // copied draw data, valid until next call
ImDrawData* gamepad_region(float target_width,float target_height,float x,float y,float width,float height,float opacity);
}
