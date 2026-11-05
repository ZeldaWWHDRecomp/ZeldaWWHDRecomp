// SPDX-License-Identifier: MPL-2.0
// A named wave sound of the game's sound archive (NW4F .bfsar, big-endian: content/Common/AudioRes/c_king.bfsar) as
// 16-bit PCM: its name through the string table and patricia tree, the wave sound file's first note, the wave archive
// and its FWAV (DSP ADPCM, mono or stereo), decoded. Pure, bounds-checked (a short or malformed file gives false, never
// a read outside it): nw_sound_archive_test. Read at run time from the player's own game; nothing of it is kept.
//
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), android/.../MenuSounds.java (extract, fileData,
// writeWav), taken over as C++.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nw_sound {

struct Pcm {
    uint32_t rate = 0, channels = 0;
    std::vector<int16_t> samples;  // interleaved
    uint8_t volume = 127;          // the sound's own volume in the archive (0..255, 127 as recorded)
};

// One frame of DSP ADPCM (8 bytes: predictor/scale, 14 nibbles) into up to `n` samples; h1 / h2 the history
inline void dsp_frame(const uint8_t* f, const int16_t coef[16], int& h1, int& h2, int16_t* out, size_t n, size_t stride) {
    const int ps = f[0], scale = 1 << (ps & 15), c1 = coef[((ps >> 4) & 7) * 2], c2 = coef[((ps >> 4) & 7) * 2 + 1];
    for (size_t k = 0; k < 14 && k < n; k++) {
        const int b = f[1 + k / 2];
        int nib = k % 2 == 0 ? b >> 4 : b & 15;
        if (nib >= 8) nib -= 16;
        const int v = std::clamp((((nib * scale) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11, -32768, 32767);
        out[k * stride] = int16_t(v);
        h2 = h1, h1 = v;
    }
}

class Archive {
public:
    explicit Archive(const std::vector<uint8_t>& bytes) : d_(bytes) {}

    bool read(const std::string& name, Pcm& out, std::string& why) {
        try {
            return read_(name, out, why);
        } catch (const Bad&) {
            why = "the sound archive is not as expected";
            return false;
        }
    }

private:
    struct Bad {};
    const std::vector<uint8_t>& d_;

    void need(size_t o, size_t n) const {
        if (o > d_.size() || n > d_.size() - o) throw Bad{};
    }
    uint32_t u8(size_t o) const { return need(o, 1), d_[o]; }
    uint32_t u16(size_t o) const { return need(o, 2), uint32_t(d_[o]) << 8 | d_[o + 1]; }
    int32_t s32(size_t o) const { return need(o, 4), int32_t(uint32_t(d_[o]) << 24 | uint32_t(d_[o + 1]) << 16 | uint32_t(d_[o + 2]) << 8 | d_[o + 3]); }
    int16_t s16(size_t o) const { return int16_t(u16(o)); }
    size_t at(int64_t v) const {
        if (v < 0 || uint64_t(v) >= d_.size()) throw Bad{};
        return size_t(v);
    }
    int32_t ref(size_t o) const { return s32(o + 4); }  // a reference: type u16, pad, offset s32
    std::vector<size_t> table(size_t base) const {
        const int32_t n = s32(base);
        if (n < 0 || n > 100000) throw Bad{};
        std::vector<size_t> t(static_cast<size_t>(n));
        for (int32_t i = 0; i < n; i++) t[size_t(i)] = at(int64_t(base) + s32(base + 4 + 8 * size_t(i) + 4));
        return t;
    }
    // a section's body in an NW4F file (header: magic, BOM, header size, version, size, count, then
    // {id u16, pad, offset u32, size u32} per section)
    size_t section(size_t file, size_t i) const { return at(int64_t(file) + s32(file + 24 + 12 * i) + 8); }

    size_t file_data(int32_t id, const std::vector<size_t>& files, size_t block) const {
        if (id < 0 || size_t(id) >= files.size()) throw Bad{};
        const size_t p = at(int64_t(files[size_t(id)]) + ref(files[size_t(id)]));
        if (ref(p) != -1) return at(int64_t(block) + ref(p));
        for (size_t fi : files) {  // in a group file
            if (u16(fi) != 0x220C) continue;
            const size_t q = at(int64_t(fi) + ref(fi));
            if (ref(q) == -1) continue;
            const size_t g = at(int64_t(block) + ref(q));
            if (uint32_t(s32(g)) != 0x46475250u) continue;  // FGRP
            const size_t gi = section(g, 0), gf = section(g, 1);
            for (size_t e : table(gi))
                if (s32(e) == id) return at(int64_t(gf) + ref(e + 4));
        }
        throw Bad{};
    }

    bool read_(const std::string& name, Pcm& out, std::string& why) {
        if (d_.size() < 0x40 || std::memcmp(d_.data(), "FSAR", 4) || d_[4] != 0xFE || d_[5] != 0xFF) return why = "not a big-endian sound archive", false;
        const size_t strg = section(0, 0), info = section(0, 1), block = section(0, 2);
        const size_t strings = at(int64_t(strg) + ref(strg)), tree = at(int64_t(strg) + ref(strg + 8));
        const int32_t count = s32(strings);
        if (count < 0 || count > 1000000) throw Bad{};
        std::vector<int32_t> items(size_t(count), -1);
        for (int32_t i = 0, n = s32(tree + 4); i < n; i++) {
            const size_t node = tree + 8 + 20 * size_t(i);
            if ((u16(node) & 1) && s32(node + 12) >= 0 && s32(node + 12) < count) items[size_t(s32(node + 12))] = s32(node + 16);
        }
        int32_t item = -1;
        for (int32_t i = 0; i < count; i++) {
            const size_t e = strings + 4 + 12 * size_t(i);
            const int32_t off = s32(e + 4), size = s32(e + 8);
            if (size - 1 != int32_t(name.size())) continue;
            need(strings + size_t(off), size_t(size));
            if (!std::memcmp(&d_[strings + size_t(off)], name.data(), name.size())) item = items[size_t(i)];
        }
        if (item < 0) return why = name + " is not in the sound archive", false;
        const std::vector<size_t> sounds = table(at(int64_t(info) + ref(info))), wave_archives = table(at(int64_t(info) + ref(info + 24))),
                                  files = table(at(int64_t(info) + ref(info + 48)));
        const size_t si = size_t(item & 0xFFFFFF);
        if (si >= sounds.size()) throw Bad{};
        const size_t s = sounds[si];
        if (u16(s + 12) != 0x2202) return why = name + " is not a wave sound", false;
        const int32_t index = s32(at(int64_t(s) + ref(s + 12)));
        out.volume = uint8_t(u8(s + 8));
        const size_t wsd = file_data(s32(s), files, block), wi = section(wsd, 0);
        const size_t wave_ids = at(int64_t(wi) + ref(wi));
        const std::vector<size_t> waves = table(at(int64_t(wi) + ref(wi + 8)));
        if (index < 0 || size_t(index) >= waves.size()) throw Bad{};
        const size_t x = waves[size_t(index)];
        const std::vector<size_t> notes = table(at(int64_t(x) + ref(x + 16)));
        if (notes.empty()) throw Bad{};
        const int32_t wid = s32(notes[0]);
        if (wid < 0) throw Bad{};
        const size_t arc = size_t(s32(wave_ids + 4 + 8 * size_t(wid)) & 0xFFFFFF);
        const int32_t wave = s32(wave_ids + 8 + 8 * size_t(wid));
        if (arc >= wave_archives.size() || wave < 0) throw Bad{};
        const size_t war = file_data(s32(wave_archives[arc]), files, block);
        const size_t war_info = section(war, 0), war_file = section(war, 1);
        return fwav(at(int64_t(war_file) + s32(war_info + 4 + 12 * size_t(wave) + 4)), out, why);
    }

    // an FWAV (DSP ADPCM, mono or stereo) as PCM
    bool fwav(size_t wav, Pcm& out, std::string& why) {
        const size_t info = section(wav, 0), data = section(wav, 1);
        if (u8(info) != 2) return why = "the wave is not DSP ADPCM", false;
        const int32_t rate = s32(info + 4), n = s32(info + 12);
        const std::vector<size_t> chs = table(info + 0x14);
        if (rate <= 0 || n <= 0 || n > 48000 * 60 || chs.empty()) throw Bad{};
        const size_t nc = std::min<size_t>(2, chs.size());
        out.rate = uint32_t(rate), out.channels = uint32_t(nc);
        out.samples.assign(size_t(n) * nc, 0);
        for (size_t c = 0; c < nc; c++) {
            const size_t ch = chs[c], src = at(int64_t(data) + ref(ch)), adpcm = at(int64_t(ch) + ref(ch + 8));
            int16_t coef[16];
            for (int i = 0; i < 16; i++) coef[i] = s16(adpcm + 2 * size_t(i));
            int h1 = s16(adpcm + 34), h2 = s16(adpcm + 36);
            for (size_t done = 0, i = src; done < size_t(n); i += 8, done += 14) {
                need(i, 8);
                dsp_frame(&d_[i], coef, h1, h2, &out.samples[done * nc + c], size_t(n) - done, nc);
            }
        }
        return true;
    }
};

// to 48 kHz stereo (the port's PCM streams), linear, scaled by `gain`
inline std::vector<int16_t> to_48k_stereo(const Pcm& p, float gain) {
    std::vector<int16_t> out;
    if (!p.rate || !p.channels || p.samples.empty()) return out;
    const size_t in = p.samples.size() / p.channels, n = size_t(double(in) * 48000.0 / p.rate);
    out.resize(n * 2);
    for (size_t i = 0; i < n; i++) {
        const double t = double(i) * p.rate / 48000.0;
        const size_t a = std::min(in - 1, size_t(t)), b = std::min(in - 1, a + 1);
        const float f = float(t - double(a));
        for (size_t c = 0; c < 2; c++) {
            const size_t ch = std::min<size_t>(c, p.channels - 1);
            const float v = (p.samples[a * p.channels + ch] * (1 - f) + p.samples[b * p.channels + ch] * f) * gain;
            out[i * 2 + c] = int16_t(std::clamp(v, -32768.0f, 32767.0f));
        }
    }
    return out;
}

}  // namespace nw_sound
