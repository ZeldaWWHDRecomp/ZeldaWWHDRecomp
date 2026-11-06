// Putting back the memory of actors drawn on passes without logic (interp.cpp actor_keep /
// actors_put_back), and the words a draw owns that stay as the draw left them.
//
// With frame interpolation every actor is drawn on each pass of a logic step, at 30 fps once. Some
// draws hand state on to the next execute, so the memory of an actor a pass without logic draws is
// saved before that draw and put back after the pass has been painted.
//
// HD draws that fill vertex or uniform buffers every frame (the custom cloth packets: the pirate
// ship's sail and flag, the King of Red Lions' sail, flags, mantles, the tapestry, Molgera's sand
// floor) keep two sets of GX2 buffers and a word selecting the set (0 or 1) in the actor: each draw
// fills the selected set and flips the word, so that it never writes the buffers the GPU may still
// be reading for the frame before. Put back with the rest of the actor, the word flipped once per
// step instead of once per draw: a pass's draw refilled the buffers the previous pass had just
// handed to the painter while the GPU was still reading them, and the pirate ship's sail was drawn
// torn (front and back faces from different poses: dark blotches) on one pass of every step at
// 60/120/240 fps (issue #446). Only draws read the word; the draws register it (DrawSideWords) and
// the put-back leaves it as the last draw left it.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace interp {

// guest addresses of words the draws of the current pass own (registered during the pass, cleared
// after its put-back)
class DrawSideWords {
public:
    static constexpr size_t kCap = 4096;
    void add(uint32_t ea) {
        if ((ea & 3) || words_.size() >= kCap) return;
        if (std::find(words_.begin(), words_.end(), ea) == words_.end()) words_.push_back(ea);
    }
    // word indices inside the actor's n words at `actor` (appended to `out`)
    void indices_in(uint32_t actor, uint32_t n, std::vector<uint32_t>& out) const {
        for (uint32_t ea : words_)
            if (ea >= actor && (ea - actor) / 4 < n) out.push_back((ea - actor) / 4);
    }
    void clear() { words_.clear(); }
    size_t size() const { return words_.size(); }

private:
    std::vector<uint32_t> words_;
};

// put back the words from `first` on that changed since `saved`, except the indices in `keep`
inline void put_back_words(uint32_t* cur, const uint32_t* saved, uint32_t n, uint32_t first, const std::vector<uint32_t>& keep) {
    for (uint32_t i = first; i < n; i++)
        if (cur[i] != saved[i] && std::find(keep.begin(), keep.end(), i) == keep.end()) cur[i] = saved[i];
}

}  // namespace interp
