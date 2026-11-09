// The app's two screens: choosing the game, and the GamePad itself (the touch screen area).
import SwiftUI

@main
struct WWHDGamePadApp: App {
    @StateObject private var link = GamePadLink()
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(link)
                .preferredColorScheme(.dark)
        }
        .onChange(of: phase) { _, now in
            // in the background iOS stops the sensors and the network: say goodbye, so the game
            // lets the keyboard and its own controllers be the GamePad again at once
            if now == .background { link.suspend(); link.stopBrowsing() }
            // back in front (maybe after allowing Local Network access, or on another Wi-Fi): look again,
            // and rejoin the game it was connected to if that is still there
            if now == .active {
                link.resumeIfNeeded()        // (first: the app is in front again)
                link.browse(restart: true)   // its results rejoin a listed game
            }
        }
    }
}

struct RootView: View {
    @EnvironmentObject private var link: GamePadLink

    var body: some View {
        switch link.status {
        case .connected, .lost: GamePadView()
        default: ConnectView()
        }
    }
}

// MARK: - choosing the game

struct ConnectView: View {
    @EnvironmentObject private var link: GamePadLink
    @MainActor private static var launchConnectDone = false
    @AppStorage("address") private var address = ""

    var body: some View {
        NavigationStack {
            List {
                // first, above the keyboard: the game asks for the code it shows on the computer
                if case let .pairing(name, prompt) = link.status {
                    PairingSection(game: name, prompt: prompt)
                }
                Section {
                    if link.games.isEmpty {
                        HStack(spacing: 12) {
                            ProgressView()
                            Text("Looking for Wind Waker HD on this network…").foregroundStyle(.secondary)
                        }
                    }
                    ForEach(link.games) { game in
                        Button { link.connect(game) } label: {
                            Label(game.name, systemImage: "desktopcomputer")
                        }
                    }
                } header: {
                    Text("Games on this network")
                } footer: {
                    VStack(alignment: .leading, spacing: 6) {
                        if let problem = link.browseProblem { Text(problem).foregroundStyle(.orange) }
                        Text("On the Mac: Settings > Controls > iPhone GamePad > Use an iPhone as the GamePad. The Mac and this phone must be on the same Wi-Fi network.")
                    }
                }

                Section("Or enter the computer's address") {
                    TextField("192.168.1.20 or Mac-name.local", text: $address)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .submitLabel(.go)
                        .onSubmit { link.connect(address: address) }
                    Button("Connect") { link.connect(address: address) }
                        .disabled(address.trimmingCharacters(in: .whitespaces).isEmpty)
                }

                Section("GamePad") {
                    LabeledContent("Controller", value: link.controllerName ?? "None: attach or pair one")
                    Toggle("Gyro aiming", isOn: $link.gyro)
                    Toggle("GamePad sound", isOn: $link.sound)
                }

                if let name = link.resuming {
                    Section {
                        HStack(spacing: 12) {
                            ProgressView()
                            Text("Rejoining \(name)…")
                            Spacer()
                            Button("Cancel") { link.cancelResume() }
                        }
                    }
                }
                if case let .connecting(name) = link.status {
                    Section {
                        HStack(spacing: 12) {
                            ProgressView()
                            Text("Connecting to \(name)…")
                            Spacer()
                            Button("Cancel") { link.disconnect() }
                        }
                    }
                }
                if case let .failed(why) = link.status {
                    Section { Text(why).foregroundStyle(.red) }
                }
            }
            .navigationTitle("GamePad")
        }
        .onAppear {
            link.browse()
            // `-connect <address>` at launch (Xcode scheme arguments, simctl launch): connect at once, once
            // (the argument stays for the whole launch; coming back from the background rejoins by itself)
            if !Self.launchConnectDone, case .idle = link.status {
                Self.launchConnectDone = true
                if let a = UserDefaults.standard.string(forKey: "connect") { link.connect(address: a) }
                else if let name = UserDefaults.standard.string(forKey: "join") { link.join(name) }  // `-join <name>`
            }
        }
    }
}

/// The game shows a code on the computer (Settings opens with it): typed here once, the phone is paired.
struct PairingSection: View {
    @EnvironmentObject private var link: GamePadLink
    let game: String
    let prompt: GamePadLink.PairPrompt
    @State private var code = ""
    @FocusState private var focused: Bool

