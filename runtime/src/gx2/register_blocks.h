// Sparse context restoration. Blocks never written are zero in every context.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gx2 {
template <size_t Words, size_t BlockWords = 256>
class RegisterBlocks {
    std::array<bool, (Words + BlockWords - 1) / BlockWords> touched{};
    std::vector<uint32_t> blocks;
public:
    void touch(size_t first, size_t count) {
        if (!count || first >= Words || count > Words - first) return;
        for (size_t b = first / BlockWords; b <= (first + count - 1) / BlockWords; ++b)
            if (!touched[b]) { touched[b] = true; blocks.push_back(uint32_t(b)); }
    }
    void include(const uint32_t* regs) {
        for (size_t i = 0; i < Words; ++i) if (regs[i]) touch(i, 1);
    }
    // Compare before copying, including zeros from a newly initialized context.
    // The predicate uses the renderer's existing shader relevance rules.
    template <class Relevant>
    bool restore(uint32_t* regs, const uint32_t* shadow, Relevant relevant) const {
        bool shader = false;
        for (size_t b : blocks) {
            size_t first = b * BlockWords, n = std::min(BlockWords, Words - first);
            if (!memcmp(regs + first, shadow + first, n * sizeof(uint32_t))) continue;
            if (!shader)
                for (size_t i = first; i < first + n; ++i)
                    if (regs[i] != shadow[i] && relevant(uint32_t(i), regs[i], shadow[i])) { shader = true; break; }
            memcpy(regs + first, shadow + first, n * sizeof(uint32_t));
        }
        return shader;
    }
};
} // namespace gx2
