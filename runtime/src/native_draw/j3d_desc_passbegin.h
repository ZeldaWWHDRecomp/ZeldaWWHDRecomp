// native_draw/j3d_desc_passbegin.h: J3D described draws that begin their render pass (WWHD_NATIVE_J3D_DESC_PASSBEGIN=1,
// with WWHD_NATIVE_J3D_DESC=2; fast build default on, =0 off). Included by j3d_desc_vk.h inside namespace gfxvk::j3ddescvk, after
// try_replay. Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only. Exactness argument: NATIVE-J3D-DESC.md section 15.
//
// The first J3D draw of a pass (12-17 per frame in every scene measured) found no state matching the open pass: its
// state was made for the pass that draw begins, and that pass is never open at the next frame's lookup. Those draws took
// the translated path (and made their state again). Here, when no state matches the open pass, the draw's targets are
// looked up exactly as draw() looks them up (pass_trusted_targets on the register file the translated draw would see:
// the run settled, the item's effect and the draw's blocks over it, restored afterwards), and a state made for exactly
// those targets (absent mask 0: the draw's own pass) is drawn from: clouds_before_draw as draw() calls it, the replay's
// texture work, then the pass begun by draw()'s own function (begin_draw_pass), then the replay's binds and draw.
// Refused (the translated path, counted): the cloud settings that make clouds_before_draw act, a pass subset draw()
// could take (take_pass_subset of the open pass), no state for the targets, a lookup without targets.
#pragma once

bool passbegin_on() {
  static const bool v = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_PASSBEGIN", 1) != 0;
  return v;
}

// the register file the translated draw resolves its targets from, made in place over the live file (the run is
// settled: what is there is the file as the translated draw's apply_effects finds it) and taken back afterwards
struct Overlay {
  uint32_t* r;
  std::vector<std::pair<uint32_t, uint32_t>> saved;
  Overlay(const In& in) : r(in.regs) {
    const Item& it = *in.item;
    saved.reserve(it.regs.size() + in.blockCount * 7);
    for (size_t k = 0; k < it.regs.size(); ++k) saved.push_back({it.regs[k], r[it.regs[k]]}), r[it.regs[k]] = it.values[k];
    for (uint32_t b = 0; b < in.blockCount; ++b) {
      uint32_t w[7];
      d1::block_words(in.blocks[b], w);
      for (uint32_t k = 0; k < 7; ++k) saved.push_back({in.blocks[b].reg + k, r[in.blocks[b].reg + k]}), r[in.blocks[b].reg + k] = w[k];
    }
  }
  ~Overlay() {  // backwards: a register written twice gets its first saved value
    for (size_t i = saved.size(); i-- > 0;) r[saved[i].first] = saved[i].second;
  }
};

// the state of LIST that draw() would draw IN into after beginning its pass, else nullptr (the translated path);
// clouds_before_draw has run for it when one is returned
State* passbegin_find(std::vector<std::unique_ptr<State>>& list, const In& in) {
  vk::Shader* ps = nullptr;
  for (auto& x : list)
    if (x->beginOk && !x->refused) { ps = x->ps; break; }
  if (!ps) return nullptr;
  // clouds_before_draw does nothing beyond its early returns without cloud shadows and toon clouds
  if (render::cloud_shadows() || cloud_proto::mode() == cloud_proto::kSdf) {
    d1::count(d1::cPassBeginClouds);
    d1::note("pass begin: cloud settings");
    return nullptr;
  }
  d1::settle_run();  // (the run settled now instead of in the translated draw's apply_effects: the same file)
  Overlay o(in);
  DrawTargets t;
  if (!PASS_TRUSTED_TARGETS(o.r, ps, t)) {  // (the translated draw returns here too)
    d1::count(d1::cPassBeginLookup);
    d1::note("pass begin: no targets");
    return nullptr;
  }
  State* s = nullptr;
  for (auto& x : list)
    if (x->beginOk && !x->refused && x->ps == ps && x->colors == t.colors && x->slices == t.slices && x->depth == t.depth &&
        x->depthSlice == t.depthSlice && x->width == t.width && x->height == t.height) {
      s = x.get();
      break;
    }
  if (!s) {
    d1::count(d1::cPassBeginNoMatch);
    d1::note("pass begin: no state for the targets");
    return nullptr;
  }
  // draw() takes a pass subset of the open pass before its textures (and keeps the open pass): never begun here
  if (pass_reuse::subset_on() && pass_state().rendering && pass_state().tracked && async_pipeline_enabled()) {
    DrawTargets u = t;
    if (take_pass_subset(o.r, s->vs, ps, u)) {
      d1::count(d1::cPassBeginSubset);
      d1::note("pass begin: pass subset");
      return nullptr;
    }
  }
  clouds_before_draw(o.r, ps, t.colors[0], t.slices[0], t.depth);  // (as draw() after its targets)
  return s;
}
