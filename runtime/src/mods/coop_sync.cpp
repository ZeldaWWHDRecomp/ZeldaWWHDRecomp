// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the session (coop_sync.h).
//
// Each other player's poses are kept by the time they were taken on their game (its clock), and shown 60 ms behind
// the newest: between the two around that time, carried on along the last move for at most 250 ms when the next is
// late, then held. That player's clock against ours is the smallest gap seen, creeping up a little with each pose
// (so a slower link is followed, a faster one at once). The host's ship and monsters are shown the same way
// (host_state). A guest takes the host's world at its join: the host's portable state (its save data, stage and
// place; savestate.cpp), sent in pieces and loaded into the Quest Log the guest plays; the guest's saves are blocked at
// the file system from then (hle/fs.cpp) and it goes back to the title screen when the host is gone (or it leaves).
#include "coop_sync.h"

#include <algorithm>
#include <bit>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>

#include "coop_monsters.h"
#include "coop_pose.h"
#include "coop_progress.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"
#include "../input.h"
#include "../input_map.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../platform/keycodes.h"
#include "imgui.h"
#include "../portable_state.h"
#include "../savestate.h"
#include "../screenshot.h"
#include "../hle/save_root.h"
#include "../platform/host.h"

#include <filesystem>
#include <fstream>

namespace hlefs { extern std::atomic<bool> g_block_saves; }

