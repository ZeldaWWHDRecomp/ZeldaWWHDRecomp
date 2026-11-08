"""Decomp build: port instruction hooks that the decompiled function carries in its own source (the decompilation's
wwhd_src/include/port_hooks.h, active only in the game build: GABI_GAME_BUILD, gen.py --game-build).

A recompiled function with an instruction hook inside (tools/recomp/hooks*.txt `@ADDR`) stays recompiled in the decomp
build (recomp.py: "contains instruction hook"). For the sites below the decompiled function asks the port at the same
point itself, so gen.py --game-build lists the site as `# moved-site ADDR` in decomp_hooks.txt when the function is
decompiled and its source carries the marker, and recomp.py leaves that site out when it decides what to keep. A decompilation
without the marker (an older one), or a build without GABI_GAME_BUILD (where the marker compiles to the original code),
keeps the function recompiled behind the site, as before.

Each entry: site (the instruction hook), func (the function that contains it), path (its source, relative to the
decompilation) and marker (text the source must contain).
"""

SITES = [
    # Classic Wind Waker (runtime/src/mods/speedrun.cpp, issue #405): procTactWait's falling check
    dict(site=0x0243A294, func=0x0243A094, path="wwhd_src/d/actor/d_a_player_main_07.cpp",
         marker="|| PORT_CLASSIC_TACT()"),
    # ... and endDemoMode's wait reset after a special demo
    dict(site=0x023F22B4, func=0x023F2048, path="wwhd_src/d/actor/d_a_player_main_03.cpp",
         marker="&& !PORT_CLASSIC_TACT()"),
]


def moved(listed, texts):
    """the SITES whose function is in `listed` (decompiled) and whose source text (texts: path -> text as compiled)
    carries the marker"""
    return [s for s in SITES if s["func"] in listed and s["marker"] in texts.get(s["path"], "")]
