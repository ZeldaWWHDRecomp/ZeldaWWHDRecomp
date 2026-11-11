// The Apple TV host (tvOS, WWHD_TVOS): UIKit instead of the AppKit host. One full-screen view whose layer is
// the TV picture's CAMetalLayer (display.mm takes it through host_tvos_layer); the game's own start-up
// (main.cpp's wwhd_main) runs on a thread of its own, as UIKit owns the main thread and its run loop.
// Controllers come through GameController.framework (input.mm), as on the Mac. The Mac's windows, menus,
// dialogs, controls window, text input and mouse have no equivalent here: their entry points below do
// nothing or decline.
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <csignal>
#include <mach/mach.h>
#include <os/proc.h>
#include <pthread.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "../input.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../platform/keycodes.h"
#include "display_modes.h"
#include "renderer.h"
#include "../interp.h"
#include "remote_video.h"
#include "../mods/save_folder.h"
#include "../mods/mods.h"
#include "../remote_pad/remote_pad.h"
#include "../audio_out.h"
#include "../platform/host.h"
#include <filesystem>
#include <thread>
#include <cmath>

int wwhd_main(int argc, char** argv);  // main.cpp
void log_msg(const char* fmt, ...);

#if TARGET_OS_TV
@interface WWMetalView : UIView
@end
#else
@interface WWMetalView : UIView <UIKeyInput>
@end
#endif
@implementation WWMetalView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (void)layoutSubviews {
    [super layoutSubviews];
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    const CGFloat scale = self.window.screen.nativeScale ?: UIScreen.mainScreen.nativeScale;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(MAX(1.0, self.bounds.size.width * scale), MAX(1.0, self.bounds.size.height * scale));
}
#if !TARGET_OS_TV
// The touch screen (iPhone, iPad): one finger at a time. While Settings or the text prompt is up it is the
// overlay's pointer; otherwise a finger on the GamePad picture (in One screen mode, the whole picture) touches
// the GamePad's screen, and a drag that leaves the picture keeps touching its edge.
static UITouch* g_finger = nil;
static bool g_finger_overlay = false;
- (bool)point:(UITouch*)t x:(float*)nx y:(float*)ny {
    const CGPoint p = [t locationInView:self];
    const CGSize sz = self.bounds.size;
    if (sz.width <= 0 || sz.height <= 0) return false;
    *nx = float(p.x / sz.width);
    *ny = float(p.y / sz.height);
    return true;
}
- (void)finger:(UITouch*)t phase:(int)phase {  // 0 down, 1 moved, 2 up
    float nx, ny, x, y;
    if (![self point:t x:&nx y:&ny]) return;
    if (phase == 0) g_finger_overlay = overlay::captures();
    if (g_finger_overlay) {
        overlay::mouse_move(nx, ny, true);
        if (phase != 1) overlay::mouse_button(0, phase == 0, true);
        return;
    }
    const bool on = gfx::overlay_hit(nx, ny, &x, &y, phase != 0);
    if (phase == 0 && !on) {
        mods::startup_title_tap();  // (on the title screen only: A, as "press A" asks)
        return;
    }
    input::set_touch(phase != 2, on ? x : 0, on ? y : 0);
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e {
    if (g_finger) return;
    g_finger = touches.anyObject;
    [self finger:g_finger phase:0];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e {
    if (g_finger && [touches containsObject:g_finger]) [self finger:g_finger phase:1];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e {
    if (!g_finger || ![touches containsObject:g_finger]) return;
    [self finger:g_finger phase:2];
    g_finger = nil;
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e { [self touchesEnded:touches withEvent:e]; }
// The system keyboard while the overlay wants text (a Settings field, the game's text prompt): the view takes
// the keyboard then (follow_text_input, polled), typed text goes to the overlay, Return and Delete as keys.
- (BOOL)canBecomeFirstResponder { return overlay::wants_text(); }
- (BOOL)hasText { return YES; }  // (Delete is always passed on)
- (UITextAutocorrectionType)autocorrectionType { return UITextAutocorrectionTypeNo; }
- (UITextAutocapitalizationType)autocapitalizationType { return UITextAutocapitalizationTypeNone; }
- (UITextSpellCheckingType)spellCheckingType { return UITextSpellCheckingTypeNo; }
- (void)key:(int)code {
    overlay::key(code, true, false, 0);
    overlay::key(code, false, false, 0);
}
- (void)insertText:(NSString*)text {
    if ([text isEqualToString:@"\n"]) {
        [self key:kVK_Return];
        return;
    }
    overlay::text(text.UTF8String);
}
- (void)deleteBackward { [self key:kVK_Delete]; }
- (void)followTextInput {
    const bool want = overlay::wants_text();
    if (want && !self.isFirstResponder) [self becomeFirstResponder];
    else if (!want && self.isFirstResponder) [self resignFirstResponder];
}
#endif
@end

@interface WWViewController : UIViewController
@end
@implementation WWViewController
- (void)loadView {
    self.view = [WWMetalView new];
    self.view.backgroundColor = UIColor.blackColor;
}
// the controller's Menu button goes to the game (its Home / Settings), not back to the Home screen
- (BOOL)controllerUserInteractionEnabled { return NO; }
@end

static WWMetalView* g_view = nil;
static int g_argc = 0;
static char** g_argv = nullptr;

// Where things are: the game files inside the app (game/, packaged with it), everything the game writes in
// the app's Caches folder (an Apple TV app may write nowhere else that lasts): settings and caches (the
// user folder), saves, and the log (relative to the working directory).
// The live shader cache packaged with the app (shaders.bin: every shader and pipeline the game is known to use,
// recorded where the game was prepared) goes where the renderer keeps it (the user folder's live shader caches,
// shader_prebuild.h bind_live_caches), when the
// copy there is missing or smaller: the first start, or after tvOS emptied the Caches folder. Its records are
// the game's state, valid on any GPU; with the packaged shader library none of them compiles here.
static void install_shader_cache(NSString* caches) {
    NSString* packaged = [[NSBundle mainBundle] pathForResource:@"shaders" ofType:@"bin"];
    if (!packaged) return;
    NSString* dir = [caches stringByAppendingPathComponent:@"user/shader-prebuild/live"];
    NSString* live = [dir stringByAppendingPathComponent:@"shaders.bin"];
    NSFileManager* fm = [NSFileManager defaultManager];
    const unsigned long long have = [[fm attributesOfItemAtPath:live error:nil] fileSize];
    const unsigned long long ship = [[fm attributesOfItemAtPath:packaged error:nil] fileSize];
    if (have >= ship) return;
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    [fm removeItemAtPath:live error:nil];
    NSError* err = nil;
    if ([fm copyItemAtPath:packaged toPath:live error:&err]) log_msg("[tvos] shader cache installed from the app (%llu MB)", ship >> 20);
    else log_msg("[tvos] shader cache not installed: %s", err.localizedDescription.UTF8String);
}

// Heat guard: the Apple TV 4K (A15) has no fan, and under the game's load it reaches tvOS's "serious" thermal
// state, where the system slows the chip until the game runs at a third of its speed (30 -> 10 fps, measured).
// The game lowers its own load instead, in two steps:
//   "serious":  60 fps pauses (30), the occlusion depth goes to half size, the internal resolution to 0.75x;
//   "critical": as well, the resolution goes to 0.5x and the iPhone GamePad gets every other picture.
// After a minute below a step's state the guard goes back one step; off, the player's settings are back as they
// were. Nothing it changes is saved. (Android pauses only 60 fps: platform/heat_guard.h.)
struct HeatGuard {
    int step = 0;  // 0 off, 1 serious, 2 critical
    double calmer_since = -1;
    int interp_mode = 0;
    bool ao_hires = false;
    float res = 1.0f;

    void apply(int to) {
        if (step == 0 && to > 0) interp_mode = interp::mode(), ao_hires = render::ao_hires(), res = render::res_scale();
        step = to;
        if (to == 0) {
            interp::set_mode(interp_mode);
            render::set_ao_hires(ao_hires);
            render::set_res_scale(res);
            remote_video::set_frame_divisor(1);
            log_msg("[tvos] heat guard: cooled down: the settings are back");
            return;
        }
        if (interp::mode()) interp::set_mode(0);
        render::set_ao_hires(false);
        render::set_res_scale(std::min(res, to == 2 ? 0.5f : 0.75f));
        remote_video::set_frame_divisor(to == 2 ? 2 : 1);
        log_msg(to == 2 ? "[tvos] heat guard: the Apple TV is very hot: 0.5x resolution, the iPhone GamePad at half rate, "
                          "30 fps and half-size occlusion depth until it cools"
                        : "[tvos] heat guard: the Apple TV is hot: 30 fps, half-size occlusion depth, 0.75x resolution until it cools");
    }
    // main thread, every 10 s
    void tick(NSInteger thermal, double now) {
        const int want = thermal >= NSProcessInfoThermalStateCritical ? 2 : thermal >= NSProcessInfoThermalStateSerious ? 1 : 0;
        if (want >= step) calmer_since = -1;
        else if (calmer_since < 0) calmer_since = now;
        if (want > step) apply(want);
        else if (want < step && now - calmer_since >= 60) {
            apply(step - 1);
            calmer_since = want < step ? now : -1;  // a further step back after another minute
        }
    }
};

// Memory: what tvOS counts against the app (its physical footprint) and what it still allows, and the thermal
// state, every 10 s
// (an Apple TV app has a fixed limit, about 2 GB on an A15, and is ended when it reaches it)
static void start_memory_log() {
    static dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), 10 * NSEC_PER_SEC, NSEC_PER_SEC);
    dispatch_source_set_event_handler(timer, ^{
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return;
        static uint64_t peak = 0;
        peak = std::max<uint64_t>(peak, info.phys_footprint);
        // and how hot the Apple TV is: tvOS slows the chip down from "serious" (the A15 Apple TV has no fan)
        static const char* const kThermal[] = {"nominal", "fair", "serious", "critical"};
        const NSInteger t = [NSProcessInfo processInfo].thermalState;
        log_msg("[tvos] memory: footprint %llu MB (peak %llu MB), %zu MB more allowed; thermal state %s",
                (unsigned long long)(info.phys_footprint >> 20), (unsigned long long)(peak >> 20), os_proc_available_memory() >> 20,
                t >= 0 && t < 4 ? kThermal[t] : "?");
        // (the renderer's settings change on the main thread, as the settings overlay changes them)
        static HeatGuard guard;
        const double now = CACurrentMediaTime();
        if (render::g_backend) dispatch_async(dispatch_get_main_queue(), ^{ guard.tick(t, now); });
    });
    dispatch_resume(timer);
}

// Code mods on an Apple TV come built into the app (tvos/README.md, tvos/bundle_mods.py): GuestMods holds each
// module built for tvOS (guest_mods.cpp bundled_module), ModManager the mod manager's storage the Mac prepared
// (the packages, on and trusted). A new app with another seed (its seed.txt) replaces the profile and those
// packages; the player's choices made on the TV last until then.
static void install_bundled_mods(NSString* user) {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* app = [NSBundle mainBundle].bundlePath;
    NSString* modules = [app stringByAppendingPathComponent:@"GuestMods"];
    if ([fm fileExistsAtPath:modules]) setenv("WWHD_BUNDLED_GUEST_MODS", modules.fileSystemRepresentation, 1);
    NSString* seed = [app stringByAppendingPathComponent:@"ModManager"];
    NSString* stamp = [NSString stringWithContentsOfFile:[seed stringByAppendingPathComponent:@"seed.txt"]
                                                encoding:NSUTF8StringEncoding error:nil];
    if (!stamp.length) return;
    NSString* storage = [user stringByAppendingPathComponent:@"ModManager"];
    NSString* copied = [storage stringByAppendingPathComponent:@"seed.txt"];
    if ([stamp isEqualToString:[NSString stringWithContentsOfFile:copied encoding:NSUTF8StringEncoding error:nil]])
        return;
    NSString* mods = [storage stringByAppendingPathComponent:@"Mods"];
    [fm createDirectoryAtPath:mods withIntermediateDirectories:YES attributes:nil error:nil];
    NSError* error = nil;
    for (NSString* id in [fm contentsOfDirectoryAtPath:[seed stringByAppendingPathComponent:@"Mods"] error:nil]) {
        if ([id hasPrefix:@"."] || [id containsString:@"/"]) continue;
        NSString* to = [mods stringByAppendingPathComponent:id];
        [fm removeItemAtPath:to error:nil];
        if (![fm copyItemAtPath:[[seed stringByAppendingPathComponent:@"Mods"] stringByAppendingPathComponent:id] toPath:to error:&error])
            NSLog(@"[mods] bundled %@ not installed: %@", id, error);
    }
    NSString* profiles = [storage stringByAppendingPathComponent:@"profiles.json"];
    [fm removeItemAtPath:profiles error:nil];
    [fm copyItemAtPath:[seed stringByAppendingPathComponent:@"profiles.json"] toPath:profiles error:nil];
    [stamp writeToFile:copied atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSLog(@"[mods] the app's mods installed (seed %@)", stamp);
}

#if defined(WWHD_ICLOUD_CONTAINER) && !TARGET_OS_TV
// iPad: the save in the app's iCloud Drive folder (WWHD_ICLOUD_CONTAINER, a build setting), "Wind Waker HD" in
// it, so it is the same on every device signed in to the same iCloud (a Mac chooses that folder in Settings >
// Saves). Through the save folder choice (mods/save_folder.h): the first time, the save here is copied there
// (or, if one is there already, the player picks which to keep); every start checks the folder before the game
// reads it. Files iCloud has not put on this iPad yet are downloaded first. Signed out of iCloud: the save stays
// in the Caches folder, as without iCloud.
static void use_icloud_save_folder(NSString* save) {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSURL* container = [fm URLForUbiquityContainerIdentifier:@WWHD_ICLOUD_CONTAINER];  // (blocks: not the main thread)
    if (!container) {
        log_msg("[icloud] iCloud Drive is off or signed out: the save stays on this device");
        return;
    }
    NSURL* folder = [[container URLByAppendingPathComponent:@"Documents"] URLByAppendingPathComponent:@"Wind Waker HD"];
    [fm createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:nil];
    // files only in iCloud so far show as ".<name>.icloud": ask for them, and wait (at most 30 s)
    auto pending = [&] {
        NSMutableArray<NSURL*>* missing = [NSMutableArray array];
        NSDirectoryEnumerator* e = [fm enumeratorAtURL:folder includingPropertiesForKeys:nil options:0 errorHandler:nil];
        for (NSURL* u in e) {
            NSString* name = u.lastPathComponent;
            if ([name hasPrefix:@"."] && [name hasSuffix:@".icloud"])
                [missing addObject:[[u URLByDeletingLastPathComponent]
                                       URLByAppendingPathComponent:[name substringWithRange:NSMakeRange(1, name.length - 8)]]];
        }
        return missing;
    };
    NSArray<NSURL*>* missing = pending();
    if (missing.count) {
        log_msg("[icloud] %lu save files not on this device yet: downloading", (unsigned long)missing.count);
        for (NSURL* u in missing) [fm startDownloadingUbiquitousItemAtURL:u error:nil];
        for (int i = 0; i < 300 && pending().count; i++) usleep(100000);
        if (NSUInteger left = pending().count) log_msg("[icloud] %lu save files still downloading", (unsigned long)left);
    }
    const auto file = save_folder::choice_file(host::config_dir());
    const std::string chosen = folder.path.fileSystemRepresentation;
    const auto current = save_folder::read_choice(file);
    if (current && current->folder == chosen) return;
    save_folder::Choice c;
    c.folder = chosen;
    c.copy_from = std::filesystem::absolute(save.fileSystemRepresentation).string();
    if (save_folder::write_choice(file, c)) log_msg("[icloud] the save moves to iCloud Drive (%s)", chosen.c_str());
}
#endif

// The audio session (audio_out.cpp, at its start): playback, so the game is heard with an iPhone's silent switch on.
// Surround 5.1 (Settings > Audio > Speakers) says the app plays multichannel content and asks for six channels:
// AirPods with spatial audio and a 5.1 receiver on an Apple TV take them. -> the route's output channels.
int uikit_audio_session(bool surround) {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* err = nil;
    if (![session setCategory:AVAudioSessionCategoryPlayback error:&err])
        log_msg("[audio] session category: %s", err.localizedDescription.UTF8String ?: "?");
    if (surround) {
        // declared while the session is inactive: the route's channels are worked out when it is activated
        [session setActive:NO error:nil];
        if (@available(iOS 15.0, tvOS 15.0, *)) [session setSupportsMultichannelContent:YES error:nil];
    }
    [session setActive:YES error:nil];
    if (surround && session.maximumOutputNumberOfChannels >= 6) [session setPreferredOutputNumberOfChannels:6 error:nil];
    AVAudioSessionPortDescription* out = session.currentRoute.outputs.firstObject;
    bool spatial = false;
    if (@available(iOS 15.0, tvOS 15.0, *)) spatial = out.spatialAudioEnabled;
    log_msg("[audio] output %s (%s): %ld of at most %ld channels%s", out.portName.UTF8String ?: "?", out.portType.UTF8String ?: "?",
            (long)session.outputNumberOfChannels, (long)session.maximumOutputNumberOfChannels, spatial ? ", spatial audio" : "");
    return int(session.outputNumberOfChannels);
}

// Surround 5.1 where the route takes two channels (AirPods, the device's speakers): the six go to the system as
// multichannel content (AVSampleBufferAudioRenderer, allowed to spatialize it), so the system's Spatial Audio
// renders them, with the listener's personalized profile and head tracking. A thread queues the game's mix
// (audio_out.cpp, L R C LFE Ls Rs) in 5 ms buffers. The renderer's clock is the time being heard, so a buffer has to
// reach it the route's output latency ahead (AirPods: about 160 ms), plus a margin (10 ms; WWHD_SPATIAL_MARGIN_MS),
// or it is dropped as late; it is kept no further ahead than that.
// The system pauses the renderer (the app in the background, an interruption): the feed then starts again with a
// new renderer and clock, from fresh audio, as soon as the audio session can be activated (it can't until the app is
// active again; tried every second). WWHD_AUDIO_TRACE=1 logs the feed every 2 s.
// The renderer suits a route of two channels (AirPods, headphones), where the system spatializes the six. Elsewhere
// a RemoteIO unit plays the mix instead: on a route of six channels or more (HDMI to a 5.1 receiver; the renderer
// plays its content there very quietly, spatialized for the TV) the six as they are, in the session's channel
// order; on the device's own speaker (the renderer plays nothing there, though it says it renders) folded to stereo.
// The game's stereo mix is its 5.1 front pair, 1.9 dB louder (measured: the start recorded both ways), its surrounds
// 5.1's own; they are folded in at -3 dB, the centre too. The feed follows the route as it changes (headphones in or
// out, AirPods connecting).
enum Route : int { kRenderer, kFolded, kDiscrete };
static std::atomic<int> g_route{kRenderer};
static void (*g_pull)(int16_t*, uint32_t) = nullptr;
static AudioUnit g_units[3] = {};  // [kFolded], [kDiscrete]
void uikit_audio_route_check() {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    audio::set_output_latency_ms(float((session.outputLatency + session.IOBufferDuration) * 1000));
    if (session.maximumOutputNumberOfChannels >= 6) {
        [session setPreferredOutputNumberOfChannels:6 error:nil];
        g_route = kDiscrete;
    } else if ([session.currentRoute.outputs.firstObject.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]) {
        g_route = kFolded;
    } else {
        g_route = kRenderer;
    }
}
static OSStatus folded_render(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                              AudioBufferList* io) {
    static int16_t six[4096 * 6];
    int16_t* out = (int16_t*)io->mBuffers[0].mData;
    for (UInt32 done = 0; done < frames;) {
        const UInt32 n = std::min<UInt32>(frames - done, 4096);
        g_pull(six, n);
        for (UInt32 i = 0; i < n; i++) {
            const int16_t* f = six + i * 6;  // L R C LFE Ls Rs
            const float c = 0.71f * f[2];
            out[(done + i) * 2] = int16_t(std::clamp(1.25f * (f[0] + c + 0.71f * f[4]), -32768.0f, 32767.0f));
            out[(done + i) * 2 + 1] = int16_t(std::clamp(1.25f * (f[1] + c + 0.71f * f[5]), -32768.0f, 32767.0f));
        }
        done += n;
    }
    return noErr;
}
static OSStatus discrete_render(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                                AudioBufferList* io) {
    g_pull((int16_t*)io->mBuffers[0].mData, frames);
    return noErr;
}
// start (on) or stop the unit of a route
static bool route_unit(int route, bool on) {
    if (route == kRenderer) return true;
    AudioUnit& unit = g_units[route];
    if (!unit) {
        AudioComponentDescription d{kAudioUnitType_Output, kAudioUnitSubType_RemoteIO, kAudioUnitManufacturer_Apple, 0, 0};
        AudioComponent comp = AudioComponentFindNext(nullptr, &d);
        if (!comp || AudioComponentInstanceNew(comp, &unit) != noErr) return false;
        const UInt32 ch = route == kDiscrete ? 6 : 2;
        AudioStreamBasicDescription f{};
        f.mSampleRate = 48000;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        f.mChannelsPerFrame = ch;
        f.mBitsPerChannel = 16;
        f.mFramesPerPacket = 1;
        f.mBytesPerFrame = f.mBytesPerPacket = 2 * ch;
        AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        AURenderCallbackStruct cb{route == kDiscrete ? discrete_render : folded_render, nullptr};
        AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        if (AudioUnitInitialize(unit) != noErr) return false;
    }
    if (on) {
        [[AVAudioSession sharedInstance] setPreferredIOBufferDuration:0.005 error:nil];
        audio::set_host_target_ms(20);  // (a unit takes 5 ms or more at a time)
        return AudioOutputUnitStart(unit) == noErr;
    }
    AudioOutputUnitStop(unit);
    audio::set_host_target_ms(8);
    return true;
}
bool uikit_spatial_start(void (*pull)(int16_t* interleaved, uint32_t frames)) {
    if (@available(iOS 15.0, tvOS 15.0, *)) {
    } else {
        return false;
    }
    g_pull = pull;
    uikit_audio_route_check();
    [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification*) { uikit_audio_route_check(); }];
    static AVSampleBufferAudioRenderer* renderer = nil;
    static AVSampleBufferRenderSynchronizer* clock = nil;
    static auto fresh = [] {
        if (clock && renderer) [clock removeRenderer:renderer atTime:kCMTimeZero completionHandler:nil];
        renderer = [AVSampleBufferAudioRenderer new];
        renderer.allowedAudioSpatializationFormats = AVAudioSpatializationFormatMultichannel | AVAudioSpatializationFormatMonoAndStereo;
        clock = [AVSampleBufferRenderSynchronizer new];
        [clock addRenderer:renderer];
    };
    fresh();
    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = 48000;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mChannelsPerFrame = 6;
    asbd.mBitsPerChannel = 16;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = asbd.mBytesPerPacket = 12;
    // the two named 5.1 layouts, the same channel order: L R C LFE, then side surrounds (Ls Rs) or rear (Rls Rrs)
    static CMAudioFormatDescriptionRef formats[2] = {};
    const AudioChannelLayoutTag tags[2] = {kAudioChannelLayoutTag_WAVE_5_1_A, kAudioChannelLayoutTag_WAVE_5_1_B};
    for (int i = 0; i < 2; i++) {
        AudioChannelLayout layout{};
        layout.mChannelLayoutTag = tags[i];
        if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, sizeof layout, &layout, 0, nullptr, nullptr, &formats[i]) != noErr) {
            log_msg("[audio] spatial 5.1: no format description");
            return false;
        }
    }
    static auto feed = pull;
    std::thread([] {
        constexpr uint32_t kChunk = 240;  // 5 ms
        static const double margin = getenv("WWHD_SPATIAL_MARGIN_MS") ? atof(getenv("WWHD_SPATIAL_MARGIN_MS")) : 10;
        static const bool trace = getenv("WWHD_AUDIO_TRACE") != nullptr;
        static int16_t mix[kChunk * 6];
        double energy[6] = {};  // (the trace: each channel's level since the last line)
        uint64_t measured = 0;
        double leadMs = [AVAudioSession sharedInstance].outputLatency * 1000 + margin;
        int64_t queued = 0;  // frames
        bool started = false, rear = audio::rear_surrounds();
        int route = kRenderer;
        double lastReport = 0, clockSeen = -1, clockSince = 0;
        for (;;) {
            // (the latency settles after a route change: again every second)
            static double latencyAt = 0;
            if (CACurrentMediaTime() - latencyAt > 1) {
                latencyAt = CACurrentMediaTime();
                AVAudioSession* session = [AVAudioSession sharedInstance];
                audio::set_output_latency_ms(float((session.outputLatency + session.IOBufferDuration) * 1000 +
                                                   (route == kRenderer ? margin : 0)));
            }
            // the route: a unit pulls the mix (the six as they are, or folded to stereo), or this thread feeds the renderer
            if (g_route != route) {
                const int to = g_route;
                if (route == kRenderer) {
                    [clock setRate:0 time:kCMTimeZero];
                    [renderer flush];
                    started = false;
                    queued = 0;
                }
                route_unit(route, false);
                const bool ok = route_unit(to, true);
                if (to == kRenderer) fresh();
                route = to;
                AVAudioSessionPortDescription* out = [AVAudioSession sharedInstance].currentRoute.outputs.firstObject;
                log_msg("[audio] 5.1 now %s to %s%s", to == kDiscrete ? "as six channels" : to == kFolded ? "folded to stereo"
                        : "as multichannel content, for the system's Spatial Audio", out.portName.UTF8String ?: "the output",
                        ok ? "" : " (no output unit)");
            }
            if (route != kRenderer) {
                usleep(20000);
                continue;
            }
            // paused by the system, fallen behind its clock, or a clock that should run but stands still (a restart
            // before the app was active again): start again from fresh audio
            const double clockNow = started ? CMTimeGetSeconds(clock.currentTime) : -1;
            if (clockNow != clockSeen) { clockSeen = clockNow; clockSince = CACurrentMediaTime(); }
            const bool behind = started && queued / 48000.0 < clockNow;
            const bool stuck = started && clock.rate != 0 && CACurrentMediaTime() - clockSince > 1;
            // a new surround placement (Settings): from fresh audio in the other layout
            const bool placed = audio::rear_surrounds() != rear;
            if (placed) rear = !rear;
            if (started && (clock.rate == 0 || behind || stuck || placed)) {
                NSError* activation = nil;
                if (![[AVAudioSession sharedInstance] setActive:YES error:&activation])
                    log_msg("[audio] spatial feed: the audio session can't be activated yet: %s",
                            activation.localizedDescription.UTF8String ?: "?");
                clockSince = CACurrentMediaTime();
                [clock setRate:0 time:kCMTimeZero];
                [renderer flush];
                fresh();
                queued = 0;
                started = false;
                log_msg("[audio] spatial feed: started again (%s)", placed ? (rear ? "rear surrounds" : "side surrounds")
                        : behind ? "it fell behind" : stuck ? "its clock stood still" : "the renderer was paused");
            }
            if (CACurrentMediaTime() - lastReport > 2) {
                lastReport = CACurrentMediaTime();
                leadMs = [AVAudioSession sharedInstance].outputLatency * 1000 + margin;  // (the route may change)
                if (trace) {
                    uint64_t underrun = 0, dropped = 0;
                    audio::stats(underrun, dropped);
                    double db[6];
                    for (int ch = 0; ch < 6; ch++)
                        db[ch] = measured ? 10 * std::log10(energy[ch] / measured / (32768.0 * 32768.0) + 1e-12) : -120;
                    log_msg("[audio] spatial feed: lead %.0f ms, underrun %llu dropped %llu frames, clock %.2f s, queued %.2f s; "
                            "dBFS L %.0f R %.0f C %.0f LFE %.0f Ls %.0f Rs %.0f; renderer status %ld%s%s, volume %.2f%s, "
                            "session output volume %.2f",
                            leadMs, (unsigned long long)underrun, (unsigned long long)dropped, std::max(0.0, clockNow),
                            queued / 48000.0, db[0], db[1], db[2], db[3], db[4], db[5], (long)renderer.status,
                            renderer.error ? " " : "", renderer.error ? renderer.error.localizedDescription.UTF8String : "",
                            renderer.volume, renderer.muted ? " muted" : "", [AVAudioSession sharedInstance].outputVolume);
                    std::fill(energy, energy + 6, 0.0);
                    measured = 0;
                }
            }
            const double lead = queued / 48000.0 - std::max(0.0, clockNow);
            if (lead * 1000 >= leadMs || !renderer.isReadyForMoreMediaData) {
                usleep(1000);
                continue;
            }
            feed(mix, kChunk);
            if (trace) {
                for (uint32_t i = 0; i < kChunk; i++)
                    for (int ch = 0; ch < 6; ch++) energy[ch] += double(mix[i * 6 + ch]) * mix[i * 6 + ch];
                measured += kChunk;
            }
            CMBlockBufferRef block = nullptr;
            if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, sizeof mix, kCFAllocatorDefault, nullptr, 0,
                                                   sizeof mix, kCMBlockBufferAssureMemoryNowFlag, &block) != noErr)
                continue;
            CMBlockBufferReplaceDataBytes(mix, block, 0, sizeof mix);
            CMSampleBufferRef sample = nullptr;
            CMAudioSampleBufferCreateReadyWithPacketDescriptions(kCFAllocatorDefault, block, formats[rear], kChunk,
                                                                 CMTimeMake(queued, 48000), nullptr, &sample);
            CFRelease(block);
            if (!sample) continue;
            [renderer enqueueSampleBuffer:sample];
            CFRelease(sample);
            queued += kChunk;
            if (!started && queued >= 2 * kChunk) {
                [clock setRate:1 time:kCMTimeZero];
                started = true;
            }
        }
    }).detach();
    AVAudioSession* session = [AVAudioSession sharedInstance];
    AVAudioSessionPortDescription* out = session.currentRoute.outputs.firstObject;
    log_msg("[audio] 5.1 as multichannel content to %s, for the system's Spatial Audio (output latency %.0f ms)",
            out.portName.UTF8String ?: "the output", session.outputLatency * 1000);
    return true;
}

