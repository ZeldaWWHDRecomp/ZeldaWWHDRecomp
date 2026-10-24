// D-sweep (off by default; setup's opt-in step "Preparing shaders: models (experimental)" runs it hidden and muted,
// tools/installer/model_sweep.py): prepare the pipelines of the game's models by drawing every model of every
// object archive once with the game's own code, instead of playing. The renderer records each pipeline with the archive whose models used it
// (pipeline_groups.h); in play the Vulkan renderer compiles them when that archive loads.
//
// WWHD_DSWEEP=<start step>[,<models per batch>[,<frames per batch>]] turns it on; WWHD_DSWEEP=title[,<per>[,<frames>]]
// hosts it from the title screen (no save needed): it presses A on the boot's notice screen until the title is up,
// and in the title's idle story intro (where no actor draws and the sweep pauses) to return to the title.
// It only runs in a settled stage (the same stage for 120 steps; never the boot's or the file select's "Name"). From
// the given logic step on, the first actor draw (fopAc_Draw) of every logic step also draws a batch of models at the
// camera's look-at point:
//  1. the models of the archives loaded already (object archives through the archive resource table:
//     dRes_info_c::loadResourceTable entries whose loader is loadModel or loadMaterialModel);
//  2. with WWHD_DSWEEP_ARCHIVES=<file> (one archive name per line), every listed object archive in turn: loaded
//     with the game's own loader (dComIfG_resLoad, as actors load theirs), swept, then released (dComIfG_resDelete).
// Each model: mDoExt_J3DModel__create on a solid heap of its batch (freed some batches later, and all before its
// archive is released), dKy_tevstr_init + settingTevStruct + setLightTevColorType, mDoExt_modelUpdateDL into the
// frame's draw lists. The game's layers draw the lists in every pass (shadow, pre-pass, main), so the renderer
// creates the pipelines those models need, as in play. Joint callbacks, unusable joint matrix calculators and the
// material animation slots of the shared model data belong to the owning actors; they are off while the sweep's
// instance is calculated.
//
//  3. with WWHD_DSWEEP_STAGES=<file> ("<stage> <archive>..." per line), every stage resource (Stage, Room<n>, LOD<n>):
//     requested on the game's resource load task as dComIfG_setStageRes does, but with that stage's name, swept,
//     then released. Room models (model.bmd, model1..3.bmd) are made and drawn as daBg does (create flags with or
//     without a texture SRT animation, the BG light types, the BG lists; model 1 on the invisible-model list), at
//     the look-at point instead of the room's place on the sea's grid (dComIfGp_getMapTrans): no clipping, so
//     every shape enters the lists. Their pipelines are filed under "<stage>_<archive>", the name the resident
//     stage resources are published with in play (publish_resident), so they are compiled when the room loads.
// Setup's progress: "WWHD-PROGRESS <archives done> <listed> <archive>" on stdout (object and stage archives).
// Crash handling and resuming (WWHD_DSWEEP_STATE=<dir>): progress.txt gets "model <archive>:<name>" before a model is
// first drawn, "load <archive>" before an archive is loaded and "archive-done <archive>" after it is released;
// skip.txt holds archive or archive:model names to leave out. A supervisor that finds the process gone adds the
// last model (or archive) of progress.txt to skip.txt and starts again; archives done already are not loaded again.
// Only names are written, no game data.
//
// Debug: WWHD_DSWEEP_SKIP=a:b,... (more names to skip), WWHD_DSWEEP_FILTER=text (only matching models),
// WWHD_DSWEEP_ONLY=<step>[:<profile>...] (no actor draws itself from that step on, except the listed profiles, so a
// run's pipelines belong to the sweep), WWHD_DSWEEP_TIME=step:degrees,... (time of day for reference runs),
// WWHD_DSWEEP_PROFILES=1 (logs each actor profile drawing in a stage, with its method addresses), WWHD_DSWEEP_ROOM
// (one more instance of a room model, made and drawn as daBg does; see room_test).
// With WWHD_VK_KEY_LOG the Vulkan renderer writes the batch number next to every pipeline it creates.
#include "dsweep.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "interp.h"
#include "runtime.h"
#include "gfx/renderer.h"
#include "gx2/gx2_cmd.h"
#include "guest_addr.h"
#include "mods/guest_heap_query.h"
#include "pipeline_groups.h"


