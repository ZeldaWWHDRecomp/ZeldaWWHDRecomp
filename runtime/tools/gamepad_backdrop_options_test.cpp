#include "gfx/gamepad_backdrop_options.h"
#include <cassert>
#include <map>
#include <string>

namespace {
std::map<std::string, std::string> preferences;
unsigned writes = 0;
}
namespace hostui {
bool get(const char* key, std::string& value) {
    auto found = preferences.find(key);
    if (found == preferences.end()) return false;
    value = found->second;
    return true;
}
void set(const char* key, const std::string& value) {
    preferences[key] = value;
    ++writes;
}
}
int main() {
    using namespace gfx::gamepad_backdrop;
    enabled.store(false);
    load_options();
    assert(enabled.load() && writes == 0);
    set_capture_requested(true);
    set_option(false);
    assert(!capture_enabled() && preferences.at(kSettingKey) == "0");
    enabled.store(true); // Simulate a fresh process's default before loading.
    load_options();
    assert(!enabled.load() && writes == 1);
    set_option(true);
    assert(capture_enabled() && preferences.at(kSettingKey) == "1");
    set_capture_requested(false);
    assert(!capture_enabled()); // No capture work for a screen without bars.
    preferences[kSettingKey] = "unknown";
    load_options();
    assert(enabled.load() && writes == 2);
}