namespace mods::coop::sync {
namespace {

const uint32_t kPlay = GD(0x1046F0B0), kStage = kPlay + 0x5134;
constexpr uint32_t kPos = 0x314, kRoom = 0x326, kAngleY = 0x32A;
constexpr int32_t kBehind = 60, kReach = 250, kStale = 500;  // (kBehind: a pose every 33 ms, and some to spare for a late one)  // a player with no pose for kStale is loading or away
constexpr size_t kChunk = 1000;

std::mutex g_mu;  // the settings (the menu writes them on the main thread)
Settings g_set;
bool g_set_read = false;
uint32_t g_set_gen = 1;
std::string g_status = "Off";

struct Clock {  // another game's clock: ours less theirs
    int32_t offset = 0;
    bool known = false;
    void seen(uint32_t theirs, uint32_t ours) {
        const int32_t gap = (int32_t)(ours - theirs);
        offset = !known || gap < offset ? gap : offset + 1, known = true;
    }
    // their time we show now (through a server, poses come at 15 Hz: 100 ms behind, as the design's buffer)
    uint32_t shown(uint32_t ours) const { return ours - offset - (net::via_server() ? 100 : kBehind); }
};
struct Remote {
    Clock clock;
    uint32_t heard = 0, gaps = 0, gap_sum = 0, gap_max = 0;  // poses' arrivals (the trace)
    size_t size = 0;
    std::deque<std::pair<uint32_t, Pose>> poses;  // by their time
    bool shown = false;
    float at[3] = {};
    int16_t yaw = 0;
    uint32_t sounds = 0;  // its sounds played here since the last trace
    uint32_t voice = 0;   // its voice frames played here since the last trace
};
Remote g_remote[net::kMaxPlayers];
std::deque<std::pair<uint32_t, std::string>> g_state[net::kMaxPlayers][kKinds];  // each player's states by kind, by its time
struct Area { char stage[8]; int8_t room; uint8_t owner; };
std::vector<Area> g_owners;  // who runs each place's monsters (owner())
std::vector<WorldMsg> g_world;
struct Effects { int from; uint32_t at; std::string body; };
std::vector<Effects> g_effects_in;  // the others' particles, started at their puppets as those are shown (kBehind)
// Link's sound effects (Anchor's PlayerSfx): JAIZelBasic::seStart(this, id, Vec* pos, u32, s8 reverb, u8, f32 x4), the
// place as an offset from Link's; played on the others at his puppet, 60 ms behind as it is shown
struct Sound { uint32_t id, r6, r7, r8; float f[4], off[3]; };
std::vector<Sound> g_sounds_out;
std::vector<Effects> g_sounds_in;

std::vector<int> g_send_join;    // host: guests the world is still to go to
std::string g_join;              // guest: the host's portable state as it comes
uint32_t g_join_total = 0;
std::atomic<int> g_join_load{0};  // guest: the host's world 0 not asked for, 1 loading, 2 arrived (ss's done)
bool g_deck_drop = false;
float g_deck[3];
bool g_was_guest = false;
uint32_t g_gen = 0, g_failed = 0;  // the settings the session runs with; ones it gave up on
std::string g_back;              // guest: why it goes back to the title (set once the host is gone)

float f32(uint32_t a) { return (float)ldf32(a); }
uint32_t now_ms_since(uint32_t t) { return net::now_ms() - t; }
std::string stage_now() { return std::string((const char*)mem::ptr(kStage), strnlen((const char*)mem::ptr(kStage), 8)); }

// a ship's frame (extras.cpp's to_local / to_world: local +x is (cos a, -sin a), +z (sin a, cos a))
void to_world(uint32_t hull, const float l[3], float w[3]) {
    const double t = (int16_t)ld16(hull + kAngleY) * M_PI / 32768;
    w[0] = (float)(f32(hull + kPos) + l[0] * cos(t) + l[2] * sin(t));
    w[1] = f32(hull + kPos + 4) + l[1];
    w[2] = (float)(f32(hull + kPos + 8) - l[0] * sin(t) + l[2] * cos(t));
}
void to_local(uint32_t hull, const float w[3], float l[3]) {
    const double t = (int16_t)ld16(hull + kAngleY) * M_PI / 32768;
    const double dx = w[0] - f32(hull + kPos), dz = w[2] - f32(hull + kPos + 8);
    l[0] = (float)(dx * cos(t) - dz * sin(t)), l[1] = w[1] - f32(hull + kPos + 4), l[2] = (float)(dx * sin(t) + dz * cos(t));
}

bool same_place(const Pose& a, const Pose& b) {
    const float d = std::hypot(a.pos[0] - b.pos[0], a.pos[2] - b.pos[2]);
    return !memcmp(a.stage, b.stage, 8) && a.flags == b.flags && d < 500;
}

// the pose shown at their time `rt`
bool pose_at(const Remote& r, uint32_t rt, Pose& out) {
    const auto& q = r.poses;
    if (q.empty()) return false;
    if ((int32_t)(rt - q.front().first) <= 0) return out = q.front().second, true;
    for (size_t i = 1; i < q.size(); i++)
        if ((int32_t)(rt - q[i].first) <= 0) {
            const auto &a = q[i - 1], &b = q[i];
            if (same_place(a.second, b.second)) blend(a.second, b.second, (float)(rt - a.first) / (float)(b.first - a.first), out);
            else out = b.second;  // a stage change or a jump: there at once
            return true;
        }
    out = q.back().second;  // late: on along its last move for a while, then held
    if (q.size() >= 2 && same_place(q[q.size() - 2].second, q.back().second)) {
        const auto &a = q[q.size() - 2], &b = q.back();
        const float k = (float)std::min<int32_t>((int32_t)(rt - b.first), kReach) / (float)std::max<int32_t>(1, (int32_t)(b.first - a.first));
        for (int i = 0; i < 3; i++) out.pos[i] += (b.second.pos[i] - a.second.pos[i]) * k;
    }
    return true;
}

// before the first online session with a save folder: a copy of its Quest Logs (cking.sav) into the user folder's
// online-backups/, once (a marker beside it); team progress is written into the player's own file (Anchor's way)
void backup_save() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::u8path(hle_fs::save_root());
    fs::path save;
    for (const fs::path& p : {root / "cking.sav", root / "user" / "cking.sav"}) if (fs::exists(p, ec)) save = p;
    if (save.empty()) for (const auto& d : fs::directory_iterator(root, ec)) if (fs::exists(d.path() / "cking.sav", ec)) save = d.path() / "cking.sav";
    if (save.empty()) return;
    uint64_t h = 0xcbf29ce484222325ull;  // (the marker: by the save's own path)
    for (const char ch : fs::absolute(save, ec).string()) h = (h ^ (uint8_t)ch) * 0x100000001b3ull;
    const fs::path dir = fs::u8path(host::config_dir()) / "online-backups";
    char name[64];
    snprintf(name, sizeof name, "%016llx", (unsigned long long)h);
    const fs::path marker = dir / (std::string(name) + ".done");
    if (fs::exists(marker, ec)) return;
    fs::create_directories(dir, ec);
    const std::time_t t = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y%m%d-%H%M%S", std::localtime(&t));
    const fs::path to = dir / (std::string(when) + "-" + name + "-cking.sav");
    if (!fs::copy_file(save, to, fs::copy_options::overwrite_existing, ec) || ec) {
        mods::trace("coop: the save's backup failed (%s)", ec.message().c_str());
        return;
    }
    std::ofstream(marker) << save.string() << "\n";
    mods::trace("coop: the save backed up before the first online session: %s", to.string().c_str());
    ss::notice("Online co-op: your save was backed up first. A new Quest Log just for co-op is recommended.");
}

std::atomic<bool> g_hud_hidden{false};
uint32_t g_hud_link = 0;  // Link when it was hidden: shown again once there is another (the guest plays again)
bool g_to_title = false;  // asked for at the play scene's next execute (below)
void back_to_title(const std::string& why) {
    ss::notice(why);
    mods::trace("coop: back to the title: %s", why.c_str());
    g_to_title = true, g_hud_hidden = true, g_hud_link = ld32(kPlay + 0x5B34);
}

// doors (Asad's rule: a guest going into a house, cave or dungeon the host isn't in needs the host's permission): the
// guest's dComIfGp_setNextStage (0252012C: r3 stage, r4 point, r5 room, r6 layer, f1 speed, r7 mode, r8 set point, r9
// wipe) into a stage other than the sea, its own or the host's is held there (its door or exit waits as the scene
// change does) and the host asked; let in, it is made as it was asked; refused, or no answer in 30 s, the guest is
// brought back beside the host as one who fell. The host answers on a panel, A or B
struct Door { bool held = false, go = false; char stage[8]; uint32_t r[7]; double f1; uint32_t since; };
Door g_door;                                // guest
uint32_t g_door_refused = 0;                // guest: when it was last refused (its exit asks again until it is moved)
char g_door_refused_stage[8];               //   and where to
struct Ask { int from; std::string stage; uint32_t at, no; };
std::deque<Ask> g_asks;                     // host: who asks to go where, oldest first
uint32_t g_asks_made = 0;
std::string g_ask_text;                     // host: the panel's question (g_mu)
uint32_t g_ask_no = 0;                      // host: the question on the panel (g_mu, with its text)
int g_ask_said = -1;                        // host: the panel's answer (1 let in, 0 not) to question g_ask_said_no (g_mu)
uint32_t g_ask_said_no = 0;
constexpr uint32_t kDoorWait = 30000;

// cutscenes (Asad's rule: everyone watches): a cutscene the host's game starts (an event of the stage's list: the story's,
// `MEET_TETORA`, `master_sword`, and the places' own camera ones, `MagmaDemo`, `Bridge00`; not the everyday ones each
// player has for themselves, talking, chests, doors, item gets, warps, falls: generic(), by their names) is
// started on each guest in the same stage and room, or within 3000 of the host with the host's room loaded (one a room
// plays as the host walks in starts before a guest following is through the door; a room not loaded here would be
// a black void), by its name (fopAcM_orderOtherEvent2 025D77DC: r3 actor, r4 name,
// r5 flag, r6 hind), so its own Link and people play it. dEvt_control_c is at play +0x51D0 (its mode +0xC2, the event
// +0xD8), dEvent_manager_c at +0x52C4 (getEventData 02544044: r3 it, r4 the event; a 0xB0 entry, its name at +0).
// One a guest's game cannot finish (a person it lacks) would hold it there: still playing 10 s after the host's is
// over, the guest is brought back beside the host, which ends it (not counting while a text box is open here: each
// player reads at their own pace)
constexpr uint32_t kEvMode = 0x5292, kEvId = 0x52A8, kEvManager = 0x52C4;
struct Cut { std::string name; char stage[8]; int8_t room; uint32_t since = 0; bool asked = false; };
Cut g_cut;            // guest: the host's, to start here
std::string g_cut_on;  // guest: the host's playing here
std::atomic<int> g_go_to{-1};  // the menu's "Go to": the player to be taken beside
uint32_t g_cut_over = 0;  // guest: when the host's was over (0: not yet)
// a room of the stage is loaded here (its status, 0x1047E8E8 + 0x22C a room, bit 0: as door10's openProc waits for)
bool room_loaded(int room) { return room >= 0 && room < 64 && (ld8(GD(0x1047E8E8) + 0x22C * (uint32_t)room) & 1); }
// (a guest) the host stands within 3000 of this game's Link, in its stage (not on a ship's deck: another frame)
bool host_near(uint32_t link) {
    const Remote& h = g_remote[0];
    if (h.poses.empty()) return false;
    const Pose& p = h.poses.back().second;
    return !(p.flags & 1) && !strncmp(p.stage, (const char*)mem::ptr(kStage), 8) &&
           std::hypot(p.pos[0] - f32(link + kPos), p.pos[2] - f32(link + kPos + 8)) < 3000;
}
// an everyday event: its own player's alone (Dragon Roost Cavern's list: DEFAULT_TALK, KNOB_START, SHUTTER_START,
// MAGMA_COMEBACK, TACT_WINDOW...)
bool generic(const std::string& n) {
    for (const char* p : {"DEFAULT_", "KNOB", "SHUTTER", "BS_SHUTTER", "TACT_WINDOW", "OPTION_CHAR", "TIMEWARP", "RiddleSound",
                          "MapToolCamera", "FALL_START", "DUNGEON_WARP", "WARPT_OPEN", "fire_", "zenfire_", "magma_cam"})
        if (!n.compare(0, strlen(p), p)) return true;
    return n.size() > 9 && !n.compare(n.size() - 9, 9, "_COMEBACK");
}
// the event running here: its name and kind ("" none)
std::string event_now(Cpu* c, uint32_t* kind = nullptr) {
    const int16_t id = (int16_t)ld16(kPlay + kEvId);
    if (!ld8(kPlay + kEvMode) || id < 0) return "";
    const uint32_t ev = guest_call(c, GC(0x02544044), {kPlay + kEvManager, (uint32_t)(int32_t)id});
    if (!ev) return "";
    if (kind) *kind = ld32(ev + 0x24);
    return std::string((const char*)mem::ptr(ev), strnlen((const char*)mem::ptr(ev), 32));
}

bool door_draw(const float* pad) {
    const input::PadState st = input_map::controller_state(input_map::current(), pad);
    const uint32_t now = st.buttons;
    static uint32_t prev = ~0u;
    static bool keys_prev[256];
    bool keys[256];
    input::held_keys(keys);
    if (prev == ~0u) prev = now, std::copy(keys, keys + 256, keys_prev);
    auto hit = [&](uint32_t b, std::initializer_list<int> ks) {
        bool k = false;
        for (int c : ks) k |= keys[c] && !keys_prev[c];
        return (now & b && !(prev & b)) || k;
    };
    const bool yes = hit(input::kA, {kVK_ANSI_K, kVK_Space, kVK_Return}), no = !yes && hit(input::kB, {kVK_ANSI_J, kVK_Escape});
    prev = now, std::copy(keys, keys + 256, keys_prev);
    std::string text;
    {
        std::lock_guard<std::mutex> l(g_mu);
        text = g_ask_text;
        if (!text.empty() && (yes || no)) g_ask_said = yes, g_ask_said_no = g_ask_no;  // (to the question shown)
    }
    if (text.empty() || yes || no) return prev = ~0u, true;  // (or no longer asked: the host went there, the guest left)
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("##coop_door", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
        ImGui::TextUnformatted(text.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("A let them in    B no");
    }
    ImGui::End();
    return yes || no;
}

// (the hook) this stage change is held for the host's answer
bool door_held(Cpu* c) {
    if (g_door.go || !guest() || !arrived() || !c->r[3] || !settings().stay_together) return false;
    char st[8] = {};
    strncpy(st, (const char*)mem::ptr(c->r[3]), 8);
    const Remote& h = g_remote[0];
    if (!strncmp(st, "sea", 8) || !strncmp(st, (const char*)mem::ptr(kStage), 8) || h.poses.empty() ||
        !strncmp(st, h.poses.back().second.stage, 8)) {
        if (g_door.held && !memcmp(st, g_door.stage, 8)) g_door = Door{};  // (the host went in meanwhile: as let in)
        return false;
    }
    if (g_door.held && !memcmp(st, g_door.stage, 8)) return true;  // (asked already)
    if (g_door_refused && now_ms_since(g_door_refused) < 5000 && !memcmp(st, g_door_refused_stage, 8)) return true;  // (refused: on its way back)
    static const uint32_t name = mem::host_alloc(16);
    for (int i = 0; i < 8; i++) st8(name + i, (uint8_t)st[i]);
    st8(name + 8, 0);
    g_door = Door{true, false, {}, {name, c->r[4], c->r[5], c->r[6], c->r[7], c->r[8], c->r[9]}, c->f[1].ps0, net::now_ms()};
    memcpy(g_door.stage, st, 8);
    net::send(0, net::kDoorAsk, std::string(st, 8));
    ss::notice("Waiting for " + net::name(0) + " to let you into " + ss::area_name((const char*)mem::ptr(name)));
    mods::trace("coop: going into %.8s: the host asked", st);
    return true;
}

struct Handler : net::Handler {
    void message(int from, net::Msg type, const uint8_t* d, size_t n) override {
        if ((type == net::kEffect || type == net::kSound) && g_effects_in.size() + g_sounds_in.size() >= 256) return;  // (bounded)
        if (type == net::kEffect) return g_effects_in.push_back({from, net::now_ms(), std::string((const char*)d, n)});
        if (type == net::kProgress) return progress::received(d, n);
        if (type == net::kDoorMove) return progress::door_moved(d, n);
        if (type == net::kRupees) return progress::rupees_received(d, n);
        if (type == net::kSound) return g_sounds_in.push_back({from, net::now_ms(), std::string((const char*)d, n)});
        if (type == net::kOwner) {  // (a guest) the host's table of who runs each place
            g_owners.clear();
            for (size_t o = 0; o + 10 <= n; o += 10) {
                Area a;
                memcpy(a.stage, d + o, 8), a.room = (int8_t)d[o + 8], a.owner = d[o + 9];
                g_owners.push_back(a);
            }
            return;
        }
        if (type == net::kDead) return monsters::dead_from(d, n);  // (a guest) the host's killed monsters
        if (type == net::kCutscene && n == 9 && guest()) {  // (over: not started here yet, not started; started, its 10 s from now)
            if (g_cut.since && !g_cut.asked) g_cut = Cut{};
            return void(g_cut_over = net::now_ms());
        }
        if (type == net::kCutscene && n >= 9 + 1 && guest()) {
            g_cut = Cut{std::string((const char*)d + 9, strnlen((const char*)d + 9, std::min<size_t>(n - 9, 32))), {}, (int8_t)d[8], net::now_ms()};
            g_cut_over = 0;
            memcpy(g_cut.stage, d, 8);
            return;
        }
        if (type == net::kDoorAsk && n >= 8 && host()) {  // (once each, a few at most)
            const std::string st((const char*)d, 8);
            if (g_asks.size() < 8 && std::none_of(g_asks.begin(), g_asks.end(), [&](const Ask& a) { return a.from == from && a.stage == st; }))
                g_asks.push_back({from, st, net::now_ms(), ++g_asks_made});
            return;
        }
        if (type == net::kDoorAnswer && n >= 9 && g_door.held && !memcmp(d, g_door.stage, 8)) {
            if (d[8]) g_door.go = true;
            else g_door.since = net::now_ms() - kDoorWait;  // (refused: back beside the host at the next step)
            return;
        }
        if (type == net::kDown) {  // (host) a guest fell: its world again, beside the host
            if (net::role() == net::Role::kHost && std::find(g_send_join.begin(), g_send_join.end(), from) == g_send_join.end())
                g_send_join.push_back(from), mods::trace("coop: player %d fell: brought back here", from);
            return;
        }
        if (type != net::kJoinState) {
            g_world.push_back({from, type, std::string((const char*)d, n)});
            return;
        }
        uint32_t total, at;
        if (n < 8) return;
        memcpy(&total, d, 4), memcpy(&at, d + 4, 4);
        if (at == 0) g_join.clear(), g_join_total = total;
        if (at != g_join.size() || total != g_join_total || total > pstate::kMaxFileSize) return;
        g_join.append((const char*)d + 8, n - 8);
        if (g_join.size() < g_join_total) return;
        auto s = std::make_shared<pstate::State>();
        std::string why;
        if (!pstate::read(g_join, *s, why)) {
            mods::trace("coop: the host's world does not load: %s", why.c_str());
            ss::notice("Can't load the host's world (" + why + ")");
            return;
        }
        // set down beside the host; on Tetra's ship's deck, there once this game has made her (take_deck_drop)
        Pose hp;
        const Remote& h = g_remote[0];
        g_deck_drop = pose_at(h, h.clock.shown(net::now_ms()), hp) && (hp.flags & 1);
        if (g_deck_drop) g_deck[0] = hp.pos[0] + 120, g_deck[1] = hp.pos[1], g_deck[2] = hp.pos[2];
        s->pos[0] += 120;
        g_door = Door{};  // (a door it waited at is behind it)
        g_join_load = 1;
        hlefs::g_block_saves = true;
        if (static bool once = false; once) ss::notice("Beside " + net::name(0) + " again");  // (after a story moment, a fall or a door refused)
        else once = true;
        ss::request_load_portable_state(s, [](bool ok, const std::string& why) {
            if (g_join_load == 1) g_join_load = ok ? 2 : 0;
            if (!ok) mods::trace("coop: the host's world did not load: %s", why.c_str());
        });
        mods::trace("coop: the host's world (%zu bytes): %s room %d at %.0f %.0f %.0f%s", g_join.size(), s->stage.c_str(), s->room,
                    s->pos[0], s->pos[1], s->pos[2], g_deck_drop ? ", the host on the ship's deck" : "");
    }
    void state(int from, uint8_t kind, uint32_t ms, const uint8_t* d, size_t n) override {
        Remote& r = g_remote[from];
        if (kind == kVoice) return;  // (voice chat: not in this build yet)
        r.clock.seen(ms, net::now_ms());
        if (kind == kPose) {
            Pose p;
            if (!unpack(d, n, p)) return;
            if (const uint32_t now = net::now_ms(), gap = now - r.heard; r.heard)
                r.gaps++, r.gap_sum += gap, r.gap_max = std::max(r.gap_max, gap), r.heard = now;
            else r.heard = now;
            r.size = n;
            if (!r.poses.empty() && (int32_t)(ms - r.poses.back().first) <= 0) return;  // (out of order: dropped)
            // that player came into this game's stage or left it: told by name
            const std::string mine = stage_now(), now_at(p.stage, strnlen(p.stage, 8));
            if (const std::string was = r.poses.empty() ? now_at : std::string(r.poses.back().second.stage, strnlen(r.poses.back().second.stage, 8));
                was != now_at && !mine.empty()) {
                if (now_at == mine) ss::notice(net::name(from) + " is back");
                else if (was == mine) ss::notice(net::name(from) + (now_at == "sea" ? " went out to sea" : " went inside"));
            }
            r.poses.emplace_back(ms, std::move(p));
            while (r.poses.size() > 2 && (int32_t)(ms - r.poses.front().first) > 1000) r.poses.pop_front();
        } else if (kind < kKinds) {
            auto& q = g_state[from][kind];
            if (!q.empty() && (int32_t)(ms - q.back().first) <= 0) return;
            q.emplace_back(ms, std::string((const char*)d, n));
            while (q.size() > 2 && (int32_t)(ms - q.front().first) > 1000) q.pop_front();
        }
    }
    void joined(int id) override {
        ss::notice(net::name(id) + " joined (" + kTunics[net::tunic(id)] + ")");
        if (net::role() == net::Role::kHost) g_send_join.push_back(id);
    }
    void left(int id, const char* why) override {
        g_remote[id] = Remote{};
        show(id < net::my_id() ? id : std::max(0, id - 1), nullptr, nullptr, 0);
        if (id != 0 || net::role() != net::Role::kOff) return ss::notice(net::name(id) + " left");
        g_failed = g_gen;  // (not joined again until the toggle or the settings change)
        if (g_was_guest && settings().stay_together) g_back = why;  // (the fork's way: the host's world is gone with it)
        else if (g_was_guest) {  // (Anchor's way: this game plays on, alone, in its own Quest Log, which it saves itself)
            hlefs::g_block_saves = false, g_was_guest = false;
            ss::notice(std::string("Online co-op: ") + why + ". You play on alone.");
            mods::trace("coop: the host is gone (%s): playing on here", why);
        }
        else ss::notice(std::string("Online co-op: ") + why);
    }
} g_handler;

}  // namespace

Settings settings() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_set_read) {
        g_set_read = true;
        std::string v;
        if (hostui::get("mod.coop.choice", v)) g_set.role = atoi(v.c_str()) == 1;
        if (hostui::get("mod.coop.addr", v) && !v.empty()) g_set.addr = v;
        if (hostui::get("mod.coop.port", v) && atoi(v.c_str()) > 0) g_set.port = atoi(v.c_str());
        if (hostui::get("mod.coop.name", v) && !v.empty()) g_set.name = v;
        if (hostui::get("mod.coop.tunic", v)) g_set.tunic = atoi(v.c_str()) & 3;
        if (hostui::get("mod.coop.voice", v)) g_set.voice = atoi(v.c_str()) != 0;
        if (hostui::get("mod.coop.enabled", v)) g_set.on = atoi(v.c_str()) != 0;
        if (hostui::get("mod.coop.stay_together", v)) g_set.stay_together = atoi(v.c_str()) != 0;
        if (hostui::get("online.strangers", v)) g_set.strangers = atoi(v.c_str()) == 1;
        if (hostui::get("online.server", v)) g_set.server = atoi(v.c_str()) != 0;
        if (hostui::get("online.server_addr", v) && !v.empty()) g_set.server_addr = v;
        if (hostui::get("online.server_port", v) && atoi(v.c_str()) > 0) g_set.server_port = atoi(v.c_str());
        // (tests: WWHD_ONLINE_SERVER=<address>[:port] goes through that server)
        if (const char* e = getenv("WWHD_ONLINE_SERVER")) {
            std::string a = e;
            g_set.server = true, g_set.on = true;
            if (const size_t colon = a.rfind(':'); colon != std::string::npos) g_set.server_port = atoi(a.c_str() + colon + 1), a.resize(colon);
            g_set.server_addr = a;
        }
        if (hostui::get("online.notice_accepted", v)) g_set.notice_ok = v == kNoticeVersion;
        if (hostui::get("mod.coop.character", v)) g_set.character = atoi(v.c_str()) & 3;
        // (tests: WWHD_COOP=host[:port] or join:<address>[:port], WWHD_COOP_NAME, WWHD_COOP_TUNIC 0-3)
        if (const char* e = getenv("WWHD_COOP")) {
            std::string s = e;
            g_set.role = s.rfind("join:", 0) == 0;
            if (g_set.role) s = s.substr(5), g_set.addr = s.substr(0, s.find(':'));
            else s = s.substr(std::min(s.size(), (size_t)4));
            if (const size_t colon = s.find(':'); colon != std::string::npos) g_set.port = atoi(s.c_str() + colon + 1);
            g_set.on = true;
        }
        // (tests: the player's acceptance of the privacy notice, which a test run cannot click)
        if (const char* e = getenv("WWHD_COOP_NOTICE_OK")) g_set.notice_ok = atoi(e) != 0;
        if (const char* e = getenv("WWHD_COOP_NAME")) g_set.name = e;
        if (const char* e = getenv("WWHD_COOP_STAY_TOGETHER")) g_set.stay_together = atoi(e) != 0;
        if (const char* e = getenv("WWHD_COOP_TUNIC")) g_set.tunic = atoi(e) & 3;
        if (const char* e = getenv("WWHD_COOP_VOICE")) g_set.voice = atoi(e) != 0;
        if (const char* e = getenv("WWHD_CHARACTER")) g_set.character = atoi(e) & 3;
        if (!getenv("WWHD_COOP_TUNIC") && !hostui::get("mod.coop.tunic", v)) g_set.tunic = g_set.role;  // host green, a guest red
    }
    return g_set;
}

