// Display: the TV and GamePad windows, full screen, and the final composition of the TV and GamePad
// images onto the screen (scale to fit with letterbox/pillarbox, scaling filter, GamePad screen as a
// separate window or as a picture-in-picture overlay inside the TV window).
//
// Everything here only decides how the finished scan-out images (R.tv.tex, R.drc.tex, any size and
// aspect) are put on the screen; what the game renders is decided elsewhere.
//
// Both renderers use these windows (renderer.h): Metal draws the composition below; Vulkan
// (gfx/vulkan, MoltenVK) gets each view's CAMetalLayer for a VK_EXT_metal_surface swapchain and asks
// display_plan() for the same layout (picture rectangles, GamePad overlay, scaling filter).
//
// Shortcuts: Cmd+F (or Ctrl+Cmd+F, or the green button) full screen; Cmd+G show/hide the GamePad screen.
// The Display menu holds the rest. Choices are kept in ~/Library/Application Support/wwhd/display.plist
// (test runs with WWHD_NO_HOST_INPUT neither read nor write it unless WWHD_DISPLAY_SETTINGS names a file).
//
// The modes, the layout of the TV window, the automatic overlay and the overlay's touch mapping are
// shared with the SDL host (display_modes.cpp, which lists their test variables: WWHD_DRC_MODE,
// WWHD_DRC_PIP, WWHD_SCALE_FILTER, WWHD_SIM_SCREEN, WWHD_TEST_TOUCH, WWHD_DRC_AUTO, WWHD_DRC_AUTO_LOG).
//
// Debug / test environment:
//   WWHD_FULLSCREEN=0|1                  TV window starts windowed / in full screen instead of as it was left
//                                        (that session's full screen is not saved; 1 takes over the screen!)
//   WWHD_DUMP_PRESENT=1                  with WWHD_DUMP_FRAMES: also write frame_<n>_present.png (the composed
//                                        TV window) and frame_<n>_present_drc.png (GamePad window, window mode)
//   WWHD_TEST_DRC_KEY=3500,3700          frames at which Cmd+G (show/hide GamePad screen) is simulated
//   WWHD_HIDDEN_WINDOWS=1                test runs: the windows are never put on screen (nothing pops up;
//                                        frame / present dumps still work, the drawables are not presented)
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "../aspect.h"
#include "display.h"
#include "display_modes.h"
#include "gx2/gx2.h"
#include "input.h"
#include "metal.h"
#include "renderer.h"
#include "runtime.h"
#ifdef WWHD_HAS_VULKAN
#include "vulkan/api.h"
#endif
#include "imgui.h"
#include "backends/imgui_impl_metal.h"
#include "../overlay/overlay.h"

namespace mods { bool mouse_captured(); }

namespace gfx {
extern Renderer R;
void install_menu(NSWindow* tv);  // menu.mm
bool fxaa_enabled();
void dump_texture(id<MTLTexture> src, const char* name, bool async, bool srgbEncode);

// ---------------------------------------------------------------- options
// (the option state itself: display_modes.cpp)
static int find_name(const char* const* names, int n, NSString* s, int def) {
    return find_name(names, n, [s isKindOfClass:[NSString class]] ? s.UTF8String : nullptr, def);
}
static bool hidden_windows() {
    static const bool h = [] { const char* e = getenv("WWHD_HIDDEN_WINDOWS"); return e && *e && strcmp(e, "0"); }();
    return h;
}

// ---------------------------------------------------------------- settings file
static NSMutableDictionary* g_settings;
static NSString* settings_path() {
    if (const char* e = getenv("WWHD_DISPLAY_SETTINGS")) return @(e);
    if (getenv("WWHD_NO_HOST_INPUT")) return nil;  // test runs leave the user's choices alone
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/wwhd/display.plist"];
}
static void load_settings() {
    NSString* p = settings_path();
    g_settings = p ? [[NSDictionary dictionaryWithContentsOfFile:p] mutableCopy] : nil;
    if (!g_settings) g_settings = [NSMutableDictionary new];
}
static void save_settings() {
    NSString* p = settings_path();
    if (!p) return;
    [[NSFileManager defaultManager] createDirectoryAtPath:[p stringByDeletingLastPathComponent]
                              withIntermediateDirectories:YES attributes:nil error:nil];
    [g_settings writeToFile:p atomically:YES];
}
static void set_setting(NSString* key, id value) {
    if (!g_settings) load_settings();
    if (value) g_settings[key] = value;
    else [g_settings removeObjectForKey:key];
    save_settings();
}
// renderer.cpp: the renderer choice lives in the same file (read before the windows exist)
bool host_setting(const char* key, std::string& value) {
    if (!g_settings) load_settings();
    id v = g_settings[@(key)];
    if (![v isKindOfClass:[NSString class]]) return false;
    value = ((NSString*)v).UTF8String;
    return true;
}
void set_host_setting(const char* key, const std::string& value) { set_setting(@(key), @(value.c_str())); }
static void save_options() {
    g_settings[@"drcMode"] = @(kModeNames[g_mode]);
    g_settings[@"pipCorner"] = @(kCornerNames[g_corner]);
    g_settings[@"pipSize"] = @(g_pip_size.load());
    g_settings[@"pipOpacity"] = @(g_pip_opacity.load());
    g_settings[@"scaleFilter"] = @(kFilterNames[g_filter]);
    save_settings();
}
static void load_options() {
    g_mode = find_name(kModeNames, kDrcModeCount, g_settings[@"drcMode"], kDrcWindow);
    if (!drc_mode_offered(g_mode)) g_mode = kDrcWindow;
    g_corner = find_name(kCornerNames, 4, g_settings[@"pipCorner"], 3);
    g_filter = find_name(kFilterNames, 3, g_settings[@"scaleFilter"], kSmooth);
    if (NSNumber* n = g_settings[@"pipSize"]) g_pip_size = std::clamp(n.floatValue, 0.1f, 0.5f);
    if (NSNumber* n = g_settings[@"pipOpacity"]) g_pip_opacity = std::clamp(n.floatValue, 0.2f, 1.0f);
    display_env_overrides();  // start-up overrides for tests (not saved)
}

}  // namespace gfx

