// The game's text prompt on the phone (protocol.h kText / kTextInput), as a Wii U GamePad shows its
// keyboard for the name of a new file. Plain logic, tested (TextEntryTests); GamePadLink runs it on its
// queue.
//
// The phone sends its whole field after each edit, numbered; the game takes it through its own rules
// and acks the number in its next kText. Over UDP: an edit or OK the game has not acked is sent again,
// and once every edit is acked the game's text wins (it leaves out characters its font lacks). A phone
// that connects while the prompt is open (or restarted) continues from the game's ack, so its next
// edit counts as newer.
import Foundation

struct TextEntrySync {
    struct Send: Equatable {
        var serial: UInt32, seq: UInt32
        var action: WWGP.TextAction
        var text: String
    }

    static let resendAfter: TimeInterval = 0.3

    /// the prompt showing (nil: none)
    private(set) var prompt: WWGP.TextPrompt?
    /// what the phone's field shows
    private(set) var field = ""
    /// +1 each time the game set `field` (a new prompt, or characters it left out): the view takes it then
    private(set) var gameEdits = 0
    private var seq: UInt32 = 0
    private var action: WWGP.TextAction = .edit
    private var sentAt: TimeInterval = 0

    /// A kText from the game. True when `prompt` or `field` changed (the view follows).
    mutating func received(_ p: WWGP.TextPrompt) -> Bool {
        guard p.open else {
            guard prompt != nil else { return false }
            prompt = nil
            action = .edit
            return true
        }
        guard let current = prompt, current.serial == p.serial else {
            prompt = p
            field = p.text
            gameEdits += 1
            seq = p.ack
            action = .edit
            return true
        }
        let before = (prompt, field)
        prompt = p
        if Self.newer(p.ack, seq) { seq = p.ack }  // edits from elsewhere (an earlier run of the app)
        if p.ack == seq && field != p.text && action == .edit {
            field = p.text
            gameEdits += 1
        }
        return before.0 != prompt || before.1 != field
    }

    /// the player changed the field: the packet to send
    mutating func edit(_ text: String, now: TimeInterval) -> Send? {
        guard prompt != nil, action == .edit, text != field else { return nil }
        field = text
        return next(.edit, now: now)
    }

    /// OK or Cancel: sent until the game closes the prompt
    mutating func finish(_ a: WWGP.TextAction, now: TimeInterval) -> Send? {
        guard prompt != nil, action == .edit, a != .edit else { return nil }
        action = a
        return next(a, now: now)
    }

    /// from the link's tick: the last packet again when the game has not acked it in time
    mutating func resend(now: TimeInterval) -> Send? {
        guard let p = prompt, Self.newer(seq, p.ack), now - sentAt >= Self.resendAfter else { return nil }
        sentAt = now
        return Send(serial: p.serial, seq: seq, action: action, text: field)
    }

    /// the connection went away for good
    mutating func reset() { self = TextEntrySync() }

    private mutating func next(_ a: WWGP.TextAction, now: TimeInterval) -> Send? {
        guard let p = prompt else { return nil }
        seq &+= 1
        sentAt = now
        return Send(serial: p.serial, seq: seq, action: a, text: field)
    }

    static func newer(_ a: UInt32, _ b: UInt32) -> Bool { Int32(bitPattern: a &- b) > 0 }
}
