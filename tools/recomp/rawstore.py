"""lfs -> stfs copies store the loaded word (raw store).

On the PowerPC an lfs followed by an stfs of the same, unmodified register writes back exactly the
word that was loaded: lfs converts single to double without rounding or quieting a NaN, and stfs of
such a value selects the same bits again (PowerPC Book I v2.01 4.6.2 / 4.6.3; ppc.h ppc_lfs_value /
ppc_stfs_bits; runtime/tools/ppc_f32_exact_test.c checks the round trip for all 2^32 words). The
exact conversions cost a conversion and a check each way, where the old host conversions let the C
compiler fold such a copy into a plain 32-bit copy. This pass gives that copy back, exactly:

  lfs fN  ->  { uint32_t w = ld32(ea); double v = ppc_lfs_value(w); ...; fwN = w; }
  stfs fN ->  st32(ea, fwN);   (only while fN still holds that lfs value)

The same holds for a register whose ps0 was last set by a single-precision result (fadds & co., frsp,
the paired-single arithmetic: ppc2c's to_single(...)): that value is exactly a single and never a
signalling NaN, so the host conversion gives the stfs bits and the exactness check is not needed:

  stfs fN ->  st32(ea, f32_as_u32((float)fN));

Within one function and one basic block only: a label (branch target), an instruction hook, a call,
a dispatch or any other write of fN (ps0 assignments, psq_load into fN, its address taken) ends the
copy. Paired-single loads and stores (psq_l / psq_st) never take this path. The statements are those
of ppc2c, before leaflocal.py turns c->f[N] into locals (fwN stays a plain local).
"""
import re

_LFS = re.compile(r"\{ double v = ldf32\(ea\); c->f\[(\d+)\]\.ps0 = v; c->f\[\1\]\.ps1 = v; \}")
# single-precision results: ppc2c's "{ double v = to_single(...); c->f[N].ps0 = v; ..." and the paired form
_SINGLE = re.compile(r"^\{ double (v0?) = to_single\(")
_STFS = re.compile(r"\bstf32\(ea, c->f\[(\d+)\]\.ps0\);")
# any write of an FPR's ps0 (or the whole register), its address, or a psq load into it
_WRITE = re.compile(r"c->f\[(\d+)\](?:\.ps0)?\s*(?:[-+*/|&^]|<<|>>)?=(?!=)")
_ADDR = re.compile(r"&c->f\[(\d+)\]")
_PSQ_LOAD = re.compile(r"\bpsq_load(?:_l)?\(c, (\d+),")
# anything that may run other code that sees or changes the register file
_CALL = re.compile(r"\b(?:f_[0-9A-F]{8}(?:_orig)?|imp_\w+|ppc_dispatch|ppc_host_call|ppc_mod_run|hook_\w+|site_\w+)\(|"
                   r"MUSTTAIL|ppc_unimplemented|ppc_trap|PPC_MOD_HOOK")


def transform(body, labels, sites):
    """body: [(addr, word, stmt)] of one function in address order. Returns (body, raw FPRs used)."""
    holder = {}      # FPR -> index of the lfs statement whose word it still holds
    single = set()   # FPRs whose ps0 holds a single-precision result
    singles = {}     # stfs index -> FPR stored from a single-precision result
    uses = {}        # lfs index -> FPR, for lfs statements that feed at least one raw store
    stores = {}      # stfs index -> FPR
    for i, (a, _, s) in enumerate(body):
        if a in labels or a in sites:
            holder.clear()
            single.clear()
        m = _STFS.search(s)
        if m and s.count("c->f[") == 1:
            n = int(m.group(1))
            if n in holder:
                stores[i] = n
                uses[holder[n]] = n
            elif n in single:
                singles[i] = n
        if _CALL.search(s):
            holder.clear()
            single.clear()
            continue
        written = {int(x) for x in _WRITE.findall(s)} | {int(x) for x in _ADDR.findall(s)} | \
                  {int(x) for x in _PSQ_LOAD.findall(s)}
        for n in written:
            holder.pop(n, None)
            single.discard(n)
        ms = _SINGLE.match(s)
        if ms:
            mt = re.search(r"c->f\[(\d+)\]\.ps0 = %s;" % ms.group(1), s)
            if mt:
                single.add(int(mt.group(1)))
        m = _LFS.search(s)
        if m and s.count("c->f[") == 2:
            holder[int(m.group(1))] = i
    if not stores and not singles:
        return body, set()
    out = []
    for i, (a, w, s) in enumerate(body):
        if i in uses:
            n = uses[i]
            s = _LFS.sub("{ uint32_t w = ld32(ea); double v = ppc_lfs_value(w); "
                         "c->f[%d].ps0 = v; c->f[%d].ps1 = v; fw%d = w; }" % (n, n, n), s)
        elif i in stores:
            s = _STFS.sub("st32(ea, fw%d);" % stores[i], s)
        elif i in singles:
            s = _STFS.sub("st32(ea, f32_as_u32((float)c->f[%d].ps0));" % singles[i], s)
        out.append((a, w, s))
    return out, set(stores.values())
