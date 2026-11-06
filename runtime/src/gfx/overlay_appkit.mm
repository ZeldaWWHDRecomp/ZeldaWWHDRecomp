// Settings overlay on the AppKit host (macOS, Metal and Vulkan): hostui.h on top of the menu bar's
// code (menu.mm, display.mm), so the overlay and the menus change the same state; and the overlay's
// mouse input from the TV window.
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../input.h"
#include "../input_map.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../runtime.h"
#include "display_modes.h"
#include "renderer.h"

namespace gfx {
void quit_answered();  // quit_prompt.mm
// menu.mm
float menu_res_scale();
void menu_set_res_scale(float f);
void menu_options_changed();
// display.mm
bool host_setting(const char* key, std::string& value);
void set_host_setting(const char* key, const std::string& value);
int display_filter();
void display_set_filter(int f);
int display_drc_mode();
void set_drc_mode(int m);
void display_set_pip(int corner, float size, float opacity);
bool tv_fullscreen();
void display_set_tv_fullscreen(bool on);
void* display_tv_window();
bool drc_window_available();
bool drc_window_shown();
void show_drc_window(bool on);
}  // namespace gfx

namespace hostui {

void post(std::function<void()> fn) {
    dispatch_async(dispatch_get_main_queue(), ^{ fn(); });
}
void choose_file(const char* title, const char* extension, bool folder, std::function<void(std::string)> chosen) {
    NSString* t = [NSString stringWithUTF8String:title ? title : ""];
    NSString* ext = extension && *extension ? [NSString stringWithUTF8String:extension] : nil;
    post([t, ext, folder, chosen] {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = t;
        if (ext && !folder) {
            UTType* type = [UTType typeWithFilenameExtension:ext];
            if (type) panel.allowedContentTypes = @[type];
        }
        panel.canChooseDirectories = folder;
        panel.canCreateDirectories = folder;
        panel.canChooseFiles = !folder;
        panel.allowsMultipleSelection = NO;
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            if (result == NSModalResponseOK) chosen(std::string(panel.URL.path.UTF8String));
        }];
    });
}
void choose_mod_source(bool folder, std::function<void(std::string)> chosen) {
    post([folder, chosen] {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = folder ? @"Choose mod folder" : @"Choose mod package";
        panel.canChooseDirectories = folder;
        panel.canChooseFiles = !folder;
        panel.allowsMultipleSelection = NO;
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            if (result == NSModalResponseOK) chosen(std::string(panel.URL.path.UTF8String));
        }];
    });
}
void choose_save_folder(std::function<void(std::string)> chosen) {
    post([chosen] {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Choose save folder";
        panel.canChooseDirectories = YES;
        panel.canChooseFiles = NO;
        panel.canCreateDirectories = YES;
        panel.allowsMultipleSelection = NO;
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            if (result == NSModalResponseOK) chosen(std::string(panel.URL.path.UTF8String));
        }];
    });
}
void choose_game_source(bool folder, std::function<void(std::string)> chosen) {
    post([folder, chosen] {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = folder ? @"Choose extracted GameCube game folder" : @"Choose GameCube disc image";
        if (!folder) panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"iso"],
                                                  [UTType typeWithFilenameExtension:@"gcm"],
                                                  [UTType typeWithFilenameExtension:@"rvz"]];
        panel.canChooseDirectories = folder;
        panel.canChooseFiles = !folder;
        panel.allowsMultipleSelection = NO;
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            if (result == NSModalResponseOK) chosen(std::string(panel.URL.path.UTF8String));
        }];
    });
}
bool get(const char* key, std::string& value) { return gfx::host_setting(key, value); }
void set(const char* key, const std::string& value) { gfx::set_host_setting(key, value); }
float res_scale() { return gfx::menu_res_scale(); }
void set_res_scale(float s) { gfx::menu_set_res_scale(s); }
void graphics_changed() { gfx::menu_options_changed(); }
int scale_filter() { return gfx::display_filter(); }
void set_scale_filter(int f) { gfx::display_set_filter(f); }
bool scale_filter_available() { return true; }
bool fullscreen() { return gfx::tv_fullscreen(); }
void set_fullscreen(bool on) { gfx::display_set_tv_fullscreen(on); }
int drc_modes() { return gfx::kDrcModeCount; }
bool drc_mode_offered(int m) { return m >= 0 && m < gfx::kDrcModeCount; }  // as the Display menu (window: greyed there without one)
int drc_mode() { return gfx::display_drc_mode(); }
void set_drc_mode(int m) { gfx::set_drc_mode(m); }
void save_pip_position() { gfx::display_set_pip(gfx::g_pip_anchor, gfx::g_pip_size, gfx::g_pip_opacity); }
int pip_anchor() { return gfx::g_pip_anchor; }
void set_pip_anchor(int c) { gfx::display_set_pip(c, gfx::g_pip_size, gfx::g_pip_opacity); }
float pip_size() { return gfx::g_pip_size; }
void set_pip_size(float s) { gfx::display_set_pip(gfx::g_pip_anchor, s, gfx::g_pip_opacity); }
float pip_opacity() { return gfx::g_pip_opacity; }
void set_pip_opacity(float o) { gfx::display_set_pip(gfx::g_pip_anchor, gfx::g_pip_size, o); }
void quit() { gfx::quit_answered(); }  // as the app menu's Quit, without asking again
bool drc_available() { return gfx::drc_window_available(); }
bool drc_shown() { return gfx::drc_window_shown(); }
void show_drc(bool on) { gfx::show_drc_window(on); }
void set_pro_controller(bool on) {
    // as Input > Wii U GamePad / Pro Controller: the GamePad window follows the choice
    input::set_pro_controller(on);
    gfx::show_drc_window(!on);
}
const char* name() { return "AppKit"; }
void set_clipboard(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
}
bool can_open_folder() { return true; }
void open_folder(const std::string& path) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()] isDirectory:YES]];
}
void run_posted() {}
void load_saved_options() {}

}  // namespace hostui

