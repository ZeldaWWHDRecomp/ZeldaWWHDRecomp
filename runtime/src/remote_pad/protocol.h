// Remote GamePad protocol: an iPhone (ios/) acts as the Wii U GamePad of the game running on this
// computer. The phone sends its controller's buttons and sticks, touches on its screen and its motion
// sensors; the computer answers so the phone knows it is connected. (Video of the GamePad screen is a
// later version of this protocol.)
//
// Transport: UDP, default port 26770, advertised over Bonjour as _wwhd-gamepad._udp. Every datagram
// starts with the 12-byte header below. All integers and floats are little-endian. The phone keeps
// sending input at about 120 Hz even when nothing changes, so a lost datagram costs one sample and
// silence means the phone is gone.
//
//   phone -> computer   kHello      header (session 0), nonce u32, device name, controller name
//   computer -> phone   kWelcome    header (session), nonce u32 (the Hello's), computer name
//                                   (also sent about once a second as a keepalive)
//   phone -> computer   kInput      header (session), Input (kInputSize bytes)
//   either              kBye        header (session): the other side may forget the session now
//
// Names are a u8 byte length then UTF-8, at most kMaxName bytes.
//
// This file is plain C++ without sockets so the packets are unit-tested (runtime/tools/remote_pad_test.cpp).
// The Swift side (ios/WWHDGamePad/Protocol.swift) mirrors it; change both together and bump kVersion.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace remote_pad {

constexpr uint8_t kMagic[4] = {'W', 'W', 'G', 'P'};
constexpr uint16_t kVersion = 1;
constexpr uint16_t kDefaultPort = 26770;
constexpr const char* kBonjourType = "_wwhd-gamepad._udp";
constexpr size_t kHeaderSize = 12;
constexpr size_t kMaxName = 63;

enum Type : uint8_t { kHello = 1, kWelcome = 2, kInput = 3, kBye = 4 };

struct Header {
    uint16_t version = kVersion;
    uint8_t type = 0;
    uint8_t flags = 0;      // none defined yet: 0
    uint32_t session = 0;   // 0 in a Hello; the Welcome's afterwards
};

// Buttons by position (Xbox names, as GameController's extended gamepad: A is the bottom face button)
enum Button : uint32_t {
    kA = 1u << 0, kB = 1u << 1, kX = 1u << 2, kY = 1u << 3,
    kLB = 1u << 4, kRB = 1u << 5, kL3 = 1u << 6, kR3 = 1u << 7,
    kMenu = 1u << 8, kOptions = 1u << 9, kHome = 1u << 10,
    kDUp = 1u << 11, kDDown = 1u << 12, kDLeft = 1u << 13, kDRight = 1u << 14,
    kCapture = 1u << 15,
};

struct Input {
    uint32_t seq = 0;              // +1 per Input; the computer drops older or repeated ones
    uint64_t time_ns = 0;          // the phone's monotonic clock when sent
    uint32_t buttons = 0;          // Button bits held
    float lx = 0, ly = 0, rx = 0, ry = 0;  // sticks -1..1, +y = up (sent as int16)
    float lt = 0, rt = 0;          // triggers 0..1 (sent as uint16)
    bool controller = false;       // a controller is attached to the phone (else the values above are 0)
    bool touch = false;            // a finger is on the GamePad screen area
    float tx = 0, ty = 0;          // its position, 0..1 from the top left of the GamePad picture (uint16)
    bool motion = false;           // the fields below hold a sample
    uint8_t quarter_turns = 0;     // screen rotation from the phone's natural (portrait) orientation,
                                   // counter-clockwise: landscape with the top to the left = 1
    uint64_t motion_ns = 0;        // the sensor sample's time (phone clock)
    float gyro[3] = {};            // rad/s, the phone's natural axes (x right, y up, z out of the screen)
    float accel[3] = {};           // m/s^2, same axes, specific force (+9.81 on z lying face up): the
                                   // Android / SDL convention; CoreMotion's value times -9.80665
    bool operator==(const Input&) const = default;
};
// header + flags u8 + quarter_turns u8 + seq u32 + time u64 + buttons u32 + 4 x i16 + 2 x u16 +
// 2 x u16 touch + motion time u64 + 6 x f32
constexpr size_t kInputSize = kHeaderSize + 1 + 1 + 4 + 8 + 4 + 8 + 4 + 4 + 8 + 24;

struct Hello { uint32_t nonce = 0; std::string device, controller; bool operator==(const Hello&) const = default; };
struct Welcome { uint32_t nonce = 0; std::string host; bool operator==(const Welcome&) const = default; };

std::vector<uint8_t> encode_hello(const Hello& h);
std::vector<uint8_t> encode_welcome(uint32_t session, const Welcome& w);
std::vector<uint8_t> encode_input(uint32_t session, const Input& in);
std::vector<uint8_t> encode_bye(uint32_t session);

// nullopt: not one of ours (wrong magic, version, size)
std::optional<Header> parse_header(const uint8_t* data, size_t size);
std::optional<Hello> parse_hello(const uint8_t* data, size_t size);
std::optional<Welcome> parse_welcome(const uint8_t* data, size_t size);
std::optional<Input> parse_input(const uint8_t* data, size_t size);

// seq is newer than last (wraps): the computer keeps only newer inputs
inline bool newer(uint32_t seq, uint32_t last) { return int32_t(seq - last) > 0; }

}  // namespace remote_pad