static void* run_game(void*) {
    NSString* caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    install_shader_cache(caches);
    start_memory_log();
    NSString* user = [caches stringByAppendingPathComponent:@"user"];
    NSString* save = [caches stringByAppendingPathComponent:@"save"];
    for (NSString* d in @[ user, save ])
        [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    chdir(caches.fileSystemRepresentation);
    setenv("WWHD_USER_DIR", user.fileSystemRepresentation, 0);
    install_bundled_mods(user);
    // a name of this device for the save folder's lock: every iPad and iPhone's host name is "localhost". The
    // model and part of the id this app has on this device (the owner's own device name needs an entitlement).
    {
        NSString* id = [UIDevice currentDevice].identifierForVendor.UUIDString ?: @"";
        NSString* name = [NSString stringWithFormat:@"%@ (%@)", [UIDevice currentDevice].model,
                                                    id.length >= 4 ? [id substringToIndex:4] : @"?"];
        setenv("WWHD_MACHINE_NAME", name.UTF8String, 0);
    }
#if defined(WWHD_ICLOUD_CONTAINER) && !TARGET_OS_TV
    use_icloud_save_folder(save);
#endif
    const std::string game = std::string([NSBundle mainBundle].bundlePath.fileSystemRepresentation) + "/game";
    std::vector<std::string> args = {g_argc ? g_argv[0] : "WindWakerHD", "--game", game, "--save", save.fileSystemRepresentation};
    for (int i = 1; i < g_argc; i++) args.push_back(g_argv[i]);  // (devicectl launch arguments come after)
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    wwhd_main(int(args.size()), argv.data());
    return nullptr;
}

@interface WWSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow* window;
@end
@implementation WWSceneDelegate
// suspended in the background, the game can't keep its save folder's lock fresh: it gives it back meanwhile
// (mods/save_folder.h pause_lock)
- (void)sceneDidEnterBackground:(UIScene*)scene { save_folder::pause_lock(); }
- (void)sceneWillEnterForeground:(UIScene*)scene {
    save_folder::resume_lock();
    uikit_audio_route_check();  // (the output may have changed meanwhile)
    // the iPhone GamePad: the system took the server's socket and its Bonjour advertisement in the background
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ remote_pad::restart(); });
}
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    UIWindowScene* ws = (UIWindowScene*)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:ws];
    WWViewController* vc = [WWViewController new];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    g_view = (WWMetalView*)vc.view;
    [g_view layoutIfNeeded];
