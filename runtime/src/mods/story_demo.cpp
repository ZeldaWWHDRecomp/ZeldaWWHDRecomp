// SPDX-License-Identifier: MPL-2.0
#include "story_demo.h"

#include "demo_event_image.h"
#include "guest_addr.h"
#include "runtime.h"
#include "story_scenes.h"
#include "story_mail.h"
#include "story_mail_book.h"
#include "mod_actor.h"
#include "story_demo_parts.h"
#include "demo_start_gate.h"

#include <optional>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace story_demo {
namespace {
// game functions (HD US addresses; see docs/decomp-notes.md "Events, demos")
const uint32_t kPlay = GC(0x025200D4), kCamera = GC(0x024F8044), kStartCamera = GC(0x02514F38), kStopCamera = GC(0x02514F2C);
const uint32_t kClearBase = GC(0x0254273C), kSetData = GC(0x025443B4), kOrderEvent = GC(0x02544534);
const uint32_t kResLoad = GC(0x02520460), kResDelete = GC(0x025204C8);
const uint32_t kDemoRemove = GC(0x02528AC4), kSearchFromName = GC(0x025D9F38), kGetArchive = GC(0x026065A8);
const uint32_t kManagerCreate = GC(0x02528CEC);  // dDemo_manager_c::create
const uint32_t kLookupId = GC(0x026067F4), kLookupName = GC(0x02606900), kManagerUpdate = GC(0x025291C8);
const uint32_t kSearchName = GC(0x025C109C), kActorCreate = GC(0x025D5834), kSearchById = GC(0x025D54C4), kActorDelete = GC(0x025D57E0);
const uint32_t kCurrentLayer = GC(0x025DED64), kSetCurrentLayer = GC(0x025DEAB4);  // fpcLy_CurrentLayer, fpcLy_SetCurrentLayer
constexpr uint32_t kProcessLayer = 0x2C;  // base_process_class: the layer the process lives in
const uint32_t kResidentAnm = GC(0x022BED70), kResidentPlayAnm = GC(0x022C2D6C), kResidentPlayTex = GC(0x022C2CC0);
const uint32_t kResidentWalk = GD(0x101C31BA), kResidentWait = GD(0x101C31B4);  // daNpcPeople_c walk/wait animation tables (as the routes use)
const uint32_t kDemoActor = GC(0x02526E70), kDemoPool = GD(0x101D5FFC), kResidentMtx = GC(0x022BF3A0), kDemoFrame = GD(0x101D6008);
const uint32_t kFindDemoMessage = GC(0x025F60EC);  // message resources: demo message code -> message label (the demo window's lookup)
const uint32_t kDemoMessageOn = GC(0x025DB58C), kDemoMessageOff = GC(0x025DB59C);  // fopMsgM_demoMsgFlagOn / Off
// the demo message window (HD parts object): its idle state's execute (it polls the demo's message code),
// the parts state change, and the window's own states Wait (idle) and Close (out animation, then the
// window's cleanup: message status 0, the demo's suspend count back, back to Wait)
const uint32_t kSetCamera = GC(0x02514F50);  // the stopped camera's place (target, eye)
const uint32_t kDemoWindowWait = GC(0x026AEEB8), kStateChange = GC(0x020063C0), kCurrentState = GC(0x02006478);
const uint32_t kDemoWindowIdle = GD(0x10497F80), kDemoWindowClose = GD(0x104980A0);
// game data
const uint32_t kDemoMode = GD(0x101D6010), kDemoFile = GD(0x101D6004), kDemoControl = GD(0x101D5FE8), kResControl = GD(0x101F4F28);
const uint32_t kDemoArcName = GD(0x1047E6B8);  // dStage_roomControl_c::mDemoArcName, char[12]; mProcID follows at +0xC
const uint32_t kLiteralVtable = GD(0x1004BDC8);  // the literal-string vtable set_demo_data uses for its archive names
const uint32_t kIdTable = GD(0x100DE8F4), kIdTableCount = GD(0x100C45C0);  // archive CRC32 -> resource id order
const uint32_t kMessageResources = GD(0x101F4AE8), kLabelVtable = GD(0x100E0A68);  // message resources; a label string's vtable
const uint32_t kDemoMessageFlag = GD(0x101F3480);  // fopMsgM_demoMsgFlagCheck's byte
constexpr uint32_t kManager = 0x52C4, kPlayer = 0x5B34, kFlagBytes = 1280;
constexpr uint32_t kActorVtable = 0xB4, kSetPlayerPosAndAngle = 0x114;  // daPy_lk_c: its vtable; setPlayerPosAndAngle(cXyz*, s16) in it
// guest record (arena)
constexpr uint32_t kMarker = 0x444D4F31;  // 'DMO1'
constexpr uint32_t kDraining = 9;  // released; archives are given back after a few frames
constexpr uint32_t kSpawning = 5;  // archives loaded; missing game actors are being created
enum : uint32_t {
    Marker = 0x000, Phase = 0x004, ManagerAt = 0x008, RetailWords = 0x00C, RetailCameraPlay = 0x02C, RetailCurrent = 0x030, Ticks = 0x034,
    SavedArc = 0x040, VirtualCount = 0x050, MessageIndex = 0x054, StbBytes = 0x058, CameraStarted = 0x05C, MessageCount = 0x060,
    SpawnCount = 0x038, CharacterCount = 0x03C, Lease = 0x04C,
    Virtual = 0x080,        // 48 x {archive[12], id}
    Spawned = 0x380,        // 8 x {process id, stage name[12]} game actors created for the demo
    Characters = 0x400,     // 16 x {actor, process id} mod-placed actors behind the stand-in names MA00..
    Scratch = 0x480,        // request struct + names for the bridge
    CharacterState = 0x500, // 12 x {last x, last z, animation, own table} resident villagers' walk/wait choice
    Origin = 0x5C0,         // x, y, z, angle the demo's tracks are relative to (end poses on skip)
    RetailFlags = 0x600,    // manager+0x3C, 1280 bytes
    Archives = 0xB00,       // 16 x {name[12], phase request[8], held} archives this demo loads
    ArchiveCount = 0x064,
    MessageNumbers = 0x068, // 2 x the message each demo message code shows (resolved when the demo starts)
    DemoFlagSet = 0x070,    // the port turned the game's demo message flag on
    MessageShown = 0x074,   // index + 1 of the message whose point the demo waits at
    WindowAt = 0x078,       // the demo message window (noted while it waits for a message code)
    WindowClosing = 0x07C,  // 1: released with the window open; 2: and skipped (end poses after the close)
    Image = 0xC80,          // event image (<= 0x300)
    Stb = 0xF80,            // STB (<= 0xC000)
    // phases 3 and 5 (story_demo_parts.h)
    TransformSaved = 0x5D4, // 0x10000 | the demo control's transform flags (+0x74, +0x75) before this demo
    ImageAt = 0x5D0,        // where this demo's event image is (Image, or ImageLarge with a LIGHT staff)
    LightRecord = Stb + 0xC000,                                     // 0x80: saved time and weather, held rain
    AttachRecord = LightRecord + story_demo_parts::light::kRecordBytes,  // 0x100: 8 attachments
    PartsScratch = AttachRecord + story_demo_parts::attach::kRecordBytes, // 0x40
    ImageLarge = PartsScratch + 0x40,                               // <= 0xC00
    Kept = ImageLarge + 0xC00,                                      // 0x10, never cleared by a demo: +0 the demo message window
    MessageCodeFirst = 0x5D8,  // the demo's message code alternation starts at kDemoMessageCodes[this]
};
static_assert(Kept + 0x10 == kArenaBytes, "story_demo.h kArenaBytes");
uint32_t g_arena = 0;
Call g_call = nullptr;
void (*g_original)(Cpu*, uint32_t) = nullptr;
bool g_installed = false;
std::vector<demo_scene::Message> g_messages;  // host copy of the running demo's messages (the record keeps the index)
std::vector<std::u16string> g_texts;           // their text, kept alive while armed
void arm(uint32_t index) { if (index < g_texts.size()) story_mail::say(ld32(g_arena + MessageNumbers + ((index + ld32(g_arena + MessageCodeFirst)) % 2) * 4), &g_texts[index]); }

uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) { Cpu saved = *c; const uint32_t r = g_call(c, fn, args); *c = saved; return r; }
std::string cstr(uint32_t p, size_t max = 64) { std::string s; if (!p) return s; for (size_t i = 0; i < max; ++i) { const char ch = char(ld8(p + uint32_t(i))); if (!ch) break; s += ch; } return s; }
bool recorded() { return g_arena && ld32(g_arena + Marker) == kMarker; }
uint32_t crc32(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) { c ^= b; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
// The game's id of a file in an archive: its position in the archive's row of the id table.
int32_t resource_id(const demo_scene::Resource& r) {
    const uint32_t hash = crc32(r.archive), rows = ld32(kIdTableCount);
    if (rows > 4096) return -1;
    for (uint32_t i = 0; i < rows; ++i) {
        const uint32_t row = kIdTable + i * 12;
        if (ld32(row) != hash) continue;
        const uint32_t names = ld32(row + 4), count = ld32(row + 8);
        for (uint32_t k = 0; k < count && k < 0x10000; ++k)
            if (cstr(ld32(names + k * 8), 40) == r.file) return int32_t(k);
        return -1;
    }
    return -1;
}
}  // namespace
std::vector<std::string> resource_names(const std::string& archive) {
    std::vector<std::string> out;
    const uint32_t hash = crc32(archive), rows = ld32(kIdTableCount);
    if (archive.empty() || rows > 4096) return out;
    for (uint32_t i = 0; i < rows; ++i) {
        const uint32_t row = kIdTable + i * 12;
        if (ld32(row) != hash) continue;
        const uint32_t names = ld32(row + 4), count = ld32(row + 8);
        for (uint32_t k = 0; k < count && k < 0x1000; ++k) {
            const uint32_t at = ld32(names + k * 8);
            if (std::string name = at ? cstr(at, 40) : std::string(); !name.empty()) out.push_back(std::move(name));
        }
        break;
    }
    return out;
}
namespace {
uint32_t text(uint32_t at, const std::string& s) { std::memset(mem::ptr(at), 0, 16); std::memcpy(mem::ptr(at), s.c_str(), std::min<size_t>(s.size(), 15)); return at; }
bool archive_loaded(Cpu* c, const std::string& archive) {
    const uint32_t req = g_arena + Scratch;
    st32(req, text(req + 0x10, archive)); st32(req + 4, kLiteralVtable);  // a {char*, vtable} literal name, as the game passes them
    return call(c, kGetArchive, {ld32(kResControl), req}) != 0;
}

// --- the virtual archive bridge (dComIfG_getObjectIDRes / getObjectRes in HD) ---
bool virtual_name(uint32_t request) { return request && recorded() && cstr(ld32(request), 16) == kVirtualArchive; }
void lookup(Cpu* c, uint32_t fn) {
    const uint32_t request = c->r[4], id = c->r[5];
    if (!virtual_name(request)) { g_original(c, fn); return; }
    const uint32_t n = id & 0xFFFF, count = ld32(g_arena + VirtualCount);
    if (n >= count) { LOG("[demo] virtual archive id %u is not mapped", n); c->r[3] = 0; return; }
    const uint32_t entry = g_arena + Virtual + n * 16, req = g_arena + Scratch + 0x40;
    st32(req, entry); st32(req + 4, ld32(request + 4));
    c->r[4] = req; c->r[5] = ld32(entry + 12);
    g_original(c, fn);
    if (!c->r[3]) LOG("[demo] virtual id %u -> %s id 0x%X: not found", n, cstr(entry, 12).c_str(), ld32(entry + 12));
}
void lookup_id(Cpu* c) { lookup(c, kLookupId); }
// getObjectRes(control, {archive}, {name}): the PACKAGE staff's STB by name
void lookup_name(Cpu* c) {
    const uint32_t archive = c->r[4], name = c->r[5];
    if (!virtual_name(archive)) { g_original(c, kLookupName); return; }
    const std::string file = cstr(name ? ld32(name) : 0, 32);
    c->r[3] = file == "moddemo.stb" && ld32(g_arena + StbBytes) ? g_arena + Stb : 0;
    LOG("[demo] PACKAGE loads %s from the virtual archive: %08X", file.c_str(), c->r[3]);
}

// fopAcM_searchFromName: the stand-in names MA00.. of a running demo are the mod-placed actors it
// drives (the game's demo system finds its actors by name and gives each a demo slot)
uint32_t spawned_actor(Cpu* c, const std::string& name);
uint32_t character(uint32_t index) {
    if (!recorded() || index >= ld32(g_arena + CharacterCount)) return 0;
    const uint32_t entry = g_arena + Characters + index * 8, actor = ld32(entry);
    return actor && ld32(actor + 4) == ld32(entry + 4) ? actor : 0;
}
void search_name(Cpu* c) {
    const std::string name = cstr(c->r[3], 8);
    if (name.size() == 4 && name[0] == 'M' && name[1] == 'A' && std::isdigit(uint8_t(name[2])) && std::isdigit(uint8_t(name[3])) && recorded()) {
        c->r[3] = character(uint32_t((name[2] - '0') * 10 + (name[3] - '0')));
        return;
    }
    // the demo's own copy of a game actor (created for it) is the one its demo object binds to
    if (!name.empty()) {
        const Cpu saved = *c;
        const uint32_t own = spawned_actor(c, name);
        *c = saved;
        if (own != ~0u) { c->r[3] = own; return; }
    }
    g_original(c, GC(0x025D9F38));
}
void apply_characters(Cpu* c);
void apply_spawned(Cpu* c);
// The demo message window's idle execute: the window is noted for a release with a message open.
void demo_window_wait(Cpu* c) {
    if (recorded()) st32(g_arena + WindowAt, c->r[3]);
    if (g_arena) st32(g_arena + Kept, c->r[3]);  // the window outlives a demo: the next demo reads its last code
    g_original(c, kDemoWindowWait);
}
void parts_update(Cpu* c);
uint32_t image_at() { const uint32_t at = ld32(g_arena + ImageAt); return at ? at : g_arena + Image; }
story_demo_parts::Context parts(uint32_t record) { return {g_arena + record, g_arena + PartsScratch, call, g_call}; }
// dDemo_manager_c::update, then the demo's values go to actors whose own code doesn't read a demo slot
void manager_create(Cpu* c);
void manager_update(Cpu* c) {
    g_original(c, kManagerUpdate);
    const uint32_t result = c->r[3];
    if (recorded() && ld32(g_arena + Phase) == 2) {
        const Cpu saved = *c;
        parts_update(c);  // carried actors' slots first: mod-placed ones take them in apply_characters
        if (ld32(g_arena + CharacterCount)) apply_characters(c);
        if (ld32(g_arena + SpawnCount)) apply_spawned(c);
        *c = saved;
    }
    c->r[3] = result;
}
// The message the demo message window shows for a demo message code, found with the window's own
// lookup (the code is one of the message data's demo codes); 0xFFFFFFFF when the game has none.
// Uses the STB area as scratch (a label string needs a buffer of at least 256 bytes), so it runs
// before the demo data is copied there.
uint32_t resolve_message(Cpu* c, uint32_t code) {
    const uint32_t out = g_arena + Stb, label = out + 4, buffer = label + 12;
    std::memset(mem::ptr(out), 0, 0x120);
    st32(label, buffer); st32(label + 4, kLabelVtable); st32(label + 8, 0x100);  // {data, vtable, capacity}
    st32(out, 0xFFFFFFFFu);
    if (!call(c, kFindDemoMessage, {ld32(kMessageResources), label, out, code})) return 0xFFFFFFFFu;
    return ld32(out);
}

void restore_manager(Cpu* c) {
    const uint32_t manager = ld32(g_arena + ManagerAt);
    if (!manager) return;
    call(c, kClearBase, {manager});
    for (unsigned i = 0; i < 8; ++i) st32(manager + i * 4, ld32(g_arena + RetailWords + i * 4));
    st32(manager + 0x20, ld32(g_arena + RetailCameraPlay));
    st32(manager + 0x53C, ld32(g_arena + RetailCurrent));
    std::memcpy(mem::ptr(manager + 0x3C), mem::ptr(g_arena + RetailFlags), kFlagBytes);
}
}  // namespace

