// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "../savestate.h"
namespace native_actor_profiles {
// Missing metadata is compatible only when no guest profiles are registered.
// A bounded exact identity avoids accepting same-version changed module code.
inline bool compatible_state(ss::Reader r,const std::string& current){
    if(r.at_end())return current.empty();
    const uint32_t bytes=r.u32();
    if(!r.ok||bytes>64*(1+64+64+0x60)||bytes>size_t(r.e-r.p))return false;
    std::string stored(bytes,'\0');r.bytes(stored.data(),bytes);
    return r.ok&&r.at_end()&&stored==current;
}
}
