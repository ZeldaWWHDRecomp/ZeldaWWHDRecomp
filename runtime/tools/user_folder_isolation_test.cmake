# ctest gives every test a user folder (WWHD_USER_DIR, CMakeLists.txt wwhd_isolate_test_user_folders), and a
# run with one writes nothing into the default folder: with HOME, APPDATA and XDG_CONFIG_HOME pointed into
# WORK/home, that stays empty. button_glyphs_test saves controls.json (the Automatic face buttons follow the
# pad): it must land in this test's user folder.
#   WWHD_USER_DIR=dir cmake -DWORK=dir -DBUTTON_GLYPHS=exe -P user_folder_isolation_test.cmake
if(NOT DEFINED ENV{WWHD_USER_DIR} OR "$ENV{WWHD_USER_DIR}" STREQUAL "")
  message(FATAL_ERROR "ctest gave this test no user folder (WWHD_USER_DIR)")
endif()
set(user "$ENV{WWHD_USER_DIR}")
file(REMOVE_RECURSE "${WORK}" "${user}")
file(MAKE_DIRECTORY "${WORK}/home")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "HOME=${WORK}/home" "APPDATA=${WORK}/home/AppData"
                        "XDG_CONFIG_HOME=${WORK}/home/.config" --unset=WWHD_CONTROLS
                        "${BUTTON_GLYPHS}"
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "button_glyphs_test failed (${rc}): ${out}${err}")
endif()
file(GLOB_RECURSE leaked LIST_DIRECTORIES true "${WORK}/home/*")
if(leaked)
  message(FATAL_ERROR "written outside the user folder: ${leaked}")
endif()
if(NOT EXISTS "${user}/controls.json")
  message(FATAL_ERROR "controls.json was not saved in the user folder")
endif()
message(STATUS "user folder only: ${user}/controls.json")
file(REMOVE_RECURSE "${WORK}" "${user}")
