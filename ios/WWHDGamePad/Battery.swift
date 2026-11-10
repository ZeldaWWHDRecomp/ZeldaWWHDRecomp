// The phone's and the controller's batteries, shown beside the GamePad screen.
import GameController
import UIKit

@MainActor
final class Batteries: ObservableObject {
    struct Level: Equatable {
        var fraction: Float
        var charging: Bool
    }

    @Published private(set) var phone: Level?
    @Published private(set) var controller: Level?
    /// the controller in use (ControllerReader)
    var gamepad: GCController? { didSet { update() } }
    private var observers: [NSObjectProtocol] = []
    private var timer: Timer?

    /// while the GamePad screen shows
    func start() {
        guard timer == nil else { return }
        UIDevice.current.isBatteryMonitoringEnabled = true
        for name in [UIDevice.batteryLevelDidChangeNotification, UIDevice.batteryStateDidChangeNotification] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.update() }
            })
        }
        // controllers post no battery notifications
        timer = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.update() }
        }
        update()
    }

    func stop() {
        for o in observers { NotificationCenter.default.removeObserver(o) }
        observers = []
        timer?.invalidate()
        timer = nil
        UIDevice.current.isBatteryMonitoringEnabled = false
    }

    private func update() {
        let d = UIDevice.current
        phone = d.batteryState == .unknown || d.batteryLevel < 0 ? nil
              : Level(fraction: d.batteryLevel, charging: d.batteryState != .unplugged)
        // a controller powered by the phone (Backbone, Kishi) reports none, or unknown at 0
        if let b = gamepad?.battery, b.batteryState != .unknown || b.batteryLevel > 0 {
            controller = Level(fraction: b.batteryLevel, charging: b.batteryState == .charging || b.batteryState == .full)
        } else {
            controller = nil
        }
    }
}

extension Batteries.Level {
    var symbol: String {
        if charging { return "battery.100percent.bolt" }
        switch fraction {
        case ..<0.13: return "battery.0percent"
        case ..<0.38: return "battery.25percent"
        case ..<0.63: return "battery.50percent"
        case ..<0.88: return "battery.75percent"
        default: return "battery.100percent"
        }
    }
    var low: Bool { !charging && fraction < 0.2 }
    var percent: String { "\(Int((fraction * 100).rounded()))%" }
}
