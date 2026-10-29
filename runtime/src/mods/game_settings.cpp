// The common settings list (game_settings.h).
#include "game_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <atomic>
#include <memory>
#include <mutex>

#include "input_map.h"
#include "manager.h"
#include "mods.h"
#include "packages.h"
#include "game_settings_graphics.h"
#include "../aspect.h"
#include "../dynres.h"
#include "../gfx/renderer.h"
#include "../interp.h"
#include "../input.h"
#include "../rumble.h"
#include "../platform/phone_rumble.h"
#ifdef __ANDROID__
#include "../gfx/display_modes.h"
#include "../platform/touch_options.h"
#endif
#include "../overlay/hostui.h"
#include "runtime.h"

namespace game_settings {
namespace packages = mods::packages;
namespace json = mods::json;
namespace {

std::mutex g_mu;
std::vector<Item>& registry() {
    static std::vector<Item> items;
    return items;
}
std::map<std::string, float> g_pending;  // id -> value posted, not yet applied (g_mu)

const char* const kTabOrder[] = {"camera", "controls", "screen", "graphics", "game", "mods"};

// a built-in mod's switch (mods/manager.cpp: the same entry the Mods tab switches)
Item builtin_toggle(const char* tab, const char* mod_id, const char* label, const char* help) {
    Item it;
    it.id = mod_id, it.tab = tab, it.label = label, it.help = help, it.kind = Kind::Toggle;
    const std::string id = mod_id;
    it.get = [id] {
        const auto* e = mods::manager::find(id);
        return e && e->enabled() ? 1.0f : 0.0f;
    };
    it.set = [id](float v) { mods::manager::set_enabled(id, v >= 0.5f); };
    return it;
}

// a label as the page shows it: capitalised ("automatic" -> "Automatic")
std::string capital(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}

std::string times(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2fx", v);
    return b;
}

}  // namespace

void set_camera_speed(float speed) {
    mods::set_camera_speed(speed);
    hostui::set("mod.direct-camera.speed", std::to_string(speed));
    mods::packages::remember_option("direct-camera.speed", speed);
}
void set_mouse_sensitivity(float degrees) {
    mods::set_mouse_sensitivity(degrees);
    hostui::set("mod.mouse-camera.sensitivity", std::to_string(degrees));
    mods::packages::remember_option("mouse-camera.sensitivity", degrees);
}
void set_invert_camera_y(bool on) {
    input_map::Mapping m = input_map::current();
    if (m.invert_camera_y == on) return;
    m.invert_camera_y = on;
    input_map::set_current(m);
}

std::vector<std::string> tabs() {
    std::lock_guard lock(g_mu);
    std::vector<std::string> out;
    for (const char* t : kTabOrder)
        for (const Item& it : registry())
            if (it.tab == t) { out.push_back(t); break; }
    for (const Item& it : registry())
        if (std::find(out.begin(), out.end(), it.tab) == out.end()) out.push_back(it.tab);
    return out;
}
std::string tab_label(const std::string& tab) {
    if (tab == "camera") return "Camera";
    if (tab == "controls") return "Controls";
    if (tab == "screen") return "Screen";
    if (tab == "graphics") return "Graphics";
    if (tab == "game") return "Game";
    if (tab == "mods") return "Mods";
    return tab;
}
std::vector<const Item*> items(const std::string& tab) {
    std::lock_guard lock(g_mu);
    std::vector<const Item*> out;
    for (const Item& it : registry())
        if (it.tab == tab) out.push_back(&it);
    return out;
}
const Item* find(const std::string& id) {
    std::lock_guard lock(g_mu);
    for (const Item& it : registry())
        if (it.id == id) return &it;
    return nullptr;
}
void add(Item item) {
    std::lock_guard lock(g_mu);
    for (Item& it : registry())
        if (it.id == item.id) { it = std::move(item); return; }
    if (registry().capacity() < 256) registry().reserve(256);  // the page keeps pointers between frames: no reallocation
    if (registry().size() >= 256) return;
    registry().push_back(std::move(item));
}

float pending(const std::string& id) {
    std::lock_guard lock(g_mu);
    auto it = g_pending.find(id);
    return it == g_pending.end() ? NAN : it->second;
}

static float current(const Item& item) {
    const float p = pending(item.id);
    return std::isnan(p) ? (item.get ? item.get() : 0.0f) : p;
}

std::string value_text(const Item& item) {
    const float v = current(item);
    switch (item.kind) {
    case Kind::Toggle: return v >= 0.5f ? "On" : "Off";
    case Kind::Choice: {
        const size_t i = size_t(std::clamp(int(std::lround(v)), 0, int(item.choices.size()) - 1));
        return item.choices.empty() ? "" : item.choices[i];
    }
    case Kind::Number:
        if (item.format) return item.format(v);
        char b[32];
        const bool whole = item.step >= 1 && std::fabs(item.step - std::round(item.step)) < 1e-4f && std::fabs(item.min - std::round(item.min)) < 1e-4f;
        std::snprintf(b, sizeof b, whole ? "%.0f" : "%.2f", v);
        return b;
    }
    return "";
}

float apply(const Item& item, int direction) {
    const float v = current(item);
    float next = v;
    switch (item.kind) {
    case Kind::Toggle: next = direction == 0 ? (v >= 0.5f ? 0.0f : 1.0f) : direction > 0 ? 1.0f : 0.0f; break;
    case Kind::Choice: {
        const int n = int(item.choices.size());
        if (n) next = float(((int(std::lround(v)) + (direction == 0 ? 1 : direction)) % n + n) % n);
        break;
    }
    case Kind::Number: {
        const int d = direction == 0 ? 1 : direction;
        // snap to the step grid, so a saved in-between value (a slider's 1.37x) lands on a step
        const float steps = std::round((v - item.min) / item.step) + float(d);
        next = std::clamp(item.min + steps * item.step, item.min, item.max);
        break;
    }
    }
    if (next == v) return v;
    {
        std::lock_guard lock(g_mu);
        g_pending[item.id] = next;
    }
    const std::string id = item.id;
    auto set = item.set;
    hostui::post([id, set, next] {
        if (set) set(next);
        std::lock_guard lock(g_mu);
        if (auto it = g_pending.find(id); it != g_pending.end() && it->second == next) g_pending.erase(it);
    });
    return next;
}

void set_fov_scale(float scale) {
    aspect::set_fov_scale(scale);
    hostui::set("gameplayFovScale", std::to_string(scale));
    hostui::graphics_changed();
}
void set_rumble(bool on) {
    rumble::set_enabled(on);
    hostui::set("rumble", on ? "1" : "0");
}
void set_move_speed_bar(bool on) {
    mods::set_move_speed_bar(on);
    hostui::set("mod.move-speed.bar", on ? "1" : "0");
    mods::packages::remember_option("move-speed.bar", on);
}

namespace {
// a change of the controls mapping (controls.json), as the Settings window's Controls tab makes it
void change_mapping(const std::function<void(input_map::Mapping&)>& fn) {
    input_map::Mapping m = input_map::current();
    fn(m);
    input_map::set_current(m);
}
std::string percent(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f%%", v * 100.0f);
    return b;
}
void register_controls() {
    using M = input_map::Mapping;
    {
        Item it;
        it.id = "controls.face-buttons", it.tab = "controls", it.label = "Face buttons", it.kind = Kind::Choice;
        it.help = "Which button does A / B / X / Y: by position (right is A), by label, or automatic.";
        it.choices = {"By position", "By label", "Automatic"};
        it.get = [] {
            const M m = input_map::current();
            if (m.face_auto) return 2.0f;
            return input_map::face_layout(m) == input_map::FaceLayout::kLabels ? 1.0f : 0.0f;
        };
        it.set = [](float v) {
            change_mapping([v](M& m) {
                const int i = int(std::lround(v));
                if (i == 2) return input_map::set_face_auto(m, true);
                if (m.face_auto) input_map::set_face_auto(m, false);
                input_map::apply_face_layout(m, i == 1 ? input_map::FaceLayout::kLabels : input_map::FaceLayout::kPosition);
            });
        };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.gamecube", it.tab = "controls", it.label = "GameCube layout";
        it.help = "Controller in use: original GameCube buttons; off restores its previous bindings. Start + D-pad for HD extras.";
        it.get = [] { return input_map::active_view(input_map::current()).preset.gamecube ? 1.f : 0.f; };
        it.set = [](float v) {
            auto m = input_map::current();
            std::string guid, name;input_map::active_controller(guid,name);
            int index = guid.empty() && name.empty() ? -1 : input_map::ensure_profile(m,guid,name);
            auto view = input_map::view_for(m,index);
            bool legacy = false;
            for (const auto& c : input::controllers())
                if (c.guid == guid && c.name == name) legacy = input_map::legacy_gamecube_faces(c);
            if (v > .5f) input_map::apply_gamecube(view,legacy);
            else input_map::restore_pad_preset(view);
            input_map::store_view(m,view,index);input_map::set_current(m);
        };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.prompts", it.tab = "controls", it.label = "Button prompts", it.kind = Kind::Choice;
        it.help = "How the game's button icons look: automatic follows the connected pad.";
        for (M::Prompts p : {M::Prompts::kAuto, M::Prompts::kOriginal, M::Prompts::kXbox, M::Prompts::kPlayStation})
            it.choices.push_back(capital(input_map::prompts_label(p)));
        it.get = [] { return float(int(input_map::current().prompts)); };
        it.set = [](float v) { change_mapping([v](M& m) { m.prompts = M::Prompts(std::clamp(int(std::lround(v)), 0, 3)); }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.shoulder-names", it.tab = "controls", it.label = "Shoulder names", it.kind = Kind::Choice;
        it.help = "How prompts name the shoulder buttons: L / R, L1 / R1 or LB / RB.";
        for (M::ShoulderNames n : {M::ShoulderNames::kAuto, M::ShoulderNames::kNintendo, M::ShoulderNames::kNumbered, M::ShoulderNames::kXbox})
            it.choices.push_back(capital(input_map::shoulder_names_label(n)));
        it.get = [] { return float(int(input_map::current().shoulder_names)); };
        it.set = [](float v) { change_mapping([v](M& m) { m.shoulder_names = M::ShoulderNames(std::clamp(int(std::lround(v)), 0, 3)); }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.prompt-colours", it.tab = "controls", it.label = "Coloured face buttons", it.kind = Kind::Toggle;
        it.help = "Xbox and PlayStation prompts in their buttons' colours.";
        it.get = [] { return input_map::current().prompt_colours ? 1.0f : 0.0f; };
        it.set = [](float v) { change_mapping([v](M& m) { m.prompt_colours = v >= 0.5f; }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.prompt-text-colours", it.tab = "controls", it.label = "Coloured face button text", it.kind = Kind::Toggle;
        it.help = "Xbox and PlayStation action words in their button's colour; off keeps the game's white.";
        it.get = [] { return input_map::current().prompt_text_colours ? 1.0f : 0.0f; };
        it.set = [](float v) { change_mapping([v](M& m) { m.prompt_text_colours = v >= 0.5f; }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.rumble", it.tab = "controls", it.label = "Rumble", it.kind = Kind::Toggle;
        it.help = "The controller rumbles when the game asks for it.";
        it.get = [] { return rumble::enabled() ? 1.0f : 0.0f; };
        it.set = [](float v) { set_rumble(v >= 0.5f); };
        it.enabled = [] { return input::has_rumble(); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.deadzone", it.tab = "controls", it.label = "Stick dead zone", it.kind = Kind::Number;
        it.help = "How far a stick moves before the game sees it.";
        it.min = 0.0f, it.max = 0.5f, it.step = 0.05f;
        it.get = [] { return input_map::current().deadzone; };
        it.set = [](float v) { change_mapping([v](M& m) { m.deadzone = v; }); };
        it.format = percent;
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.skip-controller-selection", it.tab = "controls", it.label = "Skip controller selection", it.kind = Kind::Toggle;
        it.help = "At the title screen, choose the configured Wii U controller automatically.";
        it.get = [] { return input_map::current().skip_controller_selection ? 1.0f : 0.0f; };
        it.set = [](float v) { change_mapping([v](M& m) { m.skip_controller_selection = v >= 0.5f; }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.l-turns-page", it.tab = "controls", it.label = "L turns the GamePad page", it.kind = Kind::Toggle;
        it.help = "On: in play L turns the GamePad screen between Items and Map, as on the Wii U. Off: only in menus, so L1 can jump.";
        it.get = [] { return input_map::current().l_turns_page ? 1.0f : 0.0f; };
        it.set = [](float v) { change_mapping([v](M& m) { m.l_turns_page = v >= 0.5f; }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.talk-touch", it.tab = "controls", it.kind = Kind::Toggle;
#ifdef __ANDROID__
        it.label = "Talks and shops by touch";
        it.help = "Tap to go on, hold to go on by itself, tap an answer or a shop's item.";
#else
        it.label = "Talks and shops by mouse";
        it.help = "Click to go on, hold to go on by itself, click an answer or a shop's item.";
#endif
        it.get = [] { return mods::touch_talk_on() ? 1.0f : 0.0f; };
        it.set = [](float v) { mods::set_touch_talk_on(v >= 0.5f); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "controls.pad-hold-a", it.tab = "controls", it.label = "Hold A to go on in talks", it.kind = Kind::Toggle;
        it.help = "Keep A held and the text goes on by itself; every question waits for a new press.";
        it.get = [] { return mods::pad_hold_a_on() ? 1.0f : 0.0f; };
        it.set = [](float v) { mods::set_pad_hold_a_on(v >= 0.5f); };
        add(std::move(it));
    }
#ifdef __ANDROID__
    {
        Item it;
        it.id = "controls.rumble-on", it.tab = "controls", it.label = "Rumble on", it.kind = Kind::Choice;
        it.help = "Automatic: this device while you play by touch, the controller's motors otherwise.";
        for (int m = 0; m < phone_rumble::kModeCount; m++) it.choices.push_back(phone_rumble::label(phone_rumble::Mode(m)));
        it.get = [] { return float(phone_rumble::choice.load()); };
        it.set = [](float v) {
            const int mode = std::clamp(int(v + 0.5f), 0, phone_rumble::kModeCount - 1);
            phone_rumble::choice = phone_rumble::Mode(mode);
            hostui::set("phoneRumble", std::to_string(mode));
        };
        add(std::move(it));
    }
#endif
}
void register_screen() {
    {
        Item it;
        it.id = "screen.large-picto", it.tab = "screen", it.label = "Large Picto Box view", it.kind = Kind::Toggle;
        it.help = "The finder fills the screen; the photo is what the corner marks frame.";
        it.get = [] { return mods::large_picto_on() ? 1.0f : 0.0f; };
        it.set = [](float v) { mods::set_large_picto_on(v >= 0.5f); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "screen.fov", it.tab = "screen", it.label = "Gameplay field of view", it.kind = Kind::Number;
        it.help = "100% is the original. Wider shows more of the world; cutscenes keep theirs.";
        it.min = 0.75f, it.max = 2.5f, it.step = 0.05f;
        it.get = [] { return aspect::fov_scale(); };
        it.set = [](float v) { set_fov_scale(v); };
        it.format = percent;
        add(std::move(it));
    }
    {
        Item it;
        it.id = "screen.boost-bar", it.tab = "screen", it.label = "Show boost bar", it.kind = Kind::Toggle;
        it.help = "The run/swim speed boost's stamina bar.";
        it.get = [] { return mods::move_speed_bar() ? 1.0f : 0.0f; };
        it.set = [](float v) { set_move_speed_bar(v >= 0.5f); };
        it.enabled = [] { return mods::move_speed(); };
        add(std::move(it));
    }
}
// The Settings window's main graphics options (overlay.cpp tab_graphics), through the same setters; like its
// post_changed, each change then has the host save the graphics options (hostui::graphics_changed)
void register_graphics() {
    namespace g = game_settings_graphics;
    static constexpr auto saved = [](auto fn) {
        fn();
        hostui::graphics_changed();
    };
    auto backend = [] { return render::g_backend != nullptr; };
    {
        Item it;
        it.id = "graphics.resolution", it.tab = "graphics", it.label = "Internal resolution", it.kind = Kind::Choice;
        it.help = "How sharp the game is drawn. Higher needs a faster graphics chip.";
        it.choices.assign(std::begin(g::kScaleNames), std::end(g::kScaleNames));
        it.get = [] { return float(g::scale_choice(hostui::res_scale())); };
        it.set = [](float v) {
            const float s = g::kScales[std::clamp(int(std::lround(v)), 0, int(g::kScaleCount) - 1)];
            saved([s] { hostui::set_res_scale(s); });
        };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.frame-rate", it.tab = "graphics", it.label = "Frame rate", it.kind = Kind::Choice;
        it.help = "40 fps and more add in-between frames; the game logic keeps its 30 steps. True 60 runs it at 60.";
        it.choices.assign(std::begin(g::kRateNames), std::end(g::kRateNames));
        it.get = [] { return float(g::rate_choice(interp::mode(), interp::fps())); };
        it.set = [](float v) {
            const g::Rate r = g::rate_of(int(std::lround(v)));
            saved([r] {
                if (r.fps) interp::set_fps(r.fps);
                interp::set_mode(r.mode);
            });
        };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.dynamic-resolution", it.tab = "graphics", it.label = "Dynamic resolution", it.kind = Kind::Choice;
        it.help = "Lowers the resolution while the graphics chip cannot keep up, and raises it again when there is room.";
        it.choices.assign(std::begin(g::kDynNames), std::end(g::kDynNames));
        it.get = [] { return float(g::dyn_choice(dynres::min_factor())); };
        it.set = [](float v) {
            const float m = g::kDynMins[std::clamp(int(std::lround(v)), 0, 2)];
            saved([m] {
                dynres::set_min(m);
                hostui::set("dynResMin", m > 0 ? std::to_string(m) : "0");
            });
        };
        it.enabled = [] { return !dynres::min_from_env(); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.bloom", it.tab = "graphics", it.label = "Bloom", it.kind = Kind::Number;
        it.help = "Glow around bright areas. 100% is the original; 0% turns it off.";
        it.min = 0.0f, it.max = 2.0f, it.step = 0.25f;
        it.get = [] { return render::bloom_strength(); };
        it.set = [](float v) { saved([v] { render::set_bloom_strength(v); }); };
        it.format = percent;
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.fxaa", it.tab = "graphics", it.label = "Edge smoothing (FXAA)", it.kind = Kind::Toggle;
        it.help = "Softens jagged edges, at a small cost in sharpness.";
        it.get = [backend] { return backend() && render::fxaa() ? 1.0f : 0.0f; };
        it.set = [](float v) { saved([v] { render::set_fxaa(v >= 0.5f); }); };
        it.enabled = [backend] { return backend() && render::feature_available(render::kFeatureFXAA); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.aniso", it.tab = "graphics", it.label = "Anisotropic filtering (16x)", it.kind = Kind::Toggle;
        it.help = "Sharper ground and walls seen at a slant or far away.";
        it.get = [backend] { return backend() && render::aniso() ? 1.0f : 0.0f; };
        it.set = [](float v) { saved([v] { render::set_aniso(v >= 0.5f); }); };
        it.enabled = [backend] { return backend() && render::feature_available(render::kFeatureAniso); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.clouds", it.tab = "graphics", it.label = "Clouds", it.kind = Kind::Choice;
        it.help = "Original keeps the painted clouds; Toon draws puffy cel-shaded ones. Lite is lighter on the graphics chip.";
        it.choices.assign(std::begin(g::kCloudNames), std::end(g::kCloudNames));
        it.get = [] { return float(std::clamp(render::cloud_style(), 0, 2)); };
        it.set = [](float v) { saved([v] { render::set_cloud_style(std::clamp(int(std::lround(v)), 0, 2)); }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.cloud-shadows", it.tab = "graphics", it.label = "Cloud shadows", it.kind = Kind::Toggle;
        it.help = "Soft shadows of the drifting clouds on the sea, islands and characters.";
        it.get = [] { return render::cloud_shadows() ? 1.0f : 0.0f; };
        it.set = [](float v) { saved([v] { render::set_cloud_shadows(v >= 0.5f); }); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "graphics.full-light", it.tab = "graphics", it.label = "Full-size torchlight", it.kind = Kind::Toggle;
        it.help = "Torches and other lights at full resolution; off: half size, as the original (blocky outlines).";
        it.get = [] { return render::full_light() ? 1.0f : 0.0f; };
        it.set = [](float v) { saved([v] { render::set_full_light(v >= 0.5f); }); };
        add(std::move(it));
    }
}
#ifdef __ANDROID__
void register_touch() {  // the Settings window's "Touch controls" (Android)
    {
        Item it;
        it.id = "screen.touch-buttons", it.tab = "screen", it.label = "Touch buttons", it.kind = Kind::Choice;
        it.help = "Automatic hides the floating buttons while a controller is connected.";
        it.choices = {"Automatic", "Always show", "Hide"};
        it.get = [] { return float(gfx::g_touch_buttons.load()); };
        it.set = [](float v) {
            gfx::g_touch_buttons = int(std::lround(v));
            hostui::set("touchButtons", std::to_string(int(std::lround(v))));
        };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "screen.touch-controls", it.tab = "screen", it.label = "On-screen controls", it.kind = Kind::Toggle;
        it.help = "The sticks and buttons drawn over the picture.";
        it.get = [] { return gfx::g_touch_controls.load() ? 1.0f : 0.0f; };
        it.set = [](float v) {
            gfx::g_touch_controls = v >= 0.5f;
            hostui::set("touchControls", v >= 0.5f ? "1" : "0");
        };
        add(std::move(it));
    }
    // touch_style.h (the Settings window's same options, saved by touch_options::set)
    using O = touch_style::Options;
    auto option = [](const char* id, const char* label, const char* help, Kind kind, std::function<float(const O&)> get,
                     std::function<void(O&, float)> put) {
        Item it;
        it.id = id, it.tab = "screen", it.label = label, it.help = help, it.kind = kind;
        it.get = [get] { return get(touch_options::get()); };
        it.set = [put](float v) {
            O o = touch_options::get();
            put(o, v);
            touch_options::set(o);
        };
        return it;
    };
    auto flexible = [] { return touch_options::get().style == touch_style::Flexible; };
    auto swiping = [] { const O o = touch_options::get(); return o.style == touch_style::Flexible && o.right == touch_style::RightSwipe; };
    {
        Item it = option("screen.touch-style", "Touch style", "Flexible: sticks appear where your thumbs land.", Kind::Choice,
                         [](const O& o) { return float(o.style); }, [](O& o, float v) { o.style = int(std::lround(v)); });
        it.choices = {"Classic", "Flexible"};
        add(std::move(it));
    }
    {
        Item it = option("screen.touch-right", "Right half", "Flexible: the right half turns the camera by swiping or as a stick.",
                         Kind::Choice, [](const O& o) { return float(o.right); }, [](O& o, float v) { o.right = int(std::lround(v)); });
        it.choices = {"Swipe camera", "Camera stick"};
        it.enabled = flexible;
        add(std::move(it));
    }
    {
        Item it = option("screen.touch-show-sticks", "Show sticks", "Flexible: off, the floating sticks work without being drawn.",
                         Kind::Toggle, [](const O& o) { return o.show_sticks ? 1.0f : 0.0f; }, [](O& o, float v) { o.show_sticks = v >= 0.5f; });
        it.enabled = flexible;
        add(std::move(it));
    }
    add(option("screen.touch-follow", "Left stick follows the thumb", "The stick comes along when your thumb slides past its edge.",
               Kind::Toggle, [](const O& o) { return o.follow ? 1.0f : 0.0f; }, [](O& o, float v) { o.follow = v >= 0.5f; }));
    {
        Item it = option("screen.touch-swipe-speed", "Swipe speed", "How far a swipe turns the camera.", Kind::Number,
                         [](const O& o) { return o.swipe_speed; }, [](O& o, float v) { o.swipe_speed = v; });
        it.min = mods::camera_swipe::kSpeedMin, it.max = mods::camera_swipe::kSpeedMax, it.step = 0.25f;
        it.format = percent;
        it.enabled = swiping;
        add(std::move(it));
    }
    {
        Item it = option("screen.touch-swipe-boat", "Swipe speed on the boat", "Swiping turns the camera this much faster while Link sails.",
                         Kind::Number, [](const O& o) { return o.swipe_boat; }, [](O& o, float v) { o.swipe_boat = v; });
        it.min = mods::camera_swipe::kBoatMin, it.max = mods::camera_swipe::kBoatMax, it.step = 0.25f;
        it.format = times;
        it.enabled = swiping;
        add(std::move(it));
    }
}
#endif
void register_game() {
    // the built-in gameplay mods (mods/manager.cpp), switched as the Mods tab switches them
    // (short names and help lines of the page's own, translated in game_settings_text.h; the Settings window
    // keeps the catalogue's longer descriptions)
    static const struct { const char *id, *label, *help; } kGame[] = {
        {"quick-doors", "Quick doors", "Doors open and close four times as fast."},
        {"fast-scenes", "Fast scene changes", "Shorter waits when the game loads a new place."},
        {"fast-forward", "Fast forward scenes and talks", "Hold the fast forward button to speed up cutscenes and dialogue."},
        {"jump-anywhere", "Jump anywhere", "A button of its own makes Link jump, standing or running."},
        {"wall-climb", "Climb any wall", "Grab and climb walls with a stamina wheel; A or B lets go."},
        {"move-speed", "Run/swim speed", "A boost for running and swimming, with a stamina bar."},
        {"lock-on", "Extended enemy lock-on", "Target enemies from further away; R3 cycles targets while locked on."},
        {"quick-use", "Quick-use locked items", "One press readies and uses an item against a locked enemy."},
        {"ladder-speed", "Faster ladders", "Link climbs ladders faster."},
        {"sidle-speed", "Faster sidling", "Link sidles along walls faster."},
        {"ivy-speed", "Faster ivy climbing", "Link climbs ivy and vines faster."},
        {"hang-speed", "Faster ledge hanging", "Link moves along ledges faster while hanging."},
        {"sail-speed", "Sailing speed", "The boat sails faster."},
        {"auto-wind", "Automatic wind", "The wind follows the boat with the normal sail too."},
    };
    for (const auto& g : kGame) {
        const auto* e = mods::manager::find(g.id);
        if (!e || e->restart_required) continue;  // (the page applies live only)
        add(builtin_toggle("game", g.id, g.label, g.help));
    }
}
// The options of the enabled mods that ask for the item menu ("menu": true, packages.h Option): the Mods
// tab, grouped by mod and section. Values are the manager's (packages::configure saves them in the profile,
// as the Mods tab does); the mod reads them with wwhd_config_*.
void register_mod_options() {
    std::vector<packages::View> views = packages::list();
    for (const packages::View& v : views) {
        if (!v.enabled) continue;
        std::vector<const packages::Option*> shown;
        for (const packages::Option& o : v.options)
            if (o.menu && (o.type == "bool" || o.type == "enum" || o.type == "number")) shown.push_back(&o);
        std::stable_sort(shown.begin(), shown.end(), [](const packages::Option* a, const packages::Option* b) { return a->section < b->section; });
        for (const packages::Option* o : shown) {
            Item it;
            it.id = "mod." + v.id + "." + o->id, it.tab = "mods", it.owner = v.id;
            it.label = o->name.empty() ? o->id : o->name;
            it.help = (o->section.empty() ? v.name : v.name + " / " + o->section) + (o->description.empty() ? "" : ": " + o->description);
            it.next_start = !o->live;
            const std::string mod = v.id, option = o->id, type = o->type;
            const std::vector<std::string> choices = o->choices;
            // the value as the manager has it now (kept here: list() is too heavy for every frame)
            auto value = std::make_shared<std::atomic<float>>(0.0f);
            const json::Value& cur = o->value;
            if (type == "bool") it.kind = Kind::Toggle, value->store(cur.type == json::Value::Bool && cur.boolean ? 1.0f : 0.0f);
            else if (type == "enum") {
                it.kind = Kind::Choice, it.choices = choices;
                const auto at = std::find(choices.begin(), choices.end(), cur.type == json::Value::String ? cur.text : std::string());
                value->store(at == choices.end() ? 0.0f : float(at - choices.begin()));
            } else {
                it.kind = Kind::Number, it.min = float(o->minimum), it.max = float(o->maximum), it.step = float(o->step);
                value->store(cur.type == json::Value::Number ? float(cur.number) : it.min);
            }
            it.get = [value] { return value->load(); };
            it.set = [value, mod, option, type, choices](float v) {
                json::Value j = type == "bool" ? json::Value(v >= 0.5f)
                              : type == "enum" ? json::Value(choices[size_t(std::clamp(int(std::lround(v)), 0, int(choices.size()) - 1))])
                                               : json::Value(double(v));
                std::string error;
                if (packages::configure(mod, option, j, error)) value->store(v);
                else LOG("[game-settings] %s.%s not changed: %s", mod.c_str(), option.c_str(), error.c_str());
            };
            add(std::move(it));
        }
    }
}
}  // namespace

void register_builtin() {
    static bool done = false;
    if (done) return;
    done = true;
    {
        Item it;
        it.id = "camera.invert-y", it.tab = "camera", it.label = "Invert camera up/down", it.kind = Kind::Toggle;
        it.help = "The camera, first person and aiming turn the other way up and down.";
        it.get = [] { return input_map::invert_camera_y() ? 1.0f : 0.0f; };
        it.set = [](float v) { set_invert_camera_y(v >= 0.5f); };
        add(std::move(it));
    }
    {
        Item it = builtin_toggle("camera", "direct-camera", "Direct camera",
                                 "The right stick turns the camera at once, without the game's easing.");
        add(std::move(it));
    }
    {
        Item it;
        it.id = "direct-camera.speed", it.tab = "camera", it.label = "Camera speed", it.kind = Kind::Number;
        it.help = "How fast the direct camera turns (1.00x: the game's top speed).";
        it.min = 0.5f, it.max = 2.0f, it.step = 0.25f;
        it.get = [] { return mods::camera_speed(); };
        it.set = [](float v) { set_camera_speed(v); };
        it.format = times;
        it.enabled = [] { return mods::direct_camera(); };
        add(std::move(it));
    }
    add(builtin_toggle("camera", "mouse-camera", "Mouse camera",
                       "Click the game picture to look around with the mouse; Esc lets it go."));
    {
        Item it;
        it.id = "mouse-camera.sensitivity", it.tab = "camera", it.label = "Mouse sensitivity", it.kind = Kind::Number;
        it.help = "How far the camera turns per mouse movement.";
        it.min = 0.08f, it.max = 0.30f, it.step = 0.02f;
        it.get = [] { return mods::mouse_sensitivity(); };
        it.set = [](float v) { set_mouse_sensitivity(v); };
        it.format = [](float v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.2f", v);
            return std::string(b);
        };
        it.enabled = [] { return mods::mouse_camera(); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "camera.walk-direction", it.tab = "camera", it.label = "Walking direction", it.kind = Kind::Choice;
        it.help = "Kept: Link walks on the way you pushed the stick while the camera turns.";
        it.choices = {"As the game", "Kept while you turn", "Kept while held"};
        it.get = [] { return float(mods::steer_mode()); };
        it.set = [](float v) { mods::set_steer_mode(int(std::lround(v))); };
        add(std::move(it));
    }
    {
        Item it;
        it.id = "camera.swing", it.tab = "camera", it.label = "Camera swings behind Link", it.kind = Kind::Toggle;
        it.help = "While he runs and you leave the camera alone, it turns to look over his shoulder.";
        it.get = [] { return mods::camera_swing() ? 1.0f : 0.0f; };
        it.set = [](float v) { mods::set_camera_swing(v >= 0.5f); };
        it.enabled = [] { return mods::steer_mode() != 0; };
        add(std::move(it));
    }
    add(builtin_toggle("camera", "first-person", "First person on R3 / wheel",
                       "R3 or the mouse wheel enters and leaves first person."));
    add(builtin_toggle("camera", "target-letterbox", "Letterbox while targeting",
                       "Black bars at the top and bottom while Link targets, as in the original."));
    register_controls();
    register_screen();
#ifdef __ANDROID__
    register_touch();
#endif
    register_graphics();
    register_game();
    register_mod_options();
}

}  // namespace game_settings