void configure(uint32_t arena, Call call_fn, void (*original)(Cpu*, uint32_t)) {
    g_arena = arena; g_call = call_fn; g_original = original;
    if (g_arena) std::memset(mem::ptr(g_arena), 0, kArenaBytes);
}
bool configured() { return g_arena && g_call && g_original; }

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_installed || !configured()) return;
    for (uint32_t fn : {kLookupId, kLookupName, kManagerUpdate, kManagerCreate, kSearchFromName, kDemoWindowWait})
        if (!replaced_by(fn).empty()) { LOG("[demo] %s owns %08X; demo steps disabled", replaced_by(fn).c_str(), fn); return; }
    replace(kLookupId, lookup_id);
    replace(kLookupName, lookup_name);
    replace(kManagerUpdate, manager_update);
    replace(kManagerCreate, manager_create);
    replace(kSearchFromName, search_name);
    replace(kDemoWindowWait, demo_window_wait);
    g_installed = true;
    // the environment's execute only when a declared demo changes the weather (holds it on the Great Sea)
    const bool weather = story_scenes::any_demo_step([](const demo_scene::Demo& d) { return !d.weather.empty(); });
    const bool time = story_scenes::any_demo_step([](const demo_scene::Demo& d) { return !d.time.empty(); });
    // (the world editor's previews: both, a scene made in it may change either)
    const bool editor = story_scenes::editor_previews();
    if (weather || time || editor) story_demo_parts::light::install(parts(LightRecord), g_original, replaced_by, replace, weather || editor, time || editor);
    LOG("[demo] installed: virtual demo archive %s", kVirtualArchive);
}

