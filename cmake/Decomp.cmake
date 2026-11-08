# Decomp build (WWHD_DECOMP=ON, docs/decomp-build.md): the game functions of the decompilation (the
# `decomp` submodule) are compiled and linked into the game, and run in place of the recompiled code.
#
# wwhd_decomp_configure(): run at configure time, before the game code library is set up.
#   tools/decomp_build/gen.py writes the function list, the runtime glue (hooks.cpp) and the unit list
#   (units.cmake) into ${CMAKE_BINARY_DIR}/decomp, with the decompilation's GameCube-style sources
#   rewritten by its autoglue.py (decomp/glued) and the model data headers made from your game by its
#   assets.py (decomp/include/assets; game data, kept in the build directory), and GEN_DIR is checked: it must be recompiled with
#   that list (recomp.py --decomp-hooks), which it records as GEN_DIR/decomp_hooks.txt.
# wwhd_decomp_link(target): every game unit becomes one object file: its sources compiled with hidden
#   visibility and merged with `ld -r`, which turns hidden symbols into local ones. Units then never see
#   each other's classes, inline helpers or gabi state, as in the decompilation's verification harness,
#   where each unit is its own program. On Android (ELF) ld.lld's `ld -r` keeps hidden symbols global, so
#   llvm-objcopy --localize-hidden then makes them local (the merged object's COMDAT groups are dropped
#   with them: each unit keeps its own copy of an inline function, as with ld64). These objects, the runtime part (runtime/src/decomp) and
#   hooks.cpp go into the target.
set(WWHD_DECOMP_DIR "${CMAKE_SOURCE_DIR}/decomp" CACHE PATH "decomp build: the decompilation (git submodule decomp)")
set(WWHD_DECOMP_GAME "${CMAKE_SOURCE_DIR}/game" CACHE PATH
    "decomp build: your own copy of the game (USA; the folder with code/cking.rpx), read for the model data and the source glue")
set(WWHD_DECOMP_OUT "${CMAKE_BINARY_DIR}/decomp")
# The release's part of the decomp build (tools/release/package.py --decomp-build; setup builds the decomp
# version on the player's machine, docs/decomp-build.md): the runtime compiled for the decomp build, without
# the game units and without reading the game. The generated hooks.cpp is replaced by an empty table
# (tools/decomp_build/hooks_none.cpp, which setup replaces with its own hooks and units); the object library
# wwhd_decomp_flags (never built) records in compile_commands.json how setup compiles a game unit.
option(WWHD_DECOMP_THINLTO "decomp build: ThinLTO inside each game unit (experimental)" OFF)
# Fast mode (2026-11-06, decomp build level 1): speed only. Verifier isolation of the call path, save-state compatibility
# and guest-mod hooking are dropped (the game must still play as the normal build); units stay isolated (ld -r, hidden
# symbols local), but a register-exact function is exported by address (extern "C" wwhd_ne_XXXXXXXX, native_rules.py
# --fast) and a call from another unit binds to it at link time (gabi.h GABI_FAST_NE). Forces the level-1 switches it
# builds on (below); WWHD_DECOMP_FAST_LTO: ThinLTO inside each unit (as WWHD_DECOMP_THINLTO, also on Android).
option(WWHD_DECOMP_FAST "decomp build: fast mode (calls between units bound by address; no isolation, no full states)" OFF)
option(WWHD_DECOMP_FAST_LTO "decomp build: fast mode with ThinLTO inside each game unit" ON)
# Fast mode, unit isolation in the IR (tools/decomp_build/unit_ir.py: llvm-link of the unit's bitcode, hidden definitions
# internal) instead of `ld -r` + hidden -> local. WWHD_DECOMP_FAST_UNIT_IR: each unit still optimised alone (native
# object; the baseline of the next one, e.g. for a toolchain without `ld -r` such as ld64.lld); WWHD_DECOMP_FAST_WHOLE_LTO
# (implies it): the units stay ThinLTO bitcode and the final link optimises across them (cross-unit inlining through
# the exported wwhd_ne_* entries). Needs llvm-link / llvm-dis / llvm-as of the compiler's LLVM (the NDK's on Android).
option(WWHD_DECOMP_FAST_UNIT_IR "decomp build, fast mode: units isolated in the IR (unit_ir.py), each optimised alone" OFF)
# WWHD_DECOMP_FAST_WHOLE_LTO: ON / OFF force it; AUTO (default, lead/user decision 2026-11-07, phone -1.69 ms per logic
# pass): ON when the building machine has at least WWHD_DECOMP_FAST_WHOLE_LTO_MIN_RAM_GB of memory (the final link peaks
# at ~5.5 GB on Android, ~3.4 GB on macOS; a phone building for itself usually has less) and the compiler's LLVM tools
# are there (Xcode has no llvm-link), else the per-unit link; one configure line says which and why.
set(WWHD_DECOMP_FAST_WHOLE_LTO "AUTO" CACHE STRING "decomp build, fast mode: ThinLTO across all game units at the final link (AUTO / ON / OFF)")
set_property(CACHE WWHD_DECOMP_FAST_WHOLE_LTO PROPERTY STRINGS AUTO ON OFF)
set(WWHD_DECOMP_FAST_WHOLE_LTO_MIN_RAM_GB "12" CACHE STRING "decomp build, fast mode: memory (GB) WWHD_DECOMP_FAST_WHOLE_LTO=AUTO needs for the whole-image link")
set(WWHD_DECOMP_FAST_LTO_JOBS "8" CACHE STRING "decomp build, fast mode: ThinLTO backend jobs of the final link (WWHD_DECOMP_FAST_WHOLE_LTO)")
option(WWHD_DECOMP_RUNTIME_ONLY "decomp build: the runtime only, for packaging (no units, no game)" OFF)

