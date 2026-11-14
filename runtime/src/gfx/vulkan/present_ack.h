// Late acknowledgment of the TV present submission (WWHD_VK_LATE_PRESENT_ACK=1, off by default; with the record
// worker; on by default on phones and the Steam Deck, smooth_defaults.h). Every build with cmake WWHD_VK_SMOOTH.
//
// The TV's present submission (backend.cpp submit with the acquire and finished semaphores) goes to the record worker
// like every other; the render thread then waited for the worker to have actually submitted it (it replays the whole
// frame first) before going on, because vkQueuePresentKHR must come after the submission that signals the finished
// semaphore. Measured on a Xiaomi 14 at the cliff: 1.5 ms of render-thread time every frame, while 85 % of the frames
// are then held by even pacing for ~3 ms anyway. With the switch the render thread goes on at once and waits only
// right before that frame's vkQueuePresentKHR (held or not), when the worker has usually long submitted it. The
// isolated GamePad snapshot handshake (Android) keeps the old wait.
#pragma once
#ifdef WWHD_VK_SMOOTH
#include <cstdint>

namespace gfxvk::present_ack {
bool enabled();
// present(): the next submission is the TV present submission (ON: may be deferred); cleared by take_armed()
void arm(bool on);
bool take_armed();
// submit(): the TV present submission SLOT (its SERIAL) is acknowledged later, by settle()
void defer(void* slot, uint64_t serial);
// right before the TV's vkQueuePresentKHR: RESOLVE(slot, serial) for a deferred submission (waits for the worker
// if it has not submitted it yet); nothing when none is deferred
void settle(void (*resolve)(void* slot, uint64_t serial));
void report(double frames);
}  // namespace gfxvk::present_ack
#endif
