#pragma once
// Fewer render pass splits (draw.cpp): the pure decisions and bookkeeping of two mechanisms, kept free
// of Vulkan so runtime/tools/pass_reuse_test.cpp can check them.
//
// From Nycolazs/WindWakerHD-Android-Plus 4595260 / fc5ddee via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0. Adapted to this renderer (dynamic rendering and the legacy render-pass
// path, the continued-draw fast path, stereo).
//
// (a) Pass subset (4595260): a draw whose colour targets are a strict subset of the open pass's colour
//     attachments (same colour 0 and slice, the same depth buffer or none on both, the same size) stays
//     in that pass. The slots it doesn't bind get the pass's own surfaces, and its pipeline's write mask
//     for them is cleared (CB_TARGET_MASK nibble 0, blending off), so it writes nothing there. Not when
//     the draw samples one of those targets (that needs the pass ended). WWHD_PASS_SUBSET=0: off.
// (b) Textures ready before the pass (fc5ddee): a texture first read in the middle of a pass needs a
//     layout change or barrier, which ends the pass (a store and reload of every attachment on a tiled
//     GPU). The frame has the same shape from one frame to the next, so each pass (by its attachments)
//     remembers up to 16 textures whose first read ended it, with the stages reading them, and the next
//     time that pass begins they are made ready first. Entries not seen for 120 frames are forgotten.
//     WWHD_PASS_PREFETCH=0: off.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace gfxvk::pass_reuse {

inline bool env_on(const char* name) {
  const char* e = std::getenv(name);
  return !e || !*e || std::atoi(e) != 0;
}
inline bool subset_on() {
  static const bool on = env_on("WWHD_PASS_SUBSET");
  return on;
}
inline bool prefetch_on() {
  static const bool on = env_on("WWHD_PASS_PREFETCH");
  return on;
}

// ------------------------------------------------------------------------------------- (a) subset
// One side of the comparison: 8 colour slots and the depth buffer, by identity (any pointer), with
// slices; for the pass's colour slots also the guest base address, and for the slots a draw leaves out
// (sampling check) the byte size of the target's memory and its mip chain's base address (0: none).
struct Targets {
  std::array<const void*, 8> colors{};
  std::array<uint32_t, 8> slices{};
  std::array<uint32_t, 8> addr{}, bytes{}, mipAddr{};
  const void* depth = nullptr;
  uint32_t depthSlice = 0, width = 0, height = 0;
};
// A target's memory for the sampling check, as surfaces.cpp invalidation_hits counts a surface's base
// bytes: the larger of its data size and pitch x height x slices x bytes per block.
inline uint32_t target_bytes(uint32_t dataSize, uint32_t pitch, uint32_t height, uint32_t slices, uint32_t bytesPerBlock) {
  const uint64_t geometry = uint64_t(pitch) * height * (slices ? slices : 1) * bytesPerBlock;
  const uint64_t bytes = dataSize > geometry ? dataSize : geometry;
  return bytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(bytes);
}
// Does a sampled texture address (a base or mip base, bytes, swizzle bits possibly set) fall in a
// target's memory? Conservative: the 2 KiB below the base (macro-tile swizzle bits, 0x700) count as the
// target's too.
inline bool samples_target(uint32_t sampled, uint32_t addr, uint32_t bytes) {
  if (!sampled || !addr) return false;
  const uint32_t lo = addr & ~0x7FFu;
  const uint64_t hi = uint64_t(addr) + (bytes ? bytes : 1u);
  return sampled >= lo && sampled < hi;
}
// The cheap part, by target identity only (no texture registers read): the colour slots the draw
// leaves out of the open pass, which it may take over with writes masked; 0: the draw can't stay in the
// pass that way (or binds the exact set already).
inline uint32_t subset_candidates(const Targets& pass, const Targets& draw) {
  if (!draw.colors[0] || draw.colors[0] != pass.colors[0] || draw.slices[0] != pass.slices[0]) return 0;
  if (draw.depth != pass.depth || (draw.depth && draw.depthSlice != pass.depthSlice)) return 0;
  if (draw.width != pass.width || draw.height != pass.height) return 0;
  uint32_t absent = 0;
  for (int i = 1; i < 8; i++) {
    if (draw.colors[i]) {
      if (draw.colors[i] != pass.colors[i] || draw.slices[i] != pass.slices[i]) return 0;
    } else if (pass.colors[i]) {
      if (!pass.addr[i]) return 0;
      absent |= 1u << i;
    }
  }
  return absent;
}
// Does the draw sample one of the absent slots' targets (base memory or mip chain)? sampled: the base
// and mip base addresses (bytes) of the textures the draw's stages sample.
inline bool samples_absent(const Targets& pass, uint32_t absent, const uint32_t* sampled, size_t sampledCount) {
  for (int i = 1; i < 8; i++) {
    if (!((absent >> i) & 1)) continue;
    for (size_t t = 0; t < sampledCount; t++)
      if (samples_target(sampled[t], pass.addr[i], pass.bytes[i]) ||
          samples_target(sampled[t], pass.mipAddr[i], pass.bytes[i]))
        return true;
  }
  return false;
}
// Both: the slots taken over, 0 when not a strict subset or when the draw samples one of them.
inline uint32_t subset_absent(const Targets& pass, const Targets& draw, const uint32_t* sampled, size_t sampledCount) {
  const uint32_t absent = subset_candidates(pass, draw);
  return absent && !samples_absent(pass, absent, sampled, sampledCount) ? absent : 0;
}
// CB_TARGET_MASK nibbles of the absent slots
inline uint32_t clear_mask(uint32_t absent) {
  uint32_t m = 0;
  for (int i = 0; i < 8; i++)
    if ((absent >> i) & 1) m |= 0xFu << (4 * i);
  return m;
}

