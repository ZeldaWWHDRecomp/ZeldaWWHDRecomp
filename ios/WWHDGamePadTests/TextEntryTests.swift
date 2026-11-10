import XCTest
@testable import WWHDGamePad

final class TextEntryTests: XCTestCase {
    private func prompt(serial: UInt32 = 1, ack: UInt32 = 0, open: Bool = true, text: String = "") -> WWGP.TextPrompt {
        WWGP.TextPrompt(serial: serial, ack: ack, maxLen: 8, open: open, hint: "Name", text: text)
    }

    func testEditsAreNumberedAndResentUntilAcked() {
        var s = TextEntrySync()
        XCTAssertNil(s.edit("L", now: 0))  // no prompt yet
        XCTAssertTrue(s.received(prompt()))
        XCTAssertEqual(s.edit("L", now: 0), .init(serial: 1, seq: 1, action: .edit, text: "L"))
        XCTAssertEqual(s.edit("Li", now: 0.1), .init(serial: 1, seq: 2, action: .edit, text: "Li"))
        XCTAssertNil(s.resend(now: 0.2))  // not yet
        XCTAssertEqual(s.resend(now: 0.5), .init(serial: 1, seq: 2, action: .edit, text: "Li"))
        _ = s.received(prompt(ack: 2, text: "Li"))
        XCTAssertNil(s.resend(now: 2))
    }

    func testTheGamesTextWinsOnlyOnceEveryEditIsAcked() {
        var s = TextEntrySync()
        _ = s.received(prompt())
        let start = s.gameEdits
        _ = s.edit("Li", now: 0)
        _ = s.edit("Li♥", now: 0)
        // the game took "Li" so far: the phone's newer typing stays
        _ = s.received(prompt(ack: 1, text: "Li"))
        XCTAssertEqual(s.field, "Li♥")
        XCTAssertEqual(s.gameEdits, start)
        // it took everything but left out a character its font lacks
        _ = s.received(prompt(ack: 2, text: "Li"))
        XCTAssertEqual(s.field, "Li")
        XCTAssertEqual(s.gameEdits, start + 1)
    }

    func testOkIsSentUntilThePromptCloses() {
        var s = TextEntrySync()
        _ = s.received(prompt(text: "Link"))
        XCTAssertEqual(s.finish(.ok, now: 0), .init(serial: 1, seq: 1, action: .ok, text: "Link"))
        XCTAssertNil(s.edit("Linkx", now: 0.1))  // finished: no more edits
        XCTAssertEqual(s.resend(now: 0.4)?.action, .ok)
        XCTAssertTrue(s.received(prompt(ack: 1, open: false, text: "Link")))
        XCTAssertNil(s.prompt)
        XCTAssertNil(s.resend(now: 2))
    }

    /// A phone that (re)connects while the prompt is open, after the app restarted: its numbers start
    /// over, so they continue from the game's ack, which would otherwise take them as old.
    func testARestartedAppContinuesFromTheAck() {
        var s = TextEntrySync()
        XCTAssertTrue(s.received(prompt(ack: 5, text: "Te")))
        XCTAssertEqual(s.field, "Te")
        XCTAssertEqual(s.edit("Tet", now: 0)?.seq, 6)
    }

    func testANewPromptStartsAfresh() {
        var s = TextEntrySync()
        _ = s.received(prompt(serial: 1))
        _ = s.finish(.cancel, now: 0)
        _ = s.received(prompt(serial: 1, open: false))
        XCTAssertTrue(s.received(prompt(serial: 2, ack: 0, text: "")))
        XCTAssertEqual(s.edit("A", now: 1), .init(serial: 2, seq: 1, action: .edit, text: "A"))
    }
}
