// The connection to the game on the computer (runtime/src/remote_pad/): finds it over Bonjour, says
// Hello until the game answers, then sends an Input about 120 times a second. The game answers about
// once a second; three seconds without an answer and the phone says Hello again (the game restarted,
// the network dropped).
import Foundation
import Network
import UIKit
import os

private let logger = Logger(subsystem: "org.wwhdrecomp.gamepad", category: "link")
/// the unified log, and stdout (Xcode's console, `devicectl device process launch --console`)
private func note(_ s: String) {
    logger.info("\(s, privacy: .public)")
    print("[link] \(s)")
}

@MainActor
final class GamePadLink: ObservableObject {
    enum Status: Equatable {
        case idle
        case connecting(String)       // to this game
        case connected(String)        // the computer's name
        case lost(String)             // was connected; saying Hello again
        case pairing(String, PairPrompt)  // this game shows a code to type in (protocol.h kPairing)
        case failed(String)
    }
    enum PairPrompt: Equatable {
        case enterCode                // the computer shows it now
        case wrongCode(Int)           // tries left
        case expired                  // no code pending (expired, refused, tries used up): ask for a new one
        case checking                 // sent; waiting for the answer
    }

    struct Game: Identifiable, Hashable {
        let name: String
        let endpoint: NWEndpoint
        var id: String { name }
    }

    @Published private(set) var status: Status = .idle
    @Published private(set) var games: [Game] = []
    /// why nothing is found, when Bonjour says (nil while it browses normally)
    @Published private(set) var browseProblem: String?
    /// the game being rejoined after the app came back to the front (resumeIfNeeded)
    @Published private(set) var resuming: String?

    /// what a connection was made to: a game found over Bonjour (by name) or a typed address
    enum Target: Equatable { case game(String), address(String) }
    private var lastTarget: Target?      // the latest connect()
    private var resumeTarget: Target?    // left for the background while connected: rejoin on return
    private var resumeAttempt = 0        // a newer attempt or a cancel makes an older one's timeout moot
    /// the app is in front: only then does it look for games or rejoin one (a connection made in the
    /// background dies when iOS suspends the app, moments later)
    private var foreground = true
    static let resumeTimeout: TimeInterval = 5
    @Published private(set) var controllerName: String?
    @Published private(set) var latency: LatencyMeter.Reading?
    /// the game asks for text (the name of a new file): the GamePad screen shows a keyboard
    @Published private(set) var textPrompt: TextPromptState?
    /// the running game's GamePad boot image (BootImage.swift): the GamePad screen's background
    @Published private(set) var bootImage: UIImage?
    struct TextPromptState: Equatable {
        var serial: UInt32
        var hint: String
        var maxLen: Int      // UTF-16 units
        var text: String     // the game's field as the phone last knew it
        var gameEdits: Int   // +1 when the game set `text`: the field shows it then
    }
    let batteries = Batteries()
    /// the GamePad speaker plays here (the Mac then plays the TV sound alone); sent as a Hello flag
    @Published var sound = UserDefaults.standard.object(forKey: "sound") as? Bool ?? true {
        didSet {
            UserDefaults.standard.set(sound, forKey: "sound")
            let on = sound
            queue.async { [self] in
                wantsAudio = on
                if !on { speaker.reset() }
                sendHello()  // the same session: the game switches its speaker at once
            }
        }
    }
    @Published var gyro = UserDefaults.standard.object(forKey: "gyro") as? Bool ?? true {
        didSet {
            UserDefaults.standard.set(gyro, forKey: "gyro")
            if isActive { gyro ? motion.start() : motion.stop() }
        }
    }

