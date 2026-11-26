// native_draw/pkt_persist.h: persistent packet states (decomp build fast mode; on by default, WWHD_NATIVE_PKT_PERSIST=0
// turns them off). Included by gfx/vulkan/draw.cpp inside namespace gfxvk after the kept states (alt_states.h); the data is
// native_draw/pkt_persist_state.h.
//
// Today a packet kind's lowered state replays only inside its chain (no other draw in between) and in the same frame,
// submission, pass and surface epoch; the HUD's layout panes and text interleave with other draws, so almost every run
// of them starts with a full translated draw. Here a lowered state of the layout and text sites (the packets
// with instance register values) outlives its chain: up to kSlots states per packet kind are kept, and a kind whose own
// state cannot replay takes one whose inputs still hold:
// - registers: every register the lowering read (its programs' read set, j3ddescvk::read_regs, and the packet's compared
//   checked ranges; the instance's own registers aside, as for a replay) has the lowered value; block sizes, fetch
//   shader and strides as the replay compares them;
// - pass: the open pass is the lowering's (draw()'s reusePass test: targets, slices, size, attachment layouts);
// - textures: no surface made or dropped since (g_surface_struct_gen); the textures D1 looks up again (GPU-written with
//   mips, depth, several surfaces at the address) are resolved again and must give the same surface, view and sampler;
//   the surfaces' scales as packed; each one's upload and shader-read transition as the full draw makes them (a
//   transition that ends the pass: no take, the full draw begins the pass as it would);
// - memory: the guest index bytes as lowered (else uploaded again).
// In another submission the state's slices and sets are made again: the indices (as the full draw converts them), the
// kind's own vertex buffers (the whole declared buffer, as an instance stream is copied), the non-patched support
// uniforms (the lowered bytes), and each stage's descriptor set from the lowering's writes with this submission's
// buffers (dynamic stages: at the first replay, with its blocks' slices). After any other draw the replay sets the
// dynamic state (viewport, scissor as lowered; blend constants and stencil references from the registers) and binds
// pipeline, index and vertex buffers again. WWHD_NATIVE_PKT_PERSIST_VERIFY=1: every take's instance is drawn the full
// way and compared (the packet verify mode).
#pragma once

