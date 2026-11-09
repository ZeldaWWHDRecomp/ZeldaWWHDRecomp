// An iPhone as the Wii U GamePad (ios/, protocol in protocol.h). While a phone is connected:
//   - the controller attached to it is one more host controller (input::controllers(), its own
//     bindings in Controls by its name, "iPhone GamePad"), so remapping and prompts work as for any pad;
//   - a finger on its screen is the GamePad's touch screen;
//   - its motion sensors are the GamePad's gyro under Settings > Controls > Gyro > "Device gyro";
//   - it shows the GamePad screen: the renderer encodes the picture it draws for the GamePad window
//     (gfx/remote_video.mm, Metal) and sends it here.
//
// Off by default. WWHD_REMOTE_PAD=1 (or =<port>) starts it at launch: the game then listens on UDP
// port 26770 (remote_pad::kDefaultPort) and advertises itself over Bonjour so the phone finds it.
// Anyone on the local network can then send GamePad input to the game.
//
// Host: the macOS AppKit host (gfx/input.mm) reads it. Threads: all functions are safe from any thread.
#pragma once
#include "protocol.h"

#include <cstdint>
#include <string>

namespace remote_pad {

void start_from_env();  // once at startup (input::init); does nothing unless WWHD_REMOTE_PAD is set
void stop();

// the id the host lists the phone's controller under (distinct from any real device pointer or id)
constexpr uint64_t kDeviceId = 0x5757'4750'0000'0001ull;  // "WWGP"
constexpr const char* kControllerName = "iPhone GamePad";

struct Pad {
    bool connected = false;  // a phone has the GamePad
    std::string device;      // the phone's name, e.g. "Test iPhone"
    std::string controller;  // the controller attached to it ("" for none)
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

}  // namespace remote_pad
