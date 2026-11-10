// The GamePad speaker (runtime/src/hle/ax.cpp's GamePad mix, sent as protocol.h kAudio): 48 kHz stereo
// in 3 ms pieces, played through a short jitter buffer so Wi-Fi's uneven arrival does not crackle.
//
// Latency: about JitterBuffer.target (40 ms) plus the output's own buffer. After a gap (the game paused,
// packets lost) the buffer fills to its target again before it plays; when it holds too much (the Mac's
// clock runs a little fast, a burst after a stall) it drops the oldest samples down to its target.
import AVFoundation
import Foundation
import os

/// The samples waiting to be played (tested in SpeakerTests).
struct JitterBuffer {
    static let rate = 48_000
    let target: Int          // frames buffered before playing starts
    let limit: Int           // more than this and the oldest are dropped down to target
    private var ring: [Int16]
    private var read = 0, count = 0  // in frames (2 samples each)
    private(set) var playing = false
    private var lastSeq: UInt32?
    private(set) var underruns = 0, dropped = 0

    init(target: Int = rate * 40 / 1000, limit: Int = rate * 120 / 1000) {
        self.target = target
        self.limit = limit
        ring = [Int16](repeating: 0, count: (limit + Self.rate / 10) * 2)
    }

    var buffered: Int { count }

    /// one piece; out-of-order and repeated pieces are dropped, lost ones are not filled in (the
    /// buffer runs short, and refills to its target if it runs out)
    mutating func push(_ p: WWGP.AudioPiece) {
        if let last = lastSeq, Int32(bitPattern: p.seq &- last) <= 0 { return }
        lastSeq = p.seq
        let capacity = ring.count / 2
        for f in 0..<p.frames {
            if count == capacity { read = (read + 1) % capacity; count -= 1; dropped += 1 }
            let w = (read + count) % capacity
            ring[2 * w] = p.samples?[2 * f] ?? 0
            ring[2 * w + 1] = p.samples?[2 * f + 1] ?? 0
            count += 1
        }
        if count > limit {
            let drop = count - target
            read = (read + drop) % capacity
            count -= drop
            dropped += drop
        }
    }

    /// n frames for the output, as floats -1...1 (silence while filling up or after running out)
    mutating func pull(_ n: Int, left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>) {
        if !playing && count >= target { playing = true }
        let capacity = ring.count / 2
        for i in 0..<n {
            if playing && count > 0 {
                left[i] = Float(ring[2 * read]) / 32768
                right[i] = Float(ring[2 * read + 1]) / 32768
                read = (read + 1) % capacity
                count -= 1
            } else {
                left[i] = 0
                right[i] = 0
                if playing { playing = false; underruns += 1 }  // ran out: fill up again
            }
        }
    }

    /// a new session: empty, and any sequence number is next
    mutating func reset() {
        read = 0; count = 0; playing = false; lastSeq = nil
    }
}

/// Plays the GamePad speaker. push/reset on the link queue; the output pulls on its own thread.
final class Speaker: @unchecked Sendable {
    private let buffer = OSAllocatedUnfairLock(initialState: JitterBuffer())
    private var engine: AVAudioEngine?    // link queue
    private var lastPiece = Date.distantPast
    private var retryAfter = Date.distantPast  // a failed start is not tried again on every piece

    /// link queue: a piece from the game (starts the output on the first one)
    func push(_ p: WWGP.AudioPiece) {
        buffer.withLock { $0.push(p) }
        lastPiece = Date()
        if engine == nil, lastPiece >= retryAfter { start() }
    }

    /// link queue: every tick. The output stops after a while without pieces (saves power), and when iOS
    /// stopped the engine (a call, Siri, headphones connected) the next piece starts it again.
    func tick() {
        guard let e = engine else { return }
        if !e.isRunning || Date().timeIntervalSince(lastPiece) > 2 { stop() }
    }

    /// link queue: disconnected, a new session, the background
    func reset() {
        stop()
        buffer.withLock { $0.reset() }
    }

    private func start() {
        let session = AVAudioSession.sharedInstance()
        do {
            // plays with the ring/silent switch off and alongside other apps' sound, like a game's speaker
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setPreferredIOBufferDuration(0.005)
            try session.setActive(true)
        } catch {
            print("[speaker] audio session: \(error.localizedDescription)")
        }
        let e = AVAudioEngine()
        let format = AVAudioFormat(standardFormatWithSampleRate: Double(JitterBuffer.rate), channels: 2)!
        let buffer = self.buffer
        let source = AVAudioSourceNode(format: format) { _, _, frames, list -> OSStatus in
            let abl = UnsafeMutableAudioBufferListPointer(list)
            guard abl.count >= 2, let l = abl[0].mData?.assumingMemoryBound(to: Float.self),
                  let r = abl[1].mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            buffer.withLock { $0.pull(Int(frames), left: l, right: r) }
            return noErr
        }
        e.attach(source)
        e.connect(source, to: e.mainMixerNode, format: format)
        do {
            try e.start()
            engine = e
            print("[speaker] playing the GamePad speaker (\(Int(session.ioBufferDuration * 1000)) ms output buffer)")
        } catch {
            print("[speaker] cannot start: \(error.localizedDescription)")
            retryAfter = Date() + 2
        }
    }

    private func stop() {
        guard let e = engine else { return }
        e.stop()
        engine = nil
        let (under, dropped) = buffer.withLock { ($0.underruns, $0.dropped) }
        print("[speaker] stopped (\(under) underruns, \(dropped) frames dropped)")
    }
}