namespace {
demo_scene::Demo g_demo;  // host copy of the requested demo (the record says where it is)
bool g_camera_stopped = false;
CharacterResolver g_characters;                        // mod-placed actors by id (the scene's characters)
std::vector<demo_scene::CharacterRef> g_character_refs; // their clips (host side; the record keeps the actors)
std::vector<size_t> g_clip_cursor;
uint32_t g_lease_serial = 0;
// ids resolved against the game: "@Link" means Link's demo animation archive that is loaded now
std::function<int32_t(const demo_scene::Resource&)> resolver(Cpu* c) {
    return [c](const demo_scene::Resource& r) -> int32_t {
        if (r.archive != "@Link") return resource_id(r);
        for (const char* a : demo_scene::kLinkClipArchives)
            if (!c || archive_loaded(c, a)) {
                const int32_t id = resource_id({a, r.file});
                if (id >= 0) {
                    static std::string logged;
                    if (c && logged != r.file + a) { logged = r.file + a; LOG("[demo] Link clip %s is id 0x%X in %s", r.file.c_str(), unsigned(id), a); }
                    return id;
                }
            }
        return -1;
    };
}
// A game actor the port created for the running demo answers to its name before any of the room's
// (the room's own one may be waiting for an event of its own and not draw, as Outset's Helmaroc King);
// 0 while its creation is still pending. Not one of ours: ~0u.
uint32_t spawned_actor(Cpu* c, const std::string& name) {
    if (!recorded()) return ~0u;
    const uint32_t count = ld32(g_arena + SpawnCount);
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        if (cstr(g_arena + Spawned + i * 16 + 4, 12) != name) continue;
        const uint32_t out = g_arena + Scratch + 0x74;
        st32(out, 0);
        call(c, kSearchById, {ld32(g_arena + Spawned + i * 16), out});
        return ld32(out);
    }
    return ~0u;
}
uint32_t find_actor(Cpu* c, const std::string& name) {
    if (const uint32_t own = spawned_actor(c, name); own != ~0u) return own;
    std::memset(mem::ptr(g_arena + Scratch), 0, 16);
    std::memcpy(mem::ptr(g_arena + Scratch), name.c_str(), std::min<size_t>(name.size(), 15));
    return call(c, kSearchFromName, {g_arena + Scratch, 0, 0});
}
demo_event_image::Origin origin_now(uint32_t link) {
    if (g_demo.origin_link) return {u32_as_f32(ld32(link + 0x314)), u32_as_f32(ld32(link + 0x318)), u32_as_f32(ld32(link + 0x31C)), g_demo.angle};
    if (g_demo.origin) return {g_demo.origin->x, g_demo.origin->y, g_demo.origin->z, g_demo.angle};
    return {0, 0, 0, 0};
}
// Phase 0b: a game actor the demo names but the room doesn't have is created for the demo, at the
// first key of its position track (or beside Link), and removed again afterwards.
bool spawn_missing(Cpu* c, std::string& why) {
    const uint32_t play = call(c, kPlay, {}), link = play ? ld32(play + kPlayer) : 0;
    if (!link) return why = "no game play scene", false;
    demo_scene::Compiled compiled;
    if (!demo_scene::compile(g_demo, resolver(c), compiled, why)) return false;
    uint32_t count = 0;
    for (const auto& cast : compiled.cast) {
        if (!cast.own) {
            if (const uint32_t room = find_actor(c, cast.name)) {
                LOG("[demo] %s: the room's own (params %08X) plays in the demo; its own code decides whether it shows (\"spawn\": \"always\" creates one for the demo)",
                    cast.name.c_str(), ld32(room + 0xB0));
                continue;
            }
        }
        if (!cast.spawn) return why = "actor " + cast.name + " is not in this room", false;
        if (count >= 8) return why = "a demo creates at most 8 game actors", false;
        const uint32_t name = g_arena + Scratch + 0x60;
        std::memset(mem::ptr(name), 0, 16);
        std::memcpy(mem::ptr(name), cast.name.c_str(), cast.name.size());
        const uint32_t entry = call(c, kSearchName, {name});
        if (!entry) return why = "the game has no actor named " + cast.name, false;
        const demo_event_image::Origin o = origin_now(link);
        float pos[3] = {u32_as_f32(ld32(link + 0x314)), u32_as_f32(ld32(link + 0x318)), u32_as_f32(ld32(link + 0x31C))};
        if (cast.placed) {
            const float a = o.angle_y * 3.14159265f / 180.f, cs = std::cos(a), sn = std::sin(a);
            pos[0] = o.x + cast.position.x * cs + cast.position.z * sn;
            pos[1] = o.y + cast.position.y;
            pos[2] = o.z - cast.position.x * sn + cast.position.z * cs;
        }
        const uint32_t args = g_arena + Scratch + 0x30;  // position, angle (6 bytes + pad), scale
        for (unsigned i = 0; i < 3; ++i) st32(args + i * 4, f32_as_u32(pos[i]));
        std::memset(mem::ptr(args + 12), 0, 8);
        for (unsigned i = 0; i < 3; ++i) st32(args + 20 + i * 4, f32_as_u32(1.f));
        const uint32_t sub = ld8(entry + 0xA);
        // fopAcM_create puts the actor into the process layer that is current at the call. The scene
        // player runs between the game's process handlers, where the current layer is whatever the last
        // one left, often the root layer. An actor in the root layer is drawn twice a frame: by the root
        // layer's own draw and by the actor draw queue the play scene draws. Its model packets then go
        // into the same J3D draw bucket twice, the bucket's list loops on itself and
        // J3DDrawBuffer::drawHead never returns (the scene preview hang). So the demo's actors are made
        // in Link's layer (the play scene's), where the game keeps its own actors.
        const uint32_t layer = ld32(link + kProcessLayer);
        if (!layer) return why = "Link's process layer is unknown", false;
        const uint32_t previous = call(c, kCurrentLayer, {});
        uint32_t pid = 0xFFFFFFFFu;
        call(c, kSetCurrentLayer, {layer});
        try {
            pid = call(c, kActorCreate, {ld16(entry + 8), cast.params, args, uint32_t(int8_t(ld8(link + 0x326))), args + 12, args + 20, sub == 0xFF ? 0xFFFFFFFFu : sub, 0});
        } catch (...) {
            call(c, kSetCurrentLayer, {previous});
            throw;
        }
        call(c, kSetCurrentLayer, {previous});
        if (pid == 0xFFFFFFFFu) return why = "the game could not create " + cast.name, false;
        if (previous != layer) LOG("[demo] %s goes into Link's process layer %08X (the current layer was %08X)", cast.name.c_str(), layer, previous);
        const uint32_t slot = g_arena + Spawned + count * 16;
        st32(slot, pid);
        text(slot + 4, cast.name);
        st32(g_arena + SpawnCount, ++count);
        LOG("[demo] created %s (process %u, params %08X) for the demo", cast.name.c_str(), pid, cast.params);
    }
    return true;
}
bool cast_ready(Cpu* c) {
    demo_scene::Compiled compiled;
    std::string why;
    if (!demo_scene::compile(g_demo, resolver(c), compiled, why)) return false;
    for (const auto& cast : compiled.cast) if (!find_actor(c, cast.name)) return false;
    // the mod's own placed actors may still be on their way in (a scene can start as the room loads)
    for (const auto& ch : compiled.characters) { const uint32_t a = g_characters ? g_characters(ch.character) : 0; if (!a || !ld32(a + 4) || ld32(a + 4) == 0xFFFFFFFFu) return false; }
    return true;
}
std::string missing_cast(Cpu* c) {
    demo_scene::Compiled compiled;
    std::string why, out;
    if (!demo_scene::compile(g_demo, resolver(c), compiled, why)) return why;
    for (const auto& cast : compiled.cast) if (!find_actor(c, cast.name)) out += (out.empty() ? "" : ", ") + cast.name;
    for (const auto& ch : compiled.characters) if (!(g_characters ? g_characters(ch.character) : 0)) out += (out.empty() ? "" : ", ") + ch.character;
    return out;
}
void remove_spawned(Cpu* c) {
    const uint32_t count = ld32(g_arena + SpawnCount);
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        const uint32_t pid = ld32(g_arena + Spawned + i * 16);
        st32(g_arena + Scratch + 0x70, 0);
        call(c, kSearchById, {pid, g_arena + Scratch + 0x70});
        const uint32_t actor = ld32(g_arena + Scratch + 0x70);
        if (actor) call(c, kActorDelete, {actor});
    }
    st32(g_arena + SpawnCount, 0);
}
// Phase 0: the archives the demo's files come from are loaded (and held) like an actor would.
bool load_archives(Cpu* c, bool& ready, std::string& why) {
    ready = true;
    const uint32_t count = ld32(g_arena + ArchiveCount);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entry = g_arena + Archives + i * 24;
        const uint32_t state = call(c, kResLoad, {entry + 12, entry});
        if (ld32(g_arena + Ticks) < 4) LOG("[demo] archive %s load state %u (loaded %u)", cstr(entry, 12).c_str(), state, unsigned(archive_loaded(c, cstr(entry, 12))));
        if (state == 4) { st32(entry + 20, 1); continue; }
        if (state >= 5) return why = "archive " + cstr(entry, 12) + " could not be loaded", false;
        ready = false;
    }
    return true;
}
void drop_archives(Cpu* c) {
    const uint32_t count = ld32(g_arena + ArchiveCount);
    for (uint32_t i = 0; i < count && i < 16; ++i) {
        const uint32_t entry = g_arena + Archives + i * 24;
        if (ld32(entry + 20)) call(c, kResDelete, {entry + 12, entry});
        st32(entry + 20, 0);
    }
    st32(g_arena + ArchiveCount, 0);
}
// Phase 1: participants present, then the demo's event list replaces the stage's until the end.
bool begin(Cpu* c, std::string& why) {
    const uint32_t play = call(c, kPlay, {}), link = play ? ld32(play + kPlayer) : 0;
    if (!play || !link) return why = "no game play scene", false;
    if (ld32(kDemoMode) || ld32(kDemoFile)) return why = "a game demo is running", false;
    // The demo message window takes a code only when it differs from the last one it took (window
    // +0x154), and keeps that across demos: this demo's messages continue the alternation from there.
    const uint32_t window = ld32(g_arena + Kept), window_code = window ? ld32(window + 0x154) : 0;
    const uint32_t first_code = demo_scene::first_message_code(window_code);
    st32(g_arena + MessageCodeFirst, first_code);
    demo_scene::Compiled compiled;
    if (!demo_scene::compile(g_demo, resolver(c), compiled, why, first_code)) return false;
    // the mod-placed actors behind MA00..: present now, mod actors leased from their own motion
    if (compiled.characters.size() > 12) return why = "a demo drives at most 12 characters", false;
    for (size_t i = 0; i < compiled.characters.size(); ++i) {
        const uint32_t actor = g_characters ? g_characters(compiled.characters[i].character) : 0;
        if (!actor || !ld32(actor + 4) || ld32(actor + 4) == 0xFFFFFFFFu) return why = "character " + compiled.characters[i].character + " is not here", false;
        st32(g_arena + Characters + uint32_t(i) * 8, actor);
        st32(g_arena + Characters + uint32_t(i) * 8 + 4, ld32(actor + 4));
        if (ld16(actor + 8) == 0x175) st32(g_arena + CharacterState + uint32_t(i) * 16 + 12, ld32(actor + 0x89C));  // its own animation table, back after the demo
    }
    if (!++g_lease_serial) ++g_lease_serial;
    st32(g_arena + Lease, 0x444D0000u | (g_lease_serial & 0xFFFF));
    for (size_t i = 0; i < compiled.characters.size(); ++i) {
        mod_actor::Identity id;
        if (mod_actor::identify(c, ld32(g_arena + Characters + uint32_t(i) * 8), id) && !mod_actor::begin_scene(c, id, ld32(g_arena + Lease)))
            return why = "character " + compiled.characters[i].character + " is busy", false;
    }
    st32(g_arena + CharacterCount, uint32_t(compiled.characters.size()));
    g_character_refs = compiled.characters;
    g_clip_cursor.assign(compiled.characters.size(), 0);
    // every participant must exist: the game refuses a whole demo when one named actor is missing
    for (const auto& name : compiled.participants) {
        std::memset(mem::ptr(g_arena + Scratch), 0, 16);
        std::memcpy(mem::ptr(g_arena + Scratch), name.c_str(), name.size());
        if (!call(c, kSearchFromName, {g_arena + Scratch, 0, 0})) return why = "actor " + name + " is not in this room", false;
    }
    if (compiled.stb.size() > kArenaBytes - Stb) return why = "demo data too large", false;
    uint32_t numbers[2] = {0, 0};
    // both codes when there are messages: the alternation may start on either (first_code)
    for (size_t i = 0; i < 2 && !compiled.messages.empty(); ++i)
        if ((numbers[i] = resolve_message(c, demo_scene::kDemoMessageCodes[i])) > 0xFFFF) return why = "the game has no message for a demo message code", false;
    demo_event_image::Spec spec;
    if (g_demo.origin_link) spec.origin = demo_event_image::Origin{u32_as_f32(ld32(link + 0x314)), u32_as_f32(ld32(link + 0x318)), u32_as_f32(ld32(link + 0x31C)), g_demo.angle};
    else if (g_demo.origin) spec.origin = demo_event_image::Origin{g_demo.origin->x, g_demo.origin->y, g_demo.origin->z, g_demo.angle};
    if (!g_demo.time.empty() && !story_demo_parts::light::time_ready()) return why = "demo time changes are not installed (another hook owns the game's time change)", false;
    const auto light = demo_scene::light_points(g_demo);
    for (uint32_t i = 0; i < light.size(); ++i)
        spec.light.push_back({demo_scene::kLightStartFlag0 + i, demo_scene::kLightEndFlag0 + i, light[i].hour, light[i].rain && *light[i].rain > 0});
    spec.light_wait_flag = demo_scene::kLightWaitFlag;
    std::vector<uint8_t> image;
    if (!demo_event_image::make(spec, image, why)) return false;
    const uint32_t image_address = image.size() <= Stb - Image ? g_arena + Image : g_arena + ImageLarge;
    if (image.size() > demo_event_image::kMaxImageBytes) return why = "demo event image too large", false;
    if (spec.origin) { st32(g_arena + Origin, f32_as_u32(spec.origin->x)); st32(g_arena + Origin + 4, f32_as_u32(spec.origin->y)); st32(g_arena + Origin + 8, f32_as_u32(spec.origin->z)); st32(g_arena + Origin + 12, f32_as_u32(spec.origin->angle_y)); }
    const uint32_t manager = play + kManager;
    (void)origin_now;
    st32(g_arena + ManagerAt, manager);
    for (unsigned i = 0; i < 8; ++i) st32(g_arena + RetailWords + i * 4, ld32(manager + i * 4));
    st32(g_arena + RetailCameraPlay, ld32(manager + 0x20));
    st32(g_arena + RetailCurrent, ld32(manager + 0x53C));
    std::memcpy(mem::ptr(g_arena + RetailFlags), mem::ptr(manager + 0x3C), kFlagBytes);
    std::memcpy(mem::ptr(g_arena + SavedArc), mem::ptr(kDemoArcName), 12);
    st32(g_arena + VirtualCount, uint32_t(compiled.virtual_ids.size()));
    for (size_t i = 0; i < compiled.virtual_ids.size(); ++i) {
        const uint32_t entry = g_arena + Virtual + uint32_t(i) * 16;
        text(entry, compiled.virtual_ids[i].first.archive);
        st32(entry + 12, compiled.virtual_ids[i].second);
    }
    std::memcpy(mem::ptr(image_address), image.data(), image.size());
    st32(g_arena + ImageAt, image_address);
    story_demo_parts::attach::begin(parts(AttachRecord));
    story_demo_parts::light::begin(c, parts(LightRecord), g_demo);
    std::memcpy(mem::ptr(g_arena + Stb), compiled.stb.data(), compiled.stb.size());
    st32(g_arena + StbBytes, uint32_t(compiled.stb.size()));
    st32(g_arena + MessageCount, uint32_t(compiled.messages.size()));
    g_messages = compiled.messages;
    g_texts.clear();
    for (const auto& m : g_messages) {
        std::u16string t;
        if (!story_mail::can_say() || !story_mail::append_utf16(m.text, true, t)) return why = "demo messages need the story system's message hooks", false;
        g_texts.push_back(std::move(t));
    }
    st32(g_arena + MessageIndex, 0);
    st32(g_arena + DemoFlagSet, 0);
    st32(g_arena + MessageShown, 0);
    for (unsigned i = 0; i < 2; ++i) st32(g_arena + MessageNumbers + i * 4, numbers[i]);
    if (!g_messages.empty()) LOG("[demo] messages show as messages %u and %u (demo message codes %u, %u); the first message uses %u (the window's last code %u)", numbers[0], numbers[1],
        demo_scene::kDemoMessageCodes[0], demo_scene::kDemoMessageCodes[1], demo_scene::demo_message_code(0, first_code), window_code);
    arm(0);
    std::memset(mem::ptr(kDemoArcName), 0, 12);
    std::memcpy(mem::ptr(kDemoArcName), kVirtualArchive, sizeof kVirtualArchive - 1);
    st32(g_arena + Phase, 1);
    st32(g_arena + Ticks, 0);
    call(c, kClearBase, {manager});
    call(c, kSetData, {manager, image_address});
    if (!call(c, kOrderEvent, {manager, 0})) return why = "the game refused the demo event", false;
    if (g_camera_stopped) { const uint32_t camera = call(c, kCamera, {}); if (camera) { call(c, kStartCamera, {camera}); st32(g_arena + CameraStarted, 1); } }
    LOG("[demo] started: %zu bytes STB, %zu archive files, %zu participants, %zu messages, origin %s", compiled.stb.size(), compiled.virtual_ids.size(), compiled.participants.size(),
        compiled.messages.size(), spec.origin ? "set" : "none");
    return true;
}
}  // namespace

