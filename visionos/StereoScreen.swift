// Apple Vision Pro: the game's window shows the TV picture on a RealityKit plane whose material gives each eye
// its own picture (the renderer's left and right eyes; the same for both in 2D). It lies over the game's own view
// (gfx/host_tvos.mm), which keeps the touches. The pictures come from gfx/vision_screen.mm's hand-off, copied into
// RealityKit's textures each frame they change.
import Metal
import RealityKit
import SwiftUI
import UIKit

// An unlit material, each eye its own picture, without RealityKit's tone mapping (the game's colours as drawn).
private let kMaterial = """
#usda 1.0
(
    defaultPrim = "Root"
)
def Xform "Root"
{
    def Material "StereoScreen"
    {
        asset inputs:Left
        asset inputs:Right
        token outputs:mtlx:surface.connect = </Root/StereoScreen/Unlit.outputs:out>
        def Shader "Unlit"
        {
            uniform token info:id = "ND_realitykit_unlit_surfaceshader"
            color3f inputs:color.connect = </Root/StereoScreen/PerEye.outputs:out>
            bool inputs:applyPostProcessToneMap = 0
            token outputs:out
        }
        def Shader "PerEye"
        {
            uniform token info:id = "ND_realitykit_geometry_switch_cameraindex_color3"
            color3f inputs:mono.connect = </Root/StereoScreen/LeftImage.outputs:out>
            color3f inputs:left.connect = </Root/StereoScreen/LeftImage.outputs:out>
            color3f inputs:right.connect = </Root/StereoScreen/RightImage.outputs:out>
            color3f outputs:out
        }
        def Shader "LeftImage"
        {
            uniform token info:id = "ND_RealityKitTexture2D_color3"
            asset inputs:file.connect = </Root/StereoScreen.inputs:Left>
            color3f outputs:out
        }
        def Shader "RightImage"
        {
            uniform token info:id = "ND_RealityKitTexture2D_color3"
            asset inputs:file.connect = </Root/StereoScreen.inputs:Right>
            color3f outputs:out
        }
    }
}
"""

@MainActor
private final class Screen {
    let plane = ModelEntity()
    let speakers = Entity()  // the 5.1 speakers, placed from the window's surface (playSurround)
    private var eyes: [LowLevelTexture] = []
    private var generation: UInt64 = 0
    private var queue: MTLCommandQueue?
    private var sources: [AudioGeneratorController] = []

    func make() async {
        do {
            let descriptor = LowLevelTexture.Descriptor(pixelFormat: .bgra8Unorm_srgb, width: Int(WWHD_SCREEN_WIDTH),
                                                        height: Int(WWHD_SCREEN_HEIGHT), textureUsage: [.shaderRead, .shaderWrite])
            eyes = [try LowLevelTexture(descriptor: descriptor), try LowLevelTexture(descriptor: descriptor)]
            var material = try await ShaderGraphMaterial(named: "/Root/StereoScreen", from: Data(kMaterial.utf8))
            try material.setParameter(name: "Left", value: .textureResource(try await TextureResource(from: eyes[0])))
            try material.setParameter(name: "Right", value: .textureResource(try await TextureResource(from: eyes[1])))
            plane.model = ModelComponent(mesh: try Self.quad(), materials: [material])
            queue = MTLCreateSystemDefaultDevice()?.makeCommandQueue()
            wwhd_screen_attach(1)
            playSurround()
        } catch {
            wwhd_vision_log("[screen] no 3D screen: \(error)")
        }
    }

    // a 1 x 1 plane facing +z whose texture coordinates start at the top row (the pictures are Metal textures;
    // generatePlane's show them upside down)
    private static func quad() throws -> MeshResource {
        var d = MeshDescriptor(name: "screen")
        d.positions = MeshBuffers.Positions([[-0.5, -0.5, 0], [0.5, -0.5, 0], [0.5, 0.5, 0], [-0.5, 0.5, 0]])
        d.textureCoordinates = MeshBuffers.TextureCoordinates([[0, 1], [1, 1], [1, 0], [0, 0]])
        d.primitives = .triangles([0, 1, 2, 0, 2, 3])
        return try MeshResource.generate(from: [d])
    }

