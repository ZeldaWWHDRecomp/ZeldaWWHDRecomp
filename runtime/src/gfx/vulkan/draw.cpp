#include "gamepad_backdrop.h"
#include "gamepad_backdrop_blend.h"
#include "gamepad_backdrop_replay.h"
#include "gx2/gx2_regs.h"
#include "vertex_cpu_policy.h"
#include <set>
#include <tuple>
#include "pass.h"
#include "../../exception_report.h"
#include "../../render_objects.h"
#include "aspect.h"
#include "aspect_panes.h"
#include "../renderer.h"
// Vulkan draw submission. Guest state conventions follow Cemu (MPL-2.0).
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "backend.h"
#include "dynres.h"
#include "stereo.h"
#include "../stereo_projection.h"
#include "../sampler_lod_bias.h"
#include "../artistic_mips.h"
#include "sampler_filtering.h"
#include "deferred_dispatch.h"
#include "buffer_cache.h"
#include "gx2/state_groups.h"
#include "vertex_cpu_decode.h"
#include "render_prof.h"
#include "background_shaders.h"
#include "runtime.h"
#include "../../interp.h"
#include "gx2/gx2.h"
#include "gx2/gx2_cmd.h"
#include "shaders.h"
#include "clip_emulation.h"
#include "shader_binary.h"
#include "stutter_trace.h"
#include "../../dsweep.h"
#include "../../pipeline_groups.h"
#include "pipeline_state.h"
#include "pipeline_worker.h"
#include "pipeline_demand.h"
#include "pipeline_failure.h"
#include "settings.h"
#include "vertex_formats.h"
#include "uniform_snapshot.h"
#include "index_conversion.h"
#include "vertex_history.h"
#include "vertex_snapshot_history.h"
#include "pass_reuse.h"
#include "hud_coverage.h"
#include "reproj.h"
#ifdef WWHD_NATIVE_DRAW
#include "gx2/native_draw.h"
#include "gx2/native_pass.h"
#include "framegen.h"
#include "cloud_proto.h"
#include "mods/cemu_pack.h"
#include <memory>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <unordered_map>
extern "C" uint64_t g_shader_state_gen;
using namespace Latte;
namespace gfxvk {
// diagnostics (the [vulkan pipeline stalls] line of the performance report): pipelines compiled on the
// render thread (no async ticket, or async off), render-thread waits for a pending compile, and
// background-lane requests / admissions / declines (the pass subset's masked variants)
uint64_t g_pipelineSyncCompiles = 0, g_pipelineRenderWaits = 0, g_pipelineBudgetWaits = 0;
uint64_t g_backgroundRequests = 0, g_backgroundAdmitted = 0, g_backgroundDeclined = 0;
uint64_t g_backgroundWaits = 0;  // waits for a background variant: must stay 0 (the shutdown drain aside)
bool g_drainingPipelines = false;  // drain_pending_guest_pipelines (shutdown): its waits aren't counted
namespace {
bool preparation_stats_enabled() {
  static const bool enabled=std::getenv("WWHD_VK_STATS")!=nullptr;
  return enabled;
}
// Bounded overlap: the default on every platform (measured on macOS and Windows); an explicit
// zero/invalid WWHD_VK_DRAW_BATCH disables it.
uint32_t parse_draw_batch(const char* text) {
  if (!text) return 2048;
  if (!*text) return 0;
  uint32_t value = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return 0;
    uint32_t digit = uint32_t(*p - '0');
    if (value > (1048576u - digit) / 10) return 0;
    value = value * 10 + digit;
  }
  return value;
}
uint32_t parse_draw_batch_cap(const char* text) {
  if (!text) return 3;
  // Explicit invalid caps retain the original conservative fallback.
  const uint32_t value = parse_draw_batch(text);
  return value >= 1 && value <= 3 ? value : 2;
}
struct DrawBatchState {
  uint64_t frame = ~uint64_t{0};
  uint32_t draws = 0, submissions = 0;
  bool after_draw(uint64_t currentFrame, uint32_t batch, uint32_t cap) {
    if (frame != currentFrame) {
      frame = currentFrame;
      draws = submissions = 0;
    }
    if (!batch || submissions >= cap) return false;
    if (++draws < batch) return false;
    draws = 0;
    ++submissions;
    return true;
  }
};
DrawBatchState drawBatchState;
uint64_t drawBatchSubmissions = 0;
bool aoPrivateReplay = false;
struct AoPrivate {
  uint32_t source = 0;
  uint64_t frame = ~0ull;
  Surface color, depth;
};
std::array<AoPrivate, 2> aoPrivateEyes;  // stereo: each eye replays into its own images
AoPrivate& ao_private() { return aoPrivateEyes[stereoEye]; }
// dynamic resolution (dynres.h): private AO images of other sizes, kept for a change back (private_ao_surface)
static std::vector<Surface>& ao_kept() { static std::vector<Surface> kept; return kept; }
// ... and made ahead for the sizes the target's kept images (the neighbouring levels) need, one a frame
static void prepare_ao_neighbours(const Surface& dst, const Surface* like) {
  if (!dynres::on()) return;
  static uint64_t madeFrame = ~0ull;
  if (madeFrame == R.frame) return;
  auto& kept = ao_kept();
  auto needed = [&](const Surface& e) {  // a size of this target's levels (current or kept), or another format's
    if (e.fmt.pixel != like->fmt.pixel || e.width != dst.width || e.height != dst.height) return true;
    if (e.extent.width == dst.extent.width && e.extent.height == dst.extent.height) return true;
    for (auto& k : like->spares)
      if (k->image && e.extent.width == k->extent.width * 3 / 2 && e.extent.height == k->extent.height * 3 / 2) return true;
    return false;
  };
  for (size_t i = 0; i < kept.size();)
    if (!needed(kept[i])) { destroy_surface_image(&kept[i]); kept.erase(kept.begin() + long(i)); } else ++i;
  for (auto& k : like->spares) {
    if (!k->image) continue;
    const VkExtent3D extent{k->extent.width * 3 / 2, k->extent.height * 3 / 2, 1};
    bool have = false;
    for (auto& e : kept)
      if (e.image && e.width == dst.width && e.height == dst.height && e.extent.width == extent.width &&
          e.extent.height == extent.height && e.fmt.pixel == like->fmt.pixel) have = true;
    if (have || !extent.width || !extent.height || extent.width > R.properties.limits.maxImageDimension2D ||
        extent.height > R.properties.limits.maxImageDimension2D) continue;
    Surface n = *like;
    n.spares.clear(); n.mipChain.reset();
    n.image = VK_NULL_HANDLE; n.memory = VK_NULL_HANDLE; n.view = VK_NULL_HANDLE;
    n.layerViews.clear(); n.sampledViews.clear(); n.guestLayout.reset();
    n.addr = n.mipAddr = 0;
    n.width = dst.width; n.height = dst.height; n.slices = n.mips = 1;
    n.dim = uint32_t(Latte::E_DIM::DIM_2D);
    n.gpuWritten = true; n.dirty = false;
    create_surface_image(&n, false, extent);
    // (not cleared ahead as the other images made ahead: this runs between draws, and a clear would end the pass)
    layer_view(&n, 0);  // the views the private image is used through, so its first use creates none
    for (auto& [key, view] : dst.sampledViews) make_sampled_view(&n, key);
    kept.push_back(n);
    madeFrame = R.frame;
    while (kept.size() > 8) { destroy_surface_image(&kept.front()); kept.erase(kept.begin()); }  // a safety bound
    return;
  }
}
Surface* private_ao_surface(Surface& dst, const Surface* like) {
  const uint64_t logicalWidth = uint64_t(like->width) * 3 / 2;
  const uint64_t logicalHeight = uint64_t(like->height) * 3 / 2;
  // Full-size torchlight doubles the depth copy (target_scale): the private image keeps the AO pass's size
  const uint64_t light = like->sizeClaim.size == gfx::TargetSize::Light && render::full_light() ? 2 : 1;
  const uint64_t physicalWidth = uint64_t(like->extent.width) * 3 / (2 * light);
  const uint64_t physicalHeight = uint64_t(like->extent.height) * 3 / (2 * light);
  const auto limit = R.properties.limits.maxImageDimension2D;
  if (!logicalWidth || !logicalHeight || logicalWidth > UINT32_MAX ||
      logicalHeight > UINT32_MAX || !physicalWidth || !physicalHeight ||
      physicalWidth > limit || physicalHeight > limit)
    exception_report::raise("private AO image dimensions exceed device limits");
  const uint32_t width = uint32_t(logicalWidth), height = uint32_t(logicalHeight);
  const VkExtent3D extent{uint32_t(physicalWidth), uint32_t(physicalHeight), 1};
  if (!dst.image || dst.width != width || dst.height != height ||
      dst.extent.width != extent.width || dst.extent.height != extent.height ||
      dst.fmt.pixel != like->fmt.pixel) {
    end_encoder();
    // dynamic resolution (dynres.h): the private images of other sizes are kept (a few), and one of the
    // needed size is taken back instead of creating it (its contents are drawn again each frame)
    auto& kept = ao_kept();
    if (!dynres::on()) {
      for (auto& k : kept) destroy_surface_image(&k);
      kept.clear();
    } else {
      for (size_t i = 0; i < kept.size(); ++i) {
        const Surface& k = kept[i];
        if (k.width == width && k.height == height && k.extent.width == extent.width && k.extent.height == extent.height &&
            k.fmt.pixel == like->fmt.pixel && k.image) {
          Surface take = kept[i];
          kept.erase(kept.begin() + long(i));
          if (dst.image) kept.insert(kept.begin(), dst);
          dst = take;
          while (kept.size() > 8) { destroy_surface_image(&kept.back()); kept.pop_back(); }
          return &dst;
        }
      }
      if (dst.image) {
        kept.insert(kept.begin(), dst);
        dst.image = VK_NULL_HANDLE; dst.memory = VK_NULL_HANDLE; dst.view = VK_NULL_HANDLE;
        dst.layerViews.clear(); dst.sampledViews.clear(); dst.mipChain.reset(); dst.allocationBytes = 0;
        while (kept.size() > 8) { destroy_surface_image(&kept.back()); kept.pop_back(); }
      }
    }
    destroy_surface_image(&dst);
    dst = *like;
    dst.mipChain.reset();
    dst.image = VK_NULL_HANDLE; dst.memory = VK_NULL_HANDLE;
    dst.view = VK_NULL_HANDLE;
    dst.layerViews.clear(); dst.sampledViews.clear(); dst.guestLayout.reset();
    dst.addr = dst.mipAddr = 0;
    dst.width = width; dst.height = height; dst.slices = dst.mips = 1;
    dst.dim = uint32_t(Latte::E_DIM::DIM_2D);
    dst.gpuWritten = true; // Private render image: never upload guest address 0.
    dst.dirty = false;
    create_surface_image(&dst, false, extent);
  }
  prepare_ao_neighbours(dst, like);
  return &dst;
}

float f32(uint32_t v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
UploadSlice snapshot(const void *data, size_t size, VkDeviceSize alignment) {
  auto slice = allocate_upload(std::max<size_t>(size, 16), alignment);
  // Every allocation owns its bytes until the submission fence completes.
  // Zero padding also defines shader reads for empty/small uniform payloads.
  if (data && size) {
    std::memcpy(slice.mapped, data, size);
    if (slice.size > size)
      std::memset(static_cast<uint8_t *>(slice.mapped) + size, 0,
                  slice.size - size);
  } else {
    std::memset(slice.mapped, 0, slice.size);
  }
  return slice;
}

UploadSlice vertex_snapshot(uint32_t binding, uint32_t address, uint32_t size,
                            bool bounded) {
  static const bool enabled = [] {
    const char *value = std::getenv("WWHD_VK_REUSE_VERTEX_SNAPSHOTS");
    return value && std::strcmp(value, "1") == 0;
  }();
  static const bool probeEnabled = [] {
    const char* e = std::getenv("WWHD_VK_VERTEX_HISTORY_PROBE");
    return e && !std::strcmp(e, "1") && preparation_stats_enabled();
  }();
  if (probeEnabled) {
    static VertexHistoryProbe probe;
    const uint32_t distance = probe.observe(reinterpret_cast<uintptr_t>(R.device),
        R.submissionGeneration, binding, address, size, bounded);
    if (bounded && binding < probe.bindings.size()) {
      ++R.vertexHistoryRequests;
      if (distance) {
        ++R.vertexHistoryMatches;
        R.vertexHistoryBytes += size;
        ++R.vertexHistoryDistances[distance - 1];
      }
    }
  }
  if (!enabled)
    return snapshot(mem::ptr(address), size, 4);
  static const bool historyEnabled = [] {
    const char* e = std::getenv("WWHD_VK_VERTEX_HISTORY_REUSE");
    return e && !std::strcmp(e, "1");
  }();
  static std::array<VertexSnapshotHistory<UploadSlice>, 16> cache{};
  static uint64_t generation = ~0ull;
  static VkDevice device = VK_NULL_HANDLE;
  if (device != R.device || generation != R.submissionGeneration) {
    for (auto& h : cache) h.reset();  // keeps the CPU copies' capacity
    device = R.device;
    generation = R.submissionGeneration;
  }
  if (binding >= cache.size())
    return snapshot(mem::ptr(address), size, 4);
  auto &history = cache[binding];
  auto &last = history.last;
  if (!bounded) {
    history.reset();
    return snapshot(mem::ptr(address), size, 4);
  }
  // One exact matching payload per draw; secondary is consulted only when
  // the original consecutive key differs, never after a changed-byte miss.
  auto* candidate = VertexSnapshotHistory<UploadSlice>::matches(last, address, size)
      ? &last : historyEnabled ? history.secondary(address, size) : nullptr;
  const bool secondary = candidate && candidate != &last;
  if (candidate) {
    ++R.vertexReuseChecks;
    if (secondary) ++R.vertexHistoryReuseChecks;
    static const bool timed = std::getenv("WWHD_VK_STATS") != nullptr;
    std::chrono::steady_clock::time_point start;
    if (timed) start = std::chrono::steady_clock::now();
    // the entry's CPU copy, not candidate->slice.mapped, unless the entry was made with direct reads
    // (host-cached upload memory: see vertex_snapshot_history.h)
    const bool equal = VertexSnapshotHistory<UploadSlice>::equal(*candidate, mem::ptr(address));
    if (timed)
      R.vertexReuseCompareNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
    if (equal) {
      ++R.vertexReuseHits;
      R.vertexReuseBytes += size;
      if (secondary) {
        ++R.vertexHistoryReuseHits;
        R.vertexHistoryReuseBytes += size;
        history.promote();
      }
      return history.last.slice;
    }
  }
  return history.remember(address, size, mem::ptr(address), historyEnabled, R.uploadReadsDirect,
                          [](const void* bytes, size_t n) { return snapshot(bytes, n, 4); });
}

uint32_t vertex_format_bytes(VkFormat format);
// Diagnostic "Convert vertices on the CPU" (issue #207): the group's attributes, decoded exactly like
// the shader decoder would (vertex_cpu_decode.h), 16 bytes each, interleaved per element; elements:
// 0..limit-1 when the draw's vertex extent is known (limit), else every element of the declared buffer.
UploadSlice cpu_vertex_stream(const LatteParsedFetchShaderBufferGroup& g, uint32_t addr, uint32_t size,
                              uint32_t stride, uint64_t limit) {
  uint64_t elements = stride ? (uint64_t(size) + stride - 1) / stride : 1;
  if (limit && limit < elements) elements = limit;
  const uint32_t attributes = vk::cpu_vertex_count(g, R.vertexDiagnostics.cpuClasses);
  const uint64_t bytes = elements * attributes * 16;
  if (bytes > 0x10000000u) exception_report::raise("CPU vertex conversion exceeds 256 MiB");
  auto slice = allocate_upload(std::max<size_t>(size_t(bytes), 16), 16);
  auto* out = static_cast<uint32_t*>(slice.mapped);
  if (bytes < slice.size) std::memset(static_cast<uint8_t*>(slice.mapped) + bytes, 0, slice.size - bytes);
  const auto* guest = static_cast<const uint8_t*>(mem::ptr(addr));
  for (int j = 0; j < g.attribCount; ++j) {
    const auto& a = g.attrib[j];
    if (!vk::cpu_vertex_selected(a, R.vertexDiagnostics.cpuClasses)) continue;
    uint32_t probe[4];
    static std::set<std::tuple<unsigned, unsigned, unsigned, unsigned>> unsupported;
    uint8_t zero[16]{};
    if (!vk::cpu_decode_attribute(a, zero, probe) &&
        unsupported.insert({unsigned(a.format), unsigned(a.nfa), unsigned(a.isSigned), unsigned(a.endianSwap)}).second)
      LOG("[vulkan cpu vertices] no decoder for format 0x%02x nfa %u signed %u endian %u (reads as zero, like the shader)",
          unsigned(a.format), unsigned(a.nfa), unsigned(a.isSigned), unsigned(a.endianSwap));
  }
  for (uint64_t e = 0; e < elements; ++e) {
    uint32_t slot = 0;
    for (int j = 0; j < g.attribCount; ++j) {
      const auto& a = g.attrib[j];
      if (!vk::cpu_vertex_selected(a, R.vertexDiagnostics.cpuClasses)) continue;
      const uint64_t at = e * stride + a.offset;
      uint32_t value[4] = {0, 0, 0, 0};
      if (at + vertex_format_bytes(vk::vertex_format(a.format)) <= size)
        vk::cpu_decode_attribute(a, guest + at, value);
      std::memcpy(out + (e * attributes + slot++) * 4, value, 16);
    }
  }
  rprof::add_upload(rprof::kUpVertex, bytes);
  return slice;
}

// Separate immutable window cache: a partially initialized reservation must
// never be accepted by the full-prefix snapshot cache.
UploadSlice vertex_window_snapshot(uint32_t binding,uint32_t address,uint32_t reservation,
                                   uint32_t begin,uint32_t length,
                                   const void* source=nullptr,bool poisonUnused=false) {
  using Entry=VertexWindowEntry<UploadSlice>;
  static std::array<Entry,16> entries{};
  static VkDevice device=VK_NULL_HANDLE;
  static uint64_t generation=~0ull;
  if(device!=R.device || generation!=R.submissionGeneration) {
    for(auto& e:entries) e.clear();
    device=R.device;generation=R.submissionGeneration;
  }
  static const bool reuse=[] {const char* e=std::getenv("WWHD_VK_REUSE_VERTEX_SNAPSHOTS");
    return e && !std::strcmp(e,"1");}();
  Entry* entry=reuse && binding<entries.size()?&entries[binding]:nullptr;
  const auto* fresh=static_cast<const uint8_t*>(source?source:mem::ptr(address))+begin;
  // compares the entry's CPU copy of the window, not the mapped slice, unless the entry was made with
  // direct reads (host-cached upload memory; vertex_snapshot_history.h)
  if(entry && entry->matches(address,reservation,begin,length)) {
    ++R.vertexReuseChecks;
    if(entry->equal(fresh)) {
      ++R.vertexReuseHits;R.vertexReuseBytes+=length;return entry->slice;
    }
  }
  // the slice gets the bytes of the entry's copy (one read of guest memory), or fresh if direct
  const uint8_t* bytes=entry?entry->remember(address,reservation,begin,length,fresh,R.uploadReadsDirect)
                            :fresh;
  auto slice=allocate_upload(std::max<uint32_t>(reservation,16),4);
  if(poisonUnused) std::memset(slice.mapped,0xCD,slice.size);
  std::memcpy(static_cast<uint8_t*>(slice.mapped)+begin,bytes,length);
  // Bytes before the proven minimum are never fetched. Preserve the previous
  // small-allocation padding contract without reading that unused prefix.
  if(slice.size>reservation)
    std::memset(static_cast<uint8_t*>(slice.mapped)+reservation,0,slice.size-reservation);
  if(entry) entry->slice=slice;
  return slice;
}

// Exact widths of the raw UINT formats used by the fetch pipeline.
uint32_t vertex_format_bytes(VkFormat format) {
  switch (format) {
  case VK_FORMAT_R8_UINT: return 1;
  case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R16_UINT: return 2;
  case VK_FORMAT_R8G8B8_UINT: return 3;
  case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R16G16_UINT:
  case VK_FORMAT_R32_UINT: return 4;
  case VK_FORMAT_R16G16B16_UINT: return 6;
  case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R32G32_UINT: return 8;
  case VK_FORMAT_R32G32B32_UINT: return 12;
  case VK_FORMAT_R32G32B32A32_UINT: return 16;
  default: return 0;
  }
}
struct VertexExtent {
  bool valid = false;
  uint32_t maximum = 0;
  uint32_t minimum = 0;
};
bool vertex_copy_window_enabled() {
  static const bool enabled=[] {const char* e=std::getenv("WWHD_VK_VERTEX_COPY_WINDOW");
    return e && !std::strcmp(e,"1");}();
  return enabled;
}
template<class Index, bool Restart, bool ZeroBase, bool Window=false>
VertexExtent index_extent_reduction(const void* data, size_t count, int32_t baseVertex) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  constexpr uint32_t marker = sizeof(Index) == 2 ? UINT16_MAX : UINT32_MAX;
  uint32_t minimum = UINT32_MAX, maximum = 0;
  bool any = Restart ? false : count != 0;
  for (size_t i = 0; i < count; ++i) {
    Index index;
    // Fixed-size memcpy supports unaligned guest snapshots and compiles to a
    // native load. Branches on width, restart and zero base stay outside here.
    std::memcpy(&index, bytes + i * sizeof(Index), sizeof(Index));
    const uint32_t value = index;
    if constexpr (Restart) {
      const bool included = value != marker;
      any |= included;
      maximum = std::max(maximum, included ? value : 0);
      if constexpr (!ZeroBase || Window)
        minimum = std::min(minimum, included ? value : UINT32_MAX);
    } else {
      maximum = std::max(maximum, value);
      if constexpr (!ZeroBase || Window) minimum = std::min(minimum, value);
    }
  }
  if (!any) return {}; // Empty/all-restart draws retain the full binding.
  if constexpr (ZeroBase) return {true, maximum, Window ? minimum : 0};
  const int64_t low = int64_t(minimum) + baseVertex;
  const int64_t high = int64_t(maximum) + baseVertex;
  if (low < 0 || high > UINT32_MAX) return {};
  return {true, uint32_t(high), Window ? uint32_t(low) : 0};
}
template<class Index, bool Window=false>
VertexExtent index_extent_typed(const void* data, size_t count, bool restart, int32_t baseVertex) {
  if (!baseVertex) {
    if constexpr (Window) {
      if(count) {
        Index first;
        std::memcpy(&first,data,sizeof(first));
        // A host index zero proves the unsigned global minimum. Zero is
        // never a restart marker; use the original max-only SIMD reduction.
        if(first==0)
          return restart ? index_extent_reduction<Index,true,true,false>(data,count,baseVertex)
                         : index_extent_reduction<Index,false,true,false>(data,count,baseVertex);
      }
    }
    return restart ? index_extent_reduction<Index, true, true, Window>(data, count, baseVertex)
                   : index_extent_reduction<Index, false, true, Window>(data, count, baseVertex);
  }
  return restart ? index_extent_reduction<Index, true, false, Window>(data, count, baseVertex)
                 : index_extent_reduction<Index, false, false, Window>(data, count, baseVertex);
}
VertexExtent indexed_vertex_extent(const void* data, size_t count,
                                   uint32_t width, bool restart, int32_t baseVertex) {
  if(vertex_copy_window_enabled())
    return width == 2 ? index_extent_typed<uint16_t,true>(data,count,restart,baseVertex)
                      : index_extent_typed<uint32_t,true>(data,count,restart,baseVertex);
  return width == 2 ? index_extent_typed<uint16_t>(data, count, restart, baseVertex)
                    : index_extent_typed<uint32_t>(data, count, restart, baseVertex);
}
// Index extents of buffer cache entries (buffer_cache.h): the extent of the cached bytes at base
// vertex 0 is memoized in the entry, the draw's base vertex is applied per draw. The result equals
// indexed_vertex_extent over the same bytes.
VertexExtent cached_index_extent(bufcache::Entry& e, const void* data, size_t count, uint32_t width,
                                 bool restart, int32_t baseVertex) {
  const uint64_t key = uint64_t(count) | uint64_t(width) << 40 | uint64_t(restart) << 48;
  if (e.memoKey != key) {
    if (!data) return {};  // no bytes to scan: the full binding is copied (always safe)
    const VertexExtent raw = width == 2 ? index_extent_typed<uint16_t, true>(data, count, restart, 0)
                                        : index_extent_typed<uint32_t, true>(data, count, restart, 0);
    e.memo[0] = raw.valid; e.memo[1] = raw.minimum; e.memo[2] = raw.maximum;
    e.memoKey = key;
  }
  if (!e.memo[0]) return {};
  const bool window = vertex_copy_window_enabled();
  if (!baseVertex) return {true, e.memo[2], window ? e.memo[1] : 0};
  const int64_t low = int64_t(e.memo[1]) + baseVertex, high = int64_t(e.memo[2]) + baseVertex;
  if (low < 0 || high > UINT32_MAX) return {};
  return {true, uint32_t(high), window ? uint32_t(low) : 0};
}
VkPrimitiveTopology primitive_topology(uint32_t prim) {
  switch (prim) {
  case 1: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  case 2: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case 3: case 0x12: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  case 4: case 5: case 0x13: case 0x14: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case 6: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  default: exception_report::raise("unsupported Vulkan primitive");
  }
}
uint32_t vertex_prefix_size(uint32_t declared, uint32_t stride,
                            uint64_t attributeEnd, VertexExtent extent) {
  if (!extent.valid || !attributeEnd)
    return declared;
  return uint32_t(std::min<uint64_t>(declared,
      uint64_t(extent.maximum) * stride + attributeEnd));
}

// Metadata-only opportunity measurement. It never changes the upload extent.
struct VertexWindowExtent { bool valid=false; uint32_t minimum=0, maximum=0; };
template<class Index>
VertexWindowExtent vertex_window_extent(const void* data, size_t count,
                                       bool restart, int32_t base) {
  uint32_t lo=UINT32_MAX,hi=0; bool any=false;
  const auto* bytes=static_cast<const uint8_t*>(data);
  for(size_t i=0;i<count;++i) {
    Index value; std::memcpy(&value,bytes+i*sizeof(Index),sizeof(value));
    if(restart && value==std::numeric_limits<Index>::max()) continue;
    any=true;lo=std::min(lo,uint32_t(value));hi=std::max(hi,uint32_t(value));
  }
  const int64_t low=int64_t(lo)+base,high=int64_t(hi)+base;
  if(!any || low<0 || high>UINT32_MAX) return {};
  return {true,uint32_t(low),uint32_t(high)};
}
bool vertex_window_stats_enabled() {
  static const bool enabled=[] {const char* e=std::getenv("WWHD_VK_VERTEX_WINDOW_STATS");
    return e && !std::strcmp(e,"1");}();
  return enabled;
}
struct VertexWindowStats {
  uint64_t prefix=0,window=0,bindings=0,eligible=0,fallback=0,zeroStride=0;
} vertexWindowStats;
uint32_t vertex_window_unused(uint32_t copied,uint32_t stride,uint64_t attributeEnd,
                             VertexWindowExtent extent,bool supported) {
  if(!supported || !extent.valid || !attributeEnd || !stride) return 0;
  const uint64_t begin=uint64_t(extent.minimum)*stride;
  const uint64_t end=uint64_t(extent.maximum)*stride+attributeEnd;
  // Out-of-declaration fetches cannot justify changing a snapshot contract.
  if(end>copied || begin>end) return 0;
  return uint32_t(begin);
}
void report_vertex_window_stats() {
  static uint64_t previousFrame=0;
  if(R.frame<previousFrame) {previousFrame=R.frame;vertexWindowStats={};}
  if(R.frame-previousFrame<120) return;
  const double frames=double(R.frame-previousFrame);
  const auto& t=vertexWindowStats;
  LOG("[vulkan vertex window opportunity] %.3f MiB prefix/frame %.3f MiB reachable window/frame %.3f MiB unused prefix/frame; bindings/eligible/fallback/zero-stride %llu/%llu/%llu/%llu; metadata only, uploads unchanged",
    t.prefix/frames/1048576.,t.window/frames/1048576.,(t.prefix-t.window)/frames/1048576.,
    (unsigned long long)t.bindings,(unsigned long long)t.eligible,
    (unsigned long long)t.fallback,(unsigned long long)t.zeroStride);
  vertexWindowStats={};previousFrame=R.frame;
}

VkBlendFactor blend(uint32_t v) {
  static const VkBlendFactor t[] = {VK_BLEND_FACTOR_ZERO,
                                    VK_BLEND_FACTOR_ONE,
                                    VK_BLEND_FACTOR_SRC_COLOR,
                                    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                    VK_BLEND_FACTOR_SRC_ALPHA,
                                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                    VK_BLEND_FACTOR_DST_ALPHA,
                                    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                    VK_BLEND_FACTOR_DST_COLOR,
                                    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                    VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
                                    VK_BLEND_FACTOR_SRC_ALPHA,
                                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                    VK_BLEND_FACTOR_CONSTANT_COLOR,
                                    VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
                                    VK_BLEND_FACTOR_SRC1_COLOR,
                                    VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
                                    VK_BLEND_FACTOR_SRC1_ALPHA,
                                    VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
                                    VK_BLEND_FACTOR_CONSTANT_ALPHA,
                                    VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA};
  if (v >= std::size(t))
    exception_report::raise("unsupported blend factor");
  return t[v];
}
VkBlendOp blendop(uint32_t v) {
  static const VkBlendOp t[] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT,
                                VK_BLEND_OP_MIN, VK_BLEND_OP_MAX,
                                VK_BLEND_OP_REVERSE_SUBTRACT};
  if (v >= 5)
    exception_report::raise("unsupported blend operation");
  return t[v];
}
VkStencilOp stencil(uint32_t v) {
  static const VkStencilOp t[] = {VK_STENCIL_OP_KEEP,
                                  VK_STENCIL_OP_ZERO,
                                  VK_STENCIL_OP_REPLACE,
                                  VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                  VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                  VK_STENCIL_OP_INVERT,
                                  VK_STENCIL_OP_INCREMENT_AND_WRAP,
                                  VK_STENCIL_OP_DECREMENT_AND_WRAP};
  if (v >= 8)
    exception_report::raise("unsupported stencil operation");
  return t[v];
}
struct BindingTrimMetadata {
  uint64_t attributeEnd = 0;
  uint32_t stride = 0;
  bool supported = true;
  std::optional<LatteConst::VertexFetchType2> rate;
};
template<class Group>
BindingTrimMetadata binding_trim_metadata(const uint32_t* r, vk::Shader* vs, const Group& g) {
  BindingTrimMetadata trim;
  for (int j = 0; j < g.attribCount; ++j) {
    const auto &a = g.attrib[j];
    if (vs->mapping.attributeMapping[a.semanticId] < 0)
      continue;
    const uint32_t bytes = vertex_format_bytes(vk::vertex_format(a.format));
    if (!bytes || (trim.rate && *trim.rate != a.fetchType) ||
      (a.fetchType != LatteConst::VertexFetchType2::VERTEX_DATA &&
       a.fetchType != LatteConst::VertexFetchType2::INSTANCE_DATA) ||
      (a.fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA &&
       a.aluDivisor != 1)) {
      trim.supported = false;
      break;
    }
    trim.rate = a.fetchType;
    trim.attributeEnd = std::max(trim.attributeEnd, uint64_t(a.offset) + bytes);
  }
  trim.stride =
    (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >>
     11) & 0xFFFF;
  return trim;
}
struct PendingPipeline {
  vk::Shader* shaders[2]{};
  VkDevice device{};
  VkShaderModule modules[2]{};
  std::optional<PipelineDemandTicket> ticket;
  std::shared_ptr<PipelineRecipe> recipe;
  // false: a HUD coverage variant (hud_coverage.h) or a pass subset's masked variant (pass_reuse.h),
  // never recorded as a recipe
  bool keepRecipe = true;
  bool background = false;  // compiled in the background lane (PipelineVariant::background)
  ~PendingPipeline() {
    // Even abandoned jobs can still read these immutable prerequisites.
    if (ticket) ticket->discard_and_wait();
    for (auto module : modules)
      if (module) vkDestroyShaderModule(device, module, nullptr);
  }
};
struct Pipeline {
  VkPipeline pipeline{};
  std::shared_ptr<PendingPipeline> pendingCompile;
  std::exception_ptr compileError;
  VkPipelineLayout layout{};
  VkDescriptorSetLayout sets[2]{};
  bool dynamicUniforms = false;
  const LatteFetchShader* trimFetch = nullptr;
  const vk::Shader* trimVertex = nullptr;
  std::vector<BindingTrimMetadata> bindingTrims;
  // pass subset (pass_reuse.h, draw()): this pipeline's masked variant last used, by the slots it takes
  // over and their formats, so a subset draw doesn't build and look up a second 208-byte key each time;
  // declinedFrame: the frame the background lane last had no room for it (asked again next frame)
  struct MaskedVariant {
    uint32_t clearMask = 0;
    std::array<VkFormat, 8> formats{};
    struct Pipeline* pipeline = nullptr;
    uint64_t declinedFrame = ~0ull;
  } masked;
};
// Pipeline key: the two shader identities plus exactly the fixed-function state pipeline() bakes
// in, normalized so that state the pipeline ignores (blend factors of attachments that do not
// blend, stencil words with the stencil test off, depth bias words with the bias off, write masks
// of absent attachments) does not make a new pipeline. Fixed size, compared with memcmp.
constexpr uint32_t kMaxPipelineStrides = 16;
struct PipelineKey {
  uint64_t vs = 0, ps = 0, fetch = 0;
  uint32_t topology = 0, colorControl = 0, targetMask = 0, depthControl = 0;
  uint32_t stencilMask = 0, stencilMaskBack = 0, raster = 0, clip = 0;
  std::array<uint32_t, 3> depthBias{};
  std::array<uint32_t, 8> blend{};
  std::array<uint32_t, 8> formats{};
  uint32_t depthFormat = 0, strideCount = 0;
  std::array<uint32_t, kMaxPipelineStrides> strides{};
  uint32_t reserved = 0;  // no padding bytes: the key is hashed and compared as bytes
  bool operator==(const PipelineKey& other) const {
    return !std::memcmp(this, &other, sizeof *this);
  }
};
static_assert(std::has_unique_object_representations_v<PipelineKey>,
              "pipeline keys are compared and hashed as bytes");
struct PipelineKeyHash {
  size_t operator()(const PipelineKey& key) const {
    const auto* p = reinterpret_cast<const uint8_t*>(&key);
    uint64_t hash = 0x9E3779B97F4A7C15ull;
    for (size_t i = 0; i < sizeof key; i += 8) {
      uint64_t word;
      std::memcpy(&word, p + i, 8);
      hash = (hash ^ word) * 0xFF51AFD7ED558CCDull;
      hash ^= hash >> 32;
    }
    return size_t(hash ^ (hash >> 29));
  }
};
static_assert(sizeof(PipelineKey) % 8 == 0);
static_assert(sizeof(PipelineKey) == 208, "update the persisted pipeline recipe schema");
std::unordered_map<PipelineKey, Pipeline, PipelineKeyHash> pipelines;
// covSlot (60 fps (generated), hud_coverage.h): the colour attachment the pixel shader's coverage variant
// writes (0: none). It is kept in the key's reserved word, so the key (and the persisted recipes, which
// never hold such a pipeline) keeps its size; that attachment's blend and mask are the coverage's own.
PipelineKey pipeline_key(const uint32_t* r, const vk::Shader* vs, const vk::Shader* ps,
                         const LatteFetchShader* fs, VkPrimitiveTopology topology,
                         const std::array<Surface*, 8>& colors, const Surface* depth,
                         uint32_t clearMask = 0, uint32_t covSlot = 0) {
  // clearMask: CB_TARGET_MASK nibbles of colour slots a draw keeps from the open pass without writing
  // them (pass_reuse.h subset); 0 everywhere else
  PipelineKey key;
  key.vs = vs->key; // process-local canonical variant; portable identity is separate
  key.ps = ps->key;
  key.fetch = fs->vkPipelineHashFragment;
  key.topology = uint32_t(topology);
  uint32_t ncolor = 0;
  for (uint32_t i = 0; i < 8; ++i)
    if (colors[i]) {
      key.formats[i] = uint32_t(colors[i]->fmt.pixel);
      ncolor = i + 1;
    }
  const uint32_t colorControl = r[REGADDR::CB_COLOR_CONTROL];
  uint32_t blending = 0;
  for (uint32_t i = 0; i < ncolor; ++i)
    if (colors[i] && colors[i]->fmt.kind == FormatInfo::FLOAT && ((colorControl >> (8 + i)) & 1) &&
        !((clearMask >> (4 * i)) & 15)) {
      blending |= 1u << i;
      // without SEPARATE_ALPHA_BLEND the alpha factors are the color ones
      const uint32_t raw = r[REGADDR::CB_BLEND0_CONTROL + i];
      key.blend[i] = raw & (raw & (1u << 29) ? 0x3FFF1FFFu : 0x00001FFFu);
    }
  key.colorControl = (colorControl & 0x00FF0000u) | (blending << 8);  // ROP, blend enables
  const uint32_t targetMask = r[REGADDR::CB_TARGET_MASK] & ~clearMask;
  key.targetMask = ncolor >= 8 ? targetMask : targetMask & ((1u << (4 * ncolor)) - 1);
  if (depth) {
    key.depthFormat = uint32_t(depth->fmt.pixel);
    const uint32_t dc = r[REGADDR::DB_DEPTH_CONTROL];
    uint32_t canonical = dc & 4;              // depth write
    if (dc & 2) canonical |= dc & 0x72;       // depth test and its function
    if (depth->fmt.stencil && (dc & 1)) {
      const bool back = dc & 0x80;
      canonical |= dc & (back ? 0xFFFFFF81u : 0x000FFF01u);
      key.stencilMask = r[REGADDR::DB_STENCILREFMASK] & 0x00FFFF00;
      if (back) key.stencilMaskBack = r[REGADDR::DB_STENCILREFMASK_BF] & 0x00FFFF00;
    }
    key.depthControl = canonical;
  }
  const uint32_t mode = r[REGADDR::PA_SU_SC_MODE_CNTL];
  key.raster = mode & 0x807;  // cull front/back, front face, depth bias enable
  if (mode & 0x800)
    key.depthBias = {r[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE], r[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET],
                     r[REGADDR::PA_SU_POLY_OFFSET_CLAMP]};
  key.clip = (r[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27)) |   // depth clamp
             (vk::clip_emulation::pair_clips(vs, ps) ? 1u : 0u);  // and clipping in shaders (both stages)
  if (fs->bufferGroups.size() > kMaxPipelineStrides)
    exception_report::raise("fetch shader has more vertex buffers than a pipeline key holds");
  for (auto& g : fs->bufferGroups)
    key.strides[key.strideCount++] =
        (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
  if (covSlot) {
    key.reserved = covSlot;
    key.blend[covSlot] = 0;
    key.colorControl &= ~(1u << (8 + covSlot));
    key.targetMask &= ~(15u << (4 * covSlot));
  }
  return key;
}
struct LastPipelineLookup {
  PipelineKey key{};
  VkDevice device = VK_NULL_HANDLE;
  Pipeline* value = nullptr;
};
LastPipelineLookup lastPipelineLookup;
struct PipelineLookaside {
  std::array<LastPipelineLookup, 8> entries{};
  uint32_t next = 0;
  Pipeline* find(const PipelineKey& key, VkDevice device) const {
    for (const auto& entry : entries)
      if (entry.value && entry.device == device && entry.key == key)
        return entry.value;
    return nullptr;
  }
  void remember(const LastPipelineLookup& entry) {
    if (!entry.value) return;
    entries[next] = entry;
    next = (next + 1) % entries.size();
  }
};
PipelineLookaside pipelineLookaside;
bool pipeline_lookaside_enabled() {
  static const bool enabled = [] {
    const char* e = std::getenv("WWHD_VK_PIPELINE_LOOKASIDE");
    return e && !std::strcmp(e, "1");
  }();
  return enabled;
}
void capture_pipeline_failure(VkResult result, vk::Shader* vs, vk::Shader* ps,
                              const std::vector<uint32_t>& vertexWords,
                              const std::vector<uint32_t>& pixelWords) noexcept {
  try {
    LOG("[vulkan] graphics pipeline failed (Vulkan result %d): vs %016llX ps %016llX; its draws are skipped",
        int(result), (unsigned long long)vs->key, (unsigned long long)ps->key);
    std::error_code error; std::filesystem::create_directories("captures", error);
    vk::Shader* shaders[]{vs,ps};
    const std::vector<uint32_t>* words[]{&vertexWords,&pixelWords};
    for(size_t i=0;i<2;++i) {
      char name[96];
      snprintf(name,sizeof name,"captures/pipeline-failed-%s-%016llX",
          shaders[i]->vertex ? "vs" : "ps",(unsigned long long)shaders[i]->key);
      // GLSL text is kept only with WWHD_VK_SHADER_SOURCE_VERIFY=1 (or key verify).
      const auto& glsl=shaders[i]->source_text();
      if(glsl.empty())
        LOG("[vulkan] %s GLSL not kept; set WWHD_VK_SHADER_SOURCE_VERIFY=1 to capture it",name);
      else if(FILE* f=fopen((std::string(name)+".glsl").c_str(),"wb")) {
        fwrite(glsl.data(),1,glsl.size(),f); fclose(f);
      }
      if(FILE* f=fopen((std::string(name)+".spv").c_str(),"wb")) {
        fwrite(words[i]->data(),4,words[i]->size(),f); fclose(f);
      }
    }
  } catch(...) { /* Diagnostic capture must never replace driver recovery with a crash. */ }
}
// wait=false: poll, or wait at most budgetNs (Shader compile Fast), and return with the
// compile still pending when it is not done by then
Pipeline& resolve_pending_pipeline(Pipeline& pipeline, bool wait = true, bool present = false, uint64_t budgetNs = 0) {
  if (pipeline.compileError) std::rethrow_exception(pipeline.compileError);
  if (!pipeline.pendingCompile) return pipeline;
  try {
    auto& pending = *pipeline.pendingCompile;
    if (!wait && budgetNs) ++g_pipelineBudgetWaits;  // (Shader compile Fast: a bounded wait)
    std::optional<PipelineBuildResult> built = pending.ticket->wait_for(wait ? 0 : budgetNs);
    if (!built && !wait) return pipeline;
    if (!built) {
      // something must wait for this pipeline now: its compile and its shaders' SPIR-V jump the queues
      // (Shader compile Fast queues the work of draws that were left out)
      if (bgshaders::enabled()) {
        for (auto* shader : pending.shaders)
          if (shader && shader->binary) {
            std::lock_guard lock(shader->binary->mutex);
            if (shader->binary->ticket) shader->binary->ticket->prioritize();
          }
        pending.ticket->prioritize();
      }
      if (!g_drainingPipelines) ++(pending.background ? g_backgroundWaits : g_pipelineRenderWaits);
      if (present) begin_pipeline_pending_frame();
      built = pending.ticket->wait(present ? std::function<void()>(present_completed_frame_while_pipeline_pending)
                                           : std::function<void()>{});
    }
    R.pipelineCreateNs += built->compileNs;
    ++R.pipelineCreates;
    if(recoverable_pipeline_failure(built->result)) {
      capture_pipeline_failure(built->result,pending.shaders[0],pending.shaders[1],pending.recipe->words(0),pending.recipe->words(1));
      pipeline.pendingCompile.reset(); // terminal failure, never a pending omission
      return pipeline;
    }
    vk_check(built->result, "graphics pipeline");
    pipeline.pipeline = built->pipeline;
    auto recipe = std::move(pending.recipe);
    const bool keepRecipe = pending.keepRecipe;
    // Publish the consumed result before fallible recipe bookkeeping. Never
    // retain a consumed ticket, even when recording or driver validation throws.
    pipeline.pendingCompile.reset();
    R.pipelineCacheDirty = true;
    R.pipelineCacheChangedFrame = R.frame;
    if (keepRecipe) remember_pipeline_recipe(std::move(recipe));
    return pipeline;
  } catch (...) {
    if (!pipeline.pipeline) pipeline.compileError = std::current_exception();
    pipeline.pendingCompile.reset();
    throw;
  }
}
// Shader compile (background_shaders.h): the targets a draw writes
static bool writes_color(const uint32_t* r, int i, uint32_t clearMask = 0) {
  return ((r[REGADDR::CB_TARGET_MASK] & ~clearMask) >> (4 * i)) & 15;
}
static bool writes_depth(const uint32_t* r, const Surface* depth) {
  const uint32_t dc = r[REGADDR::DB_DEPTH_CONTROL];  // bit 0 stencil, bit 2 depth write
  return depth && ((dc & 4) || ((dc & 1) && depth->fmt.stencil));
}
static void track_drawn(const uint32_t* r, const std::array<Surface*, 8>& colors, Surface* depth,
                        uint32_t clearMask = 0) {
  auto& t = bgshaders::tracker();
  for (int i = 0; i < 8; i++)
    if (colors[i] && writes_color(r, i, clearMask)) t.drawn(colors[i]->addr, R.frame);
  if (writes_depth(r, depth)) t.drawn(depth->addr, R.frame);
}
// The pipeline is still compiling: wait the frame's budget if this draw may be left out (true: draw
// it now, ready or with the exact wait that follows), false: left out of this frame.
static bool background_wait(const uint32_t* r, Pipeline& p, const std::array<Surface*, 8>& colors, Surface* depth,
                            uint32_t clearMask = 0) {
  auto& t = bgshaders::tracker();
  bgshaders::TargetFacts written[8], depthFacts;
  int count = 0;
  for (int i = 0; i < 8; i++)
    if (colors[i] && writes_color(r, i, clearMask)) written[count++] = t.facts(colors[i]->addr, R.frame);
  const bool depthWritten = writes_depth(r, depth);
  if (depthWritten) depthFacts = t.facts(depth->addr, R.frame);
  // the depth buffer the game's sun check peeks (peek_z) waits even before the first peek
  if (depthWritten && depth->addr == R.mainDepthAddr) depthFacts.peeked = true;
  const auto verdict = bgshaders::classify(bgshaders::level(), written, count, depthWritten ? &depthFacts : nullptr);
  if (verdict != bgshaders::kSkippable) { bgshaders::exact(verdict); return true; }
  const uint64_t left = bgshaders::budget_left_ns(R.frame);
  const auto started = std::chrono::steady_clock::now();
  resolve_pending_pipeline(p, false, false, left);
  const bool ready = !p.pendingCompile;
  bgshaders::spent(R.frame, uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count()), !ready);
  if (ready) return true;
  bgshaders::skipped(R.frame);
  for (int i = 0; i < 8; i++)
    if (colors[i] && writes_color(r, i, clearMask)) t.left_out(colors[i]->addr, R.frame);
  if (depthWritten) t.left_out(depth->addr, R.frame);
  return false;
}
// ---------------------------------------------------------------- HUD coverage (60 fps generated)
// (WindWakerHD-Android-Plus fc5ddee, via GreenNaugahyde v0.9.9 vk_draw.cpp, MPL-2.0) From the first layout
// drawn on the TV picture (reproj::layout, where the scene is copied aside) each draw into that picture
// also writes how much it covers what lies behind: an R8 image of the picture's size cleared to 0
// (reproj.cpp), written by a variant of the pixel shader (hud_coverage.h) with a blend that follows the
// draw's own. The in-between frame then moves the scene behind translucent HUD parts too; a frame with a
// HUD draw that couldn't write it (a shader without the variant, another target alongside, its pipeline
// still compiling) is generated with the HUD found by comparing pictures. WWHD_HUD_COVERAGE=0: off.
// Nothing of it runs unless 60 fps (generated) does (reproj::hud_coverage_wanted).
struct CoverageWords { std::vector<uint32_t> words; uint32_t location = 0; };
std::unordered_map<uint64_t, CoverageWords> coverageVariants;  // by pixel shader key; empty words: none
// the pixel shader's coverage variant and the location it writes, or null (none, or its SPIR-V not ready yet)
const std::vector<uint32_t>* coverage_words(const vk::Shader* ps, uint32_t& location) {
  if (!ps) return nullptr;
  if (auto it = coverageVariants.find(ps->key); it != coverageVariants.end()) {
    location = it->second.location;
    return it->second.words.empty() ? nullptr : &it->second.words;
  }
  std::vector<uint32_t> words = ps->spirv();
  if (words.empty() && ps->binary) {
    if (!vk::shader_binary_ready(ps->binary)) return nullptr;  // (asked again at its next draw)
    words = vk::shader_binary_words(ps->binary);
  }
  if (words.empty()) return nullptr;
  CoverageWords v;
  if (hud_coverage::add_output(words, v.location)) v.words = std::move(words);
  static int failures = 0;
  if (v.words.empty() && failures++ < 8) LOG("[framegen] HUD shader %016llx: no coverage variant", (unsigned long long)ps->key);
  auto& kept = coverageVariants[ps->key] = std::move(v);
  location = kept.location;
  return kept.words.empty() ? nullptr : &kept.words;
}
// the coverage attachment's blend from the picture's: what the draw leaves of the picture behind
VkPipelineColorBlendAttachmentState coverage_blend(const VkPipelineColorBlendAttachmentState& c0) {
  VkPipelineColorBlendAttachmentState c{};
  c.colorWriteMask = (c0.colorWriteMask & 7) ? VK_COLOR_COMPONENT_R_BIT : 0;
  if (!c0.blendEnable) return c;  // replaces what was there: covered
  c.blendEnable = VK_TRUE;
  c.colorBlendOp = c.alphaBlendOp = VK_BLEND_OP_ADD;
  VkBlendFactor src = VK_BLEND_FACTOR_SRC_ALPHA, dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;  // "over"
  if (c0.colorBlendOp == VK_BLEND_OP_ADD || c0.colorBlendOp == VK_BLEND_OP_SUBTRACT) switch (c0.dstColorBlendFactor) {
      case VK_BLEND_FACTOR_ZERO: src = VK_BLEND_FACTOR_ONE; dst = VK_BLEND_FACTOR_ZERO; break;          // replaces
      case VK_BLEND_FACTOR_ONE: src = VK_BLEND_FACTOR_ZERO; dst = VK_BLEND_FACTOR_ONE; break;           // adds: covers nothing
      case VK_BLEND_FACTOR_SRC_ALPHA: src = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; dst = VK_BLEND_FACTOR_SRC_ALPHA; break;
      default: break;
    }
  c.srcColorBlendFactor = c.srcAlphaBlendFactor = src;
  c.dstColorBlendFactor = c.dstAlphaBlendFactor = dst;
  return c;
}
// Setup's Vulkan preparation (derive_guest_pipeline_recipe): pipeline() hands the recipe it would compile to this
// sink instead of compiling it or taking a warmed pipeline. Render thread only.
std::vector<std::shared_ptr<PipelineRecipe>>* g_recipeSink = nullptr;
// What pipeline() makes besides a draw's ordinary pipeline (distinct names: both can be asked for).
struct PipelineVariant {
  // pass subset (pass_reuse.h): CB_TARGET_MASK nibbles of the colour slots a draw keeps from the open
  // pass without writing them (blending off there too)
  uint32_t clearMask = 0;
  // 60 fps (generated), hud_coverage.h: the colour slot the pixel shader's HUD coverage variant writes
  uint32_t covSlot = 0;
  // compiled only in the background lane (begin_pipeline_background), never on the render thread and
  // never waited for; never recorded as a recipe (pipeline database, template), never filed by the
  // setup's sweeps, not taken from or counted as warm-up. A miss the lane has no room for (or with
  // WWHD_VK_ASYNC_PIPELINES=0, or in the setup's preparation) returns declined_pipeline(), unstored.
  bool background = false;
};
// pipeline()'s answer to a background request it did not admit (never stored, never drawn with)
Pipeline& declined_pipeline_storage() {
  static Pipeline none;
  return none;
}
Pipeline& declined_pipeline() {
  auto& none = declined_pipeline_storage();
  none = Pipeline{};
  return none;
}
#ifdef WWHD_NATIVE_DRAW
// native draw packets, batching (gx2/native_draw.h): the per-instance vertex stream of a batch shader pair, added to
// the vertex input of the pipeline being made (set only around packet_draw's pipeline() call for its batch variant,
// whose vertex shader has a key of its own: a background variant, never recorded)
struct NativeInstanceInput {
  uint32_t binding = 0, stride = 0;
  std::vector<uint32_t> locations;  // one R32G32B32A32_SINT attribute each, at 16 * i
};
const NativeInstanceInput* g_nativeInput = nullptr;
#endif
Pipeline &pipeline(const uint32_t *r, vk::Shader *vs, vk::Shader *ps,
                   LatteFetchShader *fs, VkPrimitiveTopology topology,
                   const std::array<Surface *, 8> &colors, Surface *depth,
                   const PipelineVariant& variant = {}) {
  if(preparation_stats_enabled())++R.cpuPreparation.pipelineLookups;
  const uint32_t clearMask = variant.clearMask;
  // (the setup's preparation: never a coverage variant)
  const uint32_t covSlot = g_recipeSink ? 0 : variant.covSlot;
  const PipelineKey key = pipeline_key(r, vs, ps, fs, topology, colors, depth, clearMask, covSlot);
  // the setup's model sweep: every pipeline a swept archive's models use is filed under that archive (once each)
  if (!covSlot && !variant.background && pipeline_groups::sweeping() && !pipeline_groups::sweeping_new_only()) {
    static std::unordered_set<uint64_t> noted;
    for (const uint32_t group : pipeline_groups::sweeping_all()) {
      const uint64_t id = PipelineKeyHash{}(key) * 1099511628211ull ^ group;
      if (noted.insert(id).second) {
        auto portable = key;
        portable.vs = vs->pipelineId; portable.ps = ps->pipelineId;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&portable);
        note_pipeline_group(std::vector<uint8_t>(bytes, bytes + sizeof portable), group);
      }
    }
  }
  std::array<VkFormat, 8> formats{};
  uint32_t ncolor = 0;
  for (int i = 0; i < 8; i++) {
    formats[i] = colors[i] ? colors[i]->fmt.pixel : VK_FORMAT_UNDEFINED;
    if (colors[i])
      ncolor = i + 1;
  }
  VkFormat df = depth ? depth->fmt.pixel : VK_FORMAT_UNDEFINED;
  if (lastPipelineLookup.device != R.device || pipelines.empty()) {
    lastPipelineLookup = {};
    pipelineLookaside = {};
  }
  if (lastPipelineLookup.value && lastPipelineLookup.key == key) {
    if(preparation_stats_enabled())++R.cpuPreparation.pipelineLastHits;
    return *lastPipelineLookup.value;
  }
  const bool useLookaside = pipeline_lookaside_enabled();
  if (useLookaside)
    if (auto* value = pipelineLookaside.find(key, R.device)) {
      if(preparation_stats_enabled())++R.cpuPreparation.pipelineLookasideHits;
      lastPipelineLookup = {key, R.device, value};
      return *value;
    }
  auto remember = [&](Pipeline& value) -> Pipeline& {
    render_objects::set_pipelines(pipelines.size());  // crash logs, performance report
    lastPipelineLookup = {key, R.device, &value};
    if (useLookaside) pipelineLookaside.remember(lastPipelineLookup);
    return value;
  };
  if(preparation_stats_enabled())++R.cpuPreparation.pipelineMapLookups;
  if (auto it = pipelines.find(key); it != pipelines.end())
    return remember(it->second);
  static const bool recordOnly = [] { const char* e = getenv("WWHD_DSWEEP_RECORD_ONLY"); return e && *e == '1'; }();
  if (variant.background) {
    ++g_backgroundRequests;
    // never on the render thread: without the async compiler, in the setup's preparation or its
    // record-only sweep there is no background lane, and a full lane is not waited for
    if (g_recipeSink || recordOnly || !async_pipeline_enabled() || !pipeline_background_has_room()) {
      ++g_backgroundDeclined;
      return declined_pipeline();
    }
  }
  Pipeline p;
  vk::Shader *shaders[] = {vs, ps};
  uint32_t uniformBindings = 0;
  for (auto *shader : shaders) {
    uniformBindings += shader->mapping.uniformVarsBufferBindingPoint >= 0;
    for (int binding : shader->mapping.uniformBuffersBindingPoint)
      uniformBindings += binding >= 0;
  }
  // Dynamic UBO limits apply across both sets in the pipeline layout. Keep
  // both stages on the existing regular path if the complete layout exceeds it.
  p.dynamicUniforms = uniformBindings <=
      R.properties.limits.maxDescriptorSetUniformBuffersDynamic;
  const auto uniformType = p.dynamicUniforms
      ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
      : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  std::array<std::vector<VkDescriptorSetLayoutBinding>, 2> descriptorBindings;
  for (int st = 0; st < 2; st++) {
    auto &m = shaders[st]->mapping;
    auto& b = descriptorBindings[st];
    auto add = [&](int i, VkDescriptorType t) {
      if (i >= 0)
        b.push_back(
            {uint32_t(i), t, 1,
             VkShaderStageFlags(st ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT),
             nullptr});
    };
    add(m.uniformVarsBufferBindingPoint, uniformType);
    for (auto i : m.uniformBuffersBindingPoint)
      add(i, uniformType);
    for (int i = 0; i < m.textureUnitCount; i++)
      add(m.textureUnitBaseBindingPoint + i,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    if (m.tfStorageBindingPoint >= 0)
      exception_report::raise("Vulkan transform feedback unsupported");
  }
  auto portableKey = key;
  portableKey.vs = vs->pipelineId; portableKey.ps = ps->pipelineId;
  const auto* keyBytes = reinterpret_cast<const uint8_t*>(&portableKey);
  std::vector<uint8_t> recipeKey(keyBytes, keyBytes + sizeof key);
  // debug: WWHD_VK_KEY_LOG=file appends one line per pipeline this run creates: steady-clock ns, renderer frame,
  // portable key digest (FNV-1a of the recipe key), in-memory key hash (as in the stutter trace), D-sweep tag
  static FILE* keyLog = [] { const char* p = getenv("WWHD_VK_KEY_LOG"); return p && *p ? std::fopen(p, "a") : nullptr; }();
  if (keyLog) {
    uint64_t digest = 1469598103934665603ull;
    for (uint8_t byte : recipeKey) digest = (digest ^ byte) * 1099511628211ull;
    std::fprintf(keyLog, "%lld %llu %016llx %016llx %d\n",
        (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
        (unsigned long long)R.frame, (unsigned long long)digest, (unsigned long long)PipelineKeyHash{}(key), dsweep::tag());
    std::fflush(keyLog);
  }
  // debug: WWHD_VK_PIPELINE_WHO=file: per pipeline created, the renderer frame, the key hash (as in the stutter trace)
  // and what draws it (gx2::current_draw_context, described by dsweep.cpp: guest call chain, archive resources, actors)
  static FILE* whoLog = [] { const char* p = getenv("WWHD_VK_PIPELINE_WHO"); return p && *p ? std::fopen(p, "a") : nullptr; }();
  if (whoLog) {
    std::fprintf(whoLog, "%llu %016llx %s\n", (unsigned long long)R.frame, (unsigned long long)PipelineKeyHash{}(key),
                 gx2::current_draw_context().c_str());
    std::fflush(whoLog);
  }
  // Own prerequisite identities independently of the mutable shader cache.
  const std::array<std::shared_ptr<vk::ShaderBinary>, 2> shaderBinaries{vs->binary, ps->binary};
  std::array<std::vector<uint32_t>, 2> recipeSpirv{vs->spirv(), ps->spirv()};
  for(size_t i=0;i<shaderBinaries.size();++i)
    if(recipeSpirv[i].empty() && shaderBinaries[i]) {
      if(!async_pipeline_enabled()) recipeSpirv[i]=vk::shader_binary_words_exact(shaderBinaries[i],R.frame);
      else if(vk::shader_binary_ready(shaderBinaries[i])) recipeSpirv[i]=vk::shader_binary_words(shaderBinaries[i]);
    }
  // 60 fps (generated): the pixel shader's HUD coverage variant (the draw checked it can be made)
  if (covSlot) {
    uint32_t location = 0;
    const auto* words = coverage_words(ps, location);
    if (!words || location != covSlot) exception_report::raise("HUD coverage variant missing");
    recipeSpirv[1] = *words;
  }
  for (int st = 0; st < 2; st++) {
    auto& b = descriptorBindings[st];
    VkDescriptorSetLayoutCreateInfo ci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = b.size();
    ci.pBindings = b.data();
    vk_check(vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &p.sets[st]),
             "descriptor layout");
  }
  VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lc.setLayoutCount = 2;
  lc.pSetLayouts = p.sets;
  vk_check(vkCreatePipelineLayout(R.device, &lc, nullptr, &p.layout),
           "pipeline layout");
  VkShaderModule modules[2]{};
  VkPipelineShaderStageCreateInfo stages[2]{};
  for (int i = 0; i < 2; i++) {
    if(!async_pipeline_enabled()) {
      VkShaderModuleCreateInfo mc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      mc.codeSize = recipeSpirv[i].size() * sizeof(uint32_t);
      mc.pCode = recipeSpirv[i].data();
      vk_check(vkCreateShaderModule(R.device, &mc, nullptr, &modules[i]), "shader module");
    }
    stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[i].stage =
        i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    stages[i].module = modules[i];
    stages[i].pName = "main";
  }
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  p.trimFetch = fs;
  p.trimVertex = vs;
  p.bindingTrims.reserve(fs->bufferGroups.size());
  uint32_t tailLocation = vk::vertex_tail_location(vs->mapping.attributeMapping);
  for (auto &g : fs->bufferGroups) {
    const uint32_t cpuCount = vk::cpu_vertex_count(g, R.vertexDiagnostics.cpuClasses);
    const uint32_t cpuBinding = cpuCount ? vk::cpu_vertex_binding(*fs, g, R.vertexDiagnostics.cpuClasses,
        R.properties.limits.maxVertexInputBindings) : g.attributeBufferIndex;
    if (cpuCount && cpuBinding >= R.properties.limits.maxVertexInputBindings)
      exception_report::raise("CPU vertex classes exceed device vertex binding limit");
    bool instance = false;
    BindingTrimMetadata trim;
    std::optional<LatteConst::VertexFetchType2> fetchRate;
    for (int j = 0; j < g.attribCount; j++) {
      auto &a = g.attrib[j];
      const bool cpu = vk::cpu_vertex_selected(a, R.vertexDiagnostics.cpuClasses);
      const uint32_t cpuOffset = vk::cpu_vertex_offset(g, j, R.vertexDiagnostics.cpuClasses);
      int loc = vs->mapping.attributeMapping[a.semanticId];
      if (loc < 0)
        continue;
      auto fmt = R.vertexFormats.fetch(vk::vertex_format(a.format));
      if (fmt == VK_FORMAT_UNDEFINED)
        exception_report::raise("unsupported vertex format");
      if (cpu)
        // CPU-converted stream: 16 bytes per attribute of the group (cpu_vertex_stream)
        attributes.push_back({uint32_t(loc), cpuBinding, vk::cpu_vertex_format(a), cpuOffset});
      else
      attributes.push_back(
          {uint32_t(loc), g.attributeBufferIndex, fmt, a.offset});
      const auto nativeFormat = vk::vertex_format(a.format);
      // Metadata only: never record vertex contents, guest addresses, or save data.
      static const bool observeVertexInputs = [] {
#ifdef __ANDROID__
        return true;
#else
        const char* setting = std::getenv("WWHD_VK_VERTEX_METADATA");
        return setting && !std::strcmp(setting, "1");
#endif
      }();
      if (observeVertexInputs) {
        // Keep the offset and its own guest binding stride together. An offset-limit check
        // alone says nothing about access beyond stride; zero stride is a constant input.
        const uint32_t observedStride =
            (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
        const uint64_t attributeEnd = uint64_t(a.offset) + vertex_format_bytes(nativeFormat);
        static std::set<std::tuple<unsigned, unsigned, unsigned, unsigned, unsigned, unsigned>> seenVertexInputs;
        const auto diagnostic = std::make_tuple(unsigned(a.format), unsigned(a.endianSwap),
            unsigned(a.nfa), unsigned(a.isSigned), unsigned(a.offset), unsigned(observedStride));
        if (seenVertexInputs.size() < 64 && seenVertexInputs.insert(diagnostic).second)
          LOG("[vulkan vertex observed] guestFormat=0x%02x native=%s fetch=%s endian=%u numeric=%u signed=%u offset=%u offsetMod4=%u offsetWithinLimit=%u fallback=%u stride=%u attributeEnd=%llu offsetBeyondStride=%u attributeEndBeyondStride=%u cpu=%u",
              unsigned(a.format), vk::vertex_format_name(nativeFormat),
              cpu ? (vk::cpu_vertex_float(a) ? "R32G32B32A32_SFLOAT" : "R32G32B32A32_UINT") : vk::vertex_format_name(fmt), unsigned(a.endianSwap),
              unsigned(a.nfa), unsigned(a.isSigned), a.offset, a.offset % 4, unsigned(a.offset <= R.properties.limits.maxVertexInputAttributeOffset),
              unsigned(!cpu && R.vertexFormats.fallback(nativeFormat)), observedStride, static_cast<unsigned long long>(attributeEnd),
              unsigned(observedStride && a.offset >= observedStride), unsigned(observedStride && attributeEnd > observedStride), unsigned(cpu));
      }
      if (!cpu && R.vertexFormats.fallback(nativeFormat)) {
        if (tailLocation >= R.properties.limits.maxVertexInputAttributes ||
            attributes.size() >= R.properties.limits.maxVertexInputAttributes)
          exception_report::raise("RGB fallback exceeds device vertex attribute limit");
        const uint64_t tailOffset = uint64_t(a.offset) + vk::vertex_tail_offset(nativeFormat);
        if (tailOffset > R.properties.limits.maxVertexInputAttributeOffset)
          exception_report::raise("RGB fallback exceeds device vertex attribute offset limit");
        attributes.push_back({tailLocation++, g.attributeBufferIndex,
            vk::vertex_tail_format(nativeFormat), uint32_t(tailOffset)});
      }
      // Split fetches cover exactly the native attribute bytes.
      if (trim.supported) {
        const uint32_t width = vertex_format_bytes(vk::vertex_format(a.format));
        if (!width || (trim.rate && *trim.rate != a.fetchType) ||
            (a.fetchType != LatteConst::VertexFetchType2::VERTEX_DATA &&
             a.fetchType != LatteConst::VertexFetchType2::INSTANCE_DATA) ||
            (a.fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA && a.aluDivisor != 1))
          trim.supported = false;
        else {
          trim.rate = a.fetchType;
          trim.attributeEnd = std::max(trim.attributeEnd, uint64_t(a.offset) + width);
        }
      }
      if (fetchRate && *fetchRate != a.fetchType)
        exception_report::raise("mixed vertex/instance rate in one buffer");
      fetchRate = a.fetchType;
      if (a.fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA) {
        if (a.aluDivisor != 1)
          exception_report::raise("instance divisor unsupported");
        instance = true;
      }
    }
    uint32_t stride =
        (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >>
         11) &
        0xFFFF;
#ifdef __ANDROID__
    static std::set<uint32_t> seenStrides;
    if (seenStrides.size() < 32 && seenStrides.insert(stride).second)
      LOG("[vulkan stride observed] stride=%u mod4=%u withinLimit=%u", stride, stride % 4,
          stride <= R.properties.limits.maxVertexInputBindingStride);
#endif
    const auto rate = instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
    if (cpuCount < uint32_t(g.attribCount)) bindings.push_back({g.attributeBufferIndex, stride, rate});
    if (cpuCount) bindings.push_back({cpuBinding, stride ? 16 * cpuCount : 0, rate});
    trim.stride = stride;
    p.bindingTrims.push_back(trim);
  }
  if (R.vertexFormats.fallbackMask && attributes.size() > R.properties.limits.maxVertexInputAttributes)
    exception_report::raise("RGB fallback exceeds device vertex attribute limit");
#ifdef WWHD_NATIVE_DRAW
  if (const auto* in = g_nativeInput) {
    bindings.push_back({in->binding, in->stride, VK_VERTEX_INPUT_RATE_INSTANCE});
    for (uint32_t i = 0; i < in->locations.size(); ++i)
      attributes.push_back({in->locations[i], in->binding, VK_FORMAT_R32G32B32A32_SINT, 16 * i});
  }
#endif
  VkPipelineVertexInputStateCreateInfo vi{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = bindings.size();
  vi.pVertexBindingDescriptions = bindings.data();
  vi.vertexAttributeDescriptionCount = attributes.size();
  vi.pVertexAttributeDescriptions = attributes.data();
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = topology;
  // Strip restart is native on Metal and required by MoltenVK portability.
  ia.primitiveRestartEnable = topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
                              topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  VkPipelineViewportStateCreateInfo vp{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = vp.scissorCount = 1;
  LATTE_PA_SU_SC_MODE_CNTL pm;
  std::memcpy(&pm, r + REGADDR::PA_SU_SC_MODE_CNTL, 4);
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  LATTE_PA_CL_CLIP_CNTL pipelineClip;
  std::memcpy(&pipelineClip, r + REGADDR::PA_CL_CLIP_CNTL, 4);
  // Far-plane clipping off: depth clamp, or without the device feature the vertex shader's clamp at
  // the near plane (vk::depth_clamp_variant_wanted, part of the vertex shader's key)
  if (pipelineClip.get_ZCLIP_FAR_DISABLE() && R.enabledFeatures.depthClamp)
    rs.depthClampEnable = VK_TRUE;
  // panvk: both shaders clip this draw, so its depth is clamped instead of culled (clip_emulation.h);
  // a draw whose stages couldn't both take the rewrite keeps the hardware path (translate_stages)
  if (vk::clip_emulation::pair_clips(vs, ps))
    rs.depthClampEnable = VK_TRUE;
  rs.lineWidth = 1;
  rs.cullMode = (pm.get_CULL_FRONT() ? VK_CULL_MODE_FRONT_BIT : 0) |
                (pm.get_CULL_BACK() ? VK_CULL_MODE_BACK_BIT : 0);
  rs.frontFace =
      pm.get_FRONT_FACE() == LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW
          ? VK_FRONT_FACE_COUNTER_CLOCKWISE
          : VK_FRONT_FACE_CLOCKWISE;
  rs.depthBiasEnable = pm.get_OFFSET_FRONT_ENABLED();
  rs.depthBiasConstantFactor = f32(r[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET]);
  rs.depthBiasSlopeFactor = f32(r[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE]) / 16;
  if (rs.depthBiasEnable) {
    rs.depthBiasClamp = f32(r[REGADDR::PA_SU_POLY_OFFSET_CLAMP]);
    if (rs.depthBiasClamp != 0 && !R.enabledFeatures.depthBiasClamp)
      exception_report::raise("device lacks depth bias clamp requested by guest");
  }
  VkPipelineMultisampleStateCreateInfo ms{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  LATTE_DB_DEPTH_CONTROL dc;
  std::memcpy(&dc, r + REGADDR::DB_DEPTH_CONTROL, 4);
  VkPipelineDepthStencilStateCreateInfo ds{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = depth && dc.get_Z_ENABLE();
  ds.depthWriteEnable = depth && dc.get_Z_WRITE_ENABLE();
  ds.depthCompareOp = VkCompareOp(dc.get_Z_FUNC());
  ds.stencilTestEnable = depth && depth->fmt.stencil && dc.get_STENCIL_ENABLE();
  ds.front = {stencil(static_cast<uint32_t>(dc.get_STENCIL_FAIL_F())),
              stencil(static_cast<uint32_t>(dc.get_STENCIL_ZPASS_F())),
              stencil(static_cast<uint32_t>(dc.get_STENCIL_ZFAIL_F())),
              VkCompareOp(dc.get_STENCIL_FUNC_F()),
              (r[REGADDR::DB_STENCILREFMASK] >> 8) & 255,
              (r[REGADDR::DB_STENCILREFMASK] >> 16) & 255,
              r[REGADDR::DB_STENCILREFMASK] & 255};
  ds.back = ds.front;
  if (dc.get_BACK_STENCIL_ENABLE())
    ds.back = {stencil(static_cast<uint32_t>(dc.get_STENCIL_FAIL_B())),
               stencil(static_cast<uint32_t>(dc.get_STENCIL_ZPASS_B())),
               stencil(static_cast<uint32_t>(dc.get_STENCIL_ZFAIL_B())),
               VkCompareOp(dc.get_STENCIL_FUNC_B()),
               (r[REGADDR::DB_STENCILREFMASK_BF] >> 8) & 255,
               (r[REGADDR::DB_STENCILREFMASK_BF] >> 16) & 255,
               r[REGADDR::DB_STENCILREFMASK_BF] & 255};
  std::array<VkPipelineColorBlendAttachmentState, 8> cb{};
  for (uint32_t i = 0; i < ncolor; i++) {
    auto &b = cb[i];
    b.colorWriteMask = ((r[REGADDR::CB_TARGET_MASK] & ~clearMask) >> (4 * i)) & 15;
    b.blendEnable = colors[i] && colors[i]->fmt.kind == FormatInfo::FLOAT &&
                    ((r[REGADDR::CB_COLOR_CONTROL] >> (8 + i)) & 1) && !((clearMask >> (4 * i)) & 15);
    LATTE_CB_BLENDN_CONTROL raw;
    std::memcpy(&raw, r + REGADDR::CB_BLEND0_CONTROL + i, 4);
    b.srcColorBlendFactor =
        blend(static_cast<uint32_t>(raw.get_COLOR_SRCBLEND()));
    b.dstColorBlendFactor =
        blend(static_cast<uint32_t>(raw.get_COLOR_DSTBLEND()));
    b.colorBlendOp = blendop(static_cast<uint32_t>(raw.get_COLOR_COMB_FCN()));
    b.srcAlphaBlendFactor =
        raw.get_SEPARATE_ALPHA_BLEND()
            ? blend(static_cast<uint32_t>(raw.get_ALPHA_SRCBLEND()))
            : b.srcColorBlendFactor;
    b.dstAlphaBlendFactor =
        raw.get_SEPARATE_ALPHA_BLEND()
            ? blend(static_cast<uint32_t>(raw.get_ALPHA_DSTBLEND()))
            : b.dstColorBlendFactor;
    b.alphaBlendOp =
        raw.get_SEPARATE_ALPHA_BLEND()
            ? blendop(static_cast<uint32_t>(raw.get_ALPHA_COMB_FCN()))
            : b.colorBlendOp;
  }
  if (covSlot && covSlot < ncolor) cb[covSlot] = coverage_blend(cb[0]);  // (60 fps (generated): hud_coverage.h)
  VkPipelineColorBlendStateCreateInfo bs{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  bs.attachmentCount = ncolor;
  bs.pAttachments = cb.data();
  uint32_t rop = (r[REGADDR::CB_COLOR_CONTROL] >> 16) & 255;
  bs.logicOp = VK_LOGIC_OP_COPY;
  if (rop != 0xCC) {
    if (!R.enabledFeatures.logicOp)
      exception_report::raise("color logic operation unsupported by device");
    bs.logicOpEnable = VK_TRUE;
    switch (rop) {
    case 0x00:
      bs.logicOp = VK_LOGIC_OP_CLEAR;
      break;
    case 0x88:
      bs.logicOp = VK_LOGIC_OP_AND;
      break;
    case 0x44:
      bs.logicOp = VK_LOGIC_OP_AND_REVERSE;
      break;
    case 0x22:
      bs.logicOp = VK_LOGIC_OP_AND_INVERTED;
      break;
    case 0xAA:
      bs.logicOp = VK_LOGIC_OP_NO_OP;
      break;
    case 0x66:
      bs.logicOp = VK_LOGIC_OP_XOR;
      break;
    case 0xEE:
      bs.logicOp = VK_LOGIC_OP_OR;
      break;
    case 0x11:
      bs.logicOp = VK_LOGIC_OP_NOR;
      break;
    case 0x99:
      bs.logicOp = VK_LOGIC_OP_EQUIVALENT;
      break;
    case 0x55:
      bs.logicOp = VK_LOGIC_OP_INVERT;
      break;
    case 0xDD:
      bs.logicOp = VK_LOGIC_OP_OR_REVERSE;
      break;
    case 0x33:
      bs.logicOp = VK_LOGIC_OP_COPY_INVERTED;
      break;
    case 0xBB:
      bs.logicOp = VK_LOGIC_OP_OR_INVERTED;
      break;
    case 0x77:
      bs.logicOp = VK_LOGIC_OP_NAND;
      break;
    case 0xFF:
      bs.logicOp = VK_LOGIC_OP_SET;
      break;
    default:
      exception_report::raise("unsupported color logic operation");
    }
  }
  for (uint32_t i = 0; i < ncolor; ++i)
    if (cb[i].blendEnable) {
      auto &b = cb[i];
      if (!R.enabledFeatures.dualSrcBlend &&
          (b.srcColorBlendFactor >= VK_BLEND_FACTOR_SRC1_COLOR ||
           b.dstColorBlendFactor >= VK_BLEND_FACTOR_SRC1_COLOR ||
           b.srcAlphaBlendFactor >= VK_BLEND_FACTOR_SRC1_COLOR ||
           b.dstAlphaBlendFactor >= VK_BLEND_FACTOR_SRC1_COLOR))
        exception_report::raise("dual-source blend unsupported by device");
      if (!R.constantAlphaColorBlendFactors &&
          (b.srcColorBlendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
           b.srcColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA ||
           b.dstColorBlendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
           b.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA))
        exception_report::raise(
            "constant alpha color blend unsupported by device");
    }
  if (ds.stencilTestEnable && !R.separateStencilMaskRef &&
      (ds.front.compareMask != ds.back.compareMask ||
       ds.front.writeMask != ds.back.writeMask ||
       ds.front.reference != ds.back.reference))
    exception_report::raise("separate stencil state unsupported by device");
  VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                          VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                          VK_DYNAMIC_STATE_STENCIL_REFERENCE};
  VkPipelineDynamicStateCreateInfo dy{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = std::size(dyn);
  dy.pDynamicStates = dyn;
  VkPipelineRenderingCreateInfo rc{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rc.colorAttachmentCount = ncolor;
  rc.pColorAttachmentFormats = formats.data();
  rc.depthAttachmentFormat = df;
  rc.stencilAttachmentFormat =
      depth && depth->fmt.stencil ? df : VK_FORMAT_UNDEFINED;
  VkGraphicsPipelineCreateInfo ci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline_targets(ci, rc);
  ci.stageCount = 2;
  ci.pStages = stages;
  ci.pVertexInputState = &vi;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState = &vp;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState = &ms;
  ci.pDepthStencilState = &ds;
  ci.pColorBlendState = &bs;
  ci.pDynamicState = &dy;
  ci.layout = p.layout;
  GraphicsPipelineState pipelineState(ci, &rc);
  WarmPipeline warmed;
  // debug: WWHD_VK_RECIPE_KEY_DUMP=file appends the portable key (hex) of every pipeline derived for the setup's
  // preparation ("D") and of every pipeline the game asks for that the recipe database does not have ("M")
  static FILE* keyDump = [] { const char* p = getenv("WWHD_VK_RECIPE_KEY_DUMP"); return p && *p ? std::fopen(p, "a") : nullptr; }();
  auto dumpKey = [&](char kind) {
    if (!keyDump) return;
    std::fputc(kind, keyDump); std::fputc(' ', keyDump);
    for (uint8_t byte : recipeKey) std::fprintf(keyDump, "%02x", byte);
    std::fputc('\n', keyDump); std::fflush(keyDump);
  };
  if (variant.background) {
    // (not a template miss: masked variants are never recorded)
  } else if (!g_recipeSink && !covSlot) {
    ++R.pipelineKeysNew;
    const bool known = pipeline_recipe_known(recipeKey);
    R.pipelineKeysKnown += known;
    if (!known) dumpKey('M');
  } else dumpKey('D');
  if (g_recipeSink) {
    for (size_t i = 0; i < shaderBinaries.size(); ++i)
      if (recipeSpirv[i].empty() && shaderBinaries[i]) recipeSpirv[i] = vk::shader_binary_words_exact(shaderBinaries[i], R.frame);
    for (auto module : modules) if (module) vkDestroyShaderModule(R.device, module, nullptr);
    vkDestroyPipelineLayout(R.device, p.layout, nullptr);
    for (auto set : p.sets) vkDestroyDescriptorSetLayout(R.device, set, nullptr);
    if (!recipeSpirv[0].empty() && !recipeSpirv[1].empty()) {
      auto recipe = std::make_shared<PipelineRecipe>();
      recipe->key = std::move(recipeKey); recipe->spirv = std::move(recipeSpirv);
      recipe->descriptors = std::move(descriptorBindings);
      recipe->dynamicUniforms = p.dynamicUniforms;
      recipe->state = std::make_unique<GraphicsPipelineState>(std::move(pipelineState));
      g_recipeSink->push_back(std::move(recipe));
    }
    // (not kept in the map: the same key from another recipe is derived again and dropped as a repeat by the caller)
    static Pipeline none;
    none = Pipeline{};
    return none;
  }
  if (!covSlot && !variant.background && !recipeSpirv[0].empty() && !recipeSpirv[1].empty() &&
      take_warm_pipeline(recipeKey, recipeSpirv, descriptorBindings, p.dynamicUniforms, pipelineState, warmed)) {
    for (auto module : modules) if (module) vkDestroyShaderModule(R.device, module, nullptr);
    vkDestroyPipelineLayout(R.device, p.layout, nullptr);
    for (auto set : p.sets) vkDestroyDescriptorSetLayout(R.device, set, nullptr);
    p.pipeline = warmed.pipeline; p.layout = warmed.layout;
    std::copy(std::begin(warmed.sets), std::end(warmed.sets), p.sets);
    if (stutter::trace().enabled()) {
      const auto now = stutter::now();
      stutter::trace().add("startup_pipeline_hit", R.frame, PipelineKeyHash{}(key), now, now, 0);
    }
    return remember(pipelines.emplace(key, std::move(p)).first->second);
  }
  if (!variant.background) pipeline_groups::note_created();
  // the setup's model sweep, record only (WWHD_DSWEEP_RECORD_ONLY=1): the recipe is recorded as when the pipeline is
  // made, but the driver never compiles it; its draws are left out (nobody sees the hidden sweep)
  if (recordOnly && !covSlot) {
    for (size_t i = 0; i < shaderBinaries.size(); ++i)
      if (recipeSpirv[i].empty() && shaderBinaries[i]) recipeSpirv[i] = vk::shader_binary_words_exact(shaderBinaries[i], R.frame);
    for (auto module : modules) if (module) vkDestroyShaderModule(R.device, module, nullptr);
    if (!recipeSpirv[0].empty() && !recipeSpirv[1].empty()) {
      auto recipe = std::make_shared<PipelineRecipe>();
      recipe->key = std::move(recipeKey); recipe->spirv = std::move(recipeSpirv);
      recipe->descriptors = std::move(descriptorBindings);
      recipe->dynamicUniforms = p.dynamicUniforms;
      recipe->state = std::make_unique<GraphicsPipelineState>(std::move(pipelineState));
      for (const uint32_t group : pipeline_groups::sweeping_all()) recipe->add_group(group);
      remember_pipeline_recipe(std::move(recipe));
    }
    p.pipeline = VK_NULL_HANDLE;
    return remember(pipelines.emplace(key, std::move(p)).first->second);
  }
  const auto pipelineStarted = std::chrono::steady_clock::now();
  VkResult result;
  uint64_t compileNs = 0;
  if (async_pipeline_enabled()) {
    auto pending = std::make_shared<PendingPipeline>();
    pending->device = R.device;
    std::copy(std::begin(modules), std::end(modules), pending->modules);
    auto recipe = std::make_shared<PipelineRecipe>();
    recipe->key = std::move(recipeKey); recipe->spirv = std::move(recipeSpirv);
    recipe->descriptors = std::move(descriptorBindings);
    recipe->dynamicUniforms = p.dynamicUniforms;
    recipe->state = std::make_unique<GraphicsPipelineState>(std::move(pipelineState));
    if (!variant.background)
      for (const uint32_t group : pipeline_groups::sweeping_all()) recipe->add_group(group);
    pending->recipe = std::move(recipe);
    pending->keepRecipe = !covSlot && !variant.background;
    pending->background = variant.background;
    // The owner waits for its ticket before destroying modules or itself.
    // Capturing a shared owner here would form an owner/ticket/job cycle.
    pending->shaders[0]=vs; pending->shaders[1]=ps;
    auto* owner=pending.get();
    const VkDevice device=pending->device;
    const std::function<void(GraphicsPipelineState&)> prepare=[owner,device,shaderBinaries](GraphicsPipelineState& ownedState) {
      for(uint32_t i=0;i<2;++i) {
        auto& words=owner->recipe->spirv[i];
        if(words.empty()) words=vk::shader_binary_words(shaderBinaries[i]);
        if(words.empty()) throw std::runtime_error("empty pipeline shader binary");
        VkShaderModuleCreateInfo mc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mc.codeSize=words.size()*sizeof(uint32_t);mc.pCode=words.data();
        vk_check(vkCreateShaderModule(device,&mc,nullptr,&owner->modules[i]),"shader module");
        ownedState.set_stage_module(i,owner->modules[i]);
        owner->recipe->state->set_stage_module(i,owner->modules[i]);
      }
    };
    if (variant.background) {
      pending->ticket = begin_pipeline_background(*pending->recipe->state, R.frame,
          PipelineKeyHash{}(key), "masked_pipeline", prepare);
      if (!pending->ticket) {  // the lane filled up after all: nothing compiled here, nothing stored
        ++g_backgroundDeclined;
        pending.reset();  // (no modules yet: they are made by prepare, on the worker)
        vkDestroyPipelineLayout(R.device, p.layout, nullptr);
        for (auto set : p.sets) vkDestroyDescriptorSetLayout(R.device, set, nullptr);
        return declined_pipeline();
      }
      ++g_backgroundAdmitted;
      p.pendingCompile = std::move(pending);
      return remember(pipelines.emplace(key, std::move(p)).first->second);
    }
    pending->ticket = begin_pipeline_async(*pending->recipe->state, R.frame,
        PipelineKeyHash{}(key), "guest_pipeline", prepare);
    if (!pending->ticket) {
      ++g_pipelineSyncCompiles;
      // Capacity pressure always completes this draw exactly. No new snapshot
      // is retained and no silently omitted draw is introduced by admission.
      deferred_commands().finish_exact();
      begin_pipeline_pending_frame();
      auto built = build_pipeline_async(*pending->recipe->state, R.frame,
          PipelineKeyHash{}(key), "guest_pipeline", present_completed_frame_while_pipeline_pending, prepare);
      R.pipelineCreateNs += built.compileNs; ++R.pipelineCreates;
      if(recoverable_pipeline_failure(built.result))
        capture_pipeline_failure(built.result,vs,ps,pending->recipe->words(0),pending->recipe->words(1));
      else vk_check(built.result, "graphics pipeline");
      p.pipeline = built.pipeline;
      if(p.pipeline && !covSlot) remember_pipeline_recipe(std::move(pending->recipe));
      pending.reset();
      R.pipelineCacheDirty = true; R.pipelineCacheChangedFrame = R.frame;
    } else p.pendingCompile = std::move(pending);
    return remember(pipelines.emplace(key, std::move(p)).first->second);
  } else {
    ++g_pipelineSyncCompiles;
    result = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, pipelineState.get(), nullptr, &p.pipeline);
  }
  const bool recordRecipe = result == VK_SUCCESS && !covSlot;
  if (!async_pipeline_enabled() && stutter::trace().enabled())
    stutter::trace().add("guest_pipeline", R.frame, PipelineKeyHash{}(key),
        std::chrono::duration_cast<std::chrono::nanoseconds>(pipelineStarted.time_since_epoch()).count(),
        stutter::now(), int(result));
  R.pipelineCreateNs += async_pipeline_enabled() ? compileNs :
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-pipelineStarted).count();
  ++R.pipelineCreates;
  for (auto m : modules)
    vkDestroyShaderModule(R.device, m, nullptr);
  if (recoverable_pipeline_failure(result)) {
    p.pipeline = VK_NULL_HANDLE;
    capture_pipeline_failure(result,vs,ps,recipeSpirv[0],recipeSpirv[1]);
    return remember(pipelines.emplace(key,p).first->second);
  }
  vk_check(result, "graphics pipeline");
  if (recordRecipe) {
    auto recipe = std::make_shared<PipelineRecipe>();
    recipe->key = std::move(recipeKey); recipe->spirv = std::move(recipeSpirv);
    recipe->descriptors = std::move(descriptorBindings);
    recipe->dynamicUniforms = p.dynamicUniforms;
    recipe->state = std::make_unique<GraphicsPipelineState>(std::move(pipelineState));
    for (const uint32_t group : pipeline_groups::sweeping_all()) recipe->add_group(group);
    remember_pipeline_recipe(std::move(recipe));
  }
  R.pipelineCacheDirty = true;
  R.pipelineCacheChangedFrame = R.frame;
  return remember(pipelines.emplace(key, p).first->second);
}
struct SamplerMemo {
  VkDevice device = VK_NULL_HANDLE;
  std::array<uint32_t, 3> words{};
  bool compare = false, integer = false, forceAniso = false, nearestOnly = false;
  VkSampler value = VK_NULL_HANDLE;
  bool matches(VkDevice nextDevice, const uint32_t* nextWords,
               bool nextCompare, bool nextInteger, bool nextForceAniso, bool nextNearestOnly) const {
    return value && device == nextDevice && compare == nextCompare &&
        integer == nextInteger && forceAniso == nextForceAniso && nearestOnly == nextNearestOnly &&
        words[0] == nextWords[0] && words[1] == nextWords[1] && words[2] == nextWords[2];
  }
  void remember(VkDevice nextDevice, const uint32_t* nextWords,
                bool nextCompare, bool nextInteger, bool nextForceAniso, bool nextNearestOnly, VkSampler nextValue) {
    device = nextDevice;
    std::memcpy(words.data(), nextWords, sizeof(words));
    compare = nextCompare; integer = nextInteger; forceAniso = nextForceAniso; nearestOnly = nextNearestOnly;
    value = nextValue;
  }
};
SamplerMemo samplerMemo;
// what samplers of hand-drawn mip textures add to their LOD bias in the draw being prepared: log2 of its targets'
// internal resolution scale (gfx/artistic_mips.h; set by draw() before its stages are bound)
float g_artisticBias = 0;
VkSampler sampler(const uint32_t *words, bool compare, bool integer, bool allowAniso, bool linearSupported,
                  float extraBias = 0) {
  const bool nearestOnly = sampler_nearest_only(integer, compare, linearSupported);
  static const bool memo = [] {
    const char* e = std::getenv("WWHD_VK_SAMPLER_MEMO");
    return e && !std::strcmp(e, "1");
  }();
  const bool forceAniso = allowAniso && aniso_enabled();
  const bool stats = preparation_stats_enabled();
  if (stats) ++R.cpuPreparation.samplerRequests;
  if (memo && extraBias == 0 && samplerMemo.matches(R.device, words, compare, integer, forceAniso, nearestOnly)) {
    if (stats) ++R.cpuPreparation.samplerMemoHits;
    return samplerMemo.value;
  }
  if (stats) ++R.cpuPreparation.samplerMapLookups;
  auto remember = [&](VkSampler value) {
    if (memo && extraBias == 0) samplerMemo.remember(R.device, words, compare, integer, forceAniso, nearestOnly, value);
    return value;
  };
  std::string key(reinterpret_cast<const char *>(words), 12);
  key.push_back(compare);
  key.push_back(integer);
  key.push_back(forceAniso);
  key.push_back(nearestOnly);
  if (extraBias != 0) key.append(reinterpret_cast<const char*>(&extraBias), sizeof extraBias);
  static std::unordered_map<std::string, VkSampler> cache;
  if (auto it = cache.find(key); it != cache.end())
    return remember(it->second);
  LATTE_SQ_TEX_SAMPLER_WORD0_0 w;
  LATTE_SQ_TEX_SAMPLER_WORD1_0 w1;
  std::memcpy(&w, words, 4);
  std::memcpy(&w1, words + 1, 4);
  auto filter = [](uint32_t v) {
    return v == 0 || v == 4 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  };
  auto addr = [](uint32_t v) {
    switch (v) {
    case 0:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case 1:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3:
      if (!R.samplerMirrorClampToEdge)
        exception_report::raise("mirror-once sampler unsupported by device");
      return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    case 5:
    case 7:
      exception_report::raise(
          "mirror-once border sampling is not implemented");
    case 2:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    default:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
  };
  VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  ci.magFilter = filter(static_cast<uint32_t>(w.get_XY_MAG_FILTER()));
  ci.minFilter = filter(static_cast<uint32_t>(w.get_XY_MIN_FILTER()));
  if (nearestOnly)
    ci.minFilter = ci.magFilter = VK_FILTER_NEAREST;
  ci.mipmapMode = static_cast<uint32_t>(w.get_MIP_FILTER()) == 2
                      ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                      : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  ci.addressModeU = addr(static_cast<uint32_t>(w.get_CLAMP_X()));
  ci.addressModeV = addr(static_cast<uint32_t>(w.get_CLAMP_Y()));
  ci.addressModeW = addr(static_cast<uint32_t>(w.get_CLAMP_Z()));
  ci.mipLodBias = std::clamp(::gfx::sampler_lod_bias::levels(words[1]) + extraBias,
                             -R.properties.limits.maxSamplerLodBias,
                             R.properties.limits.maxSamplerLodBias);
  if (!R.samplerMipLodBias && ci.mipLodBias != 0) {
    // MoltenVK cannot apply sampler LOD bias: the translated shaders add it
    // (gfx/sampler_lod_bias.h, uf_texNLodBias); native Vulkan devices keep it here.
    static bool reported = false;
    if (!reported) {
      LOG("[vulkan] device lacks sampler LOD bias; the shaders add it");
      reported = true;
    }
    ci.mipLodBias = 0;
  }
  ci.minLod = w1.get_MIN_LOD() / 64.f;
  ci.maxLod =
      static_cast<uint32_t>(w.get_MIP_FILTER()) ? w1.get_MAX_LOD() / 64.f : 0;
  ci.compareEnable = compare;
  ci.compareOp = VkCompareOp(w.get_DEPTH_COMPARE_FUNCTION());
  uint32_t anisotropy = w.get_MAX_ANISO_RATIO()
      ? std::min(1u << std::min(uint32_t(w.get_MAX_ANISO_RATIO()), 4u), 16u) : 1;
  if (forceAniso && uint32_t(w.get_MIP_FILTER()) != 0 && ci.minFilter == VK_FILTER_LINEAR &&
      uint32_t(w.get_DEPTH_COMPARE_FUNCTION()) == 0)
    anisotropy = 16;
  if (R.enabledFeatures.samplerAnisotropy && !nearestOnly && anisotropy > 1) {
    ci.anisotropyEnable = VK_TRUE;
    ci.maxAnisotropy = std::min(float(anisotropy), R.properties.limits.maxSamplerAnisotropy);
  }

  ci.borderColor = static_cast<uint32_t>(w.get_BORDER_COLOR_TYPE()) == 1
                       ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
                   : static_cast<uint32_t>(w.get_BORDER_COLOR_TYPE()) == 2
                       ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                       : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  if (integer) {
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.borderColor = VkBorderColor(uint32_t(ci.borderColor) + 1);
  }
  restrict_sampler_filtering(ci, nearestOnly);
  VkSampler s;
  vk_check(vkCreateSampler(R.device, &ci, nullptr, &s), "sampler");
  cache.emplace(std::move(key), s);
  return remember(s);
}
struct StageResources {
  VkDescriptorSet set{};
  std::array<uint32_t, 17> dynamicOffsets{};
  uint32_t dynamicOffsetCount = 0;
};
// Stereo: a stage of eye 0's draw that samples only textures both eyes share (stereo.h). Eye 1
// binds the same descriptor set; only the uniform buffers eye 0's eye shift changed get eye 1's
// copy, at another dynamic offset.
struct EyeUniform {
  uint32_t dynamicIndex = 0;  // in StageResources::dynamicOffsets
  VkBuffer buffer = VK_NULL_HANDLE;  // the descriptor set's buffer
  std::vector<uint8_t> bytes;  // eye 0's upload
  stereo::EyeRows rows;
};
struct StageReplay {
  bool valid = false;
  StageResources resources;
  std::array<EyeUniform, 4> uniforms;
  uint32_t uniformCount = 0;
  std::array<Surface*, LATTE_NUM_MAX_TEX_UNITS> textures{};  // shared with eye 1
  uint32_t textureCount = 0;
  // the other textures: eye 1 resolves its own images, then writes its own set from these writes
  struct EyeTexture { uint32_t unit = 0; int binding = -1; float sx = 1, sy = 1; uint32_t write = 0; };
  std::array<EyeTexture, LATTE_NUM_MAX_TEX_UNITS> eyeTextures{};
  uint32_t eyeTextureCount = 0;
  VkDescriptorSetLayout layout{};
  uint32_t writeCount = 0;
  std::array<VkWriteDescriptorSet, 17 + LATTE_NUM_MAX_TEX_UNITS> writes{};  // without info pointers
  std::array<VkDescriptorBufferInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> bufferInfos{};
  std::array<VkDescriptorImageInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> imageInfos{};
};
VkDescriptorSet stage_descriptor_set(bool vertex, VkDescriptorSetLayout layout,
                                     VkWriteDescriptorSet* writes, uint32_t writeCount);
// Exact consecutive descriptor state, optionally used to omit redundant binds.
// Lifetime is the tracked draw pass; explicit generation/command/layout keys
// also exclude repeated handles after command/descriptor pool resets.
struct DescriptorBindProbe {
  struct Matches { bool whole=false;std::array<bool,2> stage{}; };
  struct BindRange { uint32_t first=0,count=2,offsetBegin=0,offsetCount=0; };
  static BindRange bind_range(Matches matches,bool skip,uint32_t vsCount,uint32_t psCount) {
    if(skip && matches.whole) return {0,0,0,0};
    if(skip && matches.stage[0]) return {1,1,vsCount,psCount};
    if(skip && matches.stage[1]) return {0,1,0,vsCount};
    return {0,2,0,vsCount+psCount};
  }
  VkCommandBuffer command=VK_NULL_HANDLE;
  uint64_t generation=0;
  VkPipelineLayout layout=VK_NULL_HANDLE;
  std::array<VkDescriptorSet,2> sets{};
  std::array<uint32_t,2> counts{};
  std::array<uint32_t,34> offsets{};
  bool valid=false;
  Matches observe(VkCommandBuffer nextCommand,uint64_t nextGeneration,
                  VkPipelineLayout nextLayout,const VkDescriptorSet* nextSets,
                  const uint32_t* nextOffsets,uint32_t vsCount,uint32_t psCount) {
    Matches matches;
    if(vsCount>17 || psCount>17) { *this={};return matches; }
    const std::array<uint32_t,2> nextCounts{vsCount,psCount};
    if(valid && command==nextCommand && generation==nextGeneration && layout==nextLayout) {
      for(size_t stage=0;stage<2;++stage) {
        const size_t oldBegin=stage?counts[0]:0,nextBegin=stage?vsCount:0;
        matches.stage[stage]=sets[stage]==nextSets[stage] && counts[stage]==nextCounts[stage] &&
            (!nextCounts[stage] || !std::memcmp(offsets.data()+oldBegin,
                nextOffsets+nextBegin,nextCounts[stage]*sizeof(uint32_t)));
      }
      matches.whole=matches.stage[0]&&matches.stage[1];
    }
    command=nextCommand;generation=nextGeneration;layout=nextLayout;
    std::copy_n(nextSets,2,sets.begin());counts=nextCounts;
    if(vsCount+psCount)std::copy_n(nextOffsets,vsCount+psCount,offsets.begin());
    valid=true;
    return matches;
  }
};
struct DescriptorIdentity {
  uint32_t binding = 0;
  VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLER;
  VkDescriptorBufferInfo buffer{};
  VkDescriptorImageInfo image{};
  bool isBuffer = false;
};
struct LastDescriptorSet {
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  uint32_t count = 0;
  std::array<DescriptorIdentity, 17 + LATTE_NUM_MAX_TEX_UNITS> identities{};
};
bool identities_match(const DescriptorIdentity *identities, const VkWriteDescriptorSet *writes,
                      uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    const auto &identity = identities[i];
    const auto &write = writes[i];
    if (identity.binding != write.dstBinding || identity.type != write.descriptorType ||
        identity.isBuffer != bool(write.pBufferInfo)) return false;
    if (write.pBufferInfo) {
      const auto &info = *write.pBufferInfo;
      if (identity.buffer.buffer != info.buffer || identity.buffer.range != info.range ||
          (write.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
           identity.buffer.offset != info.offset)) return false;
    } else {
      const auto &info = *write.pImageInfo;
      if (identity.image.sampler != info.sampler || identity.image.imageView != info.imageView ||
          identity.image.imageLayout != info.imageLayout) return false;
    }
  }
  return true;
}
void store_identities(DescriptorIdentity *identities, const VkWriteDescriptorSet *writes,
                      uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    auto &identity = identities[i];
    const auto &write = writes[i];
    identity.binding = write.dstBinding;
    identity.type = write.descriptorType;
    identity.isBuffer = write.pBufferInfo != nullptr;
    if (identity.isBuffer) identity.buffer = *write.pBufferInfo;
    else identity.image = *write.pImageInfo;
  }
}
bool descriptor_matches(const LastDescriptorSet &last,
                        VkDescriptorSetLayout layout,
                        const VkWriteDescriptorSet *writes, uint32_t count) {
  return last.set && last.layout == layout && last.count == count &&
         identities_match(last.identities.data(), writes, count);
}
void remember_descriptors(LastDescriptorSet &last, VkDescriptorSetLayout layout,
                          VkDescriptorSet set, const VkWriteDescriptorSet *writes,
                          uint32_t count) {
  last.layout = layout;
  last.set = set;
  last.count = count;
  store_identities(last.identities.data(), writes, count);
}
// Descriptor sets written in this submission, found by a 64-bit hash of the layout and the
// descriptors and confirmed by comparing the stored descriptors. Valid until the submission's
// pool is reset; the vectors and buckets keep their capacity across submissions.
uint64_t descriptor_hash(VkDescriptorSetLayout layout, const VkWriteDescriptorSet *writes,
                         uint32_t count) {
  auto mix = [](uint64_t hash, uint64_t value) {
    hash = (hash ^ value) * 0xFF51AFD7ED558CCDull;
    return hash ^ (hash >> 32);
  };
  uint64_t hash = mix(0x9E3779B97F4A7C15ull, uint64_t(uintptr_t(layout)) ^ (uint64_t(count) << 56));
  for (uint32_t i = 0; i < count; ++i) {
    const auto &write = writes[i];
    hash = mix(hash, uint64_t(write.dstBinding) | (uint64_t(write.descriptorType) << 32));
    if (write.pBufferInfo) {
      const auto &info = *write.pBufferInfo;
      hash = mix(hash, uint64_t(uintptr_t(info.buffer)));
      hash = mix(hash, info.range);
      if (write.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        hash = mix(hash, info.offset);
    } else {
      const auto &info = *write.pImageInfo;
      hash = mix(hash, uint64_t(uintptr_t(info.sampler)));
      hash = mix(hash, uint64_t(uintptr_t(info.imageView)) ^ (uint64_t(info.imageLayout) << 48));
    }
  }
  return hash;
}
struct SubmissionDescriptorCache {
  struct Entry {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t count = 0, first = 0, next = UINT32_MAX;
  };
  std::unordered_map<uint64_t, uint32_t> heads;
  std::vector<Entry> entries;
  std::vector<DescriptorIdentity> identities;
  void clear() { heads.clear(); entries.clear(); identities.clear(); }
  VkDescriptorSet find(uint64_t hash, VkDescriptorSetLayout layout,
                       const VkWriteDescriptorSet *writes, uint32_t count) const {
    auto head = heads.find(hash);
    if (head == heads.end()) return VK_NULL_HANDLE;
    for (uint32_t i = head->second; i != UINT32_MAX; i = entries[i].next) {
      const auto &entry = entries[i];
      if (entry.layout == layout && entry.count == count &&
          identities_match(identities.data() + entry.first, writes, count))
        return entry.set;
    }
    return VK_NULL_HANDLE;
  }
  void insert(uint64_t hash, VkDescriptorSetLayout layout, VkDescriptorSet set,
              const VkWriteDescriptorSet *writes, uint32_t count) {
    Entry entry{layout, set, count, uint32_t(identities.size()), UINT32_MAX};
    identities.resize(identities.size() + count);
    store_identities(identities.data() + entry.first, writes, count);
    auto [head, inserted] = heads.emplace(hash, uint32_t(entries.size()));
    if (!inserted) {
      entry.next = head->second;
      head->second = uint32_t(entries.size());
    }
    entries.push_back(entry);
  }
};
// A descriptor cannot sample the same subresource being written as an
// attachment without a feedback-loop extension. Snapshot before rendering.
// Separate stage/unit slots preserve every descriptor selected for one draw.
constexpr uint32_t kFeedbackUnitsPerStage = 16;
constexpr VkDeviceSize kFeedbackRetainedBudget = 128ull << 20;
struct FeedbackScratch {
  Surface surface;
  VkDevice device = VK_NULL_HANDLE;
  VkDeviceSize allocationBytes = 0;
};
// per stereo eye: each eye's command stream reuses only its own copies (stereo.h)
std::array<std::array<FeedbackScratch, kFeedbackUnitsPerStage * 2>, 2> feedbackScratchEyes;
VkDeviceSize feedbackRetainedBytes = 0;
// the draw (R.drawCount while it is prepared) that last used each retained slot
std::array<std::array<uint64_t, kFeedbackUnitsPerStage * 2>, 2> feedbackUseDrawEyes = [] {
  std::array<std::array<uint64_t, kFeedbackUnitsPerStage * 2>, 2> a;
  for (auto& eye : a) eye.fill(UINT64_MAX);
  return a; }();
bool feedback_compatible(const Surface& copy, const Surface& source) {
  return copy.image && copy.fmt.pixel == source.fmt.pixel &&
         copy.aspect == source.aspect && copy.imageType == source.imageType &&
         copy.viewType == source.viewType && copy.dim == source.dim &&
         copy.extent.width == source.extent.width &&
         copy.extent.height == source.extent.height &&
         copy.extent.depth == source.extent.depth &&
         copy.mips == source.mips && copy.arrayLayers == source.arrayLayers;
}
void make_feedback_image(Surface& copy, const Surface& source) {
  copy = source;
  copy.image = VK_NULL_HANDLE;
  copy.memory = VK_NULL_HANDLE;
  copy.view = VK_NULL_HANDLE;
  copy.layerViews.clear();
  copy.sampledViews.clear();
  copy.guestLayout.reset();
  copy.addr = copy.mipAddr = 0;
  copy.gpuWritten = true;
  copy.dirty = false;
  // The snapshot is already at physical resolution. Shader texture-scale
  // uniforms continue to use source metadata in bind_stage, never this copy.
  copy.width = source.extent.width;
  copy.height = source.extent.height;
  if (source.imageType == VK_IMAGE_TYPE_3D) copy.slices = source.extent.depth;
  create_surface_image(&copy, false);
}
void reset_feedback_scratch() {
  for (auto& feedbackScratch : feedbackScratchEyes)
  for (auto& slot : feedbackScratch) {
    // Reset must run before replacing the device. Foreign handles cannot be
    // placed into this device's deferred retirement lists.
    if (slot.surface.image && slot.device != R.device) continue;
    destroy_surface_image(&slot.surface);
    feedbackRetainedBytes -= slot.allocationBytes;
    slot = {};
  }
}
struct FeedbackStatsProbe {
  struct Key {
    VkDevice device; VkImage image; uint64_t version;
    VkFormat format; VkImageAspectFlags aspect;
    VkImageType imageType; VkImageViewType viewType;
    VkExtent3D extent; uint32_t mips,layers;
  };
  std::array<Key,2*LATTE_NUM_MAX_TEX_UNITS> keys;
  size_t count=0;
  static bool equal(const Key& a,const Key& b) {
    return a.device==b.device && a.image==b.image && a.version==b.version &&
        a.format==b.format && a.aspect==b.aspect && a.imageType==b.imageType &&
        a.viewType==b.viewType && a.extent.width==b.extent.width &&
        a.extent.height==b.extent.height && a.extent.depth==b.extent.depth &&
        a.mips==b.mips && a.layers==b.layers;
  }
  bool observe(const Surface& source) {
    const Key key{R.device,source.image,source.writeSeq,source.fmt.pixel,
        source.aspect,source.imageType,source.viewType,source.extent,
        source.mips,source.arrayLayers};
    for(size_t i=0;i<count;++i) if(equal(keys[i],key)) return true;
    if(count<keys.size()) keys[count++]=key;
    return false;
  }
};
struct FeedbackStats {
  uint64_t aliases=0,duplicates=0,texels=0,duplicateTexels=0;
  uint64_t color=0,depth=0,stencil=0,compressed=0,saturated=0;
};
FeedbackStats feedbackStats;
bool feedback_stats_enabled() {
  static const bool enabled=[] {
    const char* e=std::getenv("WWHD_VK_FEEDBACK_STATS");
    return e && !std::strcmp(e,"1");
  }();
  return enabled;
}
uint64_t feedback_texels(const Surface& source) {
  auto multiply=[](uint64_t a,uint64_t b) {
    return b && a>UINT64_MAX/b ? UINT64_MAX : a*b;
  };
  uint64_t total=0;
  for(uint32_t mip=0;mip<source.mips;++mip) {
    const auto dimension=[&](uint32_t d) { return mip<32?std::max(1u,d>>mip):1u; };
    uint64_t n=multiply(dimension(source.extent.width),dimension(source.extent.height));
    n=multiply(n,dimension(source.extent.depth));n=multiply(n,source.arrayLayers);
    total=n>UINT64_MAX-total?UINT64_MAX:total+n;
  }
  return total;
}
void report_feedback_stats() {
  static uint64_t previousFrame=0;
  if(R.frame-previousFrame<120) return;
  const double frames=double(R.frame-previousFrame);
  const auto& t=feedbackStats;
  LOG("[vulkan feedback copies] %.2f aliases/frame %.2f duplicate source versions/frame; %.3f M physical texels/frame %.3f M duplicate texels/frame; color/depth/stencil/compressed %llu/%llu/%llu/%llu; %llu saturated; copies unchanged",
      t.aliases/frames,t.duplicates/frames,t.texels/frames/1e6,t.duplicateTexels/frames/1e6,
      (unsigned long long)t.color,(unsigned long long)t.depth,(unsigned long long)t.stencil,
      (unsigned long long)t.compressed,(unsigned long long)t.saturated);
  feedbackStats={};previousFrame=R.frame;
}
VkImageView feedback_view(Surface *source, const uint32_t *textureWords,
                          bool vertex, uint32_t unit, FeedbackStatsProbe* probe) {
  if(probe) {
    const bool duplicate=probe->observe(*source);
    const uint64_t texels=feedback_texels(*source);
    if(texels==UINT64_MAX) ++feedbackStats.saturated;
    auto add=[&](uint64_t& sum) { if(texels>UINT64_MAX-sum) {sum=UINT64_MAX;++feedbackStats.saturated;} else sum+=texels; };
    ++feedbackStats.aliases;feedbackStats.duplicates+=duplicate;
    add(feedbackStats.texels);if(duplicate)add(feedbackStats.duplicateTexels);
    feedbackStats.color+=bool(source->aspect&VK_IMAGE_ASPECT_COLOR_BIT);
    feedbackStats.depth+=bool(source->aspect&VK_IMAGE_ASPECT_DEPTH_BIT);
    feedbackStats.stencil+=bool(source->aspect&VK_IMAGE_ASPECT_STENCIL_BIT);
    feedbackStats.compressed+=source->fmt.compressed;
  }
  static const bool reuse = [] {
    const char* e = std::getenv("WWHD_VK_REUSE_FEEDBACK_IMAGES");
    return e && !std::strcmp(e, "1");
  }();
  // Stereo retains allocations, never snapshot contents: every use still copies
  // the source. Compatibility includes extent, format, aspects, mips and layers.
  // Slots are per-eye; feedbackUseDraw prevents two units in one draw from
  // aliasing a slot. transition_image preserves the earlier shader-read -> next
  // transfer-write dependency (including held eye commands); replacements are
  // fence-retired, and retained allocations are bounded by 128 MiB. Mono keeps
  // its prior opt-in policy. This removes repeated allocation work in stereo;
  // it does not establish a Quest performance result.
  auto& feedbackScratch = feedbackScratchEyes[stereoEye];
  auto& feedbackUseDraw = feedbackUseDrawEyes[stereoEye];
  Surface temporary;
  Surface* copy = &temporary;
  FeedbackScratch* slot = (reuse || stereo::frame.mode != stereo::Off) && unit < kFeedbackUnitsPerStage
      ? &feedbackScratch[(vertex ? kFeedbackUnitsPerStage : 0) + unit]
      : nullptr;
  // Device recreation is not a supported lifecycle today; refuse to reuse or
  // retire foreign handles if a caller nevertheless changes the device.
  if (slot && slot->surface.image && slot->device != R.device) slot = nullptr;
  // The game samples render targets of several sizes through the same unit, so one retained
  // image per slot was destroyed and recreated on almost every draw (an expensive kernel memory
  // allocation per draw on Android drivers). Look for a compatible retained image in the other
  // slots that this draw does not use (a draw's units always get distinct copies) and swap it in.
  if (slot && !feedback_compatible(slot->surface, *source)) {
    const size_t mine = slot - feedbackScratch.data();
    for (size_t i = 0; i < feedbackScratch.size(); ++i) {
      auto& other = feedbackScratch[i];
      if (i == mine || other.device != R.device || !feedback_compatible(other.surface, *source)) continue;
      if (feedbackUseDraw[i] == R.drawCount) continue;  // another unit of this draw
      std::swap(*slot, other);
      break;
    }
    // None retained: keep this slot's image for later draws by parking it in an empty slot
    // (draws alternate a few kinds, e.g. the 1280x720 colour and depth copies, through unit 0).
    if (!feedback_compatible(slot->surface, *source) && slot->surface.image) {
      for (size_t i = 0; i < feedbackScratch.size(); ++i) {
        auto& other = feedbackScratch[i];
        if (i == mine || other.surface.image || feedbackUseDraw[i] == R.drawCount) continue;
        std::swap(*slot, other);
        std::swap(feedbackUseDraw[i], feedbackUseDraw[mine]);
        break;
      }
    }
  }
  if (slot) feedbackUseDraw[slot - feedbackScratch.data()] = R.drawCount;
  if (slot && feedback_compatible(slot->surface, *source)) {
    copy = &slot->surface;
  } else {
    if (slot && slot->surface.image) {
      end_encoder();
      destroy_surface_image(&slot->surface);
      feedbackRetainedBytes -= slot->allocationBytes;
      *slot = {};
    }
    make_feedback_image(temporary, *source);
    // debug: WWHD_VK_FEEDBACK_ALLOC_LOG=1 reports feedback image creations every 120 frames
    static const bool allocLog = getenv("WWHD_VK_FEEDBACK_ALLOC_LOG") != nullptr;
    if (allocLog) {
      static uint64_t creates = 0, unslotted = 0, overBudget = 0, lastFrame = 0;
      ++creates;
      if (!slot) ++unslotted;
      if (R.frame - lastFrame >= 120) {
        LOG("[vulkan feedback allocs] %.1f/frame (%.1f without a slot: unit %u, %.1f over budget), retained %.1f MiB, last %ux%u fmt %d mips %u",
            creates / double(R.frame - lastFrame), unslotted / double(R.frame - lastFrame), unit,
            overBudget / double(R.frame - lastFrame), feedbackRetainedBytes / 1048576.0, source->extent.width,
            source->extent.height, int(source->fmt.pixel), source->mips);
        creates = unslotted = overBudget = 0;
        lastFrame = R.frame;
      }
      if (slot) {
        VkMemoryRequirements rq{};
        vkGetImageMemoryRequirements(R.device, temporary.image, &rq);
        if (rq.size > kFeedbackRetainedBudget - feedbackRetainedBytes) ++overBudget;
      }
    }
    if (slot) {
      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(R.device, temporary.image, &requirements);
      if (requirements.size <= kFeedbackRetainedBudget - feedbackRetainedBytes) {
        slot->surface = std::move(temporary);
        temporary = {};
        slot->device = R.device;
        slot->allocationBytes = requirements.size;
        feedbackRetainedBytes += requirements.size;
        copy = &slot->surface;
      }
    }
  }
  transition_image(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  // Retained layout preserves the read→write dependency on preceding draws,
  // including draws in an earlier submission on this same graphics queue.
  transition_image(copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT);
  std::vector<VkImageCopy> regions;
  for (auto aspect : {VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                      VK_IMAGE_ASPECT_STENCIL_BIT}) {
    if (!(source->aspect & aspect))
      continue;
    for (uint32_t mip = 0; mip < source->mips; ++mip) {
      VkImageCopy region{};
      region.srcSubresource = {VkImageAspectFlags(aspect), mip, 0,
                               source->arrayLayers};
      region.dstSubresource = region.srcSubresource;
      region.extent = {std::max(1u, source->extent.width >> mip),
                       std::max(1u, source->extent.height >> mip),
                       std::max(1u, source->extent.depth >> mip)};
      regions.push_back(region);
    }
  }
  GpuScopeToken feedbackTiming;
  if(R.gpuPassTimestampsEnabled) feedbackTiming=gpu_begin_feedback_scope(*source);
  record::vkCmdCopyImage(command_buffer(), source->image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy->image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(),
                 regions.data());
  if(R.gpuPassTimestampsEnabled) gpu_end_feedback_scope(feedbackTiming);
  transition_image(copy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_SHADER_READ_BIT);
  VkImageView view = sampled_texture_view(copy, textureWords);
  destroy_surface_image(&temporary); // Temporary snapshots remain fence-retired.
  return view;
}
// ---------------------------------------------------------------- continued-draw fast path
// On by default (WWHD_VK_FASTPATH=0 turns it off): while only "continued" register groups changed since the previous resolved
// draw (gx2/state_groups.h: ALU constants, uniform-block and vertex-buffer pointers, per-draw
// dynamic words), in the same frame and surface epoch (backend.h), with the same primitive type and
// graphics settings, a draw reuses that draw's fetch/vertex/pixel shaders, render targets, pipeline
// and texture/sampler bindings (Cemu's continued draw pass: LatteCommandProcessor.cpp,
// VulkanRendererCore.cpp). Uniforms, support uniforms (incl. texture scales), descriptor sets,
// indices, vertex buffers and dynamic state are still prepared per draw, and every reused texture
// still goes through upload_surface (write tracking/invalidations): an upload ends the epoch and the
// draw resolves fully. Draws that sample one of their own targets' addresses (feedback loops and
// lookups whose winner can change with the target's write sequence), AO replays and AO-substituted
// textures are never continued.
// WWHD_VK_FASTPATH_VERIFY=N (unless WWHD_VK_FASTPATH=0): on every Nth continued draw
// also run the full resolution, compare shaders, targets, pipeline and texture/sampler bindings,
// count and log mismatches (the draw then uses the full result). Statistics every 120 frames
// only with WWHD_VK_STATS or verification: synchronous console writes can stall rendering.
// WWHD_VK_FASTPATH_ORACLE=1: on every draw that registers kept from continuing, compare what the full
// resolution gives with the state it could not continue, by exact combination of changed groups
// ([vulkan fastpath oracle] lines with the statistics). WWHD_VK_PROF_SPLIT=1: the render profiler
// also reports its draw phases for continued and register-broken full draws apart.
// WWHD_VK_FASTPATH_PARTIAL=1 (off by default): partial continuation. A draw that registers kept from
// continuing resolves its fetch/vertex/pixel shaders as a full draw does; when they are the state's
// own, it reuses the state's render targets, pipeline and each stage's texture/sampler bindings
// whose inputs did not change (gx2/state_groups.h kSgPartial*Mask, from the register groups changed
// since the state was recorded) and resolves the rest; any other difference (another frame, surface
// epoch, primitive, settings, an "other" register group) resolves in full. Reused textures still go
// through upload_surface first, and a stage is reused only while the surface epoch is the state's.
// The draw records its state as a full one does. With WWHD_VK_FASTPATH_VERIFY=N, every Nth partial
// draw also runs the full resolution against everything it recorded (a mismatch is logged and ends
// the continuation; that draw has already used the state).
struct FastPathConfig {
  bool on = false;
  bool stats = false;
  uint32_t verifyEvery = 0;
  bool oracle = false, profSplit = false, partial = false;
};
const FastPathConfig& fastpath_config() {
  static const FastPathConfig config = [] {
    FastPathConfig c;
    if (const char* v = std::getenv("WWHD_VK_FASTPATH_VERIFY")) c.verifyEvery = uint32_t(std::max(0, std::atoi(v)));
    const char* e = std::getenv("WWHD_VK_FASTPATH");
    c.on = e ? std::atoi(e) != 0 : true;
    if (!c.on) c.verifyEvery = 0;
    const char* o = std::getenv("WWHD_VK_FASTPATH_ORACLE");
    c.oracle = c.on && o && std::atoi(o) != 0;
    const char* ps = std::getenv("WWHD_VK_PROF_SPLIT");
    c.profSplit = c.on && ps && std::atoi(ps) != 0;
    const char* pa = std::getenv("WWHD_VK_FASTPATH_PARTIAL");
    c.partial = c.on && pa && std::atoi(pa) != 0;
    c.stats = preparation_stats_enabled() || c.verifyEvery || c.oracle;
    LOG("[vulkan] continued-draw fast path %s%s%s%s%s", c.on ? "on" : "off (WWHD_VK_FASTPATH=0)", c.verifyEvery ? " (verify mode)" : "",
        c.oracle ? " (oracle)" : "", c.profSplit ? " (profiler split)" : "", c.partial ? " (partial continuation)" : "");
    return c;
  }();
  return config;
}
struct ContinuedTexture {
  uint32_t unit = 0;
  int binding = -1;
  Surface* surface = nullptr;
  VkImageView view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
};
struct ContinuedStage {
  uint32_t count = 0;
  bool continuable = true;
  std::array<ContinuedTexture, LATTE_NUM_MAX_TEX_UNITS> textures{};
  void add(const ContinuedTexture& t) {
    if (count < textures.size()) textures[count++] = t;
    else continuable = false;
  }
};
struct DrawTargets {
  std::array<Surface*, 8> colors{};
  std::array<uint32_t, 8> slices{};
  Surface* depth = nullptr;
  Surface* target = nullptr;  // first colour target, else depth (before AO replay drops a mismatch)
  uint32_t depthSlice = 0, width = 0, height = 0;
  float sx = 1, sy = 1;
  uint8_t activeColors = 0;  // GetActiveColorBufferMask: it also reads the (dynamic) scissor, see continued_reason
  bool same(const DrawTargets& o) const {
    return colors == o.colors && slices == o.slices && depth == o.depth && target == o.target && depthSlice == o.depthSlice &&
           width == o.width && height == o.height && !std::memcmp(&sx, &o.sx, 4) && !std::memcmp(&sy, &o.sy, 4);
  }
};
enum ContinuedReason : uint8_t {
  kContinued, kNoState, kNewFrame, kRegisters, kSurfaces, kUpload, kPrimitive, kSettings,
  kVerifyMismatch, kNotContinuable, kAoReplay, kScissorTargets, kReasons
};
const char* const continuedReasonNames[kReasons] = {
  "continued", "no state", "new frame", "registers", "surfaces", "texture upload", "primitive",
  "settings", "verify mismatch", "not continuable", "AO replay", "scissor targets"};
struct ContinuedDraw {
  bool valid = false;
  ContinuedReason invalidReason = kNoState;  // why the state is not valid
  uint64_t stateVersion = 0, surfaceEpoch = 0, frame = ~uint64_t{0};
  uint32_t prim = 0;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
  int aoMode = 0;
  bool aoHires = false, aniso = false;
  LatteFetchShader* fs = nullptr;
  uint64_t fsKey = 0;
  vk::Shader *vs = nullptr, *ps = nullptr;
  DrawTargets targets;
  uint32_t scissorBR = 0;  // PA_SC_GENERIC_SCISSOR_BR that targets.activeColors was resolved with
  Pipeline* pipeline = nullptr;
  ContinuedStage stages[2];
  // pass subset (pass_reuse.h): the colour slots taken over from the open pass, and the colours the
  // pipeline was made for; targets stays the draw's own
  uint32_t subsetAbsent = 0;
  std::array<Surface*, 8> subsetColors{};
  Pipeline* ownPipeline = nullptr;  // the draw's own pipeline (pipeline: its masked variant, with a subset)
};
// Stereo: each eye's command stream continues its own previous draw (stereo.h).
std::array<ContinuedDraw, 2> continuedEyes;
// WWHD_VK_FASTPATH_PARTIAL: per eye, the register groups changed since the eye's state was recorded.
// Every draw's take_dirty_groups goes to both eyes (they share the register stream) before any early
// return, so neither a culled or empty draw nor eye 0 taking the changes loses a group.
std::array<uint32_t, 2> partialGroups{};
// Stereo: eye 1 draws the same guest operation right after eye 0 (draw_stereo), with the same
// registers, so it takes eye 0's fetch, vertex and pixel shaders instead of looking them up again.
// When eye 0's draw completed in the same submission, eye 1 also replays its index and vertex
// buffers, its pipeline and each stage that samples no texture (StageReplay): of its own, eye 1
// resolves only its render targets and the textures its stages sample.
struct StereoShaders {
  LatteFetchShader* fs = nullptr;
  uint64_t fsKey = 0;
  vk::Shader *vs = nullptr, *ps = nullptr;
  bool replay = false;
  uint64_t submission = 0;
  Pipeline* pipeline = nullptr;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
  std::array<VkFormat, 9> formats{};  // the pipeline's colour and depth formats
  float sx = 0, sy = 0;
  uint32_t width = 0, height = 0;
  VkViewport viewport{};
  VkRect2D scissor{};
  bool rasterReady = false;
  StageReplay stages[2];
  DrawTargets targets; // exact attachments/dimensions used by this resolved draw
  struct VertexBind { uint32_t binding; VkBuffer buffer; VkDeviceSize offset; };
  std::array<VertexBind, 32> vertexBinds{};
  uint32_t vertexBindCount = 0;
  bool indexed = false;
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VkDeviceSize indexOffset = 0;
  VkIndexType indexType = VK_INDEX_TYPE_UINT32;
  uint32_t drawCount = 0;
  void reset() {
    fs = nullptr; fsKey = 0; vs = ps = nullptr;
    replay = false; vertexBindCount = 0; rasterReady = false;
    stages[0].valid = stages[1].valid = false;
  }
  void add_vertex_bind(uint32_t binding, VkBuffer buffer, VkDeviceSize offset) {
    if (vertexBindCount < vertexBinds.size()) vertexBinds[vertexBindCount] = {binding, buffer, offset};
    ++vertexBindCount;  // past capacity: no replay
  }
} stereoShaders;
std::array<VkFormat, 9> target_formats(const std::array<Surface*, 8>& colors, const Surface* depth) {
  std::array<VkFormat, 9> f{};
  for (int i = 0; i < 8; ++i) f[i] = colors[i] ? colors[i]->fmt.pixel : VK_FORMAT_UNDEFINED;
  f[8] = depth ? depth->fmt.pixel : VK_FORMAT_UNDEFINED;
  return f;
}
// The record of a GamePad backdrop pane draw (outside stereo): one reused object, no per-draw allocation.
// Only the outermost draw writes it; its private replay reads it (backdropShadersIn) and writes none.
StereoShaders backdropSnapshot;
StereoShaders* stereoShadersOut = nullptr;       // eye 0's draw records
const StereoShaders* backdropShadersIn = nullptr; // exact original draw, no guest resource rereads
const StereoShaders* stereoShadersIn = nullptr;  // eye 1's draw reuses
struct ContinuedStats {
  uint64_t lastFrame = 0, draws = 0, verified = 0, mismatches = 0, totalMismatches = 0, totalVerified = 0;
  // WWHD_VK_FASTPATH_PARTIAL: register draws that could continue in part, those with the state's
  // shaders, and of those the pieces reused
  uint64_t partialCandidates = 0, partial = 0, partialTargets = 0, partialPipeline = 0;
  std::array<uint64_t, 2> partialStages{};
  std::array<uint64_t, kReasons> reasons{};
  std::array<uint64_t, gx2::kSgCount> groups{};  // register groups that ended a continuation
  std::array<uint64_t, 8> mismatchKinds{};
} continuedStats;
const char* const mismatchKindNames[8] = {"fetch shader", "vertex shader", "pixel shader", "targets",
                                          "pipeline", "texture count", "texture binding", "topology"};
// WWHD_VK_FASTPATH_ORACLE (fastpath_config): per draw the fast path resolves in full for changed
// registers (kRegisters), which parts of the state it could not continue the full resolution gives
// again. Groups are accumulated per eye from every draw's take_dirty_groups (both eyes: they share
// the register stream), so draws that return early (culled, empty) or that eye 0 took lose none.
// "ps only by alpha test": fs and vs are the same, ps is not, and of the pixel shader key words
// (oracle_ps_key) only SX_ALPHA_TEST_CONTROL differs.
constexpr uint32_t kOracleKeyWords = 1 + 1 + 32 + 10 + 1 + 1 + 2 + 32 + 3 + 4 + 6 + 3 + 2 + 8 +
                                     2 * LATTE_NUM_MAX_TEX_UNITS + 3 * LATTE_NUM_MAX_TEX_UNITS;
using OracleKey = std::array<uint32_t, kOracleKeyWords>;
// the words of the pixel shader key before its narrowing (shaders.cpp legacy_state_hash) and the
// program address; word 0 is SX_ALPHA_TEST_CONTROL
void oracle_ps_key(const uint32_t* r, OracleKey& k) {
  uint32_t n = 0;
  auto put = [&](uint32_t first, uint32_t count) { for (uint32_t i = 0; i < count; ++i) k[n++] = r[first + i]; };
  put(REGADDR::SX_ALPHA_TEST_CONTROL, 1);
  put(mmSQ_PGM_START_PS, 1);
  put(mmSQ_VTX_SEMANTIC_0, 32); put(mmSPI_VS_OUT_ID_0, 10); put(mmSPI_VS_OUT_CONFIG, 1); put(mmPA_CL_VS_OUT_CNTL, 1);
  put(mmSPI_PS_IN_CONTROL_0, 2); put(mmSPI_PS_INPUT_CNTL_0, 32);
  k[n++] = r[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F;
  k[n++] = r[mmSPI_INTERP_CONTROL_0] & (1u << 1);
  k[n++] = r[mmVGT_STRMOUT_EN];
  for (uint32_t b = 0; b < 4; ++b) k[n++] = r[mmVGT_STRMOUT_EN] ? r[mmVGT_STRMOUT_VTX_STRIDE_0 + b * 4] : 0;
  put(REGADDR::VGT_GS_MODE, 1); put(REGADDR::SQ_CONFIG, 1); put(mmCB_SHADER_MASK, 1);
  put(mmCB_SHADER_CONTROL, 1); put(mmDB_SHADER_CONTROL, 1); put(mmSPI_INPUT_Z, 1);
  k[n++] = r[REGADDR::PA_CL_VTE_CNTL] & 0x3F;
  k[n++] = r[REGADDR::PA_CL_CLIP_CNTL] & ((1u << 19) | (1u << 27));
  k[n++] = r[REGADDR::DB_DEPTH_CONTROL] & 0x83;
  put(REGADDR::CB_COLOR_CONTROL, 1); put(REGADDR::CB_TARGET_MASK, 1);
  put(mmCB_COLOR0_INFO, 8);
  for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS; ++t) {
    const uint32_t* w = r + REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + t * 7;
    k[n++] = (w[0] & 7) | (w[4] & 0x300);
    k[n++] = w[1] & 0x3F00000;
  }
  for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS * 3; ++t) k[n++] = r[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + t * 3] & 0xF8000000;
}
struct OracleCounts {
  uint64_t draws = 0, fs = 0, vs = 0, ps = 0, shaders = 0, targets = 0, pipeline = 0, vsTextures = 0, psTextures = 0,
           all = 0, psOnly = 0, psAlphaOnly = 0, psKeyEqual = 0;
};
struct FastPathOracle {
  std::array<uint32_t, 2> groups{};    // per eye: groups changed since the eye's last fast-path draw
  std::array<ContinuedDraw, 2> saved;  // per eye: the state a kRegisters draw could not continue
  std::array<OracleKey, 2> psKey{};    // per eye: oracle_ps_key of the recorded state
  std::unordered_map<uint32_t, OracleCounts> combos;  // by exact combination of breaking groups
} oracle;
bool same_stage_textures(const ContinuedStage& a, const ContinuedStage& b) {
  if (a.count != b.count) return false;
  for (uint32_t i = 0; i < a.count; ++i) {
    const auto &x = a.textures[i], &y = b.textures[i];
    if (x.unit != y.unit || x.binding != y.binding || x.surface != y.surface || x.view != y.view || x.sampler != y.sampler)
      return false;
  }
  return true;
}
// o: the state the draw could not continue; n: what the full resolution recorded
void oracle_count(uint32_t combo, const ContinuedDraw& o, const ContinuedDraw& n, const OracleKey& oKey, const OracleKey& nKey) {
  auto& e = oracle.combos[combo];
  const bool fs = o.fs == n.fs && o.fsKey == n.fsKey, vs = o.vs == n.vs, ps = o.ps == n.ps;
  const bool targets = n.targets.same(o.targets), pipeline = o.pipeline == n.pipeline;
  const bool vsTextures = same_stage_textures(o.stages[0], n.stages[0]), psTextures = same_stage_textures(o.stages[1], n.stages[1]);
  ++e.draws;
  e.fs += fs; e.vs += vs; e.ps += ps; e.shaders += fs && vs && ps;
  e.targets += targets; e.pipeline += pipeline; e.vsTextures += vsTextures; e.psTextures += psTextures;
  e.all += fs && vs && ps && targets && pipeline && vsTextures && psTextures;
  if (fs && vs && !ps) {
    ++e.psOnly;
    const bool restEqual = std::equal(oKey.begin() + 1, oKey.end(), nKey.begin() + 1);
    e.psAlphaOnly += restEqual && oKey[0] != nKey[0];
    e.psKeyEqual += restEqual && oKey[0] == nKey[0];
  }
}
void report_oracle(double frames) {
  if (oracle.combos.empty()) return;
  std::vector<std::pair<uint32_t, OracleCounts>> v(oracle.combos.begin(), oracle.combos.end());
  oracle.combos.clear();
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.draws > b.second.draws; });
  OracleCounts t;
  for (const auto& [combo, e] : v) {
    t.draws += e.draws; t.fs += e.fs; t.vs += e.vs; t.ps += e.ps; t.shaders += e.shaders; t.targets += e.targets;
    t.pipeline += e.pipeline; t.vsTextures += e.vsTextures; t.psTextures += e.psTextures; t.all += e.all;
    t.psOnly += e.psOnly; t.psAlphaOnly += e.psAlphaOnly; t.psKeyEqual += e.psKeyEqual;
  }
  auto pct = [](uint64_t a, uint64_t n) { return n ? 100.0 * a / n : 0.0; };
  LOG("[vulkan fastpath oracle] frame %llu: %.1f register draws/frame checked; same: fetch %.1f%% vs %.1f%% ps %.1f%% "
      "fs+vs+ps %.1f%% targets %.1f%% pipeline %.1f%% vs textures %.1f%% ps textures %.1f%% all %.1f%%; only ps "
      "differs %.1f%%, of those by alpha test only %.1f%%, with equal key words %.1f%%",
      (unsigned long long)R.frame, t.draws / frames, pct(t.fs, t.draws), pct(t.vs, t.draws), pct(t.ps, t.draws),
      pct(t.shaders, t.draws), pct(t.targets, t.draws), pct(t.pipeline, t.draws), pct(t.vsTextures, t.draws),
      pct(t.psTextures, t.draws), pct(t.all, t.draws), pct(t.psOnly, t.draws), pct(t.psAlphaOnly, t.psOnly),
      pct(t.psKeyEqual, t.psOnly));
  for (size_t i = 0; i < v.size() && i < 12; ++i) {
    const auto& [combo, e] = v[i];
    std::string name;
    for (int g = 0; g < gx2::kSgCount; ++g)
      if (combo >> g & 1) name += (name.empty() ? "" : "+") + std::string(gx2::state_group_name(g));
    LOG("[vulkan fastpath oracle]   %s: %.1f/frame (%.1f%%); same fs+vs+ps %.0f%% targets %.0f%% pipeline %.0f%% "
        "vs textures %.0f%% ps textures %.0f%% all %.0f%%; only ps differs %.0f%%, of those by alpha test only %.0f%%",
        name.empty() ? "none" : name.c_str(), e.draws / frames, pct(e.draws, t.draws), pct(e.shaders, e.draws),
        pct(e.targets, e.draws), pct(e.pipeline, e.draws), pct(e.vsTextures, e.draws), pct(e.psTextures, e.draws),
        pct(e.all, e.draws), pct(e.psOnly, e.draws), pct(e.psAlphaOnly, e.psOnly));
  }
}
void report_continued_stats() {
  auto& st = continuedStats;
  if (R.frame < st.lastFrame + 120) return;
  const double frames = double(R.frame - st.lastFrame);
  if (st.draws) {
    char buf[1024];
    int k = std::snprintf(buf, sizeof buf, "[vulkan fastpath] frame %llu: %.1f%% of %.0f draws/frame continued; full:",
                          (unsigned long long)R.frame, 100.0 * st.reasons[kContinued] / st.draws, st.draws / frames);
    for (int i = 1; i < kReasons && k < int(sizeof buf); ++i)
      if (st.reasons[i]) k += std::snprintf(buf + k, sizeof buf - k, " %s %.1f%%", continuedReasonNames[i], 100.0 * st.reasons[i] / st.draws);
    if (st.reasons[kRegisters] && k < int(sizeof buf)) {
      k += std::snprintf(buf + k, sizeof buf - k, "; groups/frame:");
      for (int g = 0; g < gx2::kSgCount && k < int(sizeof buf); ++g)
        if (st.groups[g]) k += std::snprintf(buf + k, sizeof buf - k, " %s %.0f", gx2::state_group_name(g), st.groups[g] / frames);
    }
    if (fastpath_config().partial && k < int(sizeof buf)) {
      auto pct = [&](uint64_t a) { return st.partial ? 100.0 * a / st.partial : 0.0; };
      k += std::snprintf(buf + k, sizeof buf - k,
                         "; partial: %.1f/frame of %.1f candidates (reused targets %.0f%% pipeline %.0f%% "
                         "vs textures %.0f%% ps textures %.0f%%)",
                         st.partial / frames, st.partialCandidates / frames, pct(st.partialTargets),
                         pct(st.partialPipeline), pct(st.partialStages[0]), pct(st.partialStages[1]));
    }
    if (fastpath_config().verifyEvery && k < int(sizeof buf))
      k += std::snprintf(buf + k, sizeof buf - k, "; verify: %llu checked, %llu mismatches (total %llu of %llu)",
                         (unsigned long long)st.verified, (unsigned long long)st.mismatches,
                         (unsigned long long)st.totalMismatches, (unsigned long long)st.totalVerified);
    LOG("%s", buf);
  }
  if (fastpath_config().oracle) report_oracle(frames);
  const uint64_t totalMismatches = st.totalMismatches, totalVerified = st.totalVerified;
  const auto kinds = st.mismatchKinds;
  st = {};
  st.lastFrame = R.frame;
  st.totalMismatches = totalMismatches;
  st.totalVerified = totalVerified;
  st.mismatchKinds = kinds;
}
// Render targets of a draw (both paths); false: the draw has no target.
bool resolve_targets(const uint32_t* r, vk::Shader* ps, DrawTargets& t) {
  t = {};
  const uint32_t eyeSize = stereo_eye_size_generation();
  const auto &lcr = *reinterpret_cast<const LatteContextRegister *>(r);
  auto mask = LatteMRT::GetActiveColorBufferMask(ps->dec, lcr);
  const auto claim = gfx::size_request(ps->kind);
  for (int i = 0; i < 8; i++)
    if (mask & (1 << i))
      t.colors[i] = color_target(r, i, &t.slices[i], claim);
  t.activeColors = mask;
  t.depth = LatteMRT::GetActiveDepthBufferMask(lcr) ? depth_target(r, &t.depthSlice) : nullptr;
  // a lookup switched stereo eyes to full size (an unscalable format, surfaces.cpp): the targets
  // looked up before it still have the eye size; looked up again, they are rescaled
  if (eyeSize != stereo_eye_size_generation()) return resolve_targets(r, ps, t);
  if (t.depth && t.depth->width == 1280 && t.depth->height == 720) R.mainDepthAddr = t.depth->addr;
  auto& colors = t.colors;
  auto& depth = t.depth;
  const uint32_t guestWidth = colors[0] ? colors[0]->width : 0;
  const uint32_t guestHeight = colors[0] ? colors[0]->height : 0;
  if (aoPrivateReplay && colors[0]) {
    auto& ao = ao_private();
    ao.source = colors[0]->addr;
    colors[0] = private_ao_surface(ao.color, colors[0]);
    t.slices[0] = 0;
    if (depth) {
      depth = private_ao_surface(ao.depth, depth);
      t.depthSlice = 0;
    }
  }
  Surface *target = depth;
  for (auto *c : colors)
    if (c) {
      target = c;
      break;
    }
  if (!target)
    return false;
  t.target = target;
  t.width = target->extent.width;
  t.height = target->extent.height;
  t.sx = target->sx;
  t.sy = target->sy;
  if (aoPrivateReplay && guestWidth) {
    // the viewport registers describe the game's smaller buffer (x and y differ at other aspect ratios)
    t.sx = float(colors[0]->extent.width) / guestWidth;
    t.sy = guestHeight ? float(colors[0]->extent.height) / guestHeight : t.sx;
  }
  for (auto *&c : colors)
    if (c && (c->extent.width != t.width || c->extent.height != t.height)) {
      if (aoPrivateReplay) c = nullptr;
      else exception_report::raise("mismatched Vulkan attachments");
    }
  if (depth && (depth->extent.width < t.width || depth->extent.height < t.height))
    depth = nullptr;
  return true;
}
#ifdef WWHD_NATIVE_DRAW
// native_pass.h (WWHD_NATIVE_PASS_TRUST): the targets of the open described pass. Its every target word matched the
// description at this draw (note_draw), so the registers resolve_targets reads for the attachments are those of the
// pass's previous draw; the rest it depends on is the key (frame, surface epoch: surfaces created, replaced or
// uploaded; eye size; the active colour mask, whether depth is active, and the pixel program's size claim). Not for the AO replay (private
// targets) or with stereo.
bool pass_trusted_targets(const uint32_t* r, vk::Shader* ps, DrawTargets& t) {
  const int mode = native_pass::trust_mode();
  uint32_t passId = 0;
  if (!mode || aoPrivateReplay || stereo::frame.mode != stereo::Off || !native_pass::trusted(passId, r))
    return resolve_targets(r, ps, t);
  struct Cached {
    bool valid = false;
    uint32_t pass = 0, eyeSize = 0, mask = 0;
    uint64_t frame = ~0ull, epoch = 0;
    gfx::SizeRequest claim{};
    DrawTargets targets;
  };
  static Cached c;
  const auto& lcr = *reinterpret_cast<const LatteContextRegister*>(r);
  // (as resolve_targets: the active colour buffers, and whether the depth buffer is active, bit 8)
  const uint32_t mask = LatteMRT::GetActiveColorBufferMask(ps->dec, lcr) | (LatteMRT::GetActiveDepthBufferMask(lcr) ? 0x100u : 0u);
  const auto claim = gfx::size_request(ps->kind);
  const bool hit = c.valid && c.pass == passId && c.frame == R.frame && c.epoch == g_surface_epoch &&
                   c.eyeSize == stereo_eye_size_generation() && c.mask == mask && c.claim.claim == claim.claim && c.claim.renews == claim.renews && c.claim.keeps == claim.keeps;
  if (hit && mode == 1) {
    t = c.targets;
    native_pass::count_trust(1);
    return true;
  }
  const bool ok = resolve_targets(r, ps, t);
  if (hit && (!ok || !t.same(c.targets) || t.activeColors != c.targets.activeColors)) {
    native_pass::count_trust(2);
    static int said = 0;
    if (said++ < 16)
      LOG("[native-pass] TRUSTED TARGETS DIFFER in pass %u (frame %llu): colours %d slices %d depth %d target %d size %d "
          "scale %d active %d", passId, (unsigned long long)R.frame, t.colors != c.targets.colors,
          t.slices != c.targets.slices, t.depth != c.targets.depth || t.depthSlice != c.targets.depthSlice,
          t.target != c.targets.target, t.width != c.targets.width || t.height != c.targets.height,
          std::memcmp(&t.sx, &c.targets.sx, 4) || std::memcmp(&t.sy, &c.targets.sy, 4),
          t.activeColors != c.targets.activeColors);
  } else {
    native_pass::count_trust(hit ? 1 : 0);
  }
  c = {ok, passId, stereo_eye_size_generation(), mask, R.frame, g_surface_epoch, claim, t};
  return ok;
}
#define PASS_TRUSTED_TARGETS(r, ps, t) pass_trusted_targets(r, ps, t)
#else
#define PASS_TRUSTED_TARGETS(r, ps, t) resolve_targets(r, ps, t)
#endif
// Eye 1's geometry replay still needs its own attachments, but consecutive draws do
// not need to resolve the same attachment registers again. This cache only owns
// lookup results; uploads and transitions below remain in command order.
bool resolve_replay_targets(const uint32_t* r, vk::Shader* ps, DrawTargets& targets) {
  struct Cached {
    uint64_t frame = ~0ull, version = 0, epoch = 0;
    uint32_t eyeSize = 0, scissorBR = 0;
    vk::Shader* shader = nullptr;
    DrawTargets targets;
  };
  static Cached cache;
  if (cache.frame == R.frame && cache.version == gx2::g_full_state_version &&
      cache.epoch == g_surface_epoch && cache.eyeSize == stereo_eye_size_generation() && cache.shader == ps) {
    // the scissor does not bump the version but selects the active colour buffers (continued_reason)
    const uint32_t scissorBR = r[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
    if (scissorBR == cache.scissorBR ||
        LatteMRT::GetActiveColorBufferMask(ps->dec, *reinterpret_cast<const LatteContextRegister*>(r)) ==
            cache.targets.activeColors) {
      cache.scissorBR = scissorBR;
      targets = cache.targets;
      return true;
    }
  }
  if (!resolve_targets(r, ps, targets)) return false;
  cache = {R.frame, gx2::g_full_state_version, g_surface_epoch, stereo_eye_size_generation(),
           r[REGADDR::PA_SC_GENERIC_SCISSOR_BR], ps, targets};
  return true;
}
// A sampled texture of a stage (both paths): lookup, AO substitution and the write-tracking check.
// substituted: the private AO image replaced the guest texture.
Surface* resolve_texture(const uint32_t* r, vk::Shader* sh, uint32_t unit, bool* substituted = nullptr) {
  const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
  // the mip levels its sampler reaches (sampler(): no mip filter -> the base level, else up to MAX_LOD)
  uint32_t reach = 16;
  if (uint32_t si = sh->dec->textureUnitSamplerAssignment[unit]; si < 18) {
    const uint32_t* smp = r + REGADDR::SQ_TEX_SAMPLER_WORD0_0 + ((sh->vertex ? 18 : 0) + si) * 3;
    Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0 s0;
    Latte::LATTE_SQ_TEX_SAMPLER_WORD1_0 s1;
    memcpy(&s0, &smp[0], 4);
    memcpy(&s1, &smp[1], 4);
    if (uint32_t(s0.get_MIP_FILTER()) == 0) reach = 1;
    else reach = std::max(uint32_t(s1.get_MIN_LOD()), uint32_t(s1.get_MAX_LOD())) / 64 + 2;
  }
  auto *s = sampled_texture(r + texbase + unit * 7, sh->dec->textureUsesDepthCompare[unit], reach);
  // debug D-sweep (WWHD_DSWEEP): a swept model can sample a texture its actor sets by code. An earlier texture stands
  // in (pipelines do not depend on the texture); its texture words replace the missing ones in this draw's
  // register copy, so the view matches.
  static const bool sweep = getenv("WWHD_DSWEEP") && *getenv("WWHD_DSWEEP");
  struct StandIn { Surface* surface = nullptr; uint32_t words[7] = {}; };
  static StandIn standIn[2];
  const bool depthCompare = sh->dec->textureUsesDepthCompare[unit];
  uint32_t* words = const_cast<uint32_t*>(r + texbase + unit * 7);
  if (!s && sweep && standIn[depthCompare].surface) {
    s = standIn[depthCompare].surface;
    memcpy(words, standIn[depthCompare].words, sizeof standIn[depthCompare].words);
  }
  if (!s)
    exception_report::raise("missing sampled texture");
  if (sweep) { standIn[depthCompare].surface = s; memcpy(standIn[depthCompare].words, words, sizeof standIn[depthCompare].words); }
  auto& ao = ao_private();
  if (!sh->vertex && ao_hires_enabled() && ao.source &&
      s->addr == ao.source && ao.frame == R.frame &&
      sh->kind == gfx::ProgramKind::OcclusionPixel) {
    s = &ao.color;
    if (substituted) *substituted = true;
  }
  upload_surface(s);
  return s;
}
bool aliases_target(const Surface* s, const std::array<Surface*, 8>& colors, const Surface* depth) {
  bool aliases = depth && s->image == depth->image;
  for (auto *color : colors)
    if (color && s->image == color->image)
      aliases = true;
  return aliases;
}
// a lookup at one of the draw's own target addresses can pick another surface once the target's
// write sequence advances (find_or_create_surface): such textures are not continued
bool shares_target_address(const Surface* s, const std::array<Surface*, 8>& colors, const Surface* depth) {
  if (depth && depth->addr == s->addr) return true;
  for (auto *color : colors)
    if (color && color->addr == s->addr) return true;
  return false;
}
VkSampler resolve_sampler(const uint32_t* r, vk::Shader* sh, uint32_t unit, const Surface* s) {
  uint32_t samplerId = sh->dec->textureUnitSamplerAssignment[unit];
  if (samplerId >= 18)
    exception_report::raise("missing texture sampler");
  uint32_t samplerBase = sh->vertex ? 18 : 0;
  const uint32_t* samplerWords = r + REGADDR::SQ_TEX_SAMPLER_WORD0_0 +
                                  (samplerBase + samplerId) * 3;
  uint32_t patchedSampler[3];
  if (!sh->vertex && ao_mode() >= 1 && unit == 0 &&
      sh->kind == gfx::ProgramKind::OcclusionPixel) {
    memcpy(patchedSampler, samplerWords, sizeof patchedSampler);
    patchedSampler[0] = (patchedSampler[0] & ~0x7E00u) | (1u << 9) | (1u << 12);
    samplerWords = patchedSampler;
  }
  // hand-drawn mip levels (gfx/artistic_mips.h): no forced anisotropy; the resolution-aware bias in the sampler where
  // the device applies sampler biases (else in the shader's uniform, bind_stage)
  return sampler(samplerWords, sh->dec->textureUsesDepthCompare[unit],
                 s->fmt.kind != FormatInfo::FLOAT,
                 !s->gpuWritten && s->mips > 1 && !s->artisticMips, s->linearFiltering,
                 s->artisticMips && R.samplerMipLodBias ? g_artisticBias : 0.0f);
}
// ------------------------------------------------------------- textures ready before the pass
// From Nycolazs/WindWakerHD-Android-Plus 4595260 / fc5ddee via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0 (pass_reuse.h (b)). A sampled texture's transition that ends the open pass
// (a layout change or barrier) is remembered for that pass; the next time the pass begins, the texture
// is made ready first (prefetch_pass_reads). Nothing else writes it while the pass is open (it isn't an
// attachment; copies and uploads end passes themselves), so the early barrier is the same one the draw
// would record; a guess that doesn't come true costs one barrier. Off with stereo (per-eye passes and
// textures held by eye 1, stereo.h). WWHD_PASS_PREFETCH=0: off.
pass_reuse::PassReadTable passReads;  // render thread
bool pass_trace_enabled() {  // WWHD_VK_TRACE_PASSES=1 (trace_pass): also the reads that end a pass
  static const bool on = [] { const char* e = getenv("WWHD_VK_TRACE_PASSES"); return e && *e == '1'; }();
  return on;
}
uint64_t pass_read_signature(const std::array<Surface*, 8>& colors, const Surface* depth, uint32_t width,
                             uint32_t height) {
  std::array<uint32_t, 8> keys{};
  for (int i = 0; i < 8; i++)
    if (colors[i]) keys[i] = pass_reuse::color_key(colors[i]->addr, colors[i]->format);
  return pass_reuse::signature(keys, depth ? depth->addr : 0, width, height);
}
// the surface a texture was found as (not a private copy: feedback, mip chain, AO)
bool primary_surface(const Surface* s) {
  if (!s || !s->addr) return false;
  auto range = R.surfaces.equal_range(s->addr);
  for (auto it = range.first; it != range.second; ++it)
    if (it->second.get() == s) return true;
  return false;
}
bool pass_attachment(const Surface* s) {
  if (s == R.passDepth) return true;
  for (auto* c : R.passColors)
    if (c == s) return true;
  return false;
}
// a draw samples s (bind_stage): its shader-read transition, which may end the open pass
void sample_transition(Surface* s, VkPipelineStageFlags stages) {
  if (!pass_reuse::prefetch_on() || !R.rendering || !R.passTracked || stereo::frame.mode != stereo::Off) {
    transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stages, VK_ACCESS_SHADER_READ_BIT);
    return;
  }
  const bool readied = s->passReadyFor && s->passReadyFor == R.renderPassCount;
  transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stages, VK_ACCESS_SHADER_READ_BIT);
  if (!R.rendering) {
    // the transition ended the pass (R.pass* still describe it): ready before it next time
    if (pass_attachment(s) || !primary_surface(s)) return;
    passReads.note(pass_read_signature(R.passColors, R.passDepth, R.passWidth, R.passHeight),
                   {s->addr, s->width, s->height, s->format}, stages, R.frame);
    ++R.passSplitsSeen;
    if (pass_trace_enabled())
      LOG("[vulkan pass] texture %08X %ux%u fmt %u first read inside pass %llu (ended it): ready before it next time",
          s->addr, s->width, s->height, s->format, (unsigned long long)R.renderPassCount);
  } else if (readied) {
    // made ready before this pass and read in it: the guess came true, keep it
    s->passReadyFor = 0;
    passReads.note(pass_read_signature(R.passColors, R.passDepth, R.passWidth, R.passHeight),
                   {s->addr, s->width, s->height, s->format}, stages, R.frame);
    ++R.passPrefetchUsed;
  }
}
// the pass about to begin (draw, after end_encoder): the textures it read last time, made ready now
void prefetch_pass_reads(const std::array<Surface*, 8>& colors, Surface* depth, uint32_t width, uint32_t height) {
  if (passReads.empty()) return;
  const uint64_t serial = R.renderPassCount + 1;  // the pass's number once begun
  passReads.begin(pass_read_signature(colors, depth, width, height), R.frame, [&](const pass_reuse::PassRead& pr) {
    auto range = R.surfaces.equal_range(pr.tex.addr);
    for (auto it = range.first; it != range.second; ++it) {
      Surface* s = it->second.get();
      if (s->width != pr.tex.width || s->height != pr.tex.height || s->format != pr.tex.format) continue;
      if (!s->image || s->evicted || s->layout == VK_IMAGE_LAYOUT_UNDEFINED || s == depth) continue;
      bool attached = false;
      for (auto* c : colors) attached |= c == s;
      if (attached) continue;
      if (!narrow_barriers() && s->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;  // ready
      // counted (and marked) only when a barrier was actually recorded
      if (!transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, pr.stages, VK_ACCESS_SHADER_READ_BIT)) continue;
      s->passReadyFor = serial;
      ++R.passPrefetched;
    }
  });
}
// ------------------------------------------------------------------------------------- pass subset
// From Nycolazs/WindWakerHD-Android-Plus 4595260 / fc5ddee via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0 (pass_reuse.h (a)). A draw that binds a subset of the open pass's colour
// targets (the game switches between colour only and colour + a second target) takes the pass's other
// targets with their writes masked (pipeline(): CB_TARGET_MASK nibble 0, no blending), so the reuse
// test below keeps the pass. Not when the draw samples one of those targets (it would need the pass
// ended), with stereo (per-eye passes and eye 1's pipeline replay) or in the AO replay.
// Returns the slots taken over (targets updated); 0: none. WWHD_PASS_SUBSET=0: off.
uint32_t take_pass_subset(const uint32_t* r, const vk::Shader* vs, const vk::Shader* ps, DrawTargets& t) {
  // the cheap comparison first (target identities); texture registers only for a candidate
  pass_reuse::Targets pass, draw;
  for (int i = 0; i < 8; i++) {
    Surface* c = R.passColors[i];
    pass.colors[i] = c;
    pass.slices[i] = R.passSlices[i];
    pass.addr[i] = c ? c->addr : 0;
    draw.colors[i] = t.colors[i];
    draw.slices[i] = t.slices[i];
  }
  pass.depth = R.passDepth;
  pass.depthSlice = R.passDepthSlice;
  pass.width = R.passWidth;
  pass.height = R.passHeight;
  draw.depth = t.depth;
  draw.depthSlice = t.depthSlice;
  draw.width = t.width;
  draw.height = t.height;
  const uint32_t absent = pass_reuse::subset_candidates(pass, draw);
  if (!absent) return 0;
  for (int i = 1; i < 8; i++)
    if ((absent >> i) & 1) {
      const Surface* c = R.passColors[i];
      if (c->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return 0;
      pass.bytes[i] = pass_reuse::target_bytes(c->dataSize, c->pitch, c->height, c->slices, c->fmt.bytesPerBlock);
      pass.mipAddr[i] = c->mips > 1 ? c->mipAddr : 0;
    }
  // the base and mip base addresses (SQ_TEX_RESOURCE_WORD2/3) of the textures both stages sample
  uint32_t sampled[4 * LATTE_NUM_MAX_TEX_UNITS];
  size_t count = 0;
  for (const vk::Shader* sh : {vs, ps}) {
    if (!sh->dec) return 0;
    const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (int i = 0; i < sh->dec->textureUnitListCount; i++) {
      const uint32_t unit = sh->dec->textureUnitList[i];
      if (unit >= LATTE_NUM_MAX_TEX_UNITS || count + 2 > std::size(sampled)) return 0;
      sampled[count++] = r[texbase + unit * 7 + 2] << 8;
      sampled[count++] = r[texbase + unit * 7 + 3] << 8;
    }
  }
  if (pass_reuse::samples_absent(pass, absent, sampled, count)) return 0;
  for (int i = 0; i < 8; i++)
    if ((absent >> i) & 1) {
      t.colors[i] = R.passColors[i];
      t.slices[i] = R.passSlices[i];
    }
  return absent;
}
// The masked variant of `own` for the slots `absent` of `colors` (the pass's targets), from own's link
// when it is the same one as last time (no second key built or looked up), else requested from
// pipeline() as a background variant. Never compiles or waits on the render thread: nullptr, or a
// pipeline still compiling, while it is not ready; one finished in the background is taken here.
Pipeline* masked_variant(Pipeline& own, const uint32_t* r, vk::Shader* vs, vk::Shader* ps, LatteFetchShader* fs,
                         VkPrimitiveTopology topology, const std::array<Surface*, 8>& colors, Surface* depth,
                         uint32_t absent) {
  const uint32_t clear = pass_reuse::clear_mask(absent);
  std::array<VkFormat, 8> formats{};
  for (int i = 0; i < 8; i++)
    if ((absent >> i) & 1) formats[i] = colors[i]->fmt.pixel;
  auto& link = own.masked;
  const bool same = link.clearMask == clear && link.formats == formats;
  Pipeline* v = same ? link.pipeline : nullptr;
  if (!v) {
    if (same && link.declinedFrame == R.frame) return nullptr;  // no room this frame: asked again next frame
    Pipeline& got = pipeline(r, vs, ps, fs, topology, colors, depth, PipelineVariant{.clearMask = clear, .background = true});
    if (&got == &declined_pipeline_storage()) {
      link = {clear, formats, nullptr, R.frame};
      return nullptr;
    }
    v = &got;
    link = {clear, formats, v, ~0ull};
  }
  // a compile in the background: taken if it has finished, never waited for
  if (!v->pipeline && v->pendingCompile && !v->compileError) {
    try { resolve_pending_pipeline(*v, false); } catch (...) {}
  }
  return v;
}
#ifdef WWHD_NATIVE_DRAW
// Native draw packets (gx2/native_draw.h): what a packet's lowering draw resolved, captured by draw() and bind_stage
// while g_packetCapture is set (packet_draw), so the packet's next instances can be replayed with it.
struct PacketCapture {
  static constexpr int kBlocks = int(native_draw::kMaxBlocks);
  // in: the instance blocks' uniform block indices (their locations) per stage, -1: none
  std::array<std::array<int, kBlocks>, 2> slot;
  bool hashes = false;  // WWHD_NATIVE_DRAW_VERIFY: hash the resolved inputs for the comparison
  const native_draw::Packet* packet = nullptr;  // in: the packet (its instance register ranges)
  PacketCapture() { for (auto& s : slot) s.fill(-1); }
  int block_of(int st, size_t logicalSlot) const {
    for (int k = 0; k < kBlocks; ++k)
      if (slot[st][k] >= 0 && size_t(slot[st][k]) == logicalSlot) return k;
    return -1;
  }
  // out
  bool drew = false;
  const char* refused = nullptr;
  struct Block {
    int binding = -1;              // the instance block's binding in this stage (-1: the stage does not read it)
    VkBuffer buffer = VK_NULL_HANDLE;
    int index = -1;                // dynamic uniforms: its dynamic offset's index (vertex set's first)
    int instWrite = -1;            // regular uniforms: its descriptor write
    // the support uniforms' kcache-banked entries of this block (support byte offset, block byte offset)
    std::vector<std::pair<uint32_t, uint32_t>> remap;
  };
  struct Stage {
    Block block[kBlocks];
    bool dynamic = true;
    // regular (non-dynamic) uniforms: the stage's descriptor writes, to write a set per instance with its blocks
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    uint32_t writeCount = 0;
    std::array<VkWriteDescriptorSet, 17 + LATTE_NUM_MAX_TEX_UNITS> writes{};
    std::array<VkDescriptorBufferInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> bufferInfos{};
    std::array<VkDescriptorImageInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> imageInfos{};
    // the support uniforms (Cemu's helper constants): pack_uniforms_into copies the kcache-banked entries of guest
    // uniform blocks into them, so those of the instance blocks are patched per instance into the lowering draw's
    // final support bytes
    int supBinding = -1;
    VkBuffer supBuffer = VK_NULL_HANDLE;
    int supIndex = -1, supWrite = -1;
    std::vector<uint8_t> supBytes;
    // the support bytes pack_uniforms_into copies from the packet's instance register ranges (the uniform-register
    // copy, or remapped register entries): support byte offset, first register, words
    struct RegPatch { uint32_t dst, reg, words; };
    std::vector<RegPatch> regRemap;
    bool patches() const {
      for (const auto& b : block) if (!b.remap.empty()) return true;
      return !regRemap.empty();
    }
  } stage[2];
  Pipeline* pipeline = nullptr;
  VkPipeline vkPipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkDescriptorSet sets[2]{};
  std::array<uint32_t, 34> offsets{};
  uint32_t offsetCount = 0;
  bool indexed = false;
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VkDeviceSize indexOffset = 0;
  VkIndexType indexType = VK_INDEX_TYPE_UINT32;
  uint32_t drawCount = 0;
  uint32_t nativeIndexWidth = 0;  // the guest's own index bytes were drawn (2 or 4 bytes each); 0: converted to uint32
  bool hostRestart = false;       // the host's strip restart marker (its vertex extent leaves it out)
  bool stripRestart = false;      // the conversion's guest restart marker (VGT_MULTI_PRIM_IB_RESET_*)
  uint32_t restartIndex = 0;
  // the vertex buffers the draw bound: binding (= attribute buffer index), slice, and the attribute block's
  // address, size and stride words it was made from; its copy's extent (attributeEnd, supported: as the full draw's
  // prefix)
  struct VertexBind { uint32_t binding = 0; VkBuffer buffer = VK_NULL_HANDLE; VkDeviceSize offset = 0; uint32_t regs[3]{};
                      uint32_t stride = 0; bool perInstance = false; uint64_t attributeEnd = 0; bool supported = true; };
  std::array<VertexBind, 16> vertexBinds{};
  uint32_t vertexBindCount = 0;
  std::array<Surface*, 8> colors{};
  Surface* depth = nullptr;
  // what the draw's pipeline was made from (pipeline()'s arguments; multi-draw batches: does the vertex shader read
  // the vertex index; shader-rewrite batches: the programs they are rewritten from)
  vk::Shader* vs = nullptr;
  vk::Shader* ps = nullptr;
  LatteFetchShader* fs = nullptr;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
  std::array<Surface*, 8> pipeColors{};
  Surface* pipeDepth = nullptr;
  uint32_t clearMask = 0;
  bool subset = false;
  // verify mode: every other uniform block's bytes, the textures, vertex (content; and layout: attribute index,
  // stride word) and index inputs
  uint64_t uniformHash[2]{}, textureHash = 0, vertexHash = 0, vertexLayoutHash = 0, indexHash = 0;
  void refuse(const char* why) { if (!refused) refused = why; }
};
PacketCapture* g_packetCapture = nullptr;
bool g_packetBindsDirty = false;  // a replay bound descriptor sets behind draw()'s bind cache
inline uint64_t packet_hash(uint64_t h, const void* data, size_t n) {
  const auto* b = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
  return h;
}
template <class T> inline uint64_t packet_hash_value(uint64_t h, const T& v) { return packet_hash(h, &v, sizeof v); }
#endif
StageResources bind_stage(const uint32_t *r, vk::Shader *sh,
                          VkDescriptorSetLayout layout, bool dynamicUniforms, float sx, float sy,
                          const std::array<Surface *, 8> &colors,
                          Surface *depth, FeedbackStatsProbe* feedbackProbe, float eyeOffset = 0,
                          bool sunShadow = false,
                          const ContinuedStage* reuse = nullptr, ContinuedStage* record = nullptr,
                          StageReplay* replayOut = nullptr) {
  const uint64_t stageEntered = rprof::sub_begin();
  StageResources out;
  auto &m = sh->mapping;
  // stereo eye 0: what eye 1 needs to bind this stage again (replay_stage)
  bool replayable = replayOut && dynamicUniforms && !(sunShadow && eyeOffset != 0);
  if (replayOut) replayOut->valid = false, replayOut->uniformCount = 0, replayOut->textureCount = 0,
                 replayOut->eyeTextureCount = 0;
  // own: a feedback copy or the AO substitute, which eye 1 cannot resolve on its own
  auto replayTexture = [&](Surface* s, uint32_t unit, int binding, bool own) {
    if (!replayable) return;
    if (own) replayable = false;
    else if (stereo_shared_texture(s)) replayOut->textures[replayOut->textureCount++] = s;
    else replayOut->eyeTextures[replayOut->eyeTextureCount++] = {unit, binding, s->sx, s->sy, 0};
  };
  std::array<int, 4> eyeUniformBindings{};
  UploadSlice lastUpload;
  auto eyeUniform = [&](int binding, const uint8_t* bytes, size_t size, const stereo::EyeRows& rows) {
    if (!replayable || binding < 0 || !rows.count) return;
    if (rows.overflow || replayOut->uniformCount >= replayOut->uniforms.size()) { replayable = false; return; }
    eyeUniformBindings[replayOut->uniformCount] = binding;
    auto& u = replayOut->uniforms[replayOut->uniformCount++];
    u.buffer = lastUpload.buffer;
    u.bytes.assign(bytes, bytes + size);
    u.rows = rows;
  };
  // Descriptor info pointers must remain stable until this stage's one update.
  std::array<VkDescriptorBufferInfo, 17> bufferInfos;
  std::array<VkDescriptorImageInfo, LATTE_NUM_MAX_TEX_UNITS> imageInfos;
  std::array<VkWriteDescriptorSet, 17 + LATTE_NUM_MAX_TEX_UNITS> writes;
  uint32_t bufferCount = 0, imageCount = 0, writeCount = 0;
  std::array<std::pair<uint32_t, uint32_t>, 17> dynamicBindings{};
  uint32_t dynamicCount = 0;
  static const bool ranksEnabled = [] {
    const char* value = std::getenv("WWHD_VK_DESCRIPTOR_RANKS");
    return value && std::strcmp(value, "1") == 0;
  }();
  const auto& rankPlan = sh->descriptorRanks;
  const bool useRanks = ranksEnabled && rankPlan.valid;
  std::array<bool, 17 + LATTE_NUM_MAX_TEX_UNITS> occupied;
  std::array<uint32_t, 17 + LATTE_NUM_MAX_TEX_UNITS> rankedOffsets;
  if (useRanks) occupied.fill(false);
  auto appendWrite = [&](uint8_t rank) -> VkWriteDescriptorSet & {
    if (writeCount >= writes.size())
      exception_report::raise("stage descriptor write capacity exceeded");
    const size_t slot = useRanks ? size_t(rank) : size_t(writeCount);
    if (useRanks && (slot >= rankPlan.count || occupied[slot]))
      exception_report::raise("stage descriptor rank metadata mismatch");
    ++writeCount;
    if (useRanks) occupied[slot] = true;
    auto &write = writes[slot];
    write = {}; // Initialize every active field; unused capacity is never read.
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = out.set;
    write.descriptorCount = 1;
    return write;
  };
  // guestAddr: a guest uniform block (bytes == mem::ptr(guestAddr)), served by the buffer cache if on
  // known: an upload slice already holding these bytes (a submitted block uploaded by an earlier draw)
  auto uniform = [&](int binding, const void *bytes, size_t size, size_t logicalSlot,
                     uint32_t guestAddr = 0, const UploadSlice* known = nullptr) {
    if (binding < 0)
      return;
    if (size > R.properties.limits.maxUniformBufferRange)
      exception_report::raise("uniform buffer exceeds device range");
    if(preparation_stats_enabled()) {
      ++R.cpuPreparation.uniformSnapshotCalls;
      R.cpuPreparation.uniformSnapshotBytes+=size;
    }
    static const bool reuseUniforms = [] {
      const char* value = std::getenv("WWHD_VK_REUSE_UNIFORM_SNAPSHOTS");
      return value && std::strcmp(value, "1") == 0;
    }();
    // compares CPU copies of the slots' bytes, not mapped upload memory, unless the upload memory is
    // host-cached (R.uploadReadsDirect; uniform_snapshot.h)
    static UniformSnapshotCache<UploadSlice, VkDevice> uniformCache;
    rprof::UploadKind uploads(logicalSlot == 16 ? rprof::kUpUniformVars : rprof::kUpUbo);
    auto fresh = [&](const void* source, size_t length) {
      return snapshot(source, length, R.properties.limits.minUniformBufferOffsetAlignment);
    };
    UploadSlice cachedSlice;
    const bool cached = known || (guestAddr && buffer_cache_enabled() &&
        cached_guest_range(guestAddr, uint32_t(size), rprof::kUpUbo, cachedSlice));
    if (known) cachedSlice = *known;
    const auto b = cached ? cachedSlice
        : reuseUniforms
        ? uniformCache.get(R.device, R.submissionGeneration,
                           (sh->vertex ? 0 : 17) + logicalSlot, bytes, size,
                           R.uploadReadsDirect, fresh)
        : fresh(bytes, size);
    if (reuseUniforms && preparation_stats_enabled()) {
      const auto& counts = uniformCache.counters;
      R.cpuPreparation.uniformReuseChecks = counts.checks;
      R.cpuPreparation.uniformReuseComparisons = counts.comparisons;
      R.cpuPreparation.uniformReuseHits = counts.hits;
      R.cpuPreparation.uniformReuseBytes = counts.reusedBytes;
    }
    lastUpload = b;
#ifdef WWHD_NATIVE_DRAW
    if (auto* cap = g_packetCapture) {
      const int st = sh->vertex ? 0 : 1;
      if (logicalSlot == 16) {
        cap->stage[st].supBinding = binding;
        cap->stage[st].supBuffer = b.buffer;
      } else if (const int k = cap->block_of(st, logicalSlot); k >= 0) {
        cap->stage[st].block[k].binding = binding;
        cap->stage[st].block[k].buffer = b.buffer;
      } else if (cap->hashes) {
        cap->uniformHash[st] = packet_hash(packet_hash_value(cap->uniformHash[st] + 1, binding), bytes, bytes ? size : 0);
      }
    }
#endif
    if (bufferCount >= bufferInfos.size())
      exception_report::raise("stage uniform descriptor capacity exceeded");
    auto &info = bufferInfos[bufferCount++];
    info = {b.buffer, dynamicUniforms ? 0 : b.offset, b.size};
    if (dynamicUniforms) {
      if (b.offset > UINT32_MAX)
        exception_report::raise("dynamic uniform offset exceeds uint32 range");
      if (useRanks) {
        const auto rank = logicalSlot == 16 ? rankPlan.support : rankPlan.blocks[logicalSlot];
        if (rank >= rankPlan.count)
          exception_report::raise("stage uniform rank metadata mismatch");
        rankedOffsets[rank] = uint32_t(b.offset);
      } else dynamicBindings[dynamicCount++] = {uint32_t(binding), uint32_t(b.offset)};
    }
    auto &write = appendWrite(logicalSlot == 16 ? rankPlan.support : rankPlan.blocks[logicalSlot]);
    write.dstBinding = binding;
    write.descriptorType = dynamicUniforms
        ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
        : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
  };
  // CPU-only scratch supplies an immutable upload slice below.
  // Separate stages retain capacity without retaining guest payload contents.
  static thread_local std::array<std::vector<uint8_t>, 2> supportScratch;
  auto& supportUniforms = supportScratch[sh->vertex ? 0 : 1];
  stereo::EyeRows supportRows;
  rprof::sub_end(rprof::kSubUniformSetup, stageEntered);
  const uint64_t packStarted = rprof::sub_begin();
  if (m.uniformVarsBufferBindingPoint >= 0) {
    const size_t capacity=preparation_stats_enabled()?supportUniforms.capacity():0;
    vk::pack_uniforms_into(r, *sh, supportUniforms, sx, sy, eyeOffset, stereo::frame.convergence,
                           replayOut ? &supportRows : nullptr);
    // WWHD_VK_UNIFORM_VERIFY=1 (debug): packs again into a separate buffer and compares, byte for
    // byte, with what this draw uploads; any change to how support uniforms are packed or reused
    // must keep "0 mismatches"
    static const bool verifyPack = [] {
      const char* e = std::getenv("WWHD_VK_UNIFORM_VERIFY");
      return e && std::strcmp(e, "1") == 0;
    }();
    if (verifyPack) {
      static thread_local std::vector<uint8_t> fresh;
      static uint64_t checks = 0, mismatches = 0;
      vk::pack_uniforms_into(r, *sh, fresh, sx, sy, eyeOffset, stereo::frame.convergence);
      ++checks;
      if (fresh != supportUniforms && mismatches++ < 16) {
        // where: the first differing byte, and the guest uniform-block entry it was copied from (if any)
        size_t at = 0;
        while (at < fresh.size() && at < supportUniforms.size() && fresh[at] == supportUniforms[at]) ++at;
        uint32_t source = 0;
        const int base = sh->uniforms.offset_remapped;
        const uint32_t blockBase = sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
        if (sh->dec && base >= 0)
          for (const auto& group : sh->dec->list_remappedUniformEntries_bufferGroups)
            for (const auto& entry : group.entries)
              if (at >= size_t(base + entry.mappedIndexOffset) && at < size_t(base + entry.mappedIndexOffset + 16))
                source = r[blockBase + group.kcacheBankIdOffset / 4] + entry.indexOffset;
        LOG("[vulkan uniform verify] MISMATCH: %s shader %016llx, %zu vs %zu bytes, first difference at +%zu%s%08X",
            sh->vertex ? "vertex" : "pixel", (unsigned long long)sh->key, supportUniforms.size(), fresh.size(), at,
            source ? ", read from guest memory at " : ", from registers ", source);
      }
      if ((checks & 0xFFFFF) == 0)
        LOG("[vulkan uniform verify] %llu checks, %llu mismatches", (unsigned long long)checks,
            (unsigned long long)mismatches);
    }
    if(preparation_stats_enabled()) {
      ++R.cpuPreparation.packCalls;
      R.cpuPreparation.packBytes+=supportUniforms.size();
      if(supportUniforms.capacity()>capacity) {
        ++R.cpuPreparation.packCapacityGrowths;
        R.cpuPreparation.packCapacityGrowthBytes+=supportUniforms.capacity()-capacity;
      }
    }
  }
  else
    supportUniforms.clear();
  // Bloom extract cThresholdParam.z: scale once, before blur/downsampling.
  if (!sh->vertex && (r[mmSQ_PGM_START_PS] << 8) == 0x44F91200 &&
      sh->uniforms.offset_remapped >= 0 && size_t(sh->uniforms.offset_remapped) < supportUniforms.size())
    render::scale_bloom_uniforms(supportUniforms.data() + sh->uniforms.offset_remapped,
                                supportUniforms.size() - sh->uniforms.offset_remapped);
  // Metal AO mode 2 tiles noise per 960x540 output pixel rather than 640x360.
  const int remapped = sh->uniforms.offset_remapped;
  if (sunShadow && eyeOffset != 0 && remapped >= 0 &&
      size_t(remapped) <= supportUniforms.size()) {
    // Unknown layouts keep their original uniforms. The helper validates
    // the complete block before writing any stereo adjustments.
    stereo::shadow_uniforms(supportUniforms.data()+remapped,
        supportUniforms.size()-remapped,sh->vertex,eyeOffset,stereo::frame.convergence);
  }
  if (sh->vertex && ao_mode() == 2 &&
      sh->kind == gfx::ProgramKind::OcclusionVertex && remapped >= 0 &&
      size_t(remapped) + 16 <= supportUniforms.size()) {
    float noiseScale;
    memcpy(&noiseScale, supportUniforms.data() + remapped + 12, sizeof noiseScale);
    noiseScale *= 1.5f;
    memcpy(supportUniforms.data() + remapped + 12, &noiseScale, sizeof noiseScale);
  }

  rprof::sub_end(rprof::kSubUniformPack, packStarted);
  const uint64_t blocksStarted = rprof::sub_begin();
  uint32_t block =
      sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
  for (int i = 0; i < 16; i++)
    if (m.uniformBuffersBindingPoint[i] >= 0) {
      uint32_t addr = r[block + i * 7];
      uint32_t size = std::min<uint32_t>(r[block + i * 7 + 1] + 1, 0x10000);
      // a block of a display list: its bytes as submitted (gx2/submitted_uniforms.h), uploaded once
      // per submission and upload generation, not through the buffer cache (which holds guest memory)
      const uint8_t* submitted = addr ? gx2::submitted_uniform_block(addr, size) : nullptr;
      const unsigned char* source = submitted ? submitted : static_cast<const unsigned char*>(addr ? mem::ptr(addr) : nullptr);
      // Only a block holding a camera projection gets an eye copy; the others keep their
      // buffer-cache upload by address.
      if (sh->vertex && eyeOffset != 0 && addr &&
          stereo::has_camera_projection(source, size)) {
        static thread_local std::vector<uint8_t> eyeBlock;
        eyeBlock.resize(size);memcpy(eyeBlock.data(),source,size);
        stereo::EyeRows blockRows;
        stereo::project_uniform_block(eyeBlock.data(),size,eyeOffset,stereo::frame.convergence,nullptr,
                                      replayOut ? &blockRows : nullptr);
        uniform(m.uniformBuffersBindingPoint[i],eyeBlock.data(),size,size_t(i));
        eyeUniform(m.uniformBuffersBindingPoint[i],eyeBlock.data(),size,blockRows);
      } else if (submitted) {
        struct Uploaded { uint32_t size; UploadSlice slice; };
        static std::unordered_map<uint32_t, Uploaded> uploaded;
        static uint64_t uploadedFor = 0, uploadedGeneration = 0;
        static VkDevice uploadedDevice = VK_NULL_HANDLE;
        if (uploadedFor != gx2::submitted_uniforms_id() || uploadedGeneration != R.submissionGeneration ||
            uploadedDevice != R.device) {
          uploaded.clear();
          uploadedFor = gx2::submitted_uniforms_id();
          uploadedGeneration = R.submissionGeneration;
          uploadedDevice = R.device;
        }
        auto it = uploaded.find(addr);
        if (it != uploaded.end() && it->second.size == size)
          uniform(m.uniformBuffersBindingPoint[i], submitted, size, size_t(i), 0, &it->second.slice);
        else {
          uniform(m.uniformBuffersBindingPoint[i], submitted, size, size_t(i));
          if (lastUpload.buffer) uploaded[addr] = {size, lastUpload};
        }
      } else uniform(m.uniformBuffersBindingPoint[i], addr ? mem::ptr(addr) : nullptr,
              size, size_t(i), addr);
      if (addr) rprof::guest_read(rprof::kUpUbo, addr, size);
    }
  rprof::sub_end(rprof::kSubUniformBlocks, blocksStarted);
  rprof::mark(rprof::kUniforms);
  uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS
                                : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
  auto bindTexture = [&](uint32_t unit, int binding, Surface* s, VkImageView view, VkSampler samplerValue) {
#ifdef WWHD_NATIVE_DRAW
    if (g_packetCapture && g_packetCapture->hashes) {
      auto& h = g_packetCapture->textureHash;
      h = packet_hash_value(packet_hash_value(packet_hash_value(packet_hash_value(h + 1, unit), binding), view), samplerValue);
      h = packet_hash_value(h, s);
    }
#endif
    int scaleOffset = sh->uniforms.offset_texScale[unit];
    if (scaleOffset >= 0 && size_t(scaleOffset) + 8 <= supportUniforms.size()) {
      const float scale[] = {s->sx, s->sy};
      memcpy(supportUniforms.data() + scaleOffset, scale, sizeof scale);
    }
    // hand-drawn mip levels where the shader applies the bias (MoltenVK): the resolution-aware part on top
    const int biasOffset = sh->uniforms.offset_texLodBias[unit];
    if (s->artisticMips && !R.samplerMipLodBias && biasOffset >= 0 && size_t(biasOffset) + 4 <= supportUniforms.size()) {
      float bias;
      memcpy(&bias, supportUniforms.data() + biasOffset, 4);
      bias += g_artisticBias;
      memcpy(supportUniforms.data() + biasOffset, &bias, 4);
    }
    // the half-size light buffer's grid on an upscaled buffer (gfx/area_sample.h)
    if ((sh->gatherUnits >> unit & 1) && sh->uniforms.offset_remapped >= 0 &&
        size_t(sh->uniforms.offset_remapped) < supportUniforms.size())
      ::gfx::area_sample::scale_gather_grid(reinterpret_cast<float*>(supportUniforms.data() + sh->uniforms.offset_remapped),
                                            (supportUniforms.size() - sh->uniforms.offset_remapped) / 16, s->width, s->height,
                                            s->sx, s->sy);
    if (imageCount >= imageInfos.size())
      exception_report::raise("stage image descriptor capacity exceeded");
    auto &info = imageInfos[imageCount++];
    info = {samplerValue, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto &write = appendWrite(rankPlan.textures[unit]);
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
  };
  if (reuse) {
    // continued draw: the previous draw's textures (upload_surface already ran for this draw)
    for (uint32_t i = 0; i < reuse->count; ++i) {
      const auto& t = reuse->textures[i];
      sample_transition(t.surface,
                        narrow_barriers()
                            ? (sh->vertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                          : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                            : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
      bindTexture(t.unit, t.binding, t.surface, t.view, t.sampler);
      replayTexture(t.surface, t.unit, t.binding, false);  // continued: neither feedback nor AO
    }
  } else
  for (int i = 0; i < sh->dec->textureUnitListCount; i++) {
    uint32_t unit = sh->dec->textureUnitList[i];
    if (unit >= LATTE_NUM_MAX_TEX_UNITS)
      exception_report::raise("sampled texture unit exceeds stage capacity");
    int binding = m.textureUnitToBindingPoint[unit];
    if (binding < 0)
      continue;
    bool substituted = false;
    Surface* s = resolve_texture(r, sh, unit, &substituted);
    // the point-light passes read the buffer's depth copy on unit 0: they claim the buffer's size for it,
    // which it gets from the next time it is drawn (target_scale)
    if (sh->kind == gfx::ProgramKind::PointLight && unit == 0 && s && s->gpuWritten && !substituted)
      s->sizeClaim.request(gfx::size_request(sh->kind), R.frame);
    bool aliases = aliases_target(s, colors, depth);
    int scaleOffset = sh->uniforms.offset_texScale[unit];
    if (scaleOffset >= 0 && size_t(scaleOffset) + 8 <= supportUniforms.size()) {
      const float scale[] = {s->sx, s->sy};
      memcpy(supportUniforms.data() + scaleOffset, scale, sizeof scale);
    }
    VkImageView sampledView =
        aliases ? feedback_view(s, r + texbase + unit * 7, sh->vertex, unit, feedbackProbe)
                : sampled_texture_view(s, r + texbase + unit * 7);
    if (!aliases)
      sample_transition(s,
                        narrow_barriers()
                            ? (sh->vertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                          : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                            : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
    VkSampler samplerValue = resolve_sampler(r, sh, unit, s);
    bindTexture(unit, binding, s, sampledView, samplerValue);
    replayTexture(s, unit, binding, aliases || substituted);
    if (record) {
      if (aliases || substituted || shares_target_address(s, colors, depth)) record->continuable = false;
      record->add({unit, binding, s, sampledView, samplerValue});
    }
  }
  rprof::mark(rprof::kTextures);
  if (m.uniformVarsBufferBindingPoint >= 0) {
    const uint64_t uploadStarted = rprof::sub_begin();
#ifdef WWHD_NATIVE_DRAW
    if (g_packetCapture) g_packetCapture->stage[sh->vertex ? 0 : 1].supBytes.assign(supportUniforms.begin(), supportUniforms.end());
#endif
    uniform(m.uniformVarsBufferBindingPoint, supportUniforms.data(),
            supportUniforms.size(), 16);
    rprof::sub_end(rprof::kSubUniformSupportUpload, uploadStarted);
    eyeUniform(m.uniformVarsBufferBindingPoint, supportUniforms.data(), supportUniforms.size(), supportRows);
  }
  rprof::mark(rprof::kUniforms);
  struct DescriptorMark { ~DescriptorMark() { rprof::mark(rprof::kDescriptors); } } descriptorMark;
  if (useRanks) {
    uint32_t dense = 0;
    for (uint32_t rank = 0; rank < rankPlan.count; ++rank) {
      if (!occupied[rank]) continue;
      const auto type = writes[rank].descriptorType;
      writes[dense++] = writes[rank];
      if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        dynamicBindings[dynamicCount++] = {writes[dense-1].dstBinding, rankedOffsets[rank]};
    }
  } else {
    std::sort(writes.begin(), writes.begin() + writeCount,
        [](const auto &a, const auto &b) { return a.dstBinding < b.dstBinding; });
    std::sort(dynamicBindings.begin(), dynamicBindings.begin() + dynamicCount);
  }
  out.dynamicOffsetCount = dynamicCount;
  for (uint32_t i = 0; i < dynamicCount; ++i)
    out.dynamicOffsets[i] = dynamicBindings[i].second;
#ifdef WWHD_NATIVE_DRAW
  if (auto* cap = g_packetCapture) {
    auto& c = cap->stage[sh->vertex ? 0 : 1];
    // (ranked or not: dynamicBindings holds the bindings in the order of their dynamic offsets)
    c.dynamic = dynamicUniforms;
    c.supIndex = -1;
    for (uint32_t i = 0; i < dynamicCount; ++i)
      if (c.supBinding >= 0 && dynamicBindings[i].first == uint32_t(c.supBinding)) c.supIndex = int(i);
    for (int k = 0; k < PacketCapture::kBlocks; ++k) {
      auto& blk = c.block[k];
      blk.index = -1;
      for (uint32_t i = 0; i < dynamicCount; ++i)
        if (blk.binding >= 0 && dynamicBindings[i].first == uint32_t(blk.binding)) blk.index = int(i);
      blk.remap.clear();
      const int slot = cap->slot[sh->vertex ? 0 : 1][k];
      if (sh->dec && sh->uniforms.offset_remapped >= 0 && slot >= 0)
        for (const auto& group : sh->dec->list_remappedUniformEntries_bufferGroups)
          if (uint32_t(group.kcacheBankIdOffset / 4) == uint32_t(slot) * 7)
            for (const auto& entry : group.entries)
              blk.remap.push_back({uint32_t(sh->uniforms.offset_remapped + entry.mappedIndexOffset), uint32_t(entry.indexOffset)});
      if (blk.binding >= 0 && dynamicUniforms && blk.index < 0) cap->refuse("instance binding");
    }
    // the instance register ranges' support bytes (as pack_uniforms_into copies them; stereo is off for packets)
    c.regRemap.clear();
    if (const auto* pk = cap->packet; pk && sh->dec) {
      const uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (sh->vertex ? 0x400 : 0);
      for (uint32_t i = 0; i < pk->regCount && i < native_draw::kMaxRegRanges; ++i) {
        if ((pk->reg[i] >> 16) != uint32_t(sh->vertex ? 0 : 1)) continue;
        const uint32_t lo = pk->reg[i] & 0xFFFF, hi = lo + pk->regWords[i];
        if (sh->uniforms.offset_remapped >= 0)
          for (const auto& entry : sh->dec->list_remappedUniformEntries_register)
            if (const uint32_t w = uint32_t(entry.indexOffset) / 4; w < hi && w + 4 > lo)
              c.regRemap.push_back({uint32_t(sh->uniforms.offset_remapped + entry.mappedIndexOffset), aluBase + w, 4});
        if (sh->uniforms.offset_uniformRegister >= 0) {
          const uint32_t n = uint32_t(sh->uniforms.count_uniformRegister) * 4, a = std::min(lo, n), b = std::min(hi, n);
          if (a < b) c.regRemap.push_back({uint32_t(sh->uniforms.offset_uniformRegister) + a * 4, aluBase + a, b - a});
        }
      }
      // (support bytes adjusted after packing: the bloom extract threshold, the AO noise scale)
      if (!c.regRemap.empty() && ((!sh->vertex && (r[mmSQ_PGM_START_PS] << 8) == 0x44F91200) ||
                                  (sh->vertex && sh->kind == gfx::ProgramKind::OcclusionVertex)))
        cap->refuse("support uniforms adjusted after packing");
    }
    if (c.patches()) {
      if (sh->gatherUnits) cap->refuse("gather grid in the support uniforms");
      if (c.supBinding < 0 || c.supBytes.empty()) cap->refuse("support uniforms");
      if (dynamicUniforms && c.supIndex < 0) cap->refuse("support binding");
    }
  }
#endif
  if (replayOut && replayable) {
    replayOut->valid = true;
    for (uint32_t u = 0; u < replayOut->uniformCount; ++u) {
      uint32_t i = 0;
      while (i < dynamicCount && dynamicBindings[i].first != uint32_t(eyeUniformBindings[u])) ++i;
      if (i == dynamicCount) replayOut->valid = false;
      else replayOut->uniforms[u].dynamicIndex = i;
    }
    // eye 1 resolves its own image of the other textures and writes its own set with them
    if (replayOut->eyeTextureCount) {
      replayOut->layout = layout;
      replayOut->writeCount = writeCount;
      for (uint32_t i = 0; i < writeCount; ++i) {
        auto& w = replayOut->writes[i];
        w = writes[i];
        if (w.pBufferInfo) replayOut->bufferInfos[i] = *w.pBufferInfo;
        if (w.pImageInfo) replayOut->imageInfos[i] = *w.pImageInfo;
        w.pBufferInfo = nullptr;
        w.pImageInfo = nullptr;
      }
      for (uint32_t e = 0; e < replayOut->eyeTextureCount; ++e) {
        auto& t = replayOut->eyeTextures[e];
        uint32_t i = 0;
        while (i < writeCount && (writes[i].dstBinding != uint32_t(t.binding) || !writes[i].pImageInfo)) ++i;
        if (i == writeCount) replayOut->valid = false;
        else t.write = i;
      }
    }
  }
#ifdef WWHD_NATIVE_DRAW
  if (auto* cap = g_packetCapture; cap && !dynamicUniforms) {
    auto& c = cap->stage[sh->vertex ? 0 : 1];
    c.layout = layout;
    c.writeCount = writeCount;
    for (auto& blk : c.block) blk.instWrite = -1;
    for (uint32_t i = 0; i < writeCount; ++i) {
      c.writes[i] = writes[i];
      if (writes[i].pBufferInfo) c.bufferInfos[i] = *writes[i].pBufferInfo;
      if (writes[i].pImageInfo) c.imageInfos[i] = *writes[i].pImageInfo;
      for (auto& blk : c.block)
        if (blk.binding >= 0 && writes[i].pBufferInfo && writes[i].dstBinding == uint32_t(blk.binding)) blk.instWrite = int(i);
      if (c.supBinding >= 0 && writes[i].pBufferInfo && writes[i].dstBinding == uint32_t(c.supBinding)) c.supWrite = int(i);
    }
    for (const auto& blk : c.block)
      if (blk.binding >= 0 && blk.instWrite < 0) cap->refuse("instance write");
    if (c.patches() && c.supWrite < 0) cap->refuse("support write");
  }
#endif
  out.set = stage_descriptor_set(sh->vertex, layout, writes.data(), writeCount);
  return out;
}
// The stage's descriptor set for these writes: reused within the submission, else written.
VkDescriptorSet stage_descriptor_set(bool vertex, VkDescriptorSetLayout layout,
                                     VkWriteDescriptorSet* writes, uint32_t writeCount) {
  VkDescriptorSet set{};
  // Resource preparation above must always run, even when the descriptor set
  // itself is reusable. Pool reset/slot activation invalidates every old set.
  static uint64_t cacheGeneration = ~uint64_t{0};
  static SubmissionDescriptorCache descriptorCache;
  static LastDescriptorSet lastDescriptors[2];
  if (cacheGeneration != R.submissionGeneration) {
    descriptorCache.clear();
    lastDescriptors[0] = {}; lastDescriptors[1] = {};
    cacheGeneration = R.submissionGeneration;
  }
  ++R.descriptorLookups;
  auto &last = lastDescriptors[vertex ? 0 : 1];
  if (descriptor_matches(last, layout, writes, writeCount)) {
    ++R.descriptorCacheHits;
    ++R.descriptorFastHits;
    return last.set;
  }
  const uint64_t hash = descriptor_hash(layout, writes, writeCount);
  if (auto found = descriptorCache.find(hash, layout, writes, writeCount)) {
    ++R.descriptorCacheHits;
    remember_descriptors(last, layout, found, writes, writeCount);
    return found;
  }
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = R.descriptorPool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layout;
  vk_check(vkAllocateDescriptorSets(R.device, &ai, &set), "descriptor set");
  ++R.descriptorAllocations;
  for (uint32_t i = 0; i < writeCount; ++i) writes[i].dstSet = set;
  if (writeCount)
    vkUpdateDescriptorSets(R.device, writeCount, writes, 0, nullptr);
  descriptorCache.insert(hash, layout, set, writes, writeCount);
  remember_descriptors(last, layout, set, writes, writeCount);
  return set;
}
// Eye 1's stage from eye 0's (StageReplay); false: eye 1 binds the stage itself.
bool replay_stage(const StageReplay& in, const uint32_t* r, vk::Shader* sh, float eyeOffset,
                  const std::array<Surface*, 8>& colors, const Surface* depth, StageResources& out) {
  out = in.resources;
  for (uint32_t i = 0; i < in.uniformCount; ++i) {
    const auto& u = in.uniforms[i];
    static thread_local std::vector<uint8_t> scratch;
    scratch.assign(u.bytes.begin(), u.bytes.end());
    stereo::apply_eye_rows(scratch.data(), u.rows, eyeOffset, stereo::frame.convergence);
    rprof::UploadKind uploads(rprof::kUpUbo);
    const auto b = snapshot(scratch.data(), scratch.size(), R.properties.limits.minUniformBufferOffsetAlignment);
    if (b.buffer != u.buffer || b.offset > UINT32_MAX) return false;  // the set names another buffer
    out.dynamicOffsets[u.dynamicIndex] = uint32_t(b.offset);
  }
  if (in.eyeTextureCount) {
    std::array<VkWriteDescriptorSet, 17 + LATTE_NUM_MAX_TEX_UNITS> writes;
    std::array<VkDescriptorImageInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> images;
    for (uint32_t i = 0; i < in.writeCount; ++i) {
      writes[i] = in.writes[i];
      if (writes[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        images[i] = in.imageInfos[i];
        writes[i].pImageInfo = &images[i];
      } else writes[i].pBufferInfo = &in.bufferInfos[i];
    }
    const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t e = 0; e < in.eyeTextureCount; ++e) {
      const auto& t = in.eyeTextures[e];
      bool substituted = false;
      Surface* s = resolve_texture(r, sh, t.unit, &substituted);
      // the texture scale in eye 0's uniforms
      if (substituted || aliases_target(s, colors, depth) || s->sx != t.sx || s->sy != t.sy) return false;
      transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       narrow_barriers()
                           ? (sh->vertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                           : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                       VK_ACCESS_SHADER_READ_BIT);
      images[t.write] = {resolve_sampler(r, sh, t.unit, s), sampled_texture_view(s, r + texbase + t.unit * 7),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    out.set = stage_descriptor_set(sh->vertex, in.layout, writes.data(), in.writeCount);
  }
  for (uint32_t i = 0; i < in.textureCount; ++i) stereo_hold_texture(in.textures[i]);
  return true;
}
// WWHD_VK_FASTPATH_VERIFY: the full resolution of a continued draw, compared with the reused state
// (partial: of a partial continuation, compared with the state it recorded)
// textures=false: shaders, topology, targets and pipeline only (a partial draw whose new stage cannot continue)
bool verify_continued(const uint32_t* r, uint32_t prim, bool partial = false, bool textures = true) {
  const auto& c = continuedEyes[stereoEye];
  int kind = -1;
  char detail[200] = "";
  uint64_t fsKey = 0;
  auto* fs = vk::get_fetch_shader(r, &fsKey, R.frame);
  vk::Shader* vs = nullptr;
  vk::Shader* ps = nullptr;
  if (fs) vk::translate_stages(r, fs, fsKey, R.frame, g_shader_state_gen, vs, ps);
  DrawTargets t;
  if (fs != c.fs || fsKey != c.fsKey) kind = 0;
  else if (vs != c.vs || !vs || !vs->ready()) kind = 1;
  else if (ps != c.ps || !ps || !ps->ready()) kind = 2;
  else if (primitive_topology(prim) != c.topology) kind = 7;
  else if (!resolve_targets(r, ps, t) || !t.same(c.targets)) kind = 3;
  else if (&pipeline(r, vs, ps, fs, c.topology, t.colors, t.depth) != c.ownPipeline ||
           (c.subsetAbsent && &pipeline(r, vs, ps, fs, c.topology, c.subsetColors, t.depth,
                                        {.clearMask = pass_reuse::clear_mask(c.subsetAbsent), .background = true}) != c.pipeline))
    kind = 4;
  else if (textures)
    for (int stage = 0; stage < 2 && kind < 0; ++stage) {
      vk::Shader* sh = stage ? ps : vs;
      const auto& cs = c.stages[stage];
      const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
      uint32_t n = 0;
      for (int i = 0; i < sh->dec->textureUnitListCount && kind < 0; i++) {
        const uint32_t unit = sh->dec->textureUnitList[i];
        if (unit >= LATTE_NUM_MAX_TEX_UNITS) { kind = 6; break; }
        const int binding = sh->mapping.textureUnitToBindingPoint[unit];
        if (binding < 0) continue;
        if (n >= cs.count) { kind = 5; break; }
        const auto& e = cs.textures[n++];
        Surface* s = resolve_texture(r, sh, unit);
        const bool aliases = aliases_target(s, t.colors, t.depth);
        VkImageView view = aliases ? VK_NULL_HANDLE : sampled_texture_view(s, r + texbase + unit * 7);
        VkSampler smp = resolve_sampler(r, sh, unit, s);
        if (e.unit != unit || e.binding != binding || e.surface != s || aliases || view != e.view || smp != e.sampler) {
          kind = 6;
          std::snprintf(detail, sizeof detail, " (%s unit %u: surface %08X%s/%08X, view %s, sampler %s%s)",
                        stage ? "ps" : "vs", unit, s->addr, e.surface == s ? "" : " (cached differs)",
                        e.surface ? e.surface->addr : 0, view == e.view ? "same" : "differs",
                        smp == e.sampler ? "same" : "differs", aliases ? ", aliases a target" : "");
        }
      }
      if (kind < 0 && n != cs.count) kind = 5;
    }
  if (kind < 0) return true;
  auto& st = continuedStats;
  ++st.mismatches;
  ++st.totalMismatches;
  ++st.mismatchKinds[kind];
  static int logged = 0;
  if (logged < 50) {
    ++logged;
    LOG("[vulkan fastpath] verify mismatch #%llu%s: %s%s; frame %llu draw %llu prim %u vs %08X ps %08X fs %08X",
        (unsigned long long)st.totalMismatches, partial ? " (partial continuation)" : "", mismatchKindNames[kind], detail,
        (unsigned long long)R.frame,
        (unsigned long long)R.drawCount, prim, r[mmSQ_PGM_START_VS] << 8, r[mmSQ_PGM_START_PS] << 8,
        r[mmSQ_PGM_START_FS] << 8);
  }
  return false;
}
ContinuedReason continued_reason(const uint32_t* r, uint32_t prim) {
  auto& c = continuedEyes[stereoEye];
  if (!c.valid) return c.invalidReason;
  if (c.frame != R.frame) return kNewFrame;
  if (c.stateVersion != gx2::g_full_state_version) return kRegisters;
  if (c.surfaceEpoch != g_surface_epoch) return kSurfaces;
  if (c.prim != prim) return kPrimitive;
  if (c.aoMode != ao_mode() || c.aoHires != ao_hires_enabled() || c.aniso != aniso_enabled()) return kSettings;
  // the scissor is a continued (dynamic) word, but GetActiveColorBufferMask drops the colour buffers
  // smaller than it: a new scissor continues only while it leaves the same targets active
  if (r[REGADDR::PA_SC_GENERIC_SCISSOR_BR] != c.scissorBR) {
    if (LatteMRT::GetActiveColorBufferMask(c.ps->dec, *reinterpret_cast<const LatteContextRegister*>(r)) !=
        c.targets.activeColors)
      return kScissorTargets;
    c.scissorBR = r[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
  }
  return kContinued;
}
// WWHD_VK_FASTPATH_PARTIAL: a draw that registers kept from continuing (kRegisters: the state is
// valid and of this frame) may reuse pieces of the state: everything else continued_reason checks
// holds, and no register outside the known groups changed
bool partial_candidate(uint32_t prim) {
  const auto& c = continuedEyes[stereoEye];
  return c.surfaceEpoch == g_surface_epoch && c.prim == prim && c.aoMode == ao_mode() &&
         c.aoHires == ao_hires_enabled() && c.aniso == aniso_enabled() && c.pipeline &&
         !(partialGroups[stereoEye] >> gx2::kSgOther & 1);
}
} // namespace
// Sizing (WWHD_VK_MEMORY_LOG_FRAMES): the game's pipelines (never freed), those still compiling (each holds its two
// VkShaderModules until done; a finished pipeline keeps none) and the failed ones.
void guest_pipeline_counts(size_t& total, size_t& compiling, size_t& failed) {
  total = pipelines.size(); compiling = failed = 0;
  for (const auto& [key, p] : pipelines) {
    if (p.pendingCompile) ++compiling;
    else if (!p.pipeline) ++failed;
  }
}
VkDescriptorSet texture_stage_smoke_bind(const uint32_t* registers,
    VkDescriptorSetLayout layout, uint32_t textureCount) {
  if (textureCount > LATTE_NUM_MAX_TEX_UNITS)
    exception_report::raise("invalid smoke texture count");
  LatteDecompilerShader decoded(LatteConst::ShaderType::Pixel);
  vk::Shader shader;
  shader.dec = &decoded;
  decoded.textureUnitListCount = uint8_t(textureCount);
  for (uint32_t i = 0; i < textureCount; ++i) {
    decoded.textureUnitList[i] = uint8_t(i);
    decoded.textureUnitSamplerAssignment[i] = uint16_t(i);
    shader.mapping.textureUnitToBindingPoint[i] = int8_t(i);
  }
  return bind_stage(registers, &shader, layout, false, 1, 1, {}, nullptr, nullptr).set;
}
void reset_feedback_images() { reset_feedback_scratch(); }
UploadSlice vertex_window_smoke_snapshot(uint32_t binding,uint32_t address,
    uint32_t reservation,uint32_t windowOffset,uint32_t windowLength,
    const void* data,bool poisonUnused) {
  if(!data || !windowLength || windowOffset>reservation ||
     windowLength>reservation-windowOffset)
    exception_report::raise("invalid vertex window smoke bounds");
  return vertex_window_snapshot(binding,address,reservation,windowOffset,
                                windowLength,data,poisonUnused);
}

void stereo_release_ao() {
  auto& ao = aoPrivateEyes[1];
  destroy_surface_image(&ao.color);
  destroy_surface_image(&ao.depth);
  ao = {};
}
void reset_ao_private_cache() {
  for (auto& ao : aoPrivateEyes) {
    ao.frame = ~0ull;
    ao.source = 0;
  }
}
void drain_pending_guest_pipelines() {
  // (a background variant is not waited for: never recorded, nothing here needs it; the compiler's
  // shutdown, finish_pipeline_demand, lets its lane finish)
  g_drainingPipelines = true;
  try {
    for (auto& [key, value] : pipelines)
      if (!value.pendingCompile || !value.pendingCompile->background) resolve_pending_pipeline(value);
  }
  catch (...) { g_drainingPipelines = false; throw; }
  g_drainingPipelines = false;
}
#ifdef WWHD_NATIVE_DRAW
void j3d_cache_clear();
#endif
void reset_pipeline_lookup_cache() {
  lastPipelineLookup = {}; pipelineLookaside = {}; samplerMemo = {}; drawBatchState = {}; continuedEyes = {};
#ifdef WWHD_NATIVE_DRAW
  j3d_cache_clear();
#endif
}
uint64_t draw_batch_submissions() { return drawBatchSubmissions; }
// WWHD_VK_TRACE_PASSES=1 (issue #144): log each new render pass shape once, just before the driver
// sees it, so a crash inside vkCmdBeginRendering names the last pass in the crash report's log lines
static void trace_pass(const std::array<Surface *, 8> &colors, uint32_t ncolor, const Surface *depth,
                       uint32_t depthSlice, uint32_t width, uint32_t height) {
  static const bool on = [] { const char *e = getenv("WWHD_VK_TRACE_PASSES"); return e && *e == '1'; }();
  if (!on) return;
  static std::unordered_set<std::string> seen;
  std::string key = std::to_string(ncolor) + ":";
  for (uint32_t i = 0; i < ncolor; ++i)
    key += colors[i] ? std::to_string(colors[i]->fmt.pixel) + "/" + std::to_string(colors[i]->usage) + "," : "-,";
  if (depth)
    key += " d" + std::to_string(depth->fmt.pixel) + "/" + std::to_string(depth->usage) + "/" +
           std::to_string(depth->arrayLayers) + "/" + std::to_string(depth->mips) + "/" +
           std::to_string(depth->createFlags);
  if (!seen.insert(key).second) return;
  std::string text;
  for (uint32_t i = 0; i < ncolor; ++i)
    text += colors[i] ? " c" + std::to_string(i) + "=fmt" + std::to_string(colors[i]->fmt.pixel) + "/usage" +
                            std::to_string(colors[i]->usage)
                      : " c" + std::to_string(i) + "=none";
  if (depth)
    text += " depth=fmt" + std::to_string(depth->fmt.pixel) + "/usage" + std::to_string(depth->usage) +
            "/layers" + std::to_string(depth->arrayLayers) + "/slice" + std::to_string(depthSlice) + "/mips" +
            std::to_string(depth->mips) + "/flags" + std::to_string(depth->createFlags) +
            (depth->fmt.stencil ? "/stencil" : "");
  LOG("[vulkan pass] %ux%u, %u colour attachment(s)%s", width, height, ncolor, text.c_str());
}
// The depth target is the light-space sun atlas both eyes share (stereo_light_atlas).
static bool sun_atlas_target(const uint32_t* r) {
  return r[mmDB_HTILE_DATA_BASE]==((1024u<<16)|1024u) && r[gx2::kDepthSlicesReg]==3 &&
      LatteMRT::GetActiveDepthBufferMask(*reinterpret_cast<const LatteContextRegister*>(r));
}
void draw_stereo(const uint32_t *r,uint32_t prim,uint32_t count,uint32_t indexType,
                 uint32_t indexAddr,uint32_t baseVertex,uint32_t instances) {
  if(stereo::frame.mode==stereo::Off) {
    stereo_select_eye(0);
    draw(r,prim,count,indexType,indexAddr,baseVertex,instances);
    return;
  }
  // Eye 0 always draws first, so it makes every transition of the shared atlas in the
  // renderer's stream. Eye 1's held commands may still sample the atlas: emit them before
  // it is drawn again, and emit an eye 1 draw into it at once.
  const bool atlas=sun_atlas_target(r);
  if(atlas) stereo_flush();
  bool shared=false;
  try {
    stereo_select_eye(0);
    stereoShaders.reset();
    stereoShadersOut=&stereoShaders;
    draw(r,prim,count,indexType,indexAddr,baseVertex,instances,&shared);
    if(shared) return; // keep the light-space render pass open
    stereo_select_eye(1);
    stereoShadersIn=stereoShaders.vs?&stereoShaders:nullptr;
    draw(r,prim,count,indexType,indexAddr,baseVertex,instances);
    if(atlas) stereo_flush();
  } catch(...) { stereoShadersOut=nullptr;stereoShadersIn=nullptr;stereo_select_eye(0);throw; }
}
void clouds_before_draw(const uint32_t* r, const vk::Shader* ps, Surface* color0, uint32_t slice0, Surface* depth);
bool clouds_skip_card();
// the tall grass vertex programs (Graphics > Grass draw distance), by vk::vertex_program_hash
// (the three of the islands' tall grass, 832, 1120 and 1648 bytes)
constexpr uint64_t kGrassVs[] = {0x23CDD3D4D6BAAACDull, 0x042E089911FC3CC0ull, 0x7C8717CA596569B3ull};

bool clouds_pass_subset_ok(const std::array<Surface*, 8>& colors, const Surface* depth);
#ifdef WWHD_NATIVE_DRAW
// ---------------------------------------------------------------- J3D material cache (gx2/native_draw.h)
// A J3D shape draw (named by OP_J3D_SHAPE) looks up the state its key resolved last time: the continued-draw state
// (ContinuedDraw: fetch, vertex and pixel shaders, render targets, pipeline, each stage's texture views and samplers).
// It is used, as the continued state of this draw, only when the full resolution now would give the same:
// - every register the resolution reads outside the continued groups: the shader, fetch/interface, target and
//   output groups word for word (j3d_ranges: DB_STENCILREFMASK(_BF) without the reference byte, which is dynamic),
//   the texture resource and sampler words of every unit the two programs use (j3d_texture_words), and the primitive
//   type (VGT_PRIMITIVE_TYPE, which draw() sets: the only kSgOther register the resolution reads; its readers are
//   get_fetch_shader, translate (gather_linkage, ps_link, variant_hash, clip_emulation_wanted_regs), resolve_targets
//   (GetActive*BufferMask, color_target, depth_target), pipeline_key, resolve_texture, sampled_texture(_view) and
//   resolve_sampler);
// - the programs' bytes: the vertex and pixel program hashes the shader keys start from (program_hash: a new frame
//   checks them again, as the full lookup does) and the fetch shader as get_fetch_shader finds it now;
// - the surfaces: no structural surface change since (a surface made, released, its image replaced or rescaled,
//   a save-state reset: g_surface_struct_gen, and g_surface_set_gen), each
//   sampled texture the only surface at its address in the cache (so write sequences pick no other one) with the
//   same artistic-mips and GPU-written flags; its CPU writes upload first (an upload ends the hit, as for a
//   continued draw);
// - the render pass of the state's targets (or its pass subset) open, so the draw's targets were resolved this frame
//   (their size claims and rescales made) and nothing else was drawn into them in between;
// - the settings the state was resolved with (AO, anisotropy, the shader keys' vertex format and diagnostics salts,
//   clipping in shaders, graphics-pack shaders present); program kinds
//   with port features (AO, shadow mask, bloom, point lights) and geometry shaders are not stored.
// Everything after the resolution (uniforms, support uniforms, descriptors, vertex and index data, dynamic state,
// the pass, the draw) runs as for any continued draw. Stereo, 60 fps (generated), the GamePad backdrop and the
// clouds bracket take the full draw. Entries: an open-addressed table (WWHD_NATIVE_DRAW_J3D_SLOTS, default 4096),
// 4 probes, the least recently used of them replaced.
namespace {
struct J3dEntry {
  uint64_t key = 0;  // 0: empty
  uint64_t lastUsed = 0, structGen = 0, setGen = 0, checkedFrame = ~0ull;
  uint64_t vsHash = 0, psHash = 0, salt[4]{};
  uint32_t prim = 0;
  ContinuedDraw state;
  std::vector<uint32_t> words;    // j3d_ranges' values
  std::vector<uint32_t> texRegs, texWords;
  std::array<bool, 2 * LATTE_NUM_MAX_TEX_UNITS> artistic{}, gpuWritten{};
};
struct J3dCache {
  std::vector<J3dEntry> slots;
  std::vector<std::pair<uint32_t, uint32_t>> ranges;  // (first register, count): j3d_ranges
  uint32_t rangeWords = 0;
  uint64_t verifyCounter = 0;
} j3d;
constexpr uint32_t kJ3dProbes = 4;
uint64_t j3d_struct_gen() { return g_surface_struct_gen; }
// the registers compared word for word: every register of the shader, fetch/interface, target and output groups
// (gx2/state_groups.h), and DB_STENCILREFMASK(_BF) (kSgStencilRefMask: compared without the reference byte)
const std::vector<std::pair<uint32_t, uint32_t>>& j3d_ranges() {
  if (j3d.ranges.empty()) {
    constexpr uint32_t kMask = 1u << gx2::kSgShader | 1u << gx2::kSgFetch | 1u << gx2::kSgTargets |
        1u << gx2::kSgOutput | 1u << gx2::kSgOutputTargets | 1u << gx2::kSgOutputShader;
    for (uint32_t reg = 0; reg < gx2::kNumRegs; ++reg) {
      const uint8_t g = gx2::state_group(reg);
      if (g == gx2::kSgStencilRefMask || !(kMask >> g & 1)) continue;
      if (!j3d.ranges.empty() && j3d.ranges.back().first + j3d.ranges.back().second == reg) ++j3d.ranges.back().second;
      else j3d.ranges.push_back({reg, 1});
    }
    j3d.rangeWords = 0;
    for (const auto& [first, n] : j3d.ranges) j3d.rangeWords += n;
    LOG("[native-draw] j3d: %u registers compared in %zu ranges, plus DB_STENCILREFMASK(_BF) and the units' texture words",
        j3d.rangeWords, j3d.ranges.size());
  }
  return j3d.ranges;
}
inline uint32_t j3d_stencil_bits(const uint32_t* r, int i) {
  return r[i ? REGADDR::DB_STENCILREFMASK_BF : REGADDR::DB_STENCILREFMASK] & 0xFFFFFF00u;
}
// the texture resource words (7) and sampler words (3) of every unit the program uses (as variant_hash,
// resolve_texture, sampled_texture_view and resolve_sampler read them)
void j3d_texture_words(const vk::Shader* sh, std::vector<uint32_t>& regs) {
  if (!sh || !sh->dec) return;
  const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
  for (int i = 0; i < sh->dec->textureUnitListCount; ++i) {
    const uint32_t unit = sh->dec->textureUnitList[i];
    if (unit >= LATTE_NUM_MAX_TEX_UNITS) continue;
    for (uint32_t w = 0; w < 7; ++w) regs.push_back(texbase + unit * 7 + w);
    const uint32_t si = sh->dec->textureUnitSamplerAssignment[unit];
    if (si < 18)
      for (uint32_t w = 0; w < 3; ++w) regs.push_back(REGADDR::SQ_TEX_SAMPLER_WORD0_0 + ((sh->vertex ? 18 : 0) + si) * 3 + w);
  }
}
uint32_t j3d_slot_count() {
  static const uint32_t n = [] {
    const char* e = std::getenv("WWHD_NATIVE_DRAW_J3D_SLOTS");
    uint32_t v = e && *e ? uint32_t(std::max(16, std::atoi(e))) : 4096u;
    uint32_t p = 16;
    while (p < v && p < (1u << 20)) p <<= 1;
    return p;
  }();
  return n;
}
// the entry's key: the site's key and the draw's depth view and target addresses (the shadow cascades draw the same
// shapes with the same key into the slices of one depth array: an entry per slice; the key only finds an entry)
uint64_t j3d_key(uint64_t key, const uint32_t* r) {
  for (uint32_t reg : {uint32_t(mmDB_DEPTH_VIEW), uint32_t(mmDB_DEPTH_BASE), uint32_t(mmCB_COLOR0_BASE)}) {
    key = (key ^ r[reg]) * 0xFF51AFD7ED558CCDull;
    key ^= key >> 32;
  }
  return key ? key : 1;
}
J3dEntry* j3d_find(uint64_t key) {
  if (j3d.slots.empty()) return nullptr;
  const uint32_t mask = uint32_t(j3d.slots.size() - 1);
  for (uint32_t i = 0; i < kJ3dProbes; ++i) {
    J3dEntry& e = j3d.slots[(uint32_t(key) + i) & mask];
    if (e.key == key) return &e;
  }
  return nullptr;
}
// may this draw use or make an entry? (the port features that act per draw take the full draw)
bool j3d_eligible(bool fastPathDraw, const void* replay, const void* shadersIn, const void* shadersOut) {
  return fastPathDraw && !replay && !shadersIn && !shadersOut && !aoPrivateReplay && !stereoEye &&
         stereo::frame.mode == stereo::Off && !framegen::requested() && !reproj::hud_coverage_wanted() &&
         !gamepad_backdrop_capture_active() && !gamepad_backdrop_replaying() && !cloud_proto::g_active_seq;
}
// the entry holds (see above); its state then becomes this draw's continued state. Else the counted reason
// WWHD_NATIVE_DRAW_J3D_DEBUG=1 (diagnostics): which compared registers differed when an entry did not hold
// (every differing register counted; the top ones logged every 20000 such entries)
bool j3d_debug() {
  static const bool on = [] { const char* e = std::getenv("WWHD_NATIVE_DRAW_J3D_DEBUG"); return e && *e == '1'; }();
  return on;
}
void j3d_debug_registers(const J3dEntry& e, const uint32_t* r) {
  static std::unordered_map<uint32_t, uint64_t> counts;
  static uint64_t n = 0;
  const uint32_t* w = e.words.data();
  for (const auto& [first, k] : j3d.ranges) {
    for (uint32_t i = 0; i < k; ++i)
      if (r[first + i] != w[i]) ++counts[first + i];
    w += k;
  }
  if (j3d_stencil_bits(r, 0) != w[0]) ++counts[REGADDR::DB_STENCILREFMASK];
  if (j3d_stencil_bits(r, 1) != w[1]) ++counts[REGADDR::DB_STENCILREFMASK_BF];
  for (size_t i = 0; i < e.texRegs.size(); ++i)
    if (r[e.texRegs[i]] != e.texWords[i]) ++counts[e.texRegs[i]];
  if (++n % 20000) return;
  std::vector<std::pair<uint64_t, uint32_t>> top;
  for (const auto& [reg, c] : counts) top.push_back({c, reg});
  std::sort(top.rbegin(), top.rend());
  std::string text;
  for (size_t i = 0; i < top.size() && i < 32; ++i) {
    char b[40];
    std::snprintf(b, sizeof b, " %04X:%llu", top[i].second, (unsigned long long)top[i].first);
    text += b;
  }
  LOG("[native-draw] j3d debug: %llu entries that did not hold; registers that differed (register:count):%s",
      (unsigned long long)n, text.c_str());
}
// WWHD_NATIVE_DRAW_J3D_STALE_TEST=1 (the stale-entry test only, never for play): the texture words are not
// compared, so an entry whose textures changed is used stale (verify mode and pictures must show it)
bool j3d_stale_test() {
  static const bool on = [] {
    const char* e = std::getenv("WWHD_NATIVE_DRAW_J3D_STALE_TEST");
    const bool v = e && *e == '1';
    if (v) LOG("[native-draw] j3d STALE TEST: texture words not compared (wrong pictures expected)");
    return v;
  }();
  return on;
}
native_draw::J3dCount j3d_check(const J3dEntry& e, const uint32_t* r, uint32_t prim) {
  using namespace native_draw;
  if (e.structGen != j3d_struct_gen() || e.setGen != g_surface_set_gen) return kJ3dSurfaces;
  if (e.prim != prim) return kJ3dPrimitive;
  const ContinuedDraw& c = e.state;
  if (c.aoMode != ao_mode() || c.aoHires != ao_hires_enabled() || c.aniso != aniso_enabled() ||
      e.salt[0] != R.vertexFormats.shader_salt() || e.salt[1] != R.vertexDiagnostics.shader_salt() ||
      e.salt[2] != uint64_t(R.clipEmulation) || e.salt[3] != uint64_t(mods::cemu::has_shaders()))
    return kJ3dSettings;
  const uint32_t* w = e.words.data();
  for (const auto& [first, n] : j3d.ranges) {
    if (std::memcmp(r + first, w, n * 4)) return j3d_debug() ? (j3d_debug_registers(e, r), kJ3dRegisters) : kJ3dRegisters;
    w += n;
  }
  if (j3d_stencil_bits(r, 0) != w[0] || j3d_stencil_bits(r, 1) != w[1]) return kJ3dRegisters;
  for (size_t i = 0; i < e.texRegs.size() && !j3d_stale_test(); ++i)
    if (r[e.texRegs[i]] != e.texWords[i]) return j3d_debug() ? (j3d_debug_registers(e, r), kJ3dTextureWords) : kJ3dTextureWords;
  for (int s = 0; s < 2; ++s)
    for (uint32_t i = 0; i < c.stages[s].count; ++i)
      if (c.stages[s].textures[i].surface->artisticMips != e.artistic[s * LATTE_NUM_MAX_TEX_UNITS + i] ||
          c.stages[s].textures[i].surface->gpuWritten != e.gpuWritten[s * LATTE_NUM_MAX_TEX_UNITS + i])
        return kJ3dSurfaces;
  // the open pass: the state's own targets, or the colours of its pass subset
  const DrawTargets& t = c.targets;
  bool pass = R.rendering && R.passTracked && R.passDepth == t.depth && R.passDepthSlice == t.depthSlice &&
              R.passWidth == t.width && R.passHeight == t.height &&
              (R.passColors == t.colors || (c.subsetAbsent && R.passColors == c.subsetColors)) &&
              std::equal(std::begin(t.slices), std::end(t.slices), R.passSlices.begin());
  for (int i = 0; pass && i < 8; ++i)
    if (Surface* s = R.passColors[i]) pass = s->layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  if (pass && t.depth) pass = t.depth->layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  if (pass && t.target) pass = t.target->sx == t.sx && t.target->sy == t.sy;
  if (!pass) return kJ3dPass;
  uint64_t fsKey = 0;
  if (vk::get_fetch_shader(r, &fsKey, R.frame) != c.fs || fsKey != c.fsKey) return kJ3dFetch;
  if (e.checkedFrame != R.frame &&
      (vk::vertex_program_hash(r, R.frame) != e.vsHash || vk::pixel_program_hash(r, R.frame) != e.psHash))
    return kJ3dProgram;
  return kJ3dHits;
}
// after a full draw that resolved a continuable state (continued): file it under KEY
void j3d_store(uint64_t key, const ContinuedDraw& c, const uint32_t* r, uint32_t prim) {
  using namespace native_draw;
  if (!c.valid || !c.vs || !c.ps || !c.fs || !c.pipeline) { j3d_count(kJ3dRefusedState); return; }
  if (c.vs->kind != gfx::ProgramKind::Other || c.ps->kind != gfx::ProgramKind::Other) { j3d_count(kJ3dRefusedKind); return; }
  if ((r[REGADDR::VGT_GS_MODE] & 3) || (r[REGADDR::SQ_PGM_START_GS] && r[REGADDR::SQ_PGM_START_GS + 1])) {
    j3d_count(kJ3dRefusedGeometry);
    return;
  }
  for (int s = 0; s < 2; ++s)
    for (uint32_t i = 0; i < c.stages[s].count; ++i) {
      const Surface* t = c.stages[s].textures[i].surface;
      if (!t || !primary_surface(t) || R.surfaces.count(t->addr) != 1) {
        if (j3d_debug() && t) {
          static std::unordered_set<uint32_t> said;
          if (said.size() < 64 && said.insert(t->addr).second)
            LOG("[native-draw] j3d debug: texture %08X refused: %s, %zu surfaces at the address, %ux%u fmt %X mips %u%s",
                t->addr, primary_surface(t) ? "primary" : "not primary (mip chain / private copy)",
                size_t(R.surfaces.count(t->addr)), t->width, t->height, t->format, t->mips, t->gpuWritten ? ", GPU-written" : "");
        }
        j3d_count(kJ3dRefusedTexture);
        return;
      }
    }
  if (j3d.slots.empty()) j3d.slots.resize(j3d_slot_count());
  const auto& ranges = j3d_ranges();
  const uint32_t mask = uint32_t(j3d.slots.size() - 1);
  J3dEntry* slot = nullptr;
  for (uint32_t i = 0; i < kJ3dProbes; ++i) {
    J3dEntry& e = j3d.slots[(uint32_t(key) + i) & mask];
    if (e.key == key || !e.key) { slot = &e; break; }
    if (!slot || e.lastUsed < slot->lastUsed) slot = &e;
  }
  if (slot->key && slot->key != key) j3d_count(kJ3dEvicted);
  J3dEntry& e = *slot;
  e.key = key;
  e.lastUsed = R.frame;
  e.structGen = j3d_struct_gen();
  e.setGen = g_surface_set_gen;
  e.checkedFrame = R.frame;
  e.vsHash = vk::vertex_program_hash(r, R.frame);
  e.psHash = vk::pixel_program_hash(r, R.frame);
  e.salt[0] = R.vertexFormats.shader_salt();
  e.salt[1] = R.vertexDiagnostics.shader_salt();
  e.salt[2] = uint64_t(R.clipEmulation);
  e.salt[3] = uint64_t(mods::cemu::has_shaders());
  e.prim = prim;
  e.state = c;
  e.words.resize(j3d.rangeWords + 2);
  uint32_t* w = e.words.data();
  for (const auto& [first, n] : ranges) {
    std::memcpy(w, r + first, n * 4);
    w += n;
  }
  w[0] = j3d_stencil_bits(r, 0);
  w[1] = j3d_stencil_bits(r, 1);
  e.texRegs.clear();
  j3d_texture_words(c.vs, e.texRegs);
  j3d_texture_words(c.ps, e.texRegs);
  e.texWords.resize(e.texRegs.size());
  for (size_t i = 0; i < e.texRegs.size(); ++i) e.texWords[i] = r[e.texRegs[i]];
  for (int s = 0; s < 2; ++s)
    for (uint32_t i = 0; i < c.stages[s].count; ++i)
      e.artistic[s * LATTE_NUM_MAX_TEX_UNITS + i] = c.stages[s].textures[i].surface->artisticMips,
      e.gpuWritten[s * LATTE_NUM_MAX_TEX_UNITS + i] = c.stages[s].textures[i].surface->gpuWritten;
  j3d_count(kJ3dStored);
}
}  // namespace
void j3d_cache_clear() {
  for (auto& e : j3d.slots) e.key = 0;
}
#endif
void draw(const uint32_t *r, uint32_t prim, uint32_t count, uint32_t indexType,
          uint32_t indexAddr, uint32_t baseVertex, uint32_t instances, bool* viewIndependent) {
#ifdef WWHD_NATIVE_DRAW
  // J3D material cache: the key of this draw, named by OP_J3D_SHAPE right before it (never an inner replay's)
  const uint64_t j3dSiteKey = std::exchange(native_draw::g_j3dKey, 0);
  const uint64_t j3dKey = j3dSiteKey ? j3d_key(j3dSiteKey, r) : 0;
#endif
  const uint32_t dirtyGroups = gx2::take_dirty_groups();  // register groups changed since the previous draw
  if (__builtin_expect(fastpath_config().oracle, 0))
    for (auto& g : oracle.groups) g |= dirtyGroups;
  if (__builtin_expect(fastpath_config().partial, 0))
    for (auto& g : partialGroups) g |= dirtyGroups;
  auto& continued = continuedEyes[stereoEye];
  // the top-level stereo draw only, never the AO replay below
  StereoShaders* shadersOut = std::exchange(stereoShadersOut, nullptr);
  const StereoShaders* backdropReplay = std::exchange(backdropShadersIn,nullptr);
  const StereoShaders* shadersIn = backdropReplay ? backdropReplay : std::exchange(stereoShadersIn, nullptr);
  // Record exact upload slices/descriptor bindings while resolving the original
  // draw. The normal off path records nothing and the stereo snapshot is reused.
  if(!shadersOut && !stereoEye && !aoPrivateReplay && gamepad_backdrop_capture_active()) {
    backdropSnapshot.reset();shadersOut=&backdropSnapshot;
  }
  if(backdropReplay && (!backdropReplay->replay || backdropReplay->submission!=R.submissionGeneration))return;
  // eye 1: eye 0's draw, replayed (StereoShaders)
  const StereoShaders* replay = shadersIn && shadersIn->replay && !aoPrivateReplay &&
      shadersIn->submission == R.submissionGeneration ? shadersIn : nullptr;
#ifdef WWHD_NATIVE_DRAW
  // the packet capture of this draw (packet_draw), not of the AO / backdrop replays it may run itself (those come
  // after its own draw, which hands the capture back)
  PacketCapture* const cap = g_packetCapture;
  if (cap && (replay || shadersIn || shadersOut || aoPrivateReplay || stereoEye)) cap->refuse("replay");
#endif
  if (clouds_skip_card()) return;  // clouds.cpp: toon clouds replace the game's cloud cards
  if (!count || !instances || ((prim == 0x13 || prim == 0x14) && count < 4))
    return;
  if (r[REGADDR::PA_CL_CLIP_CNTL] & (1 << 22))
    return;
  ((uint32_t *)r)[REGADDR::VGT_PRIMITIVE_TYPE] = prim;
  // Graphics > Grass draw distance (render::grass_distance): drop (before any resolution) the tall grass patches whose
  // model translation (vertex uniform block 1, a 3x4 row-major matrix) lies farther than that from the drawn camera's
  // eye (interp::drawn_eye; horizontal). The grass is told by its vertex program's bytes (kGrassVs). A patch is 108
  // vertices; the same programs also draw the small palms and plants (216), which stay.
  const int grassDistance = render::grass_distance();
  if (grassDistance > 0 && uint64_t(count) * instances == 108) {
    // a few programs a frame: their answer is kept for the frame (program_hash revalidates once per frame)
    struct Seen { uint32_t addr = 0, size = 0; uint64_t frame = ~0ull; bool grass = false; };
    static Seen seen[8];
    const uint32_t vsAddr = r[mmSQ_PGM_START_VS] << 8, vsSize = r[mmSQ_PGM_START_VS + 1];
    Seen& s = seen[(vsAddr >> 8) & 7];
    if (s.frame != R.frame || s.addr != vsAddr || s.size != vsSize) {
      const uint64_t h = vk::vertex_program_hash(r, R.frame);
      s = {vsAddr, vsSize, R.frame, h && std::find(std::begin(kGrassVs), std::end(kGrassVs), h) != std::end(kGrassVs)};
    }
    float eye[3];
    const uint32_t addr = r[mmSQ_VTX_UNIFORM_BLOCK_START + 7], size = r[mmSQ_VTX_UNIFORM_BLOCK_START + 8] + 1;
    if (s.grass && addr && size >= 48 && interp::drawn_eye(eye)) {
      float m[12];
      std::memcpy(m, mem::ptr(addr), sizeof m);  // uploaded as is (uniform())
      const float dx = m[3] - eye[0], dz = m[11] - eye[2], d = float(grassDistance);
      if (dx * dx + dz * dz > d * d) {
        // the next draw continues from the previous drawn one: keep this draw's register changes
        gx2::g_dirty_groups |= dirtyGroups;
        return;
      }
    }
  }
  // continued-draw fast path (see fastpath_config); the AO replay draw stays outside of it
  const auto& fastPath = fastpath_config();
  // a replayed eye 1 draw records no continuation of its own
  const bool fastPathDraw = fastPath.on && !aoPrivateReplay && !replay && !gamepad_backdrop_replaying();
  if (replay) {
    continued.valid = false;
    continued.invalidReason = kNoState;
  }
  bool fast = false;
  bool registerBreak = false;  // resolved in full for changed registers
  bool partialCandidate = false;  // WWHD_VK_FASTPATH_PARTIAL: such a draw may reuse pieces of the state
  uint32_t oracleCombo = 0;    // WWHD_VK_FASTPATH_ORACLE: the breaking groups of such a draw
#ifdef WWHD_NATIVE_DRAW
  const bool j3dDraw = j3dKey && j3d_eligible(fastPathDraw, replay, shadersIn, shadersOut);
  bool j3dHit = false;
  if (j3dKey) native_draw::j3d_count(native_draw::kJ3dDraws);
  if (j3dKey && !j3dDraw) native_draw::j3d_count(native_draw::kJ3dIneligible);
#endif
  if (fastPathDraw) {
    if (fastPath.stats) report_continued_stats();
    ContinuedReason reason = continued_reason(r, prim);
#ifdef WWHD_NATIVE_DRAW
    // a J3D draw that does not continue the previous draw: the state its key resolved, if it still holds
    if (j3dDraw && reason == kContinued) native_draw::j3d_count(native_draw::kJ3dContinued);
    else if (j3dDraw) {
      if (J3dEntry* e = j3d_find(j3dKey)) {
        const native_draw::J3dCount why = j3d_check(*e, r, prim);
        if (why == native_draw::kJ3dHits) {
          continued = e->state;  // (as if resolved now: the full resolution gives the same)
          continued.valid = true;
          continued.stateVersion = gx2::g_full_state_version;
          continued.surfaceEpoch = g_surface_epoch;
          continued.frame = R.frame;
          partialGroups[stereoEye] = 0;
          e->checkedFrame = R.frame;
          e->lastUsed = R.frame;
          reason = continued_reason(r, prim);  // (a scissor that leaves other colour buffers active)
          j3dHit = reason == kContinued;
          if (!j3dHit) native_draw::j3d_count(native_draw::kJ3dScissor);
        } else native_draw::j3d_count(why);
      } else native_draw::j3d_count(native_draw::kJ3dNoEntry);
    }
#endif
    if (reason == kContinued) {
      // guest writes to the reused textures (write tracking, GX2Invalidate) upload here; an upload
      // can change lookups, so it ends the continuation
      for (const auto& stage : continued.stages)
        for (uint32_t i = 0; i < stage.count; ++i) upload_surface(stage.textures[i].surface);
      if (g_surface_epoch != continued.surfaceEpoch) reason = kUpload;
    }
#ifdef WWHD_NATIVE_DRAW
    if (j3dHit && reason != kContinued) {
      j3dHit = false;
      native_draw::j3d_count(native_draw::kJ3dUpload);
    }
    // WWHD_NATIVE_DRAW_VERIFY=N: every Nth hit also resolves in full and compares (a mismatch: the full draw)
    if (j3dHit && native_draw::verify_every() && ++j3d.verifyCounter % native_draw::verify_every() == 0) {
      native_draw::j3d_count(native_draw::kJ3dVerified);
      if (!verify_continued(r, prim)) {
        reason = kVerifyMismatch;
        j3dHit = false;
        native_draw::j3d_count(native_draw::kJ3dVerifyMismatch);
        char what[160];
        std::snprintf(what, sizeof what, "j3d: the cached state differs from the full resolution (frame %llu, draw %llu)",
                      (unsigned long long)R.frame, (unsigned long long)R.drawCount);
        native_draw::verify_mismatch(what);
      }
    }
    if (j3dHit) native_draw::j3d_count(native_draw::kJ3dHits);
#endif
    if (reason == kContinued && fastPath.verifyEvery) {
      static uint64_t verifyCounter = 0;
      if (++verifyCounter % fastPath.verifyEvery == 0) {
        ++continuedStats.verified;
        ++continuedStats.totalVerified;
        if (!verify_continued(r, prim)) reason = kVerifyMismatch;
      }
    }
    fast = reason == kContinued;
    registerBreak = reason == kRegisters;
    partialCandidate = fastPath.partial && registerBreak && partial_candidate(prim);
    continuedStats.partialCandidates += partialCandidate;
    if (fastPath.oracle) {
      // the state this draw could not continue, before the full path below replaces it
      if (registerBreak) {
        oracle.saved[stereoEye] = continued;
        oracleCombo = oracle.groups[stereoEye] & ~gx2::kSgContinuedMask;
      }
      oracle.groups[stereoEye] = 0;
    }
    if (fastPath.stats) {
      ++continuedStats.draws;
      ++continuedStats.reasons[reason];
      if (reason == kRegisters)
        for (int g = 0; g < gx2::kSgCount; ++g)
          if ((dirtyGroups & ~gx2::kSgContinuedMask) >> g & 1) ++continuedStats.groups[g];
    }
    // a full draw records a new state below; exceptions leave none
    continued.valid = false;
    continued.invalidReason = kNoState;
  }
  if (fastPath.profSplit && !aoPrivateReplay)  // the AO replay draw counts in its draw's class
    rprof::set_draw_class(fast ? rprof::kClassContinued : registerBreak ? rprof::kClassFullRegisters : rprof::kClassNone);
  uint64_t fsKey = 0;
  LatteFetchShader* fs;
  vk::Shader *vs, *ps;
  if (fast) {
    fs = continued.fs;
    fsKey = continued.fsKey;
    vs = continued.vs;
    ps = continued.ps;
  } else if (shadersIn) {
    fs = shadersIn->fs;
    fsKey = shadersIn->fsKey;
    vs = shadersIn->vs;
    ps = shadersIn->ps;
    static const bool verifyReplay=std::getenv("WWHD_VK_STEREO_REPLAY_VERIFY")!=nullptr;
    if(verifyReplay && !backdropReplay) {
      uint64_t checkedKey=0;
      auto* checkedFetch=vk::get_fetch_shader(r,&checkedKey,R.frame);
      vk::Shader* checkedVs=nullptr;
      vk::Shader* checkedPs=nullptr;
      if(checkedFetch)vk::translate_stages(r,checkedFetch,checkedKey,R.frame,g_shader_state_gen,checkedVs,checkedPs);
      if(fs!=checkedFetch||fsKey!=checkedKey||vs!=checkedVs||ps!=checkedPs)
        exception_report::raise("stereo shader reuse differs from independent second-eye resolution");
      static uint64_t verified=0;
      if(++verified%100000==0)LOG("[stereo verify] %llu second-eye shader resolutions matched",(unsigned long long)verified);
    }
  } else {
    fs = vk::get_fetch_shader(r, &fsKey, R.frame);
    if (!fs)
      exception_report::raise("missing Vulkan fetch shader");
    vk::translate_stages(r, fs, fsKey, R.frame, g_shader_state_gen, vs, ps);
  }
  // WWHD_VK_FASTPATH_PARTIAL: the state's own shaders (the state's fields stay as recorded until this
  // draw records its own); partialChanged: the breaking groups changed since it was recorded
  // Not while 60 fps (generated) adds the HUD coverage: it changes a draw's targets and pipeline (covSlot below).
  const bool partial = partialCandidate && fs == continued.fs && fsKey == continued.fsKey && vs == continued.vs &&
                       ps == continued.ps && vs && vs->ready() && ps && ps->ready() && !reproj::hud_coverage_wanted();
  const uint32_t partialChanged = partial ? partialGroups[stereoEye] & ~gx2::kSgContinuedMask : 0;
  rprof::mark(rprof::kShader);
  if (shadersOut && vs && vs->ready() && ps && ps->ready()) {
    shadersOut->fs = fs;
    shadersOut->fsKey = fsKey;
    shadersOut->vs = vs;
    shadersOut->ps = ps;
  }
  if (!vs || !vs->ready() || !ps || !ps->ready())
    exception_report::raise("Vulkan shader translation failed: " +
                             (vs && !vs->ready() ? vs->error
                              : ps               ? ps->error
                                                 : "missing shader"));
  // Both eyes use the same sun-cascade atlas, rendered from the light's view.
  // All camera-space passes, including the shadow mask, still draw twice.
  if(viewIndependent)
    *viewIndependent = sun_atlas_target(r) &&
      !LatteMRT::GetActiveColorBufferMask(ps->dec,*reinterpret_cast<const LatteContextRegister*>(r));
  const bool stripRestart = indexAddr &&
      (prim == 3 || prim == 6) &&
      (r[REGADDR::VGT_MULTI_PRIM_IB_RESET_EN] & 1);
  const uint32_t restartIndex = r[REGADDR::VGT_MULTI_PRIM_IB_RESET_INDX];
  // Native guest index bytes need no widening or temporary vector. Strip
  // pipelines always enable restart for portability, so 16-bit 0xffff is only
  // safe when it is also the guest's enabled marker. Preserve other markers
  // through the existing uint32 normalization path.
  const bool nativeIndices = indexAddr && (indexType == 0 || indexType == 1) &&
      (prim == 1 || prim == 2 || prim == 3 || prim == 4 || prim == 6) &&
      ((prim != 3 && prim != 6) ||
       (indexType == 0 ? stripRestart && restartIndex == UINT16_MAX
                       : !stripRestart || restartIndex == UINT32_MAX));
  // Draw uploads copy the converted bytes before the optional AO replay calls
  // draw again. Retain CPU capacity; queued GPU work owns separate arena slices.
  static thread_local std::vector<uint32_t> indices;
  indices.clear();
  // Conversion emits a known number of indices. Allocate once rather than
  // repeatedly growing and copying the vector for every indexed draw.
  size_t convertedCount = indexAddr ? size_t(count) : 0;
  switch (prim) {
  case 5: convertedCount = count > 2 ? size_t(count - 2) * 3 : 0; break;
  case 0x13: convertedCount = size_t(count / 4) * 6; break;
  case 0x14: convertedCount = count >= 4 ? size_t((count - 2) / 2) * 6 : 0; break;
  case 0x12: convertedCount = size_t(count) + 1; break;
  default: break;
  }
  static const bool specializeIndices = [] {
    const char* e = std::getenv("WWHD_VK_SPECIALIZE_INDICES");
    return e && !std::strcmp(e, "1");
  }();
  // ld16/ld32 use uint32 guest-EA arithmetic. A wrapped BE range stays on
  // that original path instead of replacing it with linear host addressing.
  const uint64_t guestReads = prim == 0x12 ? std::max(count, 1u) : count;
  const uint64_t guestBytes = guestReads * (indexType == 4 ? 2 : 4);
  const bool guestWrap = indexAddr && (indexType == 4 || indexType == 9) &&
      uint64_t(indexAddr) + guestBytes > 0x100000000ull;
  VkPrimitiveTopology topology;
  // Converted (big-endian, fan, quad, loop, other restart marker) index data from the buffer cache:
  // keyed by everything the conversion reads; a hit skips the conversion.
  bufcache::Entry* convertedEntry = nullptr;
  bool convertedHit = false;
  bufcache::Key convertedKey{};
  if (replay) {
    topology = replay->topology;  // eye 0 prepared the indices
  } else {
  if (indexAddr && !nativeIndices && !guestWrap && buffer_cache_enabled() &&
      guestBytes <= (8u << 20) && uint64_t(indexAddr) + guestBytes <= 0x100000000ull) {
    convertedKey = {indexAddr, bufcache::kIndexConverted,
        uint64_t(count) | uint64_t(prim & 0xff) << 32 | uint64_t(indexType & 0xff) << 40 |
            uint64_t(stripRestart) << 48 | uint64_t(specializeIndices) << 49,
        stripRestart ? restartIndex : 0};
    switch (buffer_cache().lookup(convertedKey, uint32_t(guestBytes), convertedEntry)) {
    case bufcache::kHit: convertedHit = true; break;
    case bufcache::kMiss: break;  // armed: convert below, then upload
    case bufcache::kBypass: convertedEntry = nullptr; break;
    }
  }
  if (convertedHit && !buffer_cache_verify()) {
    topology = primitive_topology(prim);
  } else if (specializeIndices && !nativeIndices && !guestWrap) {
    switch (prim) {
    case 1: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
    case 2: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
    case 3: case 0x12: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
    case 4: case 5: case 0x13: case 0x14:
      topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
    case 6: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
    default: exception_report::raise("unsupported Vulkan primitive");
    }
    vk::convert_indices(indexAddr ? mem::ptr(indexAddr) : nullptr, prim, count,
                        indexType, stripRestart, restartIndex, indices);
  } else {
  if (!nativeIndices) indices.reserve(convertedCount);
  auto idx = [&](uint32_t i) {
    if (!indexAddr)
      return i;
    uint32_t value;
    switch (indexType) {
    case 0:
      value = ((uint16_t *)mem::ptr(indexAddr))[i];
      break;
    case 1:
      value = ((uint32_t *)mem::ptr(indexAddr))[i];
      break;
    case 4:
      value = ld16(indexAddr + i * 2);
      break;
    case 9:
      value = ld32(indexAddr + i * 4);
      break;
    default:
      exception_report::raise("unsupported index type");
    }
    // All host index buffers use uint32, whose native restart marker differs
    // from the guest's configurable marker (including 16-bit 0xffff).
    return stripRestart && value == restartIndex ? UINT32_MAX : value;
  };
  switch (prim) {
  case 1:
    topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    break;
  case 2:
    topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    break;
  case 3:
    topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    break;
  case 4:
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    break;
  case 5:
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    for (uint32_t i = 1; i + 1 < count; i++)
      indices.insert(indices.end(), {idx(0), idx(i), idx(i + 1)});
    break;
  case 6:
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    break;
  case 0x13:
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    for (uint32_t i = 0; i + 3 < count; i += 4)
      indices.insert(indices.end(), {idx(i), idx(i + 1), idx(i + 2), idx(i),
                                     idx(i + 2), idx(i + 3)});
    break;
  case 0x14:
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    for (uint32_t i = 0; i + 3 < count; i += 2)
      indices.insert(indices.end(), {idx(i), idx(i + 1), idx(i + 2), idx(i + 1),
                                     idx(i + 3), idx(i + 2)});
    break;
  case 0x12:
    topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    for (uint32_t i = 0; i < count; i++)
      indices.push_back(idx(i));
    indices.push_back(idx(0));
    break;
  default:
    exception_report::raise("unsupported Vulkan primitive");
  }
  if (!nativeIndices && indices.empty() && indexAddr) {
    indices.resize(count);
    for (uint32_t i = 0; i < count; i++)
      indices[i] = idx(i);
  }
  }
  if (convertedEntry) {
    const bool hostStrip = topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
                           topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (!convertedHit) {
      if (indices.empty() ||
          !buffer_cache().upload(*convertedEntry, indices.data(), uint32_t(indices.size() * 4))) {
        convertedEntry = nullptr;
      } else {
        rprof::add_upload(rprof::kUpIndex, indices.size() * 4);
        cached_index_extent(*convertedEntry, indices.data(), indices.size(), 4, hostStrip, 0);
      }
    } else if (buffer_cache_verify()) {
      uint32_t diff = 0;
      if (convertedEntry->outSize != indices.size() * 4 ||
          !buffer_cache().verify(*convertedEntry, indices.data(), uint32_t(indices.size() * 4), &diff))
        buffer_cache_mismatch("converted indices", *convertedEntry, uint32_t(indices.size() * 4), diff);
    }
  }
  }
  rprof::mark(rprof::kIndices);
  DrawTargets targets;
  // partial: the state's targets while their registers did not change and the scissor leaves the
  // same colour buffers active (continued_reason)
  const bool partialTargets = partial && !(partialChanged & gx2::kSgPartialTargetsMask) &&
      (r[REGADDR::PA_SC_GENERIC_SCISSOR_BR] == continued.scissorBR ||
       LatteMRT::GetActiveColorBufferMask(ps->dec, *reinterpret_cast<const LatteContextRegister*>(r)) ==
           continued.targets.activeColors);
  if(backdropReplay) targets=backdropReplay->targets;
  else if (fast || partialTargets) targets = continued.targets;
  else if (!(replay ? resolve_replay_targets(r, ps, targets) : PASS_TRUSTED_TARGETS(r, ps, targets)))
    return;
  if (auto* backdrop = gamepad_backdrop_replay_target()) {
    targets.colors[0] = backdrop;
    targets.slices[0] = 0;
    // A capture smaller than the pane's target (capped at about 1920x1080): the
    // same draw, scaled. Viewport and scissor follow from sx/sy (the original's
    // raster state is not reused at another scale).
    backdrop_scale_targets(targets, backdrop->extent.width, backdrop->extent.height);
  }
  auto& colors = targets.colors;
  auto& slices = targets.slices;
  Surface* depth = targets.depth;
  const uint32_t depthSlice = targets.depthSlice, width = targets.width, height = targets.height;
  const float sx = targets.sx, sy = targets.sy;
  clouds_before_draw(r, ps, colors[0], slices[0], depth);  // clouds.cpp: cloud shadows
  rprof::mark(rprof::kTargets);
  // 60 fps (generated): a HUD draw into the TV picture writes its coverage too (only while it runs;
  // a continued draw keeps the targets and pipeline it was resolved with). Decided on the draw's own
  // colours, before the pass subset (which is then not taken).
  uint32_t covSlot = 0;
  if (!fast && !replay && !aoPrivateReplay && !backdropReplay && reproj::hud_coverage_wanted())
    if (Surface* cov = reproj::hud_coverage_target(colors[0])) {
      bool alone = true;
      for (int i = 1; i < 8; i++) alone = alone && !colors[i];
      uint32_t location = 0;
      if (alone && colors[0]->fmt.kind == FormatInfo::FLOAT && cov->extent.width == width && cov->extent.height == height &&
          coverage_words(ps, location) && location > 0 && location < 8) {
        colors[location] = cov;
        slices[location] = 0;
        covSlot = location;
      } else {
        reproj::hud_coverage_failed();  // this frame's in-between one: the HUD found by comparing pictures
      }
    }
  // partial: the same pipeline key (same shaders, topology and targets, no key register changed); the
  // draw's own targets, before a pass subset takes over the open pass's (the state keeps its own too)
  const bool partialSameTargets = partial && targets.same(continued.targets);
  const bool partialPipeline = partialSameTargets && topology == continued.topology &&
                               !(partialChanged & gx2::kSgPartialPipelineMask);
  Pipeline* pp = fast || partialPipeline ? continued.ownPipeline
      : !backdropReplay && replay && replay->formats == target_formats(colors, depth) ? replay->pipeline
      : &pipeline(r, vs, ps, fs, topology, colors, depth, {.covSlot = covSlot});
  if (covSlot) {  // its coverage variant still compiling (or failed): drawn as usual, the frame generated the old way
    if (pp->pendingCompile) resolve_pending_pipeline(*pp, false);
    if (!pp->pipeline || pp->pendingCompile) {
      colors[covSlot] = nullptr;
      covSlot = 0;
      reproj::hud_coverage_failed();
      pp = &pipeline(r, vs, ps, fs, topology, colors, depth);
    }
  }
  // the draw's own pipeline, exactly as without the pass subset
  auto &own = *pp;
  // Pass subset (take_pass_subset): colour slots taken over from the open pass, written by nothing. Only
  // when the draw's own pipeline is ready (so falling back to it later never waits) and its masked
  // variant is ready too: that variant is only ever compiled in the background lane (never on the render
  // thread, never recorded as a recipe), and until it is ready the draw keeps its own pass as before.
  // Not while the GamePad backdrop captures or replays (its snapshot keeps the draw's own targets), for
  // a HUD coverage draw (60 fps (generated), decided on the draw's own colours above), nor with stereo,
  // eye 1's replay, the AO replay, or without the async compiler.
  uint32_t subsetAbsent = 0;
  std::array<Surface*, 8> ownColors{};  // the draw's own colour targets, when it took over others
  std::array<uint32_t, 8> ownSlices{};
  Pipeline* subsetPipeline = nullptr;
  if (pass_reuse::subset_on() && R.rendering && R.passTracked && !aoPrivateReplay && !replay && !backdropReplay &&
      !covSlot && stereo::frame.mode == stereo::Off && own.pipeline && !own.pendingCompile && async_pipeline_enabled() &&
      !gamepad_backdrop_replaying() && !gamepad_backdrop_capture_active()) {
    ownColors = colors;
    ownSlices = slices;
    subsetAbsent = take_pass_subset(r, vs, ps, targets);
    // toon clouds / horizon haze drawn into this pass next must not need a pipeline for its wider target set
    if (subsetAbsent && !clouds_pass_subset_ok(colors, depth)) {
      colors = ownColors;
      slices = ownSlices;
      subsetAbsent = 0;
    }
    if (subsetAbsent) {
      subsetPipeline = (fast || partialPipeline) && subsetAbsent == continued.subsetAbsent && continued.subsetColors == colors
          ? continued.pipeline : masked_variant(own, r, vs, ps, fs, topology, colors, depth, subsetAbsent);
      if (!subsetPipeline || !subsetPipeline->pipeline) {
        // its variant isn't ready (compiling, not admitted, or failed): drawn as before, in its own pass
        subsetPipeline = nullptr;
        colors = ownColors;
        slices = ownSlices;
        subsetAbsent = 0;
      }
    }
  }
  const uint32_t subsetClear = pass_reuse::clear_mask(subsetAbsent);
  auto &p = subsetPipeline ? *subsetPipeline : own;
#ifdef WWHD_NATIVE_DRAW
  if (cap) {
    cap->vs = vs;
    cap->ps = ps;
    cap->fs = fs;
    cap->topology = topology;
    cap->pipeColors = colors;
    cap->pipeDepth = depth;
    cap->clearMask = subsetClear;
    cap->subset = subsetPipeline != nullptr;
  }
#endif
  rprof::mark(rprof::kPipeline);
  if (!p.pipeline && !p.pendingCompile) return;
  // Shader compile Fast (background_shaders.h): a draw that may be left out only waits the frame's
  // budget and is left out of this frame if its pipeline is not ready by then
  const bool trackTargets = bgshaders::enabled() && !aoPrivateReplay && !gamepad_backdrop_replaying();
  if (p.pendingCompile && trackTargets && !background_wait(r, p, colors, depth, subsetClear)) return;
  if (p.pendingCompile && !bgshaders::enabled()) bgshaders::exact(bgshaders::kOff);  // (the AO pass's private
                                                                                      // copy of a draw waits)
  // A capture is published only after its private pipeline is actually ready.
  // Deferred command counts cannot certify a future driver compilation success.
  // Never wait for it on the render thread (the phone's bottleneck): poll, and
  // while it compiles in the background this pane is not captured (edge-colour
  // fallback); a later presentation captures it. Its recipe joins the pipeline
  // database like any other, so later sessions start with it warmed.
  if (gamepad_backdrop_replaying() && p.pendingCompile) {
    resolve_pending_pipeline(p, false);
    if (p.pendingCompile) { gamepad_backdrop_debug("pipeline-pending", R.frame); return; }
  }
  if (p.pendingCompile && !begin_exact_preparation()) resolve_pending_pipeline(p);
  if (trackTargets) track_drawn(r, colors, depth, subsetClear);
  if (!p.pipeline && !p.pendingCompile) return; // terminal driver failure, not pending
  if(feedback_stats_enabled()) report_feedback_stats();
  if(vertex_window_stats_enabled()) report_vertex_window_stats();
  FeedbackStatsProbe feedbackProbe;
  auto* probe=feedback_stats_enabled()?&feedbackProbe:nullptr;
  const float cameraEye = stereo::frame.mode != stereo::Off && !stereo::layoutDrawDepth && stereo_screen_target(targets.target->width,targets.target->height)
      ? (stereoEye ? 1.0f : -1.0f)*stereo::frame.depth*stereo::eyeDistanceAtUnitDepth : 0.0f;
  const bool sunShadow = ps->kind == gfx::ProgramKind::SunShadowMask;
  const bool recordState = fastPathDraw && !fast;
  if (recordState && !partial)
    for (auto& stage : continued.stages) stage = {};
  // partial: a stage keeps the state's texture/sampler bindings while the targets are the same and no
  // texture or sampler register changed. As on a continued draw, its textures' guest writes upload
  // first; an upload (of these or of the vertex stage's resolution before) ends the surface epoch
  // the bindings were resolved in, and the stage resolves in full. Each stage decides right before
  // it binds, so the pixel stage sees what the vertex stage uploaded, in the full path's order.
  const bool partialTextures = partialSameTargets && !(partialChanged & gx2::kSgPartialTexturesMask);
  std::array<bool, 2> partialStage{};
  auto partial_stage = [&](int s) {
    if (!partial) return;
    auto& stage = continued.stages[s];
    if (partialTextures) {
      for (uint32_t i = 0; i < stage.count; ++i) upload_surface(stage.textures[i].surface);
      partialStage[s] = g_surface_epoch == continued.surfaceEpoch;
    }
    if (!partialStage[s]) stage = {};
  };
  partial_stage(0);
  // eye 1 replays a stage of eye 0's draw at the same scale through the same pipeline
  const bool replayStages = replay && &p == replay->pipeline && replay->sx == sx && replay->sy == sy;
  StageResources vres, pres;
  g_artisticBias = ::gfx::artistic_mips::resolution_bias(sx, sy);
  if(backdropReplay) {
    // Identical shaders and dynamic-uniform mode give identically defined
    // descriptor-set layouts, even though the private alpha pipeline differs.
    // Bind the already resolved sets/offsets, never replay_stage/bind_stage.
    if(!backdrop_reuse_resources(*backdropReplay,p,vs,ps,R.submissionGeneration,vres,pres))return;
  } else {
  if (!replayStages || !replay->stages[0].valid ||
      !replay_stage(replay->stages[0], r, vs, cameraEye, colors, depth, vres))
    vres = bind_stage(r, vs, p.sets[0], p.dynamicUniforms, sx, sy, colors, depth, probe, cameraEye, sunShadow,
                      fast || partialStage[0] ? &continued.stages[0] : nullptr,
                      recordState && !partialStage[0] ? &continued.stages[0] : nullptr,
                      shadersOut ? &shadersOut->stages[0] : nullptr);
  partial_stage(1);
  if (!replayStages || !replay->stages[1].valid ||
      !replay_stage(replay->stages[1], r, ps, sunShadow ? cameraEye : 0, colors, depth, pres))
    pres = bind_stage(r, ps, p.sets[1], p.dynamicUniforms, sx, sy, colors, depth, probe,
                      sunShadow ? cameraEye : 0, sunShadow,
                      fast || partialStage[1] ? &continued.stages[1] : nullptr,
                      recordState && !partialStage[1] ? &continued.stages[1] : nullptr,
                      shadersOut ? &shadersOut->stages[1] : nullptr);
  }
  if (shadersOut) {
    shadersOut->targets=targets;
    shadersOut->stages[0].resources = vres;
    shadersOut->stages[1].resources = pres;
    shadersOut->submission = R.submissionGeneration;
    shadersOut->pipeline = &p;
    shadersOut->topology = topology;
    shadersOut->formats = target_formats(colors, depth);
    shadersOut->sx = sx;
    shadersOut->sy = sy;
    shadersOut->width = width;
    shadersOut->height = height;
    shadersOut->vertexBindCount = 0;
  }
  if (fastPathDraw) {
    // the resolved state of this draw: continued by the next draws while nothing breaking changes
    auto& c = continued;
    c.valid = fast || (c.stages[0].continuable && c.stages[1].continuable);
    c.invalidReason = kNotContinuable;
    if (!fast) {
      c.stateVersion = gx2::g_full_state_version;
      c.surfaceEpoch = g_surface_epoch;
      c.frame = R.frame;
      c.prim = prim;
      c.topology = topology;
      c.aoMode = ao_mode();
      c.aoHires = ao_hires_enabled();
      c.aniso = aniso_enabled();
      c.fs = fs;
      c.fsKey = fsKey;
      c.vs = vs;
      c.ps = ps;
      c.targets = targets;
      c.scissorBR = r[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
      c.pipeline = &p;
      c.ownPipeline = &own;
      c.subsetAbsent = subsetAbsent;
      c.subsetColors = colors;
      if (subsetAbsent) {  // the continued state keeps the draw's own targets (verify_continued)
        c.targets.colors = ownColors;
        c.targets.slices = ownSlices;
      }
      partialGroups[stereoEye] = 0;  // the groups changed since this state
      if (partial) {
        auto& st = continuedStats;
        ++st.partial;
        st.partialTargets += partialTargets;
        st.partialPipeline += partialPipeline;
        st.partialStages[0] += partialStage[0];
        st.partialStages[1] += partialStage[1];
        // WWHD_VK_FASTPATH_VERIFY: the full resolution against everything this draw recorded,
        // reused or resolved (the texture lists only of a state that can continue: a stage that cannot
        // was resolved in full, but the targets and pipeline it was drawn with may have been reused)
        static uint64_t partialVerifyCounter = 0;
        if (fastPath.verifyEvery && ++partialVerifyCounter % fastPath.verifyEvery == 0) {
          ++st.verified;
          ++st.totalVerified;
          if (!verify_continued(r, prim, true, c.valid)) {
            c.valid = false;
            c.invalidReason = kVerifyMismatch;
          }
        }
      }
      if (fastPath.oracle) {
        OracleKey key;
        oracle_ps_key(r, key);
        if (registerBreak && !partial) oracle_count(oracleCombo, oracle.saved[stereoEye], c, oracle.psKey[stereoEye], key);
        oracle.psKey[stereoEye] = key;
      }
    }
  }
  // Texture uploads, feedback copies, and sampling transitions above may have
  // ended the previous pass. Reuse only its exact active attachment set.
  bool reusePass = R.rendering && R.passTracked && R.passColors == colors &&
                   std::equal(std::begin(slices), std::end(slices),
                              R.passSlices.begin()) &&
                   R.passDepth == depth && R.passDepthSlice == depthSlice &&
                   R.passWidth == width && R.passHeight == height;
  for (auto *color : colors)
    if (color && color->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      reusePass = false;
  if (depth && depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    reusePass = false;
  // pass subset: the textures bound above ended the pass anyway, so the draw begins a pass of its own
  // targets with its own pipeline (ready: checked before the subset was taken), exactly as without the
  // subset. Its descriptor sets were made with the variant's set layouts, which are identically defined
  // (the same shaders and uniform mode), so they suit its own pipeline layout.
  if (subsetAbsent && !reusePass) {
    colors = ownColors;
    slices = ownSlices;
    subsetAbsent = 0;
    if (fastPathDraw && !fast) {
      continued.pipeline = &own;
      continued.subsetAbsent = 0;
      continued.subsetColors = ownColors;
    }
  } else if (subsetAbsent) {
    ++R.passSubsetKept;
  }
  Pipeline& dp = subsetAbsent ? p : own;  // the pipeline the draw is recorded with
  struct DrawStateCache {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_MAX_ENUM;
    DescriptorBindProbe descriptorProbe;
    struct VertexBind { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceSize offset=0; bool valid=false; };
    std::array<VertexBind,16> vertexBinds{};
    VkCommandBuffer vertexCommand=VK_NULL_HANDLE;
    uint64_t vertexGeneration=0;
    bool vertex_bind_matches(VkCommandBuffer command,uint64_t generation,
                             uint32_t binding,VkBuffer buffer,VkDeviceSize offset) {
      if(vertexCommand!=command || vertexGeneration!=generation) {
        vertexBinds={};vertexCommand=command;vertexGeneration=generation;
      }
      if(binding>=vertexBinds.size()) return false;
      auto& old=vertexBinds[binding];
      const bool match=old.valid && old.buffer==buffer && old.offset==offset;
      old={buffer,offset,true};
      return match;
    }
    VkViewport viewport{};
    VkRect2D scissor{};
    std::array<uint32_t, 4> blend{};
    uint32_t stencilFront = 0, stencilBack = 0;
    bool viewportValid = false, scissorValid = false;
    bool blendValid = false, stencilValid = false;
  };
  // per eye: while stereo is on, each eye's pass stays open in its own command stream
  static std::array<DrawStateCache, 2> states;
  auto& state = states[stereoEye];
  // A tracked active pass can only contain our draw commands. Every pass end,
  // including submit/fence/reset, makes reusePass false on the next draw.
  // Reset conservatively on a new pass so external command recording cannot
  // leave this cache claiming dynamic state that was never set in this buffer.
  if (!reusePass) state = {};
#ifdef WWHD_NATIVE_DRAW
  if (g_packetBindsDirty) {  // packet replays bound descriptor sets (and pipelines, vertex and index buffers) since
    state.descriptorProbe = {};
    state.pipeline = VK_NULL_HANDLE;
    state.indexBuffer = VK_NULL_HANDLE;
    state.indexType = VK_INDEX_TYPE_MAX_ENUM;
    state.vertexBinds = {};
    g_packetBindsDirty = false;
  }
#endif
  auto cmd = command_buffer();
  auto bindIndex = [&](VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
    if (state.indexBuffer == buffer && state.indexOffset == offset && state.indexType == type) return;
    record::vkCmdBindIndexBuffer(cmd, buffer, offset, type);
    state.indexBuffer = buffer; state.indexOffset = offset; state.indexType = type;
  };
  if (!reusePass) {
    end_encoder();
    std::array<VkRenderingAttachmentInfo, 8> attachments{};
    uint32_t ncolor = 0;
    for (int i = 0; i < 8; i++) {
      attachments[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      attachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      if (colors[i]) {
        upload_surface(colors[i]);
        transition_image(colors[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        attachments[i].imageView = layer_view(colors[i], slices[i]);
        ncolor = i + 1;
      }
    }
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depth) {
      upload_surface(depth);
      transition_image(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
      da.imageView = layer_view(depth, depthSlice);
      da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = {width, height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = ncolor;
    ri.pColorAttachments = attachments.data();
    ri.pDepthAttachment = depth ? &da : nullptr;
    ri.pStencilAttachment = depth && depth->fmt.stencil ? &da : nullptr;
    // textures this pass read last time it ended for (sample_transition): ready before it begins
    if (pass_reuse::prefetch_on() && stereo::frame.mode == stereo::Off) prefetch_pass_reads(colors, depth, width, height);
    if(R.gpuPassTimestampsEnabled) gpu_begin_render_scope(colors,depth,slices.data(),depthSlice,width,height);
    trace_pass(colors, ncolor, depth, depthSlice, width, height);
    begin_pass(cmd, ri, [&] {
      PassBegin pass; pass.formats.colorCount = ncolor;
      for (uint32_t i = 0; i < ncolor; ++i) if (colors[i]) {
        pass.formats.colors[i] = colors[i]->fmt.pixel; pass.layouts[i] = colors[i]->layout;
      }
      if (depth) {
        pass.formats.depth = depth->fmt.pixel; pass.formats.stencil = depth->fmt.stencil;
        pass.layouts[8] = depth->layout;
      }
      return pass;
    });
    ++R.renderPassCount;
    R.rendering = true;
    R.passTracked = true;
    R.passColors = colors;
    std::copy(std::begin(slices), std::end(slices), R.passSlices.begin());
    R.passDepth = depth;
    R.passDepthSlice = depthSlice;
    R.passWidth = width;
    R.passHeight = height;
  }
  rprof::mark(rprof::kPass);
  if (!dp.pipeline && !dp.pendingCompile) return;
  const bool deferredPipelineDraw = bool(dp.pendingCompile);
  if (dp.pendingCompile) {
    auto* ownedPipeline = &dp;
    record::emit_owned([ownedPipeline,cmd]() {
      resolve_pending_pipeline(*ownedPipeline);
      if(ownedPipeline->pipeline)
        record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ownedPipeline->pipeline);
    });
    state.pipeline = VK_NULL_HANDLE;
  } else if (state.pipeline != dp.pipeline) {
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dp.pipeline);
    state.pipeline = dp.pipeline;
  }
  VkDescriptorSet sets[] = {vres.set, pres.set};
  std::array<uint32_t, 34> dynamicOffsets{};
  std::copy_n(vres.dynamicOffsets.begin(), vres.dynamicOffsetCount, dynamicOffsets.begin());
  std::copy_n(pres.dynamicOffsets.begin(), pres.dynamicOffsetCount,
              dynamicOffsets.begin() + vres.dynamicOffsetCount);
  static const bool skipRedundantBinds = [] {
    const char* e=std::getenv("WWHD_VK_SKIP_REDUNDANT_BINDS");
    return e && !std::strcmp(e,"1");
  }();
  DescriptorBindProbe::Matches matches;
  const bool preparationStats=preparation_stats_enabled();
  if(skipRedundantBinds || preparationStats)
    matches=state.descriptorProbe.observe(cmd,R.submissionGeneration,dp.layout,sets,
        dynamicOffsets.data(),vres.dynamicOffsetCount,pres.dynamicOffsetCount);
  const auto bind=DescriptorBindProbe::bind_range(matches,skipRedundantBinds,
      vres.dynamicOffsetCount,pres.dynamicOffsetCount);
  if(preparationStats) {
    ++R.cpuPreparation.descriptorBindCandidates;
    R.cpuPreparation.descriptorWholeMatches+=matches.whole;
    for(size_t stage=0;stage<2;++stage)R.cpuPreparation.descriptorStageMatches[stage]+=matches.stage[stage];
    R.cpuPreparation.descriptorBindCalls+=bind.count!=0;
    R.cpuPreparation.descriptorSkippedSets+=2-bind.count;
  }
  if(bind.count)
    record::vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,dp.layout,
        bind.first,bind.count,sets+bind.first,bind.offsetCount,
        dynamicOffsets.data()+bind.offsetBegin);
#ifdef WWHD_NATIVE_DRAW
  if (cap) {
    cap->pipeline = &dp;
    cap->vs = vs;
    cap->vkPipeline = dp.pipeline;
    cap->layout = dp.layout;
    cap->sets[0] = sets[0];
    cap->sets[1] = sets[1];
    cap->offsets = dynamicOffsets;
    cap->offsetCount = vres.dynamicOffsetCount + pres.dynamicOffsetCount;
    for (auto& blk : cap->stage[1].block)  // (one array, vertex set first)
      if (blk.index >= 0) blk.index += int(vres.dynamicOffsetCount);
    if (cap->stage[1].supIndex >= 0) cap->stage[1].supIndex += int(vres.dynamicOffsetCount);
    cap->colors = colors;
    cap->depth = depth;
    if (deferredPipelineDraw) cap->refuse("pipeline compiling");
    if (covSlot) cap->refuse("HUD coverage");
  }
#endif
  VkViewport vp;
  VkRect2D sc;
  const bool reuseRaster = replay && replay->rasterReady && replay->sx == sx && replay->sy == sy &&
      replay->width == width && replay->height == height;
  if (reuseRaster) {
    vp = replay->viewport;
    sc = replay->scissor;
  } else {
  float xs = f32(r[REGADDR::PA_CL_VPORT_XSCALE]),
        xo = f32(r[REGADDR::PA_CL_VPORT_XOFFSET]),
        ys = f32(r[REGADDR::PA_CL_VPORT_YSCALE]),
        yo = f32(r[REGADDR::PA_CL_VPORT_YOFFSET]),
        zs = f32(r[REGADDR::PA_CL_VPORT_ZSCALE]),
        zo = f32(r[REGADDR::PA_CL_VPORT_ZOFFSET]);
  LATTE_PA_CL_CLIP_CNTL clip;
  std::memcpy(&clip, r + REGADDR::PA_CL_CLIP_CNTL, 4);
  vp = {(xo - xs) * sx,
                (yo - ys) * sy,
                xs * 2 * sx,
                ys * 2 * sy,
                clip.get_DX_CLIP_SPACE_DEF() ? zo : zo - zs,
                zo + zs};
  uint32_t tl = r[REGADDR::PA_SC_GENERIC_SCISSOR_TL],
           br = r[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
  // As the Metal renderer: the scaled edges are rounded with a tolerance, not truncated. 720 * (1200/720)
  // is just below 1200 in float (16:10 at 1.5x and 3x), which left the bottom row of every full-screen
  // pass undrawn, and its stale contents spread through the shadow mask's softening.
  auto lo = [](uint32_t v, float k, uint32_t limit) {
    return uint32_t(std::clamp(std::floor(double(v) * k + 0.01), 0.0, double(limit)));
  };
  auto hi = [](uint32_t v, float k, uint32_t limit) {
    return uint32_t(std::clamp(std::ceil(double(v) * k - 0.01), 0.0, double(limit)));
  };
  uint32_t x = lo(tl & 0x7fff, sx, width),
           y = lo((tl >> 16) & 0x7fff, sy, height),
           ex = hi(br & 0x7fff, sx, width),
           ey = hi((br >> 16) & 0x7fff, sy, height);
  float ax, ay;
  if (aspect::content_clip() && target_aspect_factors(r[mmCB_COLOR0_TILE] & 0xFFFF, r[mmCB_COLOR0_FRAG], ax, ay))
    aspect::panes::clip(width, height, ax, ay, x, y, ex, ey);
  if (ex <= x || ey <= y) {
    end_encoder();
    return;
  }
  sc = {{int32_t(x), int32_t(y)}, {ex - x, ey - y}};
  }
  if (shadersOut) {
    shadersOut->viewport = vp;
    shadersOut->scissor = sc;
    shadersOut->rasterReady = true;
  }
  // Bitwise comparison preserves distinct signed zeros and exact float state.
  if (!state.viewportValid || std::memcmp(&state.viewport, &vp, sizeof(vp))) {
    record::vkCmdSetViewport(cmd, 0, 1, &vp);
    state.viewport = vp;
    state.viewportValid = true;
  }
  if (!state.scissorValid || state.scissor.offset.x != sc.offset.x ||
      state.scissor.offset.y != sc.offset.y ||
      state.scissor.extent.width != sc.extent.width ||
      state.scissor.extent.height != sc.extent.height) {
    record::vkCmdSetScissor(cmd, 0, 1, &sc);
    state.scissor = sc;
    state.scissorValid = true;
  }
  std::array<uint32_t, 4> blendBits;
  std::copy_n(r + REGADDR::CB_BLEND_RED, blendBits.size(), blendBits.begin());
  if (!state.blendValid || state.blend != blendBits) {
    record::vkCmdSetBlendConstants(
        cmd, reinterpret_cast<const float *>(r + REGADDR::CB_BLEND_RED));
    state.blend = blendBits;
    state.blendValid = true;
  }
  uint32_t stencilFront = r[REGADDR::DB_STENCILREFMASK] & 255;
  uint32_t stencilBack = r[REGADDR::DB_STENCILREFMASK_BF] & 255;
  if (!state.stencilValid || state.stencilFront != stencilFront)
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, stencilFront);
  if (!state.stencilValid || state.stencilBack != stencilBack)
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, stencilBack);
  state.stencilFront = stencilFront;
  state.stencilBack = stencilBack;
  state.stencilValid = true;
  rprof::mark(rprof::kRecord);
  rprof::UploadKind indexUploads(rprof::kUpIndex);
  if (replay) {
    static const bool skipVertexBinds = [] {
      const char* e = std::getenv("WWHD_VK_SKIP_VERTEX_BINDS");
      return e && !std::strcmp(e, "1");
    }();
    for (uint32_t i = 0; i < replay->vertexBindCount; ++i) {
      const auto& v = replay->vertexBinds[i];
      const bool skip = skipVertexBinds && !R.vertexDiagnostics.noVertexReuse &&
          state.vertex_bind_matches(cmd, R.submissionGeneration,
          v.binding, v.buffer, v.offset);
      if (!skip) record::vkCmdBindVertexBuffers(cmd, v.binding, 1, &v.buffer, &v.offset);
      if (preparationStats) {
        R.vertexBindCalls += !skip;
        R.vertexBindSkips += skip;
      }
    }
    rprof::mark(rprof::kVertex);
    if (replay->indexed) {
      bindIndex(replay->indexBuffer, replay->indexOffset, replay->indexType);
      record::vkCmdDrawIndexed(cmd, replay->drawCount, instances, 0, int32_t(baseVertex), 0);
    } else record::vkCmdDraw(cmd, replay->drawCount, instances, baseVertex, 0);
  } else {
  // eye 0: the draw eye 1 replays
  auto recordDraw = [&](bool indexed, VkBuffer buffer, VkDeviceSize offset, VkIndexType type, size_t drawCount) {
#ifdef WWHD_NATIVE_DRAW
    if (cap) {
      cap->indexed = indexed;
      cap->indexBuffer = buffer;
      cap->indexOffset = offset;
      cap->indexType = type;
      cap->drawCount = uint32_t(drawCount);
      if (!indexed || drawCount > UINT32_MAX || baseVertex || instances != 1) cap->refuse("draw form");
      if (cap->hashes)
        cap->indexHash = packet_hash(packet_hash_value(packet_hash_value(packet_hash_value(1, indexAddr), indexType),
            drawCount), indexAddr ? mem::ptr(indexAddr) : nullptr, indexAddr ? size_t(guestBytes) : 0);
    }
#endif
    if (!shadersOut) return;
    shadersOut->indexed = indexed;
    shadersOut->indexBuffer = buffer;
    shadersOut->indexOffset = offset;
    shadersOut->indexType = type;
    shadersOut->drawCount = uint32_t(drawCount);
    shadersOut->replay = !deferredPipelineDraw && drawCount <= UINT32_MAX &&
        shadersOut->vertexBindCount <= shadersOut->vertexBinds.size() &&
        shadersOut->submission == R.submissionGeneration;
  };
  // Scan the actual immutable index snapshot, never a second guest read: the bytes of the buffer
  // cache entry's shadow, or of a CPU copy that the upload slice is written from. Never the slice's
  // mapped memory: upload memory is uncached or write-combined on discrete GPUs (issue #44). Unless it
  // is host-cached (R.uploadReadsDirect): then the slice's mapped bytes are scanned, as fast as a copy.
  static std::vector<uint8_t> nativeIndexCopy;  // render thread only
  UploadSlice nativeIndexSlice{};
  const bool hostRestart = topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
                           topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  VertexExtent vertexExtent;
  const void* nativeIndexData = nullptr;  // the bytes of nativeIndexSlice, on the CPU
  if (nativeIndices) {
    const uint32_t indexBytes = indexType == 0 ? 2 : 4;
    bufcache::Entry* indexEntry = nullptr;
    if (buffer_cache_enabled() && uint64_t(count) * indexBytes <= (8u << 20) &&
        cached_native_indices(indexAddr, count * indexBytes, nativeIndexSlice, indexEntry)) {
      nativeIndexData = indexEntry->shadow.data();
      vertexExtent = cached_index_extent(*indexEntry, nativeIndexData, count, indexBytes,
                                         hostRestart, int32_t(baseVertex));
    } else {
      const size_t bytes = size_t(count) * indexBytes;
      const auto* guest = static_cast<const uint8_t*>(mem::ptr(indexAddr));
      if (R.uploadReadsDirect) {
        nativeIndexSlice = snapshot(guest, bytes, 4);
        nativeIndexData = nativeIndexSlice.mapped;
      } else {
        nativeIndexCopy.assign(guest, guest + bytes);
        nativeIndexSlice = snapshot(nativeIndexCopy.data(), bytes, 4);
        nativeIndexData = nativeIndexCopy.data();
      }
      vertexExtent = indexed_vertex_extent(nativeIndexData, count,
                                           indexBytes, hostRestart,
                                           int32_t(baseVertex));
    }
    rprof::guest_read(rprof::kUpIndex, indexAddr, uint64_t(count) * indexBytes);
  } else if (convertedEntry) {
    vertexExtent = cached_index_extent(*convertedEntry, nullptr, convertedEntry->outSize / 4, 4,
                                       hostRestart, int32_t(baseVertex));
  } else if (!indices.empty()) {
    vertexExtent = indexed_vertex_extent(indices.data(), indices.size(), 4,
                                         hostRestart, int32_t(baseVertex));
  } else {
    const uint64_t maximum = uint64_t(baseVertex) + count - 1;
    vertexExtent = {maximum <= UINT32_MAX, uint32_t(maximum)};
  }
  VertexWindowExtent windowExtent;
  const bool windowStats=vertex_window_stats_enabled();
  if(windowStats && vertex_copy_window_enabled()) {
    windowExtent={vertexExtent.valid,vertexExtent.minimum,vertexExtent.maximum};
  } else if(windowStats) {
    if(nativeIndices) {
      windowExtent=indexType==0
        ? vertex_window_extent<uint16_t>(nativeIndexData,count,hostRestart,int32_t(baseVertex))
        : vertex_window_extent<uint32_t>(nativeIndexData,count,hostRestart,int32_t(baseVertex));
    } else if(!indices.empty()) {
      windowExtent=vertex_window_extent<uint32_t>(indices.data(),indices.size(),hostRestart,int32_t(baseVertex));
    } else if(count && uint64_t(baseVertex)+count-1<=UINT32_MAX) {
      windowExtent={true,baseVertex,uint32_t(uint64_t(baseVertex)+count-1)};
    }
  }
  rprof::g_upload_kind = rprof::kUpVertex;
  const bool cachedTrims = dp.trimFetch == fs && dp.trimVertex == vs &&
                           dp.bindingTrims.size() == fs->bufferGroups.size();
  size_t trimGroup = 0;
  for (auto &g : fs->bufferGroups) {
    uint32_t addr =
        r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7];
    uint32_t size =
        r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 1] + 1;
    if (!addr || !size)
      exception_report::raise("missing vertex buffer");
    const BindingTrimMetadata trim = cachedTrims
        ? dp.bindingTrims[trimGroup] : binding_trim_metadata(r, vs, g);
    ++trimGroup;
    const auto attributeEnd = trim.attributeEnd;
    const auto stride = trim.stride;
    const auto supported = trim.supported;
    const auto rate = trim.rate;
    VertexExtent extent = vertexExtent;
    if (rate && *rate == LatteConst::VertexFetchType2::INSTANCE_DATA)
      extent = {true, instances - 1};
    const uint32_t copied = supported
        ? vertex_prefix_size(size, stride, attributeEnd, extent) : size;
    if(windowStats) {
      auto window=windowExtent;
      if(rate && *rate==LatteConst::VertexFetchType2::INSTANCE_DATA)
        window={instances!=0,0,instances-1};
      const bool eligible=supported && attributeEnd && stride && window.valid &&
          uint64_t(window.maximum)*stride+attributeEnd<=copied;
      const uint32_t unused=vertex_window_unused(copied,stride,attributeEnd,window,supported);
      auto& t=vertexWindowStats;
      ++t.bindings;t.eligible+=eligible;t.fallback+=!eligible;t.zeroStride+=stride==0;
      t.prefix+=copied;t.window+=copied-unused;
    }
    R.vertexDeclaredBytes += size;
    R.vertexCopiedBytes += copied;
    rprof::guest_read(rprof::kUpVertex, addr, copied);
    const uint64_t windowBegin=uint64_t(extent.minimum)*stride;
    const uint64_t windowEnd=uint64_t(extent.maximum)*stride+attributeEnd;
    // Converted indices (quads) too: a buffer drawn in consecutive chunks otherwise copies its
    // whole prefix once per chunk; their extent is the memoized scan of the converted bytes.
    const bool copyWindow=vertex_copy_window_enabled() &&
        (nativeIndices || convertedEntry || !indices.empty()) && supported && rate &&
        attributeEnd && stride && extent.valid && vertexExtent.valid &&
        windowEnd<=copied && windowBegin<windowEnd && windowBegin!=0;
    UploadSlice b;
    const uint32_t cpuCount = vk::cpu_vertex_count(g, R.vertexDiagnostics.cpuClasses);
#ifdef WWHD_NATIVE_DRAW
    if (cap && cpuCount) cap->refuse("CPU vertex classes");
    if (cap && cap->hashes) {
      cap->vertexHash = packet_hash(packet_hash_value(packet_hash_value(packet_hash_value(cap->vertexHash + 1,
          g.attributeBufferIndex), addr), copied), mem::ptr(addr), copied);
      cap->vertexLayoutHash = packet_hash_value(packet_hash_value(cap->vertexLayoutHash + 1, g.attributeBufferIndex),
          r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2]);
    }
#endif
    if (cpuCount) {
      const auto converted = cpu_vertex_stream(g, addr, size, stride,
          supported && attributeEnd && extent.valid && vertexExtent.valid ? uint64_t(extent.maximum) + 1 : 0);
      const uint32_t binding = vk::cpu_vertex_binding(*fs, g, R.vertexDiagnostics.cpuClasses,
          R.properties.limits.maxVertexInputBindings);
      if (binding >= R.properties.limits.maxVertexInputBindings)
        exception_report::raise("CPU vertex classes exceed device vertex binding limit");
      record::vkCmdBindVertexBuffers(cmd, binding, 1, &converted.buffer, &converted.offset);
      if (shadersOut) shadersOut->add_vertex_bind(binding, converted.buffer, converted.offset);
      // The extra bind bypasses the normal bind cache.
      state.vertexBinds = {};
      if (preparation_stats_enabled()) ++R.vertexBindCalls;
      if (cpuCount == uint32_t(g.attribCount)) continue;
    }
    if (R.vertexDiagnostics.noVertexReuse)
      b = snapshot(mem::ptr(addr), size, 4);  // the whole declared buffer, fresh every draw
    else if (!buffer_cache_enabled() || !cached_guest_range(addr, copied, rprof::kUpVertex, b))
      b = copyWindow
        ? vertex_window_snapshot(g.attributeBufferIndex,addr,copied,
                                 uint32_t(windowBegin),uint32_t(windowEnd-windowBegin))
        : vertex_snapshot(g.attributeBufferIndex, addr, copied,
                          supported && attributeEnd && extent.valid && vertexExtent.valid);
    VkDeviceSize offset = b.offset;
    static const bool skipVertexBinds = [] {
      const char* e=std::getenv("WWHD_VK_SKIP_VERTEX_BINDS");
      return e && !std::strcmp(e,"1");
    }();
    // Snapshot preparation above stays fresh even when its immutable slice is
    // unchanged. Compare host binding identity, never guest addresses alone.
    const bool skip=skipVertexBinds && !R.vertexDiagnostics.noVertexReuse && state.vertex_bind_matches(cmd,
        R.submissionGeneration,g.attributeBufferIndex,b.buffer,offset);
    if(preparation_stats_enabled()) {
      R.vertexBindCalls+=!skip;
      R.vertexBindSkips+=skip;
    }
    if (shadersOut) shadersOut->add_vertex_bind(g.attributeBufferIndex, b.buffer, offset);
#ifdef WWHD_NATIVE_DRAW
    if (cap) {
      if (cap->vertexBindCount < cap->vertexBinds.size()) {
        auto& v = cap->vertexBinds[cap->vertexBindCount++];
        v.binding = g.attributeBufferIndex;
        v.buffer = b.buffer;
        v.offset = offset;
        for (int i = 0; i < 3; ++i) v.regs[i] = r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + i];
        v.stride = stride;
        v.perInstance = rate && *rate == LatteConst::VertexFetchType2::INSTANCE_DATA;
        v.attributeEnd = attributeEnd;
        v.supported = supported;
      } else cap->refuse("vertex bindings");
    }
#endif
    if(!skip)
      record::vkCmdBindVertexBuffers(cmd,g.attributeBufferIndex,1,&b.buffer,&offset);
  }
#ifdef __ANDROID__
  static std::set<std::tuple<uint32_t, uint32_t, bool, bool, bool, bool>> seenIndexPaths;
  const auto indexDiagnostic = std::make_tuple(prim, indexType, nativeIndices, bool(indexAddr), baseVertex != 0, stripRestart);
  if (seenIndexPaths.size() < 32 && seenIndexPaths.insert(indexDiagnostic).second)
    LOG("[vulkan index observed] guestPrimitive=0x%x guestIndexType=%u indexed=%u native=%u hostType=%s stripRestart=%u nonzeroBaseVertex=%u",
        prim, indexType, bool(indexAddr), nativeIndices,
        nativeIndices ? (indexType == 0 ? "uint16" : "uint32") : (convertedEntry || !indices.empty() ? "uint32-converted" : "none"),
        stripRestart, baseVertex != 0);
#endif
  rprof::g_upload_kind = rprof::kUpIndex;
  rprof::mark(rprof::kVertex);
#ifdef WWHD_NATIVE_DRAW
  if (cap) {
    cap->nativeIndexWidth = nativeIndices ? (indexType == 0 ? 2 : 4) : 0;
    cap->hostRestart = hostRestart;
    cap->stripRestart = stripRestart;
    cap->restartIndex = restartIndex;
  }
#endif
  if (nativeIndices) {
    bindIndex(nativeIndexSlice.buffer, nativeIndexSlice.offset,
                         indexType == 0 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    recordDraw(true, nativeIndexSlice.buffer, nativeIndexSlice.offset,
               indexType == 0 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32, count);
    if(deferredPipelineDraw) { auto* pipeline=&dp; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDrawIndexed(cmd, count, instances, 0, int32_t(baseVertex), 0); }); }
    else record::vkCmdDrawIndexed(cmd, count, instances, 0, int32_t(baseVertex), 0);
  } else if (convertedEntry) {
    const uint32_t converted = convertedEntry->outSize / 4;
    auto b = buffer_cache_slice(*convertedEntry, convertedEntry->outSize);
    rprof::guest_read(rprof::kUpIndex, indexAddr, guestBytes);
    bindIndex(b.buffer, b.offset, VK_INDEX_TYPE_UINT32);
    recordDraw(true, b.buffer, b.offset, VK_INDEX_TYPE_UINT32, converted);
    if(deferredPipelineDraw) { auto* pipeline=&dp; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDrawIndexed(cmd, converted, instances, 0, int32_t(baseVertex), 0); }); }
    else record::vkCmdDrawIndexed(cmd, converted, instances, 0, int32_t(baseVertex), 0);
  } else if (indices.empty()) {
    recordDraw(false, VK_NULL_HANDLE, 0, VK_INDEX_TYPE_UINT32, count);
    if(deferredPipelineDraw) { auto* pipeline=&dp; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDraw(cmd, count, instances, baseVertex, 0); }); }
    else record::vkCmdDraw(cmd, count, instances, baseVertex, 0);
  } else {
    auto b = snapshot(indices.data(), indices.size() * 4, 4);
    if (indexAddr) rprof::guest_read(rprof::kUpIndex, indexAddr, guestBytes);
    bindIndex(b.buffer, b.offset, VK_INDEX_TYPE_UINT32);
    recordDraw(true, b.buffer, b.offset, VK_INDEX_TYPE_UINT32, indices.size());
    const auto drawCount=indices.size(); if(deferredPipelineDraw) { auto* pipeline=&dp; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDrawIndexed(cmd, drawCount, instances, 0, int32_t(baseVertex), 0); }); }
    else record::vkCmdDrawIndexed(cmd, indices.size(), instances, 0, int32_t(baseVertex), 0);
  }
  }
  if(R.gpuPassTimestampsEnabled) gpu_count_render_draw();
  for (int i = 0; i < 8; i++)  // (slots taken over by a pass subset aren't written)
    if (colors[i] && !((subsetAbsent >> i) & 1)) mark_draw_target_written(colors[i]);
  if (depth) mark_draw_target_written(depth);
  R.drawCount++;
#ifdef WWHD_NATIVE_DRAW
  if (cap) {
    cap->drew = true;
    g_packetCapture = nullptr;
  }
  // a J3D draw resolved in full: its state, filed under its key (j3d_store refuses what cannot be reused)
  if (j3dDraw && !fast) j3d_store(j3dKey, continued, r, prim);
#endif
  if (aoPrivateReplay) {
    ao_private().frame = R.frame;
    return;
  }
  if (ao_hires_enabled() && colors[0] &&
      ps->kind == gfx::ProgramKind::DepthDownsample) {
#ifdef WWHD_NATIVE_DRAW
    if (cap) cap->refuse("AO replay");
#endif
    struct ReplayGuard {
      ReplayGuard() { aoPrivateReplay = true; ao_private().frame = ~0ull; }
      ~ReplayGuard() { aoPrivateReplay = false; }
    } guard;
    draw(r, prim, count, indexType, indexAddr, baseVertex, instances);
    // the replay changes later lookups (aoPrivateSource/aoPrivateFrame, private images)
    continued.valid = false;
    continued.invalidReason = kAoReplay;
  }
  static const uint32_t drawBatch = parse_draw_batch(std::getenv("WWHD_VK_DRAW_BATCH"));
  static const uint32_t drawBatchCap =
      parse_draw_batch_cap(std::getenv("WWHD_VK_DRAW_BATCH_CAP"));
  if (drawBatchState.after_draw(R.frame, drawBatch, drawBatchCap)) {
    // Submit only after this draw owns all its upload slices and deferred
    // resources. The next draw reopens attachments with LOAD and rebinds state.
    flush_async();
    ++drawBatchSubmissions;
  }
  rprof::mark(rprof::kSubmit);
  // Original draw is complete. Replay exactly this pane draw with its own color
  // attachment; no main-target pixels are copied and no target state escapes.
  if(stereoEye||!gamepad_backdrop_capture_active()) return;
  const auto captureBlend=backdrop_blend(r[REGADDR::CB_COLOR_CONTROL],r[REGADDR::CB_BLEND0_CONTROL],r[REGADDR::CB_TARGET_MASK]);
  bool compatible = shadersOut && shadersOut->replay && shadersOut->rasterReady &&
      shadersOut->submission==R.submissionGeneration && captureBlend.supported && colors[0] && slices[0] == 0 && !(r[REGADDR::DB_DEPTH_CONTROL] & 7);
  for (unsigned i=1;i<8;++i) compatible &= colors[i] == nullptr;
  const uint64_t snapshotFlags=uint64_t(bool(shadersOut))|(uint64_t(shadersOut&&shadersOut->replay)<<1)|
      (uint64_t(shadersOut&&shadersOut->rasterReady)<<2)|(uint64_t(shadersOut&&shadersOut->submission==R.submissionGeneration)<<3)|
      (uint64_t(captureBlend.supported)<<4)|(uint64_t(bool(colors[0]))<<5)|(uint64_t(slices[0]==0)<<6)|(uint64_t(compatible)<<7);
  gamepad_backdrop_debug("draw-gate",snapshotFlags,(uint64_t(r[REGADDR::CB_BLEND0_CONTROL])<<32)|r[REGADDR::CB_COLOR_CONTROL],
      (uint64_t(r[REGADDR::DB_DEPTH_CONTROL])<<32)|r[REGADDR::CB_TARGET_MASK],R.submissionGeneration);
  if (gamepad_backdrop_begin_replay(colors[0],compatible,captureBlend.opaque)) {
#ifdef WWHD_NATIVE_DRAW
    if (cap) cap->refuse("backdrop replay");
#endif
    const auto before=R.drawCount;
    // The coverage-only alpha override is part of the ordinary pipeline key: the
    // three capture registers of the pane's own register file (the gx2 file the
    // scope was opened with) are patched for the replay only and restored on
    // every exit, so guest/native draw state is unchanged. Any other register
    // file gets one reused private copy (never expected).
    uint32_t* regs=const_cast<uint32_t*>(r);
    if(!gamepad_backdrop_scope_registers(r)) {
      static std::vector<uint32_t> privateRegs;
      privateRegs.assign(r,r+gx2::kNumRegs);regs=privateRegs.data();
    }
    BackdropRegisterPatch patch(regs,REGADDR::CB_BLEND0_CONTROL,REGADDR::CB_COLOR_CONTROL,REGADDR::CB_TARGET_MASK,captureBlend);
    continued.valid=false;
    backdropShadersIn=shadersOut;
    try { draw(regs,prim,count,indexType,indexAddr,baseVertex,instances,nullptr); }
    catch (...) { backdropShadersIn=nullptr;gamepad_backdrop_end_replay(false);continued.valid=false;throw; }
    backdropShadersIn=nullptr;
    gamepad_backdrop_end_replay(R.drawCount!=before);
    continued.valid=false;
  }
}
#ifdef WWHD_NATIVE_DRAW
// ---------------------------------------------------------------- native draw packets (gx2/native_draw.h)
// A packet's lowered state, per mesh kind: what the kind's last full draw resolved (PacketCapture), valid for the
// packet's next instances while nothing else happened in between: no other command or register change than the
// instances' own effects (native_draw::g_epoch), no draw that is not this packet's, the same submission, frame,
// render pass and command buffer, and no texture upload (g_surface_epoch). The lowering draw is the full draw itself,
// so the shaders, pipeline, targets, textures, support uniforms and every other uniform block are exactly the full
// path's; it is refused (and every instance of the kind drawn the full way) when it was not a plain draw: a replay, a
// deferred pipeline, CPU vertex classes, an AO-substituted, feedback or target-sharing texture (the continued-draw
// state cannot continue), ranked or non-dynamic uniform descriptors, a geometry-stage instance block.
// An instance replays its kind's state with its own blocks (re-read from the registers its effects set) and, when its
// vertex input differs from the kind's (a per-draw stream: the same fetch content and strides), its own vertex
// buffers, copied as the full draw copies them. Kinds alternate: the replay rebinds the pipeline, vertex and index
// buffers when the kind changes.
namespace {
// ---------------------------------------------------------------- batching (WWHD_NATIVE_DRAW_BATCH)
// The batch shader pair of a lowered kind: its translated programs' final text (the text the pipeline was compiled
// from: translation, graphics pack, depth clamp, clip and vertex-format rewrites included), rewritten only where the
// instance blocks' entries are read. The vertex program reads each instance's entries from per-instance attributes
// (ivec4, raw bits, as the support uniforms hold them) instead of uf_remappedVS[m]; the pixel program reads its
// entries from flat varyings the vertex program passes on instead of uf_remappedPS[m]. gl_InstanceID stays 0, as in
// the one-instance draws. Every other statement is the translated one: the same operations in the same order and
// precision. Refused (the kind replays one by one): an instance block read as a whole buffer (uf_block*: it would
// need a block array indexed by the instance), instance-rate vertex input, a text without the expected markers, no
// free attribute, binding or varying slots.
struct NativeEntry {
  uint32_t block = 0, src = 0;  // instance block (packet order) and byte offset in it: one 16-byte stream entry
  uint32_t stage = 0, dst = 0;  // where the support uniforms hold it (verify mode compares the two)
};
struct NativeShaders {
  const char* refused = nullptr;
  std::unique_ptr<vk::Shader> vs, ps;  // ps empty: the translated pixel shader
  NativeInstanceInput input;
  std::vector<NativeEntry> entries;
};
std::unordered_map<std::string, std::unique_ptr<NativeShaders>> nativeShaders;

// "uf_remapped<S>[m]" -> NAME m, for every m in MAP; false when a marker is missing
bool native_replace_remapped(std::string& text, const char* array, const std::vector<std::pair<uint32_t, std::string>>& map) {
  for (const auto& [m, name] : map) {
    const std::string from = std::string(array) + "[" + std::to_string(m) + "]";
    size_t at = text.find(from), n = 0;
    for (; at != std::string::npos; at = text.find(from, at + name.size()), ++n) text.replace(at, from.size(), name);
    if (!n) return false;  // (the program reads every remapped entry it declares)
  }
  return true;
}
uint32_t native_max_location(const std::string& text) {
  uint32_t mx = 0;
  for (const char* marker : {"location = ", "location=", "ATTR_LAYOUT("})
    for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1)) {
      const char* q = text.c_str() + at + std::strlen(marker);
      if (*q >= '0' && *q <= '9') mx = std::max<uint32_t>(mx, uint32_t(std::strtoul(q, nullptr, 10)));
    }
  return mx;
}
// the first statement position of main()
size_t native_main_body(const std::string& text) {
  const size_t main = text.find("void main(");
  if (main == std::string::npos) return std::string::npos;
  const size_t brace = text.find('{', main);
  return brace == std::string::npos ? brace : brace + 1;
}
void native_dump(const std::string& name, const std::string& text) {
  static const char* dir = std::getenv("WWHD_NATIVE_DRAW_DUMP");
  if (!dir || !*dir) return;
  if (FILE* f = std::fopen((std::string(dir) + "/" + name).c_str(), "w")) {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
  }
}

NativeShaders* native_shaders(const PacketCapture& cap, const native_draw::Packet& pk) {
  // the instance blocks' remapped entries per stage: (mapped index, block, byte offset in the block)
  struct Read { uint32_t m, block, src; };
  std::vector<Read> reads[2];
  std::string sig = std::to_string(cap.vs->key) + ":" + std::to_string(cap.ps->key);
  for (int st = 0; st < 2; ++st) {
    const vk::Shader* sh = st ? cap.ps : cap.vs;
    for (int k = 0; k < PacketCapture::kBlocks; ++k)
      for (const auto& [dst, src] : cap.stage[st].block[k].remap) {
        const uint32_t m = (dst - uint32_t(sh->uniforms.offset_remapped)) / 16;
        reads[st].push_back({m, uint32_t(k), src});
        sig += "|" + std::to_string(st) + "," + std::to_string(k) + "," + std::to_string(m) + "," + std::to_string(src);
      }
  }
  auto& slot = nativeShaders[sig];
  if (slot) return slot.get();
  slot = std::make_unique<NativeShaders>();
  NativeShaders& ns = *slot;
  auto refuse = [&](const char* why) {
    ns.refused = why;
    LOG("[native-draw] packet %u (site %u pass %u): no batch shaders: %s", pk.id, pk.site, pk.pass, why);
    return &ns;
  };
  if (reads[0].empty() && reads[1].empty()) return refuse("no instance block entries read");
  std::string vt = cap.vs->source_text(), pt = cap.ps->source_text();
  if (vt.empty() || pt.empty()) return refuse("no shader text (retained only with WWHD_NATIVE_DRAW_BATCH=1 from the start)");
  for (const auto& g : cap.fs->bufferGroups)
    for (int j = 0; j < g.attribCount; ++j)
      if (g.attrib[j].fetchType != LatteConst::VertexFetchType2::VERTEX_DATA) return refuse("instance-rate vertex input");
  for (uint32_t i = 0; i < pk.blockCount && i < native_draw::kMaxBlocks; ++i) {
    const uint32_t stage = pk.block[i] >> 16, loc = pk.block[i] & 0xFFFF;
    const std::string buffer = std::string(stage ? "uf_blockPS" : "uf_blockVS") + std::to_string(loc) + "[";
    if ((stage ? pt : vt).find(buffer) != std::string::npos) return refuse("an instance block read as a buffer");
  }
  const std::string instanceDefine = "#define gl_InstanceID gl_InstanceIndex";
  const size_t def = vt.find(instanceDefine);
  if (def == std::string::npos) return refuse("no gl_InstanceID define");
  vt.replace(def, instanceDefine.size(), "#define gl_InstanceID 0");  // (the one-instance draws: instance 0)
  if (vt.find("gl_InstanceIndex") != std::string::npos) return refuse("the vertex program reads gl_InstanceIndex");
  native_dump(std::to_string(cap.vs->key) + "-vs.glsl", cap.vs->source_text());
  native_dump(std::to_string(cap.ps->key) + "-ps.glsl", cap.ps->source_text());
  // attribute locations above every location either program declares (and the vertex-format tail ones)
  uint32_t location = std::max({native_max_location(vt), native_max_location(pt),
                                uint32_t(vk::vertex_tail_location(cap.vs->mapping.attributeMapping))}) + 1;
  uint32_t varying = location;
  std::string vdecl, pdecl, vcopy;
  std::vector<std::pair<uint32_t, std::string>> vmap, pmap;
  for (const auto& r : reads[0]) {
    const std::string name = "ndInst" + std::to_string(r.m);
    vdecl += "layout(location = " + std::to_string(location) + ") in ivec4 " + name + ";\n";
    ns.input.locations.push_back(location++);
    ns.entries.push_back({r.block, r.src, 0, uint32_t(cap.vs->uniforms.offset_remapped) + 16 * r.m});
    vmap.push_back({r.m, name});
  }
  for (const auto& r : reads[1]) {
    const std::string in = "ndInstP" + std::to_string(r.m), pass = "ndPass" + std::to_string(r.m);
    vdecl += "layout(location = " + std::to_string(location) + ") in ivec4 " + in + ";\n";
    ns.input.locations.push_back(location++);
    ns.entries.push_back({r.block, r.src, 1, uint32_t(cap.ps->uniforms.offset_remapped) + 16 * r.m});
    vdecl += "layout(location = " + std::to_string(varying) + ") flat out ivec4 " + pass + ";\n";
    pdecl += "layout(location = " + std::to_string(varying) + ") flat in ivec4 " + pass + ";\n";
    vcopy += pass + " = " + in + ";\n";
    ++varying;
    pmap.push_back({r.m, pass});
  }
  if (location > R.properties.limits.maxVertexInputAttributes) return refuse("no free vertex attribute locations");
  if (varying > location && (varying > R.properties.limits.maxVertexOutputComponents / 4 ||
                             varying > R.properties.limits.maxFragmentInputComponents / 4))
    return refuse("no free varying locations");
  if (!native_replace_remapped(vt, "uf_remappedVS", vmap)) return refuse("an instance entry the vertex program does not read");
  if (!pmap.empty() && !native_replace_remapped(pt, "uf_remappedPS", pmap))
    return refuse("an instance entry the pixel program does not read");
  const size_t vmain = vt.find("invariant gl_Position;") != std::string::npos ? vt.find("invariant gl_Position;") : vt.find("void main(");
  if (vmain == std::string::npos) return refuse("no main in the vertex program");
  vt.insert(vmain, vdecl);
  if (!vcopy.empty()) {
    const size_t body = native_main_body(vt);
    if (body == std::string::npos) return refuse("no main body in the vertex program");
    vt.insert(body, "\n" + vcopy);
  }
  if (!pdecl.empty()) {
    const size_t pmain = pt.find("void main(");
    if (pmain == std::string::npos) return refuse("no main in the pixel program");
    pt.insert(pmain, pdecl);
  }
  // the binding: the lowest one the fetch shader's groups leave free (MoltenVK places vertex bindings at the top of
  // the Metal buffer table, counting down, so a high binding would meet the descriptor buffers counting up)
  uint32_t used = 0;
  for (const auto& g : cap.fs->bufferGroups)
    if (g.attributeBufferIndex < 32) used |= 1u << g.attributeBufferIndex;
  ns.input.binding = uint32_t(__builtin_ctz(~used));
  ns.input.stride = uint32_t(16 * ns.entries.size());
  if (ns.input.binding >= std::min<uint32_t>(16, R.properties.limits.maxVertexInputBindings))
    return refuse("no free vertex binding");
  if (ns.input.stride > R.properties.limits.maxVertexInputBindingStride) return refuse("instance stride");
  auto make = [&](const vk::Shader& base, const std::string& text, bool vertex) -> std::unique_ptr<vk::Shader> {
    std::string error;
    auto words = vk::compile_glsl(text, vertex, &error);
    native_dump(std::to_string(base.key) + (vertex ? "-native-vs.glsl" : "-native-ps.glsl"), text);
    if (words.empty()) {
      LOG("[native-draw] batch %s shader: %s", vertex ? "vertex" : "pixel", error.c_str());
      return nullptr;
    }
    auto n = std::make_unique<vk::Shader>(base);
    const uint64_t h = std::hash<std::string>{}(text) | 1;
    n->key = base.key ^ (h * 0x9E3779B97F4A7C15ull);
    n->pipelineId = base.pipelineId ^ (h * 0xC2B2AE3D27D4EB4Full);
    n->spirvWords = std::make_shared<const std::vector<uint32_t>>(std::move(words));
    n->binary.reset();
    n->glsl.clear();
    n->translatedGlsl.clear();
    n->retainedSource = std::make_shared<const std::string>(text);
    return n;
  };
  ns.vs = make(*cap.vs, vt, true);
  if (!ns.vs) return refuse("the batch vertex shader did not compile");
  if (!pmap.empty()) {
    ns.ps = make(*cap.ps, pt, false);
    if (!ns.ps) return refuse("the batch pixel shader did not compile");
  }
  LOG("[native-draw] packet %u (site %u pass %u): batch shaders: %zu vertex and %zu pixel instance entries, stride %u, "
      "binding %u", pk.id, pk.site, pk.pass, reads[0].size(), reads[1].size(), ns.input.stride, ns.input.binding);
  return &ns;
}
// the batch pipeline of a lowered kind: its pipeline's variant with the batch shaders and the instance stream; a
// background variant (never compiled or waited for on the render thread, never recorded). nullptr while not ready
Pipeline* native_pipeline(const uint32_t* r, const PacketCapture& cap, NativeShaders& ns) {
  g_nativeInput = &ns.input;
  Pipeline* p = nullptr;
  try {
    p = &pipeline(r, ns.vs.get(), ns.ps ? ns.ps.get() : cap.ps, cap.fs, cap.topology, cap.pipeColors, cap.pipeDepth,
                  PipelineVariant{.clearMask = cap.clearMask, .background = true});
  } catch (...) { g_nativeInput = nullptr; throw; }
  g_nativeInput = nullptr;
  if (p == &declined_pipeline_storage()) return nullptr;
  if (!p->pipeline && p->pendingCompile && !p->compileError) {
    try { resolve_pending_pipeline(*p, false); } catch (...) {}
  }
  if (!p->pipeline || p->pendingCompile || p->dynamicUniforms != cap.pipeline->dynamicUniforms) return nullptr;
  return p;
}

struct LoweredKind {
  bool valid = false;
  uint64_t epoch = 0, submission = 0, frame = 0, surfaceEpoch = 0, passCount = 0;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  PacketCapture cap;
  ContinuedStage stages[2];  // the textures: their guest writes upload before each instance (as a continued draw)
  uint32_t fsRegs[2]{};
  std::vector<uint8_t> fsBytes;  // the fetch shader's content (instances with their own fetch shader copy compare it)
  uint32_t blockSize[native_draw::kMaxBlocks]{};  // the instance blocks' sizes it was lowered with
  std::vector<uint32_t> checkWords;  // the packet's compared checked registers it was lowered with (native_draw.h)
  // multi-draw batches (WWHD_NATIVE_DRAW_MDI): the kind's instances with a stream of their own may join one; null:
  // refused (the reason, logged once per packet)
  const char* mdiRefused = "not lowered";
  // the index range it was lowered with (the kind's mesh, or the instance's own range); instances with another range
  // may join a range batch (null: refused, the reason logged once per packet)
  uint32_t rangeCount = 0, rangeAddr = 0;
  const char* rangeRefused = "not lowered";
  // batching: the kind's batch shaders and pipeline (nullptr: its instances replay one by one)
  NativeShaders* batch = nullptr;
  Pipeline* batchPipeline = nullptr;
  // shader-rewrite stream batches: the previous instance's blocks (address and size words); blocksVary: an instance
  // came with other blocks than the one before it (kept across lowerings), so a multi-draw batch, which needs the same
  // blocks, takes at most the instances of one object (cloud cards: one each; particles: one particle's buffers);
  // streamRefused: what keeps the lowered capture from stream batches (mdi_refusal, whether multi-draw batches are on
  // or not)
  uint32_t lastBlockAddr[native_draw::kMaxBlocks]{}, lastBlockSize[native_draw::kMaxBlocks]{};
  bool lastSeen = false, blocksVary = false;
  const char* streamRefused = "not lowered";
  uint64_t journalPos = 0, journalBumps = 0;  // the register journal at the lowering (native_draw.h)
  // an instance of the kind had another index range than the lowered one (kept across lowerings): its instances with a
  // stream of their own join range batches too (a magma ball's first strip starts its ball's batch); else such
  // instances form stream batches (3D lines: one index list, a stream per line)
  bool rangeVaries = false;
};
struct LoweredPacket {
  uint64_t drawCount = ~0ull;  // R.drawCount after the packet's last draw (lowering or replay)
  int boundKind = -1;          // the kind whose pipeline, vertex and index buffers are bound now
  int mdiKind = -1;            // the kind whose pipeline and index buffer a multi-draw batch bound last (its draw was the
                               // packet's last; -1 after any other draw of the packet)
  std::array<LoweredKind, native_draw::kMaxKinds> kinds;
};
std::array<std::unique_ptr<LoweredPacket>, native_draw::kMaxPackets> loweredPackets;
// Multi-draw batches (WWHD_NATIVE_DRAW_MDI=1): consecutive replayable instances of one packet kind that bind the same
// blocks and differ only in their vertex stream are drawn as one vkCmdDrawIndexedIndirect (one command per instance)
// with their streams back to back in one upload slice (vertexOffset per command). Same pipeline, shaders, descriptors
// and bytes as one replay per instance, so the pictures must be the same; refused for a kind whose vertex shader reads
// the vertex index (vertexOffset is part of it), with an instance-rate or more than one vertex buffer, or a zero
// stride. Without the device feature (or while commands are recorded deferred) the commands become one
// vkCmdDrawIndexed each, still under one bind.
// Range batches: consecutive replayable instances of one packet kind with the same blocks and the same vertex input
// that differ only in their own index range (count and index address: sea strips, a magma ball's strips) are drawn the
// same way with one command per instance: their guest index bytes back to back in one upload slice (firstIndex per
// command, vertexOffset 0, so every vertex index is the one the full draw sees) and each vertex buffer copied as the
// full draw copies it, up to the largest index of all the batch's ranges (the prefix the full draw of their union
// would copy; every instance reads only inside it). No vertex shader check is needed. Refused for a kind whose indices
// were converted (big-endian, fans, quads, other restart markers) or with an instance-rate vertex buffer.
struct MdiBatch {
  bool active = false;
  bool range = false;  // a range batch (else a stream batch)
  uint32_t id = 0, kind = 0, site = 0, count = 0, binding = 0;
  LoweredPacket* packet = nullptr;
  LoweredKind* lowered = nullptr;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint64_t submission = 0, frame = 0, passCount = 0;
  uint32_t blockAddr[native_draw::kMaxBlocks]{}, blockSize[native_draw::kMaxBlocks]{};
  VkDescriptorSet sets[2]{};
  std::array<uint32_t, 34> offsets{};
  std::vector<uint8_t> bytes;
  std::vector<VkDrawIndexedIndirectCommand> cmds;
  // range batches: the instances' index bytes; per vertex buffer of the kind its copy so far (grown at each instance
  // to the prefix the largest index so far needs, read from guest memory at that instance, as its full draw would)
  std::vector<uint8_t> indexBytes;
  uint32_t vertexRegs[16][2]{};  // a range batch's vertex buffers (address and size words)
  bool extentValid = true;
  uint32_t maxIndex = 0;
  std::array<std::vector<uint8_t>, 16> vertexBytes;
} mdiBatch;
uint64_t mdiLost = 0;
// Does the vertex shader's vertex index reach anything? The translation puts it into R0.x only (R0 = (vertex index,
// 0, 0, instance index), LatteDecompilerEmitGLSL), and only when the program uses R0. A multi-draw command's vertexOffset
// is part of the vertex index, so a batch must not change what the shader computes from it: accepted only when, in the
// straight-line code after that line, R0.x is overwritten (by a value not read from R0) before anything reads it and
// before any branch, loop or block; R0's other components may be read (y, z: 0; w: the instance index, 0 as in a
// single draw). Anything else (another use of the vertex index, a read of R0.x or of R0 whole, control flow first, no
// retained text) counts as a read. Decided once per shader (the final text: graphics packs and overrides included).
bool vertex_index_live(const vk::Shader* s) {
  static std::unordered_map<const vk::Shader*, bool> memo;
  if (auto it = memo.find(s); it != memo.end()) return it->second;
  const std::string& t = s->source_text();
  auto lines = [&](auto&& each) {  // each trimmed line after the first; stop when EACH returns false
    for (size_t pos = 0; pos < t.size();) {
      size_t end = t.find('\n', pos);
      if (end == std::string::npos) end = t.size();
      const size_t b = t.find_first_not_of(" \t", pos);
      if (b < end && !each(t.substr(b, end - b), b)) return;
      pos = end + 1;
    }
  };
  // the vertex index occurs exactly once (besides the #define): in R0's initialisation
  size_t init = std::string::npos;
  int uses = 0;
  lines([&](const std::string& line, size_t at) {
    if (line.compare(0, 7, "#define") == 0) return true;
    for (size_t k = line.find("gl_VertexID"); k != std::string::npos; k = line.find("gl_VertexID", k + 1)) {
      ++uses;
      if (line.find("ivec4(gl_VertexID, 0, 0, gl_InstanceID)") != std::string::npos) init = at;
    }
    return true;
  });
  bool live = true;
  if (uses == 1 && init != std::string::npos) {
    std::string name;  // R0f / R0i: the line is "<name> = ...ivec4(gl_VertexID, 0, 0, gl_InstanceID)..."
    for (size_t k = init; k < t.size() && (std::isalnum(uint8_t(t[k])) || t[k] == '_'); ++k) name += t[k];
    auto reads_x = [&](const std::string& text) {  // R0 whole, or a swizzle with x
      for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
        const size_t e = at + name.size();
        if (at && (std::isalnum(uint8_t(text[at - 1])) || text[at - 1] == '_')) continue;
        if (e < text.size() && (std::isalnum(uint8_t(text[e])) || text[e] == '_')) continue;
        if (e >= text.size() || text[e] != '.') return true;
        size_t q = e + 1;
        while (q < text.size() && std::strchr("xyzw", text[q]))
          if (text[q++] == 'x') return true;
        if (q == e + 1) return true;
      }
      return false;
    };
    auto starts = [](const std::string& l, const char* w) {
      const size_t n = std::strlen(w);
      return l.compare(0, n, w) == 0 && (l.size() == n || !(std::isalnum(uint8_t(l[n])) || l[n] == '_'));
    };
    bool after = false;
    lines([&](const std::string& line, size_t at) {
      if (!after) { after = at == init; return true; }
      if (line.find_first_of("{}") != std::string::npos || starts(line, "if") || starts(line, "else") ||
          starts(line, "for") || starts(line, "while") || starts(line, "do") || starts(line, "switch") ||
          starts(line, "return") || starts(line, "break") || starts(line, "continue") || starts(line, "discard"))
        return false;  // control flow first: live
      if (line.find(name) == std::string::npos) return true;
      const size_t eq = line.find(" = ");
      const bool lhsIsName = eq != std::string::npos && line.compare(0, name.size(), name) == 0 &&
                             (line[name.size()] == ' ' || line[name.size()] == '.');
      if (!lhsIsName) return !reads_x(line);
      const std::string lhs = line.substr(0, eq);
      if (reads_x(line.substr(eq + 3))) return false;
      if (lhs == name || lhs.find('x', name.size()) != std::string::npos) { live = false; return false; }
      return true;  // another component written
    });
  }
  return memo[s] = live;
}
const char* range_refusal(const PacketCapture& c) {
  if (!c.indexed) return "not indexed";
  if (!c.nativeIndexWidth && c.indexType != VK_INDEX_TYPE_UINT32) return "index form";
  for (uint32_t i = 0; i < c.vertexBindCount; ++i)
    if (c.vertexBinds[i].perInstance) return "instance-rate vertex buffer";
  return nullptr;
}
const char* mdi_refusal(const PacketCapture& c) {
  if (!c.indexed) return "not indexed";
  if (c.vertexBindCount != 1) return "not one vertex buffer";
  if (c.vertexBinds[0].perInstance) return "instance-rate vertex buffer";
  if (!c.vertexBinds[0].stride) return "zero stride";
  if (!c.vs) return "no vertex shader";
  if (c.vs->source_text().empty()) return "the vertex shader text is not kept";
  if (vertex_index_live(c.vs)) return "the vertex shader may use the vertex index";
  return nullptr;
}
// Shader-rewrite batches (WWHD_NATIVE_DRAW_BATCH): the replayable instances of one packet kind with the kind's own
// vertex input and index range (a static mesh; their instance blocks differ) collected for one instanced draw
// (native_draw::g_batchPending while count > 0). Instances a multi-draw batch can take (a stream or index range of
// their own) never join one: multi-draw batches are exact and come first.
// Stream batches: the same for a kind whose instances have a stream of their own and blocks that vary from instance to
// instance (cloud cards, particles): each instance's stream is copied back to back as a multi-draw batch copies it and
// the batch is one vkCmdDrawIndexedIndirect with one command per instance (vertexOffset: its stream; firstInstance: its
// entries in the per-instance stream).
struct PendingBatch {
  bool active = false;
  bool stream = false;  // a stream batch (else every instance draws the kind's own vertex input)
  uint32_t id = 0, kind = 0, site = 0, count = 0, binding = 0;
  LoweredPacket* packet = nullptr;
  LoweredKind* lowered = nullptr;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint64_t submission = 0, frame = 0, passCount = 0;
  std::vector<uint8_t> data;
  std::vector<uint8_t> bytes;  // stream batches: the instances' streams
  std::vector<VkDrawIndexedIndirectCommand> cmds;
} pendingBatch;
uint64_t batchLost = 0;
uint64_t packetVerifyCounter = 0;

// The register journal (native_draw.h): the kind's lowered state stays valid when every epoch bump since its lowering
// (or its last check) came from a journalled register write and every register those writes changed holds its value
// from then again, so the full draw would see the same register file. The instance's own registers are the replay's
// and are skipped: its blocks' address and size words, its vertex buffers' address and size words, the fetch shader
// (compared by content). Off with WWHD_NATIVE_DRAW_JOURNAL=0.
bool packet_regs_restored(LoweredKind& L, const native_draw::Packet& pk, const uint32_t* r) {
  using namespace native_draw;
  static const bool on = [] {
    const char* e = std::getenv("WWHD_NATIVE_DRAW_JOURNAL");
    return !(e && *e == '0');
  }();
  if (!on || g_epoch - L.epoch != g_journalBumps - L.journalBumps || g_journalPos - L.journalPos > kJournal)
    return false;
  static std::vector<uint32_t> seen(0x10000, 0);
  static uint32_t stamp = 0;
  if (++stamp == 0) std::fill(seen.begin(), seen.end(), 0), stamp = 1;
  static const uint32_t stageBase[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START,
                                        mmSQ_GS_UNIFORM_BLOCK_START};
  for (uint32_t i = 0; i < pk.blockCount && i < kMaxBlocks; ++i) {
    const uint32_t stage = pk.block[i] >> 16, reg = stageBase[stage < 3 ? stage : 0] + (pk.block[i] & 0xFFFF) * 7;
    if (reg + 1 < 0x10000) seen[reg] = seen[reg + 1] = stamp;
  }
  for (uint32_t i = 0; i < L.cap.vertexBindCount; ++i) {
    const uint32_t reg = mmSQ_VTX_ATTRIBUTE_BLOCK_START + L.cap.vertexBinds[i].binding * 7;
    seen[reg] = seen[reg + 1] = stamp;
  }
  seen[mmSQ_PGM_START_FS] = seen[mmSQ_PGM_START_FS + 1] = stamp;
  for (uint64_t p = L.journalPos; p < g_journalPos; ++p) {
    const RegChange& c = g_journal[p % kJournal];
    if (seen[c.reg] == stamp) continue;
    seen[c.reg] = stamp;
    if (r[c.reg] != c.old) return false;
  }
  L.epoch = g_epoch;
  L.journalBumps = g_journalBumps;
  L.journalPos = g_journalPos;
  return true;
}

// the lowering draw's final support bytes with the instance blocks' kcache entries (as pack_uniforms_into reads them)
// and the instance register ranges (from the registers R) of now
void packet_support_bytes(const PacketCapture::Stage& st, const uint32_t* blockAddr, const uint32_t* blockSize,
                          const uint32_t* r, std::vector<uint8_t>& bytes) {
  bytes = st.supBytes;
  for (int k = 0; k < PacketCapture::kBlocks; ++k) {
    if (st.block[k].remap.empty()) continue;
    const uint32_t addr = blockAddr[k], size = blockSize[k];
    const uint8_t* submitted = addr ? gx2::submitted_uniform_block(addr, size) : nullptr;
    const uint32_t blockBytes = std::min<uint32_t>(size, 0x10000);
    for (const auto& [dst, src] : st.block[k].remap) {
      const void* from = submitted && src + 16 <= blockBytes ? static_cast<const void*>(submitted + src) : ppc_ptr(addr + src);
      if (size_t(dst) + 16 <= bytes.size()) std::memcpy(bytes.data() + dst, from, 16);
    }
  }
  for (const auto& p : st.regRemap)
    if (size_t(p.dst) + size_t(p.words) * 4 <= bytes.size()) std::memcpy(bytes.data() + p.dst, r + p.reg, p.words * 4);
}

void packet_compare(const PacketCapture& a, const PacketCapture& b, bool sameVertexInput, bool sameRange, uint32_t site) {
  char what[200];
  auto check = [&](bool same, const char* field) {
    if (same) return;
    std::snprintf(what, sizeof what, "site %u: %s differs between the packet and the full draw (frame %llu)", site,
                  field, (unsigned long long)R.frame);
    native_draw::verify_mismatch(what);
  };
  check(a.pipeline == b.pipeline && a.layout == b.layout && a.vkPipeline == b.vkPipeline, "pipeline");
  check(a.uniformHash[0] == b.uniformHash[0], "vertex uniforms");
  check(a.uniformHash[1] == b.uniformHash[1], "pixel uniforms");
  check(a.textureHash == b.textureHash, "textures");
  // (an instance with another index range copies another prefix of the same vertex buffers: their addresses and size
  // words are the same (sameVertexInput), the content hash covers the copied prefix only)
  check(a.vertexLayoutHash == b.vertexLayoutHash && (!sameVertexInput || !sameRange || a.vertexHash == b.vertexHash),
        "vertex input");
  check((!sameRange || (a.indexHash == b.indexHash && a.drawCount == b.drawCount)) && a.indexType == b.indexType,
        "index input");
  bool layout = a.offsetCount == b.offsetCount;
  for (int s = 0; s < 2; ++s)
    for (int k = 0; k < PacketCapture::kBlocks; ++k)
      layout = layout && a.stage[s].block[k].index == b.stage[s].block[k].index;
  check(layout, "uniform layout");
  check(a.colors == b.colors && a.depth == b.depth, "targets");
}

// an instance block's bytes, as bind_stage's uniform() takes them: as submitted with the display list, else guest
// memory (through the buffer cache when it is on)
UploadSlice packet_block_upload(uint32_t addr, uint32_t size) {
  if (size > R.properties.limits.maxUniformBufferRange) exception_report::raise("uniform buffer exceeds device range");
  UploadSlice b;
  rprof::UploadKind uploads(rprof::kUpUbo);
  const uint8_t* submitted = addr ? gx2::submitted_uniform_block(addr, size) : nullptr;
  if (submitted) b = snapshot(submitted, size, R.properties.limits.minUniformBufferOffsetAlignment);
  else if (!addr || !buffer_cache_enabled() || !cached_guest_range(addr, size, rprof::kUpUbo, b))
    b = snapshot(addr ? mem::ptr(addr) : nullptr, size, R.properties.limits.minUniformBufferOffsetAlignment);
  if (addr) rprof::guest_read(rprof::kUpUbo, addr, size);
  return b;
}
}  // namespace

void packet_draw(const uint32_t* r, const native_draw::Packet& pk, uint32_t kind, const native_draw::Mesh* range) {
  // a shader-rewrite batch of another packet or kind is drawn first (this instance then lowers or chains after it)
  if (pendingBatch.active && (pendingBatch.id != pk.id || pendingBatch.kind != kind)) native_draw::flush_batch();
  auto& LP = loweredPackets[pk.id % native_draw::kMaxPackets];
  if (!LP) LP = std::make_unique<LoweredPacket>();
  auto& P = *LP;
  auto& L = P.kinds[kind % native_draw::kMaxKinds];
  const native_draw::Mesh& m = range ? *range : pk.mesh[kind % native_draw::kMaxKinds];
  // the instance blocks' registers now (their effects were applied): address and size per declared block
  uint32_t blockAddr[native_draw::kMaxBlocks]{}, blockSize[native_draw::kMaxBlocks]{};
  static const uint32_t stageBase[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START,
                                        mmSQ_GS_UNIFORM_BLOCK_START};
  for (uint32_t i = 0; i < pk.blockCount && i < native_draw::kMaxBlocks; ++i) {
    const uint32_t stage = pk.block[i] >> 16, reg = stageBase[stage < 3 ? stage : 0] + (pk.block[i] & 0xFFFF) * 7;
    blockAddr[i] = r[reg];
    blockSize[i] = r[reg + 1] + 1;
  }
  if (L.lastSeen)
    for (uint32_t i = 0; i < pk.blockCount && i < native_draw::kMaxBlocks; ++i)
      if (blockAddr[i] != L.lastBlockAddr[i] || blockSize[i] != L.lastBlockSize[i]) L.blocksVary = true;
  std::memcpy(L.lastBlockAddr, blockAddr, sizeof blockAddr);
  std::memcpy(L.lastBlockSize, blockSize, sizeof blockSize);
  L.lastSeen = true;
  // a multi-draw batch this instance cannot join is drawn first (another packet or kind, other blocks; a range batch:
  // other vertex buffers)
  if (mdiBatch.active) {
    bool join = mdiBatch.id == pk.id && mdiBatch.kind == kind;
    for (uint32_t i = 0; join && i < pk.blockCount; ++i)
      join = blockAddr[i] == mdiBatch.blockAddr[i] && blockSize[i] == mdiBatch.blockSize[i];
    for (uint32_t i = 0; join && mdiBatch.range && i < L.cap.vertexBindCount && i < 16; ++i) {
      const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + L.cap.vertexBinds[i].binding * 7;
      join = a[0] == mdiBatch.vertexRegs[i][0] && a[1] == mdiBatch.vertexRegs[i][1];
    }
    if (!join) native_draw::flush_mdi();
  }
  bool replayable = L.valid && P.drawCount == R.drawCount &&
      L.submission == R.submissionGeneration && L.frame == R.frame && L.passCount == R.renderPassCount &&
      R.rendering && R.passTracked && g_surface_epoch == L.surfaceEpoch && R.recording && L.cmd == R.cmd;
  // the register file: unchanged (the epoch), or every journalled change undone
  bool restored = false;
  if (replayable && L.epoch != native_draw::g_epoch) {
    replayable = restored = packet_regs_restored(L, pk, r);
    if (restored) native_draw::count(pk.site, native_draw::kRestored);
  }
  native_draw::Reason relower = native_draw::kReasons;  // (none of the counted instance reasons)
  if (replayable)
    for (uint32_t i = 0; i < pk.blockCount; ++i)
      if (blockSize[i] != L.blockSize[i]) replayable = false, relower = native_draw::kBlockSize;
  // the vertex input: the kind's own (static mesh), or a stream of the instance's own with the same fetch content
  // and strides
  bool sameVertexInput = true;
  if (replayable && (r[mmSQ_PGM_START_FS] != L.fsRegs[0] || r[mmSQ_PGM_START_FS + 1] != L.fsRegs[1])) {
    const uint32_t fsAddr = r[mmSQ_PGM_START_FS] << 8, fsSize = r[mmSQ_PGM_START_FS + 1] << 3;
    if (fsSize != L.fsBytes.size() || !fsAddr || std::memcmp(mem::ptr(fsAddr), L.fsBytes.data(), fsSize))
      replayable = false, relower = native_draw::kFetchChange;
  }
  // the checked registers (the site's own texture, sampler, blend calls in between): as lowered
  for (uint32_t i = 0, at = 0; replayable && i < pk.checkCount && i < native_draw::kMaxChecks; ++i) {
    if (!native_draw::check_compare(pk.check[i])) continue;
    const uint32_t first = native_draw::check_first(pk.check[i]), n = native_draw::check_count(pk.check[i]);
    if (at + n > L.checkWords.size() || std::memcmp(r + first, L.checkWords.data() + at, n * 4))
      replayable = false, relower = native_draw::kCheckChange;
    at += n;
  }
  for (uint32_t i = 0; replayable && i < L.cap.vertexBindCount; ++i) {
    const auto& v = L.cap.vertexBinds[i];
    const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
    if (a[2] != v.regs[2]) replayable = false, relower = native_draw::kStrideChange;
    else if (a[0] != v.regs[0] || a[1] != v.regs[1]) sameVertexInput = false;
  }
  if (replayable) {
    // guest writes to the packet's textures (write tracking, GX2Invalidate) upload here, as for a continued draw;
    // an upload ends the packet
    for (const auto& stage : L.stages)
      for (uint32_t i = 0; i < stage.count; ++i) upload_surface(stage.textures[i].surface);
    replayable = g_surface_epoch == L.surfaceEpoch;
  }
  const uint32_t verifyEvery = native_draw::verify_every();
  const bool verify = replayable && verifyEvery && ++packetVerifyCounter % verifyEvery == 0;
  // the index range: the one the kind was lowered with, or (range batches: every instance of a kind lowered with a
  // range of its own) another one, with the kind's vertex input or a stream of the instance's own (a range batch keeps
  // one vertex input: its first instance's)
  const bool sameRange = m.count == L.rangeCount && m.indexAddr == L.rangeAddr;
  if (replayable && !sameRange) L.rangeVaries = true;
  const bool rangeCandidate = native_draw::mdi_on() && replayable && !verify && !L.rangeRefused && m.indexAddr &&
      (!sameRange || L.rangeVaries) &&
      (L.cap.nativeIndexWidth || uint64_t(m.indexAddr) + uint64_t(m.count) * (m.indexType == 4 ? 2 : 4) <= 0x100000000ull);
  if (replayable && !verify && !sameRange && !rangeCandidate) replayable = false, relower = native_draw::kRangeChange;
  const uint64_t surfaceBefore = g_surface_epoch;
  auto chained = [&] {  // after any draw of this packet: the kinds' surface epoch moves with the packet's own writes
    P.drawCount = R.drawCount;
    for (auto& k : P.kinds)
      if (k.valid && k.surfaceEpoch == surfaceBefore) k.surfaceEpoch = g_surface_epoch;
  };
  // multi-draw batches: an instance with a stream of its own joins the batch of its kind (same blocks: checked above)
  const bool mdiCandidate = native_draw::mdi_on() && replayable && !verify && !sameVertexInput && sameRange &&
      !L.mdiRefused && !rangeCandidate;
  auto mdi_append = [&] {
    auto& B = mdiBatch;
    const auto& v = L.cap.vertexBinds[0];
    const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
    const uint32_t addr = a[0], size = a[1] + 1, stride = v.stride;
    const size_t at = B.bytes.size();
    const size_t padded = (size_t(size) + stride - 1) / stride * stride;
    B.bytes.resize(at + padded);
    std::memcpy(B.bytes.data() + at, mem::ptr(addr), size);
    if (padded > size) std::memset(B.bytes.data() + at + size, 0, padded - size);
    rprof::guest_read(rprof::kUpVertex, addr, size);
    B.cmds.push_back({L.cap.drawCount, 1, 0, int32_t(at / stride), 0});
    ++B.count;
    native_draw::g_mdiPending = true;
  };
  // a range batch's instance: its index bytes, and each vertex buffer's copy grown to the prefix its full draw needs
  // (native: the guest's index bytes; else the uint32 stream of the full draw's conversion, vk::convert_indices)
  auto range_append = [&] {
    auto& B = mdiBatch;
    const uint32_t width = L.cap.nativeIndexWidth ? L.cap.nativeIndexWidth : 4;
    const size_t at = B.indexBytes.size();
    size_t count = m.count;
    {
      rprof::UploadKind uploads(rprof::kUpIndex);
      if (L.cap.nativeIndexWidth) {
        B.indexBytes.resize(at + count * width);
        std::memcpy(B.indexBytes.data() + at, mem::ptr(m.indexAddr), count * width);
        rprof::guest_read(rprof::kUpIndex, m.indexAddr, count * width);
      } else {
        static thread_local std::vector<uint32_t> converted;
        vk::convert_indices(mem::ptr(m.indexAddr), m.prim, m.count, m.indexType, L.cap.stripRestart,
                            L.cap.restartIndex, converted);
        count = converted.size();
        B.indexBytes.resize(at + count * 4);
        std::memcpy(B.indexBytes.data() + at, converted.data(), count * 4);
        rprof::guest_read(rprof::kUpIndex, m.indexAddr, uint64_t(m.count) * (m.indexType == 4 || m.indexType == 0 ? 2 : 4));
      }
    }
    const VertexExtent e = indexed_vertex_extent(B.indexBytes.data() + at, count, width, L.cap.hostRestart, 0);
    if (!e.valid) B.extentValid = false;
    else B.maxIndex = std::max(B.maxIndex, e.maximum);
    for (uint32_t i = 0; i < L.cap.vertexBindCount && i < B.vertexBytes.size(); ++i) {
      const auto& v = L.cap.vertexBinds[i];
      const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
      const uint32_t addr = a[0], size = a[1] + 1;
      const uint32_t copied = v.supported ? vertex_prefix_size(size, v.stride, v.attributeEnd,
                                                               {B.extentValid, B.maxIndex}) : size;
      auto& out = B.vertexBytes[i];
      if (copied <= out.size()) continue;
      const size_t from = out.size();
      out.resize(copied);
      std::memcpy(out.data() + from, static_cast<const uint8_t*>(mem::ptr(addr)) + from, copied - from);
      rprof::guest_read(rprof::kUpVertex, addr + uint32_t(from), copied - from);
    }
    B.cmds.push_back({uint32_t(count), 1, uint32_t(at / width), 0, 0});
    ++B.count;
    native_draw::g_mdiPending = true;
  };
  if (mdiBatch.active && (mdiBatch.range ? rangeCandidate : mdiCandidate)) {  // (same packet, kind and blocks)
    if (mdiBatch.range) range_append();
    else mdi_append();
    return;
  }
  if (mdiBatch.active) native_draw::flush_mdi();  // (this kind's batch, before the instance drawn otherwise)
  // shader-rewrite batches: the instance's entries join the kind's instanced draw (drawn by packet_flush)
  // an instance entry's bytes, as pack_uniforms_into reads them into the support uniforms: the block as submitted,
  // else guest memory (a block without an address: zero)
  auto entry_bytes = [&](const NativeEntry& e, uint8_t* out) {
    const uint32_t addr = blockAddr[e.block];
    if (!addr) { std::memset(out, 0, 16); return; }
    const uint32_t blockBytes = std::min<uint32_t>(blockSize[e.block], 0x10000);
    const uint8_t* submitted = gx2::submitted_uniform_block(addr, blockBytes);
    const void* from = submitted && e.src + 16 <= blockBytes ? static_cast<const void*>(submitted + e.src)
                                                             : ppc_ptr(addr + e.src);
    std::memcpy(out, from, 16);
    rprof::guest_read(rprof::kUpUbo, addr + e.src, 16);
  };
  auto append_instance = [&](bool stream) {
    auto& B = pendingBatch;
    if (!B.active) {
      B.active = true;
      B.stream = stream;
      B.binding = L.cap.vertexBinds[0].binding;
      B.bytes.clear();
      B.cmds.clear();
      B.id = pk.id;
      B.kind = kind;
      B.site = pk.site;
      B.count = 0;
      B.packet = &P;
      B.lowered = &L;
      B.cmd = R.cmd;
      B.submission = R.submissionGeneration;
      B.frame = R.frame;
      B.passCount = R.renderPassCount;
      B.data.clear();
    }
    const size_t at = B.data.size();
    B.data.resize(at + L.batch->entries.size() * 16);
    uint8_t* out = B.data.data() + at;
    for (const auto& e : L.batch->entries) {
      entry_bytes(e, out);
      out += 16;
    }
    if (B.stream) {  // the instance's stream, as a multi-draw batch copies it (mdi_append)
      const auto& v = L.cap.vertexBinds[0];
      const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
      const uint32_t addr = a[0], size = a[1] + 1, stride = v.stride;
      const size_t vat = B.bytes.size();
      const size_t padded = (size_t(size) + stride - 1) / stride * stride;
      B.bytes.resize(vat + padded);
      std::memcpy(B.bytes.data() + vat, mem::ptr(addr), size);
      if (padded > size) std::memset(B.bytes.data() + vat + size, 0, padded - size);
      rprof::guest_read(rprof::kUpVertex, addr, size);
      B.cmds.push_back({L.cap.drawCount, 1, 0, int32_t(vat / stride), B.count});
    }
    ++B.count;
    native_draw::g_batchPending = true;
  };
  // which batch the instance joins (exact batches come first: an instance a multi-draw or range batch took is gone):
  // a stream batch for a kind whose blocks vary (no multi-draw batch takes more than one object's instances) and whose
  // capture a multi-draw batch could draw (one vertex buffer, the vertex index unused); else a static batch for an
  // instance with the kind's own vertex input that no multi-draw batch takes. Both need the kind's own index range
  // and no range batch.
  const bool batchable = replayable && !verify && L.batch && L.batchPipeline && sameRange && !rangeCandidate;
  const bool asStream = batchable && L.blocksVary && !L.streamRefused &&
                        (!sameVertexInput || (pendingBatch.active && pendingBatch.stream));
  const bool asStatic = !asStream && batchable && sameVertexInput && !mdiCandidate;
  if (pendingBatch.active && (pendingBatch.stream ? !asStream : !asStatic)) native_draw::flush_batch();
  if (asStream || asStatic) {
    append_instance(asStream);
    return;
  }
  if (pendingBatch.active) native_draw::flush_batch();  // (this kind's batch, before the instance drawn otherwise)
  // the instance replayed with its kind's lowered state (P1): false when a block landed in another upload buffer
  auto replay_instance = [&]() -> bool {
    UploadSlice blocks[native_draw::kMaxBlocks];
    for (uint32_t i = 0; i < pk.blockCount; ++i) blocks[i] = packet_block_upload(blockAddr[i], blockSize[i]);
    auto offsets = L.cap.offsets;
    VkDescriptorSet sets[2] = {L.cap.sets[0], L.cap.sets[1]};
    bool same = true;
    for (int s = 0; s < 2; ++s) {
      const auto& st = L.cap.stage[s];
      const bool patchSupport = st.patches();
      bool any = patchSupport;
      for (const auto& blk : st.block) any = any || blk.binding >= 0;
      if (!any) continue;
      UploadSlice sup{};
      if (patchSupport) {  // the support bytes with the instance blocks' kcache entries and register values
        static thread_local std::vector<uint8_t> bytes;
        packet_support_bytes(st, blockAddr, blockSize, r, bytes);
        rprof::UploadKind uploads(rprof::kUpUniformVars);
        sup = snapshot(bytes.data(), bytes.size(), R.properties.limits.minUniformBufferOffsetAlignment);
      }
      if (st.dynamic) {  // the same set, the blocks' offsets
        for (int k = 0; k < PacketCapture::kBlocks; ++k) {
          const auto& blk = st.block[k];
          if (blk.binding < 0) continue;
          same = same && blk.index >= 0 && blocks[k].buffer == blk.buffer && blocks[k].offset <= UINT32_MAX;
          if (blk.index >= 0) offsets[blk.index] = uint32_t(blocks[k].offset);
        }
        if (patchSupport) {
          same = same && st.supIndex >= 0 && sup.buffer == st.supBuffer && sup.offset <= UINT32_MAX;
          if (st.supIndex >= 0) offsets[st.supIndex] = uint32_t(sup.offset);
        }
      } else {  // the stage's writes with the blocks' slices: a set of its own (reused within the submission)
        static thread_local std::array<VkWriteDescriptorSet, 17 + LATTE_NUM_MAX_TEX_UNITS> writes;
        static thread_local std::array<VkDescriptorBufferInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> buffers;
        static thread_local std::array<VkDescriptorImageInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> images;
        for (uint32_t i = 0; i < st.writeCount; ++i) {
          writes[i] = st.writes[i];
          if (writes[i].pBufferInfo) { buffers[i] = st.bufferInfos[i]; writes[i].pBufferInfo = &buffers[i]; }
          if (writes[i].pImageInfo) { images[i] = st.imageInfos[i]; writes[i].pImageInfo = &images[i]; }
        }
        for (int k = 0; k < PacketCapture::kBlocks; ++k)
          if (st.block[k].binding >= 0) buffers[st.block[k].instWrite] = {blocks[k].buffer, blocks[k].offset, blocks[k].size};
        if (patchSupport) buffers[st.supWrite] = {sup.buffer, sup.offset, sup.size};
        sets[s] = stage_descriptor_set(s == 0, st.layout, writes.data(), st.writeCount);
      }
    }
    if (same && (mdiCandidate || rangeCandidate)) {  // a new batch, drawn with this instance's descriptor sets and offsets
      auto& B = mdiBatch;
      B.active = true;
      B.range = rangeCandidate;
      B.id = pk.id;
      B.kind = kind;
      B.site = pk.site;
      B.count = 0;
      B.binding = L.cap.vertexBinds[0].binding;
      B.packet = &P;
      B.lowered = &L;
      B.cmd = R.cmd;
      B.submission = R.submissionGeneration;
      B.frame = R.frame;
      B.passCount = R.renderPassCount;
      for (uint32_t i = 0; i < native_draw::kMaxBlocks; ++i) B.blockAddr[i] = blockAddr[i], B.blockSize[i] = blockSize[i];
      B.sets[0] = sets[0];
      B.sets[1] = sets[1];
      B.offsets = offsets;
      B.bytes.clear();
      B.cmds.clear();
      B.indexBytes.clear();
      for (uint32_t i = 0; i < L.cap.vertexBindCount && i < 16; ++i) {
        const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + L.cap.vertexBinds[i].binding * 7;
        B.vertexRegs[i][0] = a[0];
        B.vertexRegs[i][1] = a[1];
      }
      B.extentValid = true;
      B.maxIndex = 0;
      for (auto& v : B.vertexBytes) v.clear();
      if (B.range) range_append();
      else mdi_append();
      return true;
    }
    if (same) {
      auto cmd = command_buffer();
      const bool kindChange = P.boundKind != int(kind);
      if (kindChange) {
        record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, L.cap.vkPipeline);
        record::vkCmdBindIndexBuffer(cmd, L.cap.indexBuffer, L.cap.indexOffset, L.cap.indexType);
      }
      for (uint32_t i = 0; i < L.cap.vertexBindCount; ++i) {
        const auto& v = L.cap.vertexBinds[i];
        const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
        if (a[0] == v.regs[0] && a[1] == v.regs[1]) {  // the kind's own buffer
          if (kindChange || !sameVertexInput) record::vkCmdBindVertexBuffers(cmd, v.binding, 1, &v.buffer, &v.offset);
          continue;
        }
        // the instance's stream: its whole declared buffer, through the buffer cache when it is on
        const uint32_t addr = a[0], size = a[1] + 1;
        UploadSlice b;
        {
          rprof::UploadKind uploads(rprof::kUpVertex);
          if (!buffer_cache_enabled() || !cached_guest_range(addr, size, rprof::kUpVertex, b))
            b = snapshot(mem::ptr(addr), size, 4);
          rprof::guest_read(rprof::kUpVertex, addr, size);
        }
        record::vkCmdBindVertexBuffers(cmd, v.binding, 1, &b.buffer, &b.offset);
      }
      record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, L.cap.layout, 0, 2, sets,
                                      L.cap.offsetCount, offsets.data());
      record::vkCmdDrawIndexed(cmd, L.cap.drawCount, 1, 0, 0, 0);
      P.boundKind = sameVertexInput ? int(kind) : -1;  // (a stream bound: the next instance rebinds)
      P.mdiKind = -1;
      g_packetBindsDirty = true;
      if (R.gpuPassTimestampsEnabled) gpu_count_render_draw();
      for (auto* c : L.cap.colors)
        if (c) mark_draw_target_written(c);
      if (L.cap.depth) mark_draw_target_written(L.cap.depth);
      R.drawCount++;
      chained();  // (a first write of a target counts as an upload epoch)
      native_draw::count(pk.site, native_draw::kReplayed);
      // draw batching (WWHD_VK_DRAW_BATCH, Android's default): counted and submitted as after a full draw (the new
      // submission then lowers the packet again)
      static const uint32_t drawBatch = parse_draw_batch(std::getenv("WWHD_VK_DRAW_BATCH"));
      static const uint32_t drawBatchCap = parse_draw_batch_cap(std::getenv("WWHD_VK_DRAW_BATCH_CAP"));
      if (drawBatchState.after_draw(R.frame, drawBatch, drawBatchCap)) {
        flush_async();
        ++drawBatchSubmissions;
      }
      return true;
    }
    return false;
  };
  if (replayable && !verify) {
    if (replay_instance()) return;
    relower = native_draw::kBufferChange;  // a block landed in another upload buffer: lowered again
  }
  if (relower != native_draw::kReasons && !verify) native_draw::count(pk.site, relower);
  // the full draw, which lowers the packet's kind
  PacketCapture cap;
  for (uint32_t i = 0; i < pk.blockCount && i < native_draw::kMaxBlocks; ++i) {
    const uint32_t stage = pk.block[i] >> 16;
    if (stage >= 2) { cap.refuse("geometry block"); continue; }
    cap.slot[stage][i] = int(pk.block[i] & 0xFFFF);
  }
  cap.hashes = verifyEvery != 0;
  cap.packet = &pk;
  const uint32_t fsRegs[2] = {r[mmSQ_PGM_START_FS], r[mmSQ_PGM_START_FS + 1]};
  g_packetCapture = &cap;
  try {
    draw_stereo(r, m.prim, m.count, m.indexType, m.indexAddr, 0, 1);  // (stereo is off: the plain draw)
  } catch (...) { g_packetCapture = nullptr; L.valid = false; P.boundKind = -1; throw; }
  g_packetCapture = nullptr;
  P.boundKind = -1;  // (the full draw bound through its own cache)
  P.mdiKind = -1;
  if (verify && cap.drew) {
    packet_compare(L.cap, cap, sameVertexInput, sameRange, pk.site);
    if (L.batch)  // batching: the entries the instance would have joined its batch with
      for (const auto& e : L.batch->entries) {
        uint8_t bytes[16];
        entry_bytes(e, bytes);
        const auto& sup = cap.stage[e.stage].supBytes;
        if (size_t(e.dst) + 16 > sup.size() || std::memcmp(sup.data() + e.dst, bytes, 16))
          native_draw::verify_mismatch("batch instance entry differs from the full draw's support uniforms");
      }
    for (int s = 0; s < 2; ++s)  // the patched support bytes against the full draw's packing
      if (L.cap.stage[s].patches()) {
        static thread_local std::vector<uint8_t> patched;
        packet_support_bytes(L.cap.stage[s], blockAddr, blockSize, r, patched);
        if (patched != cap.stage[s].supBytes) {
          char what[160];
          std::snprintf(what, sizeof what, "site %u: %s support uniforms differ between the packet and the full draw (frame %llu)",
                        pk.site, s ? "pixel" : "vertex", (unsigned long long)R.frame);
          native_draw::verify_mismatch(what);
        }
      }
    native_draw::count(pk.site, native_draw::kVerified);
  }
  const auto& c = continuedEyes[0];
  const bool chain = P.drawCount == R.drawCount - 1;  // (the full draw counted itself)
  L.valid = cap.drew && !cap.refused && c.valid && c.pipeline && cap.pipeline && cap.vkPipeline;
  if (!L.valid) {
    static std::unordered_set<std::string> said;  // each reason once per packet id
    if ((cap.drew || cap.refused) && said.size() < 64 &&
        said.insert(std::to_string(pk.id) + (cap.refused ? cap.refused : !c.valid ? "state" : "pipeline")).second) {
      LOG("[native-draw] packet %u (site %u pass %u variant %u kind %u) not lowered: %s", pk.id, pk.site, pk.pass,
          pk.variant, kind, cap.refused ? cap.refused : !c.valid ? "the draw's state cannot continue (AO, feedback or shared texture)"
                                                     : "no pipeline");
    }
    native_draw::count(pk.site, verify ? native_draw::kVerified : native_draw::kRefused);
    L.batch = nullptr;
    L.batchPipeline = nullptr;
    if (cap.drew) chained();
    return;
  }
  // a new chain (another draw came in between): the other kinds' lowered states end
  if (!chain)
    for (auto& k : P.kinds) k.valid = false;
  L.valid = true;
  L.epoch = native_draw::g_epoch;
  L.journalPos = native_draw::g_journalPos;
  L.journalBumps = native_draw::g_journalBumps;
  L.submission = R.submissionGeneration;
  L.frame = R.frame;
  L.surfaceEpoch = g_surface_epoch;
  L.passCount = R.renderPassCount;
  L.cmd = R.cmd;
  L.cap = std::move(cap);
  L.stages[0] = c.stages[0];
  L.stages[1] = c.stages[1];
  L.fsRegs[0] = fsRegs[0];
  L.fsRegs[1] = fsRegs[1];
  const uint32_t fsAddr = fsRegs[0] << 8, fsSize = fsRegs[1] << 3;
  if (fsAddr && fsSize <= 16 + 64 * 16) L.fsBytes.assign(mem::ptr(fsAddr), mem::ptr(fsAddr) + fsSize);
  else L.fsBytes.clear();
  for (uint32_t i = 0; i < native_draw::kMaxBlocks; ++i) L.blockSize[i] = blockSize[i];
  L.checkWords.clear();
  for (uint32_t i = 0; i < pk.checkCount && i < native_draw::kMaxChecks; ++i)
    if (native_draw::check_compare(pk.check[i]))
      L.checkWords.insert(L.checkWords.end(), r + native_draw::check_first(pk.check[i]),
                          r + native_draw::check_first(pk.check[i]) + native_draw::check_count(pk.check[i]));
  L.mdiRefused = native_draw::mdi_on() ? mdi_refusal(L.cap) : "off";
  L.streamRefused = mdi_refusal(L.cap);
  L.rangeCount = m.count;
  L.rangeAddr = m.indexAddr;
  L.rangeRefused = native_draw::mdi_on() && range ? range_refusal(L.cap) : "off";
  if (native_draw::mdi_on()) {
    static std::unordered_set<std::string> said;
    const char* why = range ? L.rangeRefused : L.mdiRefused;
    if (why && said.size() < 64 && said.insert(std::to_string(pk.id) + "/" + std::to_string(kind) + why).second)
      LOG("[native-draw] packet %u (site %u kind %u): no %s batches: %s", pk.id, pk.site, kind,
          range ? "range" : "multi-draw", why);
  }
  L.batch = nullptr;
  L.batchPipeline = nullptr;
  if (native_draw::batch_on() && L.cap.vs && L.cap.ps && L.cap.fs) {
    // shader-rewrite batches only for the sites whose kinds have the per-kind proof (native_draw.h: batch_site_on);
    // every other kind replays (or multi-draws) as before, the reason logged once per packet kind
    const char* why = native_draw::batch_site_on(pk.site) ? nullptr : "the site's kinds have no LOD-0 proof (not enabled)";
    NativeShaders* ns = why ? nullptr : native_shaders(L.cap, pk);
    if (ns && !ns->refused) {
      L.batchPipeline = native_pipeline(r, L.cap, *ns);
      if (L.batchPipeline) L.batch = ns;
    }
    static std::unordered_set<std::string> said;
    if (why && said.size() < 128 && said.insert(std::to_string(pk.id) + "/" + std::to_string(kind)).second)
      LOG("[native-draw] packet %u (site %u pass %u kind %u): no shader-rewrite batches: %s", pk.id, pk.site, pk.pass,
          kind, why);
  }
  chained();
  if (!verify) native_draw::count(pk.site, native_draw::kLowered);
}

// the collected multi-draw batch: the kind's pipeline and index buffer, the first instance's descriptor sets and
// offsets, the instances' streams back to back, one indirect command per instance
void packet_mdi_flush() {
  auto& B = mdiBatch;
  if (!B.active) return;
  B.active = false;
  if (!B.count) return;
  LoweredKind& L = *B.lowered;
  LoweredPacket& P = *B.packet;
  if (!(R.recording && R.rendering && R.cmd == B.cmd && R.submissionGeneration == B.submission && R.frame == B.frame &&
        R.renderPassCount == B.passCount && P.drawCount == R.drawCount)) {
    if (mdiLost++ < 16) LOG("[native-draw] MULTI-DRAW BATCH LOST: %u instances of packet %u (the command buffer moved on)", B.count, B.id);
    return;
  }
  auto cmd = command_buffer();
  // (a batch starts only while nothing came since the packet's last draw: after its own stream batch of this kind,
  // the pipeline and index buffer are still bound)
  if (P.mdiKind != int(B.kind)) {
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, L.cap.vkPipeline);
    if (!B.range) record::vkCmdBindIndexBuffer(cmd, L.cap.indexBuffer, L.cap.indexOffset, L.cap.indexType);
  }
  if (B.range) {  // the instances' index bytes; each vertex buffer's copy
    UploadSlice ix;
    {
      rprof::UploadKind uploads(rprof::kUpIndex);
      ix = snapshot(B.indexBytes.data(), B.indexBytes.size(), 4);
    }
    record::vkCmdBindIndexBuffer(cmd, ix.buffer, ix.offset, L.cap.indexType);
    for (uint32_t i = 0; i < L.cap.vertexBindCount && i < B.vertexBytes.size(); ++i) {
      UploadSlice v;
      {
        rprof::UploadKind uploads(rprof::kUpVertex);
        v = snapshot(B.vertexBytes[i].data(), B.vertexBytes[i].size(), 4);
      }
      record::vkCmdBindVertexBuffers(cmd, L.cap.vertexBinds[i].binding, 1, &v.buffer, &v.offset);
    }
  } else {
    UploadSlice v;
    {
      rprof::UploadKind uploads(rprof::kUpVertex);
      v = snapshot(B.bytes.data(), B.bytes.size(), 4);
    }
    record::vkCmdBindVertexBuffers(cmd, B.binding, 1, &v.buffer, &v.offset);
  }
  record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, L.cap.layout, 0, 2, B.sets, L.cap.offsetCount,
                                  B.offsets.data());
  static PFN_vkCmdDrawIndexedIndirect drawIndirect = nullptr;
  const bool mdiDevice = R.enabledFeatures.multiDrawIndirect;
  if (mdiDevice && !drawIndirect)
    drawIndirect = reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(vkGetDeviceProcAddr(R.device, "vkCmdDrawIndexedIndirect"));
  const bool indirect = B.count > 1 && mdiDevice && drawIndirect && !dispatch_recorder();
  if (indirect) {
    UploadSlice c = snapshot(B.cmds.data(), B.cmds.size() * sizeof(VkDrawIndexedIndirectCommand), 4);
    drawIndirect(cmd, c.buffer, c.offset, B.count, sizeof(VkDrawIndexedIndirectCommand));
  } else {
    for (const auto& c : B.cmds) record::vkCmdDrawIndexed(cmd, c.indexCount, 1, c.firstIndex, c.vertexOffset, 0);
  }
  native_draw::count_mdi(B.site, B.count, indirect);
  P.mdiKind = B.range ? -1 : int(B.kind);  // (a range batch bound an index buffer of its own)
  P.boundKind = -1;  // (the batch's stream is bound: the next replay rebinds)
  g_packetBindsDirty = true;
  static const uint32_t drawBatch = parse_draw_batch(std::getenv("WWHD_VK_DRAW_BATCH"));
  static const uint32_t drawBatchCap = parse_draw_batch_cap(std::getenv("WWHD_VK_DRAW_BATCH_CAP"));
  bool submit = false;
  for (uint32_t i = 0; i < B.count; ++i) {
    if (R.gpuPassTimestampsEnabled) gpu_count_render_draw();
    R.drawCount++;
    submit = drawBatchState.after_draw(R.frame, drawBatch, drawBatchCap) || submit;
  }
  const uint64_t surfaceBefore = g_surface_epoch;
  for (auto* c : L.cap.colors)
    if (c) mark_draw_target_written(c);
  if (L.cap.depth) mark_draw_target_written(L.cap.depth);
  P.drawCount = R.drawCount;
  for (auto& k : P.kinds)  // (as chained(): the packet's own target writes keep its kinds' surface epoch)
    if (k.valid && k.surfaceEpoch == surfaceBefore) k.surfaceEpoch = g_surface_epoch;
  if (submit) {
    flush_async();
    ++drawBatchSubmissions;
  }
}
// the collected shader-rewrite batch as one instanced draw: the kind's lowered state (descriptor sets with their
// offsets, index and vertex buffers, dynamic state), the batch pipeline and the instances' entries as a per-instance
// vertex stream
void packet_flush() {
  auto& B = pendingBatch;
  if (!B.active) return;
  B.active = false;
  if (!B.count) return;
  LoweredKind& L = *B.lowered;
  LoweredPacket& P = *B.packet;
  if (!(R.recording && R.rendering && R.cmd == B.cmd && R.submissionGeneration == B.submission && R.frame == B.frame &&
        R.renderPassCount == B.passCount && L.batchPipeline && L.batch)) {
    if (batchLost++ < 16) LOG("[native-draw] BATCH LOST: %u instances of packet %u (the command buffer moved on)", B.count, B.id);
    return;
  }
  UploadSlice inst, stream;
  {
    rprof::UploadKind uploads(rprof::kUpVertex);
    inst = snapshot(B.data.data(), B.data.size(), 16);
    if (B.stream) stream = snapshot(B.bytes.data(), B.bytes.size(), 4);
  }
  auto cmd = command_buffer();
  record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, L.batchPipeline->pipeline);
  record::vkCmdBindIndexBuffer(cmd, L.cap.indexBuffer, L.cap.indexOffset, L.cap.indexType);
  if (B.stream) record::vkCmdBindVertexBuffers(cmd, B.binding, 1, &stream.buffer, &stream.offset);
  else
    for (uint32_t i = 0; i < L.cap.vertexBindCount; ++i) {
      const auto& v = L.cap.vertexBinds[i];
      record::vkCmdBindVertexBuffers(cmd, v.binding, 1, &v.buffer, &v.offset);
    }
  record::vkCmdBindVertexBuffers(cmd, L.batch->input.binding, 1, &inst.buffer, &inst.offset);
  record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, L.batchPipeline->layout, 0, 2, L.cap.sets,
                                  L.cap.offsetCount, L.cap.offsets.data());
  uint32_t draws = 1;
  bool indirect = false;
  if (B.stream) {  // one indirect draw (the device features), else one vkCmdDrawIndexed per instance
    static PFN_vkCmdDrawIndexedIndirect drawIndirect = nullptr;
    const bool device = R.enabledFeatures.multiDrawIndirect && R.enabledFeatures.drawIndirectFirstInstance;
    if (device && !drawIndirect)
      drawIndirect = reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(vkGetDeviceProcAddr(R.device, "vkCmdDrawIndexedIndirect"));
    indirect = B.count > 1 && device && drawIndirect && !dispatch_recorder();
    if (indirect) {
      UploadSlice c = snapshot(B.cmds.data(), B.cmds.size() * sizeof(VkDrawIndexedIndirectCommand), 4);
      drawIndirect(cmd, c.buffer, c.offset, B.count, sizeof(VkDrawIndexedIndirectCommand));
    } else {
      for (const auto& c : B.cmds)
        record::vkCmdDrawIndexed(cmd, c.indexCount, 1, c.firstIndex, c.vertexOffset, c.firstInstance);
      draws = B.count;
    }
  } else {
    record::vkCmdDrawIndexed(cmd, L.cap.drawCount, B.count, 0, 0, 0);
  }
  P.boundKind = -1;
  P.mdiKind = -1;  // (the batch pipeline is bound: a multi-draw batch rebinds)
  g_packetBindsDirty = true;
  const uint64_t surfaceBefore = g_surface_epoch;
  for (auto* c : L.cap.colors)
    if (c) mark_draw_target_written(c);
  if (L.cap.depth) mark_draw_target_written(L.cap.depth);
  static const uint32_t drawBatch = parse_draw_batch(std::getenv("WWHD_VK_DRAW_BATCH"));
  static const uint32_t drawBatchCap = parse_draw_batch_cap(std::getenv("WWHD_VK_DRAW_BATCH_CAP"));
  bool submit = false;
  for (uint32_t i = 0; i < draws; ++i) {
    if (R.gpuPassTimestampsEnabled) gpu_count_render_draw();
    R.drawCount++;
    submit = drawBatchState.after_draw(R.frame, drawBatch, drawBatchCap) || submit;
  }
  P.drawCount = R.drawCount;  // (the packet's chain goes on after its batch)
  for (auto& k : P.kinds)
    if (k.valid && k.surfaceEpoch == surfaceBefore) k.surfaceEpoch = g_surface_epoch;
  native_draw::count_batch(B.site, B.count, B.stream, indirect);
  if (submit) {
    flush_async();
    ++drawBatchSubmissions;
  }
}
#endif
// Setup's Vulkan preparation (headstart.cpp, the head start's Metal pipeline recipes): the recipe of the pipeline a
// draw with these registers would make, by the live derivation: fetch shader, vertex and pixel translation, active
// targets, pipeline key and state as draw() finds them. colorFormats/depthFormat: GX2 surface formats of the targets
// (0: none). nullptr with *why when the state does not make a pipeline here.
std::shared_ptr<PipelineRecipe> derive_guest_pipeline_recipe(const uint32_t* r, const uint32_t colorFormats[8],
    uint32_t depthFormat, const char** why) {
  auto fail = [&](const char* reason) { *why = reason; return std::shared_ptr<PipelineRecipe>{}; };
  const uint32_t prim = r[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F;
  VkPrimitiveTopology topology;
  switch (prim) {
  case 1: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
  case 2: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
  case 3: case 0x12: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
  case 4: case 5: case 0x13: case 0x14: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
  case 6: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
  default: return fail("primitive");
  }
  uint64_t fsKey = 0;
  auto* fs = vk::get_fetch_shader(r, &fsKey, ~uint64_t{0});
  if (!fs) return fail("fetch shader");
  vk::Shader* vs = nullptr;
  vk::Shader* ps = nullptr;
  vk::translate_stages(r, fs, fsKey, ~uint64_t{0}, ~uint64_t{0}, vs, ps);
  if (!vs || !vs->ready()) return fail("vertex shader");
  if (!ps || !ps->ready()) return fail("pixel shader");
  const auto& lcr = *reinterpret_cast<const LatteContextRegister*>(r);
  const auto mask = LatteMRT::GetActiveColorBufferMask(ps->dec, lcr);
  static std::array<Surface, 9> stubs;
  std::array<Surface*, 8> colors{};
  for (int i = 0; i < 8; ++i) {
    if (!(mask & (1u << i))) continue;
    if (!colorFormats[i]) return fail("targets");  // the draw has a target the recipe does not name
    stubs[i] = Surface{};
    stubs[i].format = colorFormats[i]; stubs[i].fmt = format_info(colorFormats[i], false);
    colors[i] = &stubs[i];
  }
  Surface* depth = nullptr;
  if (LatteMRT::GetActiveDepthBufferMask(lcr) && depthFormat) {
    stubs[8] = Surface{};
    stubs[8].format = depthFormat; stubs[8].isDepth = true; stubs[8].fmt = format_info(depthFormat, true);
    depth = &stubs[8];
  }
  if (!colors[0] && !depth) {
    bool any = false;
    for (auto* c : colors) any |= c != nullptr;
    if (!any) return fail("targets");
  }
  std::vector<std::shared_ptr<PipelineRecipe>> sink;
  g_recipeSink = &sink;
  try { pipeline(r, vs, ps, fs, topology, colors, depth); }
  catch (...) { g_recipeSink = nullptr; return fail("pipeline state"); }
  g_recipeSink = nullptr;
  if (sink.empty()) return fail("shader binaries");
  return sink.front();
}
} // namespace gfxvk
