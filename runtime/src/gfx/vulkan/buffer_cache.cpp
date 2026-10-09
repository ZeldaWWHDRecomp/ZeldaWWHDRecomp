// Vulkan guest buffer cache: GPU memory, retirement, switches and statistics (design:
// buffer_cache_core.h).
#include "buffer_cache.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "backend.h"
#include "render_prof.h"
#include "runtime.h"
#include "write_watch.h"

namespace gfxvk {
namespace {

bool env_is(const char* name, const char* value) {
  const char* e = std::getenv(name);
  return e && !std::strcmp(e, value);
}

// Requests above this are not cached (a block must hold them; such ranges are rare and unbounded
// draws with a garbage size register would pin memory)
constexpr uint32_t kMaxCachedRange = 8u << 20;
constexpr VkDeviceSize kBlockSize = 32ull << 20;

// Host-visible blocks, preferably device-local (unified memory, resizable BAR or the 256 MiB BAR
// window): the CPU writes a changed range once and draws read it from video memory.
struct VulkanBacking final : bufcache::Backing {
  std::vector<Buffer> blocks;
  std::vector<bufcache::RangeAllocator> allocators;
  VkMemoryPropertyFlags flags = 0;
  uint64_t budget = 256ull << 20;
  bool exhausted = false;
  bool allocate(uint64_t size, bufcache::Region& out) override {
    for (uint32_t i = 0; i < blocks.size(); ++i) {
      const uint64_t offset = allocators[i].allocate(size);
      if (offset == bufcache::RangeAllocator::kFail) continue;
      out = {i, offset, size, static_cast<uint8_t*>(blocks[i].mapped) + offset};
      return true;
    }
    const VkDeviceSize blockSize = std::max<VkDeviceSize>(kBlockSize, size);
    uint64_t total = 0;
    for (auto& b : blocks) total += b.size;
    if (total + blockSize > budget) return false;
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (exhausted) return false;
    Buffer b;
    try {
      b = create_buffer(blockSize, usage, flags);
    } catch (const std::exception& e) {
      // device-local host-visible memory may not take buffers of this usage, or its heap is full
      const bool retry = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
      LOG("[vulkan buffer cache] block allocation failed (%s)%s", e.what(),
          retry ? ": using host-visible memory" : ": no more blocks");
      if (!retry) { exhausted = true; return false; }
      flags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      return allocate(size, out);
    }
    blocks.push_back(b);
    allocators.emplace_back(b.size);
    return allocate(size, out);
  }
  void retire(const bufcache::Region& region) override { R.garbageCacheRegions.push_back(region); }
  void release(const bufcache::Region& region) {
    if (region.block < allocators.size()) allocators[region.block].release(region.offset, region.size);
  }
};

struct State {
  VulkanBacking backing;
  bufcache::Cache cache{backing};
  bufcache::Stats reported;
  uint64_t verifyLogged = 0;
};
// created on the render thread at the first cached draw; read by invalidate_all on any thread
std::atomic<State*> g_state{nullptr};

State& state() {
  if (State* s = g_state.load(std::memory_order_acquire)) return *s;
  State* created = new State;  // lives as long as the device (never torn down, like the upload arena)
  auto& b = created->backing;
  if (const char* mb = std::getenv("WWHD_VK_BUFFER_CACHE_MB"))
    b.budget = std::max<uint64_t>(std::strtoull(mb, nullptr, 10), 64) << 20;
  VkPhysicalDeviceMemoryProperties p;
  vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
  const VkMemoryPropertyFlags hostFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  b.flags = hostFlags;
  for (uint32_t i = 0; i < p.memoryTypeCount; ++i) {
    const auto f = p.memoryTypes[i].propertyFlags;
    if ((f & (hostFlags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) != (hostFlags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      continue;
    // a small BAR window (no resizable BAR) is shared with the driver: use at most half of it
    const uint64_t heap = p.memoryHeaps[p.memoryTypes[i].heapIndex].size;
    b.flags = hostFlags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (heap < (1ull << 30)) b.budget = std::min<uint64_t>(b.budget, heap / 2);
    break;
  }
  created->cache.keepShadow = buffer_cache_verify();  // verify() then never reads GPU memory
  LOG("[vulkan buffer cache] on%s: budget %llu MiB, %s memory, hints %s", buffer_cache_verify() ? " (verify mode)" : "",
      (unsigned long long)(b.budget >> 20),
      (b.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "device-local host-visible" : "host-visible",
      env_is("WWHD_VK_BUFFER_CACHE_HINTS", "0") ? "off" : "on");
  // WWHD_VK_BUFFER_CACHE_LOG=1: log the first 300 ranges that become dynamic
  if (env_is("WWHD_VK_BUFFER_CACHE_LOG", "1"))
    created->cache.onDynamic = [](const bufcache::Key& k, const bufcache::Entry& e, bool hintOnly) {
      static int logged = 0;
      if (logged++ < 300)
        LOG("[vulkan buffer cache] dynamic: kind %u at %08X, %u bytes, last invalidation by %s", k.kind, k.addr, e.size,
            hintOnly ? "hint" : "write");
    };
  g_state.store(created, std::memory_order_release);
  return *created;
}

}  // namespace

bool buffer_cache_verify() {
  static const bool verify = env_is("WWHD_VK_BUFFER_CACHE_VERIFY", "1");
  return verify;
}

// On by default on macOS (verified there: 0 mismatches in verify mode over long gameplay runs), on
// desktop Linux (Steam Deck included; an RK3588 report in issue #50 went from a 20 fps lock to full speed
// with it) and on Android (issue #56: Galaxy S25 Ultra, 115.8 M verify checks with 0 mismatches and 0
// protect failures; heavy Outset views 33.8 -> 47.0 presented fps, render thread 14.2 -> 10.4 ms/frame,
// uploads 21.7 -> 10.1 MiB/frame) and on Windows (issue #91: RX 6700 XT, render thread 13-19% less,
// uploads ~20 -> ~10 MiB/frame, no geometry problems over many sessions; the verify mode itself is
// unusable there, it reads back non-host-cached memory). WWHD_VK_BUFFER_CACHE=0|1 overrides it.
constexpr bool kBufferCacheDefault = true;

bool buffer_cache_enabled() {
  static const bool enabled = [] {
    bool want = kBufferCacheDefault || env_is("WWHD_VK_BUFFER_CACHE", "1") || buffer_cache_verify();
    if (env_is("WWHD_VK_BUFFER_CACHE", "0")) want = false;
    if (!want) return false;
    if (!wwatch::active()) {
      LOG("[vulkan buffer cache] off: page write tracking is unavailable");
      return false;
    }
    if (!env_is("WWHD_VK_BUFFER_CACHE_HINTS", "0")) wwatch::enable_hints();
    return true;
  }();
  return enabled;
}

bufcache::Cache& buffer_cache() { return state().cache; }

UploadSlice buffer_cache_slice(const bufcache::Entry& e, uint64_t size) {
  const auto& b = state().backing.blocks[e.region.block];
  return UploadSlice{b.buffer, e.region.offset, size, e.region.mapped};
}

void buffer_cache_mismatch(const char* what, const bufcache::Entry& e, uint32_t size, uint32_t firstDiff) {
  auto& s = state();
  if (s.verifyLogged++ < 64)
    LOG("[vulkan buffer cache] VERIFY MISMATCH %s at %08X: %u bytes requested (entry %u bytes, uploaded frame %llu, "
        "now %llu), first difference at +%u",
        what, e.addr, size, e.size, (unsigned long long)e.uploadFrame, (unsigned long long)s.cache.frame(), firstDiff);
}

bool cached_guest_range(uint32_t addr, uint32_t size, int uploadKind, UploadSlice& out) {
  if (!addr || size < 16 || size > kMaxCachedRange || uint64_t(addr) + size > 0x100000000ull) return false;
  auto& cache = state().cache;
  bufcache::Entry* e;
  switch (cache.lookup({addr, bufcache::kRaw, 0}, size, e)) {
  case bufcache::kHit:
    if (buffer_cache_verify()) {
      uint32_t diff = 0;
      if (!cache.verify(*e, mem::ptr(addr), size, &diff)) buffer_cache_mismatch("range", *e, size, diff);
    }
    break;
  case bufcache::kMiss:
    if (!cache.upload(*e, mem::ptr(addr), size)) return false;
    rprof::add_upload(rprof::Upload(uploadKind), size);
    break;
  case bufcache::kBypass: return false;
  }
  out = buffer_cache_slice(*e, size);
  return true;
}

bool cached_native_indices(uint32_t addr, uint32_t size, UploadSlice& out, bufcache::Entry*& entry) {
  if (!addr || !size || size > kMaxCachedRange || uint64_t(addr) + size > 0x100000000ull) return false;
  auto& cache = state().cache;
  bufcache::Entry* e;
  switch (cache.lookup({addr, bufcache::kIndexNative, 0}, size, e)) {
  case bufcache::kHit:
    if (buffer_cache_verify()) {
      uint32_t diff = 0;
      if (!cache.verify(*e, mem::ptr(addr), size, &diff)) buffer_cache_mismatch("indices", *e, size, diff);
    }
    break;
  case bufcache::kMiss: {
    // one read of guest memory: the shadow (extent scans) and the region get the same bytes
    std::vector<uint8_t> bytes(mem::ptr(addr), mem::ptr(addr) + size);
    if (!cache.upload(*e, bytes.data(), size)) return false;
    e->shadow = std::move(bytes);
    rprof::add_upload(rprof::kUpIndex, size);
    break;
  }
  case bufcache::kBypass: return false;
  }
  out = buffer_cache_slice(*e, size);
  entry = e;
  return true;
}

void buffer_cache_end_frame() {
  State* s = g_state.load(std::memory_order_acquire);
  if (!s) return;
  s->cache.set_frame(R.frame);
  s->cache.end_frame(s->backing.budget);
}

void buffer_cache_invalidate_all() {
  if (State* s = g_state.load(std::memory_order_acquire)) s->cache.invalidate_all();
}

void buffer_cache_guest_invalidate(uint32_t flags, uint32_t addr, uint32_t size) {
  // GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER (1), UNIFORM_BLOCK (4); in command order on the render thread
  if ((flags & 5) && g_state.load(std::memory_order_acquire)) wwatch::hint(addr, size);
}

void buffer_cache_free(const std::vector<bufcache::Region>& regions) {
  State* s = g_state.load(std::memory_order_acquire);
  if (!s) return;
  for (auto& r : regions) s->backing.release(r);
}

void buffer_cache_report(double frames) {
  State* state = g_state.load(std::memory_order_acquire);
  if (!state || frames <= 0) return;
  auto& s = *state;
  const auto& c = s.cache.stats;
  auto& o = s.reported;
  uint64_t hintCalls, hintBytes;
  wwatch::take_hint_stats(hintCalls, hintBytes);
  const double lookups = double(c.lookups - o.lookups);
  LOG("[vulkan buffer cache] %.0f lookups/frame, %.1f%% hits; uploads %.1f/frame %.3f MiB/frame; stale by writes %.1f/frame "
      "(hint only %.1f), "
      "grows %.1f/frame; bypass dynamic %.1f/frame, no memory %.1f/frame, became dynamic %llu; evictions %llu; "
      "%zu entries, %.1f MiB resident; hints %.1f/frame %.3f MiB/frame; protect failures %llu",
      lookups / frames, lookups ? 100.0 * double(c.hits - o.hits) / lookups : 0.0, (c.uploads - o.uploads) / frames,
      (c.uploadBytes - o.uploadBytes) / frames / (1 << 20), (c.staleWrites - o.staleWrites) / frames,
      (c.staleHintOnly - o.staleHintOnly) / frames,
      (c.grows - o.grows) / frames, (c.bypassDynamic - o.bypassDynamic) / frames,
      (c.bypassNoMemory - o.bypassNoMemory) / frames, (unsigned long long)(c.becameDynamic - o.becameDynamic),
      (unsigned long long)(c.evictions - o.evictions), s.cache.entries(), s.cache.resident_bytes() / double(1 << 20),
      hintCalls / frames, hintBytes / frames / (1 << 20), (unsigned long long)wwatch::protect_failures());
  if (buffer_cache_verify())
    LOG("[vulkan buffer cache] verify: %llu checks, %llu mismatches, %llu raced writes (total %llu checks, %llu mismatches)",
        (unsigned long long)(c.verifyChecks - o.verifyChecks), (unsigned long long)(c.verifyMismatches - o.verifyMismatches),
        (unsigned long long)(c.verifyRaced - o.verifyRaced), (unsigned long long)c.verifyChecks,
        (unsigned long long)c.verifyMismatches);
  o = c;
}

}  // namespace gfxvk
