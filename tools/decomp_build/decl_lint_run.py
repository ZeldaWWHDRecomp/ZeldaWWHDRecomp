#!/usr/bin/env python3
"""Runs the decompilation's tools/verify/decl_lint.py (decl_table.py's port-mode run) with autoglue's zero-filled sections
taken from the configured RPX.

usage: WWHD_DECL_DECOMP=<decomp> WWHD_DECL_RPX=<cking.rpx> decl_lint_run.py <decl_lint.py arguments>

autoglue's bss_ranges() reads the RPX at the decompilation's own game/code/cking.rpx, a link the decompilation as it
ships does not have; gen.py gives it the NOBITS sections of its --game RPX, and this does the same here. The setup runs
at import, outside the __main__ guard, so decl_lint's spawned compile workers (which re-import this script as
__mp_main__) get the sections too.

WWHD_DECL_CACHE (set by decl_table.py: the root of decl_cache.py's store, or empty) hooks the per-unit cache into
decl_lint in the main process only: Cfgs from the start (decl_lint's live-in table builds them too), the per-source
entries once decl_lint.main has set its paths (at run_compile_audit's entry), saved when the run ends."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

sys.path.insert(0, os.path.join(os.environ["WWHD_DECL_DECOMP"], "tools", "verify"))
if os.path.join(ROOT, "tools") not in sys.path:
    sys.path.append(os.path.join(ROOT, "tools"))
import autoglue  # noqa: E402

if hasattr(autoglue, "bss_ranges"):  # (an older decompilation has no zero-filled-section check)
    from rpx import Rpx, SHT_NOBITS  # noqa: E402
    autoglue._bss = [(s.addr, s.file_size) for s in Rpx(os.environ["WWHD_DECL_RPX"]).sections
                     if s.type == SHT_NOBITS and s.addr]


def cached_main(decl_lint):
    import decl_cache
    store = decl_cache.Store(os.environ["WWHD_DECL_CACHE"])
    gen = sys.argv[sys.argv.index("--gen") + 1]
    hooks = decl_cache.Hooks(store, os.environ["WWHD_DECL_DECOMP"], gen, decl_cache.tool_hash(os.environ["WWHD_DECL_DECOMP"]),
                             log=lambda m: print(m, file=sys.stderr, flush=True))
    hooks.install_cfg()
    orig = decl_lint.run_compile_audit

    def run_compile_audit(*a, **k):
        hooks.install_sources(decl_lint)
        return orig(*a, **k)
    decl_lint.run_compile_audit = run_compile_audit
    rc = decl_lint.main()
    hooks.save()
    return rc


if __name__ == "__main__":
    import decl_lint
    sys.argv[0] = decl_lint.__file__
    if os.environ.get("WWHD_DECL_CACHE") and "--gen" in sys.argv and hasattr(decl_lint, "run_compile_audit"):
        sys.path.insert(0, HERE)
        sys.exit(cached_main(decl_lint))
    sys.exit(decl_lint.main())