namespace dsweep {
extern std::atomic<bool> g_finished;
namespace {
std::atomic<int> g_tag{0};
std::atomic<bool> g_title_seen{false};
std::atomic<int64_t> g_last_draw_ns{0};  // the last actor draw (a title sweep: the title's idle intro draws none)
std::atomic<uint64_t> g_last_draw_step{0};  // ... its logic step

struct Config {
    bool on = false;
    bool title = false;  // WWHD_DSWEEP=title[,...]: hosted from the title screen (setup: no save needed)
    uint64_t start = 0;
    unsigned per = 1, hold = 2;
};
const Config& config() {
    static const Config value = [] {
        Config c;
        const char* e = std::getenv("WWHD_DSWEEP");
        if (!e || !*e) return c;
        unsigned long v[3] = {0, 1, 2};
        char* p = const_cast<char*>(e);
        if (!std::strncmp(p, "title", 5)) {
            c.title = true;
            p += 5;
            if (*p == ',') ++p;
            for (int i = 1; i < 3 && *p; ++i) {
                v[i] = std::strtoul(p, &p, 10);
                if (*p == ',') ++p;
            }
            c.on = true;
            c.per = v[1] ? unsigned(v[1]) : 1;
            c.hold = v[2] ? unsigned(v[2]) : 1;
            return c;
        }
        for (int i = 0; i < 3 && *p; ++i) {
            v[i] = std::strtoul(p, &p, 10);
            if (*p == ',') ++p;
        }
        c.on = true;
        c.start = v[0];
        c.per = v[1] ? unsigned(v[1]) : 1;
        c.hold = v[2] ? unsigned(v[2]) : 1;
        return c;
    }();
    return value;
}

struct Model {
    std::string archive, name;
    uint32_t data = 0, model = 0, heap = 0;
    bool failed = false;
    int room = -1;           // a room model (model.bmd, model1..3.bmd of a stage's Room<n>): daBg's index 0..3
    int roomNo = 0;          // the room number (Room<n>)
    uint32_t flags = 0x11000022;  // create flags (daBg: | 0x1200 with a texture SRT animation)
};
// An archive to sweep: an object archive (stage empty) or a stage resource (<stage>_<arc>: Stage, Room<n>, LOD..)
struct Entry {
    std::string stage, arc;
    size_t order = 0;  // its place in the archive and stage lists (several processes: the order of the whole work)
    bool deferred = false;  // an object archive the game itself held when its turn came: tried again at the end
    std::string id() const { return stage.empty() ? arc : stage + "_" + arc; }
};
// WWHD_DSWEEP_PART=<i>/<n>: this process does part i (0..n-1) of the work, n processes at once (setup's models step on
// desktops): every n-th archive or stage resource, every n-th actor, every n-th visit; the title's own models, the
// environment and the particles only in part 0. Placements and places read from the rooms are kept with their
// order (actors-indexed.txt, places-indexed.txt), all of them, so the supervisor merges the parts' lists into the
// lists one process would have made (model_sweep.py), before the actors and the visits.
struct Part { unsigned index = 0, count = 1; };
const Part& part() {
    static const Part value = [] {
        Part p;
        if (const char* e = std::getenv("WWHD_DSWEEP_PART"); e && *e) {
            char* end = nullptr;
            const unsigned i = unsigned(std::strtoul(e, &end, 10));
            const unsigned n = end && *end == '/' ? unsigned(std::strtoul(end + 1, nullptr, 10)) : 0;
            if (n > 1 && i < n) p.index = i, p.count = n;
        }
        return p;
    }();
    return value;
}
bool mine(size_t j) { return part().count <= 1 || j % part().count == part().index; }

enum class Phase { Start, Resident, NextArchive, Loading, StageLoading, Sweeping, Draining, Environment, Particles, Actors,
                   Plants,
                   FileSelect, Visits, Done };
constexpr int kHeaps = 6;   // solid heaps alive at once: a batch's models live until kHeaps batches later
constexpr int kDrain = 6;   // frames between an archive's last draw and its release (frames in flight)
constexpr int kLoadFrames = 900;
constexpr unsigned kRelease = 4;  // frames between a stage resource's release and the next request
std::string g_released;           // the stage resource released last: the next request waits until it is gone

struct State {
    Phase phase = Phase::Start;
    uint32_t scratch = 0, parent = 0, heap_size = 0, heap = 0;
    uint32_t heaps[kHeaps] = {};
    std::vector<Model> models;       // the work list of the current phase
    size_t cursor = 0;               // first model of the current batch
    unsigned frames = 0;             // frames the current batch (or load, or drain) has had
    int batch = 0;                   // batches so far (the key log's tag)
    std::vector<Entry> queue;        // archives still to load (object archives first, then stage resources)
    std::string archive;             // the archive being loaded or swept (its id)
    Entry entry;                     // ... as queued
    bool requested = false;          // a stage resource: its load is requested
    bool owned = false;              // ... loaded by the sweep (released after it)
    std::set<std::string> resident, skip, done;
    unsigned swept = 0, failed = 0, archives = 0, load_failed = 0;
    size_t listed = 0;               // archives in WWHD_DSWEEP_ARCHIVES and WWHD_DSWEEP_STAGES (progress total)
    int64_t started_ns = 0;
    std::string state_dir;
    // actors (WWHD_DSWEEP_ACTORS=1)
    struct Spawn {
        std::string name;
        uint16_t proc = 0;
        int8_t arg = 0;
        uint32_t params = 0xFFFFFFFF;
        int16_t ax = 0, az = 0;
        std::string id() const;
    };
    std::vector<Spawn> spawns;       // still to create (popped from the back)
    std::set<std::string> placed;    // placements read from the rooms (name params angles), deduplicated
    Spawn spawn;                     // the actor being shown
    uint32_t actor_id = 0xFFFFFFFF, actor = 0;
    unsigned actor_frames = 0, actor_draws = 0, actors = 0, actor_failed = 0;
    std::vector<uint32_t> orphans;   // actors still being created when given up: deleted once they exist
    std::set<uint32_t> host_actors;  // the title's own actors (drawn before the actor sweep; never deleted)
    std::map<uint32_t, uint32_t> ours;  // process id -> actor: the shown actor and what it made (drawn, not the title's)
    std::vector<std::pair<uint32_t, uint32_t>> frozen;  // actor, status bits added for one execute pass
    std::set<std::string> archives_before;
} g;
std::atomic<bool> g_abort{false};  // an actor changed the stage: orderly exit, the supervisor skips it
std::atomic<bool> g_file_select{false}, g_in_file_select{false};  // the last part: the title's file select
bool g_file_select_seen = false;  // ... done in an earlier attempt
bool g_particles_seen = false;     // the particle sweep, done in an earlier attempt

std::string State::Spawn::id() const {
    char b[64];
    std::snprintf(b, sizeof b, "%s:%08X:%d:%d", name.c_str(), params, ax, az);
    return b;
}

bool mem2(uint32_t a, uint32_t size = 4) { return a >= mem::kMem2Start && a <= mem::kMem2End - size; }
uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {}) {
    Cpu k = *c;
    return guest_call(&k, fn, args);
}
uint32_t call_fn(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) { return call(c, fn, args); }
std::string cstring(uint32_t a, unsigned max = 64) {
    std::string s;
    if (!mem2(a, 1)) return s;
    for (unsigned i = 0; i < max; ++i) {
        const uint8_t ch = ld8(a + i);
        if (!ch) return s;
        if (ch < 0x20 || ch > 0x7E) return {};
        s.push_back(char(ch));
    }
    return {};
}
// the current stage (dComIfG_gameInfo.play dStage_startStage_c name, 8 chars), as savestate.cpp reads it
std::string stage_name() {
    const uint32_t at = GD(0x1046F0B0) + 0x5134;
    std::string s;
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t ch = ld8(at + i);
        if (!ch) break;
        if (ch < 0x20 || ch > 0x7E) return {};
        s.push_back(char(ch));
    }
    return s;
}
int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// setup's progress line (tools/installer/shader_prebuild.py parse_progress): archives done of the list
void report_archives(const std::string& detail) {
    if (!g.listed) return;
    std::printf("WWHD-PROGRESS %zu %zu %s\n", std::min(g.done.size(), g.listed), g.listed, detail.c_str());
    std::fflush(stdout);
}
#ifdef WWHD_HAS_VULKAN
}  // namespace
}  // namespace dsweep
namespace gfxvk { int64_t pipeline_recipes_durable_ns(); }
namespace dsweep {
namespace {
#endif
// Archives (and environment states) are marked done ("...-done" lines) only once the pipelines they recorded are
// on disk: Vulkan saves its recipes in snapshots (pipeline_recipes_durable_ns); Metal appends each one at once. A
// sweep that stops the game redoes what was not saved yet instead of losing it.
std::vector<std::pair<int64_t, std::string>> g_pending_done;
void write_progress(const std::string& line);
void flush_done(bool all) {
#ifdef WWHD_HAS_VULKAN
    const int64_t durable = render::vulkan() ? gfxvk::pipeline_recipes_durable_ns() : INT64_MAX;
#else
    const int64_t durable = INT64_MAX;
#endif
    size_t n = 0;
    // (half a second of margin: the render thread records a frame's pipelines a little after the game drew it)
    while (n < g_pending_done.size() && (all || (durable != INT64_MAX ? g_pending_done[n].first + 500'000'000 < durable : true)))
        write_progress(g_pending_done[n++].second);
    g_pending_done.erase(g_pending_done.begin(), g_pending_done.begin() + long(n));
}
void progress(const std::string& line) {
    if (g.state_dir.empty()) return;
    // (actors: at once. An actor can leave something behind that stops the game a little later; redone after a
    // restart it would stop it again and an innocent actor after it would be skipped each time)
    if (line.find("-done ") != std::string::npos && line.find("-done ") < 12 && line.rfind("actor-done ", 0) != 0) {
        g_pending_done.emplace_back(now_ns(), line);
        return flush_done(false);
    }
    write_progress(line);
}
// "phase <name> <seconds since this attempt started>" in progress.txt and the log, for setup's timing
void phase_time(const char* name) {
    char line[96];
    std::snprintf(line, sizeof line, "phase %s %.1f", name, double(now_ns() - g.started_ns) / 1e9);
    LOG("[dsweep] %s", line);
    write_progress(line);
}
void write_progress(const std::string& line) {
    if (g.state_dir.empty()) return;
    if (FILE* f = std::fopen((g.state_dir + "/progress.txt").c_str(), "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fflush(f);
        std::fclose(f);
    }
}
std::vector<std::string> lines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back(line);
    }
    return out;
}
bool skipped(const std::string& archive, const std::string& name) {
    return g.skip.count(archive) || g.skip.count(archive + ":" + name);
}

// dRes_info_c of an object archive (HD layout, d_resorce.h ResourceArchiveInfoHD): name +0x1C, archive +0x5C,
// mRes +0x60, count +0x84. The resource control (*101F4F28) keeps them in a pool of 128 nodes (+0x28, 0x20 each,
// the info at node+0x18).
std::vector<uint32_t> archive_infos() {
    std::vector<uint32_t> out;
    const uint32_t control = ld32(GD(0x101F4F28));
    for (unsigned i = 0; i < 128 && mem2(control); ++i) {
        const uint32_t info = ld32(control + 0x28 + i * 0x20 + 0x18);
        if (!mem2(info, 0x8C) || std::find(out.begin(), out.end(), info) != out.end()) continue;
        const uint32_t archive = ld32(info + 0x5C), res = ld32(info + 0x60);
        const int32_t count = int32_t(ld32(info + 0x84));
        if (cstring(info + 0x1C).empty() || !mem2(archive, 0x24) || count < 0 || count > 8192 || (count && !mem2(res, count * 4)))
            continue;
        out.push_back(info);
    }
    return out;
}
// Stage resources (dComIfG_setStageRes): ResourcePathInfoHD in the resource control's path pool (+0x1028, a
// red-black tree: root +0, node left +0, right +4, the info at +0x18). Info: the stage (FixedSafeString, its text
// at *(+0x10)), the archive name (*(+0x5C)), the archive (+0xA8) whose header (+0x14) lists the models (+0x20: a
// relative offset to 16-byte dictionary entries, count +0x50).
void path_infos(uint32_t node, std::vector<uint32_t>& out, int depth = 0) {
    if (!mem2(node, 0x20) || depth > 40 || out.size() > 512) return;
    const uint32_t info = ld32(node + 0x18);
    if (mem2(info, 0xC8)) out.push_back(info);
    path_infos(ld32(node), out, depth + 1);
    path_infos(ld32(node + 4), out, depth + 1);
}
std::vector<uint32_t> path_infos() {
    std::vector<uint32_t> out;
    const uint32_t control = ld32(GD(0x101F4F28));
    if (mem2(control, 0x1040)) path_infos(ld32(control + 0x1028), out);
    return out;
}
std::string path_id(uint32_t info) {
    const std::string stage = cstring(ld32(info + 0x10)), arc = cstring(ld32(info + 0x5C));
    return arc.empty() || stage.empty() ? std::string() : stage + "_" + arc;
}
uint32_t find_path_info(const std::string& id) {
    for (uint32_t info : path_infos())
        if (path_id(info) == id) return info;
    return 0;
}
// a relative offset field (the archive header's self-relative pointers)
uint32_t rel(uint32_t at) {
    const uint32_t v = mem2(at) ? ld32(at) : 0;
    return v ? at + v : 0;
}
// SafeStrings for the stage resource calls in the sweep's scratch memory: the stage +0x800, the archive +0x840, a
// resource name +0x880 (64 bytes each); SafeString {text, vtable} objects at +0x8C0 (stage), +0x8C8 (archive),
// +0x8D0 (resource)
uint32_t safe_strings(const std::string& stage, const std::string& arc, const std::string& res = {}) {
    const uint32_t s = g.scratch;
    std::memset(mem::ptr(s + 0x800), 0, 0xE0);
    std::memcpy(mem::ptr(s + 0x800), stage.c_str(), std::min<size_t>(stage.size(), 63));
    std::memcpy(mem::ptr(s + 0x840), arc.c_str(), std::min<size_t>(arc.size(), 63));
    std::memcpy(mem::ptr(s + 0x880), res.c_str(), std::min<size_t>(res.size(), 63));
    for (uint32_t i = 0; i < 3; ++i) {
        st32(s + 0x8C0 + i * 8, s + 0x800 + i * 0x40);
        st32(s + 0x8C4 + i * 8, GD(0x1004B8C0));  // SafeString vtable
    }
    return s + 0x8C0;
}
// dComIfG_getStageRes for any stage: ResourceControlHD::getPathRes(archive, path, resource)
uint32_t stage_res(Cpu* c, const std::string& stage, const std::string& arc, const std::string& res) {
    const uint32_t ss = safe_strings(stage, arc, res);
    return call(c, GC(0x02606BEC), {ld32(GD(0x101F4F28)), ss + 8, ss, ss + 16});
}

// The models of a stage resource: each model of the archive's header ("<name>.bmd"). Room models (model.bmd,
// model1..3.bmd of Room<n>) are made and drawn as daBg does.
void add_stage_models(Cpu* c, uint32_t info, const Entry& e) {
    const uint32_t archive = ld32(info + 0xA8);
    const uint32_t header = mem2(archive, 0x18) ? ld32(archive + 0x14) : 0;
    if (!mem2(header, 0x68)) return;
    const uint32_t count = std::min<uint32_t>(ld16(header + 0x50), 256), models = rel(header + 0x20);
    static const char* filter = std::getenv("WWHD_DSWEEP_FILTER");
    const std::string id = e.id();
    const bool room = e.arc.rfind("Room", 0) == 0;
    unsigned found = 0;
    for (uint32_t i = 0; i < count && mem2(models + i * 16, 0x28); ++i) {
        const uint32_t resource = rel(models + i * 16 + 0x24);
        const std::string name = cstring(rel(resource + 4), 128);
        if (name.empty()) continue;
        Model m;
        m.archive = id;
        m.name = name + ".bmd";
        if (skipped(id, m.name) || (filter && *filter && (id + ":" + m.name).find(filter) == std::string::npos)) {
            LOG("[dsweep] skipped %s:%s", id.c_str(), m.name.c_str());
            continue;
        }
        m.data = stage_res(c, e.stage, e.arc, m.name);
        if (!mem2(m.data)) continue;
        if (room) {
            static const char* names[4] = {"model", "model1", "model2", "model3"};
            for (int k = 0; k < 4; ++k)
                if (name == names[k]) m.room = k;
            m.roomNo = std::atoi(e.arc.c_str() + 4);
            if (m.room >= 0) {
                m.flags = 0x11000022;
                if (mem2(stage_res(c, e.stage, e.arc, std::string(names[m.room]) + ".btk"))) m.flags |= 0x1200;
            }
        }
        g.models.push_back(m);
        ++found;
    }
    LOG("[dsweep] stage archive %s: %u models", id.c_str(), found);
}

// Actor placements of a stage resource (room.dzr of a room, stage.dzs of the stage archive), for the actor sweep:
// the chunk table (count, then {tag, entries, offset}) and its actor chunks (ACTR/TRES/TGOB and their layer
// variants ACTn/TREn: 0x20 bytes; SCOB/TGSC/DOOR/TGDR and SCOn: 0x24) {name[8], parameters, position, angle x,y,z}.
// Kept as "name parameters angle-x angle-z" in the sweep's state folder (actors.txt), up to kVariants per name.
constexpr size_t kVariants = 4;
std::string name8(uint32_t at) {
    std::string out;
    for (uint32_t i = 0; i < 8 && mem2(at + i, 1); ++i) {
        const uint8_t ch = ld8(at + i);
        if (!ch) break;
        if (ch < 0x21 || ch > 0x7E) return {};
        out.push_back(char(ch));
    }
    return out;
}
std::set<std::string> g_places;  // "spawn <stage> <room> <point>", "layer <stage> <room|-1> <layer>" (places.txt)
void harvest(Cpu* c, const Entry& e) {
    static const bool on = std::getenv("WWHD_DSWEEP_ACTORS") || std::getenv("WWHD_DSWEEP_VISIT") || part().count > 1;
    if (!on) return;
    const bool indexed = part().count > 1;  // all placements, with the entry's order (merged by the supervisor)
    const std::string prefix = indexed ? std::to_string(e.order) + " " : std::string();
    const uint32_t data = stage_res(c, e.stage, e.arc, e.arc == "Stage" ? "stage.dzs" : "room.dzr");
    if (!mem2(data, 4)) return;
    const uint32_t chunks = std::min<uint32_t>(ld32(data), 64);
    std::map<std::string, size_t> perName;
    for (const auto& p : g.placed) ++perName[p.substr(0, p.find(' '))];
    std::string added, places;
    for (uint32_t i = 0; i < chunks && mem2(data + 4 + i * 12, 12); ++i) {
        const uint32_t at = data + 4 + i * 12;
        char tag[5] = {char(ld8(at)), char(ld8(at + 1)), char(ld8(at + 2)), char(ld8(at + 3)), 0};
        const std::string t = tag;
        uint32_t size = 0;
        if (t == "ACTR" || t == "TRES" || t == "TGOB" || (t.size() == 4 && (t.rfind("ACT", 0) == 0 || t.rfind("TRE", 0) == 0)))
            size = 0x20;
        else if (t == "SCOB" || t == "TGSC" || t == "DOOR" || t == "TGDR" || (t.size() == 4 && t.rfind("SCO", 0) == 0))
            size = 0x24;
        // the places to visit: each room's player start (PLYR: room in the parameters' low 6 bits, the start point
        // in angle z) and the scene layers its placements use (ACTn / SCOn / TREn, n = 0..b)
        if (t == "PLYR") {
            const uint32_t count = std::min<uint32_t>(ld32(at + 4), 256), base = data + ld32(at + 8);
            for (uint32_t k = 0; k < count && mem2(base + k * 0x20, 0x20); ++k) {
                const uint32_t a = base + k * 0x20;
                const int room = e.arc == "Stage" ? int(ld32(a + 8) & 0x3F) : std::atoi(e.arc.c_str() + 4);
                char line[64];
                std::snprintf(line, sizeof line, "spawn %s %d %d", e.stage.c_str(), room, int(ld16(a + 0x1C) & 0xFF));
                if (g_places.insert(line).second) places += prefix + line + "\n";
            }
            continue;
        }
        if (t.size() == 4 && (t.rfind("ACT", 0) == 0 || t.rfind("SCO", 0) == 0 || t.rfind("TRE", 0) == 0) &&
            std::isxdigit(uint8_t(t[3])) && ld32(at + 4)) {
            const int layer = std::stoi(t.substr(3), nullptr, 16);
            const int room = e.arc == "Stage" ? -1 : std::atoi(e.arc.c_str() + 4);
            char line[64];
            std::snprintf(line, sizeof line, "layer %s %d %d", e.stage.c_str(), room, layer);
            if (g_places.insert(line).second) places += prefix + line + "\n";
        }
        if (!size) continue;
        const uint32_t count = std::min<uint32_t>(ld32(at + 4), 1024), base = data + ld32(at + 8);
        for (uint32_t k = 0; k < count && mem2(base + k * size, size); ++k) {
            const uint32_t a = base + k * size;
            const std::string name = name8(a);
            if (name.empty() || (!indexed && perName[name] >= kVariants)) continue;
            char line[96];
            std::snprintf(line, sizeof line, "%s %08X %d %d", name.c_str(), ld32(a + 8), int16_t(ld16(a + 0x18)),
                          int16_t(ld16(a + 0x1C)));
            if (g.placed.insert(line).second) ++perName[name], added += prefix + line + "\n";
        }
    }
    if (!added.empty() && !g.state_dir.empty())
        if (FILE* f = std::fopen((g.state_dir + (indexed ? "/actors-indexed.txt" : "/actors.txt")).c_str(), "a")) {
            std::fputs(added.c_str(), f);
            std::fclose(f);
        }
    if (!places.empty() && !g.state_dir.empty())
        if (FILE* f = std::fopen((g.state_dir + (indexed ? "/places-indexed.txt" : "/places.txt")).c_str(), "a")) {
            std::fputs(places.c_str(), f);
            std::fclose(f);
        }
}

uint32_t find_info(const std::string& name) {
    for (uint32_t info : archive_infos())
        if (cstring(info + 0x1C) == name) return info;
    return 0;
}

// The models of an archive as dRes_info_c::loadResourceTable finds them: the archive's row of the resource table
// (hash of the name; rows {hash, entries, count}, entries {name, type}) and the loader of each type (ResourceMethod,
// 8 bytes per type); entry e is mRes[e].
void add_models(Cpu* c, uint32_t info) {
    constexpr uint32_t kRowCount = 629;
    const uint32_t loadModel = GC(0x02608308), loadMaterialModel = GC(0x0260A0B4);
    const uint32_t rows = GD(0x1009BB1C), methods = GD(0x100E1C40);
    const std::string name = cstring(info + 0x1C);
    const uint32_t res = ld32(info + 0x60);
    const int32_t count = int32_t(ld32(info + 0x84));
    const uint32_t hash = call(c, GC(0x0273B264), {info + 0x1C, uint32_t(name.size())});  // resourceHashString
    uint32_t entries = 0, entryCount = 0;
    for (uint32_t r = 0; r < kRowCount; ++r)
        if (ld32(rows + r * 12) == hash) {
            entries = ld32(rows + r * 12 + 4);
            entryCount = ld32(rows + r * 12 + 8);
            break;
        }
    static const char* filter = std::getenv("WWHD_DSWEEP_FILTER");
    unsigned found = 0;
    for (uint32_t e = 0; e < entryCount && int32_t(e) < count && mem2(entries, entryCount * 8); ++e) {
        const uint32_t type = ld32(entries + e * 8 + 4);
        if (type > 64) continue;
        const uint32_t m = methods + type * 8;
        const int16_t adjust = int16_t(ld16(m)), selector = int16_t(ld16(m + 2));
        uint32_t target = 0;
        if (!selector) continue;
        if (selector < 0) target = ld32(m + 4);
        else {
            const uint32_t vt = ld32(info + uint32_t(int32_t(adjust)) + uint32_t(int32_t(int16_t(ld16(m + 6)))));
            if (mem2(vt)) target = ld32(vt + uint32_t(selector) * 8 + 4);
        }
        if (target != loadModel && target != loadMaterialModel) continue;
        const uint32_t data = ld32(res + e * 4);
        if (!mem2(data)) continue;
        Model model;
        model.archive = name;
        model.name = cstring(ld32(entries + e * 8), 128);
        model.data = data;
        if (skipped(name, model.name) || (filter && *filter && (name + ":" + model.name).find(filter) == std::string::npos)) {
            LOG("[dsweep] skipped %s:%s", name.c_str(), model.name.c_str());
            continue;
        }
        g.models.push_back(model);
        ++found;
    }
    LOG("[dsweep] archive %s: %u models", name.c_str(), found);
}

// The parent heap with the most free space (archive, game, zelda, command); a batch heap is a small part of it, so
// a phone-sized heap only means smaller batches.
void choose_heaps(Cpu* c) {
    const uint32_t getters[] = {GC(0x025E3268), GC(0x025E2FBC), GC(0x025EE048), GC(0x025EE060)};
    uint32_t best = 0;
    for (uint32_t getter : getters) {
        const uint32_t heap = call(c, getter);
        const uint32_t free = mem2(heap) ? guest_heap::free_size(c, heap, call_fn) : 0;
        if (free > best) best = free, g.parent = heap;
    }
    g.heap_size = std::min<uint32_t>(best / (3 * kHeaps), 2u << 20) & ~0xFFFu;
    LOG("[dsweep] batch heaps of %u KiB from %08X (%u KiB free)", g.heap_size >> 10, g.parent, best >> 10);
}
void free_heaps(Cpu* c, int keep) {
    for (int i = 0; i < kHeaps - keep; ++i)
        if (g.heaps[i]) call(c, GC(0x025E3868), {g.heaps[i]}), g.heaps[i] = 0;  // mDoExt_destroySolidHeap
    std::rotate(std::begin(g.heaps), std::begin(g.heaps) + (kHeaps - keep), std::end(g.heaps));
}

// A room model as daBg_c::draw draws it (no clipping: every shape enters the lists): calc, the BG light type of its
// index (TEV_TYPE_BG0 + i), the BG lists (model 1: the invisible-model opaque list with the sea fog), modelEntryDL.
void draw_room_model(Cpu* c, Model& m, uint32_t host) {
    const uint32_t play = GD(0x1046F0B0);
    const uint32_t tevstr = g.scratch;
    call(c, GC(0x0255FFF4), {tevstr, uint32_t(int32_t(int8_t(m.roomNo))), 0xFF});  // dKy_tevstr_init
    call(c, GC(0x027F4D5C), {m.model});                                         // J3DModel calc
    const uint32_t env = call(c, GC(0x02555D0C));                                 // dKy_getEnvlight
    call(c, GC(0x025626A4), {env, uint32_t(1 + m.room), 0, tevstr});  // settingTevStruct(TEV_TYPE_BG0 + i)
    call(c, GC(0x02562F5C), {env, m.model, tevstr});                  // setLightTevColorType
    const uint32_t opa = ld32(GD(0x104B4634)), xlu = ld32(GD(0x104B4638));
    st32(GD(0x104B4634), ld32(play + (m.room == 1 ? 0x5DA0 : 0x5D70)));  // BG lists (model 1: invisible opaque)
    st32(GD(0x104B4638), ld32(play + 0x5D74));
    if (m.room == 1) call(c, GC(0x0255F8A0));  // the invisible model's fog (GxFog_sea_set)
    call(c, GC(0x025E2E5C), {m.model});         // mDoExt_modelEntryDL
    if (m.room == 1) call(c, GC(0x0255F84C));  // GxFog_set
    st32(GD(0x104B4634), opa);
    st32(GD(0x104B4638), xlu);
    (void)host;
}

void draw_model(Cpu* c, Model& m, uint32_t host) {
    if (m.failed || !g.heap || !g.scratch) return;
    if (m.model && std::find(std::begin(g.heaps), std::end(g.heaps), m.heap) == std::end(g.heaps)) m.model = 0;
    if (!m.model) {
        progress("model " + m.archive + ":" + m.name);
        const uint32_t previous = call(c, GC(0x025E3570), {g.heap});  // mDoExt_setCurrentHeap
        // mDoExt_J3DModel__create: as actors do, room models as daBg does
        m.model = m.room >= 0 ? call(c, GC(0x025E38E0), {m.data, 0u, m.flags})
                              : call(c, GC(0x025E38E0), {m.data, 0x80000u, 0x11000022u});
        m.heap = g.heap;
        call(c, GC(0x025E3570), {previous});
        if (!m.model) {
            m.failed = true;
            ++g.failed;
            LOG("[dsweep] create failed %s:%s (batch heap %u KiB)", m.archive.c_str(), m.name.c_str(), g.heap_size >> 10);
            return;
        }
        ++g.swept;
    }
    // at the camera's look-at point (dCamera_c center +0x10), so the model is in view in any scene
    const uint32_t camera = call(c, GC(0x024F8044));
    const uint32_t at = mem2(camera, 0x20) ? camera + 0x10 : host + 0x314;
    const float x = float(ldf32(at)), y = float(ldf32(at + 4)), z = float(ldf32(at + 8));
    const float matrix[12] = {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z};
    for (int i = 0; i < 3; ++i) st32(m.model + 0xBC + i * 4, f32_as_u32(1.f));
    for (int i = 0; i < 12; ++i) st32(m.model + 0xC8 + i * 4, f32_as_u32(matrix[i]));
    if (m.room >= 0) return draw_room_model(c, m, host);
    const uint32_t tevstr = g.scratch, position = g.scratch + 0x700;
    st32(position, f32_as_u32(x));
    st32(position + 4, f32_as_u32(y));
    st32(position + 8, f32_as_u32(z));
    call(c, GC(0x0255FFF4), {tevstr, uint32_t(int32_t(int8_t(ld8(host + 0x326)))), 0xFF});  // dKy_tevstr_init
    const uint32_t env = call(c, GC(0x02555D0C));                                         // dKy_getEnvlight
    call(c, GC(0x025626A4), {env, 0, position, tevstr});  // settingTevStruct (TEV_TYPE_ACTOR)
    call(c, GC(0x02562F5C), {env, m.model, tevstr});      // setLightTevColorType
    // joint callbacks of the shared model data belong to the owning actors; unusable matrix calculators too
    const uint32_t joints = ld32(m.data + 8), jointCount = std::min<uint32_t>(ld32(m.data + 4), 1024);
    std::vector<uint32_t> saved;
    if (mem2(joints, jointCount * 0x1C))
        for (uint32_t j = 0; j < jointCount; ++j)
            for (uint32_t field : {0x08u, 0x14u}) {
                const uint32_t value = ld32(joints + j * 0x1C + field);
                saved.push_back(value);
                const bool callable = field == 0x14 && mem2(value) && mem2(ld32(value)) && ld32(ld32(value) + 0x24);
                if (!callable) st32(joints + j * 0x1C + field, 0);
            }
    // material animation slots of the model data (+0x38..+0x48), set by the owning actor for its own draw
    uint32_t animations[5];
    for (int i = 0; i < 5; ++i) animations[i] = ld32(m.data + 0x38 + i * 4), st32(m.data + 0x38 + i * 4, 0);
    const uint32_t opa = ld32(GD(0x104B4634)), xlu = ld32(GD(0x104B4638));
    call(c, GC(0x025E2DE0), {m.model, 0});  // mDoExt_modelUpdateDL
    st32(GD(0x104B4634), opa);
    st32(GD(0x104B4638), xlu);
    for (int i = 0; i < 5; ++i) st32(m.data + 0x38 + i * 4, animations[i]);
    for (uint32_t j = 0; j < saved.size(); ++j) st32(joints + (j / 2) * 0x1C + (j % 2 ? 0x14 : 0x08), saved[j]);
}

// One frame of the work list: true while models are left
bool sweep_frame(Cpu* c, uint32_t host) {
    const Config& cfg = config();
    if (g.frames >= cfg.hold || g.batch == 0 || g_tag == 0) {
        if (g.batch && g_tag) g.cursor += cfg.per;
        if (g.cursor >= g.models.size()) return false;
        free_heaps(c, kHeaps - 1);  // the oldest batch heap goes
        g.heaps[kHeaps - 1] = g.heap = g.parent && g.heap_size ? call(c, GC(0x025E33A8), {g.heap_size, g.parent, 0x20}) : 0;
        if (!g.heap) LOG("[dsweep] no heap for batch %d", g.batch + 1);
        g_tag = ++g.batch;
        if (g.cursor < g.models.size()) {
            const std::string& a = g.models[g.cursor].archive;
            pipeline_groups::set_sweeping(pipeline_groups::group_of(a.data(), a.size()));
        }
        g.frames = 0;
        std::string names;
        for (size_t i = g.cursor; i < g.models.size() && i < g.cursor + cfg.per; ++i)
            names += " " + g.models[i].archive + ":" + g.models[i].name;
        LOG("[dsweep] batch %d step %llu ns %lld:%s", g.batch, (unsigned long long)interp::executed_steps(),
            (long long)now_ns(), names.c_str());
    }
    for (size_t i = g.cursor; i < g.models.size() && i < g.cursor + cfg.per; ++i) draw_model(c, g.models[i], host);
    ++g.frames;
    return true;
}

void begin(Cpu* c) {
    g.started_ns = now_ns();
    if (const char* d = std::getenv("WWHD_DSWEEP_STATE")) g.state_dir = d;
    progress("start");
    if (const char* s = std::getenv("WWHD_DSWEEP_SKIP")) {
        std::string all = s;
        for (size_t at = 0; at <= all.size();) {
            const size_t comma = std::min(all.find(',', at), all.size());
            if (comma > at) g.skip.insert(all.substr(at, comma - at));
            at = comma + 1;
        }
    }
    bool resident_done = false;
    if (!g.state_dir.empty()) {
        for (auto& s : lines(g.state_dir + "/skip.txt")) g.skip.insert(s);
        for (auto& p : lines(g.state_dir + "/actors.txt")) g.placed.insert(p);
        for (auto& p : lines(g.state_dir + "/places.txt")) g_places.insert(p);
        for (auto& l : lines(g.state_dir + "/progress.txt")) {
            if (l.rfind("archive-done ", 0) == 0) g.done.insert(l.substr(13));
            if (l == "resident-done") resident_done = true;
            if (l.rfind("actor-done ", 0) == 0) g.done.insert(l.substr(11));
            if (l.rfind("env-done ", 0) == 0) g.done.insert(l.substr(9));
            if (l == "file-select") g_file_select_seen = true;
            if (l == "particles-done") g_particles_seen = true;
            if (l.rfind("visit-done ", 0) == 0) g.done.insert(l.substr(11));
        }
    }
    if (part().index > 0) resident_done = true;  // the title's own models: part 0
    size_t order = 0;
    if (const char* list = std::getenv("WWHD_DSWEEP_ARCHIVES"))
        for (auto& name : lines(list)) {
            if (!mine(order++)) continue;
            ++g.listed;
            if (!g.done.count(name) && !g.skip.count(name)) g.queue.push_back({{}, name, order - 1});
            else g.done.insert(name);  // a skipped archive counts as done for the progress
        }
    // WWHD_DSWEEP_STAGES=<file>: "<stage> <archive> <archive>..." per line (Stage, Room<n>, LOD<n>...): each stage
    // resource is loaded with the game's stage resource loader, its models swept, and released
    if (const char* list = std::getenv("WWHD_DSWEEP_STAGES"))
        for (auto& line : lines(list)) {
            std::vector<std::string> words;
            for (size_t at = 0; at < line.size();) {
                const size_t end = std::min(line.find(' ', at), line.size());
                if (end > at) words.push_back(line.substr(at, end - at));
                at = end + 1;
            }
            for (size_t i = 1; i < words.size(); ++i) {
                const Entry e{words[0], words[i], order};
                if (!mine(order++)) continue;
                ++g.listed;
                // a whole stage only by "stage-<stage>": skip names are shared with object archives and actors
                // (the actor "sea" on the list left out every sea room)
                if (!g.done.count(e.id()) && !g.skip.count(e.id()) && !g.skip.count("stage-" + e.stage) &&
                    e.stage.size() < 32 && e.arc.size() < 32)
                    g.queue.push_back(e);
                else g.done.insert(e.id());
            }
        }
    report_archives("archives");
    std::reverse(g.queue.begin(), g.queue.end());  // popped from the back, in file order
    // light state, position, archive name and load phase, stage resource names (host-only memory)
    g.scratch = mem::host_alloc(0x1000, 32);
    std::memset(mem::ptr(g.scratch), 0, 0x1000);
    choose_heaps(c);
    for (uint32_t info : archive_infos()) {
        g.resident.insert(cstring(info + 0x1C));
        if (!resident_done) add_models(c, info);
    }
    LOG("[dsweep] start: %zu resident archives (%zu models%s), %zu archives queued, %zu names skipped", g.resident.size(),
        g.models.size(), resident_done ? ", done before" : "", g.queue.size(), g.skip.size());
    g.phase = Phase::Resident;
    phase_time("objects");
}

void finish() {
    g.phase = Phase::Done;
    g_tag = 0;
    pipeline_groups::set_sweeping(0);
    flush_done(true);  // an orderly exit saves the rest
    phase_time("end");
    progress("sweep-done");
    g_finished = true;
    LOG("[dsweep] done: %u models swept, %u create failures, %u archives loaded, %u load failures, %u actors shown "
        "(%u not created), %.1f s", g.swept, g.failed, g.archives, g.load_failed, g.actors, g.actor_failed,
        double(now_ns() - g.started_ns) / 1e9);
}

// The actor sweep (WWHD_DSWEEP_ACTORS=1, after the archives): every actor the rooms place (up to kVariants
// placements per name: parameters and angles), with its profile from the stage actor name table
// (dStage_searchName's table), is created with the game's own fopAcM_create at the look-at point, draws for a few
// frames without running its logic (before_execute), then is deleted with what it made. Progress "actor <name:params:angles>" before it is created, "actor-done ..." after; an actor
// that changes the stage ends the attempt in order (its pipelines are kept) and is skipped the next time.
constexpr unsigned kCreateFrames = 240, kShowFrames = 24, kShowDraws = 3;
// WWHD_DSWEEP_ACTOR_GROUP=<k>[,<radius>]: k actors at once, on a circle of that radius (default 150) around the look-at
// point. Each is done as a single one would be (shown for kShowDraws draws or kShowFrames frames, or given up after
// kCreateFrames); the group's pipelines are filed under every object archive its actors loaded. A group that stops the
// game is split by the supervisor (actor-groups.txt: "<largest group> <actor>"), down to single actors, which are
// skipped as before.
struct ActorGroups { unsigned size = 1; float radius = 150.f; };
const ActorGroups& actor_groups() {
    static const ActorGroups value = [] {
        ActorGroups v;
        if (const char* e = std::getenv("WWHD_DSWEEP_ACTOR_GROUP"); e && *e) {
            char* p = nullptr;
            v.size = std::clamp(unsigned(std::strtoul(e, &p, 10)), 1u, 64u);
            if (p && *p == ',') v.radius = std::strtof(p + 1, nullptr);
        }
        return v;
    }();
    return value;
}
struct Member {
    State::Spawn spawn;
    uint32_t id = 0xFFFFFFFF, actor = 0;
    unsigned frames = 0, draws = 0;
};
std::vector<Member> g_group;                  // the group being shown
std::map<std::string, unsigned> g_group_cap;  // actor id -> the largest group it is shown in (actor-groups.txt)
std::vector<uint32_t> g_group_archives;       // the groups its pipelines are filed under
unsigned group_cap(const State::Spawn& sp) {
    auto it = g_group_cap.find(sp.id());
    return it == g_group_cap.end() ? actor_groups().size : std::max(1u, it->second);
}
std::set<std::string> archive_names() {
    std::set<std::string> out;
    for (uint32_t info : archive_infos()) out.insert(cstring(info + 0x1C));
    return out;
}
uint32_t actor_of(Cpu* c, uint32_t id) {
    st32(g.scratch + 0x9F0, id);
    return call(c, GC(0x025D5218), {GC(0x025E1234), g.scratch + 0x9F0});  // fopAcIt_Judge(fpcSch_JudgeByID, &id)
}
// The environment sweep (WWHD_DSWEEP_ENV=1, after the archives): the title's sea, sky, clouds, fog and screen
// passes at other times of day and in a storm, set with the game's own environment setters (the save's time of
// day, dKy_change_colpat, dKyw_rain_set, the thunder mode as daVrbox's storm sets them), each held for a while,
// then the title's own state is restored. Its pipelines are not filed under any archive: they compile at start.
struct EnvState {
    const char* name;
    float degrees;  // time of day (0..360 for 24 hours)
    int colpat, rain, thunder;
};
constexpr EnvState kEnvStates[] = {
    {"dawn", 90, 0, 0, 0},     {"noon", 180, 0, 0, 0},       {"dusk", 255, 0, 0, 0},
    {"night", 0, 0, 0, 0},     {"storm-day", 180, 1, 250, 1}, {"storm-night", 0, 1, 250, 1},
};
constexpr unsigned kEnvFrames = 150;  // steps per state: the palette blends and the rain builds up over time
struct EnvSaved {
    bool saved = false;
    uint32_t time = 0, rainOrig = 0, thunder = 0;
    uint8_t colpat = 0;
    size_t next = 0;
    unsigned frames = 0;
} g_env;
void begin_actors(Cpu* c);
void begin_particles(Cpu* c);
void env_apply(Cpu* c, const EnvState& e) {
    const uint32_t info = ld32(GD(0x101F84DC));
    if (mem2(info, 0x48)) st32(info + 0x44, f32_as_u32(e.degrees));
    call(c, GC(0x0255FD48), {uint32_t(e.colpat)});  // dKy_change_colpat
    call(c, GC(0x0257E7C0), {uint32_t(e.rain)});    // dKyw_rain_set
    const uint32_t env = call(c, GC(0x02555D0C));   // dKy_getEnvlight
    if (mem2(env, 0xAC0)) st32(env + 0xAB4, uint32_t(e.thunder));  // mThunderEff.mMode
}
void begin_environment(Cpu* c) {
    static const bool on = std::getenv("WWHD_DSWEEP_ENV") != nullptr;
    if (!on || part().index > 0) return begin_actors(c);
    const uint32_t info = ld32(GD(0x101F84DC)), env = call(c, GC(0x02555D0C));
    if (!mem2(info, 0x48) || !mem2(env, 0xAC0)) return begin_actors(c);
    g_env.saved = true;
    g_env.time = ld32(info + 0x44);
    g_env.rainOrig = ld32(env + 0xA44);  // mRainCountOrig (next to mRainCount +0xA40)
    g_env.thunder = ld32(env + 0xAB4);
    g_env.next = 0;
    g_env.frames = kEnvFrames;
    phase_time("environment");
    g_tag = 0;
    pipeline_groups::set_sweeping(0);
    g.phase = Phase::Environment;
}
void environment_step(Cpu* c) {
    if (++g_env.frames < kEnvFrames) return;
    g_env.frames = 0;
    // the previous state is done
    if (g_env.next > 0) {
        const std::string done = std::string("env-") + kEnvStates[g_env.next - 1].name;
        progress("env-done " + done);
        g.done.insert(done);
    }
    while (g_env.next < std::size(kEnvStates)) {
        const EnvState& e = kEnvStates[g_env.next++];
        const std::string id = std::string("env-") + e.name;
        if (g.done.count(id) || g.skip.count(id)) continue;
        progress("env " + id);
        g_tag = ++g.batch;
        LOG("[dsweep] batch %d step %llu ns %lld: %s", g.batch, (unsigned long long)interp::executed_steps(),
            (long long)now_ns(), id.c_str());
        env_apply(c, e);
        return;
    }
    // the title's own environment again
    const uint32_t info = ld32(GD(0x101F84DC)), env = call(c, GC(0x02555D0C));
    if (mem2(info, 0x48)) st32(info + 0x44, g_env.time);
    call(c, GC(0x0255FD48), {0u});
    call(c, GC(0x0257E7C0), {g_env.rainOrig});
    if (mem2(env, 0xAC0)) st32(env + 0xAB4, g_env.thunder);
    g_tag = 0;
    begin_particles(c);
}
// The file select (WWHD_DSWEEP_FILE_SELECT=1, last): A on the title opens it (the "Name" stage: its 2D screens,
// backdrop and the transitions on the way); after kFileSelectSteps there the sweep ends. Nothing is chosen there
// (no file is selected, nothing is saved).
constexpr unsigned kFileSelectSteps = 240, kFileSelectTimeout = 2400;
uint64_t g_file_select_from = 0, g_file_select_in = 0;
// Stage visits (debug, WWHD_DSWEEP_VISIT=<file>, "<stage> <room>" per line; last): each stage entered with the
// game's own stage change (the next stage request, as a door or the portable save states do) and drawn under its
// own environment (lighting, fog, sky, its actors) for kVisitSteps, filed under "<stage>_Stage". The file select
// (reached from the title only) then follows in a new attempt ("restart" in progress.txt).
constexpr unsigned kVisitSteps = 150, kVisitTimeout = 3000;
struct Visit {
    std::string stage;
    int room = 0, point = 0, layer = -1;
    std::string id() const { return stage + ":" + std::to_string(room) + ":" + std::to_string(layer); }
};
std::vector<Visit> g_visits;
size_t g_visit = 0;
uint64_t g_visit_asked = 0, g_visit_in = 0;
uint64_t g_visit_created = 0, g_visit_new = 0;  // the renderer's count of new pipelines, the step it last grew
uint64_t g_visit_gap = 0;                         // the longest time it did not grow before it grew again
std::atomic<bool> g_visiting{false};
Cpu* g_visit_cpu = nullptr;
void begin_file_select();
bool file_select_pending();
void begin_visits() {
    static const char* list = std::getenv("WWHD_DSWEEP_VISIT");
    if (!list || !*list) return begin_file_select();
    g_visits.clear();
    auto add = [&](const Visit& v) {
        if (v.stage == "sea_T" || v.stage == "Name" || g.skip.count("visit-" + v.stage) || g.skip.count(v.id()))
            return;  // (not a bare name: actors share them)
        g_visits.push_back(v);
    };
    // the rooms with a player start (the rooms' places, places.txt) and the scene layers their placements use
    std::map<std::pair<std::string, int>, int> spawn;
    std::map<std::pair<std::string, int>, std::set<int>> layers;
    std::map<std::string, std::set<int>> stageLayers;
    if (!g.state_dir.empty())
        for (auto& line : lines(g.state_dir + "/places.txt")) {
            char kind[8] = {}, stage[16] = {};
            int a = 0, b = 0;
            if (std::sscanf(line.c_str(), "%7s %15s %d %d", kind, stage, &a, &b) != 4) continue;
            if (!std::strcmp(kind, "spawn")) spawn.emplace(std::make_pair(std::string(stage), a), b);
            else if (a < 0) stageLayers[stage].insert(b);
            else layers[{stage, a}].insert(b);
        }
    if (!std::strcmp(list, "all")) {
        // every room with a player start, once with the layer the game picks and once per further scene layer its
        // placements (or the stage's) use
        for (const auto& [place, point] : spawn) {
            add({place.first, place.second, point, -1});
            std::set<int> all = layers[place];
            all.insert(stageLayers[place.first].begin(), stageLayers[place.first].end());
            for (int layer : all) add({place.first, place.second, point, layer});
        }
        LOG("[dsweep] visits: %zu places (rooms with a player start, per scene layer)", g_visits.size());
    } else {
        // "<stage> <room>", or "<stage> *": every room of that stage with a player start, at its first start, with
        // the layer the game picks (the sea: each island is its own room, entered by sailing there)
        for (auto& line : lines(list)) {
            char stage[16] = {}, room[8] = {};
            if (std::sscanf(line.c_str(), "%15s %7s", stage, room) < 1) continue;
            if (!std::strcmp(room, "*")) {
                for (const auto& [place, point] : spawn)
                    if (place.first == stage) add({stage, place.second, point, -1});
            } else {
                add({stage, std::atoi(room), 0, -1});
            }
        }
    }
    // several processes: every n-th visit; then the visits done before are left out
    std::vector<Visit> todo;
    for (size_t j = 0; j < g_visits.size(); ++j)
        if (mine(j) && !g.done.count("visit-" + g_visits[j].id()) && !g.done.count("visit-" + g_visits[j].stage))
            todo.push_back(g_visits[j]);
    g_visits.swap(todo);
    if (g_visits.empty()) return begin_file_select();
    phase_time("visits");
    g.phase = Phase::Visits;
    g_visiting = true;
    g_visit = 0;
    g_visit_asked = g_visit_in = 0;
}
void visit_step(const std::string& current) {
    const uint64_t now = interp::executed_steps();
    if (g_visit >= g_visits.size()) {
        g_visiting = false;
        // the file select is reached from the title only: the next attempt starts there (an orderly restart)
        if (file_select_pending()) {
            flush_done(true);
            progress("restart");
            LOG("[dsweep] visits done: restarting for the file select");
            g_abort = true;
            g.phase = Phase::Done;
            return;
        }
        return finish();
    }
    const Visit& v = g_visits[g_visit];
    const uint32_t next = GD(0x1046F0B0) + 0x5140;  // dStage_nextStage_c: name[8], point, room, layer, enabled, wipe
    if (!g_visit_asked) {
        if (ld8(next + 12)) return;  // a stage change is in progress
        progress("visit " + v.id());
        for (int i = 0; i < 8; ++i) st8(next + i, i < int(v.stage.size()) ? uint8_t(v.stage[i]) : 0);
        st16(next + 8, uint16_t(v.point));
        st8(next + 10, uint8_t(v.room));
        st8(next + 11, uint8_t(v.layer));
        st8(next + 13, 0);
        if (current == "sea_T") {
            // the title is not a play scene (its own next-stage check returns to the title): change to the play
            // scene, which starts at the next stage, as the file select does (fopScnM_ChangeReq(scene, PLAY_SCENE))
            st8(next + 12, 0);
            const uint32_t scene = call(g_visit_cpu, GC(0x025DC80C), {ld32(GD(0x1047E6C4))});  // fopScnM_SearchByID
            if (!mem2(scene) || !call(g_visit_cpu, GC(0x025DC86C), {scene, 7, 0, 5, 1})) {
                LOG("[dsweep] visit %s: no scene change from the title", v.stage.c_str());
                return;
            }
        } else {
            st8(next + 12, 1);
        }
        g_visit_asked = now;
        g_visit_in = 0;
        // the way there (the scene change, the loading, the arrival) is not the stage's own: compiled at start
        pipeline_groups::set_sweeping(0);
        g_tag = ++g.batch;
        LOG("[dsweep] batch %d step %llu ns %lld: visit %s (point %d)", g.batch, (unsigned long long)now,
            (long long)now_ns(), v.id().c_str(), v.point);
        return;
    }
    // there: the stage change done (the next-stage request taken) and the stage current
    if (current == v.stage && !g_visit_in && !ld8(next + 12) && now - g_visit_asked > 30) {
        g_visit_in = now;
        g_visit_created = pipeline_groups::created();
        g_visit_new = now;
        g_visit_gap = 0;
        // there: what the stage newly needs is filed under it (compiled when it loads); what it shares is not
        const std::string group = v.stage + "_Stage";
        pipeline_groups::set_sweeping(pipeline_groups::group_of(group.data(), group.size()), true);
    }
    if (g_visit_in) {
        if (const uint64_t created = pipeline_groups::created(); created != g_visit_created) {
            g_visit_gap = std::max(g_visit_gap, now - g_visit_new);
            g_visit_created = created, g_visit_new = now;
        }
    }
    // (a visit cannot end early once nothing new appears: some stages make new pipelines after quiet pauses of up to
    // 147 steps, and which ones varies from run to run; measured 2026-10-23, the log line below keeps the numbers)
    const bool shown = g_visit_in && now - g_visit_in >= kVisitSteps;
    if (!shown && now - g_visit_asked < kVisitTimeout) return;
    if (shown)  // (the last new pipeline and the longest pause before one)
        LOG("[dsweep] visit %s: there after %llu steps, last new pipeline at +%llu (longest pause before one %llu), "
            "left at +%llu", v.id().c_str(), (unsigned long long)(g_visit_in - g_visit_asked),
            (unsigned long long)(g_visit_new - g_visit_in), (unsigned long long)g_visit_gap,
            (unsigned long long)(now - g_visit_in));
    else LOG("[dsweep] visit %s: not reached (now in %s)", v.id().c_str(), current.c_str());
    progress(std::string(shown ? "visit-done visit-" : "visit-failed visit-") + v.id());
    g.done.insert("visit-" + v.id());
    ++g_visit;
    g_visit_asked = 0;
}
bool file_select_pending() { return std::getenv("WWHD_DSWEEP_FILE_SELECT") && !g_file_select_seen; }
void begin_file_select() {
    static const bool on = std::getenv("WWHD_DSWEEP_FILE_SELECT") != nullptr;
    if (!on || g_file_select_seen) return finish();
    progress("file-select");
    phase_time("file-select");
    g.phase = Phase::FileSelect;
    g_file_select_from = interp::executed_steps();
    g_file_select = true;
}
void file_select_step(const std::string& stage) {
    const uint64_t now = interp::executed_steps();
    g_in_file_select = stage == "Name";
    if (g_in_file_select && !g_file_select_in) g_file_select_in = now;
    if ((g_file_select_in && now - g_file_select_in >= kFileSelectSteps) || now - g_file_select_from > kFileSelectTimeout) {
        g_file_select = false;
        finish();
    }
}
// Plants (after the actors, which made the grass, flowers and trees the rooms place at the look-at point): the
// grass packet (dComIfGp +0x5AB8, its rooms' data lists at +0x1A5CC, 64 rooms, data: +0x2 animation index, +0x40
// next) draws grass with a wind animation and without one (a cut, dGrass_data_c::WorkAt: index -1) with other
// shapes; every other blade loses its animation for a while, then they are removed with their rooms' lists
// (dGrass_room_c::deleteData).
constexpr unsigned kPlantFrames = 30;
unsigned g_plant_frames = 0;
void begin_plants() {
    phase_time("plants");
    g.phase = Phase::Plants;
    g_plant_frames = 0;
}
void plants_step(Cpu* c) {
    const uint32_t packet = ld32(GD(0x1046F0B0) + 0x5AB8);
    if (!mem2(packet, 0x1A5CC + 64 * 4)) return begin_visits();
    if (g_plant_frames++ == 0) {
        unsigned blades = 0, cut = 0;
        for (uint32_t room = 0; room < 64; ++room)
            for (uint32_t data = ld32(packet + 0x1A5CC + room * 4), n = 0; mem2(data, 0x44) && n < 1500; data = ld32(data + 0x40), ++n)
                if (++blades % 2 == 0 && int8_t(ld8(data + 2)) < 8) st8(data + 2, 0xFF), ++cut;
        progress("plants");
        g_tag = ++g.batch;
        LOG("[dsweep] batch %d step %llu ns %lld: plants (%u grass, %u without animation)", g.batch,
            (unsigned long long)interp::executed_steps(), (long long)now_ns(), blades, cut);
        return;
    }
    if (g_plant_frames < kPlantFrames) return;
    for (uint32_t room = 0; room < 64; ++room)
        if (ld32(packet + 0x1A5CC + room * 4)) call(c, GC(0x0254AC50), {packet + 0x1A5CC + room * 4});  // deleteData
    g_tag = 0;
    begin_visits();
}
// The particle sweep (WWHD_DSWEEP_PARTICLES=1, after the environment, on the title): every particle effect of the
// loaded particle resources (the common ones and the title's scene ones) created with the game's own emitter
// manager (*1047B2D4: JPAEmitterManager::createSimpleEmitterID(position, id, group, resource), the resource by
// the game's own id lookup) at the look-at point, in each draw group the game uses for 3D effects, kParticleBatch at
// a time, drawn for kParticleFrames, then all emitters removed (forceDeleteAllEmitter). Effects are what actors
// and the world spawn while running, which the actor sweep (no logic) never shows. Not filed under any archive.
constexpr unsigned kParticleBatch = 48, kParticleFrames = 6;
constexpr uint32_t kParticleGroups[] = {0, 2, 4, 1, 3};  // (5 ship tail and 6 wind need their owners: assert)
struct ParticleState { size_t group = 0; uint32_t id = 0; unsigned frames = 0; bool alive = false; unsigned made = 0; } g_pa;
void begin_particles(Cpu* c) {
    static const bool on = std::getenv("WWHD_DSWEEP_PARTICLES") != nullptr;
    if (!on || g_particles_seen || part().index > 0 || !mem2(ld32(GD(0x1047B2D4)))) return begin_actors(c);
    phase_time("particles");
    g.phase = Phase::Particles;
    g_pa = {};
    g_tag = 0;
    pipeline_groups::set_sweeping(0);
}
void particles_step(Cpu* c) {
    const uint32_t manager = ld32(GD(0x1047B2D4));
    if (g_pa.alive && ++g_pa.frames < kParticleFrames) return;
    if (g_pa.alive) call(c, GC(0x0282228C), {manager});  // forceDeleteAllEmitter
    g_pa.alive = false;
    if (g_pa.group >= std::size(kParticleGroups)) {
        LOG("[dsweep] particles: %u effects shown", g_pa.made);
        progress("particles-done");
        return begin_actors(c);
    }
    const uint32_t group = kParticleGroups[g_pa.group];
    const uint32_t camera = call(c, GC(0x024F8044));
    const uint32_t pos = g.scratch + 0x9C0;
    for (int i = 0; i < 3; ++i) st32(pos + i * 4, mem2(camera, 0x20) ? ld32(camera + 0x10 + i * 4) : 0);
    char line[48];
    std::snprintf(line, sizeof line, "%u:%04X", group, g_pa.id);
    if (g.skip.count(line)) {  // a batch that stopped the game: its range of 256 ids is left out
        g_pa.id = (g_pa.id | 0xFF) + 1;
        if (g_pa.id > 0xFFFF) g_pa.id = 0, ++g_pa.group;
        return;
    }
    progress(std::string("particles ") + line);
    unsigned made = 0;
    while (made < kParticleBatch && g_pa.id <= 0xFFFF) {
        const uint32_t id = g_pa.id++;
        const uint32_t resource = call(c, GC(0x025A4D40), {id});  // pa_getRM_ID
        if (call(c, GC(0x02821448), {manager, pos, id, group, resource, 0, 0})) ++made;  // createSimpleEmitterID
    }
    g_pa.made += made;
    if (g_pa.id > 0xFFFF) g_pa.id = 0, ++g_pa.group;
    g_pa.alive = made > 0;
    g_pa.frames = 0;
    if (made) {
        g_tag = ++g.batch;
        LOG("[dsweep] batch %d step %llu ns %lld: %u particle effects (group %u, up to %04X)", g.batch,
            (unsigned long long)interp::executed_steps(), (long long)now_ns(), made, group, g_pa.id);
    }
}
void begin_actors(Cpu* c) {
    static const bool on = std::getenv("WWHD_DSWEEP_ACTORS") != nullptr;
    if (!on) return begin_visits();
    std::map<std::string, std::pair<uint16_t, int8_t>> table;  // the stage actor name table: name -> profile, argument
    const uint32_t names = GD(0x101EBFDC);
    for (uint32_t i = 0; i < 809; ++i) {
        const std::string name = name8(names + i * 12);
        if (!name.empty()) table.emplace(name, std::make_pair(ld16(names + i * 12 + 8), int8_t(ld8(names + i * 12 + 0xA))));
    }
    std::set<std::string> withPlacement;
    std::vector<State::Spawn> all;
    for (const auto& p : g.placed) {
        char name[16] = {};
        unsigned params = 0;
        int ax = 0, az = 0;
        if (std::sscanf(p.c_str(), "%15s %x %d %d", name, &params, &ax, &az) != 4 || !table.count(name)) continue;
        State::Spawn sp;
        sp.name = name, sp.params = params, sp.ax = int16_t(ax), sp.az = int16_t(az);
        sp.proc = table[name].first, sp.arg = table[name].second;
        all.push_back(sp);
        withPlacement.insert(name);
    }
    if (part().count > 1) {  // every n-th actor
        std::vector<State::Spawn> own;
        for (size_t j = 0; j < all.size(); ++j)
            if (mine(j)) own.push_back(all[j]);
        all.swap(own);
    }
    // only placed actors: with made-up parameters most actors stop the game in their create (their asserts)
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (!g.done.count(it->id()) && !g.skip.count(it->name) && !g.skip.count(it->id())) g.spawns.push_back(*it);
    g.listed += all.size();
    for (const auto& sp : all)
        if (g.done.count(sp.id()) || g.skip.count(sp.name) || g.skip.count(sp.id())) g.done.insert(sp.id());
    if (actor_groups().size > 1 && !g.state_dir.empty())
        for (auto& line : lines(g.state_dir + "/actor-groups.txt")) {
            const size_t space = line.find(' ');
            if (space == std::string::npos) continue;
            const unsigned cap = unsigned(std::strtoul(line.c_str(), nullptr, 10));
            auto [it, added] = g_group_cap.emplace(line.substr(space + 1), cap);
            if (!added) it->second = std::min(it->second, cap);
        }
    phase_time("actors");
    LOG("[dsweep] actors: %zu names, %zu to create (%zu placements)", table.size(), g.spawns.size(), g.placed.size());
    progress("actors");
    g.phase = Phase::Actors;
    g.actor_id = 0xFFFFFFFF;
}
void actor_group_step(Cpu* c);
void actor_step(Cpu* c) {
    // orphans: actors given up while being created, deleted once they exist
    for (size_t i = 0; i < g.orphans.size();) {
        if (const uint32_t a = actor_of(c, g.orphans[i])) {
            call(c, GC(0x025DF944), {a});  // fpcM_Delete
            g.orphans.erase(g.orphans.begin() + long(i));
        } else if (!call(c, GC(0x025DD868), {g.orphans[i]})) {  // fpcM_IsCreating: gone
            g.orphans.erase(g.orphans.begin() + long(i));
        } else ++i;
    }
    if (actor_groups().size > 1) return actor_group_step(c);
    if (g.actor_id == 0xFFFFFFFF) {
        if (g.spawns.empty()) {
            g_tag = 0;
            pipeline_groups::set_sweeping(0);
            return begin_plants();
        }
        g.spawn = g.spawns.back();
        g.spawns.pop_back();
        progress("actor " + g.spawn.id());
        g.archives_before = archive_names();
        // at the camera's look-at point; angle (x, 0, z) carries parameters for some actors
        const uint32_t camera = call(c, GC(0x024F8044));
        const uint32_t pos = g.scratch + 0x9C0, angle = g.scratch + 0x9D0;
        for (int i = 0; i < 3; ++i) st32(pos + i * 4, mem2(camera, 0x20) ? ld32(camera + 0x10 + i * 4) : 0);
        st16(angle, uint16_t(g.spawn.ax));
        st16(angle + 2, 0);
        st16(angle + 4, uint16_t(g.spawn.az));
        g.actor_id = call(c, GC(0x025D5834), {uint32_t(int32_t(int16_t(g.spawn.proc))), g.spawn.params, pos, 0, angle, 0,
                                              uint32_t(int32_t(g.spawn.arg)), 0});  // fopAcM_create
        g.actor = 0;
        g.actor_frames = g.actor_draws = 0;
        g_tag = ++g.batch;
        pipeline_groups::set_sweeping(0);
        LOG("[dsweep] batch %d step %llu ns %lld: actor %s", g.batch, (unsigned long long)interp::executed_steps(),
            (long long)now_ns(), g.spawn.id().c_str());
        if (g.actor_id == 0xFFFFFFFF) g.actor_frames = kCreateFrames;  // not requested: done below
        return;
    }
    ++g.actor_frames;
    if (!g.actor && g.actor_id != 0xFFFFFFFF) {
        g.actor = actor_of(c, g.actor_id);
        if (g.actor) {
            ++g.actors;
            // its pipelines go with the first object archive it loaded (compiled when that archive loads in play)
            for (const auto& name : archive_names())
                if (!g.archives_before.count(name)) {
                    pipeline_groups::set_sweeping(pipeline_groups::group_of(name.data(), name.size()));
                    break;
                }
            g.actor_frames = 0;
        }
    }
    const bool creating = !g.actor && g.actor_id != 0xFFFFFFFF && call(c, GC(0x025DD868), {g.actor_id});
    const bool shown = g.actor && (g.actor_draws >= kShowDraws || g.actor_frames >= kShowFrames);
    if (!shown && (creating || g.actor) && g.actor_frames < kCreateFrames) return;
    // the actor and the actors it made (those still alive)
    for (const auto& [id, a] : g.ours)
        if (actor_of(c, id) == a) call(c, GC(0x025DF944), {a});  // fpcM_Delete
    g.ours.clear();
    if (creating) g.orphans.push_back(g.actor_id);
    if (!g.actor) ++g.actor_failed;
    progress("actor-done " + g.spawn.id());
    g.done.insert(g.spawn.id());
    report_archives(g.spawn.name);
    g.actor_id = 0xFFFFFFFF;
    g.actor = 0;
}

// A group of actors (WWHD_DSWEEP_ACTOR_GROUP): the next actors of the list with the same largest group, created at
// once around the look-at point, each shown as a single actor would be; deleted together with what they made.
void actor_group_step(Cpu* c) {
    if (g_group.empty()) {
        if (g.spawns.empty()) {
            g_tag = 0;
            pipeline_groups::set_sweeping(0);
            return begin_plants();
        }
        const unsigned cap = group_cap(g.spawns.back());
        while (!g.spawns.empty() && g_group.size() < cap && group_cap(g.spawns.back()) == cap) {
            g_group.push_back({g.spawns.back()});
            g.spawns.pop_back();
        }
        progress("actor-group " + std::to_string(g_group.size()));
        for (const auto& m : g_group) progress("actor " + m.spawn.id());
        g.archives_before = archive_names();
        g_group_archives.clear();
        const uint32_t camera = call(c, GC(0x024F8044));
        const uint32_t pos = g.scratch + 0x9C0, angle = g.scratch + 0x9D0;
        const float radius = g_group.size() > 1 ? actor_groups().radius : 0.f;
        std::string names;
        for (size_t j = 0; j < g_group.size(); ++j) {
            Member& m = g_group[j];
            const float turn = 6.2831853f * float(j) / float(g_group.size());
            const float offset[3] = {radius * std::cos(turn), 0.f, radius * std::sin(turn)};
            for (int i = 0; i < 3; ++i)
                st32(pos + i * 4, mem2(camera, 0x20) ? f32_as_u32(float(ldf32(camera + 0x10 + i * 4)) + offset[i]) : 0);
            st16(angle, uint16_t(m.spawn.ax));
            st16(angle + 2, 0);
            st16(angle + 4, uint16_t(m.spawn.az));
            m.id = call(c, GC(0x025D5834), {uint32_t(int32_t(int16_t(m.spawn.proc))), m.spawn.params, pos, 0, angle, 0,
                                            uint32_t(int32_t(m.spawn.arg)), 0});  // fopAcM_create
            if (m.id == 0xFFFFFFFF) m.frames = kCreateFrames;  // not requested: done below
            names += " " + m.spawn.id();
        }
        g_tag = ++g.batch;
        pipeline_groups::set_sweeping(0);
        LOG("[dsweep] batch %d step %llu ns %lld: actors%s", g.batch, (unsigned long long)interp::executed_steps(),
            (long long)now_ns(), names.c_str());
        return;
    }
    bool all = true;
    for (Member& m : g_group) {
        ++m.frames;
        if (!m.actor && m.id != 0xFFFFFFFF && (m.actor = actor_of(c, m.id))) ++g.actors, m.frames = 0;
    }
    // its pipelines go with the object archives its actors loaded (compiled when one of them loads in play)
    std::vector<uint32_t> groups;
    for (const auto& name : archive_names())
        if (!g.archives_before.count(name) && groups.size() < 8) groups.push_back(pipeline_groups::group_of(name.data(), name.size()));
    if (groups != g_group_archives) g_group_archives = groups, pipeline_groups::set_sweeping(groups);
    std::vector<bool> creating(g_group.size());
    for (size_t j = 0; j < g_group.size(); ++j) {
        const Member& m = g_group[j];
        creating[j] = !m.actor && m.id != 0xFFFFFFFF && call(c, GC(0x025DD868), {m.id});
        const bool shown = m.actor && (m.draws >= kShowDraws || m.frames >= kShowFrames);
        if (!shown && (creating[j] || m.actor) && m.frames < kCreateFrames) all = false;
    }
    if (!all) return;
    for (const auto& [id, a] : g.ours)
        if (actor_of(c, id) == a) call(c, GC(0x025DF944), {a});  // fpcM_Delete
    g.ours.clear();
    for (size_t j = 0; j < g_group.size(); ++j) {
        const Member& m = g_group[j];
        if (creating[j]) g.orphans.push_back(m.id);
        if (!m.actor) ++g.actor_failed;
        progress("actor-done " + m.spawn.id());
        g.done.insert(m.spawn.id());
        report_archives(m.spawn.name);
    }
    g_group.clear();
}

void step(Cpu* c, uint32_t host) {
    flush_done(false);
    const uint32_t name = g.scratch + 0x780, phase = g.scratch + 0x7F0;
    switch (g.phase) {
    case Phase::Start:
        begin(c);
        return step(c, host);
    case Phase::Resident:
        if (sweep_frame(c, host)) return;
        g_tag = 0;
        pipeline_groups::set_sweeping(0);
        progress("resident-done");
        g.phase = Phase::Draining;
        g.archive.clear();
        g.frames = 0;
        return;
    case Phase::NextArchive:
        while (!g.queue.empty() && g.queue.back().stage.empty() &&
               (g.resident.count(g.queue.back().arc) || g.queue.back().arc.size() >= 0x40)) {
            g.done.insert(g.queue.back().arc);  // swept with the resident archives (or not loadable by name)
            g.queue.pop_back();
        }
        if (g.queue.empty()) return begin_environment(c);
        g.entry = g.queue.back();
        // an object archive the game loaded meanwhile (the title's idle story intro loads its Demo archives): the
        // sweep neither loads nor releases what the game holds (releasing it under the game stopped the game when
        // the intro deleted its actors); it goes last, and is swept as it is if the game still holds it then
        if (g.entry.stage.empty() && find_info(g.entry.arc)) {
            if (!g.entry.deferred) {
                g.queue.pop_back();
                g.entry.deferred = true;
                g.queue.insert(g.queue.begin(), g.entry);
                LOG("[dsweep] archive %s is held by the game: later", g.entry.arc.c_str());
                return;
            }
        }
        if (!g.entry.stage.empty()) {
            static bool once = false;  // the phase's start, for setup's timing (setup.log)
            if (!once) once = true, phase_time("rooms");
        }
        g.archive = g.entry.id();
        g.queue.pop_back();
        std::memset(mem::ptr(name), 0, 0x78);  // the name and the load phase
        std::memcpy(mem::ptr(name), g.archive.c_str(), std::min<size_t>(g.archive.size(), 0x6F));
        progress("load " + g.archive);
        g.phase = g.entry.stage.empty() ? Phase::Loading : Phase::StageLoading;
        g.frames = 0;
        g.requested = g.owned = false;
        if (g.entry.stage.empty() && find_info(g.archive)) {  // (still held by the game: swept, not loaded or released)
            ++g.archives;
            g.models.clear();
            g.cursor = 0;
            g_tag = 0;
            pipeline_groups::set_sweeping(0);
            add_models(c, find_info(g.archive));
            g.phase = Phase::Sweeping;
            return;
        }
        g.owned = g.entry.stage.empty();
        return step(c, host);
    case Phase::StageLoading: {
        // the game's stage resource loader (dComIfG_setStageRes / syncStageRes, with any stage's name): the
        // request on the resource load task (*101F4F54), then waiting while it is pending
        const uint32_t task = ld32(GD(0x101F4F54));
        const uint32_t ss = safe_strings(g.entry.stage, g.entry.arc);
        if (!g.requested) {
            if (g.frames++ < kRelease) return;  // the previous archive's release goes first (it frees its heap)
            // (and until it is out of the path pool: uncapped and record-only, kRelease steps pass in milliseconds,
            // and a request made while the release was still running failed and stopped the game when released)
            if (!g_released.empty() && find_path_info(g_released) && g.frames < kLoadFrames) return;
            g_released.clear();
            if (find_path_info(g.archive)) {  // loaded already (the title's own): swept, not released
                g.requested = true;
            } else if (call(c, GC(0x0260D618), {task, ss, ss + 8, 0})) {  // requestResource(stage, archive)
                g.requested = g.owned = true;
                return;
            } else if (++g.frames > kLoadFrames) {
                g.requested = true;  // the queue stays full: given up below
            } else return;
        }
        if (g.owned && call(c, GC(0x0260F4CC), {task, ss, ss + 8}) && ++g.frames <= kLoadFrames) return;
        const uint32_t info = find_path_info(g.archive);
        if (!info) {
            ++g.load_failed;
            LOG("[dsweep] load failed %s (after %u frames)", g.archive.c_str(), g.frames);
            static bool listed = false;
            if (!listed) {  // what the path pool holds (names only)
                listed = true;
                std::string all;
                for (uint32_t i : path_infos()) all += " [" + cstring(ld32(i + 0x10)) + "|" + cstring(ld32(i + 0x5C)) + "]";
                LOG("[dsweep] stage resources loaded:%s (root %08X)", all.c_str(), ld32(ld32(GD(0x101F4F28)) + 0x1028));
            }
            // (not released: a request that left nothing in the path pool has nothing to release, and releasing it
            // stopped the game; tried once more at the end)
            if (!g.entry.deferred) {
                LOG("[dsweep] %s: later", g.archive.c_str());
                g.entry.deferred = true;
                g.queue.insert(g.queue.begin(), g.entry);
                g.archive.clear();
                g.phase = Phase::NextArchive;
                return;
            }
            progress("load-failed " + g.archive);
            g.done.insert(g.archive);
            g.phase = Phase::NextArchive;
            return;
        }
        ++g.archives;
        g.models.clear();
        g.cursor = 0;
        g_tag = 0;
        pipeline_groups::set_sweeping(0);
        add_stage_models(c, info, g.entry);
        harvest(c, g.entry);
        g.phase = Phase::Sweeping;
        return;
    }
    case Phase::Loading: {
        const uint32_t result = call(c, GC(0x02520460), {phase, name});  // dComIfG_resLoad (cPhs: 4 complete)
        if (result == 4) {
            ++g.archives;
            g.models.clear();
            g.cursor = 0;
            g_tag = 0;
            pipeline_groups::set_sweeping(0);
            if (const uint32_t info = find_info(g.archive)) add_models(c, info);
            else {
                // complete, but not in the resource table as a usable archive: not released (releasing it stopped
                // the game); tried once more at the end
                LOG("[dsweep] %s: loaded, but not in the resource table%s", g.archive.c_str(),
                    g.entry.deferred ? "" : "; later");
                if (std::getenv("WWHD_DSWEEP_DEBUG")) {  // debug: the raw entries with that name
                    const uint32_t control = ld32(GD(0x101F4F28));
                    for (unsigned i = 0; i < 128 && mem2(control); ++i) {
                        const uint32_t info = ld32(control + 0x28 + i * 0x20 + 0x18);
                        if (mem2(info, 0x8C) && cstring(info + 0x1C) == g.archive)
                            LOG("[dsweep]   info %08X archive %08X res %08X count %d", info, ld32(info + 0x5C),
                                ld32(info + 0x60), int32_t(ld32(info + 0x84)));
                    }
                }
                g.owned = false;
                if (!g.entry.deferred) {
                    g.entry.deferred = true;
                    g.queue.insert(g.queue.begin(), g.entry);
                    g.archive.clear();
                    g.phase = Phase::NextArchive;
                    return;
                }
            }
            g.phase = Phase::Sweeping;
            return;
        }
        if (result >= 5 || ++g.frames > kLoadFrames) {
            ++g.load_failed;
            LOG("[dsweep] load failed %s (phase %u after %u frames)", g.archive.c_str(), result, g.frames);
            progress("load-failed " + g.archive);
            g.done.insert(g.archive);
            call(c, GC(0x025204C8), {phase, name});  // dComIfG_resDelete
            g.phase = Phase::NextArchive;
        }
        return;
    }
    case Phase::Sweeping:
        if (sweep_frame(c, host)) return;
        g.phase = Phase::Draining;
        g.frames = 0;
        g_tag = 0;
        pipeline_groups::set_sweeping(0);
        return;
    case Phase::Draining:
        if (++g.frames < kDrain) return;
        free_heaps(c, 0);  // every instance goes before its archive does
        g.models.clear();
        if (!g.archive.empty()) {
            if (g.entry.stage.empty()) {
                if (g.owned) call(c, GC(0x025204C8), {phase, name});  // dComIfG_resDelete
            }
            else if (g.owned) {  // the stage resource loader's release (dComIfG_deleteStageRes)
                const uint32_t ss = safe_strings(g.entry.stage, g.entry.arc);
                call(c, GC(0x0260E8A8), {ld32(GD(0x101F4F54)), ss, ss + 8});
                g_released = g.archive;
            }
            progress("archive-done " + g.archive);
            g.done.insert(g.archive);
            report_archives(g.archive);
        }
        g.phase = Phase::NextArchive;
        return;
    case Phase::Environment:
        return environment_step(c);
    case Phase::Particles:
        return particles_step(c);
    case Phase::Actors:
        return actor_step(c);
    case Phase::Plants:
        return plants_step(c);
    case Phase::Done:
        return;
    }
}
}  // namespace

