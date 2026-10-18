#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "mods/story_world_collision_proof_mesh.h"
using namespace story_world_collision::proof;
static uint32_t word(const std::vector<uint8_t>&b,size_t at){return (uint32_t(b[at])<<24)|(uint32_t(b[at+1])<<16)|(uint32_t(b[at+2])<<8)|b[at+3];}
int main(){
    for(auto kind:{Kind::Platform,Kind::Buoy,Kind::Raft}){
        const auto m=mesh(kind);assert(m.vertices.size()==(kind==Kind::Buoy?8:16));
        double volume=0;unsigned upward=0;
        for(const auto&t:m.triangles){const auto a=m.vertices.at(t[0]),b=m.vertices.at(t[1]),c=m.vertices.at(t[2]);
            const double nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),ny=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),nz=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            assert(nx*nx+ny*ny+nz*nz>0);upward+=ny>0;
            volume+=(double(a.x)*(b.y*c.z-b.z*c.y)+double(a.y)*(b.z*c.x-b.x*c.z)+double(a.z)*(b.x*c.y-b.y*c.x))/6;
        }assert(volume>0&&upward==(kind==Kind::Platform?4:2));
        const auto bytes=encode(m,0x20000000,11);assert(word(bytes,0)==m.vertices.size()&&word(bytes,8)==m.triangles.size());
        for(unsigned i=0;i<6;++i){const auto p=word(bytes,i*8+4);assert(p>=0x20000034&&p<0x20000000+bytes.size());}
        assert(word(bytes,0x30)==0x80000000u);
        const auto attributes=word(bytes,0x2C)-0x20000000;
        assert(((word(bytes,attributes)&0x7E000)>>13)==63); // no native exit
        assert((word(bytes,attributes)&255)==255&&(word(bytes,attributes+4)&255)==255);
        assert(word(bytes,attributes+8)==0xFFFFFFFF&&word(bytes,attributes+12)==0);
        assert(word(bytes,0x28)==(kind==Kind::Raft?2u:1u));
        if(kind==Kind::Raft){
            assert(word(bytes,attributes+28)==0xED); // Link/camera through, object/ship collision retained
            const auto triangles=word(bytes,0xC)-0x20000000;
            for(unsigned i=0;i<m.triangles.size();++i){const unsigned code=unsigned(bytes[triangles+i*10+6])*256+bytes[triangles+i*10+7];assert(code==(i<12?0u:1u));}
            // Envelope has walls only: the visible thin deck remains the only ground.
            assert(m.triangles.size()==20&&m.vertices.size()==16);
        }
        const auto group=word(bytes,0x24)-0x20000000;
        assert(bytes[group+0x24]==255&&bytes[group+0x25]==255&&bytes[group+0x2A]==0&&bytes[group+0x2B]==11);
    }
    assert(mesh(Kind::None).vertices.empty()&&encode({},0,0).empty());
}
