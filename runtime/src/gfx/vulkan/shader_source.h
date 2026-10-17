// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace gfxvk::vk {
// Identity of a final GLSL text without keeping the text: 128-bit XXH3 plus
// the exact length (shader_source_id, shader_disk_cache.h). Translated
// shaders, pending compilations and the SPIR-V disk cache compare this
// instead of retaining full source. Every lookup key is also an independent
// 64-bit FNV-1a hash of the same bytes, so a false match needs simultaneous
// collisions of both hashes at equal length.
struct ShaderSourceId {
    uint64_t low = 0, high = 0;
    uint32_t length = 0;
    bool operator==(const ShaderSourceId&) const = default;
};
// Optional exact text, kept only by the shader verification modes. Shared
// read-only, so aliases never copy it.
using ShaderSource = std::shared_ptr<const std::string>;
// Compiled words are immutable once published; Shader and disk cache share them.
using SpirvWords = std::shared_ptr<const std::vector<uint32_t>>;
// A colliding/newer submission must survive an older binary's publication.
template<class Map, class Key, class Binary>
void release_pending_shader_source(Map& pending, const Key& key,
        const std::shared_ptr<Binary>& binary) {
    if (auto it = pending.find(key);
        it != pending.end() && it->second.second.lock() == binary)
        pending.erase(it);
}
}
