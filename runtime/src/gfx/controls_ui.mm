// Input > Controls…: remap keyboard keys and controller inputs (input_map.h) on a drawing of the
// selected Wii U controller (GamePad or Pro Controller, as chosen in the Input menu).
//
// The drawing is an original, simplified schematic made in code (no artwork). Each button, d-pad
// direction and stick direction / click on it can be clicked: the window then waits for a key or a
// host-controller input and binds it (Esc cancels). Callouts on both sides show every binding (key,
// alternate key, controller); clicking one of those binds just that slot, right-click or its × clears
// it. Pressed inputs light up and sticks show their deflection, so the mapping can be tried out.
// Bindings used twice are drawn in orange. Changes apply at once and are saved to controls.json.
// While this window is the key window, input.mm's key monitor passes key events to it instead of the game.
//
// Debug: WWHD_SHOW_CONTROLS=1 opens the window at start (without the keyboard focus in test runs);
// WWHD_CONTROLS_SNAPSHOT=<prefix> then writes <prefix>_{gamepad,pro}_{light,dark}.png of the window
// after WWHD_CONTROLS_SNAPSHOT_AT seconds (default 3); WWHD_CONTROLS_HOVER=<input id, e.g. ZR> shows
// that input hovered. WWHD_CONTROLS_SELFTEST=1 drives the window with synthetic events.
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <Carbon/Carbon.h>  // kVK_* key codes

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../input.h"
#include "../input_map.h"
#include "../runtime.h"
#include "../platform/host.h"

namespace gfx { void show_drc_window(bool on); }

using namespace input_map;

static NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
static NSString* ns(const char* s) { return [NSString stringWithUTF8String:s]; }

static NSString* action_list(const std::vector<int>& v) {
    NSMutableArray* names = [NSMutableArray array];
    for (int a : v) [names addObject:ns(action_label(a))];
    return [names componentsJoinedByString:@", "];
}

// capture modes: what a click is waiting for
enum { kCapAny, kCapKey0, kCapKey1, kCapPad };
// chip slots in a callout row
enum { kSlotKey0, kSlotKey1, kSlotPad, kSlotCount };

// ---- colours (light / dark)

static NSColor* rgb(uint32_t v, CGFloat a = 1) {
    return [NSColor colorWithSRGBRed:((v >> 16) & 255) / 255.0 green:((v >> 8) & 255) / 255.0 blue:(v & 255) / 255.0 alpha:a];
}
static NSColor* dyn(uint32_t light, uint32_t dark) {
    return [NSColor colorWithName:nil dynamicProvider:^NSColor*(NSAppearance* ap) {
        bool d = [[ap bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]]
                    isEqualToString:NSAppearanceNameDarkAqua];
        return rgb(d ? dark : light);
    }];
}
struct Palette {
    NSColor *body = dyn(0xE6E7EB, 0x3C3D43), *outline = dyn(0x8D9099, 0x80838D), *well = dyn(0xC8CAD0, 0x232428),
            *button = dyn(0xFBFBFC, 0x5A5C64), *buttonText = dyn(0x3A3C42, 0xEDEDF0), *screen = dyn(0x262A31, 0x141518),
            *screenText = dyn(0xE8EAEE, 0xD8DAE0), *keycap = dyn(0xFFFFFF, 0x4B4D54), *keyEdge = dyn(0xB4B7BF, 0x24252A),
            *padChip = dyn(0xE3E7F8, 0x343A55), *padEdge = dyn(0xA9B1D6, 0x4F587E), *box = dyn(0xF7F7F9, 0x2A2B2F),
            *boxEdge = dyn(0xD3D5DB, 0x45474E), *leader = dyn(0xA4A7AF, 0x6D7079);
};
static const Palette& pal() {
    static Palette p;
    return p;
}
static NSColor* accent() { return NSColor.controlAccentColor; }
static NSColor* warn() { return NSColor.systemOrangeColor; }

// ---- text helpers

static NSDictionary* attrs(NSFont* f, NSColor* c) { return @{NSFontAttributeName: f, NSForegroundColorAttributeName: c}; }
static void draw_centered(NSString* s, NSPoint c, NSFont* f, NSColor* col) {
    NSDictionary* at = attrs(f, col);
    NSSize sz = [s sizeWithAttributes:at];
    [s drawAtPoint:NSMakePoint(c.x - sz.width / 2, c.y - sz.height / 2) withAttributes:at];
}
// centered in r, shrinking the font (down to 8 pt) and then truncating to fit
static void draw_fit(NSString* s, NSRect r, CGFloat size, NSFontWeight w, NSColor* col) {
    NSFont* f = [NSFont systemFontOfSize:size weight:w];
    while ([s sizeWithAttributes:attrs(f, col)].width > r.size.width && size > 8) f = [NSFont systemFontOfSize:(size -= 0.5) weight:w];
    NSMutableParagraphStyle* ps = [NSMutableParagraphStyle new];
    ps.alignment = NSTextAlignmentCenter;
    ps.lineBreakMode = NSLineBreakByTruncatingTail;
    NSDictionary* at = @{NSFontAttributeName: f, NSForegroundColorAttributeName: col, NSParagraphStyleAttributeName: ps};
    CGFloat h = [s sizeWithAttributes:at].height;
    [s drawInRect:NSMakeRect(r.origin.x, NSMidY(r) - h / 2, r.size.width, h) withAttributes:at];
}

// ---- controller geometry, in units of the controller's width (y down)

struct UPt { CGFloat x, y; };
struct ControllerDef {
    bool pro;
    CGFloat minY, maxY;  // bounding box y (x is 0..1)
    UPt lstick, rstick, dpad, face, plus, minus, home;
    CGFloat stickR, faceSpread, faceR, smallR, dpadLen, dpadW;
    NSRect l, zl;  // left shoulder tabs (the right ones are mirrored)
    NSRect screen; // GamePad only
};

static ControllerDef controller_def(bool pro) {
    ControllerDef d{};
    d.pro = pro;
    if (!pro) {  // GamePad: a wide tablet, screen in the middle
        d.minY = -0.085; d.maxY = 0.5;
        d.lstick = {0.115, 0.15}; d.rstick = {0.885, 0.15};
        d.dpad = {0.115, 0.335}; d.face = {0.885, 0.315};
        d.minus = {0.85, 0.44}; d.plus = {0.92, 0.44}; d.home = {0.5, 0.44};
        d.stickR = 0.058; d.faceSpread = 0.045; d.faceR = 0.021; d.smallR = 0.016;
        d.dpadLen = 0.052; d.dpadW = 0.034;
        d.l = NSMakeRect(0.06, -0.04, 0.15, 0.07);
        d.zl = NSMakeRect(0.045, -0.08, 0.135, 0.07);
        d.screen = NSMakeRect(0.255, 0.07, 0.49, 0.28);
    } else {  // Pro Controller: two grips
        d.minY = -0.05; d.maxY = 0.63;
        d.lstick = {0.2, 0.2}; d.rstick = {0.67, 0.385};
        d.dpad = {0.33, 0.385}; d.face = {0.8, 0.2};
        d.minus = {0.415, 0.2}; d.plus = {0.585, 0.2}; d.home = {0.5, 0.265};
        d.stickR = 0.068; d.faceSpread = 0.052; d.faceR = 0.025; d.smallR = 0.019;
        d.dpadLen = 0.056; d.dpadW = 0.036;
        d.l = NSMakeRect(0.09, -0.005, 0.2, 0.07);
        d.zl = NSMakeRect(0.12, -0.045, 0.16, 0.07);
    }
    return d;
}

static NSBezierPath* pro_body(NSPoint (^P)(CGFloat, CGFloat)) {
    NSBezierPath* b = [NSBezierPath bezierPath];
    [b moveToPoint:P(0.14, 0.035)];
    [b lineToPoint:P(0.86, 0.035)];
    [b curveToPoint:P(1.0, 0.2) controlPoint1:P(0.96, 0.035) controlPoint2:P(1.0, 0.11)];
    [b curveToPoint:P(0.86, 0.625) controlPoint1:P(1.0, 0.42) controlPoint2:P(0.96, 0.625)];
    [b curveToPoint:P(0.70, 0.52) controlPoint1:P(0.78, 0.625) controlPoint2:P(0.745, 0.545)];
    [b curveToPoint:P(0.30, 0.52) controlPoint1:P(0.6, 0.475) controlPoint2:P(0.4, 0.475)];
    [b curveToPoint:P(0.14, 0.625) controlPoint1:P(0.255, 0.545) controlPoint2:P(0.22, 0.625)];
    [b curveToPoint:P(0.0, 0.2) controlPoint1:P(0.04, 0.625) controlPoint2:P(0.0, 0.42)];
    [b curveToPoint:P(0.14, 0.035) controlPoint1:P(0.0, 0.11) controlPoint2:P(0.04, 0.035)];
    [b closePath];
    return b;
}

// parts of the drawing, in view coordinates
enum PartKind { kPartRound, kPartShoulder, kPartDpad, kPartStick };
struct Part {
    int kind;
    int action;     // kPartStick: the click action; sticks also have dirs[]
    NSRect rect;    // round: bounding box of the circle; shoulder: tab; dpad: arm
    NSRect vis = NSZeroRect;  // shoulder: the strip not covered by the body / the tab in front
    NSString* text;
    int dirs[4] = {-1, -1, -1, -1};  // stick: up, down, left, right
    NSPoint c;
    CGFloat R = 0, capR = 0;
    int stick = 0;  // 0 left, 1 right
};

struct Group {
    NSString* title;
    std::vector<int> acts;
    std::vector<NSString*> rowLabels;
    NSPoint anchor;
    bool right;
    bool leader = true;  // a line to the anchor (app actions have no part on the controller)
    NSRect box;
    std::vector<NSRect> rows;  // one per action (the title row of a multi-row group is box top)
};

struct Chip { int action, slot; NSRect r; };

