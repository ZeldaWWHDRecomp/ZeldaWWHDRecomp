# Smooth frame pacing and second-thread recording of the Vulkan renderer (runtime/src/gfx/vulkan/smooth_defaults.h):
# display pacing (display_timing.h), the late TV present acknowledgment (present_ack.h), submission pool resets on a
# reset thread (pool_reset.h), multi-threaded recording for the record worker (record_mt.h, record_tokens.h,
# pipeline_future.h) and the frame breakdown trace (frame_breakdown.h). Engine code, in every build with the Vulkan
# renderer (the normal build and the decomp build); on by default only on phones and the Steam Deck
# (smooth_defaults.cpp), each piece with its own environment variable. OFF compiles none of it (the renderer as before).
# The code lives in its own files; the shared files (backend.cpp, draw.cpp, record_worker.cpp, pod_commands.cpp,
# gx2_core.cpp, interp.cpp, overlay.cpp) only call it inside #ifdef WWHD_VK_SMOOTH.
option(WWHD_VK_SMOOTH "Vulkan renderer: smooth frame pacing and multi-threaded recording (on by default for phones and the Steam Deck only)" ON)

function(wwhd_vk_smooth target)
  if(NOT WWHD_VK_SMOOTH)
    return()
  endif()
  set(dir "${CMAKE_SOURCE_DIR}/runtime/src")
  set_property(SOURCE "${dir}/gfx/vulkan/pool_reset.cpp" "${dir}/gfx/vulkan/record_mt.cpp"
               "${dir}/gfx/vulkan/record_tokens.cpp" "${dir}/gfx/vulkan/pipeline_future.cpp"
               "${dir}/gfx/vulkan/frame_breakdown.cpp" "${dir}/gfx/vulkan/display_timing.cpp"
               "${dir}/gfx/vulkan/present_ack.cpp" "${dir}/gfx/vulkan/smooth_defaults.cpp"
               "${dir}/gfx/vulkan/backend.cpp" "${dir}/gfx/vulkan/draw.cpp" "${dir}/gx2/gx2_core.cpp"
               "${dir}/overlay/overlay.cpp" "${dir}/interp.cpp"
               APPEND PROPERTY COMPILE_DEFINITIONS WWHD_VK_SMOOTH=1)
  # the hooks in two files the unit tests also compile (without them): for the game target only
  set_property(SOURCE "${dir}/gfx/vulkan/pod_commands.cpp" "${dir}/gfx/vulkan/record_worker.cpp"
               APPEND PROPERTY COMPILE_DEFINITIONS "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,${target}>:WWHD_VK_SMOOTH=1>")
  message(STATUS "Vulkan smooth frame pacing and multi-threaded recording compiled in (defaults: phones and the Steam Deck)")
endfunction()
