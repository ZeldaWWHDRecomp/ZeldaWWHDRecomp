// SPDX-License-Identifier: MPL-2.0
// Settings > Controls > Set up controller: a short setup that gives one controller its own bindings
// (input_map profiles, controls.json format 2), offered once when a new controller is first used.
// Idea from the GreenNaugahyde Android fork (ControllerSetup.java), reimplemented as an ImGui dialog so
// it works with every host and controller (desktop, Steam Deck, Android pads).
//
// Steps:
//   offer       "New controller: <name>. Set up / Later / Don't ask again" (only when it was offered)
//   face        face buttons by position (as on the Wii U GamePad) or by the letters printed on them
//   layout      Original; Jump on an item button (that item is taken off the controller); Jump on any
//               button, what it did moves to L1 (Jump anywhere is switched on for the last two)
//   jump button press the button that should jump (the two jump layouts)
//   try it      every press shows what it does in the game; hold Start = Done, hold Select = Change,
//               so the setup finishes without touching the screen
// Pure parts (build, describe, the hold timers) and the dialog are in this header, so
// runtime/tools/controller_setup_test.cpp drives the same dialog offscreen. overlay/controller_setup.cpp
// connects it to the hosts (input::controllers(), the offer queue, input_map::set_current).
#pragma once
#include "binding_capture.h"
#include "popup.h"
#include "widgets.h"
#include "../input_map.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace overlay::controller_setup {
constexpr const char* kOfferTitle = "New controller##ctlsetup_offer";
constexpr const char* kTitle = "Set up controller##ctlsetup";
constexpr double kHoldSeconds = 1.0;  // hold Start (Done) or Select (Change) on the Try it screen

enum class Step { kClosed, kOffer, kFace, kLayout, kJumpButton, kTry };
enum class Face { kPosition, kLetters };
enum class Layout { kOriginal, kJumpOnItem, kJumpAnywhere };
enum class Answer { kNone, kLater, kNeverAsk, kDone, kCancel };

// ---- pure parts ----

struct Result {
    input_map::Mapping mapping;
    bool jump_on = false;  // switch Jump anywhere on (the two jump layouts)
};

// The controller inputs that are item buttons in a fresh assignment with these face buttons (the
// choices of "Jump on an item button": X, Y and R)
inline std::array<int, 3> item_inputs(const std::array<int, input_map::kActionCount>& pad) {
    return {pad[input_map::kX], pad[input_map::kY], pad[input_map::kR]};
}

// The face bindings for `face` on a pad with these printed letters (labels as input_map::FaceLabel in
// the Pad order south, east, west, north; unknown: the Xbox convention for letters)
inline void apply_face(input_map::Mapping& v, Face face, const input_map::FaceLabel labels[4]) {
    using namespace input_map;
    if (face == Face::kPosition) {
        apply_face_layout(v, FaceLayout::kPosition);
        return;
    }
    int out[4];
    if (labels && face_bindings_from_labels(labels, out)) {
        for (int i = 0; i < 4; i++) v.pad[kA + i] = out[i];
        v.face_auto = false;
    } else {
        apply_face_layout(v, FaceLayout::kLabels);
    }
}

// The assignment the setup makes: the controller gets its own bindings, freshly made (the defaults
// with the chosen face buttons), then the layout. jump_pad: the button chosen for jumping.
inline Result build(const input_map::Mapping& m, const std::string& guid, const std::string& name, Face face,
                    Layout layout, int jump_pad, const input_map::FaceLabel labels[4] = nullptr) {
    using namespace input_map;
    Result r;
    r.mapping = m;
    const int i = ensure_profile(r.mapping, guid, name);
    Mapping v = view_for(r.mapping, i);
    v.pad = Mapping::defaults().pad;
    apply_face(v, face, labels);
    if (layout != Layout::kOriginal && jump_pad > kPadNone && jump_pad < kPadCount) {
        r.jump_on = true;
        if (layout == Layout::kJumpOnItem) {
            bind_virtual(v, kJump, jump_pad, Displace::kTakeOff);
        } else if (!bind_virtual(v, kJump, jump_pad, Displace::kMoveToL1) &&
                   !bind_virtual(v, kJump, jump_pad, Displace::kMove)) {
            bind_virtual(v, kJump, jump_pad, Displace::kTakeOff);
        }
    }
    store_view(r.mapping, v, i);
    return r;
}