struct Geo {
    NSSize size = {0, 0};
    bool pro = false;
    FaceLayout fl = FaceLayout::kPosition;
    CGFloat s = 1;
    NSPoint o;
    NSBezierPath* body;
    NSRect screen = NSZeroRect;
    std::vector<NSRect> leds;
    std::vector<Part> parts;  // front to back for hit-testing
    std::vector<Group> groups;
    std::vector<Chip> chips;
    CGFloat colLx, colRx, drawL, drawR;
};

static const CGFloat kColW = 254, kRowH = 23, kMargin = 14, kHeaderH = 22;
static const CGFloat kChipX[kSlotCount] = {50, 110, 170}, kChipW[kSlotCount] = {56, 56, 78};

// live state shared by the window controller and the view
struct UIState {
    Mapping m;
    bool pro = false;
    int capAction = -1, capMode = kCapAny;
    bool keys[256] = {};
    float pad[kPadCount] = {};
    float act[kActionCount] = {};
    float stick[2][2] = {};  // deflection of the left / right stick, x right, y up
    double t = 0;            // for the capture pulse
    bool padConnected = false;
};

@class WWControls;

@interface WWPadView : NSView
@property(weak) WWControls* owner;
@property(assign) UIState* st;
@property(assign) int hoverAction;
@property(assign) int hoverSlot;  // -1: the drawing / row, else a chip
@property(assign) BOOL hoverX;    // over a chip's × (clear)
- (void)invalidateGeo;
@end

@interface WWControls : NSObject <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) WWPadView* padView;
@property(strong) NSTextField* status;
@property(strong) NSTextField* padInfo;
@property(strong) NSSegmentedControl* which;
@property(strong) NSSegmentedControl* faceLayout;
@property(strong) NSSlider* deadzone;
@property(strong) NSTextField* deadzoneValue;
@property(strong) NSButton* invertY;
@property(strong) NSTimer* timer;
- (BOOL)handleKeyEvent:(NSEvent*)e;
- (void)beginCapture:(int)a mode:(int)mode;
- (void)cancelCapture;
- (void)clearAction:(int)a slot:(int)slot;
- (void)showMenuForAction:(int)a event:(NSEvent*)e view:(NSView*)v;
- (void)hoverChanged;
- (void)releaseLocalKeys;
@end

// routes key presses to the controller (capture, live display) before the window handles them
@interface WWControlsWindow : NSWindow
@property(weak) WWControls* controls;
@end
@implementation WWControlsWindow
- (void)sendEvent:(NSEvent*)e {
    if ((e.type == NSEventTypeKeyDown || e.type == NSEventTypeKeyUp || e.type == NSEventTypeFlagsChanged) &&
        [self.controls handleKeyEvent:e])
        return;
    [super sendEvent:e];
}
@end

@interface WWRootView : NSView
@end
@implementation WWRootView
- (void)drawRect:(NSRect)r {
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(r);
}
@end

// ================================================================= the drawing

@implementation WWPadView {
    Geo _g;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (void)keyDown:(NSEvent*)e {}  // keys are handled in the window's sendEvent; no beep
- (void)keyUp:(NSEvent*)e {}
- (void)flagsChanged:(NSEvent*)e {}
- (void)invalidateGeo { _g.size = NSZeroSize; self.needsDisplay = YES; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                       options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                                               NSTrackingActiveAlways | NSTrackingInVisibleRect
                                                         owner:self userInfo:nil]];
}

// ---- layout

- (const Geo&)geo {
    NSSize sz = self.bounds.size;
    FaceLayout fl = face_layout(self.st->m);
    if (NSEqualSizes(sz, _g.size) && _g.pro == self.st->pro && _g.fl == fl) return _g;
    Geo g;
    g.size = sz;
    g.pro = self.st->pro;
    g.fl = fl;
    ControllerDef d = controller_def(g.pro);
    g.colLx = kMargin;
    g.colRx = sz.width - kMargin - kColW;
    g.drawL = g.colLx + kColW + 30;
    g.drawR = g.colRx - 30;
    CGFloat top = kHeaderH + 10, bottom = sz.height - 12;
    CGFloat aw = std::max<CGFloat>(g.drawR - g.drawL, 100), ah = std::max<CGFloat>(bottom - top, 100);
    CGFloat uh = d.maxY - d.minY;
    g.s = std::min({aw, ah / uh, (CGFloat)640});
    g.o = NSMakePoint((g.drawL + g.drawR) / 2 - g.s / 2, (top + bottom) / 2 - g.s * uh / 2 - g.s * d.minY);
    CGFloat s = g.s;
    NSPoint o = g.o;
    auto P = [=](CGFloat x, CGFloat y) { return NSMakePoint(o.x + x * s, o.y + y * s); };
    auto PU = [=](UPt u) { return P(u.x, u.y); };
    auto R = [=](NSRect r) { return NSMakeRect(o.x + r.origin.x * s, o.y + r.origin.y * s, r.size.width * s, r.size.height * s); };
    auto mirror = [](NSRect r) { return NSMakeRect(1 - r.origin.x - r.size.width, r.origin.y, r.size.width, r.size.height); };

    if (g.pro) {
        g.body = pro_body(^NSPoint(CGFloat x, CGFloat y) { return P(x, y); });
        for (int i = 0; i < 4; i++) {
            NSPoint c = P(0.455 + i * 0.03, 0.44);
            g.leds.push_back(NSMakeRect(c.x - 0.006 * s, c.y - 0.006 * s, 0.012 * s, 0.012 * s));
        }
    } else {
        g.body = [NSBezierPath bezierPathWithRoundedRect:R(NSMakeRect(0, 0, 1, 0.5)) xRadius:0.07 * s yRadius:0.07 * s];
        g.screen = R(d.screen);
    }

    auto button = [&](int a, UPt c, CGFloat r, NSString* text) {
        Part p;
        p.kind = kPartRound;
        p.action = a;
        NSPoint pc = PU(c);
        p.c = pc;
        p.rect = NSMakeRect(pc.x - r * s, pc.y - r * s, 2 * r * s, 2 * r * s);
        p.text = text;
        g.parts.push_back(p);
    };
    // face buttons: Wii U positions (X top, A right, B bottom, Y left). With the by-label (Xbox)
    // preset the letters follow the host pad instead (Y top, B right, A bottom, X left), so each
    // letter sits where that host button is (issue #78).
    if (g.fl == FaceLayout::kLabels) {
        button(kY, {d.face.x, d.face.y - d.faceSpread}, d.faceR, @"Y");
        button(kB, {d.face.x + d.faceSpread, d.face.y}, d.faceR, @"B");
        button(kA, {d.face.x, d.face.y + d.faceSpread}, d.faceR, @"A");
        button(kX, {d.face.x - d.faceSpread, d.face.y}, d.faceR, @"X");
    } else {
        button(kX, {d.face.x, d.face.y - d.faceSpread}, d.faceR, @"X");
        button(kA, {d.face.x + d.faceSpread, d.face.y}, d.faceR, @"A");
        button(kB, {d.face.x, d.face.y + d.faceSpread}, d.faceR, @"B");
        button(kY, {d.face.x - d.faceSpread, d.face.y}, d.faceR, @"Y");
    }
    button(kPlus, d.plus, d.smallR, @"+");
    button(kMinus, d.minus, d.smallR, @"−");
    button(kHome, d.home, d.smallR, @"⌂");
    // d-pad arms: up, down, left, right
    {
        NSPoint c = PU(d.dpad);
        CGFloat L = d.dpadLen * s, W = d.dpadW * s;
        NSRect arms[4] = {NSMakeRect(c.x - W / 2, c.y - L, W, L - W / 2), NSMakeRect(c.x - W / 2, c.y + W / 2, W, L - W / 2),
                          NSMakeRect(c.x - L, c.y - W / 2, L - W / 2, W), NSMakeRect(c.x + W / 2, c.y - W / 2, L - W / 2, W)};
        int acts[4] = {kDUp, kDDown, kDLeft, kDRight};
        for (int i = 0; i < 4; i++) {
            Part p;
            p.kind = kPartDpad;
            p.action = acts[i];
            p.rect = arms[i];
            p.c = c;
            p.dirs[0] = i;
            g.parts.push_back(p);
        }
    }
    for (int i = 0; i < 2; i++) {
        Part p;
        p.kind = kPartStick;
        p.stick = i;
        p.c = PU(i ? d.rstick : d.lstick);
        p.R = d.stickR * s;
        p.capR = p.R * 0.62;
        p.rect = NSMakeRect(p.c.x - p.R, p.c.y - p.R, 2 * p.R, 2 * p.R);
        p.action = i ? kStickRClick : kStickLClick;
        int base = i ? kRUp : kLUp;
        for (int k = 0; k < 4; k++) p.dirs[k] = base + k;  // up, down, left, right (enum order)
        p.text = i ? @"R" : @"L";
        g.parts.push_back(p);
    }
    CGFloat bodyTop = g.pro ? 0.035 : 0;
    auto shoulder = [&](int a, NSRect r, CGFloat coveredFrom, NSString* t) {
        Part p;
        p.kind = kPartShoulder;
        p.action = a;
        p.rect = R(r);
        p.vis = R(NSMakeRect(r.origin.x, r.origin.y, r.size.width, coveredFrom - r.origin.y));
        p.c = NSMakePoint(NSMidX(p.vis), NSMidY(p.vis));
        p.text = t;
        g.parts.push_back(p);
    };
    shoulder(kL, d.l, bodyTop, @"L");
    shoulder(kR, mirror(d.l), bodyTop, @"R");
    shoulder(kZL, d.zl, d.l.origin.y, @"ZL");
    shoulder(kZR, mirror(d.zl), d.l.origin.y, @"ZR");
    auto tabAnchor = [&](int a) {  // the outer end of the visible strip (the label stays clear)
        for (auto& p : g.parts)
            if (p.kind == kPartShoulder && p.action == a)
                return NSMakePoint(a == kL || a == kZL ? NSMinX(p.vis) + 3 : NSMaxX(p.vis) - 3, NSMidY(p.vis));
        return NSZeroPoint;
    };

    // callouts
    auto single = [&](NSString* title, int a, NSPoint anchor, bool right) {
        Group gr;
        gr.title = title;
        gr.acts = {a};
        gr.rowLabels = {title};
        gr.anchor = anchor;
        gr.right = right;
        g.groups.push_back(gr);
    };
    auto multi = [&](NSString* title, std::vector<int> acts, std::vector<NSString*> labels, NSPoint anchor, bool right) {
        Group gr;
        gr.title = title;
        gr.acts = acts;
        gr.rowLabels = labels;
        gr.anchor = anchor;
        gr.right = right;
        g.groups.push_back(gr);
    };
    single(@"ZL", kZL, tabAnchor(kZL), false);
    single(@"ZR", kZR, tabAnchor(kZR), true);
    single(@"L", kL, tabAnchor(kL), false);
    single(@"R", kR, tabAnchor(kR), true);
    multi(@"Left stick (move)", {kLUp, kLDown, kLLeft, kLRight, kStickLClick}, {@"↑", @"↓", @"←", @"→", @"Click"}, PU(d.lstick), false);
    multi(@"Right stick (camera)", {kRUp, kRDown, kRLeft, kRRight, kStickRClick}, {@"↑", @"↓", @"←", @"→", @"Click"}, PU(d.rstick), true);
    multi(@"D-pad", {kDUp, kDDown, kDLeft, kDRight}, {@"↑", @"↓", @"←", @"→"}, PU(d.dpad), false);
    // app actions: the bottom of the left column, no leader line ("Photo": the row label column is narrow;
    // hovering shows "Screenshot (app)")
    single(@"Photo", kScreenshot, NSMakePoint(0, sz.height), false);
    g.groups.back().leader = false;
    for (auto& p : g.parts)
        if (p.kind == kPartRound) {
            bool right = p.action == kPlus || p.action == kA || p.action == kB || p.action == kX || p.action == kY ||
                         (p.action == kMinus && !g.pro) || (p.action == kHome && g.pro);
            single(p.action == kPlus ? @"+" : p.action == kMinus ? @"−" : p.action == kHome ? @"Home" : p.text, p.action, p.c, right);
        }
    // stack each column in anchor order, as close to the anchors as fits
    for (int side = 0; side < 2; side++) {
        std::vector<Group*> col;
        for (auto& gr : g.groups)
            if (gr.right == (side == 1)) col.push_back(&gr);
        std::stable_sort(col.begin(), col.end(), [](Group* a, Group* b) {
            if (a->anchor.y != b->anchor.y) return a->anchor.y < b->anchor.y;
            return a->right ? a->anchor.x > b->anchor.x : a->anchor.x < b->anchor.x;
        });
        const CGFloat gap = 7, minTop = kHeaderH + 6, maxBottom = sz.height - 8;
        std::vector<CGFloat> y(col.size()), h(col.size());
        for (size_t i = 0; i < col.size(); i++) {
            int n = (int)col[i]->acts.size();
            h[i] = kRowH * (n == 1 ? 1 : n + 1) + 4;
            y[i] = std::max(col[i]->anchor.y - kRowH / 2 - 2, i ? y[i - 1] + h[i - 1] + gap : minTop);
        }
        for (size_t i = col.size(); i-- > 0;) {
            CGFloat limit = i + 1 < col.size() ? y[i + 1] - gap : maxBottom;
            y[i] = std::min(y[i], limit - h[i]);
        }
        for (size_t i = 0; i < col.size(); i++) {
            y[i] = std::max(y[i], i ? y[i - 1] + h[i - 1] + gap : minTop);
            Group& gr = *col[i];
            CGFloat x = side ? g.colRx : g.colLx;
            gr.box = NSMakeRect(x, y[i], kColW, h[i]);
            int n = (int)gr.acts.size();
            CGFloat ry = y[i] + 2 + (n == 1 ? 0 : kRowH);
            for (int k = 0; k < n; k++, ry += kRowH) {
                NSRect row = NSMakeRect(x, ry, kColW, kRowH);
                gr.rows.push_back(row);
                for (int sl = 0; sl < kSlotCount; sl++)
                    g.chips.push_back({gr.acts[k], sl, NSMakeRect(x + kChipX[sl], ry + 2.5, kChipW[sl], kRowH - 5)});
            }
        }
    }
    _g = g;
    return _g;
}

