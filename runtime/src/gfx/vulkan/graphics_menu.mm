// Vulkan's main-thread Cocoa menu. Rendering settings remain portable atomics.
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "settings.h"
#include "../../savestate.h"
#include "../../crashrec.h"
#include <cmath>
#include "../../interp.h"
static constexpr float scales[]={1,1.5f,2,3};
@interface WWVulkanStateMenu : NSObject <NSMenuDelegate>
@end
@implementation WWVulkanStateMenu
- (void)save:(NSMenuItem*)item { ss::request_save((int)item.tag); }
- (void)load:(NSMenuItem*)item { ss::request_load((int)item.tag); }
- (void)toggleCrashRecovery:(NSMenuItem*)item { crashrec::set_enabled(!crashrec::enabled()); }
- (void)toggleFullStates:(NSMenuItem*)item { ss::set_full_states(!ss::full_states()); }
- (void)loadAuto:(NSMenuItem*)item { crashrec::request_load((int)item.tag); }
- (void)menuNeedsUpdate:(NSMenu*)menu {
    [menu removeAllItems];
    ss::SlotInfo slots[ss::kSlots + 1];
    for(int slot=1;slot<=ss::kSlots;++slot)slots[slot]=ss::slot_info(slot);
    auto label=[&](int slot) -> NSString* {
        const auto& info=slots[slot];
        if(!info.used)return @"empty";
        NSString* title=[NSString stringWithFormat:@"%s%s%s%s",info.when.c_str(),
            info.area.empty()?"":" · ",info.area.c_str(),info.portable?"":" · full"];
        if (!info.controller.empty()) title = [title stringByAppendingFormat:@" · %s", info.controller.c_str()];
        return info.compatible?title:[title stringByAppendingString:@" (incompatible)"];
    };
    for(int slot=1;slot<=ss::kSlots;++slot){
        NSMenuItem* item=[menu addItemWithTitle:[NSString stringWithFormat:@"Save to slot %d (%@)",slot,label(slot)]
            action:@selector(save:) keyEquivalent:@""];
        item.target=self;item.tag=slot;
        item.toolTip=[NSString stringWithFormat:@"Shortcut in game: Shift+F%d",slot];
    }
    [menu addItem:NSMenuItem.separatorItem];
    for(int slot=1;slot<=ss::kSlots;++slot){
        NSMenuItem* item=[menu addItemWithTitle:[NSString stringWithFormat:@"Load slot %d (%@)",slot,label(slot)]
            action:@selector(load:) keyEquivalent:@""];
        item.target=self;item.tag=slot;
        item.enabled=slots[slot].used&&slots[slot].compatible;
        item.toolTip=slot==1?@"In game: F1 opens the settings overlay (Saves)":[NSString stringWithFormat:@"Shortcut in game: F%d",slot];
    }
    [menu addItem:NSMenuItem.separatorItem];  // full save states (savestate.h): off by default, for debugging
    NSMenuItem* fs=[menu addItemWithTitle:@"Full Save States (large, contain game data, don't share)"
        action:@selector(toggleFullStates:) keyEquivalent:@""];
    fs.target=self;fs.state=ss::full_states()?NSControlStateValueOn:NSControlStateValueOff;
    fs.enabled=!ss::full_states_forced();
    [menu addItem:NSMenuItem.separatorItem];  // crash recovery (crashrec.cpp)
    NSMenuItem* cr=[menu addItemWithTitle:[NSString stringWithFormat:@"Crash Recovery (automatic state every %d min)",
        (crashrec::interval_seconds()+30)/60] action:@selector(toggleCrashRecovery:) keyEquivalent:@""];
    cr.target=self;cr.state=crashrec::enabled()?NSControlStateValueOn:NSControlStateValueOff;
    for(int i=1;i<=crashrec::kAutoSlots;++i){
        crashrec::AutoInfo a=crashrec::auto_info(i);
        NSString* d=a.used?[NSString stringWithFormat:@"%s%s%s",a.when.c_str(),a.area.empty()?"":" · ",a.area.c_str()]:@"empty";
        NSMenuItem* item=[menu addItemWithTitle:[NSString stringWithFormat:@"Load automatic state %d (%@)",i,d]
            action:@selector(loadAuto:) keyEquivalent:@""];
        item.target=self;item.tag=i;item.enabled=a.used;
    }
}
@end
@interface WWVulkanGraphicsMenu : NSObject <NSMenuItemValidation>
@end
@implementation WWVulkanGraphicsMenu
- (void)resolution:(NSMenuItem*)i { gfxvk::set_res_scale(scales[i.tag]); }
- (void)ao:(NSMenuItem*)i { gfxvk::set_ao_mode((int)i.tag); }
- (void)hires:(NSMenuItem*)i { gfxvk::set_ao_hires(!gfxvk::ao_hires_enabled()); }
- (void)aniso:(NSMenuItem*)i { gfxvk::set_aniso(!gfxvk::aniso_enabled()); }
- (void)fxaa:(NSMenuItem*)i { gfxvk::set_fxaa(!gfxvk::fxaa_enabled()); }
- (void)filter:(NSMenuItem*)i { gfxvk::set_scale_filter((int)i.tag); }
// tag 2: true 60; 60/120/240: frame interpolation at that rate
- (void)interpolation:(NSMenuItem*)i { if(i.tag==2) interp::set_mode(interp::mode()==2?0:2); else interp::toggle_fps((int)i.tag); }
- (BOOL)validateMenuItem:(NSMenuItem*)i {
    BOOL on=NO; bool available=true;
    if(i.action==@selector(resolution:))on=std::fabs(gfxvk::requested_res_scale()-scales[i.tag])<0.01f;
    if(i.action==@selector(ao:)){on=gfxvk::ao_mode()==i.tag;available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::AO);}
    if(i.action==@selector(hires:)){on=gfxvk::ao_hires_enabled();available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::AOHires);}
    if(i.action==@selector(aniso:)){on=gfxvk::aniso_enabled();available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::Anisotropy);}
    if(i.action==@selector(fxaa:)){on=gfxvk::fxaa_enabled();available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::FXAA);}
    if(i.action==@selector(filter:)){on=gfxvk::scale_filter()==i.tag;available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::ScaleFilter);}
    if(i.action==@selector(interpolation:))on=i.tag==2?interp::mode()==2:interp::mode()==1&&interp::fps()==i.tag;
    i.state=available&&on?NSControlStateValueOn:NSControlStateValueOff;
    return available;
}
@end
namespace gfxvk {
void install_graphics_menu(SDL_Window* window) {
    // SDL owns the application and window; do not install Metal's event handlers.
    if(!window || ![NSThread isMainThread])return;
    static WWVulkanGraphicsMenu* target;
    if(target)return;
    target=[WWVulkanGraphicsMenu new];
    NSMenu* bar=NSApp.mainMenu;
    if(!bar){bar=[[NSMenu alloc]initWithTitle:@""];NSApp.mainMenu=bar;}
    NSMenuItem* root=[[NSMenuItem alloc]initWithTitle:@"Graphics" action:nil keyEquivalent:@""];
    NSMenu* menu=[[NSMenu alloc]initWithTitle:@"Graphics"];root.submenu=menu;[bar addItem:root];
    auto add=[&](NSString* title,SEL action,NSInteger tag){NSMenuItem* i=[[NSMenuItem alloc]initWithTitle:title action:action keyEquivalent:@""];i.target=target;i.tag=tag;[menu addItem:i];};
    add(@"1280 × 720 (1×) — R cycles",@selector(resolution:),0);
    add(@"1920 × 1080 (1.5×)",@selector(resolution:),1);
    add(@"2560 × 1440 (2×)",@selector(resolution:),2);
    add(@"3840 × 2160 (3×)",@selector(resolution:),3);
    [menu addItem:NSMenuItem.separatorItem];
    add(@"AO: Original — O cycles",@selector(ao:),0);
    add(@"AO: Centre fix",@selector(ao:),1);
    add(@"AO: Centre + noise fix",@selector(ao:),2);
    add(@"AO full-size depth — M",@selector(hires:),0);
    add(@"16× anisotropic filtering — N",@selector(aniso:),0);
    add(@"FXAA — 8",@selector(fxaa:),0);
    [menu addItem:NSMenuItem.separatorItem];
    add(@"Scaling: Smooth",@selector(filter:),0);
    add(@"Scaling: Sharp",@selector(filter:),1);
    add(@"Scaling: Integer",@selector(filter:),2);
    [menu addItem:NSMenuItem.separatorItem];
    add(@"60 FPS interpolation — 6",@selector(interpolation:),60);
    add(@"120 FPS interpolation",@selector(interpolation:),120);
    add(@"240 FPS interpolation",@selector(interpolation:),240);
    add(@"True 60 FPS — 7",@selector(interpolation:),2);
    static WWVulkanStateMenu* stateTarget=[WWVulkanStateMenu new];
    NSMenuItem* stateRoot=[[NSMenuItem alloc]initWithTitle:@"Save States" action:nil keyEquivalent:@""];
    NSMenu* stateMenu=[[NSMenu alloc]initWithTitle:@"Save States"];
    stateMenu.autoenablesItems=NO;
    stateMenu.delegate=stateTarget;
    stateRoot.submenu=stateMenu;
    [bar addItem:stateRoot];
}
}
