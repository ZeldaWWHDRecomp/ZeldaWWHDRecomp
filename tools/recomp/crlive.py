"""Condition-register liveness for one recompiled function chunk.

Every PowerPC compare (and record form) writes four CR bits (lt, gt, eq, so), and ppc2c turns each
into four byte stores into the Cpu struct, which the C compiler cannot drop: the struct is visible
to every callee. Usually only one bit is ever read (by the next conditional branch). This pass finds
the bits that can still be read after each writer and rewrites cr_set_* / cr0_rc into the masked
forms (ppc.h: cr_set_s_m ...), which store only those.

Assumptions, from the PowerPC EABI the game's compiler follows:
- At a return (blr), only the callee-saved fields cr2-cr4 matter to the caller.
- A call may read cr1.eq (bit 6: "FP arguments in registers" for varargs functions); every other
  bit passes through the call unchanged (so a bit the caller reads after the call stays live).
Everything the analysis cannot see counts as reading all 32 bits: branches and tail calls out of
the chunk (a chunk can be part of a larger function), indirect jumps, traps, mfcr, unimplemented
instructions and instruction-level hooks.
"""
import re

ALL = 0xFFFFFFFF
NONVOLATILE = 0x000FFF00  # cr2, cr3, cr4 (bits 8-19)
CR1_EQ = 1 << 6

_WRITER = re.compile(r"\b(cr_set_s|cr_set_u|cr_set_f)\(c, (\d+), (.*?)\);|\bcr0_rc\(c, (.*?)\);")
_CR_READ = re.compile(r"(?<!&)c->cr\[(\d+)\](?!\s*=[^=])")


def field_bits(f):
    return 0xF << (4 * f)


def effects(addr, w, s):
    """CR bits read (before the instruction) and written by it, from the word and its C."""
    op = w >> 26
    reads = kills = 0
    if "ppc_unimplemented" in s or "ppc_trap" in s:
        reads = ALL
    if op == 16:  # bc
        bo, bi = (w >> 21) & 31, (w >> 16) & 31
        if not bo & 0x10:
            reads |= 1 << bi
    elif op == 19:
        xo = (w >> 1) & 0x3FF
        d, a, b = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        if xo in (16, 528):  # bclr / bcctr
            if not d & 0x10:
                reads |= 1 << a
        elif xo in (257, 449, 193, 225, 33, 289, 129, 417):  # cr logical
            reads |= (1 << a) | (1 << b)
            kills |= 1 << d
        elif xo == 0:  # mcrf
            reads |= field_bits(a >> 2)
            kills |= field_bits(d >> 2)
    elif op == 31:
        xo = (w >> 1) & 0x3FF
        d = (w >> 21) & 31
        if xo == 19:  # mfcr
            reads = ALL
        elif xo == 144:  # mtcrf
            fxm = (w >> 12) & 0xFF
            for i in range(8):
                if fxm & (0x80 >> i):
                    kills |= field_bits(i)
        elif xo == 512:  # mcrxr
            kills |= field_bits(d >> 2)
        elif xo == 150:  # stwcx.
            kills |= field_bits(0)
        elif xo == 4:  # tw
            reads = ALL
    elif op == 63 and ((w >> 1) & 0x3FF) == 64:  # mcrfs
        kills |= field_bits(((w >> 21) & 31) >> 2)
    elif op in (3, 17):  # twi, sc
        reads = ALL
    if "c->cr[4] = c->cr[5] = c->cr[6] = c->cr[7] = 0;" in s:  # FP record form
        kills |= field_bits(1)
    m = _WRITER.search(s)
    field = None
    if m:
        field = int(m.group(2)) if m.group(1) else 0
        kills |= field_bits(field)
    return reads, kills, field


def analyze(insns, cur_start, cur_end, jump_tables, branch_target, sites):
    """insns: list of (addr, word, c_source). Returns the bits live after each instruction."""
    n = len(insns)
    index = {a: i for i, (a, _, _) in enumerate(insns)}
    reads, kills, succ, exits, is_call = [0] * n, [0] * n, [None] * n, [0] * n, [False] * n
    fields = [None] * n
    for i, (a, w, s) in enumerate(insns):
        r, k, f = effects(a, w, s)
        if a in sites:
            r = ALL
        reads[i], kills[i], fields[i] = r, k, f
        op = w >> 26
        nxt = [i + 1] if i + 1 < n else []
        ex = 0 if i + 1 < n else ALL  # falling off the chunk continues in the next one
        targets = []
        if op == 18 or op == 16:  # b, bc
            conditional = op == 16 and ((w >> 21) & 0x14) != 0x14
            if w & 1:
                is_call[i] = True
            else:
                t = branch_target(a, w)
                inside = t is not None and cur_start <= t < cur_end and t in index
                if inside:
                    targets.append(index[t])
                taken = 0 if inside else ALL  # out of the chunk: a tail call or another chunk
                if conditional:
                    ex |= taken
                else:
                    nxt, ex = [], taken
        elif op == 19 and ((w >> 1) & 0x3FF) in (16, 528):  # bclr, bcctr
            xo = (w >> 1) & 0x3FF
            conditional = ((w >> 21) & 0x14) != 0x14
            if w & 1:
                is_call[i] = True
            else:
                if xo == 16:
                    taken = NONVOLATILE  # return
                else:
                    taken = ALL  # indirect jump: jump-table cases inside the chunk, or dispatch
                    jt = jump_tables.get(a)
                    if jt:
                        base, count = jt
                        for j in range(count):
                            slot = base + 4 * j
                            if cur_start <= slot < cur_end and slot in index:
                                targets.append(index[slot])
                if conditional:
                    ex |= taken
                else:
                    nxt, ex = [], taken
        if is_call[i]:
            reads[i] |= CR1_EQ
        succ[i] = nxt + targets
        exits[i] = ex
    live_out = [0] * n
    live_in = [0] * n
    changed = True
    while changed:
        changed = False
        for i in range(n - 1, -1, -1):
            out = exits[i]
            for j in succ[i]:
                out |= live_in[j]
            lin = reads[i] | (out & ~kills[i])
            if out != live_out[i] or lin != live_in[i]:
                live_out[i], live_in[i] = out, lin
                changed = True
    return live_out, fields


def rewrite(s, field, live_after, check=False, addr=0):
    """The instruction's C with its CR writer reduced to the live bits."""
    if field is not None:
        m = (live_after >> (4 * field)) & 0xF

        def repl(mo):
            if m == 0xF:
                return mo.group(0)
            if mo.group(1):
                kind, f, args = mo.group(1), mo.group(2), mo.group(3)
                if m == 0 and kind != "cr_set_f" and not check:
                    return ";"
                return "%s_m(c, %s, %s, %d);" % (kind, f, args, m)
            if m == 0 and not check:
                return ";"
            return "cr0_rc_m(c, %s, %d);" % (mo.group(4), m)
        s = _WRITER.sub(repl, s, count=1)
    if check:
        s = _CR_READ.sub(lambda mo: "ppc_cr_read(c, %s, 0x%08Xu)" % (mo.group(1), addr), s)
    return s
