/* Wind Waker HD recomp: guest mod SDK (Mod SDK v2, prototype). See docs/mod-sdk-v2.md.
 *
 * Guest mods are C compiled for the console CPU (32-bit big-endian PowerPC, the game's ABI) with a
 * freely available clang:
 *
 *   clang --target=powerpc-unknown-eabi -mcpu=750 -O2 -G0 -ffreestanding -fno-builtin -nostdlib
 *         -fno-jump-tables -ffunction-sections -fdata-sections -I<sdk>/include -c mod.c -o mod.o
 *   ld.lld -m elf32ppc -r mod.o [more.o ...] -o mod.elf          (one relocatable ELF per mod)
 *
 * The player's installation translates mod.elf to C and compiles it with its local compiler
 * (tools/guestmod/build_guest_mod.py). Nothing from the game is part of this header: declare the game
 * functions and data you use by address (WWHD_GAME_FUNC / WWHD_GAME_DATA), e.g. from the public
 * GameCube decompilation (zeldaret/tww, CC0) plus the HD addresses.
 */
#pragma once

/* Old SDKs embedded data pointers without relocations and cannot safely run on EU. */
static const unsigned int wwhd_address_format __attribute__((used, section(".wwhd_addresses"))) = 1;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef float f32;
typedef double f64;

#define WWHD_GUEST_API_VERSION 1

/* ---- hooks: one descriptor per hook in section .wwhd_hooks (read by the translator) ---- */
typedef struct { u32 kind, target; void* func; u32 flags; } wwhd_hook_desc;
#define WWHD_KIND_REPLACE 1
#define WWHD_KIND_ENTRY 2
#define WWHD_KIND_RETURN 3
#define WWHD__CAT2(a, b) a##b
#define WWHD__CAT(a, b) WWHD__CAT2(a, b)
#define WWHD__DESC(kind, addr, fn)                                                                   \
    __attribute__((section(".wwhd_hooks"), used)) static const wwhd_hook_desc WWHD__CAT(wwhd__h_, fn) = \
        {kind, addr, (void*)&fn, 0}

/* Replace the game function at `addr` completely (one mod per function):
 *   WWHD_REPLACE(0x0200ED84, void, my_addcalc2, (f32* v, f32 target, f32 scale, f32 max_step)) { ... } */
#define WWHD_REPLACE(addr, ret, name, params) \
    static ret name params;                   \
    WWHD__DESC(WWHD_KIND_REPLACE, addr, name); \
    __attribute__((noinline, used)) static ret name params

/* Run before the function (gets its arguments; changes to argument registers are discarded, write
 * through pointers instead) or after it (gets the arguments again; the return value is kept). */
#define WWHD_HOOK(addr, name, params) \
    static void name params;          \
    WWHD__DESC(WWHD_KIND_ENTRY, addr, name); \
    __attribute__((noinline, used)) static void name params
#define WWHD_HOOK_RETURN(addr, name, params) \
    static void name params;                 \
    WWHD__DESC(WWHD_KIND_RETURN, addr, name); \
    __attribute__((noinline, used)) static void name params

/* ---- the game ---- */
#define WWHD__STR2(x) #x
#define WWHD__STR(x) WWHD__STR2(x)
/* call a game function (goes through the game's dispatch, so other mods' hooks apply) */
#define WWHD_GAME_FUNC(addr, ret, name, params) ret name params __asm__("__wwhd_game_" WWHD__STR(addr))
/* call the game's own code of a function, below every mod's hook/replacement */
#define WWHD_GAME_ORIGINAL(addr, ret, name, params) ret name params __asm__("__wwhd_orig_" WWHD__STR(addr))
/* a game variable at a fixed address (MEM2 data of the USA game) */
#define WWHD_GAME_DATA(addr, type) (*({ \
    extern u8 WWHD__CAT(wwhd_game_data_, addr)[] __asm__("__wwhd_gdata_" WWHD__STR(addr)); \
    (type volatile*)WWHD__CAT(wwhd_game_data_, addr); }))

/* ---- host services (resolved by name on install; missing ones are an install error) ---- */
void wwhd_log(const char* message);
void wwhd_log_int(const char* label, int value);
void wwhd_log_hex(const char* label, u32 value);
void wwhd_log_float(const char* label, double value);
/* integer option `id` of this mod, or `fallback` */
int wwhd_config_int(const char* id, int fallback);
/* Other typed manager options. Values are those of the start, except options the manifest marks "live": true,
 * which follow changes made in Settings or the item menu's page while the game runs. Strings include enum options.
 * config_string copies at most capacity-1 bytes, terminates them and returns bytes copied;
 * a missing/wrong-type option returns zero without modifying the buffer. */