// ---------------------------------------------------------------- views
// GamePad window: the mouse stands in for the touch panel
@interface WWDrcView : NSView
@end
@implementation WWDrcView
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (void)touch:(NSEvent*)e down:(bool)down {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    NSSize sz = self.bounds.size;
    // the image is letterboxed to its aspect inside the view
    float a = gfx::g_drc_aspect;
    float w = sz.width, h = sz.height, x0 = 0, y0 = 0;
    if (w / h > a) { x0 = (w - h * a) / 2; w = h * a; } else { y0 = (h - w / a) / 2; h = w / a; }
    float x = (p.x - x0) / w, y = 1.0f - (p.y - y0) / h;
    input::set_touch(down, std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f));
}
- (void)mouseDown:(NSEvent*)e { [self touch:e down:true]; }
- (void)mouseDragged:(NSEvent*)e { [self touch:e down:true]; }
- (void)mouseUp:(NSEvent*)e { [self touch:e down:false]; }
@end

// TV window: clicks on the GamePad overlay are touches
@interface WWTvView : NSView
@property bool touching;
@end
@implementation WWTvView
- (bool)map:(NSEvent*)e x:(float*)tx y:(float*)ty clamp:(bool)c {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    NSSize sz = self.bounds.size;
    if (sz.width <= 0 || sz.height <= 0) return false;
    return gfx::overlay_hit(p.x / sz.width, 1.0f - p.y / sz.height, tx, ty, c);
}
- (BOOL)acceptsFirstMouse:(NSEvent*)e {
    float x, y;
    return [self map:e x:&x y:&y clamp:false];
}
- (void)mouseDown:(NSEvent*)e {
    float x, y;
    if ([self map:e x:&x y:&y clamp:false]) {
        self.touching = true;
        input::set_touch(true, x, y);
        gfx::display_touched();
    } else {
        [super mouseDown:e];
    }
}
- (void)mouseDragged:(NSEvent*)e {
    float x, y;
    if (self.touching && [self map:e x:&x y:&y clamp:true]) input::set_touch(true, x, y);
}
- (void)mouseUp:(NSEvent*)e {
    float x, y;
    if (!self.touching) return;
    self.touching = false;
    if (![self map:e x:&x y:&y clamp:true]) x = y = 0;
    input::set_touch(false, x, y);
    gfx::display_touched();
}
@end

namespace gfx {

// ---------------------------------------------------------------- windows
static NSWindow* g_tv_window = nil;
static NSWindow* g_drc_window = nil;
static NSRect g_tv_normal_frame, g_drc_normal_frame;  // frames outside full screen, for saving

static bool is_fullscreen(NSWindow* w) { return w && (w.styleMask & NSWindowStyleMaskFullScreen); }
bool tv_fullscreen() { return is_fullscreen(g_tv_window); }

// a saved frame is used only if it still fits a connected screen well enough
static bool frame_usable(NSRect f) {
    if (f.size.width < 200 || f.size.height < 120) return false;
    for (NSScreen* s in NSScreen.screens) {
        NSRect v = s.visibleFrame, i = NSIntersectionRect(v, f);
        if (i.size.width * i.size.height >= 0.5 * f.size.width * f.size.height && f.size.width <= v.size.width + 1 &&
            f.size.height <= v.size.height + 1 && NSMaxY(f) <= NSMaxY(v) + 1)  // title bar reachable
            return true;
    }
    return false;
}

static NSScreen* screen_named(NSString* name) {
    if (!name) return nil;
    for (NSScreen* s in NSScreen.screens)
        if ([s.localizedName isEqualToString:name]) return s;
    return nil;
}

static void update_drawable_size(CAMetalLayer* layer, NSWindow* win) {
    if (!layer) return;
    NSView* view = win.contentView;
    layer.contentsScale = win.backingScaleFactor;  // Retina: present at the display's pixel density
    NSSize sz = view.bounds.size;
    layer.drawableSize = CGSizeMake(std::max(1.0, sz.width * layer.contentsScale), std::max(1.0, sz.height * layer.contentsScale));
}

// the layer each window's view presents to (index 0 TV, 1 GamePad); made by the renderer that starts
static CAMetalLayer* g_layers[2];
static NSWindow* g_windows[2];
static bool screen_visible(NSWindow* win) { return (win.occlusionState & NSWindowOcclusionStateVisible) != 0; }
// a window was resized, moved to another screen or (un)covered: tell the renderer
static void screen_changed(int i) {
    NSWindow* win = g_windows[i];
    if (!win || !g_layers[i]) return;
    update_drawable_size(g_layers[i], win);
    if (render::active() == render::Api::Metal) {
        Screen& scr = i ? R.drc : R.tv;
        scr.visible = screen_visible(win);
    }
#ifdef WWHD_HAS_VULKAN
    else gfxvk::screen_changed(i, screen_visible(win));
#endif
}

static NSWindow* make_window(int index, NSString* title, NSView* view, int w, int h, NSPoint origin) {
    NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(origin.x, origin.y, w, h)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [win setTitle:title];
    win.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;  // green button, Ctrl+Cmd+F
    win.backgroundColor = NSColor.blackColor;
    if (view) [win setContentView:view];
    view = [win contentView];
    [view setWantsLayer:YES];
    g_windows[index] = win;
    for (NSNotificationName n in @[ NSWindowDidChangeOcclusionStateNotification, NSWindowDidResizeNotification,
                                    NSWindowDidChangeBackingPropertiesNotification, NSWindowDidChangeScreenNotification ])
        [[NSNotificationCenter defaultCenter] addObserverForName:n object:win queue:nil usingBlock:^(NSNotification*) {
            screen_changed(index);
        }];
    return win;
}

// a fresh CAMetalLayer on a window's view (the renderer's presentation target)
static CAMetalLayer* attach_layer(int i) {
    NSWindow* win = g_windows[i];
    if (!win) return nil;
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    [win.contentView setLayer:layer];
    g_layers[i] = layer;
    update_drawable_size(layer, win);
    return layer;
}

// Metal: the layers present the composition below (present_screens)
static void attach_metal_layers() {
    for (int i = 0; i < 2; i++) {
        Screen& scr = i ? R.drc : R.tv;
        CAMetalLayer* layer = attach_layer(i);
        scr.layer = layer;
        if (!layer) continue;
        layer.device = R.device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.maximumDrawableCount = 3;
        if (hidden_windows()) scr.visible = false;  // never on screen: no drawables
    }
}

// remember frames (outside full screen) and full-screen state (fsKey nil: not remembered)
static void track_window(NSWindow* win, NSRect* normal, NSString* frameKey, NSString* fsKey) {
    *normal = win.frame;
    auto save_frame = ^(NSNotification*) {
        if (is_fullscreen(win) || win.inLiveResize) return;
        *normal = win.frame;
        set_setting(frameKey, NSStringFromRect(*normal));
    };
    for (NSNotificationName n in @[ NSWindowDidMoveNotification, NSWindowDidEndLiveResizeNotification ])
        [[NSNotificationCenter defaultCenter] addObserverForName:n object:win queue:nil usingBlock:save_frame];
    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillEnterFullScreenNotification object:win queue:nil
                                                  usingBlock:^(NSNotification*) {
        set_setting(frameKey, NSStringFromRect(*normal));
        if (fsKey) set_setting(fsKey, @YES);
    }];
    if (fsKey)
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidExitFullScreenNotification object:win queue:nil
                                                      usingBlock:^(NSNotification*) { set_setting(fsKey, @NO); }];
}

