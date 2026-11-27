// dsweep_title.h (#690): the title sweep's A pulses on the way to the title and what losing the title means.
#include "dsweep_title.h"
#include <cassert>
#include <cstdio>

using dsweep_title::BootPulse;
using dsweep_title::Lost;

int main() {
    const BootPulse p;
    // the boot's notice screen: A from frame 120 in the first 6 frames of every 60
    assert(!dsweep_title::boot_a(p, 60, "Name", false));
    assert(dsweep_title::boot_a(p, 120, "Name", false));
    assert(dsweep_title::boot_a(p, 125, "Name", false));
    assert(!dsweep_title::boot_a(p, 126, "Name", false));
    assert(dsweep_title::boot_a(p, 300, "", false));  // a stage name not readable yet
    // the title's stage (its intro takes A as "enter the game"): never, though no actor drew there yet
    for (uint64_t f = 120; f < 2000; ++f) assert(!dsweep_title::boot_a(p, f, "sea_T", false));
    // the old behaviour (gate off): A on the title's stage until its first actor draw (the race of #690)
    BootPulse old = p;
    old.gate = false;
    assert(dsweep_title::boot_a(old, 300, "sea_T", false));
    assert(!dsweep_title::boot_a(old, 300, "sea_T", true));
    assert(!dsweep_title::boot_a(p, 300, "Name", true));
    // a denser pulse (testing: WWHD_DSWEEP_BOOT_A=4,2)
    BootPulse dense = p;
    dense.period = 4, dense.width = 2;
    assert(dsweep_title::boot_a(dense, 121, "Name", false) && !dsweep_title::boot_a(dense, 122, "Name", false));
    dense.period = 0;
    assert(!dsweep_title::boot_a(dense, 120, "Name", false));

    // the file select ("Name") the sweep did not open
    assert(dsweep_title::title_lost(false, "Name", false, false) == Lost::None);  // the boot's "Name" stage
    assert(dsweep_title::title_lost(true, "sea_T", false, true) == Lost::None);
    assert(dsweep_title::title_lost(true, "Name", false, false) == Lost::EndAttempt);
    assert(dsweep_title::title_lost(true, "Name", false, true) == Lost::Restart);
    assert(dsweep_title::title_lost(true, "Name", true, true) == Lost::None);  // the file select, visits, actors
    std::puts("dsweep_title: ok");
    return 0;
}
