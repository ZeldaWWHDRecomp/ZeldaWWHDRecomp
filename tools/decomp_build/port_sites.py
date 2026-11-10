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
CU = "wwhd_src/d/d_camera_util.cpp"
CS = "wwhd_src/d/d_camera_subject.cpp"
CP = "wwhd_src/d/d_camera_proc.cpp"
CE2 = "wwhd_src/d/d_camera_engine2.cpp"
SN = "wwhd_src/d/d_snap.cpp"


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
    # Camera (runtime/src/mods/camera_points.cpp): the right stick's y (Invert camera up/down), the stored aspect, the
    # targeting letterbox, the projection calls (aspect, field of view, stereo culling)
    _p(0x02171B1C, 0x0216F7FC, "wwhd_src/d/actor/d_a_himo2_move.cpp", "PORT_CAMERA_F"),  # new_himo2_move
    _p(0x024F9DA4, 0x024F9BF4, CU, "PORT_CAMERA_F"),     # dCamera_c::updatePad
    _p(0x0250074C, 0x025006D8, CU, "PORT_CAMERA_F"),     # dCamera_c::initPad
    _p(0x02506C68, 0x02506964, CS, "PORT_CAMERA_F"),     # dCamera_c::CalcSubjectAngle: one statement, compiled twice
    dict(site=0x02506C80, func=0x02506964, path=CS, marker="PORT_CAMERA_F(0x02506C68"),
    _p(0x024F811C, 0x024F8068, CP, "PORT_PROJECTION"),   # view_setup: mDoLib_clipper::setup (both branches)
    _p(0x024F8168, 0x024F8068, CP, "PORT_PROJECTION"),
    _p(0x024FFA98, 0x024FFA3C, CP, "PORT_CAMERA_F"),     # camera_execute: the aspect stored
    _p(0x025020E0, 0x02501F98, CP, "PORT_CAMERA_F"),     # init_phase2: the same
    _p(0x024FF8C0, 0x024FF8A0, CP, "PORT_CAMERA_AT"),    # CalcTrimSize: trace
    _p(0x024FF980, 0x024FF8A0, CP, "PORT_CAMERA_F"),     # CalcTrimSize: the Vista bars' height
    _p(0x024FFD60, 0x024FFC40, CP, "PORT_PROJECTION_ARG"),  # camera_draw: C_MTXPerspective
    _p(0x0250007C, 0x024FFC40, CP, "PORT_PROJECTION_ARG"),  # camera_draw: sead PerspectiveProjection::set
    _p(0x024FF4C0, 0x024FF164, "wwhd_src/d/d_camera_run.cpp", "PORT_CAMERA_W"),  # eventCamera: one statement, twice
    dict(site=0x024FF4E4, func=0x024FF164, path="wwhd_src/d/d_camera_run.cpp", marker="PORT_CAMERA_W(0x024FF4C0"),
    _p(0x024FC080, 0x024FBF9C, "wwhd_src/d/d_camera_bg.cpp", "PORT_CAMERA_W"),  # defaultTriming: the table's size
    # dCamera_c::manualCamera (fast mode): the direct / mouse / swipe camera, Invert camera up/down (mods/camera.cpp)
    _p(0x02510448, 0x0250FDC8, CE2, "PORT_CAMERA_MANUAL_AT"),  # after each turning-speed chase (both branches)
    _p(0x025104A0, 0x0250FDC8, CE2, "PORT_CAMERA_MANUAL_AT"),
    _p(0x0251053C, 0x0250FDC8, CE2, "PORT_CAMERA_RATE"),       # the vertical stick rate
    _p(0x02510DEC, 0x0250FDC8, CE2, "PORT_CAMERA_MANUAL"),     # the latitude step
    _p(0x02510EA0, 0x0250FDC8, CE2, "PORT_CAMERA_MANUAL"),     # the V smoothing factor
    _p(0x02510EE4, 0x0250FDC8, CE2, "PORT_CAMERA_MANUAL"),     # the U smoothing factor
    # Enemy lock-on range (runtime/src/mods/lock_on.cpp): the attention's distance limits
    _p(0x024EB4A8, 0x024EB2D4, "wwhd_src/d/d_attention.cpp", "PORT_LOCK_ON_DISTANCE"),    # calcWeight
    _p(0x024EB868, 0x024EB7F0, "wwhd_src/d/d_attention.cpp", "PORT_LOCK_ON"),             # EnemyDistance
    _p(0x024ECC70, 0x024ECB08, "wwhd_src/d/d_attention_2.cpp", "PORT_LOCK_ON_DISTANCE"),  # chaseAttention
    _p(0x024EE574, 0x024EE49C, "wwhd_src/d/d_attention_3.cpp", "PORT_LOCK_ON"),           # LockonReleaseDistanse
    # The Picto Box at wider aspects (runtime/src/aspect.cpp, issue #303)
    _p(0x025BE898, 0x025BE870, SN, "PORT_PICTO"),  # Execute: after Judge (WWHD_PICTO_LOG)
    _p(0x025BF5D0, 0x025BF250, SN, "PORT_PICTO"),  # draw: the colour copy
    _p(0x025BF7C0, 0x025BF250, SN, "PORT_PICTO"),  # draw: the depth copy
    _p(0x025BFDF8, 0x025BFDC0, SN, "PORT_PICTO"),  # DebugDraw: the TV projection's off-centre terms
    _p(0x025BFFB0, 0x025BFDC0, SN, "PORT_PICTO"),  # DebugDraw: the subjects' projection
    _p(0x025C0004, 0x025BFDC0, SN, "PORT_PICTO"),  # DebugDraw: dSnap_project's projection
]