// ---- hit testing

static bool in_circle(NSPoint p, NSPoint c, CGFloat r) { return hypot(p.x - c.x, p.y - c.y) <= r; }
static NSRect x_rect(NSRect chip) { return NSMakeRect(NSMaxX(chip) - 7, chip.origin.y - 5, 12, 12); }

// action under the point on the drawing or a callout row, -1 if none; slot = chip slot or -1
- (int)partAt:(NSPoint)p slot:(int*)slot onX:(BOOL*)onX {
    const Geo& g = [self geo];
    *slot = -1;
    *onX = NO;
    const UIState& st = *self.st;
    // the × of the hovered chip
    if (self.hoverSlot >= 0 && self.hoverAction >= 0)
        for (auto& c : g.chips)
            if (c.action == self.hoverAction && c.slot == self.hoverSlot && [self chipBound:c] && NSPointInRect(p, x_rect(c.r))) {
                *slot = c.slot;
                *onX = YES;
                return c.action;
            }
    for (auto& c : g.chips)
        if (NSPointInRect(p, NSInsetRect(c.r, -2, -2))) {
            *slot = c.slot;
            return c.action;
        }
    for (auto& gr : g.groups)
        for (size_t k = 0; k < gr.rows.size(); k++)
            if (NSPointInRect(p, gr.rows[k])) return gr.acts[k];
    for (auto& part : g.parts) {
        switch (part.kind) {
        case kPartRound:
            if (in_circle(p, part.c, part.rect.size.width / 2 + 2)) return part.action;
            break;
        case kPartDpad:
            if (NSPointInRect(p, NSInsetRect(part.rect, -2, -2))) return part.action;
            break;
        case kPartStick: {
            if (!in_circle(p, part.c, part.R + 3)) break;
            CGFloat dx = p.x - part.c.x, dy = p.y - part.c.y;
            if (hypot(dx, dy) < part.capR * 0.6) return part.action;
            if (std::fabs(dx) > std::fabs(dy)) return part.dirs[dx < 0 ? 2 : 3];
            return part.dirs[dy < 0 ? 0 : 1];
        }
        case kPartShoulder:
            if (NSPointInRect(p, NSInsetRect(part.vis, -1, -1)) && ![g.body containsPoint:p]) return part.action;
            break;
        }
    }
    (void)st;
    return -1;
}

- (bool)chipBound:(const Chip&)c {
    const Mapping& m = self.st->m;
    return c.slot == kSlotPad ? m.pad[c.action] != kPadNone : m.keys[c.action][c.slot] != kNoKey;
}

- (void)updateHover:(NSEvent*)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int slot;
    BOOL onX;
    int a = [self partAt:p slot:&slot onX:&onX];
    if (a != self.hoverAction || slot != self.hoverSlot || onX != self.hoverX) {
        self.hoverAction = a;
        self.hoverSlot = slot;
        self.hoverX = onX;
        self.needsDisplay = YES;
        [self.owner hoverChanged];
    }
    if (a >= 0) [[NSCursor pointingHandCursor] set];
    else [[NSCursor arrowCursor] set];
}
- (void)mouseMoved:(NSEvent*)e { [self updateHover:e]; }
- (void)mouseEntered:(NSEvent*)e { [self updateHover:e]; }
- (void)mouseExited:(NSEvent*)e {
    self.hoverAction = self.hoverSlot = -1;
    self.hoverX = NO;
    [[NSCursor arrowCursor] set];
    self.needsDisplay = YES;
    [self.owner hoverChanged];
}

- (void)mouseDown:(NSEvent*)e {
    [self.window makeFirstResponder:self];
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int slot;
    BOOL onX;
    int a = [self partAt:p slot:&slot onX:&onX];
    if (a < 0) { [self.owner cancelCapture]; return; }
    if (onX || (e.modifierFlags & NSEventModifierFlagControl)) {
        if (slot >= 0) [self.owner clearAction:a slot:slot];
        else [self.owner showMenuForAction:a event:e view:self];
        return;
    }
    int mode = slot == kSlotKey0 ? kCapKey0 : slot == kSlotKey1 ? kCapKey1 : slot == kSlotPad ? kCapPad : kCapAny;
    if (self.st->capAction == a && self.st->capMode == mode) [self.owner cancelCapture];  // second click: cancel
    else [self.owner beginCapture:a mode:mode];
    [self updateHover:e];
}

- (void)rightMouseDown:(NSEvent*)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int slot;
    BOOL onX;
    int a = [self partAt:p slot:&slot onX:&onX];
    if (a < 0) return;
    if (slot >= 0) [self.owner clearAction:a slot:slot];
    else [self.owner showMenuForAction:a event:e view:self];
}

// ---- drawing

- (bool)lit:(int)a { return a >= 0 && self.st->act[a] > 0.5f; }
- (bool)hot:(int)a { return a >= 0 && (a == self.hoverAction || a == self.st->capAction); }

- (NSColor*)ringFor:(int)a width:(CGFloat*)w {
    const UIState& st = *self.st;
    if (a == st.capAction) {
        *w = 2.5;
        return [accent() colorWithAlphaComponent:0.55 + 0.45 * std::sin(st.t * 7)];
    }
    if (a == self.hoverAction) { *w = 2.2; return accent(); }
    if (has_conflict(st.m, a)) { *w = 2; return warn(); }
    *w = 1.2;
    return pal().outline;
}

