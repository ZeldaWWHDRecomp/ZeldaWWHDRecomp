#pragma once
#include <cstdint>
namespace gfxvk {
struct Surface;
void gamepad_backdrop_scope(const uint32_t*, bool begin);
void gamepad_backdrop_reset();
void gamepad_backdrop_debug(const char* event,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0);
void gamepad_backdrop_invalidate(Surface* source);
// Draw replay remains private: it never enters the normal surface cache.
bool gamepad_backdrop_replaying();
bool gamepad_backdrop_capture_active();
// regs is the (mutable) gx2 register file the open pane scope was opened with
bool gamepad_backdrop_scope_registers(const uint32_t* regs);
Surface* gamepad_backdrop_replay_target();
bool gamepad_backdrop_begin_replay(Surface* source, bool compatible, bool opaque=false);
void gamepad_backdrop_end_replay(bool drawn);
void gamepad_backdrop_scan(Surface* source);
void gamepad_backdrop_publish();
// once per presentation: releases captures when the option is off or after an idle period
void gamepad_backdrop_maintain();
Surface* gamepad_backdrop_image();
}
