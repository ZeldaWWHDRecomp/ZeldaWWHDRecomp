// The running game's GamePad boot image (its meta/bootDrcTex.tga, sent as a JPEG: protocol.h kBootImage) as the
// GamePad screen's background. Put together from its pieces and kept by its id in the Caches folder (each game
// has its own; the newest `keep` stay), so a phone that has it says so (kBootImageWant) and gets it no more.
import Foundation

struct BootImageAssembler {
    private(set) var id: UInt32 = 0
    private var buffer: [UInt8] = []
    private var pieces: Set<Int> = []
    private var bytes = 0

    /// a piece; the whole image once its last piece arrived
    mutating func add(_ p: WWGP.BootImagePiece) -> [UInt8]? {
        if p.id != id || p.total != buffer.count {
            id = p.id
            buffer = [UInt8](repeating: 0, count: p.total)
            pieces = []
            bytes = 0
        }
        guard p.offset + p.data.count <= buffer.count, pieces.insert(p.offset).inserted else { return nil }
        buffer.replaceSubrange(p.offset ..< p.offset + p.data.count, with: p.data)
        bytes += p.data.count
        return bytes == buffer.count ? buffer : nil
    }
}

enum BootImageCache {
    static let keep = 8

    static var folder: URL {
        FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0].appendingPathComponent("BootImages")
    }
    static func url(_ id: UInt32) -> URL { folder.appendingPathComponent(String(format: "%08x.jpg", id)) }

    static func load(_ id: UInt32) -> Data? { id == 0 ? nil : try? Data(contentsOf: url(id)) }

    static func save(_ id: UInt32, _ data: Data) {
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try? data.write(to: url(id), options: .atomic)
        // the newest `keep` images stay
        let fm = FileManager.default
        let files = (try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        let dated = files.map { ($0, (try? $0.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast) }
        for (old, _) in dated.sorted(by: { $0.1 > $1.1 }).dropFirst(keep) { try? fm.removeItem(at: old) }
    }

    /// the id last shown for a computer (its label), so it shows at once on the next connection
    static func remembered(for label: String) -> UInt32 {
        UInt32(truncatingIfNeeded: UserDefaults.standard.integer(forKey: "bootImage." + label))
    }
    static func remember(_ id: UInt32, for label: String) {
        UserDefaults.standard.set(Int(id), forKey: "bootImage." + label)
    }
}
