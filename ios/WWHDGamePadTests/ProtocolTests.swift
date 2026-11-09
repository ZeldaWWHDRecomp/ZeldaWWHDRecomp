// The phone's packets byte for byte as the game's encoder writes them (runtime/src/remote_pad/protocol.cpp).
// The expected bytes were printed by the C++ encoder for the same values; regenerate them when the
// protocol changes.
import XCTest
@testable import WWHDGamePad

final class ProtocolTests: XCTestCase {
    private func hex(_ s: String) -> [UInt8] {
        var out: [UInt8] = []
        var i = s.startIndex
        while i < s.endIndex {
            let j = s.index(i, offsetBy: 2)
            out.append(UInt8(s[i..<j], radix: 16)!)
            i = j
        }
        return out
    }

    func testHello() {
        XCTAssertEqual(WWGP.hello(nonce: 0x0102_0304, device: "Test iPhone", controller: "Backbone One"),
                       hex("575747500100010000000000040302010b54657374206950686f6e650c4261636b626f6e65204f6e65"))
    }

    func testInput() {
        var i = WWGP.Input()
        i.seq = 7; i.timeNs = 0x0102_0304_0506_0708
        i.controller = true; i.buttons = [.a, .rb, .dLeft, .home]
        i.lx = -1; i.ly = 0.5; i.rx = 0.25; i.ry = 0; i.lt = 1; i.rt = 0.5
        i.touch = true; i.tx = 0.25; i.ty = 0.75
        i.motion = true; i.quarterTurns = 1; i.motionNs = 123_456_789
        i.gyro = SIMD3(1.5, -0.25, 3); i.accel = SIMD3(0, -9.80665, 0.5)
        let bytes = WWGP.input(session: 0xA1B2_C3D4, i)
        XCTAssertEqual(bytes.count, WWGP.inputSize)
        XCTAssertEqual(bytes, hex("5757475001000300d4c3b2a10701070000000807060504030201212400000180004000200000ffff00800040ffbf15cd5b07000000000000c03f000080be00004040000000000ae81cc10000003f"))
    }

    func testBye() {
        XCTAssertEqual(WWGP.bye(session: 0xA1B2_C3D4), hex("5757475001000400d4c3b2a1"))
    }

    func testWelcome() {
        let w = WWGP.welcome(hex("5757475001000200d4c3b2a10403020108546573742d4d6163"))
        XCTAssertEqual(w, WWGP.Welcome(session: 0xA1B2_C3D4, nonce: 0x0102_0304, host: "Test-Mac"))
        XCTAssertNil(WWGP.welcome(hex("5757475001000200d4c3b2a1040302010854657374")))  // truncated name
        XCTAssertNil(WWGP.welcome(hex("5857475001000200d4c3b2a10403020108546573742d4d6163")))  // magic
        XCTAssertNil(WWGP.welcome(WWGP.bye(session: 1)))  // another type
    }

    func testNamesAreCutOnCharacterBoundaries() {
        let name = String(repeating: "a", count: 62) + "é"  // 64 bytes
        let p = WWGP.hello(nonce: 1, device: name, controller: "")
        XCTAssertEqual(p[16], 62)
    }

    func testClamping() {
        XCTAssertEqual(WWGP.toI16(7), 32767)
        XCTAssertEqual(WWGP.toI16(-7), -32767)
        XCTAssertEqual(WWGP.toI16(.nan), 0)
        XCTAssertEqual(WWGP.toU16(-1), 0)
        XCTAssertEqual(WWGP.toU16(2), 65535)
    }
}
