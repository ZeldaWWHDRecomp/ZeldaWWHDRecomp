#include "mods/fast_forward.h"
#include "game_clock.h"
// CoreAudio output (default output AudioUnit) pulling from a single-producer ring buffer.
// WWHD_AUDIO_DUMP=file.wav additionally records everything pushed by the game.
// WWHD_NO_AUDIO=1 skips opening the device (the mix still runs and can be dumped).
#include "audio_out.h"
#include "audio_channels.h"
#include "overlay/hostui.h"
#include <algorithm>
#include "mods/guest_audio.h"

#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#else
#include <SDL3/SDL.h>
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "runtime.h"

namespace audio {
namespace {

constexpr int kCapacity = 1 << 15; // frames (~680 ms)
constexpr int kTarget = kRate * 40 / 1000;

int16_t g_ring[kCapacity * 6];
std::atomic<uint32_t> g_read{0}, g_write{0};
std::once_flag g_start;
std::atomic<bool> g_flush{false}; // consumer skips everything queued
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
AudioComponentInstance g_unit = nullptr;
#else
SDL_AudioStream *g_unit = nullptr;
#endif

std::atomic<uint64_t> g_underrun{0}, g_dropped{0};

FILE *g_dump = nullptr;
std::mutex g_dump_mutex;
bool g_dump_enabled = false;
uint32_t g_dump_frames = 0;
std::atomic<float> g_gain{1};  // master volume (issue #124), read by the producer mix
std::atomic<int> g_channels{2};
std::atomic<bool> g_requested{false}, g_fallback{false}, g_device_changed{false};
std::atomic<int> g_test{-1};     // sample position; only producer advances, UI starts/stops
constexpr int kTestSlot = kRate; // 0.65 s tone + 0.35 s silence per speaker
int header_size() {
    return g_channels == 6 ? 68 : 44;
}
void fallback() {
    g_channels = 2;
    g_fallback = true;
    LOG("[audio] Surround 5.1 unavailable on this output; using stereo (restart to retry)");
}

void write_wav_header() {
    const int bytes = g_channels * 2, size = header_size();
    uint32_t data = g_dump_frames * bytes;
    uint8_t h[68]{};
    auto u32 = [&](int o, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            h[o + i] = uint8_t(v >> (8 * i));
    };
    auto u16 = [&](int o, uint16_t v) {
        h[o] = uint8_t(v);
        h[o + 1] = uint8_t(v >> 8);
    };
    memcpy(h, "RIFF", 4);
    u32(4, size - 8 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    u32(16, g_channels == 6 ? 40 : 16);
    u16(20, g_channels == 6 ? 0xfffe : 1);
    u16(22, g_channels.load());
    u32(24, kRate);
    u32(28, kRate * bytes);
    u16(32, bytes);
    u16(34, 16);
    if (g_channels == 6) {
        u16(36, 22);
        u16(38, 16);
        u32(40, 0x3f); // FL FR FC LFE BL BR
        const uint8_t pcmGuid[16] = {1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113};
        memcpy(h + 44, pcmGuid, 16);
    }
    memcpy(h + size - 8, "data", 4);
    u32(size - 4, data);
    long pos = ftell(g_dump);
    fseek(g_dump, 0, SEEK_SET);
    fwrite(h, 1, size, g_dump);
    fseek(g_dump, pos, SEEK_SET);
    fflush(g_dump);
}

void pull(int16_t *out, uint32_t frames) {
    uint32_t r = g_read.load(std::memory_order_relaxed), w = g_write.load(std::memory_order_acquire);
    if (g_flush.exchange(false))
        r = w;
    uint32_t avail = w - r, n = std::min<uint32_t>(avail, frames);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (r + i) & (kCapacity - 1);
        memcpy(out + i * g_channels, g_ring + idx * g_channels, g_channels * sizeof(int16_t));
    }
    if (n < frames) { // underrun: silence
        memset(out + n * g_channels, 0, (frames - n) * g_channels * 2);
        g_underrun += frames - n;
    }
    g_read.store(r + n, std::memory_order_release);
}
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
OSStatus output_changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *) {
    // Six-channel game state cannot be switched to stereo mid-frame. Keep it silent until
    // restart renegotiates the new device, instead of letting the OS fold it into two speakers.
    if (g_channels == 6)
        g_device_changed = true;
    audio::flush();
    return noErr;
}
OSStatus render(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32 frames,
                AudioBufferList *io) {
    if (g_device_changed)
        memset(io->mBuffers[0].mData, 0, frames * g_channels * 2);
    else
        pull((int16_t *)io->mBuffers[0].mData, frames);
    return noErr;
}
#else
void SDLCALL render(void *, SDL_AudioStream *stream, int additional, int) {
    // SDL may migrate the default logical device. Do not auto-downmix a surround game.
    if (g_channels == 6) {
        SDL_AudioSpec physical{};
        if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &physical, nullptr) || physical.channels < 6)
            g_device_changed = true;
    }
    int16_t samples[1024 * 6];
    const int bytes = g_channels * 2;
    while (additional > 0) {
        uint32_t frames = std::min(additional / bytes, 1024);
        if (!frames)
            break;
        if (g_device_changed)
            memset(samples, 0, frames * bytes);
        else
            pull(samples, frames);
        if (!SDL_PutAudioStreamData(stream, samples, (int)frames * bytes))
            break;
        additional -= (int)frames * bytes;
    }
}
#endif

} // namespace

