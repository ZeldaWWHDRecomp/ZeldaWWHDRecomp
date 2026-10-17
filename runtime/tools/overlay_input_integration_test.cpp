// Actual SDL router + overlay.cpp input API; no game, renderer, audio or state files.
#include <SDL3/SDL.h>
#include "input.h"
#include "input_map.h"
#include "gfx/renderer.h"
#include "overlay/overlay.h"
#include "platform/input_sdl.h"
#include "platform/mouse_sdl.h"
#include "platform/keycodes.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>
#include "../src/overlay/overlay.cpp"
#include "imgui_internal.h"

static std::vector<std::string> logs;
static int saves=0,loads=0,promptKeys=0;
static bool prompt=false,mouseCamera=false;
void log_msg(const char* format,...) {
 char line[2048];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
 logs.emplace_back(line);
}
namespace audio { void flush(){} float master_volume(){return 1;} void set_master_volume(float,bool){} }
namespace ss { void request_save(int){++saves;} void request_load(int){++loads;} }
namespace render { const Backend* g_backend = nullptr; uint64_t frame_count(){return 42;} Api active(){return Api::Vulkan;} }
namespace gx2 { void checkpoint_vulkan_caches(){} void checkpoint_present_observer(){} }
namespace gfxvk { bool graphics_hotkey(char,bool){return false;} }
namespace aspect { void set_window_aspect(float){} }
namespace mods { double game_time(){return 0;} void filter_pad(input::PadState&){} bool mouse_camera(){return mouseCamera;} bool first_person_wheel(){return false;} void mouse_button(int,bool){} void mouse_add(float,float){} void mouse_wheel(float){} void fast_forward_reset(){} void lock_on_reset(){} }
namespace interp { int mode(){return 0;} void toggle_fps(int){} void set_mode(int){} uint64_t logic_steps(){return 0;} }
namespace timebase { uint64_t now(){return 0;} }
namespace true60 { uint64_t link_steps(){return 0;} bool state_loaded(){return false;} }
namespace true60_test { void set_origin_step(uint64_t){} void tick(double,bool){} }
namespace text_entry { bool active(){return prompt;} void text(const char*){} void preedit(const char*){} void key(int,bool,bool){++promptKeys;} }
namespace hostui {
 void graphics_changed(){} void post(std::function<void()> fn){fn();}
 bool fullscreen(){return false;} void set_fullscreen(bool){}
 float res_scale(){return 1;} void set_res_scale(float){}
 int drc_mode(){return 0;} void set_drc_mode(int){} bool drc_shown(){return true;} void show_drc(bool){}
 void set(const char*,const std::string&){}
}
static int shots = 0;
namespace screenshot { void request(){++shots;} bool key_down(int){return false;} void poll_controller(const float*){} }