int wwhd_config_bool(const char* id, int fallback);
double wwhd_config_float(const char* id, double fallback);
u32 wwhd_config_string(const char* id, char* buffer, u32 capacity);
/* Live options: the option's revision, 1 at start and one more per change of a "live" option; 0 for an option
 * the mod doesn't have. Compare with the last revision seen (each logic step is fine) and read the value again. */
unsigned long long wwhd_config_changed(const char* id);
/* Per-mod heap: 16-byte aligned, default 256 KiB (manifest guest.heap_size changes it).
 * malloc returns null on exhaustion; free accepts null. Heap/data are part of full states. */
void* wwhd_malloc(u32 size);
void wwhd_free(void* pointer);
/* Current input, in VPAD button bits and normalized sticks/touch coordinates. */
typedef struct {
    u32 buttons;
    f32 lx, ly, rx, ry;
    u32 touch;
    f32 tx, ty;
} wwhd_input_state;
void wwhd_input_read(wwhd_input_state* state);
/* Input filter: `filter` gets the controller state of each of the game's controller reads (full logic steps)
 * after the port's own rules (touch controls, talks by touch) and before the game sees it, and may change its
 * buttons and sticks (touch fields are read only). Mods' filters run in load order. One per mod; 0 removes it.
 * The callback must be this mod's own function. Returns 1 when registered. */
typedef void (*wwhd_input_filter_fn)(wwhd_input_state* state);
int wwhd_input_filter_register(wwhd_input_filter_fn filter);
/* Touches on the TV picture (the device's touch screen, or the mouse on the desktop): the mod claims rectangles
 * in layout units (1280 x 720 around the picture's centre, +y up; a wider picture extends x) with
 * wwhd_touch_claim; a finger landing in one goes to `callback` (down, moves, up or cancel) and to nothing else
 * (no on-screen stick, no talk by touch) until it lifts. The newest claim wins where claims overlap. Events
 * arrive on the next controller read, before the input filters. Up to 8 rectangles per mod (slots 0..7); a
 * rectangle with x1 <= x0 or y1 <= y0 removes the slot's claim. Register first (0 removes the callback and the
 * claims). Returns 1 when done. */
#define WWHD_TOUCH_DOWN 0
#define WWHD_TOUCH_MOVE 1
#define WWHD_TOUCH_UP 2
#define WWHD_TOUCH_CANCEL 3
/* Pinches (newer hosts): a second finger next to the mod's finger (Android), or the mouse wheel over a claim (desktop).
 * WWHD_TOUCH_PINCH: x = the spread now / at the pinch's start (> 1: fingers apart, wheel away from the player), y = 0;
 * WWHD_TOUCH_PINCH_END: the pinch is over. A pinch goes to the mod that has the finger, else to the claim under it. */
#define WWHD_TOUCH_PINCH 4
#define WWHD_TOUCH_PINCH_END 5
typedef struct {
    u32 kind;  /* WWHD_TOUCH_* */
    f32 x, y;  /* layout units (WWHD_TOUCH_PINCH: the spread, 0) */
} wwhd_touch_event;
typedef void (*wwhd_touch_fn)(const wwhd_touch_event* event);
int wwhd_touch_register(wwhd_touch_fn callback);
int wwhd_touch_claim(u32 slot, f32 x0, f32 y0, f32 x1, f32 y1);
/* Where the drawn camera shows a point of the world (x, y, z of the current room): out[0], out[1] in the same layout
 * units as touches, with the picture's shape and field of view as drawn. Returns 1, or 0 when the point is behind the
 * camera or no camera is drawn (out unchanged). Read it on the logic step you use it (the camera moves). */
int wwhd_screen_point(const f32* world, f32* out);
/* The microphone (newer hosts), for singing and the like. Only a mod with a bool option marked "permission":
 * "microphone" in its manifest may open it, and only while that option is on: the first open asks the system for
 * permission (Android's runtime permission, macOS's prompt); declined, the port turns the option off again with a
 * notice. Close it when the option goes off (the port closes it then anyway). The sound is analysed for its pitch
 * and dropped: mods get only the pitch, never samples. wwhd_mic_pitch writes out[0] the pitch in Hz (0: none),
 * out[1] the loudness (RMS, 0..1), out[2] the clarity (0..1) of the newest analysed window (~21 ms) and returns how
 * many windows were analysed so far (a new value: a new window; 0 while not listening). */