    // The game's 5.1 from speakers around the player, each a spatial audio source playing its channel of the mix
    // (the system's renderer plays nothing on the headset's own speakers, and channel audio sounds inside the head):
    // the fronts and the centre at the window, the surrounds beside and a little behind a player about 1.5 m from it
    // (5.1's 110 degrees). Once the game's 5.1 output runs (tried each second).
    private static let layout: [(channel: Int32, at: SIMD3<Float>)] = [
        (0, [-0.8, 0, 0]), (1, [0.8, 0, 0]), (2, [0, 0, 0]),  // L R C (the LFE is mixed into L and R)
        (4, [-1.4, 0, 2.0]), (5, [1.4, 0, 2.0]),             // Ls Rs
    ]
    private func playSurround() {
        guard wwhd_audio_entity(1) else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in self?.playSurround() }
            return
        }
        do {
            for (channel, at) in Self.layout {
                let speaker = Entity()
                speaker.position = at
                // louder than a source in the room by default (they are the game's whole mix): 6 dB up, half the
                // fall-off with distance, less of the room's reverb
                speaker.spatialAudio = SpatialAudioComponent(gain: 6, reverbLevel: -6, distanceAttenuation: .rolloff(factor: 0.5))
                speakers.addChild(speaker)
                sources.append(try speaker.playAudio(configuration: AudioGeneratorConfiguration(layoutTag: kAudioChannelLayoutTag_Mono)) {
                    _, _, frames, data in
                    wwhd_audio_render_channel(channel, data, frames)
                    return noErr
                })
            }
            wwhd_vision_log("[audio] 5.1 from \(Self.layout.count) speakers around the player (RealityKit spatial audio)")
        } catch {
            sources.removeAll()
            wwhd_audio_entity(0)
            wwhd_vision_log("[audio] no 5.1 from the game window: \(error)")
        }
    }

    // each RealityKit frame: the newest pictures, when there are new ones
    func update() {
        guard eyes.count == 2, let queue else { return }
        var left: MTLTexture?, right: MTLTexture?
        var slot: Int32 = 0
        guard wwhd_screen_acquire(&generation, &slot, &left, &right) else { return }
        guard let left, let right, let commands = queue.makeCommandBuffer() else {
            wwhd_screen_release(slot)
            return
        }
        let targets = [eyes[0].replace(using: commands), eyes[1].replace(using: commands)]
        if let blit = commands.makeBlitCommandEncoder() {
            blit.copy(from: left, to: targets[0])
            blit.copy(from: right, to: targets[1])
            blit.endEncoding()
        }
        commands.addCompletedHandler { _ in wwhd_screen_release(slot) }
        commands.commit()
    }
}

private struct ScreenView: View {
    @State private var screen = Screen()
    @State private var updates: EventSubscription?

    var body: some View {
        GeometryReader3D { geometry in
            RealityView { content in
                await screen.make()
                content.add(screen.plane)
                content.add(screen.speakers)
                updates = content.subscribe(to: SceneEvents.Update.self) { _ in screen.update() }
            } update: { content in
                // the plane fills the window, 1 cm in front of its surface: measured in the simulator, the surface is
                // half the converted frame's depth below its min z (at the frame's centre the plane came out larger
                // than the window and covered its bar and ornament)
                let frame = content.convert(geometry.frame(in: .local), from: .local, to: .scene)
                let surface: SIMD3<Float> = [frame.center.x, frame.center.y, frame.min.z - frame.extents.z / 2]
                screen.plane.scale = [frame.extents.x, frame.extents.y, 1]
                screen.plane.position = surface + [0, 0, 0.01]
                screen.speakers.position = surface
            }
        }
        .onDisappear {
            updates?.cancel()
            wwhd_screen_attach(0)
            wwhd_game_window_closed()
        }
    }
}

// The game window's buttons: the GamePad's window
struct ScreenButtons: View {
    var body: some View {
        VStack {
            Button("GamePad", systemImage: "gamecontroller") { wwhd_gamepad_open() }
        }
        .padding(12)
        .glassBackgroundEffect()
    }
}

// host_tvos.mm: the window's screen, a view to lay over the game's own (it lets touches through)
@_cdecl("wwhd_screen_view")
public func wwhd_screen_view() -> UnsafeMutableRawPointer {
    let host = UIHostingController(rootView: ScreenView())
    host.view.backgroundColor = .clear
    host.view.isUserInteractionEnabled = false
    return Unmanaged.passRetained(host).toOpaque()
}
