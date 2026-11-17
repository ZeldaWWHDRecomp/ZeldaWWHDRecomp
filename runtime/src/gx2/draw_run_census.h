// Draw-run census (diagnostics only, WWHD_DRAW_RUN_CENSUS=1; batching design, nothing is drawn differently):
// for every draw on the executing thread, what changed since the previous draw, and which join rule could have
// merged it with that draw. Logged every 300 frames as "[run census]" lines. Included by gx2_core.cpp only.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace run_census {

enum Change : uint32_t {
    kVsAddr = 1, kVsOther = 2, kPsAddr = 4, kPsOther = 8, kAlu = 16, kVb = 32, kFetchAddr = 64, kOtherReg = 128,
    kOtherOp = 256, kPacket = 512,
};
enum Cat { kSame0, kVsOnly, kVsPs, kVsPsAlu, kWeather, kIdxCont, kIdxAny, kNone, kCats };
inline const char* cat_name(int c) {
    static const char* n[kCats] = {"identical", "VS-block-addr", "VS+PS-block-addr", "+ALU", "VB/fetch/blocks",
                                   "index-continue", "index-any", "no rule"};
    return n[c];
}

struct State {
    bool on = false, init = false, havePrev = false;
    uint32_t changed = 0;
    uint32_t prevOp = 0, prevN = 0, prevArgs[6] = {};
    uint64_t frames = 0, draws = 0, packets = 0, inst = 0;
    uint64_t cat[kCats] = {};
    uint64_t runs[kCats] = {};  // runs of 2+ (a join after a non-join of that category)
    int prevCat = kNone;
    std::unordered_map<uint64_t, uint64_t> byVs[kCats];  // VS address -> joins
    std::unordered_map<uint32_t, uint64_t> breakers;     // first non-VS-addr register changed (same args, VS rule broken)
    uint32_t firstBreak = 0;
};
inline State& st() { static State s; return s; }
inline bool on() {
    State& s = st();
    if (!s.init) {
        s.init = true;
        const char* e = std::getenv("WWHD_DRAW_RUN_CENSUS");
        s.on = e && *e == '1';
    }
    return s.on;
}

inline uint32_t reg_change(uint32_t r) {
    if (r >= mmSQ_ALU_CONSTANT0_0 && r < mmSQ_ALU_CONSTANT0_0 + 0x1000) return kAlu;
    if (r >= mmSQ_VTX_UNIFORM_BLOCK_START && r < mmSQ_VTX_UNIFORM_BLOCK_START + 7 * 16)
        return (r - mmSQ_VTX_UNIFORM_BLOCK_START) % 7 == 0 ? kVsAddr : kVsOther;
    if (r >= mmSQ_PS_UNIFORM_BLOCK_START && r < mmSQ_PS_UNIFORM_BLOCK_START + 7 * 16)
        return (r - mmSQ_PS_UNIFORM_BLOCK_START) % 7 == 0 ? kPsAddr : kPsOther;
    if (r >= mmSQ_VTX_ATTRIBUTE_BLOCK_START && r < mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7 * 16)
        return (r - mmSQ_VTX_ATTRIBUTE_BLOCK_START) % 7 < 2 ? kVb : kOtherReg;
    if (r == mmSQ_PGM_START_FS || r == mmSQ_PGM_START_FS + 1) return kFetchAddr;
    return kOtherReg;
}

// a register write about to be applied (regs: the values before it)
inline void regs(const uint32_t* regs, uint32_t first, const uint32_t* v, uint32_t n) {
    State& s = st();
    for (uint32_t i = 0; i < n; ++i)
        if (regs[first + i] != v[i]) {
            const uint32_t c = reg_change(first + i);
            if (c != kVsAddr && !(s.changed & ~kVsAddr) && !s.firstBreak) s.firstBreak = first + i;
            s.changed |= c;
        }
}
inline void other_op(bool packet) {
    State& s = st();
    s.changed |= packet ? kPacket : kOtherOp;
    if (packet) ++s.packets;
}

inline uint32_t index_bytes(uint32_t type) { return type == 1 || type == 9 ? 4 : 2; }