// What the game button does, in a word or two (the Try it screen)
inline const char* action_use(int a) {
    using namespace input_map;
    switch (a) {
    case kA: return "action, talk";
    case kB: return "sword, back";
    case kX: case kY: case kR: return "item button";
    case kL: return "turns the GamePad page (menus)";
    case kZL: return "target, shield";
    case kZR: return "crouch, Picto Box";
    case kPlus: return "item menu";
    case kMinus: return "map";
    case kHome: return "Settings";
    case kDUp: return "Wind Waker";
    case kDDown: return "d-pad down";
    case kDLeft: case kDRight: return "d-pad";
    case kStickLClick: return "left stick click";
    case kStickRClick: return "first person";
    case kJump: return "jumps (Jump anywhere)";
    case kMoonJump: return "moon jump (when on)";
    default: return "";
    }
}

// Everything one controller input does with these bindings: "Wii U B: sword, back", one line each
inline std::vector<std::string> describe(const std::array<int, input_map::kActionCount>& pad, int input) {
    using namespace input_map;
    std::vector<std::string> out;
    if (input <= kPadNone) return out;
    for (int a = 0; a < kActionCount; a++) {
        if (pad[a] != input || a == kScreenshot) continue;
        if (a >= kLUp && a <= kRRight) continue;  // the sticks: shown as sticks
        const char* use = action_use(a);
        if (is_virtual(a)) out.push_back(std::string("Jump: ") + use);
        else out.push_back(std::string("Wii U ") + action_label(a) + (*use ? std::string(": ") + use : std::string()));
    }
    if (out.empty()) out.push_back("nothing in the game");
    return out;
}

// Hold Start (Done) or Select (Change) for kHoldSeconds; once per hold
struct Hold {
    double start_since = -1, select_since = -1;
    bool latched = false;
    enum Result { kNone, kDone, kChange };
    Result poll(bool start, bool select, double now) {
        if (!start && !select) latched = false;
        start_since = start ? (start_since < 0 ? now : start_since) : -1;
        select_since = select ? (select_since < 0 ? now : select_since) : -1;
        if (latched) return kNone;
        if (start_since >= 0 && now - start_since >= kHoldSeconds) { latched = true; return kDone; }
        if (select_since >= 0 && now - select_since >= kHoldSeconds) { latched = true; return kChange; }
        return kNone;
    }
    float progress(bool start_button, double now) const {
        const double since = start_button ? start_since : select_since;
        return since < 0 || latched ? 0.f : float(std::min(1.0, (now - since) / kHoldSeconds));
    }
};

// ---- the dialog ----

struct State {
    Step step = Step::kClosed;
    bool open_now = false;     // the dialog opens this frame
    std::string guid, name;    // the controller being set up
    Face face = Face::kPosition;
    Layout layout = Layout::kOriginal;
    int jump_pad = input_map::kPadNone;
    BindingCapture capture;
    Hold hold;
    Result result;             // what Done applies (made when Try it opens)
    int last_input = input_map::kPadNone;  // the last pressed input on Try it
    void begin(const std::string& g, const std::string& n, bool offer) {
        *this = State{};
        guid = g;
        name = n;
        step = offer ? Step::kOffer : Step::kFace;
        open_now = true;
    }
    bool active() const { return step != Step::kClosed; }
    // the steps where the controller's own presses are the answer (no menu navigation, no shortcuts)
    bool takes_controller() const { return step == Step::kJumpButton || step == Step::kTry; }
};

struct Io {
    const float* values = nullptr;  // the controller's inputs (input_map::kPadCount), nullptr: not connected
    const input_map::FaceLabel* labels = nullptr;  // its printed letters (south, east, west, north)
    double now = 0;
    bool back = false;              // controller B / Back this frame (closes the offer and the first steps)
    bool touch = false;             // a touch screen: wider buttons
};