- (void)drawRect:(NSRect)dirty {
    const Geo& g = [self geo];
    const UIState& st = *self.st;
    const Palette& P = pal();
    CGFloat s = g.s;

    // column headers
    NSFont* hf = [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold];
    NSColor* hc = NSColor.secondaryLabelColor;
    const char* heads[kSlotCount] = {"Key", "Alt key", "Controller"};
    for (CGFloat x : {g.colLx, g.colRx})
        for (int sl = 0; sl < kSlotCount; sl++)
            draw_centered(ns(heads[sl]), NSMakePoint(x + kChipX[sl] + kChipW[sl] / 2, kHeaderH / 2 + 2), hf, hc);

    // shoulder tabs (behind the body; ZL/ZR behind L/R)
    for (int pass = 0; pass < 2; pass++)
        for (auto& p : g.parts) {
            if (p.kind != kPartShoulder) continue;
            bool z = p.action == kZL || p.action == kZR;
            if (z != (pass == 0)) continue;
            NSBezierPath* b = [NSBezierPath bezierPathWithRoundedRect:p.rect xRadius:0.02 * s yRadius:0.02 * s];
            [([self lit:p.action] ? accent() : z ? P.well : P.button) setFill];
            [b fill];
            CGFloat w;
            NSColor* ring = [self ringFor:p.action width:&w];
            [ring setStroke];
            b.lineWidth = w;
            [b stroke];
        }
    // labels of the tabs: on their visible strip
    auto tabLabel = [&](int a, NSString* t) {
        for (auto& p : g.parts)
            if (p.kind == kPartShoulder && p.action == a)
                draw_fit(t, p.vis, std::max<CGFloat>(9, p.vis.size.height * 0.62), NSFontWeightBold,
                         [self lit:a] ? NSColor.whiteColor : P.buttonText);
    };

    // body
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [NSShadow new];
    sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.25];
    sh.shadowBlurRadius = 10;
    sh.shadowOffset = NSMakeSize(0, -3);
    [sh set];
    [P.body setFill];
    [g.body fill];
    [NSGraphicsContext restoreGraphicsState];
    [P.outline setStroke];
    g.body.lineWidth = 1.5;
    [g.body stroke];
    tabLabel(kZL, @"ZL");
    tabLabel(kZR, @"ZR");
    tabLabel(kL, @"L");
    tabLabel(kR, @"R");

    // GamePad screen: shows the hovered / captured input
    if (!NSIsEmptyRect(g.screen)) {
        NSBezierPath* b = [NSBezierPath bezierPathWithRoundedRect:g.screen xRadius:0.01 * s yRadius:0.01 * s];
        [P.screen setFill];
        [b fill];
        [P.outline setStroke];
        b.lineWidth = 1;
        [b stroke];
        int a = st.capAction >= 0 ? st.capAction : self.hoverAction;
        NSRect r = NSInsetRect(g.screen, 10, 10);
        if (a >= 0) {
            draw_fit(ns(action_label(a)), NSMakeRect(r.origin.x, NSMidY(r) - 0.06 * s, r.size.width, 0.05 * s), 0.036 * s,
                     NSFontWeightSemibold, P.screenText);
            NSString* sub = st.capAction >= 0 ? (st.capMode == kCapPad ? @"Press a controller button…"
                                                 : st.capMode == kCapAny ? @"Press a key or controller button…"
                                                                         : @"Press a key…")
                                              : [self bindingSummary:a];
            draw_fit(sub, NSMakeRect(r.origin.x, NSMidY(r) + 0.005 * s, r.size.width, 0.04 * s), 0.024 * s, NSFontWeightRegular,
                     st.capAction >= 0 ? accent() : [P.screenText colorWithAlphaComponent:0.75]);
        } else {
            draw_fit(@"Click a button to rebind it", NSMakeRect(r.origin.x, NSMidY(r) - 0.02 * s, r.size.width, 0.04 * s), 0.022 * s,
                     NSFontWeightRegular, [P.screenText colorWithAlphaComponent:0.45]);
        }
    }
    for (NSRect led : g.leds) {
        [(st.padConnected ? accent() : P.well) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:led] fill];
    }

    // leader lines (under the buttons)
    for (auto& gr : g.groups) {
        if (!gr.leader) continue;
        bool hot = false, warnLine = false;
        for (int a : gr.acts) {
            hot |= [self hot:a];
            warnLine |= has_conflict(st.m, a);
        }
        CGFloat y = gr.box.origin.y + 2 + kRowH / 2;
        CGFloat x0 = gr.right ? NSMinX(gr.box) : NSMaxX(gr.box);
        CGFloat x1 = gr.right ? g.colRx - 16 : g.colLx + kColW + 16;
        NSBezierPath* b = [NSBezierPath bezierPath];
        [b moveToPoint:NSMakePoint(x0, y)];
        [b lineToPoint:NSMakePoint(x1, y)];
        [b lineToPoint:gr.anchor];
        b.lineWidth = hot ? 2 : 1;
        [(hot ? accent() : warnLine ? [warn() colorWithAlphaComponent:0.7] : P.leader) setStroke];
        [b stroke];
        [(hot ? accent() : P.leader) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(gr.anchor.x - 2.5, gr.anchor.y - 2.5, 5, 5)] fill];
    }

    NSFont* bf = [NSFont systemFontOfSize:std::max<CGFloat>(9, 0.024 * s) weight:NSFontWeightBold];
    for (auto& p : g.parts) {
        CGFloat w;
        switch (p.kind) {
        case kPartRound: {
            NSBezierPath* b = [NSBezierPath bezierPathWithOvalInRect:p.rect];
            [([self lit:p.action] ? accent() : P.button) setFill];
            [b fill];
            [[self ringFor:p.action width:&w] setStroke];
            b.lineWidth = w;
            [b stroke];
            NSFont* f = p.action == kHome || p.action == kPlus || p.action == kMinus
                            ? [NSFont systemFontOfSize:std::max<CGFloat>(8, 0.021 * s) weight:NSFontWeightBold]
                            : bf;
            draw_centered(p.text, p.c, f, [self lit:p.action] ? NSColor.whiteColor : P.buttonText);
            break;
        }
        case kPartDpad: break;  // drawn as one cross below
        case kPartStick: {
            // well
            NSBezierPath* well = [NSBezierPath bezierPathWithOvalInRect:p.rect];
            [P.well setFill];
            [well fill];
            bool dirHot = false;
            for (int k = 0; k < 4; k++) dirHot |= [self hot:p.dirs[k]];
            bool conflict = false;
            for (int k = 0; k < 4; k++) conflict |= has_conflict(st.m, p.dirs[k]);
            [(dirHot ? accent() : conflict ? warn() : P.outline) setStroke];
            well.lineWidth = dirHot || conflict ? 2 : 1.2;
            [well stroke];
            // direction arrows inside the well: up, down, left, right
            const CGFloat ang[4] = {-M_PI_2, M_PI_2, M_PI, 0};
            for (int k = 0; k < 4; k++) {
                int a = p.dirs[k];
                CGFloat r0 = p.R * 0.83, sz = p.R * 0.14;
                NSPoint c = NSMakePoint(p.c.x + std::cos(ang[k]) * r0, p.c.y + std::sin(ang[k]) * r0);
                NSBezierPath* t = [NSBezierPath bezierPath];
                [t moveToPoint:NSMakePoint(c.x + std::cos(ang[k]) * sz, c.y + std::sin(ang[k]) * sz)];
                [t lineToPoint:NSMakePoint(c.x + std::cos(ang[k] + 2.3) * sz, c.y + std::sin(ang[k] + 2.3) * sz)];
                [t lineToPoint:NSMakePoint(c.x + std::cos(ang[k] - 2.3) * sz, c.y + std::sin(ang[k] - 2.3) * sz)];
                [t closePath];
                NSColor* col = [self lit:a] ? accent()
                               : a == st.capAction ? [accent() colorWithAlphaComponent:0.55 + 0.45 * std::sin(st.t * 7)]
                               : a == self.hoverAction ? accent()
                               : has_conflict(st.m, a) ? warn()
                                                       : P.outline;
                [col setFill];
                [t fill];
            }
            // cap, moved by the stick's deflection
            float dx = st.stick[p.stick][0], dy = -st.stick[p.stick][1];
            float len = std::hypot(dx, dy);
            if (len > 1) { dx /= len; dy /= len; }
            CGFloat travel = p.R - p.capR;
            NSPoint cc = NSMakePoint(p.c.x + dx * travel, p.c.y + dy * travel);
            if (len > 0.02f) {
                NSBezierPath* v = [NSBezierPath bezierPath];
                [v moveToPoint:p.c];
                [v lineToPoint:cc];
                v.lineWidth = 2;
                [accent() setStroke];
                [v stroke];
            }
            NSBezierPath* cap = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cc.x - p.capR, cc.y - p.capR, 2 * p.capR, 2 * p.capR)];
            [([self lit:p.action] ? accent() : P.button) setFill];
            [cap fill];
            [[self ringFor:p.action width:&w] setStroke];
            cap.lineWidth = w;
            [cap stroke];
            NSBezierPath* inner = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cc.x - p.capR * 0.7, cc.y - p.capR * 0.7,
                                                                                     1.4 * p.capR, 1.4 * p.capR)];
            [[P.outline colorWithAlphaComponent:0.45] setStroke];
            inner.lineWidth = 1;
            [inner stroke];
            // the moving dot
            CGFloat dr = std::max<CGFloat>(2.5, p.capR * 0.16);
            [(len > 0.02f ? accent() : [P.outline colorWithAlphaComponent:0.8]) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cc.x - dr, cc.y - dr, 2 * dr, 2 * dr)] fill];
            draw_centered(p.text, NSMakePoint(cc.x, cc.y - p.capR * 0.42),
                          [NSFont systemFontOfSize:std::max<CGFloat>(8, p.capR * 0.34) weight:NSFontWeightBold],
                          [self lit:p.action] ? NSColor.whiteColor : P.buttonText);
            break;
        }
        case kPartShoulder: break;
        }
    }
    // d-pad cross
    {
        NSBezierPath* cross = [NSBezierPath bezierPath];
        NSRect hull = NSZeroRect;
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) hull = NSIsEmptyRect(hull) ? p.rect : NSUnionRect(hull, p.rect);
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) {
                NSPoint c = p.c;
                CGFloat W = std::min(p.rect.size.width, p.rect.size.height);
                (void)c;
                [cross appendBezierPathWithRoundedRect:p.rect xRadius:W * 0.18 yRadius:W * 0.18];
            }
        NSPoint c = NSMakePoint(NSMidX(hull), NSMidY(hull));
        CGFloat W = 0;
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) W = std::min(p.rect.size.width, p.rect.size.height);
        [cross appendBezierPathWithRect:NSMakeRect(c.x - W / 2, c.y - W / 2, W, W)];
        cross.windingRule = NSWindingRuleNonZero;
        [P.button setFill];
        [cross fill];
        CGFloat w;
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) {
                NSBezierPath* arm = [NSBezierPath bezierPathWithRoundedRect:p.rect xRadius:W * 0.18 yRadius:W * 0.18];
                if ([self lit:p.action]) {
                    [accent() setFill];
                    [arm fill];
                }
                NSColor* ring = [self ringFor:p.action width:&w];
                if (ring != P.outline) {
                    [ring setStroke];
                    arm.lineWidth = w;
                    [arm stroke];
                }
                // arrow
                int k = p.dirs[0];
                const CGFloat ang[4] = {-M_PI_2, M_PI_2, M_PI, 0};
                NSPoint m = NSMakePoint(NSMidX(p.rect), NSMidY(p.rect));
                CGFloat sz = W * 0.22;
                NSBezierPath* t = [NSBezierPath bezierPath];
                [t moveToPoint:NSMakePoint(m.x + std::cos(ang[k]) * sz, m.y + std::sin(ang[k]) * sz)];
                [t lineToPoint:NSMakePoint(m.x + std::cos(ang[k] + 2.3) * sz, m.y + std::sin(ang[k] + 2.3) * sz)];
                [t lineToPoint:NSMakePoint(m.x + std::cos(ang[k] - 2.3) * sz, m.y + std::sin(ang[k] - 2.3) * sz)];
                [t closePath];
                [([self lit:p.action] ? NSColor.whiteColor : P.outline) setFill];
                [t fill];
            }
        [P.outline setStroke];
        cross.lineWidth = 1.2;
        // outline only the outer edge: stroke the arms, then cover the inner seams with the centre square
        for (auto& p : g.parts)
            if (p.kind == kPartDpad && ![self lit:p.action] && [self ringFor:p.action width:&w] == P.outline) {
                NSBezierPath* arm = [NSBezierPath bezierPathWithRoundedRect:p.rect xRadius:W * 0.18 yRadius:W * 0.18];
                arm.lineWidth = 1.2;
                [arm stroke];
            }
        [P.button setFill];
        NSRectFill(NSMakeRect(c.x - W / 2 + 0.6, c.y - W / 2 - 1.5, W - 1.2, W + 3));
        NSRectFill(NSMakeRect(c.x - W / 2 - 1.5, c.y - W / 2 + 0.6, W + 3, W - 1.2));
    }

    // callouts
    for (auto& gr : g.groups) [self drawGroup:gr];
}