// The actor sweep's actors draw, but never run their logic: around the game's execute pass (fpcEx_Handler) they
// carry the status bits fopAc_Execute skips actors with (fopAc_ac_c::stopStatus, *101F3084); the draw pass runs
// without them.
void before_execute(Cpu* c) {
    if (g.phase != Phase::Actors || g.ours.empty()) return;
    const uint32_t stop = ld32(GD(0x101F3084));
    if (!stop) return;
    for (const auto& [id, a] : g.ours)
        if (actor_of(c, id) == a) {
            const uint32_t status = ld32(a + 0x2E0);
            g.frozen.emplace_back(a, stop & ~status);
            st32(a + 0x2E0, status | stop);
        }
}
void after_execute(Cpu*) {
    for (const auto& [a, bits] : g.frozen)
        if (mem2(a, 0x2E4)) st32(a + 0x2E0, ld32(a + 0x2E0) & ~bits);
    g.frozen.clear();
}

// debug (WWHD_VK_PIPELINE_WHO=<file>, off by default): what draws a pipeline the renderer creates. The guest thread's
// return chain (function addresses; names: build/names.tsv or the decomp) and the objects its registers and the
// top of its stack point to: resources of the loaded object archives and stage resources ("<archive>:<entry>",
// also through a J3DModel's model data at +4) and drawn actors ("actor <profile> <name>"). Reads only, except the
// archives' entry names (resourceHashString) and stage resource lookups, which call the game's pure lookups.
namespace {
std::map<uint32_t, std::string> g_who_actors;  // actor -> "actor <profile> <name>" (drawn since the last frame)
std::map<uint32_t, std::string> who_resources(Cpu* c) {
    static uint64_t built = ~0ull;
    static std::map<uint32_t, std::string> out;
    const uint64_t now = interp::executed_steps();
    if (built == now) return out;
    built = now;
    out.clear();
    constexpr uint32_t kRowCount = 629;
    const uint32_t rows = GD(0x1009BB1C);
    for (uint32_t info : archive_infos()) {
        const std::string name = cstring(info + 0x1C);
        const uint32_t res = ld32(info + 0x60);
        const int32_t count = int32_t(ld32(info + 0x84));
        const uint32_t hash = call(c, GC(0x0273B264), {info + 0x1C, uint32_t(name.size())});  // resourceHashString
        uint32_t entries = 0, entryCount = 0;
        for (uint32_t r = 0; r < kRowCount; ++r)
            if (ld32(rows + r * 12) == hash) entries = ld32(rows + r * 12 + 4), entryCount = ld32(rows + r * 12 + 8);
        for (uint32_t e = 0; e < uint32_t(std::max(count, 0)) && e < 4096; ++e) {
            const uint32_t data = ld32(res + e * 4);
            if (!mem2(data)) continue;
            const std::string entry = e < entryCount && mem2(entries, entryCount * 8) ? cstring(ld32(entries + e * 8), 128)
                                                                                       : "#" + std::to_string(e);
            out.emplace(data, name + ":" + entry);
        }
    }
    if (!g.scratch) {
        g.scratch = mem::host_alloc(0x1000, 32);
        std::memset(mem::ptr(g.scratch), 0, 0x1000);
    }
    for (uint32_t info : path_infos()) {
        const std::string stage = cstring(ld32(info + 0x10)), arc = cstring(ld32(info + 0x5C));
        const uint32_t archive = ld32(info + 0xA8);
        const uint32_t header = mem2(archive, 0x18) ? ld32(archive + 0x14) : 0;
        if (stage.empty() || arc.empty() || !mem2(header, 0x68)) continue;
        const uint32_t count = std::min<uint32_t>(ld16(header + 0x50), 256), models = rel(header + 0x20);
        for (uint32_t i = 0; i < count && mem2(models + i * 16, 0x28); ++i) {
            const std::string model = cstring(rel(rel(models + i * 16 + 0x24) + 4), 128);
            if (model.empty()) continue;
            if (const uint32_t data = stage_res(c, stage, arc, model + ".bmd"); mem2(data))
                out.emplace(data, stage + "_" + arc + ":" + model + ".bmd");
        }
    }
    return out;
}
}  // namespace
std::string describe_draw_context() {
    Cpu* c = threads::current();
    if (!c) return "chain - objs - (not a guest thread)";
    std::string text = "chain";
    char b[48];
    uint32_t sp = c->r[1];
    for (int i = 0; i < 20 && sp >= 0x10000000u && sp < 0xF0000000u; ++i) {
        const uint32_t prev = ld32(sp);
        if (!prev || prev <= sp || prev - sp > 0x100000u) break;
        std::snprintf(b, sizeof b, " %08X", ld32(prev + 4));
        text += b;
        sp = prev;
    }
    const auto resources = who_resources(c);
    std::set<std::string> seen;
    std::string objs;
    auto look = [&](uint32_t w) {
        if (!mem2(w)) return;
        std::string what;
        if (auto a = g_who_actors.find(w); a != g_who_actors.end()) what = a->second;
        else if (auto r = resources.find(w); r != resources.end()) what = r->second;
        else if (mem2(w, 8)) {
            if (auto m = resources.find(ld32(w + 4)); m != resources.end()) what = "model of " + m->second;
        }
        // else an object with a virtual table (its first word in the game's data, pointing to code): the class, by
        // the code address of its first methods (names: build/names.tsv)
        if (what.empty() && mem2(w, 8)) {
            const uint32_t vt = ld32(w);
            if (vt >= 0x10000000u && vt < 0x10800000u && mem2(vt, 0x18)) {
                for (uint32_t o = 0; o < 0x18 && what.empty(); o += 4)
                    if (const uint32_t f = ld32(vt + o); (f >> 24) == 2) {  // in the game's code
                        char v[40];
                        std::snprintf(v, sizeof v, "vt %08X:%08X", vt, f);
                        what = v;
                    }
            }
        }
        if (!what.empty() && seen.insert(what).second && seen.size() <= 12) objs += " [" + what + "]";
    };
    // the registers and the top of the stack, and one level deeper (the render thread replays command records that
    // point to the drawing objects)
    std::vector<uint32_t> roots;
    for (int i = 3; i < 32; ++i) roots.push_back(c->r[i]);
    for (uint32_t a = c->r[1]; a < c->r[1] + 0x3000 && mem2(a, 4); a += 4) roots.push_back(ld32(a));
    for (uint32_t w : roots) look(w);
    std::set<uint32_t> deeper;
    for (uint32_t w : roots)
        if (mem2(w, 0x80) && deeper.insert(w).second && deeper.size() < 4096)
            for (uint32_t o = 0; o < 0x80; o += 4) look(ld32(w + o));
    return text + " | objs" + (objs.empty() ? std::string(" -") : objs);
}
void note_drawn_actor(uint32_t actor) {
    static const bool on = std::getenv("WWHD_VK_PIPELINE_WHO") != nullptr;
    if (on && !gx2::g_draw_context_provider) gx2::g_draw_context_provider = &describe_draw_context;
    if (!on || !mem2(actor, 0x10) || g_who_actors.count(actor)) return;
    const uint16_t profile = ld16(actor + 0xE);
    std::string name = "?";
    const uint32_t names = GD(0x101EBFDC);
    for (uint32_t i = 0; i < 809; ++i)
        if (ld16(names + i * 12 + 8) == profile) { name = name8(names + i * 12); break; }
    g_who_actors.emplace(actor, "actor " + std::to_string(profile) + " " + name);
    // (and when it first draws, by the renderer's frame, to match pipelines made in the same frames)
    LOG("[dsweep who] frame %llu first draw: actor %u %s", (unsigned long long)render::frame_count(), unsigned(profile),
        name.c_str());
}

