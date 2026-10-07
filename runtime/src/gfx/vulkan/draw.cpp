// Vulkan draw submission. Guest state conventions follow Cemu (MPL-2.0).
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "backend.h"
#include "buffer_cache.h"
#include "render_prof.h"
#include "runtime.h"
#include "shaders.h"
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
constexpr uint32_t kDepthDownsamplePS = 0x3BB9DE00, kOcclusionPS = 0x44BDFD00;
constexpr uint32_t kOcclusionVS = 0x44BDF900;
bool aoPrivateReplay = false;
uint32_t aoPrivateSource = 0;
uint64_t aoPrivateFrame = ~0ull;
Surface aoPrivateColor, aoPrivateDepth;
Surface* private_ao_surface(Surface& dst, const Surface* like) {
  const uint64_t logicalWidth = uint64_t(like->width) * 3 / 2;
  const uint64_t logicalHeight = uint64_t(like->height) * 3 / 2;
  const uint64_t physicalWidth = uint64_t(like->extent.width) * 3 / 2;
  const uint64_t physicalHeight = uint64_t(like->extent.height) * 3 / 2;
  const auto limit = R.properties.limits.maxImageDimension2D;
  if (!logicalWidth || !logicalHeight || logicalWidth > UINT32_MAX ||
      logicalHeight > UINT32_MAX || !physicalWidth || !physicalHeight ||
      physicalWidth > limit || physicalHeight > limit)
    throw std::runtime_error("private AO image dimensions exceed device limits");
  const uint32_t width = uint32_t(logicalWidth), height = uint32_t(logicalHeight);
  const VkExtent3D extent{uint32_t(physicalWidth), uint32_t(physicalHeight), 1};
  if (!dst.image || dst.width != width || dst.height != height ||
      dst.extent.width != extent.width || dst.extent.height != extent.height ||
      dst.fmt.pixel != like->fmt.pixel) {
    end_encoder();
    destroy_surface_image(&dst);
    dst = *like;
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
    // the entry's CPU copy, never candidate->slice.mapped (upload memory: see vertex_snapshot_history.h)
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
  return history.remember(address, size, mem::ptr(address), historyEnabled,
                          [](const void* bytes, size_t n) { return snapshot(bytes, n, 4); });
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
  // compares the entry's CPU copy of the window, never the mapped slice (vertex_snapshot_history.h)
  if(entry && entry->matches(address,reservation,begin,length)) {
    ++R.vertexReuseChecks;
    if(entry->equal(fresh)) {
      ++R.vertexReuseHits;R.vertexReuseBytes+=length;return entry->slice;
    }
  }
  // the slice gets the bytes of the entry's copy (one read of guest memory)
  const uint8_t* bytes=entry?entry->remember(address,reservation,begin,length,fresh):fresh;
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
  default: throw std::runtime_error("unsupported Vulkan primitive");
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
    throw std::runtime_error("unsupported blend factor");
  return t[v];
}
VkBlendOp blendop(uint32_t v) {
  static const VkBlendOp t[] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT,
                                VK_BLEND_OP_MIN, VK_BLEND_OP_MAX,
                                VK_BLEND_OP_REVERSE_SUBTRACT};
  if (v >= 5)
    throw std::runtime_error("unsupported blend operation");
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
    throw std::runtime_error("unsupported stencil operation");
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
struct Pipeline {
  VkPipeline pipeline{};
  VkPipelineLayout layout{};
  VkDescriptorSetLayout sets[2]{};
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
std::unordered_map<PipelineKey, Pipeline, PipelineKeyHash> pipelines;
PipelineKey pipeline_key(const uint32_t* r, const vk::Shader* vs, const vk::Shader* ps,
                         const LatteFetchShader* fs, VkPrimitiveTopology topology,
                         const std::array<Surface*, 8>& colors, const Surface* depth) {
  PipelineKey key;
  key.vs = vs->pipelineId;
  key.ps = ps->pipelineId;
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
    throw std::runtime_error("fetch shader has more vertex buffers than a pipeline key holds");
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
  for (int st = 0; st < 2; st++) {
    auto &m = shaders[st]->mapping;
    std::vector<VkDescriptorSetLayoutBinding> b;
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
      throw std::runtime_error("Vulkan transform feedback unsupported");
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
    VkShaderModuleCreateInfo mc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mc.codeSize = shaders[i]->spirv.size() * 4;
    mc.pCode = shaders[i]->spirv.data();
    vk_check(vkCreateShaderModule(R.device, &mc, nullptr, &modules[i]),
             "shader module");
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
  for (auto &g : fs->bufferGroups) {
    bool instance = false;
    BindingTrimMetadata trim;
    std::optional<LatteConst::VertexFetchType2> fetchRate;
    for (int j = 0; j < g.attribCount; j++) {
      auto &a = g.attrib[j];
      int loc = vs->mapping.attributeMapping[a.semanticId];
      if (loc < 0)
        continue;
      auto fmt = vk::vertex_format(a.format);
      if (fmt == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("unsupported vertex format");
      attributes.push_back(
          {uint32_t(loc), g.attributeBufferIndex, fmt, a.offset});
      // Mirror the conservative prefix rules using the actual pipeline format.
      if (trim.supported) {
        const uint32_t width = vertex_format_bytes(fmt);
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
        throw std::runtime_error("mixed vertex/instance rate in one buffer");
      fetchRate = a.fetchType;
      if (a.fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA) {
        if (a.aluDivisor != 1)
          throw std::runtime_error("instance divisor unsupported");
        instance = true;
      }
    }
    uint32_t stride =
        (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >>
         11) &
        0xFFFF;
    bindings.push_back({g.attributeBufferIndex, stride,
                        instance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                 : VK_VERTEX_INPUT_RATE_VERTEX});
    trim.stride = stride;
    p.bindingTrims.push_back(trim);
  }
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
  if (pipelineClip.get_ZCLIP_FAR_DISABLE()) {
    if (!R.enabledFeatures.depthClamp)
      throw std::runtime_error("device lacks depth clamp requested by guest");
    rs.depthClampEnable = VK_TRUE;
  }
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
      throw std::runtime_error("device lacks depth bias clamp requested by guest");
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
      throw std::runtime_error("color logic operation unsupported by device");
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
      throw std::runtime_error("unsupported color logic operation");
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
        throw std::runtime_error("dual-source blend unsupported by device");
      if (!R.constantAlphaColorBlendFactors &&
          (b.srcColorBlendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
           b.srcColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA ||
           b.dstColorBlendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
           b.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA))
        throw std::runtime_error(
            "constant alpha color blend unsupported by device");
    }
  if (ds.stencilTestEnable && !R.separateStencilMaskRef &&
      (ds.front.compareMask != ds.back.compareMask ||
       ds.front.writeMask != ds.back.writeMask ||
       ds.front.reference != ds.back.reference))
    throw std::runtime_error("separate stencil state unsupported by device");
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
  ci.pNext = &rc;
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
  const auto pipelineStarted = std::chrono::steady_clock::now();
  auto result = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci,
                                          nullptr, &p.pipeline);
  R.pipelineCreateNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-pipelineStarted).count();
  ++R.pipelineCreates;
  for (auto m : modules)
    vkDestroyShaderModule(R.device, m, nullptr);
  if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_HOST_MEMORY &&
      result != VK_ERROR_OUT_OF_DEVICE_MEMORY && result != VK_ERROR_DEVICE_LOST) {
    // A driver that cannot build one pipeline (Adreno: VK_ERROR_UNKNOWN on the
    // boat ride after the sword and shield) used to end the game. The pipeline
    // stays empty, its draws are skipped, and the two shaders go to captures/
    // (GLSL and SPIR-V) to look at.
    p.pipeline = VK_NULL_HANDLE;
    LOG("[vulkan] graphics pipeline failed (Vulkan result %d): vs %016llX ps %016llX; its draws are skipped",
        int(result), (unsigned long long)vs->key, (unsigned long long)ps->key);
    std::error_code captureDirError;  // this path must not throw: it replaces a crash
    std::filesystem::create_directories("captures", captureDirError);
    for (auto *shader : shaders) {
      char name[96];
      snprintf(name, sizeof name, "captures/pipeline-failed-%s-%016llX",
               shader->vertex ? "vs" : "ps", (unsigned long long)shader->key);
      if (FILE *f = fopen((std::string(name) + ".glsl").c_str(), "wb")) {
        fwrite(shader->glsl.data(), 1, shader->glsl.size(), f);
        fclose(f);
      }
      if (FILE *f = fopen((std::string(name) + ".spv").c_str(), "wb")) {
        fwrite(shader->spirv.data(), 4, shader->spirv.size(), f);
        fclose(f);
      }
    }
    return remember(pipelines.emplace(key, p).first->second);
  }
  vk_check(result, "graphics pipeline");
  R.pipelineCacheDirty = true;
  R.pipelineCacheChangedFrame = R.frame;
  return remember(pipelines.emplace(key, p).first->second);
}
struct SamplerMemo {
  VkDevice device = VK_NULL_HANDLE;
  std::array<uint32_t, 3> words{};
  bool compare = false, integer = false, forceAniso = false;
  VkSampler value = VK_NULL_HANDLE;
  bool matches(VkDevice nextDevice, const uint32_t* nextWords,
               bool nextCompare, bool nextInteger, bool nextForceAniso) const {
    return value && device == nextDevice && compare == nextCompare &&
        integer == nextInteger && forceAniso == nextForceAniso &&
        words[0] == nextWords[0] && words[1] == nextWords[1] && words[2] == nextWords[2];
  }
  void remember(VkDevice nextDevice, const uint32_t* nextWords,
                bool nextCompare, bool nextInteger, bool nextForceAniso, VkSampler nextValue) {
    device = nextDevice;
    std::memcpy(words.data(), nextWords, sizeof(words));
    compare = nextCompare; integer = nextInteger; forceAniso = nextForceAniso;
    value = nextValue;
  }
};
SamplerMemo samplerMemo;
VkSampler sampler(const uint32_t *words, bool compare, bool integer, bool allowAniso) {
  static const bool memo = [] {
    const char* e = std::getenv("WWHD_VK_SAMPLER_MEMO");
    return e && !std::strcmp(e, "1");
  }();
  const bool forceAniso = allowAniso && aniso_enabled();
  const bool stats = preparation_stats_enabled();
  if (stats) ++R.cpuPreparation.samplerRequests;
  if (memo && samplerMemo.matches(R.device, words, compare, integer, forceAniso)) {
    if (stats) ++R.cpuPreparation.samplerMemoHits;
    return samplerMemo.value;
  }
  if (stats) ++R.cpuPreparation.samplerMapLookups;
  auto remember = [&](VkSampler value) {
    if (memo) samplerMemo.remember(R.device, words, compare, integer, forceAniso, value);
    return value;
  };
  std::string key(reinterpret_cast<const char *>(words), 12);
  key.push_back(compare);
  key.push_back(integer);
  key.push_back(forceAniso);
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
        throw std::runtime_error("mirror-once sampler unsupported by device");
      return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    case 5:
    case 7:
      throw std::runtime_error(
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
  if (integer)
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
  if (R.enabledFeatures.samplerAnisotropy && !integer && anisotropy > 1) {
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
std::array<FeedbackScratch, kFeedbackUnitsPerStage * 2> feedbackScratch;
VkDeviceSize feedbackRetainedBytes = 0;
// the draw (R.drawCount while it is prepared) that last used each retained slot
std::array<uint64_t, kFeedbackUnitsPerStage * 2> feedbackUseDraw = [] {
  std::array<uint64_t, kFeedbackUnitsPerStage * 2> a; a.fill(UINT64_MAX); return a; }();
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
  Surface temporary;
  Surface* copy = &temporary;
  FeedbackScratch* slot = reuse && unit < kFeedbackUnitsPerStage
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
  vkCmdCopyImage(command_buffer(), source->image,
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
StageResources bind_stage(const uint32_t *r, vk::Shader *sh,
                          VkDescriptorSetLayout layout, bool dynamicUniforms, float sx, float sy,
                          const std::array<Surface *, 8> &colors,
                          Surface *depth, FeedbackStatsProbe* feedbackProbe) {
  StageResources out;
  auto &m = sh->mapping;
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
      throw std::runtime_error("stage descriptor write capacity exceeded");
    const size_t slot = useRanks ? size_t(rank) : size_t(writeCount);
    if (useRanks && (slot >= rankPlan.count || occupied[slot]))
      throw std::runtime_error("stage descriptor rank metadata mismatch");
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
    if (size > R.properties.limits.maxUniformBufferRange)
      throw std::runtime_error("uniform buffer exceeds device range");
    if(preparation_stats_enabled()) {
      ++R.cpuPreparation.uniformSnapshotCalls;
      R.cpuPreparation.uniformSnapshotBytes+=size;
    }
    static const bool reuseUniforms = [] {
      const char* value = std::getenv("WWHD_VK_REUSE_UNIFORM_SNAPSHOTS");
      return value && std::strcmp(value, "1") == 0;
    }();
    // compares CPU copies of the slots' bytes, never mapped upload memory (uniform_snapshot.h)
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
                           (sh->vertex ? 0 : 17) + logicalSlot, bytes, size, fresh)
        : fresh(bytes, size);
    if (reuseUniforms && preparation_stats_enabled()) {
      const auto& counts = uniformCache.counters;
      R.cpuPreparation.uniformReuseChecks = counts.checks;
      R.cpuPreparation.uniformReuseComparisons = counts.comparisons;
      R.cpuPreparation.uniformReuseHits = counts.hits;
      R.cpuPreparation.uniformReuseBytes = counts.reusedBytes;
    }
    if (bufferCount >= bufferInfos.size())
      throw std::runtime_error("stage uniform descriptor capacity exceeded");
    auto &info = bufferInfos[bufferCount++];
    info = {b.buffer, dynamicUniforms ? 0 : b.offset, b.size};
    if (dynamicUniforms) {
      if (b.offset > UINT32_MAX)
        throw std::runtime_error("dynamic uniform offset exceeds uint32 range");
      if (useRanks) {
        const auto rank = logicalSlot == 16 ? rankPlan.support : rankPlan.blocks[logicalSlot];
        if (rank >= rankPlan.count)
          throw std::runtime_error("stage uniform rank metadata mismatch");
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
  if (m.uniformVarsBufferBindingPoint >= 0) {
    const size_t capacity=preparation_stats_enabled()?supportUniforms.capacity():0;
    vk::pack_uniforms_into(r, *sh, supportUniforms, sx, sy);
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
  // Metal AO mode 2 tiles noise per 960x540 output pixel rather than 640x360.
  const int remapped = sh->uniforms.offset_remapped;
  if (sh->vertex && ao_mode() == 2 &&
      (r[mmSQ_PGM_START_VS] << 8) == kOcclusionVS && remapped >= 0 &&
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
      uniform(m.uniformBuffersBindingPoint[i], addr ? mem::ptr(addr) : nullptr,
              size, size_t(i), addr);
      if (addr) rprof::guest_read(rprof::kUpUbo, addr, size);
    }
  rprof::mark(rprof::kUniforms);
  uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS
                                : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
  for (int i = 0; i < sh->dec->textureUnitListCount; i++) {
    uint32_t unit = sh->dec->textureUnitList[i];
    if (unit >= LATTE_NUM_MAX_TEX_UNITS)
      throw std::runtime_error("sampled texture unit exceeds stage capacity");
    int binding = m.textureUnitToBindingPoint[unit];
    if (binding < 0)
      continue;
    auto *s = sampled_texture(r + texbase + unit * 7,
                              sh->dec->textureUsesDepthCompare[unit]);
    if (!s)
      throw std::runtime_error("missing sampled texture");
    if (!sh->vertex && ao_hires_enabled() && aoPrivateSource &&
        s->addr == aoPrivateSource && aoPrivateFrame == R.frame &&
        (r[mmSQ_PGM_START_PS] << 8) == kOcclusionPS)
      s = &aoPrivateColor;
    upload_surface(s);
    int scaleOffset = sh->uniforms.offset_texScale[unit];
    if (scaleOffset >= 0 && size_t(scaleOffset) + 8 <= supportUniforms.size()) {
      const float scale[] = {s->sx, s->sy};
      memcpy(supportUniforms.data() + scaleOffset, scale, sizeof scale);
    }
    bool aliases = depth && s->image == depth->image;
    for (auto *color : colors)
      if (color && s->image == color->image)
        aliases = true;
    VkImageView sampledView =
        aliases ? feedback_view(s, r + texbase + unit * 7, sh->vertex, unit, feedbackProbe)
                : sampled_texture_view(s, r + texbase + unit * 7);
    if (!aliases)
      transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT);
    uint32_t samplerId = sh->dec->textureUnitSamplerAssignment[unit];
    if (samplerId >= 18)
      throw std::runtime_error("missing texture sampler");
    uint32_t samplerBase = sh->vertex ? 18 : 0;
    if (imageCount >= imageInfos.size())
      throw std::runtime_error("stage image descriptor capacity exceeded");
    auto &info = imageInfos[imageCount++];
    const uint32_t* samplerWords = r + REGADDR::SQ_TEX_SAMPLER_WORD0_0 +
                                    (samplerBase + samplerId) * 3;
    uint32_t patchedSampler[3];
    if (!sh->vertex && ao_mode() >= 1 && unit == 0 &&
        (r[mmSQ_PGM_START_PS] << 8) == kOcclusionPS) {
      memcpy(patchedSampler, samplerWords, sizeof patchedSampler);
      patchedSampler[0] = (patchedSampler[0] & ~0x7E00u) | (1u << 9) | (1u << 12);
      samplerWords = patchedSampler;
    }
    info = {sampler(samplerWords, sh->dec->textureUsesDepthCompare[unit],
                   s->fmt.kind != FormatInfo::FLOAT,
                   !s->gpuWritten && s->mips > 1),
                             sampledView,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto &write = appendWrite(rankPlan.textures[unit]);
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
  }
  rprof::mark(rprof::kTextures);
  if (m.uniformVarsBufferBindingPoint >= 0)
    uniform(m.uniformVarsBufferBindingPoint, supportUniforms.data(),
            supportUniforms.size(), 16);
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
  auto &last = lastDescriptors[sh->vertex ? 0 : 1];
  if (descriptor_matches(last, layout, writes.data(), writeCount)) {
    ++R.descriptorCacheHits;
    ++R.descriptorFastHits;
    out.set = last.set;
    return out;
  }
  const uint64_t hash = descriptor_hash(layout, writes.data(), writeCount);
  if (auto found = descriptorCache.find(hash, layout, writes.data(), writeCount)) {
    ++R.descriptorCacheHits;
    out.set = found;
    remember_descriptors(last, layout, out.set, writes.data(), writeCount);
    return out;
  }
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = R.descriptorPool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layout;
  vk_check(vkAllocateDescriptorSets(R.device, &ai, &out.set), "descriptor set");
  ++R.descriptorAllocations;
  for (uint32_t i = 0; i < writeCount; ++i) writes[i].dstSet = out.set;
  if (writeCount)
    vkUpdateDescriptorSets(R.device, writeCount, writes.data(), 0, nullptr);
  descriptorCache.insert(hash, layout, out.set, writes.data(), writeCount);
  remember_descriptors(last, layout, out.set, writes.data(), writeCount);
  return out;
}
} // namespace
void reset_feedback_images() { reset_feedback_scratch(); }
UploadSlice vertex_window_smoke_snapshot(uint32_t binding,uint32_t address,
    uint32_t reservation,uint32_t windowOffset,uint32_t windowLength,
    const void* data,bool poisonUnused) {
  if(!data || !windowLength || windowOffset>reservation ||
     windowLength>reservation-windowOffset)
    throw std::runtime_error("invalid vertex window smoke bounds");
  return vertex_window_snapshot(binding,address,reservation,windowOffset,
                                windowLength,data,poisonUnused);
}

void reset_ao_private_cache() { aoPrivateFrame = ~0ull; aoPrivateSource = 0; }
void reset_pipeline_lookup_cache() { lastPipelineLookup = {}; pipelineLookaside = {}; samplerMemo = {}; drawBatchState = {}; }
uint64_t draw_batch_submissions() { return drawBatchSubmissions; }
void draw(const uint32_t *r, uint32_t prim, uint32_t count, uint32_t indexType,
          uint32_t indexAddr, uint32_t baseVertex, uint32_t instances) {
  if (!count || !instances || ((prim == 0x13 || prim == 0x14) && count < 4))
    return;
  if (r[REGADDR::PA_CL_CLIP_CNTL] & (1 << 22))
    return;
  ((uint32_t *)r)[REGADDR::VGT_PRIMITIVE_TYPE] = prim;
  uint64_t fsKey = 0;
  auto *fs = vk::get_fetch_shader(r, &fsKey, R.frame);
  if (!fs)
    throw std::runtime_error("missing Vulkan fetch shader");
  auto *vs = vk::translate(r, true, fs, fsKey, R.frame, g_shader_state_gen);
  auto *ps = vk::translate(r, false, fs, fsKey, R.frame, g_shader_state_gen,
                           vs && vs->ready() ? vs : nullptr);
  rprof::mark(rprof::kShader);
  if (!vs || !vs->ready() || !ps || !ps->ready())
    throw std::runtime_error("Vulkan shader translation failed: " +
                             (vs && !vs->ready() ? vs->error
                              : ps               ? ps->error
                                                 : "missing shader"));
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
  if (indexAddr && !nativeIndices && !guestWrap && buffer_cache_enabled() &&
      guestBytes <= (8u << 20) && uint64_t(indexAddr) + guestBytes <= 0x100000000ull) {
    const bufcache::Key key{indexAddr, bufcache::kIndexConverted,
        uint64_t(count) | uint64_t(prim & 0xff) << 32 | uint64_t(indexType & 0xff) << 40 |
            uint64_t(stripRestart) << 48 | uint64_t(specializeIndices) << 49,
        stripRestart ? restartIndex : 0};
    switch (buffer_cache().lookup(key, uint32_t(guestBytes), convertedEntry)) {
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
    default: throw std::runtime_error("unsupported Vulkan primitive");
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
      throw std::runtime_error("unsupported index type");
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
    throw std::runtime_error("unsupported Vulkan primitive");
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
  rprof::mark(rprof::kIndices);
  const auto &lcr = *reinterpret_cast<const LatteContextRegister *>(r);
  std::array<Surface *, 8> colors{};
  uint32_t slices[8]{}, depthSlice = 0;
  auto mask = LatteMRT::GetActiveColorBufferMask(ps->dec, lcr);
  for (int i = 0; i < 8; i++)
    if (mask & (1 << i))
      colors[i] = color_target(r, i, &slices[i]);
  Surface *depth = LatteMRT::GetActiveDepthBufferMask(lcr)
                       ? depth_target(r, &depthSlice)
                       : nullptr;
  const uint32_t guestWidth = colors[0] ? colors[0]->width : 0;
  const uint32_t guestHeight = colors[0] ? colors[0]->height : 0;
  if (aoPrivateReplay && colors[0]) {
    aoPrivateSource = colors[0]->addr;
    colors[0] = private_ao_surface(aoPrivateColor, colors[0]);
    slices[0] = 0;
    if (depth) {
      depth = private_ao_surface(aoPrivateDepth, depth);
      depthSlice = 0;
    }
  }
  Surface *target = depth;
  for (auto *c : colors)
    if (c) {
      target = c;
      break;
    }
  if (!target)
    return;
  uint32_t width = target->extent.width, height = target->extent.height;
  float sx = target->sx, sy = target->sy;
  if (aoPrivateReplay && guestWidth) {
    // the viewport registers describe the game's smaller buffer (x and y differ at other aspect ratios)
    sx = float(colors[0]->extent.width) / guestWidth;
    sy = guestHeight ? float(colors[0]->extent.height) / guestHeight : sx;
  }
  for (auto *&c : colors)
    if (c && (c->extent.width != width || c->extent.height != height)) {
      if (aoPrivateReplay) c = nullptr;
      else throw std::runtime_error("mismatched Vulkan attachments");
    }
  if (depth && (depth->extent.width < width || depth->extent.height < height))
    depth = nullptr;
  rprof::mark(rprof::kTargets);
  auto &p = pipeline(r, vs, ps, fs, topology, colors, depth);
  rprof::mark(rprof::kPipeline);
  if (!p.pipeline)
    return;  // the driver could not build it (logged once in pipeline())
  if(feedback_stats_enabled()) report_feedback_stats();
  if(vertex_window_stats_enabled()) report_vertex_window_stats();
  FeedbackStatsProbe feedbackProbe;
  auto* probe=feedback_stats_enabled()?&feedbackProbe:nullptr;
  auto vres = bind_stage(r, vs, p.sets[0], p.dynamicUniforms, sx, sy, colors, depth, probe);
  auto pres = bind_stage(r, ps, p.sets[1], p.dynamicUniforms, sx, sy, colors, depth, probe);
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
  static DrawStateCache state;
  // A tracked active pass can only contain our draw commands. Every pass end,
  // including submit/fence/reset, makes reusePass false on the next draw.
  // Reset conservatively on a new pass so external command recording cannot
  // leave this cache claiming dynamic state that was never set in this buffer.
  if (!reusePass) state = {};
  auto cmd = command_buffer();
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
    if(R.gpuPassTimestampsEnabled) gpu_begin_render_scope(colors,depth,slices,depthSlice,width,height);
    vkCmdBeginRendering(cmd, &ri);
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
  if (state.pipeline != p.pipeline) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    state.pipeline = p.pipeline;
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
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,p.layout,
        bind.first,bind.count,sets+bind.first,bind.offsetCount,
        dynamicOffsets.data()+bind.offsetBegin);
  float xs = f32(r[REGADDR::PA_CL_VPORT_XSCALE]),
        xo = f32(r[REGADDR::PA_CL_VPORT_XOFFSET]),
        ys = f32(r[REGADDR::PA_CL_VPORT_YSCALE]),
        yo = f32(r[REGADDR::PA_CL_VPORT_YOFFSET]),
        zs = f32(r[REGADDR::PA_CL_VPORT_ZSCALE]),
        zo = f32(r[REGADDR::PA_CL_VPORT_ZOFFSET]);
  LATTE_PA_CL_CLIP_CNTL clip;
  std::memcpy(&clip, r + REGADDR::PA_CL_CLIP_CNTL, 4);
  VkViewport vp{(xo - xs) * sx,
                (yo - ys) * sy,
                xs * 2 * sx,
                ys * 2 * sy,
                clip.get_DX_CLIP_SPACE_DEF() ? zo : zo - zs,
                zo + zs};
  // Bitwise comparison preserves distinct signed zeros and exact float state.
  if (!state.viewportValid || std::memcmp(&state.viewport, &vp, sizeof(vp))) {
    vkCmdSetViewport(cmd, 0, 1, &vp);
    state.viewport = vp;
    state.viewportValid = true;
  }
  uint32_t tl = r[REGADDR::PA_SC_GENERIC_SCISSOR_TL],
           br = r[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
  auto clamp = [](uint32_t v, float k, uint32_t limit) {
    return uint32_t(std::clamp(double(v) * k, 0.0, double(limit)));
  };
  uint32_t x = clamp(tl & 0x7fff, sx, width),
           y = clamp((tl >> 16) & 0x7fff, sy, height),
           ex = clamp(br & 0x7fff, sx, width),
           ey = clamp((br >> 16) & 0x7fff, sy, height);
  if (ex <= x || ey <= y) {
    end_encoder();
    return;
  }
  VkRect2D sc{{int32_t(x), int32_t(y)}, {ex - x, ey - y}};
  if (!state.scissorValid || state.scissor.offset.x != sc.offset.x ||
      state.scissor.offset.y != sc.offset.y ||
      state.scissor.extent.width != sc.extent.width ||
      state.scissor.extent.height != sc.extent.height) {
    vkCmdSetScissor(cmd, 0, 1, &sc);
    state.scissor = sc;
    state.scissorValid = true;
  }
  std::array<uint32_t, 4> blendBits;
  std::copy_n(r + REGADDR::CB_BLEND_RED, blendBits.size(), blendBits.begin());
  if (!state.blendValid || state.blend != blendBits) {
    vkCmdSetBlendConstants(
        cmd, reinterpret_cast<const float *>(r + REGADDR::CB_BLEND_RED));
    state.blend = blendBits;
    state.blendValid = true;
  }
  uint32_t stencilFront = r[REGADDR::DB_STENCILREFMASK] & 255;
  uint32_t stencilBack = r[REGADDR::DB_STENCILREFMASK_BF] & 255;
  if (!state.stencilValid || state.stencilFront != stencilFront)
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, stencilFront);
  if (!state.stencilValid || state.stencilBack != stencilBack)
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, stencilBack);
  state.stencilFront = stencilFront;
  state.stencilBack = stencilBack;
  state.stencilValid = true;
  rprof::mark(rprof::kRecord);
  rprof::UploadKind indexUploads(rprof::kUpIndex);
  // Scan the actual immutable index snapshot, never a second guest read: the bytes of the buffer
  // cache entry's shadow, or of a CPU copy that the upload slice is written from. Never the slice's
  // mapped memory: upload memory is uncached or write-combined on discrete GPUs (issue #44).
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
      nativeIndexCopy.assign(guest, guest + bytes);
      nativeIndexSlice = snapshot(nativeIndexCopy.data(), bytes, 4);
      nativeIndexData = nativeIndexCopy.data();
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
      throw std::runtime_error("missing vertex buffer");
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
    if (!buffer_cache_enabled() || !cached_guest_range(addr, copied, rprof::kUpVertex, b))
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
    const bool skip=skipVertexBinds && state.vertex_bind_matches(cmd,
        R.submissionGeneration,g.attributeBufferIndex,b.buffer,offset);
    if(preparation_stats_enabled()) {
      R.vertexBindCalls+=!skip;
      R.vertexBindSkips+=skip;
    }
    if(!skip)
      vkCmdBindVertexBuffers(cmd,g.attributeBufferIndex,1,&b.buffer,&offset);
  }
  rprof::g_upload_kind = rprof::kUpIndex;
  rprof::mark(rprof::kVertex);
  if (nativeIndices) {
    vkCmdBindIndexBuffer(cmd, nativeIndexSlice.buffer, nativeIndexSlice.offset,
                         indexType == 0 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, count, instances, 0, int32_t(baseVertex), 0);
  } else if (convertedEntry) {
    const uint32_t converted = convertedEntry->outSize / 4;
    auto b = buffer_cache_slice(*convertedEntry, convertedEntry->outSize);
    rprof::guest_read(rprof::kUpIndex, indexAddr, guestBytes);
    vkCmdBindIndexBuffer(cmd, b.buffer, b.offset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, converted, instances, 0, int32_t(baseVertex), 0);
  } else if (indices.empty())
    vkCmdDraw(cmd, count, instances, baseVertex, 0);
  else {
    auto b = snapshot(indices.data(), indices.size() * 4, 4);
    if (indexAddr) rprof::guest_read(rprof::kUpIndex, indexAddr, guestBytes);
    vkCmdBindIndexBuffer(cmd, b.buffer, b.offset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indices.size(), instances, 0, int32_t(baseVertex), 0);
  }
  if(R.gpuPassTimestampsEnabled) gpu_count_render_draw();
  for (auto *color : colors)
    if (color) mark_gpu_written(color);
  if (depth) mark_gpu_written(depth);
  R.drawCount++;
  if (aoPrivateReplay) {
    aoPrivateFrame = R.frame;
    return;
  }
  if (ao_hires_enabled() && colors[0] &&
      (r[mmSQ_PGM_START_PS] << 8) == kDepthDownsamplePS) {
    struct ReplayGuard {
      ReplayGuard() { aoPrivateReplay = true; aoPrivateFrame = ~0ull; }
      ~ReplayGuard() { aoPrivateReplay = false; }
    } guard;
    draw(r, prim, count, indexType, indexAddr, baseVertex, instances);
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
}
} // namespace gfxvk
