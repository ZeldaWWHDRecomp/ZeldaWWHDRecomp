// SPDX-License-Identifier: MPL-2.0
#include "story_particle_format.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>
namespace story_particle_format {namespace {
using Bytes=std::vector<uint8_t>;
constexpr uint32_t Bem=0x40,Bsp=0xE0,Tdb=0x160,Tex=0x180,FileBytes=0x1A0;
constexpr uint32_t Dict=0x80,Ftex=0x100,Name=0x1C4,Image=0x2000;
void w16(Bytes&b,size_t p,uint32_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
void w32(Bytes&b,size_t p,uint32_t v){b[p]=uint8_t(v>>24);b[p+1]=uint8_t(v>>16);b[p+2]=uint8_t(v>>8);b[p+3]=uint8_t(v);}
void wf(Bytes&b,size_t p,float v){uint32_t n;std::memcpy(&n,&v,4);w32(b,p,n);}
uint32_t u32(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint32_t u16(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<8|b[p+1];}
float fl(std::span<const uint8_t>b,size_t p){uint32_t n=u32(b,p);float v;std::memcpy(&v,&n,4);return v;}
bool fail(std::string&w,const char*m){w=m;return false;}
bool name_ok(const std::string&n){return !n.empty()&&n.size()<=19&&std::all_of(n.begin(),n.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';});}
uint32_t split(const std::string&n){uint32_t bit=0;for(size_t i=0;i<n.size();++i)for(unsigned k=0;k<8;++k)if(uint8_t(n[i])&(1u<<k))bit=uint32_t(i*8+k);return bit;}
void tag(Bytes&b,size_t p,const char*t){std::memcpy(b.data()+p,t,4);}
void block(Bytes&b,uint32_t p,const char*t,uint32_t size){tag(b,p,t);w32(b,p+4,size);}
// Canonical reserved bytes prevent unsupported native branches. Validation
// identifies every nonzero/variable byte explicitly rather than trusting size.
bool reserved(std::span<const uint8_t>b,std::initializer_list<std::pair<size_t,size_t>> fields){
 for(size_t p=0;p<b.size();++p){bool used=false;for(auto [a,n]:fields)if(p>=a&&p<a+n){used=true;break;}if(!used&&b[p])return false;}return true;
}
}
bool write(const Sprite&s,Bundle&out,std::string&why){
 out={};why.clear();
 if(!name_ok(s.name))return fail(why,"Sprite name must be1..19 lowercase ASCII identifier bytes");
 if(!s.width||!s.height||s.width>256||s.height>256||s.width%64||s.height%8||s.rgba.size()!=uint64_t(s.width)*s.height*4)return fail(why,"Sprite requires exact RGBA8,64x8 multiples, at most256x256");
 if(!s.lifetime||s.lifetime>90||!std::isfinite(s.size)||s.size<0.1f||s.size>100)return fail(why,"Finite lifetime1..90 and size0.1..100 required");
 Bytes j(FileBytes),c(Image+s.rgba.size());std::memcpy(j.data(),"JPAC1-00",8);w16(j,8,1);w16(j,10,1);
 w32(j,0x20+12,3);j[0x20+0x16]=1; // resource0, three blocks, one TDB index
 block(j,Bem,"BEM1",0xA0);block(j,Bsp,"BSP1",0x80);block(j,Tdb,"TDB1",0x20);
 const uint32_t d=Bem+12,p=Bsp+12;wf(j,d+4,1);w16(j,d+14,1);wf(j,d+16,1);w16(j,d+26,1);w16(j,d+30,s.lifetime);wf(j,d+60,1);wf(j,d+68,1);for(unsigned i=0;i<3;++i)wf(j,d+84+i*4,1);
 // EXPERIMENTAL fixed selectors, never native-admitted by this module.
 // Native table decoded fields: blend1/srcfactorindex6/dstfactorindex7;
 // compareindex7, depth bits0/1..3 set. Shape0 still needs dispatch audit.
 wf(j,p+8,s.size);wf(j,p+12,s.size);w16(j,p+18,0x1D9);j[p+20]=7;j[p+23]=7;w32(j,p+32,s.tint);w32(j,p+36,0x000000FF);
 std::memcpy(j.data()+Tex+12,s.name.c_str(),s.name.size()+1);
 tag(c,0,"FRES");w32(c,4,0x03040000);w16(c,8,0xFEFF);w16(c,10,0x10);w32(c,12,uint32_t(c.size()));w32(c,16,0x2000);w32(c,0x24,Dict-0x24);w16(c,0x52,1);
 w32(c,Dict,40);w32(c,Dict+4,1);w32(c,Dict+8,0xFFFFFFFF);w16(c,Dict+12,1);
 const uint32_t leaf=Dict+24;w32(c,leaf,split(s.name));w16(c,leaf+6,1);w32(c,leaf+8,Name-(leaf+8));w32(c,leaf+12,Ftex-(leaf+12));
 w32(c,Name-4,uint32_t(s.name.size()));std::memcpy(c.data()+Name,s.name.c_str(),s.name.size()+1);
 tag(c,Ftex,"FTEX");const uint32_t surf=Ftex+4;w32(c,surf,1);w32(c,surf+4,s.width);w32(c,surf+8,s.height);w32(c,surf+12,1);w32(c,surf+16,1);w32(c,surf+20,0x41A);w32(c,surf+28,1);w32(c,surf+32,uint32_t(s.rgba.size()));w32(c,surf+48,1);w32(c,surf+56,0x2000);w32(c,surf+60,s.width);w32(c,surf+120,1);w32(c,surf+128,1);w32(c,surf+132,0x00010203);w32(c,Ftex+0xB0,Image-(Ftex+0xB0));
 std::copy(s.rgba.begin(),s.rgba.end(),c.begin()+Image);out={std::move(j),std::move(c)};return true;
}
bool validate(std::span<const uint8_t>j,std::span<const uint8_t>c,std::string&why){
 why.clear();if(j.size()!=FileBytes||c.size()<Image||c.size()>Image+256*256*4)return fail(why,"Candidate size bounds");
 if(std::memcmp(j.data(),"JPAC1-00",8)||u16(j,8)!=1||u16(j,10)!=1||!reserved(j.first(32),{{0,12}}))return fail(why,"JPAC header/count/reserved bytes");
 auto h=j.subspan(32,32);if(u32(h,12)!=3||h[22]!=1||!reserved(h,{{12,4},{22,1}}))return fail(why,"Resource header must be privateID0/threeblocks/oneTDB");
 for(auto [at,size,kind]:{std::tuple<size_t,size_t,const char*>{Bem,160,"BEM1"},{Bsp,128,"BSP1"},{Tdb,32,"TDB1"}})if(std::memcmp(j.data()+at,kind,4)||u32(j,at+4)!=size||u32(j,at+8))return fail(why,"Block type/size/prefix");
 auto d=j.subspan(Bem+12,148);if(fl(d,4)!=1||u16(d,14)!=1||fl(d,16)!=1||u16(d,26)!=1||!u16(d,30)||u16(d,30)>90||fl(d,60)!=1||fl(d,68)!=1||fl(d,84)!=1||fl(d,88)!=1||fl(d,92)!=1||!reserved(d,{{4,4},{14,2},{16,4},{26,2},{30,2},{60,4},{68,4},{84,12}}))return fail(why,"Finite canonical BEM1 fields");
 auto p=j.subspan(Bsp+12,116);const float size=fl(p,8);if(!std::isfinite(size)||size<0.1f||size>100||fl(p,12)!=size||u16(p,18)!=0x1D9||p[20]!=7||p[23]!=7||u32(p,36)!=0xFF||!reserved(p,{{8,8},{18,3},{23,1},{32,8}}))return fail(why,"Experimental fixed BSP1 profile");
 if(!reserved(j.subspan(Tdb+12,20),{}))return fail(why,"Only texture index0 is supported");
 auto t=j.subspan(Tex,32);size_t length=0;while(length<20&&t[12+length])++length;if(length==20)return fail(why,"Texture name unterminated");std::string name(reinterpret_cast<const char*>(t.data()+12),length);if(!name_ok(name)||!reserved(t,{{12,length}}))return fail(why,"Texture name/reserved bytes");
 if(std::memcmp(c.data(),"FRES",4)||u32(c,4)!=0x03040000||u16(c,8)!=0xFEFF||u16(c,10)!=0x10||u32(c,12)!=c.size()||u32(c,16)!=0x2000||u32(c,0x24)+0x24!=Dict||u16(c,0x52)!=1||!reserved(c.first(Dict),{{0,20},{0x24,4},{0x52,2}}))return fail(why,"Private FRES context header");
 auto dict=c.subspan(Dict,40);if(u32(dict,0)!=40||u32(dict,4)!=1||u32(dict,8)!=0xFFFFFFFF||u16(dict,12)!=1||u16(dict,14)||u32(dict,16)||u32(dict,20)||u32(dict,24)!=split(name)||u16(dict,28)||u16(dict,30)!=1||u32(dict,32)+Dict+32!=Name||int64_t(int32_t(u32(dict,36)))+Dict+36!=Ftex)return fail(why,"Single-entry native Patricia dictionary");
 if(u32(c,Name-4)!=name.size()||std::memcmp(c.data()+Name,name.c_str(),name.size()+1))return fail(why,"JPAC/context names differ");
 auto f=c.subspan(Ftex,0xC0);const uint32_t w=u32(f,8),height=u32(f,12);const uint64_t pixels=uint64_t(w)*height*4;
 if(std::memcmp(f.data(),"FTEX",4)||!w||!height||w>256||height>256||w%64||height%8||pixels!=c.size()-Image||u32(f,4)!=1||u32(f,16)!=1||u32(f,20)!=1||u32(f,24)!=0x41A||u32(f,32)!=1||u32(f,36)!=pixels||u32(f,52)!=1||u32(f,60)!=0x2000||u32(f,64)!=w||u32(f,124)!=1||u32(f,132)!=1||u32(f,136)!=0x00010203||uint64_t(u32(f,0xB0))+Ftex+0xB0!=Image||!reserved(f,{{0,28},{32,8},{52,4},{60,8},{124,4},{132,8},{0xB0,4}}))return fail(why,"Original RGBA8 FTEX bounds/relative image");
 if(!reserved(c.subspan(Dict+40,Ftex-(Dict+40)),{})||!reserved(c.subspan(Ftex+0xC0,Image-(Ftex+0xC0)),{{Name-4-(Ftex+0xC0),4+name.size()+1}}))return fail(why,"Context padding/noncanonical payload");
 return true;
}
}
