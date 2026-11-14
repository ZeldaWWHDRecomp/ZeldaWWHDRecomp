// Descriptor tokens: record_tokens.h
#ifdef WWHD_NATIVE_DRAW
#include "record_tokens.h"
#include "record_mt.h"
#include "record_worker.h"
#include "pod_commands.h"
#include "backend.h"
#include "runtime.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gfxvk::record_tokens {
namespace {
constexpr uint32_t kSlots = 16;  // submission_pool.h kMaxSlots
constexpr uint64_t kTag = 0xA5C3;
static_assert(sizeof(VkDescriptorSet) == sizeof(uint64_t));
uint64_t bits_of(VkDescriptorSet s) { uint64_t v; std::memcpy(&v, &s, sizeof v); return v; }
VkDescriptorSet set_of(uint64_t v) { VkDescriptorSet s; std::memcpy(&s, &v, sizeof s); return s; }
bool tagged(VkDescriptorSet s) { return (bits_of(s) >> 48) == kTag; }
[[noreturn]] void fatal(const char* what) {
  LOG("[vulkan record tokens] FATAL: %s", what);
  std::fprintf(stderr, "[vulkan record tokens] FATAL: %s\n", what);
  std::abort();
}
// render thread
struct Producer {
  std::array<uint8_t, kSlots> generation{};
  uint32_t slot = UINT32_MAX;
  uint32_t next = 0;
} P;
// emitter (the render thread only while the emitter is drained)
struct EmitterSlot {
  uint8_t generation = 0;
  std::vector<VkDescriptorPool> pools;
  size_t current = 0;  // pools [0, current] may hold sets
  std::vector<VkDescriptorSet> sets;
};
std::array<EmitterSlot, kSlots> E;
PFN_vkCmdBindDescriptorSets realBind = nullptr;
struct Stats { std::atomic<uint64_t> defined{0}, inlineDefines{0}, refused{0}, translatedBinds{0}, producerTranslations{0},
                                     poolGrows{0}, emitterNs{0}; } stats;
// the DefineSet payload: Header, then Header::count entries
struct Header {
  uint64_t token;
  VkDevice device;
  VkDescriptorSetLayout layout;
  uint32_t count, pad;
};
struct Entry {
  uint32_t binding, element, type, isBuffer;
  VkDescriptorBufferInfo buffer;  // isBuffer
  VkDescriptorImageInfo image;    // else
};
constexpr uint32_t kMaxEntries = 64;
VkDescriptorPool create_pool(VkDevice device) {
  // a quarter of the submission pool (backend.cpp try_initialize_submission); the chain grows when one is full
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8192},
                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384}};
  VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  info.maxSets = 8192;
  info.poolSizeCount = uint32_t(std::size(sizes));
  info.pPoolSizes = sizes;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  vk_check(vkCreateDescriptorPool(device, &info, nullptr, &pool), "record emitter descriptor pool");
  return pool;
}
VkDescriptorSet allocate(EmitterSlot& s, VkDevice device, VkDescriptorSetLayout layout) {
  for (;;) {
    bool fresh = false;
    if (s.current == s.pools.size()) {
      s.pools.push_back(create_pool(device));
      fresh = true;
    }
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = s.pools[s.current];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult r = vkAllocateDescriptorSets(device, &ai, &set);
    if (r == VK_SUCCESS) {
      if (tagged(set)) fatal("a driver descriptor set carries the token tag");
      return set;
    }
    if ((r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) || fresh) vk_check(r, "record emitter descriptor set");
    ++s.current;
    ++stats.poolGrows;
  }
}
void define_now(const Header& h, const Entry* entries) {
  const auto t0 = std::chrono::steady_clock::now();
  const uint64_t v = h.token;
  const uint32_t slot = uint32_t(v >> 40) & 0xFF, index = uint32_t(v);
  if (slot >= kSlots) fatal("token slot out of range");
  auto& s = E[slot];
  if (s.generation != uint8_t(v >> 32)) fatal("DefineSet for a slot generation the emitter has not reset to");
  const VkDescriptorSet set = allocate(s, h.device, h.layout);
  std::array<VkWriteDescriptorSet, kMaxEntries> writes;
  for (uint32_t i = 0; i < h.count; ++i) {
    auto& w = writes[i];
    w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = entries[i].binding;
    w.dstArrayElement = entries[i].element;
    w.descriptorCount = 1;
    w.descriptorType = VkDescriptorType(entries[i].type);
    if (entries[i].isBuffer) w.pBufferInfo = &entries[i].buffer;
    else w.pImageInfo = &entries[i].image;
  }
  if (h.count) vkUpdateDescriptorSets(h.device, h.count, writes.data(), 0, nullptr);
  if (index >= s.sets.size()) s.sets.resize(size_t(index) + 1, VK_NULL_HANDLE);
  s.sets[index] = set;
  stats.emitterNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - t0).count());
}
VkDescriptorSet resolve(VkDescriptorSet token) {
  const uint64_t v = bits_of(token);
  const uint32_t slot = uint32_t(v >> 40) & 0xFF, index = uint32_t(v);
  if (slot >= kSlots) fatal("token slot out of range");
  const auto& s = E[slot];
  if (s.generation != uint8_t(v >> 32)) fatal("stale descriptor token (slot reused)");
  if (index >= s.sets.size() || !s.sets[index]) fatal("descriptor token bound before its DefineSet");
  return s.sets[index];
}
VKAPI_ATTR void VKAPI_CALL bind(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first,
                                uint32_t count, const VkDescriptorSet* sets, uint32_t offsetCount,
                                const uint32_t* offsets) {
  bool any = false;
  for (uint32_t i = 0; i < count && !any; ++i) any = tagged(sets[i]);
  if (!any) return realBind(cmd, point, layout, first, count, sets, offsetCount, offsets);
  const bool emitter = record_mt::on_emitter();
  if (!emitter) {
    ++stats.producerTranslations;
    if (record_mt::check() && !record_worker_idle()) fatal("token translated on the render thread while the emitter runs");
  }
  std::array<VkDescriptorSet, 8> local;
  std::vector<VkDescriptorSet> large;
  VkDescriptorSet* out = local.data();
  if (count > local.size()) { large.resize(count); out = large.data(); }
  for (uint32_t i = 0; i < count; ++i) out[i] = tagged(sets[i]) ? resolve(sets[i]) : sets[i];
  if (record_mt::check())
    for (uint32_t i = 0; i < count; ++i)
      if (tagged(out[i])) fatal("a descriptor token reached vkCmdBindDescriptorSets");
  ++stats.translatedBinds;
  realBind(cmd, point, layout, first, count, out, offsetCount, offsets);
}
}  // namespace
bool active() {
  return P.slot != UINT32_MAX && gfxvk::vkCmdBindDescriptorSets == &bind && record_mt::on(record_mt::kTokens) &&
         record_worker_commands();
}
VkDescriptorSet define(VkDescriptorSetLayout layout, const VkWriteDescriptorSet* writes, uint32_t count) {
  if (count > kMaxEntries) { ++stats.refused; return VK_NULL_HANDLE; }
  struct Payload { Header h; Entry e[kMaxEntries]; } p;
  std::memset(&p, 0, sizeof(Header) + count * sizeof(Entry));  // (no padding bytes of the stack in the stream)
  for (uint32_t i = 0; i < count; ++i) {
    const auto& w = writes[i];
    if (w.descriptorCount != 1 || w.pTexelBufferView || w.pNext || (!w.pBufferInfo && !w.pImageInfo)) {
      ++stats.refused;
      return VK_NULL_HANDLE;
    }
    auto& e = p.e[i];
    e.binding = w.dstBinding;
    e.element = w.dstArrayElement;
    e.type = uint32_t(w.descriptorType);
    e.isBuffer = w.pBufferInfo != nullptr;
    if (e.isBuffer) e.buffer = *w.pBufferInfo;
    else e.image = *w.pImageInfo;
  }
  const uint32_t index = P.next++;
  p.h.token = kTag << 48 | uint64_t(P.slot) << 40 | uint64_t(P.generation[P.slot]) << 32 | index;
  p.h.device = R.device;
  p.h.layout = layout;
  p.h.count = count;
  const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(&p), sizeof(Header) + count * sizeof(Entry)};
  auto* stream = record_worker_commands();
  if (record_mt::append(*stream, record_mt::Ext::DefineSet, bytes)) {
    ++stats.defined;
  } else {
    drain_record_commands(fallback_stats::Boundary::EncoderFallback, 31, stream->failure_reason());
    define_now(p.h, p.e);  // the emitter is drained: the render thread defines it
    ++stats.inlineDefines;
  }
  return set_of(p.h.token);
}
bool slot_activated(uint32_t index, uint8_t& generation) {
  if (!record_mt::on(record_mt::kTokens) || gfxvk::vkCmdBindDescriptorSets != &bind || index >= kSlots) {
    P.slot = UINT32_MAX;
    return false;
  }
  P.slot = index;
  P.next = 0;
  generation = ++P.generation[index];
  return true;
}
void reset_slot(uint32_t index, uint8_t generation, VkDevice device) {
  auto& s = E[index];
  for (size_t i = 0; i <= s.current && i < s.pools.size(); ++i)
    vk_check(vkResetDescriptorPool(device, s.pools[i], 0), "reset record emitter descriptor pool");
  s.current = 0;
  s.sets.clear();
  s.generation = generation;
}
void replay_define(const std::byte* bytes, size_t size, bool execute) {
  Header h;
  if (size < sizeof h) throw std::runtime_error("truncated POD DefineSet");
  std::memcpy(&h, bytes, sizeof h);
  if (h.count > kMaxEntries || size != sizeof h + h.count * sizeof(Entry)) throw std::runtime_error("bad POD DefineSet");
  if (!execute) return;
  std::array<Entry, kMaxEntries> entries;
  if (h.count) std::memcpy(entries.data(), bytes + sizeof h, h.count * sizeof(Entry));
  define_now(h, entries.data());
}
void install(VkDevice) {
  if (!(record_mt::bits() & record_mt::kTokens) || gfxvk::vkCmdBindDescriptorSets == &bind) return;
  realBind = gfxvk::vkCmdBindDescriptorSets;
  gfxvk::vkCmdBindDescriptorSets = &bind;
  LOG("[vulkan record tokens] on: draw descriptor sets are allocated and written on the record worker");
}
void report(double frames, char* out, size_t outSize) {
  if (!(record_mt::bits() & record_mt::kTokens)) return;
  auto take = [](std::atomic<uint64_t>& v) { return double(v.exchange(0, std::memory_order_relaxed)); };
  const double d = take(stats.defined), i = take(stats.inlineDefines), r = take(stats.refused),
               t = take(stats.translatedBinds), pt = take(stats.producerTranslations), g = take(stats.poolGrows),
               ns = take(stats.emitterNs);
  std::snprintf(out, outSize,
                "; tokens: defined %.1f/frame (%.3f ms/frame on the emitter), inline %.2f, refused %.2f, translated "
                "binds %.1f/frame (%.2f on the render thread), pool grows %.2f/frame",
                d / frames, ns / 1e6 / frames, i / frames, r / frames, t / frames, pt / frames, g / frames);
}
}  // namespace gfxvk::record_tokens
#endif