// show or hide the GamePad window to match the mode (main thread)
static void apply_drc_window() {
    if (!g_drc_window) return;
    if (drc_window_wanted()) {
        // never takes the keyboard; test runs stay behind the user's windows
        if (hidden_windows()) return;
        if (!g_drc_window.visible) {
            if (getenv("WWHD_NO_HOST_INPUT")) [g_drc_window orderBack:nil];
            else [g_drc_window orderFront:nil];
        }
    } else if (g_drc_window.visible) {
        if (is_fullscreen(g_drc_window)) [g_drc_window toggleFullScreen:nil];  // hidden after the transition
        else [g_drc_window orderOut:nil];
    }
}

// GamePad window, shown/hidden from the Input menu (the game keeps rendering its image either way)
bool drc_window_available() { return g_drc_window != nil || g_mode == kDrcPip || g_mode == kDrcAuto; }
bool drc_window_shown() { return drc_screen_shown(g_drc_window.visible); }
// Show / hide the GamePad screen in the current mode (Cmd+G, Input menu, Pro Controller choice)
void show_drc_window(bool on) {
    auto apply = [on] {
        display_show_drc(on);
        apply_drc_window();
        LOG("[display] GamePad screen %s (%s)", on ? "shown" : "hidden", kModeNames[g_mode]);
    };
    if ([NSThread isMainThread]) apply();
    else dispatch_async(dispatch_get_main_queue(), ^{ apply(); });
}
void toggle_drc_screen() { show_drc_window(!drc_window_shown()); }

void set_drc_mode(int m) {
    display_set_mode(m);
    apply_drc_window();
    save_options();
    LOG("[display] GamePad screen mode: %s", kModeNames[m]);
}

static void move_drc_to_screen(NSScreen* s) {
    if (!g_drc_window || !s) return;
    if (is_fullscreen(g_drc_window)) return;  // leave full screen first
    NSRect v = s.visibleFrame, f = g_drc_window.frame;
    f.size.width = std::min(f.size.width, v.size.width);
    f.size.height = std::min(f.size.height, v.size.height);
    f.origin = NSMakePoint(NSMidX(v) - f.size.width / 2, NSMidY(v) - f.size.height / 2);
    [g_drc_window setFrame:f display:YES];
    set_setting(@"drcScreen", s.localizedName);
}

static void create_windows() {
    static bool done = false;  // once, whichever renderer starts (a failed Vulkan start falls back to Metal)
    if (done) return;
    done = true;
    [NSApplication sharedApplication];
    // a game: no App Nap / timer coalescing, even when the window is in the background
    static id activity = [[NSProcessInfo processInfo]
        beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical | NSActivityIdleDisplaySleepDisabled
                          reason:@"game running"];
    (void)activity;
    // scripted test runs (WWHD_NO_HOST_INPUT) run as a background app: no Dock icon, never takes the
    // keyboard focus from the user's game
    bool test = getenv("WWHD_NO_HOST_INPUT") != nullptr;
    [NSApp setActivationPolicy:test ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
    load_settings();
    load_options();
    NSWindow* tv = make_window(0, @"The Legend of Zelda: The Wind Waker HD (recompiled)",
                               [[WWTvView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 720)], 1280, 720, NSMakePoint(0, 0));
    g_tv_window = tv;
    install_menu(tv);
    NSRect saved = NSRectFromString(g_settings[@"tvFrame"] ?: @"");
    if (frame_usable(saved)) [tv setFrame:saved display:NO];
    else [tv center];
    // full screen as left (a WWHD_FULLSCREEN start leaves the saved state alone)
    track_window(tv, &g_tv_normal_frame, @"tvFrame", display_fullscreen_env() ? nil : @"tvFullScreen");
    if (!getenv("WWHD_NO_GAMEPAD")) {
        // GamePad screen to the right of the TV window (or where it was last)
        NSRect f = tv.frame;
        NSWindow* drc = make_window(1, @"GamePad", [[WWDrcView alloc] initWithFrame:NSMakeRect(0, 0, 427, 240)], 427, 240,
                                    NSMakePoint(NSMaxX(f) + 8, NSMinY(f)));
        g_drc_window = drc;
        g_has_drc_window = true;
        drc.releasedWhenClosed = NO;  // closing only hides it; the Input / Display menu can bring it back
        NSRect ds = NSRectFromString(g_settings[@"drcFrame"] ?: @"");
        NSScreen* want = screen_named(g_settings[@"drcScreen"]);
        if (frame_usable(ds)) [drc setFrame:ds display:NO];
        else if (want) move_drc_to_screen(want);
        track_window(drc, &g_drc_normal_frame, @"drcFrame", @"drcFullScreen");
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification object:drc queue:nil
                                                      usingBlock:^(NSNotification*) { if (g_mode == kDrcWindow) g_shown = false; }];
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidExitFullScreenNotification object:drc queue:nil
                                                      usingBlock:^(NSNotification*) {
            dispatch_async(dispatch_get_main_queue(), ^{ apply_drc_window(); });  // left full screen to hide
        }];
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidMoveNotification object:drc queue:nil
                                                      usingBlock:^(NSNotification*) {
            if (drc.screen && !is_fullscreen(drc)) g_settings[@"drcScreen"] = drc.screen.localizedName;
        }];
    }
    g_shown = !input::pro_controller();  // Pro Controller: GamePad screen starts hidden
    apply_drc_window();
    if (hidden_windows()) {
        // test run: never on screen
    } else if (test) {
        [tv orderBack:nil];  // scripted test run: stay behind, don't take focus
    } else {
        [tv makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
    // full screen as last time (display_modes.cpp: never in test runs unless asked for, never with hidden windows)
    if (display_start_fullscreen([g_settings[@"tvFullScreen"] boolValue], hidden_windows()))
        dispatch_async(dispatch_get_main_queue(), ^{ if (!is_fullscreen(tv)) [tv toggleFullScreen:nil]; });
    if (!test && g_drc_window && drc_window_wanted() && [g_settings[@"drcFullScreen"] boolValue] &&
        screen_named(g_settings[@"drcScreen"]) && screen_named(g_settings[@"drcScreen"]) != tv.screen)
        dispatch_async(dispatch_get_main_queue(), ^{ if (!is_fullscreen(g_drc_window)) [g_drc_window toggleFullScreen:nil]; });

    // full screen: hide the pointer after 2 s without movement (not while the mouse camera holds it)
    [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer*) {
        static NSPoint last = {-1, -1};
        static double moved = 0;
        static bool hidden = false;
        NSPoint p = [NSEvent mouseLocation];
        if (!NSEqualPoints(p, last)) { last = p; moved = display_now(); hidden = false; }
        if (!hidden && is_fullscreen(g_tv_window) && NSApp.active && g_tv_window.keyWindow && !mods::mouse_captured() &&
            display_now() - moved > 2.0) {
            [NSCursor setHiddenUntilMouseMoves:YES];
            hidden = true;
        }
    }];
    // debug: WWHD_TEST_DRC_MODE (display_modes.cpp) switches the mode as the Display menu does
    if (getenv("WWHD_TEST_DRC_MODE"))
        [NSTimer scheduledTimerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer*) {
            const int m = display_test_mode(render::frame_count());
            if (m >= 0) set_drc_mode(m);
        }];
    // debug: simulated Cmd+G presses
    if (const char* e = getenv("WWHD_TEST_DRC_KEY")) {
        static std::vector<uint64_t> frames;
        for (const char* p = e; *p;) {
            frames.push_back(strtoull(p, (char**)&p, 10));
            while (*p == ',') p++;
        }
        [NSTimer scheduledTimerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer*) {
            static size_t i = 0;
            // the active renderer's frames (gfx::frame_count is the Metal renderer's: 0 with Vulkan)
            if (i < frames.size() && render::frame_count() >= frames[i]) {
                i++;
                LOG("[display] test: Cmd+G at frame %llu", (unsigned long long)render::frame_count());
                toggle_drc_screen();
            }
        }];
    }
}

