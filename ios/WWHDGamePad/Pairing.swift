// The pairing tokens this phone got from games (runtime/src/remote_pad/pairing.h), by the name the game
// was reached by (its Bonjour name, or the address typed in). Kept in UserDefaults.
import Foundation

enum Pairing {
    private static let key = "pairTokens"

    static func token(for game: String) -> [UInt8] {
        let all = UserDefaults.standard.dictionary(forKey: key) as? [String: String] ?? [:]
        return all[game].flatMap(bytes) ?? [UInt8](repeating: 0, count: 16)
    }

    static func save(token: [UInt8], for game: String) {
        var all = UserDefaults.standard.dictionary(forKey: key) as? [String: String] ?? [:]
        all[game] = hex(token)
        UserDefaults.standard.set(all, forKey: key)
    }

    static func hex(_ b: [UInt8]) -> String { b.map { String(format: "%02x", $0) }.joined() }

    static func bytes(_ s: String) -> [UInt8]? {
        guard s.count % 2 == 0 else { return nil }
        var out: [UInt8] = []
        var i = s.startIndex
        while i < s.endIndex {
            let j = s.index(i, offsetBy: 2)
            guard let b = UInt8(s[i..<j], radix: 16) else { return nil }
            out.append(b)
            i = j
        }
        return out
    }
}
