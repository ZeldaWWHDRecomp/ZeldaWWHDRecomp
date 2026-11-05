#pragma once
/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
// GameCube player models in HD (gcmodel.h): big-endian reading and writing, Yaz0, SARC (the game's packs) and RARC
// (the GameCube's Link.arc). A port of the fork's tools/gcmodels (nintendo.py, j3d.rarc, yaz0.cpp).
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace gcmodel {

using Bytes = std::vector<uint8_t>;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline int16_t bes16(const uint8_t* p) { return (int16_t)be16(p); }
inline float bef32(const uint8_t* p) {
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
inline void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
inline void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }
inline void putf32(uint8_t* p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    put32(p, u);
}

// `n` bytes at `o` of `c`, or Error (the GameCube side comes from the player's files: never read past them)
inline const uint8_t* at(const Bytes& c, size_t o, size_t n) {
    if (o > c.size() || n > c.size() - o) throw Error("model data out of range");
    return c.data() + o;
}

// (at most kYaz0Max bytes unpacked: a damaged header must not ask for gigabytes)
constexpr uint32_t kYaz0Max = 256u << 20;
Bytes yaz0_decode(const Bytes& in);
// greedy matches from hash chains over the 4 KB window, as Nintendo's own files; `align` goes into the header
Bytes yaz0_encode(const Bytes& in, uint32_t align);

// a SARC's files by name (data copied)
std::map<std::string, Bytes> sarc_read(const Bytes& d);
// the SARC with some files' data replaced; tables, order and names as they were, each file at a multiple of `align`
Bytes sarc_replace(const Bytes& d, const std::map<std::string, Bytes>& repl, uint32_t align = 0x2000);
// a RARC's files by name (Yaz0 undone first)
std::map<std::string, Bytes> rarc_read(const Bytes& d);

}  // namespace gcmodel
