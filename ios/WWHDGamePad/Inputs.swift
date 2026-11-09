// What the phone reads for the GamePad: the attached controller (GameController), the motion sensors
// (CoreMotion) and the finger on the GamePad screen area. Everything is read on the link's queue when an
// Input is sent; touch and the screen rotation come from the main thread behind a lock.
import CoreMotion
import Foundation
import GameController
import os

/// The controller attached to (or paired with) the phone.
final class ControllerReader: @unchecked Sendable {  // (its state lives on the link queue)
    private let queue: DispatchQueue
    private var controller: GCController?          // touched on `queue` only
    private var observers: [NSObjectProtocol] = []
    /// main thread: the attached controller's name, nil without one
    var onChange: ((String?) -> Void)?

    init(queue: DispatchQueue) {
        self.queue = queue
        let nc = NotificationCenter.default
        for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect, .GCControllerDidBecomeCurrent] {
            observers.append(nc.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in self?.pick() })
        }
        GCController.startWirelessControllerDiscovery {}
        pick()
    }

    deinit { for o in observers { NotificationCenter.default.removeObserver(o) } }

    /// on `queue`: the controller in use (rumble goes to it when it has a motor)
    var current: GCController? { controller }

    /// main thread: use the current controller (the one pressed last), else any with a full layout
    private func pick() {
        let c = GCController.current?.extendedGamepad != nil ? GCController.current
              : GCController.controllers().first { $0.extendedGamepad != nil }
        if let g = c?.extendedGamepad {
            c?.handlerQueue = queue
            // the game's Home and Options / Menu: not the system's (Game Center, screenshots)
            for b in [g.buttonHome, g.buttonOptions, g.buttonMenu] { b?.preferredSystemGestureState = .disabled }
        }
        queue.async { self.controller = c }
        onChange?(c.map { $0.vendorName ?? $0.productCategory })
    }

    /// on `queue`: the controller part of the Input
    func read(into i: inout WWGP.Input) {
        guard let g = controller?.extendedGamepad else { i.controller = false; return }
        i.controller = true
        var b: WWGP.Buttons = []
        func on(_ e: GCControllerButtonInput?, _ bit: WWGP.Buttons) { if e?.isPressed == true { b.insert(bit) } }
        on(g.buttonA, .a); on(g.buttonB, .b); on(g.buttonX, .x); on(g.buttonY, .y)
        on(g.leftShoulder, .lb); on(g.rightShoulder, .rb)
        on(g.leftThumbstickButton, .l3); on(g.rightThumbstickButton, .r3)
        on(g.buttonMenu, .menu); on(g.buttonOptions, .options); on(g.buttonHome, .home)
        on(g.dpad.up, .dUp); on(g.dpad.down, .dDown); on(g.dpad.left, .dLeft); on(g.dpad.right, .dRight)
        on(controller?.physicalInputProfile.buttons[GCInputButtonShare], .capture)
        i.buttons = b
        i.lx = g.leftThumbstick.xAxis.value; i.ly = g.leftThumbstick.yAxis.value
        i.rx = g.rightThumbstick.xAxis.value; i.ry = g.rightThumbstick.yAxis.value
        i.lt = g.leftTrigger.value; i.rt = g.rightTrigger.value
    }
}

/// The phone's gyro and accelerometer.
final class MotionReader: @unchecked Sendable {  // (its state lives on the link queue)
    private let manager = CMMotionManager()
    private let operations = OperationQueue()
    private var latest: CMDeviceMotion?   // on the link queue
    private var sent: TimeInterval = 0    // the last sample's time already sent (each is sent once)

    init(queue: DispatchQueue) {
        operations.underlyingQueue = queue
        operations.maxConcurrentOperationCount = 1
    }

    var available: Bool { manager.isDeviceMotionAvailable }

    func start() {
        guard manager.isDeviceMotionAvailable, !manager.isDeviceMotionActive else { return }
        manager.deviceMotionUpdateInterval = 1.0 / 100
        manager.startDeviceMotionUpdates(to: operations) { [weak self] m, _ in self?.latest = m }
    }

    func stop() {
        manager.stopDeviceMotionUpdates()
        operations.addOperation { self.latest = nil }
    }

    /// on the link queue: a sample not sent before, in the protocol's conventions
    func read(into i: inout WWGP.Input) {
        guard let m = latest, m.timestamp != sent else { i.motion = false; return }
        sent = m.timestamp
        i.motion = true
        i.motionNs = UInt64(m.timestamp * 1e9)
        i.gyro = SIMD3(Float(m.rotationRate.x), Float(m.rotationRate.y), Float(m.rotationRate.z))
        // CoreMotion reports gravity and acceleration in g pointing down (z = -1 lying face up); the
        // protocol wants the specific force in m/s^2 (z = +9.81 lying face up), as Android and SDL report
        let g = -9.80665
        i.accel = SIMD3(Float((m.gravity.x + m.userAcceleration.x) * g),
                        Float((m.gravity.y + m.userAcceleration.y) * g),
                        Float((m.gravity.z + m.userAcceleration.z) * g))
    }
}

/// The finger on the GamePad screen area and the screen's rotation: written on the main thread.
final class TouchAndScreen: @unchecked Sendable {
    private struct State { var touch = false; var x: Float = 0; var y: Float = 0; var quarterTurns: UInt8 = 0 }
    private let lock = OSAllocatedUnfairLock(initialState: State())

    func setTouch(_ down: Bool, x: Float = 0, y: Float = 0) {
        lock.withLock { $0.touch = down; $0.x = x; $0.y = y }
    }
    func setQuarterTurns(_ q: UInt8) { lock.withLock { $0.quarterTurns = q } }

    func read(into i: inout WWGP.Input) {
        let s = lock.withLock { $0 }
        i.touch = s.touch
        i.tx = s.touch ? s.x : 0
        i.ty = s.touch ? s.y : 0
        i.quarterTurns = s.quarterTurns
    }
}
