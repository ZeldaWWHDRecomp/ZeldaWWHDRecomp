#include "file_select_quit.h"
#include "platform/process_peak_rss.h"
#include "platform/present_observer.h"
#ifdef __ANDROID__
#include <jni.h>
#include "phone_rumble.h"
#include "overlay/hostui.h"
#include "quest_theater.h"
#endif
#include "gfx/display_modes.h"
#include "android_controller.h"
#include "../mods/fast_forward.h"
#include "../mods/mods.h"
#ifdef WWHD_HAS_VULKAN
#include "gfx/vulkan/stutter_trace.h"
#include "gfx/vulkan/headstart.h"
namespace gx2 { void checkpoint_vulkan_caches(); void checkpoint_present_observer(); }
#endif
// SDL3 keyboard/gamepad input. Stable key IDs preserve existing controls.json mappings.
#include "input_sdl.h"
#include "overlay_trace_sdl.h"
#include "overlay/window_target.h"
#include "gfx/display_modes.h"
#include "keycodes.h"
#include "mouse_sdl.h"
#include "../input.h"
#include "../audio_out.h"
#include "../input_map.h"
#include "../mouse_bindings.h"
#include "../mods/mods.h"
#include "../motion/motion.h"
#include "../rumble.h"
#include "../runtime.h"
#include "../savestate.h"
#include "../true60.h"
#include "../screenshot.h"
#include "../gfx/renderer.h"
#include "../gfx/vulkan/settings.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../overlay/release_gate.h"
#include "../overlay/text_entry.h"
#include "../fixed_seed.h"
#include "../dsweep.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>
namespace render { uint64_t frame_count(); }
namespace gfxvk { bool graphics_hotkey(char key, bool activate); }
namespace gfx { void display_plus_pressed(); }  // display_modes.cpp: the GamePad screen while paused
namespace mods { void filter_pad(input::PadState&); }
namespace input {
static std::mutex g_mu;
static MouseBindings g_mouse_bindings;
void release_mouse_bindings() { std::lock_guard lk(g_mu); g_mouse_bindings.consume(); }
void mouse_binding_button(uint64_t device, int button, bool down, bool eligible) {
    std::lock_guard lk(g_mu); g_mouse_bindings.button(device, button, down, eligible);
}
void mouse_binding_wheel(float delta, bool eligible) {
    std::lock_guard lk(g_mu); g_mouse_bindings.wheel(delta, eligible);
}
static bool g_keys[256]={},g_script_keys[256]={};
static std::atomic<bool> g_gameplay_focus{false},g_window_focus{false};
bool gameplay_focused(){return g_gameplay_focus.load(std::memory_order_relaxed);}
bool window_focused(){return g_window_focus.load(std::memory_order_relaxed);}
// ---- controllers in the background (issue #267) ---------------------------------------------------
// SDL drops controller presses and stick moves while it has windows and none of them has the keyboard
// focus, unless SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS is set. SDL watches that hint (a hint
// callback), so it applies at once, also after the joystick subsystem started. Controls > "Controller
// input when the game isn't focused" sets it; keyboard and mouse are released on focus loss either
// way (handle_event, mouse_sdl.cpp). Android: the app is in front or paused, the option does nothing.
bool background_controllers(){
#ifdef __ANDROID__
 return false;
#else
 return input_map::background_controllers();
#endif
}
static void apply_background_hint(){
 static int applied=-1;
 const bool on=background_controllers();
 if(applied==(int)on)return;
 if(!SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,on?"1":"0"))LOG("[input] background controller hint: %s",SDL_GetError());
 if(applied>=0||on)LOG("[input] controller input while the game isn't focused: %s",on?"on":"off");
 applied=on;
}
// the controllers reach the game now: a game window has the focus, or the background option is on
static bool controllers_live(){return SDL_GetKeyboardFocus()!=nullptr||background_controllers();}
static PadState g_pad;
static float g_values[input_map::kPadCount]={};
static bool g_touch=false;static float g_tx=0,g_ty=0;
static std::map<SDL_JoystickID,SDL_Gamepad*> g_controllers;
static overlay::DeviceGates<SDL_JoystickID> g_gates;  // update(): one per device, see overlay/release_gate.h
static std::vector<SDL_JoystickID> g_order;  // connect order: the oldest still present is dominant
static std::set<SDL_JoystickID> g_rumble_controllers;  // the ones that have a rumble motor
// per-controller bindings (input_map profiles) and Controller details: what identifies each pad
struct PadIdentity { std::string guid, name; bool is_virtual = false; input::ControllerInfo info; };
static std::map<SDL_JoystickID,input_map::StartGate> g_start_gates;
static std::map<SDL_JoystickID,PadIdentity> g_ident;  // under g_pads_mu
static std::vector<input::ControllerInfo> g_infos;    // the latest poll, under g_mu
static std::atomic<uint32_t> g_touch_virt{0};         // the on-screen Jump / Moon jump buttons
static std::set<SDL_JoystickID> g_offer_pending;      // new controllers: the setup is offered on their first press
#ifdef __ANDROID__
// Android: TouchControls announced a real game controller (ControllerHintPolicy): only then is a pad
// offered the setup (a keyboard with d-pad keys that SDL lists as a gamepad never is)
static std::atomic<bool> g_controller_announced{false};
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeControllerSetupOffer(JNIEnv*, jclass) {
 g_controller_announced=true;
}
#endif
static SDL_Window* g_prompt_window=nullptr;
static SDL_Window* g_gamepad_window=nullptr;
static void update_overlay_window(SDL_Window* focus);
void set_gamepad_window(SDL_Window* window){g_gamepad_window=window;update_overlay_window(SDL_GetKeyboardFocus());}
static bool game_window(SDL_WindowID id){return (g_prompt_window&&id==SDL_GetWindowID(g_prompt_window))||(g_gamepad_window&&id==SDL_GetWindowID(g_gamepad_window));}
static const char* window_name(SDL_WindowID id){return g_prompt_window&&id==SDL_GetWindowID(g_prompt_window)?"TV":g_gamepad_window&&id==SDL_GetWindowID(g_gamepad_window)?"GamePad":"other";}
static std::function<void(bool,std::u16string)> g_pending,g_done;
static std::u16string g_initial,g_text;
static int g_max_len=0,g_pending_max_len=0;
static std::string g_previous_title;
void set_prompt_window(SDL_Window* window){g_prompt_window=window;update_overlay_window(SDL_GetKeyboardFocus());}
static void update_overlay_window(SDL_Window* focus) {
 using namespace overlay::window_target;
 static const bool follow = gamescope_session();
 if(!follow)return; // desktop/macOS keep the original TV destination and avoid window queries
 auto visible=[](SDL_Window* w){
  if(!w || (SDL_GetWindowFlags(w)&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED)))return false;
  int width=0,height=0;SDL_GetWindowSizeInPixels(w,&width,&height);
  return width>=16 && height>=16; // overlay::frame cannot draw below this size
 };
 const Window focused=focus==g_prompt_window&&focus?Window::TV:focus==g_gamepad_window&&focus?Window::GamePad:Window::None;
 const Window target=choose(follow,g_gamepad_window&&gfx::drc_window_wanted(),focused,visible(g_prompt_window),visible(g_gamepad_window));
 selected.store(target);
 if(target==Window::None && overlay::is_open())overlay::set_open(false);
}
bool text_prompt_active(){return (bool)g_done;}
static int keycode(SDL_Scancode code) {
 switch(code) {
 case SDL_SCANCODE_A: return kVK_ANSI_A;
 case SDL_SCANCODE_B: return kVK_ANSI_B;
 case SDL_SCANCODE_C: return kVK_ANSI_C;
 case SDL_SCANCODE_D: return kVK_ANSI_D;
 case SDL_SCANCODE_E: return kVK_ANSI_E;
 case SDL_SCANCODE_F: return kVK_ANSI_F;
 case SDL_SCANCODE_G: return kVK_ANSI_G;
 case SDL_SCANCODE_H: return kVK_ANSI_H;
 case SDL_SCANCODE_I: return kVK_ANSI_I;
 case SDL_SCANCODE_J: return kVK_ANSI_J;
 case SDL_SCANCODE_K: return kVK_ANSI_K;
 case SDL_SCANCODE_L: return kVK_ANSI_L;
 case SDL_SCANCODE_M: return kVK_ANSI_M;
 case SDL_SCANCODE_N: return kVK_ANSI_N;
 case SDL_SCANCODE_O: return kVK_ANSI_O;
 case SDL_SCANCODE_P: return kVK_ANSI_P;
 case SDL_SCANCODE_Q: return kVK_ANSI_Q;
 case SDL_SCANCODE_R: return kVK_ANSI_R;
 case SDL_SCANCODE_S: return kVK_ANSI_S;
 case SDL_SCANCODE_T: return kVK_ANSI_T;
 case SDL_SCANCODE_U: return kVK_ANSI_U;
 case SDL_SCANCODE_V: return kVK_ANSI_V;
 case SDL_SCANCODE_W: return kVK_ANSI_W;
 case SDL_SCANCODE_X: return kVK_ANSI_X;
 case SDL_SCANCODE_Y: return kVK_ANSI_Y;
 case SDL_SCANCODE_Z: return kVK_ANSI_Z;
 case SDL_SCANCODE_0: return kVK_ANSI_0;
 case SDL_SCANCODE_1: return kVK_ANSI_1;
 case SDL_SCANCODE_2: return kVK_ANSI_2;
 case SDL_SCANCODE_3: return kVK_ANSI_3;
 case SDL_SCANCODE_4: return kVK_ANSI_4;
 case SDL_SCANCODE_5: return kVK_ANSI_5;
 case SDL_SCANCODE_6: return kVK_ANSI_6;
 case SDL_SCANCODE_7: return kVK_ANSI_7;
 case SDL_SCANCODE_8: return kVK_ANSI_8;
 case SDL_SCANCODE_9: return kVK_ANSI_9;
 case SDL_SCANCODE_MINUS: return kVK_ANSI_Minus;
 case SDL_SCANCODE_EQUALS: return kVK_ANSI_Equal;
 case SDL_SCANCODE_LEFTBRACKET: return kVK_ANSI_LeftBracket;
 case SDL_SCANCODE_RIGHTBRACKET: return kVK_ANSI_RightBracket;
 case SDL_SCANCODE_BACKSLASH: return kVK_ANSI_Backslash;
 case SDL_SCANCODE_SEMICOLON: return kVK_ANSI_Semicolon;
 case SDL_SCANCODE_APOSTROPHE: return kVK_ANSI_Quote;
 case SDL_SCANCODE_COMMA: return kVK_ANSI_Comma;
 case SDL_SCANCODE_PERIOD: return kVK_ANSI_Period;
 case SDL_SCANCODE_SLASH: return kVK_ANSI_Slash;
 case SDL_SCANCODE_GRAVE: return kVK_ANSI_Grave;
 case SDL_SCANCODE_NONUSBACKSLASH: return kVK_ISO_Section;
 case SDL_SCANCODE_SPACE: return kVK_Space;
 case SDL_SCANCODE_RETURN: return kVK_Return;
 case SDL_SCANCODE_TAB: return kVK_Tab;
 case SDL_SCANCODE_BACKSPACE: return kVK_Delete;
 case SDL_SCANCODE_DELETE: return kVK_ForwardDelete;
 case SDL_SCANCODE_ESCAPE: return kVK_Escape;
 case SDL_SCANCODE_LSHIFT: return kVK_Shift;
 case SDL_SCANCODE_RSHIFT: return kVK_RightShift;
 case SDL_SCANCODE_LCTRL: return kVK_Control;
 case SDL_SCANCODE_RCTRL: return kVK_RightControl;
 case SDL_SCANCODE_LALT: return kVK_Option;
 case SDL_SCANCODE_RALT: return kVK_RightOption;
 case SDL_SCANCODE_LGUI: return kVK_Command;
 case SDL_SCANCODE_RGUI: return kVK_RightCommand;
 case SDL_SCANCODE_CAPSLOCK: return kVK_CapsLock;
 case SDL_SCANCODE_UP: return kVK_UpArrow;
 case SDL_SCANCODE_DOWN: return kVK_DownArrow;
 case SDL_SCANCODE_LEFT: return kVK_LeftArrow;
 case SDL_SCANCODE_RIGHT: return kVK_RightArrow;
 case SDL_SCANCODE_HOME: return kVK_Home;
 case SDL_SCANCODE_END: return kVK_End;
 case SDL_SCANCODE_PAGEUP: return kVK_PageUp;
 case SDL_SCANCODE_PAGEDOWN: return kVK_PageDown;
 case SDL_SCANCODE_KP_DECIMAL: return kVK_ANSI_KeypadDecimal;
 case SDL_SCANCODE_KP_PERIOD: return kVK_ANSI_KeypadDecimal;
 case SDL_SCANCODE_KP_MULTIPLY: return kVK_ANSI_KeypadMultiply;
 case SDL_SCANCODE_KP_PLUS: return kVK_ANSI_KeypadPlus;
 case SDL_SCANCODE_KP_MINUS: return kVK_ANSI_KeypadMinus;
 case SDL_SCANCODE_KP_DIVIDE: return kVK_ANSI_KeypadDivide;
 case SDL_SCANCODE_KP_ENTER: return kVK_ANSI_KeypadEnter;
 case SDL_SCANCODE_KP_EQUALS: return kVK_ANSI_KeypadEquals;
 case SDL_SCANCODE_NUMLOCKCLEAR: return kVK_ANSI_KeypadClear;
 case SDL_SCANCODE_F1: return kVK_F1;
 case SDL_SCANCODE_F2: return kVK_F2;
 case SDL_SCANCODE_F3: return kVK_F3;
 case SDL_SCANCODE_F4: return kVK_F4;
 case SDL_SCANCODE_F5: return kVK_F5;
 case SDL_SCANCODE_F6: return kVK_F6;
 case SDL_SCANCODE_F7: return kVK_F7;
 case SDL_SCANCODE_F8: return kVK_F8;
 case SDL_SCANCODE_F9: return kVK_F9;
 case SDL_SCANCODE_F10: return kVK_F10;
 case SDL_SCANCODE_F11: return kVK_F11;
 case SDL_SCANCODE_F12: return kVK_F12;
 case SDL_SCANCODE_F13: return kVK_F13;
 case SDL_SCANCODE_F14: return kVK_F14;
 case SDL_SCANCODE_F15: return kVK_F15;
 case SDL_SCANCODE_F16: return kVK_F16;
 case SDL_SCANCODE_KP_0: return kVK_ANSI_Keypad0;
 case SDL_SCANCODE_KP_1: return kVK_ANSI_Keypad1;
 case SDL_SCANCODE_KP_2: return kVK_ANSI_Keypad2;
 case SDL_SCANCODE_KP_3: return kVK_ANSI_Keypad3;
 case SDL_SCANCODE_KP_4: return kVK_ANSI_Keypad4;
 case SDL_SCANCODE_KP_5: return kVK_ANSI_Keypad5;
 case SDL_SCANCODE_KP_6: return kVK_ANSI_Keypad6;
 case SDL_SCANCODE_KP_7: return kVK_ANSI_Keypad7;
 case SDL_SCANCODE_KP_8: return kVK_ANSI_Keypad8;
 case SDL_SCANCODE_KP_9: return kVK_ANSI_Keypad9;
 default:return -1;
 }
}
void set_touch(bool down,float x,float y){std::lock_guard lk(g_mu);g_touch=down;g_tx=x;g_ty=y;}
void set_touch_virtual(uint32_t bits){g_touch_virt.store(bits,std::memory_order_relaxed);}
std::vector<ControllerInfo> controllers(){std::lock_guard lk(g_mu);return g_infos;}
void release_keys(){mods::fast_forward_reset();mods::lock_on_reset();mods::quick_use_reset();std::lock_guard lk(g_mu);g_mouse_bindings.consume();memset(g_keys,0,sizeof g_keys);g_touch=false;}
void held_keys(bool* keys){std::lock_guard lk(g_mu);for(int i=0;i<256;i++)keys[i]=g_keys[i]||g_script_keys[i];}
void controller_values(float* out){std::lock_guard lk(g_mu);std::copy(std::begin(g_values),std::end(g_values),out);}
void host_controller_values(float* out){controller_values(out);}
// settings overlay (overlay/overlay.h): keys from both game windows; pointer from the selected overlay window. True = the overlay took
// the event (F1, or anything while it is open).
static int overlay_mods(SDL_Keymod m){
 return (m&SDL_KMOD_SHIFT?overlay::kShift:0)|(m&SDL_KMOD_CTRL?overlay::kCtrl:0)|(m&SDL_KMOD_ALT?overlay::kAlt:0)|(m&SDL_KMOD_GUI?overlay::kSuper:0);
}
static bool overlay_event(const SDL_Event& event){
 if(!g_prompt_window)return false;
 const auto target=overlay::window_target::selected.load();
 SDL_Window* pointer=target==overlay::window_target::Window::GamePad?g_gamepad_window:
                     target==overlay::window_target::Window::TV?g_prompt_window:nullptr;
 const SDL_WindowID pointerId=pointer?SDL_GetWindowID(pointer):0;
 switch(event.type){
 case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:{
  if(!game_window(event.key.windowID))return false;
  int code=keycode(event.key.scancode);
  // paste in an overlay text field: Dear ImGui reads the clipboard text the host passes here
  if(event.type==SDL_EVENT_KEY_DOWN&&event.key.scancode==SDL_SCANCODE_V&&(event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI))&&overlay::wants_text()){
   char* clip=SDL_GetClipboardText();overlay::set_clipboard_text(clip?clip:"");SDL_free(clip);
  }
  return code>=0&&overlay::key(code,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat,overlay_mods(event.key.mod), {event.key.windowID,event.key.timestamp,window_name(event.key.windowID)});
 }
 case SDL_EVENT_TEXT_INPUT: return overlay::text(event.text.text);  // typed into an overlay text field
 case SDL_EVENT_MOUSE_MOTION:{
  if(!pointer || event.motion.windowID!=pointerId)return false;
  int w=1,h=1;SDL_GetWindowSize(pointer,&w,&h);
  return overlay::mouse_move(event.motion.x/std::max(1,w),event.motion.y/std::max(1,h), event.motion.which == SDL_TOUCH_MOUSEID);
 }
 case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:{
  if(!pointer || event.button.windowID!=pointerId)return false;
  int b=event.button.button==SDL_BUTTON_LEFT?0:event.button.button==SDL_BUTTON_RIGHT?1:event.button.button==SDL_BUTTON_MIDDLE?2:event.button.button==SDL_BUTTON_X1?3:event.button.button==SDL_BUTTON_X2?4:-1;
  if(b<0)return overlay::captures();
  int w=1,h=1;SDL_GetWindowSize(pointer,&w,&h);
  overlay::mouse_move(event.button.x/std::max(1,w),event.button.y/std::max(1,h), event.button.which == SDL_TOUCH_MOUSEID);
  return overlay::mouse_button(b,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN, event.button.which == SDL_TOUCH_MOUSEID);
 }
 case SDL_EVENT_MOUSE_WHEEL:{
  if(!pointer || event.wheel.windowID!=pointerId)return false;
  float y=event.wheel.y,x=event.wheel.x;if(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED){x=-x;y=-y;}
  return overlay::mouse_wheel(x,y);
 }
 case SDL_EVENT_WINDOW_MOUSE_LEAVE:
  if(pointer && event.window.windowID==pointerId)overlay::mouse_move(-FLT_MAX,-FLT_MAX);
  return false;
 case SDL_EVENT_WINDOW_MOUSE_ENTER:{
  if(!pointer || event.window.windowID!=pointerId)return false;
  float x,y;SDL_GetMouseState(&x,&y);
  int w=1,h=1;SDL_GetWindowSize(pointer,&w,&h);
  overlay::mouse_move(x/std::max(1,w),y/std::max(1,h));
  return false;
 }
 default:return false;
 }
}
static PadState keyboard_state(bool host){bool keys[256];for(int i=0;i<256;i++)keys[i]=(host&&g_keys[i])||g_script_keys[i];return g_mouse_bindings.read(input_map::current(), host && mods::mouse_captured() && !overlay::captures() && !g_done, keys, screenshot::key_down, !mods::first_person_wheel());}
// ---- rumble -----------------------------------------------------------------------------------
// The game's requests (VPADControlMotor / WPADControlMotor in runtime/src/hle) are kept by
// rumble.h; update() sets the motors from them on the main thread, with the rest of the input. A
// running motor is sent again every update with a short duration, so it never outlasts the
// requests by more than kRumbleRefreshMs; should the main loop stall (SDL ends a rumble only while
// it pumps events), a watchdog thread stops the motors after kRumbleStallMs.
// The motors are still while the option is off, the settings overlay is open (the game sees no
// input then), no game window has the keyboard (SDL reads no controllers in the background; with
// "Controller input when the game isn't focused" on they keep running there, as the game plays on), and
// from the moment the process ends (quit, exit, crash): it ends without SDL_Quit, and XInput and
// HIDAPI controllers keep the last motor level they were sent after it.
constexpr Uint32 kRumbleRefreshMs=150,kRumbleStallMs=300;
static std::mutex g_pads_mu;  // g_controllers, g_rumble_*: the watchdog and exit paths run on other threads
static std::map<SDL_JoystickID,Uint16> g_rumble_sent;  // the level each controller runs at
static std::atomic<bool> g_rumble_quit{false};
static std::atomic<Uint64> g_rumble_update_ms{0};  // SDL_GetTicks of the latest update
static void rumble_all_locked(Uint16 level,Uint32 ms){
 // after SDL_Quit (a host that shuts SDL down, the tests) the gamepads are gone: an atexit or the
 // watchdog must not touch them (SDL 3.4 frees them; it crashed input_sdl_test at exit on Linux)
 if(!SDL_WasInit(SDL_INIT_GAMEPAD))return;
 for(auto id:g_rumble_controllers){
  auto i=g_controllers.find(id);
  if(i==g_controllers.end()||!SDL_GamepadConnected(i->second))continue;
  Uint16& sent=g_rumble_sent[id];
  if(level||sent)SDL_RumbleGamepad(i->second,level,level,level?ms:0);
  if(rumble::log_enabled()&&level!=sent)LOG("[rumble] host: controller %u motor %.2f",(unsigned)id,level/65535.0);
  sent=level;
 }
}
#ifdef __ANDROID__
static void phone_rumble_level(Uint16 level) {
 static phone_rumble::Refresh refresh;
 if(!refresh.send(level,SDL_GetTicks()))return;
 auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
 if(!env)return;
 jobject activity=static_cast<jobject>(SDL_GetAndroidActivity());
 if(!activity)return;
 jclass cls=env->GetObjectClass(activity);
 if(cls){
  if(jmethodID method=env->GetStaticMethodID(cls,"phoneRumble","(I)V"))env->CallStaticVoidMethod(cls,method,(jint)level);
  env->DeleteLocalRef(cls);
 }
 if(env->ExceptionCheck())env->ExceptionClear();
 env->DeleteLocalRef(activity);
}
#endif
static void apply_rumble(){
 g_rumble_update_ms=SDL_GetTicks();
#ifdef __ANDROID__
 const auto mode=phone_rumble::choice.load();
 // Avoid the touch-visibility clock query for phone-off/controller-only play.
 const bool touch=phone_rumble::touch_selected(mode!=phone_rumble::kOff && gfx::g_touch_controls,
                  phone_rumble::last_input_touch.load()) && gfx::touch_buttons_visible();
 const bool phone=phone_rumble::allowed(mode,touch,!g_rumble_controllers.empty());
 if(!phone)phone_rumble_level(0);
 if((g_rumble_controllers.empty()&&!phone)||g_rumble_quit)return;
#else
 if(g_rumble_controllers.empty()||g_rumble_quit)return;  // no controller with a motor: nothing to do
#endif
 // the share of "on" over the next host frame (the motor spins up and down slower than that)
 float level=rumble::host_level(1000000/60);
 if(overlay::is_open()||!controllers_live())level=0;  // the game cannot stop it from here
#ifdef __ANDROID__
 if(phone)phone_rumble_level((Uint16)(std::clamp(level,0.f,1.f)*0xFFFFu));
#endif
 std::lock_guard lk(g_pads_mu);
 g_rumble_update_ms=SDL_GetTicks();  // with the motors, under the lock: the watchdog decides on both at once
#ifdef __ANDROID__
 if(!phone_rumble::controller_allowed(mode))level=0;  // Rumble on: This device
#endif
 rumble_all_locked((Uint16)(std::clamp(level,0.f,1.f)*0xFFFFu),kRumbleRefreshMs);
}
static void rumble_watchdog(){
 while(!g_rumble_quit){
  SDL_Delay(100);
  if(SDL_GetTicks()-g_rumble_update_ms<kRumbleStallMs)continue;
  std::lock_guard lk(g_pads_mu);
  // again under the lock: an update may have run between the check above and here, and the level it
  // just sent must stand (stopping it raced the update: input_sdl_test on a busy Windows runner)
  if(SDL_GetTicks()-g_rumble_update_ms<kRumbleStallMs)continue;
  if(std::any_of(g_rumble_sent.begin(),g_rumble_sent.end(),[](auto& s){return s.second!=0;})){
   LOG("[rumble] the main loop stalls: motors stopped");
   rumble_all_locked(0,0);  // the next update runs them again
  }
 }
}
void stop_rumble_now(){
 g_rumble_quit=true;
 // crash handler: never wait for a thread that may be gone
 std::unique_lock lk(g_pads_mu,std::try_to_lock);
 if(lk.owns_lock())rumble_all_locked(0,0);
}
// SDL_EVENT_QUIT and closing the TV window end the process at once (std::_Exit in the renderer's
// loop): an event watch sees them as they arrive, before the loop does
static bool rumble_quit_watch(void*,SDL_Event* event){
 if(event->type==SDL_EVENT_QUIT||event->type==SDL_EVENT_TERMINATING||
    (event->type==SDL_EVENT_WINDOW_CLOSE_REQUESTED&&g_prompt_window&&event->window.windowID==SDL_GetWindowID(g_prompt_window)))
  stop_rumble_now();
 return true;
}
bool has_rumble(){return true;}
// ---- motion sensors (motion/motion.h) ------------------------------------------------------------
// Controllers with a gyro and an accelerometer (DualSense, DualShock 4, Switch Pro, Joy-Con, Steam Deck,
// ...) send both through SDL; the sensors run only while the gyro source is "controller" (they cost
// battery and bandwidth). A gyro sample goes to motion.h with the latest accelerometer sample.
static bool g_sensors_on=false;
static std::map<SDL_JoystickID,std::array<float,3>> g_accel;
// gyro watchdog: when a controller's sensors fall silent while the game window has the focus (SDL only sends
// controller events to a focused window), they are switched off and on again, which also makes SDL send a
// controller its sensor setup again (Bluetooth reconnects, another program reconfiguring it)
static std::map<SDL_JoystickID,uint64_t> g_sensor_seen,g_sensor_kick;  // SDL_GetTicks() of the last sample / re-enable
static bool has_motion(SDL_Gamepad* pad){return SDL_GamepadHasSensor(pad,SDL_SENSOR_GYRO)&&SDL_GamepadHasSensor(pad,SDL_SENSOR_ACCEL);}
static void set_sensors_locked(SDL_Gamepad* pad,bool on){
 if(!has_motion(pad))return;
 const bool g=SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_GYRO,on),a=SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_ACCEL,on);
 if(on&&!(g&&a))LOG("[gyro] %s: turning its motion sensors on failed: %s",SDL_GetGamepadName(pad),SDL_GetError());
 if(on)g_sensor_seen[SDL_GetGamepadID(pad)]=SDL_GetTicks();
}
static void update_sensors(){
 const bool want=motion::wants_controller_sensors();
 std::lock_guard lk(g_pads_mu);
 int n=0;for(auto [id,pad]:g_controllers)if(SDL_GamepadConnected(pad)&&has_motion(pad))n++;
 motion::set_gyro_controllers(n);
 static bool focused=true;
 const bool focus=controllers_live();  // (the background option: SDL keeps sending samples there)
 if(focus!=focused){
  focused=focus;
  if(focus)for(auto& [id,t]:g_sensor_seen)t=SDL_GetTicks();  // events were paused: a fresh start for the watchdog
  if(g_sensors_on)LOG("[gyro] %s",focus?"controller events reach the game again":"the game window lost the focus: SDL pauses controller events until it is back");
 }
 if(want==g_sensors_on){
  if(!want||!focus||getenv("WWHD_NO_HOST_INPUT"))return;
  const uint64_t now=SDL_GetTicks();
  for(auto [id,pad]:g_controllers){
   if(!SDL_GamepadConnected(pad)||!has_motion(pad))continue;
   uint64_t& seen=g_sensor_seen[id];uint64_t& kick=g_sensor_kick[id];
   if(now-seen<2000||now-kick<5000)continue;
   if(seen>=kick)LOG("[gyro] %s: no motion samples for %.1f s, turning its sensors off and on again (every 5 s until they come)",SDL_GetGamepadName(pad),(now-seen)/1000.0);
   kick=now;
   const uint64_t before=seen;set_sensors_locked(pad,false);set_sensors_locked(pad,true);seen=before;
  }
  return;
 }
 g_sensors_on=want;g_accel.clear();
 for(auto [id,pad]:g_controllers)set_sensors_locked(pad,want);
 if(want)LOG("[gyro] controller motion sensors on (%d controller(s) with a gyro)",n);
}
static void sensor_event(const SDL_GamepadSensorEvent& e){
 if(e.sensor==SDL_SENSOR_ACCEL){g_accel[e.which]={e.data[0],e.data[1],e.data[2]};return;}
 if(e.sensor!=SDL_SENSOR_GYRO)return;
 auto a=g_accel.find(e.which);if(a==g_accel.end())return;
 motion::controller_sample((uint64_t)e.which,e.sensor_timestamp,e.data,a->second.data());
}
#ifdef __ANDROID__
// The device sensors are independent of gamepads. SDL reports natural-screen axes;
// remap them to the shown screen, then to the existing flat-controller fusion frame.
// No sensor subsystem/handles are opened on the default Off path.
static SDL_Sensor* g_device_gyro=nullptr;
static SDL_Sensor* g_device_accel=nullptr;
static bool g_device_attempted=false,g_device_subsystem=false,g_device_foreground=true,g_device_accel_seen=false;
static std::array<float,3> g_device_acceleration{};
static int g_device_rotation=-1;
static int orientation_quarters(SDL_DisplayOrientation orientation){
 switch(orientation){
 case SDL_ORIENTATION_PORTRAIT:return 0;
 case SDL_ORIENTATION_LANDSCAPE:return 1;
 case SDL_ORIENTATION_PORTRAIT_FLIPPED:return 2;
 case SDL_ORIENTATION_LANDSCAPE_FLIPPED:return 3;
 default:return -1;
 }
}
static void close_device_sensors(){
 if(g_device_gyro)SDL_CloseSensor(g_device_gyro);
 if(g_device_accel)SDL_CloseSensor(g_device_accel);
 g_device_gyro=g_device_accel=nullptr;
 if(g_device_subsystem)SDL_QuitSubSystem(SDL_INIT_SENSOR);
 g_device_subsystem=false;g_device_accel_seen=false;g_device_rotation=-1;
 motion::device_reset();
}
static void update_device_sensors(){
 SDL_Window* focused=motion::wants_device_sensors()?SDL_GetKeyboardFocus():nullptr;
 const bool want=focused&&g_device_foreground&&
                 !getenv("WWHD_NO_HOST_INPUT")&&!overlay::blocks_input();
 if(!want){
  if(g_device_attempted){close_device_sensors();g_device_attempted=false;}
  return;
 }
 if(!g_device_attempted){
  g_device_attempted=true;
  g_device_subsystem=SDL_InitSubSystem(SDL_INIT_SENSOR);
  if(g_device_subsystem){
   int count=0;auto* ids=SDL_GetSensors(&count);
   for(int i=0;i<count;i++){
    const auto type=SDL_GetSensorTypeForID(ids[i]);
    if(type==SDL_SENSOR_GYRO&&!g_device_gyro)g_device_gyro=SDL_OpenSensor(ids[i]);
    if(type==SDL_SENSOR_ACCEL&&!g_device_accel)g_device_accel=SDL_OpenSensor(ids[i]);
   }
   SDL_free(ids);
  }
  const bool available=g_device_gyro&&g_device_accel;
  motion::set_device_sensors_available(available);
  if(!available){LOG("[gyro] this device has no usable gyro/accelerometer pair: %s",SDL_GetError());close_device_sensors();return;}
  LOG("[gyro] this device's motion sensors on");
 }
 if(!g_device_gyro||!g_device_accel)return;
 auto display=SDL_GetDisplayForWindow(focused);
 if(!display)display=SDL_GetPrimaryDisplay();
 const int natural=orientation_quarters(SDL_GetNaturalDisplayOrientation(display));
 const int current=orientation_quarters(SDL_GetCurrentDisplayOrientation(display));
 const int rotation=natural<0||current<0?-1:(current-natural+4)%4;
 if(rotation!=g_device_rotation){
  // A bias/gravity estimate in the old axes cannot be carried into the new ones.
  // Leave the virtual GamePad pose alone, discard pending turns and await fresh acceleration.
  motion::device_reset();g_device_accel_seen=false;g_device_rotation=rotation;
 }
}
static void device_sensor_event(const SDL_SensorEvent& e){
 if(!g_device_gyro||!g_device_accel||g_device_rotation<0||!g_device_foreground||
    !SDL_GetKeyboardFocus()||overlay::blocks_input()||getenv("WWHD_NO_HOST_INPUT")||!motion::wants_device_sensors())return;
 if(e.which==SDL_GetSensorID(g_device_accel)){
  std::copy_n(e.data,3,g_device_acceleration.begin());g_device_accel_seen=true;return;
 }
 if(e.which!=SDL_GetSensorID(g_device_gyro)||!g_device_accel_seen)return;
 const auto g=motion::device_to_sdl({e.data[0],e.data[1],e.data[2]},g_device_rotation);
 const auto a=motion::device_to_sdl({g_device_acceleration[0],g_device_acceleration[1],g_device_acceleration[2]},g_device_rotation);
 const float gyro[3]={g.x,g.y,g.z},accel[3]={a.x,a.y,a.z};
 motion::device_sample(e.sensor_timestamp,gyro,accel);
}
#endif
// What the dominant pad (first connected still present) has printed on its face buttons, in the
// order kPadA, kPadB, kPadX, kPadY: south, east, west, north (issue #78's Automatic preset).
static void dominant_labels(input_map::FaceLabel out[4]){
 using FL=input_map::FaceLabel;
 for(int i=0;i<4;i++)out[i]=FL::kUnknown;
 std::lock_guard lk(g_pads_mu);
 for(SDL_JoystickID id:g_order){
  if(SDL_IsJoystickVirtual(id))continue;  // #88's on-screen pad is not a pad to follow for labels
  auto i=g_controllers.find(id);if(i==g_controllers.end())continue;
  SDL_Gamepad* pad=i->second;
#ifdef __ANDROID__
  // Android keyboards with D-pad keys can be exposed by SDL as gamepads (qwerty2).
  // They have no complete face cluster and must not decide another pad's labels.
  if(!SDL_GamepadHasButton(pad,SDL_GAMEPAD_BUTTON_SOUTH) || !SDL_GamepadHasButton(pad,SDL_GAMEPAD_BUTTON_EAST) ||
     !SDL_GamepadHasButton(pad,SDL_GAMEPAD_BUTTON_WEST) || !SDL_GamepadHasButton(pad,SDL_GAMEPAD_BUTTON_NORTH))continue;
#endif
  auto label=[&](SDL_GamepadButton b)->input_map::FaceLabel{
   switch(SDL_GetGamepadButtonLabel(pad,b)){
   case SDL_GAMEPAD_BUTTON_LABEL_A:return FL::kA;
   case SDL_GAMEPAD_BUTTON_LABEL_B:return FL::kB;
   case SDL_GAMEPAD_BUTTON_LABEL_X:return FL::kX;
   case SDL_GAMEPAD_BUTTON_LABEL_Y:return FL::kY;
   case SDL_GAMEPAD_BUTTON_LABEL_CROSS:return FL::kCross;
   case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:return FL::kCircle;
   case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:return FL::kSquare;
   case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:return FL::kTriangle;
   default:return FL::kUnknown;}};
  out[0]=label(SDL_GAMEPAD_BUTTON_SOUTH);out[1]=label(SDL_GAMEPAD_BUTTON_EAST);
  out[2]=label(SDL_GAMEPAD_BUTTON_WEST);out[3]=label(SDL_GAMEPAD_BUTTON_NORTH);
  return;  // one pad decides, the first connected
 }
}
// outside g_pads_mu: input_map may write controls.json
static void note_dominant_labels(){
 input_map::FaceLabel labels[4];dominant_labels(labels);input_map::note_face_labels(labels);
 std::string guid,name;bool detected=false,pc=false;
 {
  std::lock_guard lk(g_pads_mu);
  for(auto id:g_order){
   if(SDL_IsJoystickVirtual(id))continue;
   auto it=g_controllers.find(id);if(it==g_controllers.end())continue;
   auto* pad=it->second;
   const bool type=SDL_GetGamepadType(pad)==SDL_GAMEPAD_TYPE_GAMECUBE;
   detected=input_map::is_gamecube(type,SDL_GetGamepadVendor(pad),SDL_GetGamepadProduct(pad));
   // SDL's GameCube type uses AXBY positions. Older PC database entries use ABXY.
   pc=!type && !(SDL_GetGamepadVendor(pad)==0x057e && SDL_GetGamepadProduct(pad)==0x0337);
   if(auto who=g_ident.find(id);who!=g_ident.end()){guid=who->second.guid;name=who->second.name;}
   break;
  }
 }
 auto m=input_map::current();
 if(input_map::auto_gamecube(m,guid,name,detected,pc))input_map::set_current(m);
}
static void open_controller(SDL_JoystickID id){
#ifdef __ANDROID__
 if(!SDL_IsJoystickVirtual(id))phone_rumble::last_input_touch=false;
#endif
 std::lock_guard lk(g_pads_mu);
 if(g_controllers.contains(id))return;
#ifdef __ANDROID__
 if(android_controller::odin(SDL_GetGamepadVendorForID(id),SDL_GetGamepadProductForID(id))) {
  if(char* original=SDL_GetGamepadMappingForID(id)) {
   LOG("[input] Odin SDL mapping before correction: %s",original);
   const auto corrected=android_controller::mapping(SDL_GetGamepadVendorForID(id),SDL_GetGamepadProductForID(id),original);
   if(SDL_AddGamepadMapping(corrected.c_str())>=0)LOG("[input] applied Odin 2020:0111 Nintendo face-position mapping");
   else LOG("[input] Odin mapping correction failed: %s",SDL_GetError());
   SDL_free(original);
  }
 }
#endif
 if(auto* pad=SDL_OpenGamepad(id)){g_controllers[id]=pad;g_order.push_back(id);
  // every connected device is listed: the host combines all of them (strongest input wins), so a
  // second device (e.g. Steam Input's virtual pad next to the physical one) shows up in bug reports
  const char* name=SDL_GetGamepadName(pad);
  LOG("[input] controller connected: %s (id %u, vendor %04X, product %04X, type %d)",name?name:"?",(unsigned)id,
      SDL_GetGamepadVendor(pad),SDL_GetGamepadProduct(pad),(int)SDL_GetGamepadType(pad));
#ifdef __ANDROID__
  char* mapping=SDL_GetGamepadMapping(pad);
  LOG("[input] SDL controller face labels: id=%u south=%d east=%d west=%d north=%d mapping=%s",(unsigned)id,
      (int)SDL_GetGamepadButtonLabel(pad,SDL_GAMEPAD_BUTTON_SOUTH),
      (int)SDL_GetGamepadButtonLabel(pad,SDL_GAMEPAD_BUTTON_EAST),
      (int)SDL_GetGamepadButtonLabel(pad,SDL_GAMEPAD_BUTTON_WEST),
      (int)SDL_GetGamepadButtonLabel(pad,SDL_GAMEPAD_BUTTON_NORTH),mapping?mapping:"?");
  SDL_free(mapping);
#endif
  // asking for a rumble of zero intensity also tells us whether the controller has a motor
  if(SDL_RumbleGamepad(pad,0,0,0))g_rumble_controllers.insert(id);
  if(g_sensors_on)set_sensors_locked(pad,true);
  PadIdentity& who=g_ident[id];
  char guid[33]={};SDL_GUIDToString(SDL_GetGamepadGUIDForID(id),guid,sizeof guid);
  who.guid=guid;who.name=name?name:"";who.is_virtual=SDL_IsJoystickVirtual(id);
  input::ControllerInfo& c=who.info;
  c.id=id;c.guid=who.guid;c.name=who.name;c.is_virtual=who.is_virtual;
  c.vendor=SDL_GetGamepadVendor(pad);c.product=SDL_GetGamepadProduct(pad);c.version=SDL_GetGamepadProductVersion(pad);
  if(const char* t=SDL_GetGamepadStringForType(SDL_GetGamepadType(pad)))c.type=t;
  if(const char* p=SDL_GetGamepadPath(pad))c.path=p;
  if(const char* sn=SDL_GetGamepadSerial(pad))c.serial=sn;
  const SDL_PropertiesID props=SDL_GetGamepadProperties(pad);
  c.rumble=SDL_GetBooleanProperty(props,SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,false)||g_rumble_controllers.contains(id);
  c.trigger_rumble=SDL_GetBooleanProperty(props,SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN,false);
  c.led=SDL_GetBooleanProperty(props,SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN,false)||SDL_GetBooleanProperty(props,SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN,false);
  c.gyro=SDL_GamepadHasSensor(pad,SDL_SENSOR_GYRO);c.accel=SDL_GamepadHasSensor(pad,SDL_SENSOR_ACCEL);
  if(SDL_Joystick* j=SDL_GetGamepadJoystick(pad)){c.buttons=SDL_GetNumJoystickButtons(j);c.axes=SDL_GetNumJoystickAxes(j);c.hats=SDL_GetNumJoystickHats(j);}
  c.touchpads=SDL_GetNumGamepadTouchpads(pad);
  auto label=[&](SDL_GamepadButton b)->uint8_t{
   switch(SDL_GetGamepadButtonLabel(pad,b)){
   case SDL_GAMEPAD_BUTTON_LABEL_A:return (uint8_t)input_map::FaceLabel::kA;
   case SDL_GAMEPAD_BUTTON_LABEL_B:return (uint8_t)input_map::FaceLabel::kB;
   case SDL_GAMEPAD_BUTTON_LABEL_X:return (uint8_t)input_map::FaceLabel::kX;
   case SDL_GAMEPAD_BUTTON_LABEL_Y:return (uint8_t)input_map::FaceLabel::kY;
   case SDL_GAMEPAD_BUTTON_LABEL_CROSS:return (uint8_t)input_map::FaceLabel::kCross;
   case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:return (uint8_t)input_map::FaceLabel::kCircle;
   case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:return (uint8_t)input_map::FaceLabel::kSquare;
   case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:return (uint8_t)input_map::FaceLabel::kTriangle;
   default:return 0;}};
  c.labels[0]=label(SDL_GAMEPAD_BUTTON_SOUTH);c.labels[1]=label(SDL_GAMEPAD_BUTTON_EAST);
  c.labels[2]=label(SDL_GAMEPAD_BUTTON_WEST);c.labels[3]=label(SDL_GAMEPAD_BUTTON_NORTH);
  // a new controller (no bindings of its own, not declined) is offered the setup when it is first used
  if(!who.is_virtual&&input_map::setup_offer_due(input_map::current(),who.guid,who.name,false))g_offer_pending.insert(id);
  LOG("[input] controller %s: guid %s%s",who.name.c_str(),guid,input_map::profile_index(input_map::current(),who.guid,who.name)>=0?", its own bindings":"");}
}
static void close_controller(SDL_JoystickID id){
 {std::lock_guard lk(g_pads_mu);
  auto i=g_controllers.find(id);if(i!=g_controllers.end()){SDL_CloseGamepad(i->second);g_controllers.erase(i);LOG("[input] controller disconnected: id %u",(unsigned)id);}
  std::erase(g_order,id);
  g_rumble_controllers.erase(id);g_rumble_sent.erase(id);g_accel.erase(id);g_start_gates.erase(id);g_ident.erase(id);g_offer_pending.erase(id);
  g_sensor_seen.erase(id);g_sensor_kick.erase(id);}
 motion::controller_gone(id);
 note_dominant_labels();  // the dominant pad may have changed
}
#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL Java_org_libsdl_app_SDLControllerManager_nativeRequestDeviceRefresh(JNIEnv* env, jclass type) {
 static std::atomic<bool> queued{false};
 if(queued.exchange(true))return;
 auto global=(jclass)env->NewGlobalRef(type);
 if(!global){queued=false;return;}
 hostui::post([global] {
  queued=false;
  auto* env=(JNIEnv*)SDL_GetAndroidJNIEnv();
  // Match SDL's existing lock order. Calling Java's synchronized enumeration
  // from the UI listener directly could deadlock against SDL's input pump.
  SDL_LockJoysticks();
  const auto method=env->GetStaticMethodID(global,"pollInputDevices","()V");
  if(method)env->CallStaticVoidMethod(global,method);
  const bool failed=env->ExceptionCheck();
  if(failed)env->ExceptionClear();
  SDL_UnlockJoysticks();
  env->DeleteGlobalRef(global);
  if(failed){LOG("[input] Android controller refresh failed");return;}
  int count=0;auto* ids=SDL_GetGamepads(&count);
  for(int i=0;i<count;++i)open_controller(ids[i]);
  SDL_free(ids);note_dominant_labels();
 });
}
#endif
void init(){
 wwhd_trace::sink = [](const char* line) { LOG("[overlay-trace] %s",line); };
 if(!wwhd_trace::handoff()) LOG("[overlay] diagnostic mouse handoff disabled (WWHD_OVERLAY_MOUSE_HANDOFF=0)");
 input_map::load_startup();
 apply_background_hint();  // before the controllers open (SDL also follows a later change)
 // WWHD_NO_GAMEPAD only hides the GamePad screen window; WWHD_NO_CONTROLLERS turns off host controllers
 if(!getenv("WWHD_NO_CONTROLLERS")) {
  if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD)){LOG("[input] SDL gamepad initialization: %s",SDL_GetError());return;}
  int count=0;auto* ids=SDL_GetGamepads(&count);for(int i=0;i<count;i++)open_controller(ids[i]);SDL_free(ids);
  note_dominant_labels();  // before the first read, so Automatic is settled at startup
  SDL_AddEventWatch(rumble_quit_watch,nullptr);
  atexit(stop_rumble_now);  // std::exit: the game's exit(), its main thread returned
  std::thread(rumble_watchdog).detach();
 }
}
static std::string utf8(const std::u16string& text){
 std::string out;
 for(size_t i=0;i<text.size();i++){
  uint32_t c=text[i];if(c>=0xD800&&c<=0xDBFF&&i+1<text.size()&&text[i+1]>=0xDC00&&text[i+1]<=0xDFFF)c=0x10000+((c-0xD800)<<10)+(text[++i]-0xDC00);
  if(c<0x80)out.push_back((char)c);
  else if(c<0x800){out.push_back((char)(0xC0|(c>>6)));out.push_back((char)(0x80|(c&63)));}
  else if(c<0x10000){out.push_back((char)(0xE0|(c>>12)));out.push_back((char)(0x80|((c>>6)&63)));out.push_back((char)(0x80|(c&63)));}
  else{out.push_back((char)(0xF0|(c>>18)));out.push_back((char)(0x80|((c>>12)&63)));out.push_back((char)(0x80|((c>>6)&63)));out.push_back((char)(0x80|(c&63)));}
 }return out;
}
// The game's other windows (the GamePad window, where "Use the GamePad to input" invites a click) take
// the typing too: text input only on the TV window lost every key typed after clicking the GamePad.
static std::vector<std::pair<SDL_Window*,std::string>> g_other_titles;
static void other_windows_text_input(bool on){
 if(on){g_other_titles.clear();int n=0;if(SDL_Window** ws=SDL_GetWindows(&n)){for(int i=0;i<n;i++)if(ws[i]!=g_prompt_window){g_other_titles.push_back({ws[i],SDL_GetWindowTitle(ws[i])});SDL_StartTextInput(ws[i]);}SDL_free(ws);}}
 else{for(auto& [w,t]:g_other_titles){SDL_StopTextInput(w);SDL_SetWindowTitle(w,t.c_str());}g_other_titles.clear();}
}
static void show_prompt(){
 const std::string title="Enter text (Enter confirms, Escape cancels): "+utf8(g_text);
 if(g_prompt_window)SDL_SetWindowTitle(g_prompt_window,title.c_str());
 for(auto& [w,t]:g_other_titles)SDL_SetWindowTitle(w,title.c_str());
}
// std::exchange, not std::move: libc++ leaves a moved-from std::function set, which kept the
// prompt "active" after the first text (keys swallowed, later prompts never opened).
static void finish_prompt(bool ok){
 auto done=std::exchange(g_done,nullptr);auto text=std::move(g_text);
 if(g_prompt_window){SDL_StopTextInput(g_prompt_window);SDL_SetWindowTitle(g_prompt_window,g_previous_title.c_str());}
 other_windows_text_input(false);
 release_keys();if(done)done(ok,std::move(text));
}
// Some controllers (GameSir) also register a virtual touch screen and a keyboard with Android and
// send touches and keys for their buttons (mapping modes of the maker's app). Touches only count
// from touch devices that are not a connected controller.
bool touch_from_controller(SDL_TouchID id){
#ifdef __ANDROID__
 static std::map<SDL_TouchID,bool> known;
 if(auto it=known.find(id);it!=known.end())return it->second;
 const char* name=SDL_GetTouchDeviceName(id);bool fromPad=false;
 if(name)for(auto [pid,pad]:g_controllers)if(const char* pn=SDL_GetGamepadName(pad))if(*pn&&!strncmp(name,pn,strlen(pn)))fromPad=true;
 LOG("[input] touch device %s%s",name?name:"?",fromPad?": a controller's, ignored":"");
 known[id]=fromPad;return fromPad;
#else
 (void)id;return false;
#endif
}
// debug: WWHD_TEST_POST_KEYS=300:F1,410:Down,420:Return,430:K/30 (held 30 frames),500:Text=Tetra pushes key
// presses (and typed text) at TV frames into SDL's event queue, as gfx/input.mm does on macOS: they take the
// real path in hidden test runs and reach only the settings overlay and the game's text prompt.
// Mouse1..Mouse5 click in the TV window (the mouse camera captures the pointer; needs host input);
// Mouse1@0.04:0.5 clicks at that point of the TV window (0..1 from its top left; default its top left corner).
static constexpr Uint64 kTestEventTime=4242;  // timestamp that marks them
static bool test_event(const SDL_Event& e){return e.common.timestamp==kTestEventTime;}
static void post_test_keys(){
 struct Post{uint64_t frame;SDL_Scancode code;bool down;std::string text;SDL_Keymod mods=SDL_KMOD_NONE;Uint8 button=0;float fx=-1,fy=-1;};
 static std::vector<Post> posts=[]{
  std::vector<Post> v;const char* e=getenv("WWHD_TEST_POST_KEYS");
  for(const char* p=e;p&&*p;){
   char* end;uint64_t f=strtoull(p,&end,10);if(*end!=':')break;p=end+1;
   size_t len=strcspn(p,",");std::string k(p,len);p+=len+(p[len]==',');
   if(k.rfind("Text=",0)==0){v.push_back({f,SDL_SCANCODE_UNKNOWN,true,k.substr(5)});continue;}
   uint64_t hold=0;if(size_t sl=k.find('/');sl!=std::string::npos){hold=strtoull(k.c_str()+sl+1,nullptr,10);k.resize(sl);}
   SDL_Keymod mods=SDL_KMOD_NONE;
   for(;;){
    if(k.rfind("Shift+",0)==0){mods=SDL_Keymod(mods|SDL_KMOD_SHIFT);k=k.substr(6);}
    else if(k.rfind("Ctrl+",0)==0){mods=SDL_Keymod(mods|SDL_KMOD_CTRL);k=k.substr(5);}
    else if(k.rfind("Alt+",0)==0){mods=SDL_Keymod(mods|SDL_KMOD_ALT);k=k.substr(4);}
    else if(k.rfind("Cmd+",0)==0){mods=SDL_Keymod(mods|SDL_KMOD_GUI);k=k.substr(4);}
    else break;
   }
   float fx=-1,fy=-1;if(size_t at=k.find('@');at!=std::string::npos){if(sscanf(k.c_str()+at+1,"%f:%f",&fx,&fy)!=2)fx=fy=-1;k.resize(at);}
   int code=input_map::key_from_id(k);SDL_Scancode sc=SDL_SCANCODE_UNKNOWN;
   if(code>=input_map::kMouse1&&code<input_map::kMouse1+5){
    static constexpr Uint8 kButtons[]={SDL_BUTTON_LEFT,SDL_BUTTON_RIGHT,SDL_BUTTON_MIDDLE,SDL_BUTTON_X1,SDL_BUTTON_X2};
    const Uint8 button=kButtons[code-input_map::kMouse1];
    v.push_back({f,sc,true,{},mods,button,fx,fy});v.push_back({f+hold,sc,false,{},mods,button,fx,fy});continue;
   }
   for(int i=0;i<SDL_SCANCODE_COUNT&&code!=input_map::kNoKey;i++)if(keycode((SDL_Scancode)i)==code){sc=(SDL_Scancode)i;break;}
   if(sc==SDL_SCANCODE_UNKNOWN){LOG("[input] WWHD_TEST_POST_KEYS: unknown key %s",k.c_str());continue;}
   v.push_back({f,sc,true,{},mods});v.push_back({f+hold,sc,false,{},mods});
  }
  std::stable_sort(v.begin(),v.end(),[](const Post& a,const Post& b){return a.frame<b.frame;});
  return v;
 }();
 static size_t next=0;static std::deque<std::string> texts;  // SDL keeps the text pointer
 for(;next<posts.size()&&render::frame_count()>=posts[next].frame;next++){
  const Post& p=posts[next];SDL_Event e{};e.common.timestamp=kTestEventTime;
  if(!p.text.empty()){texts.push_back(p.text);e.type=SDL_EVENT_TEXT_INPUT;e.text.windowID=g_prompt_window?SDL_GetWindowID(g_prompt_window):0;e.text.text=texts.back().c_str();}
  else if(p.button){
   e.type=p.down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=p.button;e.button.down=p.down;e.button.which=1;
   e.button.windowID=g_prompt_window?SDL_GetWindowID(g_prompt_window):0;
   if(p.fx>=0&&g_prompt_window){int w=0,h=0;SDL_GetWindowSize(g_prompt_window,&w,&h);e.button.x=p.fx*float(w);e.button.y=p.fy*float(h);}
   LOG("[input] test mouse button %d %s at frame %llu (%.0f, %.0f)",p.button,p.down?"down":"up",(unsigned long long)render::frame_count(),double(e.button.x),double(e.button.y));
   SDL_PushEvent(&e);continue;
  }
  else{e.type=p.down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=p.code;e.key.down=p.down;e.key.mod=p.mods;e.key.windowID=g_prompt_window?SDL_GetWindowID(g_prompt_window):0;}
  LOG("[input] test %s %s%s at frame %llu",p.text.empty()?"key":"text",p.text.empty()?SDL_GetScancodeName(p.code):p.text.c_str(),p.text.empty()?(p.down?" down":" up"):"",(unsigned long long)render::frame_count());
  SDL_PushEvent(&e);
 }
}
// debug: WWHD_TEST_VIRTUAL_PAD=2000-2010:a,2100-2105:start attaches an SDL virtual gamepad and holds its
// buttons (SDL gamepad button names) during TV frame ranges (script frames with WWHD_TEST_FIXED_SEED, as
// WWHD_PRESS). Unlike WWHD_PRESS the presses take SDL's own
// path, focus filter included (issue #267: a hidden run has no focus, so they reach the game only with
// controller input in the background on). Just before the first range a window focus loss is posted for
// the TV window, as the window manager sends it. Needs host input (no WWHD_NO_HOST_INPUT); to leave real
// controllers out, start with SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_MFI=0 SDL_JOYSTICK_IOKIT=0 (macOS).
static void test_virtual_pad(){
 struct Hold{uint64_t from,to;SDL_GamepadButton button;};
 static const std::vector<Hold> holds=[]{
  std::vector<Hold> v;const char* e=getenv("WWHD_TEST_VIRTUAL_PAD");
  for(const char* p=e;p&&*p;){
   char* end;const uint64_t a=strtoull(p,&end,10);if(*end!='-')break;
   const uint64_t b=strtoull(end+1,&end,10);if(*end!=':')break;p=end+1;
   const size_t len=strcspn(p,",");const std::string name(p,len);p+=len+(p[len]==',');
   const SDL_GamepadButton button=SDL_GetGamepadButtonFromString(name.c_str());
   if(button==SDL_GAMEPAD_BUTTON_INVALID){LOG("[input] WWHD_TEST_VIRTUAL_PAD: unknown button %s",name.c_str());continue;}
   v.push_back({a,b,button});
  }
  return v;
 }();
 static SDL_Joystick* pad=nullptr;static bool failed=false;
 if(holds.empty()||failed||!SDL_WasInit(SDL_INIT_GAMEPAD))return;
 if(!pad){
  SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);
  d.type=SDL_JOYSTICK_TYPE_GAMEPAD;d.naxes=SDL_GAMEPAD_AXIS_COUNT;d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;d.name="WWHD test virtual pad";
  const SDL_JoystickID id=SDL_AttachVirtualJoystick(&d);
  pad=id?SDL_OpenJoystick(id):nullptr;
  LOG("[input] test virtual pad %u %s",(unsigned)id,pad?"attached":SDL_GetError());
  if(!pad){failed=true;return;}
 }
 const uint64_t frame=fixed_seed::script_frame(render::frame_count());  // as WWHD_PRESS (WWHD_TEST_FIXED_SEED: from the boot's anchor)
 static bool focusPosted=false;
 if(!focusPosted&&g_prompt_window&&frame+1>=holds.front().from){
  focusPosted=true;SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;e.window.windowID=SDL_GetWindowID(g_prompt_window);SDL_PushEvent(&e);
  LOG("[input] test virtual pad: focus loss posted at frame %llu (a window has the keyboard: %s; controller input in the background: %s)",
      (unsigned long long)frame,SDL_GetKeyboardFocus()?"yes":"no",background_controllers()?"on":"off");
 }
 static bool held[SDL_GAMEPAD_BUTTON_COUNT]={};
 bool want[SDL_GAMEPAD_BUTTON_COUNT]={};
 for(const Hold& h:holds)if(frame>=h.from&&frame<=h.to)want[h.button]=true;
 for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;b++)if(want[b]!=held[b]){
  held[b]=want[b];SDL_SetJoystickVirtualButton(pad,b,want[b]);
  LOG("[input] test virtual pad: %s %s at frame %llu",SDL_GetGamepadStringForButton((SDL_GamepadButton)b),want[b]?"down":"up",(unsigned long long)frame);
 }
}
// The overlay's text prompt (overlay/text_entry.h) while the menu is not over it: typed text (SDL text
// input, input method composition) and the keys of every game window. True = the prompt took the event.
static bool text_entry_event(const SDL_Event& event){
 if(!text_entry::active()||overlay::is_open())return false;
 if(getenv("WWHD_NO_HOST_INPUT")&&!test_event(event))return false;
 switch(event.type){
 case SDL_EVENT_TEXT_INPUT:text_entry::text(event.text.text);return true;
 case SDL_EVENT_TEXT_EDITING:text_entry::preedit(event.edit.text);return true;
 case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:{
  int code=keycode(event.key.scancode);
  return code>=0&&overlay::key(code,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat,overlay_mods(event.key.mod), {event.key.windowID,event.key.timestamp,window_name(event.key.windowID)});
 }
 default:return false;
 }
}
int physical_keycode(int scancode) { return keycode(static_cast<SDL_Scancode>(scancode)); }
void handle_event(const SDL_Event& event){
 // Defer focus loss until update(), after the event queue drains: a TV -> GamePad
 // handoff must move the menu rather than close it between the loss and gain events.
 if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)
  update_overlay_window(SDL_GetWindowFromID(event.window.windowID));
 else if(event.type==SDL_EVENT_WINDOW_HIDDEN || event.type==SDL_EVENT_WINDOW_MINIMIZED ||
           event.type==SDL_EVENT_WINDOW_SHOWN || event.type==SDL_EVENT_WINDOW_RESTORED)
  update_overlay_window(SDL_GetKeyboardFocus());
