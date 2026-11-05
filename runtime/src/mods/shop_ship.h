// Beedle's Shop Ship (and the other ships entered from the sea): the sea room the way out sails back to.
//
// Leaving the shop closed the game with the game's own assertion (d_stage.cpp:5179,
// "dComIfGp_getIkadaShipBeforeRoomId() >= 0 && dComIfGp_getIkadaShipBeforeRoomId() < 64"). Entering
// (025C38B8, the scene change of an exit: types 62 Obshop, 59 Abship) stores the sea room the ship is in
// (play +0x5D16, from the room of the collision Link touched) and the ship's position (+0x5D18, +0x5D20);
// leaving (type 61) sails back to that room of "sea". A room of -1 (the collision gave none) was kept, and
// the way out halted the game. While Link is inside such a ship the stored room is made sure of: where it
// isn't 0..63 it comes from the stored ship position on the 7x7 grid of sea rooms (100000 units each, room
// 1 at the north-west corner), as the fork does at the reading instruction (an instruction hook there would
// change the generated game code; this runs from the frame's mods service instead, interp.cpp).
//
// From Nycolazs/WindWakerHD-Android-Plus f9896fb and f001ff5 via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60, runtime/src/mods/shop_ship.cpp), MPL-2.0.
#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>

namespace shop_ship {
// the sea room (1..49) of a position on the sea
inline uint8_t sea_room(double x, double z) {
    auto cell = [](double v) {
        if (!std::isfinite(v)) return 3;
        int k = (int)std::floor((v + 350000.0) / 100000.0);
        return k < 0 ? 0 : k > 6 ? 6 : k;
    };
    return (uint8_t)(1 + cell(x) + 7 * cell(z));
}
// the stages entered from a ship on the sea, whose way out reads the stored sea room
inline bool ship_stage(std::string_view stage) { return stage == "Obshop" || stage == "Abship"; }
// the room the way out will use: the stored one where valid, else the room of the stored ship position
inline bool repair(uint8_t stored, double x, double z, uint8_t& room) {
    if (stored < 64) return false;
    room = sea_room(x, z);
    return true;
}
}  // namespace shop_ship
