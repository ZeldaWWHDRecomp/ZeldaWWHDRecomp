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

    enum PacketType: UInt8 { case hello = 1, welcome = 2, input = 3, bye = 4 }

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

    static func hello(nonce: UInt32, device: String, controller: String) -> [UInt8] {
        var w = Writer()
        w.header(.hello, session: 0)
        w.u32(nonce); w.name(device); w.name(controller)
        return w.bytes
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
        /// at most maxName bytes, never cut inside a UTF-8 sequence
        mutating func name(_ s: String) {
            let utf8 = Array(s.utf8)
            var n = min(utf8.count, WWGP.maxName)
            while n > 0 && n < utf8.count && (utf8[n] & 0xC0) == 0x80 { n -= 1 }
            u8(UInt8(n))
            bytes.append(contentsOf: utf8[0..<n])
        }
        mutating func header(_ type: PacketType, session: UInt32) {
            bytes.append(contentsOf: WWGP.magic)
            u16(WWGP.version); u8(type.rawValue); u8(0); u32(session)
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
        mutating func name() -> String {
            let n = Int(u8())
            guard ok, n <= WWGP.maxName, at + n <= bytes.count else { ok = false; return "" }
            defer { at += n }
            return String(decoding: bytes[at..<at + n], as: UTF8.self)
        }
    }
}
