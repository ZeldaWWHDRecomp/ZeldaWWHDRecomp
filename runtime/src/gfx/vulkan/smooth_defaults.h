// Defaults of the decomp build's fast mode (cmake WWHD_NATIVE_DRAW) on phones and the Steam Deck, where the render
// thread limits the frame rate: graphics recorded on a second thread with multi-threaded recording (record_worker.h,
// record_mt.h), 8 submission slots, the late TV present acknowledgment (present_ack.h) and, with the setting "Smooth
// frame pacing" (on by default), display pacing (display_timing.h); on the Steam Deck also even frame pacing, as on
// Android (interp.cpp). Each environment variable still wins (WWHD_VK_RECORD_WORKER, WWHD_VK_RECORD_MT,
// WWHD_VK_SUBMISSION_POOL, WWHD_VK_LATE_PRESENT_ACK, WWHD_VK_DISPLAY_PACING, WWHD_INTERP_EVEN_PACING); every other
// device and build keeps the old defaults.
// Measured on a Xiaomi 14 at the cliff (2026-11-13, against the single-thread renderer): render thread 17.3 -> 12.7 ms
// per frame; on screen 25 % -> 1.2-1.4 % of the frame intervals not two refreshes of the 120 Hz screen, 374-415 ->
// 5-14 dropped in-between frames a minute; present call to screen 16 -> 24-25 ms.
#pragma once
#ifdef WWHD_NATIVE_DRAW
namespace gfxvk::smooth_defaults {
bool steam_deck();       // Linux: Steam's SteamDeck=1, or the DMI board of Valve's Jupiter / Galileo
bool device();           // Android or the Steam Deck
bool record_worker();    // Graphics > Advanced "Record graphics on a second thread" when the player never chose
unsigned record_mt();    // WWHD_VK_RECORD_MT when it is not set (0: off)
unsigned submission_slots();  // WWHD_VK_SUBMISSION_POOL when neither variable is set (0: the old default)
bool late_present_ack();
bool smooth_pacing_setting();  // Graphics > Advanced "Smooth frame pacing" (hostui key vkSmoothPacing, default on)
int display_pacing();    // WWHD_VK_DISPLAY_PACING when it is not set: 3 (pacing + one more image) or 0
bool even_pacing();      // even frame pacing's default on this device (Android already on)
}  // namespace gfxvk::smooth_defaults
#endif
