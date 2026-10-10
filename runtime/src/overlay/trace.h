#pragma once
#include "imgui_internal.h"
#include "wwhd_trace.h"
#include <string>
namespace overlay_trace {
inline void mouse_pos(ImGuiIO& io,float x,float y) { if(wwhd_trace::enabled()) wwhd_trace::emit("mouse pos x=%.3f y=%.3f",x,y); io.AddMousePosEvent(x,y); }
inline void mouse_button(ImGuiIO& io,int button,bool down) { if(wwhd_trace::enabled()) wwhd_trace::emit("mouse button=%d down=%d",button,down); io.AddMouseButtonEvent(button,down); }
inline void wheel(ImGuiIO& io,float x,float y) { if(wwhd_trace::enabled()) wwhd_trace::emit("wheel x=%.3f y=%.3f",x,y); io.AddMouseWheelEvent(x,y); }
inline void state() {
 if(!wwhd_trace::enabled() || !wwhd_trace::open) return;
 const auto& g=*ImGui::GetCurrentContext(); char state[768];
 std::snprintf(state,sizeof state,"nav id=%08x window=%s active=%d source=%d cursor=%d active_id=%08x hovered=%s focused=%s",
  g.NavId,g.NavWindow?g.NavWindow->Name:"-",g.IO.NavActive,int(g.NavInputSource),g.NavCursorVisible,g.ActiveId,g.HoveredWindow?g.HoveredWindow->Name:"-",g.NavWindow?g.NavWindow->Name:"-");
 static std::string previous;
 if(previous!=state) { wwhd_trace::emit("frame=%d %s",g.FrameCount,state); previous=state; }
}
}