static void event_key(SDL_Window* window,SDL_Scancode key,bool down,SDL_Keymod mods=SDL_KMOD_NONE,bool repeat=false) {
 SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.windowID=SDL_GetWindowID(window);
 e.key.timestamp=123456789;e.key.scancode=key;e.key.mod=mods;e.key.repeat=repeat;
 input::handle_event(e);
}
// Exercise the actual Shortcuts widgets and capture loop without a renderer or game data.
static int shortcut_ui_test() {
 using namespace input_map;
 using namespace overlay;
 const auto file = std::filesystem::temp_directory_path() / ("wwhd-shortcut-ui-" + std::to_string(getpid()) + ".json");
 setenv("WWHD_CONTROLS", file.c_str(), 1);
 set_current(Mapping::defaults(), false);
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"); assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 auto* window=SDL_CreateWindow("Shortcuts",1280,720,SDL_WINDOW_HIDDEN); assert(window); input::set_prompt_window(window);
 ImGui::CreateContext();
 auto& io = ImGui::GetIO();
 io.IniFilename = nullptr;
 io.DisplaySize = ImVec2(1280,720); io.DeltaTime = 1.0f/60;
 io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
 io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_RendererHasTextures;
 io.Fonts->AddFontDefault();
 g_open = true;
 U.cap_action = -1;
 std::fill(std::begin(U.values), std::end(U.values), 0.0f);
 bool controlsDrawing=false;
 auto frame = [&](bool focusReset = false) {
  poll_controller_shortcuts(U.values);
  prepare_menu_input(io, is_open(), false); // actual production capture/navigation dispatcher
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(100,20), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(1000,680), ImGuiCond_Always);
  if (focusReset) ImGui::SetNextWindowFocus();
  ImGui::Begin("Shortcut UI regression");
  if (focusReset) ImGui::SetKeyboardFocusHere();
  if (controlsDrawing) {
   ControlsView view; view.m=current();view.cap_action=U.cap_action;view.cap_col=U.cap_col;
   draw_controls(view,980,600);
   if(view.clear_action>=0) clear_binding(view.clear_action,view.clear_col);
   if(view.start_action>=0) start_capture(view.start_action,view.start_col);
  } else tab_shortcuts();
  ImGui::End(); ImGui::Render();
  std::copy(std::begin(U.values),std::end(U.values),std::begin(U.prev));
 };
 auto press = [&](ImGuiKey key) {
  io.AddKeyEvent(key,true); frame(); io.AddKeyEvent(key,false); frame(); frame();
 };
 auto clickFocused = [&](ImGuiMouseSource source) {
  auto& g = *ImGui::GetCurrentContext();
  assert(g.NavId && g.NavWindow);
  const auto center = ImGui::WindowRectRelToAbs(g.NavWindow,g.NavWindow->NavRectRel[g.NavLayer]).GetCenter();
  io.AddMouseSourceEvent(source); io.AddMousePosEvent(center.x,center.y);
  io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame(); frame();
 };
 auto bindingId = [&](int action, int column, const char* label = "###shortcut_binding") {
  ImGuiID id = ImHashStr("shortcut bindings",0,ImHashStr("Shortcut UI regression"));
  id=ImHashData(&action,sizeof action,id); id=ImHashData(&column,sizeof column,id);
  return ImHashStr(label,0,id);
 };
 auto focus = [&](ImGuiID id) {
  for (int i=0;i<200 && ImGui::GetCurrentContext()->NavId!=id;++i) press(ImGuiKey_Tab);
  assert(ImGui::GetCurrentContext()->NavId == id);
 };
 auto keyPress = [&](SDL_Scancode code,SDL_Keymod mods=SDL_KMOD_NONE) {
  event_key(window,code,true,mods); event_key(window,code,false,mods);
 };
 auto pressPad = [&](int pad) { U.values[pad]=1; frame(); U.values[pad]=0; frame(); frame(); };
 frame(true); frame(); frame();
 // The real SDL path captures a new primary key, saves it, and dispatches only the new key.
 focus(bindingId(kSettings,0)); clickFocused(ImGuiMouseSource_Mouse);
 assert(U.cap_action == kActionCount+kSettings && U.cap_col == 0);
 keyPress(SDL_SCANCODE_F9,SDL_Keymod(SDL_KMOD_CTRL|SDL_KMOD_SHIFT)); frame();
 assert((current().shortcuts[kSettings].keys[0] == ShortcutKey{kVK_F9,kModCtrl|kModShift}));
 auto saved=current(); set_current(Mapping::defaults(),false); load_startup(); assert(current()==saved);
 // The replaced default Esc no longer opens Settings; the F1 alternate still does.
 g_open=false; keyPress(SDL_SCANCODE_ESCAPE); assert(!is_open());
 keyPress(SDL_SCANCODE_F9,SDL_Keymod(SDL_KMOD_CTRL|SDL_KMOD_SHIFT)); assert(is_open());
 g_open=false; SDL_Delay(160); keyPress(SDL_SCANCODE_F1); assert(is_open());
 // The alternate key uses the same SDL capture, serialization and dispatch paths.
 focus(bindingId(kSettings,1)); clickFocused(ImGuiMouseSource_Mouse);
 keyPress(SDL_SCANCODE_F8,SDL_KMOD_ALT); frame();
 assert((current().shortcuts[kSettings].keys[1] == ShortcutKey{kVK_F8,kModAlt}));
 saved=current(); set_current(Mapping::defaults(),false); load_startup(); assert(current()==saved);
 g_open=false; SDL_Delay(160); keyPress(SDL_SCANCODE_F1); assert(!is_open());
 keyPress(SDL_SCANCODE_F8,SDL_KMOD_ALT); assert(is_open());
 // Escape is recorded instead of canceling capture or closing Settings.
 focus(bindingId(kSettings,0)); clickFocused(ImGuiMouseSource_Mouse);
 keyPress(SDL_SCANCODE_ESCAPE); frame();
 assert(current().shortcuts[kSettings].keys[0].code==kVK_Escape && is_open() && U.cap_action<0);
 // A right click while waiting is a real mouse binding, not the clearing gesture.
 focus(bindingId(kPhoto,0)); clickFocused(ImGuiMouseSource_Mouse);
 assert(mouse_button(1,true)); assert(mouse_button(1,false)); frame();
 assert(current().shortcuts[kPhoto].keys[0].code==kMouse2);
 g_open=false; const int beforeMouse=shots; assert(pointer_shortcut(kMouse2,true)); assert(shots==beforeMouse+1); g_open=true;
 // Home is visible and remappable. A tap can replace it with Capture/Share.
 focus(bindingId(kSettings,2)); pressPad(face_input(kA)); assert(U.cap_action==kActionCount+kSettings);
 pressPad(kPadCapture);
 assert(current().shortcuts[kSettings].button==kPadCapture && !current().shortcuts[kSettings].hold && is_open());
 saved=current(); set_current(Mapping::defaults(),false); load_startup(); assert(current()==saved);
 g_open=false; pressPad(kPadHome); assert(!is_open()); pressPad(kPadCapture); assert(is_open());
 // A held button plus a press is captured, persists, and does not also navigate or toggle.
 focus(bindingId(kSettings,2)); pressPad(face_input(kA));
 U.values[kPadLB]=1; frame(); U.values[kPadCapture]=1; frame(); frame();
 assert(current().shortcuts[kSettings].hold==kPadLB && current().shortcuts[kSettings].button==kPadCapture && is_open());
 assert(!navigation_pressed(kPadLB));
 std::fill(std::begin(U.values),std::end(U.values),0.0f); frame();
 saved=current(); set_current(Mapping::defaults(),false); load_startup(); assert(current()==saved);
 g_open=false; pressPad(kPadCapture); assert(!is_open());
 U.values[kPadLB]=U.values[kPadCapture]=1; frame(); assert(is_open());
 std::fill(std::begin(U.values),std::end(U.values),0.0f); frame();
 // Clear is the same focused-X gesture as Controls, without a button or popup.
 focus(bindingId(kSettings,2)); pressPad(face_input(kX));
 assert(!current().shortcuts[kSettings].hold && !current().shortcuts[kSettings].button);
 // Controls capture owns L/R, Select, Home and Capture through completion and until release.
 for (int pad : {kPadLB,kPadRB,kPadOptions,kPadHome,kPadCapture,kPadRightPaddle1,kPadTouchpad}) {
  start_capture(kZR,kColPad); frame(); U.values[pad]=1; frame(); frame();
  assert(current().pad[kZR]==pad && is_open() && !navigation_pressed(pad));
  U.values[pad]=0; frame();
 }
 start_capture(kY,kColKey1); keyPress(SDL_SCANCODE_ESCAPE); frame();
 assert(current().keys[kY][1]==kVK_Escape && is_open());
 // Return keyboard focus to Shortcuts after exercising the Controls capture path.
 frame(true); frame(); frame();
 // Right click clears a shortcut; right mouse remains bindable above during capture.
 focus(bindingId(kPhoto,0));
 const auto center=ImGui::WindowRectRelToAbs(ImGui::GetCurrentContext()->NavWindow,ImGui::GetCurrentContext()->NavWindow->NavRectRel[0]).GetCenter();
 io.AddMousePosEvent(center.x,center.y); io.AddMouseButtonEvent(1,true); frame(); io.AddMouseButtonEvent(1,false); frame();
 assert(current().shortcuts[kPhoto].keys[0].code==kNoKey);
 // Reset is scoped to Shortcuts and the resulting controls file reads back exactly.
 auto m=current(); m.keys[kA]={kVK_F8,kNoKey}; set_current(m,false);
 frame(); focus(ImHashStr("Reset to defaults",0,ImHashStr("Shortcut UI regression"))); clickFocused(ImGuiMouseSource_Mouse);
 assert(current().shortcuts==shortcut_defaults() && current().keys[kA]==m.keys[kA]);
 Mapping disk; assert(load_file(file.string(),disk)); assert(disk==current());
 // The actual Controls drawing uses the same right-click gesture, and can still bind Mouse 2.
 controlsDrawing=true;frame(true);frame();frame();
 int chip=4*kA+kColKey0;
 const ImGuiID chipId=ImHashStr("##chip",0,ImHashData(&chip,sizeof chip,ImHashStr("Shortcut UI regression")));
 focus(chipId);
 auto chipCenter=ImGui::WindowRectRelToAbs(ImGui::GetCurrentContext()->NavWindow,ImGui::GetCurrentContext()->NavWindow->NavRectRel[0]).GetCenter();
 io.AddMousePosEvent(chipCenter.x,chipCenter.y);io.AddMouseButtonEvent(1,true);frame();io.AddMouseButtonEvent(1,false);frame();
 assert(current().keys[kA][0]==kNoKey);
 focus(chipId);clickFocused(ImGuiMouseSource_Mouse);assert(U.cap_action==kA);
 assert(mouse_button(1,true));assert(mouse_button(1,false));frame();assert(current().keys[kA][0]==kMouse2);

 g_open=false; ImGui::DestroyContext(); std::filesystem::remove(file); input::set_prompt_window(nullptr); SDL_DestroyWindow(window); SDL_Quit();
 puts("Shortcut UI: real SDL capture, primary/alternate dispatch, reload, Escape, right mouse, Home/Capture, combinations and shared clearing passed");
 return 0;
}

