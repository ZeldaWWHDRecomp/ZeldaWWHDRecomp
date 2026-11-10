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
            if now != .active { link.screen.setTouch(false) }  // a drag cut off by Control Center never ends
            if now == .background { link.suspend(); link.stopBrowsing() }
            if now == .active {
                link.resumeIfNeeded()        // first: browsing only runs in front
                link.browse(restart: true)
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
            // test aids: `-connect <address>` / `-join <name>` launch arguments, once per launch (they stay set
            // for the whole launch, and coming back from the background rejoins by itself)
            if !Self.launchConnectDone, case .idle = link.status {
                Self.launchConnectDone = true
                if let a = UserDefaults.standard.string(forKey: "connect") { link.connect(address: a) }
                else if let name = UserDefaults.standard.string(forKey: "join") { link.join(name) }
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
                Text("No code right now: it expired or was refused on the computer, or another phone is pairing. A new code can take up to 30 seconds.")
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
                        if digits.count == 6 { link.submitPairingCode(digits) }
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
                            Text("Touch works already. The picture needs the Metal renderer on the computer.")
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
    /// the black bars beside the picture take the battery and latency columns from this width outside
    /// the safe area (landscape)
    private static let sideBarWidth: CGFloat = 56

    var body: some View {
        // the picture may use the whole display; the columns and the top line stay clear of the camera
        // cutout (beside the picture in landscape, above it in portrait)
        GeometryReader { safe in
            pad(insets: safe.safeAreaInsets)
                .ignoresSafeArea()
        }
        .overlay {
            if let prompt = link.textPrompt { TextPromptCard(prompt: prompt) }
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
    }

    private func pad(insets: EdgeInsets) -> some View {
        GeometryReader { geo in
            let full = fullHeight(geo.size)
            let area = full ?? screenRect(in: geo.size, below: insets.top)
            // full height: the strips beside the picture, whole (the camera cutout sits mid-height on one
            // side, so their contents go to the top and bottom); otherwise inside the safe area
            let column = full != nil ? (left: area.minX, right: geo.size.width - area.maxX)
                : (left: area.minX - insets.leading, right: geo.size.width - insets.trailing - area.maxX)
            let sideBars = full != nil || min(column.left, column.right) >= Self.sideBarWidth
            ZStack(alignment: .topLeading) {
                Color.black
                // the game's GamePad boot image fills what the picture leaves, a little darker so the
                // status and the columns beside the picture stay readable
                if let image = link.bootImage {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFill()
                        .frame(width: geo.size.width, height: geo.size.height)
                        .clipped()
                        .overlay(Color.black.opacity(0.35))
                }
                ScreenArea(video: link.video)
                    .frame(width: area.width, height: area.height)
                    .offset(x: area.minX, y: area.minY)
                if let p = finger {
                    Circle().fill(.white.opacity(0.35)).frame(width: 36, height: 36)
                        .position(p)
                        .allowsHitTesting(false)
                }
                if full != nil {
                    VStack {
                        connectionLabel.outlined()
                        Spacer()
                        BatteryColumn(batteries: link.batteries, controller: link.controllerName).outlined()
                    }
                    .padding(.vertical, 18)
                    .frame(width: column.left, height: geo.size.height)
                    VStack {
                        Button { link.disconnect() } label: { Image(systemName: "xmark") }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                            .accessibilityLabel("Disconnect")
                        Spacer()
                        LatencyColumn(reading: link.latency).outlined()
                    }
                    .padding(.vertical, 18)
                    .frame(width: column.right, height: geo.size.height)
                    .offset(x: area.maxX)
                } else {
                    statusBar(sideBars: sideBars)
                        .outlined()
                        .padding(EdgeInsets(top: insets.top, leading: insets.leading, bottom: 0, trailing: insets.trailing))
                }
                if sideBars && full == nil {
                    BatteryColumn(batteries: link.batteries, controller: link.controllerName)
                        .outlined()
                        .frame(width: column.left, height: geo.size.height)
                        .offset(x: insets.leading)
                    LatencyColumn(reading: link.latency)
                        .outlined()
                        .frame(width: column.right, height: geo.size.height)
                        .offset(x: area.maxX)
                }
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .local)
                .onChanged { touch($0.location, in: area) }
                .onEnded { _ in release() })
            .onAppear {
                updateTurns()
                link.batteries.start()
            }
            .onDisappear {
                release()
                link.batteries.stop()
            }
            .onChange(of: geo.size) { _, _ in updateTurns() }
        }
    }

    private func statusBar(sideBars: Bool) -> some View {
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
            if !sideBars {
                if let l = link.latency {
                    Text("picture \(LatencyColumn.ms(l.pictureMs)) · ping \(LatencyColumn.ms(l.pingMs))").monospacedDigit()
                }
                PhoneBatteryLabel(batteries: link.batteries)
            }
            Button("Disconnect") { link.disconnect() }
                .buttonStyle(.bordered)
                .controlSize(.small)
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .padding(.horizontal, 20)
        .padding(.top, 10)
    }

    /// the connection in a side strip (full height): its state, the computer and a missing controller
    private var connectionLabel: some View {
        VStack(spacing: 4) {
            switch link.status {
            case let .connected(host):
                Circle().fill(.green).frame(width: 8, height: 8)
                Text(host).lineLimit(2).multilineTextAlignment(.center)
            case let .lost(host):
                ProgressView().controlSize(.small)
                Text(host).lineLimit(2).multilineTextAlignment(.center)
            default:
                EmptyView()
            }
            if link.controllerName == nil {
                Text("no controller").foregroundStyle(.orange).multilineTextAlignment(.center)
            }
        }
        .font(.caption2)
        .foregroundStyle(.secondary)
        .padding(.horizontal, 6)
    }

    /// Landscape on a display wider than the picture: the picture the display's whole height, centred, when
    /// the strips beside it can take the columns (the status line moves into them); nil otherwise.
    private func fullHeight(_ size: CGSize) -> CGRect? {
        let w = size.height * Self.aspect
        guard (size.width - w) / 2 >= Self.sideBarWidth else { return nil }
        return CGRect(x: (size.width - w) / 2, y: 0, width: w, height: size.height)
    }

    /// the largest GamePad-shaped rectangle below the status line (which starts at `inset`)
    private func screenRect(in size: CGSize, below inset: CGFloat) -> CGRect {
        let top: CGFloat = inset + 44, margin: CGFloat = 16
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

/// The game asks for text (the name of a new file): the phone's keyboard, as on a Wii U GamePad. The
/// prompt on the TV works too; both show the same text.
struct TextPromptCard: View {
    @EnvironmentObject private var link: GamePadLink
    let prompt: GamePadLink.TextPromptState
    @State private var text = ""
    @FocusState private var focused: Bool

    var body: some View {
        ZStack(alignment: .top) {
            Color.black.opacity(0.6)
                .ignoresSafeArea()
                .onTapGesture { focused = true }
            VStack(alignment: .leading, spacing: 12) {
                Text(prompt.hint.isEmpty ? "Enter text" : prompt.hint)
                    .font(.headline)
                HStack {
                    TextField("", text: $text)
                        .textFieldStyle(.roundedBorder)
                        .focused($focused)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.words)
                        .submitLabel(.done)
                        .onSubmit { link.textFinished(ok: true) }
                    if prompt.maxLen > 0 {
                        Text("\(text.utf16.count)/\(prompt.maxLen)")
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                    }
                }
                HStack {
                    Spacer()
                    Button("Cancel", role: .cancel) { link.textFinished(ok: false) }
                        .buttonStyle(.bordered)
                    Button("OK") { link.textFinished(ok: true) }
                        .buttonStyle(.borderedProminent)
                }
            }
            .padding(16)
            .frame(maxWidth: 520)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            .padding(.horizontal)
            .padding(.top, 12)
        }
        .onAppear {
            text = prompt.text
            focused = true
        }
        .onChange(of: text) { _, new in
            // the game counts UTF-16 units
            var t = new
            while prompt.maxLen > 0, t.utf16.count > prompt.maxLen { t.removeLast() }
            if t != new { text = t; return }
            link.textEdited(t)
        }
        // the game's text: a new prompt, or characters its font lacks left out (not its echo of the
        // typing, which can be behind)
        .onChange(of: prompt.gameEdits) { _, _ in text = prompt.text }
    }
}

/// Left of the GamePad screen: the phone's battery, and the controller's when it has its own.
struct BatteryColumn: View {
    @ObservedObject var batteries: Batteries
    let controller: String?

    var body: some View {
        VStack(spacing: 24) {
            if let b = batteries.phone { item(b, name: "iPhone") }
            if let b = batteries.controller { item(b, name: controller ?? "Controller") }
        }
        .padding(.horizontal, 6)
        .allowsHitTesting(false)
    }

    private func item(_ b: Batteries.Level, name: String) -> some View {
        VStack(spacing: 4) {
            Image(systemName: b.symbol).font(.title3).foregroundStyle(b.low ? .red : .secondary)
            Text(b.percent).font(.callout.monospacedDigit()).foregroundStyle(.secondary)
            Text(name).font(.caption2).foregroundStyle(.tertiary).multilineTextAlignment(.center).lineLimit(2)
        }
    }
}

/// the phone's battery in the top line (portrait: no side bars)
struct PhoneBatteryLabel: View {
    @ObservedObject var batteries: Batteries

    var body: some View {
        if let b = batteries.phone {
            Label(b.percent, systemImage: b.symbol).foregroundStyle(b.low ? .red : .secondary)
        }
    }
}

/// Right of the GamePad screen: how old the picture is when it arrives, and the network round trip.
struct LatencyColumn: View {
    let reading: LatencyMeter.Reading?

    static func ms(_ v: Double?) -> String { v.map { "\(Int($0.rounded())) ms" } ?? "–" }

    var body: some View {
        VStack(spacing: 24) {
            item(Self.ms(reading?.pictureMs), name: "picture")
            item(Self.ms(reading?.pingMs), name: "ping")
        }
        .padding(.horizontal, 6)
        .allowsHitTesting(false)
    }

    private func item(_ value: String, name: String) -> some View {
        VStack(spacing: 4) {
            Text(value).font(.callout.monospacedDigit()).foregroundStyle(.secondary)
            Text(name).font(.caption2).foregroundStyle(.tertiary)
        }
    }
}

extension View {
    /// a thin black outline (stacked tight shadows), so text stays readable on the game's boot image
    func outlined() -> some View {
        shadow(color: .black, radius: 0.6).shadow(color: .black, radius: 0.6).shadow(color: .black, radius: 1)
    }
}
