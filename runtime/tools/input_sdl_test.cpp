// Tests only the host input boundary. No game binary or renderer is involved.
#include <SDL3/SDL.h>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "input.h"
#include "input_map.h"
#include "rumble.h"
#include "platform/input_sdl.h"
static int audioResets=0;
namespace audio {void flush(){++audioResets;}}
static int savedSlot=0, loadedSlot=0, saveRequests=0, loadRequests=0;
namespace ss { void request_save(int slot){savedSlot=slot;++saveRequests;} void request_load(int slot){loadedSlot=slot;++loadRequests;} }
static int graphicsRequests=0; static char graphicsKey=0;
namespace render { uint64_t frame_count(){return 0;} }
namespace gfxvk { bool graphics_hotkey(char key,bool activate){if(activate){++graphicsRequests;graphicsKey=key;}return true;} }
namespace gfx { int plusPresses=0; void display_plus_pressed(){++plusPresses;} }  // display_modes.cpp: GamePad screen while paused
namespace mods { double game_time(){return 0;} void filter_pad(input::PadState&){} bool mouse_camera(){return false;} bool first_person_wheel(){return false;} void mouse_button(int,bool){} void mouse_add(float,float){} void mouse_wheel(float){} }
namespace interp { void set_mode(int){} uint64_t logic_steps(){return 0;} }
namespace timebase { uint64_t now(){return 0;} }
namespace true60 { uint64_t link_steps(){return 0;} bool state_loaded(){return false;} }  // test scenario clock (input_sdl.cpp)
namespace true60_test { void set_origin_step(uint64_t){} void tick(double,bool){} }
namespace mods { void fast_forward_reset(){} }  // fast forward (mods/fast_forward.cpp): releases the hold on focus loss
void log_msg(const char*,...){}
// settings overlay: closed (keys reach the game and its shortcuts as before); the game's text prompt
// (overlay/text_entry.h) shows while promptShown: overlay::key takes every key then, as the real one does
static bool promptShown=false;static int promptKeys=0;static std::string promptText;
static bool overlayOpen=false;
namespace overlay { bool key(int,bool,bool,int){if(promptShown)++promptKeys;return promptShown;} bool is_open(){return overlayOpen;} bool blocks_input(){return promptShown;}
 bool captures(){return promptShown;}
 bool mouse_move(float,float){return false;} bool mouse_button(int,bool){return false;} bool mouse_wheel(float,float){return false;}
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
int main(){
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
 SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;input::handle_event(focus);assert(input::read().buttons==0&&input::read().lx==0);
 auto map=input_map::Mapping::defaults();map.keys[input_map::kA]={input_map::key_from_id("J"),input_map::kNoKey};map.keys[input_map::kB]={input_map::kNoKey,input_map::kNoKey};input_map::set_current(map,false);
 key(SDL_SCANCODE_J,true);assert(input::read().buttons==input::kA);
 input::set_touch(true,0.25f,0.75f);auto pad=input::read();assert(pad.touch&&pad.tx==0.25f&&pad.ty==0.75f);
 input::set_pro_controller(true);assert(input::pro_controller());input::set_pro_controller(false);
 bool held[256];input::held_keys(held);assert(held[input_map::key_from_id("J")]);
 input::release_keys();input::held_keys(held);assert(!held[input_map::key_from_id("J")]);
 input::update();float values[input_map::kPadCount];input::controller_values(values);assert(values[input_map::kPadA]==0);
 // Hidden dummy-driver windows exercise routing without a renderer or GPU.
 auto* game=SDL_CreateWindow("Game",32,32,SDL_WINDOW_HIDDEN);
 auto* controls=SDL_CreateWindow("Controls",32,32,SDL_WINDOW_HIDDEN);
 assert(game&&controls);input::set_prompt_window(game);
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
 input::set_pro_controller(false);
 SDL_DestroyWindow(controls);SDL_DestroyWindow(game);input::set_prompt_window(nullptr);
 SDL_Quit();puts("input_sdl_test: keyboard mapping, focus, touch, Pro mode, guarded save-state shortcuts, screenshot binding, text prompt routing, rumble passed");
}
