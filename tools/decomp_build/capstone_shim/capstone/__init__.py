"""A capstone stand-in for the decomp version's declaration table on Android (tools/decomp_build/decl_table.py runs the
decompilation's decl_lint.py, which disassembles the game's .text with capstone 5 in PowerPC 32-bit big-endian
paired-single mode and reads mnemonic + operand text).

capstone's Python wheels load no library on Android (bionic), so setup puts this folder on PYTHONPATH there instead.
It covers what decl_lint uses -- Cs(arch, mode), .skipdata, .disasm(code, address) yielding objects with .address,
.mnemonic, .op_str, .bytes, .size -- and prints every instruction of the game exactly as capstone 5.0.7 does
(tools/decomp_build/test_capstone_shim.py compares the whole USA cking.rpx .text with the real capstone when it is
installed). Anything it cannot decode becomes capstone's skipdata ".byte" line, as capstone does for invalid words.
"""
from collections import namedtuple

__version__ = "5.0.7-wwhd-shim"
CS_ARCH_PPC = 4
CS_MODE_32 = 1 << 2
CS_MODE_BIG_ENDIAN = 1 << 31
CS_MODE_PS = 1 << 3

Insn = namedtuple("Insn", "address mnemonic op_str bytes size")


def _imm(v):
    """capstone's printImm: 0..9 decimal, larger hex; negatives the same with '-'"""
    if v < 0:
        return "-%d" % -v if v > -10 else "-0x%x" % -v
    return "%d" % v if v < 10 else "0x%x" % v


def _s16(v):
    return v - 0x10000 if v & 0x8000 else v


def r(n):
    return "r%d" % n


def f(n):
    return "f%d" % n


def _ra0(n):
    """RA in (RA|0) operands: register 0 reads as 0"""
    return "0" if n == 0 else r(n)


def _mem(d, ra):
    return "%s(%s)" % (_imm(d), r(ra))


_BO_COND = {(12, 2): "eq", (4, 2): "ne", (12, 0): "lt", (4, 0): "ge", (12, 1): "gt", (4, 1): "le",
            (12, 3): "so", (4, 3): "ns"}
_CR_BITS = ("lt", "gt", "eq", "un")
_SPR = {1: "xer", 8: "lr", 9: "ctr"}


def _branch_cond(bo, bi):
    """(mnemonic stem, cr prefix) for a conditional branch, or None for forms this shim does not print"""
    bo_h = bo & ~1  # the branch-prediction hint bit (capstone 5 drops it)
    if bo_h in (12, 4):
        c = _BO_COND.get((bo_h, bi & 3))
        if c is None:
            return None
        return c, ("cr%d, " % (bi >> 2) if bi >> 2 else "")
    if bo_h in (16, 18):
        return ("bdnz" if bo_h == 16 else "bdz"), None
    if bo_h == 20:
        return "", None
    return None


