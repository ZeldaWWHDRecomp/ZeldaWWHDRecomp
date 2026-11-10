// An iPhone as the Wii U GamePad (ios/, protocol in protocol.h). While a phone is connected:
//   - the controller attached to it is one more host controller (input::controllers(), its own
//     bindings in Controls by its name, "iPhone GamePad"), so remapping and prompts work as for any pad;
//   - a finger on its screen is the GamePad's touch screen;
//   - its motion sensors are the GamePad's gyro under Settings > Controls > Gyro > "Device gyro";
//   - its keyboard answers the game's text prompts (the name of a new file; overlay/text_entry.cpp);
//   - it shows the GamePad screen: the renderer encodes the picture it draws for the GamePad window
//     (gfx/remote_video.mm, Metal) and sends it here.
//
// Off by default (on on an Apple TV): Settings > Controls > iPhone GamePad (saved as "remotePad"), or WWHD_REMOTE_PAD=1
// (or =<port>), which keeps it on whatever the setting says. While on, the game listens on UDP port 26770
// (remote_pad::kDefaultPort) and advertises itself over Bonjour so the phone finds it. A phone must be
// paired first: Settings opens with a code to type on the phone (pairing.h).
//
// Host: the macOS AppKit host (gfx/input.mm) reads it. Threads: all functions are safe from any thread.
#pragma once
#include "protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace remote_pad {

void start_from_env();  // once at startup (input::init); does nothing unless WWHD_REMOTE_PAD is set
void stop();
// stop and listen again (an iPad or iPhone app back from the background, where its socket and its Bonjour
// advertisement were torn down); nothing when the GamePad is off
void restart();

// ---- Settings > Controls > iPhone GamePad ----
void set_enabled(bool on);   // the setting: listen or stop (WWHD_REMOTE_PAD keeps it listening)
bool enabled();              // the setting, or WWHD_REMOTE_PAD
bool env_forced();
void set_host_supported();   // the host reads the phone (gfx/input.mm): Settings offers the option
bool host_supported();
struct Status {
    bool listening = false;
    uint16_t port = 0;
    bool connected = false, video = false, audio = false;
    std::string device, controller;
};
Status status();
// pairing (pairing.h): the code Settings shows while a phone waits for it
struct PairingCode { std::string device; uint32_t code = 0; };
std::optional<PairingCode> pairing_code();
bool take_pairing_request();  // a phone asked for a new code: Settings opens
void refuse_pairing();        // "Don't pair"
size_t paired_phones();
void forget_paired_phones();
// the paired phones as one settings value ("remotePadPhones"): loaded at startup, saved when changed
void load_paired(const std::string& saved);
bool take_paired_changed(std::string& saved);

// the id the host lists the phone's controller under (distinct from any real device pointer or id)
constexpr uint64_t kDeviceId = 0x5757'4750'0000'0001ull;  // "WWGP"
constexpr const char* kControllerName = "iPhone GamePad";

struct Pad {
    bool connected = false;  // a phone has the GamePad
    std::string device;      // the phone's name
    std::string controller;  // the controller attached to it ("" for none)
    ControllerInfo info;     // ... as GameController describes it (protocol.h kControllerInfo)
    Input input;             // the latest input
};
Pad pad();

// the controller part of `in` as input_map::kPadCount host-controller values 0..1 (v is cleared first)
void pad_values(const Input& in, float* v);

// ---- the GamePad screen (protocol.h kVideo) ----
bool video_wanted();         // a connected phone asked for the picture (cheap: once per frame)
bool take_keyframe_request();  // the next frame must be a keyframe (a new phone, or one lost a piece)
// one encoded frame (AVCC, see protocol.h); any thread
void send_video(const uint8_t* data, size_t size, bool key, uint64_t time_ns, uint16_t width, uint16_t height);

// ---- the GamePad speaker (protocol.h kAudio) ----
// The phone plays the GamePad mix: the host then plays the TV mix alone (hle/ax.cpp). Cheap.
bool audio_wanted();
// the AX frame thread, every frame: the GamePad mix after the game's final mix, 48 kHz, s16 range
void send_audio(const int32_t* left, const int32_t* right, int frames);

// ---- the game's text prompt on the phone (protocol.h kText / kTextInput) ----
// The prompt (overlay/text_entry.cpp, render thread): how it is now, sent whenever it changes (cheap
// when it did not). The phone's edits and OK / Cancel queue up until the prompt takes them.
void text_prompt(const TextPrompt& t);
std::vector<TextInput> take_text_inputs();

// ---- the running game's GamePad boot image (protocol.h kBootImage): the phone's background ----
// any thread, once the game's files are known: JPEG bytes (empty: none)
void set_boot_image(std::vector<uint8_t> jpeg);

// ---- rumble (protocol.h kRumble) ----
// The host, every input update (any rate, one thread): the GamePad motor's strength now, 0..1
// (rumble::host_level). Sent to the phone when it changes, repeated while it runs (RumbleSender).
void rumble(float level);

// When to send the strength (plain logic, tested): on a change (at most every kMinGapMs), every
// kRefreshMs while not 0, and a stop kStopRepeats times kStopGapMs apart (a lost stop must not
// leave the phone rumbling until its own stale timeout).
class RumbleSender {
public:
    static constexpr uint64_t kMinGapMs = 8, kRefreshMs = 100, kStopGapMs = 40;
    static constexpr int kStopRepeats = 2;
    // the level to send now, or -1
    int update(float level, uint64_t now_ms);
    void reset() { *this = RumbleSender{}; }
private:
    int sent_ = 0;           // last sent, 0..255
    uint64_t at_ = 0;        // when
    int stops_left_ = 0;     // repeats of a 0 still due
    bool any_ = false;
};

}  // namespace remote_pad