void init() {
    std::call_once(g_start, [] {
        std::string saved;
        bool surround = hostui::get("audioSpeakers", saved) && saved == "surround";
        if (const char *e = getenv("WWHD_AUDIO_SPEAKERS"))
            surround = !strcmp(e, "surround");
        g_requested = surround;
        g_channels = surround ? 6 : 2;
        // Silent output still renders the requested format, for offline dump verification.
        auto open_dump = [] {
            if (const char *p = getenv("WWHD_AUDIO_DUMP")) {
                g_dump = fopen(p, "wb");
                if (g_dump) {
                    g_dump_enabled = true;
                    write_wav_header();
                    fseek(g_dump, header_size(), SEEK_SET);
                    std::atexit(finish_dump);
                }
            }
        };
        // Master volume (issue #124): the saved setting, unless WWHD_AUDIO_VOLUME starts the
        // session at a fixed value (e.g. silent tests of the real output path).
        {
            float gain = 1.0f;
            std::string saved;
            if (hostui::get("audioVolume", saved)) {
                const char *text = saved.c_str();
                char *end = nullptr;
                const float percent = std::strtof(text, &end);
                if (end != text && std::isfinite(percent))
                    gain = std::clamp(percent, 0.0f, 100.0f) / 100.0f;
            }
            if (const char *v = getenv("WWHD_AUDIO_VOLUME")) {
                const float env = float(atof(v));
                gain = std::isfinite(env) ? std::clamp(env, 0.0f, 1.0f) : 1.0f;
            }
            g_gain = gain;
        }
        if (getenv("WWHD_NO_AUDIO")) {
            open_dump();
            return;
        }

#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        if (!comp || AudioComponentInstanceNew(comp, &g_unit) != noErr) {
            LOG("[audio] no output device");
            g_unit = nullptr;
            if (g_channels == 6)
                fallback();
            open_dump();
            return;
        }
        if (g_channels == 6) {
            AudioDeviceID device = 0;
            UInt32 size = sizeof(device);
            AudioObjectPropertyAddress addr{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
            bool capable =
                AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &device) == noErr;
            addr = {kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput,
                    kAudioObjectPropertyElementMain};
            size = 0;
            capable = capable && AudioObjectGetPropertyDataSize(device, &addr, 0, nullptr, &size) == noErr;
            std::vector<uint8_t> storage(size);
            capable = capable && size >= sizeof(AudioBufferList) &&
                      AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, storage.data()) == noErr;
            UInt32 count = 0;
            if (capable) {
                auto *buffers = reinterpret_cast<AudioBufferList *>(storage.data());
                for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
                    count += buffers->mBuffers[i].mNumberChannels;
            }
            if (count < 6)
                fallback();
        }
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = kRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        fmt.mChannelsPerFrame = g_channels;
        fmt.mBitsPerChannel = 16;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerFrame = g_channels * 2;
        fmt.mBytesPerPacket = g_channels * 2;
        OSStatus format_status =
            AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
        if (g_channels == 6) {
            // Explicit descriptions avoid CoreAudio's default six-channel (AAC/MPEG) order.
            alignas(AudioChannelLayout) uint8_t
                storage[offsetof(AudioChannelLayout, mChannelDescriptions) + 6 * sizeof(AudioChannelDescription)]{};
            auto *layout = reinterpret_cast<AudioChannelLayout *>(storage);
            layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
            layout->mNumberChannelDescriptions = 6;
            const AudioChannelLabel labels[] = {kAudioChannelLabel_Left,         kAudioChannelLabel_Right,
                                                kAudioChannelLabel_Center,       kAudioChannelLabel_LFEScreen,
                                                kAudioChannelLabel_LeftSurround, kAudioChannelLabel_RightSurround};
            for (int i = 0; i < 6; ++i)
                layout->mChannelDescriptions[i].mChannelLabel = labels[i];
            OSStatus layout_status = AudioUnitSetProperty(g_unit, kAudioUnitProperty_AudioChannelLayout,
                                                          kAudioUnitScope_Input, 0, layout, sizeof storage);
            if (format_status != noErr || layout_status != noErr) {
                fallback();
                fmt.mChannelsPerFrame = 2;
                fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4;
                format_status = AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                                     &fmt, sizeof fmt);
                AudioChannelLayout stereo_layout{};
                stereo_layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
                AudioUnitSetProperty(g_unit, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0,
                                     &stereo_layout, sizeof stereo_layout);
            }
        }
        if (format_status != noErr) {
            LOG("[audio] unsupported stream format");
            AudioComponentInstanceDispose(g_unit);
            g_unit = nullptr;
            open_dump();
            return;
        }
        AURenderCallbackStruct cb{render, nullptr};
        AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        // debug: WWHD_AUDIO_VOLUME=0..1 scales the device volume (e.g. silent tests of the real output path)
        // Master gain is applied to the common producer mix, including guest PCM.
        auto start = [] { return AudioUnitInitialize(g_unit) == noErr && AudioOutputUnitStart(g_unit) == noErr; };
        bool started = start();
        if (!started && g_channels == 6) {
            AudioOutputUnitStop(g_unit);
            AudioUnitUninitialize(g_unit);
            fallback();
            fmt.mChannelsPerFrame = 2;
            fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4;
            OSStatus status = AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                                   &fmt, sizeof fmt);
            AudioChannelLayout layout{};
            layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
            AudioUnitSetProperty(g_unit, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, &layout,
                                 sizeof layout);
            started = status == noErr && start();
        }
        open_dump();
        if (!started) {
            LOG("[audio] failed to start output unit");
            AudioComponentInstanceDispose(g_unit);
            g_unit = nullptr;
            return;
        }
        AudioObjectPropertyAddress changed{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain};
        AudioObjectAddPropertyListener(kAudioObjectSystemObject, &changed, output_changed, nullptr);
        LOG("[audio] CoreAudio output started (48 kHz, %d channels)", g_channels.load());