- (NSString*)bindingSummary:(int)a {
    const Mapping& m = self.st->m;
    NSMutableArray* parts = [NSMutableArray array];
    for (int k : m.keys[a])
        if (k != kNoKey) [parts addObject:ns(key_label(k))];
    if (m.pad[a] != kPadNone) [parts addObject:[NSString stringWithFormat:@"controller %@", ns(pad_label(m.pad[a]))]];
    return parts.count ? [parts componentsJoinedByString:@"  ·  "] : @"not bound";
}

- (void)drawGroup:(const Group&)gr {
    const UIState& st = *self.st;
    const Palette& P = pal();
    bool hot = false;
    for (int a : gr.acts) hot |= [self hot:a];
    NSBezierPath* box = [NSBezierPath bezierPathWithRoundedRect:gr.box xRadius:7 yRadius:7];
    [P.box setFill];
    [box fill];
    [(hot ? accent() : P.boxEdge) setStroke];
    box.lineWidth = hot ? 1.6 : 1;
    [box stroke];
    bool multiRow = gr.acts.size() > 1;
    if (multiRow)
        draw_fit(gr.title, NSMakeRect(gr.box.origin.x + 8, gr.box.origin.y + 2, kColW - 16, kRowH), 11.5, NSFontWeightSemibold,
                 NSColor.labelColor);
    for (size_t k = 0; k < gr.acts.size(); k++) {
        int a = gr.acts[k];
        NSRect row = gr.rows[k];
        if (a == self.hoverAction || a == st.capAction) {
            NSBezierPath* hl = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(row, 3, 0.5) xRadius:5 yRadius:5];
            [[accent() colorWithAlphaComponent:0.12] setFill];
            [hl fill];
        }
        NSRect lr = NSMakeRect(row.origin.x + 6, row.origin.y, kChipX[0] - 10, kRowH);
        bool lit = [self lit:a];
        if (lit) {
            NSBezierPath* dot = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(lr, 1, 3) xRadius:5 yRadius:5];
            [accent() setFill];
            [dot fill];
        }
        draw_fit(gr.rowLabels[k], lr, multiRow ? 12 : 12.5, NSFontWeightBold, lit ? NSColor.whiteColor : NSColor.labelColor);
        for (int sl = 0; sl < kSlotCount; sl++)
            [self drawChip:a slot:sl rect:NSMakeRect(row.origin.x + kChipX[sl], row.origin.y + 2.5, kChipW[sl], kRowH - 5)];
    }
}

- (void)drawChip:(int)a slot:(int)sl rect:(NSRect)r {
    const UIState& st = *self.st;
    const Mapping& m = st.m;
    const Palette& P = pal();
    bool isPad = sl == kSlotPad;
    int code = isPad ? m.pad[a] : m.keys[a][sl];
    bool bound = isPad ? code != kPadNone : code != kNoKey;
    bool capturing = st.capAction == a && (st.capMode == kCapAny ? sl != kSlotKey1 : st.capMode == sl + 1);
    bool held = bound && (isPad ? st.pad[code] > 0.5f : st.keys[code]);
    bool conflict = bound && !(isPad ? pad_users(m, code, a) : key_users(m, code, a)).empty();
    bool hovered = a == self.hoverAction && sl == self.hoverSlot;
    // the same key / controller input as the hovered chip: show where else it is used
    bool twin = false;
    if (bound && self.hoverAction >= 0 && self.hoverSlot >= 0 && !hovered && (self.hoverSlot == kSlotPad) == isPad) {
        int hc = self.hoverSlot == kSlotPad ? m.pad[self.hoverAction] : m.keys[self.hoverAction][self.hoverSlot];
        twin = hc == code;
    }
    CGFloat rad = isPad ? r.size.height / 2 : 4;
    NSBezierPath* b = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    if (!bound && !capturing) {
        CGFloat dash[2] = {3, 2};
        [b setLineDash:dash count:2 phase:0];
        [(hovered ? accent() : P.boxEdge) setStroke];
        b.lineWidth = 1;
        [b stroke];
        draw_fit(hovered ? @"+ add" : @"—", r, 11, NSFontWeightRegular, hovered ? accent() : NSColor.tertiaryLabelColor);
        return;
    }
    if (!isPad && !held) {  // keycap: a darker bottom edge
        NSBezierPath* edge = [NSBezierPath bezierPathWithRoundedRect:NSOffsetRect(r, 0, 1.5) xRadius:rad yRadius:rad];
        [P.keyEdge setFill];
        [edge fill];
    }
    [(held ? accent() : isPad ? P.padChip : P.keycap) setFill];
    [b fill];
    NSColor* stroke = capturing ? [accent() colorWithAlphaComponent:0.55 + 0.45 * std::sin(st.t * 7)]
                      : hovered || twin ? accent()
                      : conflict ? warn()
                      : isPad ? P.padEdge
                              : P.keyEdge;
    [stroke setStroke];
    b.lineWidth = capturing || hovered || twin || conflict ? 1.8 : 1;
    [b stroke];
    NSString* text = capturing ? @"press…" : isPad ? ns(pad_short_label(code)) : ns(key_short_label(code));
    if (conflict && !capturing) text = [@"⚠︎ " stringByAppendingString:text];
    NSColor* tc = held ? NSColor.whiteColor : capturing ? accent() : conflict ? warn() : NSColor.labelColor;
    draw_fit(text, NSInsetRect(r, 3, 0), 11, NSFontWeightMedium, tc);
    if (hovered && bound && !capturing) {  // × to clear
        NSRect xr = x_rect(r);
        NSBezierPath* xc = [NSBezierPath bezierPathWithOvalInRect:xr];
        [(self.hoverX ? NSColor.systemRedColor : NSColor.secondaryLabelColor) setFill];
        [xc fill];
        draw_centered(@"×", NSMakePoint(NSMidX(xr), NSMidY(xr) - 0.5), [NSFont systemFontOfSize:10 weight:NSFontWeightBold],
                      NSColor.windowBackgroundColor);
    }
}

@end

// ================================================================= window controller

@implementation WWControls {
    UIState _st;
    uint32_t _gen;
    bool _localKeys[256];  // keys held while this window has the keyboard (not seen by the game)
    float _padBase[kPadCount];
    NSString* _message;
    int _messageLevel;
    double _messageTime;
    bool _freezeView;  // snapshot in progress: don't follow the Input menu
    NSString* _lastPadInfo;
}

- (instancetype)init {
    if (!(self = [super init])) return nil;
    _st.m = current();
    _st.pro = input::pro_controller();
    _gen = generation();
    memset(_localKeys, 0, sizeof _localKeys);
    [self build];
    return self;
}

static NSTextField* label(NSString* s) {
    NSTextField* t = [NSTextField labelWithString:s];
    t.translatesAutoresizingMaskIntoConstraints = NO;
    return t;
}

