// The GamePad's motor (runtime/src/rumble.h, sent as protocol.h kRumble): one continuous vibration whose
// strength follows the game's, as the Mac does for its own controllers (gfx/input.mm). It runs on the
// controller when GameController reports rumble for it (the Backbone One PlayStation Edition does and
// vibrates; a paired DualSense, Xbox or Switch controller), else on the phone's Taptic Engine.
//
// Threads: everything on the link queue. A strength that is not refreshed within WWGP.rumbleStale stops
// (a lost stop, the game stalled or gone).
import CoreHaptics
import Foundation
import GameController

final class RumblePlayer: @unchecked Sendable {
    private var engine: CHHapticEngine?
    private var player: CHHapticAdvancedPatternPlayer?
    private var playing = false
    private var level: Float = 0
    private var lastSeq: UInt32?
    private var lastAt = Date.distantPast
    private var target: ObjectIdentifier?     // the controller the engine belongs to (nil: the phone)
    private var retryAfter = Date.distantPast
    /// the controller to vibrate if it has rumble; set on the link queue
    weak var controller: GCController?

    /// a new strength from the game
    func set(seq: UInt32, level: Float) {
        if let last = lastSeq, Int32(bitPattern: seq &- last) <= 0 { return }  // older or repeated
        lastSeq = seq
        lastAt = Date()
        apply(level)
    }

    /// every link tick: stop a strength that is no longer refreshed
    func tick() {
        if level > 0, Date().timeIntervalSince(lastAt) > WWGP.rumbleStale { apply(0) }
    }

    /// disconnected, a new session or the background: still, and forget the game's sequence
    func reset() {
        apply(0)
        lastSeq = nil
    }

    private func apply(_ value: Float) {
        level = value
        if value <= 0 {
            if playing { try? player?.stop(atTime: CHHapticTimeImmediate) }
            playing = false
            return
        }
        guard prepare() else { return }
        do {
            if !playing {
                try engine?.start()
                try player?.start(atTime: CHHapticTimeImmediate)
                playing = true
            }
            try player?.sendParameters([CHHapticDynamicParameter(parameterID: .hapticIntensityControl, value: value,
                                                                 relativeTime: 0)], atTime: CHHapticTimeImmediate)
        } catch {
            print("[rumble] \(error.localizedDescription)")
            drop()
        }
    }

    /// an engine and a looping player for the current target
    private func prepare() -> Bool {
        let pad = controller?.haptics != nil ? controller : nil
        let want = pad.map(ObjectIdentifier.init)
        if engine != nil && want == target { return true }
        drop()
        guard Date() >= retryAfter else { return false }
        target = want
        do {
            let e: CHHapticEngine?
            if let pad { e = pad.haptics?.createEngine(withLocality: .default) }
            else { e = CHHapticEngine.capabilitiesForHardware().supportsHaptics ? try CHHapticEngine() : nil }
            guard let e else { retryAfter = Date() + 5; return false }
            e.playsHapticsOnly = true
            e.isAutoShutdownEnabled = false
            // the system stops an engine (another app, an audio interruption); start again next time
            e.stoppedHandler = { [weak self] _ in self?.playing = false }
            e.resetHandler = { [weak self] in self?.drop() }
            // full strength and a low sharpness: a motor's rumble, not a tap; the game's strength scales it
            let event = CHHapticEvent(eventType: .hapticContinuous, parameters: [
                CHHapticEventParameter(parameterID: .hapticIntensity, value: 1),
                CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.25),
            ], relativeTime: 0, duration: 30)
            let p = try e.makeAdvancedPlayer(with: CHHapticPattern(events: [event], parameters: []))
            p.loopEnabled = true
            try e.start()
            engine = e
            player = p
            print("[rumble] ready on \(pad?.vendorName ?? "the phone")")
            return true
        } catch {
            print("[rumble] no haptics: \(error.localizedDescription)")
            retryAfter = Date() + 5
            return false
        }
    }

    private func drop() {
        try? player?.stop(atTime: CHHapticTimeImmediate)
        engine?.stop()
        player = nil
        engine = nil
        playing = false
    }
}