// ---------------------------------------------------------------- presentation shader
static const char* kPresentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; float2 uv; };
vertex VOut present_vs(uint vid [[vertex_id]], constant float4& rect [[buffer(0)]]) {
    float2 p = float2((vid << 1) & 2, vid & 2);          // fullscreen triangle strip corners
    VOut o;
    o.uv = p * 0.5;                                       // 0..1 (flipped below)
    float2 ndc = rect.xy + p * 0.5 * rect.zw;
    o.pos = float4(ndc.x * 2.0 - 1.0, 1.0 - ndc.y * 2.0, 0.0, 1.0);
    return o;
}
// optional edge smoothing (FXAA, the classic console variant), evaluated on the source picture's
// texel grid while scaling it to the window
static float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
static float3 fxaa(texture2d<float> t, sampler s, float2 uv) {
    float2 rcp = 1.0 / float2(t.get_width(), t.get_height());
    float3 nw = t.sample(s, uv + float2(-1, -1) * rcp).rgb, ne = t.sample(s, uv + float2(1, -1) * rcp).rgb;
    float3 sw = t.sample(s, uv + float2(-1, 1) * rcp).rgb, se = t.sample(s, uv + float2(1, 1) * rcp).rgb;
    float3 m = t.sample(s, uv).rgb;
    float lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se), lm = luma(m);
    float lmin = min(lm, min(min(lnw, lne), min(lsw, lse))), lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));
    if (lmax - lmin < max(0.0312, lmax * 0.125)) return m;  // no edge here
    float2 dir = float2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float reduce = max((lnw + lne + lsw + lse) * (0.25 / 8.0), 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * rcp;
    float3 a = 0.5 * (t.sample(s, uv + dir * (1.0 / 3.0 - 0.5)).rgb + t.sample(s, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 b = a * 0.5 + 0.25 * (t.sample(s, uv - dir * 0.5).rgb + t.sample(s, uv + dir * 0.5).rgb);
    float lb = luma(b);
    return (lb < lmin || lb > lmax) ? a : b;
}
struct Params {
    int aa;          // FXAA
    int conv;        // 1: source is display-encoded, target expects linear; 2: the other way round
    float sharp;     // > 1: sharp bilinear (the blend band between texels shrinks by this factor)
    float foot;      // source texels per target pixel (> 1: 4-tap box filter when shrinking)
};
static float3 to_linear(float3 c) { return select(pow((c + 0.055) / 1.055, 2.4), c / 12.92, c <= 0.04045); }
static float3 to_srgb(float3 c) { return select(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, c <= 0.0031308); }
fragment float4 present_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]],
                           constant Params& p [[buffer(0)]]) {
    float2 size = float2(tex.get_width(), tex.get_height());
    float2 uv = in.uv;
    if (p.sharp > 1.0) {
        float2 t = uv * size - 0.5, i = floor(t), f = t - i;
        f = clamp((f - 0.5) * p.sharp + 0.5, 0.0, 1.0);
        uv = (i + f + 0.5) / size;
    }
    float3 c;
    if (p.aa) {
        c = fxaa(tex, s, uv);
    } else if (p.foot > 1.25) {
        float2 d = 0.25 * min(p.foot, 4.0) / size;
        c = 0.25 * (tex.sample(s, uv + float2(-d.x, -d.y)).rgb + tex.sample(s, uv + float2(d.x, -d.y)).rgb +
                    tex.sample(s, uv + float2(-d.x, d.y)).rgb + tex.sample(s, uv + float2(d.x, d.y)).rgb);
    } else {
        c = tex.sample(s, uv).rgb;
    }
    if (p.conv == 1) c = to_linear(saturate(c));
    else if (p.conv == 2) c = to_srgb(saturate(c));
    return float4(c, 1.0);
}
fragment float4 solid_fs(VOut in [[stage_in]], constant float4& color [[buffer(0)]]) { return color; }
)";

static id<MTLLibrary> g_lib;
// pipelines by (target format, solid colour, blended)
static id<MTLRenderPipelineState> pipeline(MTLPixelFormat fmt, bool solid, bool blend) {
    static std::mutex mu;
    static std::map<std::tuple<int, bool, bool>, id<MTLRenderPipelineState>> cache;
    std::lock_guard<std::mutex> lk(mu);
    auto key = std::make_tuple((int)fmt, solid, blend);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [g_lib newFunctionWithName:@"present_vs"];
    d.fragmentFunction = [g_lib newFunctionWithName:solid ? @"solid_fs" : @"present_fs"];
    d.colorAttachments[0].pixelFormat = fmt;
    if (blend) {
        // overlay opacity: the encoder's blend colour alpha (solid: the colour's alpha)
        auto* a = d.colorAttachments[0];
        a.blendingEnabled = YES;
        a.sourceRGBBlendFactor = a.sourceAlphaBlendFactor = solid ? MTLBlendFactorSourceAlpha : MTLBlendFactorBlendAlpha;
        a.destinationRGBBlendFactor = a.destinationAlphaBlendFactor =
            solid ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOneMinusBlendAlpha;
    }
    NSError* err = nil;
    id<MTLRenderPipelineState> p = [R.device newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p) fatal("present pipeline: %s", err.localizedDescription.UTF8String);
    cache[key] = p;
    return p;
}