- (void)build {
    WWControlsWindow* w = [[WWControlsWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1060, 610)
                                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                                        NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                                backing:NSBackingStoreBuffered
                                                                  defer:NO];
    w.title = @"Controls";
    w.controls = self;
    w.delegate = self;
    w.releasedWhenClosed = NO;
    w.contentMinSize = NSMakeSize(960, 570);
    // remembered window frame (NSUserDefaults); not in portable mode, which keeps nothing outside its folder
    if (!getenv("WWHD_NO_HOST_INPUT") && !host::portable()) w.frameAutosaveName = @"WWHDControls2";  // test runs: always the default size
    self.window = w;
    WWRootView* root = [[WWRootView alloc] init];
    w.contentView = root;

    NSSegmentedControl* which = [NSSegmentedControl segmentedControlWithLabels:@[@"Wii U GamePad", @"Wii U Pro Controller"]
                                                                  trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                        target:self
                                                                        action:@selector(whichChanged:)];
    which.toolTip = @"Which Wii U controller the keyboard and your controllers act as (same as the Input menu). "
                    @"Both use the same mapping.";
    self.which = which;
    NSSegmentedControl* fl = [NSSegmentedControl segmentedControlWithLabels:@[@"Face by position", @"Face by label (Xbox)", @"Face automatic"]
                                                                trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                      target:self
                                                                      action:@selector(faceLayoutChanged:)];
    fl.toolTip = @"Which host face buttons drive the Wii U's A/B/X/Y (issue #78). By position: the bottom "
                 @"button is B (Nintendo layout). By label: the button named A is A — on an Xbox pad that "
                 @"makes A accept/act and B go back. Automatic reads the labels printed on the pad that was "
                 @"plugged in first. Only these four bindings are rewritten.";
    self.faceLayout = fl;
    NSTextField* pi = label(@"");
    pi.textColor = NSColor.secondaryLabelColor;
    pi.alignment = NSTextAlignmentRight;
    pi.lineBreakMode = NSLineBreakByTruncatingTail;
    self.padInfo = pi;
    [pi setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    WWPadView* pv = [[WWPadView alloc] init];
    pv.translatesAutoresizingMaskIntoConstraints = NO;
    pv.owner = self;
    pv.st = &_st;
    pv.hoverAction = pv.hoverSlot = -1;
    self.padView = pv;

    NSBox* sep = [[NSBox alloc] init];
    sep.boxType = NSBoxSeparator;

    NSTextField* st = label(@" ");
    st.lineBreakMode = NSLineBreakByTruncatingTail;
    st.maximumNumberOfLines = 1;
    st.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
    [st setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    self.status = st;

    NSButton* reset = [NSButton buttonWithTitle:@"Reset to Defaults…" target:self action:@selector(resetAll:)];
    NSTextField* dzl = label(@"Stick dead zone:");
    NSSlider* dz = [NSSlider sliderWithValue:0 minValue:0 maxValue:0.5 target:self action:@selector(deadzoneChanged:)];
    dz.continuous = YES;
    dz.numberOfTickMarks = 11;
    self.deadzone = dz;
    NSTextField* dzv = label(@"0%");
    dzv.alignment = NSTextAlignmentRight;
    self.deadzoneValue = dzv;
    NSButton* inv = [NSButton checkboxWithTitle:@"Invert camera up/down" target:self action:@selector(invertChanged:)];
    self.invertY = inv;
    for (NSView* v in @[which, fl, pi, pv, sep, st, reset, dzl, dz, dzv, inv]) {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:v];
    }

    NSDictionary* v = NSDictionaryOfVariableBindings(which, fl, pi, pv, sep, st, reset, dzl, dz, dzv, inv);
    auto C = [&](NSString* f, NSLayoutFormatOptions o = 0) {
        [root addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:f options:o metrics:nil views:v]];
    };
    C(@"H:|-14-[which]-(>=12)-[fl]-12-[pi]-14-|", NSLayoutFormatAlignAllCenterY);
    C(@"H:|-0-[pv]-0-|");
    C(@"H:|-0-[sep]-0-|");
    C(@"H:|-16-[st]-16-|");
    C(@"H:|-16-[reset]-(>=24)-[dzl]-8-[dz(160)]-6-[dzv(40)]-24-[inv]-16-|", NSLayoutFormatAlignAllCenterY);
    C(@"V:|-12-[which]-6-[pv(>=400)]-0-[sep]-8-[st]-8-[reset]-14-|");
    [w center];
    [self syncOptions];
    [self syncWhich];
    w.initialFirstResponder = pv;
    [self refreshStatus];
}

// ---- model <-> view

- (void)reloadFromModel {
    if (generation() != _gen) {
        _st.m = current();
        _gen = generation();
    }
    [self syncOptions];
    self.padView.needsDisplay = YES;
}

- (void)syncOptions {
    self.deadzone.doubleValue = _st.m.deadzone;
    self.deadzoneValue.stringValue = [NSString stringWithFormat:@"%.0f%%", _st.m.deadzone * 100];
    self.invertY.state = _st.m.invert_camera_y ? NSControlStateValueOn : NSControlStateValueOff;
    // kCustom: no segment chosen (as the overlay's "(custom)"); any segment then applies its preset.
    // Automatic is segment 2 whatever the bindings resolved to (as the overlay's "(-> ...)").
    if (_st.m.face_auto) self.faceLayout.selectedSegment = 2;
    else {
        FaceLayout fl = face_layout(_st.m);
        self.faceLayout.selectedSegment = fl == FaceLayout::kCustom ? -1 : fl == FaceLayout::kLabels ? 1 : 0;
    }
}

- (void)syncWhich {
    self.which.selectedSegment = _st.pro ? 1 : 0;
    [self.padView invalidateGeo];
}

- (void)commit {
    set_current(_st.m);  // live: the next game read uses it; saved to controls.json
    _gen = generation();
    self.padView.needsDisplay = YES;
}

- (void)setStatus:(NSString*)s level:(int)level {  // 0 info, 1 warning, 2 refused
    _message = s;
    _messageLevel = level;
    _messageTime = CACurrentMediaTime();
    [self refreshStatus];
}

// status line: capture prompt > recent message > hover > conflicts / help
- (void)refreshStatus {
    NSString* s;
    int level = 0;
    WWPadView* pv = self.padView;
    double now = CACurrentMediaTime();
    bool recent = _message && now - _messageTime < (_messageLevel ? 8 : 4);
    if (_st.capAction >= 0 && !(recent && _messageLevel == 2)) {
        NSString* what = _st.capMode == kCapPad   ? @"press a controller button or move a stick"
                         : _st.capMode == kCapAny ? @"press a key or a controller button"
                                                  : (_st.capMode == kCapKey1 ? @"press the alternate key" : @"press a key");
        s = [NSString stringWithFormat:@"%@: %@ (Esc cancels).", ns(action_label(_st.capAction)), what];
        if (_st.capMode != kCapKey0 && _st.capMode != kCapKey1 && !_st.padConnected)
            s = [s stringByAppendingString:@" No game controller connected."];
        level = 3;
    } else if (recent) {
        s = _message;
        level = _messageLevel;
    } else if (pv.hoverAction >= 0) {
        int a = pv.hoverAction;
        if (pv.hoverSlot >= 0) {
            const char* slot = pv.hoverSlot == kSlotPad ? "controller" : pv.hoverSlot == kSlotKey1 ? "alternate key" : "key";
            bool bound = pv.hoverSlot == kSlotPad ? _st.m.pad[a] != kPadNone : _st.m.keys[a][pv.hoverSlot] != kNoKey;
            NSString* cur = !bound ? @"none" : pv.hoverSlot == kSlotPad ? ns(pad_label(_st.m.pad[a])) : ns(key_label(_st.m.keys[a][pv.hoverSlot]));
            s = [NSString stringWithFormat:@"%@ – %s: %@.  Click to change%@", ns(action_label(a)), slot, cur,
                                           bound ? @", right-click or × to clear." : @"."];
        } else {
            s = [NSString stringWithFormat:@"%@ – %@.  Click to rebind, right-click for more.", ns(action_label(a)),
                                           [pv bindingSummary:a]];
        }
    } else if (int n = conflict_count(_st.m)) {
        s = [NSString stringWithFormat:@"%d key%s or controller input%s bound to more than one button (orange) — they press all of them.",
                                       n, n == 1 ? "" : "s", n == 1 ? " is" : "s are"];
        level = 1;
    } else {
        s = @"Click a button on the controller to rebind it, or a key / controller field on the side. "
            @"Pressed inputs light up. Changes apply immediately.";
    }
    if (![self.status.stringValue isEqualToString:s]) self.status.stringValue = s;
    self.status.toolTip = s;
    self.status.textColor = level == 3 ? NSColor.controlAccentColor
                            : level == 2 ? NSColor.systemRedColor
                            : level == 1 ? NSColor.systemOrangeColor
                                         : NSColor.secondaryLabelColor;
}

- (void)hoverChanged { [self refreshStatus]; }

// ---- live state

