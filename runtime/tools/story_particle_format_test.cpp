// SPDX-License-Identifier: MPL-2.0
#include "mods/story_particle_format.h"
#include <cassert>
#include <cstring>
#include <limits>
namespace P=story_particle_format;
uint32_t be(const std::vector<uint8_t>&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
int main(){
 static_assert(!P::kNativeRegistrationReady);
 P::Sprite s;s.name="original-spark";s.width=64;s.height=8;s.rgba.resize(64*8*4);for(size_t i=0;i<s.rgba.size();++i)s.rgba[i]=uint8_t(i*13);s.tint=0x12345678;s.lifetime=17;s.size=12.5f;
 P::Bundle b,again;std::string why;assert(P::write(s,b,why)&&why.empty());assert(P::validate(b.jpac,b.texture_context,why));assert(P::write(s,again,why)&&b.jpac==again.jpac&&b.texture_context==again.texture_context);
 // Independent native-reader projection: HD texture records are32B/name+C;
 // context+24 -> native dictionary leaf+C -> FTEX; FTEX+B0 -> pixels.
 assert(b.jpac.size()==416&&!std::memcmp(b.jpac.data(),"JPAC1-00",8));assert(be(b.jpac,0x2C)==3);assert(b.jpac[0x36]==1);assert(!std::strcmp(reinterpret_cast<const char*>(b.jpac.data()+0x18C),s.name.c_str()));
 uint32_t dict=0x24+be(b.texture_context,0x24);uint32_t leaf=dict+24;uint32_t ftex=uint32_t(int64_t(leaf+12)+int32_t(be(b.texture_context,leaf+12)));uint32_t image=ftex+0xB0+be(b.texture_context,ftex+0xB0);
 assert(dict==0x80&&ftex==0x100&&image==0x2000);
 // Native-style descending-bit lookup terminates on the self-linked leaf;
 // the length-prefixed key is resolved independently from FTEX payload.
 uint32_t parent=0,cur=uint32_t(b.texture_context[dict+12])<<8|b.texture_context[dict+13];unsigned visits=0;
 while(be(b.texture_context,dict+8+parent*16)>be(b.texture_context,dict+8+cur*16)){
  assert(++visits<=2);parent=cur;uint32_t bit=be(b.texture_context,dict+8+cur*16);assert(bit<s.name.size()*8);bool side=(uint8_t(s.name[bit/8])>>(bit%8))&1;size_t child=dict+8+cur*16+4+(side?2:0);cur=uint32_t(b.texture_context[child])<<8|b.texture_context[child+1];
 }
 assert(cur==1);uint32_t keyfield=dict+8+cur*16+8,key=keyfield+be(b.texture_context,keyfield);assert(be(b.texture_context,key-4)==s.name.size()&&!std::strcmp(reinterpret_cast<const char*>(b.texture_context.data()+key),s.name.c_str()));assert(!std::memcmp(b.texture_context.data()+ftex,"FTEX",4));assert(be(b.texture_context,ftex+8)==64&&be(b.texture_context,ftex+12)==8);assert(std::equal(s.rgba.begin(),s.rgba.end(),b.texture_context.begin()+image));
 // Corruption must fail before any future native loader is invoked.
 for(size_t offset:{size_t(0),size_t(9),size_t(0x2F),size_t(0x36),size_t(0x44),size_t(0x4C+26),size_t(0x4C+30),size_t(0xEC),size_t(0xEC+18),size_t(0x16C),size_t(0x18C),size_t(0x19F)}){auto bad=b;bad.jpac[offset]^=0x80;assert(!P::validate(bad.jpac,bad.texture_context,why)&&!why.empty());}
 for(size_t offset:{size_t(0x24),size_t(0x88),size_t(0x8C),size_t(0x9C),size_t(0xA0),size_t(0xA4),size_t(0x108),size_t(0x124),size_t(0x1B0),size_t(0x1B4),size_t(0x1C0),size_t(0x1C4),size_t(0x1FFF)}){auto bad=b;bad.texture_context[offset]^=0x80;assert(!P::validate(bad.jpac,bad.texture_context,why)&&!why.empty());}
 auto short_j=b.jpac;short_j.pop_back();assert(!P::validate(short_j,b.texture_context,why));auto short_c=b.texture_context;short_c.pop_back();assert(!P::validate(b.jpac,short_c,why));auto long_c=b.texture_context;long_c.push_back(0);assert(!P::validate(b.jpac,long_c,why));
 for(unsigned k=0;k<9;++k){auto bad=s;switch(k){case 0:bad.name="../bad";break;case 1:bad.name=std::string(20,'a');break;case 2:bad.width=63;break;case 3:bad.height=7;break;case 4:bad.rgba.pop_back();break;case 5:bad.lifetime=0;break;case 6:bad.lifetime=91;break;case 7:bad.size=std::numeric_limits<float>::quiet_NaN();break;case 8:bad.size=101;break;}P::Bundle out=b;assert(!P::write(bad,out,why)&&out.jpac.empty()&&out.texture_context.empty());}
 // Changing original pixels/tint/duration deterministically changes only owned
 // serialized data, with no reference template or native asset dependency.
 auto changed=s;changed.rgba[0]^=1;changed.tint=0xFFEEDDCC;changed.lifetime=90;assert(P::write(changed,again,why)&&P::validate(again.jpac,again.texture_context,why));assert(again.jpac!=b.jpac&&again.texture_context!=b.texture_context);
}