static void create_present_pipeline() {
    NSError* err = nil;
    g_lib = [R.device newLibraryWithSource:[NSString stringWithUTF8String:kPresentShader] options:nil error:&err];
    if (!g_lib) fatal("present shader: %s", err.localizedDescription.UTF8String);
    R.presentPipeline = pipeline(MTLPixelFormatBGRA8Unorm, false, false);
    R.presentPipelineSRGB = pipeline(MTLPixelFormatBGRA8Unorm_sRGB, false, false);
    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    R.linearClamp = [R.device newSamplerStateWithDescriptor:sd];
}

// keyboard events and controller polling, after AppKit/window setup (once)
void display_start_input() {
    static bool done = false;
    if (done) return;
    done = true;
    input::init();
}

// Metal renderer start-up (metal_main.mm)
void display_init() {
    create_windows();
    attach_metal_layers();
    display_start_input();
    create_present_pipeline();
}

// Vulkan renderer start-up (gfx/vulkan/backend_table.cpp): windows, then a layer per view for its
// VK_EXT_metal_surface; display_detach() takes them away again if Vulkan cannot start
void display_create_windows() { create_windows(); }
void display_attach_vulkan(void** tv, void** drc) {
    *tv = (__bridge void*)attach_layer(0);
    *drc = (__bridge void*)attach_layer(1);
}
// after the Vulkan renderer started: windows that are never shown get no presentation
void display_vulkan_started() {
#ifdef WWHD_HAS_VULKAN
    if (hidden_windows())
        for (int i = 0; i < 2; i++) gfxvk::screen_changed(i, false);
#endif
}
void display_detach() {
    for (int i = 0; i < 2; i++) {
        if (g_windows[i]) [g_windows[i].contentView setLayer:nil];
        g_layers[i] = nil;
    }
}
CAMetalLayer* display_layer(int i) { return g_layers[i]; }

// the main thread's event loop, for either renderer
void run_appkit_loop() {
    // orderly exit (Quit, Cmd+Q): the renderer writes its caches (Vulkan pipeline / SPIR-V caches)
    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification*) {
        render::shutdown();
        // leave without running static destructors: the GX2 render thread and the game threads may
        // still be using them (exit() here crashed in the renderer's caches, Metal and Vulkan alike)
        fflush(stdout);
        fflush(stderr);
        std::_Exit(0);
    }];
    // test runs: WWHD_EXIT_AT_FRAME=n quits (orderly) once the renderer reached frame n
    if (const char* e = getenv("WWHD_EXIT_AT_FRAME")) {
        uint64_t at = strtoull(e, nullptr, 10);
        if (at)
            [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer* t) {
                if (render::frame_count() < at) return;
                [t invalidate];
                LOG("[display] WWHD_EXIT_AT_FRAME: quitting at frame %llu", (unsigned long long)render::frame_count());
                [NSApp terminate:nil];
            }];
    }
    // test runs: WWHD_TEST_RENDERER_SWITCH=frame:metal|vulkan does what Graphics > Renderer > Restart Now does
    if (const char* e = getenv("WWHD_TEST_RENDERER_SWITCH")) {
        uint64_t at = strtoull(e, nullptr, 10);
        const char* c = strchr(e, ':');
        render::Api a = c && !strcasecmp(c + 1, "vulkan") ? render::Api::Vulkan : render::Api::Metal;
        [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer* t) {
            if (render::frame_count() < at) return;
            [t invalidate];
            LOG("[display] test: renderer %s, restart at frame %llu", render::api_name(a), (unsigned long long)render::frame_count());
            unsetenv("WWHD_TEST_RENDERER_SWITCH");  // once
            render::set_preferred(a);
            render::restart();
        }];
    }
    [NSApp run];
}

// a message on the TV window that does not stop the game (renderer fallback); test runs only log it
void show_startup_notice(const std::string& title, const std::string& text) {
    LOG("[display] %s: %s", title.c_str(), text.c_str());
    if (getenv("WWHD_NO_HOST_INPUT")) return;
    NSString* t = @(title.c_str()), *m = @(text.c_str());
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert* a = [NSAlert new];
        a.messageText = t;
        a.informativeText = m;
        a.alertStyle = NSAlertStyleWarning;
        [a addButtonWithTitle:@"OK"];
        if (g_tv_window) [a beginSheetModalForWindow:g_tv_window completionHandler:nil];
        else [a runModal];
    });
}

// ---------------------------------------------------------------- composition
static bool is_srgb(MTLPixelFormat f) { return f == MTLPixelFormatBGRA8Unorm_sRGB || f == MTLPixelFormatRGBA8Unorm_sRGB; }