int tag() { return g_tag.load(std::memory_order_relaxed); }
uint32_t pad_buttons(uint64_t frame) {
    // a title sweep: the boot's notice screen waits for A; pulse it until the title stage is up. Later the idle title
    // plays its story intro (no actor draws, so the sweep pauses): A there returns to the title. Never while the title
    // itself is up (A there opens the file select).
    if (!config().title || g_finished || frame % 60 >= 6) return 0;
    // the file select at the end: A on the title until it is up, then nothing
    if (g_file_select.load(std::memory_order_relaxed)) return g_in_file_select.load(std::memory_order_relaxed) ? 0 : 0x8000u;
    if (g_visiting.load(std::memory_order_relaxed)) return 0;
    if (!g_title_seen.load(std::memory_order_relaxed)) return frame >= 120 ? 0x8000u : 0u;
    // (no actor draw for 90 logic steps. Not by time: uncapped, a record-only sweep plays the whole intro in about two
    // seconds, and a frame waiting two seconds for its pipelines on a busy machine is no intro; either way A came
    // on the title and opened the file select, where the attempt stalled)
    const int64_t last = g_last_draw_ns.load(std::memory_order_relaxed);
    const uint64_t lastStep = g_last_draw_step.load(std::memory_order_relaxed);
    // One pulse per intro: A ends the intro, and the way back to the title draws no actor either; a second pulse
    // there reached the title and opened the file select.
    static uint64_t pressedFor = ~0ull;  // the last draw's step whose intro got its A
    static uint64_t pressedFrame = 0;
    const bool idle = last && interp::executed_steps() - lastStep > 90 && (pressedFor != lastStep || frame - pressedFrame < 6);
    if (!idle) return 0;
    if (pressedFor != lastStep) pressedFor = lastStep, pressedFrame = frame;
    return 0x8000u;
}
std::atomic<bool> g_finished{false};
bool exit_requested() { return (g_finished || g_abort) && std::getenv("WWHD_DSWEEP_EXIT"); }

