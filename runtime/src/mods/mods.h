// Gameplay options (Gameplay menu). Original behavior by default; each can be switched live.
//   camera.cpp  direct right-stick camera, mouse camera, first person on R3 / mouse wheel
//   lock_on.cpp optional enemy range and R3 cycling through the native eligible list
//   mouse.mm    mouse capture in the game window (macOS events)
//   turbo.cpp   quick doors and fast scene changes (extra logic steps while they run)
//   walk_doors.cpp  Quick doors' walk-through mode (doors open as Link comes near, no door scene)
//   instant_text.cpp instant text, no pauses in texts
//   cheats.cpp  items, sword/shield, stats, infinite health/magic/ammo, songs, Triforce, dungeon items
// Test/start-up switches: WWHD_MOD_<NAME>=1 (see mods.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "move_speed.h"  // MoveMode (the run/swim speed mod's public vocabulary)
#include "fast_forward.h"

struct Cpu;
namespace input { struct PadState; }

namespace mods {

// ---- switches (menu + env) ----
// Experimental partial coverage: Leaf gust yaw/pitch plus supported initial item facing.
bool camera_directed_items();
void set_camera_directed_items(bool on);
bool direct_camera();
int targeting_letterbox_mode(); // 0: On, 1: Off with enemies, 2: Always off during targeting
void set_targeting_letterbox_mode(int mode);
bool letterbox_while_targeting();
void set_letterbox_while_targeting(bool on);
void set_direct_camera(bool on);
float camera_speed();  // direct camera: multiplier of the game's own top turning speed
void set_camera_speed(float s);
bool mouse_camera();
void set_mouse_camera(bool on);
float mouse_sensitivity();  // degrees per mouse point
void set_mouse_sensitivity(float s);
bool first_person_wheel();
void set_first_person_wheel(bool on);
// One press readies and automatically uses an item against a locked enemy.
bool quick_use();
void set_quick_use(bool on);
float quick_use_speed();  // scoped equip/boomerang wind-up only; 1, 1.25 or 1.5 (default 1)
void set_quick_use_speed(float factor);
// Emulation-thread seam: accelerate update, or observe native key crossings (advance=false).
// Authored rates are restored before returning; crossing predicates are never altered.
void quick_use_animation_call(Cpu* c, void (*native)(Cpu*), bool advance = true);
void quick_use_filter_pad(input::PadState& s);
// Source-owned item handlers preserve the local ready-trigger rule without
// publishing synthetic pad input or changing the caller CPU registers.
void ship_normal_sail_wind(Cpu* c, uint32_t ship);
float ship_sail_target(float stock);
void quick_use_item_action_dispatch(Cpu* c, uint32_t link, uint32_t item);
uint32_t quick_use_ready_item_trigger(Cpu* c, uint32_t link, uint32_t native_trigger);
void quick_use_reset();  // drop transient/pending input on capture, focus loss and savestates
bool quick_doors();
void set_quick_doors(bool on);
// Quick doors' mode: Faster (door scenes run four times as fast) or Walk through (walk_doors.cpp: dungeon
// shutters and house doors open as Link comes near, he walks through himself, no door scene; the door scenes
// that still run, such as locked doors, run fast as in Faster)
enum DoorMode { kDoorFaster = 0, kDoorWalk = 1 };
DoorMode door_mode();
void set_door_mode(DoorMode m);
bool walk_doors();                // quick_doors() and door_mode() == kDoorWalk
bool walk_doors_by_a();           // walk-through doors open with A (the game's "Open" prompt), not on coming near
void set_walk_doors_by_a(bool on);
bool walk_door_exit();            // walk_doors.cpp: Link went into the doorway of a house door: its scene change runs fast
void walk_doors_reset();          // game thread after a save state load
// fpcM_Execute's hook (game_hooks.cpp): a door's execute runs walk_doors.cpp's for this call
class DoorExecuteWrap {
public:
    explicit DoorExecuteWrap(uint32_t proc);
    ~DoorExecuteWrap();
    DoorExecuteWrap(const DoorExecuteWrap&) = delete;
    DoorExecuteWrap& operator=(const DoorExecuteWrap&) = delete;
private:
    uint32_t slot_ = 0, game_ = 0;
};
bool fast_scenes();
void set_fast_scenes(bool on);
// Instant text and No pauses in texts (instant_text.cpp): message boxes show their whole text at once; the
// waits written into texts are left out. instant_text_step: interp.cpp, around the processes' execute.
bool instant_text();
void set_instant_text(bool on);
bool no_text_pauses();
void set_no_text_pauses(bool on);
void instant_text_step();

// Optional enemy targeting assistance. Off preserves the game's targeting unchanged.
bool extended_lock_on();
void set_extended_lock_on(bool on);
float lock_on_range(); // multiplier of native enemy acquisition/release distances, 1..3
void set_lock_on_range(float factor);
bool lock_on_cycling();
void set_lock_on_cycling(bool on);
void lock_on_filter_pad(input::PadState& s); // reserve R3 only while enemy-locked
void lock_on_reset(); // focus loss, overlays and save/load require a fresh press

bool move_speed();
void set_move_speed(bool on);
MoveMode move_speed_mode();                      // hold the button, or press it once
void set_move_speed_mode(MoveMode mode);
MoveAnim move_speed_anim();                      // native cadence, early dash, or additive sprint pose
void set_move_speed_anim(MoveAnim anim);
uint32_t move_boost_anim(uint32_t anm);          // the locomotion clip to use right now (getAnmData)
float move_speed_land_factor();                  // running (PROC_MOVE)
void set_move_speed_land_factor(float factor);
float move_speed_swim_factor();                   // swimming (PROC_SWIM_MOVE)
void set_move_speed_swim_factor(float factor);
float move_speed_stamina_seconds();              // seconds of boosting on a full bar; 0 = unlimited
void set_move_speed_stamina_seconds(float seconds);
float move_speed_cooldown_seconds();             // wait after the bar empties; 0 = none
void set_move_speed_cooldown_seconds(float seconds);
bool move_speed_bar();                           // "Show boost bar": the bar is drawn (the boost works either way)
void set_move_speed_bar(bool on);
uint32_t move_speed_button();
void set_move_speed_button(uint32_t button);
void move_speed_input(uint32_t buttons); // actual active-controller sample, including replay/held half steps
float link_move_factor(uint32_t link);
void move_start_effect(Cpu* c, uint32_t link); // consume a new grounded sprint's one-shot dust burst
void move_trail_effect(Cpu* c, uint32_t link); // small puffs behind the feet while running under boost
void move_swim_effect(Cpu* c, uint32_t link);  // splash per arm pull and a foam trail while swimming under boost

// ---- traversal mods (traversal.cpp; issues #95, #96). Factors are clamped to 1x..3x. ----
bool ladder_speed();                      // faster ladder climbing (the clips play faster)
void set_ladder_speed(bool on);
float ladder_speed_factor();
void set_ladder_speed_factor(float factor);
bool sidle_speed();                       // faster sidling along walls
void set_sidle_speed(bool on);
float sidle_speed_factor();
void set_sidle_speed_factor(float factor);
bool ivy_speed();                         // faster ivy climbing (the clips play faster)
void set_ivy_speed(bool on);
float ivy_speed_factor();
void set_ivy_speed_factor(float factor);
bool hang_speed();                        // faster moving along a ledge while hanging
void set_hang_speed(bool on);
float hang_speed_factor();
void set_hang_speed_factor(float factor);
bool sail_speed();                        // sailing speed, one factor per sail
void set_sail_speed(bool on);
float sail_speed_normal_factor();         // the normal sail
void set_sail_speed_normal_factor(float factor);
float sail_speed_swift_factor();          // the HD Swift Sail
void set_sail_speed_swift_factor(float factor);
bool auto_wind();                         // the wind follows the boat with the normal sail too
// ---- jump anywhere (jump.cpp; settings in traversal_settings.cpp) ----
bool jump_anywhere();                     // the jump button jumps from standing or running
void set_jump_anywhere(bool on);
bool double_jump();                       // once more in the air
void set_double_jump(bool on);
bool no_auto_jump();                      // the game's own jump off ledges becomes a plain fall
void set_no_auto_jump(bool on);
// Format 1 controls.json only (input_map::adopt_legacy_jump_button): the Wii U button Jump anywhere
// used before it had an input of its own (input_map kJump). One of L3, R3, L, R, ZL, ZR (default L).
uint32_t jump_button();
void set_jump_button(uint32_t button);
inline bool valid_jump_button(uint32_t b) {
    constexpr uint32_t allowed = 0x40000 | 0x20000 | 0x20 | 0x10 | 0x80 | 0x40;
    return b && !(b & (b - 1)) && (b & allowed);
}
void jump_input(uint32_t virt);                     // the virtual inputs (input::kVirt*) of every VPAD/KPAD read
void jump_step(Cpu* c, uint32_t link, bool before); // around daPy_lk_c::execute (climb.cpp)
void jump_reset();                                  // game thread after a state load
// Deku Leaf: wave to rise (jump.cpp, leaf_lift.h; settings in traversal_settings.cpp)
bool leaf_rise();                         // while gliding, each wave of the leaf lifts Link
void set_leaf_rise(bool on);
float leaf_rise_height();                 // percent of the default lift's height (25..400, default 100)
void set_leaf_rise_height(float percent);
void leaf_step(Cpu* c, uint32_t link);    // after daPy_lk_c::execute (climb.cpp)
void leaf_filter_pad(input::PadState& s); // the held button waves on (camera.cpp filter_pad)
// controller_play.cpp: L and d-pad down by whether Link plays (VPAD/KPAD full reads), and which
// on-screen Jump / Moon jump buttons show (input::kVirt* bits)
void pad_play_rules(input::PadState& p);
uint32_t jump_touch_buttons();
// ---- the item ring (item_ring.cpp; rules in item_ring.h) ----
// the active controller's pad read (VPADRead, or KPADReadEx in Pro Controller mode), after the play rules:
// X / Y / R held back or opening the ring; `repeat`: interpolation's repeated read (only the stick is kept)
void item_ring_pad(input::PadState& p, bool repeat);
// frame boundary (interp.cpp, every pass): the equip, the pace, the ring drawn. `full`: a logic step
// follows
void item_ring_service(Cpu* c, bool full);
void item_ring_reset();          // a state load
bool item_ring_open();
void item_ring_finger(int button, bool down, float dx, float dy);  // Android: the finger on X / Y / R
int item_ring_mode();            // item_ring::Mode: 0 off, 1 touch only (default), 2 touch and controllers
void set_item_ring_mode(int mode);
int item_ring_pace();            // item_ring::Pace: 0 normal, 1 slow motion (default), 2 paused
void set_item_ring_pace(int pace);
int item_button_names();         // item_ring::Names: 0 X Y R, 1 A1 A2 A3, 2 auto (default)
void set_item_button_names(int names);
const char* item_button_name(int button);  // as the names are now: "X" or "A1" ...
bool item_names_on_hud();                  // A1 A2 A3 now: the HUD's X, Y and R show them (button_glyphs.cpp)
// ---- the context buttons on the touch controls (context_touch.cpp; rules in touch_play.h) ----
// the active controller's pad read, after the item ring: items by tap, auto put-away, the ZL latch (touch play only)
void context_touch_pad(input::PadState& p, bool repeat);
namespace playctx { struct PlayContext; }
void context_touch_step(const playctx::PlayContext& c);  // each logic step, after the context was read
void context_touch_service();                            // frame boundary (tests: WWHD_CONTEXT_ASSETS)
bool context_touch_hide_cluster();  // the game's own button cluster is hidden now (aspect.cpp)
bool context_touch_aim_latched();   // an item is latched by a tap (no jump buttons then)
void context_touch_pack(int32_t* out, int n);       // context_touch.h pack (Java)
int context_touch_assets(const std::string& dir);   // 0 not yet, -1 failed, > 0 generation
bool context_buttons_on();
void set_context_buttons_on(bool on);
bool item_tap_on();
void set_item_tap_on(bool on);
bool aim_only_on();
void set_aim_only_on(bool on);
bool auto_put_away_on();
void set_auto_put_away_on(bool on);
int zl_latch_mode();  // touchplay::ZlLatch: 0 off, 1 let go (default), 2 next target
void set_zl_latch_mode(int mode);
// ---- talks, answers, shops... by touch or mouse, and hold to go on (touch_talk.cpp; rules in touch_talk.h) ----
// the active controller's pad read, after the touch controls' rules: the fingers' presses and stick pushes,
// the held A; `repeat`: interpolation's repeated read (the pushed stick stays)
void touch_talk_pad(input::PadState& p, bool repeat);
void touch_talk_reset();  // a state load
// code mods (guest_mods.cpp, guest_live.h): the touches their claims got and their input filters, on the game's
// controller reads after the port's own filters
void guest_pad_filter(Cpu* c, input::PadState& p, bool repeat);
// a finger on the TV picture (Finger::Kind: 0 down, 1 move, 2 up, 3 cancel), layout units (centre, +y up)
void touch_talk_finger(int kind, float x, float y);
bool touch_talk_claims();  // a finger landing on the picture is the talk's now
bool touch_talk_claims_at(float x, float y);  // ... or a code mod's at this point (Android: nativeFreeTouch)
bool guest_touch_claimed_at(float x, float y);
bool touch_talk_mod_pinch(int phase, float scale, float x, float y);  // Android: two fingers, the first a code mod's  // a code mod's claim alone (Android: a finger the hidden controls pass on)
bool touch_talk_on();
void set_touch_talk_on(bool on);
bool talk_hold_ff_on();
void set_talk_hold_ff_on(bool on);
bool pad_hold_a_on();
void set_pad_hold_a_on(bool on);
// ---- camera extras (steer.cpp; rules in steer.h): walking direction, camera swing, boat speed, pinch FOV ----
int steer_mode();                  // steer::Mode: 0 as the game (default), 1 kept while the player turns the camera, 2 while the stick is held
void set_steer_mode(int mode);
bool camera_swing();               // the camera swings behind Link while he runs
void set_camera_swing(bool on);
bool direct_boat_speed();          // the direct camera as fast on the boat as on foot (default on)
void set_direct_boat_speed(bool on);
bool pinch_fov_on();               // Android: a pinch changes the field of view (default on)
void set_pinch_fov_on(bool on);
float steer_swing_degrees(bool player_camera);  // camera.cpp, once per logic step
bool steer_pinch(int phase, float d);           // 0 begin (true: on), 1 move, 2 end
void steer_reset();                // a state load
bool camera_turned_by_player();    // camera.cpp: the right stick, the mouse or a swipe turned the camera in the last steps
// ---- One screen (handheld) mode (one_screen.cpp; rules in ../one_screen.h) ----
bool one_screen_enabled();         // on for this session (decided at start)
bool one_screen_active();          // ... and the controller acts as the GamePad
bool one_screen_off_tv();          // ... and the game plays Off-TV now
bool one_screen_next();            // the choice for the next start
void set_one_screen_next(bool on);
void one_screen_pad(input::PadState& p);         // VPADRead full reads: Minus as Plus
bool one_screen_hidden_pane(const char* name);   // the GamePad's slide-conducting guide in Off-TV Play
// ---- HUD fades (hud_fade.cpp; rules in hud_fade.h): the game's hearts, rupees, magic bar ----
bool hud_fade_on(int element);  // hudfade::Element
void set_hud_fade_on(int element, bool on);
void hud_fade_step(const playctx::PlayContext& c);  // each logic step, after the context was read
float hud_fade_alpha_of(uint32_t pane);  // Pane::CalculateMtx: the alpha factor of a faded pane (1: none)
int hud_pane_hide(uint32_t pane);
bool large_picto_on();  // the large Picto Box view: its finder's frame not drawn (hud_fade.cpp)
void set_large_picto_on(bool on);  // Pane::Draw: 0 draw, 1 skip (a faded-out element), 2 skip in TV layouts (the hidden cluster)
bool story_system();                      // letters, quests, the story notebook, mod charts (mods/required.h)
void set_story_system(bool on);
void set_auto_wind(bool on);

// hooks' helpers (traversal_settings.cpp, traversal.cpp)
bool traversal_anim();                          // a mod that scales Link's clips is on (ladders, sidling)
float traversal_anim_factor(uint32_t proc);    // setFrameCtrl: rate factor in Link's procedure `proc` (1 = stock)
float traversal_getter_factor(int getter, uint32_t proc);  // traversal::AnmGetter's rate factor (1 = stock)
// mMaxNormalSpeed x the sidle factor for the length of one call (setNormalSpeedF with its steps,
// setBlendWHideMoveAnime), while Link sidles with the mod on; a no-op otherwise
struct SidleScope { uint32_t link = 0, stock = 0, scaled = 0; };
SidleScope sidle_scope_begin(Cpu* c, bool speed_steps);
void sidle_scope_end(const SidleScope& scope);

// The boost's on-screen bar (Vulkan present.cpp, Metal mod_hud.mm), read from the render thread.
struct MoveHud {
    float stamina;    // 0..1
    float alpha;      // 0 = hidden
    bool boosted;     // a boost cycle is running (including a forward roll within that cycle)
    bool swimming;    // ... while swimming: the bar takes its colour
    bool exhausted;   // out of stamina: the bar is refilling over the cooldown
};
MoveHud move_hud();
// ---- the gameplay HUDs and the game's own screens (hud_gate.cpp) ----
// once per frame on the game thread (interp.cpp, frame start): reads whether a game menu, an event
// or a message is up and advances the HUDs' fade over it
void hud_service();
// what the game shows over gameplay now: an event, a message, a menu (hud_visibility.h GameUi); game thread
struct GameUi;
GameUi game_ui_now();
// 0..1, multiplies the boost bar's and the climb wheel's own alpha (mod_hud.mm, vulkan/present.cpp):
// 0 while a game menu, a cutscene/event or a dialogue is shown. Any thread.
float game_ui_hud_alpha();
// frame interpolation: after each drawn pass (interp.cpp), its fraction between the logic steps; the
// HUDs as that frame shows them (render thread; climb.h ClimbHud)
void hud_drawn(float t, uint64_t step, bool interpolating);
MoveHud move_hud_drawn();

// ---- input (input.mm) ----
// called at the end of input::read(): synthetic stick and buttons (mouse camera, R3 pulses)
void filter_pad(input::PadState& s);
// a key went down in the game window (before the game's own mapping); true = consumed
bool host_key_down(uint16_t code);

// ---- mouse (mouse.mm -> camera.cpp) ----
void mouse_add(float dx, float dy);  // points moved while captured (dy > 0 = down)
void mouse_wheel(float dy);          // wheel / scroll (dy > 0 = forward)
// swipe camera (Android touch controls): a finger moved by (dx, dy) pixels on a display of `dpi`
// (dy > 0 = down); turned into degrees by camera_swipe.h with the touch options' swipe speed
void swipe_add(float dx_px, float dy_px, float dpi);
void mouse_button(int button, bool down);  // 0 left, 1 right
bool mouse_captured();
void mouse_init(void* tv_window);  // NSWindow* of the TV picture (menu.mm, main thread)
void mouse_release();  // mouse.mm: release the pointer (also called when the mod is switched off)

// ---- logic (interp.cpp: fpcEx_Handler) ----
// after a normal logic step: runs extra steps while a door event or a scene change is in progress
void after_execute(Cpu* c, uint32_t execute_fn);

// ---- cheats (cheats.cpp): one-shot save data edits, applied at the top of the next frame ----
enum Cheat {
    kCheatItems = 1, kCheatSword = 2, kCheatStats = 4,
    // story progress (can change or break story events)
    kCheatSongs = 8, kCheatTriforce = 16, kCheatDungeon = 32, kCheatKey = 64,
    kCheatBags = 128,  // spoils and bait bag filled (not story progress)
    kCheatFigurines = 256,  // every Nintendo Gallery figurine (Carlov then leaves, as in the game's ending)
};
void request_cheat(int which);  // any thread
// the cheats that pause Achievements now, as the menus name them: the infinite ones that are on, and the one-shot
// cheats and story-item grants used in this run (any thread)
std::vector<std::string> cheats_pausing();
// Explicit, one-shot inventory grants. The first request needs consent; no grant precedes a backup.
enum StoryItem { kStoryNayru = 1, kStoryDin = 2, kStoryFarore = 4, kStorySword = 8 };
bool story_item_confirmation_needed();
void request_story_item(int which, bool confirmed = false);
enum Infinite {
    kInfHealth = 1, kInfMagic = 2, kInfAmmo = 4, kMoonJump = 8, kMoonNoFall = 16,
    kInfRupees = 32, kInfBags = 64, kInfBreath = 128,
};
bool infinite(int which);
void set_infinite(int which, bool on);
void moon_jump_input(const input::PadState& pad);
void moon_jump_camera(uint32_t camera);
void moon_jump_reset(); // game thread after state restore
void moon_jump_step(Cpu* cpu, uint32_t link, bool before);
bool moon_jump_move(uint32_t link, float& x, float& z);
void cheats_service();          // game main thread, frame start (interp.cpp)
// Beedle's Shop Ship and the other ships entered from the sea: the sea room their way out sails back to is made sure
// of (mods/shop_ship.h); game main thread, frame start (interp.cpp)
void shop_ship_service();

// shared helpers (mods.cpp)
uint64_t step();    // full logic steps so far (interp::logic_steps)
double game_time(); // seconds of game time (steps / 30)
bool trace_on();    // WWHD_MODS_TRACE: log mod decisions
void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace mods
