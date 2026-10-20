// SPDX-License-Identifier: MPL-2.0
#include "story_room_paths.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace story_room_paths {
namespace path = story_room_path;
namespace {
struct Entry {
    std::string mod;
    path::Path path;
    unsigned slot = 0;
    uint8_t number = 0;
};
std::vector<Entry> g_entries;  // under g_mu (the editor lists them from its own thread)
std::recursive_mutex g_mu;
uint32_t g_memory = 0;

uint32_t slot_address(unsigned slot) { return g_memory + slot * path::kSlotBytes; }
uint16_t next_number(const Entry& e);
void write(const Entry& e) {
    if (!g_memory) return;
    const auto bytes = path::encode(e.path, e.number, slot_address(e.slot), next_number(e));
    std::memcpy(mem::ptr(slot_address(e.slot)), bytes.data(), bytes.size());
}
void clear(unsigned slot) {
    if (g_memory) std::memset(mem::ptr(slot_address(slot)), 0, path::kSlotBytes);
}
const Entry* find(const std::string& mod, const std::string& id) {
    for (const auto& e : g_entries)
        if (e.mod == mod && e.path.id == id) return &e;
    return nullptr;
}
bool same_room(const path::Path& a, const path::Path& b) { return a.stage == b.stage && a.room == b.room; }
uint16_t next_number(const Entry& e) {
    if (e.path.next.empty()) return 0xFFFF;
    const Entry* n = find(e.mod, e.path.next);
    return n && same_room(n->path, e.path) ? n->number : 0xFFFF;
}
}  // namespace

void set_memory(uint32_t guest) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    g_memory = guest;
    if (!guest) return;
    std::memset(mem::ptr(guest), 0, path::kMemoryBytes);
    for (const auto& e : g_entries) write(e);
}
uint32_t memory() { return g_memory; }

bool declare(const std::string& mod, const std::vector<path::Path>& paths, std::string& why) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    std::vector<Entry> next;
    for (const auto& e : g_entries)
        if (e.mod != mod) next.push_back(e);
    // kept: the same id in the same room keeps its record and number
    std::vector<const path::Path*> fresh;
    for (const auto& p : paths) {
        const Entry* old = find(mod, p.id);
        if (old && same_room(old->path, p)) next.push_back({mod, p, old->slot, old->number});
        else fresh.push_back(&p);
    }
    for (const auto* p : fresh) {
        std::vector<bool> used(path::kSlots, false);
        std::vector<uint8_t> taken;
        for (const auto& e : next) {
            used[e.slot] = true;
            if (same_room(e.path, *p)) taken.push_back(e.number);
        }
        unsigned slot = path::kSlots;
        for (unsigned s = 0; s < path::kSlots; s++)
            if (!used[s]) { slot = s; break; }
        if (slot == path::kSlots) return why = "the enabled mods have more than 64 paths", false;
        const uint8_t number = path::free_number(taken);
        if (!number || taken.size() >= path::kMaxPerRoom) return why = "more than 24 paths in " + p->stage + " room " + std::to_string(p->room), false;
        next.push_back({mod, *p, slot, number});
    }
    // records of paths that went
    for (const auto& e : g_entries) {
        const bool kept = std::any_of(next.begin(), next.end(), [&](const Entry& n) { return n.slot == e.slot; });
        if (!kept) clear(e.slot);
    }
    g_entries = std::move(next);
    for (const auto& e : g_entries)
        if (e.mod == mod) write(e);
    return true;
}
bool declared() {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    return !g_entries.empty();
}
uint8_t number(const std::string& mod, const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    const Entry* e = find(mod, id);
    return e ? e->number : 0;
}
uint32_t record(const std::string& mod, const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    const Entry* e = find(mod, id);
    return e && g_memory ? slot_address(e->slot) : 0;
}
bool is_record(uint32_t address) {
    return g_memory && address >= g_memory && address < g_memory + path::kMemoryBytes && (address - g_memory) % path::kSlotBytes == path::kHeader;
}
uint32_t next_record(uint32_t address, int room) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!is_record(address) || ld32(address - path::kHeader + path::kMagicAt) != path::kMagic) return 0;
    const uint16_t next = ld16(address + 2);
    if (next == 0xFFFF) return 0;
    const unsigned slot = (address - g_memory) / path::kSlotBytes;
    const Entry* from = nullptr;
    for (const auto& e : g_entries)
        if (e.slot == slot) from = &e;
    if (!from || (room >= 0 && unsigned(room) != from->path.room)) return 0;
    for (const auto& e : g_entries)
        if (e.mod == from->mod && e.number == next && same_room(e.path, from->path)) return slot_address(e.slot) + path::kHeader;
    return 0;
}
uint32_t answer(const std::string& mod, const std::string& id, uint32_t number, int room, const std::string& stage) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    const Entry* e = find(mod, id);
    if (!e || !g_memory || number != e->number || room < 0 || unsigned(room) != e->path.room || stage != e->path.stage) return 0;
    const uint32_t at = slot_address(e->slot);
    if (ld32(at + path::kMagicAt) != path::kMagic || ld8(at + path::kNumber) != e->number) write(*e);  // a state from before this path
    return at + path::kHeader;
}
void state_loaded() {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!g_memory) return;
    std::memset(mem::ptr(g_memory), 0, path::kMemoryBytes);
    for (const auto& e : g_entries) write(e);
}
std::vector<Info> list() {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    std::vector<Info> out;
    for (const auto& e : g_entries) {
        Info i;
        i.mod = e.mod, i.id = e.path.id, i.stage = e.path.stage, i.room = e.path.room, i.points = unsigned(e.path.points.size());
        i.closed = e.path.closed, i.number = e.number;
        i.address = g_memory ? slot_address(e.slot) : 0;
        i.valid = g_memory && ld32(i.address + path::kMagicAt) == path::kMagic && ld32(i.address + path::kHash) == path::hash(e.path, e.number);
        out.push_back(i);
    }
    return out;
}
void reset() {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    g_entries.clear();
    g_memory = 0;
}

}  // namespace story_room_paths
