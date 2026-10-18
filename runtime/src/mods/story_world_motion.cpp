#include "story_world_motion.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace story_world_motion { namespace {
constexpr double Pi=3.14159265358979323846,Rad=Pi/180,Deg=180/Pi;
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool range(float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
float angle(double v){return float(std::remainder(v,360.));}
float damp_angle(float from,float to,double alpha){return angle(double(from)+std::remainder(double(to)-from,360.)*alpha);}
double residual_limit(float p){return 4*std::max(std::abs(double(std::nextafter(p,INFINITY))-p),std::abs(double(std::nextafter(p,-INFINITY))-p));}
bool coordinates(Point p){return finite(p)&&std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})<=MaxCoordinate;}
bool inside(Point p,const Config&c){return std::hypot(double(p.x)-c.anchor.x,double(p.z)-c.anchor.z)<=double(c.anchor_radius);}
}
bool valid(const Config&c,std::string&why){
    why.clear();if(!coordinates(c.origin)||!finite(c.base_rotation)||!range(c.base_rotation.x,-360,360)||!range(c.base_rotation.y,-360,360)||!range(c.base_rotation.z,-360,360)){why="invalid floating origin or rotation";return false;}
    if(!range(c.draft,-1000,1000)||!range(c.damping,0,10)||!range(c.max_tilt,0,30)){why="invalid bounded buoyancy configuration";return false;}
    if(!range(c.wind_speed,0,100)||!range(c.current_x,-100,100)||!range(c.current_z,-100,100)||!range(c.max_speed,0,100)){why="invalid bounded drift configuration";return false;}
    if(c.anchored&&(!c.drifting||!coordinates(c.anchor)||!range(c.anchor_radius,0,10000)||c.anchor_radius==0||!inside(c.origin,c))){why="invalid anchor or origin outside anchor radius";return false;}
    return true;
}
bool valid_state(const Config&c,const State&state,std::string&why){
    if(!valid(c,why))return false;
    if(!state.initialized||!coordinates(state.position)||!finite(state.rotation)||!finite(state.velocity)||!finite(state.residual)||!range(state.rotation.x,-360,360)||!range(state.rotation.y,-360,360)||!range(state.rotation.z,-360,360)){why="invalid restored floating state";return false;}
    if(std::abs(double(state.residual.x))>residual_limit(state.position.x)||std::abs(double(state.residual.y))>residual_limit(state.position.y)||std::abs(double(state.residual.z))>residual_limit(state.position.z)){why="floating position residual exceeds its precision bound";return false;}
    if(c.anchored&&!inside(state.position,c)){why="restored floating state is outside its anchor";return false;}
    why.clear();return true;
}
bool step(const Config&c,const Samples&s,Point wind,float dt,State&state,std::string&why){
    if(!valid(c,why))return false;
    if(!range(dt,std::numeric_limits<float>::min(),10)||!finite(wind)||!std::isfinite(s.center)||!std::isfinite(s.left)||!std::isfinite(s.right)||!std::isfinite(s.front)||!std::isfinite(s.back)||!range(s.radius,.001f,20000)){why="invalid native wave/wind samples or timestep";return false;}
    if(state.initialized&&!valid_state(c,state,why))return false;
    State next=state;if(!state.initialized){next.position=c.origin;next.rotation=c.base_rotation;next.velocity={};next.residual={};}
    const Point previous=next.position;const double previous_x=double(previous.x)+next.residual.x,previous_y=double(previous.y)+next.residual.y,previous_z=double(previous.z)+next.residual.z;
    double vx=0,vz=0;
    if(c.drifting){const double magnitude=std::hypot(double(wind.x),double(wind.z)),scale=magnitude>1?1/magnitude:1;
        vx=double(wind.x)*scale*c.wind_speed+c.current_x;vz=double(wind.z)*scale*c.wind_speed+c.current_z;
        const double speed=std::hypot(vx,vz);if(speed>c.max_speed){const double ratio=c.max_speed/speed;vx*=ratio;vz*=ratio;}
    }
    double px=previous_x+vx*dt,pz=previous_z+vz*dt;
    if(c.anchored){const double ax=px-c.anchor.x,az=pz-c.anchor.z,distance=std::hypot(ax,az);if(distance>c.anchor_radius){const double scale=c.anchor_radius/distance;px=c.anchor.x+ax*scale;pz=c.anchor.z+az*scale;}}
    next.position.x=float(px);next.position.z=float(pz);
    if(c.anchored&&!inside(next.position,c)){
        const double ax=px-c.anchor.x,az=pz-c.anchor.z,distance=std::hypot(ax,az);
        const double ulp=std::max(std::abs(double(std::nextafter(next.position.x,INFINITY))-next.position.x),std::abs(double(std::nextafter(next.position.z,INFINITY))-next.position.z));
        const double inward=std::max(0.,double(c.anchor_radius)-2*ulp),scale=distance?inward/distance:0;
        next.position.x=float(c.anchor.x+ax*scale);next.position.z=float(c.anchor.z+az*scale);
        if(!inside(next.position,c)){next.position.x=c.anchor.x;next.position.z=c.anchor.z;}
    }
    next.residual.x=float(px-next.position.x);next.residual.z=float(pz-next.position.z);
    if(std::abs(double(next.residual.x))>residual_limit(next.position.x))next.residual.x=0;
    if(std::abs(double(next.residual.z))>residual_limit(next.position.z))next.residual.z=0;
    double sx=(double(s.right)-s.left)/(2*s.radius),sz=(double(s.back)-s.front)/(2*s.radius);
    const double magnitude=std::hypot(sx,sz),limit=std::tan(double(c.max_tilt)*Rad);if(magnitude>limit){const double scale=limit/magnitude;sx*=scale;sz*=scale;}
    const double yaw=double(c.base_rotation.y)*Rad,cy=std::cos(yaw),sy=std::sin(yaw),local_x=cy*sx-sy*sz,local_z=sy*sx+cy*sz;
    // Native up vector for RY*RX*RZ is yawRotate(-sin(roll),
    // cos(pitch)*cos(roll),sin(pitch)*cos(roll)). Match the sampled normal.
    const Point target_rotation{angle(c.base_rotation.x-std::atan(local_z)*Deg),angle(c.base_rotation.y),angle(c.base_rotation.z+std::atan2(local_x,std::sqrt(1+local_z*local_z))*Deg)};
    const double target_y=double(s.center)+c.origin.y-c.draft,alpha=!state.initialized||c.damping==0?1:-std::expm1(-double(dt)/c.damping);
    const double py=previous_y+(target_y-previous_y)*alpha;next.position.y=float(py);next.residual.y=float(py-next.position.y);
    next.rotation={damp_angle(next.rotation.x,target_rotation.x,alpha),target_rotation.y,damp_angle(next.rotation.z,target_rotation.z,alpha)};
    next.velocity={float((px-previous_x)/dt),state.initialized?float((py-previous_y)/dt):0,float((pz-previous_z)/dt)};
    if(!finite(next.position)||!finite(next.rotation)||!finite(next.velocity)||!finite(next.residual)){why="floating result exceeds numeric range";return false;}
    next.initialized=true;if(!valid_state(c,next,why))return false;state=next;why.clear();return true;
}
}
