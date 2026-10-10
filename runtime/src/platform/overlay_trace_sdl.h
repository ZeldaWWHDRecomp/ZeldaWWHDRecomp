// Raw per-device diagnostic snapshots before aggregation. Caller owns the SDL thread.
#pragma once
#include "wwhd_trace.h"
#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <string>
namespace overlay_trace_sdl {
struct Snapshot { std::array<Sint16,SDL_GAMEPAD_AXIS_COUNT> axes{}; unsigned long long buttons=0; bool valid=false; };
inline std::map<SDL_JoystickID,Snapshot> previous;
inline void begin(bool open) { wwhd_trace::open=open; if(!open) previous.clear(); }
inline void pad(SDL_JoystickID id, SDL_Gamepad* pad) {
 if(!wwhd_trace::open) return;
 Snapshot now;
 for(int a=0;a<SDL_GAMEPAD_AXIS_COUNT;a++) now.axes[a]=SDL_GetGamepadAxis(pad,(SDL_GamepadAxis)a);
 for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;b++) if(SDL_GetGamepadButton(pad,(SDL_GamepadButton)b)) now.buttons|=1ull<<b;
 auto& old=previous[id];
 if(!old.valid||old.axes!=now.axes||old.buttons!=now.buttons) {
  std::string axes, buttons;
  for(int a=0;a<SDL_GAMEPAD_AXIS_COUNT;a++) { char item[96]; std::snprintf(item,sizeof item," %s=%d/%.6f",SDL_GetGamepadStringForAxis((SDL_GamepadAxis)a),now.axes[a],now.axes[a]/(a>=SDL_GAMEPAD_AXIS_LEFT_TRIGGER?32767.f:32768.f)); axes+=item; }
  for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;b++) if(now.buttons&(1ull<<b)) { buttons+=' '; buttons+=SDL_GetGamepadStringForButton((SDL_GamepadButton)b); }
  wwhd_trace::emit("pad id=%u name=%s axes(raw/normalized)%s buttons(raw=normalized=1):%s",(unsigned)id,SDL_GetGamepadName(pad),axes.c_str(),buttons.empty()?" none":buttons.c_str());
  now.valid=true; old=now;
 }
}
}