#define WWHD_MIC_CLOSED 0
#define WWHD_MIC_ASKING 1      /* waiting for the player's answer or the device */
#define WWHD_MIC_OPEN 2
#define WWHD_MIC_DENIED 3      /* the player declined (the option is being turned off) */
#define WWHD_MIC_UNAVAILABLE 4 /* no microphone, or this mod may not open it now */
int wwhd_mic_open(void);
int wwhd_mic_close(void);
int wwhd_mic_state(void);
unsigned long long wwhd_mic_pitch(f32* out);
/* Flat filenames in this mod's Data/<id> folder; no paths or symlinks. At most 1 MiB per call.
 * read returns bytes read (zero at EOF), write replaces the file; -1 indicates failure. */
s32 wwhd_file_read(const char* filename, void* buffer, u32 size);
s32 wwhd_file_write(const char* filename, const void* buffer, u32 size);
/* Seconds in the current logic step (including true-60 scaling), and full-step count. */
double wwhd_logic_dt(void);
unsigned long long wwhd_logic_step(void);

/* Mod actors: register an actor ID declared by this mod. Keep the registration
 * table in this mod's static data or wwhd_malloc memory. Callbacks must be exact
 * functions from this module. Handles are owner-scoped and never reused.
 * Full restore calls create again with RESTORED, a fresh handle and preserved
 * userdata; replace stored handles there. Room/progress recreation uses NEW.
 * Execute runs once per actor game step. Draw must not advance behavior; actor
 * mutation services reject calls from draw or while a scene owns the actor.
 * A failing callback quarantines this actor registration until restart. */
typedef u32 wwhd_actor_handle;
enum { WWHD_ACTOR_NEW=0, WWHD_ACTOR_RESTORED=1 };
enum { WWHD_ACTOR_TALK_BEGIN=0, WWHD_ACTOR_TALK_END=1, WWHD_ACTOR_TALK_CANCEL=2 };
enum { WWHD_ACTOR_SOURCE_LINK=1, WWHD_ACTOR_SOURCE_MOD=2, WWHD_ACTOR_SOURCE_OTHER=3 };
enum { WWHD_ACTOR_COLLISION_ENABLED=1, WWHD_ACTOR_PUSHABLE=2, WWHD_ACTOR_SCENE_OWNED=4 };
typedef struct { f32 x,y,z; } wwhd_actor_vec3;
typedef struct {
    u32 size,version,sequence,attack_type,attack_power,source_kind;
    wwhd_actor_handle source_handle;
    wwhd_actor_vec3 position,reaction;
    u32 reserved[3];
} wwhd_actor_hit_v1; /* 64 bytes; informational native hit, no combat damage API. */
typedef struct {
    u32 size,version,userdata_size;
    void (*create)(wwhd_actor_handle,void* userdata,u32 reason);
    void (*execute)(wwhd_actor_handle,void* userdata,f32 dt_seconds);
    void (*draw)(wwhd_actor_handle,void* userdata);
    void (*talk)(wwhd_actor_handle,void* userdata,u32 phase);
    void (*on_hit)(wwhd_actor_handle,void* userdata,const wwhd_actor_hit_v1* hit);
} wwhd_actor_behavior_v1; /* size=32, version=1, userdata_size=0..256. */
typedef struct {
    u32 size,version;
    wwhd_actor_vec3 position,rotation; /* rotation in degrees */
    f32 collision_radius,collision_height;
    u32 flags;
    char animation[36];
} wwhd_actor_state_v1; /* size=80, version=1 */
u32 wwhd_actor_register(const char* actor_id,const wwhd_actor_behavior_v1* behavior);
wwhd_actor_handle wwhd_actor_find(const char* actor_id); /* zero until create dispatch */
u32 wwhd_actor_get(wwhd_actor_handle,wwhd_actor_state_v1* state);
void* wwhd_actor_userdata(wwhd_actor_handle); /* valid only while the handle is live */
u32 wwhd_actor_set_position(wwhd_actor_handle,f32 x,f32 y,f32 z);
u32 wwhd_actor_set_rotation(wwhd_actor_handle,f32 x,f32 y,f32 z);
u32 wwhd_actor_set_collision(wwhd_actor_handle,f32 radius,f32 height,u32 flags);
u32 wwhd_actor_set_animation(wwhd_actor_handle,const char* clip,f32 blend_seconds,u32 loop);
/* Points/state buffers must lie in this mod's region or its live actor userdata.
 * Paths use game units, 2..16 points, speed>0..1500, loop=0/1. Native collision
 * and ground checks can refuse movement. A quest place uses current-room X/Z. */
