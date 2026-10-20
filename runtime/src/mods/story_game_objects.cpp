// Game objects in the running game (story_game_objects.h).
#include "story_game_objects.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

#include "guest_addr.h"
#include "runtime.h"

namespace story_game_objects {
namespace {
const uint32_t kSearchName = GC(0x025C109C);  // dStage_searchName: the object name table entry of a stage actor name
struct Resolved {
    uint16_t process = 0;
    uint8_t subtype = 0xFF;
    bool known = false;
};
std::map<std::string, Resolved> g_names;  // native name -> what the game's table says (static data)

using Heaps = std::array<std::pair<uint32_t, uint32_t>, 4>;  // archive, game, zelda, command: heap, free bytes
std::map<std::pair<std::string, std::string>, Heaps> g_before;
uint32_t heap_value(Cpu* c, Call call, uint32_t heap, uint32_t slot) {
    if (!heap) return 0;
    const uint32_t vt = ld32(heap + 0xC), fn = vt ? ld32(vt + slot) : 0;
    return fn ? call(c, fn, {heap}) : 0;
}
Heaps heaps(Cpu* c, Call call) {
    Cpu probe = *c;
    Heaps out{};
    const uint32_t getter[] = {GC(0x025E3268), GC(0x025E2FBC), GC(0x025EE048), GC(0x025EE060)};
    for (unsigned i = 0; i < 4; ++i) {
        out[i].first = call(&probe, getter[i], {});
        out[i].second = heap_value(&probe, call, out[i].first, 0x74);
    }
    return out;
}
}  // namespace

bool resolve(Cpu* c, Call call, uint32_t scratch, const Entry& e, const std::string& variant, Native& out, std::string& why) {
    out = {};
    const Variant* v = e.variant(variant);
    if (!v) return why = "unknown variant \"" + variant + "\"", false;
    auto found = g_names.find(e.native_name);
    if (found == g_names.end()) {
        Resolved r;
        if (c && call && scratch && e.native_name.size() < 8) {
            std::memset(mem::ptr(scratch), 0, 16);
            std::memcpy(mem::ptr(scratch), e.native_name.data(), e.native_name.size());
            Cpu probe = *c;
            const uint32_t entry = call(&probe, kSearchName, {scratch});
            if (entry) {
                r.known = true;
                r.process = ld16(entry + 8);
                r.subtype = ld8(entry + 0xA);
            }
            LOG("[game_objects] %s (%s): %s process %04X subtype %u", e.id.c_str(), e.native_name.c_str(), r.known ? "the game has it as" : "not in the game's object name table", unsigned(r.process),
                unsigned(r.subtype));
        }
        found = g_names.emplace(e.native_name, r).first;
    }
    const Resolved& r = found->second;
    if (!r.known) return why = "the game has no object called " + e.native_name, false;
    if (r.subtype != uint8_t(e.subtype)) return why = "the game's " + e.native_name + " is another kind of object (subtype " + std::to_string(r.subtype) + ")", false;
    out.process = r.process;
    out.subtype = r.subtype;
    out.params = v->params;
    return true;
}

ReadBack read_back(uint32_t actor, const Native& n) {
    if (!actor || (actor & 3) || actor < mem::kMem2Start || actor > mem::kMem2End - 0x400) return ReadBack::Mismatch;
    return ld16(actor + 0xE) == n.process && ld32(actor + 0xB0) == n.params && ld8(actor + 0x2DD) == n.subtype ? ReadBack::Match : ReadBack::Mismatch;
}

std::map<uint32_t, unsigned> g_room_ages;
void note_room(uint32_t room_process) {
    if (!room_process) return;
    if (g_room_ages.size() > 64 && !g_room_ages.contains(room_process)) g_room_ages.clear();
    unsigned& age = g_room_ages[room_process];
    if (age < 1000000) ++age;
}
unsigned room_age(uint32_t room_process) {
    const auto it = g_room_ages.find(room_process);
    return it == g_room_ages.end() ? 0 : it->second;
}

int batch_free(const std::string& kind) {
    struct Layout {
        const char* kind;
        uint32_t pointer, data, count, stride;
        bool (*used)(uint32_t);
    };
    static const Layout layouts[] = {
        {"grass", 0x5AB8, 0x9C, 1500, 0x44, [](uint32_t d) { return ld8(d) != 0; }},
        {"tree", 0x5ABC, 0x9C, 64, 0xA8, [](uint32_t d) { return ld8(d) != 0; }},
        {"wood", 0x5AC0, 0x98, 200, 0x248, [](uint32_t d) { return (ld32(d + 0x10) & 1) != 0; }},
        {"flower", 0x5AC4, 0x9C, 200, 0x44, [](uint32_t d) { return (ld8(d) & 2) != 0; }},
    };
    for (const auto& l : layouts) {
        if (kind != l.kind) continue;
        const uint32_t packet = ld32(GD(0x1046F0B0) + l.pointer);  // dComIfGp_get{Grass,Tree,Wood,Flower}
        if (!packet || (packet & 3) || packet < mem::kMem2Start || uint64_t(packet) + l.data + uint64_t(l.count) * l.stride > mem::kMem2End) return -1;
        int free = 0;
        for (uint32_t i = 0; i < l.count; ++i) free += !l.used(packet + l.data + i * l.stride);
        return free;
    }
    return -1;
}

bool heap_trace() {
    const char* v = std::getenv("WWHD_MOD_ACTOR_HEAPS");
    return v && std::strcmp(v, "1") == 0;
}
void trace_before_create(Cpu* c, Call call, const std::string& mod, const std::string& id) {
    if (!heap_trace() || !c || !call) return;
    g_before[{mod, id}] = heaps(c, call);
}
void trace_created(Cpu* c, Call call, const std::string& mod, const std::string& id, uint32_t actor) {
    if (!heap_trace() || !c || !call) return;
    const Heaps after = heaps(c, call);
    const auto before = g_before.find({mod, id});
    const char* names[] = {"archive", "game", "zelda", "command"};
    if (before != g_before.end()) {
        for (unsigned i = 0; i < 4; ++i)
            LOG("[game_object_heaps] %s:%s actor=%08X stage=created heap=%s pointer=%08X free_before=%u free_after=%u delta=%lld", mod.c_str(), id.c_str(), actor, names[i],
                after[i].first, before->second[i].second, after[i].second, static_cast<long long>(before->second[i].second) - after[i].second);
        g_before.erase(before);
    }
    Cpu probe = *c;
    const uint32_t solid = ld32(actor + 0xF4);
    const uint32_t size = solid >= mem::kMem2Start && solid < mem::kMem2End ? heap_value(&probe, call, solid, 0x6C) : 0;
    const uint32_t free = size ? heap_value(&probe, call, solid, 0x74) : 0;
    LOG("[game_object_heaps] %s:%s actor=%08X stage=solid heap=%08X size=%u free=%u used=%u", mod.c_str(), id.c_str(), actor, solid, size, free, size >= free ? size - free : 0);
}

void forget_resolved() {
    g_room_ages.clear();
    g_names.clear();
    g_before.clear();
}

}  // namespace story_game_objects
