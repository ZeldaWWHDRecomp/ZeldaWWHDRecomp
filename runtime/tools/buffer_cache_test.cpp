// Vulkan guest buffer cache, renderer-independent part (runtime/src/gfx/vulkan/buffer_cache_core.h):
// the region allocator, hits and sub-ranges, invalidation by CPU writes (page faults), kernel writes
// (HostWrite), explicit hints (DCFlushRange / GX2Invalidate), save-state loads (invalidate_all),
// overlapping ranges, converted-data keys, dynamic ranges, deferred region retirement, eviction, the
// verify mode, and a concurrent writer.
#include "gfx/vulkan/buffer_cache_core.h"

#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace gfxvk::bufcache;

static uint8_t* g_mem;
constexpr uint64_t kSize = 64ull << 20;
static uint32_t g_page;

static uint32_t host_page() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return (uint32_t)sysconf(_SC_PAGESIZE);
#endif
}

// CPU blocks standing in for the GPU buffers; retired regions are freed by complete() (the fence)
struct TestBacking : Backing {
    static constexpr uint64_t kBlock = 1 << 20;
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<RangeAllocator> allocators;
    std::vector<Region> pending;
    uint64_t budget = 8 << 20, allocations = 0;
    bool allocate(uint64_t size, Region& out) override {
        for (uint32_t i = 0; i < blocks.size(); i++) {
            uint64_t o = allocators[i].allocate(size);
            if (o == RangeAllocator::kFail) continue;
            out = {i, o, size, blocks[i].data() + o};
            allocations++;
            return true;
        }
        if ((blocks.size() + 1) * kBlock > budget || size > kBlock) return false;
        blocks.emplace_back(kBlock);
        allocators.emplace_back(kBlock);
        return allocate(size, out);
    }
    void retire(const Region& r) override { pending.push_back(r); }
    void complete() {
        for (auto& r : pending) allocators[r.block].release(r.offset, r.size);
        pending.clear();
    }
    bool pending_has(const Region& r) const {
        for (auto& p : pending)
            if (p.block == r.block && p.offset == r.offset) return true;
        return false;
    }
};

// the draw path for a raw range: lookup, upload on a miss; returns the status
static Status use(Cache& c, uint32_t addr, uint32_t size, Entry** out = nullptr) {
    Entry* e;
    Status s = c.lookup({addr, kRaw, 0}, size, e);
    if (s == kMiss) assert(c.upload(*e, g_mem + addr, size));
    if (s != kBypass) assert(!memcmp(e->region.mapped, g_mem + addr, size));
    if (out) *out = e;
    return s;
}

static void allocator_tests() {
    RangeAllocator a(1024);
    uint64_t x = a.allocate(256), y = a.allocate(256), z = a.allocate(512);
    assert(x == 0 && y == 256 && z == 512 && a.used() == 1024);
    assert(a.allocate(1) == RangeAllocator::kFail);
    a.release(y, 256);
    assert(a.allocate(512) == RangeAllocator::kFail);  // only 256 free
    a.release(x, 256);                                  // coalesces with y's range
    assert(a.free_ranges() == 1 && a.allocate(512) == 0);
    a.release(0, 512);
    a.release(z, 512);
    assert(a.free_ranges() == 1 && a.used() == 0 && a.allocate(1024) == 0);
    // best fit: the smallest range that holds the request
    RangeAllocator b(4096);
    uint64_t p0 = b.allocate(1024), p1 = b.allocate(256), p2 = b.allocate(512), p3 = b.allocate(256);
    (void)p3;
    b.release(p0, 1024);
    b.release(p2, 512);
    assert(b.allocate(512) == p2);
    (void)p1;
    printf("allocator: ok\n");
}

int main() {
#ifdef _WIN32
    g_mem = (uint8_t*)VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    g_mem = (uint8_t*)mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_mem == (uint8_t*)MAP_FAILED) g_mem = nullptr;