u32 wwhd_actor_walk(wwhd_actor_handle,const wwhd_actor_vec3* points,u32 count,f32 speed,u32 loop);
u32 wwhd_actor_walk_quest(wwhd_actor_handle,const char* quest_id,f32 speed);
u32 wwhd_actor_stop(wwhd_actor_handle);
/* One-shot native particles from the curated named list. The actor call takes
 * the current position of a live owned handle; the place call takes explicit
 * world XYZ in Link's current room. Neither follows the actor after emission.
 * Return 1 only when the native bank accepts the emitter. Both refuse Draw. */
u32 wwhd_actor_particle(wwhd_actor_handle,const char* effect,f32 scale,u32 alpha);
u32 wwhd_particle_at(const char* effect,f32 x,f32 y,f32 z,f32 scale,u32 alpha);
/* Combat is opt-in immutable manifest configuration. These additions preserve v1
 * behavior/hit/state layouts. on_hit remains informational; execute may poll. */
enum { WWHD_COMBAT_ALIVE=0, WWHD_COMBAT_FLINCH=1, WWHD_COMBAT_STUNNED=2,
       WWHD_COMBAT_DYING=3, WWHD_COMBAT_DEFEATED=4 };
enum { WWHD_COMBAT_FLAG_ALIVE=1, WWHD_COMBAT_FLAG_ATTACKING=2,
       WWHD_COMBAT_FLAG_SCENE_OWNED=4, WWHD_COMBAT_FLAG_QUARANTINED=8 };
enum { WWHD_COMBAT_REWARD_NONE=0, WWHD_COMBAT_REWARD_PENDING=1,
       WWHD_COMBAT_REWARD_CLAIMING=2, WWHD_COMBAT_REWARD_COMMITTED=3,
       WWHD_COMBAT_REWARD_FAILED=4 };
enum { WWHD_COMBAT_DROP_UNISSUED=0, WWHD_COMBAT_DROP_ISSUING=1,
       WWHD_COMBAT_DROP_QUEUED=2, WWHD_COMBAT_DROP_FAILED=3,
       WWHD_COMBAT_DROP_ADMITTED=4 };
typedef struct {
    u32 size,version,health,max_health,phase,flags,last_damage_sequence;
    u32 reward_status,native_drop_status;
    u32 invulnerability_frames,reaction_frames,attack_active_frames,attack_cooldown_frames;
    u32 reserved[3];
} wwhd_actor_combat_state_v1; /* 64 bytes. Initialize size=64, version=1. */
/* Output must be owned static/package memory or live owned actor userdata.
 * Failed queries leave the output unchanged. Queries are permitted in draw.
 * Frame counts round up and use native 30 Hz logic frames, never display frames. */
u32 wwhd_actor_combat_get(wwhd_actor_handle,wwhd_actor_combat_state_v1* state);
/* Begin uses the declared attack, fails while unavailable/leased/cooling down.
 * Cancel disables an attack without refunding its cooldown. Both deny draw.
 * Native queued heart status describes issuance, never successful pickup. */
u32 wwhd_actor_attack_begin(wwhd_actor_handle);
u32 wwhd_actor_attack_cancel(wwhd_actor_handle);


/* A scene music cue invokes this mod's real hook. A music step fails preflight
 * without one. RESTORE is sent once per distinct cue whose START or STOP callback
 * was invoked when the scene releases its lease, including failure/abort/load.
 * The callback owns playback through audio services
 * or authored game bindings; return nonzero only after handling the event. */
enum { WWHD_SCENE_MUSIC_START=0, WWHD_SCENE_MUSIC_STOP=1, WWHD_SCENE_MUSIC_RESTORE=2 };
u32 wwhd_scene_music_register(u32 (*callback)(const char* cue,u32 phase,u32 fade_frames,u32 stop_reason));
void* memcpy(void* dst, const void* src, unsigned long n);
void* memmove(void* dst, const void* src, unsigned long n);
void* memset(void* dst, int v, unsigned long n);

/* Read-only port settings v1. Stable types; unknown keys and wrong types return 0.
 * Successful reads return bytes including the string terminator; short buffers
 * are unchanged. A null buffer with capacity 0 queries the required byte count.
 * Buffers and key strings must belong to this mod's code/data/heap region. */
