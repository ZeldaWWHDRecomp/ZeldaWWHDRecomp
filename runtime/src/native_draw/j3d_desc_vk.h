// native_draw/j3d_desc_vk.h: J3D described draws on the Vulkan renderer (stage D1, gx2/native_j3d_desc_draw.h).
// Included by gfx/vulkan/draw.cpp inside namespace gfxvk, after the definitions it uses there (PacketCapture,
// stage_descriptor_set, resolve_texture, sample_transition, the buffer cache, packet_block_upload, track_drawn).
// Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only. The per-draw machinery is B2's material replay
// (native-j3dmat-b2d j3d_replay.h: per-(state, submission slot) descriptor sets, mesh indices from the buffer cache,
// block uploads through the submitted-uniform snapshot, support template + copy list), keyed by the description
// instead of a register image, with every input that B2 read from the register file per draw taken once at the
// state's making (from the item's effect or the ambient words) or from the draw's blocks.
//
// A state (per item and target pass) is made from the item's first translated draw into those targets (captured):
// pipeline, programs, descriptor layouts and writes, support bytes and copy list, textures (and their register words),
// vertex buffers (address, size, stride, extent), index form, viewport, scissor, blend constants, stencil references,
// the targets it writes, and the ambient words. Per draw drawn from the state (try_replay): the state found by index
// (item -> its states; the pass it matched last is remembered), the blocks' layout compared (count and registers), the
// ambient class checked once per run, the texture work B2 kept (upload / transition where not a no-op, an unstable
// texture looked up again from its stored words), indices from the mesh's buffer-cache entry, the blocks uploaded,
// support bytes patched, the slot's descriptor set reused, binds when changed, vkCmdDrawIndexed; then the draw's
// effect joins the run. Anything else: the translated path (counted by reason), which makes a new state.
#pragma once
// D2 (depth families on descriptions, branch j3d-desc-d2): wired when its header is in the tree, active only with
// WWHD_NATIVE_J3D_DESC_DEPTH=1 (depth::on()); without it the hooks below compile to nothing
#if __has_include("native_draw/j3d_desc_depth.h")
#include "native_draw/j3d_desc_depth.h"
#define J3DDESC_DEPTH 1
#else
#define J3DDESC_DEPTH 0
#endif

namespace j3ddescvk {
namespace d1 = native_draw::j3ddesc::d1;
using d1::In;
using d1::Item;

constexpr uint32_t kWrites = 17 + LATTE_NUM_MAX_TEX_UNITS;

struct Tex {
  Surface* surface = nullptr;
  VkImageView view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  uint32_t unit = 0;
  bool unstable = false;  // looked up again at each draw (from words)
  uint32_t words[7] = {}, samplerWords[3] = {};
  uint32_t samplerIndex = ~0u;  // the sampler register slot (vertex: + 18), ~0u: none
};
struct Stage {
  vk::Shader* sh = nullptr;
  bool dynamic = true;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  uint32_t writeCount = 0;
  std::array<VkWriteDescriptorSet, kWrites> writes{};
  std::array<VkDescriptorImageInfo, kWrites> imageInfos{};
  std::array<int8_t, kWrites> source{};   // a uniform buffer write: its block (0-15) or 16 = support; -1: an image
  std::array<int8_t, 17> offsetSource{};  // per dynamic offset: its block or support
  uint32_t dynCount = 0;
  uint32_t blockMask = 0;  // the blocks bound as buffers
  // per block: the draw's block that binds it (index into In::blocks), or -1: a fixed block (address and size taken
  // at the making, from ambient words or the item's effect)
  std::array<int8_t, 16> blockFrom{};
  std::array<uint32_t, 16> fixedAddr{}, fixedSize{};
  bool support = false;
  std::vector<uint8_t> sup;  // the support bytes (the first draw's)
  struct Copy { uint32_t dst, block, src; };
  std::vector<Copy> copies;  // kcache entries copied from uniform blocks into the support bytes
  std::vector<Tex> tex;
  VkPipelineStageFlags texStages = 0;
  struct SlotSet {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    uint64_t gen = ~0ull;
    bool written = false;
    std::array<VkBuffer, kWrites> buffers{};
    std::array<VkDeviceSize, kWrites> ranges{};
  };
  std::vector<SlotSet> slots;
  std::array<uint8_t, kWrites> bufWrite{};  // the uniform buffer writes (source >= 0)
  uint32_t bufCount = 0;
  uint64_t supSubmission = ~0ull;
  UploadSlice supSlice{};
  // (SUPPORT2, j3d_desc_support2.h) the copy list as runs, the blocks it reads, the slice kept for its inputs
  struct SupRun { uint32_t dst, block, src, len; };
  std::vector<SupRun> supRuns;
  uint32_t supBlocks = 0;
  bool supRunsMade = false;
  UploadSlice supKept{};
  uint64_t supKeptSubmission = ~0ull, supKeptUid = 0;
  std::array<uint32_t, 16> supKeptAddr{}, supKeptSize{};
};
// ambient words not written by J3D: interned (same registers, same values), checked once per run
struct Range { uint32_t first, count, at; };
struct Class {
  std::vector<uint32_t> regs, values;
  std::vector<Range> ranges;
  uint64_t checkedRun = 0;
  bool ok = false;
};
struct State {
  bool refused = false;
  uint64_t retryFrame = 0, lastUsed = 0;
  uint64_t passSerial = ~0ull, passSubmission = ~0ull;
  VkCommandBuffer passCmd = VK_NULL_HANDLE;
  std::array<Surface*, 8> colors{};
  std::array<uint32_t, 8> slices{};
  Surface* depth = nullptr;
  uint32_t depthSlice = 0, width = 0, height = 0;
  uint64_t structGen = 0;
  Pipeline* pipeline = nullptr;
  VkPipeline vkPipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  vk::Shader *vs = nullptr, *ps = nullptr;
  LatteFetchShader* fs = nullptr;
  // the draw's blocks as made with (first register each)
  uint32_t blockCount = 0;
  std::array<uint32_t, native_draw::j3ddesc::kMaxBlocks> blockReg{};
  // ambient: every ambient word with its value; split by W into a class and the J3D-written ones
  std::vector<uint32_t> ambientRegs, ambientValues;
  uint64_t wGen = 0;
  Class* cls = nullptr;
  std::vector<Range> vol;          // J3D-written ambient words (values in volValues)
  std::vector<uint32_t> volValues;
  uint32_t prim = 0, indexType = 0;
  Stage st[2];
  // addr/size: from the item's effect (fixed: live = false), else read from the register file at each draw
  struct VertexBind { uint32_t binding, stride, addr, size; uint64_t attributeEnd; bool supported, live; };
  std::vector<VertexBind> vertex;
  uint32_t nativeIndexWidth = 0;
  bool stripRestart = false, hostRestart = false;
  uint32_t restartIndex = 0;
  VkViewport viewport{};
  VkRect2D scissor{};
  std::array<uint32_t, 4> blend{};
  uint32_t stencilFront = 0, stencilBack = 0;
  uint32_t drawnColors = 0;  // track_drawn: the colour slots the draw writes
  bool drawnDepth = false, mainDepth = false;
  uint32_t absent = 0;
  bool beginOk = false;  // (PASSBEGIN) drawn into its own pass: may begin it (absent mask 0)
  void* depthFamily = nullptr;  // (D2) the depth family state chosen at the making (depth::State*), else the translated bind
  bool depthCanon = false;      // (D2 INST) commutes with collected depth draws when on their cascade (commutes_image)
};
#include "native_draw/j3d_desc_msmemo.h"  // (WWHD_NATIVE_J3D_DESC_MSMEMO=1)
#include "native_draw/dfix_timing.h"  // (D1TIMING: make_state / support build ns per call)
struct States {
  std::vector<std::unique_ptr<State>> list;
  MakeMemo memo;  // (MSMEMO) make_state's program-only work, last key
};
std::vector<std::unique_ptr<States>> g_states;  // owned here; Item::render points at one
std::vector<Item*> g_itemsWithStates;
std::unordered_map<uint64_t, std::vector<std::unique_ptr<Class>>> g_classes;
uint64_t g_verifyCounter = 0;
uint32_t g_said = 0;
std::vector<uint32_t> g_scratch;  // a register file holding only an unstable texture's words (resolve_texture)

struct Bound {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint64_t drawCount = ~0ull, pass = ~0ull;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VkDeviceSize indexOffset = 0;
  VkIndexType indexType = VK_INDEX_TYPE_MAX_ENUM;
  std::array<VkBuffer, 32> vb{};
  std::array<VkDeviceSize, 32> vbo{};
  bool dynamic = false;
  VkViewport viewport{};
  VkRect2D scissor{};
  std::array<uint32_t, 4> blend{};
  uint32_t stencilFront = 0, stencilBack = 0;
} g_bound;

// descriptor sets of the states: pools of their own (never reset with a submission); a dropped state's set is reused
// with its layout 64 frames later (it may still be bound in a submission in flight)
struct Pools {
  VkDevice device = VK_NULL_HANDLE;
  std::vector<VkDescriptorPool> pools;
  struct Garbage { VkDescriptorPool pool; VkDescriptorSet set; VkDescriptorSetLayout layout; uint64_t frame; };
  std::vector<Garbage> garbage;
  std::unordered_map<VkDescriptorSetLayout, std::vector<std::pair<VkDescriptorPool, VkDescriptorSet>>> spare;
} g_pools;
VkDescriptorSet allocate_set(VkDescriptorSetLayout layout, VkDescriptorPool& owner) {
  if (g_pools.device != R.device) g_pools = Pools{}, g_pools.device = R.device;
  if (auto it = g_pools.spare.find(layout); it != g_pools.spare.end() && !it->second.empty()) {
    owner = it->second.back().first;
    const VkDescriptorSet set = it->second.back().second;
    it->second.pop_back();
    return set;
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!g_pools.pools.empty()) {
      VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool = g_pools.pools.back();
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &layout;
      VkDescriptorSet set = VK_NULL_HANDLE;
      if (vkAllocateDescriptorSets(R.device, &ai, &set) == VK_SUCCESS) {
        owner = ai.descriptorPool;
        return set;
      }
    }
    constexpr uint32_t kSets = 512;
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSets * 17},
                                          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets * 17},
                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets * 20}};
    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.maxSets = kSets;
    ci.poolSizeCount = 3;
    ci.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(R.device, &ci, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
    g_pools.pools.push_back(pool);
  }
  return VK_NULL_HANDLE;
}
void collect_sets() {
  auto& g = g_pools.garbage;
  size_t k = 0;
  for (auto& x : g)
    if (R.frame > x.frame + 64) {
      if (g_pools.device == R.device) g_pools.spare[x.layout].push_back({x.pool, x.set});
    } else g[k++] = x;
  g.resize(k);
}
void drop_sets(State& s) {
  for (auto& S : s.st)
    for (auto& x : S.slots)
      if (x.set) g_pools.garbage.push_back({x.pool, x.set, S.layout, R.frame});
}
void mismatch(const In& in, const char* field) {
  d1::count(d1::cMismatch);
  native_draw::verify_mismatch("j3d described draw's state differs from the translated draw");
  if (g_said < 40 && ++g_said)
    LOG("[j3d-d1] VERIFY MISMATCH desc %u v%u env %u v%u (pass %u): %s (frame %llu)%s", in.item->desc->id,
        in.item->desc->version, in.item->env->id, in.item->env->version, in.item->desc->pass, field,
        (unsigned long long)R.frame, g_said == 40 ? " (no more of these lines)" : "");
}
bool on_pass(const State& s) {
  return R.recording && pass_state().rendering && pass_state().tracked && pass_state().colors == s.colors && pass_state().slices == s.slices &&
         pass_state().depth == s.depth && pass_state().depthSlice == s.depthSlice && pass_state().width == s.width && pass_state().height == s.height;
}

