// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_sarc_extent.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_sarc_extent;
namespace {
constexpr uint32_t Res=0x50000000, Base=0x60000000;
void w16(std::vector<uint8_t>& b,unsigned p,uint16_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
void w32(std::vector<uint8_t>& b,unsigned p,uint32_t v){
    b[p]=uint8_t(v>>24);b[p+1]=uint8_t(v>>16);b[p+2]=uint8_t(v>>8);b[p+3]=uint8_t(v);
}
std::vector<uint8_t> fixture(){
    std::vector<uint8_t>b(0x200);
    w32(b,0x14,Base);w32(b,0x18,0x180);w32(b,0x1c,0x200);
    unsigned s=0x40;
    std::memcpy(b.data()+s,"SARC",4);w16(b,s+4,0x14);w16(b,s+6,0xfeff);
    w32(b,s+8,0x180);w32(b,s+0xc,0x80);w16(b,s+0x10,0x100);
    std::memcpy(b.data()+s+0x14,"SFAT",4);w16(b,s+0x18,0xc);w16(b,s+0x1a,1);w32(b,s+0x1c,0x65);
    const char* name="Stage.bfres";uint32_t hash=0;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(name);*p;++p)hash=hash*0x65+*p;
    w32(b,s+0x20,hash);w32(b,s+0x24,0x01000000);w32(b,s+0x28,0x20);w32(b,s+0x2c,0x60);
    std::memcpy(b.data()+s+0x30,"SFNT",4);w16(b,s+0x34,8);
    std::memcpy(b.data()+s+0x38,name,std::strlen(name)+1);
    return b;
}
bool check(const std::vector<uint8_t>& b,Extent& out,uint32_t expected=Base+0xa0){
    Read read=[&](uint32_t a,std::span<uint8_t>dst){
        unsigned at=a>=Base ? a-Base+0x40 : a>=Res ? a-Res : 0xffffffffu;
        if(uint64_t(at)+dst.size()>b.size())return false;
        std::memcpy(dst.data(),b.data()+at,dst.size());return true;
    };
    return find(read,Res,"Stage.bfres",expected,out);
}
}
int main(int argc,char** argv){
    // Optional read-only real-archive check: Yaz0-decoded SARC on stdin and
    // independently recorded Stage.bfres offset in argv[1]. Never persists it.
    if(argc==2){
        const uint32_t offset=uint32_t(std::strtoul(argv[1],nullptr,0));
        std::vector<uint8_t> sarc((std::istreambuf_iterator<char>(std::cin)),{});
        std::vector<uint8_t> descriptor(0x20);
        w32(descriptor,0x14,Base);w32(descriptor,0x18,uint32_t(sarc.size()));
        w32(descriptor,0x1c,uint32_t(sarc.size()));
        Read read=[&](uint32_t a,std::span<uint8_t>dst){
            if(a>=Base){uint64_t at=uint64_t(a)-Base;
                if(at+dst.size()>sarc.size())return false;
                std::memcpy(dst.data(),sarc.data()+at,dst.size());return true;}
            if(a>=Res){uint64_t at=uint64_t(a)-Res;
                if(at+dst.size()>descriptor.size())return false;
                std::memcpy(dst.data(),descriptor.data()+at,dst.size());return true;}
            return false;
        };
        Extent real{};
        assert(find(read,Res,"Stage.bfres",Base+offset,real));
        std::cout << real.bytes << '\n';
        return 0;
    }
    assert(argc==1);
    auto b=fixture();Extent out{};
    assert(check(b,out)&&out.base==Base+0xa0&&out.bytes==0x40&&out.archive_bytes==0x180&&out.allocation_bytes==0x200);
    assert(!check(b,out,Base+0xa1)&&!out.base);
    auto bad=b;w32(bad,0x1c,0x17f);assert(!check(bad,out));
    bad=b;w32(bad,0x18,0x201);assert(!check(bad,out));
    bad=b;w32(bad,0x40+0x2c,0x300);assert(!check(bad,out));
    bad=b;w32(bad,0x40+0x24,0);assert(!check(bad,out));
    bad=b;bad[0x40+0x38]='X';assert(!check(bad,out));
}