// draw `tex` into the target rectangle (pixels); src_linear: the image holds linear values (sRGB scan buffer)
static void draw_image(id<MTLRenderCommandEncoder> e, MTLPixelFormat fmt, float dw, float dh, id<MTLTexture> tex,
                       bool src_linear, Box b, float alpha) {
    if (!tex || b.w <= 0 || b.h <= 0) return;
    float rect[4] = {b.x / dw, b.y / dh, b.w / dw, b.h / dh};
    bool blend = alpha < 0.999f;
    [e setRenderPipelineState:pipeline(fmt, false, blend)];
    if (blend) [e setBlendColorRed:0 green:0 blue:0 alpha:alpha];
    struct { int aa, conv; float sharp, foot; } p;
    p.aa = fxaa_enabled() ? 1 : 0;
    bool dst_linear = is_srgb(fmt);
    p.conv = src_linear == dst_linear ? 0 : dst_linear ? 1 : 2;
    float scale = std::min(b.w / tex.width, b.h / tex.height);  // target pixels per source texel
    int filter = g_filter;
    p.sharp = filter == kSmooth || scale <= 1.0f ? 1.0f : filter == kSharp ? scale : 1e4f;
    p.foot = scale < 1.0f ? 1.0f / scale : 1.0f;
    [e setVertexBytes:rect length:sizeof rect atIndex:0];
    [e setFragmentTexture:tex atIndex:0];
    [e setFragmentBytes:&p length:sizeof p atIndex:0];
    [e setFragmentSamplerState:R.linearClamp atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

static void draw_solid(id<MTLRenderCommandEncoder> e, MTLPixelFormat fmt, float dw, float dh, Box b, const float rgba[4]) {
    float rect[4] = {b.x / dw, b.y / dh, b.w / dw, b.h / dh};
    [e setRenderPipelineState:pipeline(fmt, true, true)];
    [e setVertexBytes:rect length:sizeof rect atIndex:0];
    [e setFragmentBytes:rgba length:16 atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

// this frame's settings overlay (null: nothing shown)
static ImDrawData* g_overlay_draw = nullptr;
static void overlay_metal_init() { ImGui_ImplMetal_Init(R.device); }

// the TV window's picture: TV image scaled to fit, GamePad overlay on top
static void compose_tv(id<MTLTexture> target, const Layout& L) {
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [command_buffer() renderCommandEncoderWithDescriptor:rp];
    float dw = target.width, dh = target.height;
    MTLPixelFormat fmt = target.pixelFormat;
    draw_image(e, fmt, dw, dh, R.tv.tex, R.tv.srgb, L.tv, 1.0f);
    if (L.pip_on && R.drc.tex) {
        float op = L.drc_only ? 1.0f : g_pip_opacity.load(), bw = std::max(1.0f, roundf(L.pip.w / 200.0f));
        float frame[4] = {0, 0, 0, 0.6f * op};
        draw_solid(e, fmt, dw, dh, {L.pip.x - bw, L.pip.y - bw, L.pip.w + 2 * bw, L.pip.h + 2 * bw}, frame);
        draw_image(e, fmt, dw, dh, R.drc.tex, R.drc.srgb, L.pip, op);
    }
    // settings overlay (overlay/overlay.h) on top: Dear ImGui's Metal backend in the same pass
    if (g_overlay_draw) {
        if (is_srgb(fmt)) overlay::linearize_colors(g_overlay_draw);
        ImGui_ImplMetal_NewFrame(rp);
        ImGui_ImplMetal_RenderDrawData(g_overlay_draw, command_buffer(), e);
    }
    [e endEncoding];
}

static void compose_drc(id<MTLTexture> target) {
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [command_buffer() renderCommandEncoderWithDescriptor:rp];
    Layout L = layout(target.width, target.height, R.drc.tex.width, R.drc.tex.height, 0, 0, false);
    draw_image(e, target.pixelFormat, target.width, target.height, R.drc.tex, R.drc.srgb, L.tv, 1.0f);
    [e endEncoding];
}

// ---------------------------------------------------------------- automatic overlay
// Every 4th frame both pictures are reduced to 32x18 (mipmapped copy, then a trilinear draw, display-
// encoded RGBA8) and read back; display_auto_signature (display_modes.cpp) decides.
namespace {
const uint32_t kSigW = 32, kSigH = 18, kSigN = kSigW * kSigH;
struct Sig {
    id<MTLTexture> mip, small;
    id<MTLBuffer> buf;
    // encode the reduction of `src` (on the current command buffer)
    void encode(id<MTLTexture> src, bool src_linear) {
        if (!mip || mip.width != src.width || mip.height != src.height || mip.pixelFormat != src.pixelFormat) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat width:src.width
                                                                                         height:src.height mipmapped:YES];
            d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            d.storageMode = MTLStorageModePrivate;
            mip = [R.device newTextureWithDescriptor:d];
        }
        if (!small) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:kSigW
                                                                                         height:kSigH mipmapped:NO];
            d.usage = MTLTextureUsageRenderTarget;
            d.storageMode = MTLStorageModePrivate;
            small = [R.device newTextureWithDescriptor:d];
            buf = [R.device newBufferWithLength:kSigN * 4 options:MTLResourceStorageModeShared];
        }
        static id<MTLSamplerState> trilinear = [] {
            MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
            sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
            sd.mipFilter = MTLSamplerMipFilterLinear;
            sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
            return [R.device newSamplerStateWithDescriptor:sd];
        }();
        id<MTLBlitCommandEncoder> b = [command_buffer() blitCommandEncoder];
        [b copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(src.width, src.height, 1) toTexture:mip destinationSlice:0 destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
        [b generateMipmapsForTexture:mip];
        [b endEncoding];
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = small;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> e = [command_buffer() renderCommandEncoderWithDescriptor:rp];
        float rect[4] = {0, 0, 1, 1};
        struct { int aa, conv; float sharp, foot; } p = {0, src_linear ? 2 : 0, 1.0f, 1.0f};
        [e setRenderPipelineState:pipeline(MTLPixelFormatRGBA8Unorm, false, false)];
        [e setVertexBytes:rect length:sizeof rect atIndex:0];
        [e setFragmentTexture:mip atIndex:0];
        [e setFragmentBytes:&p length:sizeof p atIndex:0];
        [e setFragmentSamplerState:trilinear atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [e endEncoding];
        b = [command_buffer() blitCommandEncoder];
        [b copyFromTexture:small sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(kSigW, kSigH, 1)
                  toBuffer:buf destinationOffset:0 destinationBytesPerRow:kSigW * 4 destinationBytesPerImage:kSigN * 4];
        [b endEncoding];
    }
    std::vector<float> luma() const {
        std::vector<float> v(kSigN);
        const uint8_t* px = (const uint8_t*)buf.contents;
        for (uint32_t i = 0; i < kSigN; i++) v[i] = (0.299f * px[i * 4] + 0.587f * px[i * 4 + 1] + 0.114f * px[i * 4 + 2]) / 255.0f;
        return v;
    }
};
}  // namespace

// Metal: reduce on the GPU, decide when the command buffer completed
static void sample_screens() {
    static std::atomic<bool> busy{false};
    static Sig drc, tv;
    if (g_mode != kDrcAuto || !R.drc.tex || R.frame % 4 || busy) {
        if (g_mode != kDrcAuto) display_auto_signature({}, nullptr, R.frame);  // forgets the last signature
        return;
    }
    drc.encode(R.drc.tex, R.drc.srgb);
    bool have_tv = R.tv.tex != nil;
    if (have_tv) tv.encode(R.tv.tex, R.tv.srgb);
    busy = true;
    uint64_t frame = R.frame;
    [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer>) {
        std::vector<float> t;
        if (have_tv) t = tv.luma();
        display_auto_signature(drc.luma(), have_tv ? &t : nullptr, frame);
        busy = false;
    }];
}

// ---------------------------------------------------------------- present
static id<MTLTexture> offscreen(NSUInteger w, NSUInteger h, bool srgb) {
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm
                                                                                 width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    return [R.device newTextureWithDescriptor:d];
}

static void present_to_layer(Screen& scr, void (^draw)(id<MTLTexture>)) {
    if (!scr.layer || !scr.tex) return;
    MTLPixelFormat want = scr.srgb ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm;
    if (scr.layer.pixelFormat != want) scr.layer.pixelFormat = want;
    id<CAMetalDrawable> drawable = scr.visible ? [scr.layer nextDrawable] : nil;
    if (!drawable) return;
    draw(drawable.texture);
    [command_buffer() presentDrawable:drawable];
}

// Metal, called from swap(): compose and present the TV window (with the GamePad overlay) and the GamePad window
void present_screens() {
    sample_screens();
    CGSize ls = R.tv.layer ? R.tv.layer.drawableSize : CGSizeMake(0, 0);
    PresentPlan P = display_plan(R.tv.tex != nil, R.tv.tex.width, R.tv.tex.height, R.drc.tex != nil, R.drc.tex.width, R.drc.tex.height,
                                 ls.width, ls.height, R.frame);
    Layout L;
    L.tv = P.tv;
    L.pip = P.pip;
    L.pip_on = P.pip_on;
    L.drc_only = P.drc_only;
    L.scale = P.scale;
    float dw = P.dw, dh = P.dh;
    bool pip = P.pip_wanted, drc_only = P.drc_only;
    g_overlay_draw = overlay::frame(dw, dh, overlay_metal_init);  // settings overlay, drawn by compose_tv
    if (P.sim) {
        present_to_layer(R.tv, ^(id<MTLTexture> t) {
            CGSize ds = R.tv.layer.drawableSize;
            compose_tv(t, layout(ds.width, ds.height, R.tv.tex.width, R.tv.tex.height, R.drc.tex ? R.drc.tex.width : 0,
                                 R.drc.tex ? R.drc.tex.height : 0, pip, drc_only));
        });
    } else {
        present_to_layer(R.tv, ^(id<MTLTexture> t) { compose_tv(t, L); });
    }
    bool drc_win = P.drc_window;
    if (drc_win) present_to_layer(R.drc, ^(id<MTLTexture> t) { compose_drc(t); });

    for (auto& path : display_take_present_dumps()) {
        if (!R.tv.tex || dw < 1 || dh < 1) continue;
        id<MTLTexture> t = offscreen((NSUInteger)dw, (NSUInteger)dh, R.tv.srgb);
        compose_tv(t, L);
        dump_texture(t, path.c_str(), true, false);
        if (drc_win && R.drc.tex && R.drc.layer) {
            CGSize ds = R.drc.layer.drawableSize;
            id<MTLTexture> d = offscreen((NSUInteger)ds.width, (NSUInteger)ds.height, R.drc.srgb);
            compose_drc(d);
            std::string p = path;
            size_t dot = p.rfind(".png");
            p.insert(dot == std::string::npos ? p.size() : dot, "_drc");
            dump_texture(d, p.c_str(), true, false);
        }
        display_log_present_dump(path, P, R.tv.tex.width, R.tv.tex.height);
    }
}

// settings overlay (overlay_appkit.mm): the Display menu's options
int display_filter() { return g_filter; }
void display_set_filter(int f) {
    g_filter = std::clamp(f, 0, 2);
    save_options();
}
int display_drc_mode() { return g_mode; }
// settings overlay: the Display menu's picture-in-picture choices
void display_set_pip(int corner, float size, float opacity) {
    g_corner = std::clamp(corner, 0, 3);
    g_pip_size = std::clamp(size, 0.1f, 0.5f);
    g_pip_opacity = std::clamp(opacity, 0.2f, 1.0f);
    save_options();
}
void display_set_tv_fullscreen(bool on) {
    if (g_tv_window && is_fullscreen(g_tv_window) != on) [g_tv_window toggleFullScreen:nil];
}
void* display_tv_window() { return (__bridge void*)g_tv_window; }

// mouse camera (mods/mouse.mm): a click on the GamePad overlay is a touch, not a capture
bool drc_overlay_hit(void* window, double wx, double wy) {
    NSWindow* w = (__bridge NSWindow*)window;
    if (!w || w != g_tv_window) return false;
    NSView* v = w.contentView;
    NSPoint p = [v convertPoint:NSMakePoint(wx, wy) fromView:nil];
    NSSize sz = v.bounds.size;
    float tx, ty;
    return sz.width > 0 && sz.height > 0 && overlay_hit(p.x / sz.width, 1.0f - p.y / sz.height, &tx, &ty);
}

// ---------------------------------------------------------------- Display menu
}  // namespace gfx