#else
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG("[audio] SDL audio initialization: %s", SDL_GetError());
            if (g_channels == 6)
                fallback();
            open_dump();
            return;
        }
        if (g_channels == 6) {
            SDL_AudioSpec physical{};
            if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &physical, nullptr) ||
                physical.channels < 6)
                fallback();
        }
        SDL_AudioSpec spec{SDL_AUDIO_S16, g_channels.load(), kRate};
        g_unit = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, nullptr);
        if (!g_unit && g_channels == 6) {
            fallback();
            spec.channels = 2;
            g_unit = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, nullptr);
        }
        if (g_unit && g_channels == 6) {
            SDL_AudioSpec actual{};
            if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_unit), &actual, nullptr) || actual.channels < 6) {
                SDL_DestroyAudioStream(g_unit);
                fallback();
                spec.channels = 2;
                g_unit = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, nullptr);
            }
        }
        if (!g_unit) {
            LOG("[audio] no output device: %s", SDL_GetError());
            open_dump();
            return;
        }

        bool started = SDL_ResumeAudioStreamDevice(g_unit);
        if (!started && g_channels == 6) {
            SDL_DestroyAudioStream(g_unit);
            fallback();
            spec.channels = 2;
            g_unit = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, nullptr);
            started = g_unit && SDL_ResumeAudioStreamDevice(g_unit);
        }
        open_dump();
        if (!started) {
            LOG("[audio] failed to start: %s", SDL_GetError());
            if (g_unit)
                SDL_DestroyAudioStream(g_unit);
            g_unit = nullptr;
            return;
        }
        LOG("[audio] SDL output started (48 kHz, %d channels)", g_channels.load());
#endif
    });
}

