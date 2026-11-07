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
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/gen.py"
                          "${WWHD_DECOMP_DIR}" "${WWHD_DECOMP_OUT}"
                          --game "${WWHD_DECOMP_GAME}/code/cking.rpx"
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
  set(decl_args)
  if(WWHD_DECOMP_DECL_TABLE)
    set(decl_args --decl "${WWHD_DECOMP_DECL_TABLE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WWHD_DECOMP_DECL_TABLE}")
  elseif(EXISTS "${WWHD_DECOMP_DIR}/tools/verify/decl_handoff.tsv")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WWHD_DECOMP_DIR}/tools/verify/decl_handoff.tsv")
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/native_rules.py"
                          "${WWHD_DECOMP_DIR}" "${WWHD_DECOMP_OUT}" "${GEN_DIR}" ${WWHD_DECOMP_PARK_RULE} ${decl_args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "WWHD_DECOMP: tools/decomp_build/native_rules.py failed:\n${out}\n${err}")
  endif()
  message(STATUS "${out}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/decomp_build/native_rules.py")
  # site-exact LR (tools/decomp_build/site_lr.py): the original's bl lists per unit and the sited sources' list
  # default OFF (2026-11-04: about 3 ms per logic pass on the phone, ladder step 2); level 1 sets LR only at the edges
option(WWHD_DECOMP_SITE_LR "decomp build: each guest call of a decompiled function gets its own bl's return address where it can be matched" OFF)
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
  if(WWHD_DECOMP_SHARED_TLS)
    list(APPEND opts -DGABI_SHARED_TLS=1)
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
  if(WWHD_DECOMP_THINLTO)
    list(APPEND opts -flto=thin)
    set(unit_link "${CMAKE_CXX_COMPILER}" -r -flto=thin)
  endif()
  set(objs)
  file(MAKE_DIRECTORY "${WWHD_DECOMP_OUT}/units")
  set(merge "${CMAKE_LINKER}" -r)
  set(localize)
  if(ANDROID)  # ELF: ld.lld -r, then hidden -> local with llvm-objcopy (the NDK's)
    find_program(WWHD_DECOMP_LLD ld.lld HINTS "${ANDROID_TOOLCHAIN_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
    find_program(WWHD_DECOMP_OBJCOPY llvm-objcopy HINTS "${ANDROID_TOOLCHAIN_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
    set(merge "${WWHD_DECOMP_LLD}" -r)
    set(unit_link ${merge})  # (WWHD_DECOMP_THINLTO is not supported on Android)
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
    add_custom_command(OUTPUT "${o}"
      COMMAND ${unit_link} -o "${o}" "$<TARGET_OBJECTS:${lib}>"
      ${localize}
      DEPENDS ${lib} "$<TARGET_OBJECTS:${lib}>"
      COMMAND_EXPAND_LISTS VERBATIM)
    list(APPEND objs "${o}")
  endforeach()
  set_source_files_properties(${objs} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${WWHD_DECOMP_OUT}/hooks.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${WWHD_DECOMP_DIR}/tools/verify/include")
  target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/runtime/src/decomp/decomp.cpp"
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${WWHD_DECOMP_OUT}/hooks.cpp" ${objs})
  target_compile_definitions(${target} PRIVATE WWHD_DECOMP=1)
  if(WWHD_DECOMP_SITE_LR)
    target_compile_definitions(${target} PRIVATE WWHD_DECOMP_SITE_LR=1)
  endif()
endfunction()
