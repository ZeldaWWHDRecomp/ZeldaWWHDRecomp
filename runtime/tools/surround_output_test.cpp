// Actual AX buses -> merge -> final-mix callbacks -> upsampling -> producer -> WAV.
// The fixture provides guest scratch memory and a synthetic callback, never game files.
#include "runtime.h"
#undef HLE
#define HLE(lib, name) static void hle_impl_##lib##_##name(Cpu *c)
#include "../src/hle/ax.cpp"
#include "mods/guest_audio.h"
#include <cassert>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ax.cpp's sound trace reads the host clock; Windows' linker keeps that code (no section GC there)
namespace timebase { uint64_t now() { return 0; } }
namespace hostui {
bool get(const char *, std::string &value) {
    value = "surround";
    return true;
}
void set(const char *, const std::string &) {}
} // namespace hostui
namespace mods {
bool fast_forward_mute() { return false; }
} // namespace mods
namespace remote_pad {
bool audio_wanted() { return false; }
void send_audio(const int32_t *, const int32_t *, int, int) {}
} // namespace remote_pad
void log_msg(const char *, ...) {}
namespace mem {
uint32_t runtime_alloc(uint32_t size, uint32_t align) {
    static uint32_t next = 0x1000;
    next = (next + align - 1) & ~(align - 1);
    uint32_t a = next;
    next += size;
    assert(next < 0x100000);
    return a;
}
uint32_t host_alloc(uint32_t size, uint32_t align) { return runtime_alloc(size, align); }
} // namespace mem
static bool callback = false;
uint32_t guest_call(Cpu *, uint32_t fn, std::initializer_list<uint32_t> args) {
    assert(fn == 1 && args.size() == 1);
    const uint32_t param = *args.begin();
    assert(ld16(param + 4) == 6 && ld16(param + 8) == 1);
    callback = true;
    // Write known content to AX centre, after the bus merge. It must reach host FC.
    uint32_t centre = ld32(ld32(param) + 4 * 4);
    for (int i = 0; i < ld16(param + 6); ++i)
        st32(centre + 4 * i, 7000);
    return 0;
}
static void env(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
static uint32_t le(const unsigned char *p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i)
        v |= uint32_t(p[i]) << (i * 8);
    return v;
}
int main(int argc, char **argv) {
    assert(argc == 3 || argc == 4);
    const int channels = argc == 4 ? 2 : 6;
    const int header_size = channels == 2 ? 44 : 68;
#ifdef _WIN32
    void *memory = VirtualAlloc(PPC_MEM_BASE, 0x100000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *memory = mmap(PPC_MEM_BASE, 0x100000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
#endif
    assert(memory == PPC_MEM_BASE);
    env("WWHD_NO_AUDIO", "1");
    env("WWHD_AUDIO_OUTPUT", "auto");
    env("WWHD_AUDIO_DUMP", argv[1]);
    env("WWHD_AUDIO_VOLUME", "1");
    env("WWHD_AUDIO_SPEAKERS", channels == 2 ? "stereo" : "surround");
    audio::init();
    assert(audio::channels() == channels);
    Cpu cpu{};
    cpu.r[3] = 0;
    cpu.r[4] = 0x800;
    hle_impl_snd_core_AXGetDeviceMode(&cpu);
    assert(ld32(0x800) == (channels == 6 ? 3 : 0));
    cpu.r[3] = 1;
    hle_impl_snd_core_AXGetDeviceMode(&cpu);
    assert(ld32(0x800) == 0);
    // Full snapshots cache the game's mixer mode. Reject a cross-mode restore before
    // it can silently make a surround session emit stereo (normal/portable saves work).
    ss::Writer snapshot;
    ax_ss_save(snapshot);
    std::string why;
    assert(ax_ss_check(ss::Reader(snapshot.b.data(), snapshot.b.size()), why));
    snapshot.b.back() = channels == 6 ? 2 : 6;
    assert(!ax_ss_check(ss::Reader(snapshot.b.data(), snapshot.b.size()), why));
    snapshot.b.pop_back();
    assert(ax_ss_check(ss::Reader(snapshot.b.data(), snapshot.b.size()), why) == (channels == 2));
    init_buffers();
    // Real decoding and accumulation: any TV bus or pending TV ramp excludes the voice.
    auto& voice = g_voices[0];
    voice = Voice{}; voice.acquired = true; voice.state = 1; voice.format = PCM16;
    voice.filter = FILTER_NONE; voice.samples = 0xF0000; voice.cur_abs = voice.loop_abs = 0x78000;
    voice.end_abs = 0x7805F; voice.loop = 1; voice.vpb = 0xE0000;
    for (int i = 0; i < kSamples; ++i) st16(0xF0000 + 2 * i, 1000);
    g_drc.mix[0][0][0] = {16384, 1};
    process_voices();
    assert(g_gamepad_only[0][0] > 500 * 256 && g_gamepad_only[1][0] == 0);
    assert(g_drc.mix[0][0][0].vol == 16480); // advanced once, not twice
    for (int ch = 0; ch < kTvChannels; ++ch)
        for (int bus = 0; bus < kBuses; ++bus) {
            voice.tv[ch][bus] = {0, 1};
            process_voices();
            assert(g_gamepad_only[0][50] == 0);
            voice.tv[ch][bus] = {};
        }
    voice.tv[0][0] = {1, 0};
    process_voices(); assert(g_gamepad_only[0][50] == 0);
    voice = Voice{};
    memset(g_tv_bus, 0, sizeof g_tv_bus); memset(g_drc_bus, 0, sizeof g_drc_bus);
    memset(g_gamepad_only, 0, sizeof g_gamepad_only);
    g_upsample_stage[0] = atoi(argv[2]);
    g_upsample_stage[1] = atoi(argv[2]);
    // One second per AX channel, plus callback and GamePad checks. 334*144 frames
    // avoids truncating each second to a non-integral 3 ms audio frame.
    constexpr int blocks = 334, span = blocks * 144;
    for (int ax = 0; ax < 6; ++ax) {
        memset(g_tv_bus, 0, sizeof g_tv_bus);
        memset(g_up_hist, 0, sizeof g_up_hist);
        memset(g_post_hist, 0, sizeof g_post_hist);
        for (int i = 0; i < 96; ++i)
            g_tv_bus[0][ax][i] = 1000 * 256;
        for (int f = 0; f < blocks; ++f)
            output_frame(&cpu);
    }
    memset(g_tv_bus, 0, sizeof g_tv_bus);
    memset(g_up_hist, 0, sizeof g_up_hist);
    memset(g_post_hist, 0, sizeof g_post_hist);
    g_final_mix_cb[0] = 1;
    for (int f = 0; f < blocks; ++f)
        output_frame(&cpu);
    assert(callback);
    g_final_mix_cb[0] = 0;
    memset(g_up_hist, 0, sizeof g_up_hist);
    memset(g_post_hist, 0, sizeof g_post_hist);
    g_output.play_tv = false;
    g_output.play_drc = true;
    for (int i = 0; i < 96; ++i) {
        g_drc_bus[0][0][i] = 2000 * 256;
        g_drc_bus[0][1][i] = 3000 * 256;
    }
    for (int f = 0; f < blocks; ++f)
        output_frame(&cpu);
    // Host-only sounds: full, half, disabled, "both", Off-TV, and zero gain.
    memset(g_tv_bus, 0, sizeof g_tv_bus);
    for (int i = 0; i < 96; ++i) {
        g_gamepad_only[0][i] = 4000 * 256;
        g_gamepad_only[1][i] = 6000 * 256;
        // A shared DRC signal must never leak into TV play alongside the selected voices.
        g_drc_bus[0][0][i] = 1000 * 256;
        g_drc_bus[0][1][i] = 2000 * 256;
    }
    for (int segment = 0; segment < 6; ++segment) {
        audio::Fader tv{1, 1, 0}, pad{};
        if (segment == 3) pad = {1, 1, 0};
        if (segment == 4) { tv = {}; pad = {1, 1, 0}; }
        g_output.update(true, tv, pad);
        audio::set_gamepad_sounds(segment != 2);
        audio::set_gamepad_sounds_volume(segment == 1 ? 0.5f : segment == 5 ? 0.f : 1.f, false);
        for (int f = 0; f < blocks; ++f) output_frame(&cpu);
    }
    memset(g_gamepad_only, 0, sizeof g_gamepad_only);
    // Flush valid header and stdio before inspecting the still-open dump.
    // A further whole second of silence also checks that all histories settle.
    memset(g_drc_bus, 0, sizeof g_drc_bus);
    for (int f = 0; f < blocks; ++f)
        output_frame(&cpu);
    audio::finish_dump();
    std::ifstream in(argv[1], std::ios::binary);
    unsigned char h[68];
    in.read(reinterpret_cast<char *>(h), header_size);
    assert(!memcmp(h, "RIFF", 4) && le(h + 22, 2) == channels);
    if (channels == 6) assert(le(h + 20, 2) == 0xfffe && le(h + 40, 4) == 0x3f);
    // Independently specified AX->host destinations, from Cemu's submit assignments.
    constexpr int destination[6] = {0, 1, 4, 5, 2, 3};
    for (int segment = 0; segment < 14; ++segment) {
        in.seekg(header_size + int64_t(segment * span + 100) * channels * 2);
        for (int frame = 100; frame < span - 100; ++frame) {
            int16_t samples[6];
            in.read(reinterpret_cast<char *>(samples), channels * 2);
            assert(in.good());
            for (int ch = 0; ch < channels; ++ch) {
                int expected = segment < 6    ? (ch == destination[segment] ? 1000 : 0)
                               : segment == 6 ? (ch == 2 ? 7000 : 0)
                                              : (ch == 0   ? 2000
                                                 : ch == 1 ? 3000
                                                           : 0);
                if (segment >= 8) {
                    const int gain = segment == 8 ? 2 : segment == 9 ? 1 : 0;
                    expected = ch == 0 ? 2000 * gain : ch == 1 ? 3000 * gain : 0;
                    if (segment == 12) expected = ch == 0 ? 1000 : ch == 1 ? 2000 : 0;
                }
                assert(samples[ch] == expected);
            }
        }
    }
    in.close();
    std::remove(argv[1]);
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, 0x100000);
#endif
}