bool hide_actor_draw(uint32_t actor) {
    // debug (attribution runs): WWHD_DSWEEP_PROFILES=1 logs each actor profile the first time it draws in a stage
    static const bool profiles = std::getenv("WWHD_DSWEEP_PROFILES") != nullptr;
    if (profiles && mem2(actor, 0x10)) {
        static std::set<std::string> seen;
        const std::string id = stage_name() + ":" + std::to_string(ld16(actor + 0xE));
        if (seen.insert(id).second) {
            // the profile definition (base_process_class +0x10) and the code addresses of the method tables it names
            // (the actor's own draw is one of them), to look the actor up by function address
            const uint32_t prof = ld32(actor + 0x10);
            std::string methods;
            for (uint32_t o = 0; o < 0x40 && mem2(prof + o); o += 4) {
                const uint32_t v = ld32(prof + o);
                if (!mem2(v, 0x18)) continue;
                for (uint32_t m = 0; m < 0x18; m += 4)
                    if ((ld32(v + m) >> 24) == 2) {  // in the game's code
                        char b[24];
                        std::snprintf(b, sizeof b, " +%X.%X:%08X", o, m, ld32(v + m));
                        methods += b;
                    }
            }
            LOG("[dsweep] profile %u draws in %s (step %llu):%s", unsigned(ld16(actor + 0xE)), stage_name().c_str(),
                (unsigned long long)interp::executed_steps(), methods.c_str());
        }
    }
    static const char* e = std::getenv("WWHD_DSWEEP_ONLY");
    static uint64_t from = 0;
    static std::vector<uint32_t> keep;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (e && *e) {
            char* p = const_cast<char*>(e);
            from = std::strtoull(p, &p, 10);
            while (*p == ':') keep.push_back(uint32_t(std::strtoul(p + 1, &p, 0)));
        }
    }
    if (!from || interp::executed_steps() < from) return false;
    const uint32_t profile = mem2(actor, 0x10) ? ld16(actor + 0xE) : 0xFFFF;
    return std::find(keep.begin(), keep.end(), profile) == keep.end();
}

