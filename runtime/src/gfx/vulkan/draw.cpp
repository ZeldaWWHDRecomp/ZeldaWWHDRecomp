#include "vertex_cpu_policy.h"
#include <set>
#include <map>
#include <tuple>
#include "pass.h"
#include "../../exception_report.h"
#include "aspect.h"
#include "aspect_panes.h"
#include "../renderer.h"
// Vulkan draw submission. Guest state conventions follow Cemu (MPL-2.0).
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "backend.h"
#include "stereo.h"
#include "../stereo_projection.h"
#include "sampler_filtering.h"
#include "deferred_dispatch.h"
#include "buffer_cache.h"
#include "gx2/state_groups.h"
#include "vertex_cpu_decode.h"
#include "render_prof.h"
#include "runtime.h"
#include "gx2/gx2.h"
#include "cloud_proto.h"
#include "shaders.h"
#include "stutter_trace.h"
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
void finish_draw_batch() {
  static const uint32_t size = parse_draw_batch(std::getenv("WWHD_VK_DRAW_BATCH"));
  static const uint32_t cap = parse_draw_batch_cap(std::getenv("WWHD_VK_DRAW_BATCH_CAP"));
  if (drawBatchState.after_draw(R.frame, size, cap)) {
    flush_async();
    ++drawBatchSubmissions;
  }
}
struct InstanceDraw {
  VkCommandBuffer command = VK_NULL_HANDLE;
  uint32_t count = 0, instances = 0, vertex = 0, firstInstance = 0;
  bool indexed = false;
} instanceDraw;
uint64_t arrayGuestDraws = 0, arrayNativeDraws = 0, arrayPreparedDraws = 0;
struct InstanceGeometry {
  struct Input { uint32_t address, bytes, kind; UploadSlice slice; bufcache::Entry* entry; };
  std::array<Input, 17> inputs{};
  uint32_t inputCount = 0, count = 0, indexType = 0, indexAddr = 0, baseVertex = 0;
  uint32_t nativeCount = 0;
  uint64_t submission = 0;
  bool valid = false;
  void add(uint32_t address, uint32_t bytes, uint32_t kind, UploadSlice slice, bufcache::Entry* entry) {
    if (inputCount == inputs.size()) { valid = false; return; }
    inputs[inputCount++] = {address, bytes, kind, slice, entry};
  }
} instanceGeometry;
void flush_instance_draw() {
  before_record_command = nullptr;
  const auto draw = std::exchange(instanceDraw, {});
  if (!draw.instances) return;
  ++arrayNativeDraws;
  if (draw.indexed)
    record::vkCmdDrawIndexed(draw.command, draw.count, draw.instances, 0, int32_t(draw.vertex), draw.firstInstance);
  else record::vkCmdDraw(draw.command, draw.count, draw.instances, draw.vertex, draw.firstInstance);
}
void append_instance_draw(VkCommandBuffer command, bool indexed, uint32_t count,
                          uint32_t vertex, uint32_t firstInstance) {
  ++arrayGuestDraws;
  if (instanceDraw.instances && instanceDraw.command == command && instanceDraw.indexed == indexed &&
      instanceDraw.count == count && instanceDraw.vertex == vertex &&
      instanceDraw.firstInstance + instanceDraw.instances == firstInstance) {
    ++instanceDraw.instances;
    return;
  }
  flush_instance_draw();
  instanceDraw = {command, count, 1, vertex, firstInstance, indexed};
  before_record_command = flush_instance_draw;
}
bool aoPrivateReplay = false;
struct AoPrivate {
  uint32_t source = 0;
  uint64_t frame = ~0ull;
  Surface color, depth;
};
std::array<AoPrivate, 2> aoPrivateEyes;  // stereo: each eye replays into its own images
AoPrivate& ao_private() { return aoPrivateEyes[stereoEye]; }
Surface* private_ao_surface(Surface& dst, const Surface* like) {
  const uint64_t logicalWidth = uint64_t(like->width) * 3 / 2;
  const uint64_t logicalHeight = uint64_t(like->height) * 3 / 2;
  const uint64_t physicalWidth = uint64_t(like->extent.width) * 3 / 2;
  const uint64_t physicalHeight = uint64_t(like->extent.height) * 3 / 2;
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
  VkDescriptorSetLayout resourceSets[2]{};
  bool dynamicUniforms = false;
  const LatteFetchShader* trimFetch = nullptr;
  const vk::Shader* trimVertex = nullptr;
  std::vector<BindingTrimMetadata> bindingTrims;
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
// Pipelines own their set layouts (prewarmed pipelines included). Descriptor
// sets are allocated from one layout per binding definition instead, created
// and owned here for the device's lifetime, independent of any pipeline: a set
// may be bound with any identically defined layout. The key names every
// binding field; these layouts have no flags or immutable samplers.
VkDescriptorSetLayout canonical_resource_layout(VkDescriptorSetLayout layout,
                                                const vk::Shader& shader, bool dynamic) {
  static const bool enabled = [] {
    const char* e = std::getenv("WWHD_VK_SHARED_STAGE_LAYOUTS");
    return !e || std::strcmp(e, "0");
  }();
  if (!enabled) return layout;
  using Key = std::vector<std::array<uint32_t, 4>>;
  static std::map<Key, VkDescriptorSetLayout> layouts;
  static VkDevice device = VK_NULL_HANDLE;
  if (device != R.device) { layouts.clear(); device = R.device; }
  Key key;
  auto add = [&](int binding, VkDescriptorType type) {
    if (binding >= 0) key.push_back({uint32_t(binding), uint32_t(type), 1,
        uint32_t(shader.vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT)});
  };
  const auto& m = shader.mapping;
  const auto uniformType = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  add(m.uniformVarsBufferBindingPoint, uniformType);
  for (int binding : m.uniformBuffersBindingPoint) add(binding, uniformType);
  for (int i = 0; i < m.textureUnitCount; ++i)
    add(m.textureUnitBaseBindingPoint + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  std::sort(key.begin(), key.end());
  auto [found, inserted] = layouts.try_emplace(std::move(key), VK_NULL_HANDLE);
  if (inserted) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& b : found->first)
      bindings.push_back({b[0], VkDescriptorType(b[1]), b[2], VkShaderStageFlags(b[3]), nullptr});
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = uint32_t(bindings.size());
    ci.pBindings = bindings.data();
    try { vk_check(vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &found->second), "shared stage descriptor layout"); }
    catch (...) { layouts.erase(found); throw; }
  }
  return found->second;
}
PipelineKey pipeline_key(const uint32_t* r, const vk::Shader* vs, const vk::Shader* ps,
                         const LatteFetchShader* fs, VkPrimitiveTopology topology,
                         const std::array<Surface*, 8>& colors, const Surface* depth) {
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
    if (colors[i] && colors[i]->fmt.kind == FormatInfo::FLOAT && ((colorControl >> (8 + i)) & 1)) {
      blending |= 1u << i;
      // without SEPARATE_ALPHA_BLEND the alpha factors are the color ones
      const uint32_t raw = r[REGADDR::CB_BLEND0_CONTROL + i];
      key.blend[i] = raw & (raw & (1u << 29) ? 0x3FFF1FFFu : 0x00001FFFu);
    }
  key.colorControl = (colorControl & 0x00FF0000u) | (blending << 8);  // ROP, blend enables
  key.targetMask = ncolor >= 8 ? r[REGADDR::CB_TARGET_MASK]
                               : r[REGADDR::CB_TARGET_MASK] & ((1u << (4 * ncolor)) - 1);
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
  key.clip = r[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27);  // depth clamp
  if (fs->bufferGroups.size() > kMaxPipelineStrides)
    exception_report::raise("fetch shader has more vertex buffers than a pipeline key holds");
  for (auto& g : fs->bufferGroups)
    key.strides[key.strideCount++] =
        (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
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
Pipeline& resolve_pending_pipeline(Pipeline& pipeline, bool wait = true, bool present = false) {
  if (pipeline.compileError) std::rethrow_exception(pipeline.compileError);
  if (!pipeline.pendingCompile) return pipeline;
  try {
    auto& pending = *pipeline.pendingCompile;
    std::optional<PipelineBuildResult> built = pending.ticket->wait_for(0);
    if (!built && !wait) return pipeline;
    if (!built) {
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
    // Publish the consumed result before fallible recipe bookkeeping. Never
    // retain a consumed ticket, even when recording or driver validation throws.
    pipeline.pendingCompile.reset();
    R.pipelineCacheDirty = true;
    R.pipelineCacheChangedFrame = R.frame;
    remember_pipeline_recipe(std::move(recipe));
    return pipeline;
  } catch (...) {
    if (!pipeline.pipeline) pipeline.compileError = std::current_exception();
    pipeline.pendingCompile.reset();
    throw;
  }
}
Pipeline &pipeline(const uint32_t *r, vk::Shader *vs, vk::Shader *ps,
                   LatteFetchShader *fs, VkPrimitiveTopology topology,
                   const std::array<Surface *, 8> &colors, Surface *depth) {
  if(preparation_stats_enabled())++R.cpuPreparation.pipelineLookups;
  const PipelineKey key = pipeline_key(r, vs, ps, fs, topology, colors, depth);
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
    if (!value.resourceSets[0]) {
      value.resourceSets[0] = canonical_resource_layout(value.sets[0], *vs, value.dynamicUniforms);
      value.resourceSets[1] = canonical_resource_layout(value.sets[1], *ps, value.dynamicUniforms);
    }
    lastPipelineLookup = {key, R.device, &value};
    if (useLookaside) pipelineLookaside.remember(lastPipelineLookup);
    return value;
  };
  if(preparation_stats_enabled())++R.cpuPreparation.pipelineMapLookups;
  if (auto it = pipelines.find(key); it != pipelines.end())
    return remember(it->second);
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
  // Own prerequisite identities independently of the mutable shader cache.
  const std::array<std::shared_ptr<vk::ShaderBinary>, 2> shaderBinaries{vs->binary, ps->binary};
  std::array<std::vector<uint32_t>, 2> recipeSpirv{vs->spirv(), ps->spirv()};
  for(size_t i=0;i<shaderBinaries.size();++i)
    if(recipeSpirv[i].empty() && shaderBinaries[i]) {
      if(!async_pipeline_enabled()) recipeSpirv[i]=vk::shader_binary_words_exact(shaderBinaries[i],R.frame);
      else if(vk::shader_binary_ready(shaderBinaries[i])) recipeSpirv[i]=vk::shader_binary_words(shaderBinaries[i]);
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
    b.colorWriteMask = (r[REGADDR::CB_TARGET_MASK] >> (4 * i)) & 15;
    b.blendEnable = colors[i] && colors[i]->fmt.kind == FormatInfo::FLOAT &&
                    ((r[REGADDR::CB_COLOR_CONTROL] >> (8 + i)) & 1);
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
  if (!recipeSpirv[0].empty() && !recipeSpirv[1].empty() &&
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
    pending->recipe = std::move(recipe);
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
    pending->ticket = begin_pipeline_async(*pending->recipe->state, R.frame,
        PipelineKeyHash{}(key), "guest_pipeline", prepare);
    if (!pending->ticket) {
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
      if(p.pipeline) remember_pipeline_recipe(std::move(pending->recipe));
      pending.reset();
      R.pipelineCacheDirty = true; R.pipelineCacheChangedFrame = R.frame;
    } else p.pendingCompile = std::move(pending);
    return remember(pipelines.emplace(key, std::move(p)).first->second);
  } else {
    result = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, pipelineState.get(), nullptr, &p.pipeline);
  }
  const bool recordRecipe = result == VK_SUCCESS;
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
VkSampler sampler(const uint32_t *words, bool compare, bool integer, bool allowAniso, bool linearSupported) {
  const bool nearestOnly = sampler_nearest_only(integer, compare, linearSupported);
  static const bool memo = [] {
    const char* e = std::getenv("WWHD_VK_SAMPLER_MEMO");
    return e && !std::strcmp(e, "1");
  }();
  const bool forceAniso = allowAniso && aniso_enabled();
  const bool stats = preparation_stats_enabled();
  if (stats) ++R.cpuPreparation.samplerRequests;
  if (memo && samplerMemo.matches(R.device, words, compare, integer, forceAniso, nearestOnly)) {
    if (stats) ++R.cpuPreparation.samplerMemoHits;
    return samplerMemo.value;
  }
  if (stats) ++R.cpuPreparation.samplerMapLookups;
  auto remember = [&](VkSampler value) {
    if (memo) samplerMemo.remember(R.device, words, compare, integer, forceAniso, nearestOnly, value);
    return value;
  };
  std::string key(reinterpret_cast<const char *>(words), 12);
  key.push_back(compare);
  key.push_back(integer);
  key.push_back(forceAniso);
  key.push_back(nearestOnly);
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
  ci.mipLodBias = std::clamp(float(w1.get_LOD_BIAS()) / 64.f,
                             -R.properties.limits.maxSamplerLodBias,
                             R.properties.limits.maxSamplerLodBias);
  if (!R.samplerMipLodBias && ci.mipLodBias != 0) {
    // MoltenVK cannot apply sampler LOD bias. Match the existing Metal renderer
    // there; native Vulkan devices retain the requested bias.
    static bool reported = false;
    if (!reported) {
      LOG("[vulkan] device lacks sampler LOD bias; using zero bias (Metal-compatible fallback)");
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
  uint32_t firstInstance = 0;
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
// count and log mismatches (the draw then uses the full result). Statistics every 120 frames.
struct FastPathConfig {
  bool on = false;
  uint32_t verifyEvery = 0;
};
const FastPathConfig& fastpath_config() {
  static const FastPathConfig config = [] {
    FastPathConfig c;
    if (const char* v = std::getenv("WWHD_VK_FASTPATH_VERIFY")) c.verifyEvery = uint32_t(std::max(0, std::atoi(v)));
    const char* e = std::getenv("WWHD_VK_FASTPATH");
    c.on = e ? std::atoi(e) != 0 : true;
    if (!c.on) c.verifyEvery = 0;
    LOG("[vulkan] continued-draw fast path %s%s", c.on ? "on" : "off (WWHD_VK_FASTPATH=0)", c.verifyEvery ? " (verify mode)" : "");
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
  uint32_t textureReg = 0, samplerReg = 0;
  std::array<uint32_t, 10> inputWords{};
  void capture_inputs(const uint32_t* r, const vk::Shader& shader) {
    textureReg = (shader.vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS) + unit * 7;
    samplerReg = REGADDR::SQ_TEX_SAMPLER_WORD0_0 +
        ((shader.vertex ? 18 : 0) + shader.dec->textureUnitSamplerAssignment[unit]) * 3;
    std::copy_n(r + textureReg, 7, inputWords.begin());
    std::copy_n(r + samplerReg, 3, inputWords.begin() + 7);
  }
  bool inputs_match(const uint32_t* r) const {
    return !std::memcmp(inputWords.data(), r + textureReg, 7 * sizeof(uint32_t)) &&
        !std::memcmp(inputWords.data() + 7, r + samplerReg, 3 * sizeof(uint32_t));
  }
};
struct PreparedStage {
  struct Uniform {
    uint32_t slot = 0, dynamicIndex = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize range = 0;
  };
  std::array<Uniform, 17> uniforms{};
  uint32_t count = 0;
  uint64_t submission = 0;
  uint64_t uniformVersion = 0;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  const vk::Shader* shader = nullptr;
  StageResources resources;
  std::vector<uint8_t> support;
  bool valid = false;
};
struct ContinuedStage {
  uint32_t count = 0;
  bool continuable = true;
  std::array<ContinuedTexture, LATTE_NUM_MAX_TEX_UNITS> textures{};
  mutable PreparedStage prepared;
  void add(const ContinuedTexture& t, const uint32_t* r, const vk::Shader& shader) {
    if (count < textures.size()) {
      auto& stored = textures[count++];
      stored = t;
      stored.capture_inputs(r, shader);
    }
    else continuable = false;
  }
  bool texture_inputs_match(const uint32_t* r) const {
    for (uint32_t i = 0; i < count; ++i)
      if (!textures[i].inputs_match(r)) return false;
    return true;
  }
};
struct DrawTargets {
  std::array<Surface*, 8> colors{};
  std::array<uint32_t, 8> slices{};
  Surface* depth = nullptr;
  Surface* target = nullptr;  // first colour target, else depth (before AO replay drops a mismatch)
  uint32_t depthSlice = 0, width = 0, height = 0;
  float sx = 1, sy = 1;
  bool same(const DrawTargets& o) const {
    return colors == o.colors && slices == o.slices && depth == o.depth && target == o.target && depthSlice == o.depthSlice &&
           width == o.width && height == o.height && !std::memcmp(&sx, &o.sx, 4) && !std::memcmp(&sy, &o.sy, 4);
  }
};
enum ContinuedReason : uint8_t {
  kContinued, kNoState, kNewFrame, kRegisters, kSurfaces, kUpload, kPrimitive, kSettings,
  kVerifyMismatch, kNotContinuable, kAoReplay, kReasons
};
const char* const continuedReasonNames[kReasons] = {
  "continued", "no state", "new frame", "registers", "surfaces", "texture upload", "primitive",
  "settings", "verify mismatch", "not continuable", "AO replay"};
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
  Pipeline* pipeline = nullptr;
  ContinuedStage stages[2];
};
// Stereo: each eye's command stream continues its own previous draw (stereo.h).
std::array<ContinuedDraw, 2> continuedEyes;
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
StereoShaders* stereoShadersOut = nullptr;       // eye 0's draw records
const StereoShaders* stereoShadersIn = nullptr;  // eye 1's draw reuses
struct ContinuedStats {
  uint64_t lastFrame = 0, draws = 0, verified = 0, mismatches = 0, totalMismatches = 0, totalVerified = 0;
  std::array<uint64_t, kReasons> reasons{};
  std::array<uint64_t, gx2::kSgCount> groups{};  // register groups that ended a continuation
  std::array<uint64_t, 8> mismatchKinds{};
} continuedStats;
const char* const mismatchKindNames[8] = {"fetch shader", "vertex shader", "pixel shader", "targets",
                                          "pipeline", "texture count", "texture binding", "topology"};
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
    if (fastpath_config().verifyEvery && k < int(sizeof buf))
      k += std::snprintf(buf + k, sizeof buf - k, "; verify: %llu checked, %llu mismatches (total %llu of %llu)",
                         (unsigned long long)st.verified, (unsigned long long)st.mismatches,
                         (unsigned long long)st.totalMismatches, (unsigned long long)st.totalVerified);
    LOG("%s", buf);
  }
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
  for (int i = 0; i < 8; i++)
    if (mask & (1 << i))
      t.colors[i] = color_target(r, i, &t.slices[i]);
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
// Eye 1's geometry replay still needs its own attachments, but consecutive draws do
// not need to resolve the same attachment registers again. This cache only owns
// lookup results; uploads and transitions below remain in command order.
bool resolve_replay_targets(const uint32_t* r, vk::Shader* ps, DrawTargets& targets) {
  struct Cached {
    uint64_t frame = ~0ull, version = 0, epoch = 0;
    uint32_t eyeSize = 0;
    vk::Shader* shader = nullptr;
    DrawTargets targets;
  };
  static Cached cache;
  if (cache.frame == R.frame && cache.version == gx2::g_full_state_version &&
      cache.epoch == g_surface_epoch && cache.eyeSize == stereo_eye_size_generation() && cache.shader == ps) {
    targets = cache.targets;
    return true;
  }
  if (!resolve_targets(r, ps, targets)) return false;
  cache = {R.frame, gx2::g_full_state_version, g_surface_epoch, stereo_eye_size_generation(), ps, targets};
  return true;
}
// A sampled texture of a stage (both paths): lookup, AO substitution and the write-tracking check.
// substituted: the private AO image replaced the guest texture.
Surface* resolve_texture(const uint32_t* r, vk::Shader* sh, uint32_t unit, bool* substituted = nullptr) {
  const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
  auto *s = sampled_texture(r + texbase + unit * 7, sh->dec->textureUsesDepthCompare[unit]);
  if (!s)
    exception_report::raise("missing sampled texture");
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
  return sampler(samplerWords, sh->dec->textureUsesDepthCompare[unit],
                 s->fmt.kind != FormatInfo::FLOAT,
                 !s->gpuWritten && s->mips > 1, s->linearFiltering);
}
// Uniform payload allocation is independent of descriptor preparation. Both the
// resolved and prepared paths use the same immutable snapshot/cache contract.
UploadSlice stage_uniform_snapshot(bool vertex, size_t logicalSlot, const void* bytes,
                                   size_t size, uint32_t guestAddr) {
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
    const bool cached = guestAddr && buffer_cache_enabled() &&
        cached_guest_range(guestAddr, uint32_t(size), rprof::kUpUbo, cachedSlice);
    const auto b = cached ? cachedSlice
        : reuseUniforms
        ? uniformCache.get(R.device, R.submissionGeneration,
                           (vertex ? 0 : 17) + logicalSlot, bytes, size,
                           R.uploadReadsDirect, fresh)
        : fresh(bytes, size);
    if (reuseUniforms && preparation_stats_enabled()) {
      const auto& counts = uniformCache.counters;
      R.cpuPreparation.uniformReuseChecks = counts.checks;
      R.cpuPreparation.uniformReuseComparisons = counts.comparisons;
      R.cpuPreparation.uniformReuseHits = counts.hits;
      R.cpuPreparation.uniformReuseBytes = counts.reusedBytes;
    }
    return b;
}
// A draw-array shader's constants for its next draw: one of drawArrayCount slots of an upload
// reservation, selected by the native instance index. Each shader keeps its reservation for the
// submission, so draws alternating between shaders fill theirs instead of each starting a new one.
UploadSlice stage_draw_array(vk::Shader* shader, const void* bytes, size_t size, uint32_t& index) {
  struct Reservation {
    UploadSlice slice;
    uint32_t used = 0;
  };
  static std::unordered_map<const vk::Shader*, Reservation> reservations;  // this submission's
  static uint64_t submission = ~uint64_t{0};
  if (submission != R.submissionGeneration) {
    reservations.clear();
    submission = R.submissionGeneration;
  }
  auto& current = reservations[shader];
  if (!current.slice.mapped || current.used == shader->drawArrayCount) {
    rprof::UploadKind uploads(rprof::kUpUniformVars);
    current = {allocate_upload(size * shader->drawArrayCount, R.properties.limits.minUniformBufferOffsetAlignment), 0};
    // allocating can start the submission the reservation belongs to (ensure_submission)
    if (submission != R.submissionGeneration) {
      const auto fresh = current;
      reservations.clear();
      submission = R.submissionGeneration;
      reservations[shader] = fresh;
      return stage_draw_array(shader, bytes, size, index);
    }
  }
  index = current.used++;
  memcpy(static_cast<uint8_t*>(current.slice.mapped) + size * index, bytes, size);
  return current.slice;
}
// Execute a resolved stage, updating only its variable payloads. Texture uploads
// and the surface-epoch check have already run in the continued-draw gate. No
// texture can alias an attachment here; those stages never become continuations.
// Descriptor contents remain immutable, while fresh dynamic offsets select this
// draw's uniform snapshots. A buffer rollover/resize returns to full resolution.
bool bind_prepared_stage(const uint32_t* r, vk::Shader* sh, VkDescriptorSetLayout layout,
                         float sx, float sy, const ContinuedStage& resolved, StageResources& out, bool texturesReady = false) {
  auto& prepared = resolved.prepared;
  // A pipeline owns its layout handles, but a stage's binding definitions are
  // determined by this shader and the dynamic-uniform mode. Vulkan permits a
  // set allocated from an identically defined layout to be bound here.
  if (!prepared.valid || prepared.submission != R.submissionGeneration || prepared.shader != sh)
    return false;
  out = prepared.resources;
  const auto uniformVersion = gx2::g_uniform_state_version[sh->vertex ? 1 : 0];
  const bool supportUnchanged = !sh->uniformInputs.readsGuestMemory &&
      prepared.uniformVersion == uniformVersion;
  static const bool verify = std::getenv("WWHD_VK_PREPARED_VERIFY") != nullptr;
  static uint64_t checks = 0;
  const bool verifyNow = verify && !prepared.support.empty() && (++checks & 63) == 0;
  struct GuestInput { uint32_t address; std::vector<uint8_t> bytes; };
  std::vector<GuestInput> guestInputs;
  // The guest can update a uniform block while the render thread is reading it.
  // Comparing two live reads can report a false mismatch. Both algorithms must
  // consume one immutable input snapshot; neither algorithm supplies the other's
  // output. This extra copying exists only in verification mode.
  if (verifyNow) {
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    for (const auto& span : sh->uniformInputs.spans) if (span.indirect && r[span.reg]) {
      const uint32_t address = r[span.reg] + span.source;
      ranges.emplace_back(address, uint64_t(address) + span.bytes);
    }
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 0; i < ranges.size();) {
      const auto start = ranges[i].first;
      auto end = ranges[i++].second;
      while (i < ranges.size() && ranges[i].first <= end) end = std::max(end, ranges[i++].second);
      const auto* bytes = static_cast<const uint8_t*>(mem::ptr(uint32_t(start)));
      guestInputs.push_back({uint32_t(start), {bytes, bytes + (end - start)}});
    }
  }
  vk::UniformReader reader{&guestInputs, [](void* context, uint32_t address, size_t size) -> const void* {
    for (const auto& input : *static_cast<std::vector<GuestInput>*>(context))
      if (address >= input.address && uint64_t(address) + size <= uint64_t(input.address) + input.bytes.size())
        return input.bytes.data() + (address - input.address);
    exception_report::raise("uniform verification read outside captured inputs");
  }};
  const auto* verificationReader = verifyNow ? &reader : nullptr;
  if (!supportUnchanged && !prepared.support.empty() &&
      !vk::update_prepared_uniforms(r, *sh, prepared.support, sx, sy, verificationReader))
    return false;
  if (verifyNow) {
      std::vector<uint8_t> reference;
      // Keep the reference independent of the copy plan being verified.
      vk::pack_uniforms_into(r, *sh, reference, sx, sy, 0, 600, nullptr, verificationReader, false);
      for (uint32_t i = 0; i < resolved.count; ++i) {
        const auto& texture = resolved.textures[i];
        const int offset = sh->uniforms.offset_texScale[texture.unit];
        if (offset >= 0 && size_t(offset) + 8 <= reference.size()) {
          const float scale[] = {texture.surface->sx, texture.surface->sy};
          memcpy(reference.data() + offset, scale, sizeof scale);
        }
      }
      if (reference != prepared.support) {
        const auto first = std::mismatch(reference.begin(), reference.end(), prepared.support.begin()).first;
        const auto offset = size_t(first - reference.begin());
        LOG("[vulkan prepared mismatch] frame %llu %s shader %016llx byte %zu: resolved %02x prepared %02x; unchanged %d indirect %d versions %llu/%llu scale %.4f %.4f",
            (unsigned long long)R.frame, sh->vertex ? "VS" : "PS", (unsigned long long)sh->key,
            offset, unsigned(reference[offset]), unsigned(prepared.support[offset]), supportUnchanged,
            sh->uniformInputs.readsGuestMemory, (unsigned long long)uniformVersion,
            (unsigned long long)prepared.uniformVersion, sx, sy);
        exception_report::raise("prepared uniform payload differs from resolved shader payload");
      }
  }
  // Resource use still accumulates shader readers, even when no new descriptor
  // needs writing. Transfers between draws can also change an image's layout.
  if (!texturesReady) for (uint32_t i = 0; i < resolved.count; ++i)
    transition_image(resolved.textures[i].surface, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        narrow_barriers() ? (sh->vertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                          : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
        VK_ACCESS_SHADER_READ_BIT);
  const uint32_t block = sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
  for (uint32_t i = 0; i < prepared.count; ++i) {
    const auto& uniform = prepared.uniforms[i];
    const bool support = uniform.slot == 16;
    if (support && supportUnchanged) continue;
    const uint32_t addr = support ? 0 : r[block + uniform.slot * 7];
    const size_t size = support ? prepared.support.size()
        : std::min<uint32_t>(r[block + uniform.slot * 7 + 1] + 1, 0x10000);
    const void* bytes = support ? prepared.support.data() : addr ? mem::ptr(addr) : nullptr;
    const auto slice = support && sh->drawArrayCount
        ? stage_draw_array(sh, bytes, size, out.firstInstance)
        : stage_uniform_snapshot(sh->vertex, uniform.slot, bytes, size, addr);
    if (addr) rprof::guest_read(rprof::kUpUbo, addr, size);
    if (slice.buffer != uniform.buffer || slice.size != uniform.range || slice.offset > UINT32_MAX)
      return false;
    out.dynamicOffsets[uniform.dynamicIndex] = uint32_t(slice.offset);
  }
  rprof::mark(rprof::kUniforms);
  prepared.resources = out;
  prepared.uniformVersion = uniformVersion;
  return true;
}
// Repeated mesh instances need only new vertex constants. The pending native
// draw proves no command has changed GPU state since the resolved draw. Validate
// all cached geometry against guest write tracking before reusing its bindings.
bool append_prepared_instance(const uint32_t* r, uint32_t count, uint32_t indexType,
                              uint32_t indexAddr, uint32_t baseVertex, uint32_t instances,
                              uint32_t dirtyGroups, ContinuedDraw& continued) {
  const auto& geometry = instanceGeometry;
  if (!geometry.valid || !instanceDraw.instances || geometry.submission != R.submissionGeneration ||
      instanceDraw.command != R.cmd || !R.rendering || !R.passTracked ||
      stereo::frame.mode != stereo::Off || !continued.vs->drawArrayCount || instances != 1 ||
      (dirtyGroups & ~((1u << gx2::kSgAluConst) | (1u << gx2::kSgUniformBlock))) || geometry.count != count ||
      geometry.indexType != indexType || geometry.indexAddr != indexAddr || geometry.baseVertex != baseVertex ||
      buffer_cache_verify() || render::cloud_shadows() || cloud_proto::mode() != cloud_proto::kOff)
    return false;
  const auto& pixel = continued.stages[1].prepared;
  if (!pixel.valid || pixel.submission != R.submissionGeneration)
    return false;
  for (uint32_t i = 0; i < geometry.inputCount; ++i) {
    const auto& input = geometry.inputs[i];
    if (!input.entry || !buffer_cache().retained_hit(*input.entry, input.bytes) ||
        input.entry->region.mapped != input.slice.mapped || input.entry->region.offset != input.slice.offset)
      return false;
    rprof::guest_read(input.kind ? rprof::kUpIndex : rprof::kUpVertex, input.address, input.bytes);
  }
  const auto previousPixel = pixel.resources;
  StageResources pixelNow;
  if (!bind_prepared_stage(r, continued.ps, continued.pipeline->resourceSets[1],
      continued.targets.sx, continued.targets.sy, continued.stages[1], pixelNow, true) ||
      pixelNow.set != previousPixel.set || pixelNow.dynamicOffsetCount != previousPixel.dynamicOffsetCount ||
      !std::equal(pixelNow.dynamicOffsets.begin(), pixelNow.dynamicOffsets.begin() + pixelNow.dynamicOffsetCount,
                  previousPixel.dynamicOffsets.begin()))
    return false;
  const auto previous = continued.stages[0].prepared.resources;
  StageResources vertex;
  if (!bind_prepared_stage(r, continued.vs, continued.pipeline->resourceSets[0],
      continued.targets.sx, continued.targets.sy, continued.stages[0], vertex, true) ||
      !instanceDraw.instances || vertex.set != previous.set || vertex.dynamicOffsetCount != previous.dynamicOffsetCount ||
      !std::equal(vertex.dynamicOffsets.begin(), vertex.dynamicOffsets.begin() + vertex.dynamicOffsetCount,
                  previous.dynamicOffsets.begin()))
    return false;
  append_instance_draw(R.cmd, true, geometry.nativeCount, baseVertex, vertex.firstInstance);
  for (auto* color : continued.targets.colors) if (color) mark_draw_target_written(color);
  if (continued.targets.depth) mark_draw_target_written(continued.targets.depth);
  if (R.gpuPassTimestampsEnabled) gpu_count_render_draw();
  ++R.drawCount;
  ++arrayPreparedDraws;
  continued.valid = true;
  // This extends a pending native draw; it does not add another command to
  // the GPU batch. Counting each guest instance here prematurely submitted
  // the batch, invalidating prepared descriptors and splitting the instance
  // array. The resolved draw that opened it already counted toward the limit.
  rprof::mark(rprof::kSubmit);
  return true;
}
StageResources bind_stage(const uint32_t *r, vk::Shader *sh,
                          VkDescriptorSetLayout layout, bool dynamicUniforms, float sx, float sy,
                          const std::array<Surface *, 8> &colors,
                          Surface *depth, FeedbackStatsProbe* feedbackProbe, float eyeOffset = 0,
                          bool sunShadow = false,
                          const ContinuedStage* reuse = nullptr, ContinuedStage* record = nullptr,
                          StageReplay* replayOut = nullptr) {
  StageResources out;
  auto &m = sh->mapping;
  static const bool preparedStages = [] {
    const char* e = std::getenv("WWHD_VK_PREPARED_STAGES");
    return !e || std::strcmp(e, "0");
  }();
  const bool prepareStage = preparedStages && dynamicUniforms && stereo::frame.mode == stereo::Off &&
      !aoPrivateReplay && sh->kind != gfx::ProgramKind::OcclusionVertex &&
      (sh->vertex || (r[mmSQ_PGM_START_PS] << 8) != 0x44F91200);
  if (prepareStage && reuse && bind_prepared_stage(r, sh, layout, sx, sy, *reuse, out))
    return out;
  // A reusable stage can fall back on upload-arena rollover. Rebuild its
  // texture list rather than appending a second copy of the old bindings.
  if (record) {
    // In-place reuse aliases this list. Once it is cleared below, the fallback
    // must resolve the textures again instead of iterating an empty reuse list.
    if (reuse == record) reuse = nullptr;
    record->count = 0;
    record->continuable = true;
    record->prepared.valid = false;
  }
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
  auto uniform = [&](int binding, const void *bytes, size_t size, size_t logicalSlot,
                     uint32_t guestAddr = 0) {
    if (binding < 0)
      return;
    const auto b = logicalSlot == 16 && sh->drawArrayCount
        ? stage_draw_array(sh, bytes, size, out.firstInstance)
        : stage_uniform_snapshot(sh->vertex, logicalSlot, bytes, size, guestAddr);
    lastUpload = b;
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
  if (m.uniformVarsBufferBindingPoint >= 0) {
    const size_t capacity=preparation_stats_enabled()?supportUniforms.capacity():0;
    vk::pack_uniforms_into(r, *sh, supportUniforms, sx, sy, eyeOffset, stereo::frame.convergence,
                           replayOut ? &supportRows : nullptr);
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

  uint32_t block =
      sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
  for (int i = 0; i < 16; i++)
    if (m.uniformBuffersBindingPoint[i] >= 0) {
      uint32_t addr = r[block + i * 7];
      uint32_t size = std::min<uint32_t>(r[block + i * 7 + 1] + 1, 0x10000);
      // Only a block holding a camera projection gets an eye copy; the others keep their
      // buffer-cache upload by address.
      if (sh->vertex && eyeOffset != 0 && addr &&
          stereo::has_camera_projection(static_cast<const unsigned char*>(mem::ptr(addr)), size)) {
        static thread_local std::vector<uint8_t> eyeBlock;
        eyeBlock.resize(size);memcpy(eyeBlock.data(),mem::ptr(addr),size);
        stereo::EyeRows blockRows;
        stereo::project_uniform_block(eyeBlock.data(),size,eyeOffset,stereo::frame.convergence,nullptr,
                                      replayOut ? &blockRows : nullptr);
        uniform(m.uniformBuffersBindingPoint[i],eyeBlock.data(),size,size_t(i));
        eyeUniform(m.uniformBuffersBindingPoint[i],eyeBlock.data(),size,blockRows);
      } else uniform(m.uniformBuffersBindingPoint[i], addr ? mem::ptr(addr) : nullptr,
              size, size_t(i), addr);
      if (addr) rprof::guest_read(rprof::kUpUbo, addr, size);
    }
  rprof::mark(rprof::kUniforms);
  uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS
                                : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
  auto bindTexture = [&](uint32_t unit, int binding, Surface* s, VkImageView view, VkSampler samplerValue) {
    int scaleOffset = sh->uniforms.offset_texScale[unit];
    if (scaleOffset >= 0 && size_t(scaleOffset) + 8 <= supportUniforms.size()) {
      const float scale[] = {s->sx, s->sy};
      memcpy(supportUniforms.data() + scaleOffset, scale, sizeof scale);
    }
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
      transition_image(t.surface, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       narrow_barriers()
                           ? (sh->vertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                         : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                           : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                       VK_ACCESS_SHADER_READ_BIT);
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
      transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       narrow_barriers()
                           ? (sh->vertex ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                         : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                           : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                       VK_ACCESS_SHADER_READ_BIT);
    VkSampler samplerValue = resolve_sampler(r, sh, unit, s);
    bindTexture(unit, binding, s, sampledView, samplerValue);
    replayTexture(s, unit, binding, aliases || substituted);
    if (record) {
      if (aliases || substituted || shares_target_address(s, colors, depth)) record->continuable = false;
      record->add({unit, binding, s, sampledView, samplerValue}, r, *sh);
    }
  }
  rprof::mark(rprof::kTextures);
  if (m.uniformVarsBufferBindingPoint >= 0) {
    uniform(m.uniformVarsBufferBindingPoint, supportUniforms.data(),
            supportUniforms.size(), 16);
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
  out.set = stage_descriptor_set(sh->vertex, layout, writes.data(), writeCount);
  // Compile the stage's binding assignments once after resolving its resources.
  // Each following draw executes this small program, rather than rebuilding,
  // sorting and hashing VkWriteDescriptorSets and walking texture bindings.
  auto* prepared = record ? &record->prepared : reuse ? &reuse->prepared : nullptr;
  if (prepared) {
    prepared->valid = false;
    if (prepareStage && (!record || record->continuable)) {
      prepared->count = 0;
      prepared->submission = R.submissionGeneration;
      prepared->uniformVersion = gx2::g_uniform_state_version[sh->vertex ? 1 : 0];
      prepared->layout = layout;
      prepared->shader = sh;
      prepared->resources = out;
      prepared->support.assign(supportUniforms.begin(), supportUniforms.end());
      bool complete = true;
      for (uint32_t slot = 0; slot < 17; ++slot) {
        const int binding = slot == 16 ? m.uniformVarsBufferBindingPoint : m.uniformBuffersBindingPoint[slot];
        if (binding < 0) continue;
        uint32_t w = 0, d = 0;
        while (w < writeCount && writes[w].dstBinding != uint32_t(binding)) ++w;
        while (d < dynamicCount && dynamicBindings[d].first != uint32_t(binding)) ++d;
        if (w == writeCount || d == dynamicCount || !writes[w].pBufferInfo) { complete = false; break; }
        const auto& info = *writes[w].pBufferInfo;
        prepared->uniforms[prepared->count++] = {slot, d, info.buffer, info.range};
      }
      prepared->valid = complete;
    }
  }
  return out;
}
// The stage's descriptor set for these writes: reused within the submission, else written.
VkDescriptorSet stage_descriptor_set(bool vertex, VkDescriptorSetLayout layout,
                                     VkWriteDescriptorSet* writes, uint32_t writeCount) {
  ensure_submission();
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
  ai.descriptorPool = descriptor_pool();
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
bool verify_continued(const uint32_t* r, uint32_t prim) {
  const auto& c = continuedEyes[stereoEye];
  int kind = -1;
  char detail[200] = "";
  uint64_t fsKey = 0;
  auto* fs = vk::get_fetch_shader(r, &fsKey, R.frame);
  vk::Shader* vs = fs ? vk::translate(r, true, fs, fsKey, R.frame, g_shader_state_gen) : nullptr;
  vk::Shader* ps = fs ? vk::translate(r, false, fs, fsKey, R.frame, g_shader_state_gen,
                                     vs && vs->ready() ? vs : nullptr) : nullptr;
  if (c.vs && c.vs->drawArrayCount && vs && vs->drawArrayVariant) vs = vs->drawArrayVariant.get();
  DrawTargets t;
  if (fs != c.fs || fsKey != c.fsKey) kind = 0;
  else if (vs != c.vs || !vs || !vs->ready()) kind = 1;
  else if (ps != c.ps || !ps || !ps->ready()) kind = 2;
  else if (primitive_topology(prim) != c.topology) kind = 7;
  else if (!resolve_targets(r, ps, t) || !t.same(c.targets)) kind = 3;
  else if (&pipeline(r, vs, ps, fs, c.topology, t.colors, t.depth) != c.pipeline) kind = 4;
  else
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
    LOG("[vulkan fastpath] verify mismatch #%llu: %s%s; frame %llu draw %llu prim %u vs %08X ps %08X fs %08X",
        (unsigned long long)st.totalMismatches, mismatchKindNames[kind], detail, (unsigned long long)R.frame,
        (unsigned long long)R.drawCount, prim, r[mmSQ_PGM_START_VS] << 8, r[mmSQ_PGM_START_PS] << 8,
        r[mmSQ_PGM_START_FS] << 8);
  }
  return false;
}
ContinuedReason continued_reason(uint32_t prim) {
  const auto& c = continuedEyes[stereoEye];
  if (!c.valid) return c.invalidReason;
  if (c.frame != R.frame) return kNewFrame;
  if (c.stateVersion != gx2::g_full_state_version) return kRegisters;
  if (c.surfaceEpoch != g_surface_epoch) return kSurfaces;
  if (c.prim != prim) return kPrimitive;
  if (c.aoMode != ao_mode() || c.aoHires != ao_hires_enabled() || c.aniso != aniso_enabled()) return kSettings;
  return kContinued;
}
} // namespace
void stage_input_smoke_check() {
  std::vector<uint32_t> r(0x10000);
  for (bool vertex : {false, true}) {
    LatteDecompilerShader decoded(vertex ? LatteConst::ShaderType::Vertex : LatteConst::ShaderType::Pixel);
    vk::Shader shader; shader.vertex = vertex; shader.dec = &decoded;
    decoded.textureUnitSamplerAssignment[2] = 5; // sampler differs from texture unit
    ContinuedStage stage;
    stage.add({2, 0, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE}, r.data(), shader);
    const auto& input = stage.textures[0];
    const auto require = [](bool ok) { if (!ok) exception_report::raise("stage texture dependency mismatch"); };
    require(stage.texture_inputs_match(r.data()));
    // Every word of a used texture or its assigned sampler invalidates reuse.
    for (uint32_t i = 0; i < 10; ++i) {
      const uint32_t reg = i < 7 ? input.textureReg + i : input.samplerReg + i - 7;
      r[reg] ^= 1; require(!stage.texture_inputs_match(r.data())); r[reg] ^= 1;
    }
    const uint32_t otherTexture = (vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS) + 2 * 7;
    const uint32_t otherSampler = REGADDR::SQ_TEX_SAMPLER_WORD0_0 + ((vertex ? 0 : 18) + 5) * 3;
    r[otherTexture] ^= 1; r[otherSampler] ^= 1;
    r[input.textureReg + 7] ^= 1; // unused unit in the same stage
    require(stage.texture_inputs_match(r.data()));
    r[otherTexture] ^= 1; r[otherSampler] ^= 1; r[input.textureReg + 7] ^= 1;
    stage.count = 0;
    r[input.textureReg] ^= 1;
    require(stage.texture_inputs_match(r.data()));
    r[input.textureReg] ^= 1;
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
  for (auto& [key, value] : pipelines) resolve_pending_pipeline(value);
}
void reset_pipeline_lookup_cache() { lastPipelineLookup = {}; pipelineLookaside = {}; samplerMemo = {}; drawBatchState = {}; continuedEyes = {}; }
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
  ensure_submission();
  if (preparation_stats_enabled()) {
    static uint64_t reportedFrame = 0;
    if (R.frame >= reportedFrame + 120) {
      if (arrayGuestDraws)
        LOG("[vulkan draw arrays] %.1f guest draws/frame in %.1f native draws/frame; %.1f prepared instances/frame",
            arrayGuestDraws / double(R.frame - reportedFrame), arrayNativeDraws / double(R.frame - reportedFrame),
            arrayPreparedDraws / double(R.frame - reportedFrame));
      arrayGuestDraws = arrayNativeDraws = arrayPreparedDraws = 0;
      reportedFrame = R.frame;
    }
  }
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
void draw(const uint32_t *r, uint32_t prim, uint32_t count, uint32_t indexType,
          uint32_t indexAddr, uint32_t baseVertex, uint32_t instances, bool* viewIndependent) {
  ensure_submission();
  const uint32_t dirtyGroups = gx2::take_dirty_groups();  // register groups changed since the previous draw
  auto& continued = continuedEyes[stereoEye];
  // the top-level stereo draw only, never the AO replay below
  StereoShaders* shadersOut = std::exchange(stereoShadersOut, nullptr);
  const StereoShaders* shadersIn = std::exchange(stereoShadersIn, nullptr);
  // eye 1: eye 0's draw, replayed (StereoShaders)
  const StereoShaders* replay = shadersIn && shadersIn->replay && !aoPrivateReplay &&
      shadersIn->submission == R.submissionGeneration ? shadersIn : nullptr;
  if (clouds_skip_card()) return;  // clouds.cpp: toon clouds replace the game's cloud cards
  if (!count || !instances || ((prim == 0x13 || prim == 0x14) && count < 4))
    return;
  if (r[REGADDR::PA_CL_CLIP_CNTL] & (1 << 22))
    return;
  ((uint32_t *)r)[REGADDR::VGT_PRIMITIVE_TYPE] = prim;
  // continued-draw fast path (see fastpath_config); the AO replay draw stays outside of it
  const auto& fastPath = fastpath_config();
  // a replayed eye 1 draw records no continuation of its own
  const bool fastPathDraw = fastPath.on && !aoPrivateReplay && !replay;
  if (replay) {
    continued.valid = false;
    continued.invalidReason = kNoState;
  }
  bool fast = false;
  if (fastPathDraw) {
    report_continued_stats();
    ContinuedReason reason = continued_reason(prim);
    if (reason == kContinued && continued.vs->drawArrayCount && instances != 1) reason = kNoState;
    if (reason == kContinued) {
      // guest writes to the reused textures (write tracking, GX2Invalidate) upload here; an upload
      // can change lookups, so it ends the continuation
      for (const auto& stage : continued.stages)
        for (uint32_t i = 0; i < stage.count; ++i) upload_surface(stage.textures[i].surface);
      if (g_surface_epoch != continued.surfaceEpoch) reason = kUpload;
    }
    if (reason == kContinued && fastPath.verifyEvery) {
      static uint64_t verifyCounter = 0;
      if (++verifyCounter % fastPath.verifyEvery == 0) {
        ++continuedStats.verified;
        ++continuedStats.totalVerified;
        if (!verify_continued(r, prim)) reason = kVerifyMismatch;
      }
    }
    fast = reason == kContinued;
    ++continuedStats.draws;
    ++continuedStats.reasons[reason];
    if (reason == kRegisters)
      for (int g = 0; g < gx2::kSgCount; ++g)
        if ((dirtyGroups & ~gx2::kSgContinuedMask) >> g & 1) ++continuedStats.groups[g];
    // a full draw records a new state below; exceptions leave none
    continued.valid = false;
    continued.invalidReason = kNoState;
  }
  uint64_t fsKey = 0;
  if (fast && append_prepared_instance(r, count, indexType, indexAddr, baseVertex, instances, dirtyGroups, continued))
    return;
  instanceGeometry.valid = false;
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
    if(verifyReplay) {
      uint64_t checkedKey=0;
      auto* checkedFetch=vk::get_fetch_shader(r,&checkedKey,R.frame);
      auto* checkedVs=checkedFetch?vk::translate(r,true,checkedFetch,checkedKey,R.frame,g_shader_state_gen):nullptr;
      auto* checkedPs=checkedVs?vk::translate(r,false,checkedFetch,checkedKey,R.frame,g_shader_state_gen,
                                           checkedVs->ready()?checkedVs:nullptr):nullptr;
      if(fs!=checkedFetch||fsKey!=checkedKey||vs!=checkedVs||ps!=checkedPs)
        exception_report::raise("stereo shader reuse differs from independent second-eye resolution");
      static uint64_t verified=0;
      if(++verified%100000==0)LOG("[stereo verify] %llu second-eye shader resolutions matched",(unsigned long long)verified);
    }
  } else {
    fs = vk::get_fetch_shader(r, &fsKey, R.frame);
    if (!fs)
      exception_report::raise("missing Vulkan fetch shader");
    vs = vk::translate(r, true, fs, fsKey, R.frame, g_shader_state_gen);
    ps = vk::translate(r, false, fs, fsKey, R.frame, g_shader_state_gen,
                           vs && vs->ready() ? vs : nullptr);
  }
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
  if (stereo::frame.mode == stereo::Off && !aoPrivateReplay && instances == 1 && vs->drawArrayVariant) {
    bool perVertex = true;
    for (const auto& group : fs->bufferGroups)
      for (int i = 0; i < group.attribCount; ++i)
        perVertex &= group.attrib[i].fetchType == LatteConst::VertexFetchType2::VERTEX_DATA;
    if (perVertex) vs = vk::draw_array_variant(*vs, R.frame);
  }
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
  if (fast) targets = continued.targets;
  else if (!(replay ? resolve_replay_targets(r, ps, targets) : resolve_targets(r, ps, targets)))
    return;
  auto& colors = targets.colors;
  auto& slices = targets.slices;
  Surface* depth = targets.depth;
  const uint32_t depthSlice = targets.depthSlice, width = targets.width, height = targets.height;
  const float sx = targets.sx, sy = targets.sy;
  clouds_before_draw(r, ps, colors[0], slices[0], depth);  // clouds.cpp: cloud shadows
  rprof::mark(rprof::kTargets);
  auto &p = fast ? *continued.pipeline
      : replay && replay->formats == target_formats(colors, depth) ? *replay->pipeline
      : pipeline(r, vs, ps, fs, topology, colors, depth);
  rprof::mark(rprof::kPipeline);
  if (!p.pipeline && !p.pendingCompile) return;
  if (p.pendingCompile && !begin_exact_preparation()) resolve_pending_pipeline(p);
  if (!p.pipeline && !p.pendingCompile) return; // terminal driver failure, not pending
  if(feedback_stats_enabled()) report_feedback_stats();
  if(vertex_window_stats_enabled()) report_vertex_window_stats();
  FeedbackStatsProbe feedbackProbe;
  auto* probe=feedback_stats_enabled()?&feedbackProbe:nullptr;
  const float cameraEye = stereo::frame.mode != stereo::Off && !stereo::layoutDrawDepth && stereo_screen_target(targets.target->width,targets.target->height)
      ? (stereoEye ? 1.0f : -1.0f)*stereo::frame.depth*stereo::eyeDistanceAtUnitDepth : 0.0f;
  const bool sunShadow = ps->kind == gfx::ProgramKind::SunShadowMask;
  const bool recordState = fastPathDraw && !fast;
  static const bool independentStages = [] {
    const char* e = std::getenv("WWHD_VK_INDEPENDENT_STAGES");
    return !e || std::strcmp(e, "0");
  }();
  // Geometry, shaders, and raster state need not invalidate an unchanged
  // stage's resource bindings. Texture/sampler writes are global dirty groups,
  // but a stage depends only on the units it samples. Compare those exact inputs
  // so pixel material changes do not rebuild an unaffected vertex stage (or vice
  // versa). Attachment changes, external invalidations and settings stay gated.
  bool reuseStage[2] = {fast, fast};
  if (independentStages && recordState && (stereo::frame.mode == stereo::Off || stereoEye == 0) &&
      continued.frame == R.frame && continued.surfaceEpoch == g_surface_epoch &&
      continued.aoMode == ao_mode() && continued.aoHires == ao_hires_enabled() &&
      continued.aniso == aniso_enabled() && continued.targets.same(targets) &&
      !(dirtyGroups & ((1u << gx2::kSgTargets) | (1u << gx2::kSgOther)))) {
    vk::Shader* current[] = {vs, ps};
    for (int st = 0; st < 2; ++st) {
      auto& stage = continued.stages[st];
      if (!stage.continuable || !stage.prepared.valid || stage.prepared.shader != current[st]) continue;
      if ((dirtyGroups & ((1u << gx2::kSgTexture) | (1u << gx2::kSgSampler))) &&
          !stage.texture_inputs_match(r)) continue;
      for (uint32_t i = 0; i < stage.count; ++i) upload_surface(stage.textures[i].surface);
      if (continued.surfaceEpoch == g_surface_epoch) reuseStage[st] = true;
    }
    if (continued.surfaceEpoch != g_surface_epoch) reuseStage[0] = reuseStage[1] = false;
  }
  // eye 1 replays a stage of eye 0's draw at the same scale through the same pipeline
  const bool replayStages = replay && &p == replay->pipeline && replay->sx == sx && replay->sy == sy;
  StageResources vres, pres;
  if (!replayStages || !replay->stages[0].valid ||
      !replay_stage(replay->stages[0], r, vs, cameraEye, colors, depth, vres))
    vres = bind_stage(r, vs, p.resourceSets[0], p.dynamicUniforms, sx, sy, colors, depth, probe, cameraEye, sunShadow,
                      reuseStage[0] ? &continued.stages[0] : nullptr, recordState ? &continued.stages[0] : nullptr,
                      shadersOut ? &shadersOut->stages[0] : nullptr);
  if (!replayStages || !replay->stages[1].valid ||
      !replay_stage(replay->stages[1], r, ps, sunShadow ? cameraEye : 0, colors, depth, pres))
    pres = bind_stage(r, ps, p.resourceSets[1], p.dynamicUniforms, sx, sy, colors, depth, probe,
                      sunShadow ? cameraEye : 0, sunShadow,
                      reuseStage[1] ? &continued.stages[1] : nullptr, recordState ? &continued.stages[1] : nullptr,
                      shadersOut ? &shadersOut->stages[1] : nullptr);
  if (shadersOut) {
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
      c.pipeline = &p;
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
  if (!p.pipeline && !p.pendingCompile) return;
  const bool deferredPipelineDraw = bool(p.pendingCompile);
  if (p.pendingCompile) {
    auto* ownedPipeline = &p;
    record::emit_owned([ownedPipeline,cmd]() {
      resolve_pending_pipeline(*ownedPipeline);
      if(ownedPipeline->pipeline)
        record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ownedPipeline->pipeline);
    });
    state.pipeline = VK_NULL_HANDLE;
  } else if (state.pipeline != p.pipeline) {
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    state.pipeline = p.pipeline;
  }
  VkDescriptorSet sets[] = {vres.set, pres.set};
  const uint32_t drawFirstInstance = vres.firstInstance;
  auto nativeDraw = [&](bool indexed, uint32_t drawCount) {
    if (vs->drawArrayCount) append_instance_draw(cmd, indexed, drawCount, baseVertex, drawFirstInstance);
    else if (indexed) record::vkCmdDrawIndexed(cmd, drawCount, instances, 0, int32_t(baseVertex), drawFirstInstance);
    else record::vkCmdDraw(cmd, drawCount, instances, baseVertex, drawFirstInstance);
  };
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
    matches=state.descriptorProbe.observe(cmd,R.submissionGeneration,p.layout,sets,
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
    record::vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,p.layout,
        bind.first,bind.count,sets+bind.first,bind.offsetCount,
        dynamicOffsets.data()+bind.offsetBegin);
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
  const bool recordGeometry = vs->drawArrayCount && (nativeIndices || convertedEntry) && !deferredPipelineDraw &&
      !R.vertexDiagnostics.noVertexReuse && buffer_cache_enabled();
  instanceGeometry.valid = recordGeometry;
  instanceGeometry.inputCount = 0;
  instanceGeometry.count = count;
  instanceGeometry.nativeCount = convertedEntry ? convertedEntry->outSize / 4 : count;
  instanceGeometry.indexType = indexType;
  instanceGeometry.indexAddr = indexAddr;
  instanceGeometry.baseVertex = baseVertex;
  instanceGeometry.submission = R.submissionGeneration;
  if (recordGeometry && convertedEntry)
    instanceGeometry.add(indexAddr, uint32_t(guestBytes), 2,
        buffer_cache_slice(*convertedEntry, convertedEntry->outSize), convertedEntry);
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
      if (recordGeometry) instanceGeometry.add(indexAddr, count * indexBytes, 1, nativeIndexSlice, indexEntry);
      nativeIndexData = indexEntry->shadow.data();
      vertexExtent = cached_index_extent(*indexEntry, nativeIndexData, count, indexBytes,
                                         hostRestart, int32_t(baseVertex));
    } else {
      instanceGeometry.valid = false;
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
  const bool cachedTrims = p.trimFetch == fs && p.trimVertex == vs &&
                           p.bindingTrims.size() == fs->bufferGroups.size();
  size_t trimGroup = 0;
  for (auto &g : fs->bufferGroups) {
    uint32_t addr =
        r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7];
    uint32_t size =
        r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 1] + 1;
    if (!addr || !size)
      exception_report::raise("missing vertex buffer");
    const BindingTrimMetadata trim = cachedTrims
        ? p.bindingTrims[trimGroup] : binding_trim_metadata(r, vs, g);
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
    const bool copyWindow=vertex_copy_window_enabled() &&
        (nativeIndices || !indices.empty()) && supported && rate &&
        attributeEnd && stride && extent.valid && vertexExtent.valid &&
        windowEnd<=copied && windowBegin<windowEnd && windowBegin!=0;
    UploadSlice b;
    const uint32_t cpuCount = vk::cpu_vertex_count(g, R.vertexDiagnostics.cpuClasses);
    if (cpuCount) {
      instanceGeometry.valid = false;
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
    bufcache::Entry* vertexEntry = nullptr;
    if (R.vertexDiagnostics.noVertexReuse)
      b = snapshot(mem::ptr(addr), size, 4);  // the whole declared buffer, fresh every draw
    else if (!buffer_cache_enabled() || !cached_guest_range(addr, copied, rprof::kUpVertex, b, recordGeometry ? &vertexEntry : nullptr)) {
      instanceGeometry.valid = false;
      b = copyWindow
        ? vertex_window_snapshot(g.attributeBufferIndex,addr,copied,
                                 uint32_t(windowBegin),uint32_t(windowEnd-windowBegin))
        : vertex_snapshot(g.attributeBufferIndex, addr, copied,
                          supported && attributeEnd && extent.valid && vertexExtent.valid);
    } else if (recordGeometry) instanceGeometry.add(addr, copied, 0, b, vertexEntry);
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
  if (nativeIndices) {
    bindIndex(nativeIndexSlice.buffer, nativeIndexSlice.offset,
                         indexType == 0 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    recordDraw(true, nativeIndexSlice.buffer, nativeIndexSlice.offset,
               indexType == 0 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32, count);
    if(deferredPipelineDraw) { auto* pipeline=&p; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDrawIndexed(cmd, count, instances, 0, int32_t(baseVertex), drawFirstInstance); }); }
    else nativeDraw(true, count);
  } else if (convertedEntry) {
    const uint32_t converted = convertedEntry->outSize / 4;
    auto b = buffer_cache_slice(*convertedEntry, convertedEntry->outSize);
    rprof::guest_read(rprof::kUpIndex, indexAddr, guestBytes);
    bindIndex(b.buffer, b.offset, VK_INDEX_TYPE_UINT32);
    recordDraw(true, b.buffer, b.offset, VK_INDEX_TYPE_UINT32, converted);
    if(deferredPipelineDraw) { auto* pipeline=&p; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDrawIndexed(cmd, converted, instances, 0, int32_t(baseVertex), drawFirstInstance); }); }
    else nativeDraw(true, converted);
  } else if (indices.empty()) {
    recordDraw(false, VK_NULL_HANDLE, 0, VK_INDEX_TYPE_UINT32, count);
    if(deferredPipelineDraw) { auto* pipeline=&p; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDraw(cmd, count, instances, baseVertex, drawFirstInstance); }); }
    else nativeDraw(false, count);
  } else {
    auto b = snapshot(indices.data(), indices.size() * 4, 4);
    if (indexAddr) rprof::guest_read(rprof::kUpIndex, indexAddr, guestBytes);
    bindIndex(b.buffer, b.offset, VK_INDEX_TYPE_UINT32);
    recordDraw(true, b.buffer, b.offset, VK_INDEX_TYPE_UINT32, indices.size());
    const auto drawCount=indices.size(); if(deferredPipelineDraw) { auto* pipeline=&p; record::emit_owned([=] { if(pipeline->pipeline) gfxvk::vkCmdDrawIndexed(cmd, drawCount, instances, 0, int32_t(baseVertex), drawFirstInstance); }); }
    else nativeDraw(true, indices.size());
  }
  }
  if(R.gpuPassTimestampsEnabled) gpu_count_render_draw();
  for (auto *color : colors)
    if (color) mark_draw_target_written(color);
  if (depth) mark_draw_target_written(depth);
  R.drawCount++;
  if (aoPrivateReplay) {
    ao_private().frame = R.frame;
    return;
  }
  if (ao_hires_enabled() && colors[0] &&
      ps->kind == gfx::ProgramKind::DepthDownsample) {
    struct ReplayGuard {
      ReplayGuard() { aoPrivateReplay = true; ao_private().frame = ~0ull; }
      ~ReplayGuard() { aoPrivateReplay = false; }
    } guard;
    draw(r, prim, count, indexType, indexAddr, baseVertex, instances);
    // the replay changes later lookups (aoPrivateSource/aoPrivateFrame, private images)
    continued.valid = false;
    continued.invalidReason = kAoReplay;
  }
  finish_draw_batch();
  rprof::mark(rprof::kSubmit);
}
} // namespace gfxvk