namespace {
// a demo step requested while the previous demo is still being cleared (host side: a state load ends
// the scene, and release() drops it)
struct Waiting { demo_scene::Demo demo; bool camera_stopped = false; CharacterResolver characters; };
std::optional<Waiting> g_waiting;
demo_start_gate::Gate g_gate;
bool archives_of(const demo_scene::Demo& demo, std::vector<std::string>& archives, std::string& why) {
    // resolve every file now (static tables): a wrong archive or file name refuses before anything changes
    demo_scene::Compiled compiled;
    if (!demo_scene::compile(demo, resolver(nullptr), compiled, why)) return false;
    for (const auto& [r, id] : compiled.virtual_ids)
        if (std::find(archives.begin(), archives.end(), r.archive) == archives.end()) archives.push_back(r.archive);
    if (archives.size() > 16) return why = "demo uses more than 16 archives", false;
    return true;
}
}  // namespace

bool start(Cpu* c, const demo_scene::Demo& demo, bool camera_stopped, CharacterResolver characters, std::string& why) {
    if (!g_installed || !configured()) return why = "demo steps are not installed", false;
    if (recorded() && ld32(g_arena + Phase) != kDraining) return why = "another demo is running", false;
    std::vector<std::string> archives;
    if (!archives_of(demo, archives, why)) return false;
    if (recorded()) {
        // right after another demo: this one starts once that one is cleared (poll())
        g_waiting = Waiting{demo, camera_stopped, std::move(characters)};
        g_gate.arm();
        LOG("[demo] requested while the previous demo is being cleared: it starts when that is done");
        return true;
    }
    std::memset(mem::ptr(g_arena), 0, Stb);
    for (size_t i = 0; i < archives.size(); ++i) text(g_arena + Archives + uint32_t(i) * 24, archives[i]);
    st32(g_arena + ArchiveCount, uint32_t(archives.size()));
    g_demo = demo;
    g_camera_stopped = camera_stopped;
    g_characters = std::move(characters);
    (void)c;
    st32(g_arena + Phase, 0);
    st32(g_arena + Marker, kMarker);
    LOG("[demo] requested: %zu archives to load", archives.size());
    return true;
}

