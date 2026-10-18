// Independent scalar model of the GLSL decoder: byte addressing, signed arithmetic,
// normalization and half expansion. Support masks follow the emitter's handled cases.
#include "gfx/vulkan/vertex_cpu_decode.h"
#include "gfx/vulkan/vertex_cpu_policy.h"
#include "gfx/vulkan/vertex_format_policy.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
using namespace gfxvk::vk;
using F = Latte::E_HWFMT;
using E = LatteConst::VertexFetchEndianMode;
void cemu_shim_log(const std::string&) {}
static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
struct Format { F format; bool floating; unsigned width, count; bool packed; unsigned support[4]; };
static const Format formats[] = {
    {F::HWFMT_32_32_32_32_FLOAT, true, 4, 4, false, {48,0,48}},
    {F::HWFMT_32_32_32_FLOAT, true, 4, 3, false, {48,0,48}},
    {F::HWFMT_32_32_FLOAT, true, 4, 2, false, {48,0,48}},
    {F::HWFMT_32_FLOAT, true, 4, 1, false, {0,0,48}},
    {F::HWFMT_8_8_8_8, false, 1, 4, false, {31,0,23}},
    {F::HWFMT_8_8_8, false, 1, 3, false, {0,0,0}},
    {F::HWFMT_8_8, false, 1, 2, false, {23,0,0}},
    {F::HWFMT_8, false, 1, 1, false, {5,0,0}},
    {F::HWFMT_32_32_32_32, false, 4, 4, false, {0,0,4}},
    {F::HWFMT_32_32_32, false, 4, 3, false, {0,0,4}},
    {F::HWFMT_32_32, false, 4, 2, false, {0,0,4}},
    {F::HWFMT_32, false, 4, 1, false, {4,0,12}},
    {F::HWFMT_16_16_16_16, false, 2, 4, false, {34,47,0}},
    {F::HWFMT_16_16_16, false, 2, 3, false, {0,0,0}},
    {F::HWFMT_16_16, false, 2, 2, false, {2,63,0}},
    {F::HWFMT_16, false, 2, 1, false, {0,5,0}},
    {F::HWFMT_16_16_16_16_FLOAT, true, 2, 4, false, {48,48,0}},
    {F::HWFMT_16_16_16_FLOAT, true, 2, 3, false, {0,0,0}},
    {F::HWFMT_16_16_FLOAT, true, 2, 2, false, {48,48,0}},
    {F::HWFMT_16_FLOAT, true, 2, 1, false, {0,0,0}},
    {F::HWFMT_2_10_10_10, false, 4, 1, true, {1,0,3}},
};
static std::array<uint32_t,4> reference(const Format& f, const LatteParsedFetchShaderAttribute& a,
        const uint8_t* p) {
    std::array<uint32_t,4> result{};
    if (!(f.support[unsigned(a.endianSwap)] & (1u << (2*a.nfa+a.isSigned)))) return result;
    auto convert = [&](uint32_t value, unsigned width, bool half) {
        if (half) {
            const unsigned exp = (value >> 10) & 31, mantissa = value & 1023;
            float v = exp == 0 ? std::ldexp(float(mantissa), -24) : std::ldexp(float(1024+mantissa), int(exp)-25);
            if (exp == 31) return (value & 0x8000 ? 0x80000000u : 0u) | 0x7f800000u | (mantissa << 13);
            return bits(value & 0x8000 ? -v : v);
        }
        if (f.floating) return value; // 32-bit float is reinterpreted, never numerically converted
        int64_t signedValue = value;
        if (a.isSigned && (uint64_t(value) & (uint64_t(1) << (width-1)))) signedValue -= int64_t(1) << width;
        if (a.nfa == 1) return uint32_t(signedValue);
        float v = a.isSigned ? float(signedValue) : float(value);
        if (a.nfa == 0) v = std::max(-1.0f, v / float((uint64_t(1) << (width-(a.isSigned ? 1 : 0)))-1));
        return bits(v);
    };
    uint32_t words[4]{};
    for (unsigned i=0; i<f.count; ++i) {
        const unsigned component = f.width == 1 && a.endianSwap == E::SWAP_U32 ? 3-i : i;
        for (unsigned byte=0; byte<f.width; ++byte) {
            const bool big = (f.width==2 && a.endianSwap==E::SWAP_U16) || (f.width==4 && a.endianSwap==E::SWAP_U32);
            words[i] |= uint32_t(p[component*f.width+byte]) << (8*(big ? f.width-1-byte : byte));
        }
    }
    if (f.packed) {
        for (unsigned i=0;i<3;++i) result[i]=convert((words[0]>>(10*i))&1023,10,false);
        result[3]=bits(float(words[0]>>30)*(1.0f/3.0f));
    } else for (unsigned i=0;i<f.count;++i) result[i]=convert(words[i],8*f.width,f.floating && f.width==2);
    return result;
}
int main() {
    assert(choose_cpu_vertices(0x1010,nullptr,nullptr,nullptr).mask==CpuSwap32);  // PowerVR: swap32 only (#207)
    assert(choose_cpu_vertices(0x1010,"auto",nullptr,nullptr).mask==CpuSwap32);
    for (uint32_t vendor : {0u,0x8086u,0x10deu,0x1002u,0x13b5u,0x5143u})
        assert(choose_cpu_vertices(vendor,nullptr,nullptr,nullptr).mask==0);
    assert(choose_cpu_vertices(0x1010,"0",nullptr,nullptr).mask==0);
    assert(choose_cpu_vertices(0,"1",nullptr,nullptr).mask==CpuAll);
    assert(choose_cpu_vertices(0x1010,"1","0","swap16").mask==0);
    assert(choose_cpu_vertices(0,"0","1","none").mask==CpuAll);
    assert(choose_cpu_vertices(0x1010,"auto",nullptr,"swap16,snorm").mask==(CpuSwap16|CpuSnorm));
    assert(choose_cpu_vertices(0x1010,nullptr,nullptr,"invalid").mask==CpuSwap32);
    assert(choose_cpu_vertices(0x1010,"1",nullptr,nullptr).mask==CpuAll);  // "On (all)" still converts everything
    uint32_t parsed;
    for (uint32_t mask=0;mask<=CpuAll;++mask) {
        assert(parse_cpu_vertex_classes(cpu_vertex_class_names(mask).c_str(),parsed) && parsed==mask);
        VertexDiagnostics d; d.cpuClasses=mask; d.cpuVertices=mask!=0;
        for (uint32_t other=0;other<mask;++other) {
            VertexDiagnostics b; b.cpuClasses=other;b.cpuVertices=other!=0;
            assert(d.shader_salt()!=b.shader_salt());
        }
    }
    assert(!parse_cpu_vertex_classes("swap16,",parsed));
    assert(!parse_cpu_vertex_classes("",parsed));
    // Mixed groups reserve distinct unused bindings, including on the minimum 16-binding device.
    struct Fetch { std::vector<LatteParsedFetchShaderBufferGroup> bufferGroups; } fetch;
    LatteParsedFetchShaderAttribute attrs[4]{};
    for (auto& a:attrs) { a.format=F::HWFMT_32_32_32_FLOAT;a.nfa=2;a.endianSwap=E::SWAP_U32; }
    attrs[1].endianSwap=attrs[3].endianSwap=E::SWAP_NONE;
    fetch.bufferGroups.resize(3);
    for (unsigned i=0;i<2;++i) {
        fetch.bufferGroups[i].attributeBufferIndex=i*2;
        fetch.bufferGroups[i].attribCount=2;
        fetch.bufferGroups[i].attrib=attrs+2*i;
    }
    fetch.bufferGroups[2].attributeBufferIndex=1;
    assert(cpu_vertex_count(fetch.bufferGroups[0],CpuSwap32)==1);
    assert(cpu_vertex_offset(fetch.bufferGroups[0],1,CpuSwap32)==16); // preceding unused semantic still owns a slot
    assert(cpu_vertex_offset(fetch.bufferGroups[0],1,CpuSwap16)==0);
    assert(cpu_vertex_offset(fetch.bufferGroups[0],2,CpuAll)==32);
    assert(cpu_vertex_binding(fetch,fetch.bufferGroups[0],CpuSwap32,16)==3);
    assert(cpu_vertex_binding(fetch,fetch.bufferGroups[1],CpuSwap32,16)==4);
    assert(cpu_vertex_binding(fetch,fetch.bufferGroups[1],CpuSwap32,4)==4); // exhausted
    assert(cpu_vertex_binding(fetch,fetch.bufferGroups[1],CpuAll,16)==2);
    assert(cpu_vertex_classes(attrs[0])==(CpuSwap32|CpuThreeComponent));
    attrs[0].format=F::HWFMT_16_16_FLOAT;attrs[0].endianSwap=E::SWAP_U16;
    assert(cpu_vertex_classes(attrs[0])==CpuSwap16); // half is not a scaled integer
    attrs[0].format=F::HWFMT_16_16;attrs[0].isSigned=1;attrs[0].nfa=0;attrs[0].offset=2;
    assert(cpu_vertex_classes(attrs[0])==(CpuSwap16|CpuSnorm|CpuUnaligned));
    unsigned cases=0;uint32_t random=0x207;
    for (const auto& f:formats) for (unsigned endian=0;endian<4;++endian)
    for (unsigned numeric=0;numeric<3;++numeric) for (unsigned sign=0;sign<2;++sign)
    for (unsigned offset=0;offset<4;++offset) for (unsigned pattern=0;pattern<20;++pattern) {
        LatteParsedFetchShaderAttribute a{};
        a.format=f.format;a.nfa=numeric;a.isSigned=sign;a.endianSwap=E(endian);a.offset=offset;
        if (f.floating) assert(!(cpu_vertex_classes(a)&CpuScaled));
        // Component classification includes half-float RGB, even for decoder combinations
        // which are explicitly unsupported; it follows the attribute shape, not its numeric mode.
        assert(bool(cpu_vertex_classes(a)&CpuThreeComponent)==(f.count==3));
        uint8_t storage[20]{};auto* p=storage+offset;
        for (unsigned i=0;i<16;++i) {
            random=random*1664525u+1013904223u;
            constexpr uint8_t edges[]={0,255,128,127,1,254};
            p[i]=pattern<6 ? edges[pattern] : uint8_t(random>>24);
        }
        uint32_t full[4];
        const bool supported=cpu_decode_attribute(a,p,full);
        assert(supported==bool(f.support[endian]&(1u<<(numeric*2+sign))));
        const auto expected=reference(f,a,p);
        for (unsigned i=0;i<4;++i) assert(full[i]==expected[i]);
        for (uint32_t mask=0;mask<=CpuAll;++mask) {
            uint32_t split[4];
            if (cpu_vertex_selected(a,mask)) cpu_decode_attribute(a,p,split);
            else std::memcpy(split,expected.data(),16); // unchanged raw shader path
            assert(!std::memcmp(full,split,16));
        }
        ++cases;
    }
    printf("vertex_cpu_classes_test: %u combinations/patterns, all 256 masks: ok\n",cases);
}