#ifdef __ANDROID__
 if(event.type==SDL_EVENT_WILL_ENTER_BACKGROUND||event.type==SDL_EVENT_TERMINATING){
  g_device_foreground=false;close_device_sensors();g_device_attempted=false;
 }
 if(event.type==SDL_EVENT_DID_ENTER_FOREGROUND)g_device_foreground=true;
 if(event.type==SDL_EVENT_DISPLAY_ORIENTATION)update_device_sensors();
 if(event.type==SDL_EVENT_SENSOR_UPDATE){device_sensor_event(event.sensor);return;}
#endif
 if((event.type==SDL_EVENT_AUDIO_DEVICE_ADDED||event.type==SDL_EVENT_AUDIO_DEVICE_REMOVED||event.type==SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED)&&!event.adevice.recording)audio::flush();
#ifdef __ANDROID__
 // Showing touch buttons alone does not select phone feedback. Actual touch-pad input
 // selects it; physical-controller or keyboard input deselects the phone again.
 if(event.type==SDL_EVENT_KEY_DOWN ||
    (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN && !SDL_IsJoystickVirtual(event.gbutton.which)) ||
    (event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(int(event.gaxis.value))>8192 && !SDL_IsJoystickVirtual(event.gaxis.which)))
  phone_rumble::last_input_touch=false;