#define WWHD_SETTING_API_VERSION 1
enum { WWHD_SETTING_STRING=1, WWHD_SETTING_BOOL=2, WWHD_SETTING_U32=3, WWHD_SETTING_F64=4 };
u32 wwhd_setting_get(const char* key,u32 type,void* buffer,u32 capacity);
/* Per-key observed revision, initially 1; 0 means absent. Compare for inequality. */
unsigned long long wwhd_setting_changed(const char* key);

/* HUD v2. Register from a game hook; callback receives a recording-list handle once
 * per logic step. Its immutable output is held for every presentation until the next
 * step. TV=0 (1280x720), DRC=1 (854x480), both=2 (TV coordinates scaled to each screen).
 * Element/text/path buffers must be static or allocated in this mod's heap. */
#define WWHD_HUD_API_VERSION 2
enum { WWHD_HUD_TV=0,WWHD_HUD_DRC=1,WWHD_HUD_BOTH=2 };
enum { WWHD_HUD_RECT=0,WWHD_HUD_TEXT=1,WWHD_HUD_IMAGE=2,WWHD_HUD_RECT_OUTLINE=3,
       WWHD_HUD_CIRCLE=4,WWHD_HUD_CIRCLE_OUTLINE=5,WWHD_HUD_LINE=6,WWHD_HUD_CLIP_PUSH=7,WWHD_HUD_CLIP_POP=8 };
enum { WWHD_HUD_CENTER=0,WWHD_HUD_TOP_LEFT=1,WWHD_HUD_TOP=2,WWHD_HUD_TOP_RIGHT=3,
       WWHD_HUD_LEFT=4,WWHD_HUD_RIGHT=5,WWHD_HUD_BOTTOM_LEFT=6,WWHD_HUD_BOTTOM=7,WWHD_HUD_BOTTOM_RIGHT=8 };
enum { WWHD_HUD_ALPHA=0,WWHD_HUD_ADDITIVE=1 };
enum { WWHD_HUD_PACKAGE=0,WWHD_HUD_DATA=1,WWHD_HUD_GAME=2 };
typedef struct {
    u32 kind,anchor,blend;
    f32 x,y,w,h,size,thickness,rotation;
    f32 u0,v0,u1,v1;
    u32 rgba,image;
    const char* text;
    u32 text_bytes;
} wwhd_hud_element;
/* x/y is the top-left except circles (center). w/h is extent, or signed line delta.
 * size is circle radius / text height. Rotation is radians about image center.
 * UVs are normalized subrect coordinates. rgba is RRGGBBAA. Set thickness>0. */
u32 wwhd_hud_register(void (*callback)(u32 list),u32 screen);
u32 wwhd_hud_emit(u32 list,const wwhd_hud_element* element);
/* v2: record CLIP_PUSH/CLIP_POP only through this service. PUSH uses x/y/w/h
 * and anchor, intersecting the screen and parent clip. Max depth 16; every push
 * must be popped in the same callback. Invalid nesting drops the whole list.
 * The element ABI remains 72 bytes. Importing this service makes an older host
 * refuse the module at load, rather than silently drawing without clipping. */
u32 wwhd_hud_clip(u32 list,const wwhd_hud_element* element);
/* PNG path relative to this package's assets/ or textures/, or this mod's Data folder.
 * WWHD_HUD_GAME: a texture of the game's own layout art, read from the player's installed 2D pack of
 * the running language: "<Layout>/<texture>" (e.g. "Map_00/MapShip_00^t"), optionally "#<material>" to
 * colour it with that material's black/white colours as the game does, and "@2".."@4" at the end to get it
 * enlarged that many times (Lanczos-3, sharpened; for small art drawn large). Older hosts return zero.
 * Returns an owned handle, zero on failure. Reuse handles between logic steps. */
u32 wwhd_hud_texture(u32 source,const char* path);
u32 wwhd_hud_release(u32 image);
/* Changes after full state load; old handles are invalid. Reload PNGs on a change. */
unsigned long long wwhd_hud_epoch(void);

#define WWHD_AUDIO_API_VERSION 1
#define WWHD_AUDIO_INVALID (-1)
#define WWHD_AUDIO_BUSY (-2)
#define WWHD_AUDIO_QUOTA (-3)
#define WWHD_AUDIO_CAPACITY 8192u
#define WWHD_AUDIO_MAX_SUBMIT 2048u
/* PCM stream v1: 48000 Hz, signed 16-bit guest-endian mono/stereo.
 * Handles are owned by the calling mod. Errors: -1 invalid/stale, -2 busy,
 * -3 quota. Submit accepts up to 2048 frames and returns accepted frames;
 * available reports free frames (capacity8192). Four streams/mod,32 total.
 * Epoch changes invalidate every handle; queues are not in save states.
 * No-audio mode drains on the AX producer clock. Output gain/mute is shared.
 */