// where each control of the dialogs landed (controller_setup_test clicks them); nullptr: not recorded
struct Marks {
    std::vector<std::pair<std::string, ImRect>> items;
    const ImRect* find(const std::string& label) const {
        for (const auto& [l, r] : items)
            if (l == label) return &r;
        return nullptr;
    }
};
inline Marks*& marks() {
    static Marks* m = nullptr;
    return m;
}

namespace detail {
inline void mark(const char* label) {
    if (Marks* m = marks()) m->items.emplace_back(label, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
}
// a footer button: up to 9 font sizes wide, three to a line however narrow the screen
inline float footer_width() {
    const float avail = ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x;
    return std::max(ImGui::GetFontSize() * 4, std::min(ImGui::GetFontSize() * 9, avail / 3));
}
inline bool wide_button(const char* label, bool nav = true, float width = 0) {
    if (!nav) ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool r = ImGui::Button(label, ImVec2(width > 0 ? width : ImGui::GetFontSize() * 9, 0));
    if (!nav) ImGui::PopItemFlag();
    mark(label);
    return r;
}
inline bool radio(const char* label, bool active) {
    const bool r = ImGui::RadioButton(label, active);
    mark(label);
    return r;
}
inline void hold_bar(const char* label, float progress) {
    ImGui::ProgressBar(progress, ImVec2(ImGui::GetFontSize() * 9, ImGui::GetFrameHeight() * 0.5f), "");
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}
}  // namespace detail

// One frame. m: the live mapping (the starting point); returns the answer of this frame. kDone: apply
// s.result (the caller saves it and switches Jump anywhere on when result.jump_on).
inline Answer frame(State& s, const input_map::Mapping& m, const Io& io) {
    using namespace widgets;
    using namespace input_map;
    Answer answer = Answer::kNone;
    if (s.step == Step::kOffer) {
        if (popup::begin(kOfferTitle, s.open_now)) {
            s.open_now = false;
            ImGui::PushTextWrapPos(popup::wrap_pos(28));
            ImGui::Text("%s", s.name.empty() ? "A new controller" : s.name.c_str());
            note("Set it up now? A short setup chooses its face buttons and where Jump anywhere goes; this "
                 "controller then keeps its own buttons. Settings > Controls > Set up controller does it any time. "
                 "Settings open with Home or by holding Select.");
            ImGui::PopTextWrapPos();
            const float w = detail::footer_width();
            if (detail::wide_button("Set up", true, w)) { s.step = Step::kFace; s.open_now = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (detail::wide_button("Later", true, w) || popup::dismissed(io.back)) { answer = Answer::kLater; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (detail::wide_button("Don't ask", true, w)) { answer = Answer::kNeverAsk; ImGui::CloseCurrentPopup(); }
            popup::end();
        }
        if (answer != Answer::kNone) s.step = Step::kClosed;
        return answer;
    }
    if (s.step == Step::kClosed) return answer;
    const bool own = s.takes_controller();
    if (!popup::begin(kTitle, s.open_now, ImGui::GetFontSize() * 26, !own)) return answer;
    s.open_now = false;
    popup::body_begin();
    ImGui::PushTextWrapPos(popup::wrap_pos(30));
    ImGui::TextDisabled("%s", s.name.c_str());
    bool next = false, back_step = false;
    switch (s.step) {
    case Step::kFace:
        ImGui::TextUnformatted("Face buttons");
        note("The game knows only the Wii U GamePad: each of its buttons is pressed by one of yours.");
        if (detail::radio("By position, as on the Wii U GamePad", s.face == Face::kPosition)) s.face = Face::kPosition;
        note("The bottom button is the sword (the Wii U's B), the right one the action button (A).");
        if (detail::radio("By the letters on the buttons", s.face == Face::kLetters)) s.face = Face::kLetters;
        note("The button marked A is the action button, B the sword, X and Y item buttons.");
        break;
    case Step::kLayout:
        ImGui::TextUnformatted("Layout");
        if (detail::radio("Original", s.layout == Layout::kOriginal)) s.layout = Layout::kOriginal;
        note("Every button as on the Wii U. Jump anywhere and the moon jump (when on) on L1, which the game "
             "doesn't use while you play.");
        if (detail::radio("Jump on an item button", s.layout == Layout::kJumpOnItem)) s.layout = Layout::kJumpOnItem;
        note("Jump anywhere on one of the three item buttons. That item can't be used from the controller; "
             "the other two can.");
        if (detail::radio("Jump on any button, its use moved to L1", s.layout == Layout::kJumpAnywhere))
            s.layout = Layout::kJumpAnywhere;
        note("Jump anywhere on a button of your choice (a face button, for example). What it did moves to L1, "
             "so nothing is lost. Both jump layouts switch Jump anywhere on.");
        break;
    case Step::kJumpButton: {
        ImGui::TextUnformatted(s.layout == Layout::kJumpOnItem ? "Press the item button that should jump"
                                                               : "Press the button that should jump");
        Mapping fresh = view_for(m, -1);
        fresh.pad = Mapping::defaults().pad;
        apply_face(fresh, s.face, io.labels);
        const auto items = item_inputs(fresh.pad);
        if (s.layout == Layout::kJumpOnItem) {
            std::string list;
            for (int p : items) list += std::string(list.empty() ? "" : ", ") + pad_label(p);
            note("Item buttons: %s.", list.c_str());
        }
        if (!io.values) note("The controller is not connected.");
        if (io.values) {
            const int p = s.capture.poll(io.values);
            bool ok = p > kPadNone;
            if (ok && s.layout == Layout::kJumpOnItem) ok = p == items[0] || p == items[1] || p == items[2];
            if (ok && (p == kPadMenu || p == kPadOptions || p == kPadHome)) ok = false;  // Start, Select, Home stay theirs
            if (p > kPadNone && !ok) s.capture.reset();
            if (ok) { s.jump_pad = p; next = true; }
        }
        break;
    }
    case Step::kTry: {
        ImGui::TextUnformatted("Try it");
        note("Press any button: it shows what it does in the game. Hold Start to finish, hold Select to change "
             "the setup.");
        const auto& pads = s.result.mapping.controllers[profile_index(s.result.mapping, s.guid, s.name)].pad;
        if (io.values) {
            for (int p = 1; p < kPadCount; p++)
                if (io.values[p] > 0.6f && p != kPadMenu && p != kPadOptions) s.last_input = p;
        } else {
            note("The controller is not connected.");
        }
        if (s.last_input > kPadNone) {
            ImGui::Text("%s", pad_label(s.last_input));
            for (const std::string& line : describe(pads, s.last_input)) ImGui::BulletText("%s", line.c_str());
        } else {
            ImGui::TextDisabled("(press a button)");
        }
        const bool start = io.values && io.values[kPadMenu] > 0.5f, select = io.values && io.values[kPadOptions] > 0.5f;
        const Hold::Result h = s.hold.poll(start, select, io.now);
        detail::hold_bar("hold Start: Done", s.hold.progress(true, io.now));
        detail::hold_bar("hold Select: Change", s.hold.progress(false, io.now));
        if (h == Hold::kDone) next = true;
        if (h == Hold::kChange) back_step = true;
        break;
    }
    default: break;
    }
    ImGui::PopTextWrapPos();
    popup::body_end();
    // the footer: Back / Next (Done on Try it), Cancel. On the controller's own steps they take no
    // controller navigation (its presses are the answer there): touch, mouse or the holds.
    const float w = detail::footer_width();
    if (s.step != Step::kFace && detail::wide_button(s.step == Step::kTry ? "Change" : "Back", !own, w)) back_step = true;
    if (s.step != Step::kFace) ImGui::SameLine();
    if (s.step != Step::kJumpButton && detail::wide_button(s.step == Step::kTry ? "Done" : "Next", !own, w)) next = true;
    if (s.step != Step::kJumpButton) ImGui::SameLine();
    if (detail::wide_button("Cancel", !own, w) || popup::dismissed(io.back)) answer = Answer::kCancel;
    if (answer == Answer::kCancel) {
        s.step = Step::kClosed;
    } else if (back_step) {
        // Change (Try it) starts over; Back goes one step back
        s.step = s.step == Step::kJumpButton ? Step::kLayout : Step::kFace;
        s.hold = {};
    } else if (next) {
        switch (s.step) {
        case Step::kFace: s.step = Step::kLayout; break;
        case Step::kLayout:
            s.capture.reset();
            s.step = s.layout == Layout::kOriginal ? Step::kTry : Step::kJumpButton;
            break;
        case Step::kJumpButton: s.step = Step::kTry; break;
        case Step::kTry: answer = Answer::kDone; s.step = Step::kClosed; break;
        default: break;
        }
        if (s.step == Step::kTry) {
            s.result = build(m, s.guid, s.name, s.face, s.layout, s.jump_pad, io.labels);
            s.hold = {};
            s.last_input = kPadNone;
        }
    }
    if (s.step == Step::kClosed) ImGui::CloseCurrentPopup();
    popup::end();
    return answer;
}
// ---- binding Jump or the moon jump to a game button: what happens to the button's own use ----
constexpr const char* kDisplaceTitle = "That button is in use##displace";

struct Displacing {
    int action = -1;                    // input_map::kJump or kMoonJump
    int pad = input_map::kPadNone;      // the controller input chosen for it
    bool open_now = false;
    void ask(int a, int p) { action = a; pad = p; open_now = true; }
    bool active() const { return action >= 0; }
};

// Bind at once when nothing is displaced (true); else open the question (false)
inline bool bind_or_ask(Displacing& d, input_map::Mapping& view, int action, int pad) {
    if (input_map::displaced_by(view, pad).empty()) {
        input_map::bind_virtual(view, action, pad, input_map::Displace::kTakeOff);
        return true;
    }
    d.ask(action, pad);
    return false;
}

// The question; true when `view` changed (bound). back: controller B (Cancel).
inline bool displace_dialog(Displacing& d, input_map::Mapping& view, bool back) {
    using namespace input_map;
    using namespace widgets;
    bool changed = false;
    if (!popup::begin(kDisplaceTitle, d.open_now)) return false;
    d.open_now = false;
    const std::vector<int> users = displaced_by(view, d.pad);
    ImGui::PushTextWrapPos(popup::wrap_pos(28));
    std::string who;
    for (int a : users) who += std::string(who.empty() ? "" : ", ") + "the Wii U's " + action_label(a);
    ImGui::Text("%s is %s.", pad_label(d.pad), who.c_str());
    note("What should it do now that %s takes this button?", action_label(d.action));
    ImGui::PopTextWrapPos();
    bool done = false;
    auto choice = [&](const char* label, Displace how, const char* tip) {
        Mapping trial = view;
        const bool ok = bind_virtual(trial, d.action, d.pad, how);
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 14, 0))) {
            view = trial;
            changed = done = true;
        }
        detail::mark(label);
        ImGui::EndDisabled();
        help(tip);
    };
    const bool item = users.size() == 1 && is_item_action(users[0]);
    if (item) {
        choice("Move to L1", Displace::kMoveToL1,
               "The item button goes to L1 with its item; L1 then no longer turns the GamePad page.");
        choice("Move to d-pad down", Displace::kMoveToDDown,
               "The item button goes to d-pad down while you play; in menus d-pad down stays d-pad down.");
    } else {
        const int to = users.size() == 1 ? move_target(view, users[0]) : kPadNone;
        const std::string label = std::string("Move") + (to != kPadNone ? std::string(" (to ") + pad_short_label(to) + ")" : "");
        choice(label.c_str(), Displace::kMove, "It goes to a button the game doesn't use while you play (L1, else d-pad down).");
    }
    choice("Take off", Displace::kTakeOff, item ? "That item can't be used from the controller; the other two can."
                                                : "No controller button presses it any more.");
    const bool cancel = ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 14, 0));
    detail::mark("Cancel");
    if (cancel || popup::dismissed(back)) done = true;
    if (done) {
        d = Displacing{};
        ImGui::CloseCurrentPopup();
    }
    popup::end();
    return changed;
}
}  // namespace overlay::controller_setup