void set_settings(const Settings& s) {
    settings();
    std::lock_guard<std::mutex> lk(g_mu);
    // (a session restarts only for what it was started with: not the character or voice chat)
    if (s.role != g_set.role || s.addr != g_set.addr || s.port != g_set.port || s.name != g_set.name || s.tunic != g_set.tunic ||
        s.server != g_set.server || s.server_addr != g_set.server_addr || s.server_port != g_set.server_port)
        g_set_gen++;
    g_set = s;
}

void save_settings(const Settings& s) {
    set_settings(s);
    hostui::set("mod.coop.enabled", s.on ? "1" : "0");
    hostui::set("online.notice_accepted", s.notice_ok ? kNoticeVersion : "");
    hostui::set("mod.coop.choice", std::to_string(s.role));
    hostui::set("mod.coop.addr", s.addr), hostui::set("mod.coop.port", std::to_string(s.port));
    hostui::set("mod.coop.name", s.name), hostui::set("mod.coop.tunic", std::to_string(s.tunic));
    hostui::set("mod.coop.stay_together", s.stay_together ? "1" : "0");
    hostui::set("online.strangers", std::to_string(s.strangers));
    hostui::set("online.server", s.server ? "1" : "0");
    hostui::set("online.server_addr", s.server_addr), hostui::set("online.server_port", std::to_string(s.server_port));
}

