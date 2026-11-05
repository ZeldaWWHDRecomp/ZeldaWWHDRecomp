// The ships' way back to the sea (mods/shop_ship.h): the sea room of a position on the 7x7 grid, the stages it
// applies to, and that a valid stored room is kept.
#include "mods/shop_ship.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace shop_ship;
    // room 1 is the north-west corner, rooms count west to east, then north to south, 100000 units each
    assert(sea_room(-350000, -350000) == 1);
    assert(sea_room(-250001, -350000) == 1 && sea_room(-250000, -350000) == 2);
    assert(sea_room(349999, -350000) == 7);
    assert(sea_room(-350000, -250000) == 8);
    assert(sea_room(0, 0) == 25);  // the middle square
    assert(sea_room(349999, 349999) == 49);
    // off the grid: the nearest edge square; no number at all: the middle
    assert(sea_room(-1e7, -1e7) == 1 && sea_room(1e7, 1e7) == 49);
    assert(sea_room(NAN, 0) == 25 && sea_room(INFINITY, -INFINITY) == 25);
    // the ships entered from the sea
    assert(ship_stage("Obshop") && ship_stage("Abship"));
    assert(!ship_stage("sea") && !ship_stage("Obshops") && !ship_stage("") && !ship_stage("obshop"));
    // a stored room 0..63 stays; -1 (0xFF) and other invalid ones come from the position
    uint8_t room = 99;
    assert(!repair(0, 0, 0, room) && room == 99);
    assert(!repair(13, 0, 0, room) && !repair(63, 0, 0, room) && room == 99);
    assert(repair(0xFF, 0, 0, room) && room == 25);
    assert(repair(64, -350000, -350000, room) && room == 1);
    assert(repair(0x80, 349999, 349999, room) && room == 49);
}