// ---------------------------------------------------------------- uploads (as the full draw makes them)
// a uniform block: display-list blocks once per submission per address (bind_stage's map), else packet_block_upload
UploadSlice block_upload(uint32_t addr, uint32_t size) {
  const uint8_t* submitted = addr ? gx2::submitted_uniform_block(addr, size) : nullptr;
  if (!submitted) return packet_block_upload(addr, size);
  struct Uploaded { uint32_t size; UploadSlice slice; };
  static std::unordered_map<uint32_t, Uploaded> uploaded;
  static uint64_t uploadedFor = 0, uploadedGeneration = 0;
  if (uploadedFor != gx2::submitted_uniforms_id() || uploadedGeneration != R.submissionGeneration) {
    uploaded.clear();
    uploadedFor = gx2::submitted_uniforms_id();
    uploadedGeneration = R.submissionGeneration;
  }
  auto it = uploaded.find(addr);
  if (it != uploaded.end() && it->second.size == size) return it->second.slice;
  rprof::UploadKind uploads(rprof::kUpUbo);
  const UploadSlice b = snapshot(submitted, size, R.properties.limits.minUniformBufferOffsetAlignment);
  rprof::guest_read(rprof::kUpUbo, addr, size);
  uploaded[addr] = {size, b};
  return b;
}
// the draw's address and size of block J of stage S (the draw's block or the fixed one)
// (decision 1 as refined, 2026-11-14: a block the program reads but the draw does not bind is taken live from the
// register file, as the run leaves it)
inline void block_of(const Stage& S, const In& in, uint32_t j, uint32_t& addr, uint32_t& size) {
  const int f = S.blockFrom[j];
  if (f >= 0) {
    addr = in.blocks[f].addr, size = std::min<uint32_t>(in.blocks[f].size, 0x10000);
    return;
  }
  const uint32_t reg = (S.sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START) + j * 7;
  uint32_t w1 = 0;
  d1::effective(reg, addr);
  d1::effective(reg + 1, w1);
  size = std::min<uint32_t>(w1 + 1, 0x10000);
}
// the support bytes of stage S as pack_uniforms_into makes them for this draw: the state's bytes with the kcache
// entries of the blocks copied again (a block without an address: its entries stay zero), into BYTES (S.sup.size())
void support_patch(const Stage& S, const In& in, uint8_t* bytes) {
  const size_t size = S.sup.size();
  uint32_t lastBlock = ~0u, address = 0, blockSize = 0;
  const uint8_t* submitted = nullptr;
  for (const auto& c : S.copies) {
    if (c.block != lastBlock) {
      lastBlock = c.block;
      if (c.block < 16) block_of(S, in, c.block, address, blockSize);
      else address = 0, blockSize = 0;
      submitted = address ? gx2::submitted_uniform_block(address, blockSize) : nullptr;
    }
    if (size_t(c.dst) + 16 > size) continue;
    if (!address) { std::memset(bytes + c.dst, 0, 16); continue; }
    const void* src = submitted && c.src + 16 <= blockSize ? static_cast<const void*>(submitted + c.src)
                                                           : ppc_ptr(address + c.src);
    std::memcpy(bytes + c.dst, src, 16);
#ifdef WWHD_DECOMP_FAST
    CHEAPIB_B1_SRC(bytes, c.dst, src)  // (cheapib b1: rebuilt from this pass's block on a replay; guest reads: not noted)
    if (!(submitted && c.src + 16 <= blockSize)) CHEAPIB_B1_GUEST(4, address, blockSize)
#endif
  }
  if (S.sh->gatherUnits && S.sh->uniforms.offset_remapped >= 0)
    for (const Tex& x : S.tex)
      if (S.sh->gatherUnits >> x.unit & 1)
        for (const auto& c : S.copies)
          if (size_t(c.dst) + 16 <= size)
            ::gfx::area_sample::scale_gather_grid(reinterpret_cast<float*>(bytes + c.dst), 1, x.surface->width,
                                                  x.surface->height, x.surface->sx, x.surface->sy);
}
#ifdef WWHD_DECOMP_FAST
// cheapib b1: the register entries of the support bytes (the state's, made with the ambient words of its class), as
// pack_uniforms_into lays them out: inherited ones are rebuilt from the register file on a replay. Registers J3D items
// write (d1's W set: deferred in a run, so not yet seen by the call's write list) count as the call's own (kept)
void b1_note_support_regs(const Stage& S, const uint8_t* bytes) {
  const vk::Shader* sh = S.sh;
  if (!sh || !sh->dec) return;
  const uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (sh->vertex ? 0x400 : 0);
  auto note = [&](int dst, uint32_t word) {
    if (word + 4 > 0x400) return;
    for (uint32_t w = 0; w < 4; ++w)
      if (d1::written(aluBase + word + w)) return;
    ::cheapib::b1_src_reg(bytes, uint32_t(dst), aluBase + word, 4);
  };
  if (sh->uniforms.offset_remapped >= 0)
    for (const auto& e : sh->dec->list_remappedUniformEntries_register)
      note(sh->uniforms.offset_remapped + e.mappedIndexOffset, e.indexOffset / 4);
  if (sh->uniforms.offset_uniformRegister >= 0)
    for (int i = 0; i < sh->uniforms.count_uniformRegister; ++i) note(sh->uniforms.offset_uniformRegister + 16 * i, 4 * i);
}
#endif
void support_bytes(const Stage& S, const In& in, std::vector<uint8_t>& bytes) {
  bytes = S.sup;
#ifdef WWHD_DECOMP_FAST
  CHEAPIB_B1_SRC_ARM(bytes.data(), 3)
#endif
  support_patch(S, in, bytes.data());
#ifdef WWHD_DECOMP_FAST
  if (::cheapib::g_b1_capturing) b1_note_support_regs(S, bytes.data());
#endif
}
#include "native_draw/j3d_desc_support2.h"  // (WWHD_NATIVE_J3D_DESC_SUPPORT2, default on; =0 off)
// the mesh's indices: the buffer-cache entry the full draw binds for this range (a converted entry made once, here or
// by a full draw); false: no entry (the translated path then)
bool mesh_indices(const State& s, const native_draw::j3ddesc::Mesh& m, UploadSlice& ib, VkIndexType& type,
                  uint32_t& drawCount, VertexExtent& extent) {
  if (!buffer_cache_enabled() || buffer_cache_verify() || !m.indexAddr || m.baseVertex) return false;
  rprof::UploadKind uploads(rprof::kUpIndex);
  if (s.nativeIndexWidth) {
    const uint32_t bytes = m.count * s.nativeIndexWidth;
    if (uint64_t(m.count) * s.nativeIndexWidth > (8u << 20)) return false;
    bufcache::Entry* entry = nullptr;
    if (!cached_native_indices(m.indexAddr, bytes, ib, entry) || !entry) return false;
    extent = cached_index_extent(*entry, entry->shadow.data(), m.count, s.nativeIndexWidth, s.hostRestart, 0);
    rprof::guest_read(rprof::kUpIndex, m.indexAddr, bytes);
    type = s.nativeIndexWidth == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    drawCount = m.count;
    return true;
  }
  static const bool specializeIndices = [] {
    const char* e = std::getenv("WWHD_VK_SPECIALIZE_INDICES");
    return e && !std::strcmp(e, "1");
  }();
  const uint64_t guestReads = m.prim == 0x12 ? std::max(m.count, 1u) : m.count;
  const uint64_t guestBytes = guestReads * (m.indexType == 4 ? 2 : 4);
  if (guestBytes > (8u << 20) || uint64_t(m.indexAddr) + guestBytes > 0x100000000ull) return false;
  const bufcache::Key key{m.indexAddr, bufcache::kIndexConverted,
      uint64_t(m.count) | uint64_t(m.prim & 0xff) << 32 | uint64_t(m.indexType & 0xff) << 40 |
          uint64_t(s.stripRestart) << 48 | uint64_t(specializeIndices) << 49,
      s.stripRestart ? s.restartIndex : 0};
  bufcache::Entry* entry = nullptr;
  switch (buffer_cache().lookup(key, uint32_t(guestBytes), entry)) {
  case bufcache::kHit: break;
  case bufcache::kMiss: {
    if (!entry || !(specializeIndices || (m.prim == 4 && (m.indexType == 4 || m.indexType == 9) && !s.stripRestart)))
      return false;
    static std::vector<uint32_t> converted;
    vk::convert_indices(mem::ptr(m.indexAddr), m.prim, m.count, m.indexType, s.stripRestart, s.restartIndex, converted);
    if (converted.empty() || !buffer_cache().upload(*entry, converted.data(), uint32_t(converted.size() * 4))) return false;
    rprof::add_upload(rprof::kUpIndex, converted.size() * 4);
    cached_index_extent(*entry, converted.data(), converted.size(), 4, s.hostRestart, 0);
    break;
  }
  case bufcache::kBypass: return false;
  }
  drawCount = entry->outSize / 4;
  ib = buffer_cache_slice(*entry, entry->outSize);
  extent = cached_index_extent(*entry, nullptr, drawCount, 4, s.hostRestart, 0);
  rprof::guest_read(rprof::kUpIndex, m.indexAddr, guestBytes);
  type = VK_INDEX_TYPE_UINT32;
  return true;
}

