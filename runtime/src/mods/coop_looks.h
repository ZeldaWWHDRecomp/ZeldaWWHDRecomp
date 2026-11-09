// SPDX-License-Identifier: MPL-2.0
// Online co-op's looks (docs/online-coop.md): which of a Link texture's tinted copies a look is drawn from, the pure
// part of coop_pose.cpp's worn(). Each texture keeps at most a fixed number of copies (memory stays bounded); a copy
// whose look no shown player wears any more (a look changed on the colour page, whose live preview sends every step,
// or a player gone) is tinted again in place for a new look, the least recently used first. Only when every copy is
// worn by a shown player in a different look does a look past them show the fallback (the game's colours).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mods::coop::looks {

enum class Pick {
    kHit,       // a copy in this look: drawn as it is
    kRetint,    // a copy whose memory is used again: tinted in this look in place
    kNew,       // a new copy (fewer than the cap so far)
    kFallback,  // every copy worn by someone else: the fallback copy, as it is
};
struct Choice {
    Pick pick;
    size_t index;  // into the copies (kNew: the one to append, at the end)
};

// E: a copy, with `key` (the look encoded; empty: memory free to use again), `obj` (non-zero once made) and `used`
// (when last drawn, the caller's clock). worn(key): whether a shown player wears that look now. `fallback`: the look
// shown when nothing is free (the game's colours, encoded)
template <class E, class Worn>
Choice pick(const std::vector<E>& copies, const std::vector<uint8_t>& key, size_t cap, Worn worn,
            const std::vector<uint8_t>& fallback) {
    for (size_t i = 0; i < copies.size(); i++)
        if (copies[i].key == key) return {Pick::kHit, i};
    for (size_t i = 0; i < copies.size(); i++)  // (a copy of a texture found before: its memory again)
        if (copies[i].key.empty() && copies[i].obj) return {Pick::kRetint, i};
    if (copies.size() < cap) return {Pick::kNew, copies.size()};
    size_t lru = copies.size();
    for (size_t i = 0; i < copies.size(); i++)
        if (copies[i].obj && !worn(copies[i].key) && (lru == copies.size() || copies[i].used < copies[lru].used)) lru = i;
    if (lru < copies.size()) return {Pick::kRetint, lru};
    for (size_t i = 0; i < copies.size(); i++)  // (more looks shown than copies)
        if (copies[i].key == fallback) return {Pick::kFallback, i};
    return {Pick::kFallback, 0};
}

}  // namespace mods::coop::looks
