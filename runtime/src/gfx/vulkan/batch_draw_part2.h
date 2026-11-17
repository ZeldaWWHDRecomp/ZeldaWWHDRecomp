// Same-state draw runs, part 2 (batch_draw.h): the batched variants, what the renderer takes, the chunked draw.
// Included by draw.cpp only, inside namespace gfxvk, before draw_stereo. PROVENANCE: as batch_draw.h.
// Draw threads (T1, drawthreads DESIGN.md): the held run's entry (batch_draw_run, from draw_stereo before any hand-off to
// a draw thread; gx2 holds runs on the command processor thread) runs on the render thread only, so its counters
// (g_batchWhy, g_batchPieces, g_batchDraws), batch_log's frame and batch_can_hold's log count stay plain. A draw thread
// that is ever handed a held run must carry draw_batch::current() in its job and move these first. The variant map is
// a shared cache all the same: under the shaders lock (batch_variant). The per-draw state the draw path reads
// (batch_draw(), bind_stage's piece buffers) is per thread.
#pragma once

inline bool batch_runs_on() {
  static const bool on = draw_batch::switch_on("WWHD_VK_BATCH_RUNS");
  return on;
}
// a draw whose run gx2 may hold: drawn with these registers on the render thread, one eye, no capture or replay
inline bool batch_can_hold(const uint32_t* r) {
  const bool stereoOff = stereo::frame.mode == stereo::Off, backdrop = gamepad_backdrop_capture_active() ||
             gamepad_backdrop_replaying(), coverage = reproj::hud_coverage_wanted(), gs = r[REGADDR::VGT_GS_MODE] != 0;
  bool capture = false;
#ifdef WWHD_NATIVE_DRAW
  capture = g_packetCapture != nullptr;
#endif
  const bool ok = batch_runs_on() && R.device && stereoOff && !backdrop && !coverage && !gs && !capture;
  // (diagnostics: what keeps draws from runs, the first few times; render thread only: gx2 and draw_stereo)
  static int logged = 0;
  if (!ok && logged < 4) {
    ++logged;
    LOG("[vulkan batch] no run held: on %d device %d stereo off %d backdrop %d HUD coverage %d geometry shader %d capture %d",
        batch_runs_on(), R.device != VK_NULL_HANDLE, stereoOff, backdrop, coverage, gs, capture);
  }
  return ok;
}
// (registered at the first draw: draw_stereo)
inline void batch_register() {
  static const bool registered = [] {
    draw_batch::can_hold() = batch_can_hold;
    LOG("[vulkan batch] same-state runs %s (WWHD_VK_BATCH_RUNS)", batch_runs_on() ? "on" : "off");
    return true;
  }();
  (void)registered;
}

// a vertex shader's batched variant for some blocks: rewritten and compiled on a thread of its own the first time
struct BatchVariant {
  std::atomic<int> state{0};  // 0 being made, 1 ready, 2 failed
  const vk::Shader* base = nullptr;
  const LatteDecompilerShader* baseDec = nullptr;
  std::unique_ptr<vk::Shader> shader;
  vk::batch::Variant layout;
};
// (under the shaders lock: batch_variant; its entries are read by the draw path through BatchDraw, and their state by
// the compile thread's release store)
inline std::map<std::pair<uint64_t, uint32_t>, std::shared_ptr<BatchVariant>> g_batchVariants;
inline std::atomic<uint64_t> g_batchVariantsMade{0}, g_batchVariantsFailed{0};
// blocks: the batched guest blocks (bit i); bit 16: the support block too
inline BatchVariant* batch_variant(vk::Shader* base, uint32_t blocks) {
  draw_locks::Guard locked(draw_locks::kShaders);  // (draw threads: a shared cache)
  auto& slot = g_batchVariants[{base->key, blocks}];
  // (a shader cache cleared since: the variant was made from another translation; a new one is made)
  if (slot && (slot->base != base || slot->baseDec != base->dec)) slot.reset();
  if (slot) return slot.get();
  slot = std::make_shared<BatchVariant>();
  slot->base = base;
  slot->baseDec = base->dec;
  auto sh = std::make_unique<vk::Shader>(*base);  // (the base is ready: its fields no longer change)
  const uint64_t salt = 0xBA7C4ED000000000ull ^ (0x9E3779B97F4A7C15ull * (uint64_t(blocks) + 1));
  sh->key = base->key ^ salt;
  sh->pipelineId = base->pipelineId ^ salt;
  sh->spirvWords.reset();
  sh->binary.reset();
  sh->glsl.clear();
  sh->translatedGlsl.clear();
  sh->retainedSource.reset();
  slot->shader = std::move(sh);
  slot->layout.blocks = blocks & 0xFFFF;
  slot->layout.support = blocks >> 16 & 1;
  slot->layout.supportBytes = uint32_t((std::max(base->uniforms.offset_endOfBlock, 16) + 15) & ~15);  // (pack_uniforms_into)
  std::thread([v = slot, source = base->retainedSource, maxRange = R.properties.limits.maxUniformBufferRange] {
    std::string glsl, why;
    bool ok = false;
    try {
      ok = source && vk::batch::rewrite(*source, v->layout, maxRange, glsl, &why);
      if (!source) why = "no shader text";
      if (ok) {
        auto compiled = vk::compile_shader(glsl, vk::ShaderStage::Vertex, ~uint64_t{0}, 0, {}, true);
        ok = !compiled.words.empty();
        if (ok) {
          v->shader->spirvWords = std::make_shared<const std::vector<uint32_t>>(std::move(compiled.words));
          v->shader->retainedSource = std::make_shared<const std::string>(std::move(glsl));
        } else why = compiled.error.empty() ? "an empty compile" : compiled.error;
      }
    } catch (const std::exception& e) {
      ok = false;
      why = e.what();
    }
    if (!ok && g_batchVariantsFailed.load() < 8) LOG("[vulkan batch] a vertex shader's batched variant failed: %s", why.c_str());
    ++(ok ? g_batchVariantsMade : g_batchVariantsFailed);
    v->state.store(ok ? 1 : 2, std::memory_order_release);
  }).detach();
  return slot.get();
}
// why runs were drawn draw by draw (the log line)
enum BatchWhy { kBatchDrawn, kBatchState, kBatchFetch, kBatchShader, kBatchGrass, kBatchBlocks, kBatchMaking,
                kBatchFailed, kBatchPipeline, kBatchWhys };