void push(const int16_t *stereo, int frames) {
    // The game producer supplies a bounded AX frame. Preserve the caller's data.
    constexpr int chunk = 2048;
    const int channels = g_channels.load(std::memory_order_relaxed);
    if (frames <= 0)
        return;
    if (frames > chunk) {
        for (int i = 0; i < frames; i += chunk)
            push(stereo + i * channels, std::min(chunk, frames - i));
        return;
    }
    // Mods' PCM streams are mixed in game time, before fast forward, so they speed up with the game.
    int16_t mixed[chunk * 6];
    if (guestmods::pcm::store().active() || g_gain != 1) {
        memcpy(mixed, stereo, size_t(frames) * channels * 2);
        guestmods::pcm::store().mix(mixed, uint32_t(frames), g_gain == 0, channels);
        if (g_gain != 1)
            for (int i = 0; i < frames * channels; ++i)
                mixed[i] = int16_t(mixed[i] * g_gain);
        stereo = mixed;
    }
    // AX still processes every 3 ms guest frame and every callback. The output device
    // stays at 48 kHz: downsample the accelerated stream (pitch rises with speed).
    std::vector<int16_t> accelerated;
    const unsigned rate = game_clock::rate();
    if (rate > 1) {
        const int count = frames / rate;
        accelerated.resize(count * channels);
        const bool silent = mods::fast_forward_mute();
        for (int i = 0; i < count; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                int sum = 0;
                for (unsigned j = 0; j < rate; ++j)
                    sum += stereo[(i * rate + j) * channels + ch];
                accelerated[i * channels + ch] = silent ? 0 : sum / int(rate);
            }
        }
        stereo = accelerated.data();
        frames = count;
    }
    // Replace game audio during the test; mods have already drained and fast forward cannot
    // shorten the sequence. The tone follows master gain and shares the real device/WAV path.
    int test = g_test.load(), expected_test = test;
    if (test >= 0) {
        for (int i = 0; i < frames; ++i, ++test) {
            for (int ch = 0; ch < channels; ++ch)
                mixed[i * channels + ch] = 0;
            if (test < channels * kTestSlot) {
                const int ch = test / kTestSlot, pos = test % kTestSlot;
                if (pos < kRate * 65 / 100) {
                    const double envelope = std::min({1.0, pos / 480.0, (kRate * 65 / 100 - pos) / 480.0});
                    mixed[i * channels + ch] = int16_t(
                        4096 * g_gain * envelope * std::sin(6.283185307179586 * (ch == 3 ? 80 : 440) * pos / kRate));
                }
            }
        }
        g_test.compare_exchange_strong(expected_test, test >= channels * kTestSlot ? -1 : test);
        stereo = mixed;
    }
    if (g_dump_enabled) {
        std::lock_guard lock(g_dump_mutex);
        if (g_dump) {
            fwrite(stereo, channels * 2, frames, g_dump);
            g_dump_frames += frames;
            if (g_dump_frames % kRate < (uint32_t)frames)
                write_wav_header();
        }
    }
    if (!g_unit || g_device_changed)
        return;
    uint32_t w = g_write.load(std::memory_order_relaxed), r = g_read.load(std::memory_order_acquire);
    if (w - r + frames > kCapacity) { // device stalled: drop rather than overwrite
        g_dropped += frames;
        return;
    }
    for (int i = 0; i < frames; i++) {
        uint32_t idx = (w + i) & (kCapacity - 1);
        memcpy(g_ring + idx * channels, stereo + i * channels, channels * sizeof(int16_t));
    }
    g_write.store(w + frames, std::memory_order_release);
}

void finish_dump() {
    std::lock_guard lock(g_dump_mutex);
    if (g_dump) {
        write_wav_header();
        fclose(g_dump);
        g_dump = nullptr;
    }
}
int channels() {
    return g_channels;
}
bool requested_surround() {
    return g_requested;
}
void set_requested_surround(bool enabled) {
    g_requested = enabled;
    hostui::set("audioSpeakers", enabled ? "surround" : "stereo");
}
float master_volume() {
    return g_gain.load();
}
void set_master_volume(float gain) {
    g_gain = std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 1.0f;
    hostui::set("audioVolume", std::to_string(int(std::lround(g_gain.load() * 100.0f))));
}
bool surround_fallback() {
    return g_fallback;
}
bool output_device_changed() {
    return g_device_changed;
}
void start_speaker_test() {
    g_flush = true;
    g_test = 0;
}
void stop_speaker_test() {
    g_test = -1;
    g_flush = true;
}
const char *speaker_test_channel() {
    int pos = g_test.load();
    return pos < 0 ? nullptr : kSpeakerNames[std::min(pos / kTestSlot, g_channels - 1)];
}

int buffered_frames() {
    if (!g_unit || g_device_changed)
        return kTarget;
    return (int)(g_write.load(std::memory_order_acquire) - g_read.load(std::memory_order_acquire));
}

int target_frames() {
    return kTarget;
}

void flush() {
    g_flush = true;
    guestmods::pcm::store().reset();
}

void stats(uint64_t &underrun, uint64_t &dropped) {
    underrun = g_underrun;
    dropped = g_dropped;
}

} // namespace audio
