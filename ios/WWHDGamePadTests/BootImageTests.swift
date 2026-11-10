import XCTest
@testable import WWHDGamePad

final class BootImageTests: XCTestCase {
    private func piece(_ id: UInt32, _ bytes: [UInt8], at offset: Int, of total: Int) -> WWGP.BootImagePiece {
        WWGP.BootImagePiece(id: id, total: total, offset: offset, data: bytes[...])
    }

    func testPiecesInAnyOrderMakeTheImage() {
        var a = BootImageAssembler()
        XCTAssertNil(a.add(piece(7, [4, 5], at: 3, of: 5)))
        XCTAssertNil(a.add(piece(7, [4, 5], at: 3, of: 5)))  // a repeated piece counts once
        XCTAssertEqual(a.add(piece(7, [1, 2, 3], at: 0, of: 5)), [1, 2, 3, 4, 5])
    }

    func testAnotherImageStartsAfresh() {
        var a = BootImageAssembler()
        XCTAssertNil(a.add(piece(7, [1, 2], at: 0, of: 4)))
        XCTAssertNil(a.add(piece(8, [9], at: 1, of: 2)))     // a new game's image: the old pieces go
        XCTAssertEqual(a.add(piece(8, [6], at: 0, of: 2)), [6, 9])
        XCTAssertEqual(a.id, 8)
    }

    func testAPiecePastTheEndIsIgnored() {
        var a = BootImageAssembler()
        XCTAssertNil(a.add(piece(7, [1, 2, 3], at: 2, of: 4)))
        XCTAssertEqual(a.add(piece(7, [1, 2, 3, 4], at: 0, of: 4)), [1, 2, 3, 4])
    }
}
