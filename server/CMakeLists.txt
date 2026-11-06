# wwhd-online-server: the standalone online server (docs/online-server.md).
# Builds on its own (cmake -S server -B build) or from the top level with -DWWHD_ONLINE=ON.
cmake_minimum_required(VERSION 3.20)
project(wwhd_online_server CXX)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WWHD_ONLINE_SANITIZE "build the server and its tests with ASan/UBSan" OFF)
option(WWHD_ONLINE_STATIC "link wwhd-online-server statically (Linux, for the Docker image)" OFF)
if(WWHD_ONLINE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src/online/proto ${CMAKE_CURRENT_BINARY_DIR}/proto)

find_package(Threads REQUIRED)

enable_testing()
add_executable(wwhd_online_proto_test tests/proto_test.cpp)
target_link_libraries(wwhd_online_proto_test PRIVATE wwhd_online_proto)
add_test(NAME online_proto COMMAND wwhd_online_proto_test)

add_library(wwhd_online_server_core STATIC
  src/admin.cpp
  src/config.cpp
  src/party.cpp
  src/server.cpp
  src/shard.cpp)
target_include_directories(wwhd_online_server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wwhd_online_server_core PUBLIC wwhd_online_proto Threads::Threads)

add_executable(wwhd-online-server src/main.cpp)
target_link_libraries(wwhd-online-server PRIVATE wwhd_online_server_core)
if(WWHD_ONLINE_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(wwhd-online-server PRIVATE -static)
endif()

add_executable(wwhd_online_server_test tests/server_test.cpp)
target_link_libraries(wwhd_online_server_test PRIVATE wwhd_online_server_core)
add_test(NAME online_server COMMAND wwhd_online_server_test ${CMAKE_CURRENT_BINARY_DIR})

# the fuzz target: libFuzzer when WWHD_ONLINE_FUZZ=ON (a clang with libFuzzer), else the built-in random driver
option(WWHD_ONLINE_FUZZ "build the fuzz target with libFuzzer" OFF)
add_executable(wwhd_online_fuzz tests/fuzz_server.cpp)
target_link_libraries(wwhd_online_fuzz PRIVATE wwhd_online_server_core)
if(WWHD_ONLINE_FUZZ)
  target_compile_definitions(wwhd_online_fuzz PRIVATE WWHD_LIBFUZZER)
  target_compile_options(wwhd_online_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(wwhd_online_fuzz PRIVATE -fsanitize=fuzzer)
endif()
