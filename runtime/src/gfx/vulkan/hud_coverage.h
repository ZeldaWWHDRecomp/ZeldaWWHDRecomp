// HUD coverage for 60 fps (generated) (reproj.cpp): a variant of a pixel shader that also writes, to its
// first free colour output (1..7), (1, 1, 1, alpha) of its colour output 0. Drawn into an R8 image with a
// blend that follows the draw's own (draw.cpp, hud coverage), the image holds how much of the scene behind
// each pixel the HUD covers, so the in-between frame can move the scene behind translucent HUD parts
// (button frames, item icons, glows) as it moves the rest. The variant is made from the shader's SPIR-V:
// a new Output variable at that location, and before each return of the entry point a load of output 0,
// its alpha, and a store.
// From Nycolazs/WindWakerHD-Android-Plus (fc5ddee) via GreenNaugahyde/ZeldaWWHDRecompAndroid v0.9.9
// (db65d60, runtime/src/vk/hud_coverage.h), MPL-2.0: taken as published.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace hud_coverage {

// false (words untouched) when the shader isn't one with a float vec4 output 0 and a free output;
// `location`: where the coverage goes
inline bool add_output(std::vector<uint32_t>& words, uint32_t& location) {
    enum : uint32_t {
        OpEntryPoint = 15, OpDecorate = 71, OpTypeFloat = 22, OpTypeVector = 23, OpTypePointer = 32, OpConstant = 43,
        OpVariable = 59, OpFunction = 54, OpFunctionEnd = 56, OpReturn = 253, OpLoad = 61, OpStore = 62,
        OpCompositeExtract = 81, OpCompositeConstruct = 80, DecLocation = 30, Output = 3, Fragment = 4,
    };
    if (words.size() < 5 || words[0] != 0x07230203) return false;
    struct Ins { size_t at; uint32_t op, n; };
    std::vector<Ins> ins;
    for (size_t at = 5; at < words.size();) {
        uint32_t n = words[at] >> 16, op = words[at] & 0xFFFF;
        if (!n || at + n > words.size()) return false;
        ins.push_back({at, op, n});
        at += n;
    }
    auto w = [&](const Ins& i, uint32_t k) { return k < i.n ? words[i.at + k] : 0u; };
    // the entry point (fragment) and its function
    const Ins* entry = nullptr;
    for (auto& i : ins)
        if (i.op == OpEntryPoint && w(i, 1) == Fragment) { entry = &i; break; }
    if (!entry) return false;
    const uint32_t mainFn = w(*entry, 2);
    // outputs by location
    auto storage_of = [&](uint32_t id, uint32_t& ptrType) -> int {
        for (auto& i : ins)
            if (i.op == OpVariable && w(i, 2) == id) { ptrType = w(i, 1); return (int)w(i, 3); }
        return -1;
    };
    uint32_t out0 = 0, ptrType = 0, used = 0;
    const Ins* dec0 = nullptr;
    for (auto& i : ins) {
        if (i.op != OpDecorate || w(i, 2) != DecLocation) continue;
        uint32_t pt = 0;
        if (storage_of(w(i, 1), pt) != (int)Output) continue;
        if (w(i, 3) < 32) used |= 1u << w(i, 3);
        if (w(i, 3) == 0) { out0 = w(i, 1); ptrType = pt; dec0 = &i; }
    }
    if (!out0 || !dec0) return false;
    location = 0;
    for (uint32_t l = 1; l < 8 && !location; l++)
        if (!(used >> l & 1)) location = l;
    if (!location) return false;
    uint32_t vec4T = 0, floatT = 0;
    for (auto& i : ins)
        if (i.op == OpTypePointer && w(i, 1) == ptrType && w(i, 2) == Output) vec4T = w(i, 3);
    for (auto& i : ins)
        if (i.op == OpTypeVector && w(i, 1) == vec4T && w(i, 3) == 4) floatT = w(i, 2);
    bool isFloat = false;
    for (auto& i : ins)
        if (i.op == OpTypeFloat && w(i, 1) == floatT && w(i, 2) == 32) isFloat = true;
    if (!vec4T || !floatT || !isFloat) return false;
    uint32_t bound = words[3];
    uint32_t one = 0;
    for (auto& i : ins)
        if (i.op == OpConstant && w(i, 1) == floatT && i.n == 4 && w(i, 3) == 0x3F800000u) { one = w(i, 2); break; }
    const bool newOne = !one;
    if (newOne) one = bound++;
    const uint32_t cov = bound++;
    // rebuilt with the additions
    std::vector<uint32_t> out(words.begin(), words.begin() + 5);
    out.reserve(words.size() + 64);
    bool globalsDone = false, inMain = false;
    for (auto& i : ins) {
        if (i.op == OpFunction && !globalsDone) {
            globalsDone = true;
            if (newOne) out.insert(out.end(), {(4u << 16) | OpConstant, floatT, one, 0x3F800000u});
            out.insert(out.end(), {(4u << 16) | OpVariable, ptrType, cov, Output});
        }
        if (i.op == OpFunction) inMain = w(i, 2) == mainFn;
        if (&i == entry) {
            out.push_back(((i.n + 1) << 16) | OpEntryPoint);
            out.insert(out.end(), words.begin() + i.at + 1, words.begin() + i.at + i.n);
            out.push_back(cov);
            continue;
        }
        if (inMain && i.op == OpReturn) {
            const uint32_t l = bound++, a = bound++, c = bound++;
            out.insert(out.end(), {(4u << 16) | OpLoad, vec4T, l, out0});
            out.insert(out.end(), {(5u << 16) | OpCompositeExtract, floatT, a, l, 3u});
            out.insert(out.end(), {(7u << 16) | OpCompositeConstruct, vec4T, c, one, one, one, a});
            out.insert(out.end(), {(3u << 16) | OpStore, cov, c});
        }
        out.insert(out.end(), words.begin() + i.at, words.begin() + i.at + i.n);
        if (&i == dec0) out.insert(out.end(), {(4u << 16) | OpDecorate, cov, DecLocation, location});
        if (i.op == OpFunctionEnd) inMain = false;
    }
    if (!globalsDone) return false;
    out[3] = bound;
    words.swap(out);
    return true;
}

}  // namespace hud_coverage
