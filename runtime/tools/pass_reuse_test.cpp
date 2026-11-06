// Fewer render pass splits (gfx/vulkan/pass_reuse.h): the pass subset decision and the bookkeeping of
// the textures made ready before their pass. From Nycolazs/WindWakerHD-Android-Plus 4595260 / fc5ddee
// via GreenNaugahyde/ZeldaWWHDRecompAndroid v0.9.9 (db65d60), MPL-2.0.
#include "gfx/vulkan/pass_reuse.h"
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace gfxvk::pass_reuse;

int main() {
  int a, b, c, d, z;  // surface identities
  // the open pass: colour 0 (0x10000000) + R8 second target (0x20000000), depth
  Targets pass;
  pass.colors = {&a, &b};
  pass.addr = {0x10000000u, 0x20000000u};
  pass.bytes = {0x400000u, 0x100000u};
  pass.depth = &z;
  pass.width = 960;
  pass.height = 540;
  Targets draw = pass;
  draw.colors[1] = nullptr;  // colour 0 only
  draw.addr[1] = draw.bytes[1] = 0;
  const uint32_t none[1] = {0};
  // a strict subset keeps the pass: slot 1 taken over, its write mask cleared
  assert(subset_absent(pass, draw, none, 0) == 0b10);
  assert(clear_mask(0b10) == 0xF0u);
  assert(clear_mask(0b10000010) == 0xF00000F0u);
  assert(clear_mask(0) == 0);
  // the exact set: nothing to take over (the ordinary reuse test keeps the pass)
  assert(subset_absent(pass, pass, none, 0) == 0);
  // sampling the absent target (exactly, with swizzle bits, or inside it): the pass has to end
  {
    const uint32_t s1[] = {0x30000000u, 0x20000000u};
    assert(subset_absent(pass, draw, s1, 2) == 0);
    const uint32_t s2[] = {0x20000300u};  // macro-tile swizzle bits in the sampled base
    assert(subset_absent(pass, draw, s2, 1) == 0);
    const uint32_t s3[] = {0x20080000u};  // inside its memory
    assert(subset_absent(pass, draw, s3, 1) == 0);
    const uint32_t s4[] = {0x20100000u, 0x10000000u, 0};  // just past it; colour 0 itself; empty unit
    assert(subset_absent(pass, draw, s4, 3) == 0b10);
  }
  // colour 0 differs, or its slice: no
  {
    Targets t = draw;
    t.colors[0] = &c;
    assert(subset_absent(pass, t, none, 0) == 0);
    t = draw;
    t.slices[0] = 1;
    assert(subset_absent(pass, t, none, 0) == 0);
    t = draw;
    t.colors[0] = nullptr;
    assert(subset_absent(pass, t, none, 0) == 0);
  }
  // depth: the same one (and slice) or none on both sides
  {
    Targets t = draw;
    t.depth = nullptr;  // the pass has one: it would end anyway (no depth borrowing here)
    assert(subset_absent(pass, t, none, 0) == 0);
    t.depth = &d;
    assert(subset_absent(pass, t, none, 0) == 0);
    t.depth = &z;
    t.depthSlice = 2;
    assert(subset_absent(pass, t, none, 0) == 0);
    Targets p2 = pass, t2 = draw;
    p2.depth = t2.depth = nullptr;
    assert(subset_absent(p2, t2, none, 0) == 0b10);
  }
  // another size, a colour slot the pass doesn't have, a different surface in a shared slot: no
  {
    Targets t = draw;
    t.width = 1280;
    assert(subset_absent(pass, t, none, 0) == 0);
    t = draw;
    t.colors[2] = &c;
    assert(subset_absent(pass, t, none, 0) == 0);
    Targets p3 = pass;
    p3.colors[2] = &c;
    p3.addr[2] = 0x40000000u;
    Targets t3 = p3;
    t3.colors[1] = nullptr;
    t3.colors[2] = &d;
    assert(subset_absent(p3, t3, none, 0) == 0);
    t3.colors[2] = &c;
    t3.slices[2] = 1;
    assert(subset_absent(p3, t3, none, 0) == 0);
    // two absent slots of three
    t3 = p3;
    t3.colors[1] = t3.colors[2] = nullptr;
    assert(subset_absent(p3, t3, none, 0) == 0b110);
  }
  // an absent slot without a guest address (a private image) is never taken over
  {
    Targets p4 = pass;
    p4.addr[1] = 0;
    assert(subset_absent(p4, draw, none, 0) == 0);
  }
  // the cheap comparison alone (no texture registers read): the same answer before the sampling check
  {
    assert(subset_candidates(pass, draw) == 0b10);
    assert(subset_candidates(pass, pass) == 0);
    Targets t = draw;
    t.width = 1280;
    assert(subset_candidates(pass, t) == 0);
    const uint32_t hit[] = {0x20000000u};
    assert(samples_absent(pass, 0b10, hit, 1) && !samples_absent(pass, 0, hit, 1));
  }
  // a target's memory as invalidation_hits counts it: the larger of its data size and pitch x height x
  // slices x bytes per block
  assert(target_bytes(0x1000, 256, 64, 1, 4) == 0x10000);
  assert(target_bytes(0x20000, 256, 64, 1, 4) == 0x20000);
  assert(target_bytes(0, 256, 64, 6, 4) == 6 * 0x10000);
  assert(target_bytes(0, 256, 64, 0, 4) == 0x10000);  // (no slice count: one)
  assert(target_bytes(0, 0x10000, 0x10000, 4, 16) == 0xFFFFFFFFu);  // clamped
  {
    // a sampled address past the data size but inside the pitch-padded size (a wide pitch) hits
    Targets p5 = pass;
    p5.bytes[1] = target_bytes(0x1000, 1024, 544, 1, 1);  // pitch-padded: 0x88000
    const uint32_t s5[] = {0x20004000u};
    assert(subset_absent(p5, draw, s5, 1) == 0);
    // a later slice of an array target
    p5.bytes[1] = target_bytes(0x80000, 1024, 512, 2, 1);
    const uint32_t s6[] = {0x200C0000u};
    assert(subset_absent(p5, draw, s6, 1) == 0);
  }
  // the absent target's mip chain: a texture whose base or mip base (WORD3) is in it ends the pass
  {
    Targets p6 = pass;
    p6.mipAddr[1] = 0x28000000u;
    const uint32_t s7[] = {0x30000000u, 0x28000100u};  // base elsewhere, mip base inside the chain
    assert(subset_absent(p6, draw, s7, 2) == 0);
    const uint32_t s8[] = {0x30000000u, 0x30040000u};
    assert(subset_absent(p6, draw, s8, 2) == 0b10);
    // a sampled mip base inside the target's base memory
    const uint32_t s9[] = {0x40000000u, 0x20010000u};
    assert(subset_absent(pass, draw, s9, 2) == 0);
  }
  // samples_target edge cases
  assert(!samples_target(0, 0x1000, 0x100));
  assert(!samples_target(0x1000, 0, 0x100));
  assert(samples_target(0x1000, 0x1000, 0));  // no size known: its base address only
  assert(!samples_target(0x1100, 0x1000, 0));

  // -------------------------------------------------------------------- ready before the pass
  const uint64_t s1 = signature({color_key(0x10000000u, 0x1a), color_key(0x20000000u, 0x1)}, 0x30000000u, 960, 540);
  const uint64_t s2 = signature({color_key(0x10000000u, 0x1a)}, 0x30000000u, 960, 540);
  assert(s1 != s2);
  assert(s1 == signature({color_key(0x10000000u, 0x1a), color_key(0x20000000u, 0x1)}, 0x30000000u, 960, 540));
  assert(s1 != signature({color_key(0x10000000u, 0x1a), color_key(0x20000000u, 0x1)}, 0x30000000u, 1280, 720));
  assert(color_key(0x1000, 0) == 0x1000 && color_key(0x1000, 1) != color_key(0x1000, 2));

  PassReadTable t;
  assert(t.empty());
  const TextureKey tex{0x50000000u, 256, 256, 0x1a};
  assert(t.note(s1, tex, 0x80, 10));
  assert(t.note(s1, tex, 0x08, 11));  // the same texture: stages merged, not a second entry
  assert(t.reads(s1) == 1 && t.passes() == 1);
  int seen = 0;
  t.begin(s1, 20, [&](const PassRead& r) {
    assert(r.tex == tex && r.stages == 0x88 && r.lastFrame == 11);
    seen++;
  });
  assert(seen == 1);
  // another pass sees nothing
  seen = 0;
  t.begin(s2, 20, [&](const PassRead&) { seen++; });
  assert(seen == 0);
  // 120 frames after its last note it is still there, 121 after it is forgotten (and the pass with it)
  seen = 0;
  t.begin(s1, 11 + 120, [&](const PassRead&) { seen++; });
  assert(seen == 1);
  seen = 0;
  t.begin(s1, 11 + 121, [&](const PassRead&) { seen++; });
  assert(seen == 0 && t.reads(s1) == 0 && t.empty());
  // a note refreshes it (the prefetched texture was read in its pass)
  t.note(s1, tex, 0x80, 200);
  t.note(s1, tex, 0x80, 300);
  seen = 0;
  t.begin(s1, 400, [&](const PassRead&) { seen++; });
  assert(seen == 1);
  // at most 16 textures per pass
  for (uint32_t i = 0; i < 20; i++) {
    const bool kept = t.note(s2, {0x60000000u + i * 0x1000, 64, 64, 0x1a}, 0x80, 400);
    assert(kept == (i < 16));
  }
  assert(t.reads(s2) == 16);
  // at most kMaxPasses passes; stale ones make room
  t.clear();
  for (uint64_t i = 0; i < PassReadTable::kMaxPasses; i++) assert(t.note(1000 + i, tex, 0x80, 0));
  assert(!t.note(5, tex, 0x80, 100));  // all current: full
  assert(t.note(5, tex, 0x80, 500));   // all stale by then: forgotten, room again
  assert(t.passes() == 1);
  // forget() prunes everything stale
  t.note(6, tex, 0x80, 600);
  t.forget(700);  // pass 5 (frame 500) goes, pass 6 (frame 600) stays
  assert(t.passes() == 1 && t.reads(6) == 1 && t.reads(5) == 0);
  t.forget(800);
  assert(t.empty());

  // env switches: default on, =0 off
#ifndef _WIN32
  unsetenv("WWHD_PASS_SUBSET");
  assert(env_on("WWHD_PASS_SUBSET"));
  setenv("WWHD_PASS_SUBSET", "0", 1);
  assert(!env_on("WWHD_PASS_SUBSET"));
  setenv("WWHD_PASS_SUBSET", "1", 1);
  assert(env_on("WWHD_PASS_SUBSET"));
#endif
  std::puts("pass_reuse_test: ok");
  return 0;
}
