#include "../src/interp_chain.h"
#include <cassert>

int main() {
    interp::ChainCount c;
    // nothing filled yet: the game's own count
    assert(c.drawn(0x1000, 5, 12) == 12);
    // the chain flies out: step S had 4 links, S+1 has 12; a blended pass fills 7
    c.filled(0x1000, 6, 7);
    assert(c.drawn(0x1000, 6, 12) == 7);   // links 7..11 are not drawn from their old matrices
    // drawn several times on the pass (colour, depth, shadow): the same count each time
    assert(c.drawn(0x1000, 6, 12) == 7);
    // the chain pulls in: the blended count may be above the exact one, all of it filled
    c.filled(0x1000, 9, 15);
    assert(c.drawn(0x1000, 9, 12) == 15);
    // the next pass (record pass: filled with the exact count by the game) and another shape
    assert(c.drawn(0x1000, 10, 12) == 12);
    assert(c.drawn(0x2000, 9, 3) == 3);
    // no chain at all on the blended pass
    c.filled(0x1000, 11, 0);
    assert(c.drawn(0x1000, 11, 12) == 0);
    // the shot starts: the step before had no chain (angle 0, or the last shot's): the new direction
    assert(interp::chain_angle(0, 0, 4, 21736, 0.25f) == 21736);
    assert(interp::chain_angle(0, -1746, 4, 21736, 0.5f) == 21736);
    // the chain is back in the hookshot: the last direction it had
    assert(interp::chain_angle(9, 21912, 0, 0, 0.75f) == 21912);
    // a chain on both steps: blended, the short way round
    assert(interp::chain_angle(10, 1000, 12, 3000, 0.5f) == 2000);
    assert(interp::chain_angle(10, 32000, 12, -32000, 0.5f) == -32768 || interp::chain_angle(10, 32000, 12, -32000, 0.5f) == 32767);
    return 0;
}
