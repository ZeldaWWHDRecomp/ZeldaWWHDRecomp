// dsweep_archives.h: the sweep finds the object archives the resource control holds by its archive tree, not by the
// tree's node storage, where a released archive's node keeps its info pointer (issue #703).
#include "dsweep_archives.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>

namespace {
using namespace dsweep_archives;
// a TreeMapPool as the game keeps it: nodes in a storage array, a LIFO free list through the left links
// (TreeMapNode::erase rewrites only the left link), a search tree by key (unbalanced here: the walk does not care)
struct Pool {
    std::map<uint32_t, uint32_t> mem;
    std::map<uint32_t, uint32_t> key;  // node -> key (the game keeps a hash at +0x14)
    uint32_t storage = 0x20000000, root = 0, free = 0;
    Pool() {
        free = storage;
        for (uint32_t i = 0; i < kNodes; ++i) mem[storage + i * kNodeSize + kLeft] = i + 1 < kNodes ? storage + (i + 1) * kNodeSize : 0;
    }
    uint32_t ld(uint32_t a) const {
        auto it = mem.find(a);
        return it == mem.end() ? 0 : it->second;
    }
    void insert(uint32_t k, uint32_t value) {
        const uint32_t n = free;
        assert(n);
        free = ld(n + kLeft);
        mem[n + kLeft] = mem[n + kRight] = 0;
        mem[n + kValue] = value;
        key[n] = k;
        uint32_t* at = &root;
        while (*at) at = &mem[*at + (k < key[*at] ? kLeft : kRight)];
        *at = n;
    }
    void erase(uint32_t k) {
        uint32_t* at = &root;
        while (*at && key[*at] != k) at = &mem[*at + (k < key[*at] ? kLeft : kRight)];
        assert(*at);
        const uint32_t n = *at, l = ld(n + kLeft), r = ld(n + kRight);
        if (!l || !r) {
            *at = l ? l : r;
        } else {  // the right subtree's minimum takes its place
            uint32_t* m = &mem[n + kRight];
            while (ld(*m + kLeft)) m = &mem[*m + kLeft];
            const uint32_t s = *m;
            *m = ld(s + kRight);
            mem[s + kLeft] = l;
            mem[s + kRight] = ld(n + kRight);
            *at = s;
        }
        key.erase(n);
        mem[n + kLeft] = free;  // its value stays
        free = n;
    }
    std::vector<uint32_t> held() const {
        std::vector<uint32_t> out;
        tree_values(root, [&](uint32_t a) { return ld(a); },
                    [&](uint32_t a) { return a >= storage && a < storage + kNodes * kNodeSize; }, out);
        std::sort(out.begin(), out.end());
        return out;
    }
    std::vector<uint32_t> slots() const {
        auto all = storage_values(storage, [&](uint32_t a) { return ld(a); });
        std::vector<uint32_t> out;
        for (uint32_t v : all)
            if (v) out.push_back(v);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
};
bool has(const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }
}  // namespace

int main() {
    Pool p;
    assert(p.held().empty() && p.slots().empty());
    // the title's resident archives, then one the sweep loads
    const uint32_t always = 0x30000100, system = 0x30000200, vtil = 0x30000300, vtin1 = 0x30000400;
    p.insert(50, always);
    p.insert(20, system);
    p.insert(70, vtil);
    assert((p.held() == std::vector<uint32_t>{always, system, vtil}));
    // the title's scene change: the sweep drops Vtil (released) and queues it again; looked up again after the
    // change, before any other load: the storage still names it, the tree does not
    p.erase(70);
    assert(!has(p.held(), vtil));
    assert(has(p.slots(), vtil));  // the old lookup: "held by the game", then its freed models swept (#703)
    assert((p.held() == std::vector<uint32_t>{always, system}));
    // the next load takes the released node (LIFO): its value is overwritten
    p.insert(80, vtin1);
    assert(has(p.held(), vtin1) && !has(p.slots(), vtil));
    // an inner node released (two children): the tree stays whole
    for (uint32_t k = 1; k <= 40; ++k) p.insert(100 + k, 0x31000000 + k);
    p.erase(50);
    p.erase(120);
    const auto held = p.held();
    assert(held.size() == 3 + 40 - 2 && !has(held, always) && !has(held, 0x31000000 + 20) && has(held, system));
    // a pool emptied and refilled: nothing released is held
    for (uint32_t k = 1; k <= 40; ++k)
        if (k != 20) p.erase(100 + k);
    p.erase(20);
    p.erase(80);
    assert(p.held().empty() && !p.slots().empty());
    // a broken link (a node outside the storage) ends the walk there
    p.insert(5, 0x32000000);
    p.mem[p.root + kLeft] = 0x10;
    assert((p.held() == std::vector<uint32_t>{0x32000000}));
    std::puts("dsweep_archives: ok");
    return 0;
}