Poll poll(Cpu* c) {
    Poll out;
    if (g_waiting) {
        const bool cleaning = recorded() && ld32(g_arena + Phase) == kDraining;
        switch (g_gate.step(cleaning, recorded())) {
        case demo_start_gate::Step::Wait: out.state = Poll::State::Running; return out;
        case demo_start_gate::Step::GiveUp:
            out.why = cleaning ? "the previous demo was not cleared in time" : "another demo is running";
            LOG("[demo] waiting demo not started: %s", out.why.c_str());
            g_waiting.reset();
            out.state = Poll::State::Failed;
            return out;
        case demo_start_gate::Step::Start: {
            Waiting w = std::move(*g_waiting);
            g_waiting.reset();
            LOG("[demo] previous demo cleared after %u ticks: the waiting demo starts", g_gate.ticks);
            std::string why;
            if (!start(c, w.demo, w.camera_stopped, std::move(w.characters), why)) { out.state = Poll::State::Failed; out.why = why; return out; }
            out.state = Poll::State::Running;
            return out;
        }
        }
    }
    if (!recorded() || ld32(g_arena + Phase) == kDraining) return out;
    const uint32_t ticks = ld32(g_arena + Ticks) + 1;
    st32(g_arena + Ticks, ticks);
    if (ld32(g_arena + Phase) == 0) {
        bool ready = false;
        std::string why;
        if (ticks > 300) why = "archives did not load in 10 seconds";
        else if (load_archives(c, ready, why) && ready && spawn_missing(c, why)) { st32(g_arena + Phase, kSpawning); st32(g_arena + Ticks, 0); }
        if (!why.empty()) { release(c, why.c_str()); out.state = Poll::State::Failed; out.why = why; return out; }
        out.state = Poll::State::Running;
        return out;
    }
    if (ld32(g_arena + Phase) == kSpawning) {
        std::string why;
        if (ticks > 300) why = "the demo's actors were not ready in 10 seconds (" + missing_cast(c) + ")";
        else if (cast_ready(c) && !begin(c, why) && why.empty()) why = "demo could not start";
        if (!why.empty()) { release(c, why.c_str()); out.state = Poll::State::Failed; out.why = why; return out; }
        out.state = Poll::State::Running;
        return out;
    }
    const uint32_t mode = ld32(kDemoMode), file = ld32(kDemoFile), state = ld32(image_at() + demo_event_image::kEventStateOffset);
    const bool ours = file == g_arena + Stb;
    if (ld32(g_arena + Phase) == 1) {
        if (ours && mode == 1) { st32(g_arena + Phase, 2); LOG("[demo] playing (event state %u)", state); }
        else if (ticks > 90) {  // the PACKAGE never started the demo (refused data): never leave the event hanging
            release(c, "demo did not start");
            out.state = Poll::State::Failed; out.why = "the game did not start the demo data";
            return out;
        }
        out.state = Poll::State::Running;
        return out;
    }
    // suspended at a message point: the next message in frame order
    const uint32_t control = ld32(kDemoControl), index = ld32(g_arena + MessageIndex);
    if (ticks % 30 == 0 && std::getenv("WWHD_DEMO_TRACE"))
        LOG("[demo] trace tick %u mode %u frame %u ours %d state %u control %08X +38 %08X +50 %08X msgstatus %u msgctl %08X", ticks, mode, ld32(GD(0x101D6008)), int(ours), state, control,
            control ? ld32(control + 0x38) : 0, control ? ld32(control + 0x50) : 0, call(c, GC(0x025F795C), {ld32(GD(0x101F4B5C))}), ld32(GD(0x104756C8)));
    // Message points: only messages suspend the demo, so a suspension is the next message's point.
    // The demo message window has the message then but waits for the talk camera, which a demo has
    // none of (retail demo events pause the camera with their own CAMERA staff); the game's demo
    // message flag opens it, as the game's own demo actors do. The window turns the flag off when the
    // player closes the message, and the game resumes the demo; the next message's text is armed then.
    const bool suspended = ours && mode == 1 && control && int32_t(ld32(control + 0x38)) > 0;
    const bool waiting = index < ld32(g_arena + MessageCount) && index < g_messages.size();
    if (suspended && waiting && ld32(g_arena + MessageShown) != index + 1) {
        if (!ld8(kDemoMessageFlag)) call(c, kDemoMessageOn, {});
        st32(g_arena + DemoFlagSet, 1);
        st32(g_arena + MessageShown, index + 1);
        LOG("[demo] message %u shown", index);
    } else if (!suspended && waiting && ld32(g_arena + MessageShown) == index + 1) {
        story_mail::unsay();
        st32(g_arena + MessageIndex, index + 1);
        arm(index + 1);
        LOG("[demo] message %u closed", index);
    }
    if (ours && mode == 1) {
        const uint32_t frame = ld32(kDemoFrame);
        for (size_t i = 0; i < g_character_refs.size() && i < g_clip_cursor.size(); ++i) {
            const auto& clips = g_character_refs[i].clips;
            while (g_clip_cursor[i] < clips.size() && clips[g_clip_cursor[i]].frame <= frame) {
                mod_actor::Identity id;
                const auto& clip = clips[g_clip_cursor[i]++];
                if (mod_actor::identify(c, character(uint32_t(i)), id)) {
                    const bool ok = mod_actor::scene_animation(c, id, ld32(g_arena + Lease), clip.clip, 6, clip.loop);
                    LOG("[demo] %s plays its clip %s at demo frame %u%s", g_character_refs[i].character.c_str(), clip.clip.c_str(), frame, ok ? "" : " (refused)");
                }
            }
        }
    }
    if (!ours && mode == 0) {
        // the game's PACKAGE cut removed the demo when its data ended (its event stays running in the
        // compulsory event the scene owns): the step is done
        LOG("[demo] finished by the game at tick %u (event state %u)", ticks, state);
        release(c, "finished");
        out.state = Poll::State::Done;
        return out;
    }
    out.state = Poll::State::Running;
    return out;
}

