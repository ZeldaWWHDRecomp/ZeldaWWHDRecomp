// Run the native Controls self-test and synthetic mouse captures without showing a window/game.
#include "../src/gfx/controls_ui.mm"
#include "../src/gfx/menu_shortcuts.h"
#include <cassert>
#include <cstdarg>
#include <filesystem>
#include <unistd.h>
namespace gfx { void show_drc_window(bool){} }
namespace overlay { void external_binding_capture(bool){} }
static std::vector<input::ControllerInfo> fixtureControllers;
namespace input {
std::vector<ControllerInfo> controllers(){return fixtureControllers;}
bool pro_controller(){return false;}
void set_pro_controller(bool){}
void release_keys(){}
void held_keys(bool* v){std::fill(v,v+256,false);}
void controller_values(float* v){std::fill(v,v+input_map::kPadCount,0.f);}
}
// The isolated AppKit target uses camera-setting fixtures, like its input/window fixtures.
// Exercise the actual controls' routing without linking the game or writing player settings.
namespace {
bool cameraFlags[2]{};
float cameraSpeed = 1.f, mouseSensitivity = .15f, mouseFirstPerson = 1.f;
}
namespace mods {
float camera_speed() { return cameraSpeed; }
float mouse_sensitivity() { return mouseSensitivity; }
float mouse_first_person_scale() { return mouseFirstPerson; }
bool stickPitch = false;
bool camera_stick_pitch() { return stickPitch; }
void set_camera_stick_pitch(bool look) { stickPitch = look; }
int zoomHold = 1;
int camera_zoom_hold() { return zoomHold; }
void set_camera_zoom_hold(int choice) { zoomHold = choice; }
bool aimLeft = false;
bool aim_left_stick() { return aimLeft; }
int wheelMode = 2;  // Bindings, the default (mouse_wheel.h)
int mouse_wheel_mode() { return wheelMode; }
int wheelSmooth = 2;  // 0.3 s, the default
int wheel_zoom_smoothing() { return wheelSmooth; }
void set_wheel_zoom_smoothing(int c) { wheelSmooth = c; }
void set_aim_left_stick(bool left) { aimLeft = left; }
}
namespace game_settings {
void set_camera_speed(float v) { cameraSpeed = v; }
void set_mouse_sensitivity(float v) { mouseSensitivity = v; }
void set_mouse_first_person_scale(float v) { mouseFirstPerson = v; }
}
namespace mods::manager {
const Entry* find(std::string_view id) {
    static const Entry entries[] = {
        {"mouse-camera","Mouse camera","","","",[] { return cameraFlags[0]; },nullptr},
        {"direct-camera","Direct camera","","","",[] { return cameraFlags[1]; },nullptr}};
    for (const auto& entry : entries) if (id == entry.id) return &entry;
    return nullptr;
}
bool set_enabled(std::string_view id, bool enabled) {
    const char* ids[] = {"mouse-camera","direct-camera"};
    for (int i=0;i<2;++i) if (id == ids[i]) { cameraFlags[i]=enabled; return true; }
    return false;
}
bool wheelLocked = false;  // a save profile locks the first-person switch
bool set_mouse_wheel(int mode) { if (wheelLocked) return false; mods::wheelMode = mode; return true; }
}
// counts what the menu bar's key equivalents trigger (menu.mm's targets, without the game)
@interface MenuCounter : NSObject
@property int settings, shortcuts, lastTag;
@end
@implementation MenuCounter
- (void)openSettings:(NSMenuItem*)item { ++self.settings; }
- (void)activate:(NSMenuItem*)item { ++self.shortcuts; self.lastTag = (int)item.tag; }
@end
static NSEvent* command_key(unsigned short code, NSString* chars) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
        timestamp:0 windowNumber:0 context:nil characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:code];
}
// Cmd+, is the app menu's fixed Settings... key: kept by clear_host_keys, never a Shortcuts item, once per press.
static void menu_settings_key_test() {
    using namespace input_map;
    MenuCounter* counter = [MenuCounter new];
    NSMenu* bar = [NSMenu new];
    NSMenu* app = [NSMenu new];
    [bar addItemWithTitle:@"" action:nil keyEquivalent:@""].submenu = app;
    NSMenuItem* settings = menu_shortcuts::add_settings_item(app, counter, @selector(openSettings:));
    NSMenu* view = [NSMenu new];
    [view addItemWithTitle:@"Full Screen" action:@selector(fullScreen:) keyEquivalent:@"f"];
    [bar addItemWithTitle:@"View" action:nil keyEquivalent:@""].submenu = view;
    menu_shortcuts::clear_host_keys(bar);
    assert([settings.keyEquivalent isEqualToString:@","] && settings.keyEquivalentModifierMask == NSEventModifierFlagCommand);
    assert([view itemAtIndex:0].keyEquivalent.length == 0);  // remappable native keys still cleared
    // A player's own Cmd+, (Full screen, and the macOS Settings alternate of an unmigrated file) shows no equivalent.
    auto m = Mapping::defaults();
    m.shortcuts[kFullscreen].keys[0] = {kVK_ANSI_Comma, kModSuper};
    m.shortcuts[kSettings].keys = {{{kVK_F9}, {kVK_ANSI_Comma, kModSuper}}};
    NSMenu* shortcuts = [NSMenu new];
    [bar addItemWithTitle:@"Shortcuts" action:nil keyEquivalent:@""].submenu = shortcuts;
    menu_shortcuts::fill_shortcut_menu(shortcuts, m, counter, @selector(activate:));
    int fullscreenItems = 0;
    for (NSMenuItem* item in shortcuts.itemArray) {
        assert(!([item.keyEquivalent isEqualToString:@","] && (item.keyEquivalentModifierMask & NSEventModifierFlagCommand)));
        fullscreenItems += item.tag == kFullscreen;
    }
    assert(fullscreenItems == 2);  // still listed (and usable from the menu), only without the key
    // the defaults: Settings Esc (no menu equivalent: Esc stays with the game window) and F1
    menu_shortcuts::fill_shortcut_menu(shortcuts, Mapping::defaults(), counter, @selector(activate:));
    NSMenuItem* first = [shortcuts itemAtIndex:0];
    NSMenuItem* alternate = [shortcuts itemAtIndex:1];
    assert(first.tag == kSettings && first.keyEquivalent.length == 0);
    assert(alternate.tag == kSettings && [alternate.keyEquivalent characterAtIndex:0] == NSF1FunctionKey);
    menu_shortcuts::fill_shortcut_menu(shortcuts, m, counter, @selector(activate:));
    // Cmd+, fires Settings... exactly once per press, never the player's Cmd+, binding; again toggles back.
    assert([bar performKeyEquivalent:command_key(kVK_ANSI_Comma, @",")]);
    assert(counter.settings == 1 && counter.shortcuts == 0);
    assert([bar performKeyEquivalent:command_key(kVK_ANSI_Comma, @",")]);
    assert(counter.settings == 2 && counter.shortcuts == 0);
    assert(menu_shortcuts::settings_menu_key({kVK_ANSI_Comma, kModSuper}));
    assert(!menu_shortcuts::settings_menu_key({kVK_ANSI_Comma, 0}) && !menu_shortcuts::settings_menu_key({kVK_ANSI_Comma, kModSuper | kModShift}));
    // An old macOS controls file with the untouched F1 + Cmd+, migrates to Esc + F1 (Cmd+, is the menu's).
    Mapping old = Mapping::defaults();
    old.shortcuts[kSettings].keys = {{{kVK_F1}, {kVK_ANSI_Comma, kModSuper}}};
    std::string text = to_json(old);
    const std::string now = "\"shortcuts_version\": " + std::to_string(kShortcutsVersion);
    text.replace(text.find(now), now.size(), "\"shortcuts_version\": 2");
    Mapping loaded; assert(from_json(text, loaded));
    assert(loaded.shortcuts[kSettings].keys == shortcut_defaults()[kSettings].keys);
    assert(loaded.shortcuts[kSettings].keys[0].code == kVK_Escape && loaded.shortcuts[kSettings].keys[1].code == kVK_F1);
}
void log_msg(const char* fmt,...) {
    va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);fputc('\n',stderr);
}
int main() {
    @autoreleasepool {
        const auto path = std::filesystem::temp_directory_path()/ ("wwhd-controls-165-"+std::to_string(getpid())+".json");
        setenv("WWHD_CONTROLS",path.c_str(),1);setenv("WWHD_NO_HOST_INPUT","1",1);
        [NSApplication sharedApplication];
        input_map::set_current(input_map::Mapping::defaults(),false);
        g_controls = [[WWControls alloc] init];
        assert(self_test()==0);
        for (int i=0;i<5;++i) {
            [g_controls beginCapture:kA mode:kCapKey1];
            CGEventRef cg = CGEventCreateMouseEvent(nullptr, i==0?kCGEventLeftMouseDown:i==1?kCGEventRightMouseDown:kCGEventOtherMouseDown,CGPointZero,static_cast<CGMouseButton>(i));
            NSEvent* e = [NSEvent eventWithCGEvent:cg];
            assert(e.buttonNumber==i);
            [g_controls.window sendEvent:e];
            assert(current().keys[kA][1]==kMouse1+i);
            CFRelease(cg);
        }
        for (int delta : {1,-1}) {
            [g_controls beginCapture:kB mode:kCapAny];
            CGEventRef cg = CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,1,delta);
            [g_controls.window sendEvent:[NSEvent eventWithCGEvent:cg]];
            assert(current().keys[kB][0]==(delta>0?kWheelUp:kWheelDown));CFRelease(cg);
        }
        [g_controls beginCapture:kR mode:kCapPad];
        CGEventRef cg=CGEventCreateMouseEvent(nullptr,kCGEventLeftMouseDown,CGPointZero,kCGMouseButtonLeft);
        const auto before=current();[g_controls handleMouseEvent:[NSEvent eventWithCGEvent:cg]];
        assert(current()==before);CFRelease(cg);[g_controls cancelCapture];
        // Actual AppKit controls: apply, persisted restore, and one controller's own bindings.
        auto presetBefore = current();
        g_controls.faceLayout.selectedSegment=3;[g_controls faceLayoutChanged:g_controls.faceLayout];
        assert(current().preset.gamecube && current().preset.saved);
        Mapping loaded;assert(from_json(to_json(current()),loaded));assert(loaded.preset.previous==presetBefore.pad);
        g_controls.faceLayout.selectedSegment=4;[g_controls faceLayoutChanged:g_controls.faceLayout];
        assert(current()==presetBefore);
        input::ControllerInfo controller;controller.guid="gc-ui";controller.name="GameCube fixture";
        fixtureControllers={controller,controller};[g_controls syncOptions];
        assert(g_controls.bindingTarget.numberOfItems==2); // shared +one editable adapter profile
        [g_controls.bindingTarget selectItemAtIndex:1];[g_controls bindingTargetChanged:g_controls.bindingTarget];
        assert(g_controls.faceLayout.segmentCount==4);
        for(NSInteger i=0;i<g_controls.faceLayout.segmentCount;++i)
            assert(![[g_controls.faceLayout labelForSegment:i] containsString:@"automatic"]);
        g_controls.faceLayout.selectedSegment=2;[g_controls faceLayoutChanged:g_controls.faceLayout];
        assert(current().controllers.size()==1 && current().controllers[0].preset.gamecube);
        assert(current().pad==presetBefore.pad && !current().preset.gamecube);
        g_controls.faceLayout.selectedSegment=3;[g_controls faceLayoutChanged:g_controls.faceLayout];
        assert(current().controllers[0].pad==presetBefore.pad);
        [g_controls.bindingTarget selectItemAtIndex:0];[g_controls bindingTargetChanged:g_controls.bindingTarget];
        assert(g_controls.faceLayout.segmentCount==5 && [g_controls.faceLayout isEnabledForSegment:3]);
        input_map::set_current(presetBefore,false);
        // The window opens on the controller in use when it has bindings of its own (AYN Odin report: the
        // shared set was shown and edited, play used the Odin's own); the shared set then says so.
        {
            Mapping withOdin=presetBefore;ensure_profile(withOdin,"","Odin Controller");
            withOdin.controllers[0].pad[kZR]=kPadY;set_current(withOdin,false);
            input::ControllerInfo odin;odin.name="Odin Controller";fixtureControllers={odin};
            set_active_controller("","Odin Controller");
            [g_controls followActiveController];[g_controls reloadFromModel];
            assert(g_controls.bindingTarget.indexOfSelectedItem==1 && g_controls.bindingView.pad[kZR]==kPadY);
            assert(g_controls.sharedNotice.stringValue.length==0);
            // the user picks the shared set: the notice shows, and the pick stays while the Odin is in use
            [g_controls.bindingTarget selectItemAtIndex:0];[g_controls bindingTargetChanged:g_controls.bindingTarget];
            assert([g_controls.sharedNotice.stringValue isEqualToString:@"Odin Controller uses its own bindings, so changes here don't apply to it."]);
            [g_controls followActiveController];[g_controls reloadFromModel];
            assert(g_controls.bindingTarget.indexOfSelectedItem==0 && g_controls.bindingView.pad[kZR]==presetBefore.pad[kZR]);
            // a controller without bindings of its own in use: the shared set, no notice
            set_active_controller("","Xbox Controller");
            [g_controls followActiveController];[g_controls syncOptions];
            assert(g_controls.bindingTarget.indexOfSelectedItem==0 && g_controls.sharedNotice.stringValue.length==0);
            // the Odin again: its own
            set_active_controller("","Odin Controller");
            [g_controls followActiveController];[g_controls reloadFromModel];
            assert(g_controls.bindingTarget.indexOfSelectedItem==1);
            set_active_controller("","");fixtureControllers={};
            [g_controls.bindingTarget selectItemAtIndex:0];[g_controls bindingTargetChanged:g_controls.bindingTarget];
            input_map::set_current(presetBefore,false);[g_controls reloadFromModel];
        }
        assert(g_controls.cameraButtons.count==2);
        for (int i=0;i<2;++i) {
            NSButton* button = g_controls.cameraButtons[i];
            button.state = NSControlStateValueOn;
            [g_controls cameraChanged:button];
            for (int j=0;j<2;++j) assert(cameraFlags[j] == (j<=i));
        }
        // #696: Mouse wheel, Bindings by default (the old First-person shortcut switch's off); a locked switch stays
        assert(g_controls.mouseWheel.numberOfItems==3 && g_controls.mouseWheel.indexOfSelectedItem==2);
        // Wheel zoom smoothing: 0.3 s by default, only active with Zoom
        assert(g_controls.wheelSmoothing.numberOfItems==5 && g_controls.wheelSmoothing.indexOfSelectedItem==2 &&
               !g_controls.wheelSmoothing.enabled && [[g_controls.wheelSmoothing itemTitleAtIndex:0] isEqualToString:@"Off"]);
        assert([[g_controls.mouseWheel itemTitleAtIndex:0] isEqualToString:@"First person"] &&
               [[g_controls.mouseWheel itemTitleAtIndex:1] isEqualToString:@"Zoom"]);
        [g_controls.mouseWheel selectItemAtIndex:1];[g_controls mouseWheelChanged:g_controls.mouseWheel];
        assert(mods::wheelMode==1 && g_controls.wheelSmoothing.enabled);
        [g_controls.wheelSmoothing selectItemAtIndex:0];[g_controls wheelSmoothingChanged:g_controls.wheelSmoothing];
        assert(mods::wheelSmooth==0);
        mods::wheelSmooth=4;[g_controls syncOptions];assert(g_controls.wheelSmoothing.indexOfSelectedItem==4);
        mods::wheelSmooth=2;[g_controls syncOptions];
        mods::manager::wheelLocked=true;
        [g_controls.mouseWheel selectItemAtIndex:0];[g_controls mouseWheelChanged:g_controls.mouseWheel];
        assert(mods::wheelMode==1 && g_controls.mouseWheel.indexOfSelectedItem==1);
        mods::manager::wheelLocked=false;
        mods::wheelMode=0;[g_controls syncOptions];assert(g_controls.mouseWheel.indexOfSelectedItem==0);
        mods::wheelMode=2;[g_controls syncOptions];assert(g_controls.mouseWheel.indexOfSelectedItem==2 && !g_controls.wheelSmoothing.enabled);
        g_controls.cameraSpeed.floatValue=1.75f;
        [g_controls cameraSpeedChanged:g_controls.cameraSpeed];
        g_controls.mouseSensitivity.floatValue=.25f;
        [g_controls mouseSensitivityChanged:g_controls.mouseSensitivity];
        assert(cameraSpeed==1.75f && mouseSensitivity==.25f);
        cameraFlags[1]=false; cameraSpeed=1.25f; mouseSensitivity=.125f;
        [g_controls syncOptions];
        assert(g_controls.cameraButtons[1].state==NSControlStateValueOff);
        assert(g_controls.cameraSpeed.floatValue==1.25f && g_controls.mouseSensitivity.floatValue==.125f);
        // the sensitivity reaches 1 degree per point; First-person sensitivity 0.25x..4x, its own setter, synced back
        assert(g_controls.mouseSensitivity.minValue==mods::camera_mouse::kSensitivityMin && g_controls.mouseSensitivity.maxValue==1.0);
        assert(g_controls.mouseFirstPerson.minValue==.25 && g_controls.mouseFirstPerson.maxValue==4.0 && g_controls.mouseFirstPerson.floatValue==1.f);
        g_controls.mouseSensitivity.floatValue=.8f;[g_controls mouseSensitivityChanged:g_controls.mouseSensitivity];
        assert(mouseSensitivity==.8f);
        g_controls.mouseFirstPerson.floatValue=2.5f;[g_controls mouseFirstPersonChanged:g_controls.mouseFirstPerson];
        assert(mouseFirstPerson==2.5f && mouseSensitivity==.8f);
        mouseFirstPerson=.5f;[g_controls syncOptions];assert(g_controls.mouseFirstPerson.floatValue==.5f);
        // #661: Right stick up/down, Zoom (original) by default
        assert(g_controls.stickPitch.indexOfSelectedItem==0);
        [g_controls.stickPitch selectItemAtIndex:1];[g_controls stickPitchChanged:g_controls.stickPitch];
        assert(mods::stickPitch);
        mods::stickPitch=false;[g_controls syncOptions];
        assert(g_controls.stickPitch.indexOfSelectedItem==0);
        // #688: Zoom while holding, L by default, only active with Look up and down
        assert(g_controls.zoomHold.indexOfSelectedItem==1 && !g_controls.zoomHold.enabled);
        assert(g_controls.zoomHold.numberOfItems==3 && [[g_controls.zoomHold itemTitleAtIndex:2] isEqualToString:@"ZL (targeting)"]);
        [g_controls.stickPitch selectItemAtIndex:1];[g_controls stickPitchChanged:g_controls.stickPitch];
        assert(g_controls.zoomHold.enabled);
        [g_controls.zoomHold selectItemAtIndex:2];[g_controls zoomHoldChanged:g_controls.zoomHold];
        assert(mods::zoomHold==2);
        mods::zoomHold=0;[g_controls syncOptions];
        assert(g_controls.zoomHold.indexOfSelectedItem==0 && g_controls.zoomHold.enabled);
        [g_controls.stickPitch selectItemAtIndex:0];[g_controls stickPitchChanged:g_controls.stickPitch];
        assert(!mods::stickPitch && !g_controls.zoomHold.enabled);
        mods::zoomHold=1;
        // #666: First-person aiming, Right stick (HD) by default
        assert(g_controls.aimStick.indexOfSelectedItem==0&&g_controls.aimStick.numberOfItems==2);
        [g_controls.aimStick selectItemAtIndex:1];[g_controls aimStickChanged:g_controls.aimStick];
        assert(mods::aimLeft);
        [g_controls syncOptions];assert(g_controls.aimStick.indexOfSelectedItem==1);
        mods::aimLeft=false;[g_controls syncOptions];
        assert(g_controls.aimStick.indexOfSelectedItem==0);
        menu_settings_key_test();
        std::filesystem::remove(path);
        fprintf(stderr,"controls_ui_test: passed (incl. fixed Cmd+, Settings menu key)\n");
    }
}
