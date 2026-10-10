// Run the actual backend negotiation and callback against a fake device. No speakers/drivers.
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdint>
static int capacity = 6, configured = 0;
static bool reject_layout = false, reject_start = false, opened_stereo = false;
static bool has_saved = true;
static std::string saved = "surround", written;
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
static AURenderCallbackStruct callback{};
static AudioComponent fake_find(AudioComponent, const AudioComponentDescription *) {
    return reinterpret_cast<AudioComponent>(1);
}
static OSStatus fake_new(AudioComponent, AudioComponentInstance *unit) {
    *unit = reinterpret_cast<AudioComponentInstance>(1);
    return noErr;
}
static OSStatus fake_dispose(AudioComponentInstance) { return noErr; }
static OSStatus fake_size(AudioObjectID, const AudioObjectPropertyAddress *, UInt32, const void *, UInt32 *size) {
    *size = sizeof(AudioBufferList);
    return noErr;
}
static OSStatus fake_get(AudioObjectID, const AudioObjectPropertyAddress *a, UInt32, const void *, UInt32 *,
                         void *out) {
    if (a->mSelector == kAudioHardwarePropertyDefaultOutputDevice)
        *static_cast<AudioDeviceID *>(out) = 1;
    else {
        auto *b = static_cast<AudioBufferList *>(out);
        b->mNumberBuffers = 1;
        b->mBuffers[0].mNumberChannels = capacity;
    }
    return noErr;
}
static OSStatus fake_set(AudioUnit, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement, const void *value,
                         UInt32) {
    if (id == kAudioUnitProperty_StreamFormat)
        configured = static_cast<const AudioStreamBasicDescription *>(value)->mChannelsPerFrame;
    if (id == kAudioUnitProperty_SetRenderCallback)
        callback = *static_cast<const AURenderCallbackStruct *>(value);
    if (id == kAudioUnitProperty_AudioChannelLayout && configured == 6) {
        const auto *layout = static_cast<const AudioChannelLayout *>(value);
        assert(layout->mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelDescriptions &&
               layout->mNumberChannelDescriptions == 6);
        const AudioChannelLabel expected[] = {kAudioChannelLabel_Left,         kAudioChannelLabel_Right,
                                              kAudioChannelLabel_Center,       kAudioChannelLabel_LFEScreen,
                                              kAudioChannelLabel_LeftSurround, kAudioChannelLabel_RightSurround};
        for (int i = 0; i < 6; ++i)
            assert(layout->mChannelDescriptions[i].mChannelLabel == expected[i]);
        if (reject_layout)
            return -1;
    }
    return noErr;
}
static OSStatus fake_init(AudioUnit) { return reject_start && configured == 6 ? -1 : noErr; }
static OSStatus fake_start(AudioUnit) { return noErr; }
static OSStatus fake_stop(AudioUnit) { return noErr; }
static OSStatus fake_uninit(AudioUnit) { return noErr; }
static OSStatus fake_listener(AudioObjectID, const AudioObjectPropertyAddress *, AudioObjectPropertyListenerProc,
                              void *) {
    return noErr;
}
#define AudioComponentFindNext fake_find
#define AudioComponentInstanceNew fake_new
#define AudioComponentInstanceDispose fake_dispose
#define AudioObjectGetPropertyDataSize fake_size
#define AudioObjectGetPropertyData fake_get
#define AudioUnitSetProperty fake_set
#define AudioUnitInitialize fake_init
#define AudioOutputUnitStart fake_start
#define AudioOutputUnitStop fake_stop
#define AudioUnitUninitialize fake_uninit
#define AudioObjectAddPropertyListener fake_listener
#else
#include <SDL3/SDL.h>
static SDL_AudioStreamCallback callback = nullptr;
static std::vector<int16_t> received;
static bool fake_init(SDL_InitFlags) { return true; }
static bool fake_format(SDL_AudioDeviceID, SDL_AudioSpec *spec, int *) {
    *spec = {SDL_AUDIO_S16, opened_stereo && configured == 6 ? 2 : capacity, 48000};
    return true;
}
static SDL_AudioStream *fake_open(SDL_AudioDeviceID, const SDL_AudioSpec *spec, SDL_AudioStreamCallback cb, void *) {
    configured = spec->channels;
    callback = cb;
    return reinterpret_cast<SDL_AudioStream *>(1);
}
static bool fake_resume(SDL_AudioStream *) { return !(reject_start && configured == 6); }
static void fake_destroy(SDL_AudioStream *) {}
static bool fake_put(SDL_AudioStream *, const void *data, int len) {
    const auto *p = static_cast<const int16_t *>(data);
    received.insert(received.end(), p, p + len / 2);
    return true;
}
static SDL_AudioDeviceID fake_device(SDL_AudioStream *) { return 1; }
#define SDL_InitSubSystem fake_init
#define SDL_GetAudioDeviceFormat fake_format
#define SDL_OpenAudioDeviceStream fake_open
#define SDL_ResumeAudioStreamDevice fake_resume
#define SDL_DestroyAudioStream fake_destroy
#define SDL_PutAudioStreamData fake_put
#define SDL_GetAudioStreamDevice fake_device
#endif
#include "../src/audio_out.cpp"
namespace hostui {
bool get(const char *, std::string &s) {
    s = saved;
    return has_saved;
}
void set(const char *key, const std::string &value) {
    assert(!strcmp(key, "audioSpeakers"));
    written = value;
}
} // namespace hostui
namespace mods {
bool fast_forward_mute() { return false; }
} // namespace mods
void log_msg(const char *, ...) {}
int main(int argc, char **argv) {
    assert(argc == 2);
    std::string mode = argv[1];
    capacity = mode == "stereo-device" ? 2 : 6;
    reject_layout = mode == "layout-failure";
    reject_start = mode == "start-failure";
    opened_stereo = mode == "opened-stereo";
    has_saved = mode != "default-stereo";
    const bool wants_surround = mode != "default-stereo" && mode != "override-stereo";
#ifdef _WIN32
    _putenv_s("WWHD_NO_AUDIO", "");
    _putenv_s("WWHD_AUDIO_DUMP", "");
    _putenv_s("WWHD_AUDIO_VOLUME", "1");
    _putenv_s("WWHD_AUDIO_SPEAKERS", mode == "override-stereo" ? "stereo" : "");
#else
    unsetenv("WWHD_NO_AUDIO");
    unsetenv("WWHD_AUDIO_DUMP");
    setenv("WWHD_AUDIO_VOLUME", "1", 1);
    unsetenv("WWHD_AUDIO_SPEAKERS");
    if (mode == "override-stereo")
        setenv("WWHD_AUDIO_SPEAKERS", "stereo", 1);
#endif
    audio::init();
    int n = audio::channels();
    assert(n == configured);
    assert(n == ((!wants_surround || capacity < 6 || reject_layout || reject_start || opened_stereo) ? 2 : 6));
    assert(audio::surround_fallback() == (wants_surround && n == 2));
    assert(audio::requested_surround() == wants_surround);
    audio::set_requested_surround(!wants_surround);
    assert(audio::requested_surround() != wants_surround && audio::channels() == n);
    assert(written == (wants_surround ? "stereo" : "surround"));
    int16_t samples[144 * 6]{};
    for (int i = 0; i < 144; ++i)
        for (int ch = 0; ch < n; ++ch)
            samples[i * n + ch] = int16_t(1000 * (ch + 1));
    audio::push(samples, 144);
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
    int16_t out[144 * 6]{};
    AudioBufferList buffers{};
    buffers.mNumberBuffers = 1;
    buffers.mBuffers[0] = {UInt32(n), UInt32(144 * n * 2), out};
    AudioUnitRenderActionFlags flags = 0;
    AudioTimeStamp timestamp{};
    callback.inputProc(&flags, &flags, &timestamp, 0, 144, &buffers);
    assert(!memcmp(samples, out, 144 * n * 2));
    audio::output_changed(0, 0, nullptr, nullptr);
    if (n == 6) {
        callback.inputProc(&flags, &flags, &timestamp, 0, 144, &buffers);
        for (int16_t s : out)
            assert(s == 0);
        assert(audio::output_device_changed());
    }
#else
    callback(nullptr, reinterpret_cast<SDL_AudioStream *>(1), 144 * n * 2, 0);
    assert(received.size() == size_t(144 * n) && !memcmp(samples, received.data(), 144 * n * 2));
    capacity = 2;
    if (n == 6) {
        received.clear();
        callback(nullptr, reinterpret_cast<SDL_AudioStream *>(1), 144 * n * 2, 0);
        for (auto s : received)
            assert(s == 0);
        assert(audio::output_device_changed());
    }
#endif
}