namespace {
// A demo slot's values (dDemo_actor_c: flags +4, translation +8, scale +0x14, rotation s16 +0x20) go to an
// actor whose code doesn't read its slot: mod actors through their scene lease, resident villagers with
// their own matrix update, any other actor (enemies, objects) by its position, angles and scale.
void apply_characters(Cpu* c) {
    const uint32_t pool = ld32(kDemoPool);
    if (!pool) return;
    for (uint32_t i = 0; i < ld32(g_arena + CharacterCount) && i < 16; ++i) {
        const uint32_t actor = character(i);
        if (!actor) continue;
        const uint32_t slot = ld8(actor + 0x2DC);
        const uint32_t demo = slot ? call(c, kDemoActor, {pool, slot}) : 0;
        if (!demo) continue;
        const uint16_t flags = ld16(demo + 4);
        if (!(flags & 0x0E)) continue;
        const uint32_t position = (flags & 2) ? demo + 8 : actor + 0x314;
        const int16_t rx = int16_t(ld16((flags & 8) ? demo + 0x20 : actor + 0x328)), ry = int16_t(ld16((flags & 8) ? demo + 0x22 : actor + 0x32A)), rz = int16_t(ld16((flags & 8) ? demo + 0x24 : actor + 0x32C));
        mod_actor::Identity id;
        if (mod_actor::identify(c, actor, id)) {
            const story_notebook::WorldPoint p{u32_as_f32(ld32(position)), u32_as_f32(ld32(position + 4)), u32_as_f32(ld32(position + 8))};
            mod_actor::scene_pose(c, id, ld32(g_arena + Lease), p, {rx * 180.f / 32768.f, ry * 180.f / 32768.f, rz * 180.f / 32768.f});
            continue;
        }
        if (flags & 2) for (uint32_t off : {0x314u, 0x300u}) for (unsigned k = 0; k < 12; k += 4) st32(actor + off + k, ld32(demo + 8 + k));
        if (flags & 8) for (uint32_t off : {0x320u, 0x328u}) { st16(actor + off, uint16_t(rx)); st16(actor + off + 2, uint16_t(ry)); st16(actor + off + 4, uint16_t(rz)); }
        if (flags & 4) for (unsigned k = 0; k < 12; k += 4) st32(actor + 0x330 + k, ld32(demo + 0x14 + k));
        if (ld16(actor + 8) == 0x175) {
            // a villager walks while its track moves it and waits when it stands, with its own clips
            const uint32_t state = g_arena + CharacterState + i * 16;
            const float x = u32_as_f32(ld32(actor + 0x314)), z = u32_as_f32(ld32(actor + 0x31C));
            const float dx = x - u32_as_f32(ld32(state)), dz = z - u32_as_f32(ld32(state + 4));
            const uint32_t previous = ld32(state + 8), now = previous >> 16;
            const bool moving = now != 0 && dx * dx + dz * dz > 0.25f;  // more than half a unit a frame
            const uint32_t settled = moving ? 0 : std::min<uint32_t>((previous & 0xFFFF) + 1, 0xFFFF);
            uint32_t target = now;
            if (!now) target = 2;
            else if (moving && now != 1) target = 1;
            else if (!moving && now != 2 && settled >= 4) target = 2;
            if (target != now) call(c, kResidentAnm, {actor, target == 1 ? kResidentWalk : kResidentWait, 1});
            st32(state, f32_as_u32(x)); st32(state + 4, f32_as_u32(z));
            st32(state + 8, target << 16 | settled);
            call(c, kResidentPlayTex, {actor}); call(c, kResidentPlayAnm, {actor});
            call(c, kResidentMtx, {actor});  // daNpcPeople_c::setMtx
        }
        if (std::getenv("WWHD_DEMO_TRACE") && ld32(kDemoFrame) % 30 == 0)
            LOG("[demo] character %u profile %04X at %.0f,%.0f,%.0f angle %d (frame %u)", i, ld16(actor + 8), u32_as_f32(ld32(actor + 0x314)), u32_as_f32(ld32(actor + 0x318)),
                u32_as_f32(ld32(actor + 0x31C)), int(int16_t(ld16(actor + 0x32A))), ld32(kDemoFrame));
    }
}
}  // namespace

