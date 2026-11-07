// Decomp build: native stack frames (gmem_frame_info, decomp/tools/verify/include/native_frames.h).
//
// gabi's automatic native frame replays the original's prologue (frame size, back chain, LR and the
// callee-saved registers it saves on entry) and runs the decompiled function's callees at the
// original's sp, so a decompiled function leaves the guest stack as the original does (stale stack
// bytes reach game data, e.g. the padding of the save file). The decompilation's harness reads these
// frames from a table its tools/verify/frames_all.py writes from a disassembly of the game. Here they
// come from the game's own code in guest memory at start-up, with the same rules (this file is a port
// of frames_all.py's analyse / objects_lo / frameless; WWHD_DECOMP_FRAMES_DUMP=path writes the result
// for every function in frames_all.py's format, to compare the two).
//
// The rules work on the disassembly text frames_all.py sees: a mnemonic and its first operand. Here an
// instruction is decoded into the same facts: whether it is a branch, one of the stores and moves the
// analysis looks at, an instruction that writes no register by those rules (stores, compares, moves to
// special registers, cache and sync instructions, traps), or which GPR / FPR its first operand names.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "decomp/decomp.h"
#include "native_frames.h"
#include "../runtime.h"
#include "recomp_table.h"

namespace decomp {
namespace {

enum Kind {
    kOther,      // writes the register in `dest` (or none: dest < 0)
    kNoWrite,    // stores, compares, mt*, cache, sync, traps (frames_all.py NOWRITE)
    kBranch,     // any b* mnemonic
    kStwu,       // stwu rS, d(rA)
    kStwux,
    kMflr,
    kStore,      // stw / stmw / stfd / stfs / psq_st (the save candidates)
    kMerge10,    // ps_merge10 fD, fA, fB
    kAddi,       // addi rD, rA, SIMM (rA != 0)
};
enum StoreOp { kStw, kStmw, kStfd, kStfs, kPsqSt };
struct Ins {
    Kind kind = kOther;
    int dest = -1;      // first operand: 0..31 rN, 32..63 fN, -1 none
    int rs = 0, ra = 0, rb = 0;
    int32_t d = 0;      // displacement / immediate
    StoreOp st = kStw;
    // branches
    bool link = false;
    bool target_known = false;  // a relative/absolute target (b, bc)
    uint32_t target = 0;
    bool ctr_tail = false;      // bctr, beqctr, bnectr, bltctr, bgectr
};

constexpr int F(int n) { return 32 + n; }

Ins decode(uint32_t addr, uint32_t w) {
    Ins in;
    const int op = w >> 26, rd = (w >> 21) & 31, ra = (w >> 16) & 31, rb = (w >> 11) & 31;
    const int xo10 = (w >> 1) & 0x3FF, xo5 = (w >> 1) & 31, xo6 = (w >> 1) & 63;
    const int32_t simm = (int16_t)(w & 0xFFFF);
    in.rs = rd; in.ra = ra; in.rb = rb; in.d = simm;
    auto none = [&] { in.kind = kNoWrite; return in; };
    auto dst = [&](int r) { in.kind = kOther; in.dest = r; return in; };
    switch (op) {
    case 3: return none();                                   // twi
    case 7: case 8: case 12: case 13: case 15: return dst(rd);  // mulli subfic addic addic. addis
    case 14:
        if (ra != 0) { in.kind = kAddi; in.dest = rd; return in; }
        return dst(rd);                                      // li
    case 10: case 11: return none();                         // cmplwi cmpwi
    case 16: {                                               // bc
        in.kind = kBranch;
        in.link = w & 1;
        in.target_known = true;
        int32_t bd = (int16_t)(w & 0xFFFC);
        in.target = (w & 2) ? (uint32_t)bd : addr + bd;
        return in;
    }
    case 17: return in;                                      // sc: no operands
    case 18: {                                               // b
        in.kind = kBranch;
        in.link = w & 1;
        in.target_known = true;
        int32_t li = (int32_t)((w & 0x03FFFFFC) << 6) >> 6;
        in.target = (w & 2) ? (uint32_t)li : addr + li;
        return in;
    }
    case 19:
        if (xo10 == 16 || xo10 == 528) {                     // bclr / bcctr
            in.kind = kBranch;
            in.link = w & 1;
            if (xo10 == 528 && !in.link) {
                const int bo = rd, cond = ra & 3;
                in.ctr_tail = bo == 20 || ((bo == 12 || bo == 4) && (cond == 0 || cond == 2));
            }
            return in;
        }
        return none();                                       // cr ops, mcrf, isync, rfi
    case 20: case 21: case 23: return dst(ra);               // rlwimi rlwinm rlwnm
    case 24:                                                 // ori (nop)
        if (w == 0x60000000u) return none();
        return dst(ra);
    case 25: case 26: case 27: case 28: case 29: return dst(ra);
    case 31:
        switch (xo10) {
        case 183: in.kind = kStwux; return in;
        case 339:                                            // mfspr
            if ((((w >> 16) & 31) | (((w >> 11) & 31) << 5)) == 8) { in.kind = kMflr; in.dest = rd; return in; }
            return dst(rd);
        case 23: case 55: case 87: case 119: case 279: case 311: case 343: case 375: case 20: case 790: case 534:
        case 533: case 597: case 310: case 19: case 83: case 595: case 659: case 371:
            return dst(rd);                                  // loads, mfcr, mfmsr, mfsr, mftb
        case 535: case 567: case 599: case 631: return dst(F(rd));  // lfsx lfsux lfdx lfdux
        case 28: case 60: case 444: case 412: case 316: case 124: case 476: case 284: case 24: case 536: case 792:
        case 824: case 26: case 922: case 954:
            return dst(ra);                                  // logical, shifts, cntlzw, exts
        case 438: return dst(rd);                            // ecowx (first operand rS)
        case 306: return dst(rb);                            // tlbie rB
        case 0: case 32: case 4: case 151: case 215: case 247: case 407: case 439: case 662: case 918: case 150:
        case 661: case 725: case 663: case 695: case 727: case 759: case 983: case 86: case 54: case 278: case 246:
        case 1014: case 470: case 982: case 598: case 854: case 467: case 144: case 146: case 210: case 242:
        case 512: case 566:
            return none();
        }
        switch (xo10 & 0x1FF) {                              // XO-form arithmetic (OE either way)
        case 266: case 10: case 138: case 234: case 202: case 40: case 8: case 136: case 232: case 200: case 104:
        case 235: case 75: case 11: case 491: case 459:
            return dst(rd);
        }
        return in;                                           // not an instruction: no operands
    case 32: case 33: case 34: case 35: case 40: case 41: case 42: case 43: case 46: return dst(rd);
    case 48: case 49: case 50: case 51: case 56: case 57: return dst(F(rd));
    case 36: in.kind = kStore; in.st = kStw; return in;
    case 37:
        if (rd == 1) { in.kind = kStwu; return in; }
        return none();
    case 47: in.kind = kStore; in.st = kStmw; return in;
    case 52: in.kind = kStore; in.st = kStfs; return in;
    case 54: in.kind = kStore; in.st = kStfd; return in;
    case 60:                                                 // psq_st fS, d(rA), W, I: 12-bit displacement
        in.kind = kStore; in.st = kPsqSt;
        in.d = (int32_t)((w & 0xFFF) << 20) >> 20;
        return in;
    case 38: case 39: case 44: case 45: case 53: case 55: case 61: return none();
    case 59:
        switch (xo5) {
        case 18: case 20: case 21: case 22: case 24: case 25: case 28: case 29: case 30: case 31: return dst(F(rd));
        }
        return in;
    case 63:
        switch (xo5) {
        case 18: case 20: case 21: case 22: case 23: case 25: case 26: case 28: case 29: case 30: case 31: return dst(F(rd));
        }
        switch (xo10) {
        case 583: case 40: case 72: case 136: case 264: case 12: case 14: case 15: return dst(F(rd));
        case 0: case 32: case 64: case 38: case 70: case 134: case 711: return none();
        }
        return in;
    case 4:
        switch (xo6) {
        case 6: case 38: return dst(F(rd));                  // psq_lx psq_lux
        case 7: case 39: return none();                      // psq_stx psq_stux
        }
        switch (xo5) {
        case 10: case 11: case 12: case 13: case 14: case 15: case 18: case 20: case 21: case 23: case 24: case 25:
        case 26: case 28: case 29: case 30: case 31:
            return dst(F(rd));
        }
        switch (xo10) {
        case 592:
            if (rd == ra && ra == rb) { in.kind = kMerge10; in.dest = F(rd); return in; }
            return dst(F(rd));
        case 40: case 72: case 136: case 264: case 528: case 560: case 624: return dst(F(rd));
        case 0: case 32: case 64: case 96: case 1014: return none();
        }
        return in;
    }
    return in;
}

struct Item { int kind; int reg; int32_t off; };  // kind: GABI_FI_*
struct Result {
    bool present = false;   // frames_all.py writes a row
    int32_t size = 0;
    std::vector<Item> items;
    std::vector<std::string> flags;
    int32_t objects_lo = -1;
};

uint32_t word(uint32_t a) { return ld32(a); }

// frames_all.py frameless(): no stwu
Result frameless(uint32_t addr, uint32_t size) {
    Result r;
    bool tail = false;
    for (uint32_t k = 0; k < size / 4; k++) {
        uint32_t a = addr + 4 * k;
        Ins in = decode(a, word(a));
        if (in.kind != kBranch) continue;
        if (in.link) return r;  // a linking branch: left out
        if (in.ctr_tail) tail = true;
        else if (in.target_known && !(addr <= in.target && in.target < addr + size)) tail = true;
    }
    if (tail) { r.present = true; r.size = 0; r.flags = {"tail"}; }
    return r;
}

// frames_all.py objects_lo(): the first addi rD, r1, OFF with 8 <= OFF < frame, in code order
int32_t objects_lo(uint32_t addr, uint32_t size, int32_t frame) {
    for (uint32_t k = 0; k < size / 4; k++) {
        Ins in = decode(addr + 4 * k, word(addr + 4 * k));
        if (in.kind == kAddi && in.ra == 1 && 8 <= in.d && in.d < frame) return in.d;
    }
    return -1;
}

Result analyse(uint32_t addr, uint32_t size) {
    Result r;
    const uint32_t n = std::min<uint32_t>(size / 4, 64);
    bool have_sp = false;
    int32_t sp_delta = 0;
    uint64_t written = 0;  // bit i: register i (0..31 r, 32..63 f)
    uint32_t swapped = 0;  // FPRs whose halves ps_merge10 exchanged
    int lr_reg = -1;
    struct Pending { int kind, reg; int32_t off; };
    std::vector<Pending> pending;
    auto add = [&](int kind, int reg, int32_t d) {
        if (!have_sp) pending.push_back({kind, reg, d});
        else r.items.push_back({kind, reg, d});
    };
    auto fail = [&](const char* flag) {
        r = Result();
        r.present = true;
        r.flags = {flag};
        return r;
    };
    for (uint32_t k = 0; k < n; k++) {
        uint32_t a = addr + 4 * k;
        Ins in = decode(a, word(a));
        if (in.kind == kBranch) break;
        if (in.kind == kStwu) {
            if (in.ra != 1 || have_sp) return fail("stwu");
            have_sp = true;
            sp_delta = -in.d;
            r.items.insert(r.items.begin(), Item{GABI_FI_BACKCHAIN, 0, 0});
            for (auto& p : pending) r.items.push_back({p.kind, p.reg, p.off + sp_delta});
            continue;
        }
        if (in.kind == kStwux) return fail("stwux");
        if (in.kind == kMflr) {
            lr_reg = in.dest;
            written |= 1ull << in.dest;
            continue;
        }
        if (in.kind == kStore) {
            if (in.ra != 1) continue;
            const int reg = in.rs;
            if (in.st == kStw && reg == lr_reg) {
                add(GABI_FI_LR, 0, in.d);
            } else if (in.st == kStmw) {
                bool body = false;
                for (int x = reg; x < 32; x++) body |= (written >> x) & 1;
                if (body) continue;  // not a save of entry values: body content
                for (int x = reg; x < 32; x++) add(GABI_FI_GPR, x, in.d + 4 * (x - reg));
            } else if (in.st == kStw) {
                if (reg < 14) continue;               // an argument spill
                if ((written >> reg) & 1) continue;   // a modified register: body content
                add(GABI_FI_GPR, reg, in.d);
            } else {
                if (reg < 14) continue;
                if ((written >> F(reg)) & 1) continue;
                if (in.st == kStfd) add(GABI_FI_STFD, reg, in.d);
                else if (in.st == kStfs) add((swapped >> reg) & 1 ? GABI_FI_STFS1 : GABI_FI_STFS0, reg, in.d);
                else add(GABI_FI_PSQ, reg, in.d);
            }
            continue;
        }
        if (in.kind == kMerge10) {
            swapped ^= 1u << (in.dest - 32);
            continue;
        }
        if (in.kind == kNoWrite || in.dest < 0) continue;
        written |= 1ull << in.dest;
        if (in.dest == lr_reg) lr_reg = -1;  // the register no longer holds LR
        if (in.dest == 1) {
            if (have_sp) break;  // the epilogue (addi r1,r1,N) of a function without a call
            return fail("r1");
        }
    }
    if (!have_sp) return frameless(addr, size);
    r.present = true;
    r.size = sp_delta;
    r.objects_lo = objects_lo(addr, size, sp_delta);
    return r;
}

uint32_t func_size(const RecompEntry* e) {
    const RecompEntry* end = g_recomp_funcs + g_recomp_func_count;
    return e + 1 < end ? e[1].addr - e->addr : 0;  // as frames_all.py: the last function has none
}

const char* kItemTag[] = {"b", "l", "g", "d", "s0", "s1", "p"};

void dump(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (unsigned i = 0; i < g_recomp_func_count; i++) {
        const RecompEntry* e = &g_recomp_funcs[i];
        Result r = analyse(e->addr, func_size(e));
        if (!r.present) continue;
        fprintf(f, "%08X\t%d\t", e->addr, r.size);
        bool first = true;
        for (auto& it : r.items) {
            fputs(first ? "" : " ", f);
            first = false;
            if (it.kind == GABI_FI_BACKCHAIN) fputs("b", f);
            else if (it.kind == GABI_FI_LR) fprintf(f, "l:%s%x", it.off < 0 ? "-" : "", std::abs(it.off));
            else fprintf(f, "%s:%d:%s%x", kItemTag[it.kind], it.reg, it.off < 0 ? "-" : "", std::abs(it.off));
        }
        if (r.objects_lo >= 0) fprintf(f, "%so:%x", first ? "" : " ", r.objects_lo);
        fputc('\t', f);
        for (size_t k = 0; k < r.flags.size(); k++) fprintf(f, "%s%s", k ? "," : "", r.flags[k].c_str());
        fputc('\n', f);
    }
    fclose(f);
    LOG("[decomp] native frames of every function written to %s", path);
}

// per slot (g_slots order); info.size 0 + no tail flag = no frame
std::vector<std::vector<GabiFrameItem>> g_items;
std::vector<GabiFrameInfo> g_info;
std::vector<uint8_t> g_has;
// gmem_frame_info's lookup (every decompiled entry, gabi AutoFrame): open addressing over the functions with a
// frame, power of two, under 1/2 full
struct FrameEntry {
    uint32_t addr;  // 0: empty
    const GabiFrameInfo* info;
};
std::vector<FrameEntry> g_lookup;
uint32_t g_lookup_mask = 0;
inline uint32_t frame_hash(uint32_t addr) { return (addr >> 2) * 0x9E3779B1u; }
}  // namespace

void frames_init() {
    if (const char* p = getenv("WWHD_DECOMP_FRAMES_DUMP")) dump(p);
    const char* off = getenv("WWHD_DECOMP_FRAMES");
    if (off && !strcmp(off, "0")) {
        LOG("[decomp] WWHD_DECOMP_FRAMES=0: native frames off");
        return;
    }
    g_items.assign(kNumSlots, {});
    g_info.assign(kNumSlots, GabiFrameInfo{});
    g_has.assign(kNumSlots, 0);
    const RecompEntry* b = g_recomp_funcs;
    const RecompEntry* end = g_recomp_funcs + g_recomp_func_count;
    unsigned n = 0;
    for (unsigned i = 0; i < kNumSlots; i++) {
        const RecompEntry* e = std::lower_bound(b, end, g_slots[i].addr, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
        if (e == end || e->addr != g_slots[i].addr) continue;
        Result r = analyse(e->addr, func_size(e));
        // as the decompilation's table loader (native_frames.cpp): a size-0 row counts only with "tail"
        bool tail = std::find(r.flags.begin(), r.flags.end(), "tail") != r.flags.end();
        if (!r.present || (!r.size && !tail)) continue;
        auto& items = g_items[i];
        for (auto& it : r.items) items.push_back(GabiFrameItem{(uint8_t)it.kind, (uint8_t)it.reg, (uint16_t)it.off});
        g_info[i] = GabiFrameInfo{(uint32_t)r.size, (uint32_t)items.size(), items.data(), tail ? (uint32_t)GABI_FF_TAIL : 0u,
                                  r.objects_lo >= 0 ? (uint32_t)r.objects_lo : 0u};
        g_has[i] = 1;
        n++;
    }
    uint32_t size = 64;
    while (size < 2 * n + 2) size *= 2;
    g_lookup.assign(size, FrameEntry{0, nullptr});
    g_lookup_mask = size - 1;
    for (unsigned i = 0; i < kNumSlots; i++) {
        if (!g_has[i]) continue;
        uint32_t h = frame_hash(g_slots[i].addr) & g_lookup_mask;
        while (g_lookup[h].addr) h = (h + 1) & g_lookup_mask;
        g_lookup[h] = FrameEntry{g_slots[i].addr, &g_info[i]};
    }
    LOG("[decomp] native frames: %u of %u decompiled functions", n, kNumSlots);
}
}  // namespace decomp

extern "C" const GabiFrameInfo* gmem_frame_info(uint32_t addr) {
    using namespace decomp;
    if (g_lookup.empty() || !addr) return nullptr;
    for (uint32_t h = frame_hash(addr) & g_lookup_mask;; h = (h + 1) & g_lookup_mask) {
        const FrameEntry& e = g_lookup[h];
        if (e.addr == addr) return e.info;
        if (!e.addr) return nullptr;
    }
}
