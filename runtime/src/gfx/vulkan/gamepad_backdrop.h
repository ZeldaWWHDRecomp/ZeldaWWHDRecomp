#pragma once
#include <cstdint>
namespace gfxvk {
struct Surface;
void gamepad_backdrop_scope(const uint32_t*, bool begin);
void gamepad_backdrop_reset();
// Draw replay remains private: it never enters the normal surface cache.
bool gamepad_backdrop_replaying();
bool gamepad_backdrop_capture_active();
Surface* gamepad_backdrop_replay_target();
bool gamepad_backdrop_begin_replay(Surface* source, bool compatible, bool opaque=false);
void gamepad_backdrop_end_replay(bool drawn);
void gamepad_backdrop_scan(Surface* source);
void gamepad_backdrop_publish();
Surface* gamepad_backdrop_image();
}