#if !TARGET_OS_TV
    [NSTimer scheduledTimerWithTimeInterval:0.15 repeats:YES block:^(NSTimer*) { [g_view followTextInput]; }];
#endif
    static bool started = false;
    if (started) return;
    started = true;
    // the game's start-up and its guest thread want deep stacks, as the Mac's main thread has (8 MB)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    pthread_t t;
    pthread_create(&t, &attr, run_game, nullptr);
    pthread_attr_destroy(&attr);
}
@end

@interface WWAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation WWAppDelegate
- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* c = [[UISceneConfiguration alloc] initWithName:nil sessionRole:session.role];
    c.delegateClass = [WWSceneDelegate class];
    return c;
}
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    application.idleTimerDisabled = YES;  // a game: no screen saver while it is played with a controller
    return YES;
}
@end

namespace gfx {
// display.mm: the TV picture's layer (main thread; the view exists once the scene connected)
CAMetalLayer* host_tvos_layer() { return g_view ? (CAMetalLayer*)g_view.layer : nil; }
bool host_setting(const char* key, std::string& value);
void set_host_setting(const char* key, const std::string& value);

// The graphics options the Mac's menus save (menu.mm, not built here), in the same settings file display.mm keeps
// and under the same names. The occlusion depth starts at half size: the A15's GPU is the limit, as a phone's.
static bool saved_number(const char* key, double& v) {
    std::string s;
    if (!host_setting(key, s) || s.empty()) return false;
    char* end = nullptr;
    v = strtod(s.c_str(), &end);
    return end && end != s.c_str();
}
void host_tvos_load_graphics() {
    double v;
    if (!getenv("WWHD_RES_SCALE") && saved_number("resScale", v) && v > 0) render::set_res_scale(float(v));
    if (!getenv("WWHD_AO_HIRES")) render::set_ao_hires(saved_number("aoHires", v) && v != 0);  // (half size unless chosen)
    if (saved_number("fxaa", v)) render::set_fxaa(v != 0);
    if (saved_number("aniso", v)) render::set_aniso(v != 0);
    if (saved_number("cloudShadows", v)) render::set_cloud_shadows(v != 0);
    if (saved_number("interpFps", v)) interp::set_fps(int(v));
    if (saved_number("fps60", v)) interp::set_mode(int(v));
}
static void save_graphics() {
    set_host_setting("resScale", std::to_string(render::res_scale()));
    set_host_setting("aoHires", render::ao_hires() ? "1" : "0");
    set_host_setting("fxaa", render::fxaa() ? "1" : "0");
    set_host_setting("aniso", render::aniso() ? "1" : "0");
    set_host_setting("cloudShadows", render::cloud_shadows() ? "1" : "0");
    set_host_setting("interpFps", std::to_string(interp::fps()));
    set_host_setting("fps60", std::to_string(interp::mode()));
}

// display.mm
int display_filter();
void display_set_filter(int f);
int display_drc_mode();
void set_drc_mode(int m);
void display_set_pip(int corner, float size, float opacity);
bool drc_window_available();
bool drc_window_shown();
void show_drc_window(bool on);

// player_question.h: a question before the game runs (the save folder's choices, mods/save_folder.h) as a system
// alert on the main thread; the game thread waits for the answer. -1: no way to ask (called on the main thread,
// no window yet).
int ask_player_alert(const std::string& title, const std::string& text, const std::vector<std::string>& buttons) {
    if ([NSThread isMainThread] || buttons.empty()) return -1;
    __block int answer = -1;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSString* t = [NSString stringWithUTF8String:title.c_str()];
    NSString* m = [NSString stringWithUTF8String:text.c_str()];
    std::vector<std::string> labels = buttons;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* top = nil;
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass:UIWindowScene.class])
                for (UIWindow* w in ((UIWindowScene*)scene).windows)
                    if (w.isKeyWindow) top = w.rootViewController;
        while (top.presentedViewController) top = top.presentedViewController;
        if (!top) { dispatch_semaphore_signal(done); return; }
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:t message:m
                                                                 preferredStyle:UIAlertControllerStyleAlert];
        for (size_t i = 0; i < labels.size(); i++)
            [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithUTF8String:labels[i].c_str()]
                                                      style:UIAlertActionStyleDefault
                                                    handler:^(UIAlertAction*) { answer = int(i); dispatch_semaphore_signal(done); }]];
        [top presentViewController:alert animated:YES completion:nil];
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    return answer;
}
}  // namespace gfx

