// native_draw/j3d_desc_msmemo.h: make_state's program-only work memoized per item (WWHD_NATIVE_J3D_DESC_MSMEMO=1, with
// WWHD_NATIVE_J3D_DESC=2; fast build default on, =0 off). Included by j3d_desc_vk.h inside namespace gfxvk::j3ddescvk, after State.
// Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
//
// Exact by construction: the ambient register list (read_regs, the extra words, sort + unique, the filters) is a pure
// function of the key below: the programs (vs, ps, fs), the item (its effect's registers), the textures' units and
// sampler slots, the vertex bindings and whether each is live, and the draw's block registers. A state made again with
// an equal key gets the list kept with the key. The class split (split()) is a pure function of the list, its values
// and W: with W unchanged (g_wGen) and the values byte-equal, the kept class and J3D-written words are taken.
#pragma once

bool msmemo_on() {
  static const bool v = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_MSMEMO", 1) != 0;
  return v;
}
struct MakeMemo {
  std::vector<uint32_t> key, regs;  // the key and its ambient register list
  bool splitKept = false;           // the split of (regs, values) under W generation wGen
  uint64_t wGen = 0;
  std::vector<uint32_t> values;
  Class* cls = nullptr;
  std::vector<Range> vol;
  std::vector<uint32_t> volValues;
};
// the key of the ambient list (S.tex and s.vertex as make_state filled them before the ambient words)
inline void msmemo_key(const State& s, const PacketCapture& cap, const In& in, std::vector<uint32_t>& key) {
  key.clear();
  auto ptr = [&](const void* p) {
    const uint64_t v = uint64_t(reinterpret_cast<uintptr_t>(p));
    key.push_back(uint32_t(v)), key.push_back(uint32_t(v >> 32));
  };
  ptr(cap.vs), ptr(cap.ps), ptr(cap.fs), ptr(in.item);
  for (int k = 0; k < 2; ++k) {
    key.push_back(0xA0000000u | uint32_t(s.st[k].tex.size()));
    for (const Tex& x : s.st[k].tex) key.push_back(x.unit), key.push_back(x.samplerIndex);
    key.push_back(s.st[k].blockMask);
    for (int j = 0; j < 16; ++j) key.push_back(uint32_t(int32_t(s.st[k].blockFrom[j])));
  }
  key.push_back(0xB0000000u | uint32_t(s.vertex.size()));
  for (const auto& v : s.vertex) key.push_back(v.binding << 1 | uint32_t(v.live));
  key.push_back(0xC0000000u | in.blockCount);
  for (uint32_t i = 0; i < in.blockCount; ++i) key.push_back(in.blocks[i].reg);
}
