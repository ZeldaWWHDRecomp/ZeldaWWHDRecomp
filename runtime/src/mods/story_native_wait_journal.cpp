// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_journal.h"
#include <algorithm>
#include <limits>
namespace story_native_wait_journal { namespace {
void put(std::span<uint8_t>b,unsigned o,uint32_t v){for(unsigned n=0;n<4;++n)b[o+n]=uint8_t(v>>(24-8*n));}
uint32_t get(std::span<const uint8_t>b,unsigned o){return uint32_t(b[o])<<24|uint32_t(b[o+1])<<16|uint32_t(b[o+2])<<8|b[o+3];}
uint32_t sum(std::span<const uint8_t>b){uint32_t h=2166136261u;for(unsigned n=0;n<124;++n)h=(h^b[n])*16777619u;return h;}
bool valid(const Record&r){return r.generation&&r.serial&&r.arena>=0x60000000&&uint64_t(r.arena)+0x7000<=0x68000000&&!(r.arena&15)&&uint32_t(r.phase)<=6&&std::any_of(r.definition.begin(),r.definition.end(),[](auto v){return v!=0;});}
}
bool encode(const Record&r,std::span<uint8_t>b){if(b.size()!=kBytes||!valid(r))return false;std::array<uint8_t,kBytes>x{};put(x,0,0x574A4E4C);put(x,4,kVersion);put(x,8,kBytes);put(x,12,uint32_t(r.phase));put(x,16,r.generation>>32);put(x,20,r.generation);put(x,24,r.serial>>32);put(x,28,r.serial);put(x,32,r.arena);std::copy(r.definition.begin(),r.definition.end(),x.begin()+40);put(x,124,sum(x));std::copy(x.begin(),x.end(),b.begin());return true;}
bool decode(std::span<const uint8_t>b,Record&out){out={};if(b.size()!=kBytes||get(b,0)!=0x574A4E4C||get(b,4)!=kVersion||get(b,8)!=kBytes||get(b,124)!=sum(b))return false;for(unsigned n=36;n<124;++n)if((n<40||n>=72)&&b[n])return false;Record r;r.phase=Phase(get(b,12));r.generation=uint64_t(get(b,16))<<32|get(b,20);r.serial=uint64_t(get(b,24))<<32|get(b,28);r.arena=get(b,32);std::copy_n(b.begin()+40,32,r.definition.begin());if(!valid(r))return false;out=r;return true;}
bool transition(const Record&r,Phase p,Record&out){out={};if(!valid(r)||r.serial==UINT64_MAX)return false;const auto a=r.phase;bool ok=p==Phase::Quarantined&&a!=Phase::Quarantined;ok|=(a==Phase::Unpublished&&p==Phase::Preparing)||(a==Phase::Preparing&&(p==Phase::Published||p==Phase::Unpublished))||(a==Phase::Published&&(p==Phase::Running||p==Phase::Draining))||(a==Phase::Running&&p==Phase::Draining)||(a==Phase::Draining&&p==Phase::Detaching)||(a==Phase::Detaching&&p==Phase::Unpublished);if(!ok)return false;out=r;out.phase=p;++out.serial;return true;}
bool next_generation(const Record&r,bool detached,Record&out){out={};if(!valid(r)||!detached||r.phase!=Phase::Unpublished||r.generation==UINT64_MAX||r.serial==UINT64_MAX)return false;out=r;++out.generation;++out.serial;return true;}
}