@interface WWDisplayMenu : NSObject <NSMenuItemValidation, NSMenuDelegate>
@end
@implementation WWDisplayMenu
- (void)fullScreen:(NSMenuItem*)item { [gfx::g_tv_window toggleFullScreen:nil]; }
- (void)setFilter:(NSMenuItem*)item { gfx::g_filter = (int)item.tag; gfx::save_options(); }
- (void)setMode:(NSMenuItem*)item { gfx::set_drc_mode((int)item.tag); }
- (void)toggleDrc:(NSMenuItem*)item { gfx::toggle_drc_screen(); }
- (void)setCorner:(NSMenuItem*)item { gfx::g_corner = (int)item.tag; gfx::save_options(); }
- (void)setSize:(NSMenuItem*)item { gfx::g_pip_size = item.tag / 100.0f; gfx::save_options(); }
- (void)setOpacity:(NSMenuItem*)item { gfx::g_pip_opacity = item.tag / 100.0f; gfx::save_options(); }
- (void)drcScreen:(NSMenuItem*)item {
    NSArray<NSScreen*>* s = NSScreen.screens;
    if (item.tag < 0 || item.tag >= (NSInteger)s.count) return;
    if (gfx::g_mode != gfx::kDrcWindow) gfx::set_drc_mode(gfx::kDrcWindow);
    gfx::show_drc_window(true);
    if (gfx::is_fullscreen(gfx::g_drc_window)) [gfx::g_drc_window toggleFullScreen:nil];
    else gfx::move_drc_to_screen(s[item.tag]);
}
- (void)drcFullScreen:(NSMenuItem*)item {
    if (!gfx::g_drc_window) return;
    if (gfx::g_mode != gfx::kDrcWindow) gfx::set_drc_mode(gfx::kDrcWindow);
    gfx::show_drc_window(true);
    [gfx::g_drc_window toggleFullScreen:nil];
}
- (void)menuNeedsUpdate:(NSMenu*)m {
    [m removeAllItems];
    NSArray<NSScreen*>* s = NSScreen.screens;
    for (NSUInteger i = 0; i < s.count; i++) {
        NSString* t = [NSString stringWithFormat:@"%@%@", s[i].localizedName, s[i] == gfx::g_tv_window.screen ? @" (TV window)" : @""];
        NSMenuItem* it = [m addItemWithTitle:t action:@selector(drcScreen:) keyEquivalent:@""];
        it.target = self;
        it.tag = (NSInteger)i;
    }
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
    auto on = [&](bool b) { item.state = b ? NSControlStateValueOn : NSControlStateValueOff; };
    SEL a = item.action;
    if (a == @selector(fullScreen:)) item.title = gfx::tv_fullscreen() ? @"Exit Full Screen" : @"Enter Full Screen";
    if (a == @selector(setFilter:)) on(gfx::g_filter == item.tag);
    if (a == @selector(setMode:)) {
        on(gfx::g_mode == item.tag);
        if (item.tag == gfx::kDrcWindow) return gfx::g_drc_window != nil;
    }
    if (a == @selector(toggleDrc:)) {
        on(gfx::drc_window_shown());
        return gfx::g_mode != gfx::kDrcOff && gfx::drc_window_available();
    }
    if (a == @selector(setCorner:)) on(gfx::g_corner == item.tag);
    if (a == @selector(setSize:)) on(fabsf(gfx::g_pip_size * 100 - item.tag) < 0.5f);
    if (a == @selector(setOpacity:)) on(fabsf(gfx::g_pip_opacity * 100 - item.tag) < 0.5f);
    if (a == @selector(drcScreen:)) {
        NSArray<NSScreen*>* s = NSScreen.screens;
        on(gfx::g_drc_window && item.tag < (NSInteger)s.count && gfx::g_drc_window.screen == s[item.tag] &&
           gfx::g_mode == gfx::kDrcWindow);
        return gfx::g_drc_window != nil;
    }
    if (a == @selector(drcFullScreen:)) {
        on(gfx::is_fullscreen(gfx::g_drc_window));
        return gfx::g_drc_window != nil;
    }
    return YES;
}
@end

