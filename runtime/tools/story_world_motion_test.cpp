#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include "mods/story_world_motion.h"
using namespace story_world_motion;
namespace {
constexpr double Pi=3.14159265358979323846;
bool near_value(double a,double b,double epsilon=1e-4){return std::abs(a-b)<=epsilon;}
Point up(Point r){const double x=r.x*Pi/180,y=r.y*Pi/180,z=r.z*Pi/180;return {float(-std::cos(y)*std::sin(z)+std::sin(y)*std::sin(x)*std::cos(z)),float(std::cos(x)*std::cos(z)),float(std::sin(y)*std::sin(z)+std::cos(y)*std::sin(x)*std::cos(z))};}
bool same(Point a,Point b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool same(const State&a,const State&b){return a.initialized==b.initialized&&same(a.position,b.position)&&same(a.rotation,b.rotation)&&same(a.velocity,b.velocity)&&same(a.residual,b.residual);}
}
int main(){std::string why;Config c;c.origin={100,40,-200};c.draft=12;Samples s;s.center=100;s.left=s.right=s.front=s.back=100;State a;
    assert(step(c,s,{},1.f/30,a,why)&&a.initialized&&near_value(a.position.y,128)&&a.velocity.y==0&&a.position.x==100&&a.position.z==-200);
    // Stable first-order response: one large step and many smaller steps agree,
    // including restoring only the explicit state half way through.
    s.center=200;State one=a,many=a;assert(step(c,s,{},1,one,why));for(unsigned i=0;i<30;++i){if(i==15){State restored=many;many=restored;}const float before=many.position.y;assert(step(c,s,{},1.f/30,many,why));assert(many.position.y>=before&&many.position.y<228);}
    assert(near_value(one.position.y,many.position.y,.0001)&&near_value(one.position.y,228-100*std::exp(-4),.0001));
    c.damping=0;assert(step(c,s,{},1.f/30,a,why)&&a.position.y==228);
    // World-axis slopes must remain correct through the native yaw convention.
    c.origin={};c.draft=0;c.max_tilt=30;s={0,-40,40,-20,20,200};
    for(float yaw:{0.f,90.f,-90.f,180.f,37.f}){c.base_rotation={0,yaw,0};State state;assert(step(c,s,{},1.f/30,state,why));const Point normal=up(state.rotation);const double norm=std::sqrt(1+.2*.2+.1*.1);assert(near_value(normal.x,-.2/norm)&&near_value(normal.y,1/norm)&&near_value(normal.z,-.1/norm));}
    c.base_rotation={5,42,-3};s={0,0,0,0,0,200};State authored;assert(step(c,s,{},1.f/30,authored,why)&&same(authored.rotation,c.base_rotation));
    c.base_rotation={};c.max_tilt=10;s={0,-20000,20000,-20000,20000,200};State tilted;assert(step(c,s,{},1.f/30,tilted,why));const auto normal=up(tilted.rotation);assert(near_value(std::acos(normal.y)*180/Pi,10,.0001));
    c.max_tilt=0;assert(step(c,s,{},1.f/30,tilted,why)&&same(tilted.rotation,Point{}));
    // Wind retains native strength; current and combined speed are bounded.
    c.drifting=true;c.wind_speed=100;c.current_x=0;c.current_z=0;c.max_speed=100;s={};State calm,half,full;assert(step(c,s,{},1,calm,why)&&calm.position.x==0);assert(step(c,s,{.5f,0,0},1,half,why)&&half.position.x==50);assert(step(c,s,{1000,0,0},1,full,why)&&full.position.x==100);
    c.current_x=100;c.current_z=100;c.max_speed=25;State bounded;assert(step(c,s,{1,0,1},1,bounded,why));assert(near_value(std::hypot(bounded.velocity.x,bounded.velocity.z),25)&&near_value(std::hypot(bounded.position.x,bounded.position.z),25));
    // A slow current at sea coordinates must accumulate sub-ULP displacement.
    c.origin={200000,0,-200000};c.wind_speed=0;c.current_x=.01f;c.current_z=0;c.max_speed=1;State weak;for(unsigned i=0;i<3000;++i)assert(step(c,s,{},1.f/30,weak,why));assert(near_value(double(weak.position.x)+weak.residual.x,200001,.001));
    // Projection onto a convex anchor disk cannot increase drift speed. Large
    // timesteps and nonzero sea coordinates still leave the actor inside it.
    c.origin={200000,0,-200000};c.anchor=c.origin;c.anchored=true;c.anchor_radius=10;c.current_x=100;c.current_z=100;c.max_speed=100;State tethered;for(unsigned i=0;i<100;++i){assert(step(c,s,{},10,tethered,why));assert(std::hypot(double(tethered.position.x)-c.anchor.x,double(tethered.position.z)-c.anchor.z)<=10);assert(std::hypot(tethered.velocity.x,tethered.velocity.z)<=100.001);}
    assert(valid_state(c,tethered,why));State corrupt=tethered;corrupt.residual.x=1000;assert(!valid_state(c,corrupt,why));corrupt=tethered;corrupt.position.x=350001;assert(!valid_state(c,corrupt,why));corrupt=tethered;corrupt.position.y=INFINITY;assert(!valid_state(c,corrupt,why));corrupt=tethered;corrupt.rotation.z=361;assert(!valid_state(c,corrupt,why));corrupt=tethered;corrupt.velocity.x=NAN;assert(!valid_state(c,corrupt,why));
    const State saved=tethered;Config invalid=c;invalid.damping=-1;assert(!step(invalid,s,{},1,tethered,why)&&same(saved,tethered));invalid=c;invalid.origin.x+=11;assert(!valid(invalid,why));invalid=c;invalid.anchor_radius=0;assert(!valid(invalid,why));invalid=c;invalid.current_x=101;assert(!valid(invalid,why));
    auto bad=s;bad.left=std::numeric_limits<float>::quiet_NaN();assert(!step(c,bad,{},1,tethered,why)&&same(saved,tethered));bad=s;bad.radius=0;assert(!step(c,bad,{},1,tethered,why)&&same(saved,tethered));assert(!step(c,s,{},0,tethered,why)&&same(saved,tethered));assert(!step(c,s,{INFINITY,0,0},1,tethered,why)&&same(saved,tethered));
    State outside=saved;outside.position.x+=30;const State before=outside;assert(!step(c,s,{},1,outside,why)&&same(before,outside));
}
