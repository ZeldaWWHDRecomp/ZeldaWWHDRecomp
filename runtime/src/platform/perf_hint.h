// Android performance hints (ADPF): the threads that make each frame tell the system how long a
// frame took against its 33 ms budget, so the CPU governor raises their cores' clocks when the game
// falls behind (without it, a phone left the render thread's prime core at about 1 GHz).
// No-ops elsewhere.
#pragma once

namespace perf_hint {
void add_current_thread();  // a thread whose work makes the frames (render thread, game main thread, record worker)
void remove_current_thread();  // such a thread before it ends (the hint session is made anew without it)
void frame_done();          // render thread, once per presented frame
// Android: 60 fps (frame interpolation) as chosen by the player. frame_done() pauses and resumes the
// interpolation itself where the phone cannot draw 60 frames a second or runs hot, so interp::mode()
// says what runs now and this what was chosen (and is saved). A mode set elsewhere (settings
// overlay) counts as a new choice.
bool fps60_chosen();
void set_fps60_chosen(bool on);
}
