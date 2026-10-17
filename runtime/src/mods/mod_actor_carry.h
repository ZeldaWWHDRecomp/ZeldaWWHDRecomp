// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cmath>
#include <cstdint>
namespace mod_actor::carry {
enum class Phase:uint32_t {Ground,Held,Flight,Broken,Quarantined};
struct State {uint32_t version=1,phase=0,pid=0,link_pid=0,frames=0;float vx=0,vy=0,vz=0;};
static_assert(sizeof(State)==32);
inline bool valid(const State&s,uint32_t pid){
 if(s.version!=1||s.pid!=pid||!pid||pid==0xFFFFFFFFu||s.phase>4||s.frames>300||!std::isfinite(s.vx)||!std::isfinite(s.vy)||!std::isfinite(s.vz)||std::fabs(s.vx)>50||std::fabs(s.vy)>50||std::fabs(s.vz)>50)return false;
 if(s.phase==1)return s.link_pid&&s.link_pid!=0xFFFFFFFFu&&s.frames==0&&s.vx==0&&s.vy==0&&s.vz==0;
 if(s.link_pid)return false;
 return s.phase==2||(s.vx==0&&s.vy==0&&s.vz==0&&s.frames<=(s.phase==3?1u:0u));
}
inline bool hold(State&s,uint32_t pid,uint32_t link){if(!valid(s,pid)||s.phase!=0||!link||link==0xFFFFFFFFu)return false;s.phase=1;s.link_pid=link;s.frames=0;s.vx=s.vy=s.vz=0;return true;}
// Native throw supplies speedY34/speedF19. All units here are per30Hz logic tick.
inline bool release(State&s,bool thrown,float forward,float up,int16_t yaw){if(s.phase!=1||!valid(s,s.pid)||!std::isfinite(forward)||!std::isfinite(up)||forward<0||forward>50||std::fabs(up)>50)return false;s.link_pid=0;s.frames=0;s.vx=s.vy=s.vz=0;if(thrown){const float a=float(yaw)*3.14159265358979323846f/32768.f;s.phase=2;s.vx=std::sin(a)*forward;s.vz=std::cos(a)*forward;s.vy=up;}else s.phase=0;return true;}
inline bool advance(State&s){if(!valid(s,s.pid)||s.phase!=2||s.frames>=300)return false;++s.frames;s.vy=std::fmax(-50.f,s.vy-2.f);return true;}
inline bool impact(State&s,bool breakable){if(!valid(s,s.pid)||s.phase!=2)return false;s.phase=breakable?3:0;s.vx=s.vy=s.vz=0;s.frames=0;return true;}
inline void quarantine(State&s){s.phase=4;s.link_pid=0;s.frames=0;s.vx=s.vy=s.vz=0;}
}