function(wwhd_decomp_configure)
  if(NOT APPLE AND NOT ANDROID)
    # the unit isolation needs ld64's `ld -r` (hidden -> local), or on Android ld.lld -r + llvm-objcopy
    # --localize-hidden (wwhd_decomp_link); other platforms need their own step
    message(FATAL_ERROR "WWHD_DECOMP: the decomp build is available on macOS and Android only so far")
  endif()
  if(NOT EXISTS "${WWHD_DECOMP_DIR}/tools/verify/include/gabi.h")
    message(FATAL_ERROR "WWHD_DECOMP: no decompilation in ${WWHD_DECOMP_DIR}; run: git submodule update --init decomp")
  endif()
  if(WWHD_DECOMP_RUNTIME_ONLY)
    set(WWHD_DECOMP_UNIT_COUNT 0 PARENT_SCOPE)
    return()
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  # fast mode: the functions the runtime hooks run their decompiled bodies behind the hook (gen.py --hooked-bodies;
  # WWHD_DECOMP_FAST_HOOKED=OFF keeps them recompiled as in the default decomp build)
  option(WWHD_DECOMP_FAST_HOOKED "decomp build, fast mode: runtime-hooked functions run decompiled behind their hook" ON)
  # fast mode: calls by constant address outside the ext stubs (the headers' gabi::call<R>(0xADDR, ...) wrappers and the
  # sources' own) written as gabi::hcall, which binds a register-exact callee of another unit at link time as lcall does
  # (gen.py --hcall: OUT/hinc, native_rules.py --hcall; WWHD_DECOMP_FAST_HCALL=OFF: as before)
  option(WWHD_DECOMP_FAST_HCALL "decomp build, fast mode: header calls by constant address bound at link time" ON)
  set(gen_extra)
  if(WWHD_DECOMP_FAST AND WWHD_DECOMP_FAST_HOOKED)
    list(APPEND gen_extra --hooked-bodies)
  endif()
  if(WWHD_DECOMP_FAST AND WWHD_DECOMP_FAST_HCALL)
    list(APPEND gen_extra --hcall)
  endif()
  # fast mode: call_ptr / tail_ptr (function pointers, vtables) keep their last target's native entry per call site
  # (gen.py --ptr-ic, gabi.h GABI_PTR_IC) instead of probing the address table on every call
  option(WWHD_DECOMP_FAST_PTR_IC "decomp build, fast mode: per-call-site cache of call_ptr / tail_ptr targets" ON)
  if(WWHD_DECOMP_FAST AND WWHD_DECOMP_FAST_PTR_IC)
    list(APPEND gen_extra --ptr-ic)
  endif()
  # ... with the last two targets per site (gabi.h GABI_PTR_IC2; for sites whose target alternates)
  option(WWHD_DECOMP_FAST_PTR_IC2 "decomp build, fast mode: two cached call_ptr / tail_ptr targets per call site" OFF)
  # the decompilation's port_hooks.h points call the port in this game build (-DGABI_GAME_BUILD below): their
  # instruction hooks no longer keep the functions recompiled (gen.py --game-build, tools/decomp_build/port_sites.py)
  list(APPEND gen_extra --game-build)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/gen.py"
                          "${WWHD_DECOMP_DIR}" "${WWHD_DECOMP_OUT}"
                          --game "${WWHD_DECOMP_GAME}/code/cking.rpx" ${gen_extra}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "WWHD_DECOMP: tools/decomp_build/gen.py failed:\n${out}\n${err}")
  endif()
  message(STATUS "${out}")
  include("${WWHD_DECOMP_OUT}/units.cmake")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WWHD_DECOMP_GEN_DEPENDS})
  set(list "${WWHD_DECOMP_OUT}/decomp_hooks.txt")
  string(CONCAT fix "Translate the game code with the decomp build's function list:\n"
         "  python3 tools/recomp/recomp.py game/code/cking.rpx ${GEN_DIR} --decomp-hooks ${list}\n"
         "(keep the normal build's GEN_DIR as it is; e.g. -DGEN_DIR=build/gen-decomp for this build)")
  if(NOT EXISTS "${GEN_DIR}/decomp_hooks.txt")
    message(FATAL_ERROR "WWHD_DECOMP: ${GEN_DIR} is not decomp build code.\n${fix}")
  endif()
  file(SHA256 "${GEN_DIR}/decomp_hooks.txt" have)
  file(SHA256 "${list}" want)
  if(NOT have STREQUAL want)
    message(FATAL_ERROR "WWHD_DECOMP: ${GEN_DIR} was made with another decomp function list.\n${fix}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GEN_DIR}/decomp_hooks.txt")
  # the native-call rules (tools/decomp_build/native_rules.py: decomp/native.txt and the units' native tables).
  # WWHD_DECOMP_PARK_RULE=strict: callees that may park (C4, indirect calls included) stay guest calls (comparison)
  set(WWHD_DECOMP_PARK_RULE "" CACHE STRING "decomp build: native-call park rule (empty: none, strict: C4)")
  # the decompilation's declaration table (decl_lint.py --handoff): register-exact functions get real native calls
  # (C++ arguments and result, no guest registers). Empty: the decompilation's tools/verify/decl_handoff.tsv if any.
  set(WWHD_DECOMP_DECL_TABLE "$ENV{WWHD_DECOMP_DECL_TABLE}" CACHE FILEPATH "decomp build: decl_lint.py --handoff table (development override; default: the environment variable of that name)")
  # level 1 step 2: guest-path callees made native, one switch per category (native_rules.py --lift; gabi.h
  # GABI_NATIVE_PLACE places r1 as do_call does on such entries). Off by default until the phone confirms each.
  option(WWHD_DECOMP_NATIVE_AUTOFRAME "decomp build: AutoFrame callees (and their own calls) native (level 1 step 2)" OFF)
  option(WWHD_DECOMP_NATIVE_LOCAL "decomp build: callees with guest-stack Locals native (level 1 step 2)" OFF)
  option(WWHD_DECOMP_NATIVE_TAILPTR "decomp build: tail_ptr callees native (level 1 step 2)" OFF)
  set(lift)
  foreach(k AUTOFRAME LOCAL TAILPTR)
    if(WWHD_DECOMP_NATIVE_${k} OR WWHD_DECOMP_FAST)  # fast mode: all step-2 categories
      string(TOLOWER "${k}" l)
      list(APPEND lift "${l}")
    endif()
  endforeach()
  set(lift_args)
  if(lift)
    list(JOIN lift "," lj)
    set(lift_args --lift "${lj}")
  endif()
  set(decl_args)
  if(WWHD_DECOMP_DECL_TABLE)
    set(decl_args --decl "${WWHD_DECOMP_DECL_TABLE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WWHD_DECOMP_DECL_TABLE}")
  elseif(EXISTS "${WWHD_DECOMP_DIR}/tools/verify/decl_handoff.tsv")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WWHD_DECOMP_DIR}/tools/verify/decl_handoff.tsv")
  elseif(WWHD_DECOMP_FAST)
    # fast mode needs the declaration table: without it no function is register-exact, and fast mode's shape-matched
    # entries of the other functions (gabi.h bit 14) are only safe for the few calls the table leaves to them (a fast
    # build without a table crashed at start, 2026-11-07). Made here from this build's own translation with the
    # decompilation's decl_lint.py (port mode; tools/decomp_build/decl_table.py, cached by a hash of its inputs: a few
    # minutes when the decompilation or the translation changed, nothing otherwise). WWHD_DECOMP_DECL_TABLE overrides it.
    cmake_host_system_information(RESULT ncpu QUERY NUMBER_OF_LOGICAL_CORES)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/decl_table.py"
                            "${WWHD_DECOMP_DIR}" "${GEN_DIR}" "${WWHD_DECOMP_OUT}/verify/image.bin"
                            "${WWHD_DECOMP_GAME}/code/cking.rpx" "${WWHD_DECOMP_OUT}/decl" --jobs ${ncpu}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "WWHD_DECOMP_FAST needs the decompilation's declaration table and could not make it:\n${out}\n${err}\n"
              "(or give one: WWHD_DECOMP_DECL_TABLE=<decl_lint.py --handoff table>)")
    endif()
    message(STATUS "${out}")
    set(decl_args --decl "${WWHD_DECOMP_OUT}/decl/handoff.tsv")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/decomp_build/decl_table.py")
  endif()
  set(fast_args)
  if(WWHD_DECOMP_FAST)
    set(fast_args --fast)
    if(WWHD_DECOMP_FAST_HCALL)
      list(APPEND fast_args --hcall)
    endif()
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/native_rules.py"
                          "${WWHD_DECOMP_DIR}" "${WWHD_DECOMP_OUT}" "${GEN_DIR}" ${WWHD_DECOMP_PARK_RULE} ${decl_args} ${lift_args} ${fast_args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "WWHD_DECOMP: tools/decomp_build/native_rules.py failed:\n${out}\n${err}")
  endif()
  message(STATUS "${out}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/decomp_build/native_rules.py")
  # site-exact LR (tools/decomp_build/site_lr.py): the original's bl lists per unit and the sited sources' list
  # default OFF (2026-11-04: about 3 ms per logic pass on the phone, ladder step 2); level 1 sets LR only at the edges
