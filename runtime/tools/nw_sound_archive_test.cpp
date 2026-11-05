// SPDX-License-Identifier: MPL-2.0
// The sound archive reader (runtime/src/mods/nw_sound_archive.h): DSP ADPCM frames, bad input, resampling. With
// WWHD_GAME_DIR=<game> it also reads the item fanfare (NW_BGM_ITEM_GET) from the player's c_king.bfsar and prints its
// format and loudness (nothing is written; CI has no game files).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "mods/nw_sound_archive.h"

int main() {
    // a frame with predictor 0 (coefficients 2048, 0: each sample is the last one plus the nibble), scale 1
    const int16_t coef[16] = {2048, 0};
    const uint8_t frame[8] = {0x00, 0x12, 0x34, 0xF0, 0, 0, 0, 0};
    int h1 = 0, h2 = 0;
    int16_t out[14];
    nw_sound::dsp_frame(frame, coef, h1, h2, out, 14, 1);
    assert(out[0] == 1 && out[1] == 3 && out[2] == 6 && out[3] == 10 && out[4] == 9 && out[5] == 9);  // (0xF = -1)
    // a short or wrong file: false, no crash
    for (size_t n : {0u, 8u, 64u, 200u}) {
        std::vector<uint8_t> junk(n, 0xAB);
        if (n >= 6) std::memcpy(junk.data(), "FSAR\xFE\xFF", 6);
        nw_sound::Archive a(junk);
        nw_sound::Pcm p;
        std::string why;
        assert(!a.read("NW_BGM_ITEM_GET", p, why) && !why.empty());
    }
    // 32 kHz mono to 48 kHz stereo: 1.5 times the frames, both channels the same, gain applied
    nw_sound::Pcm m;
    m.rate = 32000, m.channels = 1, m.samples.assign(3200, 1000);
    const auto s = nw_sound::to_48k_stereo(m, 0.5f);
    assert(s.size() == 4800 * 2 && s[100] == 500 && s[101] == 500);

    if (const char* game = std::getenv("WWHD_GAME_DIR")) {
        std::ifstream f(std::string(game) + "/content/Common/AudioRes/c_king.bfsar", std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        nw_sound::Archive a(bytes);
        nw_sound::Pcm p;
        std::string why;
        const bool ok = a.read("NW_BGM_ITEM_GET", p, why);
        double sum = 0;
        int peak = 0;
        for (int16_t v : p.samples) sum += double(v) * v, peak = std::max(peak, std::abs(int(v)));
        printf("NW_BGM_ITEM_GET: %s%s, %u Hz, %u channels, %.2f s, peak %d, rms %.0f, volume %u\n", ok ? "read" : "not read: ", why.c_str(), p.rate,
               p.channels, p.channels ? double(p.samples.size()) / p.channels / p.rate : 0.0, peak,
               p.samples.empty() ? 0.0 : std::sqrt(sum / double(p.samples.size())), p.volume);
        assert(ok);
    }
    puts("nw_sound_archive: ok");
    return 0;
}
