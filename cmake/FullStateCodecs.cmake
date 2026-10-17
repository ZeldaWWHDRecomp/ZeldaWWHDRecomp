# zstd is already pinned by Zstd.cmake. Raw liblz4 is also needed on Apple
# to read portable v2 full states; Apple Compression remains for legacy v1.
if(NOT TARGET lz4_static AND NOT WWHD_BUNDLED_DEPS)
  find_path(STATE_LZ4_INCLUDE_DIR lz4.h)
  find_library(STATE_LZ4_LIBRARY NAMES lz4)
endif()
if(NOT TARGET lz4_static AND (WWHD_BUNDLED_DEPS OR NOT STATE_LZ4_INCLUDE_DIR OR NOT STATE_LZ4_LIBRARY))
  include(FetchContent)
  set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
  set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
  set(LZ4_BUNDLED_MODE ON)
  FetchContent_Declare(lz4
    URL https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz
    URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
    SOURCE_SUBDIR build/cmake)
  FetchContent_MakeAvailable(lz4)
endif()
add_library(wwhd_state_codecs INTERFACE)
target_link_libraries(wwhd_state_codecs INTERFACE wwhd_zstd)
if(TARGET lz4_static)
  target_link_libraries(wwhd_state_codecs INTERFACE lz4_static)
else()
  target_include_directories(wwhd_state_codecs INTERFACE ${STATE_LZ4_INCLUDE_DIR})
  target_link_libraries(wwhd_state_codecs INTERFACE ${STATE_LZ4_LIBRARY})
endif()
