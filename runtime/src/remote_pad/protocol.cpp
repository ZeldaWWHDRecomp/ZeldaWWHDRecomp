// Remote GamePad packets (protocol.h).
#include "protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace remote_pad {
namespace {

enum InputFlag : uint8_t { kHasController = 1, kHasTouch = 2, kHasMotion = 4 };

struct Writer {
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void f32(float v) { uint32_t b; memcpy(&b, &v, 4); u32(b); }
    void bytes(const Bytes16& b) { out.insert(out.end(), b.begin(), b.end()); }
    void name(const std::string& s) {
        // cut at kMaxName bytes, never inside a UTF-8 sequence
        size_t n = std::min(s.size(), kMaxName);
        while (n > 0 && n < s.size() && (uint8_t(s[n]) & 0xC0) == 0x80) n--;
        u8(uint8_t(n));
        out.insert(out.end(), s.begin(), s.begin() + n);
    }
    void header(Type type, uint32_t session, uint8_t flags = 0) {
        out.insert(out.end(), kMagic, kMagic + 4);
        u16(kVersion); u8(type); u8(flags); u32(session);
    }
};

struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool ok = true;
    bool need(size_t k) { if (n - at < k) ok = false; return ok; }
    uint8_t u8() { if (!need(1)) return 0; return p[at++]; }
    uint16_t u16() { uint16_t lo = u8(); return uint16_t(lo | uint16_t(u8()) << 8); }
    uint32_t u32() { uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
    uint64_t u64() { uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    float f32() { uint32_t b = u32(); float v; memcpy(&v, &b, 4); return std::isfinite(v) ? v : 0.0f; }
    Bytes16 bytes() {
        Bytes16 b{};
        if (need(16)) { memcpy(b.data(), p + at, 16); at += 16; }
        return b;
    }
    std::string name() {
        const size_t len = u8();
        if (len > kMaxName || !need(len)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return s;
    }
};

int16_t to_i16(float v) { return int16_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, -1.0f, 1.0f) * 32767.0f)); }
uint16_t to_u16(float v) { return uint16_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f) * 65535.0f)); }
float from_i16(uint16_t v) { return std::max(float(int16_t(v)) / 32767.0f, -1.0f); }
float from_u16(uint16_t v) { return float(v) / 65535.0f; }

// a packet of `type` (its header checked); the reader is positioned after the header
std::optional<Reader> body(const uint8_t* data, size_t size, Type type) {
    auto h = parse_header(data, size);
    if (!h || h->type != type) return std::nullopt;
    Reader r{data, size};
    r.at = kHeaderSize;
    return r;
}

}  // namespace

