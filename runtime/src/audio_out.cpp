#include "mods/fast_forward.h"
#include "game_clock.h"
// CoreAudio output (default output AudioUnit) pulling from a single-producer ring buffer.
// WWHD_AUDIO_DUMP=file.wav additionally records everything pushed by the game.
// WWHD_NO_AUDIO=1 skips opening the device (the mix still runs and can be dumped).
#include "audio_out.h"
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

constexpr int kCapacity = 1 << 15;  // frames (~680 ms)
constexpr int kTarget = kRate * 40 / 1000;

int16_t g_ring[kCapacity * 2];
std::atomic<uint32_t> g_read{0}, g_write{0};
std::atomic<bool> g_started{false};
std::atomic<bool> g_flush{false};  // consumer skips everything queued
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
AudioComponentInstance g_unit = nullptr;
#else
SDL_AudioStream* g_unit = nullptr;
#endif

std::atomic<uint64_t> g_underrun{0}, g_dropped{0};

FILE* g_dump = nullptr;
uint32_t g_dump_frames = 0;
float g_gain=1;

void write_wav_header() {
    uint32_t data = g_dump_frames * 4;
    uint8_t h[44];
    auto u32 = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); };
    auto u16 = [&](int o, uint16_t v) { memcpy(h + o, &v, 2); };
    memcpy(h, "RIFF", 4);
    u32(4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, 2);
    u32(24, kRate);
    u32(28, kRate * 4);
    u16(32, 4);
    u16(34, 16);
    memcpy(h + 36, "data", 4);
    u32(40, data);
    long pos = ftell(g_dump);
    fseek(g_dump, 0, SEEK_SET);
    fwrite(h, 1, 44, g_dump);
    fseek(g_dump, pos, SEEK_SET);
    fflush(g_dump);
}

void pull(int16_t* out,uint32_t frames) {
    uint32_t r = g_read.load(std::memory_order_relaxed), w = g_write.load(std::memory_order_acquire);
    if (g_flush.exchange(false)) r = w;
    uint32_t avail = w - r, n = std::min<uint32_t>(avail, frames);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (r + i) & (kCapacity - 1);
        out[i * 2] = g_ring[idx * 2];
        out[i * 2 + 1] = g_ring[idx * 2 + 1];
    }
    if (n < frames) {  // underrun: silence
        memset(out + n * 2, 0, (frames - n) * 4);
        g_underrun += frames - n;
    }
    g_read.store(r + n, std::memory_order_release);
}
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
OSStatus output_changed(AudioObjectID,UInt32,const AudioObjectPropertyAddress*,void*) {
    audio::flush();return noErr;
}
OSStatus render(void*,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32 frames,AudioBufferList* io) {
    pull((int16_t*)io->mBuffers[0].mData,frames);return noErr;
}
#else
void SDLCALL render(void*,SDL_AudioStream* stream,int additional,int) {
    // SDL requests input bytes in our configured S16/stereo format. Keep the callback bounded.
    int16_t samples[1024*2];
    while(additional>0) { uint32_t frames=std::min(additional/4,1024);if(!frames)break;
      pull(samples,frames);if(!SDL_PutAudioStreamData(stream,samples,(int)frames*4))break;additional-=(int)frames*4;
    }
}
#endif

}  // namespace

