// Small declaration-only state identity; no game memory or filesystem dependency.
#pragma once
#include <string>
namespace mods::save_profile {
inline bool valid_provider(const std::string &value) {
    if (value.empty() || value.size() > 24 || value.front() < 'a' || value.front() > 'z')
        return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}
inline bool valid_hash(const std::string &value) {
    if (value.size() != 64)
        return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
inline bool valid_namespace(const std::string &value) {
    if (!value.starts_with("mod-") || value.size() < 70 || value.size() > 93)
        return false;
    const size_t separator = value.size() - 65;
    return value[separator] == '-' && valid_provider(value.substr(4, separator - 4)) &&
           valid_hash(value.substr(separator + 1));
}
inline bool valid_owner(const std::string &value) {
    if (value.empty() || value.size() > 64 || value.front() == '.')
        return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}
struct StateIdentity {
    std::string namespace_key, owner, fingerprint;
    bool empty() const { return namespace_key.empty(); }
    bool valid() const {
        return empty() ? (owner.empty() && fingerprint.empty())
                       : valid_namespace(namespace_key) && valid_owner(owner) &&
                             valid_hash(fingerprint);
    }
    bool operator==(const StateIdentity &) const = default;
};
inline bool portable_compatible(const std::string &saved, const std::string &current) {
    // Legacy states belong to ordinary gameplay only. Portable states can cross
    // compatible code updates, but never provider/seed boundaries.
    return saved == current && (saved.empty() || valid_namespace(saved));
}
} // namespace mods::save_profile
