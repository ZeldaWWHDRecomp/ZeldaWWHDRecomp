// Mouse capture for the mouse camera (macOS). With the mod on, a click into the TV window captures
// the pointer: it is hidden and detached from the cursor position, and its movement goes to the
// camera (camera.cpp). Esc, a middle click, switching the mod off or leaving the window releases it.
// The mouse wheel (first person mod) works whenever the TV window is the key window.
// WWHD_NO_HOST_INPUT ignores the real mouse (test runs; WWHD_TEST_MOUSE injects movement).
#import <AppKit/AppKit.h>
#include "../overlay/overlay.h"
#include <Carbon/Carbon.h>  // kVK_Escape

#include <atomic>
#include "../input_map.h"

#include "mods.h"
#include "runtime.h"
#include "../motion/motion.h"

namespace gfx { bool drc_overlay_hit(void* window, double x, double y); }  // gfx/display.mm

namespace mods {
namespace {
NSWindow* __weak g_tv;
std::atomic<bool> g_captured{false};

bool in_tv(NSEvent* e) { return g_tv && e.window == g_tv; }

bool gyro_capture_active();
void capture() {
    if (g_captured.exchange(true)) return;
    if (!gyro_capture_active()) {  // else the pointer is captured and hidden already
        CGAssociateMouseAndMouseCursorPosition(false);
        [NSCursor hide];
    }
    LOG("[mods] mouse captured (Esc releases; unbound middle click also releases)");
}
void release_now() {
    if (!g_captured.exchange(false)) return;
    if (!gyro_capture_active()) {  // else the gyro's capture keeps the pointer until the aim ends
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }
    input::release_mouse_bindings();
    mouse_button(0, false);
    mouse_button(1, false);
    LOG("[mods] mouse released");
}
}  // namespace

bool mouse_captured() { return g_captured.load(std::memory_order_relaxed); }

// the mouse as a gyro (motion/motion.h): while the game aims the pointer is captured, so that Steam
// Input's gyro-to-mouse (or the mouse) never stops at the screen's edge; released when the aim ends.
// Main thread, from the input timer (gfx/input.mm).
static bool g_gyro_capture = false;
namespace { bool gyro_capture_active() { return g_gyro_capture; } }
void update_gyro_mouse() {
    const bool want = g_tv && motion::mouse_drives_gyro() && !overlay::captures() && g_tv.isKeyWindow && NSApp.isActive &&
                      !getenv("WWHD_NO_HOST_INPUT");
    if (want && !g_gyro_capture) {
        g_gyro_capture = true;
        if (!g_captured.load()) {
            CGAssociateMouseAndMouseCursorPosition(false);
            [NSCursor hide];
            LOG("[gyro] mouse captured while the game aims");
        }
    } else if (!want && g_gyro_capture) {
        g_gyro_capture = false;
        if (!g_captured.load()) {
            CGAssociateMouseAndMouseCursorPosition(true);
            [NSCursor unhide];
        }
    }
}

void mouse_release() {
    if ([NSThread isMainThread]) release_now();
    else dispatch_async(dispatch_get_main_queue(), ^{ release_now(); });
}

bool host_key_down(uint16_t code) {
    if (code == kVK_Escape && mouse_captured()) {
        mouse_release();
        return true;
    }
    return false;
}

void mouse_init(void* tv_window) {
    g_tv = (__bridge NSWindow*)tv_window;
    if (getenv("WWHD_NO_HOST_INPUT")) return;
    g_tv.acceptsMouseMovedEvents = YES;
    NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged |
                       NSEventMaskOtherMouseDragged | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp |
                       NSEventMaskRightMouseDown | NSEventMaskRightMouseUp | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskScrollWheel;
    [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* e) {
        const bool eligible = in_tv(e) && mouse_captured() && !overlay::captures() && !overlay::layer_takes_pointer() &&
            !gfx::drc_overlay_hit((__bridge void*)g_tv, e.locationInWindow.x, e.locationInWindow.y);
        if (e.type == NSEventTypeLeftMouseDown || e.type == NSEventTypeRightMouseDown || e.type == NSEventTypeOtherMouseDown ||
            e.type == NSEventTypeLeftMouseUp || e.type == NSEventTypeRightMouseUp || e.type == NSEventTypeOtherMouseUp) {
            const bool down = e.type == NSEventTypeLeftMouseDown || e.type == NSEventTypeRightMouseDown || e.type == NSEventTypeOtherMouseDown;
            input::mouse_binding_button(0, (int)e.buttonNumber, down, eligible);
        }
        if (e.type == NSEventTypeScrollWheel) {
            float dy = (float)e.scrollingDeltaY; if (e.hasPreciseScrollingDeltas) dy /= 10.f;
            input::mouse_binding_wheel(dy, eligible && !first_person_wheel());
        }
        if (overlay::captures() || overlay::layer_takes_pointer()) return e;  // the settings overlay, the text prompt or the editor's overlays have the mouse (gfx/overlay_appkit.mm)
        if (e.type == NSEventTypeScrollWheel) {
            if (first_person_wheel() && (in_tv(e) || mouse_captured())) {
                // a wheel notch is about 1 line; trackpads send many small precise deltas
                float dy = (float)e.scrollingDeltaY;
                if (e.hasPreciseScrollingDeltas) dy /= 10.0f;
                mouse_wheel(dy);
                return nil;
            }
            return e;
        }
        // the mouse as a gyro: movement in the game window (motion.h keeps it only while the game aims)
        if ((e.type == NSEventTypeMouseMoved || e.type == NSEventTypeLeftMouseDragged || e.type == NSEventTypeRightMouseDragged ||
             e.type == NSEventTypeOtherMouseDragged) && (in_tv(e) || mouse_captured() || g_gyro_capture)) {
            motion::mouse_motion((float)e.deltaX, (float)e.deltaY);
            if (motion::mouse_drives_gyro()) return nil;
        }
        if (!mouse_camera()) return e;
        if (!mouse_captured()) {
            // a click into the game picture captures the pointer (the title bar stays usable)
            // (except on the GamePad picture-in-picture, where a click is a touch)
            if (e.type == NSEventTypeLeftMouseDown && in_tv(e) && NSPointInRect(e.locationInWindow, g_tv.contentView.frame) &&
                !gfx::drc_overlay_hit((__bridge void*)g_tv, e.locationInWindow.x, e.locationInWindow.y)) {
                capture();
                return nil;
            }
            return e;
        }
        if (!in_tv(e)) return e;
        switch (e.type) {
        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
            mouse_add((float)e.deltaX, (float)e.deltaY);  // deltaY > 0: down
            break;
        case NSEventTypeLeftMouseDown: mouse_button(0, true); break;
        case NSEventTypeLeftMouseUp: mouse_button(0, false); break;
        case NSEventTypeRightMouseDown: mouse_button(1, true); break;
        case NSEventTypeRightMouseUp: mouse_button(1, false); break;
        case NSEventTypeOtherMouseDown:
            // Cocoa previously released for every "other" button. Keep that when unbound.
            if (e.buttonNumber >= 5 || input_map::key_users(input_map::current(), input_map::kMouse1+(int)e.buttonNumber).empty()) release_now();
            break;
        default: break;
        }
        return nil;
    }];
    // leaving the game (other app, other window) releases the pointer
    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification
                                                      object:nil queue:nil usingBlock:^(NSNotification*) { release_now(); }];
    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidResignKeyNotification
                                                      object:g_tv queue:nil usingBlock:^(NSNotification*) { release_now(); }];
}

}  // namespace mods