// ---------------------------------------------------------------- ambient words
// the registers a draw reads (B2's native_coverage read_regs): the fixed groups the resolution reads, the programs'
// texture and sampler words, their blocks' address/size words and uniform registers, the fetch's attribute words
void read_regs(const vk::Shader* vs, const vk::Shader* ps, const LatteFetchShader* fs, std::vector<uint32_t>& list) {
  static std::vector<uint32_t> fixed;
  if (fixed.empty())
    for (uint32_t reg = 0; reg < gx2::kNumRegs; ++reg) {
      const uint8_t g = gx2::state_group(reg);
      if (reg >= mmSQ_VTX_ATTRIBUTE_BLOCK_START && reg < mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7 * 16) continue;
      if (g == gx2::kSgShader || g == gx2::kSgFetch || g == gx2::kSgTargets || g == gx2::kSgOutput ||
          g == gx2::kSgOutputTargets || g == gx2::kSgOutputShader || g == gx2::kSgDynamic ||
          g == gx2::kSgStencilRefMask)
        fixed.push_back(reg);
    }
  list = fixed;
  for (const vk::Shader* sh : {vs, ps}) {
    if (!sh || !sh->dec) continue;
    const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (int i = 0; i < sh->dec->textureUnitListCount; ++i) {
      const uint32_t unit = sh->dec->textureUnitList[i];
      if (unit >= LATTE_NUM_MAX_TEX_UNITS) continue;
      for (uint32_t w = 0; w < 7; ++w) list.push_back(texbase + unit * 7 + w);
      const uint32_t si = sh->dec->textureUnitSamplerAssignment[unit];
      if (si < 18)
        for (uint32_t w = 0; w < 3; ++w) list.push_back(REGADDR::SQ_TEX_SAMPLER_WORD0_0 + ((sh->vertex ? 18 : 0) + si) * 3 + w);
    }
    const uint32_t blockBase = sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    const uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (sh->vertex ? 0x400 : 0);
    for (const auto& g : sh->dec->list_remappedUniformEntries_bufferGroups)
      for (uint32_t w = 0; w < 2; ++w) list.push_back(blockBase + g.kcacheBankIdOffset / 4 + w);
    for (const auto& q : sh->dec->list_quickBufferList)
      for (uint32_t w = 0; w < 2; ++w) list.push_back(blockBase + q.index * 7 + w);
    if (sh->uniforms.offset_remapped >= 0)
      for (const auto& e : sh->dec->list_remappedUniformEntries_register)
        for (uint32_t w = 0; w < 4; ++w)
          if (e.indexOffset / 4 + w < 0x400) list.push_back(aluBase + e.indexOffset / 4 + w);
    if (sh->uniforms.offset_uniformRegister >= 0)
      for (uint32_t w = 0; w < uint32_t(sh->uniforms.count_uniformRegister) * 4 && w < 0x400; ++w) list.push_back(aluBase + w);
  }
  if (fs)
    for (const auto& g : fs->bufferGroups)
      for (uint32_t w = 0; w < 3; ++w) list.push_back(mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + w);
}
void ranges_of(const std::vector<uint32_t>& regs, std::vector<Range>& out) {
  out.clear();
  for (uint32_t i = 0; i < regs.size(); ++i) {
    if (out.empty() || out.back().first + out.back().count != regs[i]) out.push_back({regs[i], 0, i});
    ++out.back().count;
  }
}
// the state's ambient words split by W (again when W grew): the class (interned) and the J3D-written words
void split(State& s) {
  s.wGen = d1::g_wGen;
  std::vector<uint32_t> regs, values;
  std::vector<uint32_t> vregs;
  s.volValues.clear();
  for (size_t i = 0; i < s.ambientRegs.size(); ++i) {
    if (d1::written(s.ambientRegs[i])) vregs.push_back(s.ambientRegs[i]), s.volValues.push_back(s.ambientValues[i]);
    else regs.push_back(s.ambientRegs[i]), values.push_back(s.ambientValues[i]);
  }
  ranges_of(vregs, s.vol);
  d1::add_watched(vregs.data(), vregs.size());
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < regs.size(); ++i) h = ((h ^ regs[i]) * 0x100000001B3ull ^ values[i]) * 0x100000001B3ull;
  auto& bucket = g_classes[h];
  for (auto& c : bucket)
    if (c->regs == regs && c->values == values) { s.cls = c.get(); return; }
  auto c = std::make_unique<Class>();
  c->regs = std::move(regs);
  c->values = std::move(values);
  ranges_of(c->regs, c->ranges);
  s.cls = c.get();
  bucket.push_back(std::move(c));
}
// the ambient words hold: the class once per run (registers no J3D draw writes change only between runs), the
// J3D-written ones against the register file with the run settled
const char* ambient_holds(State& s, const uint32_t* r) {
  if (s.wGen != d1::g_wGen) split(s);
  Class& c = *s.cls;
  if (c.checkedRun != d1::g_runSerial) {
    c.checkedRun = d1::g_runSerial;
    d1::count(d1::cClassChecks);
    d1::count(d1::cClassWords, c.regs.size());
    c.ok = true;
    for (const Range& g : c.ranges)
      if (std::memcmp(r + g.first, c.values.data() + g.at, g.count * 4)) {
        c.ok = false;
        if (d1::debug_on())
          for (uint32_t i = 0; i < g.count; ++i)
            if (r[g.first + i] != c.values[g.at + i]) {
              static char k[48];
              std::snprintf(k, sizeof k, "ambient %04X", g.first + i);
              d1::note(k);
              break;
            }
        break;
      }
  }
  if (!c.ok) {
    if (d1::debug_on())  // (the first differing word of this state's class)
      for (const Range& g : c.ranges)
        if (std::memcmp(r + g.first, c.values.data() + g.at, g.count * 4)) {
          for (uint32_t i = 0; i < g.count; ++i)
            if (r[g.first + i] != c.values[g.at + i]) {
              static char k[48];
              std::snprintf(k, sizeof k, "ambient (state) %04X", g.first + i);
              d1::note(k);
              break;
            }
          break;
        }
    return "ambient";
  }
  if (!s.vol.empty()) {
    d1::count(d1::cVolatileSettles);
    // each word as the run leaves it (looked up backwards in the run; a long run without it: settled first)
    bool same = true;
    uint32_t badReg = 0;
    for (const Range& g : s.vol) {
      for (uint32_t i = 0; i < g.count && same; ++i) {
        uint32_t v;
        if (!d1::effective(g.first + i, v)) d1::settle_run(), v = r[g.first + i];
        same = v == s.volValues[g.at + i];
        badReg = g.first + i;
      }
      d1::count(d1::cVolatileWords, g.count);
    }
    if (!same) {
      if (d1::debug_on()) {
        static char k[48];
        std::snprintf(k, sizeof k, "J3D-written ambient %04X", badReg);
        d1::note(k);
      }
      return "ambient (J3D-written)";
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------- the state from a translated draw's capture
// R: the register file with the run settled and the draw's effect applied (what the draw resolved from)
const char* make_state(State& s, const PacketCapture& cap, const uint32_t* r, const In& in) {
  dfixt::Scope makeTime(dfixt::kMake);
  if (!cap.drew) return "not drawn";
  if (cap.refused) return cap.refused;
  if (!cap.raster) return "raster reused";
  if (!cap.indexed || !cap.vkPipeline || !cap.pipeline || cap.pipeline->pendingCompile) return "pipeline or draw form";
  if (!cap.vs || !cap.ps || !cap.fs || !cap.vs->dec || !cap.ps->dec) return "programs";
  if (cap.vs->kind != gfx::ProgramKind::Other || cap.ps->kind != gfx::ProgramKind::Other) return "program kind";
  if (R.vertexDiagnostics.noVertexReuse) return "vertex diagnostics";
  if (in.mesh->baseVertex) return "base vertex";
  const auto& c = continued_eyes()[0];
  if (!c.valid || c.vs != cap.vs || c.ps != cap.ps || c.frame != R.frame) return "textures not continuable";
  if (!pass_state().rendering || pass_state().colors != cap.colors || pass_state().depth != cap.depth) return "pass";
  s.colors = pass_state().colors;
  s.slices = pass_state().slices;
  s.depth = pass_state().depth;
  s.depthSlice = pass_state().depthSlice;
  s.width = pass_state().width;
  s.height = pass_state().height;
  s.structGen = g_surface_struct_gen;
  s.pipeline = cap.pipeline;
  s.vkPipeline = cap.vkPipeline;
  s.layout = cap.layout;
  s.vs = cap.vs;
  s.ps = cap.ps;
  s.fs = cap.fs;
  s.prim = in.mesh->prim;
  s.indexType = in.mesh->indexType;
  s.nativeIndexWidth = cap.nativeIndexWidth;
  s.stripRestart = cap.stripRestart;
  s.hostRestart = cap.hostRestart;
  s.restartIndex = cap.restartIndex;
  s.viewport = cap.viewport;
  s.scissor = cap.scissor;
  s.mainDepth = cap.depth && cap.depth->width == 1280 && cap.depth->height == 720;
  s.absent = cap.absent;
  s.beginOk = !cap.absent;
  std::copy_n(r + REGADDR::CB_BLEND_RED, 4, s.blend.begin());
  s.stencilFront = r[REGADDR::DB_STENCILREFMASK] & 255;
  s.stencilBack = r[REGADDR::DB_STENCILREFMASK_BF] & 255;
  const uint32_t clearMask = pass_reuse::clear_mask(s.absent);
  s.drawnColors = 0;
  for (int i = 0; i < 8; ++i)
    if (s.colors[i] && writes_color(r, i, clearMask)) s.drawnColors |= 1u << i;
  s.drawnDepth = writes_depth(r, s.depth);
  s.vertex.clear();
  for (uint32_t i = 0; i < cap.vertexBindCount; ++i) {
    const auto& v = cap.vertexBinds[i];
    if (v.perInstance) return "instance-rate vertex buffer";
    if (v.binding >= g_bound.vb.size()) return "vertex binding";
    const uint32_t* a = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
    if (!a[0]) return "vertex buffer address";
    uint32_t w0, w1;
    const uint32_t reg = mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7;
    const bool fixed = in.item->value(reg, w0) && in.item->value(reg + 1, w1);
    s.vertex.push_back({v.binding, v.stride, a[0], a[1] + 1, v.attributeEnd, v.supported, !fixed});
  }
  s.blockCount = in.blockCount;
  for (uint32_t i = 0; i < in.blockCount; ++i) s.blockReg[i] = in.blocks[i].reg;
  for (int k = 0; k < 2; ++k) {
    Stage& S = s.st[k];
    const auto& cs = cap.stage[k];
    vk::Shader* sh = k ? cap.ps : cap.vs;
    const auto& mp = sh->mapping;
    S = Stage{};
    S.sh = sh;
    S.dynamic = cs.dynamic;
    S.layout = cs.layout;
    S.writeCount = cs.writeCount;
    if (!S.layout) return "descriptor writes";
    S.source.fill(-1);
    for (uint32_t i = 0; i < cs.writeCount; ++i) {
      S.writes[i] = cs.writes[i];
      S.imageInfos[i] = cs.imageInfos[i];
      if (S.writes[i].pBufferInfo) {
        const int b = int(S.writes[i].dstBinding);
        int src = b == mp.uniformVarsBufferBindingPoint ? 16 : -1;
        for (int j = 0; j < 16 && src < 0; ++j)
          if (mp.uniformBuffersBindingPoint[j] == b) src = j;
        if (src < 0) return "uniform write";
        S.source[i] = int8_t(src);
      }
      S.writes[i].pBufferInfo = nullptr;
      S.writes[i].pImageInfo = nullptr;
    }
    S.dynCount = S.dynamic ? cs.dynCount : 0;
    for (uint32_t i = 0; i < S.dynCount; ++i) {
      const int b = int(cs.dynBindings[i]);
      int src = b == mp.uniformVarsBufferBindingPoint ? 16 : -1;
      for (int j = 0; j < 16 && src < 0; ++j)
        if (mp.uniformBuffersBindingPoint[j] == b) src = j;
      if (src < 0) return "dynamic binding";
      S.offsetSource[i] = int8_t(src);
    }
    const uint32_t blockBase = k ? mmSQ_PS_UNIFORM_BLOCK_START : mmSQ_VTX_UNIFORM_BLOCK_START;
    S.blockFrom.fill(-1);
    for (int j = 0; j < 16; ++j) {
      if (mp.uniformBuffersBindingPoint[j] >= 0) S.blockMask |= 1u << j;
      for (uint32_t i = 0; i < in.blockCount; ++i)
        if (in.blocks[i].reg == blockBase + uint32_t(j) * 7) S.blockFrom[j] = int8_t(i);  // (the last bind wins)
      S.fixedAddr[j] = r[blockBase + j * 7];
      S.fixedSize[j] = std::min<uint32_t>(r[blockBase + j * 7 + 1] + 1, 0x10000);
    }
    S.support = mp.uniformVarsBufferBindingPoint >= 0;
    if (S.support) {
      if (cs.supBytes.empty()) return "support bytes";
      S.sup = cs.supBytes;
      if (sh->uniforms.offset_remapped >= 0)
        for (const auto& group : sh->dec->list_remappedUniformEntries_bufferGroups)
          for (const auto& entry : group.entries)
            S.copies.push_back({uint32_t(sh->uniforms.offset_remapped + entry.mappedIndexOffset),
                                uint32_t(group.kcacheBankIdOffset / 4 / 7), uint32_t(entry.indexOffset)});
    }
    S.texStages = narrow_barriers() ? (k ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
                                    : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    const uint32_t texbase = k ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
    const auto& stage = c.stages[k];
    for (uint32_t i = 0; i < stage.count; ++i) {
      const auto& x = stage.textures[i];
      Surface* sf = x.surface;
      if (!sf) return "texture";
      Tex t;
      t.surface = sf, t.view = x.view, t.sampler = x.sampler, t.unit = x.unit;
      t.unstable = sf->gpuWritten && (sf->mips > 1 || sf->isDepth || R.surfaces.count(sf->addr) > 1);
      std::copy_n(r + texbase + x.unit * 7, 7, t.words);
      if (const uint32_t si = sh->dec->textureUnitSamplerAssignment[x.unit]; si < 18) {
        t.samplerIndex = (k ? 0 : 18) + si;
        std::copy_n(r + REGADDR::SQ_TEX_SAMPLER_WORD0_0 + t.samplerIndex * 3, 3, t.samplerWords);
      }
      S.tex.push_back(t);
    }
    S.bufCount = 0;
    for (uint32_t i = 0; i < S.writeCount; ++i)
      if (S.source[i] >= 0) S.bufWrite[S.bufCount++] = uint8_t(i);
  }
  // the ambient words: what the draw reads besides the item's effect, the draw's blocks and the primitive
  MakeMemo* memo = msmemo_on() && in.item->render ? &static_cast<States*>(in.item->render)->memo : nullptr;
  static std::vector<uint32_t> memoKey;
  bool memoHit = false;
  if (memo) {
    msmemo_key(s, cap, in, memoKey);
    memoHit = memoKey == memo->key;
  }
  static std::vector<uint32_t> list;
  if (!memoHit) read_regs(cap.vs, cap.ps, cap.fs, list);
  // (and every word the state keeps from the register file, should a group above not hold it)
  if (!memoHit) {
  for (uint32_t w = 0; w < 4; ++w) list.push_back(REGADDR::CB_BLEND_RED + w);
  list.insert(list.end(), {uint32_t(REGADDR::DB_STENCILREFMASK), uint32_t(REGADDR::DB_STENCILREFMASK_BF),
                           uint32_t(REGADDR::CB_TARGET_MASK), uint32_t(REGADDR::DB_DEPTH_CONTROL)});
  }
  for (int k = 0; k < 2; ++k) {
    const uint32_t blockBase = k ? mmSQ_PS_UNIFORM_BLOCK_START : mmSQ_VTX_UNIFORM_BLOCK_START;
    // (blocks the draw does not bind: read live through the run, so watched)
    for (uint32_t mask = s.st[k].blockMask; mask; mask &= mask - 1)
      if (s.st[k].blockFrom[__builtin_ctz(mask)] < 0) {
        const uint32_t w[2] = {blockBase + __builtin_ctz(mask) * 7, blockBase + __builtin_ctz(mask) * 7 + 1};
        d1::add_watched(w, 2);
      }
    const uint32_t texbase = k ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
    if (!memoHit)
    for (const Tex& x : s.st[k].tex) {
      for (uint32_t w = 0; w < 7; ++w) list.push_back(texbase + x.unit * 7 + w);
      if (x.samplerIndex != ~0u)
        for (uint32_t w = 0; w < 3; ++w) list.push_back(REGADDR::SQ_TEX_SAMPLER_WORD0_0 + x.samplerIndex * 3 + w);
    }
  }
  s.ambientRegs.clear();
  s.ambientValues.clear();
  if (memoHit) {  // (MSMEMO) the list kept with the key, its values from R
    s.ambientRegs = memo->regs;
    s.ambientValues.resize(s.ambientRegs.size());
    for (size_t i = 0; i < s.ambientRegs.size(); ++i) s.ambientValues[i] = r[s.ambientRegs[i]];
    d1::count(d1::cMemoHits);
  } else {
  std::sort(list.begin(), list.end());
  list.erase(std::unique(list.begin(), list.end()), list.end());
  const Item& it = *in.item;
  for (uint32_t reg : list) {
    if (reg >= gx2::kNumRegs || reg == REGADDR::VGT_PRIMITIVE_TYPE) continue;
    if (std::binary_search(it.regs.begin(), it.regs.end(), reg)) continue;
    // decision 1 as refined (user 2026-11-14): words past the program's own counts (vertex semantics, VS output ids, PS
    // input controls) are ignored by rule (the resolution compare, VERIFY, is the proof); uniform-block words: live
    if ((reg >= mmSQ_VTX_SEMANTIC_0 && reg < mmSQ_VTX_SEMANTIC_0 + 32) || reg == mmSQ_VTX_SEMANTIC_CLEAR ||
        (reg >= mmSPI_VS_OUT_ID_0 && reg < mmSPI_VS_OUT_ID_0 + 10) || (reg >= mmSPI_PS_INPUT_CNTL_0 && reg < mmSPI_PS_INPUT_CNTL_0 + 32))
      continue;
    if ((reg >= mmSQ_VTX_UNIFORM_BLOCK_START && reg < mmSQ_VTX_UNIFORM_BLOCK_START + 16 * 7) ||
        (reg >= mmSQ_PS_UNIFORM_BLOCK_START && reg < mmSQ_PS_UNIFORM_BLOCK_START + 16 * 7) ||
        (reg >= mmSQ_GS_UNIFORM_BLOCK_START && reg < mmSQ_GS_UNIFORM_BLOCK_START + 16 * 7))
      continue;
    // a vertex buffer's address and size: per-draw inputs read at each draw (they change per frame for animated
    // meshes; the game sets them before the J3D site), not ambient words
    bool stream = false;
    for (const auto& v : s.vertex)
      stream = stream || (v.live && (reg == mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7 ||
                                     reg == mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7 + 1));
    if (stream) continue;
    bool bound = false;
    for (uint32_t i = 0; i < in.blockCount && !bound; ++i) bound = reg >= in.blocks[i].reg && reg < in.blocks[i].reg + 7;
    if (bound) continue;
    s.ambientRegs.push_back(reg);
    s.ambientValues.push_back(r[reg]);
  }
  if (memo) memo->key = memoKey, memo->regs = s.ambientRegs, memo->splitKept = false;
  }
  for (uint32_t i = 0; i < in.blockCount; ++i) d1::add_written_block(in.blocks[i].reg);  // (W: before the split)
  if (memoHit && memo->splitKept && memo->wGen == d1::g_wGen && memo->values == s.ambientValues) {
    s.wGen = memo->wGen, s.cls = memo->cls, s.vol = memo->vol, s.volValues = memo->volValues;  // (MSMEMO: split's result)
    d1::count(d1::cMemoClassKept);
  } else {
    split(s);
    if (memo) {
      memo->splitKept = true, memo->wGen = s.wGen, memo->values = s.ambientValues;
      memo->cls = s.cls, memo->vol = s.vol, memo->volValues = s.volValues;
    }
  }
#if J3DDESC_DEPTH
  // (D2) pass 0: the depth family, chosen from what this define resolved (R = the define's register image). Its other
  // inputs (poly offset, clip and mode words) are ambient words or the item's effect: a change makes a new state.
  namespace dp = native_draw::j3ddesc::depth;
  s.depthCanon = native_draw::j3ddesc::depth::commutes_image(r);  // (D2 INST: commutes with collected depth draws)
  if (dp::on() && in.item->desc->pass == 0) {
    dp::Query q;
    q.desc = in.item->desc;
    q.env = in.item->env;
    q.image = r;
    q.vs = s.vs, q.ps = s.ps, q.fetch = s.fs;
    if (Surface* d = pass_state().depth) q.depthFormat = int(d->fmt.pixel), q.depthStencil = d->fmt.stencil;
    for (int i = 0; i < 8; ++i)
      if (s.colors[i]) q.colorFormats[i] = int(s.colors[i]->fmt.pixel), q.colorCount = uint32_t(i + 1);
    const Stage& P = s.st[1];
    q.psSupport = P.sup.data();
    q.psSupportSize = uint32_t(P.sup.size());
    static std::array<dp::Query::Image, kWrites> images;
    for (uint32_t i = 0; i < P.writeCount; ++i)
      if (P.source[i] < 0)
        images[q.imageCount++] = {P.writes[i].dstBinding, uint64_t(reinterpret_cast<uintptr_t>(P.imageInfos[i].sampler)),
                                  uint64_t(reinterpret_cast<uintptr_t>(P.imageInfos[i].imageView)),
                                  uint32_t(P.imageInfos[i].imageLayout)};
    q.images = images.data();
    s.depthFamily = dp::choose(q);
  }
#endif
  return nullptr;
}

// verify: the translated draw's capture (and R: the register file it drew from) against the state
void compare(const State& s, const PacketCapture& cap, const uint32_t* r, const In& in) {
  if (!cap.drew) return;
  d1::count(d1::cVerified);
  if (cap.refused) return mismatch(in, cap.refused);
  if (cap.absent != s.absent) return mismatch(in, "pass subset");
  if (cap.vkPipeline != s.vkPipeline || cap.pipeline != s.pipeline || cap.layout != s.layout) return mismatch(in, "pipeline");
  if (cap.vs != s.vs || cap.ps != s.ps || cap.fs != s.fs) return mismatch(in, "programs");
  if (cap.colors != s.colors || cap.depth != s.depth) return mismatch(in, "targets");
  if (std::memcmp(&cap.viewport, &s.viewport, sizeof s.viewport) || std::memcmp(&cap.scissor, &s.scissor, sizeof s.scissor))
    return mismatch(in, "viewport or scissor");
  if (cap.nativeIndexWidth != s.nativeIndexWidth || cap.stripRestart != s.stripRestart) return mismatch(in, "index form");
  if (cap.vertexBindCount != s.vertex.size()) return mismatch(in, "vertex layout");
  for (uint32_t i = 0; i < cap.vertexBindCount; ++i) {
    const auto& a = cap.vertexBinds[i];
    const auto& b = s.vertex[i];
    const uint32_t* w = r + mmSQ_VTX_ATTRIBUTE_BLOCK_START + b.binding * 7;
    if (a.binding != b.binding || a.stride != b.stride || a.attributeEnd != b.attributeEnd || a.supported != b.supported)
      return mismatch(in, "vertex layout");
    if (w[0] != b.addr || w[1] + 1 != b.size) {
      if (!b.live) return mismatch(in, "vertex buffer words");
    }
  }
  if (!std::equal(s.blend.begin(), s.blend.end(), r + REGADDR::CB_BLEND_RED) ||
      s.stencilFront != (r[REGADDR::DB_STENCILREFMASK] & 255) || s.stencilBack != (r[REGADDR::DB_STENCILREFMASK_BF] & 255))
    return mismatch(in, "blend constants or stencil references");
  const auto& c = continued_eyes()[0];
  for (int k = 0; k < 2; ++k) {
    const Stage& S = s.st[k];
    const auto& cs = cap.stage[k];
    if (cs.dynamic != S.dynamic || cs.writeCount != S.writeCount || (S.dynamic && cs.dynCount != S.dynCount))
      return mismatch(in, "descriptor layout");
    for (uint32_t i = 0; i < S.writeCount; ++i) {
      if (cs.writes[i].dstBinding != S.writes[i].dstBinding || cs.writes[i].descriptorType != S.writes[i].descriptorType)
        return mismatch(in, "descriptor layout");
      if (S.source[i] < 0 && (cs.imageInfos[i].imageView != S.imageInfos[i].imageView ||
                              cs.imageInfos[i].sampler != S.imageInfos[i].sampler))
        return mismatch(in, "texture view or sampler");
    }
    if (!c.valid || c.stages[k].count != S.tex.size()) return mismatch(in, "textures");
    const uint32_t texbase = k ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
    for (uint32_t i = 0; i < S.tex.size(); ++i) {
      const Tex& x = S.tex[i];
      if (c.stages[k].textures[i].surface != x.surface || c.stages[k].textures[i].view != x.view ||
          c.stages[k].textures[i].sampler != x.sampler)
        return mismatch(in, "textures");
      if (std::memcmp(r + texbase + x.unit * 7, x.words, sizeof x.words) ||
          (x.samplerIndex != ~0u && std::memcmp(r + REGADDR::SQ_TEX_SAMPLER_WORD0_0 + x.samplerIndex * 3, x.samplerWords,
                                                sizeof x.samplerWords)))
        return mismatch(in, "texture words");
    }
    const uint32_t blockBase = k ? mmSQ_PS_UNIFORM_BLOCK_START : mmSQ_VTX_UNIFORM_BLOCK_START;
    for (uint32_t mask = S.blockMask; mask; mask &= mask - 1) {
      const int j = __builtin_ctz(mask);
      uint32_t addr, size;
      block_of(S, in, uint32_t(j), addr, size);
      if (addr != r[blockBase + j * 7] || size != std::min<uint32_t>(r[blockBase + j * 7 + 1] + 1, 0x10000))
        return mismatch(in, "uniform block words");
    }
    if (S.support) {
      static std::vector<uint8_t> bytes;
      support_bytes(S, in, bytes);
      if (bytes != cs.supBytes) return mismatch(in, k ? "pixel support uniforms" : "vertex support uniforms");
    }
  }
  for (int i = 0; i < 8; ++i)
    if (((s.drawnColors >> i) & 1) != uint32_t(s.colors[i] && writes_color(r, i, pass_reuse::clear_mask(s.absent))))
      return mismatch(in, "colour writes");
  if (s.drawnDepth != writes_depth(r, s.depth)) return mismatch(in, "depth writes");
}

// ---------------------------------------------------------------- the draw from the state
// CHECKONLY (verify): the checks and the texture work only, nothing recorded
// (WWHD_NATIVE_J3D_DESC_D1TIMING=1) the replay's phases in ns: checks, textures, indices + vertex words, blocks +
// support + sets, recording, after the draw
struct Phases {
  bool on;
  uint64_t t;
  static uint64_t now() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  Phases() : on(native_draw::switch_value("WWHD_NATIVE_J3D_DESC_D1TIMING", 0) != 0), t(on ? now() : 0) {}
  void mark(d1::Count c) {
    if (!on) return;
    const uint64_t n = now();
    d1::count(c, n - t);
    t = n;
  }
};
// BEGINPASS (j3d_desc_passbegin.h): the state was found by the draw's own target lookup; the pass is begun here when it
// is not the open one (after every check that can still send the draw the translated way)
#if defined(WWHD_DECOMP_FAST) && defined(WWHD_VK_SMOOTH)
#include "native_draw/pardecide_desc.h"  // (WWHD_NATIVE_PAR_DECIDE: the record step as one POD command)
#endif
const char* try_replay(State& s, const In& in, bool checkOnly, bool beginPass = false) {
  static Phases proto;
  Phases ph = proto;
  ph.t = ph.on ? Phases::now() : 0;
#if J3DDESC_DEPTH
  // (D2 INST) a draw that does not commute with the collected depth draws (not canonical, or another target) draws
  // them first; that ends the open pass, so this draw takes the translated path
  if (native_draw::j3ddesc::depth::g_instPending &&
      (!s.depthCanon || uint64_t(reinterpret_cast<uintptr_t>(s.depth)) != native_draw::j3ddesc::depth::inst_target())) {
    native_draw::j3ddesc::depth::inst_flush_now("described draw");
    return "binds";
  }
#endif
  if (s.structGen != g_surface_struct_gen) return "surfaces";
  const native_draw::j3ddesc::Mesh& m = *in.mesh;
  if (m.prim != s.prim || m.indexType != s.indexType || in.blockCount != s.blockCount) return "binds";
  for (uint32_t i = 0; i < in.blockCount; ++i)
    if (in.blocks[i].reg != s.blockReg[i]) return "binds";
  if (const char* why = ambient_holds(s, in.regs)) return why;
  ph.mark(d1::pChecks);
  // textures (the continued path's per-draw work, skipped where it would do nothing)
  const uint64_t epoch = g_surface_epoch, pass = R.renderPassCount;
  for (int k = 0; k < 2; ++k) {
    Stage& S = s.st[k];
    for (Tex& x : S.tex) {
      Surface* sf = x.surface;
      if (x.unstable) {  // (looked up again, from its words: the register file does not hold them during a run)
        if (g_scratch.size() != gx2::kNumRegs) g_scratch.assign(gx2::kNumRegs, 0);
        const uint32_t texbase = S.sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
        std::copy_n(x.words, 7, g_scratch.data() + texbase + x.unit * 7);
        if (x.samplerIndex != ~0u)
          std::copy_n(x.samplerWords, 3, g_scratch.data() + REGADDR::SQ_TEX_SAMPLER_WORD0_0 + x.samplerIndex * 3);
        bool substituted = false;
        Surface* now = resolve_texture(g_scratch.data(), S.sh, x.unit, &substituted);
        const VkImageView view = substituted ? VK_NULL_HANDLE : sampled_texture_view(now, g_scratch.data() + texbase + x.unit * 7);
        const VkSampler sampler = substituted ? VK_NULL_HANDLE : resolve_sampler(g_scratch.data(), S.sh, x.unit, now);
        if (substituted || now != sf || view != x.view || sampler != x.sampler) return "texture";
      } else if (sf->image && !sf->evicted && (sf->gpuWritten || sf->lastCheckedFrame == R.frame)) {
        sf->lastUsedFrame = R.frame;  // (upload_surface would return here)
      } else {
        upload_surface(sf);
      }
      if (narrow_barriers() || sf->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
          (sf->passReadyFor && sf->passReadyFor == R.renderPassCount))
        sample_transition(sf, S.texStages);
    }
  }
  bool needPass = false;
  if (beginPass) {  // (draw()'s reusePass test, after its textures)
    if (g_surface_epoch != epoch) return "texture";
    needPass = !on_pass(s);
    for (Surface* c : s.colors) needPass = needPass || (c && c->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    needPass = needPass || (s.depth && s.depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  } else {
  if (g_surface_epoch != epoch || R.renderPassCount != pass || !on_pass(s)) return "texture";
  for (Surface* c : s.colors)
    if (c && c->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return "texture";
  if (s.depth && s.depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) return "texture";
  }
  if (checkOnly) return nullptr;
  ph.mark(d1::pTextures);
  // indices (nothing recorded yet: a miss is still a plain translated draw)
  UploadSlice ib;
  VkIndexType indexType = VK_INDEX_TYPE_UINT32;
  uint32_t drawCount = 0;
  VertexExtent extent;
  if (!mesh_indices(s, m, ib, indexType, drawCount, extent)) return "index";
  // vertex buffers from the register file (a J3D-written one: the run settled first)
  uint32_t vaddr[16], vsize[16];
  {
    bool settled = false;
    for (size_t i = 0; i < s.vertex.size(); ++i) {
      if (!s.vertex[i].live) {
        vaddr[i] = s.vertex[i].addr, vsize[i] = s.vertex[i].size;
        continue;
      }
      d1::count(d1::cLiveVertex);
      const uint32_t reg = mmSQ_VTX_ATTRIBUTE_BLOCK_START + s.vertex[i].binding * 7;
      if (!settled && (d1::written(reg) || d1::written(reg + 1))) d1::settle_run(), settled = true;
      vaddr[i] = in.regs[reg];
      vsize[i] = in.regs[reg + 1] + 1;
      if (!vaddr[i]) return "index";  // (the translated draw raises)
    }
  }
  if (needPass) {  // (draw()'s own pass begin; nothing after it sends the draw the translated way but "uniform offset")
    begin_draw_pass(command_buffer(), s.colors, s.slices, s.depth, s.depthSlice, s.width, s.height);
    s.passSerial = R.renderPassCount, s.passCmd = R.cmd, s.passSubmission = R.submissionGeneration;
    d1::count(d1::cPassBegun);
  }
  // (D2) a depth family state: its pipeline, set and offsets in place of the translated ones
  bool familyDraw = false;
  VkPipeline pipe = s.vkPipeline;
  VkPipelineLayout pipeLayout = s.layout;
#if J3DDESC_DEPTH
  static native_draw::j3ddesc::depth::ProofIn g_depthProof;
  native_draw::j3ddesc::depth::Binding fam;
  if (s.depthFamily && !s.absent) {
    native_draw::j3ddesc::depth::Draw dd;
    dd.blocks = in.raw;
    dd.blockCount = in.rawCount;
    dd.upload = [](uint32_t addr, uint32_t size) -> native_draw::j3ddesc::depth::Slice {
      const UploadSlice b = block_upload(addr, size);
      return {uint64_t(reinterpret_cast<uintptr_t>(b.buffer)), uint64_t(b.offset), uint64_t(b.size)};
    };
    auto* ds = static_cast<native_draw::j3ddesc::depth::State*>(s.depthFamily);
    if (native_draw::j3ddesc::depth::inst_wanted(ds) && s.vertex.size() <= 16) {
      // (D2 INST) pass 0's family draws are collected per (state, mesh) across the cascade slices and drawn layered at
      // the next flush point (j3d_desc_depth_inst.h): nothing recorded here
      namespace dp = native_draw::j3ddesc::depth;
      dp::Geometry g;
      g.mesh = uint32_t(reinterpret_cast<uintptr_t>(&m) >> 4);
      for (size_t vi = 0; vi < s.vertex.size(); ++vi) {
        const auto& v = s.vertex[vi];
        const uint32_t copied = v.supported ? vertex_prefix_size(vsize[vi], v.stride, v.attributeEnd, extent) : vsize[vi];
        UploadSlice b;
        {
          rprof::UploadKind uploads(rprof::kUpVertex);
          if (!cached_guest_range(vaddr[vi], copied, rprof::kUpVertex, b)) b = snapshot(mem::ptr(vaddr[vi]), copied, 4);
          rprof::guest_read(rprof::kUpVertex, vaddr[vi], copied);
        }
        g.vertex[vi] = b.buffer, g.vertexOffset[vi] = b.offset, g.vertexBinding[vi] = v.binding;
      }
      g.vertexCount = uint32_t(s.vertex.size());
      g.index = ib.buffer, g.indexOffset = ib.offset, g.indexType = indexType, g.indexCount = drawCount;
      dp::Target tg;
      tg.image = s.depth->image;
      tg.surface = uint64_t(reinterpret_cast<uintptr_t>(s.depth));
      tg.format = s.depth->fmt.pixel;
      tg.stencil = s.depth->fmt.stencil;
      tg.layer = s.depthSlice, tg.width = s.width, tg.height = s.height;
      tg.viewport = s.viewport, tg.scissor = s.scissor;
      dp::Binding plain;
      const bool sampled = dp::proof_wanted() && dp::bind(ds, dd, plain);  // (INST proof: the per-slice native binding)
      if (dp::inst_collect(ds, dd, g, tg, sampled ? &plain : nullptr)) {
        // the draw's side effects, as below (nothing recorded: no draw count, no draw batch)
        in.regs[REGADDR::VGT_PRIMITIVE_TYPE] = m.prim;
        if (bgshaders::enabled() && !gamepad_backdrop_replaying() && s.drawnDepth) bgshaders::tracker().drawn(s.depth->addr, R.frame);
        for (int i = 0; i < 8; ++i)
          if (s.colors[i] && !(s.absent >> i & 1)) mark_draw_target_written(s.colors[i]);
        mark_draw_target_written(s.depth);
        if (s.mainDepth) R.mainDepthAddr = s.depth->addr;
        return nullptr;
      }
      if (dp::inst_target() && dp::inst_target() != tg.surface) {  // (collected for another target: drawn first)
        dp::inst_flush_now("described draw");
        return "binds";
      }
    }
    familyDraw = native_draw::j3ddesc::depth::bind(ds, dd, fam);
    if (familyDraw) {
      pipe = reinterpret_cast<VkPipeline>(fam.pipeline);
      pipeLayout = reinterpret_cast<VkPipelineLayout>(fam.layout);
    }
  }
#endif
  ph.mark(d1::pIndices);
#if J3DDESC_DEPTH
  const bool depthProof = familyDraw && native_draw::j3ddesc::depth::proof_wanted();  // (D2 proof: translated sets too)
#else
  constexpr bool depthProof = false;
#endif
  // uniform blocks, support bytes, descriptor sets, offsets
  VkDescriptorSet sets[2];
  std::array<uint32_t, 34> offsets{};
  uint32_t offsetCount = 0;
  for (int k = 0; k < 2 && (!familyDraw || depthProof); ++k) {
    Stage& S = s.st[k];
    UploadSlice slices[17];
    for (uint32_t mask = S.blockMask; mask; mask &= mask - 1) {
      const int j = __builtin_ctz(mask);
      uint32_t addr, size;
      block_of(S, in, uint32_t(j), addr, size);
      slices[j] = block_upload(addr, size);
    }
    if (S.support) {  // (D1TIMING: support build ns per call)
    dfixt::Scope supTime(dfixt::kSupport);
    if (support2_on()) {
      rprof::UploadKind uploads(rprof::kUpUniformVars);
      slices[16] = support2(S, in);
    } else {
      if (S.copies.empty() && S.supSubmission == R.submissionGeneration) slices[16] = S.supSlice;
      else {
        rprof::UploadKind uploads(rprof::kUpUniformVars);
        static std::vector<uint8_t> bytes;
        support_bytes(S, in, bytes);
        slices[16] = snapshot(bytes.data(), bytes.size(), R.properties.limits.minUniformBufferOffsetAlignment);
        if (S.copies.empty()) S.supSubmission = R.submissionGeneration, S.supSlice = slices[16];
      }
    }
    }
    static std::array<VkWriteDescriptorSet, kWrites> writes;
    static std::array<VkDescriptorBufferInfo, kWrites> buffers;
    static std::array<VkDescriptorImageInfo, kWrites> images;
    auto fill = [&]() {
      for (uint32_t i = 0; i < S.writeCount; ++i) {
        writes[i] = S.writes[i];
        if (S.source[i] >= 0) {
          const UploadSlice& b = slices[S.source[i]];
          buffers[i] = {b.buffer, S.dynamic ? 0 : b.offset, b.size};
          writes[i].pBufferInfo = &buffers[i];
        } else {
          images[i] = S.imageInfos[i];
          writes[i].pImageInfo = &images[i];
        }
      }
    };
    // the slot's set: as it is (the same buffers), rewritten (other buffers, not bound yet in this submission), else a
    // set of this submission (stage_descriptor_set; also non-dynamic uniforms)
    Stage::SlotSet* ss = nullptr;
    if (S.dynamic) {
      const size_t slot = R.activeSubmission;
      if (S.slots.size() <= slot) S.slots.resize(slot + 1);
      ss = &S.slots[slot];
      bool same = ss->written;
      for (uint32_t q = 0; q < S.bufCount && same; ++q) {
        const uint32_t i = S.bufWrite[q];
        same = ss->buffers[i] == slices[S.source[i]].buffer && ss->ranges[i] == slices[S.source[i]].size;
      }
      if (!same) {
        if (ss->gen == R.submissionGeneration) ss = nullptr;  // (bound earlier in this submission: not rewritten)
        else {
          if (!ss->set) ss->set = allocate_set(S.layout, ss->pool);
          if (!ss->set) ss = nullptr;
          else {
            fill();
            for (uint32_t i = 0; i < S.writeCount; ++i) writes[i].dstSet = ss->set;
            if (S.writeCount) vkUpdateDescriptorSets(R.device, S.writeCount, writes.data(), 0, nullptr);
            for (uint32_t i = 0; i < S.writeCount; ++i)
              if (S.source[i] >= 0) ss->buffers[i] = slices[S.source[i]].buffer, ss->ranges[i] = slices[S.source[i]].size;
            ss->written = true;
          }
        }
      }
    }
    if (ss) {
      ss->gen = R.submissionGeneration;
      sets[k] = ss->set;
    } else {
      fill();
      sets[k] = stage_descriptor_set(k == 0, S.layout, writes.data(), S.writeCount);
    }
    for (uint32_t i = 0; i < S.dynCount; ++i) {
      const UploadSlice& b = slices[S.offsetSource[i]];
      if (b.offset > UINT32_MAX) return "uniform offset";
      offsets[offsetCount++] = uint32_t(b.offset);
    }
  }
  ph.mark(d1::pSets);
  // record
  auto cmd = command_buffer();
  Bound& B = g_bound;
  if (B.cmd != cmd || B.drawCount != R.drawCount || B.pass != R.renderPassCount) {
    B = Bound{};
    B.cmd = cmd;
  }
#if defined(WWHD_DECOMP_FAST) && defined(WWHD_VK_SMOOTH)
  // (cheapib b1 captures the per-call commands of a logic pass's calls: no compact command while it captures)
  if (::native_draw::pardecide::level() >= 1 && !depthProof && !::cheapib::g_b1_capturing) {
#if J3DDESC_DEPTH
    if (familyDraw) {
      const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(fam.set);
      pdesc::record(cmd, B, s, pipe, pipeLayout, 1, &set, fam.offsetCount, fam.offsets, vaddr, vsize, extent, ib, indexType,
                    drawCount);
    } else
#endif
    pdesc::record(cmd, B, s, pipe, pipeLayout, 2, sets, offsetCount, offsets.data(), vaddr, vsize, extent, ib, indexType,
                  drawCount);
  } else {
#endif
  if (B.pipeline != pipe) {
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    B.pipeline = pipe;
  }
#if J3DDESC_DEPTH
  if (familyDraw) {
    const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(fam.set);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &set, fam.offsetCount, fam.offsets);
  } else
#endif
  record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 2, sets, offsetCount, offsets.data());
  for (size_t vi = 0; vi < s.vertex.size(); ++vi) {
    const auto& v = s.vertex[vi];
    const uint32_t copied = v.supported ? vertex_prefix_size(vsize[vi], v.stride, v.attributeEnd, extent) : vsize[vi];
    UploadSlice b;
    {
      rprof::UploadKind uploads(rprof::kUpVertex);
      if (!cached_guest_range(vaddr[vi], copied, rprof::kUpVertex, b)) b = snapshot(mem::ptr(vaddr[vi]), copied, 4);
      rprof::guest_read(rprof::kUpVertex, vaddr[vi], copied);
    }
    if (B.vb[v.binding] != b.buffer || B.vbo[v.binding] != b.offset) {
      record::vkCmdBindVertexBuffers(cmd, v.binding, 1, &b.buffer, &b.offset);
      B.vb[v.binding] = b.buffer;
      B.vbo[v.binding] = b.offset;
    }
#if J3DDESC_DEPTH
    if (depthProof && vi < 16) g_depthProof.vb[vi] = b.buffer, g_depthProof.vbo[vi] = b.offset, g_depthProof.vbBinding[vi] = v.binding;
#endif
  }
  if (B.indexBuffer != ib.buffer || B.indexOffset != ib.offset || B.indexType != indexType) {
    record::vkCmdBindIndexBuffer(cmd, ib.buffer, ib.offset, indexType);
    B.indexBuffer = ib.buffer;
    B.indexOffset = ib.offset;
    B.indexType = indexType;
  }
  if (!B.dynamic || std::memcmp(&B.viewport, &s.viewport, sizeof s.viewport))
    record::vkCmdSetViewport(cmd, 0, 1, &s.viewport), B.viewport = s.viewport, g_packetDynamicDirty = true;
  if (!B.dynamic || std::memcmp(&B.scissor, &s.scissor, sizeof s.scissor))
    record::vkCmdSetScissor(cmd, 0, 1, &s.scissor), B.scissor = s.scissor, g_packetDynamicDirty = true;
  if (!B.dynamic || B.blend != s.blend) {
    record::vkCmdSetBlendConstants(cmd, reinterpret_cast<const float*>(s.blend.data()));
    B.blend = s.blend;
    g_packetDynamicDirty = true;
  }
  if (!B.dynamic || B.stencilFront != s.stencilFront) {
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, s.stencilFront);
    B.stencilFront = s.stencilFront;
    g_packetDynamicDirty = true;
  }
  if (!B.dynamic || B.stencilBack != s.stencilBack) {
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, s.stencilBack);
    B.stencilBack = s.stencilBack;
    g_packetDynamicDirty = true;
  }
  B.dynamic = true;
  record::vkCmdDrawIndexed(cmd, drawCount, 1, 0, 0, 0);
#if defined(WWHD_DECOMP_FAST) && defined(WWHD_VK_SMOOTH)
  }
#endif
  g_packetBindsDirty = true;
  ph.mark(d1::pRecord);
#if J3DDESC_DEPTH
  if (depthProof) {  // (D2) the draw again, translated and native, into private targets (ends the pass: binds forgotten)
    auto& P = g_depthProof;
    P.pipeline = s.vkPipeline, P.layout = s.layout, P.sets[0] = sets[0], P.sets[1] = sets[1];
    P.offsets = offsets.data(), P.offsetCount = offsetCount, P.vbCount = uint32_t(std::min<size_t>(s.vertex.size(), 16));
    P.ib = ib.buffer, P.ibo = ib.offset, P.indexType = indexType, P.count = drawCount;
    P.viewport = s.viewport, P.scissor = s.scissor, P.blend = s.blend.data();
    P.stencilFront = s.stencilFront, P.stencilBack = s.stencilBack;
    for (int i = 0; i < 8; ++i) P.colors[i] = s.colors[i];
    P.depth = s.depth, P.depthSlice = s.depthSlice, P.width = s.width, P.height = s.height;
    P.vs = s.vs, P.ps = s.ps, P.fs = s.fs, P.native = fam;
    native_draw::j3ddesc::depth::proof(static_cast<native_draw::j3ddesc::depth::State*>(s.depthFamily), P);
    g_bound = Bound{};
  }
#endif
  // the translated draw's side effects
  in.regs[REGADDR::VGT_PRIMITIVE_TYPE] = m.prim;  // (draw() writes it so, not through apply_regs)
  if (bgshaders::enabled() && !gamepad_backdrop_replaying()) {
    auto& t = bgshaders::tracker();
    for (int i = 0; i < 8; ++i)
      if (s.drawnColors >> i & 1) t.drawn(s.colors[i]->addr, R.frame);
    if (s.drawnDepth) t.drawn(s.depth->addr, R.frame);
  }
  if (R.gpuPassTimestampsEnabled) gpu_count_render_draw();
  for (int i = 0; i < 8; ++i)  // (slots taken over by a pass subset aren't written)
    if (s.colors[i] && !(s.absent >> i & 1)) mark_draw_target_written(s.colors[i]);
  if (s.depth) mark_draw_target_written(s.depth);
  if (s.mainDepth) R.mainDepthAddr = s.depth->addr;
  R.drawCount++;
  B.drawCount = R.drawCount;
  B.pass = R.renderPassCount;
  static const uint32_t drawBatch = parse_draw_batch(std::getenv("WWHD_VK_DRAW_BATCH"));
  static const uint32_t drawBatchCap = parse_draw_batch_cap(std::getenv("WWHD_VK_DRAW_BATCH_CAP"));
  if (drawBatchState.after_draw(R.frame, drawBatch, drawBatchCap)) draw_batch_submit();
  ph.mark(d1::pAfter);
  return nullptr;
}

// the translated path: the run settled, the draw's effect applied, the full draw (captured when CAP)
void full_draw(const In& in, PacketCapture* cap) {
  d1::apply_effects(in);
  g_packetCapture = cap;
  try {
    const native_draw::j3ddesc::Mesh& m = *in.mesh;
    draw(in.regs, m.prim, m.count, m.indexType, m.indexAddr, m.baseVertex, 1);
  } catch (...) {
    g_packetCapture = nullptr;
    LOG("[j3d-d1] the translated draw of desc %u v%u env %u v%u (pass %u, mesh prim %u count %u) raised (frame %llu)",
        in.item->desc->id, in.item->desc->version, in.item->env->id, in.item->env->version, in.item->desc->pass,
        in.mesh->prim, in.mesh->count, (unsigned long long)R.frame);
    for (uint32_t u = 0; u < 18; ++u) {  // (its pixel texture units: words 0-2, written by the item or not)
      const uint32_t* t = in.regs + REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + u * 7;
      uint32_t v;
      if (t[2]) LOG("[j3d-d1]   ps unit %u: %08X %08X %08X%s%s", u, t[0], t[1], t[2],
                    in.item->value(REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + u * 7 + 1, v) ? " (item)" : "",
                    d1::written(REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + u * 7 + 1) ? " (J3D-written)" : "");
    }
    {
      std::string runs;
      const auto& R2 = in.item->runs;
      for (size_t k = 0; k + 1 < R2.size() && runs.size() < 900; k += 2) runs += " " + std::to_string(R2[k + 1]) + "@" + std::to_string(R2[k]);
      LOG("[j3d-d1]   item: %zu registers, runs (count@first, decimal):%s; segments %zu/%zu/%zu words, lists %zu bytes, env image %zu words",
          in.item->regs.size(), runs.c_str(), in.item->desc->segment[1].size(), in.item->desc->segment[2].size(),
          in.item->desc->segment[3].size(), in.item->desc->lists.size(), in.item->env->image.size());
    }
    LOG("[j3d-d1]   CB_COLOR0 base %08X info %08X, DB base %08X info %08X", in.regs[mmCB_COLOR0_BASE],
        in.regs[mmCB_COLOR0_INFO], in.regs[mmDB_DEPTH_BASE], in.regs[mmDB_DEPTH_INFO]);
    throw;
  }
  g_packetCapture = nullptr;
  d1::count(d1::cFull);
}
d1::Count reason_count(const char* why) {
  if (!std::strcmp(why, "surfaces")) return d1::fSurfaces;
  if (!std::strcmp(why, "binds")) return d1::fBinds;
  if (!std::strcmp(why, "ambient")) return d1::fAmbient;
  if (!std::strcmp(why, "ambient (J3D-written)")) return d1::fVolatile;
  if (!std::strcmp(why, "index")) return d1::fIndex;
  if (!std::strcmp(why, "uniform offset")) return d1::fOffset;
  return d1::fTexture;
}
#include "native_draw/j3d_desc_passbegin.h"  // (WWHD_NATIVE_J3D_DESC_PASSBEGIN=1)
}  // namespace j3ddescvk

void j3ddesc_clear() {
  using namespace j3ddescvk;
  for (Item* it : g_itemsWithStates) it->render = nullptr;
  g_itemsWithStates.clear();
  for (auto& x : g_states)
    for (auto& s : x->list) drop_sets(*s);
  g_states.clear();
  g_classes.clear();
#if J3DDESC_DEPTH
  native_draw::j3ddesc::depth::reset();
#endif
  g_pools.spare.clear();  // (layouts may go with the reset; those sets stay unused in their pools)
  g_bound = {};
}
void j3ddesc_draw(const native_draw::j3ddesc::d1::In& in) {
  using namespace j3ddescvk;
  static uint64_t housekept = 0;
  if (R.frame != housekept) {  // once per frame: sets of dropped states freed; every 600 frames unused states dropped
    housekept = R.frame;
#if J3DDESC_DEPTH
    native_draw::j3ddesc::depth::frame_end();  // (D2: its counter line every 300 frames)
#endif
    collect_sets();
    dfixt::report(R.frame);
    if (R.frame % 600 == 0)
      for (auto& x : g_states) {
        auto& v = x->list;
        for (size_t i = 0; i < v.size();)
          if (v[i]->lastUsed + 600 < R.frame) drop_sets(*v[i]), v.erase(v.begin() + i);
          else ++i;
      }
  }
  States* ss = static_cast<States*>(in.item->render);
  if (!ss) {
    g_states.push_back(std::make_unique<States>());
    ss = g_states.back().get();
    in.item->render = ss;
    g_itemsWithStates.push_back(in.item);
  }
  auto& list = ss->list;
  State* s = nullptr;
  const bool open = R.recording && pass_state().rendering && pass_state().tracked;
  for (auto& x : list) {
    if (open && x->passSerial == R.renderPassCount && x->passCmd == R.cmd && x->passSubmission == R.submissionGeneration) {
      s = x.get();
      break;
    }
    if (on_pass(*x)) {
      x->passSerial = R.renderPassCount, x->passCmd = R.cmd, x->passSubmission = R.submissionGeneration;
      s = x.get();
      break;
    }
  }
  bool beginPass = false;
  if (!s && passbegin_on() && !list.empty()) beginPass = (s = passbegin_find(list, in)) != nullptr;
  if (s) s->lastUsed = R.frame;
  if (s && s->refused && R.frame < s->retryFrame) {
    d1::count(d1::fRefusedState);
    full_draw(in, nullptr);
    return;
  }
  bool verify = s && !s->refused && d1::verify_every() && ++g_verifyCounter % d1::verify_every() == 0;
  if (s && !s->refused) {
    const char* why = try_replay(*s, in, verify, beginPass);
    if (!why && !verify) {
      d1::count(d1::cReplayed);
      d1::push_run(in);
      return;
    }
    if (why) {
      verify = false;
      d1::count(reason_count(why));
      d1::note(why);
      d1::note_site(why, *in.item);
    }
  } else if (!s) {
    d1::count(list.empty() ? d1::fNoState : d1::fNoPass);
    d1::note(list.empty() ? "no state" : !pass_state().rendering ? "no pass open" : "no state for the open pass");
    d1::note_site(list.empty() ? "no state" : !pass_state().rendering ? "no pass open" : "no state for the open pass", *in.item);
  }
  static PacketCapture cap;
  static Phases fproto;  // (D1TIMING: the fallback's full draw, then its state making)
  Phases fph = fproto;
  fph.t = fph.on ? Phases::now() : 0;
  cap = PacketCapture{};
  cap.keepWrites = true;
  full_draw(in, &cap);
  fph.mark(d1::tFallbackFullNs);
  struct MakeTimer { Phases& p; ~MakeTimer() { p.mark(d1::tFallbackMakeNs); } } makeTimer{fph};
  if (verify) compare(*s, cap, in.regs, in);
  auto fresh = std::make_unique<State>();
  fresh->lastUsed = R.frame;
  const char* why = make_state(*fresh, cap, in.regs, in);
  if (why) {
    if (!cap.drew) return;  // (nothing drawn: nothing learnt)
    fresh->refused = true;
    fresh->retryFrame = R.frame + 60;
    fresh->colors = pass_state().colors, fresh->slices = pass_state().slices, fresh->depth = pass_state().depth;
    fresh->depthSlice = pass_state().depthSlice, fresh->width = pass_state().width, fresh->height = pass_state().height;
    d1::count(d1::cStatesRefused);
    static std::unordered_set<std::string> said;
    if (said.size() < 64 && said.insert(why).second) LOG("[j3d-d1] state refused: %s", why);
    d1::note(why);
  } else {
    d1::count(d1::cStates);
  }
  for (auto& x : list)
    if (on_pass(*x) || (x->colors == fresh->colors && x->depth == fresh->depth && x->slices == fresh->slices &&
                        x->depthSlice == fresh->depthSlice)) {
      drop_sets(*x);
      x = std::move(fresh);
      return;
    }
  if (list.size() >= 8) drop_sets(*list.front()), list.erase(list.begin());
  list.push_back(std::move(fresh));
}