    nonisolated let queue = DispatchQueue(label: "org.wwhdrecomp.gamepad.link", qos: .userInteractive)
    nonisolated let screen = TouchAndScreen()
    nonisolated let video = VideoReceiver()
    nonisolated let rumble: RumblePlayer      // (link queue)
    nonisolated let speaker = Speaker()       // (link queue)
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
    nonisolated(unsafe) private var lastPing = Date.distantPast
    nonisolated(unsafe) private var meter = LatencyMeter()
    nonisolated(unsafe) private var textSync = TextEntrySync()
    nonisolated(unsafe) private var bootImageId: UInt32 = 0          // the one shown (0: none)
    nonisolated(unsafe) private var bootPieces = BootImageAssembler()
    nonisolated(unsafe) private var lastBootWant = Date.distantPast
    nonisolated(unsafe) private var target = ""
    nonisolated(unsafe) private var helloDevice = "", helloController = ""
    nonisolated(unsafe) private var wantsAudio = UserDefaults.standard.object(forKey: "sound") as? Bool ?? true
    nonisolated(unsafe) private var pairingWait = false   // a code is asked for: no Hellos until it is typed
    nonisolated(unsafe) private var token: [UInt8] = []   // this game's pairing token (Pairing.token(for:))
    /// this phone's id for pairing (pairing.h): random, kept for good
    nonisolated static let phoneID: [UInt8] = {
        if let hex = UserDefaults.standard.string(forKey: "phoneID"), let b = Pairing.bytes(hex), b.count == 16 { return b }
        var b = [UInt8](repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, 16, &b)
        UserDefaults.standard.set(Pairing.hex(b), forKey: "phoneID")
        return b
    }()

    private var isActive: Bool {
        switch status { case .connecting, .connected, .lost: return true; default: return false }
    }

    init() {
        rumble = RumblePlayer(queue: queue)
        controller = ControllerReader(queue: queue)
        motion = MotionReader(queue: queue)
        controller.onChange = { [weak self] c in
            self?.controllerName = c.map { $0.vendorName ?? $0.productCategory }
            self?.batteries.gamepad = c
        }
        video.frameShown = { [weak self] frame in
            self?.meter.frame(computerNs: frame.timeNs, receivedNs: DispatchTime.now().uptimeNanoseconds)
        }
        video.requestKeyframe = { [weak self] in
            guard let self, let c = self.connection, self.session != 0 else { return }
            c.send(content: Data(WWGP.keyframeRequest(session: self.session)), completion: .idempotent)
        }
    }

    // MARK: finding the game