// debug: WWHD_DSWEEP_TIME=step:degrees[,step:degrees...] sets the time of day (dSv_player_status_b_c::mTime,
// 0..360 for 24 hours) once at each logic step, for reference runs by day and night
void time_of_day(uint32_t actor) {
    static const char* e = std::getenv("WWHD_DSWEEP_TIME");
    static size_t done = 0;
    if (!e || ld16(actor + 0xE) != 0xA8) return;
    const uint64_t step = interp::executed_steps();
    const char* p = e;
    for (size_t i = 0; *p; ++i) {
        char* end;
        const unsigned long at = std::strtoul(p, &end, 10);
        if (*end != ':') return;
        const float degrees = std::strtof(end + 1, &end);
        if (i == done && step >= at) {
            const uint32_t info = ld32(GD(0x101F84DC));
            if (mem2(info, 0x48)) st32(info + 0x44, f32_as_u32(degrees));
            LOG("[dsweep] time of day %.1f at step %llu", double(degrees), (unsigned long long)step);
            ++done;
            return;
        }
        p = *end == ',' ? end + 1 : end;
    }
}

// The object archives in the resource table (requested or loaded), published as pipeline groups once a recipe
// database with groups is loaded (pipeline_groups.h): the renderer compiles their pipelines ahead. Reads only.
void publish_resident() {
    static uint64_t last = ~0ull;
    const uint64_t now = interp::executed_steps();
    if (!pipeline_groups::wanted() || now == last) return;
    last = now;
    // a play stage drawing for a few seconds (not the boot's, the title's or the file select's): the background
    // compile may use all its threads now (pipeline_groups::playing)
    if (!pipeline_groups::playing()) {
        static std::string stage;
        static uint64_t since = 0;
        const std::string current = stage_name();
        if (current != stage) stage = current, since = now;
        if (!current.empty() && current != "Name" && current != "sea_T" && now - since >= 90) pipeline_groups::set_playing();
    }
    std::vector<uint32_t> groups;
    const uint32_t control = ld32(GD(0x101F4F28));
    for (unsigned i = 0; i < 128 && mem2(control, 0x28 + 128 * 0x20); ++i) {
        const uint32_t info = ld32(control + 0x28 + i * 0x20 + 0x18);
        if (!mem2(info, 0x8C)) continue;
        const std::string name = cstring(info + 0x1C);
        if (!name.empty()) groups.push_back(pipeline_groups::group_of(name.data(), name.size()));
    }
    // and the stage resources (rooms, the stage archive, LODs) as <stage>_<archive>
    static const bool trace = std::getenv("WWHD_DSWEEP_PUBLISH_LOG") != nullptr;  // debug: the stage resources published
    std::string ids;
    for (uint32_t info : path_infos())
        if (const std::string id = path_id(info); !id.empty()) {
            groups.push_back(pipeline_groups::group_of(id.data(), id.size()));
            if (trace) ids += " " + id;
        }
    if (trace) {
        static std::string previous;
        if (ids != previous) LOG("[dsweep] stage resources published at step %llu:%s", (unsigned long long)now, ids.c_str());
        previous = ids;
    }
    pipeline_groups::publish(std::move(groups));
}