std::vector<uint8_t> encode_hello(const Hello& h) {
    Writer w;
    w.header(kHello, 0, uint8_t((h.video ? kWantsVideo : 0) | (h.audio ? kWantsAudio : 0)));
    w.u32(h.nonce); w.name(h.device); w.name(h.controller);
    w.bytes(h.id); w.bytes(h.token);
    return std::move(w.out);
}
std::vector<uint8_t> encode_welcome(uint32_t session, const Welcome& v) {
    Writer w;
    w.header(kWelcome, session);
    w.u32(v.nonce); w.name(v.host);
    return std::move(w.out);
}
std::vector<uint8_t> encode_input(uint32_t session, const Input& in) {
    Writer w;
    w.header(kInput, session);
    w.u8(uint8_t((in.controller ? kHasController : 0) | (in.touch ? kHasTouch : 0) | (in.motion ? kHasMotion : 0)));
    w.u8(in.quarter_turns & 3);
    w.u32(in.seq); w.u64(in.time_ns); w.u32(in.buttons);
    for (float v : {in.lx, in.ly, in.rx, in.ry}) w.u16(uint16_t(to_i16(v)));
    w.u16(to_u16(in.lt)); w.u16(to_u16(in.rt));
    w.u16(to_u16(in.tx)); w.u16(to_u16(in.ty));
    w.u64(in.motion_ns);
    for (float v : in.gyro) w.f32(v);
    for (float v : in.accel) w.f32(v);
    return std::move(w.out);
}
std::vector<uint8_t> encode_bye(uint32_t session) {
    Writer w;
    w.header(kBye, session);
    return std::move(w.out);
}
std::vector<uint8_t> encode_keyframe_request(uint32_t session) {
    Writer w;
    w.header(kKeyframe, session);
    return std::move(w.out);
}
std::vector<uint8_t> encode_rumble(uint32_t session, uint32_t seq, uint8_t level) {
    Writer w;
    w.header(kRumble, session);
    w.u32(seq); w.u8(level);
    return std::move(w.out);
}
std::vector<uint8_t> encode_ping(uint32_t session, uint64_t phone_ns) {
    Writer w;
    w.header(kPing, session);
    w.u64(phone_ns);
    return std::move(w.out);
}
std::vector<uint8_t> encode_pong(uint32_t session, const Pong& p) {
    Writer w;
    w.header(kPong, session);
    w.u64(p.phone_ns); w.u64(p.computer_ns);
    return std::move(w.out);
}
std::vector<uint8_t> encode_pairing(const PairingReply& p) {
    Writer w;
    w.header(kPairing, 0);
    w.u32(p.nonce); w.u8(p.state); w.u8(p.attempts);
    return std::move(w.out);
}
std::vector<uint8_t> encode_pair(const PairRequest& p) {
    Writer w;
    w.header(kPair, 0);
    w.u32(p.nonce); w.bytes(p.id); w.u32(p.code);
    return std::move(w.out);
}
std::vector<uint8_t> encode_paired(const Paired& p) {
    Writer w;
    w.header(kPaired, 0);
    w.u32(p.nonce); w.bytes(p.token);
    return std::move(w.out);
}
std::vector<uint8_t> encode_audio(uint32_t session, uint32_t seq, const int16_t* lr, uint16_t frames) {
    frames = std::min(frames, kMaxAudioFrames);
    bool silent = !lr;
    if (lr) {
        silent = true;
        for (size_t i = 0; i < size_t(frames) * 2 && silent; i++) silent = lr[i] == 0;
    }
    Writer w;
    w.out.reserve(kAudioHeaderSize + (silent ? 0 : size_t(frames) * 4));
    w.header(kAudio, session);
    w.u32(seq); w.u8(silent ? 1 : 0); w.u8(0); w.u16(frames);
    if (!silent)
        for (size_t i = 0; i < size_t(frames) * 2; i++) w.u16(uint16_t(lr[i]));
    return std::move(w.out);
}
std::vector<std::vector<uint8_t>> encode_video(uint32_t session, uint32_t frame, bool key, uint64_t time_ns,
                                               uint16_t width, uint16_t height, const uint8_t* data, size_t size) {
    const size_t count = std::max<size_t>(1, (size + kMaxVideoPayload - 1) / kMaxVideoPayload);
    std::vector<std::vector<uint8_t>> out;
    if (count > 0xFFFF) return out;  // (over 89 MB: never a GamePad frame)
    out.reserve(count);
    for (size_t i = 0; i < count; i++) {
        Writer w;
        w.out.reserve(kMaxDatagram);
        w.header(kVideo, session);
        w.u32(frame); w.u16(uint16_t(i)); w.u16(uint16_t(count));
        w.u8(key ? 1 : 0); w.u8(0); w.u64(time_ns); w.u16(width); w.u16(height);
        const size_t at = i * kMaxVideoPayload, n = std::min(kMaxVideoPayload, size - std::min(size, at));
        if (n) w.out.insert(w.out.end(), data + at, data + at + n);
        out.push_back(std::move(w.out));
    }
    return out;
}

std::optional<Header> parse_header(const uint8_t* data, size_t size) {
    if (!data || size < kHeaderSize || memcmp(data, kMagic, 4) != 0) return std::nullopt;
    Reader r{data, size};
    r.at = 4;
    Header h;
    h.version = r.u16(); h.type = r.u8(); h.flags = r.u8(); h.session = r.u32();
    if (h.version != kVersion) return std::nullopt;
    return h;
}
std::optional<Hello> parse_hello(const uint8_t* data, size_t size) {
    auto r = body(data, size, kHello);
    if (!r) return std::nullopt;
    Hello h;
    h.video = parse_header(data, size)->flags & kWantsVideo;
    h.audio = parse_header(data, size)->flags & kWantsAudio;
    h.nonce = r->u32(); h.device = r->name(); h.controller = r->name();
    if (!r->ok) return std::nullopt;
    if (size - r->at >= 32) { h.id = r->bytes(); h.token = r->bytes(); }  // (absent: a phone before pairing existed)
    return h;
}
std::optional<Welcome> parse_welcome(const uint8_t* data, size_t size) {
    auto r = body(data, size, kWelcome);
    if (!r) return std::nullopt;
    Welcome w;
    w.nonce = r->u32(); w.host = r->name();
    if (!r->ok) return std::nullopt;
    return w;
}
std::optional<Input> parse_input(const uint8_t* data, size_t size) {
    if (size < kInputSize) return std::nullopt;  // (longer: a later minor addition, ignored)
    auto r = body(data, size, kInput);
    if (!r) return std::nullopt;
    Input in;
    const uint8_t flags = r->u8();
    in.controller = flags & kHasController;
    in.touch = flags & kHasTouch;
    in.motion = flags & kHasMotion;
    in.quarter_turns = r->u8() & 3;
    in.seq = r->u32(); in.time_ns = r->u64(); in.buttons = r->u32();
    in.lx = from_i16(r->u16()); in.ly = from_i16(r->u16());
    in.rx = from_i16(r->u16()); in.ry = from_i16(r->u16());
    in.lt = from_u16(r->u16()); in.rt = from_u16(r->u16());
    in.tx = from_u16(r->u16()); in.ty = from_u16(r->u16());
    in.motion_ns = r->u64();
    for (float& v : in.gyro) v = r->f32();
    for (float& v : in.accel) v = r->f32();
    if (!r->ok) return std::nullopt;
    if (!in.controller) { in.buttons = 0; in.lx = in.ly = in.rx = in.ry = in.lt = in.rt = 0; }
    if (!in.touch) in.tx = in.ty = 0;
    if (!in.motion) { in.motion_ns = 0; for (int i = 0; i < 3; i++) in.gyro[i] = in.accel[i] = 0; }
    return in;
}

}  // namespace remote_pad