namespace pkpersist {
using pkpstate::Extra;
using pkpstate::ReadSet;
inline bool on() {
  static const bool v = [] {
    const char* e = std::getenv("WWHD_NATIVE_PKT_PERSIST");
    const bool on = !(e && *e == '0');
    if (on) LOG("[pkt-persist] persistent packet states on (WWHD_NATIVE_PKT_PERSIST=0 turns them off)");
    return on;
  }();
  return v;
}
inline bool verify_takes() {
  static const bool v = [] {
    const char* e = std::getenv("WWHD_NATIVE_PKT_PERSIST_VERIFY");
    return on() && e && *e == '1';
  }();
  return v;
}
inline bool site_on(const native_draw::Packet& pk) {
  return on() && pk.regCount && (pk.site == native_draw::kSiteLayout || pk.site == native_draw::kSiteText);
}
constexpr uint32_t kSlots = 256;
// a kind's kept states, found by the hash of their read set's values (the register file's hash over each read set the
// kind's states have, usually one)
struct Slots {
  std::vector<std::unique_ptr<LoweredKind>> slot;
  std::unordered_multimap<uint64_t, uint32_t> byHash;
  std::vector<const ReadSet*> sets;
};
inline Slots& slots_of(uint32_t id, uint32_t kind) {
  static auto* t = new std::array<std::array<Slots, native_draw::kMaxKinds>, native_draw::kMaxPackets>();
  return (*t)[id % native_draw::kMaxPackets][kind % native_draw::kMaxKinds];
}

// ---------------------------------------------------------------- counters ("[pkt-persist]" every 300 frames)
enum Count : uint32_t { cTakeOwn, cTakeSlot, cRebuilt, cMissNone, cMissStruct, cMissPassKey, cMissInputs, cMissRegs,
                        cMissPass, cMissTexture, cMissPassEnded, cMissRebuild, cStored, cMade, cRefused, cCounts };
static const char* const kCountNames[cCounts] = {"taken (own state)", "taken (kept state)", "of them rebuilt",
                                                 "miss: none kept", "miss: surfaces changed", "miss: range/pass",
                                                 "miss: instance inputs", "miss: registers", "miss: no open pass",
                                                 "miss: texture differs", "miss: transition ended the pass",
                                                 "miss: rebuild", "stored", "made", "not persisted"};
struct Counters {
  uint64_t n[native_draw::kSites][cCounts]{};
  std::unordered_map<std::string, uint64_t> why;
  uint64_t lastFrame = 0;
  bool started = false;
  size_t readSetWords = 0, readSets = 0;
  // (WWHD_PKT_PERSIST_TIME=1, measurement) steady-clock ns: takes that succeeded / failed, lowerings of the sites
  uint64_t tRebuild = 0, nRebuild = 0, tTex = 0, tFind = 0, tTake = 0, nTake = 0, tMiss = 0, nMiss = 0, tLower = 0, nLower = 0, lowerStart = 0;
};
inline bool timing() {
  static const bool v = [] { const char* e = std::getenv("WWHD_PKT_PERSIST_TIME"); return e && *e == '1'; }();
  return v;
}
inline uint64_t now_ns() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline Counters& counters() {
  static auto* c = new Counters();
  return *c;
}
inline void report() {
  auto& C = counters();
  for (uint32_t s = 0; s < native_draw::kSites; ++s) {
    uint64_t any = 0;
    for (uint32_t c = 0; c < cCounts; ++c) any += C.n[s][c];
    if (!any) continue;
    char buf[600];
    int k = std::snprintf(buf, sizeof buf, "[pkt-persist] site %u per frame:", s);
    for (uint32_t c = 0; c < cCounts && k < 560; ++c)
      if (C.n[s][c]) k += std::snprintf(buf + k, sizeof buf - k, " %s %.1f,", kCountNames[c], C.n[s][c] / 300.0);
    LOG("%s", buf);
  }
  {
    std::string w;
    for (const auto& [why, n] : C.why) w += " " + why + " " + std::to_string(n) + ";";
    LOG("[pkt-persist] not persisted (300 frames):%s read sets %zu (%.0f words each)", w.c_str(), C.readSets,
        C.readSets ? double(C.readSetWords) / C.readSets : 0.0);
  }
  if (timing())
    LOG("[pkt-persist] time per call: take %.2f us (%llu), failed take %.2f us (%llu), lowering %.2f us (%llu)",
        C.nTake ? C.tTake / 1e3 / C.nTake : 0.0, (unsigned long long)C.nTake, C.nMiss ? C.tMiss / 1e3 / C.nMiss : 0.0,
        (unsigned long long)C.nMiss, C.nLower ? C.tLower / 1e3 / C.nLower : 0.0, (unsigned long long)C.nLower);
  if (timing())
    LOG("[pkt-persist] time per take: find %.2f us, textures %.2f us; rebuild %.2f us (%llu)", C.nTake ? C.tFind / 1e3 / C.nTake : 0.0,
        C.nTake ? C.tTex / 1e3 / C.nTake : 0.0, C.nRebuild ? C.tRebuild / 1e3 / C.nRebuild : 0.0, (unsigned long long)C.nRebuild);
  C.tRebuild = C.nRebuild = C.tTex = C.tFind = 0;
  C.tTake = C.nTake = C.tMiss = C.nMiss = C.tLower = C.nLower = 0;
  std::memset(C.n, 0, sizeof C.n);
  C.why.clear();
}
inline void count(uint32_t site, Count c) {
  auto& C = counters();
  if (!C.started) C.started = true, C.lastFrame = R.frame;
  if (R.frame - C.lastFrame >= 300) { report(); C.lastFrame = R.frame; }
  ++C.n[site < native_draw::kSites ? site : 0][c];
}

// ---------------------------------------------------------------- the read set
inline const ReadSet* read_set(const PacketCapture& c, const native_draw::Packet& pk) {
  struct Key {
    const void *vs, *ps, *fs;
    uint32_t id;
    bool operator==(const Key& o) const { return vs == o.vs && ps == o.ps && fs == o.fs && id == o.id; }
  };
  struct KeyHash {
    size_t operator()(const Key& k) const {
      return std::hash<const void*>()(k.vs) ^ (std::hash<const void*>()(k.ps) << 1) ^
             (std::hash<const void*>()(k.fs) << 2) ^ k.id;
    }
  };
  static std::unordered_map<Key, std::unique_ptr<ReadSet>, KeyHash> sets;
  const Key key{c.vs, c.ps, c.fs, pk.id};
  if (auto it = sets.find(key); it != sets.end()) return it->second.get();
  std::vector<uint32_t> list;
  j3ddescvk::read_regs(c.vs, c.ps, c.fs, list);
  for (uint32_t i = 0; i < pk.checkCount && i < native_draw::kMaxChecks; ++i)
    if (native_draw::check_compare(pk.check[i]))
      for (uint32_t w = 0; w < native_draw::check_count(pk.check[i]); ++w) list.push_back(native_draw::check_first(pk.check[i]) + w);
  // the instance's own registers (the replay's inputs)
  std::vector<uint8_t> own(gx2::kNumRegs, 0);
  auto mark = [&](uint32_t first, uint32_t n) { for (uint32_t i = first; i < first + n && i < gx2::kNumRegs; ++i) own[i] = 1; };
  static const uint32_t stageBase[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START,
                                        mmSQ_GS_UNIFORM_BLOCK_START};
  for (uint32_t i = 0; i < pk.blockCount && i < native_draw::kMaxBlocks; ++i) {
    const uint32_t stage = pk.block[i] >> 16;
    mark(stageBase[stage < 3 ? stage : 0] + (pk.block[i] & 0xFFFF) * 7, 2);
  }
  for (uint32_t b = 0; b < 16; ++b) mark(mmSQ_VTX_ATTRIBUTE_BLOCK_START + b * 7, 2);
  mark(mmSQ_PGM_START_FS, 2);
  for (uint32_t i = 0; i < pk.regCount && i < native_draw::kMaxRegRanges; ++i)
    mark(mmSQ_ALU_CONSTANT0_0 + native_draw::reg_first(pk.reg[i]), pk.regWords[i]);
  for (uint32_t i = 0; i < pk.checkCount && i < native_draw::kMaxChecks; ++i)
    if (!native_draw::check_compare(pk.check[i])) mark(native_draw::check_first(pk.check[i]), native_draw::check_count(pk.check[i]));
  std::sort(list.begin(), list.end());
  list.erase(std::unique(list.begin(), list.end()), list.end());
  auto s = std::make_unique<ReadSet>();
  for (uint32_t reg : list)
    if (reg < gx2::kNumRegs && !own[reg]) s->regs.push_back(reg);
  for (uint32_t reg : s->regs) {
    if (s->runs.empty() || s->runs.back().first + s->runs.back().count != reg) s->runs.push_back({reg, 0});
    ++s->runs.back().count;
  }
  auto& C = counters();
  ++C.readSets;
  C.readSetWords += s->regs.size();
  return (sets[key] = std::move(s)).get();
}
inline bool values_hold(const ReadSet& s, const uint32_t* r, const std::vector<uint32_t>& v) {
  size_t at = 0;
  for (const auto& run : s.runs) {
    if (std::memcmp(r + run.first, v.data() + at, run.count * 4)) return false;
    at += run.count;
  }
  return true;
}
struct Hasher {
  uint64_t a = 0x9E3779B97F4A7C15ull, b = 0xC2B2AE3D27D4EB4Full;
  void add(const uint32_t* p, uint32_t n) {
    uint32_t i = 0;
    for (; i + 1 < n; i += 2) a = (a ^ p[i]) * 0x100000001B3ull, b = (b ^ p[i + 1]) * 0x100000001B3ull;
    if (i < n) a = (a ^ p[i]) * 0x100000001B3ull, a ^= a >> 29;
  }
  uint64_t done() const { return a ^ (b * 0x9E3779B97F4A7C15ull) ^ (b >> 31); }
};
inline uint64_t hash_regs(const ReadSet& s, const uint32_t* r) {
  Hasher h;
  for (const auto& run : s.runs) h.add(r + run.first, run.count);
  return h.done();
}
inline uint64_t hash_values(const ReadSet& s, const std::vector<uint32_t>& v) {
  Hasher h;
  size_t at = 0;
  for (const auto& run : s.runs) h.add(v.data() + at, run.count), at += run.count;
  return h.done();
}
inline uint32_t index_bytes(const native_draw::Mesh& m) {
  return m.count * (m.indexType == 4 || m.indexType == 0 ? 2u : 4u);
}

// ---------------------------------------------------------------- making (end of a lowering)
inline void make_(LoweredKind& L, const native_draw::Packet& pk, const native_draw::Mesh& m, const uint32_t* r);
inline void make(LoweredKind& L, const native_draw::Packet& pk, const native_draw::Mesh& m, const uint32_t* r) {
  make_(L, pk, m, r);
  auto& C = counters();
  if (timing() && C.lowerStart) C.tLower += now_ns() - C.lowerStart, ++C.nLower, C.lowerStart = 0;
}
inline void make_(LoweredKind& L, const native_draw::Packet& pk, const native_draw::Mesh& m, const uint32_t* r) {
  if (!L.persist) L.persist = std::make_unique<Extra>();
  Extra& E = *L.persist;
  E.set = nullptr;
  const PacketCapture& c = *L.cap;
  auto refuse = [&](const char* why) {
    E.why = why;
    count(pk.site, cRefused);
    ++counters().why[why];
  };
  if (!c.vs || !c.ps || !c.fs || !c.vs->dec || !c.ps->dec) return refuse("programs");
  if (!pass_state().rendering || !pass_state().tracked || !R.recording) return refuse("no pass");
  if (!c.indexed || !m.indexAddr || index_bytes(m) > 0x10000) return refuse("index form");
  for (uint32_t i = 0; i < c.vertexBindCount; ++i)
    if (c.vertexBinds[i].regs[1] + 1 > 0x40000 || !c.vertexBinds[i].regs[0]) return refuse("vertex buffer size");
  for (int s = 0; s < 2; ++s) {
    const auto& st = c.stage[s];
    if (!st.layout) return refuse("stage writes");
    for (uint32_t i = 0; i < st.writeCount; ++i) {
      int8_t src = -1;
      if (st.writes[i].pBufferInfo) {
        src = -2;
        for (int k = 0; k < PacketCapture::kBlocks; ++k)
          if (st.block[k].binding >= 0 && st.writes[i].dstBinding == uint32_t(st.block[k].binding)) src = int8_t(k);
        if (st.supBinding >= 0 && st.writes[i].dstBinding == uint32_t(st.supBinding)) src = 16;
        if (src == -2) return refuse("a uniform block of its own");  // (its content is not an input of the state)
      } else if (!st.writes[i].pImageInfo) return refuse("descriptor kind");
      E.src[s][i] = src;
    }
    if (st.supBinding >= 0 && st.dynamic && st.supIndex < 0) return refuse("support offset");
    for (uint32_t i = 0; i < L.stages[s].count; ++i) {
      const Surface* sf = L.stages[s].textures[i].surface;
      if (!sf) return refuse("texture");
      E.tex[s][i] = {sf->gpuWritten && (sf->mips > 1 || sf->isDepth || R.surfaces.count(sf->addr) > 1), sf->sx, sf->sy};
    }
  }
  E.set = read_set(c, pk);
  E.values.resize(E.set->regs.size());
  {
    size_t at = 0;
    for (const auto& run : E.set->runs) {
      std::memcpy(E.values.data() + at, r + run.first, run.count * 4);
      at += run.count;
    }
  }
  E.hash = hash_values(*E.set, E.values);
  const PassState& ps = pass_state();
  E.colors = ps.colors, E.slices = ps.slices, E.depth = ps.depth, E.depthSlice = ps.depthSlice;
  E.width = ps.width, E.height = ps.height;
  for (Surface* t : {E.colors[0], E.depth})  // (DrawTargets' target: the first colour target, else depth)
    if (t) { E.targetSx = t->sx, E.targetSy = t->sy; break; }
  E.structGen = g_surface_struct_gen;
  E.builtFor = E.setsFor = R.submissionGeneration;
  E.indexAddr = m.indexAddr;
  E.indexBytes.assign(static_cast<const uint8_t*>(mem::ptr(m.indexAddr)),
                      static_cast<const uint8_t*>(mem::ptr(m.indexAddr)) + index_bytes(m));
  E.supUploaded[0] = E.supUploaded[1] = false;
  E.why = nullptr;
  count(pk.site, cMade);
}

// ---------------------------------------------------------------- keeping
inline void flush_open() {
  if (pendingBatch.active) native_draw::flush_batch();  // (an open batch draws with the kind's state)
  if (mdiBatch.active) native_draw::flush_mdi();
}
inline bool keepable(const LoweredKind& L) { return L.valid && L.persist && L.persist->set; }
inline void unindex(Slots& S, uint32_t i) {
  const LoweredKind& k = *S.slot[i];
  if (!keepable(k)) return;
  auto range = S.byHash.equal_range(k.persist->hash);
  for (auto it = range.first; it != range.second; ++it)
    if (it->second == i) { S.byHash.erase(it); return; }
}
// slot I and L exchange their states (the index follows)
inline void swap_in(Slots& S, uint32_t i, LoweredKind& L) {
  unindex(S, i);
  kept_swap(L, *S.slot[i]);
  const LoweredKind& k = *S.slot[i];
  if (keepable(k)) {
    S.byHash.emplace(k.persist->hash, i);
    if (std::find(S.sets.begin(), S.sets.end(), k.persist->set) == S.sets.end()) S.sets.push_back(k.persist->set);
  }
}
// the kind's state goes into the kind's slots (a lowering replaces it, or its chain ends): L is left invalid
inline void store_(LoweredKind& L, uint32_t id, uint32_t kind, uint32_t site);
// (a lowering's start: L is the kind's state)
inline void store_lowering(LoweredKind& L, uint32_t id, uint32_t kind, uint32_t site) {
  if (timing()) counters().lowerStart = now_ns();
  store_(L, id, kind, site);
}
inline void store(LoweredKind& L, uint32_t id, uint32_t kind, uint32_t site) { store_(L, id, kind, site); }
inline void store_(LoweredKind& L, uint32_t id, uint32_t kind, uint32_t site) {
  if (!keepable(L)) return;
  Slots& S = slots_of(id, kind);
  // a free slot (none, or a state that no longer persists), else the least recently used one
  uint32_t to = ~0u;
  for (uint32_t i = 0; i < S.slot.size() && to == ~0u; ++i)
    if (!keepable(*S.slot[i])) to = i;
  if (to == ~0u && S.slot.size() < kSlots) {
    to = uint32_t(S.slot.size());
    S.slot.push_back(std::make_unique<LoweredKind>());
  }
  if (to == ~0u) {
    to = 0;
    for (uint32_t i = 1; i < S.slot.size(); ++i)
      if (S.slot[i]->frame < S.slot[to]->frame) to = i;
  }
  flush_open();
  swap_in(S, to, L);
  L.valid = false;
  count(site, cStored);
}

// ---------------------------------------------------------------- taking
// does state C hold for this instance (registers, pass, the replay's instance checks)
// (how far the best candidate got: 0 none kept, 1 surfaces made or dropped since, 2 another range or pass, 3 instance
// inputs, 4 registers)
inline int g_reach = 0;
inline bool holds(const LoweredKind& C, const native_draw::Packet& pk, const native_draw::Mesh& m, const uint32_t* r,
                  const uint32_t* blockSize) {
  if (!keepable(C)) return false;
  const Extra& E = *C.persist;
  if (E.structGen != g_surface_struct_gen) return g_reach = std::max(g_reach, 1), false;
  g_reach = std::max(g_reach, 2);
  if (E.indexAddr != m.indexAddr || C.rangeCount != m.count || C.rangeAddr != m.indexAddr) return false;
  const PassState& ps = pass_state();
  if (ps.colors != E.colors || ps.slices != E.slices || ps.depth != E.depth || ps.depthSlice != E.depthSlice ||
      ps.width != E.width || ps.height != E.height)
    return false;
  for (Surface* t : {E.colors[0], E.depth})
    if (t) {
      if (std::memcmp(&t->sx, &E.targetSx, 4) || std::memcmp(&t->sy, &E.targetSy, 4)) return false;
      break;
    }
  g_reach = std::max(g_reach, 3);
  for (uint32_t i = 0; i < pk.blockCount && i < native_draw::kMaxBlocks; ++i)
    if (blockSize[i] != C.blockSize[i]) return false;
  if (r[mmSQ_PGM_START_FS] != C.fsRegs[0] || r[mmSQ_PGM_START_FS + 1] != C.fsRegs[1]) {
    const uint32_t fsAddr = r[mmSQ_PGM_START_FS] << 8, fsSize = r[mmSQ_PGM_START_FS + 1] << 3;
    if (fsSize != C.fsBytes.size() || !fsAddr || std::memcmp(mem::ptr(fsAddr), C.fsBytes.data(), fsSize)) return false;
  }
  for (uint32_t i = 0; i < (*C.cap).vertexBindCount; ++i) {
    const auto& v = (*C.cap).vertexBinds[i];
    if (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + v.binding * 7 + 2] != v.regs[2]) return false;
  }
  g_reach = std::max(g_reach, 4);
  return values_hold(*E.set, r, E.values);
}
// the state's textures for a draw now: true when they are the lowered ones and their transitions kept the pass open
inline Count textures_now(LoweredKind& L, const uint32_t* r) {
  Extra& E = *L.persist;
  PacketCapture& c = *L.cap;
  for (int s = 0; s < 2; ++s) {
    vk::Shader* sh = s ? c.ps : c.vs;
    const uint32_t texbase = s ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
    const VkPipelineStageFlags stages = narrow_barriers()
        ? (s ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
        : (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    for (uint32_t i = 0; i < L.stages[s].count; ++i) {
      const auto& t = L.stages[s].textures[i];
      Surface* sf = t.surface;
      if (E.tex[s][i].unstable) {
        bool substituted = false;
        Surface* now = resolve_texture(r, sh, t.unit, &substituted);
        if (substituted || now != sf) return cMissTexture;
        if (sampled_texture_view(now, r + texbase + t.unit * 7) != t.view) return cMissTexture;
        if (resolve_sampler(r, sh, t.unit, now) != t.sampler) return cMissTexture;
      }
      if (std::memcmp(&sf->sx, &E.tex[s][i].sx, 4) || std::memcmp(&sf->sy, &E.tex[s][i].sy, 4)) return cMissTexture;
      upload_surface(sf);
      if (narrow_barriers() || sf->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
          (sf->passReadyFor && sf->passReadyFor == R.renderPassCount))
        sample_transition(sf, stages);
      if (!pass_state().rendering) return cMissPassEnded;
    }
  }
  for (Surface* col : E.colors)
    if (col && col->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return cMissPassEnded;
  if (E.depth && E.depth->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) return cMissPassEnded;
  return cCounts;
}
inline bool sets_due(const LoweredKind& L) { return L.persist && L.persist->setsFor != R.submissionGeneration; }
// the stage's writes with buffers for this submission (block K: BLOCKS[K]; support: SUP), as a set of this submission
inline VkDescriptorSet stage_set(LoweredKind& L, int s, const UploadSlice* blocks, const UploadSlice& sup) {
  auto& st = (*L.cap).stage[s];
  const Extra& E = *L.persist;
  static thread_local std::array<VkWriteDescriptorSet, 17 + LATTE_NUM_MAX_TEX_UNITS> writes;
  static thread_local std::array<VkDescriptorBufferInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> buffers;
  static thread_local std::array<VkDescriptorImageInfo, 17 + LATTE_NUM_MAX_TEX_UNITS> images;
  for (uint32_t i = 0; i < st.writeCount; ++i) {
    writes[i] = st.writes[i];
    if (E.src[s][i] >= 0) {
      buffers[i] = st.bufferInfos[i];
      const UploadSlice& b = E.src[s][i] == 16 ? (sup.buffer ? sup : E.sup[s]) : blocks[E.src[s][i]];
      buffers[i].buffer = b.buffer;
      if (!st.dynamic) buffers[i].offset = b.offset;
      st.bufferInfos[i] = buffers[i];
      writes[i].pBufferInfo = &buffers[i];
    } else {
      images[i] = st.imageInfos[i];
      writes[i].pImageInfo = &images[i];
    }
  }
  for (int k = 0; k < PacketCapture::kBlocks; ++k)
    if (st.block[k].binding >= 0) st.block[k].buffer = blocks[k].buffer;
  if (st.supBinding >= 0) st.supBuffer = sup.buffer ? sup.buffer : E.sup[s].buffer;
  return (*L.cap).sets[s] = stage_descriptor_set(s == 0, st.layout, writes.data(), st.writeCount);
}
// the state's slices for this submission: indices, the kind's vertex buffers, non-patched support uniforms; the sets of
// stages without per-instance buffers (the others: stage_set at the first replay)
inline bool upload_indices(PacketCapture& c, const native_draw::Mesh& m) {
  {
    rprof::UploadKind uploads(rprof::kUpIndex);
    UploadSlice ib;
    if (c.nativeIndexWidth) {
      ib = snapshot(mem::ptr(m.indexAddr), size_t(m.count) * c.nativeIndexWidth, 4);
      if (m.count != c.drawCount) return false;
    } else {
      static thread_local std::vector<uint32_t> converted;
      vk::convert_indices(mem::ptr(m.indexAddr), m.prim, m.count, m.indexType, c.stripRestart, c.restartIndex, converted);
      if (converted.size() != c.drawCount) return false;
      ib = snapshot(converted.data(), converted.size() * 4, 4);
    }
    rprof::guest_read(rprof::kUpIndex, m.indexAddr, index_bytes(m));
    c.indexBuffer = ib.buffer;
    c.indexOffset = ib.offset;
  }
  return true;
}
// the kind's own vertex buffers (bound by an instance with the lowered attribute words): the whole declared buffer as
// it is now (as an instance's stream is copied), at every take (other draws came in between)
inline void upload_vertices(PacketCapture& c) {
  for (uint32_t i = 0; i < c.vertexBindCount; ++i) {
    auto& v = c.vertexBinds[i];
    rprof::UploadKind uploads(rprof::kUpVertex);
    const UploadSlice b = snapshot(mem::ptr(v.regs[0]), v.regs[1] + 1, 4);
    rprof::guest_read(rprof::kUpVertex, v.regs[0], v.regs[1] + 1);
    v.buffer = b.buffer;
    v.offset = b.offset;
  }
}
inline bool rebuild(LoweredKind& L, const native_draw::Mesh& m) {
  PacketCapture& c = *L.cap;
  Extra& E = *L.persist;
  if (!upload_indices(c, m)) return false;
  for (int s = 0; s < 2; ++s) {
    auto& st = c.stage[s];
    bool perInstance = st.patches();
    for (const auto& blk : st.block) perInstance = perInstance || blk.binding >= 0;
    UploadSlice sup{};
    E.supUploaded[s] = false;
    if (st.supBinding >= 0 && !st.patches()) {
      rprof::UploadKind uploads(rprof::kUpUniformVars);
      sup = E.sup[s] = snapshot(st.supBytes.data(), st.supBytes.size(), R.properties.limits.minUniformBufferOffsetAlignment);
      E.supUploaded[s] = true;
      if (sup.offset > UINT32_MAX) return false;
      if (st.dynamic) c.offsets[st.supIndex] = uint32_t(sup.offset);
      else if (st.supWrite >= 0) st.bufferInfos[st.supWrite] = {sup.buffer, sup.offset, sup.size};
    }
    if (!perInstance) {
      UploadSlice none[PacketCapture::kBlocks]{};
      stage_set(L, s, none, sup);
    }
  }
  E.builtFor = R.submissionGeneration;
  E.setsFor = ~0ull;  // (dynamic stages with per-instance buffers: at the first replay)
  return true;
}
// the kind's state cannot replay: a kept state (or its own, from an earlier chain) that holds becomes its state (true)
inline bool take_(LoweredPacket& P, LoweredKind& L, const native_draw::Packet& pk, uint32_t kind,
                  const native_draw::Mesh& m, const uint32_t* r, const uint32_t* blockSize);
inline uint64_t g_takeStart = 0;
inline bool take(LoweredPacket& P, LoweredKind& L, const native_draw::Packet& pk, uint32_t kind,
                 const native_draw::Mesh& m, const uint32_t* r, const uint32_t* blockSize) {
  if (!timing()) return take_(P, L, pk, kind, m, r, blockSize);
  const uint64_t t = g_takeStart = now_ns();
  const bool ok = take_(P, L, pk, kind, m, r, blockSize);
  auto& C = counters();
  (ok ? C.tTake : C.tMiss) += now_ns() - t;
  ++(ok ? C.nTake : C.nMiss);
  return ok;
}
inline bool take_(LoweredPacket& P, LoweredKind& L, const native_draw::Packet& pk, uint32_t kind,
                  const native_draw::Mesh& m, const uint32_t* r, const uint32_t* blockSize) {
  if (!(R.recording && pass_state().rendering && pass_state().tracked)) { count(pk.site, cMissPass); return false; }
  Slots& S = slots_of(pk.id, kind);
  g_reach = 0;
  LoweredKind* C = holds(L, pk, m, r, blockSize) ? &L : nullptr;
  uint32_t ci = ~0u;
  for (size_t q = 0; !C && q < S.sets.size(); ++q) {
    auto range = S.byHash.equal_range(hash_regs(*S.sets[q], r));
    for (auto it = range.first; it != range.second && !C; ++it)
      if (S.slot[it->second]->persist->set == S.sets[q] && holds(*S.slot[it->second], pk, m, r, blockSize))
        C = S.slot[it->second].get(), ci = it->second;
  }
  if (!C && !S.byHash.empty()) g_reach = std::max(g_reach, 4);  // (no kept state with these register values)
  if (!C) {
    static const Count reach[5] = {cMissNone, cMissStruct, cMissPassKey, cMissInputs, cMissRegs};
    count(pk.site, reach[g_reach]);
    return false;
  }
  uint64_t t1 = timing() ? now_ns() : 0;
  if (timing()) counters().tFind += t1 - g_takeStart;
  const bool chainStart = P.drawCount != R.drawCount;
  if (C != &L) {
    flush_open();
    swap_in(S, ci, L);  // (the kind's old state goes to the slot)
  }
  if (const Count why = textures_now(L, r); why != cCounts) { count(pk.site, why); return false; }
  if (timing()) { const uint64_t t2 = now_ns(); counters().tTex += t2 - t1; t1 = t2; }
  Extra& E = *L.persist;
  if (E.builtFor != R.submissionGeneration) {
    if (!rebuild(L, m)) { count(pk.site, cMissRebuild); return false; }
    count(pk.site, cRebuilt);
    if (timing()) counters().tRebuild += now_ns() - t1, ++counters().nRebuild;
    E.indexBytes.assign(static_cast<const uint8_t*>(mem::ptr(m.indexAddr)),
                        static_cast<const uint8_t*>(mem::ptr(m.indexAddr)) + E.indexBytes.size());
  } else if (std::memcmp(mem::ptr(m.indexAddr), E.indexBytes.data(), E.indexBytes.size())) {
    // (the guest indices changed: uploaded again as the full draw would)
    if (!upload_indices(*L.cap, m)) { count(pk.site, cMissRebuild); return false; }
    E.indexBytes.assign(static_cast<const uint8_t*>(mem::ptr(m.indexAddr)),
                        static_cast<const uint8_t*>(mem::ptr(m.indexAddr)) + E.indexBytes.size());
  }
  upload_vertices(*L.cap);
  L.epoch = native_draw::g_epoch;
  L.journalPos = native_draw::g_journalPos;
  L.journalBumps = native_draw::g_journalBumps;
  L.submission = R.submissionGeneration;
  L.frame = R.frame;
  L.cmd = R.cmd;
  L.passCount = R.renderPassCount;
  L.surfaceEpoch = g_surface_epoch;
  if (chainStart) {  // the packet's other kinds belong to the old chain: kept
    for (uint32_t k = 0; k < native_draw::kMaxKinds; ++k)
      if (&P.kinds[k] != &L) store(P.kinds[k], pk.id, k, pk.site), P.kinds[k].valid = false;
    if (kept_states()) kept_end(pk.id);
  }
  P.boundKind = -1;
  P.mdiKind = -1;
  count(pk.site, C == &L ? cTakeOwn : cTakeSlot);
  return true;
}
// the replay after a take: the dynamic state as the lowering set it (another draw may have set its own since)
inline void dynamic_state(VkCommandBuffer cmd, const PacketCapture& c, const uint32_t* r) {
  record::vkCmdSetViewport(cmd, 0, 1, &c.viewport);
  record::vkCmdSetScissor(cmd, 0, 1, &c.scissor);
  record::vkCmdSetBlendConstants(cmd, reinterpret_cast<const float*>(r + REGADDR::CB_BLEND_RED));
  record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, r[REGADDR::DB_STENCILREFMASK] & 255);
  record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, r[REGADDR::DB_STENCILREFMASK_BF] & 255);
  g_packetDynamicDirty = true;
}
}  // namespace pkpersist