const char* status() { return g_status.c_str(); }
bool host() { return net::role() == net::Role::kHost; }
bool guest() { return net::role() == net::Role::kGuest && net::my_id() != 0; }

bool place(int id, float out[3]) {
    if (id < 0 || id >= net::kMaxPlayers || !g_remote[id].shown || !net::member(id)) return false;  // (strangers: puppets only)
    memcpy(out, g_remote[id].at, 12);
    return true;
}

void nearest(const float at[3], uint32_t link, int& target, float out[3]) {
    const int me = net::role() == net::Role::kOff ? 0 : net::my_id();
    auto where = [&](int id, float p[3]) {
        if (id == me) return p[0] = f32(link + kPos), p[1] = f32(link + kPos + 4), p[2] = f32(link + kPos + 8), true;
        return id >= 0 && id < net::kMaxPlayers && g_remote[id].shown && net::member(id) && (memcpy(p, g_remote[id].at, 12), true);
    };
    auto dist = [&](const float p[3]) { return std::hypot(p[0] - at[0], p[2] - at[2]); };
    float p[3], best[3];
    int pick = -1;
    for (int id = 0; id < net::kMaxPlayers; id++)
        if (where(id, p) && (pick < 0 || dist(p) < dist(best))) pick = id, memcpy(best, p, 12);
    if (pick < 0) pick = me, where(me, best);
    if (float cur[3]; target != pick && where(target, cur) && dist(best) > 0.7f * dist(cur)) pick = target, memcpy(best, cur, 12);
    target = pick;
    memcpy(out, best, 12);
}

