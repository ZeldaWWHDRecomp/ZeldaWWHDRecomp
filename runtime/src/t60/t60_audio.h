// The sound system on a general step length (true 60 Hz logic, decomp build only).
//
// The sound engine stays a 30 Hz system (no JAudio dt-scaling): its per-frame work runs once per 30 Hz frame, on the
// frame's tick30 step, as in the 30 Hz game; the game's sound starts reach it from the step they happen on.
//   CTL     the audio frame callback 020315CC (the HD BGM controller's update: output faders, the delayed SE timer, the
//           JAudio step with JAIZelBasic's frame work: interp.cpp hook_020315CC). Frame interpolation already runs it
//           once per logic step; with true 60 it runs on the tick30 step only (once per 30 Hz frame, after every step's
//           sound starts of that frame).
//   PLAYER  the HD sound player's update 0202FA3C (updateSoundPlayer: the NW4F player and BGM faders, their fade frames;
//           decompiled in hd/hd_snd_0202EE08.cpp, which asks wwhd_t60_audio_player before it runs). With true 60 it runs
//           once per 30 Hz frame, on the tick30 step (a fade of n frames lasts n 30 Hz frames).
//   SE      the sound start hooks (0201EBA0, 025E1988 ... 025E1AA4, interp.cpp se_held): a start on an in-between pass
//           is suppressed (drawing code repeats there), except from the logic of a step: a process executing on one of the
//           frame's earlier steps (the rate gate's converted processes) and a converted UI system stepping there
//           (t60_ui.h: the HUD's counters and sounds run on the frame's first step).
//   LISTENER the camera listener (025E1B44 / 02027754): a converted camera is drawn as an exact camera on every step
//           (interp.cpp t60_camera), so the listener follows it every step; an unconverted camera keeps interpolation's
//           rule (the record pass's exact camera, once per 30 Hz frame).
// Animation sounds (JAIZelAnime::setAnimSound 0201BF08 -> JAIAnimeSound::setAnimSoundActor): the frame control keeps the
// per-30 Hz-frame rate (frame += rate x dt, mRate undivided), so the sound gets the undivided rate (pitch, loop
// window). JAIAnimeSound itself is a once-per-30-Hz-frame engine, not a pure crossing test: a loop wrap plays the keys up
// to the last call's frame + speed, and every call re-checks / re-starts its playing slots (a start == end key restarts
// its sound on each call). Its caller therefore runs it once per 30 Hz frame, on the tick30 step, with the frame-end
// frame (2026-11-24: Link's daPy_lk_c::execute, lkT60_animSoundStep in
// d_a_player_seinit_t60.h; the converted actors' mDoExt_McaMorf / McaMorf2::play likewise). Keys crossing per step
// apply only to sound paths that are pure crossing tests. A latched first-step animation set's sound init steps the
// whole rate back (lkT60_seInitBack), so that frame's one call sees A's key window.
//
// N = 1 (and every 30 Hz fallback frame): unchanged.
// PLAYER at N = 1 with frame interpolation: once per logic step, as CTL (normal port PR #627; fades at double speed
// before). debug: WWHD_SND_PLAYER_EVERY_PASS=1 runs it on every pass again.
// debug: WWHD_T60_AUD=0 runs CTL / PLAYER as before (frame interpolation's rules) (the switch; default on).
// debug: WWHD_T60_AUD_STATS=1 logs every 5 s how often CTL / PLAYER / LISTENER ran per pass kind
//        [30 Hz: no interpolation / tick30 / earlier step / plain in-between], and the SE starts let through or held.
#pragma once
#include <cstdint>

namespace t60::audio {
enum What { kCtl, kPlayer, kListener, kPolygon, kSeOn, kSeHeld, kCamTimer, kCamGroup, kCamMap, kWhatN };
// interp.cpp hook_020315CC: whether the audio frame callback runs on this pass. interp_rule: frame interpolation's own
// verdict (once per logic step); with true 60 active the tick30 step decides (once per 30 Hz frame)
bool ctl_runs(bool interp_rule);
// whether this pass's step runs the 30 Hz sound frame (PLAYER): N = 1 every call; true 60: the tick30 step, once
bool player_runs();
// a sound start on an in-between pass from the logic of a step (not suppressed): a converted UI system stepping there
// JAIZelBasic::getCameraInfo's camera timer (mNoCameraTimer) counts on this hand-over: once per 30 Hz frame (true 60) /
// once per logic step (interpolation; the listener takes the camera twice per step); N = 1 without interpolation: always
bool camera_timer_counts();
// JAIZelBasic::setCameraGroupInfo runs on this hand-over (same rule as the camera timer, its own once-latch)
bool camera_group_counts();
// JAIZelBasic::getCameraMapInfo runs on this hand-over (same rule, its own once-latch)
bool camera_map_counts();
bool se_step_logic();
void ui_step(bool enter);  // t60_ui.cpp: a converted UI system's step begins (true) / ends (false)
// WWHD_T60_AUD_STATS: count one call (ran: it did its work)
void count(What w, bool ran);
}  // namespace t60::audio

// the decompiled updateSoundPlayer 0202FA3C asks this before it runs (hd/hd_snd_t60.h; game build only)
extern "C" int wwhd_t60_audio_player();
// the decompiled JAIZelBasic::getCameraInfo 02027558 asks this before counting mNoCameraTimer down (JAIZelBasic_t60.h)
extern "C" int wwhd_t60_audio_camtimer();
// the decompiled JAIZelBasic::setCameraGroupInfo 02027670 asks this before it runs (JAIZelBasic_t60.h)
extern "C" int wwhd_t60_audio_camgroup();
// the decompiled JAIZelBasic::getCameraMapInfo 02027668 asks this before it runs (JAIZelBasic_t60.h)
extern "C" int wwhd_t60_audio_cammap();