// mods/mouse.mm: no mouse on an Apple TV
namespace mods {
bool mouse_captured() { return false; }
void mouse_release() {}
void update_gyro_mouse() {}
}  // namespace mods

// main.cpp's crash handler asks the Vulkan renderer's compile journal (not built here)
extern "C" void wwhd_compile_journal_on_crash() {}

// ---------------------------------------------------------------- the settings overlay's host (hostui.h)
// As the AppKit host's (overlay_appkit.mm) for what display.mm keeps; no file panels, folders or clipboard.
namespace hostui {
void post(std::function<void()> fn) {
    dispatch_async(dispatch_get_main_queue(), ^{ fn(); });
}
void choose_game_source(bool, std::function<void(std::string)>) {}
void choose_mod_source(bool, std::function<void(std::string)>) {}
void choose_save_folder(std::function<void(std::string)>) {}
void choose_file(const char*, const char*, bool, std::function<void(std::string)>) {}
bool get(const char* key, std::string& value) { return gfx::host_setting(key, value); }
void set(const char* key, const std::string& value) { gfx::set_host_setting(key, value); }
float res_scale() { return render::res_scale(); }
void set_res_scale(float s) {
    render::set_res_scale(s);
    gfx::set_host_setting("resScale", std::to_string(s));
}
void graphics_changed() { gfx::save_graphics(); }  // (an option of Settings > Graphics changed)
int scale_filter() { return gfx::display_filter(); }
void set_scale_filter(int f) { gfx::display_set_filter(f); }
bool scale_filter_available() { return true; }
bool fullscreen() { return true; }
void set_fullscreen(bool) {}
int drc_modes() { return gfx::kDrcModeCount; }
bool drc_mode_offered(int m) { return m >= 0 && m < gfx::kDrcModeCount && m != gfx::kDrcWindow; }  // (no second window)
int drc_mode() { return gfx::display_drc_mode(); }
void set_drc_mode(int m) { gfx::set_drc_mode(m); }
void save_pip_position() { gfx::display_set_pip(gfx::g_pip_anchor, gfx::g_pip_size, gfx::g_pip_opacity); }
int pip_anchor() { return gfx::g_pip_anchor; }
void set_pip_anchor(int c) { gfx::display_set_pip(c, gfx::g_pip_size, gfx::g_pip_opacity); }
float pip_size() { return gfx::g_pip_size; }
void set_pip_size(float s) { gfx::display_set_pip(gfx::g_pip_anchor, s, gfx::g_pip_opacity); }
float pip_opacity() { return gfx::g_pip_opacity; }
void set_pip_opacity(float o) { gfx::display_set_pip(gfx::g_pip_anchor, gfx::g_pip_size, o); }
bool drc_available() { return gfx::drc_window_available(); }
bool drc_shown() { return gfx::drc_window_shown(); }
void show_drc(bool on) { gfx::show_drc_window(on); }
void set_pro_controller(bool on) {
    input::set_pro_controller(on);
    gfx::show_drc_window(!on);
}
const char* name() { return "tvOS"; }
void set_clipboard(const std::string&) {}
bool can_open_folder() { return false; }
void open_folder(const std::string&) {}
// Settings > Quit Game: an Apple TV app does not quit itself; back to the Home screen keeps it as it is
void quit() {}
}  // namespace hostui

int main(int argc, char** argv) {
    // a closed pipe must not end the game: the console of a Mac that started it (xcrun devicectl
    // --console) goes when that Mac lets go of the Apple TV, and the next log line would raise SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    g_argc = argc;
    g_argv = argv;
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([WWAppDelegate class]));
    }
}
