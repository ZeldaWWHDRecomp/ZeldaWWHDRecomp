// Story UI layouts: layouts of our own (Layout/<name>.szs) shown with the game's item screen, built,
// animated, drawn and freed by the game's own UI framework (HD decomp FW-layout_mgr / FW-parts_*).
// Several clients may have a layout at once (the notebook's page, the Delivery Bag's extra pages): each is
// built, drawn and freed on its own (one that fails doesn't stop the others), in the clients' draw order
// (Client::order, story_lyt_clients.h); the six game functions are hooked once for all of them.
//
//   built   when the game builds its item screen (hdIngameLoadItemScreen, with the item screen's
//           projection and matrix): hdLayoutResMgr_c::cache(name), NoControlPart_l, attachArchives,
//           hdPartsLayout_c + vLoad, then the client's animations bound (bindGroup); then the client's
//           built() (text pane capacities and first texts, story_lyt_text.h)
//   drawn   every time the item menu's screens are drawn and the item screen was among them
//           (hdIngameUi_c::drawPrimaryScreens on the GamePad; drawHudScreens on the TV, where the item menu
//           is with the Pro Controller; while ours covers the menu, the game's selection cursor
//           (hdCursorMgr_c::draw, a later pass on the TV) is not drawn): after all of them (sub window, menu
//           buttons, cursor, messages), with the item screen's viewport; the client's frame() decides (and
//           sets panes / plays animations), then hdParts_c::calcLayout + draw of our parts
//   freed   when the game frees its screens (hdIngameUi_c::finalizeScreens): our parts' finalize + delete,
//           as the game does for its own screens. The archive stays in the layout resource cache (the
//           game's own screens' archives do too) and is reused when the item screen is built again.
//   state load  before their next use the handles are checked against the loaded memory (our objects'
//           vtables and links as built): kept when the objects are there (a portable state, or a full state
//           saved after the build), else dropped without touching the memory. The loaded game keeps the
//           item screen it was saved with (the game builds one only with a stage or controller change), so
//           a dropped layout (or one never built, a load from the title screen) is built for that item
//           screen the next time it is drawn, with the projection and matrix the game's screen creation
//           gives it (the GamePad's render layer); until then nothing is drawn (the client falls back).
//
// The archive file: archive_for() serves the client's archive for the game's read of
// Layout/<name>.szs (hle/fs.cpp). Nothing is hooked without a client.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct Cpu;

namespace story_lyt_screen {

struct Anim {
    std::string name, group;  // anim/<layout>_<name>.bflan, bound to the group's panes
    uint32_t slot = 0;        // play slot: animations of one slot replace each other
};

struct Client {
    std::string layout;        // the layout's name (Layout/<layout>.szs, blyt/<layout>.bflyt)
    std::string archive;       // the archive file on the host
    std::vector<Anim> anims;   // the animation set's entries, in this order
    // further archives the layout's resource accessor gets after its own (hdParts_c::attachArchives): the
    // game's layout archives the layout names textures of (txl1 "<name>^<suffix>.bflim", e.g. the sea
    // chart's Link icon); only archives the game keeps resident (the layout resource manager's cache);
    // one that isn't there is skipped by the game
    std::vector<std::string> game_archives;
    std::function<void(Cpu*, uint32_t lyt)> built;   // right after the build (lyt: the layout object)
    std::function<bool(Cpu*, uint32_t lyt)> frame;   // every item screen draw: true draws the layout
    std::function<void()> before;                    // before the item menu's screens are drawn (each pass)
    std::function<void()> after;                     // restore temporary changes after the game screen pass
    std::function<void()> finalized;                 // screens are going away: discard client UI state
    std::function<bool()> covers;                    // the client's page is up (before ours draws it, too):
                                                     // the menus' selection cursor is not drawn
    const char* owner = "story layout";              // for the log and hook conflicts
    int order = 0;                                   // draw order: lower first (below the others)
    bool draw_hides_cursor = true;                   // while drawn, the menus' selection cursor is not (else
                                                     // only while covers() says so)
};

// The clients (before install()): add_client returns the client's id (0, 1, ... in the order added).
// set_client is add_client for the first client (id 0, the story notebook's page): the calls below
// without an id are that client's.
using Id = int;
Id add_client(Client client);
void set_client(Client client);
bool has_client();  // any client
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
// The hooks (only with a client, and only when none of the six game functions is replaced already:
// replaced_by(addr) names who replaces it, "" if nobody).
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
void state_loaded();
void service(Cpu* c);  // at the frame boundary (main thread): builds after a state load (see "state load")

// the host file for a game read of `guest` (Layout/<layout>.szs of a client), else ""
std::string archive_for(const std::string& guest);

// ---- for the client's callbacks (game thread, layout built); each also with the client's id first ----
bool ready();                                // the layout is built
uint32_t lyt();                              // the layout object (hdPartsLayout_c +4), 0 if not built
uint32_t anim_set();                         // its animation set (entries in Client::anims order), 0 if none
uint32_t pane(Cpu* c, const char* name);     // a pane by name (cached), 0 if there is none
// pane fields (HD decomp hdPartsPaneSRT_c: translate 0x1C, size 0x3C, flags 0x44 bit 0 visible, alpha
// 0x45; a changed translate / size sets flag 0x10, the game's own way of asking for a new matrix)
void set_visible(uint32_t pane, bool visible);
void set_alpha(uint32_t pane, uint8_t alpha);
void set_translate(uint32_t pane, float x, float y);
void set_size(uint32_t pane, float w, float h);
// animations by name (Client::anims): play from the start at `rate`; jump to a frame; finished?
void play(Cpu* c, const std::string& anim, float rate = 1.0f);
void set_frame(Cpu* c, const std::string& anim, float frame);
bool ended(Cpu* c, const std::string& anim);
bool ready(Id id);
uint32_t lyt(Id id);
uint32_t anim_set(Id id);
uint32_t pane(Cpu* c, Id id, const char* name);
void play(Cpu* c, Id id, const std::string& anim, float rate = 1.0f);
void set_frame(Cpu* c, Id id, const std::string& anim, float frame);
bool ended(Cpu* c, Id id, const std::string& anim);

// A texture of the built layout (timg/<file>, e.g. "NbPortrait0_00^l.bflim"), found in the archive in guest
// memory (hdLayoutResMgr_c::find): its RGBA8 texels replaced by `rgba` (w x h: the texture's own size).
// The texture keeps being drawn from that memory, so the picture changes in place. False if there is no
// such RGBA8 texture of that size.
bool set_texture_pixels(const std::string& file, const uint8_t* rgba, uint32_t w, uint32_t h);
bool set_texture_pixels(Id id, const std::string& file, const uint8_t* rgba, uint32_t w, uint32_t h);

uint32_t safe_string(const char* text);  // a sead SafeString in guest memory (kept for the session)

}  // namespace story_lyt_screen
