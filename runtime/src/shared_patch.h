// Temporary changes to game memory that another game thread may use at the same moment. In the
// game's "play on GamePad" mode two threads draw the item screen at once (#326), so a plain
// save, write, restore let the second thread save the first one's change as the original and put
// it back for good; each such overlap added another tint. Here the first thread to change a value
// keeps the original on the host, later threads change it from that original, and the last one
// out restores it, unless the game wrote the value meanwhile. The lock covers only this table,
// never game code.
#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ppc.h"

namespace shared_patch {

struct Held {
    uint32_t address;
    unsigned size;  // 1, 2 or 4 bytes
};

namespace detail {
struct Entry {
    uint32_t original, written;
    unsigned size, users;
};
inline std::mutex mutex;
inline std::unordered_map<uint32_t, Entry> entries;

inline uint32_t load(uint32_t address, unsigned size) {
    return size == 1 ? ld8(address) : size == 2 ? ld16(address) : ld32(address);
}
inline void store(uint32_t address, unsigned size, uint32_t value) {
    if (size == 1) st8(address, (uint8_t)value);
    else if (size == 2) st16(address, (uint16_t)value);
    else st32(address, value);
}
// The game's own value: the kept original while a change is still in memory, else memory.
inline uint32_t original_locked(uint32_t address, unsigned size) {
    const uint32_t now = load(address, size);
    const auto it = entries.find(address);
    return it != entries.end() && it->second.size == size && now == it->second.written ? it->second.original : now;
}
}  // namespace detail

// What the game holds at `address`, even while some thread has a change there.
inline uint32_t authored(uint32_t address, unsigned size = 4) {
    std::lock_guard lock(detail::mutex);
    return detail::original_locked(address, size);
}
inline float authored_float(uint32_t address) {
    const uint32_t bits = authored(address);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Write `value(original)` at `address` until release(); `value` is host arithmetic only.
template <class F>
void hold(std::vector<Held>& held, uint32_t address, unsigned size, F value) {
    std::lock_guard lock(detail::mutex);
    const uint32_t original = detail::original_locked(address, size);
    const uint32_t written = (uint32_t)value(original);
    auto [it, fresh] = detail::entries.try_emplace(address, detail::Entry{original, written, size, 0});
    // A game write since (or a different width) starts over from what memory holds now.
    it->second.original = original;
    it->second.written = written;
    it->second.size = size;
    it->second.users++;
    detail::store(address, size, written);
    held.push_back({address, size});
}
inline void hold_value(std::vector<Held>& held, uint32_t address, unsigned size, uint32_t value) {
    hold(held, address, size, [value](uint32_t) { return value; });
}
inline void hold_float(std::vector<Held>& held, uint32_t address, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    hold_value(held, address, 4, bits);
}
inline void hold_offset(std::vector<Held>& held, uint32_t address, float offset) {
    hold(held, address, 4, [offset](uint32_t bits) {
        float f;
        std::memcpy(&f, &bits, 4);
        f += offset;
        std::memcpy(&bits, &f, 4);
        return bits;
    });
}

// Ends this thread's changes, newest first. The last user restores the original, unless memory no
// longer holds the change (the game wrote there since; its value stays).
inline void release(std::vector<Held>& held) {
    std::lock_guard lock(detail::mutex);
    for (auto h = held.rbegin(); h != held.rend(); ++h) {
        const auto it = detail::entries.find(h->address);
        if (it == detail::entries.end()) continue;
        if (--it->second.users) continue;
        if (detail::load(h->address, it->second.size) == it->second.written)
            detail::store(h->address, it->second.size, it->second.original);
        detail::entries.erase(it);
    }
    held.clear();
}

}  // namespace shared_patch
