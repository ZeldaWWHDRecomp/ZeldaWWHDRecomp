// The get moment of a mod item in a treasure chest (#269): the chest's get-item demo item is the game's
// stand-in item (story_treasure_chest.h kStandIn, a card); instead of the card, the mod item's own icon
// (its manifest "icon", the same picture as in the Delivery Bag) is shown where the game holds the item
// up over Link's head, from the moment the game shows the demo item until Link has the item.
//
// The demo item stays the game's own, fully made actor (created, executed and deleted by the game with the
// stand-in's resources); only its draw is replaced while it stands for a mod item: the icon is a picture of
// the port's HUD layer (guest_hud.h, the same layer the guest mods' HUDs use) at the demo item's place,
// projected with the game camera, its size following the distance like a model's. An item with a held model
// and "hold_up" shows that model instead (story_item_models::draw_demo_item is asked first); the icon is the
// fall-back while the model isn't ready.
//
// The hook: daDitem_Draw (0x02123B60), the demo item's draw, installed with the mod items (or in the editor
// launch mode). Every other demo item (the game's items) is drawn by the game as before.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Cpu;

namespace story_item_get_icon {

// The game camera as drawn last (editor::CameraView's fields: camera_class eye, center, up, its 4x4
// projection matrix, fovy and aspect)
struct Camera {
    bool valid = false;
    float eye[3] = {}, center[3] = {}, up[3] = {};
    float fovy = 0, aspect = 0;
    float proj[16] = {};
};
// ---- pure (unit-tested) ----
// A world point on the TV in the HUD's units (1280 x 720, centre anchor; a wider picture widens both the
// game's projection and the HUD's canvas alike), and the size there of `world_size` units at that distance.
// False when the point is behind or too close to the camera.
bool screen_point(const Camera& c, const float p[3], float world_size, float& x, float& y, float& size);
inline constexpr float kWorldSize = 32.f;  // about the size of the game's held-up item models

// ---- the game side ----
using Picture = std::vector<uint8_t>;  // 128 x 128 RGBA (item_icons::picture)
void configure(void (*original)(Cpu*, uint32_t), Camera (*camera)(), Picture (*load)(const std::string& png));
// the stand-in's demo item stands for this mod item (its icon, story_items::Item::icon_path), until off
void show(uint16_t key, const std::string& icon_png);
void hide();
uint16_t showing();  // tests: the key shown (0: none)
// The demo item's draw hook (only when nobody else replaces daDitem_Draw); false and nothing installed on a
// conflict
bool install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
void logic(uint64_t step);  // each logic step: the icon goes when the demo item stopped drawing
void state_loaded();        // the HUD layer was reset: the picture is made again when needed
void reset();               // tests
namespace hooks {
void demo_item_draw(Cpu* c);  // daDitem_Draw(item)
}

}  // namespace story_item_get_icon