def decode(w, addr):
    """(mnemonic, op_str) or None"""
    op = w >> 26
    rd, ra, rb = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    simm, uimm = _s16(w & 0xFFFF), w & 0xFFFF
    if op == 14:
        if ra == 0:
            return "li", "%s, %s" % (r(rd), _imm(simm))
        return "addi", "%s, %s, %s" % (r(rd), r(ra), _imm(simm))
    if op == 15:
        if ra == 0:
            return "lis", "%s, %s" % (r(rd), _imm(simm))
        return "addis", "%s, %s, %s" % (r(rd), r(ra), _imm(simm))
    if op in (12, 13):
        return ("addic" if op == 12 else "addic."), "%s, %s, %s" % (r(rd), r(ra), _imm(simm))
    if op == 7:
        return "mulli", "%s, %s, %s" % (r(rd), r(ra), _imm(simm))
    if op == 8:
        return "subfic", "%s, %s, %s" % (r(rd), r(ra), _imm(simm))
    if op in (10, 11):
        crf, l = rd >> 2, rd & 1
        if l:
            return None
        name = "cmplwi" if op == 10 else "cmpwi"
        v = _imm(uimm) if op == 10 else _imm(simm)
        return name, ("cr%d, " % crf if crf else "") + "%s, %s" % (r(ra), v)
    if op in (24, 25, 26, 27, 28, 29):
        if op == 24 and w == 0x60000000:
            return "nop", ""
        name = {24: "ori", 25: "oris", 26: "xori", 27: "xoris", 28: "andi.", 29: "andis."}[op]
        return name, "%s, %s, %s" % (r(ra), r(rd), _imm(uimm))
    if op == 18:
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        aa, lk = (w >> 1) & 1, w & 1
        tgt = (li if aa else addr + li) & 0xFFFFFFFF
        return ("b" + ("l" if lk else "") + ("a" if aa else "")), "0x%x" % tgt
    if op == 16:
        bo, bi = rd, ra
        bd = w & 0xFFFC
        if bd & 0x8000:
            bd -= 0x10000
        aa, lk = (w >> 1) & 1, w & 1
        tgt = (bd if aa else addr + bd) & 0xFFFFFFFF
        c = _branch_cond(bo, bi)
        if c is None or c[0] == "":
            return None
        stem, pre = c
        name = (stem if stem.startswith("bd") else "b" + stem) + ("l" if lk else "") + ("a" if aa else "")
        return name, (pre or "") + "0x%x" % tgt
    if op == 19:
        xo = (w >> 1) & 0x3FF
        lk = w & 1
        if xo in (16, 528):
            if rb or (w >> 11) & 3:
                return None
            bo, bi = rd, ra
            tail = "lr" if xo == 16 else "ctr"
            c = _branch_cond(bo, bi)
            if c is None:
                return None
            stem, pre = c
            if stem == "":
                return "b" + tail + ("l" if lk else ""), ""
            if stem.startswith("bd"):
                if xo == 528:
                    return None
                return stem + "lr" + ("l" if lk else ""), ""
            return "b" + stem + tail + ("l" if lk else ""), (pre or "").rstrip(", ")
        if xo == 150 and not lk:
            return "isync", ""
        if xo in (193, 289) and not lk and rd == ra == rb:  # crxor / creqv of a bit with itself
            b = rd
            return ("crclr" if xo == 193 else "crset"), "cr%d%s" % (b >> 2, _CR_BITS[b & 3])
        return None
    if op == 20 or op == 21:  # rlwimi / rlwinm
        sh, mb, me, rc = rb, (w >> 6) & 31, (w >> 1) & 31, w & 1
        dot = "." if rc else ""
        if op == 20:
            return "rlwimi" + dot, "%s, %s, %s, %s, %s" % (r(ra), r(rd), _imm(sh), _imm(mb), _imm(me))
        # capstone 5's aliases (in its order); with Rc only clrlwi.
        if sh == 0 and me == 31 and (rc or mb):
            return "clrlwi" + dot, "%s, %s, %s" % (r(ra), r(rd), _imm(mb))
        if not rc:
            if mb == 0 and me == 31 - sh:
                return "slwi", "%s, %s, %s" % (r(ra), r(rd), _imm(sh))
            if mb == 0 and me == 31:
                return "rotlwi", "%s, %s, %s" % (r(ra), r(rd), _imm(sh))
            if me == 31 and sh == (32 - mb) & 31 and sh:
                return "srwi", "%s, %s, %s" % (r(ra), r(rd), _imm(mb))
        return "rlwinm" + dot, "%s, %s, %s, %s, %s" % (r(ra), r(rd), _imm(sh), _imm(mb), _imm(me))
    if op == 23:
        mb, me, rc = (w >> 6) & 31, (w >> 1) & 31, w & 1
        if mb == 0 and me == 31:
            return "rotlw" + ("." if rc else ""), "%s, %s, %s" % (r(ra), r(rd), r(rb))
        return "rlwnm" + ("." if rc else ""), "%s, %s, %s, %s, %s" % (r(ra), r(rd), r(rb), _imm(mb), _imm(me))
    if 32 <= op <= 55:
        names = {32: "lwz", 33: "lwzu", 34: "lbz", 35: "lbzu", 36: "stw", 37: "stwu", 38: "stb", 39: "stbu",
                 40: "lhz", 41: "lhzu", 42: "lha", 43: "lhau", 44: "sth", 45: "sthu", 46: "lmw", 47: "stmw",
                 48: "lfs", 49: "lfsu", 50: "lfd", 51: "lfdu", 52: "stfs", 53: "stfsu", 54: "stfd", 55: "stfdu"}
        reg = f(rd) if op >= 48 else r(rd)
        return names[op], "%s, %s" % (reg, _mem(simm, ra))
    if op in (56, 57, 60, 61):  # psq_l, psq_lu, psq_st, psq_stu
        d = w & 0xFFF
        if d & 0x800:
            d -= 0x1000
        wbit, i = (w >> 15) & 1, (w >> 12) & 7
        name = {56: "psq_l", 57: "psq_lu", 60: "psq_st", 61: "psq_stu"}[op]
        return name, "%s, %s, %d, %d" % (f(rd), _mem(d, ra), wbit, i)
    if op == 4:
        return _ps(w, rd, ra, rb)
    if op == 59 or op == 63:
        return _fp(w, op, rd, ra, rb)
    if op == 31:
        return _x31(w, rd, ra, rb)
    return None


