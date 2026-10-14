// Tests only the host input boundary. No game binary or renderer is involved.
#include <SDL3/SDL.h>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include "input.h"
#include "gfx/renderer.h"
#include "input_map.h"
#include "rumble.h"
#include "platform/input_sdl.h"
#include "platform/mouse_sdl.h"
#include "mods/mods.h"
#include "platform/android_controller.h"
#include "overlay/binding_capture.h"
#include "overlay/settings_shortcut.h"
#include "overlay/toggle_guard.h"
#include "platform/keycodes.h"
static int audioResets=0;
namespace audio {void flush(){++audioResets;}}
static int savedSlot=0, loadedSlot=0, saveRequests=0, loadRequests=0;
namespace ss { void request_save(int slot){savedSlot=slot;++saveRequests;} void request_load(int slot){loadedSlot=slot;++loadRequests;} }
static int graphicsRequests=0; static char graphicsKey=0;
namespace render { uint64_t frame_count(){return 0;} Api active(){return Api::Vulkan;} }
namespace gx2 { void checkpoint_vulkan_caches(){} }
namespace gfxvk { bool graphics_hotkey(char key,bool activate){if(activate){++graphicsRequests;graphicsKey=key;}return true;} }
namespace gfx { bool drc_window_wanted(){return true;} int plusPresses=0; void display_plus_pressed(){++plusPresses;} }  // display_modes.cpp: GamePad screen while paused
static bool mouseCamera=false, wheelOption=false;
namespace mods { double game_time(){return 0;} void filter_pad(input::PadState&){} bool mouse_camera(){return mouseCamera;} bool first_person_wheel(){return wheelOption;} void mouse_button(int,bool){} void mouse_add(float,float){} void mouse_wheel(float){} }
namespace interp { void set_mode(int){} uint64_t logic_steps(){return 0;} }
namespace timebase { uint64_t now(){return 0;} }
namespace true60 { uint64_t link_steps(){return 0;} bool state_loaded(){return false;} }  // test scenario clock (input_sdl.cpp)
namespace true60_test { void set_origin_step(uint64_t){} void tick(double,bool){} }
static int quickUseResets=0,quickUseFilters=0;static bool quickUsePending=false;
static input::PadState quickUseSample{};
namespace mods {
 void quick_use_reset(){++quickUseResets;quickUsePending=false;}
 void quick_use_filter_pad(input::PadState& s){++quickUseFilters;quickUseSample=s;if(quickUsePending){s.buttons|=input::kX;quickUsePending=false;}}
 void fast_forward_reset(){} }  // fast forward (mods/fast_forward.cpp): releases the hold on focus loss
void log_msg(const char*,...){}
// settings overlay: closed (keys reach the game and its shortcuts as before); the game's text prompt
// (overlay/text_entry.h) shows while promptShown: overlay::key takes every key then, as the real one does
static bool promptShown=false;static int promptKeys=0;static std::string promptText;
static bool overlayOpen=false;
// Matrix: real SDL routing and shared production guard; the overlay API is a lightweight model
// (no renderer/game). MENU112_BASELINE pairs the pre-fix router with its original F1 semantics.
static bool menuMatrix=false; static double keyClock=0; static overlay::ToggleGuard keyGuard;
static bool lastPointerTouch=false;
static int pointerEvents=0;
// Home / Select toggle overlayOpen in the input pump through the real SettingsShortcut (settings_shortcut.h)
static double shortcutClock=0;
// a port's controller shortcut (overlay/extensions.h) when platformShortcut: LB + RB + Select, once per
// press; the overlay keeps the inputs it took from the settings shortcut and reports the poll as taken
static bool platformShortcut=false,shortcutTook=false;static int platformShortcuts=0;
namespace overlay { void set_open(bool open,OpenReason,KeyOrigin){overlayOpen=open;} void focus_lost(){}
 void poll_controller_shortcuts(const float* v){static SettingsShortcut sc;static bool latched=false;
  using namespace input_map;bool select=v[kPadOptions]>0.5f;
  if(platformShortcut&&v[kPadLB]>0.5f&&v[kPadRB]>0.5f&&select){if(!latched){latched=true;shortcutTook=true;++platformShortcuts;}}
  if(!select)latched=false;if(latched)select=false;
  const auto a=sc.poll(shortcutClock,select,v[input_map::kPadHome]>0.5f,overlayOpen);if(a!=SettingsShortcut::None)overlayOpen=a==SettingsShortcut::Open;}
 bool shortcut_took_input(){const bool took=shortcutTook;shortcutTook=false;return took;}
 bool key(int code,bool down,bool repeat,int mods,KeyOrigin){
  if(promptShown){++promptKeys;return true;}
  if(!menuMatrix)return false;
#ifdef MENU112_BASELINE
  const bool press=down&&!repeat;
#else
  const bool press=code==kVK_F1&&keyGuard.press(down,repeat,keyClock);
#endif
  if(code==kVK_F1&&!mods){if(press)overlayOpen=!overlayOpen;return true;}
  return overlayOpen;
 } bool is_open(){return overlayOpen;} bool blocks_input(){return promptShown||overlayOpen;}
 bool captures(){return promptShown||overlayOpen;}
 bool mouse_move(float,float,bool touch){lastPointerTouch=touch;++pointerEvents;return overlayOpen;}
 bool mouse_button(int,bool,bool touch){lastPointerTouch=touch;++pointerEvents;return overlayOpen;} bool mouse_wheel(float,float){return false;}
 bool text(const char*){return false;} void set_clipboard_text(const char*){} bool wants_text(){return false;} }
