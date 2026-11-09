// Remote GamePad protocol: an iPhone (ios/) acts as the Wii U GamePad of the game running on this
// computer. The phone sends its controller's buttons and sticks, touches on its screen and its motion
// sensors; the computer answers so the phone knows it is connected and, when the phone asks for it,
// sends it the GamePad screen as H.264 video.
//
// Transport: UDP, default port 26770, advertised over Bonjour as _wwhd-gamepad._udp. Every datagram
// starts with the 12-byte header below. All integers and floats are little-endian. The phone keeps
// sending input at about 120 Hz even when nothing changes, so a lost datagram costs one sample and
// silence means the phone is gone.
//
//   phone -> computer   kHello      header (session 0), nonce u32, device name, controller name,
//                                   phone id [16], token [16] (all zero before pairing; pairing.h)
//   computer -> phone   kWelcome    header (session), nonce u32 (the Hello's), computer name
//                                   (also sent about once a second as a keepalive)
//   phone -> computer   kInput      header (session), Input (kInputSize bytes)
//   either              kBye        header (session): the other side may forget the session now
//   computer -> phone   kVideo      header (session), video fields, one piece of an encoded frame
//                                   (only after a Hello with kWantsVideo in its header flags)
//   phone -> computer   kKeyframe   header (session): a piece was lost; make the next frame a keyframe
//   computer -> phone   kRumble     header (session), seq u32, level u8: the GamePad motor's strength
//                                   (0 = still, 255 = full; rumble.h's host level). Sent when it changes
//                                   (at most 120 a second), again every 100 ms while it is not 0, and a
//                                   stop twice; the phone stops by itself after kRumbleStaleMs without one.
//   computer -> phone   kAudio      header (session), seq u32, flags u8 (1: silent), 0 u8, frames u16,
//                                   then frames x (left, right) int16 unless silent: the GamePad speaker,
//                                   48 kHz stereo, one game audio frame (3 ms, 144 frames) per packet.
//                                   Only after a Hello with kWantsAudio; the computer then plays the TV
//                                   mix alone, as a Wii U does with its GamePad speaker.
//
// Pairing (pairing.h), all with session 0, answering the Hello's nonce:
//   computer -> phone   kPairing    header, nonce u32, state u8 (PairState), attempts left u8: the
//                                   computer shows a code (or the last one was wrong, or none is pending)
//   phone -> computer   kPair       header, nonce u32, phone id [16], code u32: the code typed in
//   computer -> phone   kPaired     header, nonce u32, token [16]: trusted; say Hello with this token
//
// A Hello repeated with the same nonce keeps the session and updates kWantsVideo / kWantsAudio (the
// phone's switches).
//
// Video: H.264, no frame reordering. A frame is its NAL units, each with a 4-byte big-endian length
// (AVCC); a keyframe starts with its SPS and PPS. A frame is cut into pieces of at most kMaxVideoPayload
// bytes, sent in order; the phone shows a frame once it has all its pieces and drops the rest until the
// next keyframe after a loss. width/height: the picture's size, which is the GamePad screen (854 x 480)
// or that shape at a higher resolution.
//
// Names are a u8 byte length then UTF-8, at most kMaxName bytes.
//
// Tested in runtime/tools/remote_pad_test.cpp. ios/WWHDGamePad/Protocol.swift mirrors this file: change
// both together and bump kVersion.
#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
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
constexpr size_t kMaxDatagram = 1400;  // below the Wi-Fi MTU with IPv6 and UDP headers

enum Type : uint8_t { kHello = 1, kWelcome = 2, kInput = 3, kBye = 4, kVideo = 5, kKeyframe = 6, kRumble = 7, kAudio = 8,
                      kPairing = 9, kPair = 10, kPaired = 11 };
enum HelloFlag : uint8_t { kWantsVideo = 1, kWantsAudio = 2 };  // a Hello's header flags
enum PairState : uint8_t { kEnterCode = 1, kWrongCode = 2, kNoCode = 3 };
constexpr uint32_t kRumbleStaleMs = 250;
using Bytes16 = std::array<uint8_t, 16>;