def _ps(w, rd, ra, rb):
    rc = "." if w & 1 else ""
    rcb = (w >> 6) & 31
    xo5 = (w >> 1) & 31
    a5 = {10: "ps_sum0", 11: "ps_sum1", 12: "ps_muls0", 13: "ps_muls1", 14: "ps_madds0", 15: "ps_madds1",
          18: "ps_div", 20: "ps_sub", 21: "ps_add", 23: "ps_sel", 24: "ps_res", 25: "ps_mul", 26: "ps_rsqrte",
          28: "ps_msub", 29: "ps_madd", 30: "ps_nmsub", 31: "ps_nmadd"}
    if xo5 in a5:
        n = a5[xo5] + rc
        if xo5 in (12, 13, 25):
            return n, "%s, %s, %s" % (f(rd), f(ra), f(rcb))
        if xo5 in (18, 20, 21):
            return n, "%s, %s, %s" % (f(rd), f(ra), f(rb))
        if xo5 in (24, 26):
            return n, "%s, %s" % (f(rd), f(rb))
        return n, "%s, %s, %s, %s" % (f(rd), f(ra), f(rcb), f(rb))
    xo = (w >> 1) & 0x3FF
    x = {40: "ps_neg", 72: "ps_mr", 136: "ps_nabs", 264: "ps_abs"}
    if xo in x:
        return x[xo] + rc, "%s, %s" % (f(rd), f(rb))
    m = {528: "ps_merge00", 560: "ps_merge01", 592: "ps_merge10", 624: "ps_merge11"}
    if xo in m:
        return m[xo] + rc, "%s, %s, %s" % (f(rd), f(ra), f(rb))
    c = {0: "ps_cmpu0", 32: "ps_cmpo0", 64: "ps_cmpu1", 96: "ps_cmpo1"}
    if xo in c:
        return c[xo], "cr%d, %s, %s" % (rd >> 2, f(ra), f(rb))
    return None


def _fp(w, op, rd, ra, rb):
    rc = "." if w & 1 else ""
    rcb = (w >> 6) & 31
    xo5 = (w >> 1) & 31
    s = "s" if op == 59 else ""
    a = {18: "fdiv", 20: "fsub", 21: "fadd", 25: "fmul", 28: "fmsub", 29: "fmadd", 30: "fnmsub", 31: "fnmadd"}
    if xo5 in a:
        n = a[xo5] + s + rc
        if xo5 == 25:
            return n, "%s, %s, %s" % (f(rd), f(ra), f(rcb))
        if xo5 in (18, 20, 21):
            return n, "%s, %s, %s" % (f(rd), f(ra), f(rb))
        return n, "%s, %s, %s, %s" % (f(rd), f(ra), f(rcb), f(rb))
    if op == 59:
        if xo5 == 24:
            return "fres" + rc, "%s, %s" % (f(rd), f(rb))
        return None
    if xo5 == 23:
        return "fsel" + rc, "%s, %s, %s, %s" % (f(rd), f(ra), f(rcb), f(rb))
    if xo5 == 26:
        return "frsqrte" + rc, "%s, %s" % (f(rd), f(rb))
    xo = (w >> 1) & 0x3FF
    x = {12: "frsp", 14: "fctiw", 15: "fctiwz", 40: "fneg", 72: "fmr", 136: "fnabs", 264: "fabs"}
    if xo in x:
        return x[xo] + rc, "%s, %s" % (f(rd), f(rb))
    if xo in (0, 32):
        return ("fcmpu" if xo == 0 else "fcmpo"), "cr%d, %s, %s" % (rd >> 2, f(ra), f(rb))
    return None


_X3 = {266: "add", 10: "addc", 138: "adde", 40: "subf", 8: "subfc", 136: "subfe", 235: "mullw", 75: "mulhw",
       11: "mulhwu", 491: "divw", 459: "divwu"}
_X2 = {202: "addze", 200: "subfze", 104: "neg", 234: "addme", 232: "subfme"}
_LOGIC = {28: "and", 60: "andc", 444: "or", 412: "orc", 316: "xor", 476: "nand", 124: "nor", 284: "eqv",
          24: "slw", 536: "srw", 792: "sraw"}
