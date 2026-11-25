#pragma once
#include <cstdint>
struct ImDrawData;
struct ImDrawList;
struct ImDrawCmd;
namespace overlay::guesthud {
bool active(); // includes renderer texture retirement work
// physical presentation pixels: the TV picture (the file select's quit button) and the game's picture
// (gfx::game_picture_box: the code mods' TV layer; the GamePad picture in One screen mode's Off-TV Play)
void set_regions(float tv_x,float tv_y,float tv_width,float tv_height,bool tv_visible,
                 float x,float y,float width,float height,bool visible);
void blend_callback(const ImDrawList*,const ImDrawCmd*); // backend-recognized alpha/additive marker
// half the extra width TV edge anchors follow (aspect.h's HUD constraint); NaN keeps the picture's shape
void set_tv_hud_anchor_extra(float extra);
bool image_texture(uint64_t id); // PNG samples are display-encoded
void backend_destroyed(); // after the backend releases registered GPU textures
void frame();  // after ImGui::NewFrame(), before ImGui::Render()
ImDrawData* gamepad_frame(float width,float height); // copied draw data, valid until next call
ImDrawData* gamepad_region(float target_width,float target_height,float x,float y,float width,float height,float opacity);
}