static int window_target_test() {
 using namespace overlay::window_target;
 unsetenv("GAMESCOPE_WAYLAND_DISPLAY");unsetenv("WAYLAND_DISPLAY");
 setenv("XDG_CURRENT_DESKTOP","gamescope",1);
 assert(gamescope_session());
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 auto* tv=SDL_CreateWindow("TV",1280,720,0);
 auto* drc=SDL_CreateWindow("GamePad",854,480,0);
 assert(tv&&drc);gfx::g_has_drc_window=true;input::set_prompt_window(tv);input::set_gamepad_window(drc);
 SDL_WindowID previousFocus=SDL_GetWindowID(tv);
 auto focus=[&](SDL_Window* w) {
  const bool wasOpen=overlay::is_open();
  SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;e.window.windowID=previousFocus;input::handle_event(e);
  assert(overlay::is_open()==wasOpen); // do not close during a TV/GamePad focus handoff
  e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;e.window.windowID=SDL_GetWindowID(w);input::handle_event(e);
  previousFocus=e.window.windowID;
 };
 auto release=[&](){overlay::set_open(false);event_key(drc,SDL_SCANCODE_F1,false);SDL_Delay(160);};
 focus(drc);assert(selected.load()==Window::GamePad);
 event_key(drc,SDL_SCANCODE_F1,true);assert(overlay::is_open());
 // Exercise the actual Vulkan swap frame builder, including dimensions and refusal paths.
 ImDrawData menu,toast;
 int built=0;
 auto build=[&](ImDrawData* draw) {
  const auto window=selected.load();
  const int before=built;
  const auto plan=gfx::display_plan(true,1280,720,true,854,480,1280,720,42);
  auto result=build_frame(window,plan.drc_window,true,{plan.dw,plan.dh,true},{854,480,true},
   [&](float width,float height) {
    ++built;assert(width==(window==Window::GamePad?854:1280));
    assert(height==(window==Window::GamePad?480:720));return draw;
   },[]{overlay::set_open(false);});
  assert(built==before+1);return result;
 };
 DrawFrame present=build(&menu);
 assert(present.for_window(true)==&menu && present.for_window(false)==nullptr);
 for(int i=0;i<5;++i) { // rendering itself never reclaims TV focus
  present=build(&menu);assert(selected.load()==Window::GamePad&&present.for_window(true)==&menu);
 }
 // Stereo packing remains TV-only in every mode, including Full SBS's half-width TV canvas.
 for(int mode:{stereo::Off,stereo::HalfSBS,stereo::FullSBS,stereo::TAB}) {
  stereo::set_mode(mode);stereo::latch();
  const float tvWidth=mode==stereo::FullSBS?640:1280;
  auto drcFrame=build_frame(selected.load(),true,true,{tvWidth,720,true},{854,480,true},
   [&](float width,float height) {assert(width==854&&height==480);return &menu;},[]{assert(false);});
  assert(drcFrame.for_window(true)==&menu&&!drcFrame.for_window(false));
  assert(!drcFrame.stereo_packed(mode!=stereo::Off,true));
  auto tvFrame=build_frame(Window::TV,true,true,{tvWidth,720,true},{854,480,true},
   [&](float width,float height) {assert(width==tvWidth&&height==720);return &menu;},[]{assert(false);});
  assert(tvFrame.stereo_packed(mode!=stereo::Off,true)==(mode!=stereo::Off));
  assert(!tvFrame.stereo_packed(mode!=stereo::Off,false));
 }
 stereo::set_mode(stereo::Off);stereo::latch();
 release();event_key(drc,SDL_SCANCODE_F2,true);assert(loads==1);
 present=build(&toast); // host messages use the same overlay frame, even with menu closed
 assert(present.for_window(true)==&toast && present.for_window(false)==nullptr);
 event_key(drc,SDL_SCANCODE_F1,true);assert(overlay::is_open());
 SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;move.motion.windowID=SDL_GetWindowID(drc);
 move.motion.x=427;move.motion.y=240;move.motion.which=SDL_TOUCH_MOUSEID;
 overlay::g_events.clear();input::handle_event(move);
 assert(overlay::g_events.size()==1);const auto pointer=overlay::g_events.back();
 assert(pointer.kind==overlay::Event::MousePos&&pointer.x==.5f&&pointer.y==.5f&&pointer.touch);
 move.motion.windowID=SDL_GetWindowID(tv);input::handle_event(move);assert(overlay::g_events.size()==1);
 move.motion.windowID=SDL_GetWindowID(drc);move.motion.which=1;input::handle_event(move);assert(!overlay::g_events.back().touch);
 SDL_Event click{};click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.windowID=SDL_GetWindowID(drc);
 click.button.which=SDL_TOUCH_MOUSEID;click.button.button=SDL_BUTTON_LEFT;click.button.x=427;click.button.y=240;
 input::handle_event(click);assert(overlay::g_events.back().kind==overlay::Event::MouseButton&&overlay::g_events.back().touch);
 click.type=SDL_EVENT_MOUSE_BUTTON_UP;input::handle_event(click);assert(!overlay::g_events.back().down);
 SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.windowID=SDL_GetWindowID(drc);wheel.wheel.y=1;
 input::handle_event(wheel);assert(overlay::g_events.back().kind==overlay::Event::Wheel&&overlay::g_events.back().y==1);
 SDL_Event leave{};leave.type=SDL_EVENT_WINDOW_MOUSE_LEAVE;leave.window.windowID=SDL_GetWindowID(drc);
 input::handle_event(leave);assert(overlay::g_events.back().kind==overlay::Event::MousePos&&overlay::g_events.back().x==-FLT_MAX);
 const auto pointerCount=overlay::g_events.size();leave.window.windowID=SDL_GetWindowID(tv);
 input::handle_event(leave);assert(overlay::g_events.size()==pointerCount);
 SDL_Event enter{};enter.type=SDL_EVENT_WINDOW_MOUSE_ENTER;enter.window.windowID=SDL_GetWindowID(drc);
 input::handle_event(enter);assert(overlay::g_events.size()==pointerCount+1&&overlay::g_events.back().kind==overlay::Event::MousePos);
 // Focus changes while open move the menu without another toggle or a blind input hold.
 focus(tv);assert(overlay::is_open()&&selected.load()==Window::TV);
 present=build(&menu);assert(present.for_window(false)==&menu&&present.for_window(true)==nullptr);
 focus(drc);assert(overlay::is_open()&&selected.load()==Window::GamePad);
 // A render destination can disappear after SDL focus selection. Never build an invisible frame.
 auto unavailable=[&](Window window,bool separate,Destination destination) {
  overlay::set_open(true);assert(overlay::is_open());
  auto result=build_frame(window,separate,true,{1280,720,true},destination,
   [](float,float)->ImDrawData* {assert(false);return nullptr;},[]{overlay::set_open(false);});
  assert(result.target==Window::None&&!result.draw&&!overlay::is_open());
 };
 unavailable(Window::GamePad,true,{854,480,false}); // no scan buffer / drawable
 unavailable(Window::GamePad,true,{15,480,true}); // too small for overlay::frame
 unavailable(Window::GamePad,false,{854,480,true}); // no GamePad present this frame
 unavailable(Window::None,true,{854,480,true}); // no focused destination
 overlay::set_open(true);auto failed=build(nullptr);
 assert(failed.target==Window::None&&!overlay::is_open()); // renderer produced no overlay
 overlay::set_open(true);
 // An auxiliary/compositor window taking focus also makes the menu unavailable.
 auto* auxiliary=SDL_CreateWindow("Controls",32,32,SDL_WINDOW_HIDDEN);assert(auxiliary);
 focus(auxiliary);assert(selected.load()==Window::None&&!overlay::is_open());
 overlay::set_open(true);assert(!overlay::is_open());SDL_DestroyWindow(auxiliary);
 focus(drc);overlay::set_open(true);assert(overlay::is_open());
 // Losing both destinations refuses all menu-opening paths and releases game input.
 SDL_HideWindow(tv);SDL_HideWindow(drc);
 SDL_Event hidden{};hidden.type=SDL_EVENT_WINDOW_HIDDEN;hidden.window.windowID=SDL_GetWindowID(drc);input::handle_event(hidden);
 assert(selected.load()==Window::None&&!overlay::is_open()&&!overlay::blocks_input());
 release();event_key(drc,SDL_SCANCODE_F1,true);assert(!overlay::is_open());
 overlay::set_open(true);assert(!overlay::is_open());
 float pad[input_map::kPadCount]={};pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 // PIP/one-window policy and normal desktops retain the TV destination.
 setenv("WWHD_DRC_MODE","pip",1);gfx::display_env_overrides();
 assert(!gfx::drc_window_wanted());
 const auto pip=gfx::display_plan(true,1280,720,true,854,480,1280,720,42);
 assert(pip.pip_on&&!pip.drc_window);
 input::handle_event(hidden);assert(selected.load()==Window::TV);
 overlay::set_open(true);assert(overlay::is_open());overlay::set_open(false);
 assert(choose(false,true,Window::GamePad,true,true)==Window::TV);
 assert(choose(true,false,Window::GamePad,true,false)==Window::TV);
 SDL_ShowWindow(tv);setenv("WWHD_DRC_MODE","window",1);gfx::display_env_overrides();
 focus(tv);assert(selected.load()==Window::TV);
 prompt=true;event_key(tv,SDL_SCANCODE_F1,false);SDL_Delay(160);event_key(tv,SDL_SCANCODE_F1,true);
 assert(overlay::is_open());prompt=false;overlay::set_open(false);
 setenv("XDG_CURRENT_DESKTOP","KDE",1);setenv("SteamDeck","1",1);
 assert(!gamescope_session()); // Steam Deck desktop mode must keep the desktop policy
 SDL_DestroyWindow(drc);SDL_DestroyWindow(tv);SDL_Quit();
 puts("gamescope two-window menu/toast present routing, pointer/touch, focus migration, invisible-menu guard, PIP and prompt F1 passed");
 return 0;
}
int main(int argc,char** argv){
 if(argc>1&&std::string(argv[1])=="--shortcuts-ui")return shortcut_ui_test();
 if(argc>1&&std::string(argv[1])=="--window-target")return window_target_test();
 const auto controlsFile=std::filesystem::temp_directory_path()/("wwhd-overlay-mouse-165-"+std::to_string(getpid())+".json");
 setenv("WWHD_CONTROLS",controlsFile.c_str(),1);
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 auto* tv=SDL_CreateWindow("TV",32,32,SDL_WINDOW_HIDDEN);
 auto* drc=SDL_CreateWindow("GamePad",32,32,SDL_WINDOW_HIDDEN);
 auto* auxiliary=SDL_CreateWindow("Controls",32,32,SDL_WINDOW_HIDDEN);
 assert(tv&&drc&&auxiliary);input::set_prompt_window(tv);
#ifndef MENU112_BASELINE
 input::set_gamepad_window(drc);
#endif
 auto reset=[&](){overlay::set_open(false);event_key(tv,SDL_SCANCODE_F1,false);SDL_Delay(160);logs.clear();};
 auto f1=[&](SDL_Window* w,bool down){event_key(w,SDL_SCANCODE_F1,down);};
 auto focus=[&](){SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;e.window.windowID=SDL_GetWindowID(tv);e.window.timestamp=987654321;input::handle_event(e);
  e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;e.window.windowID=SDL_GetWindowID(drc);input::handle_event(e);};
 auto result=[](const char* name,bool expected){printf("%s: %s\n",name,overlay::is_open()?"open":"closed");assert(overlay::is_open()==expected);};
#ifdef MENU112_BASELINE
 constexpr bool duplicateExpected=false;
#else
 constexpr bool duplicateExpected=true;
#endif
 reset();f1(tv,true);focus();result("focus after F1",true);
#ifndef MENU112_BASELINE
 auto logged=[](const std::string& needle){for(const auto& line:logs)if(line.find(needle)!=std::string::npos)return true;return false;};
 assert(logged("focus lost window TV id "+std::to_string(SDL_GetWindowID(tv))+" timestamp 987654321"));
 assert(logged("focus gained window GamePad id "+std::to_string(SDL_GetWindowID(drc))+" timestamp 987654321"));
#endif
 reset();f1(tv,true);f1(tv,true);result("duplicate F1 down on TV",duplicateExpected);
 reset();f1(tv,true);f1(tv,false);focus();f1(tv,true);result("F1 down/up/down on TV across focus",duplicateExpected);
 reset();f1(tv,true);focus();f1(drc,true);result("duplicate F1 down on GamePad",true);
 reset();f1(tv,true);f1(tv,false);focus();f1(drc,true);result("F1 down/up/down ending on GamePad",true);
#ifndef MENU112_BASELINE
 reset();f1(tv,true);focus();f1(drc,false);SDL_Delay(160);f1(tv,true);result("GamePad release then deliberate TV F1",false);
#endif
 reset();float pad[input_map::kPadCount]={};pad[input_map::kPadOptions]=1;
 overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());SDL_Delay(510);
 overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());focus();pad[input_map::kPadOptions]=0;
 overlay::poll_controller_shortcuts(pad);result("View hold/release across focus",true);