namespace text_entry { bool active(){return promptShown;} void text(const char* s){promptText+=s;} void preedit(const char*){} }
namespace hostui { void graphics_changed(){} }
// screenshot.h: the binding as the real one (keys bound to Screenshot take the key)
static int shots=0;
namespace screenshot { bool key_down(int code){for(int k:input_map::current().keys[input_map::kScreenshot])if(k==code){++shots;return true;}return false;} void poll_controller(const float*){} }
// The runtime reads the C runtime's environment (getenv). On Windows SDL_setenv_unsafe only
// changes the Win32 environment block, which the CRT copy does not see.
static void set_env(const char* name,const char* value){
#ifdef _WIN32
 _putenv_s(name,value?value:"");
#else
 if(value)setenv(name,value,1);else unsetenv(name);
#endif
}
static int menu_matrix(){
 menuMatrix=true;
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 auto* tv=SDL_CreateWindow("TV",32,32,SDL_WINDOW_HIDDEN);
 auto* drc=SDL_CreateWindow("GamePad",32,32,SDL_WINDOW_HIDDEN);
 assert(tv&&drc);input::set_prompt_window(tv);
#ifndef MENU112_BASELINE
 input::set_gamepad_window(drc);
#endif
 auto reset=[](){overlayOpen=false;keyGuard=overlay::ToggleGuard{};keyClock+=1;};
 auto key=[&](SDL_Window* w,bool down,int slot=1,SDL_Keymod mods=SDL_KMOD_NONE){
  SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.windowID=SDL_GetWindowID(w);
  e.key.scancode=SDL_Scancode(SDL_SCANCODE_F1+slot-1);e.key.mod=mods;input::handle_event(e);
 };
 auto focus=[&](){SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;e.window.windowID=SDL_GetWindowID(tv);input::handle_event(e);
  e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;e.window.windowID=SDL_GetWindowID(drc);input::handle_event(e);};
 auto result=[](const char* name){printf("%s: %s\n",name,overlayOpen?"open":"closed");};
 reset();key(tv,true);focus();result("focus after F1");assert(overlayOpen);
 reset();key(tv,true);keyClock+=.016;key(tv,true);result("duplicate F1 down on TV");
#ifdef MENU112_BASELINE
 assert(!overlayOpen);
#else
 assert(overlayOpen);
#endif
 reset();key(tv,true);keyClock+=.016;key(tv,false);focus();key(tv,true);result("F1 down/up/down on TV across focus");
#ifdef MENU112_BASELINE
 assert(!overlayOpen);
#else
 assert(overlayOpen);
#endif
 reset();key(tv,true);keyClock+=.016;focus();key(drc,true);result("duplicate F1 down on GamePad");assert(overlayOpen);
 reset();key(tv,true);key(tv,false);keyClock+=.016;focus();key(drc,true);result("F1 down/up/down with second down on GamePad");assert(overlayOpen);
#ifndef MENU112_BASELINE
 reset();key(tv,true);focus();key(drc,false);keyClock+=.2;key(tv,true);
 result("release on GamePad then deliberate F1 on TV");assert(!overlayOpen);
#endif
 reset();overlay::SettingsShortcut view;
 assert(view.poll(0,true,false,false)==overlay::SettingsShortcut::None);
 assert(view.poll(.5,true,false,false)==overlay::SettingsShortcut::Open);overlayOpen=true;focus();
 assert(view.poll(.6,false,false,true)==overlay::SettingsShortcut::None);result("View hold/release across focus");assert(overlayOpen);
 reset();for(int slot=1;slot<=5;++slot)key(drc,true,slot,SDL_KMOD_SHIFT);
 for(int slot=2;slot<=5;++slot)key(drc,true,slot);
 printf("GamePad save/load requests: %d/%d\n",saveRequests,loadRequests);
#ifdef MENU112_BASELINE
 assert(saveRequests==0&&loadRequests==0);
#else
 assert(saveRequests==5&&loadRequests==4);
#endif
 promptShown=true;int saved=saveRequests,loaded=loadRequests;
 key(drc,true,1,SDL_KMOD_SHIFT);key(drc,true,2);assert(saveRequests==saved&&loadRequests==loaded);promptShown=false;
 SDL_DestroyWindow(drc);SDL_DestroyWindow(tv);SDL_Quit();return 0;
}
int main(int argc,char** argv){
 if(argc>1&&std::string(argv[1])=="--menu-matrix")return menu_matrix();
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
 assert(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_VIDEO));
 SDL_Event device{};device.type=SDL_EVENT_AUDIO_DEVICE_REMOVED;device.adevice.recording=false;
 input::handle_event(device);assert(audioResets==1);
 device.adevice.recording=true;input::handle_event(device);assert(audioResets==1);
 device.adevice.recording=false;device.type=SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED;input::handle_event(device);assert(audioResets==2);
 input::init();input_map::set_current(input_map::Mapping::defaults(),false);
 auto key=[](SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;input::handle_event(e);};
 key(SDL_SCANCODE_K,true);assert(input::read().buttons&input::kA);
 key(SDL_SCANCODE_K,false);assert(!(input::read().buttons&input::kA));
 key(SDL_SCANCODE_W,true);assert(input::read().ly==1);
 key(SDL_SCANCODE_D,true);assert(input::read().lx>0.7f&&input::read().lx<0.71f);
 key(SDL_SCANCODE_LSHIFT,true);assert(input::read().buttons&input::kZL);
 quickUsePending=true;const int focusResets=quickUseResets;
 SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;input::handle_event(focus);assert(input::read().buttons==0&&input::read().lx==0);
 assert(quickUseResets>focusResets&&!quickUsePending);
 auto map=input_map::Mapping::defaults();map.keys[input_map::kA]={input_map::key_from_id("J"),input_map::kNoKey};map.keys[input_map::kB]={input_map::kNoKey,input_map::kNoKey};input_map::set_current(map,false);
 key(SDL_SCANCODE_J,true);assert(input::read().buttons==input::kA);
 input::set_touch(true,0.25f,0.75f);auto pad=input::read();assert(pad.touch&&pad.tx==0.25f&&pad.ty==0.75f);
 input::set_pro_controller(true);assert(input::pro_controller());input::set_pro_controller(false);
 bool held[256];input::held_keys(held);assert(held[input_map::key_from_id("J")]);
 quickUsePending=true;const int releaseResets=quickUseResets;
 input::release_keys();input::held_keys(held);assert(!held[input_map::key_from_id("J")]);
 assert(quickUseResets>releaseResets&&!quickUsePending);
 // The quick-use filter sees the final mapped pad and can synthesize a button only for gameplay.
 key(SDL_SCANCODE_J,true);quickUsePending=true;auto quickPad=input::read();
 assert(quickUseSample.buttons==input::kA&&(quickPad.buttons&(input::kA|input::kX))==(input::kA|input::kX));
 int filters=quickUseFilters,resets=quickUseResets;quickUsePending=true;overlayOpen=true;
 assert(input::read().buttons==0&&quickUseFilters==filters&&quickUseResets>resets&&!quickUsePending);
 overlayOpen=false;assert(!(input::read().buttons&input::kX));
 quickUsePending=true;filters=quickUseFilters;resets=quickUseResets;promptShown=true;
 assert(input::read().buttons==0&&quickUseFilters==filters&&quickUseResets>resets&&!quickUsePending);
 promptShown=false;assert(!(input::read().buttons&input::kX));input::release_keys();
 input::update();float values[input_map::kPadCount];input::controller_values(values);assert(values[input_map::kPadA]==0);
 // Hidden dummy-driver windows exercise routing without a renderer or GPU.
 auto* game=SDL_CreateWindow("Game",32,32,SDL_WINDOW_HIDDEN);
 auto* controls=SDL_CreateWindow("Controls",32,32,SDL_WINDOW_HIDDEN);
 assert(game&&controls);input::set_prompt_window(game);
 auto* drc=SDL_CreateWindow("GamePad",32,32,SDL_WINDOW_HIDDEN);assert(drc);input::set_gamepad_window(drc);
 // Mouse binding routing, using the real camera capture boundary without a game/GPU.
 mods::mouse_init(game); mouseCamera=true;
 auto mouseMap=input_map::Mapping::defaults();
 mouseMap.keys[input_map::kB]={input_map::kMouse1,input_map::kNoKey};
 mouseMap.keys[input_map::kR]={input_map::kMouse3,input_map::kNoKey};
 mouseMap.keys[input_map::kX]={input_map::kWheelUp,input_map::kNoKey};
 mouseMap.keys[input_map::kY]={input_map::kMouse4,input_map::kNoKey};
 input_map::set_current(mouseMap,false);
 auto click=[&](SDL_Window* w,Uint8 b,bool down,SDL_MouseID device=1){SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.windowID=SDL_GetWindowID(w);e.button.which=device;e.button.button=b;input::handle_event(e);};
 auto wheel=[&](){SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.windowID=SDL_GetWindowID(game);e.wheel.y=1;input::handle_event(e);};
 click(game,SDL_BUTTON_LEFT,true);assert(mods::mouse_captured());assert(!(input::read().buttons&input::kB));
 click(game,SDL_BUTTON_LEFT,false);click(game,SDL_BUTTON_LEFT,true);assert(input::read().buttons&input::kB);
 overlayOpen=true;input::release_keys();assert(input::read().buttons==0);
 overlayOpen=false;assert(input::read().buttons==0); // held press remains gated
 click(game,SDL_BUTTON_X1,true,2);assert(input::read().buttons==input::kY);
 click(game,SDL_BUTTON_X1,false,2);click(game,SDL_BUTTON_LEFT,false);
 click(controls,SDL_BUTTON_LEFT,true);assert(input::read().buttons==0);click(controls,SDL_BUTTON_LEFT,false);
 input::set_touch(true,0.25f,0.75f);click(drc,SDL_BUTTON_LEFT,true);
 auto touched=input::read();assert(touched.buttons==0 && touched.touch && touched.tx==0.25f);
 click(drc,SDL_BUTTON_LEFT,false);input::set_touch(false,0,0);
 click(game,SDL_BUTTON_MIDDLE,true);assert(mods::mouse_captured());assert(input::read().buttons==input::kR);click(game,SDL_BUTTON_MIDDLE,false);
 wheelOption=true;wheel();assert(input::read().buttons==0);
 wheelOption=false;wheel();assert(input::read().buttons==input::kX);assert(input::read().buttons==0);
 promptShown=true;click(game,SDL_BUTTON_LEFT,true);assert(input::read().buttons==0);
 promptShown=false;assert(input::read().buttons==0);click(game,SDL_BUTTON_LEFT,false);
 mods::mouse_release();mods::update_mouse();assert(!mods::mouse_captured());
 click(game,SDL_BUTTON_X1,true);assert(input::read().buttons==0);click(game,SDL_BUTTON_X1,false);
 mouseCamera=false;input_map::set_current(map,false);SDL_DestroyWindow(drc);input::set_gamepad_window(nullptr);
 // Touch-emulated pointers keep their source through the overlay boundary; a real mouse restores hover.
 overlayOpen=true;
 for(SDL_MouseID id : {SDL_TOUCH_MOUSEID, SDL_MouseID(1)}) {
  SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=SDL_GetWindowID(game);e.motion.which=id;
  int before=pointerEvents;input::handle_event(e);
  assert(pointerEvents==before+1 && lastPointerTouch==(id==SDL_TOUCH_MOUSEID));
  e={};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(game);
  e.button.which=id;e.button.button=SDL_BUTTON_LEFT;
  before=pointerEvents;input::handle_event(e);
  assert(pointerEvents==before+2 && lastPointerTouch==(id==SDL_TOUCH_MOUSEID));
 }
 overlayOpen=false;
 auto stateKey=[&](int slot,SDL_Keymod mod,bool repeat=false,SDL_Window* window=nullptr,Uint32 type=SDL_EVENT_KEY_DOWN){
  SDL_Event e{};e.type=type;e.key.scancode=SDL_Scancode(SDL_SCANCODE_F1+slot-1);
  e.key.windowID=SDL_GetWindowID(window?window:game);e.key.mod=mod;e.key.repeat=repeat;input::handle_event(e);
 };
 for(int slot=1;slot<=5;++slot){
  stateKey(slot,SDL_KMOD_NONE);assert(loadedSlot==slot&&loadRequests==slot);
  stateKey(slot,SDL_KMOD_LSHIFT);assert(savedSlot==slot&&saveRequests==2*slot-1);
  stateKey(slot,SDL_KMOD_RSHIFT);assert(savedSlot==slot&&saveRequests==2*slot);
 }
 const int saves=saveRequests,loads=loadRequests;
 stateKey(1,SDL_KMOD_LSHIFT,true);stateKey(1,SDL_KMOD_NONE,false,controls);
 stateKey(1,SDL_KMOD_LSHIFT,false,nullptr,SDL_EVENT_KEY_UP);
 stateKey(6,SDL_KMOD_NONE);input::set_prompt_window(nullptr);stateKey(1,SDL_KMOD_NONE);
 assert(saveRequests==saves&&loadRequests==loads);
 input::set_prompt_window(game);input::release_keys();
 for(int slot=1;slot<=5;++slot)stateKey(slot,SDL_KMOD_NONE,true);
 input::held_keys(held);
 for(const char* id:{"F1","F2","F3","F4","F5"})assert(!held[input_map::key_from_id(id)]);
 auto graphicsEvent=[&](SDL_Scancode code,SDL_Window* window, bool repeat=false, Uint32 type=SDL_EVENT_KEY_DOWN,SDL_Keymod mod=SDL_KMOD_NONE){SDL_Event e{};e.type=type;e.key.scancode=code;e.key.windowID=SDL_GetWindowID(window);e.key.repeat=repeat;e.key.mod=mod;input::handle_event(e);};
 for(auto code:{SDL_SCANCODE_R,SDL_SCANCODE_O,SDL_SCANCODE_M,SDL_SCANCODE_N,SDL_SCANCODE_8,SDL_SCANCODE_6,SDL_SCANCODE_7}){
  int before=graphicsRequests;graphicsEvent(code,game);assert(graphicsRequests==before+1);
  graphicsEvent(code,game,true);graphicsEvent(code,game,false,SDL_EVENT_KEY_UP);graphicsEvent(code,controls);graphicsEvent(code,game,false,SDL_EVENT_KEY_DOWN,SDL_KMOD_CTRL);assert(graphicsRequests==before+1);
 }
 assert(graphicsKey=='7');input::release_keys();
 // Screenshot (F10 by default): one per press, never a held game key; rebinding moves it
 graphicsEvent(SDL_SCANCODE_F10,game);graphicsEvent(SDL_SCANCODE_F10,game,true);assert(shots==1);
 input::held_keys(held);assert(!held[input_map::key_from_id("F10")]);
 graphicsEvent(SDL_SCANCODE_F10,game,false,SDL_EVENT_KEY_UP);
 {auto m=input_map::current();m.keys[input_map::kScreenshot]={input_map::key_from_id("F9"),input_map::kNoKey};input_map::set_current(m,false);
  graphicsEvent(SDL_SCANCODE_F10,game);assert(shots==1);graphicsEvent(SDL_SCANCODE_F9,game);assert(shots==2);
  m.keys[input_map::kScreenshot]={input_map::key_from_id("F10"),input_map::kNoKey};input_map::set_current(m,false);input::release_keys();}
 set_env("WWHD_NO_HOST_INPUT","1");graphicsEvent(SDL_SCANCODE_R,game);assert(graphicsRequests==7);set_env("WWHD_NO_HOST_INPUT",nullptr);
 // the text prompt: typed text goes to it, keys of every game window to overlay::key, none to the game
 promptShown=true;input::update();
 SDL_Event typed{};typed.type=SDL_EVENT_TEXT_INPUT;typed.text.text="Link";input::handle_event(typed);assert(promptText=="Link");
 key(SDL_SCANCODE_K,true);graphicsEvent(SDL_SCANCODE_RETURN,controls);assert(promptKeys==2);
 input::held_keys(held);assert(!held[input_map::key_from_id("K")]&&!(input::read().buttons&input::kA));
 promptShown=false;input::update();
 // the key that confirmed is still held: its repeats don't press it in the game
 graphicsEvent(SDL_SCANCODE_RETURN,game,true);input::held_keys(held);assert(!held[input_map::key_from_id("Return")]);
 graphicsEvent(SDL_SCANCODE_RETURN,game,false,SDL_EVENT_KEY_UP);
 // rumble (issue #35): a virtual controller with a motor follows the game's requests and stops on
 // every path the game cannot stop it from (option off, overlay open, no focus, quit)
 static std::atomic<Uint16> motorLow{0},motorHigh{0};static std::atomic<int> motorCalls{0};
 SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
 desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.name="rumble test pad";
 desc.Rumble=[](void*,Uint16 low,Uint16 high){motorLow=low;motorHigh=high;++motorCalls;return true;};
 const SDL_JoystickID padId=SDL_AttachVirtualJoystick(&desc);assert(padId);
 for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);  // SDL_EVENT_GAMEPAD_ADDED opens it
 SDL_ShowWindow(game);for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
 const bool focused=SDL_GetKeyboardFocus()!=nullptr;  // the dummy driver may give no window the keyboard
 const uint8_t on[15]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
 rumble::set_enabled(true);rumble::gamepad_pattern(0,on,120);input::update();
 if(focused){
  assert(motorLow==0xFFFF&&motorHigh==0xFFFF);
  rumble::set_enabled(false);input::update();assert(motorLow==0);  // the option stops it at once
  rumble::set_enabled(true);input::update();assert(motorLow==0xFFFF);
  overlayOpen=true;input::update();assert(motorLow==0);overlayOpen=false;
  input::update();assert(motorLow==0xFFFF);
  rumble::gamepad_stop(0);input::update();assert(motorLow==0);
  const int calls=motorCalls;input::update();input::update();assert(motorCalls==calls);  // still: nothing more sent
  rumble::pro_motor(0,true);input::update();assert(motorLow==0xFFFF);
  SDL_HideWindow(game);for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
  if(!SDL_GetKeyboardFocus()){input::update();assert(motorLow==0);SDL_ShowWindow(game);for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);}
  input::update();assert(motorLow==0xFFFF);
  SDL_Delay(600);assert(motorLow==0);  // no update for a while (main loop stalled): the watchdog stops it
  rumble::pro_motor(0,true);input::update();assert(motorLow==0xFFFF);  // (the game sends it every frame)
  SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);assert(motorLow==0);  // the app ends now
  input::update();assert(motorLow==0);
 }else{
  assert(motorLow==0);puts("input_sdl_test: no window has the keyboard here, rumble checked with the motors still only");
 }
 rumble::reset();
 // Issue #40: real SDL virtual gamepads, through polling and configured bindings.
 // Dummy video keeps the test headless; enable joystick updates without focus.
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
 input::release_keys();input_map::set_current(input_map::Mapping::defaults(),false);
 for(const auto layout:{SDL_GAMEPAD_TYPE_XBOXONE,SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO}){
  SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);
  d.type=SDL_JOYSTICK_TYPE_GAMEPAD;d.naxes=SDL_GAMEPAD_AXIS_COUNT;d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
  d.vendor_id=layout==SDL_GAMEPAD_TYPE_XBOXONE?0x045e:0x057e;
  d.product_id=layout==SDL_GAMEPAD_TYPE_XBOXONE?0x02ea:0x2009;
  d.name=layout==SDL_GAMEPAD_TYPE_XBOXONE?"Xbox virtual #40":"Switch Pro virtual #40";
  auto id=SDL_AttachVirtualJoystick(&d);assert(id);
  for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
  auto* joystick=SDL_OpenJoystick(id);assert(joystick);
  assert(SDL_GetGamepadType(SDL_GetGamepadFromID(id))==layout);
  auto poll=[&](){SDL_UpdateJoysticks();for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);input::update();return input::read();};
  const SDL_GamepadButton dirs[]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
  const uint32_t bits[]={input::kUp,input::kDown,input::kLeft,input::kRight};
  for(bool pro:{false,true})for(auto face:{input_map::FaceLayout::kPosition,input_map::FaceLayout::kLabels}){
   input::set_pro_controller(pro);auto m=input_map::Mapping::defaults();input_map::apply_face_layout(m,face);input_map::set_current(m,false);
   for(int i=0;i<4;++i){
    assert(SDL_SetJoystickVirtualButton(joystick,dirs[i],true));auto p=poll();assert(p.buttons==bits[i]);assert(p.lx==0&&p.ly==0);
    printf("#40 layout=%d pro=%d face=%d dpad=%d buttons=%08X left=%.3f,%.3f\n",int(layout),pro,int(face),i,p.buttons,p.lx,p.ly);
    assert(SDL_SetJoystickVirtualButton(joystick,dirs[i],false));assert(poll().buttons==0);
    for(bool right:{false,true}){
     const auto axis=right?(i<2?SDL_GAMEPAD_AXIS_RIGHTY:SDL_GAMEPAD_AXIS_RIGHTX):(i<2?SDL_GAMEPAD_AXIS_LEFTY:SDL_GAMEPAD_AXIS_LEFTX);
     assert(SDL_SetJoystickVirtualAxis(joystick,axis,(i==0||i==2)?-32768:32767));p=poll();
     const float x=i==2?-1.f:i==3?1.f:0.f,y=i==0?1.f:i==1?-1.f:0.f;
     assert(std::abs((right?p.rx:p.lx)-x)<0.001f&&std::abs((right?p.ry:p.ly)-y)<0.001f&&p.buttons==0);
     printf("#40 layout=%d pro=%d face=%d stick=%s direction=%d buttons=%08X axes=%.3f,%.3f\n",int(layout),pro,int(face),right?"right":"left",i,p.buttons,right?p.rx:p.lx,right?p.ry:p.ly);
     assert(SDL_SetJoystickVirtualAxis(joystick,axis,0));p=poll();assert(p.lx==0&&p.ly==0&&p.rx==0&&p.ry==0);
    }
   }
  }
  // Binding edits must also reach the game: route D-pad down to stick-down.
  auto rebound=input_map::Mapping::defaults();rebound.pad[input_map::kDDown]=input_map::kPadNone;
  rebound.pad[input_map::kLDown]=input_map::kPadDDown;input_map::set_current(rebound,false);
  assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));
  auto reboundState=poll();assert(reboundState.buttons==0&&reboundState.ly==-1);
  assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll();
  input_map::set_current(input_map::Mapping::defaults(),false);
  SDL_CloseJoystick(joystick);assert(SDL_DetachVirtualJoystick(id));poll();
 }
 // Thor/Odin's Android raw letter order, through the same narrow SDL mapping correction.
 {
  SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);
  d.type=SDL_JOYSTICK_TYPE_GAMEPAD;d.naxes=SDL_GAMEPAD_AXIS_COUNT;d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
  d.vendor_id=0x2020;d.product_id=0x0111;d.name="Odin Controller fixture";
  auto id=SDL_AttachVirtualJoystick(&d);assert(id);
  char* original=SDL_GetGamepadMappingForID(id);assert(original);
  const auto corrected=android_controller::mapping(0x2020,0x0111,original);SDL_free(original);
  assert(SDL_AddGamepadMapping(corrected.c_str())>=0);
  for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
  auto* joystick=SDL_OpenJoystick(id);assert(joystick);
  auto* pad=SDL_GetGamepadFromID(id);assert(pad);
  const SDL_GamepadButton positions[]={SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH};
  const SDL_GamepadButtonLabel printed[]={SDL_GAMEPAD_BUTTON_LABEL_B,SDL_GAMEPAD_BUTTON_LABEL_A,SDL_GAMEPAD_BUTTON_LABEL_Y,SDL_GAMEPAD_BUTTON_LABEL_X};
  for(int i=0;i<4;++i)assert(SDL_GetGamepadButtonLabel(pad,positions[i])==printed[i]);
  auto poll=[&](){SDL_UpdateJoysticks();for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);input::update();};
  // Android BUTTON_A/B/X/Y raw indices; Nintendo labels match each game's action.
  const uint32_t bits[]={input::kA,input::kB,input::kX,input::kY};
  for(int raw=0;raw<4;++raw){
   auto m=input_map::Mapping::defaults();input_map::apply_face_layout(m,input_map::FaceLayout::kPosition);input_map::set_current(m,false);
   poll();float values[input_map::kPadCount]={};input::host_controller_values(values);
   overlay::BindingCapture capture;assert(capture.poll(values)==-1);
   assert(SDL_SetJoystickVirtualButton(joystick,raw,true));poll();
   assert(input::read().buttons==bits[raw]);
   input::host_controller_values(values);const int captured=capture.poll(values);assert(captured>0);
   input_map::set_pad_binding(m,input_map::kX,captured);input_map::set_current(m,false);poll();
   assert(input::read().buttons&input::kX);
   assert(SDL_SetJoystickVirtualButton(joystick,raw,false));poll();
  }
  SDL_CloseJoystick(joystick);assert(SDL_DetachVirtualJoystick(id));poll();
  input_map::set_current(input_map::Mapping::defaults(),false);
 }
 // issue #153: after the settings overlay or the name prompt had the input, the game gets the controllers
 // back; only an input still held from the overlay waits for its release, per device
 {
  auto attach=[](const char* name){SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);
   d.type=SDL_JOYSTICK_TYPE_GAMEPAD;d.naxes=SDL_GAMEPAD_AXIS_COUNT;d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
   d.vendor_id=0x045e;d.product_id=0x02ea;d.name=name;auto id=SDL_AttachVirtualJoystick(&d);assert(id);
   auto* j=SDL_OpenJoystick(id);assert(j);return std::make_pair(id,j);};
  auto [id1,pad1]=attach("Xbox virtual #153 one");
  auto [id2,pad2]=attach("Xbox virtual #153 two");
  auto poll=[&](){SDL_UpdateJoysticks();for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);input::update();return input::read();};
  auto btn=[&](SDL_Joystick* j,SDL_GamepadButton b,bool down){assert(SDL_SetJoystickVirtualButton(j,b,down));return poll();};
  input_map::set_current(input_map::Mapping::defaults(),false);
  // the default layout maps by position: south (Xbox A) is the Wii U's B, east its A, north its X
  const uint32_t S=input::kB,E=input::kA,N=input::kX;
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==S);assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,false).buttons==0);
  // the reporter's pad: an input that rests above 0.3 (a trigger here; the game presses ZR above 0.5)
  // (a virtual trigger's axis spans -32768..32767: -6500 reads 0.4)
  assert(SDL_SetJoystickVirtualAxis(pad1,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-6500));assert(poll().buttons==0);
  {float vv[input_map::kPadCount];input::host_controller_values(vv);assert(vv[input_map::kPadRT]>0.35f&&vv[input_map::kPadRT]<0.45f);}
  // F1 / Esc (keys, the overlay's own): open, close, then a press reaches the game at once
  for(int i=0;i<2;i++){
   overlayOpen=true;assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==0);assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,false).buttons==0);
   overlayOpen=false;assert(poll().buttons==0);
   assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==S);assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,false).buttons==0);
   assert(btn(pad1,SDL_GAMEPAD_BUTTON_START,true).buttons==input::kPlus);assert(btn(pad1,SDL_GAMEPAD_BUTTON_START,false).buttons==0);
  }
  // B closes the menu (the overlay's navigation) and is still held: it stays out until released, the
  // other inputs (and the other pad) reach the game meanwhile
  overlayOpen=true;btn(pad1,SDL_GAMEPAD_BUTTON_EAST,true);overlayOpen=false;
  assert(poll().buttons==0);assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==S);
  assert(btn(pad2,SDL_GAMEPAD_BUTTON_EAST,true).buttons==(S|E));  // pad two's own east button
  btn(pad2,SDL_GAMEPAD_BUTTON_EAST,false);assert(poll().buttons==S);
  btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,false);assert(btn(pad1,SDL_GAMEPAD_BUTTON_EAST,false).buttons==0);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_EAST,true).buttons==E);btn(pad1,SDL_GAMEPAD_BUTTON_EAST,false);
  // Home opens and closes in the input pump: the closing press is the overlay's, not the game's
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_GUIDE,true).buttons==0&&overlayOpen);btn(pad1,SDL_GAMEPAD_BUTTON_GUIDE,false);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_GUIDE,true).buttons==0&&!overlayOpen);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==S);btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,false);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_GUIDE,false).buttons==0);
  // Select: held half a second opens, a press closes; that press (Minus in the game) waits for its release
  btn(pad2,SDL_GAMEPAD_BUTTON_BACK,true);shortcutClock+=0.6;poll();assert(overlayOpen);
  btn(pad2,SDL_GAMEPAD_BUTTON_BACK,false);assert(btn(pad2,SDL_GAMEPAD_BUTTON_BACK,true).buttons==0&&!overlayOpen);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_START,true).buttons==input::kPlus);btn(pad1,SDL_GAMEPAD_BUTTON_START,false);
  assert(btn(pad2,SDL_GAMEPAD_BUTTON_BACK,false).buttons==0);assert(btn(pad2,SDL_GAMEPAD_BUTTON_BACK,true).buttons==input::kMinus);
  btn(pad2,SDL_GAMEPAD_BUTTON_BACK,false);
  // a port's shortcut (overlay/extensions.h, issue #173) took LB + RB + Select: like the overlay's input, the
  // three stay from the game until each is released (and Select held does not open the menu)
  platformShortcut=true;
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true).buttons==input::kL);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true).buttons==(input::kL|input::kR));
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_BACK,true).buttons==0&&platformShortcuts==1);
  shortcutClock+=0.6;assert(poll().buttons==0&&!overlayOpen&&platformShortcuts==1);
  assert(btn(pad2,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==S);btn(pad2,SDL_GAMEPAD_BUTTON_SOUTH,false);  // the other pad meanwhile
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_BACK,false).buttons==0);  // L and R still held: still kept
  btn(pad1,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);assert(btn(pad1,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true).buttons==input::kL);
  btn(pad1,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);btn(pad1,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false);
  assert(btn(pad1,SDL_GAMEPAD_BUTTON_BACK,true).buttons==input::kMinus);btn(pad1,SDL_GAMEPAD_BUTTON_BACK,false);
  platformShortcut=false;
  // the name prompt: A / Start confirms (the prompt closes while it is held), with either pad
  for(auto [j,b,bit]:{std::make_tuple(pad1,SDL_GAMEPAD_BUTTON_SOUTH,S),std::make_tuple(pad2,SDL_GAMEPAD_BUTTON_START,uint32_t(input::kPlus))}){
   promptShown=true;poll();assert(btn(j,b,true).buttons==0);promptShown=false;
   for(int i=0;i<3;i++)assert(poll().buttons==0);  // held: not a press in the game
   assert(btn(j,b,false).buttons==0);assert(btn(j,b,true).buttons==bit);btn(j,b,false);
  }
  // Enter confirms on the keyboard: the controllers are the game's at once, Enter's repeats press nothing
  promptShown=true;poll();graphicsEvent(SDL_SCANCODE_RETURN,game);promptShown=false;graphicsEvent(SDL_SCANCODE_RETURN,game,true);
  assert(!(poll().buttons&input::kPlus));assert(btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,true).buttons==S);
  btn(pad1,SDL_GAMEPAD_BUTTON_SOUTH,false);graphicsEvent(SDL_SCANCODE_RETURN,game,false,SDL_EVENT_KEY_UP);
  graphicsEvent(SDL_SCANCODE_RETURN,game);assert(poll().buttons==input::kPlus);graphicsEvent(SDL_SCANCODE_RETURN,game,false,SDL_EVENT_KEY_UP);
  // a held stick across the close: kept until it returns, then the game's again
  overlayOpen=true;assert(SDL_SetJoystickVirtualAxis(pad2,SDL_GAMEPAD_AXIS_LEFTX,32767));assert(poll().lx==0);overlayOpen=false;
  assert(poll().lx==0);assert(SDL_SetJoystickVirtualAxis(pad2,SDL_GAMEPAD_AXIS_LEFTX,0));poll();
  assert(SDL_SetJoystickVirtualAxis(pad2,SDL_GAMEPAD_AXIS_LEFTX,32767));assert(poll().lx>0.9f);
  assert(SDL_SetJoystickVirtualAxis(pad2,SDL_GAMEPAD_AXIS_LEFTX,0));poll();
  // replug: a button held across the close on a pad that reconnects (new gate) is the game's again
  overlayOpen=true;btn(pad1,SDL_GAMEPAD_BUTTON_NORTH,true);overlayOpen=false;assert(poll().buttons==0);
  SDL_CloseJoystick(pad1);assert(SDL_DetachVirtualJoystick(id1));poll();
  auto [id3,pad3]=attach("Xbox virtual #153 one again");poll();
  assert(btn(pad3,SDL_GAMEPAD_BUTTON_NORTH,true).buttons==N);btn(pad3,SDL_GAMEPAD_BUTTON_NORTH,false);
  SDL_CloseJoystick(pad3);assert(SDL_DetachVirtualJoystick(id3));SDL_CloseJoystick(pad2);assert(SDL_DetachVirtualJoystick(id2));poll();
 }
 input::set_pro_controller(false);
 SDL_DestroyWindow(controls);SDL_DestroyWindow(game);input::set_prompt_window(nullptr);
 SDL_Quit();puts("input_sdl_test: keyboard mapping, focus, touch, Pro mode, guarded save-state shortcuts, screenshot binding, text prompt routing, rumble, overlay hand-back (#153), platform shortcut hand-back (#173) passed");
}