namespace {
// dDemo_manager_c::create for a mod demo starts from a fresh control's transform state. create applies
// frame 0 (TControl::forward(0)) before it sets the demo's origin (control +0x74/+0x75 and the origin
// transform); with the flags still on from the previous demo, actors without a position track got
// (0,0,0) instead of the origin, and Link, who takes his slot's place in his demo clips, stood at the
// world origin and fell out of his room (the scene was interrupted). The game's own values come back
// once the demo is cleared (tick()).
// Link with a position track starts the demo at its first key. Of his demo modes, only the placing
// ones the game's own cutscenes start with (and his clips) move him onto his track: in wait, walk and
// the others he stayed where he stood, so a demo whose keys are away from him (made with the editor's
// fly camera somewhere else) played without him in the picture, or with him walking there from afar.
// daPy_lk_c::setPlayerPosAndAngle(cXyz*, s16), the virtual those placing modes call, places him.
void place_link(Cpu* c) {
    const uint32_t play = call(c, kPlay, {}), link = play ? ld32(play + kPlayer) : 0;
    if (!link) return;
    const demo_scene::Vec origin{u32_as_f32(ld32(g_arena + Origin)), u32_as_f32(ld32(g_arena + Origin + 4)), u32_as_f32(ld32(g_arena + Origin + 8))};
    const auto start = demo_scene::link_start(g_demo, origin, u32_as_f32(ld32(g_arena + Origin + 12)));
    if (!start) return;
    const uint32_t vtable = ld32(link + kActorVtable), place = vtable ? ld32(vtable + kSetPlayerPosAndAngle) : 0;
    if (!place) return;
    const float dx = u32_as_f32(ld32(link + 0x314)) - start->position.x, dy = u32_as_f32(ld32(link + 0x318)) - start->position.y, dz = u32_as_f32(ld32(link + 0x31C)) - start->position.z;
    const uint32_t at = g_arena + Scratch + 0x50;
    st32(at, f32_as_u32(start->position.x)); st32(at + 4, f32_as_u32(start->position.y)); st32(at + 8, f32_as_u32(start->position.z));
    const int16_t heading = start->heading ? int16_t(int32_t(*start->heading * 32768.f / 180.f)) : int16_t(ld16(link + 0x32A));
    call(c, place, {link, at, uint32_t(int32_t(heading))});
    LOG("[demo] Link starts at his first position key %.0f,%.0f,%.0f (he stood %.0f units from it)", start->position.x, start->position.y, start->position.z,
        std::sqrt(dx * dx + dy * dy + dz * dz));
}
void manager_create(Cpu* c) {
    if (recorded() && c->r[3] == g_arena + Stb) {
        const uint32_t control = ld32(kDemoControl);
        if (control) {
            if (!(ld32(g_arena + TransformSaved) >> 16)) st32(g_arena + TransformSaved, 0x10000u | uint32_t(ld8(control + 0x74)) << 8 | ld8(control + 0x75));
            st8(control + 0x74, 0); st8(control + 0x75, 0);
        }
    }
    g_original(c, kManagerCreate);
    if (!recorded() || ld32(kDemoFile) != g_arena + Stb) return;
    // create also applied frame 0 to the slots before the origin was set: a slot says "translation
    // set" at (0,0,0) until the first update, and an actor that executes in between and takes its slot's
    // place every frame (Link in his demo clips, once he has been in a demo before) would stand there
    // for a frame. The participants' slots wait for the first update, which sets them with the origin.
    const Cpu saved = *c;
    const uint32_t pool = ld32(kDemoPool), play = call(c, kPlay, {});
    unsigned cleared = 0, characters = 0;
    for (const auto& d : g_demo.actors) {
        uint32_t actor = 0;
        if (d.kind == demo_scene::Actor::Kind::Link) actor = play ? ld32(play + kPlayer) : 0;
        else if (d.kind == demo_scene::Actor::Kind::Character) actor = character(characters++);
        else if (d.kind == demo_scene::Actor::Kind::Game) actor = find_actor(c, d.actor);
        const uint32_t id = actor ? ld8(actor + 0x2DC) : 0;
        const uint32_t slot = pool && id && id <= 32 ? call(c, kDemoActor, {pool, id}) : 0;
        if (slot && (ld16(slot + 4) & 0x0E)) { st16(slot + 4, uint16_t(ld16(slot + 4) & ~0x0Eu)); ++cleared; }
    }
    if (cleared) LOG("[demo] created: %u actor slots wait for the first update", cleared);
    place_link(c);
    *c = saved;
}
// Phases 3 and 5: carried actors and the demo's time/weather changes, each demo frame
void parts_update(Cpu* c) {
    if (g_demo.attach.empty() && g_demo.time.empty() && g_demo.weather.empty()) return;
    const uint32_t frame = ld32(kDemoFrame);
    story_demo_parts::light::update(c, parts(LightRecord), g_demo, frame, ld32(g_arena + ManagerAt));
    if (g_demo.attach.empty()) return;
    const uint32_t play = call(c, kPlay, {});
    story_demo_parts::attach::update(c, parts(AttachRecord), g_demo, frame, [&](const std::string& id) -> uint32_t {
        uint32_t characters = 0;
        for (const auto& d : g_demo.actors) {
            const bool is_character = d.kind == demo_scene::Actor::Kind::Character;
            if (d.id == id) {
                if (d.kind == demo_scene::Actor::Kind::Link) return play ? ld32(play + kPlayer) : 0;
                if (is_character) return character(characters);
                if (d.kind == demo_scene::Actor::Kind::Game) return find_actor(c, d.actor);
                return 0;
            }
            characters += is_character ? 1 : 0;
        }
        return 0;
    });
}
// Game actors the port created for the demo follow their tracks also where their own code doesn't read
// a demo slot (a pig, an enemy, an object): their slot's values go to their position, angles and scale,
// as for the mod-placed actors. Without that a created pig stood where its own code put it (at
// Windfall, on a point of the room's pig path), not in the picture. Actors whose code reads its slot
// set the same values themselves; the room's own actors are left to their code.
void apply_spawned(Cpu* c) {
    const uint32_t pool = ld32(kDemoPool);
    if (!pool) return;
    const uint32_t count = std::min<uint32_t>(ld32(g_arena + SpawnCount), 8);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t out = g_arena + Scratch + 0x74;
        st32(out, 0);
        call(c, kSearchById, {ld32(g_arena + Spawned + i * 16), out});
        const uint32_t actor = ld32(out), slot = actor ? ld8(actor + 0x2DC) : 0;
        const uint32_t demo = slot && slot <= 32 ? call(c, kDemoActor, {pool, slot}) : 0;
        if (!demo) continue;
        const uint16_t flags = ld16(demo + 4);
        if (flags & 2) for (uint32_t off : {0x314u, 0x300u}) for (unsigned k = 0; k < 12; k += 4) st32(actor + off + k, ld32(demo + 8 + k));
        if (flags & 8) for (uint32_t off : {0x320u, 0x328u}) for (unsigned k = 0; k < 6; k += 2) st16(actor + off + k, ld16(demo + 0x20 + k));
        if (flags & 4) for (unsigned k = 0; k < 12; k += 4) st32(actor + 0x330 + k, ld32(demo + 0x14 + k));
        if (std::getenv("WWHD_DEMO_TRACE") && ld32(kDemoFrame) % 30 == 0)
            LOG("[demo] %s (created for the demo) at %.0f,%.0f,%.0f (frame %u)", cstr(g_arena + Spawned + i * 16 + 4, 12).c_str(), u32_as_f32(ld32(actor + 0x314)),
                u32_as_f32(ld32(actor + 0x318)), u32_as_f32(ld32(actor + 0x31C)), ld32(kDemoFrame));
    }
}
// Skip: every participant takes the pose of its tracks' last keys (relative to the demo's origin).
void end_poses(Cpu* c) {
    const float ox = u32_as_f32(ld32(g_arena + Origin)), oy = u32_as_f32(ld32(g_arena + Origin + 4)), oz = u32_as_f32(ld32(g_arena + Origin + 8));
    const float angle = u32_as_f32(ld32(g_arena + Origin + 12)), a = angle * 3.14159265f / 180.f, cs = std::cos(a), sn = std::sin(a);
    const uint32_t play = call(c, kPlay, {});
    uint32_t character_index = 0;
    for (const auto& d : g_demo.actors) {
        const bool is_character = d.kind == demo_scene::Actor::Kind::Character;
        const uint32_t index = is_character ? character_index++ : 0;
        if (d.kind == demo_scene::Actor::Kind::Model || (d.position.keys.empty() && d.rotation.keys.empty())) continue;
        uint32_t actor = 0;
        if (d.kind == demo_scene::Actor::Kind::Link) actor = play ? ld32(play + kPlayer) : 0;
        else if (is_character) actor = character(index);
        else {
            bool spawned = false;
            for (uint32_t k = 0; k < ld32(g_arena + SpawnCount) && k < 8; ++k) spawned |= cstr(g_arena + Spawned + k * 16 + 4, 12) == d.actor;
            if (!spawned) actor = find_actor(c, d.actor);
        }
        if (!actor) continue;
        story_notebook::WorldPoint p{u32_as_f32(ld32(actor + 0x314)), u32_as_f32(ld32(actor + 0x318)), u32_as_f32(ld32(actor + 0x31C))};
        if (!d.position.keys.empty()) {
            const auto& v = d.position.keys.back().value;
            p = {ox + v.x * cs + v.z * sn, oy + v.y, oz - v.x * sn + v.z * cs};
        }
        story_notebook::WorldPoint r{int16_t(ld16(actor + 0x328)) * 180.f / 32768.f, int16_t(ld16(actor + 0x32A)) * 180.f / 32768.f, int16_t(ld16(actor + 0x32C)) * 180.f / 32768.f};
        if (!d.rotation.keys.empty()) { const auto& v = d.rotation.keys.back().value; r = {v.x, std::remainder(v.y + angle, 360.f), v.z}; }
        mod_actor::Identity id;
        if (is_character && mod_actor::identify(c, actor, id)) {
            const bool ok = mod_actor::scene_pose(c, id, ld32(g_arena + Lease), p, r);
            LOG("[demo] skip: %s (mod actor) takes its end pose %.0f,%.0f,%.0f angle %.0f%s", d.id.c_str(), p.x, p.y, p.z, r.y, ok ? "" : " (refused)");
            continue;
        }
        for (uint32_t off : {0x314u, 0x300u}) { st32(actor + off, f32_as_u32(p.x)); st32(actor + off + 4, f32_as_u32(p.y)); st32(actor + off + 8, f32_as_u32(p.z)); }
        const uint16_t rx = uint16_t(int32_t(r.x * 32768.f / 180.f)), ry = uint16_t(int32_t(r.y * 32768.f / 180.f)), rz = uint16_t(int32_t(r.z * 32768.f / 180.f));
        for (uint32_t off : {0x320u, 0x328u}) { st16(actor + off, rx); st16(actor + off + 2, ry); st16(actor + off + 4, rz); }
        if (ld16(actor + 8) == 0x175) call(c, kResidentMtx, {actor});
        LOG("[demo] skip: %s takes its end pose %.0f,%.0f,%.0f", d.id.c_str(), p.x, p.y, p.z);
    }
}

