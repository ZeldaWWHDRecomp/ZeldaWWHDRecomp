import XCTest
@testable import WWHDGamePad

final class LatencyTests: XCTestCase {
    /// the computer's clock runs this far ahead of the phone's
    private let offset: UInt64 = 5_000_000_000_000
    private let ms: UInt64 = 1_000_000

    /// a ping sent at phone time `sent` that took `rtt` ms there and back, answered halfway
    private func pong(_ m: inout LatencyMeter, sent: UInt64, rtt: UInt64) {
        m.pong(sentNs: sent, computerNs: sent + rtt * ms / 2 + offset, receivedNs: sent + rtt * ms)
    }

    func testNothingBeforeTheFirstPong() {
        var m = LatencyMeter()
        m.frame(computerNs: offset, receivedNs: 30 * ms)
        XCTAssertNil(m.reading())
    }

    func testPictureAgeOnTheComputersClock() {
        var m = LatencyMeter()
        pong(&m, sent: 100 * ms, rtt: 4)
        m.frame(computerNs: 1000 * ms + offset, receivedNs: 1030 * ms)
        m.frame(computerNs: 1016 * ms + offset, receivedNs: 1050 * ms)
        m.frame(computerNs: 1033 * ms + offset, receivedNs: 1065 * ms)
        XCTAssertEqual(m.reading(), LatencyMeter.Reading(pingMs: 4, pictureMs: 32))
        XCTAssertEqual(m.reading(), LatencyMeter.Reading(pingMs: 4, pictureMs: nil))  // no frame since
    }

    func testTheShortestRoundTripSetsTheClock() {
        var m = LatencyMeter()
        // a slow ping whose answer waited on the way back: its offset is 10 ms off
        m.pong(sentNs: 100 * ms, computerNs: 105 * ms + offset, receivedNs: 130 * ms)
        pong(&m, sent: 200 * ms, rtt: 2)
        pong(&m, sent: 300 * ms, rtt: 6)
        m.frame(computerNs: 1000 * ms + offset, receivedNs: 1020 * ms)
        XCTAssertEqual(m.reading(), LatencyMeter.Reading(pingMs: 6, pictureMs: 20))  // median of 30, 2, 6
    }

    func testOnlyTheLastPingsCount() {
        var m = LatencyMeter()
        for i in 0..<20 { pong(&m, sent: UInt64(i) * 500 * ms, rtt: i < 12 ? 50 : 3) }
        XCTAssertEqual(m.reading()?.pingMs, 3)
    }
}
