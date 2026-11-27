// The input.face_layout bridge returns the face layout of the bindings the controller in use plays
// with, a face button taken by a virtual action put back (#636), not the shared mapping. Only that
// key is covered here; the owners of the other keys read() serves are stubbed below.
#include "mods/guest_settings.h"
#include "mods/game_settings.h"
#include "input_map.h"
#include "input.h"
#include "aspect.h"
#include "interp.h"
#include "console_language.h"
#include "gfx/display_modes.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>

namespace gfx {
std::atomic<int> g_mode{0};
const char* const kModeNames[kDrcModeCount] = {"window", "pip", "auto", "off", "gamepad"};
}
namespace aspect {
float game() { return 1280.0f / 720.0f; }
float hud_shape() { return 1280.0f / 720.0f; }
}
namespace interp {
int mode() { return 0; }
int output_fps() { return 30; }
}
namespace game_settings {
const Item* find(const std::string&) { return nullptr; }
}
namespace input {
bool pro_controller() { return false; }
}
uint32_t console_language() { return 0; }

namespace {
using namespace input_map;

std::string setting() {
    const auto value = guestmods::settings::read("input.face_layout");
    assert(value && value->type() == guestmods::settings::String);
    return std::get<std::string>(value->data);
}

// one pad with its own face preset, as the controller setup dialog leaves it; the shared mapping
// keeps whatever it had
void own_profile(const std::string& guid, const std::string& name, FaceLayout faces) {
    Mapping m = current();
    const int index = ensure_profile(m, guid, name);
    Mapping view = view_for(m, index);
    apply_face_layout(view, faces);
    store_view(m, view, index);
    set_current(m, false);
}

void shared_faces(FaceLayout faces) {
    Mapping m = current();
    apply_face_layout(m, faces);
    set_current(m, false);
}
}  // namespace

int main() {
    set_current(Mapping::defaults(), false);
    const std::string guid = "test-xbox-guid", name = "Xbox Series pad";

    // shared by position, the pad's own profile by label: the setting follows the pad
    own_profile(guid, name, FaceLayout::kLabels);
    set_active_controller(guid, name, PadFamily::kXbox, false);
    assert(setting() == "labels");

    // no pad profile in use: the shared mapping decides
    set_active_controller("", "", PadFamily::kUnknown, false);
    assert(setting() == "position");

    // the inverse: shared by label, the pad's own profile by position
    shared_faces(FaceLayout::kLabels);
    own_profile(guid, name, FaceLayout::kPosition);
    set_active_controller(guid, name, PadFamily::kXbox, false);
    assert(setting() == "position");

    // a face taken by Jump anywhere: the raw mapping describes no preset, the prompts keep the look
    shared_faces(FaceLayout::kPosition);
    {
        Mapping m = current();
        const int index = ensure_profile(m, guid, name);
        Mapping view = view_for(m, index);
        apply_face_layout(view, FaceLayout::kLabels);
        assert(bind_virtual(view, kJump, kPadA, Displace::kMove));
        assert(face_layout(view) == FaceLayout::kCustom);
        store_view(m, view, index);
        set_current(m, false);
    }
    assert(setting() == "labels");
    set_active_controller("", "", PadFamily::kUnknown, false);
    assert(setting() == "position");

    std::puts("guest_settings_read_test: ok");
    return 0;
}
