// The remote GamePad protocol, phone side. Mirrors runtime/src/remote_pad/protocol.h (the reference:
// layout, units and conventions are documented there). Change both together and bump `version`.
// ProtocolTests checks these bytes against packets encoded by the C++ side.
import Foundation

enum WWGP {
    static let magic: [UInt8] = Array("WWGP".utf8)
    static let version: UInt16 = 1
    static let defaultPort: UInt16 = 26770
    static let bonjourType = "_wwhd-gamepad._udp"
    static let headerSize = 12
    static let inputSize = 78
    static let maxName = 63

    static let wantsVideo: UInt8 = 1, wantsAudio: UInt8 = 2   // a Hello's header flags
    static let rumbleStale: TimeInterval = 0.25               // protocol.h kRumbleStaleMs

    enum PacketType: UInt8 {
        case hello = 1, welcome = 2, input = 3, bye = 4, video = 5, keyframe = 6, rumble = 7, audio = 8
        case pairing = 9, pair = 10, paired = 11
    }
    /// protocol.h PairState: what the computer says about pairing
    enum PairState: UInt8 { case enterCode = 1, wrongCode = 2, noCode = 3 }
    static let audioHeaderSize = headerSize + 8
    static let videoHeaderSize = headerSize + 22
    static let maxDatagram = 1400

    /// Buttons by position (Xbox names, as GCExtendedGamepad: A is the bottom face button).
    struct Buttons: OptionSet {
        let rawValue: UInt32
        static let a = Buttons(rawValue: 1 << 0), b = Buttons(rawValue: 1 << 1)
        static let x = Buttons(rawValue: 1 << 2), y = Buttons(rawValue: 1 << 3)
        static let lb = Buttons(rawValue: 1 << 4), rb = Buttons(rawValue: 1 << 5)
        static let l3 = Buttons(rawValue: 1 << 6), r3 = Buttons(rawValue: 1 << 7)
        static let menu = Buttons(rawValue: 1 << 8), options = Buttons(rawValue: 1 << 9)
        static let home = Buttons(rawValue: 1 << 10)
        static let dUp = Buttons(rawValue: 1 << 11), dDown = Buttons(rawValue: 1 << 12)
        static let dLeft = Buttons(rawValue: 1 << 13), dRight = Buttons(rawValue: 1 << 14)
        static let capture = Buttons(rawValue: 1 << 15)
    }

    struct Input: Equatable {
        var seq: UInt32 = 0
        var timeNs: UInt64 = 0
        var controller = false
        var buttons: Buttons = []
        var lx: Float = 0, ly: Float = 0, rx: Float = 0, ry: Float = 0  // -1...1, +y = up
        var lt: Float = 0, rt: Float = 0                                // 0...1
        var touch = false
        var tx: Float = 0, ty: Float = 0      // 0...1 from the top left of the GamePad picture
        var motion = false
        var quarterTurns: UInt8 = 0           // counter-clockwise from portrait: top to the left = 1
        var motionNs: UInt64 = 0
        var gyro: SIMD3<Float> = .zero        // rad/s, natural phone axes
        var accel: SIMD3<Float> = .zero       // m/s^2, specific force (+9.81 on z lying face up)
    }

    struct Welcome: Equatable {
        var session: UInt32
        var nonce: UInt32
        var host: String
    }

    // MARK: encoding

    /// One piece of an encoded frame (H.264, AVCC; protocol.h kVideo).
    struct VideoPiece {
        var frame: UInt32
        var index: Int, count: Int
        var key: Bool
        var timeNs: UInt64
        var width: Int, height: Int
        var payload: ArraySlice<UInt8>
    }

    static func hello(nonce: UInt32, device: String, controller: String, video: Bool = false, audio: Bool = false,
                      id: [UInt8] = [UInt8](repeating: 0, count: 16), token: [UInt8] = [UInt8](repeating: 0, count: 16)) -> [UInt8] {
        var w = Writer()
        w.header(.hello, session: 0, flags: (video ? wantsVideo : 0) | (audio ? wantsAudio : 0))
        w.u32(nonce); w.name(device); w.name(controller)
        w.bytes16(id); w.bytes16(token)
        return w.bytes
    }

