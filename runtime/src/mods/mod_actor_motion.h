// Pure game-step actor motion; all restorable state is explicit and pointer-free.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
namespace mod_actor::motion {
inline constexpr double Pi=3.14159265358979323846;
struct Point {float x=0,y=0,z=0;};
inline bool valid(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<=350000&&std::fabs(p.y)<=350000&&std::fabs(p.z)<=350000;}
inline bool timestep(float dt){return std::isfinite(dt)&&dt>=0&&dt<=0.25f;}
inline double wrap(double a){a=std::fmod(a+Pi,2*Pi);if(a<0)a+=2*Pi;return a-Pi;}
inline double approach(double from,double to,double maximum){return from+std::clamp(wrap(to-from),-maximum,maximum);}
struct LookLimits {float yaw=1.2f,pitch=0.6f,rate=3,radius=500;bool upper_body=false;float upper_yaw=0.4f,upper_fraction=0.3f;};
struct LookState {float head_yaw=0,head_pitch=0,upper_yaw=0;};
inline bool look_at(Point head,Point target,float body_yaw,float dt,const LookLimits&limits,LookState&state){
    if(!valid(head)||!valid(target)||!std::isfinite(body_yaw)||!timestep(dt)||!std::isfinite(limits.yaw)||limits.yaw<0||limits.yaw>Pi||!std::isfinite(limits.pitch)||limits.pitch<0||limits.pitch>Pi/2||!std::isfinite(limits.rate)||limits.rate<=0||limits.rate>20||!std::isfinite(limits.radius)||limits.radius<=0||limits.radius>1000000||!std::isfinite(state.head_yaw)||!std::isfinite(state.head_pitch)||!std::isfinite(state.upper_yaw))return false;
    if(limits.upper_body&&(!std::isfinite(limits.upper_yaw)||limits.upper_yaw<0||limits.upper_yaw>Pi||!std::isfinite(limits.upper_fraction)||limits.upper_fraction<0||limits.upper_fraction>1))return false;
    if(dt==0)return true;
    const double dx=double(target.x)-head.x,dy=double(target.y)-head.y,dz=double(target.z)-head.z,horizontal=std::hypot(dx,dz),distance=std::hypot(horizontal,dy);
    if(distance<1e-6)return true; // no direction exists; retain current pose
    double yaw=0,pitch=0;if(distance<=limits.radius){yaw=wrap(std::atan2(dx,dz)-body_yaw);pitch=std::atan2(dy,horizontal);}
    const double rate=double(limits.rate)*dt;
    if(limits.upper_body)state.upper_yaw=float(approach(state.upper_yaw,std::clamp(yaw*limits.upper_fraction,-double(limits.upper_yaw),double(limits.upper_yaw)),rate));else state.upper_yaw=0;
    state.head_yaw=float(approach(state.head_yaw,std::clamp(wrap(yaw-state.upper_yaw),-double(limits.yaw),double(limits.yaw)),rate));
    state.head_pitch=float(approach(state.head_pitch,std::clamp(pitch,-double(limits.pitch),double(limits.pitch)),rate));return true;
}
struct Blocker {Point position;float radius=0,height=0;bool enabled=false;};
struct Sweep {Point position;bool blocked=false,valid=false;};
inline Sweep sweep(Point from,Point to,float radius,float height,const Blocker&b){
    Sweep result{from,false,false};if(!valid(from)||!valid(to)||!std::isfinite(radius)||radius<=0||radius>1000||!std::isfinite(height)||height<=0||height>2000)return result;
    if(!b.enabled){return {to,false,true};}if(!valid(b.position)||!std::isfinite(b.radius)||b.radius<=0||b.radius>1000||!std::isfinite(b.height)||b.height<=0||b.height>2000)return result;
    if(std::max(from.y,to.y)+height<=b.position.y||std::min(from.y,to.y)>=b.position.y+b.height)return {to,false,true};
    const double x=double(from.x)-b.position.x,z=double(from.z)-b.position.z,vx=double(to.x)-from.x,vz=double(to.z)-from.z,a=vx*vx+vz*vz,r=double(radius)+b.radius,c=x*x+z*z-r*r,dot=x*vx+z*vz;
    if(a<1e-16)return {to,false,true};
    if(c<0){if(dot>=0)return {to,false,true};return {from,true,true};} // only separating motion from an overlap
    const double discriminant=dot*dot-a*c;if(discriminant<0)return {to,false,true};const double hit=(-dot-std::sqrt(discriminant))/a;
    if(hit<0||hit>1)return {to,false,true};const double fraction=std::max(0.0,hit-0.01/std::sqrt(a));return {{float(from.x+vx*fraction),float(from.y+(to.y-from.y)*fraction),float(from.z+vz*fraction)},true,true};
}
// bounce: back and forth (it turns at the last point and walks back to the first, and again)
struct Path {std::array<Point,16> points{};unsigned count=0;bool loop=false;float speed=100,arrival=1,height_tolerance=30;bool bounce=false;};
struct PathState {unsigned next=0;bool finished=false;bool reverse=false;};
enum class Status {Moving,Blocked,Finished,Invalid,SubstepLimit};
struct Move {Point position;float yaw=0,traveled=0;unsigned substeps=0;Status status=Status::Invalid;bool has_direction=false;};
inline double horizontal(Point a,Point b){return std::hypot(double(a.x)-b.x,double(a.z)-b.z);}
inline bool valid(const Path&p){
    if(p.count<2||p.count>16||!std::isfinite(p.speed)||p.speed<=0||p.speed>1500||!std::isfinite(p.arrival)||p.arrival<=0||p.arrival>10||!std::isfinite(p.height_tolerance)||p.height_tolerance<0||p.height_tolerance>2000)return false;
    for(unsigned n=0;n<p.count;++n){if(!valid(p.points[n])||(n&&horizontal(p.points[n-1],p.points[n])<=p.arrival))return false;}return !p.loop||horizontal(p.points[0],p.points[p.count-1])>p.arrival;
}
// apply(from,proposal) returns the position actually accepted by native collision
// and ground following. Rejection leaves the waypoint pending; no intended move
// or render interpolation can advance the serialized path state.
template<class Apply> Move advance(const Path&path,PathState&state,Point position,float dt,float radius,float height,const Blocker&blocker,Apply apply){
    Move result{position};if(!valid(path)||!valid(position)||!timestep(dt)||state.next>=path.count||!std::isfinite(radius)||radius<=0||radius>1000||!std::isfinite(height)||height<=0||height>2000)return result;
    if(state.finished){result.status=Status::Finished;return result;}result.status=Status::Moving;if(dt==0)return result;double budget=double(path.speed)*dt;
    for(unsigned step=0;step<16;++step){result.substeps=step+1;const Point target=path.points[state.next];const double remaining=horizontal(result.position,target);
        if(remaining<=path.arrival){if(std::fabs(result.position.y-target.y)>path.height_tolerance){result.status=Status::Blocked;return result;}
            if(state.reverse){if(state.next==0){state.reverse=false;state.next=1;}else --state.next;continue;}
            if(state.next+1==path.count){if(path.bounce){state.reverse=true;state.next=path.count-2;continue;}if(!path.loop){state.finished=true;result.status=Status::Finished;return result;}state.next=0;}else ++state.next;continue;}
        if(budget<=1e-4)return result;const double distance=std::min(budget,remaining),fraction=distance/remaining;const Point from=result.position,proposal{float(from.x+(target.x-from.x)*fraction),from.y,float(from.z+(target.z-from.z)*fraction)};
        result.yaw=float(std::atan2(double(target.x)-from.x,double(target.z)-from.z));result.has_direction=true;const auto clipped=sweep(from,proposal,radius,height,blocker);if(!clipped.valid){result.status=Status::Invalid;return result;}
        const Point accepted=apply(from,clipped.position);const double traveled=horizontal(from,accepted);
        if(!valid(accepted)||traveled>distance+0.02){result.status=Status::Invalid;return result;}result.position=accepted;result.traveled+=float(traveled);budget=std::max(0.0,budget-traveled);
        if(clipped.blocked||horizontal(accepted,clipped.position)>0.02||traveled<1e-6){result.status=Status::Blocked;return result;}
    }
    result.status=Status::SubstepLimit;return result;
}
// Strolling pauses (a path's "pauses"): walk 200..300 logic steps, then stand 60..90, from a seed the
// restorable state carries. step() advances one logic step: true while it walks.
struct Pauses {bool waiting=false;unsigned timer=0;uint32_t seed=1;};
inline uint32_t pause_random(uint32_t& seed){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;if(!seed)seed=1;return seed;}
inline bool pause_step(Pauses& p){
    if(p.timer==0){p.timer=p.waiting?60+pause_random(p.seed)%31:200+pause_random(p.seed)%101;}
    const bool walking=!p.waiting;
    if(--p.timer==0)p.waiting=!p.waiting;
    return walking;
}
static_assert(std::is_trivially_copyable_v<LookState>&&std::is_trivially_copyable_v<PathState>);
}
