// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_setinfo_candidate.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <string>
#include <cstdint>
#include <span>
#include <utility>
using namespace story_native_setinfo_candidate;
namespace {
constexpr uint32_t Ctrl=0x50000000,Args=0x52000000,Mgr=0x53000000;
constexpr uint32_t InfoNode=0x54000000,Info=0x55000000,ResourceNode=0x56000000;
constexpr uint32_t Wrapper=0x57000000,HolderNode=0x58000000,Holder=0x59000000;
constexpr uint32_t Res=0x5a000000,Sarc=0x60000000,Bfres=Sarc+0x80;
constexpr uint32_t RequestHeap=0x5b000000,DataHeap=0x5c000000;
constexpr Layout L{0x100e222c,0x100e1dc0,0x100e1960,0x100e25d0,
                   0x1015e184,0x100e1a84,0x100e1a9c,0x10145b38};
void w32(std::vector<uint8_t>&b,unsigned p,uint32_t v){
    b[p]=uint8_t(v>>24);b[p+1]=uint8_t(v>>16);b[p+2]=uint8_t(v>>8);b[p+3]=uint8_t(v);
}
void w16(std::vector<uint8_t>&b,unsigned p,uint16_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
void rel(std::vector<uint8_t>&b,unsigned p,unsigned target){w32(b,p,uint32_t(int32_t(target)-int32_t(p)));}
uint32_t crc(const char*s){uint32_t v=~0u;for(;*s;++s){v^=uint8_t(*s);for(int i=0;i<8;++i)v=(v>>1)^(v&1?0xedb88320u:0u);}return ~v;}
struct Region {uint32_t base;std::vector<uint8_t> bytes;};
struct Fixture {
 std::vector<Region> regions;
 std::vector<uint8_t>& at(uint32_t base){for(auto& r:regions)if(r.base==base)return r.bytes;std::abort();}
 Read read(){return [this](uint32_t a,std::span<uint8_t>dst){
     for(const auto&r:regions)if(a>=r.base&&uint64_t(a-r.base)+dst.size()<=r.bytes.size()){
         std::memcpy(dst.data(),r.bytes.data()+(a-r.base),dst.size());return true;}
     return false;};}
};
Fixture fixture(){
 Fixture f;
 for(auto [base,n]:{std::pair{Ctrl,0x1040},std::pair{Args,0x80},std::pair{Mgr,0x40},
                  std::pair{InfoNode,0x40},std::pair{Info,0xd0},std::pair{ResourceNode,0x40},
                  std::pair{Wrapper,0x40},std::pair{HolderNode,0x60},std::pair{Holder,0x60},
                  std::pair{Res,0x40},std::pair{RequestHeap,0x30},
                  std::pair{DataHeap,0x30},std::pair{Sarc,0x500}})f.regions.push_back({base,std::vector<uint8_t>(n)});
 auto& a=f.at(Args);w32(a,0,Args+8);std::memcpy(a.data()+8,"Stage",6);
 w32(a,0x20,Args+0x28);std::memcpy(a.data()+0x28,"Abesso",7);
 auto& c=f.at(Ctrl);w32(c,0x1028,InfoNode);
 auto& m=f.at(Mgr);w32(m,0,L.manager_vtable);w32(m,0x14,HolderNode);w32(m,0x20,1);
 auto& in=f.at(InfoNode);w32(in,0xc,crc("Abesso_Stage"));w32(in,0x10,L.info_node_vtable);
 w32(in,0x18,Info);w32(in,0x1c,Ctrl+0x1028);
 auto& info=f.at(Info);w32(info,0xc,L.info_vtable);
 w32(info,0x10,Info+0x1c);std::memcpy(info.data()+0x1c,"Abesso",7);
 w32(info,0x5c,Info+0x68);std::memcpy(info.data()+0x68,"Stage",6);
 w32(info,0xa8,Wrapper);w32(info,0xac,ResourceNode);
 w32(info,0xc0,RequestHeap);w32(info,0xc4,DataHeap);
 w32(f.at(RequestHeap),0xc,L.solid_heap_vtable);
 w32(f.at(DataHeap),0xc,L.solid_heap_vtable);w32(f.at(DataHeap),0x24,RequestHeap);
 auto& rn=f.at(ResourceNode);w32(rn,0xc,crc("event_list.dat"));
 w32(rn,0x10,L.resource_node_vtable);w32(rn,0x18,Bfres+0x240);w32(rn,0x1c,Info+0xac);
 w32(f.at(Wrapper),0xc,L.wrapper_vtable);w32(f.at(Wrapper),0x14,Bfres);
 auto& hn=f.at(HolderNode);w32(hn,0xc,HolderNode+0x20);w32(hn,0x18,Holder);
 w32(hn,0x1c,Mgr+0x14);std::memcpy(hn.data()+0x20,"Abesso_Stage",13);
 auto& holder=f.at(Holder);w32(holder,0xc,L.holder_vtable);w32(holder,0x10,Holder+0x18);
 std::memcpy(holder.data()+0x18,"Abesso_Stage",13);w32(holder,0x4c,Res);
 w32(holder,0x50,RequestHeap);
 auto& res=f.at(Res);w32(res,0x10,L.archive_resource_vtable);w32(res,0x14,Sarc);
 w32(res,0x18,0x480);w32(res,0x1c,0x500);
 auto& s=f.at(Sarc);std::memcpy(s.data(),"SARC",4);w16(s,4,0x14);w16(s,6,0xfeff);
 w32(s,8,0x480);w32(s,0xc,0x80);w16(s,0x10,0x100);
 std::memcpy(s.data()+0x14,"SFAT",4);w16(s,0x18,0xc);w16(s,0x1a,1);w32(s,0x1c,0x65);
 uint32_t hash=0;for(unsigned char ch:std::string("Stage.bfres"))hash=hash*0x65+ch;
 w32(s,0x20,hash);w32(s,0x24,0x01000000);w32(s,0x28,0);w32(s,0x2c,0x400);
 std::memcpy(s.data()+0x30,"SFNT",4);w16(s,0x34,8);std::memcpy(s.data()+0x38,"Stage.bfres",12);
 std::memcpy(s.data()+0x80,"FRES",4);s[0x88]=0xfe;s[0x89]=0xff;w32(s,0x8c,0x400);
 w16(s,0x80+0x66,1);rel(s,0x80+0x4c,0x80+0x100);w32(s,0x80+0x104,1);
 rel(s,0x80+0x120,0x80+0x1a0);rel(s,0x80+0x124,0x80+0x200);
 w32(s,0x80+0x19c,14);std::memcpy(s.data()+0x80+0x1a0,"event_list.dat",15);
 rel(s,0x80+0x200,0x80+0x240);w32(s,0x80+0x204,0x80);
 return f;
}
}
#ifndef STORY_NATIVE_SETINFO_CANDIDATE_FIXTURE_ONLY
int main(){
 auto f=fixture();Candidate c{};
 assert(verify(f.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 assert(c.info==Info&&c.archive_resource==Res&&c.outer_base==Bfres&&
        c.outer_bytes==0x400&&c.resource==Bfres+0x240&&c.resource_bytes==0x80);
 assert(c.request_heap==RequestHeap&&c.data_heap==DataHeap&&
        c.pack_heap==RequestHeap&&!c.archive_buffer_owned&&c.stage_name[0]=='A');
 Candidate live{};
 assert(verify_live(f.read(),L,Ctrl,Mgr,c,live)&&live==c);
 auto stale=c;stale.resource_bytes++;
 assert(!verify_live(f.read(),L,Ctrl,Mgr,stale,live));
 stale=c;stale.stage_name[0]='Q';
 assert(!verify_live(f.read(),L,Ctrl,Mgr,stale,live));
 auto expired=fixture();std::memset(expired.at(Args).data(),0,expired.at(Args).size());
 assert(verify_live(expired.read(),L,Ctrl,Mgr,c,live));
 auto owned=fixture();w32(owned.at(Res),0x20,1);
 Candidate owned_candidate{};
 assert(verify(owned.read(),L,Ctrl,Args,Args+0x20,Mgr,owned_candidate));
 assert(owned_candidate.archive_buffer_owned);
 assert(!verify_live(owned.read(),L,Ctrl,Mgr,c,live));
 assert(!verify_live(f.read(),L,Ctrl,Mgr,owned_candidate,live));
 assert(verify_live(owned.read(),L,Ctrl,Mgr,owned_candidate,live));
 // Only the native ownership bit is observed; unrelated flag bits are not
 // evidence that the backing allocation changed.
 w32(owned.at(Res),0x20,0x80000001u);
 assert(verify_live(owned.read(),L,Ctrl,Mgr,owned_candidate,live));
 auto truncated=fixture();truncated.at(Res).resize(0x20);
 assert(!verify(truncated.read(),L,Ctrl,Args,Args+0x20,Mgr,live));
 auto b=fixture();w32(b.at(InfoNode),0x18,Info+4);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Holder),0x4c,Res+4);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Wrapper),0x14,Bfres+4);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(ResourceNode),0x18,Bfres+0x241);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();b.at(Args)[8]='Q';assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();b.at(Info)[0x68]='Q';assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(InfoNode),0xc,crc("Other_Stage"));assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Mgr),0x20,2);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(HolderNode),0,HolderNode);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Wrapper),0xc,0);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Mgr),0,0);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Holder),0x50,DataHeap);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Info),0xc0,DataHeap);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(DataHeap),0x24,0);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(DataHeap),0x24,DataHeap);
 assert(!verify_live(b.read(),L,Ctrl,Mgr,live,c));
 b=fixture();w32(b.at(RequestHeap),0xc,0);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Info),0xc4,RequestHeap);assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
 b=fixture();w32(b.at(Info),0xc0,0xfffffff0);w32(b.at(Holder),0x50,0xfffffff0);
 assert(!verify(b.read(),L,Ctrl,Args,Args+0x20,Mgr,c));
}

#endif
