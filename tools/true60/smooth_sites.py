#!/usr/bin/env python3
"""Find per-step exponential smoothing in recompiled functions: x += (target - x) * r.

True 60 fps runs converted actors with a step length dt < 1; such a smoothing step then needs the
ratio 1 - (1 - r)^dt. This scans the generated C (build/gen, generated with WWHD_RECOMP_PLAIN=1 for
the plain c->f[N] form, tools/recomp/recomp.py) of the given functions for the
compiled forms of the expression and prints, per site, the instruction address and the register
holding r, so tools/true60/gen_sites.py can emit instruction-level hooks for them.

Recognised (single precision, f = FPR):
  fsubs  fD = fT - fX ; fmadds fY = fD * fR + fX        (or fR * fD + fX)
  fsubs  fD = fT - fX ; fmuls fM = fD * fR ; fadds fY = fX + fM  (or fM + fX)
The subtraction must be within WINDOW instructions before, with fX not redefined in between.

usage: smooth_sites.py ADDR [ADDR ...]   (function entry addresses, hex)
"""
import glob, re, sys

WINDOW = 24
FN_RE = r'^void f_%s(?:_orig)?\(Cpu\* __restrict c\) \{\n(.*?)^\}'
OP = re.compile(r'\{ double v = to_single\((.*)\); c->f\[(\d+)\]\.ps0 = v; c->f\[\d+\]\.ps1 = v; \} /\* ([0-9A-F]{8}):')
SUB = re.compile(r'^c->f\[(\d+)\]\.ps0 - c->f\[(\d+)\]\.ps0$')
MUL = re.compile(r'^c->f\[(\d+)\]\.ps0 \* round25\(c->f\[(\d+)\]\.ps0\)$')
MADD = re.compile(r'^c->f\[(\d+)\]\.ps0 \* round25\(c->f\[(\d+)\]\.ps0\) \+ c->f\[(\d+)\]\.ps0$')
ADD = re.compile(r'^c->f\[(\d+)\]\.ps0 \+ c->f\[(\d+)\]\.ps0$')
DEF = re.compile(r'c->f\[(\d+)\]\.ps0 = ')
ADDR = re.compile(r'/\* ([0-9A-F]{8}): ')
LOAD = re.compile(r'uint32_t ea = c->r\[(\d+)\] \+ 0x([0-9A-F]+)u; \{ double v = ldf32\(ea\); c->f\[(\d+)\]\.ps0 = v;')
STORE = re.compile(r'uint32_t ea = c->r\[(\d+)\] \+ 0x([0-9A-F]+)u; stf32\(ea, c->f\[(\d+)\]\.ps0\);')
GPRDEF = re.compile(r'c->r\[(\d+)\] = ')


def body(addr):
    for f in sorted(glob.glob('build/gen/code_*.c')):
        t = open(f).read()
        m = re.search(FN_RE % addr, t, re.S | re.M)
        if m:
            return m.group(1)
    return None


def scan(addr):
    src = body(addr)
    if src is None:
        print('# %s: not found' % addr, file=sys.stderr)
        return []
    return scan_source(src)


def scan_source(src):
    ins = []  # (address, kind, args, dest, defs)
    meminfo = []  # per instruction: (load/store, GPRs defined)
    for line in src.split('\n'):
        a = ADDR.search(line)
        if not a:
            continue
        m = OP.search(line)
        defs = set(int(d) for d in DEF.findall(line))
        ld = LOAD.search(line)
        st = STORE.search(line)
        mem = ('ld', int(ld.group(1)), int(ld.group(2), 16), int(ld.group(3))) if ld else \
              ('st', int(st.group(1)), int(st.group(2), 16), int(st.group(3))) if st else None
        gdefs = set(int(g) for g in GPRDEF.findall(line))
        meminfo.append((mem, gdefs))
        if m:
            expr, d = m.group(1), int(m.group(2))
            for kind, rx in (('sub', SUB), ('madd', MADD), ('mul', MUL), ('add', ADD)):
                mm = rx.match(expr)
                if mm:
                    ins.append((a.group(1), kind, tuple(int(x) for x in mm.groups()), d, defs))
                    break
            else:
                ins.append((a.group(1), 'other', (), d, defs))
        else:
            ins.append((a.group(1), 'other', (), None, defs))
    sites = []

    def find_sub(i, reg, x):
        """the instruction before i that last defined reg: a subtraction t - x, with x unchanged since"""
        for j in range(i - 1, max(-1, i - WINDOW), -1):
            if reg in ins[j][4]:
                if ins[j][1] == 'sub' and ins[j][2][1] == x:
                    # x must not be redefined between j and i
                    if any(x in ins[k][4] for k in range(j + 1, i)):
                        return None
                    return j
                return None
        return None

    def state(i, x, d):
        """x was loaded from base+off and the result d is stored back there: a per-step state update"""
        src = None
        for j in range(i - 1, max(-1, i - 3 * WINDOW), -1):
            if x in ins[j][4]:
                mem = meminfo[j][0]
                if mem and mem[0] == 'ld' and mem[3] == x:
                    src = (mem[1], mem[2], j)
                break
        if not src:
            return None
        for k in range(i + 1, min(len(ins), i + 3 * WINDOW)):
            mem = meminfo[k][0]
            if mem and mem[0] == 'st' and mem[3] == d and (mem[1], mem[2]) == src[:2]:
                if any(src[0] in meminfo[m][1] for m in range(src[2] + 1, k)):
                    return None
                return '+0x%X' % src[1]
            if d in ins[k][4]:
                break
        return None

    for i, (a, kind, args, d, defs) in enumerate(ins):
        if kind == 'madd':
            p, q, x = args
            if find_sub(i, p, x) is not None:
                sites.append((a, q, d, 'madd', state(i, x, d)))
            elif find_sub(i, q, x) is not None:
                sites.append((a, p, d, 'madd', state(i, x, d)))
        elif kind == 'mul':
            p, q = args
            # a following add of the product to x, where the subtraction was t - x
            for k in range(i + 1, min(len(ins), i + WINDOW)):
                if ins[k][1] == 'add' and d in ins[k][2]:
                    x = ins[k][2][0] if ins[k][2][1] == d else ins[k][2][1]
                    if find_sub(i, p, x) is not None:
                        sites.append((a, q, d, 'mul+add', state(k, x, ins[k][3])))
                    elif find_sub(i, q, x) is not None:
                        sites.append((a, p, d, 'mul+add', state(k, x, ins[k][3])))
                    break
                if d in ins[k][4]:
                    break
    return sites


if __name__ == '__main__':
    for fn in sys.argv[1:]:
        for a, r, d, kind, st in scan(fn.upper()):
            print('%s %s f%d f%d %s %s' % (fn.upper(), a, r, d, kind, ('state ' + st) if st else 'lerp?'))
