#pragma once
// The object archives loaded in the game right now, as pipeline recipe groups (gfx/vulkan/pipeline_recipe.h,
// group_of: FNV-1a of the archive name). The game thread publishes the set when it changes (dsweep.cpp,
// read-only scan of the resource table); the Vulkan pipeline worker compiles the recipes of a newly loaded
// archive ahead of its models' first draw. Only active once a recipe database with groups (the setup's model
// sweep) is loaded: the worker turns the scan on with want().
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace pipeline_groups {
// the group of an object archive: FNV-1a (32 bits) of its name, never 0
inline uint32_t group_of(const char* name, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ uint8_t(name[i])) * 16777619u;
    return h ? h : 1;
}
struct Shared {
    std::mutex mutex;
    std::vector<uint32_t> resident;  // sorted
    std::atomic<uint64_t> generation{0};
    std::atomic<bool> wanted{false};
    std::atomic<uint32_t> sweeping{0};
    // more groups at once (the actor sweep's groups of actors: every archive the group's actors loaded)
    std::array<std::atomic<uint32_t>, 7> sweepingMore{};
    std::atomic<bool> sweepingNewOnly{false};
    std::atomic<uint64_t> created{0};
    std::atomic<bool> playing{false};
    std::atomic<bool> playingWanted{false};
};
inline Shared& shared() { static Shared value; return value; }
// worker: grouped recipes exist, start publishing
inline void want() { shared().wanted.store(true, std::memory_order_relaxed); }
inline bool wanted() { return shared().wanted.load(std::memory_order_relaxed); }
// worker: only when play begins is wanted (the Metal-derived queue's throttle), not the archive scan
inline void want_playing() { shared().playingWanted.store(true, std::memory_order_relaxed); }
inline bool playing_wanted() { return shared().playingWanted.load(std::memory_order_relaxed); }
// game thread: the current set (any order); a new generation only when it changed
inline void publish(std::vector<uint32_t> groups) {
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    if (groups == s.resident) return;
    s.resident = std::move(groups);
    s.generation.fetch_add(1, std::memory_order_release);
}
// the setup's model sweep: the group of the archive whose models are being drawn (0: none); the renderer files the
// pipelines it creates or uses meanwhile under it
// new_only (stage visits): only pipelines created meanwhile are filed under it, not the ones a visited stage merely
// uses (those are shared by many places; filing them under every stage would have each stage load compile them again)
inline void set_sweeping(uint32_t group, bool new_only = false) {
    auto& s = shared();
    s.sweepingNewOnly.store(new_only, std::memory_order_relaxed);
    for (auto& more : s.sweepingMore) more.store(0, std::memory_order_relaxed);
    s.sweeping.store(group, std::memory_order_relaxed);
}
// several groups at once (up to 8; the first is sweeping())
inline void set_sweeping(const std::vector<uint32_t>& groups) {
    auto& s = shared();
    s.sweepingNewOnly.store(false, std::memory_order_relaxed);
    for (size_t i = 0; i < s.sweepingMore.size(); ++i)
        s.sweepingMore[i].store(i + 1 < groups.size() ? groups[i + 1] : 0, std::memory_order_relaxed);
    s.sweeping.store(groups.empty() ? 0 : groups[0], std::memory_order_relaxed);
}
inline uint32_t sweeping() { return shared().sweeping.load(std::memory_order_relaxed); }
// every group being swept (sweeping() first; empty: none)
inline std::vector<uint32_t> sweeping_all() {
    auto& s = shared();
    std::vector<uint32_t> out;
    if (const uint32_t first = s.sweeping.load(std::memory_order_relaxed)) out.push_back(first);
    else return out;
    for (auto& more : s.sweepingMore)
        if (const uint32_t g = more.load(std::memory_order_relaxed)) out.push_back(g);
    return out;
}
// the renderer: a pipeline it has not recorded before (counted for the sweep's stage visits, which leave a place once
// nothing new appears for a while)
inline void note_created() { shared().created.fetch_add(1, std::memory_order_relaxed); }
inline uint64_t created() { return shared().created.load(std::memory_order_relaxed); }
inline bool sweeping_new_only() { return shared().sweepingNewOnly.load(std::memory_order_relaxed); }
// the game is in a play stage (past the boot, the title and the file select, and their loading): the background
// compile then uses all its threads (before that one, so the loads into the first stage keep their CPU)
inline void set_playing() { shared().playing.store(true, std::memory_order_relaxed); }
inline bool playing() { return shared().playing.load(std::memory_order_relaxed); }
inline uint64_t generation() { return shared().generation.load(std::memory_order_acquire); }
inline std::vector<uint32_t> resident() {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    return s.resident;
}
}  // namespace pipeline_groups