namespace gfx {

// test aid: WWHD_TEST_POST_MOUSE=600:move:0.5:0.1,620:click:0.1:0.06,640:rdown:..,660:rup:..
// posts mouse events (moved, left click, left down / up, right down / up) at TV frames into the
// app's event queue for the TV window, at (x, y) as fractions of its content (0,0 top left), so they take
// the real path (the event monitors here and in mods/mouse.mm) also in hidden test runs. With
// WWHD_NO_HOST_INPUT only these posted events reach the overlay, never the real mouse.
static const NSTimeInterval kTestMouseTimestamp = 4243.0;
static bool test_mouse_event(NSEvent* e) { return e.timestamp == kTestMouseTimestamp; }
static void start_test_mouse(NSWindow* tv) {
    const char* env = getenv("WWHD_TEST_POST_MOUSE");
    if (!env || !*env) return;
    struct Post { uint64_t frame; std::string what; double x, y; };
    auto posts = std::make_shared<std::vector<Post>>();
    for (const char* p = env; *p;) {
        size_t len = strcspn(p, ",");
        const std::string item(p, len);
        p += len + (p[len] == ',');
        char what[16] = {};
        unsigned long long f = 0;
        double x = 0, y = 0;
        if (sscanf(item.c_str(), "%llu:%15[a-z]:%lf:%lf", &f, what, &x, &y) != 4) {
            LOG("[overlay] WWHD_TEST_POST_MOUSE: cannot read \"%s\"", item.c_str());
            continue;
        }
        posts->push_back({f, what, x, y});
    }
    std::stable_sort(posts->begin(), posts->end(), [](const Post& a, const Post& b) { return a.frame < b.frame; });
    [NSTimer scheduledTimerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer* t) {
        const uint64_t frame = render::frame_count();
        while (!posts->empty() && frame >= posts->front().frame) {
            const Post po = posts->front();
            posts->erase(posts->begin());
            NSView* v = tv.contentView;
            const NSSize sz = v.bounds.size;
            NSPoint local = NSMakePoint(po.x * sz.width, v.isFlipped ? po.y * sz.height : (1.0 - po.y) * sz.height);
            const NSPoint at = [v convertPoint:local toView:nil];  // window coordinates
            std::vector<NSEventType> types;
            if (po.what == "move") types = {NSEventTypeMouseMoved};
            else if (po.what == "click") types = {NSEventTypeMouseMoved, NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp};
            else if (po.what == "down") types = {NSEventTypeMouseMoved, NSEventTypeLeftMouseDown};
            else if (po.what == "up") types = {NSEventTypeLeftMouseUp};
            else if (po.what == "rdown") types = {NSEventTypeMouseMoved, NSEventTypeRightMouseDown};
            else if (po.what == "rup") types = {NSEventTypeRightMouseUp};
            else {
                LOG("[overlay] test mouse: unknown action %s", po.what.c_str());
                continue;
            }
            LOG("[overlay] test mouse %s at %.3f %.3f (frame %llu)", po.what.c_str(), po.x, po.y, (unsigned long long)frame);
            for (NSEventType type : types) {
                NSEvent* ev = [NSEvent mouseEventWithType:type location:at modifierFlags:0 timestamp:kTestMouseTimestamp
                                             windowNumber:tv.windowNumber context:nil eventNumber:0 clickCount:1
                                                 pressure:type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ? 1.0f : 0.0f];
                [NSApp postEvent:ev atStart:NO];
            }
        }
        if (posts->empty()) [t invalidate];
    }];
}