    var body: some View {
        Section {
            switch prompt {
            case .expired:
                Text("The code expired or was refused on the computer.")
                Button("Show a new code") { link.requestNewPairingCode() }
            default:
                TextField("6-digit code", text: $code)
                    .keyboardType(.numberPad)
                    .textContentType(.oneTimeCode)
                    .font(.title2.monospacedDigit())
                    .focused($focused)
                    .onChange(of: code) { _, new in
                        let digits = String(new.filter(\.isNumber).prefix(6))
                        if digits != new { code = digits }
                        if digits.count == 6 { link.submitPairingCode(digits) }  // no extra tap
                    }
                    .disabled(prompt == .checking)
                if case let .wrongCode(left) = prompt {
                    Text("That code was wrong: \(left) \(left == 1 ? "try" : "tries") left.").foregroundStyle(.orange)
                }
                if prompt == .checking {
                    HStack(spacing: 8) { ProgressView(); Text("Checking…") }
                }
            }
            Button("Cancel", role: .cancel) { link.disconnect() }
        } header: {
            Text("Pair with \(game)")
        } footer: {
            Text("The first time, the game shows a code on the computer (Settings opens with it). Type it here; after that, this phone connects by itself.")
        }
        .onAppear { focused = true }
        .onChange(of: prompt) { _, new in
            if case .wrongCode = new { code = ""; focused = true }
        }
    }
}

// MARK: - the GamePad

/// The GamePad screen: the game's picture, or a note until the first one arrives.
struct ScreenArea: View {
    @ObservedObject var video: VideoReceiver

    var body: some View {
        ZStack {
            VideoView(video: video)
            if !video.showing {
                RoundedRectangle(cornerRadius: 12)
                    .fill(Color(white: 0.08))
                    .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color(white: 0.25), lineWidth: 1))
                    .overlay {
                        VStack(spacing: 8) {
                            ProgressView()
                            Text("Waiting for the GamePad screen…").font(.headline)
                            Text("Touch works already. The picture needs a game build with video (ios branch, step 2).")
                                .font(.footnote)
                                .multilineTextAlignment(.center)
                                .frame(maxWidth: 360)
                        }
                        .foregroundStyle(Color(white: 0.5))
                    }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: video.showing ? 0 : 12))
    }
}

struct GamePadView: View {
    @EnvironmentObject private var link: GamePadLink
    @State private var dragging = false
    @State private var finger: CGPoint?

    /// the GamePad picture's aspect (854 x 480)
    private static let aspect: CGFloat = 854.0 / 480.0

    var body: some View {
        GeometryReader { geo in
            let area = screenRect(in: geo.size)
            ZStack(alignment: .topLeading) {
                Color.black
                ScreenArea(video: link.video)
                    .frame(width: area.width, height: area.height)
                    .offset(x: area.minX, y: area.minY)
                if let p = finger {
                    Circle().fill(.white.opacity(0.35)).frame(width: 36, height: 36)
                        .position(p)
                        .allowsHitTesting(false)
                }
                statusBar
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .local)
                .onChanged { touch($0.location, in: area) }
                .onEnded { _ in release() })
            .onAppear { updateTurns() }
            .onChange(of: geo.size) { _, _ in updateTurns() }
        }
        .ignoresSafeArea()
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
    }

    private var statusBar: some View {
        HStack(spacing: 10) {
            switch link.status {
            case let .connected(host):
                Circle().fill(.green).frame(width: 8, height: 8)
                Text(host)
            case let .lost(host):
                ProgressView().controlSize(.small)
                Text("Reconnecting to \(host)…")
            default:
                EmptyView()
            }
            if link.controllerName == nil {
                Text("· no controller").foregroundStyle(.orange)
            }
            Spacer()
            Button("Disconnect") { link.disconnect() }
                .buttonStyle(.bordered)
                .controlSize(.small)
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .padding(.horizontal, 20)
        .padding(.top, 10)
    }

    /// the largest GamePad-shaped rectangle below the status line
    private func screenRect(in size: CGSize) -> CGRect {
        let top: CGFloat = 44, margin: CGFloat = 16
        let w = size.width - margin * 2, h = size.height - top - margin
        let fit = w / h > Self.aspect ? CGSize(width: h * Self.aspect, height: h) : CGSize(width: w, height: w / Self.aspect)
        return CGRect(x: (size.width - fit.width) / 2, y: top + (h - fit.height) / 2, width: fit.width, height: fit.height)
    }

    /// as the game's own touch mapping (gamepad_touch.h): a press must start on the screen area, a drag
    /// may leave it (held at the edge)
    private func touch(_ p: CGPoint, in area: CGRect) {
        if !dragging {
            guard area.contains(p) else { return }
            dragging = true
        }
        let x = Float(min(max((p.x - area.minX) / area.width, 0), 1))
        let y = Float(min(max((p.y - area.minY) / area.height, 0), 1))
        finger = CGPoint(x: area.minX + CGFloat(x) * area.width, y: area.minY + CGFloat(y) * area.height)
        link.screen.setTouch(true, x: x, y: y)
    }

    private func release() {
        dragging = false
        finger = nil
        link.screen.setTouch(false)
    }

    /// the screen rotation the motion samples are turned by (counter-clockwise quarter turns from portrait)
    private func updateTurns() {
        let scene = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first
        let turns: UInt8
        switch scene?.effectiveGeometry.interfaceOrientation ?? .portrait {
        case .landscapeRight: turns = 1      // the phone's top to the left
        case .portraitUpsideDown: turns = 2
        case .landscapeLeft: turns = 3       // the phone's top to the right
        default: turns = 0
        }
        link.screen.setQuarterTurns(turns)
    }
}
