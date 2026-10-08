#!/usr/bin/env python3
"""Survey of per-step logic in the game's actors, for true 60 fps conversion planning.

For every WWHD function named in build/names.tsv, grouped by GameCube source file, this counts in
the generated code (build/gen, generated with WWHD_RECOMP_PLAIN=1: the plain c->r[N] form these patterns
match, tools/recomp/recomp.py):
  - calls to the shared per-step primitives that true60.cpp already scales by dt
    (cLib_addCalc*/chase*, cLib_calcTimer, fopAcM_calcSpeed/posMove/posMoveF, J3DFrameCtrl::update,
    mDoExt_McaMorf::play, mDoExt_baseAnm::play, ...)
  - inline exponential smoothing x += (t - x) * r stored back to the object (smooth_sites.py)
  - inline step counters: a 16/32-bit field of the object loaded, +/-1, stored back
    (offsets listed, so they can be held like Link's timers)
  - direct writes to fopAc_ac_c position / speed fields (+0x314..0x31C, +0x33C..0x344, speedF +0x370)
    outside the primitives (inline physics that needs a site hook)
Output: build/true60_survey.tsv (one line per source file) and a summary on stdout.
usage: actor_survey.py [filter substring of the source file, e.g. d_a_]
"""
import collections, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smooth_sites  # noqa: E402

PRIMS = {
    '0200ECD4': 'addCalc', '0200ED84': 'addCalc2', '0200EDC8': 'addCalc0', '0200EE00': 'addCalcPos',
    '0200EF78': 'addCalcPosXZ', '0200F164': 'addCalcPos2', '0200F268': 'addCalcPosXZ2',
    '0200F378': 'addCalcAngleS', '0200F428': 'addCalcAngleS2', '0200F474': 'addCalcAngleL',
    '0200F4FC': 'chaseUC', '0200F564': 'chaseS', '0200F5C8': 'chaseF', '0200F62C': 'chasePos',
    '0200F764': 'chasePosXZ', '0200F8D0': 'chaseAngleS',
    '0207A9A0': 'calcTimer', '022ED850': 'calcTimer', '02055B64': 'calcTimer', '0211D2F8': 'calcTimer', '025AAE08': 'calcTimer',
    '025D67A8': 'calcSpeed', '025D6800': 'posMove', '025D6870': 'posMoveF',
    '027F2FC4': 'frameCtrl', '025E535C': 'McaMorf::play', '025E742C': 'baseAnm::play', '025E65FC': 'McaMorf2::play',
    '0282167C': 'emitterCalc',
}
FN = re.compile(r'^void f_([0-9A-F]{8})(?:_orig)?\(Cpu\* __restrict c\) \{\n(.*?)^\}', re.S | re.M)
CALL = re.compile(r'f_([0-9A-F]{8})(?:_orig)?\(c\);')
LD = re.compile(r'c->r\[(\d+)\] = \(uint32_t\)\(int32_t\)\(int16_t\)ld16\(c->r\[(\d+)\] \+ 0x([0-9A-F]+)u\)|c->r\[(\d+)\] = ld(16|32)\(c->r\[(\d+)\] \+ 0x([0-9A-F]+)u\)')
ADD = re.compile(r'c->r\[(\d+)\] = c->r\[(\d+)\] \+ 0x(00000001|FFFFFFFF)u;')
ST = re.compile(r'st(16|32)\(c->r\[(\d+)\] \+ 0x([0-9A-F]+)u, c->r\[(\d+)\]\)')
FST = re.compile(r'stf32\(ea, c->f\[\d+\]\.ps0\)')
EA = re.compile(r'uint32_t ea = c->r\[(\d+)\] \+ 0x([0-9A-F]+)u; stf32')
POSF = {0x314: 'pos', 0x318: 'pos', 0x31C: 'pos', 0x33C: 'speed', 0x340: 'speed', 0x344: 'speed', 0x370: 'speedF'}


def counters(src):
    """object fields loaded, +/-1, stored back (same base register and offset)"""
    out = set()
    lines = src.split('\n')
    for i, line in enumerate(lines):
        m = ADD.search(line)
        if not m:
            continue
        dst, srcr, imm = int(m.group(1)), int(m.group(2)), m.group(3)
        load = None
        for j in range(i - 1, max(-1, i - 6), -1):
            l = LD.search(lines[j])
            if l:
                if l.group(1) and int(l.group(1)) == srcr:
                    load = (int(l.group(2)), int(l.group(3), 16), 16)
                elif l.group(4) and int(l.group(4)) == srcr:
                    load = (int(l.group(6)), int(l.group(7), 16), int(l.group(5)))
                if load:
                    break
        if not load:
            continue
        for k in range(i + 1, min(len(lines), i + 6)):
            s = ST.search(lines[k])
            if s and int(s.group(4)) == dst and int(s.group(2)) == load[0] and int(s.group(3), 16) == load[1]:
                if load[0] != 1 and load[1] >= 0x100:  # not the stack; skip small offsets (often linked lists)
                    out.add(('+' if imm == '00000001' else '-', load[1], load[2]))
                break
    return out


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else 'd_a_'
    names = {}
    for l in open('build/names.tsv'):
        p = l.rstrip('\n').split('\t')
        if len(p) >= 3 and filt in p[2]:
            names[p[0]] = (p[1], p[2])
    per = collections.defaultdict(lambda: dict(funcs=0, prims=collections.Counter(), smooth=0, lerp=0, counters=set(), physics=collections.Counter()))
    for f in sorted(glob.glob('build/gen/code_*.c')):
        for m in FN.finditer(open(f).read()):
            a = m.group(1)
            if a not in names:
                continue
            fname, sfile = names[a]
            src = m.group(2)
            d = per[sfile]
            d['funcs'] += 1
            for c in CALL.findall(src):
                if c in PRIMS:
                    d['prims'][PRIMS[c]] += 1
            d['counters'] |= counters(src)
            for site in smooth_sites.scan_source(src):
                d['smooth' if site[4] else 'lerp'] += 1
            for e in EA.finditer(src):
                off = int(e.group(2), 16)
                if off in POSF and int(e.group(1)) != 1:
                    d['physics'][POSF[off]] += 1
    rows = list(per.items())
    with open('build/true60_survey.tsv', 'w') as o:
        o.write('file\tfunctions\tprimitive_calls\tprimitives\tinline_smoothing\tstep_counters\tcounter_fields\tpos_speed_stores\n')
        for sfile, d in sorted(rows, key=lambda r: -sum(r[1]['prims'].values())):
            cf = ','.join('%s%s0x%X' % ('s16' if w == 16 else 's32', sign, off) for sign, off, w in sorted(d['counters'], key=lambda x: x[1]))
            o.write('%s\t%d\t%d\t%s\t%d\t%d\t%s\t%d\n' % (sfile, d['funcs'], sum(d['prims'].values()),
                    ' '.join('%s:%d' % kv for kv in d['prims'].most_common()), d['smooth'], len(d['counters']), cf, sum(d['physics'].values())))
    tot = collections.Counter()
    for sfile, d in rows:
        tot['files'] += 1
        tot['functions'] += d['funcs']
        tot['primitive calls'] += sum(d['prims'].values())
        tot['counter fields'] += len(d['counters'])
        tot['inline smoothing'] += d['smooth']
        tot['pos/speed stores'] += sum(d['physics'].values())
    print(dict(tot))


if __name__ == '__main__':
    main()