inline uint64_t g_batchWhy[kBatchWhys]{}, g_batchPieces = 0, g_batchDraws = 0;  // (render thread only: see the top)
inline bool batch_accept(const uint32_t* r, const draw_batch::Batch& run, uint32_t count, BatchDraw& out) {
  auto no = [](BatchWhy why) { ++g_batchWhy[why]; return false; };
  if (!batch_can_hold(r)) return no(kBatchState);
  uint64_t fsKey = 0;
  LatteFetchShader* fs = vk::get_fetch_shader(r, &fsKey, R.frame);
  if (!fs) return no(kBatchFetch);
  // (an attribute read per instance would read another element in each instance)
  for (const auto& g : fs->bufferGroups)
    for (int j = 0; j < g.attribCount; ++j)
      if (g.attrib[j].fetchType != LatteConst::VertexFetchType2::VERTEX_DATA) return no(kBatchFetch);
  vk::Shader *vs = nullptr, *ps = nullptr;
  vk::translate_stages(r, fs, fsKey, R.frame, g_shader_state_gen, vs, ps);
  // (the uniforms these change in place per draw: AO's noise scale, the sun shadow's eye shift; the AO private replay)
  if (!vs || !vs->ready() || !vs->retainedSource || !vs->dec || !ps || !ps->ready() ||
      vs->kind == gfx::ProgramKind::OcclusionVertex || ps->kind == gfx::ProgramKind::SunShadowMask ||
      ps->kind == gfx::ProgramKind::DepthDownsample)
    return no(kBatchShader);
  // Graphics > Grass draw distance drops tall grass tufts one by one by their block (draw)
  if (render::grass_distance() > 0 && count == 108) return no(kBatchGrass);
  // the blocks the shader reads whose address changes (the others' changes do not matter to it): bound as blocks, or
  // their entries remapped into the support block (which is then per piece too: bit 16)
  uint32_t blocks = 0, remapped = 0;
  for (uint32_t i = 0; i < 16; ++i)
    if ((run.blocks >> i & 1) && vs->mapping.uniformBuffersBindingPoint[i] >= 0) blocks |= 1u << i;
  if (vs->uniforms.offset_remapped >= 0 && vs->mapping.uniformVarsBufferBindingPoint >= 0)
    for (const auto& group : vs->dec->list_remappedUniformEntries_bufferGroups) {
      const uint32_t i = group.kcacheBankIdOffset / 4 / 7;
      if (i < 16 && (run.blocks >> i & 1)) remapped |= 1u << i;
    }
  for (uint32_t i = 0; i < 16; ++i) {
    if (!((blocks | remapped) >> i & 1)) continue;
    for (uint32_t k = 0; k < run.count; ++k)
      if (!run.addrs[k][i]) return no(kBatchBlocks);
  }
  BatchVariant* v = batch_variant(vs, blocks | (remapped ? 1u << 16 : 0));
  const int state = v->state.load(std::memory_order_acquire);
  if (state == 0) return no(kBatchMaking);
  if (state != 1) return no(kBatchFailed);
  out = {&run, vs, v->shader.get(), &v->layout, 0, 0, false};
  return true;
}
inline void batch_log() {
  static const bool stats = std::getenv("WWHD_VK_STATS") != nullptr;  // (counters logged with WWHD_VK_STATS only)
  if (!stats) return;
  static uint64_t frame0 = 0;
  if (R.frame < frame0 + 600) return;
  const double f = frame0 ? double(R.frame - frame0) : 600.0;
  frame0 = R.frame;
  LOG("[vulkan batch] per frame: %.1f runs drawn as %.1f instanced draws (%.0f draws in them); runs drawn draw by draw: "
      "%.1f state, %.1f fetch, %.1f shader, %.1f grass distance, %.1f blocks, %.1f variant being made, %.1f variant "
      "failed, %.1f pipeline not ready; variants made %llu, failed %llu",
      g_batchWhy[kBatchDrawn] / f, g_batchDraws / f, g_batchPieces / f, g_batchWhy[kBatchState] / f,
      g_batchWhy[kBatchFetch] / f, g_batchWhy[kBatchShader] / f, g_batchWhy[kBatchGrass] / f,
      g_batchWhy[kBatchBlocks] / f, g_batchWhy[kBatchMaking] / f, g_batchWhy[kBatchFailed] / f,
      g_batchWhy[kBatchPipeline] / f, (unsigned long long)g_batchVariantsMade.load(),
      (unsigned long long)g_batchVariantsFailed.load());
  for (auto& w : g_batchWhy) w = 0;
  g_batchPieces = 0;
  g_batchDraws = 0;
}
// a held run (draw_batch::current()): one instanced draw per chunk of the pieces its variant's blocks hold
// (draw_batch::consumed() set), or nothing drawn here (consumed() stays false: gx2 draws the pieces one by one)
inline void batch_draw_run(const uint32_t* r, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr,
                           uint32_t baseVertex, const draw_batch::Batch& run) {
  batch_log();
  BatchDraw b;
  if (!batch_accept(r, run, count, b)) return;
  struct Clear { ~Clear() { batch_draw() = nullptr; } } clear;
  // (TEST ONLY, WWHD_TEST_BATCH_CHUNK=n: at most n pieces per instanced draw; 1 = the batched variant drawn once per
  // piece, which separates the variant's own code from the instancing when pictures are compared)
  static const uint32_t testChunk = [] { const char* e = getenv("WWHD_TEST_BATCH_CHUNK"); return e ? uint32_t(atoi(e)) : 0u; }();
  const uint32_t chunk = testChunk ? std::min(testChunk, b.layout->pieces) : b.layout->pieces;
  for (uint32_t first = 0; first < run.count; first += chunk) {
    b.first = first;
    b.count = std::min(chunk, run.count - first);
    b.declined = false;
    batch_draw() = &b;
    draw(r, prim, count, indexType, indexAddr, baseVertex, b.count);
    batch_draw() = nullptr;
    if (!b.declined) {
      ++g_batchDraws;
      continue;
    }
    if (!first) {  // (nothing drawn: gx2 draws the pieces)
      ++g_batchWhy[kBatchPipeline];
      return;
    }
    // (a later chunk: its pieces one by one here, each with its own addresses; the registers put back. Draw threads:
    // these writes go to the register file this draw was given and are undone before return, so they are not
    // forwarded: on a draw thread that file is its own copy)
    uint32_t* regs = const_cast<uint32_t*>(r);
    uint32_t saved[16];
    for (uint32_t i = 0; i < 16; ++i) saved[i] = regs[mmSQ_VTX_UNIFORM_BLOCK_START + i * 7];
    for (uint32_t k = first; k < run.count; ++k) {
      for (uint32_t i = 0; i < 16; ++i) regs[mmSQ_VTX_UNIFORM_BLOCK_START + i * 7] = run.addrs[k][i];
      draw(r, prim, count, indexType, indexAddr, baseVertex, 1);
    }
    for (uint32_t i = 0; i < 16; ++i) regs[mmSQ_VTX_UNIFORM_BLOCK_START + i * 7] = saved[i];
    break;
  }
  draw_batch::consumed() = true;
  ++g_batchWhy[kBatchDrawn];
  g_batchPieces += run.count;
}
