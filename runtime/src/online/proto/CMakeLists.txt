# The online protocol library, shared by the game client, wwhd-online-server and wwhd-online-bots.
# Plain C++20 and OS sockets; no includes from the rest of runtime/ (the server builds on its own).
# Include as "proto/client.h" etc.
cmake_minimum_required(VERSION 3.20)

if(NOT TARGET wwhd_online_proto)
  add_library(wwhd_online_proto STATIC
    channel.cpp
    client.cpp
    crypto_plain.cpp
    handshake.cpp
    messages.cpp
    protocol.cpp
    sha256.cpp
    udp.cpp)
  target_include_directories(wwhd_online_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(wwhd_online_proto PUBLIC cxx_std_20)
  set_target_properties(wwhd_online_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(WIN32)
    target_link_libraries(wwhd_online_proto PUBLIC ws2_32 bcrypt)
  endif()
endif()
