#include "overlay/toggle_guard.h"
#include "overlay/open_reason.h"
#include "overlay/settings_shortcut.h"
#include "overlay/host_focus.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>
int main(){
 using namespace overlay;
 ImGui::CreateContext();auto& io=ImGui::GetIO();
 io.KeysData[ImGuiKey_A-ImGuiKey_NamedKey_BEGIN].Down=true;
 io.KeyShift=true;io.MouseDown[0]=true;
 io.AddKeyEvent(ImGuiKey_B,true);
 assert(ImGui::GetCurrentContext()->InputEventsQueue.Size>0);
 release_focus_inputs(io);
 assert(!io.KeysData[ImGuiKey_A-ImGuiKey_NamedKey_BEGIN].Down&&!io.KeyShift&&!io.MouseDown[0]);
 assert(ImGui::GetCurrentContext()->InputEventsQueue.Size==0);
 io.AddKeyEvent(ImGuiKey_C,true); // the new window's events still arrive
 assert(ImGui::GetCurrentContext()->InputEventsQueue.Size==1);
 ImGui::DestroyContext();
 ToggleGuard g;
 assert(g.press(true,false,1));
 assert(!g.press(true,false,2)); // same press, even well beyond debounce
 assert(!g.press(true,true,2));
 assert(!g.press(false,false,2));
 assert(g.press(true,false,2.1));
 assert(!g.press(false,false,2.11));
 assert(!g.press(true,false,2.12)); // release/repress noise
 assert(!g.press(false,false,2.13));
 assert(g.press(true,false,2.26)); // deliberate next press
 for(auto reason:{OpenReason::API,OpenReason::F1,OpenReason::Esc,OpenReason::Home,OpenReason::View,OpenReason::B,OpenReason::CloseButton,OpenReason::LoadSlot,OpenReason::Focus}){
  auto line=open_message(false,reason,{7,1234,"GamePad"},2);
  assert(line.find(reason_name(reason))!=std::string::npos);
  assert(line.find("window GamePad id 7, timestamp 1234, frame 2")!=std::string::npos);
  assert(line.starts_with("[overlay] closed ("));
 }
 assert(open_message(true,OpenReason::F1,{1,99,"TV"},42)=="[overlay] opened (F1 key, window TV id 1, timestamp 99, frame 42)");
 SettingsShortcut s;
 assert(s.poll(0,false,true,false)==SettingsShortcut::Open&&s.reason()==OpenReason::Home);
 assert(s.poll(.1,false,false,true)==SettingsShortcut::None);
 assert(s.poll(.2,true,false,true)==SettingsShortcut::Close&&s.reason()==OpenReason::View);
 // Select for the game while a hold could open Settings: a tap arrives on release, a hold never
 SelectForGame tap;
 assert(tap.filter(1.0,1,true)==0&&tap.filter(1.2,1,true)==0);  // held: not yet
 assert(tap.filter(1.3,0,true)==1);  // let go before 0.5 s: the game's press now
 assert(tap.filter(1.3+SelectForGame::kPulseSeconds*0.9,0,true)==1);
 assert(tap.filter(1.3+SelectForGame::kPulseSeconds*1.1,0,true)==0);
 SelectForGame hold;
 for(double t=5;t<5+kSelectHoldSeconds+0.3;t+=0.01) assert(hold.filter(t,1,true)==0);  // Settings opens
 assert(hold.filter(6,0,true)==0&&hold.filter(6.05,0,true)==0);  // let go after: still nothing
 // open with a hold, close with a tap: the press that closed Settings (while it was open, inactive) is still
 // down when the filter becomes active again; it stays from the game and its release gives no tap
 SelectForGame close;
 for(double t=10;t<10+kSelectHoldSeconds+0.1;t+=0.01) assert(close.filter(t,1,true)==0);  // the hold that opens
 assert(close.filter(11,0,false)==0);   // Settings open, Select up
 assert(close.filter(12,1,false)==1);   // the closing press, while open (the game's input is blocked then)
 assert(close.filter(12.05,1,true)==0); // Settings closed, Select still down
 assert(close.filter(12.1,0,true)==0);  // let go: no tap
 for(double t=12.1;t<12.5;t+=0.01) assert(close.filter(t,0,true)==0);
 assert(close.filter(13,1,true)==0&&close.filter(13.1,0,true)==1);  // the next tap is one again
 SelectForGame off;  // Select is a modifier of another shortcut, or Settings can't open: as before
 assert(off.filter(0,1,false)==1&&off.filter(0.6,1,false)==1&&off.filter(0.7,0,false)==0);
 puts("overlay_toggle: focus release, debounce, all transition reasons and the Select hold passed");
}
