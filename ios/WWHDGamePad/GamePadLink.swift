// The connection to the game on the computer (runtime/src/remote_pad/): finds it over Bonjour, says
// Hello until the game answers, then sends an Input about 120 times a second. The game answers about
// once a second; three seconds without an answer and the phone says Hello again (the game restarted,
// the network dropped).
import Foundation
import Network
import UIKit

@MainActor
final class GamePadLink: ObservableObject {
    enum Status: Equatable {
        case idle
        case connecting(String)       // to this game
        case connected(String)        // the computer's name
        case lost(String)             // was connected; saying Hello again
        case failed(String)
    }

    struct Game: Identifiable, Hashable {
        let name: String
        let endpoint: NWEndpoint
        var id: String { name }
    }

    @Published private(set) var status: Status = .idle
    @Published private(set) var games: [Game] = []
    @Published private(set) var controllerName: String?
    @Published var gyro = UserDefaults.standard.object(forKey: "gyro") as? Bool ?? true {
        didSet {
            UserDefaults.standard.set(gyro, forKey: "gyro")
            if isActive { gyro ? motion.start() : motion.stop() }
        }
    }

    nonisolated let queue = DispatchQueue(label: "org.wwhdrecomp.gamepad.link", qos: .userInteractive)
    nonisolated let screen = TouchAndScreen()
    nonisolated let video = VideoReceiver()
    nonisolated private let controller: ControllerReader
    nonisolated private let motion: MotionReader
    private var browser: NWBrowser?

    // link-queue state
    nonisolated(unsafe) private var connection: NWConnection?
    nonisolated(unsafe) private var timer: DispatchSourceTimer?
    nonisolated(unsafe) private var session: UInt32 = 0
    nonisolated(unsafe) private var nonce: UInt32 = 0
    nonisolated(unsafe) private var seq: UInt32 = 0
    nonisolated(unsafe) private var lastWelcome = Date.distantPast
    nonisolated(unsafe) private var lastHello = Date.distantPast
    nonisolated(unsafe) private var target = ""

    private var isActive: Bool {
        switch status { case .connecting, .connected, .lost: return true; default: return false }
    }

    init() {
        controller = ControllerReader(queue: queue)
        motion = MotionReader(queue: queue)
        controller.onChange = { [weak self] name in self?.controllerName = name }
        video.requestKeyframe = { [weak self] in
            guard let self, let c = self.connection, self.session != 0 else { return }
            c.send(content: Data(WWGP.keyframeRequest(session: self.session)), completion: .idempotent)
        }
    }

    // MARK: finding the game

    func browse() {
        guard browser == nil else { return }
        let b = NWBrowser(for: .bonjour(type: WWGP.bonjourType, domain: nil), using: .udp)
        b.browseResultsChangedHandler = { [weak self] results, _ in
            let found = results.compactMap { r -> Game? in
                if case let .service(name, _, _, _) = r.endpoint { return Game(name: name, endpoint: r.endpoint) }
                return nil
            }.sorted { $0.name < $1.name }
            Task { @MainActor in self?.games = found }
        }
        b.start(queue: .main)
        browser = b
    }

    func stopBrowsing() {
        browser?.cancel()
        browser = nil
    }

    // MARK: connecting

    func connect(_ game: Game) { connect(to: game.endpoint, label: game.name) }

    /// "192.168.1.20", "Mac-mini.local" or either with ":port"
    func connect(address: String) {
        var host = address.trimmingCharacters(in: .whitespaces)
        var port = WWGP.defaultPort
        // "host:port" (one colon) or "[IPv6]:port"; a bare IPv6 address keeps its colons
        if let colon = host.lastIndex(of: ":"), let p = UInt16(host[host.index(after: colon)...]),
           host.filter({ $0 == ":" }).count == 1 || host.hasPrefix("[") {
            port = p
            host = String(host[..<colon])
        }
        if host.hasPrefix("[") && host.hasSuffix("]") { host = String(host.dropFirst().dropLast()) }
        guard !host.isEmpty, let nwPort = NWEndpoint.Port(rawValue: port) else {
            status = .failed("Enter the computer's address.")
            return
        }
        connect(to: .hostPort(host: NWEndpoint.Host(host), port: nwPort), label: host)
    }