#endif
 if (event.type==SDL_EVENT_MOUSE_REMOVED) {
  std::lock_guard lk(g_mu);g_mouse_bindings.remove_device(event.mdevice.which);
 }
 if (!getenv("WWHD_NO_HOST_INPUT")) {
  const bool eligible = g_prompt_window && mods::mouse_captured() && !overlay::captures() && !g_done;
  if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
   int b=event.button.button==SDL_BUTTON_LEFT?0:event.button.button==SDL_BUTTON_RIGHT?1:event.button.button==SDL_BUTTON_MIDDLE?2:event.button.button==SDL_BUTTON_X1?3:event.button.button==SDL_BUTTON_X2?4:-1;
   if (b>=0 && event.button.which!=SDL_TOUCH_MOUSEID && game_window(event.button.windowID) &&
       overlay::pointer_shortcut(input_map::kMouse1+b,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN)) return;
   mouse_binding_button(event.button.which,b,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN,
       eligible && event.button.which != SDL_TOUCH_MOUSEID && event.button.windowID==SDL_GetWindowID(g_prompt_window));
  }
  if(event.type==SDL_EVENT_MOUSE_WHEEL) {
   float y=event.wheel.y; if(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED)y=-y;
   if (y!=0 && event.wheel.which!=SDL_TOUCH_MOUSEID && game_window(event.wheel.windowID) &&
       overlay::pointer_shortcut(y>0?input_map::kWheelUp:input_map::kWheelDown,true)) return;
   mouse_binding_wheel(y,eligible && event.wheel.which != SDL_TOUCH_MOUSEID && event.wheel.windowID==SDL_GetWindowID(g_prompt_window) && !mods::first_person_wheel());
  }
 }
 if(overlay::captures())mods::update_mouse();
 else if(mods::handle_mouse_event(event))return;
 if(event.type==SDL_EVENT_GAMEPAD_ADDED&&!getenv("WWHD_NO_CONTROLLERS")){open_controller(event.gdevice.which);note_dominant_labels();}
 if(event.type==SDL_EVENT_GAMEPAD_REMOVED)close_controller(event.gdevice.which);
 if(event.type==SDL_EVENT_GAMEPAD_SENSOR_UPDATE){g_sensor_seen[event.gsensor.which]=SDL_GetTicks();if(!getenv("WWHD_NO_HOST_INPUT")&&!overlay::blocks_input())sensor_event(event.gsensor);return;}
 static const bool trace=getenv("WWHD_OVERLAY_TRACE")&&std::string(getenv("WWHD_OVERLAY_TRACE"))=="1";
 if(trace&&(event.type==SDL_EVENT_WINDOW_FOCUS_LOST||event.type==SDL_EVENT_WINDOW_FOCUS_GAINED))
  LOG("[overlay] focus %s window %s id %u timestamp %llu",event.type==SDL_EVENT_WINDOW_FOCUS_LOST?"lost":"gained",window_name(event.window.windowID),event.window.windowID,(unsigned long long)event.window.timestamp);
 if(trace&&(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&(event.key.scancode==SDL_SCANCODE_F1||event.key.scancode==SDL_SCANCODE_ESCAPE))
  LOG("[overlay] key %s %s window %s id %u timestamp %llu repeat %d",SDL_GetScancodeName(event.key.scancode),event.type==SDL_EVENT_KEY_DOWN?"down":"up",window_name(event.key.windowID),event.key.windowID,(unsigned long long)event.key.timestamp,event.key.repeat);
 if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){release_keys();overlay::focus_lost();}
 if(text_entry_event(event))return;
 if(g_done){
  if(event.type==SDL_EVENT_KEY_DOWN){
   if(event.key.scancode==SDL_SCANCODE_RETURN){finish_prompt(true);return;}
   if(event.key.scancode==SDL_SCANCODE_ESCAPE){finish_prompt(false);return;}
   if(event.key.scancode==SDL_SCANCODE_BACKSPACE&&!g_text.empty()){char16_t c=g_text.back();g_text.pop_back();if(c>=0xDC00&&c<=0xDFFF&&!g_text.empty()&&g_text.back()>=0xD800&&g_text.back()<=0xDBFF)g_text.pop_back();show_prompt();}
  }
  if(event.type==SDL_EVENT_TEXT_INPUT){
   const unsigned char* p=(const unsigned char*)event.text.text;
   while(*p){uint32_t c=*p++;int rest=0;if(c>=0xF0){c&=7;rest=3;}else if(c>=0xE0){c&=15;rest=2;}else if(c>=0xC0){c&=31;rest=1;}while(rest--&&*p)c=(c<<6)|(*p++&63);
    size_t units=c>0xFFFF?2:1;if(g_text.size()+units>(size_t)g_max_len)break;
    if(c>0xFFFF){c-=0x10000;g_text.push_back((char16_t)(0xD800+(c>>10)));g_text.push_back((char16_t)(0xDC00+(c&1023)));}else g_text.push_back((char16_t)c);
   }show_prompt();
  }return;
 }
 if(getenv("WWHD_NO_HOST_INPUT")&&!test_event(event))return;