EMPTY = [
    # posMoveFromFootPos: foot movement, gravity (retired True 60)
    dict(site=0x023FCEB0, func=0x023FCB9C), dict(site=0x023FD338, func=0x023FCB9C),
    dict(site=0x023FD35C, func=0x023FCB9C),
    # (daPy_lk_c::execute's three empty sites stay, fast mode: its decompiled body, never run in the game before, crashes
    # after the file select with the TV-frame clock (a read at guest address 8); to be looked at in the decompilation first)
    # (camera_run 024FE3E8's two empty sites stay: its decompiled body, never run in the game before, does not play as the
    # recompiled one with the 21:9 aspect and the letterbox on; to be looked at in the decompilation first)
    # the chase camera's step-length sites (025028B8; retired True 60)
    dict(site=0x02502CCC, func=0x025028B8), dict(site=0x02502CD0, func=0x025028B8), dict(site=0x02502E44, func=0x025028B8),
    dict(site=0x02502E48, func=0x025028B8), dict(site=0x02502E8C, func=0x025028B8), dict(site=0x02502E90, func=0x025028B8),
    dict(site=0x02502ECC, func=0x025028B8), dict(site=0x02502ED0, func=0x025028B8), dict(site=0x0250329C, func=0x025028B8),
    dict(site=0x025032CC, func=0x025028B8), dict(site=0x025034AC, func=0x025028B8), dict(site=0x025034B0, func=0x025028B8),
    dict(site=0x025034D0, func=0x025028B8), dict(site=0x02503540, func=0x025028B8), dict(site=0x02503544, func=0x025028B8),
    dict(site=0x02503564, func=0x025028B8), dict(site=0x025035BC, func=0x025028B8), dict(site=0x02503670, func=0x025028B8),
    dict(site=0x02503674, func=0x025028B8), dict(site=0x025037B0, func=0x025028B8), dict(site=0x025037B4, func=0x025028B8),
    dict(site=0x02503E20, func=0x025028B8), dict(site=0x02503E24, func=0x025028B8), dict(site=0x02503EC8, func=0x025028B8),
    dict(site=0x02503ECC, func=0x025028B8), dict(site=0x025042F8, func=0x025028B8), dict(site=0x025043C8, func=0x025028B8),
    dict(site=0x0250447C, func=0x025028B8), dict(site=0x02504620, func=0x025028B8), dict(site=0x02504624, func=0x025028B8),
    dict(site=0x02504660, func=0x025028B8), dict(site=0x02504664, func=0x025028B8), dict(site=0x025046C0, func=0x025028B8),
    dict(site=0x025046C4, func=0x025028B8), dict(site=0x025046FC, func=0x025028B8), dict(site=0x02504700, func=0x025028B8),
    dict(site=0x025047B8, func=0x025028B8), dict(site=0x025047BC, func=0x025028B8), dict(site=0x02504808, func=0x025028B8),
    dict(site=0x0250480C, func=0x025028B8), dict(site=0x025049CC, func=0x025028B8), dict(site=0x025049D0, func=0x025028B8),
    dict(site=0x02504B1C, func=0x025028B8), dict(site=0x02504B70, func=0x025028B8), dict(site=0x02504B74, func=0x025028B8),
    dict(site=0x02504B78, func=0x025028B8), dict(site=0x02504B7C, func=0x025028B8), dict(site=0x02504B94, func=0x025028B8),
    dict(site=0x02504B98, func=0x025028B8), dict(site=0x02504BA8, func=0x025028B8), dict(site=0x02504BAC, func=0x025028B8),
    dict(site=0x02504C00, func=0x025028B8), dict(site=0x02504C04, func=0x025028B8), dict(site=0x02504C14, func=0x025028B8),
    dict(site=0x02504C18, func=0x025028B8), dict(site=0x02504C68, func=0x025028B8), dict(site=0x02504C6C, func=0x025028B8),
    dict(site=0x02504D50, func=0x025028B8), dict(site=0x02504DA0, func=0x025028B8), dict(site=0x02504DA4, func=0x025028B8),
    dict(site=0x02504DB0, func=0x025028B8), dict(site=0x02504DB4, func=0x025028B8), dict(site=0x02504ECC, func=0x025028B8),
    dict(site=0x02504F5C, func=0x025028B8),
]


def moved(listed, texts):
    """the SITES whose function is in `listed` (decompiled) and whose source text (texts: path -> text as compiled)
    carries the marker, and the EMPTY sites whose function is in `listed` (path: the handler's file)"""
    return ([s for s in SITES if s["func"] in listed and s["marker"] in texts.get(s["path"], "")] +
            [dict(s, path="runtime/src/game_hooks.cpp: empty") for s in EMPTY if s["func"] in listed])
