#pragma once
// The TV picture nobody sees (gpusave). WWHD_VK_SKIP_HIDDEN_TV (on by default; =0 turns it off): while no output shows the TV
// picture (the presenter's actual layout of the previous frame: no surface drew the TV scan in a non-empty box; e.g.
// One screen (handheld) mode in Off-TV Play, where the TV buffer holds only the game's "playing on the GamePad"
// screen), the game's TV copy to the scan buffer (copy_to_scan target 1: a 1080p resample) is left pending instead of
// recorded. The TV scan keeps its older picture, which nothing shows.
//
// Each swap decides from this frame's layout: the quads every surface will draw (the main window, the GamePad window
// on desktop hosts, Android's second display and its swapped source, present dumps share them). If any draws the TV
// scan in a non-empty box, the pending copy is recorded there, in the same frame (one-screen mode off, the game back in
// TV mode with Minus, PiP, a second display taking the GamePad). A TV capture (request_tv_dump), a frame dump of the
// TV scan and a screenshot record it too. The late copy is the same picture when the TV colour buffer is unchanged
// since the game's copy: the very surface the game copied (the game may point the same colour buffer at
// another surface afterwards), not written since (writeSeq); a changed or gone one is counted and logged.
// Never deferred: stereo, generated frames (reproj), the headless composition diagnostic.
// WWHD_VK_SKIP_HIDDEN_TV=2 (test): frame dumps write the TV scan as it is (the older picture), to prove the window
// does not change.
#include <cstdint>

namespace gfxvk {
struct Surface;
namespace hidden_tv {
bool on();
bool stale_dumps();  // (test mode 2)
// copy_to_scan(cb, 1): true = the copy is left pending (no output showed the TV picture last frame)
bool defer(uint32_t colorBuffer, Surface* source);
bool pending();
// the pending copy, taken: the surface the game copied (null: none, or it no longer exists); `unchanged`: not
// written since. The caller records it (copy_to_scan's work) and publishes the TV scan.
Surface* take(bool& unchanged);
// a pending copy recorded late: why, and whether its source was unchanged
void count_late(const char* why, bool exact);
// swap, after the layout: whether any surface shows the TV scan this frame (decides the next frame's copy)
void layout_shown(bool shown);
bool shown_last();  // (the Android display smoke test checks it)
// end of the swap: a copy still pending is dropped (not needed)
void end_frame();
}  // namespace hidden_tv
// backend.cpp: records the pending TV copy now and publishes the TV scan (no-op without one)
void hidden_tv_materialize(const char* why);
// hidden_tv.cpp: this frame's quads (the current present plan) of every surface presented draw the TV scan in a
// non-empty box; `drcWindow`: the GamePad surface is presented too
bool hidden_tv_scan_shown(bool drcWindow);
}  // namespace gfxvk
