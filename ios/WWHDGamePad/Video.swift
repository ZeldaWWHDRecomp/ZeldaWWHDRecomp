// The GamePad screen from the game (runtime/src/gfx/remote_video.mm): frames arrive in pieces
// (Protocol.swift VideoPiece), are put back together, and go to an AVSampleBufferDisplayLayer, which
// decodes them in hardware and shows each at once.
//
// Loss: a frame missing a piece is dropped, and so is every frame after it until a keyframe (they refer
// to the lost one); the phone asks the game for that keyframe, at most every 250 ms.
import AVFoundation
import CoreMedia
import SwiftUI
import UIKit

/// Puts frames back together from their pieces (tested in VideoTests).
struct FrameAssembler {
    struct Frame: Equatable {
        var id: UInt32
        var key: Bool
        var width: Int, height: Int
        var data: [UInt8]
    }

    private var current: UInt32?
    private var pieces: [ArraySlice<UInt8>?] = []
    private var have = 0
    private var key = false
    private var size = (0, 0)
    private var lastDone: UInt32?
    /// a frame was lost: nothing is shown until the next keyframe
    private(set) var broken = true

    /// A piece; returns the frame it completes (if it is shown) and whether to ask for a keyframe.
    mutating func add(_ p: WWGP.VideoPiece) -> (frame: Frame?, needKey: Bool) {
        var needKey = false
        if let c = current, p.frame != c {
            if Int32(bitPattern: p.frame &- c) < 0 { return (nil, false) }  // an older frame's late piece
            broken = true                                                   // the current one stays incomplete
            needKey = true
            current = nil
        }
        if current == nil {
            if let done = lastDone, Int32(bitPattern: p.frame &- done) <= 0 { return (nil, false) }  // already shown
            if let done = lastDone, p.frame != done &+ 1 { broken = true; needKey = true }  // whole frames missed
            current = p.frame
            pieces = Array(repeating: nil, count: p.count)
            have = 0
            key = p.key
            size = (p.width, p.height)
        }
        guard p.count == pieces.count, pieces[p.index] == nil else { return (nil, needKey) }
        pieces[p.index] = p.payload
        have += 1
        guard have == pieces.count else { return (nil, needKey) }
        lastDone = p.frame
        current = nil
        if broken && !key { return (nil, true) }
        broken = false
        var data: [UInt8] = []
        data.reserveCapacity(pieces.reduce(0) { $0 + ($1?.count ?? 0) })
        for piece in pieces { data.append(contentsOf: piece!) }
        pieces = []
        return (Frame(id: p.frame, key: key, width: size.0, height: size.1, data: data), needKey)
    }
}

/// H.264 frames (AVCC, SPS and PPS ahead of a keyframe) to sample buffers for the display layer.
struct SampleMaker {
    private(set) var format: CMVideoFormatDescription?

    /// NAL units with their 4-byte lengths
    static func nals(_ d: [UInt8]) -> [ArraySlice<UInt8>]? {
        var out: [ArraySlice<UInt8>] = []
        var at = 0
        while at + 4 <= d.count {
            let n = Int(d[at]) << 24 | Int(d[at + 1]) << 16 | Int(d[at + 2]) << 8 | Int(d[at + 3])
            at += 4
            guard n > 0, n <= d.count - at else { return nil }
            out.append(d[at..<at + n])
            at += n
        }
        return at == d.count ? out : nil
    }