s32 wwhd_audio_open(u32 rate,u32 channels);
s32 wwhd_audio_submit(u32 handle,const short* samples,u32 frames,u32 channels);
s32 wwhd_audio_available(u32 handle);
s32 wwhd_audio_close(u32 handle);
unsigned long long wwhd_audio_epoch(void);

/* Letters v1: a letter from the mod, delivered by the Rito postbox like the game's own mail.
 * letter_id: 1..32 of a-z 0-9 - _ . (unique within the mod). sender: up to 48 bytes, one line.
 * text: up to 1200 bytes of UTF-8 plain text, '\n' for line breaks, no other control codes.
 * The first call in a session keeps the text; the save file records whether the letter was
 * delivered and read, so calling again (every logic step is fine) never sends it twice.
 * Returns the letter's state in the current save file, or a negative error. */
#define WWHD_MAIL_API_VERSION 1
#define WWHD_MAIL_NONE 0     /* never sent to this save file */
#define WWHD_MAIL_WAITING 1  /* waiting in the postbox (handed out once Link has the Delivery Bag) */
#define WWHD_MAIL_READ 2     /* handed out and read */
#define WWHD_MAIL_INVALID (-1)
#define WWHD_MAIL_NO_ROOM (-2)  /* no save loaded, or its story record is full (19 entries) */
s32 wwhd_mail_send(const char* letter_id, const char* sender, const char* text);
s32 wwhd_mail_state(const char* letter_id);

/* Charts and markers v1: a chart from the mod with marked places, treated like the game's treasure
 * charts. The save file records whether Link owns the chart (give) and whether it is finished
 * (finish). The markers are session state: the mod places them (mark adds or moves). While the chart
 * is only owned, the sea chart shows nothing for it (like an owned treasure chart); once it is
 * finished, the game's red circle (the mark of a salvaged treasure) marks each marker's spot.
 * chart_id, marker_id: 1..32 of a-z 0-9 - _ . (chart IDs unique within the mod, marker IDs within the
 * chart). x, z: Great Sea world units (one square = 100000, square D4's center = 0, 0; x east, z
 * south). label: one line of UTF-8, up to 64 bytes, or NULL;
 * it names the marker for lists (a notebook, a minimap); the sea chart shows the circle only.
 * Results: the chart's state in the current save file, or a negative error. */
#define WWHD_CHART_API_VERSION 1
#define WWHD_CHART_NONE 0     /* not given to this save file */
#define WWHD_CHART_OWNED 1    /* Link has it: nothing on the sea chart yet */
#define WWHD_CHART_DONE 2     /* finished: the red circle marks its markers' spots */
#define WWHD_CHART_INVALID (-1)
#define WWHD_CHART_NO_ROOM (-2)  /* no save loaded or its record is full; or too many markers */
#define WWHD_CHART_ICON_STORY 0  /* the only icon for now */
#define WWHD_CHART_COLUMN_X(letter) ((f32)((letter) - 'D') * 100000.0f) /* column 'A'..'G' */
#define WWHD_CHART_ROW_Z(row) ((f32)((row) - 4) * 100000.0f)            /* row 1..7 */
s32 wwhd_chart_give(const char* chart_id);
s32 wwhd_chart_finish(const char* chart_id);
s32 wwhd_chart_state(const char* chart_id);
s32 wwhd_chart_mark(const char* chart_id, const char* marker_id, f32 x, f32 z, u32 icon, const char* label);
s32 wwhd_chart_unmark(const char* chart_id, const char* marker_id); /* NULL marker_id: all of the chart */

/* The story notebook v1: the port's quest journal. Link gets it with the Story Notebook package's letter
 * (wwhd_mail_send); it is kept in the Delivery Bag, and selecting it opens its page: one row per
 * quest with its next step, where, and status, and one quest the player may show on the sea chart.
 * Register the quest in every session (wwhd_quest_define, then the texts of its current step); the
 * save file records only the step reached, whether it is done and which quest is shown on the chart.
 * quest_id: 1..32 of a-z 0-9 - _ . (unique within the mod). Texts are UTF-8, one line unless noted;
 * NULL is empty. title: 1..48 bytes. reward: up to 96. condition: up to 96, NULL when the quest can
 * be started now (else the notebook says "Not yet available: <condition>"). next: up to 120 bytes,
 * one sentence. where: up to 48, the island or place. known: up to 600 bytes, '\n' allowed.
 * Results: the quest's state in the current save file, or a negative error. */
