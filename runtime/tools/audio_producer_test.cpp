// Synthetic-only verification of all-channel gain, guest PCM, acceleration and speaker test.
#include "audio_out.h"
#include "audio_channels.h"
#include "game_clock.h"
#include "mods/guest_audio.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
static bool mute = false;
namespace mods {
bool fast_forward_mute() {
    return mute;
}
} // namespace mods
namespace hostui {
bool get(const char *, std::string &) {
    return false;
}
void set(const char *, const std::string &) {}
} // namespace hostui
void log_msg(const char *, ...) {}
static void env(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
int main(int argc, char **argv) {
    assert(argc == 3);
    std::string mode = argv[2];
    const bool stereo = mode.starts_with("stereo");
    const bool tone = mode == "tone";
    mute = mode == "mute" || mode == "stereo-mute";
    env("WWHD_NO_AUDIO", "1");
    env("WWHD_AUDIO_SPEAKERS", stereo ? "stereo" : "surround");
    env("WWHD_AUDIO_VOLUME", "0.5");
    env("WWHD_AUDIO_DUMP", argv[1]);
    audio::init();
    const int channels = audio::channels();
    assert(channels == (stereo ? 2 : 6));
    if (mode == "fast" || mode == "stereo-fast" || mute)
        game_clock::set_rate(4);
    const int rate = game_clock::rate(), frames = 48000;
    if (tone)
        audio::start_speaker_test();
    // Each channel has a distinct signal; guest streams add only to FL/FR and saturate
    // before gain. Submit enough samples for each bounded producer block.
    auto &store = guestmods::pcm::store();
    int h = store.open("synthetic", 48000, 2);
    assert(h > 0);
    std::vector<int16_t> source(144 * channels), guest(144 * 2);
    std::vector<int16_t> expected;
    for (int second = 0; second < (tone ? 6 : 1) * rate; ++second) {
        if (tone)
            assert(!strcmp(audio::speaker_test_channel(), audio::kSpeakerNames[second]));
        for (int begin = 0; begin < frames; begin += 144) {
            int count = std::min(144, frames - begin);
            for (int i = 0; i < count; ++i) {
                guest[i * 2] = 6000;
                guest[i * 2 + 1] = -6000;
                for (int ch = 0; ch < channels; ++ch)
                    source[i * channels + ch] = int16_t((ch == 0 ? 30000 : ch == 1 ? -30000 : (ch + 1) * 1000) + i % 4);
            }
            assert(store.submit("synthetic", h, guest.data(), count, 2) == count);
            audio::push(source.data(), count);
            assert(store.available("synthetic", h) == 8192);
            if (!tone)
                for (int i = 0; i < count / rate; ++i)
                    for (int ch = 0; ch < channels; ++ch) {
                        int sum = 0;
                        for (int j = 0; j < rate; ++j) {
                            int sample = source[(i * rate + j) * channels + ch] + (ch == 0   ? 6000
                                                                                   : ch == 1 ? -6000
                                                                                             : 0);
                            sum += int16_t(std::clamp(sample, -32768, 32767) * 0.5f);
                        }
                        expected.push_back(mute ? 0 : sum / rate);
                    }
        }
    }
    if (tone)
        assert(audio::speaker_test_channel() == nullptr);
    audio::finish_dump();
    std::ifstream file(argv[1], std::ios::binary);
    file.seekg(stereo ? 44 : 68);
    if (!tone) {
        std::vector<int16_t> actual(expected.size());
        file.read(reinterpret_cast<char *>(actual.data()), actual.size() * 2);
        assert(file.good() && actual == expected);
    } else {
        for (int speaker = 0; speaker < 6; ++speaker) {
            bool heard = false;
            for (int frame = 0; frame < 48000; ++frame) {
                int16_t samples[6];
                file.read(reinterpret_cast<char *>(samples), 12);
                assert(file.good());
                for (int ch = 0; ch < 6; ++ch) {
                    if (ch != speaker || frame >= 31200)
                        assert(samples[ch] == 0);
                    else {
                        heard |= samples[ch] != 0;
                        assert(std::abs(int(samples[ch])) <= 2048);
                    }
                }
            }
            assert(heard);
        }
    }
    file.close();
    std::remove(argv[1]);
}
