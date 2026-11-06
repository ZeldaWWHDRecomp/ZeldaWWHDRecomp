#pragma once
namespace gfxvk {
// Renderer-thread aggregate. The producer-visible atomic remains unchanged
// while either window is being evaluated, then receives one complete result.
class GamepadBackdropDemand {
 bool active_=false,needed_=false;
public:
 void begin() { active_=true;needed_=false; }
 void request() { if(active_)needed_=true; }
 bool finish(bool& needed) { if(!active_)return false;needed=needed_;active_=false;return true; }
 void cancel() { active_=false; }
};
}
