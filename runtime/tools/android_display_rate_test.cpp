#include "platform/android_display_rate.h"
#include <cassert>
#include <initializer_list>
using namespace android_display_rate;
int main() {
    for (int fps : {60, 120, 240}) {
        assert(requested_hz(0, fps, false) == 60);
        assert(requested_hz(2, fps, false) == 60);
        assert(requested_hz(1, fps, false) == fps);
        for (int mode : {0, 1, 2}) assert(requested_hz(mode, fps, true) == 0);
    }
    RequestState state;
    assert(!state.changed(0, 0, 60));
    assert(state.changed(1, 0, 60));
    for (int i = 0; i < 100; ++i) assert(!state.changed(1, 0, 60));
    assert(state.changed(1, 0, 120)); // settings change while playing
    assert(state.changed(1, 0, 60));
    assert(state.changed(1, 1, 60)); // replacement may reuse the native address
    assert(!state.changed(1, 1, 60));
    assert(!state.changed(0, 1, 60)); // absent surface during backgrounding
    assert(state.changed(1, 1, 60)); // resume with same address
    assert(state.changed(2, 1, 60)); // new native window
    assert(state.changed(2, 1, 0)); // clear request / opt out
    assert(!state.changed(2, 1, 0));

    Resend resend;
    assert(!resend.due(5000, 60));       // nothing sent yet
    resend.sent(0, 60);
    assert(!resend.due(10000, 60));      // 60 Hz requests are never repeated
    resend.sent(0, 120);
    assert(!resend.due(1000, 60));       // too early
    assert(!resend.due(4000, 120));      // the display follows
    assert(!resend.due(4000, 0));        // read-back unknown
    assert(resend.due(4000, 60));        // stayed at 60 Hz: once
    assert(!resend.due(5000, 60));       // again only 3 s later
    assert(resend.due(7000, 90));        // still lower: twice
    assert(!resend.due(20000, 60));      // and no more
    resend.sent(30000, 120);             // a new request (setting or surface change) starts over
    assert(resend.due(33000, 60));
}