option(WWHD_DECOMP_SITE_LR "decomp build: each guest call of a decompiled function gets its own bl's return address where it can be matched" OFF)
  if(WWHD_DECOMP_FAST AND WWHD_DECOMP_SITE_LR)
    message(FATAL_ERROR "WWHD_DECOMP_FAST needs WWHD_DECOMP_SITE_LR off")
  endif()
  if(WWHD_DECOMP_SITE_LR)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/site_lr.py" prepare
                            "${WWHD_DECOMP_OUT}" "${GEN_DIR}" "${WWHD_DECOMP_DIR}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "WWHD_DECOMP: tools/decomp_build/site_lr.py prepare failed:\n${out}\n${err}")
    endif()
    message(STATUS "${out}")
    include("${WWHD_DECOMP_OUT}/sites/sites.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/decomp_build/site_lr.py")
  endif()
  set(WWHD_DECOMP_UNIT_COUNT ${WWHD_DECOMP_UNIT_COUNT} PARENT_SCOPE)
  foreach(i RANGE 1 ${WWHD_DECOMP_UNIT_COUNT})
    math(EXPR k "${i} - 1")
    set(WWHD_DECOMP_UNIT_${k}_NAME "${WWHD_DECOMP_UNIT_${k}_NAME}" PARENT_SCOPE)
    set(WWHD_DECOMP_UNIT_${k}_SOURCES "${WWHD_DECOMP_UNIT_${k}_SOURCES}" PARENT_SCOPE)
    set(WWHD_DECOMP_UNIT_${k}_SITED "${WWHD_DECOMP_UNIT_${k}_SITED}" PARENT_SCOPE)
  endforeach()
