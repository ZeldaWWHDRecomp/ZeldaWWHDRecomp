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
                       hex("575747500100010000000000040302010b54657374206950686f6e650c4261636b626f6e65204f6e650000000000000000000000000000000000000000000000000000000000000000"))
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

    func testPairingPacketsMatchTheGame() {
        // the bytes runtime/src/remote_pad/protocol.cpp writes for the same values
        let id = (0..<16).map { UInt8($0) }, token = (0..<16).map { UInt8(0xF0 + $0) }
        XCTAssertEqual(WWGP.hello(nonce: 1, device: "P", controller: "", video: true, audio: true, id: id, token: token),
                       hex("57574750010001030000000001000000015000000102030405060708090a0b0c0d0e0ff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"))
        XCTAssertEqual(WWGP.pair(nonce: 0x0102_0304, id: id, code: 123_456),
                       hex("5757475001000a000000000004030201000102030405060708090a0b0c0d0e0f40e20100"))
        let p = WWGP.pairing(hex("575747500100090000000000040302010202"))
        XCTAssertEqual(p?.nonce, 0x0102_0304)
        XCTAssertEqual(p?.state, .wrongCode)
        XCTAssertEqual(p?.attempts, 2)
        let q = WWGP.paired(hex("5757475001000b000000000004030201f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"))
        XCTAssertEqual(q?.nonce, 0x0102_0304)
        XCTAssertEqual(q?.token, token)
        XCTAssertNil(WWGP.paired(hex("5757475001000b000000000004030201f0f1")))  // short
        XCTAssertEqual(Pairing.bytes(Pairing.hex(token)), token)
    }

    func testBye() {
        XCTAssertEqual(WWGP.bye(session: 0xA1B2_C3D4), hex("5757475001000400d4c3b2a1"))
    }

    func testPingAndPong() {
        XCTAssertEqual(WWGP.ping(session: 0xA1B2_C3D4, phoneNs: 0x0102_0304_0506_0708),
                       hex("5757475001000c00d4c3b2a10807060504030201"))
        let pong = hex("5757475001000d00d4c3b2a108070605040302011817161514131211")
        let p = WWGP.pong(pong)
        XCTAssertEqual(p?.session, 0xA1B2_C3D4)
        XCTAssertEqual(p?.phoneNs, 0x0102_0304_0506_0708)
        XCTAssertEqual(p?.computerNs, 0x1112_1314_1516_1718)
        XCTAssertNil(WWGP.pong(Array(pong.dropLast())))
        XCTAssertNil(WWGP.pong(WWGP.ping(session: 1, phoneNs: 2)))
    }

    func testTextPromptAndInput() {
        // bytes from the C++ encoder (encode_text, encode_text_input)
        let p = WWGP.text(hex("5757475001000e00050000000700000003000000080001000f456e74657220796f7572206e616d65044c696e6b"))
        XCTAssertEqual(p?.session, 5)
        XCTAssertEqual(p?.prompt, WWGP.TextPrompt(serial: 7, ack: 3, maxLen: 8, open: true, hint: "Enter your name", text: "Link"))
        XCTAssertNil(WWGP.text(hex("5757475001000e0005000000070000000300000008000100")))  // truncated
        XCTAssertEqual(WWGP.textInput(session: 5, serial: 7, seq: 4, action: .ok, text: "Tetra"),
                       hex("5757475001000f000500000007000000040000000100055465747261"))
    }

    func testControllerInfo() {
        // bytes from the C++ encoder (encode_controller_info)
        let info = WWGP.ControllerInfo(name: "Pad", vendor: "V", category: "C", faces: ["a.circle", "", "x", "y"])
        XCTAssertEqual(WWGP.controllerInfo(session: 5, info),
                       hex("575747500100120005000000035061640156014308612e636972636c650001780179"))
    }

    func testBootImage() {
        // bytes from the C++ encoder (encode_boot_image, encode_boot_image_want)
        let p = WWGP.bootImage(hex("575747500100100005000000040302010300000000000000aabbcc"))
        XCTAssertEqual(p?.session, 5)
        XCTAssertEqual(p?.piece.id, 0x0102_0304)
        XCTAssertEqual(p?.piece.total, 3)
        XCTAssertEqual(p?.piece.offset, 0)
        XCTAssertEqual(p.map { Array($0.piece.data) }, [0xAA, 0xBB, 0xCC])
        XCTAssertNil(WWGP.bootImage(hex("575747500100100005000000040302010200000000000000aabbcc")))  // more than its total
        XCTAssertEqual(WWGP.bootImageWant(session: 5, id: 0x0102_0304), hex("57574750010011000500000004030201"))
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