- (void)startTimer {
    if (self.timer) return;
    self.timer = [NSTimer timerWithTimeInterval:1.0 / 60 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
}
- (void)stopTimer {
    [self.timer invalidate];
    self.timer = nil;
}

- (void)tick:(NSTimer*)t { [self poll]; }

- (void)poll {
    static const bool no_host = getenv("WWHD_NO_HOST_INPUT") != nullptr;
    bool dirty = false;
    if (generation() != _gen && _st.capAction < 0) {  // changed elsewhere (another window, reset)
        _st.m = current();
        _gen = generation();
        [self syncOptions];
        dirty = true;
    }
    if (!_freezeView && input::pro_controller() != _st.pro) {  // Input menu
        _st.pro = input::pro_controller();
        [self syncWhich];
    }
    bool keys[256];
    input::held_keys(keys);
    for (int i = 0; i < 256; i++) keys[i] = keys[i] || _localKeys[i];
    float v[kPadCount] = {};
    NSArray<GCController*>* pads = no_host ? @[] : GCController.controllers;
    if (!no_host) input::controller_values(v);
    _st.padConnected = pads.count > 0;
    // capture from a controller: a new press (held inputs count once released)
    if (_st.capAction >= 0 && (_st.capMode == kCapAny || _st.capMode == kCapPad)) {
        for (int p = 1; p < kPadCount; p++) {
            if (_padBase[p] > 0.5f) { _padBase[p] = v[p]; continue; }
            _padBase[p] = v[p];
            if (v[p] > 0.6f) {
                [self assignPad:p];
                break;
            }
        }
    }
    // pressed state per action, stick deflection (as the game sees it, without camera inversion)
    float act[kActionCount];
    for (int a = 0; a < kActionCount; a++) {
        act[a] = 0;
        for (int k : _st.m.keys[a])
            if (k != kNoKey && keys[k]) act[a] = 1;
        int p = _st.m.pad[a];
        if (p != kPadNone) act[a] = std::max(act[a], v[p]);
    }
    Mapping m = _st.m;
    m.invert_camera_y = false;
    input::PadState ks = keyboard_state(m, keys), cs = controller_state(m, v);
    float stick[2][2] = {{ks.lx || ks.ly ? ks.lx : cs.lx, ks.lx || ks.ly ? ks.ly : cs.ly},
                         {ks.rx || ks.ry ? ks.rx : cs.rx, ks.rx || ks.ry ? ks.ry : cs.ry}};
    if (memcmp(act, _st.act, sizeof act) || memcmp(stick, _st.stick, sizeof stick) || memcmp(keys, _st.keys, sizeof keys) ||
        memcmp(v, _st.pad, sizeof v))
        dirty = true;
    memcpy(_st.act, act, sizeof act);
    memcpy(_st.stick, stick, sizeof stick);
    memcpy(_st.keys, keys, sizeof keys);
    memcpy(_st.pad, v, sizeof v);
    _st.t = CACurrentMediaTime();
    if (_st.capAction >= 0) dirty = true;  // pulse
    if (dirty) self.padView.needsDisplay = YES;

    NSString* info;
    if (no_host) info = @"Host controllers ignored (test run)";
    else if (!pads.count) info = @"No game controller connected — keyboard only";
    else {
        NSMutableArray* names = [NSMutableArray array];
        for (GCController* c in pads) [names addObject:c.vendorName ?: @"Controller"];
        info = [NSString stringWithFormat:@"Connected: %@", [names componentsJoinedByString:@", "]];
    }
    if (![info isEqualToString:_lastPadInfo]) {
        _lastPadInfo = info;
        self.padInfo.stringValue = info;
    }
    static int n = 0;
    if (++n % 15 == 0) [self refreshStatus];  // let messages time out
}

// ---- capture

- (void)beginCapture:(int)a mode:(int)mode {
    _st.capAction = a;
    _st.capMode = mode;
    _message = nil;
    if (mode == kCapAny || mode == kCapPad) input::controller_values(_padBase);  // held now: not until released
    [self refreshStatus];
    self.padView.needsDisplay = YES;
}

- (void)cancelCapture {
    if (_st.capAction < 0) return;
    _st.capAction = -1;
    [self refreshStatus];
    self.padView.needsDisplay = YES;
}

static bool modifier_down(uint16_t code, NSEventModifierFlags f, bool* known) {
    static const NSEventModifierFlags bits[] = {0x02, 0x04, 0x01, 0x2000, 0x20, 0x40};
    static const uint16_t codes[] = {kVK_Shift, kVK_RightShift, kVK_Control, kVK_RightControl, kVK_Option, kVK_RightOption};
    for (int i = 0; i < 6; i++)
        if (codes[i] == code) {
            *known = true;
            return (f & bits[i]) != 0;
        }
    *known = code == kVK_CapsLock;
    return code == kVK_CapsLock && (f & NSEventModifierFlagCapsLock);
}

- (BOOL)handleKeyEvent:(NSEvent*)e {
    if (e.type == NSEventTypeKeyDown && (e.modifierFlags & NSEventModifierFlagCommand) && e.keyCode == kVK_ANSI_W) {
        [self.window performClose:nil];  // Cmd-W, before the Window menu sees it
        return YES;
    }
    uint16_t code = e.keyCode & 0xFF;
    bool known = false, down = false;
    if (e.type == NSEventTypeFlagsChanged) down = modifier_down(code, e.modifierFlags, &known);
    // live display of held keys
    if (e.type == NSEventTypeKeyDown) _localKeys[code] = true;
    else if (e.type == NSEventTypeKeyUp) _localKeys[code] = false;
    else if (known) _localKeys[code] = down;
    if (e.modifierFlags & NSEventModifierFlagCommand) return NO;  // menu shortcuts keep working
    if (_st.capAction < 0) {
        // Esc with nothing to cancel: hand the keyboard back to the game
        if (e.type == NSEventTypeKeyDown && code == kVK_Escape && !e.isARepeat) [self.window performClose:nil];
        return e.type != NSEventTypeFlagsChanged;  // swallow (no beeps); the slider etc. use the mouse
    }
    if (e.type == NSEventTypeKeyUp) return YES;
    if (e.type == NSEventTypeFlagsChanged) {
        if (!known || !down || code == kVK_CapsLock) return YES;  // modifiers bind on press; not Caps Lock (it toggles)
    } else if (e.isARepeat) {
        return YES;
    }
    if (code == kVK_Escape) {
        [self cancelCapture];
        [self setStatus:@"Cancelled." level:0];
        return YES;
    }
    if (_st.capMode == kCapPad) {
        [self setStatus:@"Waiting for a controller button — click a key field to bind a key (Esc cancels)." level:1];
        return YES;
    }
    [self assignKey:code];
    return YES;
}

- (void)assignKey:(int)code {
    int a = _st.capAction, slot = _st.capMode == kCapKey1 ? 1 : 0;
    NSString* name = ns(key_label(code));
    if (const char* why = reserved_key(code)) {
        [self setStatus:[NSString stringWithFormat:@"“%@” is an app shortcut (%@) and can’t be used for game input. "
                                                   @"Press another key, or Esc.", name, ns(why)]
                  level:2];
        NSBeep();
        return;  // still waiting
    }
    _st.m.keys[a][slot] = code;
    if (_st.m.keys[a][slot ^ 1] == code) _st.m.keys[a][slot ^ 1] = kNoKey;  // same key twice on one input
    _st.capAction = -1;
    [self commit];
    auto others = key_users(_st.m, code, a);
    if (others.empty())
        [self setStatus:[NSString stringWithFormat:@"%@ → %@", ns(action_label(a)), name] level:0];
    else
        [self setStatus:[NSString stringWithFormat:@"“%@” is also bound to %@ — that key now presses both. "
                                                   @"Clear one of them if that’s not what you want.", name, action_list(others)]
                  level:1];
    LOG("[controls] %s key %d = %s", action_id(a), slot, key_id(code).c_str());
}

- (void)assignPad:(int)p {
    int a = _st.capAction;
    set_pad_binding(_st.m, a, p);  // a hand-edited face binding drops the Automatic preset
    _st.capAction = -1;
    [self commit];
    auto others = pad_users(_st.m, p, a);
    if (others.empty())
        [self setStatus:[NSString stringWithFormat:@"%@ → %@", ns(action_label(a)), ns(pad_label(p))] level:0];
    else
        [self setStatus:[NSString stringWithFormat:@"“%@” is also bound to %@ — it now presses both.", ns(pad_label(p)),
                                                   action_list(others)]
                  level:1];
    LOG("[controls] %s pad = %s", action_id(a), pad_id(p));
}

// ---- clearing / reset

- (void)clearAction:(int)a slot:(int)slot {
    if (_st.capAction == a) _st.capAction = -1;
    if (slot == kSlotPad) set_pad_binding(_st.m, a, kPadNone);
    else _st.m.keys[a][slot] = kNoKey;
    [self commit];
    [self setStatus:[NSString stringWithFormat:@"Cleared %@ (%s).", ns(action_label(a)),
                                               slot == kSlotPad ? "controller" : slot ? "alternate key" : "key"]
              level:0];
}

- (void)menuAction:(NSMenuItem*)it {
    int a = (int)(it.tag >> 4), what = (int)(it.tag & 15);
    Mapping d = Mapping::defaults();
    switch (what) {
    case 0: [self beginCapture:a mode:kCapAny]; return;
    case 1: _st.m.keys[a].fill(kNoKey); break;
    case 2: set_pad_binding(_st.m, a, kPadNone); break;
    case 3: _st.m.keys[a].fill(kNoKey); set_pad_binding(_st.m, a, kPadNone); break;
    case 4: _st.m.keys[a] = d.keys[a]; set_pad_binding(_st.m, a, d.pad[a]); break;
    }
    if (_st.capAction == a) _st.capAction = -1;
    [self commit];
    static const char* done[] = {"", "Cleared the keys of", "Cleared the controller input of", "Cleared", "Reset"};
    [self setStatus:[NSString stringWithFormat:@"%s %@.", done[what], ns(action_label(a))] level:0];
}

- (void)showMenuForAction:(int)a event:(NSEvent*)e view:(NSView*)v {
    NSMenu* m = [[NSMenu alloc] initWithTitle:@""];
    m.autoenablesItems = NO;
    NSMenuItem* head = [m addItemWithTitle:ns(action_label(a)) action:nil keyEquivalent:@""];
    head.enabled = NO;
    const char* titles[] = {"Rebind…", "Clear Keys", "Clear Controller Input", "Clear All", "Reset to Default"};
    for (int i = 0; i < 5; i++) {
        if (i == 4) [m addItem:[NSMenuItem separatorItem]];
        NSMenuItem* it = [m addItemWithTitle:ns(titles[i]) action:@selector(menuAction:) keyEquivalent:@""];
        it.target = self;
        it.tag = (a << 4) | i;
    }
    [NSMenu popUpContextMenu:m withEvent:e forView:v];
}

- (void)resetAll:(id)sender {
    [self cancelCapture];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Reset all controls to their defaults?";
    alert.informativeText = @"Keyboard keys, controller buttons, the dead zone and camera inversion go back to the built-in layout.";
    [alert addButtonWithTitle:@"Reset"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        if (r == NSAlertFirstButtonReturn) [self resetNow];
    }];
}

- (void)resetNow {
    _st.m = Mapping::defaults();
    _st.capAction = -1;
    [self commit];
    [self syncOptions];
    [self setStatus:@"Controls reset to defaults." level:0];
}

- (void)deadzoneChanged:(NSSlider*)s {
    _st.m.deadzone = std::round(s.doubleValue * 100) / 100;
    self.deadzoneValue.stringValue = [NSString stringWithFormat:@"%.0f%%", _st.m.deadzone * 100];
    set_current(_st.m);
    _gen = generation();
}