// debug (rooms): WWHD_DSWEEP_ROOM=<room archive>|-:<file>[:<tev type>] draws one more instance of a room model right
// after its room's daBg draw (in the stage WWHD_DSWEEP_ROOM_STAGE, default "sea"), made and drawn as daBg_c does:
// mDoExt_J3DModel__create(data, 0, 0x11000022), daBg's base matrix (the room's map transform), J3DModel calc,
// settingTevStruct(BG type), setLightTevColorType, BG lists, modelEntryDL. With WWHD_DSWEEP_ONLY=1000 (daBg hidden)
// the room appears as with daBg's own draw. WWHD_DSWEEP_ROOM_CLIP=1 adds daBg's clipper call, WWHD_DSWEEP_ROOM_ZERO=1
// zeroes the instance's heap first, WWHD_DSWEEP_ROOM_DIFF=<n> logs where the two instances differ at the n-th draw
// (all three ruled out as causes; the base matrix was it).
void room_test(Cpu* c, uint32_t actor) {
    static const char* e = std::getenv("WWHD_DSWEEP_ROOM");
    if (!e || !*e || ld16(actor + 0xE) != 439) return;
    // only once the test's stage (WWHD_DSWEEP_ROOM_STAGE, default "sea") has run for 120 steps (its rooms are loaded)
    static const std::string want = std::getenv("WWHD_DSWEEP_ROOM_STAGE") ? std::getenv("WWHD_DSWEEP_ROOM_STAGE") : "sea";
    static uint64_t since = ~0ull;
    const uint64_t now = interp::executed_steps();
    if (stage_name() != want) { since = ~0ull; return; }
    if (since == ~0ull) since = now;
    if (now - since < 120) return;
    static uint32_t model = 0, tevstr = 0, scratch = 0, heap = 0, host = 0;
    static bool failed = false;
    if (failed || (host && actor != host)) return;  // drawn once per frame, after its own room's daBg
    const uint32_t play = GD(0x1046F0B0);
    if (!model) {
        std::string spec = e, arc = spec.substr(0, spec.find(':')), file = spec.substr(spec.find(':') + 1);
        file = file.substr(0, file.find(':'));
        if (!scratch) {
            scratch = mem::host_alloc(0x900, 32);
            std::memset(mem::ptr(scratch), 0, 0x900);
        }
        std::memcpy(mem::ptr(scratch + 0x800), arc.c_str(), arc.size());
        std::memcpy(mem::ptr(scratch + 0x880), file.c_str(), file.size());
        // the drawing daBg's own archive name (daBg_setArcName, as its createHeap does): only this room's daBg
        const uint32_t arcName = call(c, GC(0x02077E30), {actor});
        if (arc != "-" && cstring(arcName) != arc) return;
        arc = cstring(arcName);
        LOG("[dsweep room] daBg %08X archive %s, model %s", actor, arc.c_str(), file.c_str());
        uint32_t data = call(c, GC(0x0252447C), {arcName, scratch + 0x880});  // dComIfG_getStageRes
        if (!mem2(data)) {
            // the resource table's own names (as the sweep lists models): <archive>:<entry name>
            const uint32_t info = find_info(arc);
            if (info) {
                const size_t before = g.models.size();
                add_models(c, info);
                for (size_t i = before; i < g.models.size(); ++i) {
                    LOG("[dsweep room] %s has %s", arc.c_str(), g.models[i].name.c_str());
                    if (g.models[i].name == file) data = g.models[i].data;
                }
                g.models.resize(before);
            } else LOG("[dsweep room] no archive %s in the resource table", arc.c_str());
        }
        if (!mem2(data)) { LOG("[dsweep room] %s not loaded", e); failed = true; return; }
        choose_heaps(c);
        heap = g.parent && g.heap_size ? call(c, GC(0x025E33A8), {g.heap_size, g.parent, 0x20}) : 0;
        if (!heap) { LOG("[dsweep room] no heap"); failed = true; return; }
        if (std::getenv("WWHD_DSWEEP_ROOM_ZERO")) {  // the solid heap's free area (head +0x94, tail +0x98) zeroed
            const uint32_t head = ld32(heap + 0x94), tail = ld32(heap + 0x98);
            LOG("[dsweep room] heap %08X free %08X..%08X", heap, head, tail);
            if (mem2(head) && tail > head && mem2(tail - 1, 1) && tail - head <= (8u << 20)) std::memset(mem::ptr(head), 0, tail - head);
        }
        const uint32_t previous = call(c, GC(0x025E3570), {heap});
        model = call(c, GC(0x025E38E0), {data, 0u, 0x11000022u});  // mDoExt_J3DModel__create, as daBg
        call(c, GC(0x025E3570), {previous});
        if (!model) { LOG("[dsweep room] create failed"); failed = true; return; }
        tevstr = scratch;
        call(c, GC(0x0255FFF4), {tevstr, uint32_t(int32_t(int8_t(ld8(actor + 0x326)))), 0xFF});  // dKy_tevstr_init
        host = actor;
        LOG("[dsweep room] %s: data %08X model %08X", e, data, model);
    }
    // WWHD_DSWEEP_ROOM_DIFF=<frames>: after that many draws, log the words of daBg's own model 0 instance and ours
    // that differ (and, one level deep, the first 0x60 bytes of the blocks their pointer fields name)
    static const unsigned diffAt = std::getenv("WWHD_DSWEEP_ROOM_DIFF") ? unsigned(std::atoi(std::getenv("WWHD_DSWEEP_ROOM_DIFF"))) : 0;
    static unsigned draws = 0;
    if (diffAt && ++draws == diffAt) {
        const uint32_t own = ld32(actor + 0x3B4);
        LOG("[dsweep room diff] daBg model %08X, ours %08X (model data %08X / %08X)", own, model, ld32(own + 4), ld32(model + 4));
        for (uint32_t off = 0; off < 0x300 && mem2(own + off) && mem2(model + off); off += 4) {
            const uint32_t a = ld32(own + off), b = ld32(model + off);
            const bool pa = mem2(a, 0x60) && a >= 0x10000000, pb = mem2(b, 0x60) && b >= 0x10000000;
            if (a != b && !(pa && pb)) LOG("[dsweep room diff] +%03X daBg %08X ours %08X", off, a, b);
            if (pa && pb && a != b) {
                std::string d;
                for (uint32_t o = 0; o < 0x60; o += 4)
                    if (ld32(a + o) != ld32(b + o) && !(mem2(ld32(a + o)) && mem2(ld32(b + o))))
                        d += " +" + std::to_string(o) + ":" + std::to_string(ld32(a + o)) + "/" + std::to_string(ld32(b + o));
                if (!d.empty()) LOG("[dsweep room diff] +%03X -> %08X / %08X:%s", off, a, b, d.c_str());
            }
        }
    }
    const unsigned type = std::strchr(std::strchr(e, ':') + 1, ':') ? std::atoi(std::strchr(std::strchr(e, ':') + 1, ':') + 1) : 1;
    // the room's place: daBg_Create sets each model's base matrix from dComIfGp_getMapTrans (sea rooms sit on the
    // sea's grid; a model left at the origin is far out of view): copy daBg's own model 0 base matrix (+0xC8)
    if (const uint32_t own = ld32(actor + 0x3B4); mem2(own, 0x100))
        for (int i = 0; i < 12; ++i) st32(model + 0xC8 + i * 4, ld32(own + 0xC8 + i * 4));
    call(c, GC(0x027F4D5C), {model});  // J3DModel calc
    static const bool clip = std::getenv("WWHD_DSWEEP_ROOM_CLIP") != nullptr;
    if (clip) {  // mDoLib_clipper (1048CFF0): far 100000, the model's shapes against the view, far reset
        const uint32_t clipper = GD(0x1048CFF0);
        const uint32_t savedFar = ld32(clipper + 0x54);  // (not "far": a Windows macro)
        st32(clipper + 0x54, f32_as_u32(100000.f));
        call(c, GC(0x0283801C), {clipper});
        call(c, GC(0x02838524), {clipper, model});
        st32(clipper + 0x54, ld32(clipper + 0x5C));
        call(c, GC(0x0283801C), {clipper});
        (void)savedFar;
    }
    const uint32_t env = call(c, GC(0x02555D0C));
    call(c, GC(0x025626A4), {env, type, 0, tevstr});  // settingTevStruct(TEV_TYPE_BG0 + i)
    call(c, GC(0x02562F5C), {env, model, tevstr});    // setLightTevColorType
    const uint32_t opa = ld32(GD(0x104B4634)), xlu = ld32(GD(0x104B4638));
    st32(GD(0x104B4634), ld32(play + 0x5D70));  // dComIfGd_setListBG
    st32(GD(0x104B4638), ld32(play + 0x5D74));
    call(c, GC(0x025E2E5C), {model});  // mDoExt_modelEntryDL
    st32(GD(0x104B4634), opa);
    st32(GD(0x104B4638), xlu);
}