    /// Starts looking for games; `restart` starts over (the app came to the front, the browser failed or
    /// waited: a browser that began before Local Network access was allowed, or before Wi-Fi was up, can
    /// stay waiting and never report anything)
    func browse(restart: Bool = false) {
        if restart { stopBrowsing() }
        guard browser == nil, foreground else { return }
        let b = NWBrowser(for: .bonjour(type: WWGP.bonjourType, domain: nil), using: .udp)
        b.stateUpdateHandler = { [weak self, weak b] state in
            note("browser: \(state)")
            MainActor.assumeIsolated {  // (the browser runs on the main queue)
                guard let self, let b, b === self.browser else { return }
                switch state {
                case .ready:
                    self.browseProblem = nil
                case let .waiting(error):
                    self.browseProblem = Self.describe(error)
                    // try again shortly: waiting browsers do not always recover on their own
                    DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self, weak b] in
                        guard let self, let b, b === self.browser, case .waiting = b.state else { return }
                        self.browse(restart: true)
                    }
                case let .failed(error):
                    self.browseProblem = Self.describe(error)
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.browse(restart: true) }
                default:
                    break
                }
            }
        }
        b.browseResultsChangedHandler = { [weak self] results, _ in
            let found = results.compactMap { r -> Game? in
                if case let .service(name, _, _, _) = r.endpoint { return Game(name: name, endpoint: r.endpoint) }
                return nil
            }.sorted { $0.name < $1.name }
            note("browser: found \(found.map(\.name))")
            Task { @MainActor in
                self?.games = found
                self?.resumeIfFound()
            }
        }
        b.start(queue: .main)
        browser = b
    }

    nonisolated private static func describe(_ error: NWError) -> String {
        if case let .dns(code) = error {
            switch Int(code) {
            case -65570: return "Local Network access is off for GamePad: Settings > Privacy & Security > Local Network."  // PolicyDenied
            case -65555: return "Bonjour refused the search (NoAuth). Enter the address below."
            default: break
            }
        }
        return "Bonjour: \(error.localizedDescription). Enter the address below."
    }

    func stopBrowsing() {
        browser?.cancel()
        browser = nil
    }

    // MARK: connecting

    /// The player's choice: it replaces a pending rejoin.
    func connect(_ game: Game) {
        cancelResume()
        open(game)
    }

    /// "192.168.1.20", "Mac-mini.local" or either with ":port"
    func connect(address: String) {
        cancelResume()
        open(address: address)
    }

    private func open(_ game: Game) {
        lastTarget = .game(game.name)
        connect(to: game.endpoint, label: game.name)
    }

    private func open(address: String) {
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
        lastTarget = .address(address)
        connect(to: .hostPort(host: NWEndpoint.Host(host), port: nwPort), label: host)
    }

    private func connect(to endpoint: NWEndpoint, label: String) {
        drop()
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
            token = Pairing.token(for: label)
            // the image this computer's game showed last time, at once (it says so if its game has another)
            bootImageId = 0
            showBootImage(BootImageCache.remembered(for: label), BootImageCache.load(BootImageCache.remembered(for: label)))
            lastBootWant = .distantPast
            pairingWait = false
            helloDevice = device
            helloController = controllerName
            lastWelcome = .distantPast
            lastHello = .distantPast
            c.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                if case let .failed(error) = state {
                    Task { @MainActor in self.linkStatus(.failed("\(label): \(error.localizedDescription)")) }
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

    /// The player's Disconnect (or Cancel): no rejoining later.
    func disconnect() {
        cancelResume()
        drop()
    }

    // MARK: leaving and coming back

    /// The app goes to the background: iOS stops the network and the sensors, so say goodbye now (the
    /// game gives the GamePad back to the keyboard and its own controllers at once) and remember what to
    /// rejoin if this phone was connected, connecting or reconnecting.
    func suspend() {
        foreground = false
        screen.setTouch(false)
        note("background: status \(status), last target \(String(describing: lastTarget))")
        if isActive, let t = lastTarget {
            resumeTarget = t
            note("background: will rejoin \(t) on return")
        }
        resuming = nil
        games = []  // only what Bonjour lists after the return counts as still there
        drop()
    }

    /// The app is in front again: rejoin what it left, if that is still there. A game found over Bonjour
    /// must be listed again (resumeIfFound); a typed address must answer. Either gets resumeTimeout.
    func resumeIfNeeded() {
        foreground = true
        note("front: rejoin \(String(describing: resumeTarget)), status \(status), listed \(games.map(\.name))")
        guard let t = resumeTarget else { return }
        resumeAttempt += 1
        let attempt = resumeAttempt
        switch t {
        case let .game(name):
            resuming = name
            resumeIfFound()
        case let .address(address):
            resumeTarget = nil
            resuming = nil
            open(address: address)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.resumeTimeout) { [weak self] in
            guard let self, attempt == self.resumeAttempt else { return }
            if case let .game(name) = t, self.resumeTarget == t {
                // never listed again: the game was closed (or its Mac left the network)
                self.cancelResume()
                self.status = .failed("\(name) is no longer on the network.")
            } else if case let .connecting(label) = self.status {
                // listed or typed, but no Welcome: nothing answers there any more
                self.drop()
                self.status = .failed("\(label) did not answer.")
            }
        }
    }

    /// a listed game is the one to rejoin: connect to it (its endpoint as Bonjour lists it now)
    private func resumeIfFound() {
        guard foreground, case let .game(name)? = resumeTarget, let game = games.first(where: { $0.name == name }) else { return }
        note("rejoining \(name)")
        resumeTarget = nil
        resuming = nil
        open(game)
    }

    /// `-join <name>` at launch: connect to the listed game of that name as soon as it is found
    func join(_ name: String) {
        resumeTarget = .game(name)
        resumeIfNeeded()
    }

    func cancelResume() {
        resumeTarget = nil
        resuming = nil
        resumeAttempt += 1
    }

    /// closes the connection (saying goodbye); what to rejoin is left alone
    private func drop() {
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
            rumble.reset()
            speaker.reset()
            meter = LatencyMeter()
            textSync.reset()
        }
        latency = nil
        textPrompt = nil
    }

    // MARK: the game's text prompt

    /// the player changed the field
    func textEdited(_ text: String) {
        queue.async { [self] in
            if let p = textSync.edit(text, now: ProcessInfo.processInfo.systemUptime) { sendText(p) }
        }
    }

    /// OK (true) or Cancel
    func textFinished(ok: Bool) {
        queue.async { [self] in
            if let p = textSync.finish(ok ? .ok : .cancel, now: ProcessInfo.processInfo.systemUptime) { sendText(p) }
        }
    }

    nonisolated private func sendText(_ p: TextEntrySync.Send) {
        guard let c = connection, session != 0 else { return }
        c.send(content: Data(WWGP.textInput(session: session, serial: p.serial, seq: p.seq, action: p.action, text: p.text)),
               completion: .idempotent)
    }

    /// link queue: show this boot image (data nil: none known) and remember it for this computer
    nonisolated private func showBootImage(_ id: UInt32, _ data: Data?) {
        guard let data, let image = UIImage(data: data) else { return }
        bootImageId = id
        BootImageCache.remember(id, for: target)
        Task { @MainActor in self.bootImage = image }
    }

    nonisolated private func publishText() {
        let state = textSync.prompt.map {
            TextPromptState(serial: $0.serial, hint: $0.hint, maxLen: $0.maxLen, text: textSync.field,
                            gameEdits: textSync.gameEdits)
        }
        Task { @MainActor in self.textPrompt = state }
    }

    /// A status from the link queue. It arrives late, so it is dropped once the link was closed (idle) or
    /// given up (failed) in the meantime.
    private func linkStatus(_ s: Status) {
        switch status {
        case .idle, .failed: return
        default: status = s
        }
    }

    // MARK: link queue

    nonisolated private func receive(on c: NWConnection) {
        c.receiveMessage { [weak self] data, _, _, error in
            guard let self else { return }
            if error != nil {
                switch c.state {
                case .failed, .cancelled: return
                default: self.queue.asyncAfter(deadline: .now() + 0.1) { self.receive(on: c) }
                }
                return
            }
            let bytes = data.map { Array($0) } ?? []
            if let (session, piece) = WWGP.video(bytes) {
                if session == self.session, c === self.connection { self.video.add(piece) }
            } else if let (session, piece) = WWGP.audio(bytes) {
                if session == self.session, c === self.connection, self.wantsAudio { self.speaker.push(piece) }
            } else if let (session, sent, computer) = WWGP.pong(bytes) {
                if session == self.session, c === self.connection {
                    self.meter.pong(sentNs: sent, computerNs: computer, receivedNs: DispatchTime.now().uptimeNanoseconds)
                }
            } else if let (session, piece) = WWGP.bootImage(bytes) {
                if session == self.session, c === self.connection, piece.id != self.bootImageId {
                    if let cached = BootImageCache.load(piece.id) {
                        self.showBootImage(piece.id, cached)
                    } else if let whole = self.bootPieces.add(piece) {
                        let data = Data(whole)
                        BootImageCache.save(piece.id, data)
                        self.showBootImage(piece.id, data)
                    }
                }
            } else if let (session, prompt) = WWGP.text(bytes) {
                if session == self.session, c === self.connection, self.textSync.received(prompt) { self.publishText() }
            } else if let (session, seq, level) = WWGP.rumble(bytes) {
                if session == self.session, c === self.connection { self.rumble.set(seq: seq, level: level) }
            } else if let p = WWGP.pairing(bytes), p.nonce == self.nonce, c === self.connection {
                // not paired (or the token is no longer known there): ask the player for the code
                self.pairingWait = true
                let label = self.target
                let prompt: PairPrompt = switch p.state {
                case .enterCode: .enterCode
                case .wrongCode: .wrongCode(p.attempts)
                case .noCode: .expired
                }
                Task { @MainActor in self.linkStatus(.pairing(label, prompt)) }
            } else if let p = WWGP.paired(bytes), p.nonce == self.nonce, c === self.connection {
                let label = self.target
                Pairing.save(token: p.token, for: label)
                self.token = p.token
                self.pairingWait = false
                self.lastHello = .distantPast  // Hello with the token at once
                Task { @MainActor in self.linkStatus(.connecting(label)) }
            } else if let w = WWGP.welcome(bytes), w.nonce == self.nonce, c === self.connection {
                let wasConnected = self.session != 0 && Date().timeIntervalSince(self.lastWelcome) < 3
                if w.session != self.session {  // a new session: from its first keyframe, motor still
                    self.video.reset()
                    self.rumble.reset()
                    self.speaker.reset()
                    self.meter = LatencyMeter()
                }
                self.session = w.session
                self.lastWelcome = Date()
                if !wasConnected {
                    let host = w.host.isEmpty ? self.target : w.host
                    Task { @MainActor in self.linkStatus(.connected(host)) }
                }
            }
            self.receive(on: c)
        }
    }

    /// link queue: Hello with what this phone wants (the picture; the speaker if `sound` is on)
    nonisolated private func sendHello() {
        guard let c = connection else { return }
        c.send(content: Data(WWGP.hello(nonce: nonce, device: helloDevice, controller: helloController,
                                        video: true, audio: wantsAudio, id: Self.phoneID, token: token)),
               completion: .idempotent)
    }

    // MARK: pairing

    /// the code shown on the computer (6 digits)
    func submitPairingCode(_ code: String) {
        guard let n = UInt32(code.filter(\.isNumber)), n < 1_000_000, case let .pairing(label, _) = status else { return }
        status = .pairing(label, .checking)
        queue.async { [self] in
            guard let c = connection else { return }
            c.send(content: Data(WWGP.pair(nonce: nonce, id: Self.phoneID, code: n)), completion: .idempotent)
        }
    }

    /// after an expired or refused code: say Hello again, and the computer shows a new one
    func requestNewPairingCode() {
        guard case let .pairing(label, _) = status else { return }
        status = .connecting(label)
        queue.async { [self] in
            pairingWait = false
            lastHello = .distantPast
        }
    }

    nonisolated private func tick(_ c: NWConnection, device: String, controller name: String) {
        rumble.controller = controller.current
        rumble.tick()
        speaker.tick()
        guard c === connection, c.state == .ready else { return }
        let now = Date()
        if session != 0 && now.timeIntervalSince(lastWelcome) > 3 {
            session = 0
            let label = target
            Task { @MainActor in self.linkStatus(.lost(label)) }
        }
        if session == 0 {
            if !pairingWait, now.timeIntervalSince(lastHello) >= 0.5 {
                lastHello = now
                sendHello()
            }
            return
        }
        if let p = textSync.resend(now: ProcessInfo.processInfo.systemUptime) { sendText(p) }
        if now.timeIntervalSince(lastBootWant) >= 2 {  // (protocol.h kBootImageWant)
            lastBootWant = now
            c.send(content: Data(WWGP.bootImageWant(session: session, id: bootImageId)), completion: .idempotent)
        }
        if now.timeIntervalSince(lastPing) >= 0.5 {
            lastPing = now
            c.send(content: Data(WWGP.ping(session: session, phoneNs: DispatchTime.now().uptimeNanoseconds)),
                   completion: .idempotent)
            let reading = meter.reading()
            Task { @MainActor in if self.isActive { self.latency = reading } }
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