#ifndef MENU112_BASELINE
 assert(logs.back().find("focus gained")!=std::string::npos);
 bool viewLogged=false;for(const auto& line:logs)viewLogged|=line.find("opened (View")!=std::string::npos;assert(viewLogged);
#endif
 // Releasing View does not close; the next press still closes once and stays latched.
 pad[input_map::kPadOptions]=1;overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
#ifndef MENU112_BASELINE
 assert(logs.back().find("closed (View")!=std::string::npos);
#endif
 pad[input_map::kPadOptions]=0;overlay::poll_controller_shortcuts(pad);
 reset();saves=loads=0;
 for(int i=0;i<5;++i)event_key(drc,SDL_Scancode(SDL_SCANCODE_F1+i),true,SDL_KMOD_SHIFT);
 for(int i=1;i<5;++i)event_key(drc,SDL_Scancode(SDL_SCANCODE_F1+i),true);
 printf("GamePad save/load requests: %d/%d\n",saves,loads);
#ifdef MENU112_BASELINE
 assert(saves==0&&loads==0);
#else
 assert(saves==5&&loads==4);
#endif
 event_key(tv,SDL_SCANCODE_F6,true);assert(loads == 5);
 event_key(tv,SDL_SCANCODE_F10,true);assert(shots == 1);
 auto remapped = input_map::current();
 remapped.shortcuts[input_map::kPhoto].keys = {{{kVK_F9,input_map::kModCtrl}, {}}};
 remapped.shortcuts[input_map::kPhoto].hold = input_map::kPadOptions;
 remapped.shortcuts[input_map::kPhoto].button = input_map::kPadDUp;
 input_map::set_current(remapped, false);
 event_key(tv,SDL_SCANCODE_F10,true);assert(shots == 1);
 event_key(tv,SDL_SCANCODE_F9,true,SDL_KMOD_CTRL);assert(shots == 2);
 event_key(tv,SDL_SCANCODE_F9,true);assert(shots == 2);
 pad[input_map::kPadOptions]=1;pad[input_map::kPadDUp]=1;
 overlay::poll_controller_shortcuts(pad);assert(shots == 3 && !overlay::is_open());
 overlay::poll_controller_shortcuts(pad);assert(shots == 3);
 float game[input_map::kPadCount];std::copy(std::begin(pad),std::end(pad),game);
 input_map::consume_shortcut_buttons(remapped,pad,game);
 assert(!game[input_map::kPadOptions]&&!game[input_map::kPadDUp]);
 pad[input_map::kPadDUp]=0;overlay::poll_controller_shortcuts(pad);
 pad[input_map::kPadOptions]=0;overlay::poll_controller_shortcuts(pad);
 remapped.shortcuts[input_map::kSettings].keys = {};
 remapped.shortcuts[input_map::kSettings].hold = 0;
 remapped.shortcuts[input_map::kSettings].button = input_map::kPadCapture;
 input_map::set_current(remapped,false);
 event_key(tv,SDL_SCANCODE_F1,true);assert(!overlay::is_open());
 pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 pad[input_map::kPadHome]=0;overlay::poll_controller_shortcuts(pad);
 pad[input_map::kPadCapture]=1;overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());
 pad[input_map::kPadCapture]=0;overlay::poll_controller_shortcuts(pad);
 event_key(tv,SDL_SCANCODE_ESCAPE,true);assert(!overlay::is_open());
 input_map::set_current(input_map::Mapping::defaults(),false);
 const int beforeSaves=saves,beforeLoads=loads;prompt=true;
 event_key(drc,SDL_SCANCODE_F1,true,SDL_KMOD_SHIFT);event_key(drc,SDL_SCANCODE_F2,true);
 assert(saves==beforeSaves&&loads==beforeLoads&&promptKeys==2);prompt=false;
 reset();prompt=true;f1(tv,true);assert(overlay::is_open());prompt=false; // desktop F1 still opens settings over a text prompt
