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
/* Other typed manager options (values are frozen until restart). Strings include enum options.
 * config_string copies at most capacity-1 bytes, terminates them and returns bytes copied;
 * a missing/wrong-type option returns zero without modifying the buffer. */
int wwhd_config_bool(const char* id, int fallback);
double wwhd_config_float(const char* id, double fallback);
u32 wwhd_config_string(const char* id, char* buffer, u32 capacity);
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
enum { WWHD_HUD_PACKAGE=0,WWHD_HUD_DATA=1 };
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