// ----------------------------------------------------------------------- (b) ready before the pass
inline uint64_t signature(const std::array<uint32_t, 8>& colorKeys, uint32_t depthAddr, uint32_t width, uint32_t height) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint32_t v) {
    for (int b = 0; b < 4; b++) h = (h ^ ((v >> (8 * b)) & 0xFF)) * 1099511628211ull;
  };
  for (uint32_t k : colorKeys) mix(k);
  mix(depthAddr);
  mix(width);
  mix(height);
  return h;
}
// a colour slot's part of the signature (address and format; 0 for an empty slot)
inline uint32_t color_key(uint32_t addr, uint32_t format) { return addr ^ (format << 1); }

struct TextureKey {
  uint32_t addr = 0, width = 0, height = 0, format = 0;
  bool operator==(const TextureKey& o) const {
    return addr == o.addr && width == o.width && height == o.height && format == o.format;
  }
};
struct PassRead {
  TextureKey tex;
  uint32_t stages = 0;  // VkPipelineStageFlags reading it
  uint64_t lastFrame = 0;
};
class PassReadTable {
public:
  static constexpr size_t kPerPass = 16, kMaxPasses = 512;
  static constexpr uint64_t kForgetFrames = 120;
  // a texture first read inside the pass `sig` ended it at `frame`; false: not remembered (full)
  bool note(uint64_t sig, const TextureKey& tex, uint32_t stages, uint64_t frame) {
    auto it = passes_.find(sig);
    if (it == passes_.end()) {
      if (passes_.size() >= kMaxPasses) forget(frame);
      if (passes_.size() >= kMaxPasses) return false;
      it = passes_.emplace(sig, std::vector<PassRead>{}).first;
    }
    for (auto& r : it->second)
      if (r.tex == tex) {
        r.stages |= stages;
        r.lastFrame = frame;
        return true;
      }
    if (it->second.size() >= kPerPass) return false;
    it->second.push_back({tex, stages, frame});
    return true;
  }
  // the pass `sig` begins at `frame`: f(const PassRead&) for each texture it read in the last 120
  // frames; older ones are forgotten
  template <class F> void begin(uint64_t sig, uint64_t frame, F&& f) {
    auto it = passes_.find(sig);
    if (it == passes_.end()) return;
    auto& v = it->second;
    for (size_t k = 0; k < v.size();) {
      if (stale(v[k], frame)) {
        v[k] = v.back();
        v.pop_back();
        continue;
      }
      f(v[k]);
      k++;
    }
    if (v.empty()) passes_.erase(it);
  }
  void forget(uint64_t frame) {
    for (auto it = passes_.begin(); it != passes_.end();) {
      auto& v = it->second;
      for (size_t k = 0; k < v.size();)
        if (stale(v[k], frame)) {
          v[k] = v.back();
          v.pop_back();
        } else k++;
      it = v.empty() ? passes_.erase(it) : std::next(it);
    }
  }
  bool empty() const { return passes_.empty(); }
  size_t passes() const { return passes_.size(); }
  size_t reads(uint64_t sig) const {
    auto it = passes_.find(sig);
    return it == passes_.end() ? 0 : it->second.size();
  }
  void clear() { passes_.clear(); }

private:
  static bool stale(const PassRead& r, uint64_t frame) { return frame > r.lastFrame && frame - r.lastFrame > kForgetFrames; }
  std::unordered_map<uint64_t, std::vector<PassRead>> passes_;
};

}  // namespace gfxvk::pass_reuse
