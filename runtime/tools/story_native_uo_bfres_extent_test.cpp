// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_uo_bfres_extent.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#include <cstdint>
#include <span>
#include <string_view>
using namespace story_native_uo_bfres_extent;
namespace {
constexpr uint32_t Heap=0x20000000,Res=Heap+0x100,Base=Heap+0x200;
void put(auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
void half(auto&b,unsigned p,uint16_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
struct Fixture{
 Input in;std::vector<uint8_t>memory=std::vector<uint8_t>(0x1000);std::map<uint32_t,uint32_t>fixed{{0x10145B94,0x02755280},{0x10145BA4,0x02755298},{0x1015E1C0,0x0275E208},{0x1015E1B8,0x0275CF28}};bool tamper=false;
 Fixture(){
  in.parent_allocation={Heap,0x1000};in.parent_heap=in.archive_parent_heap=in.holder_heap=Heap;in.resource_address=Res;in.archive_file=Base+0x90;
  put(in.heap,0xC,0x10145B38);put(in.heap,0x1C,Heap);put(in.heap,0x20,0x1000);
  put(in.resource,0x10,0x1015E184);put(in.resource,0x14,Base);put(in.resource,0x18,0x180);put(in.resource,0x1C,0x200);put(in.resource,0x28,Base);put(in.resource,0x2C,Base+0x14);put(in.resource,0x30,Base+0x38);put(in.resource,0x34,1);put(in.resource,0x38,Base+0x20);put(in.resource,0x3C,Base+0x80);
  auto p=0x200u;put(memory,p,0x53415243);half(memory,p+4,0x14);half(memory,p+6,0xFEFF);put(memory,p+8,0x180);put(memory,p+0xC,0x80);half(memory,p+0x10,0x100);put(memory,p+0x14,0x53464154);half(memory,p+0x18,0xC);half(memory,p+0x1A,1);put(memory,p+0x1C,0x65);
  uint32_t hash=0;for(unsigned char ch:std::string_view("Uo.bfres"))hash=hash*0x65+ch;put(memory,p+0x20,hash);put(memory,p+0x24,0x01000000);put(memory,p+0x28,0x10);put(memory,p+0x2C,0x60);put(memory,p+0x30,0x53464E54);half(memory,p+0x34,8);std::memcpy(memory.data()+p+0x38,"Uo.bfres",9);
 }
 bool verify(Extent&out){
  std::copy(in.heap.begin(),in.heap.end(),memory.begin());std::copy(in.resource.begin(),in.resource.end(),memory.begin()+0x100);
  Read read=[&](uint32_t a,std::span<uint8_t>b){if(a>=Heap&&uint64_t(a)-Heap+b.size()<=memory.size()){std::copy_n(memory.begin()+a-Heap,b.size(),b.begin());if(tamper&&a==Res)b[0x18]^=1;return true;}auto it=fixed.find(a);if(it==fixed.end()||b.size()!=4)return false;put(b,0,it->second);return true;};
  return story_native_uo_bfres_extent::verify(read,in,out);
 }
};
}
int main(){
 Fixture f;Extent out;assert(f.verify(out)&&out.base==Base+0x90&&out.bytes==0x50&&out.archive_bytes==0x180&&out.allocation_bytes==0x200);
 for(unsigned n=0;n<25;++n){Fixture x;switch(n){
 case 0:x.in.parent_allocation={};break;case 1:x.in.parent_allocation.bytes=0x300;break;case 2:x.in.archive_parent_heap+=4;break;case 3:x.in.holder_heap+=4;break;
 case 4:put(x.in.heap,0xC,0xDEADBEEF);break;case 5:x.fixed[0x10145B94]++;break;case 6:put(x.in.resource,0x10,0);break;case 7:put(x.in.resource,0x1C,0x1001);break;
 case 8:put(x.memory,0x208,0x10000);break;case 9:half(x.memory,0x21A,0x4000);break;case 10:put(x.memory,0x224,0x02000000);break;case 11:put(x.memory,0x224,0x01000020);break;
 case 12:x.memory[0x238]='X';break;case 13:x.memory[0x240]=1;break;case 14:put(x.memory,0x220,1);break;case 15:put(x.memory,0x228,0x61);break;case 16:put(x.memory,0x22C,0xFFFFFFFF);break;
 case 17:x.in.archive_file+=4;break;case 18:put(x.in.resource,0x38,Base+0x24);break;case 19:put(x.in.resource,0x3C,Base+0x84);break;case 20:x.tamper=true;break;
 case 21:half(x.memory,0x206,0xFFFE);break;case 22:put(x.memory,0x20C,0xFFFFFFFF);break;case 23:put(x.memory,0x224,0);break;case 24:x.in.resource_address=Heap+0xFF0;break;
 }out={1,2,3,4,5};assert(!x.verify(out)&&!out.base&&!out.bytes&&!out.archive_base&&!out.archive_bytes&&!out.allocation_bytes);}
 // A duplicate selected hash/name must never depend on native collision order.
 Fixture duplicate;half(duplicate.memory,0x21A,2);put(duplicate.in.resource,0x34,2);put(duplicate.in.resource,0x30,Base+0x48);std::copy_n(duplicate.memory.begin()+0x220,16,duplicate.memory.begin()+0x230);put(duplicate.memory,0x240,0x53464E54);half(duplicate.memory,0x244,8);std::memcpy(duplicate.memory.data()+0x248,"Uo.bfres",9);assert(!duplicate.verify(out)&&!out.base);
 // The actual native initializer accepts at most0x3FFF SFAT rows. Exercise
 // its exact upper boundary with every row bounded by the actual allocation.
 Fixture largest;largest.memory.resize(0x50000);largest.in.parent_allocation.bytes=0x50000;put(largest.in.heap,0x20,0x50000);
 constexpr unsigned rows=0x3FFF,sfnt=0x20+rows*16,names=sfnt+8,data=0x40040;
 put(largest.in.resource,0x18,data+0x100);put(largest.in.resource,0x1C,0x4FE00);put(largest.in.resource,0x30,Base+names);put(largest.in.resource,0x34,rows);put(largest.in.resource,0x3C,Base+data);largest.in.archive_file=Base+data+0x10;
 put(largest.memory,0x208,data+0x100);put(largest.memory,0x20C,data);half(largest.memory,0x21A,rows);
 uint32_t selectedhash=0;for(unsigned char ch:std::string_view("Uo.bfres"))selectedhash=selectedhash*0x65+ch;
 for(unsigned n=0;n<rows;++n){put(largest.memory,0x220+n*16,n+1==rows?selectedhash:n);put(largest.memory,0x224+n*16,n+1==rows?0x01000000:0);put(largest.memory,0x228+n*16,0x10);put(largest.memory,0x22C+n*16,0x60);}
 put(largest.memory,0x200+sfnt,0x53464E54);half(largest.memory,0x204+sfnt,8);std::memcpy(largest.memory.data()+0x200+names,"Uo.bfres",9);
 assert(largest.verify(out)&&out.bytes==0x50);half(largest.memory,0x21A,0x4000);assert(!largest.verify(out)&&!out.base);
 // Exceptions and missing reads fail closed and clear caller-provided output.
 out={1,2,3,4,5};Read throwing=[](uint32_t,std::span<uint8_t>)->bool{throw 1;};assert(!verify(throwing,f.in,out)&&!out.base);assert(!verify({},f.in,out));
}
