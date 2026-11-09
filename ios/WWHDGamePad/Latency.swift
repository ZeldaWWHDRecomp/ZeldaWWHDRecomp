// How far behind the game the phone is: the network round trip, and how old the GamePad picture is when
// its last piece arrives (before decoding and display). The picture's times are on the computer's clock;
// a pong left the computer about half a round trip before it arrived, and the ping with the shortest round
// trip of the last few gives the best estimate of the clock offset (as NTP does).
import Foundation

struct LatencyMeter {
    struct Reading: Equatable {
        var pingMs: Double
        var pictureMs: Double?    // nil: no picture since the last reading
    }

    static let window = 8
    private var pings: [(rtt: UInt64, offset: Int64)] = []  // offset: computer clock minus phone clock
    private var ages: [Double] = []                          // ms, since the last reading

    mutating func pong(sentNs: UInt64, computerNs: UInt64, receivedNs: UInt64) {
        guard receivedNs >= sentNs else { return }
        let rtt = receivedNs - sentNs
        pings.append((rtt, Int64(bitPattern: computerNs &- (sentNs + rtt / 2))))
        if pings.count > Self.window { pings.removeFirst() }
    }

    /// a frame the computer drew at `computerNs` was complete here at `receivedNs`
    mutating func frame(computerNs: UInt64, receivedNs: UInt64) {
        guard let offset = pings.min(by: { $0.rtt < $1.rtt })?.offset else { return }
        let drawn = computerNs &- UInt64(bitPattern: offset)
        ages.append(max(Double(Int64(bitPattern: receivedNs &- drawn)) / 1e6, 0))
    }

    /// the median round trip of the window, and the median picture age since the last reading
    mutating func reading() -> Reading? {
        guard !pings.isEmpty else { return nil }
        let r = Reading(pingMs: Self.median(pings.map { Double($0.rtt) / 1e6 }),
                        pictureMs: ages.isEmpty ? nil : Self.median(ages))
        ages.removeAll(keepingCapacity: true)
        return r
    }

    private static func median(_ v: [Double]) -> Double {
        let s = v.sorted()
        return s.count % 2 == 1 ? s[s.count / 2] : (s[s.count / 2 - 1] + s[s.count / 2]) / 2
    }
}
