#include "mods/mod_actor_motion.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
using namespace mod_actor::motion;
static Point accept(Point,Point proposal){return proposal;}
static bool near(float a,float b,float e=1e-4f){return std::fabs(a-b)<e;}
int main(){
    LookState look;LookLimits limits;assert(look_at({0,0,0},{100,100,0},0,.1f,limits,look));assert(near(look.head_yaw,.3f)&&near(look.head_pitch,.3f));for(int n=0;n<10;++n)assert(look_at({0,0,0},{100,100,0},0,.1f,limits,look));assert(near(look.head_yaw,limits.yaw)&&near(look.head_pitch,limits.pitch));
    look={};assert(look_at({0,0,0},{-1,0,-100},float(Pi-.01),.1f,limits,look));assert(std::fabs(look.head_yaw)<.03);const auto before=look;assert(look_at({0,0,0},{0,0,0},0,.1f,limits,look)&&look.head_yaw==before.head_yaw);assert(!look_at({0,0,0},{0,0,1},0,1,limits,look));
    look={1,.5,0};assert(look_at({0,0,0},{0,0,1000},0,.1f,limits,look)&&near(look.head_yaw,.7f)&&near(look.head_pitch,.2f));
    limits.upper_body=true;look={};for(int n=0;n<30;++n)assert(look_at({0,0,0},{100,0,100},0,1.f/30,limits,look));assert(near(look.upper_yaw,float(Pi/4)*limits.upper_fraction)&&near(look.head_yaw+look.upper_yaw,float(Pi/4)));
    LookState a,b;limits.upper_body=false;for(int n=0;n<30;++n)assert(look_at({0,0,0},{100,0,100},0,1.f/30,limits,a));for(int n=0;n<60;++n)assert(look_at({0,0,0},{100,0,100},0,1.f/60,limits,b));assert(near(a.head_yaw,b.head_yaw));
    Blocker link{{50,0,0},10,100,true};auto hit=sweep({0,0,0},{100,0,0},10,100,link);assert(hit.valid&&hit.blocked&&near(hit.position.x,29.99f,.001f));assert(!sweep({0,200,0},{100,200,0},10,100,link).blocked);assert(sweep({40,0,0},{45,0,0},10,100,link).blocked);assert(!sweep({40,0,0},{30,0,0},10,100,link).blocked);
    Path path;path.count=3;path.points[0]={0,0,0};path.points[1]={10,0,0};path.points[2]={10,0,10};path.speed=100;path.arrival=.01f;PathState state;auto move=advance(path,state,{0,0,0},.15f,1,10,{},accept);assert(move.status==Status::Moving&&near(move.position.x,10)&&near(move.position.z,5)&&state.next==2&&move.has_direction);move=advance(path,state,move.position,.1f,1,10,{},accept);assert(move.status==Status::Finished&&state.finished&&near(move.position.z,10));
    state={};move=advance(path,state,{0,0,0},.1f,1,10,{},[](Point from,Point){return from;});assert(move.status==Status::Blocked&&state.next==1&&move.position.x==0);move=advance(path,state,move.position,.1f,1,10,{},accept);assert(near(move.position.x,10));
    Path straight;straight.count=2;straight.points[0]={0,0,0};straight.points[1]={100,0,0};straight.speed=1000;straight.arrival=.01f;state={};move=advance(straight,state,{0,0,0},.1f,10,100,link,accept);assert(move.status==Status::Blocked&&near(move.position.x,29.99f,.001f)&&state.next==1);move=advance(straight,state,move.position,.1f,10,100,{},accept);assert(move.status==Status::Finished&&state.finished);
    PathState s30,s60;Point p30{},p60{};straight.speed=60;for(int n=0;n<30;++n)p30=advance(straight,s30,p30,1.f/30,1,10,{},accept).position;for(int n=0;n<60;++n)p60=advance(straight,s60,p60,1.f/60,1,10,{},accept).position;assert(near(p30.x,p60.x,.001f));
    path.loop=true;state={};move=advance(path,state,{0,0,0},.25f,1,10,{},accept);assert(!state.finished&&move.status==Status::Moving);Path tiny; tiny.count=2;tiny.points[0]={0,0,0};tiny.points[1]={1,0,0};tiny.loop=true;tiny.arrival=.01f;tiny.speed=1500;state={};move=advance(tiny,state,{},.25f,1,10,{},accept);assert(move.status==Status::SubstepLimit&&!state.finished&&move.traveled<=375);
    auto bad=path;bad.points[1]=bad.points[0];assert(!valid(bad));bad=path;bad.speed=1501;assert(!valid(bad));state={};assert(advance(path,state,{},std::numeric_limits<float>::quiet_NaN(),1,10,{},accept).status==Status::Invalid);
    const auto snapshot=state;PathState restored=snapshot;assert(restored.next==state.next&&restored.finished==state.finished);
    state={};assert(advance(path,state,{},0,1,10,{},accept).status==Status::Moving&&state.next==0);
    look={.2f,.1f,.3f};const auto zero_pose=look;assert(look_at({0,0,0},{1,0,1},0,0,limits,look)&&look.head_yaw==zero_pose.head_yaw&&look.upper_yaw==zero_pose.upper_yaw);
    PathState finished{path.count-1,true};const auto idle=advance(path,finished,path.points[path.count-1],.1f,1,10,{},accept);assert(idle.status==Status::Finished&&!idle.has_direction);
    // back and forth: it turns at the last point, walks back to the first and out again
    {Path line;line.count=3;line.points[0]={0,0,0};line.points[1]={10,0,0};line.points[2]={20,0,0};line.speed=10;line.arrival=.01f;line.bounce=true;
     PathState st;Point at{0,0,0};std::vector<unsigned> targets;for(int n=0;n<120;++n){const auto m=advance(line,st,at,.1f,1,10,{},accept);assert(m.status==Status::Moving&&!st.finished);at=m.position;if(targets.empty()||targets.back()!=st.next)targets.push_back(st.next);}
     assert((targets==std::vector<unsigned>{1,2,1,0,1,2,1,0}||targets.size()>=6));assert(targets[0]==1&&targets[1]==2&&targets[2]==1&&targets[3]==0&&targets[4]==1);
     assert(at.x>=0&&at.x<=20);}
    // strolling pauses: 200..300 steps walking, 60..90 standing, again and again, from the saved seed
    {Pauses p{false,0,12345};unsigned walked=0,stood=0,switches=0;bool last=true;for(int n=0;n<2000;++n){const bool w=pause_step(p);if(w)++walked;else ++stood;if(w!=last)++switches;last=w;}
     assert(switches>=10&&walked>stood*2&&stood>0);
     Pauses q{false,0,777};unsigned run=0;while(pause_step(q))++run;assert(run>=199&&run<=300);unsigned rest=1;while(!pause_step(q))++rest;assert(rest>=60&&rest<=91);
     Pauses r{false,0,777};Pauses copy=r;for(int n=0;n<500;++n)assert(pause_step(r)==pause_step(copy));}
    std::cout<<"mod actor motion tests passed\n";
}