void after_actor_draw(Cpu* c, uint32_t actor) {
    if (!mem2(actor, 0x330)) return;
    note_drawn_actor(actor);
    room_test(c, actor);
    publish_resident();
    const Config& cfg = config();
    if (cfg.title) {
        g_last_draw_ns.store(now_ns(), std::memory_order_relaxed);
        g_last_draw_step.store(interp::executed_steps(), std::memory_order_relaxed);
    }
    time_of_day(actor);
    const uint64_t now = interp::executed_steps();
    if (!cfg.on || now < cfg.start) return;
    if (g.actor && actor == g.actor) ++g.actor_draws;
    for (Member& m : g_group)
        if (m.actor == actor) ++m.draws;
    if (cfg.title && g.phase != Phase::Actors) g.host_actors.insert(actor);
    else if (g.phase == Phase::Actors && !g.host_actors.count(actor)) g.ours.emplace(ld32(actor + 4), actor);
    // once per logic step, from the first actor drawn (the camera and the frame's draw lists are set up then)
    static uint64_t last = ~0ull;
    if (now == last) return;
    last = now;
    // only in a settled stage (the title "sea_T" or a game stage, the same one for kSettle steps): the stage's light
    // and environment state, which the models' light setup reads, is ready then; the sweep pauses while a stage
    // changes (it never changes during a setup sweep from the title)
    constexpr uint64_t kSettle = 120;
    static std::string stage;
    static uint64_t since = 0;
    const std::string current = stage_name();
    if (current != stage) {
        stage = current, since = now;
        if (current == "sea_T") g_title_seen = true;
        LOG("[dsweep] stage \"%s\" at step %llu", current.c_str(), (unsigned long long)now);
    }
    if (g.phase == Phase::FileSelect) return file_select_step(current);
    if (g.phase == Phase::Visits) return g_visit_cpu = c, visit_step(current);
    if (g.phase == Phase::Actors && cfg.title && !current.empty() && current != "sea_T" && !g_abort) {
        LOG("[dsweep] actor %s changed the stage to %s: ending this attempt", g.spawn.id().c_str(), current.c_str());
        g_abort = true;  // the supervisor skips it (the last "actor" line) and resumes
    }
    if (current.empty() || current == "Name" || now - since < kSettle) return;
    if (cfg.title && current != "sea_T") return;  // a title sweep never sweeps elsewhere
    step(c, actor);
}
}  // namespace dsweep
