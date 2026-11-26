// native_draw/j3d_desc_support2.h: a described draw's support bytes built straight into their upload slice
// (WWHD_NATIVE_J3D_DESC_SUPPORT2, with WWHD_NATIVE_J3D_DESC=2; default on since 2026-11-17, =0 off). Included by j3d_desc_vk.h inside
// namespace gfxvk::j3ddescvk, after support_patch. Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
//
// Exact by construction: the slice holds the bytes snapshot(support_bytes(S, in)) makes (the template, each kcache entry
// copied as support_patch copies it, the gather grid patch, zero padding), built in place:
// - the copy list as runs: consecutive entries of one block with consecutive destinations and sources are one copy
//   (entries past the template's end are skipped, as support_patch skips them); a run reads its source as each of its
//   entries would (a whole run from the submitted block when it lies inside it, else from guest memory when the block
//   is not submitted; a run that would mix the two is copied entry by entry, as support_patch);
// - each block's address, size and submitted bytes looked up once per build;
// - the slice kept and bound again while every copied block is a submitted display-list block (its bytes fixed for the
//   submitted snapshot's id) at the same address and size, in the same submission, and the state has no gather patch
//   inputs that change (they are the state's textures: fixed per state).
#pragma once

bool support2_on() {
  static const bool v = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_SUPPORT2", 1) != 0;
  return v;
}

inline void support2_runs(Stage& S) {
  S.supRuns.clear();
  S.supBlocks = 0;
  const size_t size = S.sup.size();
  for (const auto& c : S.copies) {
    if (size_t(c.dst) + 16 > size) continue;
    if (c.block < 16) S.supBlocks |= 1u << c.block;
    auto& v = S.supRuns;
    if (!v.empty() && v.back().block == c.block && v.back().dst + v.back().len == c.dst && v.back().src + v.back().len == c.src)
      v.back().len += 16;
    else
      v.push_back({c.dst, c.block, c.src, 16});
  }
  S.supRunsMade = true;
}

UploadSlice support2(Stage& S, const In& in) {
  if (!S.supRunsMade) support2_runs(S);
  uint32_t addr[16], size[16];
  const uint8_t* sub[16];
  bool keepable = true;  // (the gather patch's inputs are the state's textures: fixed while the state holds)
  for (uint32_t mask = S.supBlocks; mask; mask &= mask - 1) {
    const int j = __builtin_ctz(mask);
    block_of(S, in, uint32_t(j), addr[j], size[j]);
    sub[j] = addr[j] ? gx2::submitted_uniform_block(addr[j], size[j]) : nullptr;
  }
  for (const auto& run : S.supRuns)
    if (run.block < 16 && addr[run.block] && !(sub[run.block] && run.src + run.len <= size[run.block])) keepable = false;
  const uint64_t uid = gx2::submitted_uniforms_id();
#ifdef WWHD_DECOMP_FAST
  // (cheapib b1: a captured call's slice is built and noted as support_bytes + snapshot note it; no kept slice then)
  const bool b1cap = ::cheapib::g_b1_capturing;
#else
  constexpr bool b1cap = false;
#endif
  if (keepable && !b1cap && S.supKept.buffer && S.supKeptSubmission == R.submissionGeneration && S.supKeptUid == uid) {
    bool same = true;
    for (uint32_t mask = S.supBlocks; mask && same; mask &= mask - 1) {
      const int j = __builtin_ctz(mask);
      same = S.supKeptAddr[j] == addr[j] && S.supKeptSize[j] == size[j];
    }
    if (same) {
      d1::count(d1::cSupReused);
      return S.supKept;
    }
  }
  const size_t n = S.sup.size();
  const UploadSlice slice = allocate_upload(std::max<size_t>(n, 16), R.properties.limits.minUniformBufferOffsetAlignment);
  uint8_t* bytes = static_cast<uint8_t*>(slice.mapped);
  if (n) std::memcpy(bytes, S.sup.data(), n);
  if (slice.size > n) std::memset(bytes + n, 0, slice.size - n);
#ifdef WWHD_DECOMP_FAST
  if (b1cap) ::cheapib::b1_src_arm(bytes, 3);
#endif
  for (const auto& run : S.supRuns) {
    const uint32_t a = run.block < 16 ? addr[run.block] : 0;
    if (!a) { std::memset(bytes + run.dst, 0, run.len); continue; }
    const uint8_t* sb = sub[run.block];
#ifdef WWHD_DECOMP_FAST
    if (b1cap) {  // (support_patch's notes, entry by entry)
      for (uint32_t o = 0; o < run.len; o += 16) {
        const bool fromSub = sb && run.src + o + 16 <= size[run.block];
        ::cheapib::b1_src(bytes, run.dst + o, fromSub ? static_cast<const void*>(sb + run.src + o) : ppc_ptr(a + run.src + o));
        if (!fromSub) ::cheapib::b1_guest_read(4, a, size[run.block]);
      }
    }
#endif
    if (sb && run.src + run.len <= size[run.block]) std::memcpy(bytes + run.dst, sb + run.src, run.len);
    else if (!sb) std::memcpy(bytes + run.dst, ppc_ptr(a + run.src), run.len);
    else
      for (uint32_t o = 0; o < run.len; o += 16)  // (as support_patch, entry by entry)
        std::memcpy(bytes + run.dst + o, run.src + o + 16 <= size[run.block] ? static_cast<const void*>(sb + run.src + o)
                                                                             : ppc_ptr(a + run.src + o), 16);
  }
  if (S.sh->gatherUnits && S.sh->uniforms.offset_remapped >= 0)  // (support_patch's grid patch, unchanged)
    for (const Tex& x : S.tex)
      if (S.sh->gatherUnits >> x.unit & 1)
        for (const auto& c : S.copies)
          if (size_t(c.dst) + 16 <= n)
            ::gfx::area_sample::scale_gather_grid(reinterpret_cast<float*>(bytes + c.dst), 1, x.surface->width,
                                                  x.surface->height, x.surface->sx, x.surface->sy);
  d1::count(d1::cSupBuilt);
#ifdef WWHD_DECOMP_FAST
  if (b1cap) {
    b1_note_support_regs(S, bytes);
    if (n) ::cheapib::b1_note_upload(uint64_t(slice.buffer), slice.offset, slice.size, bytes, n);
  }
#endif
  if (keepable && !b1cap) {
    S.supKept = slice, S.supKeptSubmission = R.submissionGeneration, S.supKeptUid = uid;
    for (uint32_t mask = S.supBlocks; mask; mask &= mask - 1) {
      const int j = __builtin_ctz(mask);
      S.supKeptAddr[j] = addr[j], S.supKeptSize[j] = size[j];
    }
  } else {
    S.supKept = UploadSlice{};
  }
  return slice;
}