    /// the code typed in (protocol.h kPair)
    static func pair(nonce: UInt32, id: [UInt8], code: UInt32) -> [UInt8] {
        var w = Writer()
        w.header(.pair, session: 0)
        w.u32(nonce); w.bytes16(id); w.u32(code)
        return w.bytes
    }

    static func pairing(_ b: [UInt8]) -> (nonce: UInt32, state: PairState, attempts: Int)? {
        guard let (type, _) = header(b), type == .pairing else { return nil }
        var r = Reader(bytes: b, at: headerSize)
        let nonce = r.u32(), state = r.u8(), attempts = r.u8()
        guard r.ok, let s = PairState(rawValue: state) else { return nil }
        return (nonce, s, Int(attempts))
    }

    static func paired(_ b: [UInt8]) -> (nonce: UInt32, token: [UInt8])? {
        guard let (type, _) = header(b), type == .paired else { return nil }
        var r = Reader(bytes: b, at: headerSize)
        let nonce = r.u32()
        let token = r.bytes16()
        return r.ok ? (nonce, token) : nil
    }

    static func input(session: UInt32, _ i: Input) -> [UInt8] {
        var w = Writer()
        w.header(.input, session: session)
        w.u8((i.controller ? 1 : 0) | (i.touch ? 2 : 0) | (i.motion ? 4 : 0))
        w.u8(i.quarterTurns & 3)
        w.u32(i.seq); w.u64(i.timeNs); w.u32(i.buttons.rawValue)
        for v in [i.lx, i.ly, i.rx, i.ry] { w.u16(UInt16(bitPattern: toI16(v))) }
        w.u16(toU16(i.lt)); w.u16(toU16(i.rt))
        w.u16(toU16(i.tx)); w.u16(toU16(i.ty))
        w.u64(i.motionNs)
        for k in 0..<3 { w.f32(i.gyro[k]) }
        for k in 0..<3 { w.f32(i.accel[k]) }
        return w.bytes
    }

    static func bye(session: UInt32) -> [UInt8] {
        var w = Writer()
        w.header(.bye, session: session)
        return w.bytes
    }

    static func keyframeRequest(session: UInt32) -> [UInt8] {
        var w = Writer()
        w.header(.keyframe, session: session)
        return w.bytes
    }

    // MARK: decoding

    /// (type, session) of one of our packets; nil for anything else.
    static func header(_ b: [UInt8]) -> (PacketType, UInt32)? {
        guard b.count >= headerSize, Array(b[0..<4]) == magic else { return nil }
        var r = Reader(bytes: b, at: 4)
        guard r.u16() == version, let type = PacketType(rawValue: r.u8()) else { return nil }
        _ = r.u8()
        return (type, r.u32())
    }

    static func welcome(_ b: [UInt8]) -> Welcome? {
        guard let (type, session) = header(b), type == .welcome else { return nil }
        var r = Reader(bytes: b, at: headerSize)
        let nonce = r.u32()
        let host = r.name()
        return r.ok ? Welcome(session: session, nonce: nonce, host: host) : nil
    }

    /// One game audio frame of the GamePad speaker (protocol.h kAudio): 48 kHz stereo.
    struct AudioPiece {
        var seq: UInt32
        var frames: Int
        var samples: [Int16]?   // interleaved left/right; nil: silence
    }

    static func audio(_ b: [UInt8]) -> (session: UInt32, piece: AudioPiece)? {
        guard b.count >= audioHeaderSize, let (type, session) = header(b), type == .audio else { return nil }
        var r = Reader(bytes: b, at: headerSize)
        let seq = r.u32()
        let silent = r.u8() & 1 != 0
        _ = r.u8()
        let frames = Int(r.u16())
        guard r.ok else { return nil }
        if silent { return (session, AudioPiece(seq: seq, frames: frames, samples: nil)) }
        guard b.count - audioHeaderSize >= frames * 4 else { return nil }
        var samples = [Int16](repeating: 0, count: frames * 2)
        for i in 0..<frames * 2 {
            let at = audioHeaderSize + 2 * i
            samples[i] = Int16(bitPattern: UInt16(b[at]) | UInt16(b[at + 1]) << 8)
        }
        return (session, AudioPiece(seq: seq, frames: frames, samples: samples))
    }

