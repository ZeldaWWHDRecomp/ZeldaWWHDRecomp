#include "gx2/register_blocks.h"
#include <cassert>
#include <cstdio>
#include <random>

int main() {
    constexpr size_t words = 65536, primitive = 0x2256;
    gx2::RegisterBlocks<words> blocks;
    std::vector<uint32_t> regs(words), baseline(words);
    std::array<std::vector<uint32_t>, 5> contexts, full;
    for (auto& c : contexts) c.resize(words);
    full = contexts;
    int active = -1;
    std::mt19937 rng(0x81ca);
    auto relevant = [](uint32_t r, uint32_t, uint32_t) { return r % 4 == 0; };
    for (int step = 0; step < 20000; ++step) {
        unsigned op = rng() % 8, slot = rng() % contexts.size();
        if (op <= 2) {
            // Cross block boundaries, identical writes, zeros and writes without a shadow.
            size_t first = rng() % words, n = std::min<size_t>(1 + rng() % 600, words - first);
            blocks.touch(first, n);
            for (size_t i = first; i < first + n; ++i) {
                uint32_t v = op == 0 ? 0 : op == 1 ? regs[i] : rng();
                regs[i] = baseline[i] = v;
                if (active >= 0) contexts[active][i] = full[active][i] = v;
            }
        } else if (op == 3) {
            contexts[slot].assign(words, 0); full[slot].assign(words, 0);
            active = slot; // setup affects shadow only
        } else if (op == 4) active = -1;
        else if (op == 5) {
            regs[primitive] = baseline[primitive] = rng(); // renderer writes only the live file
        } else if (op == 6) {
            // Full-state load reconstructs the map from values, including inactive contexts.
            blocks = {};
            blocks.include(regs.data());
            for (const auto& c : contexts) blocks.include(c.data());
        } else {
            blocks.touch(primitive, 1);
            bool expected = false;
            for (size_t i = 0; i < words; ++i)
                expected |= baseline[i] != full[slot][i] && relevant(i, baseline[i], full[slot][i]);
            bool changed = blocks.restore(regs.data(), contexts[slot].data(), relevant);
            baseline = full[slot];
            assert(changed == expected);
            assert(regs == baseline);
            assert(!blocks.restore(regs.data(), contexts[slot].data(), relevant));
            active = slot;
        }
        assert(regs == baseline);
        assert(contexts == full);
    }
    puts("20000 random register write/setup/switch/load sequences match full-copy reference");
}
