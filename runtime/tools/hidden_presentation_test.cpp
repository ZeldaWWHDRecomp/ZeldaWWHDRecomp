// gfx/hidden_presentation.h: hidden runs present nothing (also when Android brings the app to the foreground);
// the proof switch only works together with hidden windows.
#include "gfx/hidden_presentation.h"
#include <cassert>
#include <cstdio>

int main() {
    using gfx::hidden::parse;
    assert(parse(nullptr, nullptr).presents());          // play: presents
    assert(parse("0", nullptr).presents());
    assert(parse("", "1").presents());                   // not hidden: the proof switch changes nothing
    assert(!parse("", "1").test_present);
    assert(!parse("1", nullptr).presents());             // hidden: never presents
    assert(!parse("1", "0").presents());
    assert(parse("1", nullptr).hidden);
    assert(parse("1", "1").presents());                  // proof run: hidden windows present to their swapchains
    assert(parse("1", "1").test_present);
    std::puts("hidden_presentation_test: ok");
    return 0;
}
