# The online protocol library, shared by the game client, wwhd-online-server and wwhd-online-bots.
# Plain C++20 and OS sockets; no includes from the rest of runtime/ (the server builds on its own).
# Include as "proto/client.h" etc.
cmake_minimum_required(VERSION 3.20)

# the crypto backend behind crypto.h: "monocypher" (the vendored third_party/monocypher; the default) or "plain"
# (no encryption, no authentication: protocol debugging only)
set(WWHD_ONLINE_CRYPTO "monocypher" CACHE STRING "online protocol crypto backend: monocypher or plain")
set_property(CACHE WWHD_ONLINE_CRYPTO PROPERTY STRINGS monocypher plain)

if(NOT TARGET wwhd_online_proto)
  set(_crypto crypto_monocypher.cpp third_party/monocypher/monocypher.c)
  if(WWHD_ONLINE_CRYPTO STREQUAL "plain")
    set(_crypto crypto_plain.cpp)
  elseif(NOT WWHD_ONLINE_CRYPTO STREQUAL "monocypher")
    message(FATAL_ERROR "WWHD_ONLINE_CRYPTO must be monocypher or plain")
  endif()
  enable_language(C)
  add_library(wwhd_online_proto STATIC
    channel.cpp
    client.cpp
    crypto_common.cpp
    ${_crypto}
    handshake.cpp
    messages.cpp
    protocol.cpp
    sha256.cpp
    tcp.cpp
    udp.cpp)
  target_include_directories(wwhd_online_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(wwhd_online_proto PUBLIC cxx_std_20)
  set_target_properties(wwhd_online_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(WWHD_ONLINE_CRYPTO STREQUAL "monocypher")
    target_compile_definitions(wwhd_online_proto PUBLIC WWHD_ONLINE_MONOCYPHER=1)
  endif()
  if(WIN32)
    target_link_libraries(wwhd_online_proto PUBLIC ws2_32 bcrypt)
  endif()
endif()