void init() {
    if (g_started.exchange(true)) return;
    if (const char* p = getenv("WWHD_AUDIO_DUMP")) {
        g_dump = fopen(p, "wb");
        if (g_dump) {
            write_wav_header();
            fseek(g_dump, 44, SEEK_SET);
        }
    }
    if(const char* v=getenv("WWHD_AUDIO_VOLUME")){float gain=float(atof(v));g_gain=std::isfinite(gain)?std::clamp(gain,0.0f,1.0f):1.0f;}
    if (getenv("WWHD_NO_AUDIO")) return;

#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &g_unit) != noErr) {
        LOG("[audio] no output device");
        g_unit = nullptr;
        return;
    }
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = kRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mBytesPerPacket = 4;
    AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    AURenderCallbackStruct cb{render, nullptr};
    AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    // debug: WWHD_AUDIO_VOLUME=0..1 scales the device volume (e.g. silent tests of the real output path)
    // Master gain is applied to the common producer mix, including guest PCM.
    if (AudioUnitInitialize(g_unit) != noErr || AudioOutputUnitStart(g_unit) != noErr) {
        LOG("[audio] failed to start output unit");
        return;
    }
    AudioObjectPropertyAddress changed{kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListener(kAudioObjectSystemObject,&changed,output_changed,nullptr);
    LOG("[audio] CoreAudio output started (48 kHz stereo)");
#else
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO)){LOG("[audio] SDL audio initialization: %s",SDL_GetError());return;}
    SDL_AudioSpec spec{SDL_AUDIO_S16,2,kRate};
    g_unit=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,render,nullptr);
    if(!g_unit){LOG("[audio] no output device: %s",SDL_GetError());return;}

    if(!SDL_ResumeAudioStreamDevice(g_unit)){LOG("[audio] failed to start: %s",SDL_GetError());SDL_DestroyAudioStream(g_unit);g_unit=nullptr;return;}
    LOG("[audio] SDL output started (48 kHz stereo)");
#endif
}

void push(const int16_t* stereo, int frames) {
    // The game producer supplies a bounded AX frame. Preserve the caller's data.
    constexpr int chunk=2048;
    if(frames<=0)return;
    if(frames>chunk){for(int i=0;i<frames;i+=chunk)push(stereo+i*2,std::min(chunk,frames-i));return;}
    // Mods' PCM streams are mixed in game time, before fast forward, so they speed up with the game.
    int16_t mixed[chunk*2];
    if(guestmods::pcm::store().active()||g_gain!=1) {
        memcpy(mixed,stereo,size_t(frames)*4);
        guestmods::pcm::store().mix(mixed,uint32_t(frames),g_gain==0);
        if(g_gain!=1)for(int i=0;i<frames*2;++i)mixed[i]=int16_t(mixed[i]*g_gain);
        stereo=mixed;
    }
    // AX still processes every 3 ms guest frame and every callback. The output device
    // stays at 48 kHz: downsample the accelerated stream (pitch rises with speed).
    std::vector<int16_t> accelerated;
    const unsigned rate = game_clock::rate();
    if (rate > 1) {
        const int count = frames / rate;
        accelerated.resize(count * 2);
        const bool silent = mods::fast_forward_mute();
        for (int i = 0; i < count; ++i) {
            int l = 0, r = 0;
            for (unsigned j = 0; j < rate; ++j) {
                l += stereo[(i * rate + j) * 2];
                r += stereo[(i * rate + j) * 2 + 1];
            }
            accelerated[i * 2] = silent ? 0 : l / int(rate);
            accelerated[i * 2 + 1] = silent ? 0 : r / int(rate);
        }
        stereo = accelerated.data();
        frames = count;
    }
    if (g_dump) {
        fwrite(stereo, 4, frames, g_dump);
        g_dump_frames += frames;
        if (g_dump_frames % kRate < (uint32_t)frames) write_wav_header();  // keep the file valid about once a second
    }
    if (!g_unit) return;
    uint32_t w = g_write.load(std::memory_order_relaxed), r = g_read.load(std::memory_order_acquire);
    if (w - r + frames > kCapacity) {  // device stalled: drop rather than overwrite
        g_dropped += frames;
        return;
    }
    for (int i = 0; i < frames; i++) {
        uint32_t idx = (w + i) & (kCapacity - 1);
        g_ring[idx * 2] = stereo[i * 2];
        g_ring[idx * 2 + 1] = stereo[i * 2 + 1];
    }
    g_write.store(w + frames, std::memory_order_release);
}

int buffered_frames() {
    if (!g_unit) return kTarget;
    return (int)(g_write.load(std::memory_order_acquire) - g_read.load(std::memory_order_acquire));
}

int target_frames() { return kTarget; }

void flush() { g_flush = true; guestmods::pcm::store().reset(); }

void stats(uint64_t& underrun, uint64_t& dropped) {
    underrun = g_underrun;
    dropped = g_dropped;
}

}  // namespace audio
