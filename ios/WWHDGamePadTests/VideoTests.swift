// The GamePad screen on the phone: video packets as the game writes them (bytes from
// runtime/src/remote_pad/protocol.cpp), frames put back together, and what is dropped after a loss.
import XCTest
@testable import WWHDGamePad

final class VideoTests: XCTestCase {
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

    func testPacketsMatchTheGame() {
        XCTAssertEqual(WWGP.hello(nonce: 0x0102_0304, device: "Test iPhone", controller: "Backbone One", video: true),
                       hex("575747500100010100000000040302010b54657374206950686f6e650c4261636b626f6e65204f6e65"))
        XCTAssertEqual(WWGP.keyframeRequest(session: 0xA1B2_C3D4), hex("5757475001000600d4c3b2a1"))
        let v = WWGP.video(hex("5757475001000500d4c3b2a14433221100000100010008070605040302015603e001000000026588"))
        XCTAssertEqual(v?.session, 0xA1B2_C3D4)
        XCTAssertEqual(v?.piece.frame, 0x1122_3344)
        XCTAssertEqual(v?.piece.index, 0)
        XCTAssertEqual(v?.piece.count, 1)
        XCTAssertEqual(v?.piece.key, true)
        XCTAssertEqual(v?.piece.timeNs, 0x0102_0304_0506_0708)
        XCTAssertEqual(v?.piece.width, 854)
        XCTAssertEqual(v?.piece.height, 480)
        XCTAssertEqual(v.map { Array($0.piece.payload) }, [0, 0, 0, 2, 0x65, 0x88])
        // a bad index or count is rejected
        var bad = hex("5757475001000500d4c3b2a14433221100000100010008070605040302015603e001000000026588")
        bad[16] = 1  // index 1 of 1
        XCTAssertNil(WWGP.video(bad))
        XCTAssertNil(WWGP.video(WWGP.bye(session: 1)))
    }

    func testRumblePacketMatchesTheGame() {
        // encode_rumble(0xA1B2C3D4, 77, 200) in runtime/src/remote_pad/protocol.cpp
        let r = WWGP.rumble(hex("5757475001000700d4c3b2a14d000000c8"))
        XCTAssertEqual(r?.session, 0xA1B2_C3D4)
        XCTAssertEqual(r?.seq, 77)
        XCTAssertEqual(r?.level ?? -1, 200.0 / 255, accuracy: 1e-6)
        XCTAssertNil(WWGP.rumble(hex("5757475001000700d4c3b2a14d000000")))  // short
        XCTAssertNil(WWGP.rumble(WWGP.bye(session: 1)))
    }

    private func piece(_ frame: UInt32, _ index: Int, _ count: Int, key: Bool = false, _ bytes: [UInt8] = [1]) -> WWGP.VideoPiece {
        WWGP.VideoPiece(frame: frame, index: index, count: count, key: key, timeNs: 0, width: 854, height: 480,
                        payload: bytes[...])
    }

    func testFramesInOrder() {
        var a = FrameAssembler()
        XCTAssertNil(a.add(piece(1, 0, 2, key: true, [1, 2])).frame)
        let f = a.add(piece(1, 1, 2, key: true, [3])).frame
        XCTAssertEqual(f?.data, [1, 2, 3])
        XCTAssertEqual(f?.key, true)
        XCTAssertEqual(a.add(piece(2, 0, 1, [9])).frame?.data, [9])
        // pieces may come out of order within a frame
        XCTAssertNil(a.add(piece(3, 1, 2, [5])).frame)
        XCTAssertEqual(a.add(piece(3, 0, 2, [4])).frame?.data, [4, 5])
        // a repeated piece of a shown frame is ignored
        XCTAssertNil(a.add(piece(3, 0, 2, [4])).frame)
    }

    func testFirstFrameMustBeAKeyframe() {
        var a = FrameAssembler()
        let r = a.add(piece(5, 0, 1))
        XCTAssertNil(r.frame)
        XCTAssertTrue(r.needKey)
        XCTAssertNotNil(a.add(piece(6, 0, 1, key: true)).frame)
        XCTAssertNotNil(a.add(piece(7, 0, 1)).frame)
    }

    func testLossDropsFramesUntilTheNextKeyframe() {
        var a = FrameAssembler()
        XCTAssertNotNil(a.add(piece(1, 0, 1, key: true)).frame)
        // frame 2 loses its second piece: frame 3's first piece gives it up and asks for a keyframe
        XCTAssertNil(a.add(piece(2, 0, 2)).frame)
        let r = a.add(piece(3, 0, 1))
        XCTAssertTrue(r.needKey)
        XCTAssertNil(r.frame)
        XCTAssertTrue(a.broken)
        // frame 2's late piece changes nothing
        XCTAssertNil(a.add(piece(2, 1, 2)).frame)
        XCTAssertNil(a.add(piece(4, 0, 1)).frame)
        // the keyframe brings the picture back
        XCTAssertNotNil(a.add(piece(5, 0, 1, key: true)).frame)
        XCTAssertFalse(a.broken)
        XCTAssertNotNil(a.add(piece(6, 0, 1)).frame)
    }

    func testWholeFramesMissed() {
        var a = FrameAssembler()
        XCTAssertNotNil(a.add(piece(1, 0, 1, key: true)).frame)
        let r = a.add(piece(4, 0, 1))  // frames 2 and 3 never arrived
        XCTAssertTrue(r.needKey)
        XCTAssertNil(r.frame)
        XCTAssertNotNil(a.add(piece(5, 0, 1, key: true)).frame)
    }

    func testFrameNumbersWrap() {
        var a = FrameAssembler()
        XCTAssertNotNil(a.add(piece(UInt32.max, 0, 1, key: true)).frame)
        XCTAssertNotNil(a.add(piece(0, 0, 1)).frame)
    }

    func testNalUnits() {
        XCTAssertEqual(SampleMaker.nals([0, 0, 0, 2, 7, 8, 0, 0, 0, 1, 5]).map { $0.map(Array.init) }, [[7, 8], [5]])
        XCTAssertNil(SampleMaker.nals([0, 0, 0, 9, 1]))  // longer than the frame
    }
}
