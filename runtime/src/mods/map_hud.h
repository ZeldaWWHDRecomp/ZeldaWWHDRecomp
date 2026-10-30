#pragma once
#include <cstdint>
#include <functional>
#include <string>
struct Cpu;
namespace guestmods::maphud {
using Hook=void(*)(Cpu*);
using Original=void(*)(Cpu*,uint32_t);
void install(Original,const std::function<bool(uint32_t,bool)>& available,const std::function<void(uint32_t,Hook)>& replace);
void begin_logic(Cpu*,double dt);
void finish_logic();
void state_loaded();
void revoke(uint32_t owner);
bool hide_pane(uint32_t pane);
uint32_t capability();
int compasses(Cpu*,uint32_t owner,uint32_t replace,bool draw);
// Rectangle: TV HUD coordinates, top-left origin, +y down, enclosing box.
struct Rect {float x,y,w,h;};
int rectangle(uint32_t owner,Rect&);
int selected(Cpu*,bool draw);
int select(Cpu*,uint32_t owner,uint32_t index,bool draw);
int cancel(Cpu*,uint32_t owner,bool draw);
}
