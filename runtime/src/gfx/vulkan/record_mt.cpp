// MT recording stage 1: record_mt.h
#ifdef WWHD_VK_SMOOTH
#include "record_mt.h"
#include "record_tokens.h"
#include "pipeline_future.h"
#include "record_worker.h"
#include "pod_commands.h"
#include "pool_reset.h"
#include "smooth_defaults.h"
#include "backend.h"
#include "runtime.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace gfxvk::record_mt {
namespace {
thread_local bool emitterThread = false;
std::unordered_map<const void*, VkCommandPool> deferredResets;  // render thread: slot -> its command pool
std::mutex freeMutex;
std::vector<std::vector<std::byte>> freeBytes;  // replayed byte vectors (capacity kept)
constexpr size_t kMaxFree = 8;
constexpr size_t kPassPublishBytes = 8 * 1024;
Counters stats;
struct SlotReset {
  uint32_t index;
  uint8_t generation, tokens, pad[2];
  VkDevice device;
  VkCommandPool commands;
};
void slot_reset(const SlotReset& s) {
  if (s.commands) vk_check(vkResetCommandPool(s.device, s.commands, 0), "reset command pool (record emitter)");
  if (s.tokens) record_tokens::reset_slot(s.index, s.generation, s.device);
}
}  // namespace
unsigned bits() {
  static const unsigned value = [] {
    unsigned v = 0;
    if (const char* e = std::getenv("WWHD_VK_RECORD_MT")) v = !std::strcmp(e, "all") ? 127u : unsigned(std::atoi(e)) & 127u;
    else v = smooth_defaults::record_mt();  // (smooth_defaults.h: phones and the Steam Deck)
    if (const char* e = std::getenv("WWHD_VK_RECORD_DESCRIPTORS"); e && *e == '1') v |= kTokens;
    if (const char* e = std::getenv("WWHD_VK_RECORD_PIPELINE_FUTURES"); e && *e == '1') v |= kFutures;
    LOG("[vulkan record mt] WWHD_VK_RECORD_MT=%u: emitter pool reset %u, publish at pass cuts %u, recycled buffers %u, "
        "native present default %u, descriptor tokens %u, pipeline futures %u, no draw-batch submissions %u; spin %u us; "
        "check %u (with the record worker only)", v, v & 1, (v >> 1) & 1, (v >> 2) & 1, (v >> 3) & 1, (v >> 4) & 1,
        (v >> 5) & 1, (v >> 6) & 1, spin_us(),
        unsigned(check()));
    return v;
  }();
  return value;
}
bool on(unsigned bit) { return (bits() & bit) && record_worker_active(); }
unsigned spin_us() {
  static const unsigned value = [] {
    const char* e = std::getenv("WWHD_VK_RECORD_SPIN_US");
    return e && *e ? unsigned(std::atoi(e)) : 30u;
  }();
  return value;
}
bool check() {
  static const bool value = [] {
    const char* e = std::getenv("WWHD_VK_RECORD_CHECK");
    return e && *e == '1';
  }();
  return value;
}
void emitter_started() { emitterThread = true; }
bool on_emitter() { return emitterThread; }

bool defer_command_pool_reset(const void* slot, VkCommandPool commands) {
  // (not for a slot the WWHD_VK_ASYNC_POOL_RESET reset thread owns: one the render thread submitted itself; the record
  // worker's submissions are left to the emitter)
  if (!on(kPoolReset) || pool_reset::queued(slot)) return false;
  deferredResets[slot] = commands;
  return true;
}
void activated(const void* slot, uint32_t index, VkDevice device) {
  SlotReset s{index, 0, 0, {}, device, VK_NULL_HANDLE};
  if (auto it = deferredResets.find(slot); it != deferredResets.end()) {
    s.commands = it->second;
    deferredResets.erase(it);
  }
  s.tokens = record_tokens::slot_activated(index, s.generation);
  if (!s.commands && !s.tokens) return;
  if (auto* stream = record_worker_commands()) {
    if (append(*stream, Ext::SlotReset, {reinterpret_cast<const std::byte*>(&s), sizeof s})) {
      if (s.commands) ++stats.cmdResetsQueued;
      return;
    }
    drain_record_commands(fallback_stats::Boundary::EncoderFallback, 31, stream->failure_reason());
  }
  // no stream records (the producer presentation scope drained the emitter, or the worker stopped): here
  slot_reset(s);
  if (s.commands) ++stats.cmdResetsInline;
}
void pass_ended() {
  if (!on(kPassPublish)) return;
  if (auto* stream = record_worker_commands(); stream && stream->payload_bytes() >= kPassPublishBytes) {
    publish_record_commands();
    ++stats.passPublishes;
  }
}
std::vector<std::byte> recycled_bytes() {
  std::lock_guard lock(freeMutex);
  if (freeBytes.empty()) return {};
  auto v = std::move(freeBytes.back());
  freeBytes.pop_back();
  ++stats.recycled;
  return v;
}
void recycle(std::vector<std::byte>&& bytes) {
  bytes.clear();
  std::lock_guard lock(freeMutex);
  if (freeBytes.size() < kMaxFree) freeBytes.push_back(std::move(bytes));
}
bool native_present_default() { return bits() & kNativePresent; }

