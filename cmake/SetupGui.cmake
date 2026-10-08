# The graphical installer (tools/installer/gui): SDL3 + Dear ImGui (the vendored copy, with its
# official SDL3 platform and SDL_Renderer backends). Release builds turn it on (WWHD_SETUP_GUI).
#
# SDL3: macOS and Windows link it statically (the pinned SDL3 source, built with the release
# toolchain), so the setup program is one self-contained file. Linux uses the SDL3 the runtime
# build already uses (a shared library shipped in sdk/runtime, found through the rpath).
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(NOT TARGET SDL3::SDL3-static AND (APPLE OR NOT TARGET SDL3::SDL3))
  if(NOT APPLE AND NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG QUIET)
  endif()
  if(APPLE OR NOT TARGET SDL3::SDL3)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
      URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
    FetchContent_MakeAvailable(SDL3)
  endif()
endif()
if(TARGET SDL3::SDL3-static)
  set(WWHD_SETUP_SDL SDL3::SDL3-static)
else()
  set(WWHD_SETUP_SDL SDL3::SDL3)
endif()

add_executable(wwhd-setup WIN32
  tools/installer/gui/setup_gui.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(wwhd-setup PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(wwhd-setup PRIVATE imgui ${WWHD_SETUP_SDL})
set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  PROPERTIES COMPILE_OPTIONS "-w")
if(WIN32)
  # Windows: --console-setup and the bundled Python (tools\python; no download code in this program)
  target_sources(wwhd-setup PRIVATE tools/installer/gui/console_setup_win.cpp)
  wwhd_windows_resources(wwhd-setup "Wind Waker HD setup and launcher" "Wind Waker HD.exe" gui)
  if(WWHD_STRIP_RELEASE)
    target_link_options(wwhd-setup PRIVATE ${WWHD_STRIP_RELEASE})
  endif()
endif()
if(APPLE)
  target_link_libraries(wwhd-setup PRIVATE "-framework CoreGraphics")  # Shift held at start: the setup
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # the release puts the program (wind-waker-hd) at the top of the folder and SDL3 in sdk/runtime
  set_target_properties(wwhd-setup PROPERTIES INSTALL_RPATH "\$ORIGIN/sdk/runtime;\$ORIGIN" BUILD_WITH_INSTALL_RPATH TRUE)
endif()