    /// the GamePad motor's strength (protocol.h kRumble): (session, seq, level 0...1)
    static func rumble(_ b: [UInt8]) -> (session: UInt32, seq: UInt32, level: Float)? {
        guard let (type, session) = header(b), type == .rumble else { return nil }
        var r = Reader(bytes: b, at: headerSize)
        let seq = r.u32(), level = r.u8()
        return r.ok ? (session, seq, Float(level) / 255) : nil
    }

    static func video(_ b: [UInt8]) -> (session: UInt32, piece: VideoPiece)? {
        guard b.count >= videoHeaderSize, let (type, session) = header(b), type == .video else { return nil }
        var r = Reader(bytes: b, at: headerSize)
        let frame = r.u32(), index = Int(r.u16()), count = Int(r.u16())
        let key = r.u8() & 1 != 0
        _ = r.u8()
        let time = r.u64()
        let width = Int(r.u16()), height = Int(r.u16())
        guard r.ok, count > 0, index < count else { return nil }
        return (session, VideoPiece(frame: frame, index: index, count: count, key: key, timeNs: time,
                                    width: width, height: height, payload: b[videoHeaderSize...]))
    }

    // MARK: helpers

    static func toI16(_ v: Float) -> Int16 {
        guard v.isFinite else { return 0 }
        return Int16((min(max(v, -1), 1) * 32767).rounded())
    }
    static func toU16(_ v: Float) -> UInt16 {
        guard v.isFinite else { return 0 }
        return UInt16((min(max(v, 0), 1) * 65535).rounded())
    }

    struct Writer {
        var bytes: [UInt8] = []
        mutating func u8(_ v: UInt8) { bytes.append(v) }
        mutating func u16(_ v: UInt16) { u8(UInt8(v & 0xFF)); u8(UInt8(v >> 8)) }
        mutating func u32(_ v: UInt32) { u16(UInt16(v & 0xFFFF)); u16(UInt16(v >> 16)) }
        mutating func u64(_ v: UInt64) { u32(UInt32(v & 0xFFFF_FFFF)); u32(UInt32(v >> 32)) }
        mutating func f32(_ v: Float) { u32(v.bitPattern) }
        mutating func bytes16(_ b: [UInt8]) { bytes.append(contentsOf: (b + [UInt8](repeating: 0, count: 16)).prefix(16)) }
        /// at most maxName bytes, never cut inside a UTF-8 sequence
        mutating func name(_ s: String) {
            let utf8 = Array(s.utf8)
            var n = min(utf8.count, WWGP.maxName)
            while n > 0 && n < utf8.count && (utf8[n] & 0xC0) == 0x80 { n -= 1 }
            u8(UInt8(n))
            bytes.append(contentsOf: utf8[0..<n])
        }
        mutating func header(_ type: PacketType, session: UInt32, flags: UInt8 = 0) {
            bytes.append(contentsOf: WWGP.magic)
            u16(WWGP.version); u8(type.rawValue); u8(flags); u32(session)
        }
    }

    struct Reader {
        let bytes: [UInt8]
        var at: Int
        var ok = true
        mutating func u8() -> UInt8 {
            guard at < bytes.count else { ok = false; return 0 }
            defer { at += 1 }
            return bytes[at]
        }
        mutating func u16() -> UInt16 { let lo = UInt16(u8()); return lo | UInt16(u8()) << 8 }
        mutating func u32() -> UInt32 { let lo = UInt32(u16()); return lo | UInt32(u16()) << 16 }
        mutating func u64() -> UInt64 { let lo = UInt64(u32()); return lo | UInt64(u32()) << 32 }
        mutating func bytes16() -> [UInt8] {
            guard at + 16 <= bytes.count else { ok = false; return [] }
            defer { at += 16 }
            return Array(bytes[at..<at + 16])
        }
        mutating func name() -> String {
            let n = Int(u8())
            guard ok, n <= WWGP.maxName, at + n <= bytes.count else { ok = false; return "" }
            defer { at += n }
            return String(decoding: bytes[at..<at + n], as: UTF8.self)
        }
    }
}
