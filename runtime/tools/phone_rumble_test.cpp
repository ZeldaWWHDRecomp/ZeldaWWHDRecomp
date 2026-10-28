#include "platform/phone_rumble.h"
#include <cassert>
#include <initializer_list>
using namespace phone_rumble;
int main() {
    assert(parse(nullptr)==kTouch && parse("1")==kTouch && parse("touch")==kTouch);
    assert(parse("0")==kOff && parse("garbage")==kOff);
    assert(parse("2")==kFallback && parse("fallback")==kFallback);
    assert(parse("3")==kDevice && parse("device")==kDevice && parse("4")==kBoth && parse("both")==kBoth);
    for (bool touch : {false,true}) for (bool motor : {false,true}) {
        assert(!allowed(kOff,touch,motor));
        assert(allowed(kTouch,touch,motor)==touch);
        assert(allowed(kFallback,touch,motor)==(touch||!motor));
        assert(allowed(kDevice,touch,motor) && allowed(kBoth,touch,motor));  // this device always
    }
    // the controller's motors: all but "This device"
    assert(controller_allowed(kOff) && controller_allowed(kTouch) && controller_allowed(kFallback) && controller_allowed(kBoth));
    assert(!controller_allowed(kDevice));
    for (int m = 0; m < kModeCount; m++) assert(*label(Mode(m)));
    assert(!touch_selected(true,false)); // visible overlay alone is not touch input (keyboard/controller)
    assert(touch_selected(true,true)); // actual touch selects the phone regardless of attached devices
    assert(!touch_selected(false,true)); // hidden controls never select phone feedback
    Refresh lease;
    assert(!lease.send(0,0)); // idle path never crosses JNI
    assert(lease.send(65535,1));
    assert(!lease.send(65535,100));
    assert(lease.send(65535,101)); // renew before 150ms expiry
    assert(lease.send(0,102)); // stopping must never wait for the refresh interval
    assert(!lease.send(0,1000));
    assert(lease.send(32768,1001));
    assert(lease.send(10000,1002)); // strength change reaches hardware immediately
}