// The demo message window with a message open (any state but its idle Wait).
uint32_t open_window(Cpu* c) {
    const uint32_t window = ld32(g_arena + WindowAt);
    if (!window || !ld32(g_arena + MessageShown)) return 0;
    return call(c, kCurrentState, {window + 0x18}) != kDemoWindowIdle ? window : 0;
}
// The game removes the demo; a skipped demo's actors then take their end poses, and the game actors
// created for it go.
void remove_demo(Cpu* c, bool skipped) {
    if (ld32(kDemoFile) == g_arena + Stb && ld32(kDemoMode)) call(c, kDemoRemove, {});
    if (skipped && !g_demo.actors.empty()) end_poses(c);
    remove_spawned(c);
}
}  // namespace

void release(Cpu* c, const char* reason) {
    if (g_waiting) { g_waiting.reset(); g_gate.reset(); LOG("[demo] the waiting demo is dropped (%s)", reason); }
    if (!recorded() || ld32(g_arena + Phase) == kDraining) return;
    const bool ours = ld32(kDemoFile) == g_arena + Stb && ld32(g_arena + StbBytes);
    const bool skipped = std::strcmp(reason, "skipped") == 0 && ld32(g_arena + Phase) >= 2;
    // A message still open (skip or abort at a message point): the window closes through its own Close
    // state (out animation, its cleanup, which also gives the demo's suspend count back) and the demo is
    // removed once the window is idle again (tick()), the order the game itself closes a demo message in.
    st32(g_arena + WindowClosing, 0);
    if (const uint32_t window = ours && ld32(kDemoMode) ? open_window(c) : 0) {
        call(c, kStateChange, {window + 0x18, kDemoWindowClose});
        st32(g_arena + WindowClosing, skipped ? 2 : 1);
        LOG("[demo] open demo message: the window closes before the demo is removed");
    } else remove_demo(c, skipped);
    if (ld32(g_arena + Phase) >= 1) {
        restore_manager(c);
        std::memcpy(mem::ptr(kDemoArcName), mem::ptr(g_arena + SavedArc), 12);
    }
    story_demo_parts::attach::release(c, parts(AttachRecord));
    story_demo_parts::light::release(c, parts(LightRecord));
    if (ld32(g_arena + CameraStarted)) {
        const uint32_t camera = call(c, kCamera, {});
        if (camera) {
            call(c, kStopCamera, {camera});
            // A demo that played to its end leaves the scene's held camera on its last framing (not the
            // view from before the demo), so a following step (another demo, which waits for this one's
            // cleanup, a say or camera step) continues from it.
            if (std::strcmp(reason, "finished") == 0 && !g_demo.eye.keys.empty()) {
                const float ox = u32_as_f32(ld32(g_arena + Origin)), oy = u32_as_f32(ld32(g_arena + Origin + 4)), oz = u32_as_f32(ld32(g_arena + Origin + 8));
                const float a = u32_as_f32(ld32(g_arena + Origin + 12)) * 3.14159265f / 180.f, cs = std::cos(a), sn = std::sin(a);
                const uint32_t at = g_arena + PartsScratch + 0x20;  // target, eye
                unsigned k = 0;
                for (const auto* t : {&g_demo.target, &g_demo.eye}) {
                    const auto& v = t->keys.back().value;
                    st32(at + k, f32_as_u32(ox + v.x * cs + v.z * sn)); st32(at + k + 4, f32_as_u32(oy + v.y)); st32(at + k + 8, f32_as_u32(oz - v.x * sn + v.z * cs));
                    k += 12;
                }
                call(c, kSetCamera, {camera, at, at + 12});
            }
        }
    }
    if (ld32(g_arena + MessageCount)) story_mail::unsay();
    for (uint32_t i = 0; i < ld32(g_arena + CharacterCount) && i < 16; ++i) {
        mod_actor::Identity id;
        const uint32_t actor = character(i);
        if (mod_actor::identify(c, actor, id)) mod_actor::end_scene(c, id, ld32(g_arena + Lease));
        else if (actor && ld16(actor + 8) == 0x175 && i < 12 && ld32(g_arena + CharacterState + i * 16 + 8)) {
            const uint32_t own = ld32(g_arena + CharacterState + i * 16 + 12);
            call(c, kResidentAnm, {actor, own ? own : kResidentWait, 1});
        }
    }
    st32(g_arena + CharacterCount, 0);
    g_character_refs.clear();
    if (ld32(g_arena + DemoFlagSet) && ld8(kDemoMessageFlag)) { call(c, kDemoMessageOff, {}); LOG("[demo] demo message flag off (was on at release; now %u)", ld8(kDemoMessageFlag)); }
    st32(g_arena + DemoFlagSet, 0);
    LOG("[demo] released (%s)%s", reason, ours ? (ld32(g_arena + WindowClosing) ? ": demo data removed after the message window" : ": demo data removed") : "");
    // the demo's model actors delete themselves on their next execute; the archives they draw from
    // are given back a few frames later (tick())
    st32(g_arena + Phase, kDraining);
    st32(g_arena + Ticks, 0);
    st32(g_arena + StbBytes, 0);
    g_messages.clear();
    g_texts.clear();
}

void tick(Cpu* c) {
    if (!recorded() || ld32(g_arena + Phase) != kDraining) return;
    const uint32_t ticks = ld32(g_arena + Ticks) + 1;
    st32(g_arena + Ticks, ticks);
    if (const uint32_t closing = ld32(g_arena + WindowClosing)) {
        if (open_window(c) && ticks <= 120) return;  // the window's out animation and cleanup run
        if (open_window(c)) LOG("[demo] the message window did not close in time");
        remove_demo(c, closing == 2);
        st32(g_arena + WindowClosing, 0);
        st32(g_arena + Ticks, 0);
        LOG("[demo] message window closed after %u ticks: demo data removed", ticks);
        return;
    }
    if (ticks < 10) return;
    drop_archives(c);
    if (const uint32_t t = ld32(g_arena + TransformSaved); (t >> 16) && !ld32(kDemoFile))
        if (const uint32_t control = ld32(kDemoControl)) { st8(control + 0x74, uint8_t(t >> 8)); st8(control + 0x75, uint8_t(t)); }
    st32(g_arena + TransformSaved, 0);
    st32(g_arena + Marker, 0);
    st32(g_arena + Phase, 0);
}
bool active() { return recorded() && ld32(g_arena + Phase) != kDraining; }
}  // namespace story_demo

namespace {
// registers the demo-step backend with the scene player at startup (story_scenes.h DemoBackend)
const bool g_registered = [] {
    story_scenes::DemoBackend b;
    b.arena_bytes = story_demo::kArenaBytes;
    b.configure = story_demo::configure;
    b.configured = story_demo::configured;
    b.install = story_demo::install;
    b.start = story_demo::start;
    b.poll = [](Cpu* c, std::string& why) {
        const auto p = story_demo::poll(c);
        why = p.why;
        return p.state == story_demo::Poll::State::Running ? 1 : p.state == story_demo::Poll::State::Done ? 2 : p.state == story_demo::Poll::State::Failed ? 3 : 0;
    };
    b.release = story_demo::release;
    b.tick = story_demo::tick;
    story_scenes::register_demo_backend(b);
    return true;
}();
}  // namespace