#ifdef __ANDROID__
 // Android's Back (the navigation bar's back button or the back gesture) opens and closes Settings, like F1 on
 // desktop: the one control every Android device has (#215). SDL passes it on as AC_BACK; backend.cpp traps it
 // so Android does not close the app. Left alone while a text prompt is open (the game's name entry).
 if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&event.key.scancode==SDL_SCANCODE_AC_BACK&&
    !text_entry::active()&&!overlay::wants_text()){
  // a dialog in Settings closes first (as Esc closes it), then the menu
  if(overlay::dialog_open())overlay::key(kVK_Escape,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat,0);
  else if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat)overlay::set_open(!overlay::is_open(),overlay::OpenReason::API);
  return;
 }
#endif
 if(overlay_event(event))return;
 if(test_event(event))return;
 #ifdef __ANDROID__
 // phones: the controller is the GamePad. Keys only type text (above): controllers such as the
 // GameSir also show up as a keyboard and would press keyboard-mapped GamePad buttons twice.
 static const bool keyboardPad=getenv("WWHD_ANDROID_KEYBOARD")!=nullptr;
 if(!keyboardPad&&(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)) {
  static std::set<std::pair<SDL_KeyboardID, SDL_Scancode>> logged;
  const auto key=std::make_pair(event.key.which,event.key.scancode);
  if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&logged.size()<512&&logged.insert(key).second)
   LOG("[input] Android key not delivered to game: keyboard=%u scancode=%d (%s) keycode=%u reason=keys-only-type-text WWHD_ANDROID_KEYBOARD=unset",
       (unsigned)event.key.which,int(event.key.scancode),SDL_GetScancodeName(event.key.scancode),(unsigned)event.key.key);
  return;
 }
 #endif
 // a repeat never presses a key: one held down while the overlay or the text prompt had the keyboard (Enter
 // that confirmed a name) stays out of the game until pressed again
 if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP){int code=keycode(event.key.scancode);if(code>=0){std::lock_guard lk(g_mu);g_keys[code]=event.type==SDL_EVENT_KEY_DOWN&&(g_keys[code]||!event.key.repeat);}}
}
void update(){
 update_overlay_window(SDL_GetKeyboardFocus());
 g_gameplay_focus.store(SDL_GetKeyboardFocus()!=nullptr&&!text_prompt_active(),std::memory_order_relaxed);
 g_window_focus.store(SDL_GetKeyboardFocus()!=nullptr,std::memory_order_relaxed);
 apply_background_hint();  // the Controls option, applied before this poll reads the controllers
 if(wwhd_trace::enabled()) {
  overlay_trace_sdl::begin(overlay::is_open());
  for(auto [id,pad]:g_controllers) overlay_trace_sdl::pad(id,pad);
 }
 mods::update_mouse();
 apply_rumble();
 update_sensors();
#ifdef __ANDROID__
 update_device_sensors();
#endif
 std::function<void(bool,std::u16string)> cancelled;
 {std::lock_guard lk(g_mu);if(g_pending&&!g_done){g_done=std::exchange(g_pending,nullptr);g_text=std::move(g_initial);g_max_len=g_pending_max_len;g_mouse_bindings.consume();memset(g_keys,0,sizeof g_keys);if(!g_prompt_window)g_prompt_window=SDL_GetKeyboardFocus();if(g_prompt_window){g_previous_title=SDL_GetWindowTitle(g_prompt_window);if(SDL_StartTextInput(g_prompt_window)){other_windows_text_input(true);show_prompt();LOG("[input] the game asks for text: type it in a game window (shown in the window title), Enter confirms, Escape cancels; WWHD_SWKBD_TEXT=<text> answers automatically");}else{LOG("[input] text input unavailable (%s); set WWHD_SWKBD_TEXT=<text>",SDL_GetError());cancelled=std::exchange(g_done,nullptr);}}else{LOG("[input] text input: no window to type in; set WWHD_SWKBD_TEXT=<text>");cancelled=std::exchange(g_done,nullptr);}}}
 if(cancelled)cancelled(false,{});
 // SDL text input in every game window while the overlay's text prompt shows (typed text, input methods;
 // on Android it brings up the system keyboard); the input method's candidates open over the prompt
 // (also while a text field of the settings overlay has the keyboard)
 // Not in the Quest theater: an immersive OpenXR session never shows Android's system keyboard, which
 // would still take every controller press from the on-screen keyboard (the controllers type there).
#ifdef __ANDROID__
 const bool system_keyboard=!quest_theater::active();
#else
 const bool system_keyboard=true;
#endif
 static bool entry_text=false;
 if(system_keyboard&&(text_entry::active()||overlay::wants_text())!=entry_text&&!g_done){
  entry_text=!entry_text;int n=0;
  if(SDL_Window** ws=SDL_GetWindows(&n)){
   for(int i=0;i<n;i++){
    if(!entry_text){SDL_StopTextInput(ws[i]);continue;}
    int w=1,h=1;SDL_GetWindowSize(ws[i],&w,&h);SDL_Rect area{w/2-160,h/2-60,320,30};
    SDL_SetTextInputArea(ws[i],&area,0);SDL_StartTextInput(ws[i]);
   }
   SDL_free(ws);
  }
 }
 post_test_keys();
 test_virtual_pad();
 using namespace input_map;
 // v: every device combined (strongest input wins), as the settings overlay reads it; game: the same with
 // each device's inputs that the overlay or the text prompt had and that are still held left out
 // (overlay/release_gate.h, issue #153)
 static uint32_t layout_generation=0;
 if(layout_generation!=input_map::generation()){note_dominant_labels();layout_generation=input_map::generation();}
 const input_map::Mapping current=input_map::current();
 float v[kPadCount]={},game[kPadCount]={};
 const bool captured_before=overlay::captures();
 std::vector<std::pair<SDL_JoystickID,overlay::PadValues>> devices;
 for(auto [id,pad]:g_controllers){
  if(!SDL_GamepadConnected(pad))continue;
  std::array<float,kPadCount> d{};
  auto put=[&](int p,float x){d[p]=std::max(d[p],x);};
  auto button=[&](SDL_GamepadButton b,int p){put(p,SDL_GetGamepadButton(pad,b)?1.f:0.f);};
  button(SDL_GAMEPAD_BUTTON_SOUTH,kPadA);button(SDL_GAMEPAD_BUTTON_EAST,kPadB);button(SDL_GAMEPAD_BUTTON_WEST,kPadX);button(SDL_GAMEPAD_BUTTON_NORTH,kPadY);
  button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,kPadLB);button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,kPadRB);
  button(SDL_GAMEPAD_BUTTON_START,kPadMenu);button(SDL_GAMEPAD_BUTTON_BACK,kPadOptions);button(SDL_GAMEPAD_BUTTON_GUIDE,kPadHome);
  button(SDL_GAMEPAD_BUTTON_MISC1,kPadCapture);
  button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,kPadRightPaddle1);button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,kPadLeftPaddle1);
  button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,kPadRightPaddle2);button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,kPadLeftPaddle2);
  button(SDL_GAMEPAD_BUTTON_TOUCHPAD,kPadTouchpad);
  button(SDL_GAMEPAD_BUTTON_MISC2,kPadMisc2);button(SDL_GAMEPAD_BUTTON_MISC3,kPadMisc3);button(SDL_GAMEPAD_BUTTON_MISC4,kPadMisc4);
  button(SDL_GAMEPAD_BUTTON_MISC5,kPadMisc5);button(SDL_GAMEPAD_BUTTON_MISC6,kPadMisc6);
  button(SDL_GAMEPAD_BUTTON_LEFT_STICK,kPadL3);button(SDL_GAMEPAD_BUTTON_RIGHT_STICK,kPadR3);
  button(SDL_GAMEPAD_BUTTON_DPAD_UP,kPadDUp);button(SDL_GAMEPAD_BUTTON_DPAD_DOWN,kPadDDown);button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,kPadDLeft);button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,kPadDRight);
  put(kPadLT,std::max(0.f,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)/32767.f));put(kPadRT,std::max(0.f,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)/32767.f));
  auto stick=[&](SDL_GamepadAxis ax,SDL_GamepadAxis ay,int up,int down,int left,int right){float x=SDL_GetGamepadAxis(pad,ax)/32768.f,y=SDL_GetGamepadAxis(pad,ay)/32768.f;put(right,std::max(x,0.f));put(left,std::max(-x,0.f));put(up,std::max(-y,0.f));put(down,std::max(y,0.f));};
  stick(SDL_GAMEPAD_AXIS_LEFTX,SDL_GAMEPAD_AXIS_LEFTY,kPadLSUp,kPadLSDown,kPadLSLeft,kPadLSRight);stick(SDL_GAMEPAD_AXIS_RIGHTX,SDL_GAMEPAD_AXIS_RIGHTY,kPadRSUp,kPadRSDown,kPadRSLeft,kPadRSRight);
  PadIdentity identity;
  {std::lock_guard lk(g_pads_mu);if(auto who=g_ident.find(id);who!=g_ident.end())identity=who->second;}
  auto view=input_map::view_for(current,input_map::profile_index(current,identity.guid,identity.name));
  if(input_map::gamecube_settings_chord(view,d.data()))d[kPadHome]=1;
  for(int p=0;p<kPadCount;p++)v[p]=std::max(v[p],d[p]);  // the shortcut below reads them ungated
  devices.emplace_back(id,d);
 }
 // who is who (per-controller bindings, Controller details, the setup offer)
 std::vector<PadIdentity> idents;
 {std::lock_guard lk(g_pads_mu);
  for(auto& [id,d]:devices){auto i=g_ident.find(id);idents.push_back(i!=g_ident.end()?i->second:PadIdentity{});}}
 // Home / Select open and close the menu here (independent of the frame rate): the press that does is
 // the overlay's, so the gates count this poll as captured when the menu was open before or is after;
 // so does a poll whose input a platform shortcut took (overlay/extensions.h)
 overlay::poll_controller_shortcuts(v);
 const bool captured=captured_before||overlay::captures()||overlay::shortcut_took_input();
 std::vector<overlay::PadValues> gated;
 g_gates.combine(devices,captured,v,game,&gated);  // a reconnected pad (new id) gets a new gate
 input_map::consume_shortcut_buttons(current,v,game);
 input::PadState state;
 if(current.controllers.empty()&&!current.preset.saved&&!current.preset.gamecube){
  state=input_map::controller_state(current,game);  // one set of bindings for all: as before
 }else{
  // each controller through its own bindings (a profile for its GUID / name, else the shared ones)
  for(size_t i=0;i<gated.size();i++){
   input_map::consume_shortcut_buttons(current,v,gated[i].data());
   auto& gate=g_start_gates[devices[i].first];
   if(captured)gate={};
   state=input_map::combine(state,gate.read(input_map::view_for(current,input_map::profile_index(current,idents[i].guid,idents[i].name)),gated[i].data()));
  }
 }
 // the controller in use (the overlay's confirm / back buttons follow its bindings), the setup offer on
 // a new controller's first press, and the list for Controller details
 static SDL_JoystickID active=0;
 std::vector<input::ControllerInfo> infos;
 for(size_t i=0;i<devices.size();i++){
  const SDL_JoystickID id=devices[i].first;
  bool pressed=false;
  for(int p=1;p<kPadCount;p++)pressed|=devices[i].second[p]>0.6f;
  if(pressed&&!idents[i].is_virtual&&id!=active){active=id;input_map::set_active_controller(idents[i].guid,idents[i].name);}
  if(pressed){
   bool offer=false;
   {std::lock_guard lk(g_pads_mu);offer=g_offer_pending.erase(id)>0;}
#ifdef __ANDROID__
   if(offer&&!g_controller_announced.load())offer=false;
#endif
   if(offer&&input_map::setup_offer_due(input_map::current(),idents[i].guid,idents[i].name,idents[i].is_virtual))
    overlay::offer_controller_setup(idents[i].guid,idents[i].name);
  }
  input::ControllerInfo c=idents[i].info;
  c.active=id==active;
  c.values.assign(devices[i].second.begin(),devices[i].second.end());
  {std::lock_guard lk(g_pads_mu);
   if(auto it=g_controllers.find(id);it!=g_controllers.end()){
    int pct=-1;
    switch(SDL_GetGamepadPowerInfo(it->second,&pct)){
    case SDL_POWERSTATE_ON_BATTERY:c.power="on battery";break;
    case SDL_POWERSTATE_NO_BATTERY:c.power="wired, no battery";break;
    case SDL_POWERSTATE_CHARGING:c.power="charging";break;
    case SDL_POWERSTATE_CHARGED:c.power="charged";break;
    default:c.power="unknown";break;}
    c.battery=pct;
    switch(SDL_GetGamepadConnectionState(it->second)){
    case SDL_JOYSTICK_CONNECTION_WIRED:c.connection="wired";break;
    case SDL_JOYSTICK_CONNECTION_WIRELESS:c.connection="wireless";break;
    default:c.connection="unknown";break;}
   }}
  infos.push_back(std::move(c));
 }
