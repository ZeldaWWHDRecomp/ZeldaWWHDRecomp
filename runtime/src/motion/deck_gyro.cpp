#include "deck_gyro.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace deck_gyro {
namespace {
using Pair = std::pair<uint16_t, uint16_t>;
// the pairs as SDL reads them (SDL_LoadVIDPIDListFromHint): each "0x" number, then the next "0x" number
std::vector<Pair> pairs(const std::string& list) {
    std::vector<Pair> out;
    const char* s = list.c_str();
    while ((s = std::strstr(s, "0x")) != nullptr) {
        char* end = nullptr;
        const uint16_t vendor = (uint16_t)std::strtol(s, &end, 0);
        s = std::strstr(end, "0x");
        if (!s) break;
        const uint16_t product = (uint16_t)std::strtol(s, &end, 0);
        s = end;
        out.push_back({vendor, product});
    }
    return out;
}
std::string format(const std::vector<Pair>& v) {
    std::string out;
    for (auto [vendor, product] : v) {
        char entry[32];
        std::snprintf(entry, sizeof entry, "0x%04x/0x%04x", vendor, product);
        if (!out.empty()) out += ',';
        out += entry;
    }
    return out;
}
}  // namespace

bool is_deck_controller(uint16_t vendor, uint16_t product) { return vendor == kValve && product == kDeckController; }
bool is_steam_virtual_pad(uint16_t vendor, uint16_t product, uint64_t steam_handle) {
    return steam_handle != 0 || (vendor == kValve && product == kSteamVirtualPad);
}

bool enabled(bool steam_deck, const char* env) {
    if (!steam_deck) return false;
    if (!env) return true;
    return std::strcmp(env, "0") != 0 && std::strcmp(env, "off") != 0 && std::strcmp(env, "false") != 0;
}

bool lists_device(const std::string& list, uint16_t vendor, uint16_t product) {
    const auto v = pairs(list);
    return std::find(v.begin(), v.end(), Pair{vendor, product}) != v.end();
}
std::string without_device(const std::string& list, uint16_t vendor, uint16_t product, bool* changed) {
    auto v = pairs(list);
    const size_t before = v.size();
    std::erase(v, Pair{vendor, product});
    if (changed) *changed = v.size() != before;
    return v.size() == before ? list : format(v);
}
std::string with_device(const std::string& list, uint16_t vendor, uint16_t product) {
    auto v = pairs(list);
    if (std::find(v.begin(), v.end(), Pair{vendor, product}) != v.end()) return list;
    v.push_back({vendor, product});
    return format(v);
}

Prepared prepare(bool on, const std::function<const char*(const char*)>& get,
                 const std::function<std::string(const std::string&)>& read_file) {
    Prepared p;
    if (!on) return p;
    // SDL's gamepad lists may name a file ("@path"): its contents are the list
    auto resolve = [&](const char* value) -> std::string {
        if (!value) return {};
        if (value[0] == '@') return read_file ? read_file(value + 1) : std::string{};
        return value;
    };
    // Steam's list of controllers it handles itself: the Deck's leaves it, the others stay
    for (const char* name : {"SDL_GAMECONTROLLER_IGNORE_DEVICES", "SDL_HIDAPI_IGNORE_DEVICES"}) {
        const char* value = get(name);
        if (!value) continue;
        const std::string list = std::strcmp(name, "SDL_HIDAPI_IGNORE_DEVICES") == 0 ? std::string(value) : resolve(value);
        bool changed = false;
        const std::string rest = without_device(list, kValve, kDeckController, &changed);
        if (changed) {
            p.set.push_back({name, rest});
            p.was_ignored = true;
        }
    }
    // "only these controllers": the Deck's is one of them
    if (const char* value = get("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT")) {
        const std::string list = resolve(value);
        if (!pairs(list).empty() && !lists_device(list, kValve, kDeckController)) {
            p.set.push_back({"SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT", with_device(list, kValve, kDeckController)});
            p.was_ignored = true;
        }
    }
    // SDL's HIDAPI Steam Deck driver (the one with the sensors), whatever the HIDAPI defaults are
    p.set.push_back({"SDL_JOYSTICK_HIDAPI_STEAMDECK", "1"});
    return p;
}

Role role(bool on, const Pad& pad, const std::vector<Pad>& present) {
    if (!on || !is_deck_controller(pad.vendor, pad.product)) return Role::kController;
    for (const Pad& other : present)
        if (other.id != pad.id && is_steam_virtual_pad(other.vendor, other.product, other.steam_handle))
            return Role::kSensorOnly;
    return Role::kController;
}

bool sensor_only_feeds(bool active_known, bool active_has_motion) { return !active_known || !active_has_motion; }

}  // namespace deck_gyro