namespace gfx {

static WWDisplayMenu* g_display_menu;

// the Display menu, added to the menu bar by install_menu (menu.mm)
void install_display_menu(NSMenu* bar) {
    g_display_menu = [WWDisplayMenu new];
    NSMenuItem* top = [bar addItemWithTitle:@"Display" action:nil keyEquivalent:@""];
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"Display"];
    auto add = [&](NSMenu* menu, NSString* title, SEL sel, NSInteger tag = 0, NSString* key = @"",
                   NSEventModifierFlags mods = NSEventModifierFlagCommand) {
        NSMenuItem* it = [menu addItemWithTitle:title action:sel keyEquivalent:key];
        it.keyEquivalentModifierMask = mods;
        it.target = g_display_menu;
        it.tag = tag;
        return it;
    };
    auto header = [&](NSMenu* menu, NSString* title) { [menu addItemWithTitle:title action:nil keyEquivalent:@""].enabled = NO; };
    add(m, @"Enter Full Screen", @selector(fullScreen:), 0, @"f");
    NSMenuItem* alt = add(m, @"Enter Full Screen", @selector(fullScreen:), 0, @"f", NSEventModifierFlagCommand | NSEventModifierFlagControl);
    alt.alternate = YES;  // Ctrl+Cmd+F, the system's full-screen shortcut, does the same
    [m addItem:[NSMenuItem separatorItem]];
    header(m, @"Picture scaling");
    add(m, @"    Smooth (bilinear)", @selector(setFilter:), kSmooth).toolTip = @"Bilinear filtering, box-filtered when the picture is shrunk";
    add(m, @"    Sharp", @selector(setFilter:), kSharp).toolTip = @"Crisp pixel edges at any scale (sharp bilinear)";
    add(m, @"    Integer scale (pixel exact)", @selector(setFilter:), kInteger).toolTip =
        @"Whole-number scale with a border; falls back to Smooth when the picture is larger than the window";
    [m addItem:[NSMenuItem separatorItem]];
    header(m, @"GamePad screen");
    add(m, @"    Separate window", @selector(setMode:), kDrcWindow);
    add(m, @"    Picture-in-picture in the TV window", @selector(setMode:), kDrcPip).toolTip = @"Click the overlay to touch";
    add(m, @"    Automatic picture-in-picture", @selector(setMode:), kDrcAuto).toolTip =
        @"The overlay appears for a few seconds when the GamePad picture changes a lot (a menu opens); Cmd+G keeps it up";
    add(m, @"    Off", @selector(setMode:), kDrcOff).toolTip = @"Not shown; the game keeps drawing it";
    add(m, @"    GamePad only", @selector(setMode:), kDrcGamePad).toolTip =
        @"Only the GamePad picture in the TV window (click it to touch); in the game, Minus switches to Off-TV Play";
    add(m, @"Show GamePad screen", @selector(toggleDrc:), 0, @"g");
    NSMenuItem* pipItem = [m addItemWithTitle:@"Picture-in-picture" action:nil keyEquivalent:@""];
    NSMenu* pm = [[NSMenu alloc] initWithTitle:@"Picture-in-picture"];
    header(pm, @"Corner");
    add(pm, @"    Top left", @selector(setCorner:), 0);
    add(pm, @"    Top right", @selector(setCorner:), 1);
    add(pm, @"    Bottom left", @selector(setCorner:), 2);
    add(pm, @"    Bottom right", @selector(setCorner:), 3);
    header(pm, @"Size (of the TV picture's width)");
    for (int s : {20, 25, 33, 40}) add(pm, [NSString stringWithFormat:@"    %d%%", s], @selector(setSize:), s);
    header(pm, @"Opacity");
    for (int o : {100, 85, 70, 50}) add(pm, [NSString stringWithFormat:@"    %d%%", o], @selector(setOpacity:), o);
    pipItem.submenu = pm;
    NSMenuItem* scrItem = [m addItemWithTitle:@"GamePad window on display" action:nil keyEquivalent:@""];
    NSMenu* sm = [[NSMenu alloc] initWithTitle:@"GamePad window on display"];
    sm.delegate = g_display_menu;
    scrItem.submenu = sm;
    add(m, @"GamePad window full screen", @selector(drcFullScreen:));
    top.submenu = m;
}

}  // namespace gfx