// the Wind Waker's sounds as a player conducts (JAIZelInst, d_a_player_tact.inc: judge's beat pulses and metronome,
// metronomePlay's drone and upbeat, melodyPlay's song once it is matched); started with no place, so they go to the
// others as if at his head (the conductor's own menu sounds, seStartSystem's, stay his)
bool tact_sound(uint32_t id) {
    return (id >= 0x887 && id <= 0x895) || id == 0x8ED || id == 0x8EE || id == 0x2868 || id == 0x2880 || id == 0x2881;
}
bool sound_started(Cpu* c) {
    const uint32_t link = ld32(kPlay + 0x5B34), pos = c->r[5];
    if (link && !pos && tact_sound(c->r[4]) && net::role() != net::Role::kOff && g_sounds_out.size() < 32) {
        Sound sd{c->r[4], c->r[6], c->r[7], c->r[8], {}, {0, 100, 0}};
        for (int k = 0; k < 4; k++) sd.f[k] = (float)c->f[k + 1].ps0;
        g_sounds_out.push_back(sd);
        return true;
    }
    if (!link || pos < link || pos >= link + 0x7000) return true;  // (daPy_lk_c is about 0x6A80 bytes in WWHD)
    if (net::role() != net::Role::kOff && g_sounds_out.size() < 32) {  // (as Link's: each game says it as that player's character)
        Sound sd{c->r[4], c->r[6], c->r[7], c->r[8], {}, {}};
        for (int k = 0; k < 4; k++) sd.f[k] = (float)c->f[k + 1].ps0;
        for (int k = 0; k < 3; k++) sd.off[k] = f32(pos + 4 * k) - f32(link + kPos + 4 * k);
        g_sounds_out.push_back(sd);
    }
    return (c->r[4] = char_voice(c->r[4], own_char())) != 0;
}

// (the host's world arrived: not the Quest Log the guest picked to have it loaded into, whose Link comes first)
bool arrived() { return host() || (guest() && (!settings().stay_together || g_join_load == 2)); }

bool take_deck_drop(float local[3]) {
    if (!g_deck_drop || !arrived()) return false;
    g_deck_drop = false;
    memcpy(local, g_deck, 12);
    return true;
}

std::vector<WorldMsg> take_world() { return std::move(g_world); }

bool host_state(Kind k, std::string& a, std::string& b, float& t) { return state_of(0, k, a, b, t); }

int owner(const char stage[8], int room) {
    for (const Area& a : g_owners) if (!memcmp(a.stage, stage, 8) && a.room == room) return a.owner;
    return -1;
}

bool anyone_in(const char stage[8]) {
    return std::any_of(g_owners.begin(), g_owners.end(), [&](const Area& a) { return !memcmp(a.stage, stage, 8); });
}

bool state_of(int from, Kind k, std::string& a, std::string& b, float& t) {
    if (from < 0 || from >= net::kMaxPlayers || k >= kKinds) return false;
    const auto& q = g_state[from][k];
    if (q.empty()) return false;
    const uint32_t rt = g_remote[from].clock.shown(net::now_ms());
    a = b = q.back().second, t = 0;
    for (size_t i = 1; i < q.size(); i++)
        if ((int32_t)(rt - q[i].first) <= 0) {
            a = q[i - 1].second, b = q[i].second;
            t = std::clamp((float)(int32_t)(rt - q[i - 1].first) / (float)(q[i].first - q[i - 1].first), 0.f, 1.f);
            return true;
        }
    if (q.size() >= 2) {  // late: on past the newest for a while (t over 1)
        const auto &p = q[q.size() - 2], &n = q.back();
        a = p.second, t = 1 + (float)std::min<int32_t>((int32_t)(rt - n.first), kReach) / (float)std::max<int32_t>(1, (int32_t)(n.first - p.first));
    }
    return true;
}

void poll(Cpu* c, bool on) {
    uint32_t &gen = g_gen, &failed = g_failed;
    Settings s = settings();
    uint32_t want_gen;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        static int role = -1;
        if (role != s.role) role = s.role, g_set_gen++;
        want_gen = g_set_gen;
    }
    // test aid: WWHD_COOP_SHOT=<file>: a screenshot when that file appears (a test driver touches it), then it is removed
    if (static const char* shot = getenv("WWHD_COOP_SHOT"); shot && remove(shot) == 0) screenshot::request(), mods::trace("coop: shot");
    const std::string stage = stage_now();
    const bool title = stage.empty() || stage == "sea_T" || stage == "Name";
    if (static bool was = true; title != was && (was = title)) mods::trace("coop: at the title");
    if (title && net::role() == net::Role::kOff) hlefs::g_block_saves = false;  // the guest's own Quest Logs again
    if (net::role() != net::Role::kOff && (!on || gen != want_gen)) {  // switched off, or other settings
        const bool was = guest();
        net::stop(nullptr);
        for (Remote& r : g_remote) r = Remote{};
        for (int i = 0; i < kPuppets; i++) show(i, nullptr, nullptr, 0);
        if (was && settings().stay_together) g_back = "You left the host's game";
        else if (was) hlefs::g_block_saves = false, g_was_guest = false;
    }
    if (!g_back.empty()) {
        back_to_title(g_back);
        g_back.clear(), g_was_guest = false, g_deck_drop = false, g_door = Door{}, g_join_load = 0;
    }
    // opt-in: no socket, no packet before the player has switched online on and accepted the privacy notice
    if (on && !s.notice_ok) {
        if (static bool told = false; !told) told = true, mods::trace("coop: not started: the privacy notice is not accepted");
        g_status = "Not started: accept the privacy notice first (other players see your IP address)";
        on = false;
    }
    if (on && net::role() == net::Role::kOff && failed != want_gen) {
        gen = want_gen;
        std::string why;
        if (!(s.server ? net::start_server(s.server_addr, s.server_port, s.name, s.tunic, why)
                       : net::start(s.role ? net::Role::kGuest : net::Role::kHost, s.addr, s.port, s.name, s.tunic, why))) {
            failed = want_gen;
            g_status = "Not started: " + why;
            ss::notice("Online co-op: " + why);
        } else backup_save();
    }
    if (!on && net::role() == net::Role::kOff && g_status.rfind("Not started", 0) != 0) g_status = "Off";
    if (net::role() == net::Role::kOff) {
        progress::stop();
        g_asks.clear(), g_cut = Cut{}, g_cut_on.clear();
        std::lock_guard<std::mutex> l(g_mu);
        g_ask_text.clear(), g_ask_said = -1;  // (a question still up closes)
    }
    net::poll(g_handler);
    if (static bool told = false; !arrived()) told = false;
    else if (guest() && !told) told = true, net::send(0, net::kArrived, std::string());
    own_tunic(host() ? net::tunic(0) : guest() ? net::tunic(net::my_id()) : -1);
    if (guest() && !g_was_guest && net::tunic(net::my_id()) != s.tunic)
        ss::notice(std::string(kTunics[s.tunic]) + " was taken, you're " + kTunics[net::tunic(net::my_id())]);
    if (guest()) g_was_guest = true;
    if (net::role() != net::Role::kOff)
        g_status = std::string(net::status()) + (guest() && g_join_total && g_join.size() < g_join_total ? ", loading the host's world" : "");
    else if (!g_back.empty() || failed != want_gen) g_status = net::status();
}

