"""Decomp build: port instruction hooks that the decompiled function carries in its own source (the decompilation's
wwhd_src/include/port_hooks.h, active only in the game build: GABI_GAME_BUILD, gen.py --game-build).

A recompiled function with an instruction hook inside (tools/recomp/hooks*.txt `@ADDR`) stays recompiled in the decomp
build (recomp.py: "contains instruction hook"). For the sites below the decompiled function asks the port at the same
point itself, so gen.py --game-build lists the site as `# moved-site ADDR` in decomp_hooks.txt when the function is
decompiled and its source carries the marker, and recomp.py leaves that site out when it decides what to keep. A decompilation
without the marker (an older one), or a build without GABI_GAME_BUILD (where the marker compiles to the original code),
keeps the function recompiled behind the site, as before.

Each entry: site (the instruction hook), func (the function that contains it), path (its source, relative to the
decompilation) and marker (text the source must contain). A point that names a site (PORT_QUICK_USE(0x023EAE30, ...))
runs that site's own handler in the port (runtime/src/port_points.h), with the registers the handler reads set from
the point's arguments.

EMPTY: retired sites whose handler does nothing (runtime/src/game_hooks.cpp, the retired True 60 SITE()s, kept so the
normal build's code stays as it was): the decompiled function needs no point for them, so they are moved whenever
their function is decompiled (game build). tools/decomp_build/test_gen.py checks that each handler is still empty.
"""

P01 = "wwhd_src/d/actor/d_a_player_main_01.cpp"
P02 = "wwhd_src/d/actor/d_a_player_main_02.cpp"
P03 = "wwhd_src/d/actor/d_a_player_main_03.cpp"
P06 = "wwhd_src/d/actor/d_a_player_main_06.cpp"
P07 = "wwhd_src/d/actor/d_a_player_main_07.cpp"


def _p(site, func, path, macro):
    """a point that names its site: marker `MACRO(0xSITE`"""
    return dict(site=site, func=func, path=path, marker="%s(0x%08X" % (macro, site))


