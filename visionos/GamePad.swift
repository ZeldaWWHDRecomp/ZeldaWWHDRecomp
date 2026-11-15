// Apple Vision Pro: the game's two windows as SwiftUI scenes, so the GamePad's can be placed below the game's.
// The game's window shows the TV picture on a 3D screen (gfx/host_tvos.mm's view under StereoScreen.swift's); the
// GamePad's shows the GamePad picture, where a pinch touches it.
import SwiftUI
import UIKit

// the game's view controller (host_tvos.mm), told when it is in its window
private final class GameHost: UIViewController {
    private let game = Unmanaged<UIViewController>.fromOpaque(wwhd_game_view_controller()).takeRetainedValue()
    override func viewDidLoad() {
        super.viewDidLoad()
        addChild(game)
        game.view.frame = view.bounds
        game.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(game.view)
        game.didMove(toParent: self)
    }
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        wwhd_game_window_shown(Unmanaged.passUnretained(game).toOpaque())
    }
}

private struct GameScreen: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> GameHost { GameHost() }
    func updateUIViewController(_ controller: GameHost, context: Context) {}
}

// the game window: the game, the GamePad button on its right edge (the bottom is the window bar's: move, close,
// share; the corners resize)
private struct GameWindow: View {
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        GameScreen()
            .ornament(attachmentAnchor: .scene(.trailing), contentAlignment: .leading) { ScreenButtons() }
            .onAppear { gamePadOpener = { openWindow(id: "gamepad") } }
    }
}

// the GamePad window: opens the game's window if the app started with only this one restored
private struct GamePadWindow: View {
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        GamePadScreen { openWindow(id: "game") }.aspectRatio(854.0 / 480.0, contentMode: .fit)
    }
}

private struct GamePadScreen: UIViewRepresentable {
    let openGame: () -> Void
    func makeUIView(context: Context) -> UIView {
        let view = Unmanaged<UIView>.fromOpaque(wwhd_gamepad_view()).takeRetainedValue()
        wwhd_gamepad_shown(Unmanaged.passUnretained(view).toOpaque(), 1)
        wwhd_vision_event("GamePad view made; game window up: \(wwhd_game_window_up())")
        DispatchQueue.main.async {
            // tagged as the GamePad's window (wwhd_gamepad_open, _close find it by it)
            view.window?.windowScene?.session.userInfo = ["window": "gamepad"]
            // the app started with only this window restored: the game's window first (the game opens this one again)
            guard !wwhd_game_window_up() else { return }
            wwhd_vision_event("no game window: asking for one")
            openGame()
            wwhd_gamepad_close()
        }
        return view
    }
    func updateUIView(_ view: UIView, context: Context) {}
    static func dismantleUIView(_ view: UIView, coordinator: ()) {
        wwhd_gamepad_shown(Unmanaged.passUnretained(view).toOpaque(), 0)
    }
}

@objc(WWVisionScenes)
final class WWVisionScenes: UIResponder, UIHostingSceneDelegate {
    @SceneBuilder static var rootScene: some Scene {
        WindowGroup(id: "game") { GameWindow() }
            .defaultSize(width: 1280, height: 720)
        WindowGroup(id: "gamepad") {
            GamePadWindow()
        }
        .defaultSize(width: 854, height: 480)
        // below the game's window; without one, close in front of the player
        .defaultWindowPlacement { _, context in
            let game = context.windows.first { $0.id == "game" }
            wwhd_vision_event("GamePad window placed \(game == nil ? "as a utility panel (no game window)" : "below the game window")")
            return WindowPlacement(game.map { .below($0) } ?? .utilityPanel)
        }
    }
}

// set by the game window: opens the GamePad window with SwiftUI's openWindow (its placement applies then)
@MainActor private var gamePadOpener: (() -> Void)?

private func gamePadSessions() -> [UISceneSession] {
    UIApplication.shared.openSessions.filter { $0.userInfo?["window"] as? String == "gamepad" }
}

// display.mm when the game's window is up, and the game window's GamePad button (on the main thread): opens the
// GamePad's window, or brings it back in front
@_cdecl("wwhd_gamepad_open")
public func wwhd_gamepad_open() {
    MainActor.assumeIsolated {
        // visionOS may restore the last run's GamePad window by itself: bring that one back rather than open another
        // (only the newest view is drawn to; another would stay black), and close any extra
        let existing = gamePadSessions()
        if let first = existing.first {
            for extra in existing.dropFirst() { UIApplication.shared.requestSceneSessionDestruction(extra, options: nil) }
            UIApplication.shared.activateSceneSession(for: UISceneSessionActivationRequest(session: first)) { error in
                wwhd_vision_log("[vision] the GamePad window didn't come back: \(error.localizedDescription)")
            }
            return
        }
        guard let gamePadOpener else {
            wwhd_vision_log("[vision] no game window to open the GamePad window from")
            return
        }
        gamePadOpener()
    }
}

// display.mm, when the GamePad screen mode leaves the window: closes the GamePad's window
@_cdecl("wwhd_gamepad_close")
public func wwhd_gamepad_close() {
    for session in gamePadSessions() { UIApplication.shared.requestSceneSessionDestruction(session, options: nil) }
}