bool hud_hidden() { return g_hud_hidden.load(std::memory_order_relaxed); }

void go_to(int id) { g_go_to = id; }

void step(Cpu* c, uint32_t link, uint32_t hull, bool aboard) {
    if (link != g_hud_link) g_hud_hidden = false;  // (playing again)
    if (net::role() == net::Role::kOff) return;
    // this game's pose out (on Tetra's ship's deck, or the craft's: in its frame)
    Pose me;
    const float at[3] = {f32(link + kPos), f32(link + kPos + 4), f32(link + kPos + 8)};
    const bool on_deck = hull && aboard;
    if (own_pose(me)) {
        if (on_deck) {
            to_local(hull, me.pos, me.pos);
            me.yaw = (int16_t)(me.yaw - (int16_t)ld16(hull + kAngleY)), me.flags |= 1;
        }
        std::string b;
        pack(me, b);
        net::put_state(kPose, b);
    }
    if (const std::vector<Effect> fx = take_effects(); !fx.empty())
        net::send(-1, net::kEffect, std::string((const char*)fx.data(), fx.size() * sizeof(Effect)));
    if (!g_sounds_out.empty())
        net::send(-1, net::kSound, std::string((const char*)g_sounds_out.data(), g_sounds_out.size() * sizeof(Sound))), g_sounds_out.clear();
    progress::step(c, arrived());
    // who runs each place's monsters (owner()): the host where it is; elsewhere the player there longest, kept while they
    // stay (Valheim's way: no handing over mid-fight to whoever comes nearer). Sent to the guests as it changes, and every
    // 3 s for those who joined since
    if (host()) {
        struct Here { char stage[8]; int8_t room; uint32_t since; bool in; };
        static Here here[net::kMaxPlayers];
        const uint32_t now = net::now_ms();
        for (int id = 0; id < net::kMaxPlayers; id++) {
            const Remote& r = g_remote[id];
            const Pose* p = id && net::member(id) && !r.poses.empty() && now - r.heard < (uint32_t)kStale ? &r.poses.back().second : nullptr;
            Here& h = here[id];
            if (id && !p) { h.in = false; continue; }
            char st[8];
            memcpy(st, p ? p->stage : (const char*)mem::ptr(kStage), 8);
            const int8_t rm = p ? p->room : (int8_t)ld8(link + kRoom);
            if (!h.in || memcmp(h.stage, st, 8) || h.room != rm) memcpy(h.stage, st, 8), h.room = rm, h.since = now;
            h.in = true;
        }
        std::vector<Area> next;
        for (int id = 0; id < net::kMaxPlayers; id++) {
            auto same = [&](const Area& a, int p) { return here[p].in && !memcmp(here[p].stage, a.stage, 8) && here[p].room == a.room; };
            if (!here[id].in || std::any_of(next.begin(), next.end(), [&](const Area& a) { return same(a, id); })) continue;
            Area a{};
            memcpy(a.stage, here[id].stage, 8), a.room = here[id].room;
            const int was = owner(a.stage, a.room);
            int first = id;
            for (int p = id; p < net::kMaxPlayers; p++) if (same(a, p) && here[p].since < here[first].since) first = p;
            a.owner = (uint8_t)(same(a, 0) ? 0 : was >= 0 && same(a, was) ? was : first);
            next.push_back(a);
        }
        static uint32_t sent = 0;
        const bool changed = next.size() != g_owners.size() ||
                             !std::equal(next.begin(), next.end(), g_owners.begin(), [](const Area& x, const Area& y) {
                                 return !memcmp(x.stage, y.stage, 8) && x.room == y.room && x.owner == y.owner;
                             });
        g_owners = std::move(next);
        if (changed || now - sent > 3000) {
            std::string b;
            for (const Area& a : g_owners) b.append(a.stage, 8), b += (char)a.room, b += (char)a.owner;
            net::send(-1, net::kOwner, b), sent = now;
        }
    }
    // the host's story moments: an event (dComIfGp_event_runCheck) of 3 s or more that set story bits (dSv_event_c) or
    // changed the stage: each guest elsewhere (another stage or room, or over 1500 away) is brought beside the host
    // afterwards, by the host's world sent again as at the join (Asad's rule: guests are pulled to the host; a shared
    // cutscene itself is not played on the guests). Talking, chests and doors are shorter or set no story bits
    if (host()) {
        static bool ev_was = false;
        static uint32_t ev_steps = 0;
        static std::string ev_bits, ev_stage;
        const uint32_t sv = ld32(GD(0x101F84DC)) + 0x20;  // dSv_info_c (savestate.cpp); dSv_event_c at +0x624
        const auto bits = [&] { return std::string((const char*)mem::ptr(sv + 0x624), 0x100); };
        const bool ev = ld8(kPlay + 0x5292) != 0;          // dEvt_control_c mode (savestate.cpp kEventRun)
        static int16_t ev_seen = -1;  // (the event last looked at: one leads into another without the mode dropping)
        static bool ev_cut = false;
        if (ev && !ev_was) ev_steps = 0, ev_bits = bits(), ev_stage = stage_now(), ev_seen = -1, ev_cut = false;
        if (!ev && ev_was && ev_cut) net::send(-1, net::kCutscene, std::string(9, '\0'));  // (over)
        if (ev) ev_steps++;
        // a cutscene: to the guests, who play it where they are in the same stage and room
        if (uint32_t kind = 0; ev && (int16_t)ld16(kPlay + kEvId) != ev_seen) {
            const std::string name = event_now(c, &kind);
            if (!name.empty()) {
                ev_seen = (int16_t)ld16(kPlay + kEvId);
                mods::trace("coop: the host plays event %s (kind %u)", name.c_str(), kind);
                if (kind == 3 || !generic(name)) {
                    ev_cut = true;
                    std::string b(8, '\0');
                    memcpy(&b[0], mem::ptr(kStage), 8);
                    b += (char)ld8(link + kRoom), b += name;
                    net::send(-1, net::kCutscene, b);
                    mods::trace("coop: the host's cutscene %s: to the guests", name.c_str());
                }
            }
        }
        if (settings().stay_together && !ev && ev_was && ev_steps >= 90 && (bits() != ev_bits || stage_now() != ev_stage)) {
            const std::string here = stage_now();
            const int8_t my_room = (int8_t)ld8(link + kRoom);
            for (int id = 1; id < net::kMaxPlayers; id++) {
                if (!net::member(id) || std::find(g_send_join.begin(), g_send_join.end(), id) != g_send_join.end()) continue;
                const Remote& r = g_remote[id];
                const Pose* p = r.poses.empty() ? nullptr : &r.poses.back().second;
                const bool near = p && !strncmp(p->stage, here.c_str(), 8) && p->room == my_room && !(p->flags & 1) &&
                                  std::hypot(p->pos[0] - at[0], p->pos[2] - at[2]) < 1500;
                if (near) continue;
                g_send_join.push_back(id);
                mods::trace("coop: a story moment (%u steps) over: player %d brought here", ev_steps, id);
            }
        }
        ev_was = ev;
    }
    // "Go to" a player: beside them (100 to their left), facing as they do, by a warp; one on a ship's deck is in her
    // frame, not taken to
    // (test aid: WWHD_COOP_GOTO=<player>@<seconds>: the menu's button, that long after joining)
    if (static const char* e = getenv("WWHD_COOP_GOTO"); e && arrived()) {
        static uint32_t since = net::now_ms();
        static bool done = false;
        int who = -1; float secs = 0;
        if (!done && sscanf(e, "%d@%f", &who, &secs) == 2 && now_ms_since(since) >= (uint32_t)(secs * 1000)) done = true, g_go_to = who;
    }
    if (const int id = g_go_to.exchange(-1); id >= 0 && id < net::kMaxPlayers && id != net::my_id()) {
        const Remote& r = g_remote[id];
        std::string why;
        if (!net::connected(id) || r.poses.empty()) why = "not playing yet";
        else if (r.poses.back().second.flags & 1) why = "on a ship's deck";
        else {
            const Pose& p = r.poses.back().second;
            const double t = p.yaw * M_PI / 32768;
            const float at[3] = {p.pos[0] + 100 * (float)cos(t), p.pos[1], p.pos[2] - 100 * (float)sin(t)};
            ss::Warp w;
            w.stage = std::string(p.stage, strnlen(p.stage, 8)), w.room = p.room, w.angle = (uint16_t)p.yaw, w.at_position = true;
            memcpy(w.pos, at, sizeof w.pos);
            ss::request_warp(w, [id](bool ok, const std::string& no) {
                if (!ok) ss::notice("Can't go to " + net::name(id) + ": " + no);
            });
        }
        ss::notice(why.empty() ? "Going to " + net::name(id) : "Can't go to " + net::name(id) + ": " + why);
        mods::trace("coop: go to player %d: %s", id, why.empty() ? "warped" : why.c_str());
    }
    // the host's cutscene, started here where this game is in its stage and room (for 3 s, until it plays)
    if (g_cut.since && guest() && arrived()) {
        const std::string here = event_now(c);
        if (here == g_cut.name) {
            mods::trace("coop: the host's cutscene %s plays here", here.c_str());
            g_cut_on = here, g_cut = Cut{};
        } else if (now_ms_since(g_cut.since) > 3000) {
            mods::trace("coop: the host's cutscene %s: not here (%s)", g_cut.name.c_str(), g_cut.asked ? "it did not start" : "elsewhere or busy");
            g_cut = Cut{};
        } else if (here.empty() && !ld8(kPlay + kEvMode) && !strncmp(g_cut.stage, (const char*)mem::ptr(kStage), 8) &&
                   (g_cut.room == (int8_t)ld8(link + kRoom) || (host_near(link) && room_loaded(g_cut.room)))) {
            static const uint32_t name = mem::host_alloc(40);
            for (size_t i = 0; i <= g_cut.name.size(); i++) st8(name + i, i < g_cut.name.size() ? (uint8_t)g_cut.name[i] : 0);
            guest_call(c, GC(0x025D77DC), {link, name, 1, 0xFFFF});
            g_cut.asked = true;
        }
    }
    if (!g_cut_on.empty() && event_now(c) != g_cut_on) g_cut_on.clear();  // (over here too)
    if (!g_cut_on.empty() && g_cut_over && ld8(kPlay + 0x5BB2)) g_cut_over = net::now_ms();  // (dComIfGp_getMesgStatus)
    if (!g_cut_on.empty() && g_cut_over && now_ms_since(g_cut_over) > 10000) {
        mods::trace("coop: the host's cutscene %s is over there, not here: back beside the host", g_cut_on.c_str());
        net::send(0, net::kDown, std::string());
        g_cut_on.clear(), g_cut_over = 0;
    }
    // doors: the guest's held stage change, made once let in (or the host has gone in); refused or unanswered, back
    // beside the host
    if (const Remote& h = g_remote[0]; g_door.held && !h.poses.empty() && !memcmp(h.poses.back().second.stage, g_door.stage, 8))
        g_door.go = true;
    if (g_door.held && !g_door.go && now_ms_since(g_door.since) >= kDoorWait) {
        g_door.held = false, g_door_refused = net::now_ms();
        memcpy(g_door_refused_stage, g_door.stage, 8);
        net::send(0, net::kDown, std::string());
        ss::notice("Not let in: back beside " + net::name(0));
        mods::trace("coop: not let into %.8s: back beside the host", g_door.stage);
    }
    if (g_door.held && g_door.go) {
        const Door d = g_door;
        g_door = Door{};
        double keep = c->f[1].ps0;
        c->f[1].ps0 = d.f1;
        g_door.go = true;  // (the hook lets it through)
        guest_call(c, GC(0x0252012C), {d.r[0], d.r[1], d.r[2], d.r[3], d.r[4], d.r[5], d.r[6]});
        g_door.go = false, c->f[1].ps0 = keep;
        mods::trace("coop: let into %.8s", d.stage);
    }
    // the host: the first question on its panel while none is open, the answer sent
    if (host()) {
        int said;
        uint32_t said_no;
        { std::lock_guard<std::mutex> l(g_mu); said = g_ask_said, said_no = g_ask_said_no, g_ask_said = -1; }
        if (said >= 0 && !g_asks.empty() && g_asks.front().no == said_no) {
            const Ask& a = g_asks.front();
            net::send(a.from, net::kDoorAnswer, a.stage + (char)said);
            mods::trace("coop: player %d %s into %.8s", a.from, said ? "let" : "not let", a.stage.c_str());
            g_asks.pop_front();
        }
        // no longer asked: the guest gone, the host there itself, or the guest's 30 s over (it is on its way back)
        while (!g_asks.empty() && (!net::connected(g_asks.front().from) || !strncmp(g_asks.front().stage.c_str(), stage_now().c_str(), 8) ||
                                   now_ms_since(g_asks.front().at) > kDoorWait)) {
            g_asks.pop_front();
            std::lock_guard<std::mutex> l(g_mu);
            g_ask_text.clear();
        }
        if (!g_asks.empty() && !overlay::captures()) {
            const Ask& a = g_asks.front();
            std::lock_guard<std::mutex> l(g_mu);
            g_ask_text = net::name(a.from) + " wants to go into " + ss::area_name(a.stage.c_str()), g_ask_no = a.no;
            overlay::show_panel(door_draw);
        }
    }
    // falling (Asad's rule: a guest comes back beside the host; the host falling is a Game Over for everyone): a guest's
    // Link with no health for 45 steps (a fairy in a bottle revives sooner) tells the host, which sends its world again;
    // the host's own fall: once it plays again (a new Link after the Game Over), every guest is brought to it
    if (settings().stay_together) {
        const uint16_t life = ld16(ld32(GD(0x101F84DC)) + 0x20 + 2);  // dSv_player_status_a_c::mLife (quarter hearts)
        static uint32_t down_steps = 0, down_link = 0;
        down_steps = life ? 0 : down_steps + 1;
        if (guest() && arrived() && down_steps == 45) {
            net::send(0, net::kDown, std::string());
            ss::notice("You fell: back beside " + net::name(0));
        }
        if (host() && down_steps == 45) down_link = link;
        if (host() && down_link && link != down_link && life) {
            down_link = 0;
            for (int id = 1; id < net::kMaxPlayers; id++)
                if (net::member(id) && std::find(g_send_join.begin(), g_send_join.end(), id) == g_send_join.end())
                    g_send_join.push_back(id);
            mods::trace("coop: the host plays again after falling: every guest brought here");
        }
    }
    // the host: to each guest that has just joined, the team state (Anchor's way: the shared progress, union-merged into
    // the guest's own Quest Log, the guest staying where it is); with "stay together" (the fork's way) its world
    if (!settings().stay_together && host()) {
        for (const int id : g_send_join) {
            const std::vector<std::string> team = progress::team_state();
            size_t bytes = 0;
            for (const std::string& m : team) if (!m.empty()) net::send(id, net::kProgress, m), bytes += m.size();
            mods::trace("coop: the team state sent to player %d (%zu messages, %zu bytes)", id, team.size(), bytes);
        }
        g_send_join.clear();
    }
    for (size_t i = 0; i < g_send_join.size();) {
        std::string text, why;
        if (!ss::capture_portable_text(c, text, why)) {
            if (static uint32_t told = 0; told != g_send_join[i]) told = g_send_join[i], mods::trace("coop: the world waits to be sent: %s", why.c_str());
            break;
        }
        const uint32_t total = (uint32_t)text.size();
        for (uint32_t at = 0; at < total; at += kChunk) {
            std::string b(8, '\0');
            memcpy(&b[0], &total, 4), memcpy(&b[4], &at, 4);
            b += text.substr(at, kChunk);
            net::send(g_send_join[i], net::kJoinState, b);
        }
        mods::trace("coop: the world sent to player %d (%u bytes)", g_send_join[i], total);
        g_send_join.erase(g_send_join.begin() + i);
    }
    if (static uint32_t traced = 0; now_ms_since(traced) >= 5000) {  // how the link goes (the trace)
        traced = net::now_ms();
        for (int id = 0; id < net::kMaxPlayers; id++)
            if (Remote& r = g_remote[id]; id != net::my_id() && r.gaps) {
                mods::trace("coop: player %d: ping %d ms, a pose every %u ms (longest gap %u), %zu bytes, shown %d ms behind, %u sounds, %u voice frames", id,
                            net::ping_ms(id), r.gap_sum / r.gaps, r.gap_max, r.size, kBehind, r.sounds, r.voice);
                r.gaps = r.gap_sum = r.gap_max = r.sounds = r.voice = 0;
            }
    }
    // the others, each where its pose was taken (in the same stage and room; on the ship's deck, wherever she is here)
    const std::string stage = stage_now();
    const int8_t room = (int8_t)ld8(link + kRoom);
    const uint32_t now = net::now_ms();
    for (int id = 0; id < net::kMaxPlayers; id++) {
        if (id == net::my_id()) continue;
        const int slot = id < net::my_id() ? id : id - 1;
        Remote& r = g_remote[id];
        Pose p;
        const bool was = r.shown;
        r.shown = net::connected(id) && now - r.heard < (uint32_t)kStale && pose_at(r, r.clock.shown(now), p) && !strncmp(p.stage, stage.c_str(), 8) &&
                  ((p.flags & 1) ? hull != 0 : p.room == room);
        if (!r.shown) {
            if (was) show(slot, nullptr, nullptr, 0);  // (only the ones it showed: the ghost test aid has slot 0 to itself)
            continue;
        }
        int16_t yaw = p.yaw;
        if (p.flags & 1) to_world(hull, p.pos, r.at), yaw = (int16_t)(yaw + (int16_t)ld16(hull + kAngleY));
        else memcpy(r.at, p.pos, 12);
        r.yaw = yaw;
        show(slot, &p, r.at, yaw, net::tunic(id));
    }
    // their sounds, at their puppets as those are shown (each player's place kept for the sound engine, which keeps reading
    // it while the sound plays)
    static const uint32_t sound_at = mem::host_alloc(16 * net::kMaxPlayers);
    for (size_t i = 0; i < g_sounds_in.size();) {
        const Effects& e = g_sounds_in[i];
        if (now - e.at < (uint32_t)kBehind) { i++; continue; }
        if (e.from >= 0 && e.from < net::kMaxPlayers && g_remote[e.from].shown)
            for (size_t o = 0; o + sizeof(Sound) <= e.body.size(); o += sizeof(Sound)) {
                Sound sd;
                memcpy(&sd, e.body.data() + o, sizeof sd);
                const uint32_t pos = sound_at + 16 * e.from;
                for (int k = 0; k < 3; k++) st32(pos + 4 * k, std::bit_cast<uint32_t>(g_remote[e.from].at[k] + sd.off[k]));
                double keep[4];
                for (int k = 0; k < 4; k++) keep[k] = c->f[k + 1].ps0, c->f[k + 1].ps0 = sd.f[k];
                const uint32_t id = char_voice(sd.id, g_remote[e.from].poses.empty() ? 0 : g_remote[e.from].poses.back().second.flags >> 4 & 3);
                if (id) guest_call(c, GC(0x0201EBA0), {ld32(GD(0x101FFC78)), id, pos, sd.r6, sd.r7, sd.r8});  // (JAIZelBasic)
                g_remote[e.from].sounds++;
                for (int k = 0; k < 4; k++) c->f[k + 1].ps0 = keep[k];
            }
        g_sounds_in.erase(g_sounds_in.begin() + i);
    }
    // their particles, as their puppets are where they were made (dropped for a player not shown here)
    for (size_t i = 0; i < g_effects_in.size();) {
        const Effects& e = g_effects_in[i];
        if (now - e.at < (uint32_t)kBehind) { i++; continue; }
        if (e.from >= 0 && e.from < net::kMaxPlayers && g_remote[e.from].shown)
            for (size_t o = 0; o + sizeof(Effect) <= e.body.size(); o += sizeof(Effect)) {
                Effect fx;
                memcpy(&fx, e.body.data() + o, sizeof fx);
                effect_at(c, fx, g_remote[e.from].at, g_remote[e.from].yaw);
            }
        g_effects_in.erase(g_effects_in.begin() + i);
    }
}

}  // namespace mods::coop::sync

// dComIfG_resetToOpening (025202F8, this scene): every scene's execute asks it each step whether the reset button
// sends the game back to the opening; a guest's way back to the title is taken here, as that reset is:
// changeOpeningScene(this scene, the opening), the music faded and the sound stopped (asked for from the mods' step,
// in the middle of the actors' executes, the scene change request crashed)
extern "C" void f_025202F8_orig(Cpu* c);
extern "C" void hook_025202F8(Cpu* c) {
    if (!mods::coop::sync::g_to_title) return f_025202F8_orig(c);
    mods::coop::sync::g_to_title = false;
    const uint32_t scene = c->r[3];
    guest_call(c, GC(0x02520230), {scene, 8});
    guest_call(c, GC(0x025E1904), {0x1E});
    guest_call(c, GC(0x025E1D40));
    mods::trace("coop: the opening asked for (scene proc name %d)", (int16_t)ld16(scene + 8));
    c->r[3] = 1;
}

// dComIfGp_setNextStage (0252012C): a guest's door into a stage the host isn't in waits for the host's answer (doors)
extern "C" void f_0252012C_orig(Cpu* c);
extern "C" void hook_0252012C(Cpu* c) {
    if (!mods::coop::sync::door_held(c)) f_0252012C_orig(c);
}