    mutating func sample(_ f: FrameAssembler.Frame) -> CMSampleBuffer? {
        guard let units = Self.nals(f.data) else { return nil }
        var params: [[UInt8]] = []
        var payload: [UInt8] = []
        for u in units {
            switch u.first! & 0x1F {
            case 7, 8: params.append(Array(u))   // SPS, PPS
            default:
                let n = UInt32(u.count)
                payload += [UInt8(n >> 24), UInt8(n >> 16 & 0xFF), UInt8(n >> 8 & 0xFF), UInt8(n & 0xFF)]
                payload += u
            }
        }
        if params.count >= 2 {
            var desc: CMFormatDescription?
            let status = params[0].withUnsafeBufferPointer { sps in
                params[1].withUnsafeBufferPointer { pps -> OSStatus in
                    let ptrs = [sps.baseAddress!, pps.baseAddress!]
                    let sizes = [sps.count, pps.count]
                    return CMVideoFormatDescriptionCreateFromH264ParameterSets(
                        allocator: nil, parameterSetCount: 2, parameterSetPointers: ptrs,
                        parameterSetSizes: sizes, nalUnitHeaderLength: 4, formatDescriptionOut: &desc)
                }
            }
            if status == noErr { format = desc }
        }
        guard let format, !payload.isEmpty else { return nil }
        var block: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(allocator: nil, memoryBlock: nil, blockLength: payload.count,
                                                 blockAllocator: nil, customBlockSource: nil, offsetToData: 0,
                                                 dataLength: payload.count, flags: 0, blockBufferOut: &block) == noErr,
              let block,
              payload.withUnsafeBytes({ CMBlockBufferReplaceDataBytes(with: $0.baseAddress!, blockBuffer: block,
                                                                      offsetIntoDestination: 0, dataLength: payload.count) }) == noErr
        else { return nil }
        var sample: CMSampleBuffer?
        var size = payload.count
        guard CMSampleBufferCreateReady(allocator: nil, dataBuffer: block, formatDescription: format, sampleCount: 1,
                                        sampleTimingEntryCount: 0, sampleTimingArray: nil, sampleSizeEntryCount: 1,
                                        sampleSizeArray: &size, sampleBufferOut: &sample) == noErr, let sample
        else { return nil }
        // shown as soon as it is decoded: no clock, no buffering
        if let att = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: true), CFArrayGetCount(att) > 0 {
            let dict = unsafeBitCast(CFArrayGetValueAtIndex(att, 0), to: CFMutableDictionary.self)
            CFDictionarySetValue(dict, Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                                 Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }
        return sample
    }
}

/// The view the GamePad screen is shown in.
final class VideoLayerView: UIView {
    override class var layerClass: AnyClass { AVSampleBufferDisplayLayer.self }
    var displayLayer: AVSampleBufferDisplayLayer { layer as! AVSampleBufferDisplayLayer }

    override init(frame: CGRect) {
        super.init(frame: frame)
        displayLayer.videoGravity = .resize  // the view already has the picture's shape
        backgroundColor = .black
        isUserInteractionEnabled = false
    }
    required init?(coder: NSCoder) { fatalError() }
}

struct VideoView: UIViewRepresentable {
    let video: VideoReceiver
    func makeUIView(context: Context) -> VideoLayerView {
        let v = VideoLayerView()
        video.attach(v.displayLayer)
        return v
    }
    func updateUIView(_ v: VideoLayerView, context: Context) {}
}

/// Takes the pieces on the link queue and feeds the display layer.
final class VideoReceiver: ObservableObject, @unchecked Sendable {
    /// main thread: a picture has been shown since the last reset
    @Published private(set) var showing = false
    private var assembler = FrameAssembler()    // link queue
    private var maker = SampleMaker()           // link queue
    private var lastKeyRequest = Date.distantPast
    private var shown = false                   // link queue: `showing` as last sent to the main thread
    private let layerLock = NSLock()
    private var layer: AVSampleBufferDisplayLayer?
    var requestKeyframe: (() -> Void)?          // link queue

    func attach(_ l: AVSampleBufferDisplayLayer) {
        layerLock.withLock { layer = l }
    }

    /// link queue: a new session; the picture starts again at a keyframe
    func reset() {
        assembler = FrameAssembler()
        maker = SampleMaker()
        lastKeyRequest = .distantPast
        shown = false
        layerLock.withLock { layer }?.sampleBufferRenderer.flush(removingDisplayedImage: true, completionHandler: nil)
        DispatchQueue.main.async { self.showing = false }
    }

    /// link queue
    func add(_ piece: WWGP.VideoPiece) {
        let (frame, needKey) = assembler.add(piece)
        if needKey { askForKeyframe() }
        guard let frame, let sample = maker.sample(frame), let l = layerLock.withLock({ layer }) else { return }
        let renderer = l.sampleBufferRenderer
        if renderer.status == .failed {
            renderer.flush()
            askForKeyframe()
            return
        }
        renderer.enqueue(sample)
        if !shown {
            shown = true
            DispatchQueue.main.async { self.showing = true }
        }
    }

    private func askForKeyframe() {
        let now = Date()
        guard now.timeIntervalSince(lastKeyRequest) >= 0.25 else { return }
        lastKeyRequest = now
        requestKeyframe?()
    }
}