inline void draw(const uint32_t* regs, uint32_t op, const uint32_t* p, uint32_t n, bool indexed) {
    State& s = st();
    ++s.draws;
    const uint32_t instances = indexed ? p[5] : p[3];
    if (instances != 1) ++s.inst;
    int c = kNone;
    if (s.havePrev && op == s.prevOp && n == s.prevN && instances == 1) {
        const bool same = !std::memcmp(p, s.prevArgs, n * 4);
        const uint32_t ch = s.changed;
        if (same && !ch) c = kSame0;
        else if (same && !(ch & ~kVsAddr)) c = kVsOnly;
        else if (same && !(ch & ~(kVsAddr | kPsAddr))) c = kVsPs;
        else if (same && !(ch & ~(kVsAddr | kPsAddr | kAlu))) c = kVsPsAlu;
        else if (same && !(ch & ~(kVsAddr | kPsAddr | kVb | kFetchAddr))) c = kWeather;
        else if (!ch && p[0] == s.prevArgs[0]) {
            if (indexed) {
                const bool listOrStrip = p[0] == 4 || p[0] == 6;
                if (listOrStrip && p[2] == s.prevArgs[2] && p[4] == s.prevArgs[4] &&
                    p[3] == s.prevArgs[3] + s.prevArgs[1] * index_bytes(p[2]))
                    c = kIdxCont;
                else if (listOrStrip && p[2] == s.prevArgs[2]) c = kIdxAny;
            } else if (p[2] == s.prevArgs[2] + s.prevArgs[1]) c = kIdxCont;
        }
        if (same && c == kNone && s.firstBreak) ++s.breakers[s.firstBreak];
    }
    if (c != kNone) {
        ++s.cat[c];
        if (s.prevCat != c) ++s.runs[c];
        ++s.byVs[c][regs[mmSQ_PGM_START_VS]];
    }
    s.prevCat = c;
    s.havePrev = true;
    s.prevOp = op;
    s.prevN = n;
    std::memcpy(s.prevArgs, p, std::min<uint32_t>(n, 6) * 4);
    s.changed = 0;
    s.firstBreak = 0;
}

inline void frame_end() {
    State& s = st();
    if (++s.frames < 300) return;
    const double f = double(s.frames);
    LOG("[run census] per frame: %.1f draws (%.1f instanced), %.1f native packet draws", s.draws / f, s.inst / f,
        s.packets / f);
    for (int c = 0; c < kNone; ++c) {
        std::vector<std::pair<uint64_t, uint64_t>> v;
        for (auto& [k, x] : s.byVs[c]) v.push_back({x, k});
        std::sort(v.rbegin(), v.rend());
        char buf[512];
        int k = 0;
        for (size_t i = 0; i < v.size() && i < 6 && k < int(sizeof buf) - 40; ++i)
            k += std::snprintf(buf + k, sizeof buf - k, " %08llX:%.1f", (unsigned long long)v[i].second, v[i].first / f);
        buf[k] = 0;
        LOG("[run census]   %-18s joins %.1f in %.1f runs; by VS:%s", cat_name(c), s.cat[c] / f, s.runs[c] / f, buf);
    }
    {
        std::vector<std::pair<uint64_t, uint32_t>> v;
        for (auto& [k, x] : s.breakers) v.push_back({x, k});
        std::sort(v.rbegin(), v.rend());
        char buf[512];
        int k = 0;
        for (size_t i = 0; i < v.size() && i < 10 && k < int(sizeof buf) - 40; ++i)
            k += std::snprintf(buf + k, sizeof buf - k, " %04X:%.1f", v[i].second, v[i].first / f);
        buf[k] = 0;
        LOG("[run census]   same-args draws not joined, first register breaking the VS rule:%s", buf);
    }
    s.frames = s.draws = s.packets = s.inst = 0;
    for (auto& x : s.cat) x = 0;
    for (auto& x : s.runs) x = 0;
    for (auto& m : s.byVs) m.clear();
    s.breakers.clear();
}

}  // namespace run_census