namespace remote_pad {
std::optional<PairingReply> parse_pairing(const uint8_t* data, size_t size) {
    auto r = body(data, size, kPairing);
    if (!r) return std::nullopt;
    PairingReply p;
    p.nonce = r->u32(); p.state = r->u8(); p.attempts = r->u8();
    if (!r->ok) return std::nullopt;
    return p;
}
std::optional<PairRequest> parse_pair(const uint8_t* data, size_t size) {
    auto r = body(data, size, kPair);
    if (!r) return std::nullopt;
    PairRequest p;
    p.nonce = r->u32(); p.id = r->bytes(); p.code = r->u32();
    if (!r->ok) return std::nullopt;
    return p;
}
std::optional<Paired> parse_paired(const uint8_t* data, size_t size) {
    auto r = body(data, size, kPaired);
    if (!r) return std::nullopt;
    Paired p;
    p.nonce = r->u32(); p.token = r->bytes();
    if (!r->ok) return std::nullopt;
    return p;
}
std::optional<AudioPiece> parse_audio(const uint8_t* data, size_t size) {
    if (size < kAudioHeaderSize) return std::nullopt;
    auto r = body(data, size, kAudio);
    if (!r) return std::nullopt;
    AudioPiece a;
    a.seq = r->u32();
    a.silent = r->u8() & 1;
    r->u8();
    a.frames = r->u16();
    if (!r->ok || a.frames > kMaxAudioFrames) return std::nullopt;
    if (!a.silent) {
        if (size - kAudioHeaderSize < size_t(a.frames) * 4) return std::nullopt;
        a.data = data + kAudioHeaderSize;
    }
    return a;
}
std::optional<Rumble> parse_rumble(const uint8_t* data, size_t size) {
    auto r = body(data, size, kRumble);
    if (!r) return std::nullopt;
    Rumble m;
    m.seq = r->u32(); m.level = r->u8();
    if (!r->ok) return std::nullopt;
    return m;
}
std::optional<uint64_t> parse_ping(const uint8_t* data, size_t size) {
    auto r = body(data, size, kPing);
    if (!r) return std::nullopt;
    const uint64_t t = r->u64();
    if (!r->ok) return std::nullopt;
    return t;
}
std::optional<Pong> parse_pong(const uint8_t* data, size_t size) {
    auto r = body(data, size, kPong);
    if (!r) return std::nullopt;
    Pong p;
    p.phone_ns = r->u64(); p.computer_ns = r->u64();
    if (!r->ok) return std::nullopt;
    return p;
}
std::optional<VideoPiece> parse_video(const uint8_t* data, size_t size) {
    if (size < kVideoHeaderSize) return std::nullopt;
    auto r = body(data, size, kVideo);
    if (!r) return std::nullopt;
    VideoPiece v;
    v.frame = r->u32(); v.index = r->u16(); v.count = r->u16();
    v.key = r->u8() & 1; r->u8();
    v.time_ns = r->u64(); v.width = r->u16(); v.height = r->u16();
    if (!r->ok || v.count == 0 || v.index >= v.count) return std::nullopt;
    v.data = data + kVideoHeaderSize;
    v.size = size - kVideoHeaderSize;
    return v;
}
}  // namespace remote_pad