endfunction()

# the host replacements of the port-owned guest runtime (GHS C/C++ runtime, nn static wrappers): decomp build
# only (recomp.py --decomp-hooks routes tools/recomp/decomp_hostrt_hooks.txt to them)
file(GLOB WWHD_HOSTRT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/hostrt/*.cpp")

function(wwhd_decomp_link target)
  target_sources(${target} PRIVATE ${WWHD_HOSTRT_SOURCES})
  # as the decompilation's verify.py; decomp/include holds the generated assets/ headers
  set(inc "${CMAKE_SOURCE_DIR}/runtime/include" "${WWHD_DECOMP_DIR}/tools/verify/include"
          "${WWHD_DECOMP_DIR}/tools/verify/src" "${WWHD_DECOMP_DIR}/wwhd_src/include" "${WWHD_DECOMP_OUT}/include")
  if(WWHD_DECOMP_FAST AND (WWHD_DECOMP_FAST_HCALL OR WWHD_DECOMP_FAST_PTR_IC))  # gen.py's copy of wwhd_src/include first
    list(INSERT inc 3 "${WWHD_DECOMP_OUT}/hinc")
  endif()
  # as the decompilation's harness compiles them (exact floating point: no contraction, no fast math);
  # these options come after the configuration's, so they win. GABI_GAME_BUILD: gabi.h's game-build forms
  # (its per-thread state in one block, guest memory accessed inline), which the harness never uses;
  # GABI_SHARED_TLS: that block is one for all units (glue/0.cpp defines it; real native calls between units).
  set(opts -O2 -g0 -w -fvisibility=hidden -fvisibility-inlines-hidden -ffp-contract=off -fno-fast-math -fwrapv
           -fno-strict-aliasing -DGABI_GAME_BUILD=1 -include "${CMAKE_SOURCE_DIR}/tools/decomp_build/frames_vis.h")
  # real native calls (gabi.h): one thread-local block for all units, and the units' native entries for calls
  # between units (both on by default; off only to measure what they cost)
  option(WWHD_DECOMP_SHARED_TLS "decomp build: one gabi thread-local block for all game units" ON)
  option(WWHD_DECOMP_NATIVE_ENTRIES "decomp build: native entries for calls between game units (NativeReg)" ON)
  # fast mode forces the level-1 switches it builds on (gabi.h GABI_FAST checks them) and keeps the others off; these
  # are function-scope variables, the cache entries stay as the user set them
  set(fast_on SHARED_TLS NATIVE_ENTRIES LEAN_ENTRY SLIM_SCOPE TP_CACHE TP_ENTRY GUEST_LCALL)
  set(fast_off TP_CACHE_CHECK EDGE_LCALL SITE_LR THINLTO)
  if(WWHD_DECOMP_FAST)
    if(WWHD_DECOMP_EDGE_LCALL)
      message(FATAL_ERROR "WWHD_DECOMP_FAST keeps WWHD_DECOMP_EDGE_LCALL off (no gain on the phone, 2026-11-06)")
    endif()
    foreach(k ${fast_on})
      set(WWHD_DECOMP_${k} ON)
    endforeach()
    foreach(k ${fast_off})
      set(WWHD_DECOMP_${k} OFF)
    endforeach()
    set(WWHD_DECOMP_NATIVE_AUTOFRAME ON)  # (native_rules.py got --lift autoframe,local,tailptr: GABI_NATIVE_PLACE)
    list(APPEND opts -DGABI_FAST=1 -DGABI_FAST_NE=1)
    if(WWHD_DECOMP_FAST_PTR_IC)
      list(APPEND opts -DGABI_PTR_IC=1)
      if(WWHD_DECOMP_FAST_PTR_IC2)
        list(APPEND opts -DGABI_PTR_IC2=1)
      endif()
    endif()
  endif()
  if(WWHD_DECOMP_SHARED_TLS)
    list(APPEND opts -DGABI_SHARED_TLS=1)
  endif()
  # level 1: register-exact native entries keep r1 and LR (guest state only at the edges); on since the phone confirmed
  # it (2026-11-05 P1: 21.35 against 21.65 ms per logic pass, ranges apart)
  option(WWHD_DECOMP_LEAN_ENTRY "decomp build: register-exact native entries set no r1/LR (level 1)" ON)
  if(WWHD_DECOMP_LEAN_ENTRY)
    list(APPEND opts -DGABI_LEAN_ENTRY=1)
  endif()
  # level 1 step 1b: NativeScope's fast path inline in each body, its rare parts out of line (gabi.h GABI_SLIM_SCOPE);
  # on since the phone confirmed it (2026-11-05 S1: 19.16 against 21.31 ms per logic pass, ranges apart)
  option(WWHD_DECOMP_SLIM_SCOPE "decomp build: inline native-call scope fast path (level 1 step 1b)" ON)
  if(WWHD_DECOMP_SLIM_SCOPE)
    list(APPEND opts -DGABI_SLIM_SCOPE=1)
  endif()
  # level 1 step 1a: the shared block through a thread-pointer table instead of a thread-local access per body
  # (gabi.h GABI_TP_CACHE; needs the slim scope); _CHECK traps on any hit that is not the thread's own block (debug)
  option(WWHD_DECOMP_TP_CACHE "decomp build: thread-pointer table for gabi's thread block (level 1 step 1a)" OFF)
  option(WWHD_DECOMP_TP_CACHE_CHECK "decomp build: verify every thread-pointer table hit (debug)" OFF)
  # step 1a-fix: under the table, a guest entry (EntryScope) looks the block up once instead of once per field
  option(WWHD_DECOMP_TP_ENTRY "decomp build: one thread-block lookup per guest entry (level 1 step 1a-fix; needs TP_CACHE)" OFF)
  if(WWHD_DECOMP_TP_ENTRY AND NOT WWHD_DECOMP_TP_CACHE)
    message(FATAL_ERROR "WWHD_DECOMP_TP_ENTRY needs WWHD_DECOMP_TP_CACHE")
  endif()
  if(WWHD_DECOMP_TP_CACHE)
    list(APPEND opts -DGABI_TP_CACHE=1)
    if(WWHD_DECOMP_TP_ENTRY)
      list(APPEND opts -DGABI_TP_ENTRY=1)
    endif()
    if(WWHD_DECOMP_TP_CACHE_CHECK)
      list(APPEND opts -DGABI_TP_CACHE_CHECK=1)
    endif()
  endif()
  if(WWHD_DECOMP_NATIVE_AUTOFRAME OR WWHD_DECOMP_NATIVE_LOCAL OR WWHD_DECOMP_NATIVE_TAILPTR)
    if(NOT WWHD_DECOMP_SLIM_SCOPE OR NOT WWHD_DECOMP_LEAN_ENTRY)
      message(FATAL_ERROR "WWHD_DECOMP_NATIVE_AUTOFRAME/_LOCAL/_TAILPTR need WWHD_DECOMP_SLIM_SCOPE and WWHD_DECOMP_LEAN_ENTRY")
    endif()
    list(APPEND opts -DGABI_NATIVE_PLACE=1)
  endif()
  # level 1 step 3: edges. _EDGE_LCALL: a call to a constant address that is no decompiled function enters the
  # runtime's edge entry (decomp_edge_call: LR through a one-word cache per call site, no site-table lookup);
  # _GUEST_LCALL: a body's guest call of a callee of its own unit enters by slot (decomp_lcall) as calls between units do
  option(WWHD_DECOMP_EDGE_LCALL "decomp build: edge calls through a per-site LR cache (level 1 step 3)" OFF)
  option(WWHD_DECOMP_GUEST_LCALL "decomp build: guest calls inside a unit by slot (level 1 step 3b)" OFF)
  if(WWHD_DECOMP_EDGE_LCALL OR WWHD_DECOMP_GUEST_LCALL)
    if(WWHD_DECOMP_SITE_LR)
      message(FATAL_ERROR "WWHD_DECOMP_EDGE_LCALL / _GUEST_LCALL need WWHD_DECOMP_SITE_LR off (no site marks)")
    endif()
  endif()
  if(WWHD_DECOMP_EDGE_LCALL)
    list(APPEND opts -DGABI_EDGE_LCALL=1)
  endif()
  if(WWHD_DECOMP_GUEST_LCALL)
    list(APPEND opts -DGABI_GUEST_LCALL=1)
  endif()
  if(WWHD_DECOMP_NATIVE_ENTRIES)
    # (without the shared block the units register no entry: glue.cpp)
    list(APPEND opts -DGABI_NATIVE_ENTRIES=1)
  endif()
  if(ANDROID)  # bionic's libc macros that clash with the decompilation's names (android_compat.h)
    list(APPEND opts "SHELL:-include ${CMAKE_SOURCE_DIR}/tools/decomp_build/android_compat.h")
  endif()
  if(WWHD_DECOMP_RUNTIME_ONLY)
    add_library(wwhd_decomp_flags OBJECT "${CMAKE_SOURCE_DIR}/tools/decomp_build/flags_probe.cpp")
    target_include_directories(wwhd_decomp_flags PRIVATE ${inc})
    target_compile_options(wwhd_decomp_flags PRIVATE ${opts})
    set_target_properties(wwhd_decomp_flags PROPERTIES CXX_EXTENSIONS OFF EXCLUDE_FROM_ALL ON)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp"
      PROPERTIES INCLUDE_DIRECTORIES "${WWHD_DECOMP_DIR}/tools/verify/include")
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/runtime/src/decomp/decomp.cpp"
      "${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${CMAKE_SOURCE_DIR}/tools/decomp_build/hooks_none.cpp")
    target_compile_definitions(${target} PRIVATE WWHD_DECOMP=1)
    return()
  endif()
  # WWHD_DECOMP_THINLTO: ThinLTO inside each unit (its sources and glue are optimised together when `ld -r` merges
  # them; units stay isolated, so nothing is inlined across units or into the runtime)
  set(unit_link "${CMAKE_LINKER}" -r)
  set(unit_lto OFF)
  if(WWHD_DECOMP_THINLTO OR (WWHD_DECOMP_FAST AND WWHD_DECOMP_FAST_LTO))
    set(unit_lto ON)
    list(APPEND opts -flto=thin)
    set(unit_link "${CMAKE_CXX_COMPILER}" -r -nostdlib -flto=thin)  # (no libraries in a relocatable link)
  endif()
  set(unit_ir OFF)
  # WWHD_DECOMP_FAST_WHOLE_LTO: AUTO / ON / OFF -> whole_lto (see the option)
  set(whole_lto OFF)
  if(WWHD_DECOMP_FAST AND unit_lto)
    string(TOUPPER "${WWHD_DECOMP_FAST_WHOLE_LTO}" wl)
    if(ANDROID)
      set(wl_hints "${ANDROID_TOOLCHAIN_ROOT}/bin")
    else()
      get_filename_component(wl_hints "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()
    if(wl STREQUAL "AUTO")
      cmake_host_system_information(RESULT ram_mb QUERY TOTAL_PHYSICAL_MEMORY)
      math(EXPR need_mb "${WWHD_DECOMP_FAST_WHOLE_LTO_MIN_RAM_GB} * 1024")
      find_program(WWHD_DECOMP_LLVM_LINK_PROBE llvm-link HINTS "${wl_hints}" NO_DEFAULT_PATH)
      if(ram_mb LESS need_mb)
        message(STATUS "decomp fast mode: per-unit link (WWHD_DECOMP_FAST_WHOLE_LTO=AUTO: ${ram_mb} MB of memory, the whole-image link needs ${need_mb})")
      elseif(NOT WWHD_DECOMP_LLVM_LINK_PROBE)
        message(STATUS "decomp fast mode: per-unit link (WWHD_DECOMP_FAST_WHOLE_LTO=AUTO: no llvm-link next to the compiler)")
      else()
        set(whole_lto ON)
        message(STATUS "decomp fast mode: whole-image ThinLTO link (WWHD_DECOMP_FAST_WHOLE_LTO=AUTO: ${ram_mb} MB of memory)")
      endif()
    elseif(wl)
      set(whole_lto ON)
      message(STATUS "decomp fast mode: whole-image ThinLTO link (WWHD_DECOMP_FAST_WHOLE_LTO=${WWHD_DECOMP_FAST_WHOLE_LTO})")
    else()
      message(STATUS "decomp fast mode: per-unit link (WWHD_DECOMP_FAST_WHOLE_LTO=${WWHD_DECOMP_FAST_WHOLE_LTO})")
    endif()
  endif()
  if(WWHD_DECOMP_FAST AND (WWHD_DECOMP_FAST_UNIT_IR OR whole_lto))
    if(NOT unit_lto)
      message(FATAL_ERROR "WWHD_DECOMP_FAST_UNIT_IR needs WWHD_DECOMP_FAST_LTO (bitcode units)")
    endif()
    set(unit_ir ON)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(ANDROID)
      set(ir_hints "${ANDROID_TOOLCHAIN_ROOT}/bin")
    else()
      get_filename_component(ir_hints "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()
    find_program(WWHD_DECOMP_LLVM_LINK llvm-link HINTS "${ir_hints}" NO_DEFAULT_PATH REQUIRED)
    get_filename_component(ir_tools "${WWHD_DECOMP_LLVM_LINK}" DIRECTORY)
    set(ir_cc "${CMAKE_CXX_COMPILER}")
    set(ir_args --tools "${ir_tools}" --cc "${ir_cc}")
    if(NOT whole_lto)
      list(APPEND ir_args --native)
    endif()
  endif()
  set(objs)
  file(MAKE_DIRECTORY "${WWHD_DECOMP_OUT}/units")
  set(merge "${CMAKE_LINKER}" -r)
  set(localize)
  if(ANDROID)  # ELF: ld.lld -r, then hidden -> local with llvm-objcopy (the NDK's)
    find_program(WWHD_DECOMP_LLD ld.lld HINTS "${ANDROID_TOOLCHAIN_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
    find_program(WWHD_DECOMP_OBJCOPY llvm-objcopy HINTS "${ANDROID_TOOLCHAIN_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
    set(merge "${WWHD_DECOMP_LLD}" -r)
    set(unit_link ${merge})
    if(unit_lto)  # ld.lld -r runs the (Thin)LTO backend on the unit's bitcode and writes one relocatable object
      if(WWHD_DECOMP_THINLTO AND NOT WWHD_DECOMP_FAST)
        message(FATAL_ERROR "WWHD_DECOMP_THINLTO is not supported on Android (use WWHD_DECOMP_FAST, ThinLTO per unit)")
      endif()
      set(unit_link ${merge} --lto-O2 --thinlto-jobs=1)  # one backend thread per unit link (ninja runs several)
    endif()
  endif()
  if(WWHD_DECOMP_SITE_LR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
  endif()
  foreach(i RANGE 1 ${WWHD_DECOMP_UNIT_COUNT})
    math(EXPR k "${i} - 1")
    set(lib wwhd_decomp_unit_${k})
    set(unit_opts ${opts})
    # the unit's native-call table (tools/decomp_build/native_rules.py; gabi.h GABI_NATIVE_TABLE)
    if(EXISTS "${WWHD_DECOMP_OUT}/native/${k}.h")
      list(APPEND unit_opts "SHELL:-include \"${WWHD_DECOMP_OUT}/native/${k}.h\"")
    endif()
    set(unit_srcs ${WWHD_DECOMP_UNIT_${k}_SOURCES})
    if(WWHD_DECOMP_SITE_LR)
      list(APPEND unit_opts -DGABI_SITE_LR=1)  # gabi.h: calls look for the sites' marks
      # site-exact LR: the unit's sources compiled once more, as LLVM IR scanned for the guest calls of each call
      # expression (the object files are the scans), then the sited copies (tools/decomp_build/site_lr.py)
      add_library(${lib}_scan OBJECT ${WWHD_DECOMP_UNIT_${k}_SOURCES})
      target_include_directories(${lib}_scan PRIVATE ${inc})
      target_compile_options(${lib}_scan PRIVATE ${unit_opts})
      set_target_properties(${lib}_scan PROPERTIES CXX_EXTENSIONS OFF EXCLUDE_FROM_ALL ON
        CXX_COMPILER_LAUNCHER "${Python3_EXECUTABLE};${CMAKE_SOURCE_DIR}/tools/decomp_build/site_lr.py;cc")
      if(ANDROID)
        set_target_properties(${lib}_scan PROPERTIES POSITION_INDEPENDENT_CODE ON)
      endif()
      add_custom_command(OUTPUT ${WWHD_DECOMP_UNIT_${k}_SITED} "${WWHD_DECOMP_OUT}/sites/${k}.txt"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/site_lr.py" unit "${WWHD_DECOMP_OUT}" ${k}
                "$<TARGET_OBJECTS:${lib}_scan>"
        DEPENDS ${lib}_scan "$<TARGET_OBJECTS:${lib}_scan>" "${CMAKE_SOURCE_DIR}/tools/decomp_build/site_lr.py"
                "${WWHD_DECOMP_OUT}/sites/bl_${k}.json" "${WWHD_DECOMP_OUT}/sites/macros.txt" "${WWHD_DECOMP_OUT}/sites/noscope.txt"
        COMMAND_EXPAND_LISTS VERBATIM)
      set(unit_srcs ${WWHD_DECOMP_UNIT_${k}_SITED})
    endif()
    add_library(${lib} OBJECT ${unit_srcs} "${WWHD_DECOMP_OUT}/glue/${k}.cpp")
    target_include_directories(${lib} PRIVATE ${inc})
    target_compile_options(${lib} PRIVATE ${unit_opts})
    set_target_properties(${lib} PROPERTIES CXX_EXTENSIONS OFF EXCLUDE_FROM_ALL ON)
    set(o "${WWHD_DECOMP_OUT}/units/${WWHD_DECOMP_UNIT_${k}_NAME}.o")
    if(ANDROID)
      set_target_properties(${lib} PROPERTIES POSITION_INDEPENDENT_CODE ON)  # linked into libmain.so
      set(localize COMMAND "${WWHD_DECOMP_OBJCOPY}" --localize-hidden "${o}")
    endif()
    if(unit_ir)  # the unit isolated in the IR (unit_ir.py) instead of ld -r + localize
      add_custom_command(OUTPUT "${o}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/unit_ir.py" "${o}" ${ir_args}
                "$<TARGET_OBJECTS:${lib}>"
        DEPENDS ${lib} "$<TARGET_OBJECTS:${lib}>" "${CMAKE_SOURCE_DIR}/tools/decomp_build/unit_ir.py"
        COMMAND_EXPAND_LISTS VERBATIM)
    else()
    add_custom_command(OUTPUT "${o}"
      COMMAND ${unit_link} -o "${o}" "$<TARGET_OBJECTS:${lib}>"
      ${localize}
      DEPENDS ${lib} "$<TARGET_OBJECTS:${lib}>"
      COMMAND_EXPAND_LISTS VERBATIM)
    endif()
    list(APPEND objs "${o}")
  endforeach()
  set_source_files_properties(${objs} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${WWHD_DECOMP_OUT}/hooks.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${WWHD_DECOMP_DIR}/tools/verify/include")
  target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/runtime/src/decomp/decomp.cpp"
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${WWHD_DECOMP_OUT}/hooks.cpp" ${objs})
  target_compile_definitions(${target} PRIVATE WWHD_DECOMP=1)
  if(WWHD_DECOMP_FAST)  # the runtime's fast-mode parts (runtime/src/decomp)
    target_compile_definitions(${target} PRIVATE WWHD_DECOMP_FAST=1)
  endif()
  # fast mode: the GHS libm functions decompiled code calls most (sqrtf, sinf, sqrt, cosf, fmodf) go from gmem_call
  # straight to their recompiled originals, without call_to and the dispatch table (runtime/src/hostrt/hostrt_fast.cpp);
  # OFF (default until the phone pair): the guest path as before
  option(WWHD_DECOMP_FAST_LIBM "decomp build, fast mode: GHS libm calls straight to their recompiled originals" OFF)
  if(WWHD_DECOMP_FAST AND WWHD_DECOMP_FAST_LIBM)
    target_compile_definitions(${target} PRIVATE WWHD_DECOMP_FAST_LIBM=1)  # (only hostrt_fast.cpp reads it)
  endif()
  if(unit_ir AND whole_lto)  # the final link runs ThinLTO over the units' bitcode
    if(APPLE)
      target_link_options(${target} PRIVATE -flto=thin "LINKER:--thinlto-jobs=${WWHD_DECOMP_FAST_LTO_JOBS}"
                          "LINKER:-cache_path_lto,${WWHD_DECOMP_OUT}/ltocache")  # (ld64.lld: Mach-O option names)
    else()
      target_link_options(${target} PRIVATE -flto=thin "LINKER:--thinlto-jobs=${WWHD_DECOMP_FAST_LTO_JOBS}" "LINKER:--lto-O2"
                          "LINKER:--thinlto-cache-dir=${WWHD_DECOMP_OUT}/ltocache")
    endif()
  endif()
  if(WWHD_DECOMP_SITE_LR)
    target_compile_definitions(${target} PRIVATE WWHD_DECOMP_SITE_LR=1)
  endif()
  if(WWHD_DECOMP_EDGE_LCALL)  # the runtime's edge entry (decomp.cpp only)
    set_property(SOURCE "${CMAKE_SOURCE_DIR}/runtime/src/decomp/decomp.cpp" APPEND PROPERTY COMPILE_DEFINITIONS WWHD_DECOMP_EDGE_LCALL=1)
  endif()
endfunction()