SITES = [
    # Classic Wind Waker (runtime/src/mods/speedrun.cpp, issue #405): procTactWait's falling check
    dict(site=0x0243A294, func=0x0243A094, path=P07, marker="|| PORT_CLASSIC_TACT()"),
    # ... and endDemoMode's wait reset after a special demo
    dict(site=0x023F22B4, func=0x023F2048, path=P03, marker="&& !PORT_CLASSIC_TACT()"),
    # Quick use (runtime/src/mods/quick_use.cpp, hooks_mods.txt): item dispatch, equip, ready checks and launch traces
    _p(0x023DF7C0, 0x023DF600, P01, "PORT_QUICK_USE_AT"),  # makeItemType: inventory bomb published
    _p(0x023E88B0, 0x023E88B0, P02, "PORT_QUICK_USE_AT"),  # procBowMove_init entry
    _p(0x023EA474, 0x023EA1AC, P02, "PORT_QUICK_USE"),     # checkNextActionGrab: bomb trigger
    _p(0x023EAE30, 0x023EAD5C, P02, "PORT_QUICK_USE"),     # checkNextActionHookshotReady
    _p(0x023EAE78, 0x023EAD5C, P02, "PORT_QUICK_USE"),
    _p(0x023EAE84, 0x023EAD5C, P02, "PORT_QUICK_USE"),
    _p(0x023EB2B0, 0x023EB208, P02, "PORT_QUICK_USE"),     # checkNextActionBowReady
    _p(0x023EB2C4, 0x023EB208, P02, "PORT_QUICK_USE_AT"),
    _p(0x023EB434, 0x023EB208, P02, "PORT_QUICK_USE"),
    _p(0x023EB548, 0x023EB510, P02, "PORT_QUICK_USE"),     # checkNextActionBoomerangReady
    _p(0x023EB614, 0x023EB510, P02, "PORT_QUICK_USE"),
    _p(0x023EC12C, 0x023EC084, P02, "PORT_QUICK_USE"),     # checkNextActionRopeReady
    _p(0x023EC178, 0x023EC084, P02, "PORT_QUICK_USE"),
    _p(0x023EC184, 0x023EC084, P02, "PORT_QUICK_USE"),
    _p(0x023EFB44, 0x023EF7CC, P03, "PORT_QUICK_USE_AT"),  # checkNewItemChange: accepted slot change
    _p(0x023F05EC, 0x023EFF20, P03, "PORT_QUICK_USE"),     # checkNextActionFromButton: ready-item trigger
    _p(0x023F0624, 0x023EFF20, P03, "PORT_QUICK_USE_AT"),  # ... hookshot / rope / boomerang dispatch
    _p(0x023F0670, 0x023EFF20, P03, "PORT_QUICK_USE_AT"),
    _p(0x023F06AC, 0x023EFF20, P03, "PORT_QUICK_USE_AT"),
    _p(0x023F1728, 0x023F14E0, P03, "PORT_QUICK_USE_AT"),  # checkMode: boomerang / hookshot / rope dispatch
    _p(0x023F1764, 0x023F14E0, P03, "PORT_QUICK_USE_AT"),
    _p(0x023F1784, 0x023F14E0, P03, "PORT_QUICK_USE_AT"),
    _p(0x023FB49C, 0x023FB230, P03, "PORT_QUICK_USE_AT"),  # checkItemAction: boomerang launch trace
    _p(0x0242E14C, 0x0242E010, P07, "PORT_QUICK_USE_AT"),  # procGrabThrow: bomb launch traces
    _p(0x0242E198, 0x0242E010, P07, "PORT_QUICK_USE_AT"),
    _p(0x0243C6F4, 0x0243C61C, P07, "PORT_QUICK_USE_AT"),  # procHammerSideSwing: launch trace
    _p(0x0243CB70, 0x0243CA98, P07, "PORT_QUICK_USE_AT"),  # procHammerFrontSwing: launch trace
    # Camera-directed items (runtime/src/mods/camera_directed_items.cpp): initial facing of an accepted item use
    _p(0x023E7A70, 0x023E79DC, P02, "PORT_ITEM_FACING"),   # procFanSwing_init
    _p(0x023E7CF4, 0x023E7C98, P02, "PORT_ITEM_FACING"),   # procBowSubject_init
    _p(0x023E88F4, 0x023E88B0, P02, "PORT_ITEM_FACING"),   # procBowMove_init
    _p(0x023E8A58, 0x023E89FC, P02, "PORT_ITEM_FACING"),   # procHookshotSubject_init
    _p(0x023E8B20, 0x023E8ADC, P02, "PORT_ITEM_FACING"),   # procHookshotMove_init
    _p(0x023E8D34, 0x023E8CA4, P02, "PORT_ITEM_FACING"),   # procBoomerangSubject_init
    _p(0x023E8E40, 0x023E8DC8, P02, "PORT_ITEM_FACING"),   # procBoomerangMove_init
    _p(0x023E902C, 0x023E8FBC, P02, "PORT_ITEM_FACING"),   # procRopeSubject_init
    _p(0x023E9130, 0x023E90EC, P02, "PORT_ITEM_FACING"),   # procRopeMove_init
    _p(0x023E9E98, 0x023E9E48, P02, "PORT_ITEM_FACING"),   # procGrabThrow_init
    _p(0x023EDE00, 0x023EDD9C, P02, "PORT_ITEM_FACING"),   # procBottleSwing_init
    _p(0x023EDFC4, 0x023EDF70, P03, "PORT_ITEM_FACING"),   # procHammerSideSwing_init
    _p(0x0243C830, 0x0243C7D8, P07, "PORT_ITEM_FACING"),   # procHammerFrontSwing_init
    # Link's movement mods (runtime/src/mods/link_move_hooks.cpp): posMoveFromFootPos's current.pos += speed
    dict(site=0x023FD39C, func=0x023FCB9C, path=P03, marker="PORT_LINK_MOVE(&speed)"),
    # Climbing (runtime/src/mods/climb.cpp): the wall-steepness compares and the ledge search at the top
    _p(0x023E39EC, 0x023E3850, P02, "PORT_CLIMB_STEEP"),   # setFrontWallType
    _p(0x02429570, 0x02429408, P06, "PORT_CLIMB_STEEP"),   # setMoveBGCorrectClimb
    dict(site=0x02429C04, func=0x02429950, path=P06, marker="PORT_CLIMB_LEDGE(this,"),  # checkBgClimbMove
    # Steering (runtime/src/mods/steer.cpp): setStickData's walking direction; one statement in the source, which the
    # compiled code has twice (both sites)
    _p(0x023F35BC, 0x023F32E4, P03, "PORT_STEER_WALK"),
    dict(site=0x023F3610, func=0x023F32E4, path=P03, marker="PORT_STEER_WALK(0x023F35BC"),
]

EMPTY = [
    # posMoveFromFootPos: foot movement, gravity (retired True 60)
    dict(site=0x023FCEB0, func=0x023FCB9C), dict(site=0x023FD338, func=0x023FCB9C),
    dict(site=0x023FD35C, func=0x023FCB9C),
    # (daPy_lk_c::execute's three empty sites stay, fast mode: its decompiled body, never run in the game before, crashes
    # after the file select with the TV-frame clock (a read at guest address 8); to be looked at in the decompilation first)
]


def moved(listed, texts):
    """the SITES whose function is in `listed` (decompiled) and whose source text (texts: path -> text as compiled)
    carries the marker, and the EMPTY sites whose function is in `listed` (path: the handler's file)"""
    return ([s for s in SITES if s["func"] in listed and s["marker"] in texts.get(s["path"], "")] +
            [dict(s, path="runtime/src/game_hooks.cpp: empty") for s in EMPTY if s["func"] in listed])
