#pragma once
#include "save_profile_identity.h"
#include "../savestate.h"
#include "../full_state_header.h"
namespace mods::save_profile {
inline void write_state_identity(ss::Writer &writer, const StateIdentity &identity) {
    writer.str(identity.namespace_key);
    writer.str(identity.owner);
    writer.str(identity.fingerprint);
}
inline bool read_state_identity(ss::Reader reader, StateIdentity &identity) {
    identity = {};
    if (!reader.ok)
        return false;
    if (reader.at_end())
        return true; // Legacy full state: no declarative profile.
    auto field = [&](std::string &value, size_t maximum) {
        const uint32_t size = reader.u32();
        if (!reader.ok || size > maximum)
            return false;
        value.resize(size);
        return reader.bytes(value.data(), size);
    };
    return field(identity.namespace_key, 93) && field(identity.owner, 64) &&
           field(identity.fingerprint, 64) && reader.ok && reader.at_end() && identity.valid();
}
inline bool valid_state_requirement(uint32_t flags, const StateIdentity &identity) {
    return !(flags & ~ss::kRequiresSaveProfile) &&
           bool(flags & ss::kRequiresSaveProfile) == !identity.empty();
}
inline bool compatible_full_state(ss::Reader reader, const StateIdentity &current) {
    StateIdentity saved;
    return read_state_identity(reader, saved) && saved == current;
}
} // namespace mods::save_profile
