// AX TV order verified against Cemu snd_core/ax_out.cpp, AXOut_SubmitTVFrame.
// Host and WAV order: FL FR FC LFE BL BR. CoreAudio uses explicit labels in this order.
#pragma once
#include <algorithm>
#include <cstdint>
namespace audio {
inline constexpr int kHostToAx[6] = {0, 1, 4, 5, 2, 3};
inline constexpr const char *kSpeakerNames[6] = {"Front left", "Front right",   "Centre",
                                                 "Subwoofer",  "Surround left", "Surround right"};
inline int16_t tv_sample(const int32_t *ax, int host_channel, bool play_tv, int32_t drc, bool play_drc) {
    const int64_t sample =
        (play_tv ? int64_t(ax[kHostToAx[host_channel]]) : 0) + (play_drc && host_channel < 2 ? int64_t(drc) : 0);
    return int16_t(std::clamp<int64_t>(sample, -32768, 32767));
}
} // namespace audio