_IDX = {23: "lwzx", 55: "lwzux", 87: "lbzx", 119: "lbzux", 279: "lhzx", 311: "lhzux", 343: "lhax", 375: "lhaux",
        151: "stwx", 183: "stwux", 215: "stbx", 247: "stbux", 407: "sthx", 439: "sthux",
        535: "lfsx", 567: "lfsux", 599: "lfdx", 631: "lfdux", 663: "stfsx", 695: "stfsux", 727: "stfdx", 759: "stfdux"}
_IDX0 = {20: "lwarx", 534: "lwbrx", 662: "stwbrx", 983: "stfiwx"}


def _x31(w, rd, ra, rb):
    rc = w & 1
    dot = "." if rc else ""
    xo = (w >> 1) & 0x3FF
    xo9 = xo & 0x1FF
    if xo9 in _X3 and not (w >> 10) & 1:
        return _X3[xo9] + dot, "%s, %s, %s" % (r(rd), r(ra), r(rb))
    if xo9 in _X2 and not (w >> 10) & 1 and rb == 0:
        return _X2[xo9] + dot, "%s, %s" % (r(rd), r(ra))
    if xo in _LOGIC:
        if xo == 444 and rd == rb and not rc:
            return "mr", "%s, %s" % (r(ra), r(rd))
        return _LOGIC[xo] + dot, "%s, %s, %s" % (r(ra), r(rd), r(rb))
    if xo in (954, 922, 26) and rb == 0:
        return {954: "extsb", 922: "extsh", 26: "cntlzw"}[xo] + dot, "%s, %s" % (r(ra), r(rd))
    if xo == 824:
        return "srawi" + dot, "%s, %s, %s" % (r(ra), r(rd), _imm(rb))
    if xo == 150 and rc:
        return "stwcx.", "%s, %s, %s" % (r(rd), _ra0(ra), r(rb))
    if rc:
        return None
    if xo in (0, 32):
        crf, l = rd >> 2, rd & 1
        if l:
            return None
        return ("cmpw" if xo == 0 else "cmplw"), ("cr%d, " % crf if crf else "") + "%s, %s" % (r(ra), r(rb))
    if xo in _IDX:
        reg = f(rd) if xo >= 535 else r(rd)
        return _IDX[xo], "%s, %s, %s" % (reg, r(ra), r(rb))
    if xo in _IDX0:
        reg = f(rd) if xo == 983 else r(rd)
        return _IDX0[xo], "%s, %s, %s" % (reg, _ra0(ra), r(rb))
    if xo == 597:
        return "lswi", "%s, %s, %s" % (r(rd), r(ra), _imm(rb or 32))
    if xo == 725:
        return "stswi", "%s, %s, %s" % (r(rd), r(ra), _imm(rb or 32))
    if xo in (1014, 54) and rd == 0:  # dcbz, dcbst: capstone prints (RA|0) as 0
        return ("dcbz" if xo == 1014 else "dcbst"), "%s, %s" % (_ra0(ra), r(rb))
    if xo == 86 and rd == 0:  # dcbf: capstone 5 prints RA as a register
        return "dcbf", "%s, %s" % (r(ra), r(rb))
    if xo == 19 and ra == 0 and rb == 0:
        return "mfcr", r(rd)
    if xo in (339, 467):
        spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
        name = _SPR.get(spr)
        if xo == 339:
            return ("mf" + name, r(rd)) if name else ("mfspr", "%s, %s" % (r(rd), _imm(spr)))
        return ("mt" + name, r(rd)) if name else ("mtspr", "%s, %s" % (_imm(spr), r(rd)))
    if xo == 4:  # tw: only the unconditional form (TO 31) is in the game
        if rd == 31:
            return "twu", "%s, %s" % (r(ra), r(rb))
        return None
    if xo == 598 and rd == 0 and ra == 0 and rb == 0:
        return "sync", ""
    return None


class Cs:
    def __init__(self, arch, mode):
        if arch != CS_ARCH_PPC or not (mode & CS_MODE_BIG_ENDIAN) or not (mode & CS_MODE_PS):
            raise ValueError("this capstone stand-in decodes PowerPC 32-bit big-endian with paired singles only")
        self.skipdata = False
        self.detail = False

    def disasm(self, code, offset, count=0):
        code = bytes(code)
        n = 0
        for k in range(0, len(code) - 3, 4):
            word = code[k:k + 4]
            addr = offset + k
            d = decode(int.from_bytes(word, "big"), addr)
            if d is None:
                if not self.skipdata:
                    return
                d = (".byte", ", ".join("0x%02x" % b for b in word))
            yield Insn(addr, d[0], d[1], word, 4)
            n += 1
            if count and n >= count:
                return
