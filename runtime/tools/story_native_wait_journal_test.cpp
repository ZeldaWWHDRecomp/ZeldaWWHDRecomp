// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_journal.h"
#include <cassert>
#include <array>
#include <cstdint>
using namespace story_native_wait_journal;
int main(){Record r{1,1,0x60010000};r.definition[0]=1;std::array<uint8_t,kBytes>b{};assert(encode(r,b));Record d;assert(decode(b,d)&&d==r);for(unsigned i=0;i<kBytes;++i){auto bad=b;bad[i]^=1;assert(!decode(bad,d));}Record n;assert(!next_generation(r,false,n));assert(next_generation(r,true,n)&&n.generation==2);for(auto p:{Phase::Preparing,Phase::Published,Phase::Running,Phase::Draining,Phase::Detaching,Phase::Unpublished}){assert(transition(r,p,n));r=n;}assert(!transition(r,Phase::Running,n));assert(transition(r,Phase::Quarantined,n));assert(!transition(n,Phase::Unpublished,d));r.generation=UINT64_MAX;assert(!next_generation(r,true,n));return 0;}
