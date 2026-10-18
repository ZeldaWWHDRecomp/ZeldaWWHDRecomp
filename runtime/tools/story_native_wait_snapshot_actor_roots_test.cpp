// Synthetic trusted placement producer; shipped bounded resolver/census and
// footprint code are real. No dispatch/native invocation authority is linked.
#include "mods/story_native_wait_snapshot_actor_roots.h"
#include "mods/story_actors.h"
#include "runtime.h"
#include <cassert>
#include <map>
#include <cstring>
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
namespace {
std::map<uint32_t,std::vector<uint8_t>>memory;
std::array<story_actors::ResidentEventProcess,2>processes;
constexpr uint32_t actor=0x20001000,other=0x20003000,global=0x101F3328,profile=0x101C4CD0,scratch=0x60002000;
unsigned producer_calls=0;bool producer_fail=false,identity_fail=false;
void map(uint32_t a,size_t n){memory[a].resize(n);}
bool read_guest(uint32_t a,std::span<uint8_t>b){for(const auto&[at,data]:memory)if(a>=at&&uint64_t(a)+b.size()<=uint64_t(at)+data.size()){std::copy_n(data.begin()+a-at,b.size(),b.begin());return true;}return false;}
void word(uint32_t a,uint32_t v){for(auto&[at,data]:memory)if(a>=at&&uint64_t(a)+4<=uint64_t(at)+data.size()){for(unsigned n=0;n<4;++n)data[a-at+n]=uint8_t(v>>(24-n*8));return;}assert(false);}
void byte(uint32_t a,uint8_t v){for(auto&[at,data]:memory)if(a>=at&&a<uint64_t(at)+data.size()){data[a-at]=v;return;}assert(false);}
void setup(){memory.clear();producer_calls=0;producer_fail=identity_fail=false;
 map(actor,0x920);map(other,0x920);map(global,12);map(profile,0x20);map(scratch,0x2000);
 map(0x1046F0B0+0x5134,8);map(0x1047E6C4,4);map(0x1047E8F0+0x22C,4);
 word(profile+0x10,0x920);word(global,actor+0xC8);word(global+4,other+0xC8);word(global+8,2);
 for(unsigned n=0;n<2;++n){const uint32_t a=n?other:actor;
  word(a+4,100+n);byte(a+0xE,1);byte(a+0xF,0x75);word(a+0x10,profile);word(a+0xB4,0x100209C4);
  word(a+0xC8,n?actor+0xC8:0);word(a+0xCC,global);word(a+0xD0,n?0:other+0xC8);word(a+0xD4,a);byte(a+0xD8,1);
  auto&p=processes[n];p={};p.presence=story_actors::ResidentEventProcess::Presence::Present;p.pid=100+n;p.declaration={10+n,1,1,"sea",{}};
  p.pid_slot=scratch+128+n*4;p.identity_row=scratch+5632+n*40;p.scratch_window=scratch+0x300;
  word(p.pid_slot,p.pid);word(p.identity_row,0x52455631);word(p.identity_row+4,10+n);
  assert(read_guest(p.pid_slot,p.pid_bytes)&&read_guest(p.identity_row,p.identity_bytes)&&read_guest(p.scratch_window,p.scratch_bytes));
 }
}
}
namespace story_actors {
bool resident_event_process(const std::string&m,const std::string&id,ResidentEventProcess&out){++producer_calls;out={};if(producer_fail||m!="test"||(id!="a"&&id!="b"))return false;out=processes[id=="b"];return true;}
bool resident_event_identity_quiet(const std::string&m,const std::string&id,uint32_t a,uint32_t pid,ResidentEventIdentity&out){out={};if(identity_fail||m!="test"||(id!="a"&&id!="b"))return false;const auto&p=processes[id=="b"];if(a!=(id=="b"?other:actor)||pid!=p.pid)return false;out={p.declaration.token,a,pid,77,88,1,1,p.declaration.fingerprint};return true;}
}
int main(){namespace R=story_native_wait_snapshot_actor_roots;namespace F=story_native_wait_run_footprint;
 const auto layout=story_native_event_arena::make(0x60010000);const std::array<std::string,2>ids{"a","b"};
 setup();map(layout.allocation.address,layout.allocation.bytes);F::Footprint f{{layout.allocation.address,std::vector<uint8_t>(layout.allocation.bytes)}};
 assert(R::append(read_guest,layout,"test",ids,f)&&f.size()<=16&&R::no_borrow(read_guest,layout));const auto good=f;
 for(unsigned off:{0u,8u,0xCu,0x1Cu,0x40u,0x68u}){word(other+0x3E0+off,layout.allocation.address+0x100);assert(!R::no_borrow(read_guest,layout));auto unchanged=good;assert(!R::append(read_guest,layout,"test",ids,unchanged)&&unchanged==good);word(other+0x3E0+off,0);}
 // Timer/action/staff ID are scalars, even when their bit patterns resemble pointers.
 for(unsigned off:{4u,0x10u,0x18u})word(actor+0x3E0+off,layout.allocation.address);assert(R::no_borrow(read_guest,layout));
 auto bad=good;producer_fail=true;assert(!R::append(read_guest,layout,"test",ids,bad)&&bad==good);producer_fail=false;identity_fail=true;assert(!R::append(read_guest,layout,"test",ids,bad));identity_fail=false;
 word(other+0xD0,actor+0xC8);assert(!R::no_borrow(read_guest,layout));word(other+0xD0,0);
 // A changing actor classification cannot hide a borrower during traversal.
 bool changed=false;auto changing=[&](uint32_t at,std::span<uint8_t>bytes){const bool ok=read_guest(at,bytes);if(ok&&at==actor&&bytes.size()==0x14&&!changed){changed=true;byte(actor+0xF,0x74);}return ok;};
 assert(!R::no_borrow(changing,layout));byte(actor+0xF,0x75);
 word(other+4,100);auto duplicate=good;assert(!R::append(read_guest,layout,"test",ids,duplicate));word(other+4,101);
 auto fail_read=[&](uint32_t at,std::span<uint8_t>bytes){return at!=other+0x3E0&&read_guest(at,bytes);};assert(!R::no_borrow(fail_read,layout));
 // Genuine absent host rows do not excuse any still-listed native borrower.
 for(auto&p:processes){p.presence=story_actors::ResidentEventProcess::Presence::Absent;p.pid=0xFFFFFFFFu;word(p.pid_slot,p.pid);assert(read_guest(p.pid_slot,p.pid_bytes));}
 F::Footprint absent;assert(R::append(read_guest,layout,"test",ids,absent));word(actor+0x3E0,layout.allocation.address);assert(!R::append(read_guest,layout,"test",ids,absent));word(actor+0x3E0,0);
 // Saved-only proof reads actual chunks; missing even all-zero census bytes refuse.
 ss::MemoryChunks saved_chunks;std::map<uint32_t,std::vector<uint8_t>>chunks;
 for(const auto&[at,data]:memory)for(size_t n=0;n<data.size();++n){const auto address=at+uint32_t(n),base=address&~(ss::kMemoryChunk-1);auto&c=chunks[base];if(c.empty())c.resize(ss::kMemoryChunk);c[address-base]=data[n];}
 std::vector<ss::MemoryRegion>regions;for(auto&[at,c]:chunks){saved_chunks[at]=c.data();regions.push_back({at,ss::kMemoryChunk});}ss::native_gate::SavedView saved{{},regions,saved_chunks};
 assert(R::no_borrow_saved(saved,layout));const auto census_chunk=global&~(ss::kMemoryChunk-1);auto ptr=saved_chunks[census_chunk];saved_chunks.erase(census_chunk);assert(!R::no_borrow_saved(saved,layout));saved_chunks[census_chunk]=ptr;
 auto&chunk=chunks[(actor+0x3E0)&~(ss::kMemoryChunk-1)];const auto offset=(actor+0x3E0)&(ss::kMemoryChunk-1);for(unsigned n=0;n<4;++n)chunk[offset+n]=uint8_t(layout.allocation.address>>(24-n*8));assert(!R::no_borrow_saved(saved,layout));
}