#ifdef __ANDROID__
 // Automatic gyro: this device's sensors while the player plays by touch (motion.h)
 motion::set_touch_play(phone_rumble::last_input_touch.load()&&gfx::g_touch_controls&&gfx::touch_buttons_visible());
#endif
 // + went down: the view may switch to the GamePad screen while the game is paused
 // (not while the settings overlay or the text prompt takes the buttons: Start acts there)
 {static bool plus=false;const bool now=(state.buttons&input::kPlus)!=0;if(now&&!plus&&!overlay::blocks_input())gfx::display_plus_pressed();plus=now;}
 std::lock_guard lk(g_mu);std::copy(std::begin(v),std::end(v),g_values);g_pad=state;g_infos=std::move(infos);
 if(!overlay::blocks_input()&&!getenv("WWHD_NO_HOST_INPUT")){motion::poll_recalibrate(game,g_keys);screenshot::poll_controller(game);}
}
void prompt_text(const std::u16string& initial,int max_len,std::function<void(bool,std::u16string)> done){std::lock_guard lk(g_mu);g_initial=initial;g_pending_max_len=std::max(0,max_len);if(g_initial.size()>(size_t)g_pending_max_len)g_initial.resize(g_pending_max_len);g_pending=std::move(done);}
// debug: WWHD_PRESS=1000-1010:8000,1500-1505:0008 holds VPAD buttons (hex) during TV frame ranges
struct Press { uint64_t from, to; uint32_t bits; };
static std::vector<Press> scripted() {
    std::vector<Press> v;
    if (const char* e = getenv("WWHD_PRESS")) {
        unsigned long long a, b; unsigned bits; int n;
        while (sscanf(e, "%llu-%llu:%x%n", &a, &b, &bits, &n) == 3) {
            v.push_back({a, b, bits});
            e += n;
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

// debug: WWHD_KEYS=1200-1210:K,1300-1305:LeftShift+J holds keyboard keys (input_map key names)
// during TV frame ranges. Unlike WWHD_PRESS they go through the controls mapping, so a test can
// check a remapped controls file (also with WWHD_NO_HOST_INPUT=1).
struct KeyPress { uint64_t from, to; std::vector<int> codes; };
static std::vector<KeyPress> scripted_keys() {
    std::vector<KeyPress> v;
    if (const char* e = getenv("WWHD_KEYS")) {
        unsigned long long a, b; int n;
        while (sscanf(e, "%llu-%llu:%n", &a, &b, &n) == 2) {
            e += n;
            KeyPress kp{a, b, {}};
            for (;;) {
                size_t len = strcspn(e, "+,");
                int code = input_map::key_from_id(std::string(e, len));
                if (code == input_map::kNoKey) LOG("[input] WWHD_KEYS: unknown key %.*s", (int)len, e);
                else kp.codes.push_back(code);
                e += len;
                if (*e != '+') break;
                e++;
            }
            v.push_back(kp);
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

// debug: WWHD_STICK=9400-9600:0:1,... holds the left stick at (x, y) during TV frame ranges
// (WWHD_RSTICK: the same for the right stick)
struct Stick { uint64_t from, to; float x, y; };
static std::vector<Stick> scripted_stick(const char* var = "WWHD_STICK") {
    std::vector<Stick> v;
    if (const char* e = getenv(var)) {
        unsigned long long a, b; float x, y; int n;
        while (sscanf(e, "%llu-%llu:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
            v.push_back({a, b, x, y});
            e += n;
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

// debug: timed test scenario, in real seconds from TV frame WWHD_TEST_ORIGIN (so a 30 fps and a 60 fps
// run get the same input at the same real time):
//   WWHD_TEST_STICK=2-5:0:1,...   left stick (x, y) from 2 s to 5 s
//   WWHD_TEST_RSTICK=2-5:1:0,...  right stick
//   WWHD_TEST_PRESS=3-3.1:8000    buttons (hex)
//   WWHD_TEST_MODE=2@0.5          60 fps mode at 0.5 s (0 off, 1 interpolation, 2 true 60)
//   WWHD_TEST_END=12              writes the file "test_done" at 12 s (the test script stops the game)
namespace {
struct TimedStick { double from, to; float x, y; };
struct TimedPress { double from, to; uint32_t bits; };
struct Scenario {
    uint64_t origin = 0;
    std::vector<TimedStick> sticks, rsticks;
    std::vector<TimedPress> presses;
    int mode = -1;
    double mode_at = 0, end = 0;
    Scenario() {
        if (const char* e = getenv("WWHD_TEST_ORIGIN")) origin = strtoull(e, nullptr, 10);
        auto timed_sticks = [](const char* e, std::vector<TimedStick>& out) {
            double a, b; float x, y; int n;
            while (e && sscanf(e, "%lf-%lf:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
                out.push_back({a, b, x, y});
                e += n;
                if (*e != ',') break;
                e++;
            }
        };
        timed_sticks(getenv("WWHD_TEST_STICK"), sticks);
        timed_sticks(getenv("WWHD_TEST_RSTICK"), rsticks);
        if (const char* e = getenv("WWHD_TEST_PRESS")) {
            double a, b; unsigned bits; int n;
            while (sscanf(e, "%lf-%lf:%x%n", &a, &b, &bits, &n) == 3) {
                presses.push_back({a, b, bits});
                e += n;
                if (*e != ',') break;
                e++;
            }
        }
        if (const char* e = getenv("WWHD_TEST_MODE")) sscanf(e, "%d@%lf", &mode, &mode_at);
        if (const char* e = getenv("WWHD_TEST_END")) end = atof(e);
    }
};
}  // namespace
}  // namespace input
namespace interp { void set_mode(int m); uint64_t logic_steps(); }
namespace true60_test { void set_origin_step(uint64_t s); void tick(double t, bool ended); }
namespace input {
static void apply_scenario(PadState& s) {
    static Scenario sc;
    // WWHD_TEST_ORIGIN_LOAD=n: the scenario starts n logic steps after the last save-state load (a
    // load completes asynchronously, so a fixed frame can fall a step apart between two runs)
    static const char* ol = getenv("WWHD_TEST_ORIGIN_LOAD");
    uint64_t ol_step = 0;
    // (the clock is the number of Link's full-pass executes since the load, true60::link_steps: the
    // pass at which a load lands differs between runs, and the steps after it are the game's)
    if (ol) {
        if (!true60::state_loaded()) return;
        static uint64_t found = 0;
        if (!found) {
            int64_t past = (int64_t)true60::link_steps() - (int64_t)strtoull(ol, nullptr, 10);
            if (past < 0) return;
            found = interp::logic_steps() - (uint64_t)past;  // the logic step at which the count reached it
        }
        ol_step = found;
        sc.origin = 1;
    }
    if (!sc.origin || (!ol && fixed_seed::script_frame(render::frame_count()) < sc.origin)) return;
    // scenario time = game time: full logic steps / 30 (frame-time hitches don't shift the input)
    static const uint64_t s0 = [ol_step] {
        present_observer::scenario_origin(render::frame_count(), ol_step ? ol_step : interp::logic_steps());
        if (ol_step) {
            LOG("[test] origin at logic step %llu (load + WWHD_TEST_ORIGIN_LOAD), logic step %llu", (unsigned long long)ol_step,
                (unsigned long long)ol_step);
            true60_test::set_origin_step(ol_step);
            return ol_step;
        }
        LOG("[test] origin at guest time %.4f s, logic step %llu", (double)timebase::now() / timebase::kTicksPerSec,
            (unsigned long long)interp::logic_steps());
        true60_test::set_origin_step(interp::logic_steps());
        return interp::logic_steps();
    }();
    double t = (double)(interp::logic_steps() - s0) / 30.0;
    static std::atomic<bool> mode_set{false}, ended{false};
    static std::atomic<int> dbg{0};
    if (getenv("WWHD_TEST_DEBUG") && dbg++ % 30 == 0) LOG("[test] t=%.3f frame %llu", t, (unsigned long long)render::frame_count());
    if (sc.mode >= 0 && t >= sc.mode_at && !mode_set.exchange(true)) {
        LOG("[test] t=%.3f s: 60 fps mode %d", t, sc.mode);
        interp::set_mode(sc.mode);
    }
    if (sc.end > 0 && t >= sc.end && !ended.exchange(true)) {
        LOG("[test] t=%.3f s: end", t);
        #ifdef WWHD_HAS_VULKAN
        gfxvk::stutter::trace().flush();
        if (render::active() == render::Api::Vulkan) {
            gx2::checkpoint_vulkan_caches();
            gx2::checkpoint_present_observer();
        }
#endif
        process_peak_rss::write_endpoint(render::frame_count());
        // The headless harness stops the process as soon as it sees test_done.
        // Publish complete trace records before that marker, rather than losing
        // buffered Link/camera records or leaving a partial final line.
        fflush(nullptr);
        if (FILE* f = fopen("test_done", "w")) fclose(f);
    }
    true60_test::tick(t, sc.end > 0 && t >= sc.end);
    // Opt-in raw controller fixture: exercise bindings and the dead zone before guest delivery.
    static const bool controller_stick = getenv("WWHD_TEST_CONTROLLER_STICK") != nullptr;
    for (auto& p : sc.sticks)
        if (t >= p.from && t < p.to) {
            s.lx = p.x; s.ly = p.y;
            if (controller_stick) {
                float values[input_map::kPadCount]{};
                values[p.x < 0 ? input_map::kPadLSLeft : input_map::kPadLSRight] = std::fabs(p.x);
                values[p.y < 0 ? input_map::kPadLSDown : input_map::kPadLSUp] = std::fabs(p.y);
                auto mapped = input_map::controller_state(input_map::current(), values);
                s.lx = mapped.lx; s.ly = mapped.ly;
            }
        }
    for (auto& p : sc.rsticks)
        if (t >= p.from && t < p.to) { s.rx = p.x; s.ry = p.y; }
    for (auto& p : sc.presses)
        if (t >= p.from && t < p.to) s.buttons |= p.bits;
}

static std::atomic<bool> g_pro{getenv("WWHD_PRO_CONTROLLER") != nullptr};
bool pro_controller() { return g_pro.load(std::memory_order_relaxed); }
static std::atomic<void (*)(bool)> g_controller_listener{nullptr};
void set_controller_listener(void (*fn)(bool pro)) { g_controller_listener = fn; }
void set_pro_controller(bool on) {
    const bool was = g_pro.exchange(on);
    LOG("[input] keyboard/controller act as %s", on ? "Pro Controller" : "GamePad");
    if (was != on)
        if (auto fn = g_controller_listener.load()) fn(on);
}

PadState read() {
    static const std::vector<Press> script = scripted();
    static const std::vector<Stick> sticks = scripted_stick();
    static const std::vector<KeyPress> keys = scripted_keys();
    static const std::vector<Stick> rsticks = scripted_stick("WWHD_RSTICK");
    std::lock_guard<std::mutex> lk(g_mu);
    // debug: WWHD_NO_HOST_INPUT=1 ignores keyboard and host controllers (scripted test runs)
    static const bool no_host = getenv("WWHD_NO_HOST_INPUT") != nullptr;
    const uint64_t frame = fixed_seed::script_frame(render::frame_count());  // (TV frame unless WWHD_TEST_FIXED_SEED)
    if (!keys.empty()) {
        memset(g_script_keys, 0, sizeof g_script_keys);
        for (auto& p : keys)
            if (frame >= p.from && frame <= p.to)
                for (int c : p.codes) if (c < 256) g_script_keys[c] = true;
    }
    PadState k = keyboard_state(!no_host), s = g_pad;
    if (no_host) s = PadState{};
    for (auto& p : script)
        if (frame >= p.from && frame <= p.to) s.buttons |= p.bits;
    s.buttons |= dsweep::pad_buttons(frame);  // a title D-sweep (setup) passes the boot's notice screen
    for (auto& p : sticks)
        if (frame >= p.from && frame <= p.to) { s.lx = p.x; s.ly = p.y; }
    for (auto& p : rsticks)
        if (frame >= p.from && frame <= p.to) { s.rx = p.x; s.ry = p.y; }
    apply_scenario(s);
    s.buttons |= k.buttons;
    if (k.lx || k.ly) { s.lx = k.lx; s.ly = k.ly; }
    if (k.rx || k.ry) { s.rx = k.rx; s.ry = k.ry; }
    s.touch = g_touch;
    s.tx = g_tx;
    s.ty = g_ty;
    s.virt |= g_touch_virt.load(std::memory_order_relaxed);  // the on-screen Jump / Moon jump buttons
    s.virt |= k.virt;
    // debug: WWHD_LOG_BUTTONS=1 logs every change of the merged button bits
    static const bool log_buttons = getenv("WWHD_LOG_BUTTONS") != nullptr;
    static uint32_t last_buttons = 0;
    if (log_buttons && s.buttons != last_buttons) {
        LOG("[input] frame %llu buttons %04X", (unsigned long long)render::frame_count(), s.buttons);
        last_buttons = s.buttons;
    }
    mods::filter_pad(s);  // gameplay mods: mouse camera, wheel -> R3
    if (overlay::blocks_input()) {
        mods::quick_use_reset();  // no pending item press survives menu/text capture
        s = PadState{};
    } else {
        mods::quick_use_filter_pad(s);  // final merged/polled pad, never overlay-owned input
    }
    file_select_quit::filter(s);
    return s;
}


} // namespace input