#endif
    assert(g_mem);
    assert(wwatch::init(g_mem, kSize) && wwatch::active());
    wwatch::enable_hints();
    g_page = host_page();
    for (uint64_t i = 0; i < kSize; i++) g_mem[i] = (uint8_t)(i * 13 + 5);
    allocator_tests();

    TestBacking backing;
    Cache cache(backing);
    uint64_t frame = 1;
    cache.set_frame(frame);
    const uint32_t P = g_page;

    // 1. first use uploads, later uses hit without re-reading; a sub-range with the same start hits
    const uint32_t a = 0x100000;
    Entry* e;
    assert(use(cache, a, 3 * P, &e) == kMiss);
    Region first = e->region;
    assert(use(cache, a, 3 * P) == kHit && use(cache, a, 100) == kHit);
    assert(cache.stats.uploads == 1);
    printf("hit and sub-range: ok\n");

    // 2. a CPU write into the range (page fault) makes it stale; the old region is retired, not reused
    g_mem[a + P + 7] ^= 0x5A;
    assert(use(cache, a, 3 * P, &e) == kMiss);
    assert(e->region.offset != first.offset && backing.pending_has(first));
    assert(cache.stats.staleWrites == 1);
    backing.complete();
    assert(use(cache, a, 3 * P) == kHit);
    // a write on another page, outside the range, changes nothing
    g_mem[a + 5 * P] ^= 1;
    assert(use(cache, a, 3 * P) == kHit);
    printf("invalidation by write fault: ok\n");

    // 3. a longer request with the same start uploads the longer range (a grow, not a write)
    assert(use(cache, a, 4 * P) == kMiss && cache.stats.grows == 1);
    assert(use(cache, a, 4 * P) == kHit && use(cache, a, 3 * P) == kHit);
    printf("grow: ok\n");

    // 4. hints: DCFlushRange / GX2Invalidate of a range inside, outside; "everything" is ignored
    const uint32_t h = 0x200000;
    assert(use(cache, h, 2 * P) == kMiss);
    wwatch::hint(h + 2 * P + 16, 32);  // next page: outside
    assert(use(cache, h, 2 * P) == kHit);
    wwatch::hint(h + P + 16, 4);
    assert(use(cache, h, 2 * P) == kMiss);
    wwatch::hint(0, 0x10000000);
    assert(use(cache, h, 2 * P) == kHit);
    // hints do not touch the texture view of the stamps (written_since)
    uint64_t st = wwatch::arm(h, P);
    wwatch::hint(h, P);
    assert(!wwatch::written_since(h, P, st) && wwatch::changed_since(h, P, st));
    assert(use(cache, h, 2 * P) == kMiss);
    printf("invalidation by hint: ok\n");

    // 5. save-state load: everything is uploaded again
    assert(use(cache, a, 4 * P) == kHit && use(cache, h, 2 * P) == kHit);
    std::thread([&] { cache.invalidate_all(); }).join();  // any thread
    assert(use(cache, a, 4 * P) == kMiss && use(cache, h, 2 * P) == kMiss);
    assert(use(cache, a, 4 * P) == kHit && use(cache, h, 2 * P) == kHit);
    printf("invalidate all: ok\n");

    // 6. kernel writes bracketed by HostWrite (FSReadFile)
    const uint32_t k = 0x300000;
    assert(use(cache, k, 2 * P) == kMiss);
    {
        wwatch::HostWrite w(k + 100, 64);
        memset(g_mem + k + 100, 0xEE, 64);  // the kernel's write (pages are writable meanwhile)
    }
    assert(use(cache, k, 2 * P) == kMiss);
    assert(use(cache, k, 2 * P) == kHit);
    printf("host write: ok\n");

    // 7. partial overlap: two entries with different starts share a page; each is validated on its own
    const uint32_t o = 0x400000;
    assert(use(cache, o, 2 * P) == kMiss);          // pages 0, 1
    assert(use(cache, o + P + 64, 2 * P) == kMiss);  // pages 1, 2, 3
    g_mem[o + P + 200] ^= 3;                          // shared page 1
    assert(use(cache, o, 2 * P) == kMiss && use(cache, o + P + 64, 2 * P) == kMiss);
    g_mem[o + 10] ^= 3;                               // only the first
    assert(use(cache, o + P + 64, 2 * P) == kHit && use(cache, o, 2 * P) == kMiss);
    g_mem[o + 3 * P + 1] ^= 3;                        // only the second
    assert(use(cache, o, 2 * P) == kHit && use(cache, o + P + 64, 2 * P) == kMiss);
    // a range that starts inside another entry is its own entry with its own bytes
    assert(use(cache, o + 32, 64) == kMiss && use(cache, o + 32, 64) == kHit);
    printf("partial overlaps and sub-ranges: ok\n");

    // 8. derived data: the same guest range with other conversion parameters is another entry
    const uint32_t ix = 0x500000;
    uint32_t conv1[4] = {1, 2, 3, 4}, conv2[6] = {1, 2, 3, 1, 3, 4};
    Entry *c1, *c2;
    assert(cache.lookup({ix, kIndexConverted, 4, 0}, 16, c1) == kMiss && cache.upload(*c1, conv1, 16));
    assert(cache.lookup({ix, kIndexConverted, 6, 0}, 16, c2) == kMiss && cache.upload(*c2, conv2, 24));
    assert(c1 != c2);
    assert(cache.lookup({ix, kIndexConverted, 4, 0}, 16, c1) == kHit && !memcmp(c1->region.mapped, conv1, 16));
    assert(cache.lookup({ix, kIndexConverted, 4, 0xFFFF}, 16, c1) == kMiss);  // other restart marker
    assert(cache.lookup({ix, kRaw, 0}, 16, c1) == kMiss);  // raw bytes: yet another entry
    g_mem[ix + 3] ^= 1;
    assert(cache.lookup({ix, kIndexConverted, 6, 0}, 16, c2) == kMiss);
    printf("derived data keys: ok\n");

    // 9. a range written every frame becomes dynamic (bypass), is tried again after the back-off, and
    // the regions it used are all retired
    const uint32_t d = 0x600000;
    int bypassed = 0, misses = 0;
    for (int f = 0; f < 200; f++) {
        cache.set_frame(++frame);
        g_mem[d + 40] = (uint8_t)f;  // the game's per-frame write
        Status s = use(cache, d, 512);
        bypassed += s == kBypass;
        misses += s == kMiss;
        cache.end_frame(backing.budget);
        backing.complete();
    }
    assert(cache.stats.becameDynamic >= 1 && bypassed > 150 && misses < 20);
    printf("dynamic ranges: ok (%d bypassed, %d uploads in 200 frames)\n", bypassed, misses);

    // 10. eviction of idle entries; their regions are retired (freed after the fence)
    size_t before = cache.entries();
    cache.set_frame(frame += Cache::kIdleFrames + 100);
    assert(use(cache, a, 4 * P) == kHit);  // still used: kept
    cache.end_frame(backing.budget);
    assert(cache.entries() < before && cache.entries() >= 1);
    assert(use(cache, a, 4 * P) == kHit);
    backing.complete();
    // over budget: the least recently used regions go first
    for (uint32_t i = 0; i < 30; i++) {
        cache.set_frame(++frame);
        use(cache, 0x800000 + i * 0x40000, 0x30000);
    }
    cache.set_frame(++frame);
    cache.end_frame(1 << 20);
    assert(cache.resident_bytes() <= (1 << 20) / 4 * 3);
    assert(use(cache, 0x800000 + 29 * 0x40000, 0x30000) == kHit);  // the newest survived
    backing.complete();
    printf("eviction: ok\n");

    // 11. no memory: the caller falls back (kMiss, upload false)
    {
        TestBacking tiny;
        tiny.budget = 0;
        Cache noMem(tiny);
        Entry* t;
        assert(noMem.lookup({a, kRaw, 0}, 64, t) == kMiss && !noMem.upload(*t, g_mem + a, 64));
        assert(noMem.stats.bypassNoMemory == 1);
    }
    printf("no memory: ok\n");

    // 12. verify mode: a corrupted copy is a mismatch; a difference caused by a racing write is not
    const uint32_t v = 0xA00000;
    use(cache, v, P, &e);
    assert(use(cache, v, P, &e) == kHit);
    e->region.mapped[5] ^= 0xFF;  // what a missed invalidation looks like
    uint32_t diff = 0;
    assert(!cache.verify(*e, g_mem + v, P, &diff) && diff == 5 && cache.stats.verifyMismatches == 1);
    e->region.mapped[5] ^= 0xFF;
    g_mem[v + 9] ^= 0xFF;  // a write after the lookup: stamped, so "raced"
    assert(cache.verify(*e, g_mem + v, P) && cache.stats.verifyRaced == 1 && cache.stats.verifyMismatches == 1);
    // with keepShadow (the runtime's verify mode) the check uses the CPU copy, never the mapped bytes
    {
        TestBacking sb;
        Cache shadowed(sb);
        shadowed.keepShadow = true;
        shadowed.set_frame(frame);
        const uint32_t w = 0xA40000;
        Entry* s;
        use(shadowed, w, P, &s);
        assert(s->shadow.size() == P && !memcmp(s->shadow.data(), g_mem + w, P));
        s->region.mapped[3] ^= 0xFF;  // the GPU copy is not read in this mode
        assert(shadowed.verify(*s, g_mem + w, P) && shadowed.stats.verifyMismatches == 0);
        s->region.mapped[3] ^= 0xFF;
        s->shadow[7] ^= 0xFF;  // a stale copy (missed invalidation) is still caught
        uint32_t d = 0;
        assert(!shadowed.verify(*s, g_mem + w, P, &d) && d == 7 && shadowed.stats.verifyMismatches == 1);
    }
    printf("verify: ok\n");

    // 13. a concurrent writer (the game thread) against lookups (the render thread): every hit's bytes
    // equal guest memory unless a write raced the check; no mismatch may remain
    {
        const uint32_t base = 0x1000000, ranges = 64, len = 3000;
        std::atomic<bool> stop{false};
        std::thread writer([&] {
            std::mt19937 rng(7);
            while (!stop.load(std::memory_order_relaxed)) {
                // every other range is written: the rest share pages with written ones or not
                uint32_t r = (rng() % (ranges / 2)) * 2, off = rng() % len;
                g_mem[base + r * 0x2000 + off] = (uint8_t)rng();
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
        });
        std::mt19937 rng(11);
        Stats s0 = cache.stats;
        for (int i = 0; i < 200000; i++) {
            if (i % 1000 == 0) {
                cache.set_frame(++frame);
                cache.end_frame(backing.budget);
                backing.complete();
            }
            uint32_t addr = base + (rng() % ranges) * 0x2000;
            Entry* x;
            Status s = cache.lookup({addr, kRaw, 0}, len, x);
            if (s == kMiss) {
                std::vector<uint8_t> copy(g_mem + addr, g_mem + addr + len);  // read after arming
                cache.upload(*x, copy.data(), len);
            } else if (s == kHit) {
                cache.verify(*x, g_mem + addr, len);
            }
        }
        stop = true;
        writer.join();
        printf("concurrent writer: %llu hits verified, %llu raced, %llu uploads, %llu mismatches\n",
               (unsigned long long)(cache.stats.verifyChecks - s0.verifyChecks),
               (unsigned long long)(cache.stats.verifyRaced - s0.verifyRaced),
               (unsigned long long)(cache.stats.uploads - s0.uploads),
               (unsigned long long)(cache.stats.verifyMismatches - s0.verifyMismatches));
        assert(cache.stats.verifyMismatches == s0.verifyMismatches);
    }

    cache.clear();
    backing.complete();
    for (auto& al : backing.allocators) assert(al.used() == 0);
    printf("all regions freed: ok\nbuffer cache test passed\n");
    return 0;
}
