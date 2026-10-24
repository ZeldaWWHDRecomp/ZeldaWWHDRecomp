# Android only. The runtime is loaded on demand; ordinary phones never open it.
include(FetchContent)
FetchContent_Declare(wwhd_openxr_headers
  URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.63.tar.gz
  URL_HASH SHA256=5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f
  SOURCE_SUBDIR headers-only)
FetchContent_MakeAvailable(wwhd_openxr_headers)
FetchContent_Declare(wwhd_cgltf
  URL https://github.com/jkuhlmann/cgltf/archive/refs/tags/v1.15.tar.gz
  URL_HASH SHA256=84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a
  SOURCE_SUBDIR headers-only)
FetchContent_MakeAvailable(wwhd_cgltf)
target_sources(wwhd PRIVATE runtime/src/platform/quest_theater.cpp)
set_property(SOURCE runtime/src/platform/quest_theater.cpp runtime/src/gfx/vulkan/backend.cpp
  runtime/src/gfx/vulkan/android_driver.cpp runtime/src/platform/input_sdl.cpp
  APPEND PROPERTY COMPILE_DEFINITIONS WWHD_QUEST_THEATER=1)
set_property(SOURCE runtime/src/platform/quest_theater.cpp APPEND PROPERTY
  INCLUDE_DIRECTORIES ${wwhd_openxr_headers_SOURCE_DIR}/include ${wwhd_cgltf_SOURCE_DIR})
target_link_libraries(wwhd PRIVATE EGL GLESv3)
