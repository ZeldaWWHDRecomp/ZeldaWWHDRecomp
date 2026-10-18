// Deterministic floating pose and bounded drift; native game queries supply samples.
#pragma once
#include <string>
namespace story_world_motion {
struct Point {float x=0,y=0,z=0;};
struct Samples {float center=0,left=0,right=0,front=0,back=0,radius=200;};
struct Config {
    Point origin,base_rotation; // Base rotation is in degrees, native RY * RX * RZ order.
    float draft=0,damping=.25f,max_tilt=10; // Seconds; max_tilt bounds wave-induced tilt.
    float wind_speed=0,current_x=0,current_z=0,max_speed=0;
    bool drifting=false,anchored=false;
    Point anchor;
    float anchor_radius=0;
};
struct State {Point position,rotation,velocity,residual;bool initialized=false;};
inline constexpr float MaxCoordinate=350000;
bool valid(const Config&,std::string& why);
// Requires initialized snapshot-owned state, including bounded sub-ULP residuals.
bool valid_state(const Config&,const State&,std::string& why);
// wind is the native direction multiplied by native strength; its planar length
// is clamped to one, retaining calm/partial strength. dt is seconds, (0,10].
// Samples are world-axis heights at center and +/-radius X/Z. First invocation
// initializes the pose at water height. Failure leaves State unchanged.
bool step(const Config&,const Samples&,Point wind,float dt,State&,std::string& why);
}
