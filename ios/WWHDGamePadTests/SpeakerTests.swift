// The GamePad speaker on the phone: audio packets as the game writes them (bytes from
// runtime/src/remote_pad/protocol.cpp) and the jitter buffer's rules.
import XCTest
@testable import WWHDGamePad

final class SpeakerTests: XCTestCase {
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
        XCTAssertEqual(WWGP.hello(nonce: 1, device: "P", controller: "", video: true, audio: true),
                       hex("575747500100010300000000010000000150000000000000000000000000000000000000000000000000000000000000000000"))
        let a = WWGP.audio(hex("5757475001000800d4c3b2a109000000000002000100feff2c0170fe"))
        XCTAssertEqual(a?.session, 0xA1B2_C3D4)
        XCTAssertEqual(a?.piece.seq, 9)
        XCTAssertEqual(a?.piece.frames, 2)
        XCTAssertEqual(a?.piece.samples, [1, -2, 300, -400])
        let s = WWGP.audio(hex("5757475001000800d4c3b2a10a00000001009000"))
        XCTAssertEqual(s?.piece.frames, 144)
        XCTAssertNil(s?.piece.samples)
        XCTAssertNil(WWGP.audio(hex("5757475001000800d4c3b2a109000000000002000100feff2c01")))  // short
    }

    private func piece(_ seq: UInt32, frames: Int = 144, value: Int16? = 1000) -> WWGP.AudioPiece {
        WWGP.AudioPiece(seq: seq, frames: frames, samples: value.map { [Int16](repeating: $0, count: frames * 2) })
    }

    private func pull(_ b: inout JitterBuffer, _ n: Int) -> [Float] {
        var l = [Float](repeating: -1, count: n), r = [Float](repeating: -1, count: n)
        l.withUnsafeMutableBufferPointer { lp in r.withUnsafeMutableBufferPointer { rp in
            b.pull(n, left: lp.baseAddress!, right: rp.baseAddress!)
        } }
        return l
    }

    func testFillsToTheTargetBeforePlaying() {
        var b = JitterBuffer(target: 480, limit: 1440)
        b.push(piece(1))  // 144 frames: not enough yet
        XCTAssertEqual(pull(&b, 100), [Float](repeating: 0, count: 100))
        XCTAssertFalse(b.playing)
        for s in 2...4 { b.push(piece(UInt32(s))) }  // 576 frames
        let out = pull(&b, 100)
        XCTAssertTrue(b.playing)
        XCTAssertEqual(out[0], 1000.0 / 32768, accuracy: 1e-6)
        XCTAssertEqual(b.buffered, 476)
    }

    func testRunningOutFillsUpAgain() {
        var b = JitterBuffer(target: 288, limit: 1440)
        b.push(piece(1)); b.push(piece(2))
        _ = pull(&b, 288)
        XCTAssertTrue(b.playing)
        let out = pull(&b, 10)  // empty: silence, and wait for the target again
        XCTAssertEqual(out, [Float](repeating: 0, count: 10))
        XCTAssertFalse(b.playing)
        XCTAssertEqual(b.underruns, 1)
        b.push(piece(3))
        XCTAssertEqual(pull(&b, 10), [Float](repeating: 0, count: 10))  // 144 < 288: still filling
        b.push(piece(4))
        XCTAssertNotEqual(pull(&b, 10)[0], 0)
    }

    func testTooMuchIsDroppedToTheTarget() {
        var b = JitterBuffer(target: 288, limit: 720)
        for s in 1...6 { b.push(piece(UInt32(s))) }  // 864 > 720: down to 288
        XCTAssertEqual(b.buffered, 288)
        XCTAssertEqual(b.dropped, 576)
    }

    func testSilenceKeepsTimeAndOrderIsKept() {
        var b = JitterBuffer(target: 144, limit: 1440)
        b.push(piece(1, value: nil))
        b.push(piece(1))           // repeated: dropped
        b.push(piece(0))           // older: dropped
        XCTAssertEqual(b.buffered, 144)
        XCTAssertEqual(pull(&b, 144), [Float](repeating: 0, count: 144))
        b.push(piece(2, value: 16384))
        XCTAssertEqual(pull(&b, 1)[0], 0.5, accuracy: 1e-6)
        b.reset()
        b.push(piece(1))           // a new session: any number is next
        XCTAssertEqual(b.buffered, 144)
    }
}
