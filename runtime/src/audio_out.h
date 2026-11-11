// Host audio output: 48 kHz interleaved s16 through CoreAudio / SDL, fed by the AX frame thread.
#pragma once
#include <cstdint>

namespace audio {

constexpr int kRate = 48000;

void init();                                    // opens the default output device (safe to call twice)
// Frozen at startup; the game must restart after changing the saved preference.
int channels();                                 // 2 or 6, effective device mode
void pull_interleaved(int16_t* out, uint32_t frames);  // the mix (channels() per frame), for a host that renders it itself
bool requested_surround();
// 5.1 rendered by the system's Spatial Audio (AirPods, the device's speakers): the six channels go there as a
// named layout, whose surrounds are at the sides (Ls Rs, the default) or behind (Rls Rrs). Applies at once, saved.
bool spatial_surround();
void set_host_target_ms(int ms);
// the output's own latency after this side's queue (the host measures it: the route's latency and I/O buffer)
int output_latency_frames();
// the iPhone GamePad's sound: more delay (ms) for what the route doesn't report (a TV's or receiver's own
// processing). Settings > Audio; saved; applies at once. WWHD_PAD_AUDIO_EXTRA_MS starts with a value, not saved.
int pad_audio_extra_ms();
void set_pad_audio_extra_ms(int ms, bool save = true);
void set_output_latency_ms(float ms);  // the host rendering the mix: how much the mixer keeps queued for it (8 ms default)
bool rear_surrounds();
void set_rear_surrounds(bool rear);
void set_requested_surround(bool enabled);      // save for next start
// Master volume 0..1 for the game and mod mix (issue #124). Applied live; saved for the next start
// when `save` is set (the settings slider saves once on release, not on every step).
// WWHD_AUDIO_VOLUME starts the session at a fixed value whatever was saved, and is not saved.
float master_volume();
bool master_volume_from_env();
void set_master_volume(float gain, bool save = true);
// GamePad-only effects in TV play. Live host preferences, never part of a save state.
// WWHD_AUDIO_GAMEPAD_SOUNDS=0|1 and WWHD_AUDIO_GAMEPAD_VOLUME=0..1 override startup preferences;
// live edits to overridden preferences are not saved (same rule as the master volume).
bool gamepad_sounds();
float gamepad_sounds_volume();
bool gamepad_sounds_from_env();
bool gamepad_sounds_volume_from_env();
void set_gamepad_sounds(bool enabled);
void set_gamepad_sounds_volume(float gain, bool save = true);
bool surround_fallback();
bool output_device_changed();
void start_speaker_test();
void stop_speaker_test();
const char* speaker_test_channel();             // nullptr when idle
void push(const int16_t* samples, int frames);   // channels() samples per frame
int buffered_frames();                          // frames queued for the device
int target_frames();                            // latency the producer should aim for
void stats(uint64_t& underrun, uint64_t& dropped);  // frames of silence inserted / frames discarded
void finish_dump();                             // finalise recording (also called at process exit)
void flush();                                   // drop device queue and invalidate guest PCM streams (state/device reset)

}  // namespace audio
