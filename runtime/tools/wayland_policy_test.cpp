#include "platform/wayland_policy.h"
#include <cassert>
#include <cstdio>
int main() {
    using wayland_policy::disable_nvidia_explicit_sync;
    assert(disable_nvidia_explicit_sync(true, false));
    assert(!disable_nvidia_explicit_sync(true, true)); // user set 0, 1, or empty
    assert(!disable_nvidia_explicit_sync(false, false)); // X11, Windows, Cocoa
    assert(!disable_nvidia_explicit_sync(false, true));
    puts("wayland_policy_test: native Wayland default and user overrides passed");
}