#ifndef MENU112_BASELINE
 reset();event_key(auxiliary,SDL_SCANCODE_F2,true);
 event_key(drc,SDL_SCANCODE_F2,true,SDL_KMOD_NONE,true);
 assert(saves==beforeSaves&&loads==beforeLoads); // auxiliary windows and repeats do not load
#endif
#ifndef MENU112_BASELINE
 // Real transitions emit one line, with the original SDL key provenance.
 reset();f1(tv,true);assert(overlay::is_open());
 auto matches=[](const std::string& needle){int n=0;for(const auto& line:logs)n+=line.find(needle)!=std::string::npos;return n;};
 assert(matches("[overlay] opened (F1 key, window TV id "+std::to_string(SDL_GetWindowID(tv))+", timestamp 123456789, frame 42)")==1);
 event_key(tv,SDL_SCANCODE_F1,true,SDL_KMOD_NONE,true);
 f1(tv,true);assert(matches("[overlay] opened (")==1&&matches("[overlay] closed (")==0);
 event_key(drc,SDL_SCANCODE_ESCAPE,true);assert(!overlay::is_open());
 assert(matches("[overlay] closed (Esc key, window GamePad id "+std::to_string(SDL_GetWindowID(drc))+", timestamp 123456789, frame 42)")==1);
 f1(tv,false);assert(matches("key F1 up window TV")==1);
 assert(matches("key F1 down window TV")>=2&&matches("key Escape down window GamePad")==1);
 event_key(drc,SDL_SCANCODE_ESCAPE,false);assert(matches("key Escape up window GamePad")==1);
 reset();f1(auxiliary,true);assert(!overlay::is_open()); // auxiliary windows do not toggle
 // Focus releases game keys while keeping an already open menu open.
 event_key(tv,SDL_SCANCODE_K,true);bool held[256];input::held_keys(held);assert(held[kVK_ANSI_K]);
 focus();input::held_keys(held);assert(!held[kVK_ANSI_K]);
 overlay::set_open(true);focus();assert(overlay::is_open());
 // Home retains press/release behavior and reports its reason.
 overlay::set_open(false);pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());
 assert(logs.back().find("opened (Home")!=std::string::npos);
 overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());pad[input_map::kPadHome]=0;overlay::poll_controller_shortcuts(pad);
 pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 assert(logs.back().find("closed (Home")!=std::string::npos);
 // A click that starts capture has already been routed before start_capture; its up must still
 // reach ImGui, and only the next down is the binding. No game or rendering is involved.
 overlay::set_open(true);
 for (int b=0;b<5;++b) {
  overlay::start_capture(input_map::kA,0);
  assert(overlay::mouse_button(0,false));overlay::apply_capture();
  assert(overlay::U.cap_action==input_map::kA);
  SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(tv);
  e.button.which=1;e.button.button=b==0?SDL_BUTTON_LEFT:b==1?SDL_BUTTON_RIGHT:b==2?SDL_BUTTON_MIDDLE:b==3?SDL_BUTTON_X1:SDL_BUTTON_X2;
  input::handle_event(e);overlay::apply_capture();
  assert(input_map::current().keys[input_map::kA][0]==input_map::kMouse1+b);
  assert(input::read().buttons==0);
  e.type=SDL_EVENT_MOUSE_BUTTON_UP;input::handle_event(e);
 }
 for (int dy : {1,-1}) {
  overlay::start_capture(input_map::kB,1);
  SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.windowID=SDL_GetWindowID(tv);e.wheel.y=dy;
  input::handle_event(e);overlay::apply_capture();
  assert(input_map::current().keys[input_map::kB][1]==(dy>0?input_map::kWheelUp:input_map::kWheelDown));
 }
 overlay::start_capture(input_map::kR,2); // controller field does not take a mouse binding
 overlay::mouse_button(0,true);overlay::apply_capture();assert(overlay::U.cap_action==input_map::kR);
 overlay::U.cap_action=-1;overlay::g_capturing_keys=false; // end the deliberately unfilled controller capture
 overlay::set_open(false);
 // Esc is the default Settings key (F1 the alternate). With the mouse captured by the mouse camera, the first
 // Esc only frees the mouse; the next one opens Settings, and Esc inside Settings closes it.
 {
  input_map::set_current(input_map::Mapping::defaults(),false);
  const auto& keys=input_map::current().shortcuts[input_map::kSettings].keys;
  assert(keys[0].code==kVK_Escape&&keys[1].code==kVK_F1&&input_map::current().shortcuts[input_map::kSettings].button==input_map::kPadHome);
  auto esc=[&](bool down){event_key(tv,SDL_SCANCODE_ESCAPE,down);};
  auto capture=[&](){
   SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(tv);e.button.which=1;e.button.button=SDL_BUTTON_LEFT;
   input::handle_event(e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;input::handle_event(e);assert(mods::mouse_captured());
  };
  mouseCamera=true;mods::mouse_init(tv);
  reset();capture();
  esc(true);assert(!mods::mouse_captured()&&!overlay::is_open());esc(false);assert(!overlay::is_open());
  esc(true);assert(overlay::is_open()&&matches("[overlay] opened (Esc key, window TV")==1);esc(false);
  esc(true);assert(!overlay::is_open()&&matches("[overlay] closed (Esc key, window TV")==1);esc(false);
  // no capture: Esc opens Settings directly; a held Esc (repeats) does not toggle it again
  SDL_Delay(160);esc(true);assert(overlay::is_open());
  event_key(tv,SDL_SCANCODE_ESCAPE,true,SDL_KMOD_NONE,true);assert(overlay::is_open());
  esc(false);esc(true);assert(!overlay::is_open());esc(false);
  // the macOS host gives overlay::key the key before its own mouse handling: same order there
  reset();capture();
  assert(overlay::key(kVK_Escape,true,false,0));mods::update_mouse();
  assert(!mods::mouse_captured()&&!overlay::is_open());
  assert(overlay::key(kVK_Escape,false,false,0));
  assert(overlay::key(kVK_Escape,true,false,0)&&overlay::is_open());overlay::key(kVK_Escape,false,false,0);
  overlay::set_open(false);
  // the game's text prompt keeps Esc (cancel), also when Esc is the Settings key
  reset();const int keysBefore=promptKeys;prompt=true;esc(true);esc(false);
  assert(!overlay::is_open()&&promptKeys==keysBefore+2);prompt=false;
  // Settings moved away from Esc: Esc still frees the mouse, but no longer opens Settings
  auto moved=input_map::Mapping::defaults();moved.shortcuts[input_map::kSettings].keys={{{kVK_F1},{}}};
  input_map::set_current(moved,false);
  reset();capture();esc(true);esc(false);assert(!mods::mouse_captured()&&!overlay::is_open());
  SDL_Delay(160);esc(true);esc(false);assert(!overlay::is_open());
  f1(tv,true);assert(overlay::is_open());f1(tv,false);overlay::set_open(false);
  input_map::set_current(input_map::Mapping::defaults(),false);
  mouseCamera=false;mods::mouse_init(nullptr);
 }
 puts("real overlay transitions, key/focus trace, Home, text prompt, Esc mouse release / Settings and auxiliary-window checks passed");
#endif
 // Native Controls can finish capture before the overlay's poll sees the pressed Home button.
 overlay::set_open(false);std::fill(std::begin(pad),std::end(pad),0.0f);overlay::poll_controller_shortcuts(pad);
 overlay::external_binding_capture(true);pad[input_map::kPadHome]=1;
 overlay::external_binding_capture(false);overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 pad[input_map::kPadHome]=0;overlay::poll_controller_shortcuts(pad);
 pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());
 overlay::set_open(false);
 SDL_DestroyWindow(auxiliary);SDL_DestroyWindow(drc);SDL_DestroyWindow(tv);SDL_Quit();
 std::filesystem::remove(controlsFile);
}
