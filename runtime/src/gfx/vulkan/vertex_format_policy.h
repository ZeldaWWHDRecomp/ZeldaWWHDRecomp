#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>

namespace gfxvk::vk {
// Every distinct result of vertex_format(). Add new fetch formats here too.
inline constexpr std::array vertex_formats = {
    VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT,
    VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT,
    VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT};
inline const char* vertex_format_name(VkFormat format) {
    constexpr std::array names = {"R8_UINT", "R8G8_UINT", "R8G8B8_UINT", "R8G8B8A8_UINT",
        "R16_UINT", "R16G16_UINT", "R16G16B16_UINT", "R16G16B16A16_UINT",
        "R32_UINT", "R32G32_UINT", "R32G32B32_UINT", "R32G32B32A32_UINT"};
    for (size_t i = 0; i < vertex_formats.size(); ++i)
        if (vertex_formats[i] == format) return names[i];
    return "UNKNOWN";
}
// Split RGB into RG + B. No fetch is wider than the guest attribute, including
// tightly packed vertices and the final vertex at the buffer boundary.
inline std::string vertex_fallback_declaration(const std::string& name, uint32_t tailLocation) {
    return "in uvec4 " + name + "Head;\nlayout(location = " + std::to_string(tailLocation) +
        ") in uint " + name + "Tail;\n#define " + name +
        " uvec4(" + name + "Head.xy, " + name + "Tail, 1u)\n";
}
inline constexpr VkFormat split_vertex_format(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8_UINT: return VK_FORMAT_R8G8_UINT;
    case VK_FORMAT_R16G16B16_UINT: return VK_FORMAT_R16G16_UINT;
    case VK_FORMAT_R32G32B32_UINT: return VK_FORMAT_R32G32_UINT;
    default: return format;
    }
}
inline constexpr uint32_t vertex_tail_offset(VkFormat format) {
    return format == VK_FORMAT_R8G8B8_UINT ? 2u :
           format == VK_FORMAT_R16G16B16_UINT ? 4u :
           format == VK_FORMAT_R32G32B32_UINT ? 8u : 0u;
}
inline constexpr VkFormat vertex_tail_format(VkFormat format) {
    return format == VK_FORMAT_R8G8B8_UINT ? VK_FORMAT_R8_UINT :
           format == VK_FORMAT_R16G16B16_UINT ? VK_FORMAT_R16_UINT :
           format == VK_FORMAT_R32G32B32_UINT ? VK_FORMAT_R32_UINT : VK_FORMAT_UNDEFINED;
}
template<class Mapping> uint32_t vertex_tail_location(const Mapping& mapping) {
    uint32_t location = 0;
    for (auto value : mapping) if (value >= 0) location = std::max(location, uint32_t(value) + 1);
    return location;
}
struct VertexFormatPolicy {
    uint32_t fallbackMask = 0;
    static constexpr uint32_t bit(VkFormat format) {
        return format == VK_FORMAT_R8G8B8_UINT ? 1u :
               format == VK_FORMAT_R16G16B16_UINT ? 2u :
               format == VK_FORMAT_R32G32B32_UINT ? 4u : 0u;
    }
    constexpr bool fallback(VkFormat format) const { return fallbackMask & bit(format); }
    constexpr VkFormat fetch(VkFormat format) const {
        return fallback(format) ? split_vertex_format(format) : format;
    }
    // Zero leaves the pre-fallback shader/cache identity exactly unchanged.
    constexpr uint64_t shader_salt() const {
        return fallbackMask ? 0x5646583200000000ull | fallbackMask : 0;
    }
};
// Driver troubleshooting switches for issue #207 (Graphics > Advanced, applied at the next start;
// WWHD_VK_CPU_VERTICES, WWHD_VK_FULL_PRECISION, WWHD_VK_NO_VERTEX_REUSE take precedence). All off
// unless a driver rule applies; explicit off restores the default shader path.
struct VertexDiagnostics {
    uint32_t cpuClasses = 0;
    bool cpuVertices = false;    // byte swap + format conversion on the CPU, native float/uint fetch
    bool fullPrecision = false;  // strip RelaxedPrecision from every SPIR-V module
    bool noVertexReuse = false;  // fresh full vertex copies and binds every draw
    // shader/pipeline cache identity; zero when all are off
    constexpr uint64_t shader_salt() const {
        return (cpuVertices || fullPrecision) ? 0x4449414700000000ull | (uint64_t(cpuClasses) << 8) | (cpuVertices ? 1u : 0u) | (fullPrecision ? 2u : 0u) : 0;
    }
};
template<class Supported>
VertexFormatPolicy vertex_format_policy(Supported supported, bool force) {
    VertexFormatPolicy policy;
    for (auto format : vertex_formats) {
        if (!supported(format) || (force && VertexFormatPolicy::bit(format))) {
            const auto replacement = split_vertex_format(format);
            if (replacement == format || (!supported(replacement) || !supported(vertex_tail_format(format))))
                throw std::runtime_error(std::string("no supported vertex fetch format for ") + vertex_format_name(format));
            policy.fallbackMask |= VertexFormatPolicy::bit(format);
        }
    }
    return policy;
}
}
