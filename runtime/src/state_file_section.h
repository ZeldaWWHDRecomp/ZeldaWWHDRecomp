// Nonmutating structural preflight for the existing open-file section schema.
#pragma once
#include "savestate.h"
#include <limits>
#include <unordered_set>
namespace ss {
inline bool check_file_section(Reader r,std::string& why) {
    auto fail=[&]{why="corrupt file section";return false;};
    const uint32_t next=r.u32();
    if(!r.ok || !next)return fail();
    std::unordered_set<uint32_t> handles;
    auto handle=[&]{const uint32_t h=r.u32();return r.ok && h && h<next && handles.insert(h).second;};
    auto string=[&](uint32_t limit,bool required=false) {
        const uint32_t n=r.u32();
        if(!r.ok || (required && !n) || n>limit || size_t(r.e-r.p)<n || memchr(r.p,0,n))return false;
        return r.bytes(nullptr,n);
    };
    uint32_t count=r.u32();
    if(!r.ok || count>65536 || uint64_t(count)*21>uint64_t(r.e-r.p))return fail();
    for(uint32_t i=0;i<count;++i)
        if(!handle() || !string(1u<<20) || !string(16,true) || r.u64()>uint64_t(std::numeric_limits<int64_t>::max()) || !r.ok)return fail();
    count=r.u32();
    if(!r.ok || count>65536 || uint64_t(count)*12>uint64_t(r.e-r.p))return fail();
    for(uint32_t i=0;i<count;++i)
        if(!handle() || !string(1u<<20) || r.u32()>1000000 || !r.ok)return fail();
    return r.ok && r.at_end()?true:fail();
}
}
