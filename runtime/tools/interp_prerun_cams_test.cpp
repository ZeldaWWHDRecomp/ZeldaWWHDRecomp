#include "interp_prerun_cams.h"
#include <cassert>
using namespace interp::prerun_cams;
int main() {
    // One camera per pass: one pre-run per pass, as before.
    Cams a;
    assert(a.take(false, 0x47C6CE10, true) == Kind::First);
    assert(a.take(true, 0x47C6CE10, true) == Kind::None);
    assert(a.take(false, 0x47C6CE10, true) == Kind::First);  // next pass

    // Two cameras on a pass (the sea): the second gets its own pre-run, once.
    Cams b;
    assert(b.take(false, 0x47C6CE10, true) == Kind::First);
    assert(b.take(true, 0x47C6C2D8, true) == Kind::More);
    assert(b.take(true, 0x47C6C2D8, true) == Kind::None);
    assert(b.take(true, 0x47C6CE10, true) == Kind::None);
    // next pass starts over: both again
    assert(b.take(false, 0x47C6CE10, true) == Kind::First);
    assert(b.take(true, 0x47C6C2D8, true) == Kind::More);

    // Switch off (WWHD_INTERP_CULL_ALL_CAMS=0): only the first camera, the old behaviour.
    Cams c;
    assert(c.take(false, 0x47C6CE10, false) == Kind::First);
    assert(c.take(true, 0x47C6C2D8, false) == Kind::None);

    // Bounded: at most kMax cameras per pass.
    Cams d;
    assert(d.take(false, 0x1000, true) == Kind::First);
    for (uint32_t i = 1; i < Cams::kMax; i++) assert(d.take(true, 0x1000 + i, true) == Kind::More);
    assert(d.take(true, 0x2000, true) == Kind::None);
    return 0;
}