// mouse in the TV window while the overlay is open (installed with the menus, menu.mm)
void install_overlay_input() {
    NSWindow* tv = (__bridge NSWindow*)display_tv_window();
    if (!tv) return;
    auto density = [tv] { overlay::set_density((float)tv.backingScaleFactor); };
    density();
    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidChangeBackingPropertiesNotification object:tv queue:nil
                                                  usingBlock:^(NSNotification*) { density(); }];
    const bool test_only = getenv("WWHD_NO_HOST_INPUT") != nullptr;
    if (test_only && !getenv("WWHD_TEST_POST_MOUSE")) return;
    start_test_mouse(tv);
    tv.acceptsMouseMovedEvents = YES;
    NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged |
                       NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskRightMouseDown | NSEventMaskRightMouseUp |
                       NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskScrollWheel;
    [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* e) {
        if (e.window != tv || (test_only && !test_mouse_event(e))) return e;
        if (!overlay::captures() && overlay::pointer_layer_active() && !overlay::layer_takes_pointer()) {
            // the editor's small overlays over the running game: they follow the pointer, the game keeps the click
            NSView* v = tv.contentView;
            NSPoint p = [v convertPoint:e.locationInWindow fromView:nil];
            NSSize sz = v.bounds.size;
            if (sz.width > 0 && sz.height > 0 && p.y <= sz.height)
                overlay::mouse_move((float)(p.x / sz.width), (float)(v.isFlipped ? p.y / sz.height : 1.0 - p.y / sz.height));
        }
        if (!overlay::captures() && !overlay::layer_takes_pointer()) {
            const bool down = e.type==NSEventTypeLeftMouseDown || e.type==NSEventTypeRightMouseDown || e.type==NSEventTypeOtherMouseDown;
            const bool up = e.type==NSEventTypeLeftMouseUp || e.type==NSEventTypeRightMouseUp || e.type==NSEventTypeOtherMouseUp;
            if ((down || up) && e.buttonNumber < 5 && overlay::pointer_shortcut(input_map::kMouse1+(int)e.buttonNumber,down)) return nil;
            if (e.type==NSEventTypeScrollWheel && e.scrollingDeltaY!=0 &&
                overlay::pointer_shortcut(e.scrollingDeltaY>0?input_map::kWheelUp:input_map::kWheelDown,true)) return nil;
            return e;
        }
        NSView* v = tv.contentView;
        NSPoint p = [v convertPoint:e.locationInWindow fromView:nil];
        NSSize sz = v.bounds.size;
        if (sz.width <= 0 || sz.height <= 0) return e;
        // the title bar stays the window's (move, close)
        if (p.y > sz.height && e.type != NSEventTypeScrollWheel) return e;
        overlay::mouse_move((float)(p.x / sz.width), (float)(v.isFlipped ? p.y / sz.height : 1.0 - p.y / sz.height));
        // This monitor can swallow the event before the mouse-camera monitor sees it. Keep the
        // physical button gate current, particularly an up inside the overlay before it closes.
        if (e.type == NSEventTypeLeftMouseDown || e.type == NSEventTypeRightMouseDown || e.type == NSEventTypeOtherMouseDown ||
            e.type == NSEventTypeLeftMouseUp || e.type == NSEventTypeRightMouseUp || e.type == NSEventTypeOtherMouseUp) {
            const bool down = e.type == NSEventTypeLeftMouseDown || e.type == NSEventTypeRightMouseDown || e.type == NSEventTypeOtherMouseDown;
            input::mouse_binding_button(0, (int)e.buttonNumber, down, false);
        }
        switch (e.type) {
        case NSEventTypeLeftMouseDown: overlay::mouse_button(0, true); break;
        case NSEventTypeLeftMouseUp: overlay::mouse_button(0, false); break;
        case NSEventTypeRightMouseDown: overlay::mouse_button(1, true); break;
        case NSEventTypeRightMouseUp: overlay::mouse_button(1, false); break;
        case NSEventTypeOtherMouseDown: overlay::mouse_button((int)e.buttonNumber, true); break;
        case NSEventTypeOtherMouseUp: overlay::mouse_button((int)e.buttonNumber, false); break;
        case NSEventTypeScrollWheel: {
            float dx = (float)e.scrollingDeltaX, dy = (float)e.scrollingDeltaY;
            if (e.hasPreciseScrollingDeltas) dx /= 10.0f, dy /= 10.0f;  // trackpad: points, not lines
            overlay::mouse_wheel(dx, dy);
            break;
        }
        default: break;
        }
        return nil;  // the overlay's, not the game's (mouse camera, GamePad touch)
    }];
}

}  // namespace gfx
