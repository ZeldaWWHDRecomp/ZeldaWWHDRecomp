#pragma once
#include "guest_identity.h"
#include "../savestate.h"
#include <set>
namespace guestmods {
inline void save_mod_set(ss::Writer& w,const std::vector<ModIdentity>& mods) {
    w.u32(uint32_t(mods.size()));for(const auto& mod:mods){w.str(mod.id);w.str(mod.version);}
}
inline bool read_mod_set(ss::Reader r,std::vector<ModIdentity>& mods) {
    if(r.at_end())return true; // old full states have no guest metadata
    uint32_t count=r.u32();if(count>256)return false;
    std::set<std::string> ids;
    auto field=[&](std::string& s){uint32_t n=r.u32();if(n>64||!r.ok)return false;s.resize(n);return r.bytes(s.data(),n);};
    for(uint32_t i=0;i<count;++i){ModIdentity mod;
        if(!field(mod.id)||!field(mod.version)||!valid_identity(mod)||!ids.insert(mod.id).second)return false;
        mods.push_back(std::move(mod));
    }
    return r.ok&&r.at_end();
}
} // namespace guestmods
