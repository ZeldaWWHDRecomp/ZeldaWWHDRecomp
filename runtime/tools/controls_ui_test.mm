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
        fixtureControllers={controller};[g_controls syncOptions];
        [g_controls.bindingTarget selectItemAtIndex:1];[g_controls bindingTargetChanged:g_controls.bindingTarget];
        g_controls.faceLayout.selectedSegment=3;[g_controls faceLayoutChanged:g_controls.faceLayout];
        assert(current().controllers.size()==1 && current().controllers[0].preset.gamecube);
        assert(current().pad==presetBefore.pad && !current().preset.gamecube);
        g_controls.faceLayout.selectedSegment=4;[g_controls faceLayoutChanged:g_controls.faceLayout];
        assert(current().controllers[0].pad==presetBefore.pad);
        [g_controls.bindingTarget selectItemAtIndex:0];[g_controls bindingTargetChanged:g_controls.bindingTarget];
        input_map::set_current(presetBefore,false);
        menu_settings_key_test();
        std::filesystem::remove(path);
        fprintf(stderr,"controls_ui_test: passed (incl. fixed Cmd+, Settings menu key)\n");
    }
}
