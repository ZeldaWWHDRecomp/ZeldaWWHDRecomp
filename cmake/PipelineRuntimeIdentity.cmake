# Local Vulkan recipes must not survive changes to the code that interprets them.
# Hash public runtime sources, never generated game code or local cache data.
function(wwhd_pipeline_runtime_identity source_root output)
  # Source files only: any other file there (Finder's .DS_Store, an editor's swap file) re-ran cmake and
  # recompiled the Vulkan backend when it came or went, though no code changed.
  set(patterns)
  foreach(dir runtime/src runtime/include runtime/third_party/cemu)
    foreach(extension c cpp h hpp inc mm)
      list(APPEND patterns "${source_root}/${dir}/*.${extension}")
    endforeach()
  endforeach()
  file(GLOB_RECURSE inputs CONFIGURE_DEPENDS ${patterns})
  list(APPEND inputs "${source_root}/CMakeLists.txt" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
  list(SORT inputs)
  set(material "pipeline-runtime-1\n")
  foreach(input IN LISTS inputs)
    if(NOT IS_DIRECTORY "${input}")
      # Content edits, additions and removals all trigger automatic reconfiguration.
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
      file(RELATIVE_PATH relative "${source_root}" "${input}")
      file(SHA256 "${input}" digest)
      string(APPEND material "${relative}\n${digest}\n")
    endif()
  endforeach()
  string(SHA256 identity "${material}")
  set(${output} "${identity}" PARENT_SCOPE)
endfunction()