bool append(PodCommands& stream, Ext op, std::span<const std::byte> payload) {
  std::byte buffer[4608];  // (a DefineSet of up to 64 descriptors)
  std::vector<std::byte> large;
  std::byte* out = buffer;
  const size_t size = sizeof(uint32_t) + payload.size();
  if (size > sizeof buffer) {
    try { large.resize(size); } catch (const std::bad_alloc&) { return false; }
    out = large.data();
  }
  const uint32_t code = uint32_t(op);
  std::memcpy(out, &code, sizeof code);
  if (!payload.empty()) std::memcpy(out + sizeof code, payload.data(), payload.size());
  return stream.extension({out, size});
}
void replay_extension(const std::byte* bytes, size_t size, bool execute) {
  uint32_t code = 0;
  if (size < sizeof code) throw std::runtime_error("truncated POD extension");
  std::memcpy(&code, bytes, sizeof code);
  bytes += sizeof code;
  size -= sizeof code;
  switch (Ext(code)) {
  case Ext::SlotReset: {
    SlotReset s;
    if (size != sizeof s) throw std::runtime_error("bad POD slot reset");
    std::memcpy(&s, bytes, sizeof s);
    if (execute) slot_reset(s);
    break;
  }
  case Ext::DefineSet: record_tokens::replay_define(bytes, size, execute); break;
  case Ext::BindFuture: pipeline_future::replay(bytes, size, execute); break;
  default: throw std::runtime_error("unknown POD extension");
  }
}
Counters& counters() { return stats; }
bool skip_draw_batch() {
  if (!on(kNoBatchSubmit)) return false;
  stats.batchSkips.fetch_add(1, std::memory_order_relaxed);
  return true;
}
void report(double frames) {
  if (!bits() || !record_worker_active()) return;
  char tokens[256] = "", futures[256] = "";
  record_tokens::report(frames, tokens, sizeof tokens);
  pipeline_future::report(frames, futures, sizeof futures);
  auto take = [](std::atomic<uint64_t>& v) { return double(v.exchange(0, std::memory_order_relaxed)); };
  const double queued = take(stats.cmdResetsQueued), inl = take(stats.cmdResetsInline), pub = take(stats.passPublishes),
               rec = take(stats.recycled), ws = take(stats.waitSpins), wh = take(stats.waitSpinHits),
               is = take(stats.idleSpins), ih = take(stats.idleSpinHits), dr = take(stats.drains),
               ef = take(stats.encoderFallbacks), bs = take(stats.batchSkips);
  LOG("[vulkan record mt] command-pool resets on the emitter %.2f/frame, inline %.2f/frame; pass-cut publishes "
      "%.2f/frame; recycled buffers %.2f/frame; producer waits spun %.2f/frame (%.2f done in the spin); emitter idle "
      "spins %.2f/frame (%.2f found work); drains %.2f/frame, encoder fallbacks %.2f/frame; draw-batch submissions "
      "skipped %.2f/frame%s%s",
      queued / frames, inl / frames, pub / frames, rec / frames, ws / frames, wh / frames, is / frames, ih / frames,
      dr / frames, ef / frames, bs / frames, tokens, futures);
}
}  // namespace gfxvk::record_mt
#endif
