#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace guestmods::hud {
inline constexpr uint32_t kMaxTextureDimension=2048;
inline constexpr size_t kMaxTextureBytes=16*1024*1024;
struct Pixels { uint32_t width=0,height=0; std::vector<uint8_t> rgba; };
// Throws a bounded, path-free diagnostic on malformed or oversized input.
Pixels decode_png(std::span<const uint8_t> bytes);
// The caller supplies its own package or per-mod Data root, never a guest path.
Pixels load_png(const std::filesystem::path& root,const std::string& relative);
}
