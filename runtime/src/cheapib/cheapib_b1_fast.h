// cheapib b1, cheaper bookkeeping (decomp fast build; included by cheapib_b1.h inside namespace cheapib::b1).
// Switch WWHD_CHEAPIB_B1_FAST (mask, default 31; 0 = the code before these steps, every path as it was):
//   1  one snapshot lookup per entry (Snapshot::find_held: the bytes and their held size from find's cache, instead of
//      find + block_size's binary search); b1_src's / b1_note_upload's pointer -> block uses locate_cached (the last
//      block hit first)
//   2  replay(): flat open-addressing tables reset by a generation counter (no per-call / per-frame unordered_map
//      build, clear and node malloc/free) for the moved slices (per frame), the moved instance/vertex slices (per call)
//      and the room check (per call: a few buffers, linear)
//   4  the logic pass's segments are recycled (their vectors keep their capacity: no regrowth every logic pass)
//   8  b1_src_keep / _inherit's kept source lists in a flat table (entries and their vectors reused; erase = mark dead)
//   16 lean appends: source-list triples and captured commands appended by resize + stores (no initializer-list or
//      iterator-range insert)
// The results are the same as the old paths' (same keys, same values, same order of decisions): a pure cost change.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>

uint32_t fast_mask() {
    static const uint32_t m = [] {
        const char* e = getenv("WWHD_CHEAPIB_B1_FAST");
        const uint32_t v = e ? uint32_t(strtoul(e, nullptr, 0)) : 31u;
        LOG("[cheapib] b1 fast: mask %u (1 one snapshot lookup, 2 flat replay tables, 4 recycled segments, 8 flat kept "
            "lists, 16 lean appends)", v);
        return v;
    }();
    return m;
}
inline bool fast(uint32_t bit) { return (fast_mask() & bit) != 0; }
// WWHD_CHEAPIB_B1_FAST_VERIFY=1 (debug, Mac): every fast lookup also runs the old path and compares (counted and logged
// in the b1 report: "[cheapib] b1 fast verify: N compared, M differ")
bool fast_verify() {
    static const bool v = getenv("WWHD_CHEAPIB_B1_FAST_VERIFY") && atoi(getenv("WWHD_CHEAPIB_B1_FAST_VERIFY"));
    return v;
}
uint64_t g_fvChecks = 0, g_fvBad = 0;
inline void fv(bool same, const char* what) {
    g_fvChecks++;
    if (same) return;
    g_fvBad++;
    static int n = 16;
    if (n > 0) {
        n--;
        LOG("[cheapib] b1 fast verify: mismatch in %s", what);
    }
}

// three words appended to a source list: v.insert(v.end(), {a, b, c}), or (fast 16) one resize and three stores
__attribute__((always_inline)) inline void put3(std::vector<uint32_t>& v, uint32_t a, uint32_t b, uint32_t c) {
    if (!fast(16)) {
        v.insert(v.end(), {a, b, c});
        return;
    }
    const size_t n = v.size();
    v.resize(n + 3);
    uint32_t* p = v.data() + n;
    p[0] = a;
    p[1] = b;
    p[2] = c;
}
// uint64 key -> two uint64 values; entries of an older generation count as empty (reset = ++gen, no clearing)
struct GenMap {
    struct E {
        uint64_t key, a, b;
        uint32_t gen;
    };
    std::vector<E> t;
    uint32_t gen = 1, live = 0, mask = 0;
    void reset() {
        if (++gen == 0) {  // (wrapped: clear for real once)
            for (E& e : t) e.gen = 0;
            gen = 1;
        }
        live = 0;
    }
    static uint64_t h(uint64_t k) {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdull;
        k ^= k >> 33;
        return k;
    }
    E* find(uint64_t k) {
        if (t.empty()) return nullptr;
        for (uint32_t i = uint32_t(h(k)) & mask;; i = (i + 1) & mask) {
            E& e = t[i];
            if (e.gen != gen) return nullptr;
            if (e.key == k) return &e;
        }
    }
    // the entry for k (inserted with a, b = 0 when new); NEW says which
    E& get(uint64_t k, bool& isNew) {
        if (2 * (live + 1) > t.size()) grow();
        for (uint32_t i = uint32_t(h(k)) & mask;; i = (i + 1) & mask) {
            E& e = t[i];
            if (e.gen != gen) {
                e = {k, 0, 0, gen};
                live++;
                isNew = true;
                return e;
            }
            if (e.key == k) {
                isNew = false;
                return e;
            }
        }
    }
    void grow() {
        std::vector<E> old;
        old.swap(t);
        t.assign(old.empty() ? 1024 : old.size() * 2, E{0, 0, 0, 0});
        mask = uint32_t(t.size() - 1);
        const uint32_t g = gen;
        for (const E& e : old) {
            if (e.gen != g) continue;
            for (uint32_t i = uint32_t(h(e.key)) & mask;; i = (i + 1) & mask)
                if (t[i].gen != g) {
                    t[i] = e;
                    break;
                }
        }
    }
};

// pointer -> Kept, entries never removed (erase = dead; their vectors keep their capacity); rebuilt without the dead
// ones when the table fills
template <class V>
struct PtrTable {
    struct E {
        const void* key = nullptr;
        bool used = false, alive = false;
        V v;
    };
    std::vector<E> t;
    uint32_t used = 0, alive = 0, mask = 0;
    static uint32_t h(const void* p) {
        uint64_t k = uint64_t(reinterpret_cast<uintptr_t>(p));
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdull;
        k ^= k >> 33;
        return uint32_t(k);
    }
    E* slot(const void* k) {
        if (t.empty()) return nullptr;
        for (uint32_t i = h(k) & mask;; i = (i + 1) & mask) {
            E& e = t[i];
            if (!e.used) return nullptr;
            if (e.key == k) return &e;
        }
    }
    V* find(const void* k) {
        E* e = slot(k);
        return e && e->alive ? &e->v : nullptr;
    }
    void erase(const void* k) {
        E* e = slot(k);
        if (e && e->alive) {
            e->alive = false;
            alive--;
        }
    }
    V& get(const void* k) {
        if (E* e = slot(k)) {
            if (!e->alive) {
                e->alive = true;
                alive++;
            }
            return e->v;
        }
        if (2 * (used + 1) > t.size()) rebuild();
        for (uint32_t i = h(k) & mask;; i = (i + 1) & mask) {
            E& e = t[i];
            if (!e.used) {
                e.key = k;
                e.used = e.alive = true;
                used++;
                alive++;
                return e.v;
            }
        }
    }
    bool empty() const { return alive == 0; }
    void rebuild() {  // (twice the live entries' room at least; dead entries dropped)
        std::vector<E> old;
        old.swap(t);
        size_t n = 4096;  // (keys are short-lived pointers: room for many dead ones between rebuilds)
        while (n < 4ull * (alive + 1)) n *= 2;
        t.resize(n);
        mask = uint32_t(n - 1);
        used = 0;
        for (E& e : old) {
            if (!e.used || !e.alive) continue;
            for (uint32_t i = h(e.key) & mask;; i = (i + 1) & mask)
                if (!t[i].used) {
                    t[i] = std::move(e);
                    used++;
                    break;
                }
        }
    }
};