#define WWHD_QUEST_API_VERSION 1
#define WWHD_QUEST_NEW 0       /* not started in this save file (new, or not yet available) */
                               /* 1..254: the step reached ("in progress") */
#define WWHD_QUEST_DONE 255
#define WWHD_QUEST_INVALID (-1)
#define WWHD_QUEST_NO_ROOM (-2)   /* no save loaded or its story record is full; or too many quests */
s32 wwhd_quest_define(const char* quest_id, const char* title, const char* reward, const char* condition);
s32 wwhd_quest_step(const char* quest_id, u32 step, const char* next, const char* where); /* step 0: texts only */
s32 wwhd_quest_place(const char* quest_id, f32 x, f32 z);  /* the next step's place (wwhd_chart_* coordinates) */
s32 wwhd_quest_no_place(const char* quest_id);             /* the next step has no place on the chart */
s32 wwhd_quest_known(const char* quest_id, const char* known);
s32 wwhd_quest_portrait(const char* quest_id, const char* png_path); /* assets/ or textures/, up to 256 x 256 */
s32 wwhd_quest_done(const char* quest_id);
s32 wwhd_quest_state(const char* quest_id);
/* The quest's 8 flags (n 1..8), saved with the story progress: set_flag returns the new value (0/1),
 * flag the current one; WWHD_QUEST_INVALID / WWHD_QUEST_NO_ROOM as above. */
s32 wwhd_quest_set_flag(const char* quest_id, u32 n, s32 on);
s32 wwhd_quest_flag(const char* quest_id, u32 n);
/* A letter Link carries for the quest (shown in the Delivery Bag, never in the game's inventory):
 * carried 1 gives it, 0 takes it away. */
s32 wwhd_quest_carry(const char* quest_id, s32 carried);
/* Talking to a game resident: at `step`, while this quest is at that step and Link carries its
 * letter, talking to `resident` shows `lines` (UTF-8, up to three lines, 240 bytes) instead of the
 * resident's own line, takes the letter, gives `reward` (a rupee item 0x01..0x06, 0 none, through the
 * game's get-item scene) and the quest is done. When several quests want the resident, the one shown on the
 * chart talks first, else the one started first. `resident` is a
 * name from the NPC catalogue (docs/mod-sdk-v2.md), e.g. "windfall-um1"; an unknown name is
 * refused (WWHD_QUEST_INVALID). */
s32 wwhd_quest_talk(const char* quest_id, u32 step, const char* resident, const char* lines, u32 reward);

/* Mod items v1: items the mod declares in its manifest ("items": id, name, description, icon, stack,
 * use), kept in the Delivery Bag (its extra pages, or one of the game's places the player picks). The
 * save file's story block records how many Link has; nothing is written to the game's items, so a save
 * without the mod is a normal one. item_id: an id of the manifest's "items". A give shows the item's
 * caption with the game's small get jingle. Results: the count now (0..254), or a negative error. */
#define WWHD_ITEM_API_VERSION 1
#define WWHD_ITEM_INVALID (-1)     /* not an item of the manifest, or a count of 0 or over 254 */
#define WWHD_ITEM_NO_ROOM (-2)     /* no save loaded or its story block is full */
#define WWHD_ITEM_NOT_ENOUGH (-3)  /* take: Link has fewer */
s32 wwhd_item_give(const char* item_id, u32 count);  /* up to the item's "stack" */
s32 wwhd_item_take(const char* item_id, u32 count);
s32 wwhd_item_count(const char* item_id);
/* An item with "use": {"action": "event"}: how often the player used it (X, Y or R, or the bag) since
 * the last call (the count is read and cleared; at most 255). */
s32 wwhd_item_used(const char* item_id);

/* A pane of the game's own layout art (see WWHD_HUD_GAME): centre and size in the layout's root
 * coordinates (+y up; parents' translation and scale applied, rotation not), its own Z rotation in
 * degrees, alpha, and for a picture pane its first texture's name (without ".bflim").
 * Returns 1 when found; 0 for a missing pane or an older host. Read it once, it doesn't change. */