- (void)invertChanged:(NSButton*)b {
    _st.m.invert_camera_y = b.state == NSControlStateValueOn;
    [self commit];
}

- (void)whichChanged:(NSSegmentedControl*)c {
    bool pro = c.selectedSegment == 1;
    // the same as Input > Wii U GamePad / Pro Controller
    input::set_pro_controller(pro);
    gfx::show_drc_window(!pro);
    _st.pro = pro;
    [self syncWhich];
    [self.window makeKeyWindow];  // showing the GamePad window must not take the keyboard from here
}

- (void)faceLayoutChanged:(NSSegmentedControl*)c {
    // the same as Input > Face buttons; rewrites the four face bindings only
    if (c.selectedSegment == 2) set_face_auto(_st.m, true);
    else apply_face_layout(_st.m, c.selectedSegment == 1 ? FaceLayout::kLabels : FaceLayout::kPosition);
    [self commit];
    [self.padView invalidateGeo];
    [self setStatus:_st.m.face_auto
                     ? [NSString stringWithFormat:@"Face buttons: automatic (%s).", face_layout_label(face_layout(_st.m))]
                     : [NSString stringWithFormat:@"Face buttons: %s.", face_layout_label(face_layout(_st.m))]
               level:0];
}

// ---- window

- (void)releaseLocalKeys { memset(_localKeys, 0, sizeof _localKeys); }

- (void)windowDidBecomeKey:(NSNotification*)n {
    input::release_keys();  // keys held for the game would stay down while this window has the keyboard
    memset(_localKeys, 0, sizeof _localKeys);
    [self reloadFromModel];
}
- (void)windowDidResignKey:(NSNotification*)n {
    [self cancelCapture];
    memset(_localKeys, 0, sizeof _localKeys);  // key-ups now go to the game window
}
- (void)windowWillClose:(NSNotification*)n {
    [self cancelCapture];
    [self stopTimer];
    // give the keyboard back to the game: the TV window (or any other) becomes key
    if (self.window.isKeyWindow)
        for (NSWindow* w in NSApp.orderedWindows)
            if (w != self.window && w.visible && w.canBecomeKeyWindow) {
                [w makeKeyWindow];
                break;
            }
}

- (void)show:(bool)takeFocus {
    [self reloadFromModel];
    [self startTimer];
    [self poll];
    const char* hidden = getenv("WWHD_HIDDEN_WINDOWS");
    if (hidden && *hidden && strcmp(hidden, "0")) return;  // test runs: never on screen (snapshots still work)
    if (takeFocus) [self.window makeKeyAndOrderFront:nil];
    else [self.window orderFront:nil];
}

// ---- debug: render the window into PNGs (both controllers, light and dark)

- (void)snapshotTo:(NSString*)prefix {
    _freezeView = true;
    bool pro0 = _st.pro;
    NSAppearance* ap0 = self.window.appearance;
    for (int pro = 0; pro < 2; pro++)
        for (int dark = 0; dark < 2; dark++) {
            self.window.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
            _st.pro = pro;
            [self syncWhich];
            NSView* v = self.window.contentView;
            [v layoutSubtreeIfNeeded];
            [v displayIfNeeded];
            NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
            [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
            NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            NSString* path = [NSString stringWithFormat:@"%@_%s_%s.png", prefix, pro ? "pro" : "gamepad", dark ? "dark" : "light"];
            BOOL ok = [png writeToFile:path atomically:YES];
            LOG("[controls] snapshot %s%s", path.UTF8String, ok ? "" : " FAILED");
        }
    self.window.appearance = ap0;
    _st.pro = pro0;
    [self syncWhich];
    _freezeView = false;
}

@end

// ================================================================= debug self-test
// WWHD_CONTROLS_SELFTEST=1 drives the window with synthetic events (no focus needed) and logs the
// results; the window stays open with a sample state (swapped A/B, a conflict, a capture in progress).

static WWControls* g_controls;

static NSEvent* key_event(NSWindow* w, uint16_t code, NSString* chars) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
                        windowNumber:w.windowNumber context:nil characters:chars charactersIgnoringModifiers:chars
                           isARepeat:NO keyCode:code];
}

static void self_test() {
    if (!getenv("WWHD_CONTROLS")) {  // it rewrites the controls file: never the user's own
        LOG("[controls-test] needs WWHD_CONTROLS=<scratch file>");
        return;
    }
    WWControls* c = g_controls;
    NSWindow* w = c.window;
    int fails = 0;
    auto expect = [&](bool ok, const char* what) {
        LOG("[controls-test] %s: %s", ok ? "ok" : "FAILED", what);
        if (!ok) fails++;
    };
    // 1. A <- J (key field): J is B's key, so the binding works and a conflict warning shows
    [c beginCapture:kA mode:kCapKey0];
    [w sendEvent:key_event(w, kVK_ANSI_J, @"j")];
    expect(current().keys[kA][0] == kVK_ANSI_J, "A key 1 = J via a key press");
    expect([c.status.stringValue containsString:@"also bound to B"], "conflict warning names B");
    expect(has_conflict(current(), kA) && has_conflict(current(), kB), "A and B marked as conflicting");
    // 2. B <- K: A<->B swapped, no conflict left
    [c beginCapture:kB mode:kCapKey0];
    [w sendEvent:key_event(w, kVK_ANSI_K, @"k")];
    expect(current().keys[kB][0] == kVK_ANSI_K && conflict_count(current()) == 0, "B key 1 = K: A/B swapped, no conflict");
    // 3. an app hotkey is refused and capture continues
    [c beginCapture:kY mode:kCapKey1];
    [w sendEvent:key_event(w, kVK_ANSI_R, @"r")];
    expect(current().keys[kY][1] == kNoKey, "R (resolution hotkey) refused");
    expect([c.status.stringValue containsString:@"app shortcut"], "hotkey warning shown");
    // 4. Esc cancels
    [w sendEvent:key_event(w, kVK_Escape, @"\x1b")];
    expect(current().keys[kY][1] == kNoKey, "Esc cancels without binding");
    // 5. clicking the drawing waits for a key or a controller button: a key replaces the main key
    [c beginCapture:kHome mode:kCapAny];
    [w sendEvent:key_event(w, kVK_ANSI_G, @"g")];
    expect(current().keys[kHome][0] == kVK_ANSI_G, "Home (drawing click) = G");
    // 6. clear one slot (× / right-click)
    [c clearAction:kHome slot:kSlotKey0];
    expect(current().keys[kHome][0] == kNoKey, "clear Home's key");
    // 7. controller binding with a conflict
    [c beginCapture:kZR mode:kCapPad];
    [w sendEvent:key_event(w, kVK_ANSI_U, @"u")];
    expect(current().keys[kZR][0] == kVK_ANSI_C, "a key does not bind while waiting for a controller");
    [c assignPad:kPadY];
    expect(current().pad[kZR] == kPadY && has_conflict(current(), kX), "ZR pad = Y (X's), conflict");
    [c clearAction:kZR slot:kSlotPad];
    expect(current().pad[kZR] == kPadNone, "clear ZR's controller input");
    // 8. options
    c.invertY.state = NSControlStateValueOn;
    [c invertChanged:c.invertY];
    expect(current().invert_camera_y, "invert camera Y");
    // 9. the file on disk follows
    Mapping disk;
    expect(load_file(default_path(), disk) && disk == current(), "controls.json matches the live mapping");
    // 10. reset
    [c resetNow];
    expect(current() == Mapping::defaults(), "reset to defaults");
    // sample state for screenshots: A<->B swapped, a conflict, a capture in progress
    Mapping m = Mapping::defaults();
    std::swap(m.keys[kA], m.keys[kB]);
    std::swap(m.pad[kA], m.pad[kB]);
    m.keys[kZR][1] = kVK_ANSI_K;
    m.deadzone = 0.1f;
    set_current(m);
    [c reloadFromModel];
    [c beginCapture:kX mode:kCapAny];
    [c releaseLocalKeys];  // the synthetic key-downs never get key-ups
    LOG("[controls-test] done, %d failed; window id %ld", fails, (long)w.windowNumber);
}

namespace gfx {

void show_controls_window() {
    if (!g_controls) g_controls = [[WWControls alloc] init];
    [g_controls show:true];
}

bool controls_window_is_key() { return g_controls && g_controls.window.isKeyWindow; }

}  // namespace gfx

@interface WWControlsMenuTarget : NSObject
@end
@implementation WWControlsMenuTarget
- (void)open:(id)sender { gfx::show_controls_window(); }
@end

namespace gfx {

// the Input menu's "Controls…" item (menu.mm)
NSMenuItem* controls_menu_item() {
    static WWControlsMenuTarget* target = [WWControlsMenuTarget new];
    NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:@"Controls…" action:@selector(open:) keyEquivalent:@""];
    it.target = target;
    bool selftest = getenv("WWHD_CONTROLS_SELFTEST"), show = getenv("WWHD_SHOW_CONTROLS"), snap = getenv("WWHD_CONTROLS_SNAPSHOT");
    if (selftest || show || snap)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (!g_controls) g_controls = [[WWControls alloc] init];
            // a test run (WWHD_NO_HOST_INPUT) never takes the keyboard focus
            [g_controls show:!getenv("WWHD_NO_HOST_INPUT")];
            if (selftest) self_test();
            if (const char* h = getenv("WWHD_CONTROLS_HOVER")) {
                g_controls.padView.hoverAction = action_from_id(h);
                g_controls.padView.hoverSlot = -1;
                [g_controls hoverChanged];
            }
            if (const char* path = getenv("WWHD_CONTROLS_SNAPSHOT")) {
                double at = getenv("WWHD_CONTROLS_SNAPSHOT_AT") ? atof(getenv("WWHD_CONTROLS_SNAPSHOT_AT")) : 3;
                NSString* prefix = ns(path);
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(std::max(0.0, at - 1) * NSEC_PER_SEC)),
                               dispatch_get_main_queue(), ^{ [g_controls snapshotTo:prefix]; });
            }
        });
    return it;
}

}  // namespace gfx
