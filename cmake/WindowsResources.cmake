# Windows: version information (VERSIONINFO) and an application manifest for the programs a release ships
# or builds (Wind Waker HD.exe, wwhd-extract.exe, the game). Unsigned programs without them look like
# anonymous droppers to antivirus heuristics (issue #58).
#
#   wwhd_windows_resources(TARGET DESCRIPTION ORIGINAL_FILENAME gui|console)
#
# The version comes from WWHD_VERSION (release builds pass the tag, e.g. v0.2.7): its first three numbers
# become FILEVERSION, the text itself the version strings ("dev" without one).
if(NOT WIN32)
  function(wwhd_windows_resources)
  endfunction()
  return()
endif()

enable_language(RC)

function(wwhd_windows_resources target description original kind)
  set(WWHD_RC_DESCRIPTION "${description}")
  set(WWHD_RC_ORIGINAL "${original}")
  get_filename_component(WWHD_RC_INTERNAL "${original}" NAME_WE)
  set(WWHD_RC_VERSION_STRING "${WWHD_VERSION}")
  if(NOT WWHD_RC_VERSION_STRING)
    set(WWHD_RC_VERSION_STRING "dev")
  endif()
  string(REGEX REPLACE "[\"\\\\]" "" WWHD_RC_VERSION_STRING "${WWHD_RC_VERSION_STRING}")
  set(WWHD_RC_VERSION_COMMAS "0,0,0,0")
  if(WWHD_RC_VERSION_STRING MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(WWHD_RC_VERSION_COMMAS "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
  endif()
  set(manifest "${CMAKE_SOURCE_DIR}/cmake/windows/${kind}.manifest")
  if(CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR MSVC)
    # link.exe / lld-link embed the manifest themselves (a second one in the .rc would collide)
    set(WWHD_RC_MANIFEST_LINE "")
    target_sources(${target} PRIVATE "${manifest}")
  else()
    # MinGW (llvm-mingw, MSYS2): the manifest as resource 1 of type RT_MANIFEST (24)
    file(TO_CMAKE_PATH "${manifest}" manifest_path)
    set(WWHD_RC_MANIFEST_LINE "1 24 \"${manifest_path}\"")
  endif()
  set(rc "${CMAKE_BINARY_DIR}/generated/${target}.rc")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/windows/app.rc.in" "${rc}" @ONLY)
  target_sources(${target} PRIVATE "${rc}")
endfunction()
