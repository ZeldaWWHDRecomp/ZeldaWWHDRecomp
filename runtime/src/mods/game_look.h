// The game's look for the port's own pages (the story notebook, the item menu's settings page): the
// game's menu font and button icons, read glyph by glyph (story_notebook_font.h), the item menu's window
// art (story_notebook_style.h) and the boxes composed from it, all read at run time from the player's
// game through the GameUi services (story_notebook_game.h); nothing of the game is stored by the port.
// One Look per HUD owner: it turns a page's draw commands (Text and the "nb:" box markers) into HUD
// pictures and appends them to a HUD list.
//
// Also the small guest layout helpers the pages share: a screen's layout root, a pane by name, a pane's
// texture and its visibility (nw::lyt::Pane: name at +0x80, flags +0x44 bit 0 visible, children list
// sentinel +0x14).
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../game_glyph.h"
#include "guest_hud.h"
#include "story_notebook_font.h"
#include "story_notebook_game.h"
#include "story_notebook_style.h"

struct Cpu;

namespace game_look {

using guestmods::hud::Command;
using guestmods::hud::Handle;
namespace font = story_notebook::font;
namespace style = story_notebook::style;

// ---- guest layouts (game thread; every pointer is checked before it is followed) ----
bool guest_pointer(uint32_t a);
uint32_t layout_root(uint32_t screen);  // an hdParts_c screen: +0x44 layout, +4 nw::lyt::Layout, +0xC root pane
uint32_t find_pane(uint32_t pane, const std::string& name);  // depth first from `pane`, 0 if none
std::string pane_name(uint32_t pane);
bool visible(uint32_t pane);
void set_visible(uint32_t pane, bool on);
// a texture `offsets` pointer hops from `base`, decoded; w/h: the size it must have (0: any)
bool texture(uint32_t base, std::initializer_list<uint32_t> offsets, game_glyph::Bitmap& out, int w = 0, int h = 0);

// ---- the look ----
struct Look {
    std::string owner;  // the HUD owner its images belong to
    std::string tag;    // for the log ("notebook")
    const story_notebook::GameUi* ui = nullptr;  // null: the overlay font and plain boxes (unit tests)
    bool (*trace)() = nullptr;

    font::Face main, fallback, icons;  // CKingMain, CKingMsg (characters CKingMain lacks), the button icons
    bool font_failed = false, icons_probed = false;
    font::Atlas atlas;
    Handle atlas_image = 0;
    uint64_t store_generation = 0, fonts_key = 0;
    style::Art art;
    bool art_tried = false, art_ok = false;
    struct Box {
        Handle handle = 0;
        uint64_t store_generation = 0;
        std::shared_ptr<guestmods::hud::Image> image;
    };
    std::map<std::string, Box> boxes;  // "name:WxH" in pixels
    font::Prompts prompts;
    float font_scale = 1.05f, atlas_scale = 0.62f;
    // pixels per page unit a box is composed at (default: the game's art at 75 %)
    std::function<float(const std::string& name)> box_resolution;

    Look(std::string owner_, std::string tag_) : owner(std::move(owner_)), tag(std::move(tag_)) {}
    bool font_ready() const { return main.ready(); }
    // a line's width in page units in the game's font (0 while it isn't loaded)
    float measure(const std::string& text, float size) const;
    // the glyphs these lines need, from the game's fonts (game thread, `c` for the game's calls); true
    // when the menu font became usable with this call
    bool load_glyphs(Cpu* c, const std::vector<std::string>& lines);
    // the page's commands as HUD commands: text and boxes become pictures of their images (`images`
    // alongside, 0 for the others). `screen`: 0 TV, 1 GamePad (text is composed at its real pixel size).
    std::vector<Command> render(std::vector<Command> cmds, std::vector<Handle>& images, uint32_t screen);
    void append(Handle list, std::vector<Command>& cmds, const std::vector<Handle>& images) const;
    void forget_boxes() { boxes.clear(); }
};

}  // namespace game_look
