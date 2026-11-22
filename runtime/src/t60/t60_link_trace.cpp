// Debug aid of the true 60 pairs (decomp build only): WWHD_LINK_TRACE's line gets Link's base foot and both toes.
// mBaseFootIdx (+0x68D8: 0 right, 1 left, 2 without old-frame data) is the foot posMoveFromFootPos moves Link by;
// mFootData[i].mToePos (+0x73F0 + i * 0x118 + 0x18, model space) is the toe pose its foot travel is measured against
// (pgrab's frame-120 stop, the base-foot switch question). Appended after the attention columns, so
// cliff_check (columns 3-17) and the earlier readers are unaffected. Column 32: m3598 (+0x69F0), posMoveFromFootPos's
// foot-travel weight r (speedF = r x foot travel + (1 - r) x mNormalSpeed), for compare60's foot-travel term.
// Columns 33-35 (pkk1_2): the lock-on actor's eyePos (fopAc_ac_c +0x37C), the point
// setShapeAngleToAtnActor turns Link to (cLib_targetAngleY(current.pos, eyePos)); "- - -" when no lock-on actor.
#include <cstdint>
#include <cstdio>
#include "ppc.h"

void t60_link_trace_feet(FILE* f, uint32_t link) {
    fprintf(f, " %u", (unsigned)ld8(link + 0x68D8));
    for (uint32_t i = 0; i < 2; i++) {
        const uint32_t toe = link + 0x73F0 + i * 0x118 + 0x18;
        fprintf(f, " %.4f %.4f", ldf32(toe), ldf32(toe + 8));
    }
    fprintf(f, " %.6f", ldf32(link + 0x69F0));
    const uint32_t lock = ld32(link + 0x65B0);
    if (lock >= 0x10000000 && lock < 0x50000000)
        fprintf(f, " %.3f %.3f %.3f", ldf32(lock + 0x37C), ldf32(lock + 0x380), ldf32(lock + 0x384));
    else
        fprintf(f, " - - -");
}