    private func connect(to endpoint: NWEndpoint, label: String) {
        disconnect()
        status = .connecting(label)
        UIApplication.shared.isIdleTimerDisabled = true
        if gyro { motion.start() }
        let params = NWParameters.udp
        params.serviceClass = .interactiveVoice  // low-latency Wi-Fi queue
        let c = NWConnection(to: endpoint, using: params)
        let device = UIDevice.current.name
        let controllerName = controllerName ?? ""
        queue.async { [self] in
            connection = c
            session = 0
            seq = 0
            nonce = UInt32.random(in: 1...UInt32.max)
            target = label
            lastWelcome = .distantPast
            lastHello = .distantPast
            c.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                if case let .failed(error) = state {
                    Task { @MainActor in self.status = .failed("\(label): \(error.localizedDescription)") }
                }
            }
            c.start(queue: queue)
            receive(on: c)
            let t = DispatchSource.makeTimerSource(flags: .strict, queue: queue)
            t.schedule(deadline: .now(), repeating: .milliseconds(8), leeway: .milliseconds(1))
            t.setEventHandler { [weak self] in self?.tick(c, device: device, controller: controllerName) }
            t.resume()
            timer = t
        }
    }

    func disconnect() {
        UIApplication.shared.isIdleTimerDisabled = false
        motion.stop()
        status = .idle
        queue.async { [self] in
            timer?.cancel()
            timer = nil
            if let c = connection {
                if session != 0 { c.send(content: Data(WWGP.bye(session: session)), completion: .idempotent) }
                // let the goodbye leave before the connection goes
                queue.asyncAfter(deadline: .now() + 0.1) { c.cancel() }
            }
            connection = nil
            session = 0
        }
    }

    // MARK: link queue

    nonisolated private func receive(on c: NWConnection) {
        c.receiveMessage { [weak self] data, _, _, error in
            guard let self, error == nil else { return }
            let bytes = data.map { Array($0) } ?? []
            if let (session, piece) = WWGP.video(bytes) {
                if session == self.session, c === self.connection { self.video.add(piece) }
            } else if let w = WWGP.welcome(bytes), w.nonce == self.nonce, c === self.connection {
                let wasConnected = self.session != 0 && Date().timeIntervalSince(self.lastWelcome) < 3
                if w.session != self.session { self.video.reset() }  // a new session: from its first keyframe
                self.session = w.session
                self.lastWelcome = Date()
                if !wasConnected { Task { @MainActor in self.status = .connected(w.host.isEmpty ? self.target : w.host) } }
            }
            self.receive(on: c)
        }
    }

    nonisolated private func tick(_ c: NWConnection, device: String, controller name: String) {
        guard c === connection, c.state == .ready else { return }
        let now = Date()
        if session != 0 && now.timeIntervalSince(lastWelcome) > 3 {
            session = 0
            let label = target
            Task { @MainActor in self.status = .lost(label) }
        }
        if session == 0 {
            if now.timeIntervalSince(lastHello) >= 0.5 {
                lastHello = now
                c.send(content: Data(WWGP.hello(nonce: nonce, device: device, controller: name, video: true)), completion: .idempotent)
            }
            return
        }
        var i = WWGP.Input()
        seq &+= 1
        i.seq = seq
        i.timeNs = DispatchTime.now().uptimeNanoseconds
        controller.read(into: &i)
        motion.read(into: &i)
        screen.read(into: &i)
        c.send(content: Data(WWGP.input(session: session, i)), completion: .idempotent)
    }
}