struct Header {
    uint16_t version = kVersion;
    uint8_t type = 0;
    uint8_t flags = 0;      // a Hello: HelloFlag bits; others: 0
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

struct Hello {
    uint32_t nonce = 0;
    std::string device, controller;
    bool video = false;  // kWantsVideo
    bool audio = false;  // kWantsAudio
    Bytes16 id{}, token{};  // the phone's (pairing.h); zero from a phone that never paired
    bool operator==(const Hello&) const = default;
};
struct Welcome { uint32_t nonce = 0; std::string host; bool operator==(const Welcome&) const = default; };

std::vector<uint8_t> encode_hello(const Hello& h);
std::vector<uint8_t> encode_welcome(uint32_t session, const Welcome& w);
std::vector<uint8_t> encode_input(uint32_t session, const Input& in);
std::vector<uint8_t> encode_bye(uint32_t session);
std::vector<uint8_t> encode_keyframe_request(uint32_t session);

struct Rumble { uint32_t seq = 0; uint8_t level = 0; };
std::vector<uint8_t> encode_rumble(uint32_t session, uint32_t seq, uint8_t level);
std::optional<Rumble> parse_rumble(const uint8_t* data, size_t size);

struct PairingReply { uint32_t nonce = 0; uint8_t state = 0; uint8_t attempts = 0; };
struct PairRequest { uint32_t nonce = 0; Bytes16 id{}; uint32_t code = 0; };
struct Paired { uint32_t nonce = 0; Bytes16 token{}; };
std::vector<uint8_t> encode_pairing(const PairingReply& p);
std::vector<uint8_t> encode_pair(const PairRequest& p);
std::vector<uint8_t> encode_paired(const Paired& p);
std::optional<PairingReply> parse_pairing(const uint8_t* data, size_t size);
std::optional<PairRequest> parse_pair(const uint8_t* data, size_t size);
std::optional<Paired> parse_paired(const uint8_t* data, size_t size);

constexpr size_t kAudioHeaderSize = kHeaderSize + 4 + 1 + 1 + 2;
constexpr uint16_t kMaxAudioFrames = (kMaxDatagram - kAudioHeaderSize) / 4;
// interleaved left/right samples; nullptr (or all zero) sends a silent packet
std::vector<uint8_t> encode_audio(uint32_t session, uint32_t seq, const int16_t* lr, uint16_t frames);
struct AudioPiece {
    uint32_t seq = 0;
    uint16_t frames = 0;
    bool silent = false;
    const uint8_t* data = nullptr;  // frames x 4 bytes (little-endian int16 L, R) unless silent
};
std::optional<AudioPiece> parse_audio(const uint8_t* data, size_t size);

// header + frame u32 + index u16 + count u16 + flags u8 (1: keyframe) + 0 u8 + time u64 + width u16 + height u16
constexpr size_t kVideoHeaderSize = kHeaderSize + 4 + 2 + 2 + 1 + 1 + 8 + 2 + 2;
constexpr size_t kMaxVideoPayload = kMaxDatagram - kVideoHeaderSize;
struct VideoPiece {
    uint32_t frame = 0;         // +1 per frame (wraps)
    uint16_t index = 0, count = 1;
    bool key = false;
    uint64_t time_ns = 0;       // the computer's clock when the frame was drawn
    uint16_t width = 0, height = 0;
    const uint8_t* data = nullptr;  // into the parsed datagram
    size_t size = 0;
};
// the datagrams for one encoded frame (at least one; count <= 65535 pieces)
std::vector<std::vector<uint8_t>> encode_video(uint32_t session, uint32_t frame, bool key, uint64_t time_ns,
                                               uint16_t width, uint16_t height, const uint8_t* data, size_t size);
std::optional<VideoPiece> parse_video(const uint8_t* data, size_t size);

// nullopt: not one of ours (wrong magic, version, size)
std::optional<Header> parse_header(const uint8_t* data, size_t size);
std::optional<Hello> parse_hello(const uint8_t* data, size_t size);
std::optional<Welcome> parse_welcome(const uint8_t* data, size_t size);
std::optional<Input> parse_input(const uint8_t* data, size_t size);

// seq is newer than last (wraps): the computer keeps only newer inputs
inline bool newer(uint32_t seq, uint32_t last) { return int32_t(seq - last) > 0; }

}  // namespace remote_pad
