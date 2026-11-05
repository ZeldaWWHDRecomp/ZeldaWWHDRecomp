# SPDX-License-Identifier: MPL-2.0
# wwhd-rando: TWWHD Randomizer 1.2.0 (twwhd_rando/, MIT, see twwhd_rando/WWHD.md) as the setup's randomizer command.
# A project of its own; the game's build includes it with -DWWHD_RANDO=ON (release builds), the game code is the same.
#   cmake -S tools/rando -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build <dir>
#   <dir>/wwhd-rando --data <dir>/data --work WORK COMMAND   (cli/main.cpp)
# Adapted from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0) tools/rando/host/CMakeLists.txt at db65d60.
cmake_minimum_required(VERSION 3.20)
project(wwhd_rando CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RANDO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/twwhd_rando)
file(GLOB RANDO_SOURCES CONFIGURE_DEPENDS
  ${RANDO_DIR}/*.cpp ${RANDO_DIR}/command/*.cpp ${RANDO_DIR}/customizer/*.cpp ${RANDO_DIR}/filetypes/*.cpp
  ${RANDO_DIR}/filetypes/util/*.cpp ${RANDO_DIR}/filetypes/shared/*.cpp ${RANDO_DIR}/filetypes/texture/*.cpp
  ${RANDO_DIR}/filetypes/subfiles/*.cpp ${RANDO_DIR}/logic/*.cpp ${RANDO_DIR}/logic/flatten/*.cpp ${RANDO_DIR}/seedgen/*.cpp
  ${RANDO_DIR}/utility/*.cpp ${RANDO_DIR}/libs/yaml.cpp
  ${RANDO_DIR}/libs/tinyxml2/tinyxml2.cpp ${RANDO_DIR}/libs/base64pp/base64pp/base64pp.cpp ${RANDO_DIR}/libs/yaml-cpp/src/*.cpp
  ${RANDO_DIR}/libs/yaml-cpp/src/contrib/*.cpp)

set(GIT_TAG 1.2.0)  # the release it is: permalinks and seeds match the PC randomizer 1.2.0
set(SEED_KEY "")    # the official builds' race-mode key is secret; race mode is refused (cli/main.cpp)
configure_file(${RANDO_DIR}/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/rando/version.hpp @ONLY)
configure_file(${RANDO_DIR}/keys/keys.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/rando/keys/keys.hpp @ONLY)

add_executable(wwhd-rando ${RANDO_SOURCES} cli/main.cpp)
target_include_directories(wwhd-rando PRIVATE ${RANDO_DIR} ${CMAKE_CURRENT_BINARY_DIR}/rando ${RANDO_DIR}/libs/yaml-cpp/include
                                              ${RANDO_DIR}/libs/base64pp/base64pp/include)
string(LENGTH "${RANDO_DIR}/" RANDO_PATH_SIZE)
# WWHD_NO_GAME_ART: the randomizer's edited game textures (title logo, icons, key chest) are not shipped (tweaks.cpp)
# WWHD_RANDO_UPSTREAM_ART=ON: only for the golden test against the official 1.2.0 (test_rando_golden.py), with the
# randomizer's assets/ from an upstream checkout in the data folder; never for a release
option(WWHD_RANDO_UPSTREAM_ART "golden test build: keep the tweaks that use upstream's assets/" OFF)
target_compile_definitions(wwhd-rando PRIVATE WWHD_APP SOURCE_PATH_SIZE=${RANDO_PATH_SIZE} YAML_CPP_STATIC_DEFINE)
if(NOT WWHD_RANDO_UPSTREAM_ART)
  target_compile_definitions(wwhd-rando PRIVATE WWHD_NO_GAME_ART)
endif()
if(MSVC)
  target_compile_options(wwhd-rando PRIVATE /W0 /EHsc /utf-8 /bigobj)
else()
  target_compile_options(wwhd-rando PRIVATE -w)
endif()
if(NOT TARGET ZLIB::ZLIB)  # (in the game's build: its zlib, the pinned one in release builds)
  if(WWHD_BUNDLED_DEPS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/WindowsDependencies.cmake)
  else()
    find_package(ZLIB REQUIRED)
  endif()
endif()
find_package(Threads REQUIRED)
target_link_libraries(wwhd-rando PRIVATE ZLIB::ZLIB Threads::Threads)

# the data folder the command reads (--data), laid out as the randomizer's CMakeLists lays out its own
set(DATA ${CMAKE_CURRENT_BINARY_DIR}/data)
add_custom_target(wwhd-rando-data ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${DATA}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DATA}/asm ${DATA}/asm_eur ${DATA}/asm_jpn ${DATA}/logic ${DATA}/customizer/Link
  COMMAND ${CMAKE_COMMAND} -E copy ${RANDO_DIR}/asm/custom_symbols.yaml ${DATA}/asm/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RANDO_DIR}/asm/patch_diffs ${DATA}/asm/patch_diffs
  COMMAND ${CMAKE_COMMAND} -E copy ${RANDO_DIR}/asm_eur/custom_symbols.yaml ${RANDO_DIR}/asm_eur/address_map.txt ${DATA}/asm_eur/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RANDO_DIR}/asm_eur/patch_diffs ${DATA}/asm_eur/patch_diffs
  COMMAND ${CMAKE_COMMAND} -E copy ${RANDO_DIR}/asm_jpn/custom_symbols.yaml ${RANDO_DIR}/asm_jpn/address_map.txt ${DATA}/asm_jpn/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RANDO_DIR}/asm_jpn/patch_diffs ${DATA}/asm_jpn/patch_diffs
  COMMAND ${CMAKE_COMMAND} -E copy ${RANDO_DIR}/logic/data/world.yaml ${RANDO_DIR}/logic/data/macros.yaml
          ${RANDO_DIR}/logic/data/item_data.yaml ${RANDO_DIR}/logic/data/location_data.yaml ${RANDO_DIR}/logic/data/area_names.yaml
          ${RANDO_DIR}/logic/data/entrance_shuffle_table.yaml ${RANDO_DIR}/logic/data/dungeon_entrance_info.yaml ${DATA}/logic/
  COMMAND ${CMAKE_COMMAND} -E copy ${RANDO_DIR}/customizer/data/Link/metadata.yaml ${RANDO_DIR}/customizer/data/Link/color_presets.yaml
          ${DATA}/customizer/Link/
  COMMAND ${CMAKE_COMMAND} -E copy ${RANDO_DIR}/LICENSE.txt ${DATA}/LICENSE-TWWHD-Randomizer.txt
  VERBATIM)
