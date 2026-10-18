// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace story_native_camera_image {namespace {
using Bytes=std::vector<uint8_t>;
constexpr uint32_t None=0xFFFFFFFFu,Event=0x40,Staff=0xF0,Cut=0x230,Property=0x460,Float=0xB60,Int=0xBD0,String=0xC00;
constexpr std::array<uint32_t,7> Offsets={Event,Staff,Cut,Property,Float,Int,String},Counts={1,4,7,28,25,11,32},Strides={176,80,80,64,4,4,1};
uint32_t get(const Bytes&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
float read_float(const Bytes&b,size_t p){uint32_t v=get(b,p);float f;std::memcpy(&f,&v,4);return f;}
void put(Bytes&b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-8*i));}
void put_float(Bytes&b,size_t p,float f){uint32_t v;std::memcpy(&v,&f,4);put(b,p,v);}
void name(Bytes&b,size_t p,const char*s){std::memcpy(b.data()+p,s,std::strlen(s));}
bool fail(std::string&w,const char*m){w=m;return false;}
bool point(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<=350000&&std::fabs(p.y)<=350000&&std::fabs(p.z)<=350000;}
bool config(const Program&p,std::string&w){
 if(!p.hold_timer||!p.move_timer||p.hold_timer>9000||p.move_timer>9000||uint64_t(p.hold_timer)+p.move_timer>9000)return fail(w,"Native camera timers must be positive with total<=9000");
 if(!point(p.start_eye)||!point(p.start_center)||!point(p.end_eye)||!point(p.end_center)||!std::isfinite(p.fovy)||p.fovy<5||p.fovy>120)return fail(w,"Invalid finite camera points/FOV");
 // The native globe/direction conversion must never receive a zero vector,
 // including halfway between distinct endpoints.
 const double a[]={double(p.start_eye.x)-p.start_center.x,double(p.start_eye.y)-p.start_center.y,double(p.start_eye.z)-p.start_center.z};
 const double z[]={double(p.end_eye.x)-p.end_center.x,double(p.end_eye.y)-p.end_center.y,double(p.end_eye.z)-p.end_center.z};
 double dot=0,length=0;for(unsigned i=0;i<3;++i){double d=z[i]-a[i];dot+=a[i]*d;length+=d*d;}
 double t=length?std::clamp(-dot/length,0.0,1.0):0,sum=0;for(unsigned i=0;i<3;++i){double v=a[i]+t*(z[i]-a[i]);sum+=v*v;}
 if(sum<1)return fail(w,"Camera eye/center must stay at least1unit apart throughout move");return true;
}
void vector(Bytes&b,unsigned index,Point p){put_float(b,Float+index*4,p.x);put_float(b,Float+(index+1)*4,p.y);put_float(b,Float+(index+2)*4,p.z);}
Bytes build(const Program&p){
 Bytes b(kImageBytes);
 for(unsigned i=0;i<7;++i){put(b,i*8,Offsets[i]);put(b,i*8+4,Counts[i]);}
 name(b,Event,"SDK_CAMERA");for(unsigned i=0;i<20;++i)put(b,Event+0x2C+i*4,i<4?i:None);
 put(b,Event+0x7C,4);put(b,Event+0x88,5);put(b,Event+0x8C,None);put(b,Event+0x90,None);
 const char*staff_names[]={"SDK_ACTOR_A","SDK_ACTOR_B","CAMERA","TIMEKEEPER"};const uint32_t kinds[]={0,0,2,4};
 for(unsigned i=0;i<4;++i){size_t at=Staff+i*80;name(b,at,staff_names[i]);put(b,at+0x24,i);put(b,at+0x2C,kinds[i]);put(b,at+0x30,i);put(b,at+0x38,None);put(b,at+0x3C,None);}
 const char*cut_names[]={"WAIT","WAIT","FIXEDFRM","COUNTDOWN","WAIT","WAIT","UNITRANS"};
 const uint32_t first_property[]={0,1,2,11,None,None,12},next[]={None,None,6,4,5,None,None};
 for(unsigned i=0;i<7;++i){size_t at=Cut+i*80;name(b,at,cut_names[i]);put(b,at+0x20,i);for(unsigned j=0;j<3;++j)put(b,at+0x28+j*4,None);put(b,at+0x34,i);put(b,at+0x38,first_property[i]);put(b,at+0x3C,next[i]);}
 for(unsigned j=0;j<3;++j)put(b,Cut+4*80+0x28+j*4,std::array<uint32_t,3>{0,1,3}[j]);put(b,Cut+5*80+0x28,6);put(b,Cut+5*80+0x2C,4);
 struct Prop{const char*name;uint32_t type,index,count,next;};
 const std::array<Prop,28> props={{{"Timer",3,0,1,None},{"Timer",3,1,1,None},
 {"Eye",1,0,1,3},{"Center",1,3,1,4},{"Fovy",0,6,1,5},{"Bank",0,7,1,6},{"Timer",3,2,1,7},{"RelUseMask",4,0,8,8},{"Trim",4,8,8,9},{"BGCheck",3,3,1,10},{"MoveBGCheck",3,4,1,None},
 {"Timer",3,5,1,None},
 {"StartEye",1,8,1,13},{"StartCenter",1,11,1,14},{"Eye",1,14,1,15},{"Center",1,17,1,16},{"StartFovy",0,20,1,17},{"Fovy",0,21,1,18},{"StartBank",0,22,1,19},{"Bank",0,23,1,20},{"Cushion",0,24,1,21},{"Timer",3,6,1,22},{"TransType",3,7,1,23},{"BSpCurve",3,8,1,24},{"BGCheck",3,9,1,25},{"MoveBGCheck",3,10,1,26},{"RelUseMask",4,16,8,27},{"Trim",4,24,8,None}}};
 for(unsigned i=0;i<props.size();++i){const auto&v=props[i];size_t at=Property+i*64;name(b,at,v.name);put(b,at+0x20,i);put(b,at+0x24,v.type);put(b,at+0x28,v.index);put(b,at+0x2C,v.count);put(b,at+0x30,v.next);}
 vector(b,0,p.start_eye);vector(b,3,p.start_center);put_float(b,Float+6*4,p.fovy);
 vector(b,8,p.start_eye);vector(b,11,p.start_center);vector(b,14,p.end_eye);vector(b,17,p.end_center);put_float(b,Float+20*4,p.fovy);put_float(b,Float+21*4,p.fovy);put_float(b,Float+24*4,1);
 const uint32_t total=p.hold_timer+p.move_timer;put(b,Int,total);put(b,Int+4,total);put(b,Int+8,p.hold_timer);put(b,Int+20,total);put(b,Int+24,p.move_timer);
 name(b,String,"--");name(b,String+8,"CINESCO");name(b,String+16,"----");name(b,String+24,"CINESCO");return b;
}
}
bool make(const Program&p,Bytes&out,std::string&why){out.clear();why.clear();if(!config(p,why))return false;out=build(p);return true;}
bool validate(const Bytes&b,Program&out,std::string&why){
 out={};why.clear();if(b.size()!=kImageBytes)return fail(why,"Wrong canonical camera image extent");
 size_t end=0x38;for(unsigned i=0;i<7;++i){uint32_t off=get(b,i*8),count=get(b,i*8+4);if(off!=Offsets[i]||count!=Counts[i]||off%16||off<end||count>(b.size()-off)/Strides[i])return fail(why,"Camera section layout/count/span");end=off+size_t(count)*Strides[i];}
 Program p;p.hold_timer=get(b,Int+8);p.move_timer=get(b,Int+24);p.start_eye={read_float(b,Float),read_float(b,Float+4),read_float(b,Float+8)};p.start_center={read_float(b,Float+12),read_float(b,Float+16),read_float(b,Float+20)};p.end_eye={read_float(b,Float+14*4),read_float(b,Float+15*4),read_float(b,Float+16*4)};p.end_center={read_float(b,Float+17*4),read_float(b,Float+18*4),read_float(b,Float+19*4)};p.fovy=read_float(b,Float+6*4);
 if(!config(p,why))return false;
 // Exact pristine grammar includes reserved bytes, all typed pools, chains,
 // flags, actor aliases, duplicate start values and fixed camera options.
 if(b!=build(p))return fail(why,"Noncanonical camera action/property/reference/reserved field");out=p;return true;
}
}
