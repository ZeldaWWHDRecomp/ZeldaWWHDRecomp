#pragma once
#include <cstdint>
#include <string>
#include <cstring>
#include "Cafe/HW/Latte/Core/FetchShader.h"

namespace gfxvk::vk {
enum CpuVertexClass : uint32_t {
    CpuSwap16 = 1, CpuSwap32 = 2, CpuSnorm = 4, CpuScaled = 8,
    CpuThreeComponent = 16, CpuUnaligned = 32, CpuInteger = 64,
    CpuUnorm = 128, CpuAll = 255
};
inline constexpr const char* cpuVertexClassNames[] = {
    "swap16", "swap32", "snorm", "scaled", "3comp", "unaligned", "integer", "unorm"
};
inline uint32_t cpu_vertex_classes(const LatteParsedFetchShaderAttribute& a) {
    using F = Latte::E_HWFMT;
    using E = LatteConst::VertexFetchEndianMode;
    return (a.endianSwap == E::SWAP_U16 ? CpuSwap16 : 0u) |
        (a.endianSwap == E::SWAP_U32 ? CpuSwap32 : 0u) |
        (a.nfa == 0 && a.isSigned ? CpuSnorm : 0u) |
        (a.nfa == 2 && a.format != F::HWFMT_32_FLOAT && a.format != F::HWFMT_32_32_FLOAT &&
         a.format != F::HWFMT_32_32_32_FLOAT && a.format != F::HWFMT_32_32_32_32_FLOAT &&
         a.format != F::HWFMT_16_FLOAT && a.format != F::HWFMT_16_16_FLOAT &&
         a.format != F::HWFMT_16_16_16_FLOAT && a.format != F::HWFMT_16_16_16_16_FLOAT ? CpuScaled : 0u) |
        (a.format == F::HWFMT_8_8_8 || a.format == F::HWFMT_16_16_16 ||
         a.format == F::HWFMT_16_16_16_FLOAT || a.format == F::HWFMT_32_32_32 || a.format == F::HWFMT_32_32_32_FLOAT ? CpuThreeComponent : 0u) |
        (a.offset % 4 ? CpuUnaligned : 0u) | (a.nfa == 1 ? CpuInteger : 0u) |
        (a.nfa == 0 && !a.isSigned ? CpuUnorm : 0u);
}
inline bool cpu_vertex_selected(const LatteParsedFetchShaderAttribute& a, uint32_t mask) {
    return mask && (mask == CpuAll || (cpu_vertex_classes(a) & mask));
}
inline uint32_t cpu_vertex_count(const LatteParsedFetchShaderBufferGroup& g, uint32_t mask) {
    uint32_t n = 0;
    for (int j = 0; j < g.attribCount; ++j) n += cpu_vertex_selected(g.attrib[j], mask);
    return n;
}
// Count every preceding selected attribute, including semantics unused by this shader.
inline uint32_t cpu_vertex_offset(const LatteParsedFetchShaderBufferGroup& g, int index, uint32_t mask) {
    uint32_t offset = 0;
    for (int j = 0; j < index; ++j) offset += cpu_vertex_selected(g.attrib[j], mask) ? 16 : 0;
    return offset;
}
inline bool parse_cpu_vertex_classes(const char* text, uint32_t& mask) {
    mask = 0;
    if (!text) return false;
    if (!std::strcmp(text, "all")) { mask = CpuAll; return true; }
    if (!std::strcmp(text, "none")) return true;
    std::string s(text);
    size_t begin = 0;
    do {
        const auto end = s.find(',', begin);
        const auto token = s.substr(begin, end - begin);
        bool found = false;
        for (unsigned i = 0; i < 8; ++i)
            if (token == cpuVertexClassNames[i]) { mask |= 1u << i; found = true; }
        if (!found) { mask = 0; return false; }
        if (end == std::string::npos) return true;
        begin = end + 1;
    } while (true);
}
inline std::string cpu_vertex_class_names(uint32_t mask) {
    if (mask == CpuAll) return "all";
    std::string result;
    for (unsigned i = 0; i < 8; ++i) if (mask & (1u << i)) {
        if (!result.empty()) result += ',';
        result += cpuVertexClassNames[i];
    }
    return result.empty() ? "none" : result;
}
struct CpuVertexChoice { uint32_t mask; const char* reason; };
// Explicit all/off override wins over class diagnostics and the automatic driver rule.
inline CpuVertexChoice choose_cpu_vertices(uint32_t vendor, const char* option,
        const char* env, const char* classes) {
    const char* override = env ? env : option;
    if (override && !std::strcmp(override, "1")) return {CpuAll, env ? "WWHD_VK_CPU_VERTICES=1" : "Graphics option: CPU vertices on"};
    if (override && !std::strcmp(override, "0")) return {0, env ? "WWHD_VK_CPU_VERTICES=0" : "Graphics option: CPU vertices off"};
    uint32_t mask;
    if (classes && parse_cpu_vertex_classes(classes, mask)) return {mask, "CPU vertex class diagnostic"};
    if (vendor == 0x1010) return {CpuAll, "driver rule: PowerVR CPU vertex conversion (#207)"};
    return {0, "default"};
}
// Reserve unused bindings for mixed groups. Raw attributes retain their original layout.
template<class FetchShader>
inline uint32_t cpu_vertex_binding(const FetchShader& fs, const LatteParsedFetchShaderBufferGroup& target,
        uint32_t mask, uint32_t maximum) {
    uint32_t candidate = 0;
    for (const auto& g : fs.bufferGroups) {
        if (!cpu_vertex_count(g, mask)) continue;
        if (cpu_vertex_count(g, mask) == uint32_t(g.attribCount)) {
            if (&g == &target) return g.attributeBufferIndex;
            continue;
        }
        for (;;) {
            bool used = false;
            for (const auto& raw : fs.bufferGroups) used |= raw.attributeBufferIndex == candidate;
            if (!used) break;
            ++candidate;
        }
        if (&g == &target) return candidate < maximum ? candidate : maximum;
        ++candidate;
    }
    return maximum;
}
}
