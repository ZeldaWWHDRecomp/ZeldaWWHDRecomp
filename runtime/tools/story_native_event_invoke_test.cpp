// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_invoke.h"
#include "guest_addr.h"
#include "ppc.h"
#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
namespace I=story_native_event_invoke;
int main(){
 Cpu c{};c.r[1]=0x60001000;c.lr=42;c.ctr=43;c.pc=44;c.f[3].ps0=1.25;c.f[3].ps1=2.5;c.res_addr=123;c.mod_skip=9;c.thread=reinterpret_cast<void*>(0x1234);Cpu before=c;
 std::array<uint8_t,64> frame;for(unsigned i=0;i<64;++i)frame[i]=uint8_t(i);const auto pristine=frame;unsigned calls=0,writes=0;bool throws=false,writefail=false;
 I::Config config{{0x60000000,0x2000},{{0x60002000,0xE60},{0x60004000,64},{0x60005000,512}}};
 I::Read read=[&](auto ea,auto out){assert(ea==before.r[1]-64&&out.size()==64);std::copy(frame.begin(),frame.end(),out.begin());return true;};
 I::Write write=[&](auto ea,auto bytes){++writes;assert(ea>=before.r[1]-64&&uint64_t(ea)+bytes.size()<=before.r[1]);if(writefail)return false;std::copy(bytes.begin(),bytes.end(),frame.begin()+ea-(before.r[1]-64));return true;};
 I::Call call=[&](Cpu*cpu,uint32_t fn,std::initializer_list<uint32_t> regs){++calls;assert(cpu->r[1]==before.r[1]);if(fn==GC(0x0253EC0C)){assert(regs.size()==8);assert(frame[8]==0&&frame[9]==0&&frame[10]==0&&frame[11]==0xFF);}cpu->r[1]-=64;std::memset(frame.data(),0xA5,64);cpu->r[9]=999;cpu->lr=1;cpu->f[3].ps1=99;cpu->mod_skip=2;if(throws)throw std::runtime_error("native");return 77;};
 I::Bridge bridge(config,read,write,call);
 auto r=bridge.invoke(&c,GC(0x0253EC0C),{1,3,1,0,0,0,0,0xFFFFFFFF},{0xFF});assert(r.ok&&r.restored&&r.value==77&&frame==pristine&&!std::memcmp(&c,&before,sizeof(c)));
 const uint32_t one[]={GC(0x0253FD1C),GC(0x0253EFA8),GC(0x0254273C)};for(auto fn:one)assert(bridge.invoke(&c,fn,{1},{}).ok);assert(bridge.invoke(&c,GC(0x025443B4),{1,2},{}).ok);assert(bridge.invoke(&c,GC(0x02544534),{1,0},{}).ok);assert(bridge.invoke(&c,GC(0x0259F7D4),{1,2,3},{}).ok);
 throws=true;r=bridge.invoke(&c,GC(0x0253EC0C),{1,3,1,0,0,0,0,0xFFFFFFFF},{0xFF});assert(!r.ok&&r.restored&&r.reason==I::Reason::Call&&frame==pristine&&!std::memcmp(&c,&before,sizeof(c)));throws=false;
 auto count=calls;assert(!bridge.invoke(&c,GC(0x0253EC0C),{1,2},{0xFF}).ok);assert(!bridge.invoke(&c,GC(0x0253EC0C),{1,3,1,0,0,0,0,0xFFFFFFFF},{0}).ok);assert(!bridge.invoke(&c,GC(0x0253FD1C),{1},{0xFF}).ok);assert(!bridge.invoke(&c,0x12345678,{1},{}).ok);assert(calls==count);
 c.r[1]++;assert(!bridge.invoke(&c,GC(0x0253FD1C),{1},{}).ok);c=before;
 for(auto range:{I::Range{before.r[1]-64,1},I::Range{before.r[1]-4,4}}){auto cfg=config;cfg.protected_ranges.push_back(range);I::Bridge blocked(cfg,read,write,call);assert(!blocked.invoke(&c,GC(0x0253FD1C),{1},{}).ok);}auto cfg=config;cfg.stack={before.r[1]-63,63};I::Bridge shortstack(cfg,read,write,call);assert(!shortstack.invoke(&c,GC(0x0253FD1C),{1},{}).ok);
 writefail=true;r=bridge.invoke(&c,GC(0x0253EC0C),{1,3,1,0,0,0,0,0xFFFFFFFF},{0xFF});assert(!r.ok&&!r.restored&&r.reason==I::Reason::Restore&&!std::memcmp(&c,&before,sizeof(c)));writefail=false;
 unsigned attempts=0;I::Bridge partial(config,read,[&](auto ea,auto bytes){if(++attempts==1){frame[8]=0xAA;throw std::runtime_error("partial write");}return write(ea,bytes);},call);r=partial.invoke(&c,GC(0x0253EC0C),{1,3,1,0,0,0,0,0xFFFFFFFF},{0xFF});assert(!r.ok&&r.restored&&frame==pristine&&!std::memcmp(&c,&before,sizeof(c)));
 I::Bridge badread(config,[](auto,auto)->bool{throw std::runtime_error("read");},write,call);assert(!badread.invoke(&c,GC(0x0253FD1C),{1},{}).ok);assert(!std::memcmp(&c,&before,sizeof(c)));
}
