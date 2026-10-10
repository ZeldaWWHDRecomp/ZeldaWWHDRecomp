// Host audio output: 48 kHz interleaved s16 through CoreAudio / SDL, fed by the AX frame thread.
#pragma once
#include <cstdint>

namespace audio {

constexpr int kRate = 48000;

void init();                                    // opens the default output device (safe to call twice)
// Frozen at startup; the game must restart after changing the saved preference.
int channels();                                // 2 or 6, effective device mode
bool requested_surround();
void set_requested_surround(bool enabled);      // save for next start
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