typedef struct {
    u32 version, flags; /* flags: 1 visible (with its parents), 2 picture pane */
    f32 x, y, w, h, rotation;
    u32 alpha;
    char texture[32];
} wwhd_game_pane;
#ifdef __cplusplus
static_assert(sizeof(wwhd_game_pane) == 64, "wwhd_game_pane layout");
#else
_Static_assert(sizeof(wwhd_game_pane) == 64, "wwhd_game_pane layout");
#endif
/* Structured query type 6: key "layout-pane:<Layout>/<pane>" (at most 63 bytes). The key
 * "layout-texture:<Layout>/<texture>" instead checks that a texture exists without loading it:
 * flags 4, w/h its size. */
static inline u32 wwhd_game_pane_get(const char* key, wwhd_game_pane* pane) {
    return wwhd_setting_get(key, 6, pane, sizeof(*pane)) == sizeof(*pane);
}

/* Optional current-room map from a mod stage. Returns 1 with a 808-byte output,
 * 0 for no map or an older host. Call in a HUD callback. cached_revision is zero
 * after an epoch change or release. A changed revision creates an owned HUD image;
 * release the old one. Otherwise image is zero and the caller keeps its image.
 * Markers are already compass/chest filtered. No game-owned pointers are exposed. */
typedef struct {
    u32 version, image, revision;
    s32 floor;
    f32 bounds[4];
    u32 marker_count, reserved;
    struct { f32 x, z; u32 kind; } markers[64]; /* 1 chest, 2 boss door */
} wwhd_dungeon_room;
/* Optional native map/HUD bridge v1. Default helpers return -1 on old hosts,
 * including with older installers. Query the unsigned capability first. */
#define WWHD_MAP_HUD_API_VERSION 1
#define WWHD_MAP_HUD_UNSUPPORTED (-1)
#define WWHD_MAP_HUD_INVALID (-2)
#define WWHD_MAP_HUD_BUSY (-3)
#define WWHD_MAP_HUD_PHASE (-4)
typedef struct {u32 size,version;f32 x,y,w,h;} wwhd_boat_compasses_rect;
/* Default helpers use the existing optional query transport, including with
 * older installers. DIRECT_IMPORTS is for bindings using translator >=0.3. */
#ifdef WWHD_MAP_HUD_DIRECT_IMPORTS
s32 wwhd_hud_boat_compasses(u32 replace);
s32 wwhd_hud_boat_compasses_rect(wwhd_boat_compasses_rect* out);
s32 wwhd_map_chart_selected(void);
s32 wwhd_map_chart_select(u32 native_chart_index);
s32 wwhd_map_chart_cancel(void);

#else
static inline s32 wwhd_map_hud_request(u32 action,u32 argument,wwhd_boat_compasses_rect* rect) {
    static struct {u32 size,version,action,argument;s32 result;f32 x,y,w,h;} request;
    request.size=36;request.version=1;request.action=action;request.argument=argument;
    if(wwhd_setting_get("native-map-hud.v1",7,&request,36)!=36)return WWHD_MAP_HUD_UNSUPPORTED;
    if(rect&&request.result==1){rect->x=request.x;rect->y=request.y;rect->w=request.w;rect->h=request.h;}
    return request.result;
}
static inline s32 wwhd_hud_boat_compasses(u32 replace){return wwhd_map_hud_request(1,replace,0);}
static inline s32 wwhd_hud_boat_compasses_rect(wwhd_boat_compasses_rect* out){
    if(!out||out->size!=24||out->version!=1)return WWHD_MAP_HUD_INVALID;
    return wwhd_map_hud_request(2,0,out);
}
static inline s32 wwhd_map_chart_selected(void){return wwhd_map_hud_request(3,0,0);}
static inline s32 wwhd_map_chart_select(u32 index){return wwhd_map_hud_request(4,index,0);}
static inline s32 wwhd_map_chart_cancel(void){return wwhd_map_hud_request(5,0,0);}
#endif

#ifdef __cplusplus
static_assert(sizeof(wwhd_dungeon_room) == 808, "wwhd_dungeon_room layout");
#else
_Static_assert(sizeof(wwhd_dungeon_room) == 808, "wwhd_dungeon_room layout");
#endif
/* Structured query type 5 on the existing optional settings/query transport.
 * Hosts predating this key/type return zero without touching the output. */
static inline u32 wwhd_dungeon_room_get(wwhd_dungeon_room* room, u32 cached_revision) {
    room->revision = cached_revision;
    return wwhd_setting_get("mod-stage.current-room-map", 5, room, sizeof(*room)) == sizeof(*room);
}
