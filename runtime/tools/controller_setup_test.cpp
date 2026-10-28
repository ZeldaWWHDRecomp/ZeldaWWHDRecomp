// The controller setup (overlay/controller_setup.h), the Move / Take off question and Controller details
// (overlay/controller_details.h), offscreen with real ImGui frames as settings_popup_ui_test does; no
// window, audio or game files.
//  - the pure parts: the assignments each face choice and layout make, what a button does (Try it),
//    the Start / Select holds;
//  - the whole flow by touch (clicks) and by controller alone (the controller's own presses, the holds):
//    offer -> face buttons -> layout -> the jump button -> Try it -> Done; Later, Don't ask again, Change,
//    Cancel; the setup takes no Start / Select / Home as the jump button and only an item button for
//    "Jump on an item button";
//  - the question when Jump takes a game button (Move to L1 / d-pad down / Take off / Cancel);
//  - Controller details and the kernel-driver explanation (Android);
//  - every dialog on screen at phone, handheld and desktop sizes.
// Optional argument: a folder for BMP pictures of the steps.
#include "overlay/controller_setup.h"
#include "overlay/controller_details.h"
#include "overlay/mobile_ui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>

using namespace input_map;
namespace cs = overlay::controller_setup;
namespace cd = overlay::controller_details;

namespace {
struct Case { const char* name; int w, h; float dpi; };

struct Ui {
    Case c;
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool android = false;
    float scale = 1;
    explicit Ui(const Case& cs_) : c(cs_) {
        android = c.dpi > 0;
        mobile_ui::Layout layout;
        if (android) layout = mobile_ui::layout(c.dpi, c.dpi / 160, mobile_ui::kAutomatic, float(c.w), float(c.h));
        scale = android ? layout.scale : mobile_ui::desktop_scale(2, float(c.h));
        surface = SDL_CreateSurface(c.w, c.h, SDL_PIXELFORMAT_RGBA32);
        renderer = SDL_CreateSoftwareRenderer(surface);
        assert(surface && renderer);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = ImVec2(c.w / scale, c.h / scale);
        io.DeltaTime = 1.f / 60;
        SDL_SetRenderScale(renderer, scale, scale);
        ImGui::StyleColorsDark();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowPadding = ImVec2(16, 14); s.FramePadding = ImVec2(9, 5); s.ItemSpacing = ImVec2(10, 7);
        if (android) mobile_ui::apply_touch_style(s, layout);
        const char* fonts[] = {"/System/Library/Fonts/Supplemental/Arial.ttf", "C:/Windows/Fonts/arial.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};
        bool font = false;
        for (const char* f : fonts)
            if (!font && std::filesystem::exists(f)) font = io.Fonts->AddFontFromFileTTF(f, mobile_ui::kText) != nullptr;
        if (!font) { ImFontConfig cfg; cfg.SizePixels = mobile_ui::kText; io.Fonts->AddFontDefaultVector(&cfg); }
        s.FontSizeBase = mobile_ui::kText;
        assert(ImGui_ImplSDLRenderer3_Init(renderer));
    }
    ~Ui() {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
    void frame(const std::function<void()>& draw) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 23, 29, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    void save(const char* dir, const std::string& what) {
        if (!dir) return;
        std::filesystem::create_directories(dir);
        assert(SDL_SaveBMP(surface, (std::filesystem::path(dir) / (std::string(c.name) + "-" + what + ".bmp")).string().c_str()));
    }
};

bool up(const char* title) {
    for (const ImGuiPopupData& p : ImGui::GetCurrentContext()->OpenPopupStack)
        if (p.Window && !strcmp(p.Window->Name, title)) return true;
    return false;
}
// the dialog's window is whole on screen
void on_screen(const char* title) {
    ImGuiWindow* w = ImGui::FindWindowByName(title);
    assert(w);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const ImRect r = w->Rect();
    assert(r.Min.x >= -0.5f && r.Min.y >= -0.5f && r.Max.x <= ds.x + 0.5f && r.Max.y <= ds.y + 0.5f);
}

// ---- the pure parts ----

void pure() {
    const Mapping m = Mapping::defaults();
    const FaceLabel xbox[4] = {FaceLabel::kA, FaceLabel::kB, FaceLabel::kX, FaceLabel::kY};
    const FaceLabel nintendo[4] = {FaceLabel::kB, FaceLabel::kA, FaceLabel::kY, FaceLabel::kX};
    // Original by position: a profile with the defaults; the shared bindings untouched, Jump stays off
    auto r = cs::build(m, "g1", "Pad", cs::Face::kPosition, cs::Layout::kOriginal, kPadNone, xbox);
    assert(r.mapping.controllers.size() == 1 && r.mapping.controllers[0].pad == Mapping::defaults().pad && !r.jump_on);
    assert(r.mapping.pad == m.pad);
    // by letters: an Xbox pad's bottom A is the Wii U's A; a Nintendo pad's bottom B stays B (as by position)
    r = cs::build(m, "g1", "Pad", cs::Face::kLetters, cs::Layout::kOriginal, kPadNone, xbox);
    assert(r.mapping.controllers[0].pad[kA] == kPadA && r.mapping.controllers[0].pad[kB] == kPadB);
    r = cs::build(m, "g1", "Pad", cs::Face::kLetters, cs::Layout::kOriginal, kPadNone, nintendo);
    assert(r.mapping.controllers[0].pad[kA] == kPadB && r.mapping.controllers[0].pad[kB] == kPadA);
    r = cs::build(m, "g1", "Pad", cs::Face::kLetters, cs::Layout::kOriginal, kPadNone, nullptr);  // unknown: Xbox
    assert(r.mapping.controllers[0].pad[kA] == kPadA);
    // Jump on an item button: that item is taken off, the others stay, Jump anywhere goes on
    Mapping base = Mapping::defaults();
    const auto items = cs::item_inputs(base.pad);
    r = cs::build(m, "g1", "Pad", cs::Face::kPosition, cs::Layout::kJumpOnItem, items[0], xbox);
    const auto p = r.mapping.controllers[0].pad;
    assert(r.jump_on && p[kJump] == items[0] && p[kX] == kPadNone && p[kY] == items[1] && p[kR] == items[2]);
    assert(p[kMoonJump] == kPadLB && p[kL] == kPadLB);
    // Jump on any button, its use to L1: the bottom face button (by position: the sword) jumps, the
    // sword moves to L1, L1 no longer presses L; the moon jump comes along to the jump button
    r = cs::build(m, "g1", "Pad", cs::Face::kPosition, cs::Layout::kJumpAnywhere, kPadA, xbox);
    const auto q = r.mapping.controllers[0].pad;
    assert(q[kJump] == kPadA && q[kB] == kPadLB && q[kL] == kPadNone && q[kMoonJump] == kPadA);
    // ... on L1 itself: nothing moves
    r = cs::build(m, "g1", "Pad", cs::Face::kPosition, cs::Layout::kJumpAnywhere, kPadLB, xbox);
    assert(r.mapping.controllers[0].pad[kJump] == kPadLB && r.mapping.controllers[0].pad[kL] == kPadLB);
    // a second setup of the same controller replaces its profile, never adds one
    Mapping twice = cs::build(m, "g1", "Pad", cs::Face::kPosition, cs::Layout::kJumpAnywhere, kPadA, xbox).mapping;
    twice = cs::build(twice, "g1", "Pad", cs::Face::kLetters, cs::Layout::kOriginal, kPadNone, xbox).mapping;
    assert(twice.controllers.size() == 1 && twice.controllers[0].pad[kA] == kPadA && twice.controllers[0].pad[kB] == kPadB);
    // Try it: what a button does
    auto d = cs::describe(q, kPadA);
    assert(d.size() == 2 && d[0].find("Jump") == 0 && d[1].find("Jump") == 0);
    d = cs::describe(q, kPadLB);
    assert(d.size() == 1 && d[0].find("Wii U B") == 0);
    assert(cs::describe(q, kPadMisc6) == std::vector<std::string>{"nothing in the game"});
    // the holds: one second, once per hold; Start is Done, Select is Change
    cs::Hold h;
    assert(h.poll(true, false, 0) == cs::Hold::kNone && h.poll(true, false, 0.5) == cs::Hold::kNone);
    assert(h.progress(true, 0.5) > 0.45f && h.progress(true, 0.5) < 0.55f);
    assert(h.poll(true, false, 1.0) == cs::Hold::kDone && h.poll(true, false, 2.5) == cs::Hold::kNone);
    assert(h.poll(false, false, 3) == cs::Hold::kNone && h.poll(false, true, 3.1) == cs::Hold::kNone);
    assert(h.poll(false, true, 4.2) == cs::Hold::kChange);
    // details: the kernel drivers named for the families that need them, none for others
    input::ControllerInfo ds;
    ds.name = "DualSense Wireless Controller"; ds.vendor = 0x054C; ds.product = 0x0CE6;
    assert(cd::kernel_driver(0x054C, 0x0CE6) && !strcmp(cd::kernel_driver(0x054C, 0x0CE6)->driver, "hid-playstation"));
    assert(!strcmp(cd::kernel_driver(0x057E, 0x2009)->driver, "hid-nintendo"));
    assert(!cd::kernel_driver(0x045E, 0x0B13));
    const std::string why = cd::missing_note(ds, true);
    assert(why.find("motion sensors and rumble") != std::string::npos && why.find("5.12") != std::string::npos);
    assert(cd::missing_note(ds, false).empty());  // desktop: SDL reads them itself
    ds.gyro = ds.accel = ds.rumble = true;
    assert(cd::missing_note(ds, true).empty());
    ds.rumble = false;
    assert(cd::missing_note(ds, true).find("its rumble") != std::string::npos);
    std::printf("controller setup: assignments, Try it, holds, kernel drivers\n");
}

// ---- the dialog flows ----

struct Flow {
    Ui& ui;
    cs::State s;
    Mapping m = Mapping::defaults();
    cs::Marks marks;
    float values[kPadCount] = {};
    FaceLabel labels[4] = {FaceLabel::kA, FaceLabel::kB, FaceLabel::kX, FaceLabel::kY};
    double now = 0;
    bool back = false, connected = true;
    cs::Answer last = cs::Answer::kNone;
    std::function<void()> draw = [this] {
        marks.items.clear();
        cs::marks() = &marks;
        cs::Io io;
        io.values = connected ? values : nullptr;
        io.labels = labels;
        io.now = now;
        io.back = back;
        const cs::Answer a = cs::frame(s, m, io);
        if (a != cs::Answer::kNone) last = a;
        if (a == cs::Answer::kDone) m = s.result.mapping;
        cs::marks() = nullptr;
        back = false;
    };
    explicit Flow(Ui& u) : ui(u) {}
    void frames(int n) { for (int i = 0; i < n; i++) { now += 1.0 / 60; ui.frame(draw); } }
    void click(const std::string& label) {
        frames(1);
        const ImRect* r = marks.find(label);
        if (!r) std::fprintf(stderr, "no control \"%s\" (step %d)\n", label.c_str(), int(s.step));
        assert(r);
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 c = r->GetCenter();
        io.AddMousePosEvent(c.x, c.y); frames(1);
        io.AddMouseButtonEvent(0, true); frames(1);
        io.AddMouseButtonEvent(0, false); frames(6);  // (a new step's dialog settles its size)
    }
    void press(int pad, int n = 3) { values[pad] = 1; frames(n); }
    void release(int pad, int n = 3) { values[pad] = 0; frames(n); }
};

void flows(const Case& c, const char* dir) {
    Ui ui(c);
    // offer -> Later: nothing changes; Don't ask again is the caller's (answer kNeverAsk)
    {
        Flow f(ui);
        f.s.begin("g1", "Xbox Wireless Controller", true);
        f.frames(20);
        assert(up(cs::kOfferTitle));
        on_screen(cs::kOfferTitle);
        ui.save(dir, "1-offer");
        f.click("Later");
        assert(!up(cs::kOfferTitle) && f.last == cs::Answer::kLater && !f.s.active() && f.m == Mapping::defaults());
        f.s.begin("g1", "Xbox Wireless Controller", true);
        f.frames(20);
        f.click("Don't ask");
        assert(f.last == cs::Answer::kNeverAsk && !f.s.active());
        // B on the offer: Later
        f.s.begin("g1", "Xbox Wireless Controller", true);
        f.frames(20);
        f.back = true;
        f.frames(2);
        assert(f.last == cs::Answer::kLater && !up(cs::kOfferTitle));
    }
    // by touch: Set up -> by letters -> Jump on any button -> press the bottom button -> Try it -> Done
    {
        Flow f(ui);
        f.s.begin("g1", "Xbox Wireless Controller", true);
        f.frames(20);
        f.click("Set up");
        f.frames(20);
        assert(up(cs::kTitle) && f.s.step == cs::Step::kFace);
        on_screen(cs::kTitle);
        ui.save(dir, "2-face");
        f.click("By the letters on the buttons");
        assert(f.s.face == cs::Face::kLetters);
        f.click("Next");
        assert(f.s.step == cs::Step::kLayout);
        on_screen(cs::kTitle);
        ui.save(dir, "3-layout");
        f.click("Jump on any button, its use moved to L1");
        f.click("Next");
        assert(f.s.step == cs::Step::kJumpButton);
        ui.save(dir, "4-jump-button");
        // Start, Select and Home are never the jump button; the next press is
        f.press(kPadMenu); f.release(kPadMenu);
        f.press(kPadOptions); f.release(kPadOptions);
        assert(f.s.step == cs::Step::kJumpButton);
        f.press(kPadA);
        assert(f.s.step == cs::Step::kTry && f.s.jump_pad == kPadA);
        f.frames(20);
        on_screen(cs::kTitle);
        ui.save(dir, "5-try");
        f.release(kPadA);
        // the bottom button (the letter A) jumps; the Wii U A it was moved to L1
        const auto& pads = f.s.result.mapping.controllers[0].pad;
        assert(pads[kJump] == kPadA && pads[kA] == kPadLB && pads[kL] == kPadNone && f.s.result.jump_on);
        f.press(kPadLB);
        assert(f.s.last_input == kPadLB);
        f.release(kPadLB);
        f.click("Done");
        assert(f.last == cs::Answer::kDone && !up(cs::kTitle) && f.m.controllers.size() == 1);
        assert(profile_index(f.m, "g1", "Xbox Wireless Controller") == 0 && f.m.pad == Mapping::defaults().pad);
    }
    // by controller alone: Return (A) on the defaults, Jump on an item button, the hold to finish
    {
        Flow f(ui);
        f.s.begin("g2", "8BitDo Pro 2", false);
        f.frames(20);
        assert(f.s.step == cs::Step::kFace);
        f.click("Next");
        f.click("Jump on an item button");
        f.click("Next");
        assert(f.s.step == cs::Step::kJumpButton);
        // a face button that is no item button (by position: the bottom one is the sword) is not taken
        f.press(kPadA); f.release(kPadA);
        assert(f.s.step == cs::Step::kJumpButton);
        const int item = Mapping::defaults().pad[kY];  // by position: the Wii U's Y is the left button
        f.press(item);
        assert(f.s.step == cs::Step::kTry && f.s.jump_pad == item);
        f.release(item);
        // hold Select: Change (back to the face buttons); then all the way again and hold Start: Done
        f.values[kPadOptions] = 1;
        f.frames(70);
        assert(f.s.step == cs::Step::kFace);
        f.release(kPadOptions);
        f.click("Next");
        f.click("Next");
        f.press(item);
        f.release(item);
        assert(f.s.step == cs::Step::kTry);
        f.values[kPadMenu] = 1;
        f.frames(30);
        assert(f.s.step == cs::Step::kTry);  // half a second: not yet
        f.frames(40);
        assert(f.last == cs::Answer::kDone && !f.s.active());
        f.release(kPadMenu);
        const auto& pads = f.m.controllers[0].pad;
        assert(pads[kJump] == item && pads[kY] == kPadNone && pads[kX] != kPadNone);
        // B does not cancel the controller's own steps (it may be the button wanted); Cancel by touch does
        f.s.begin("g2", "8BitDo Pro 2", false);
        f.frames(20);
        f.click("Next");
        f.click("Jump on any button, its use moved to L1");
        f.click("Next");
        f.back = true;
        f.frames(3);
        assert(f.s.step == cs::Step::kJumpButton);
        f.click("Cancel");
        assert(f.last == cs::Answer::kCancel && !f.s.active() && !up(cs::kTitle));
        // a controller that went away: the steps say so and wait
        f.s.begin("g2", "8BitDo Pro 2", false);
        f.connected = false;
        f.frames(20);
        f.click("Next");
        f.click("Next");  // Original: Try it
        assert(f.s.step == cs::Step::kTry);
        f.click("Cancel");
    }
    std::printf("%-26s setup flows by touch and by controller\n", c.name);
}

void displace(const Case& c, const char* dir) {
    Ui ui(c);
    Mapping view = Mapping::defaults();
    cs::Displacing d;
    cs::Marks marks;
    bool changed = false, back = false;
    auto draw = [&] {
        marks.items.clear();
        cs::marks() = &marks;
        if (d.active()) changed = cs::displace_dialog(d, view, back) || changed;
        cs::marks() = nullptr;
        back = false;
    };
    auto click = [&](const char* label) {
        ui.frame(draw);
        const ImRect* r = marks.find(label);
        assert(r);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(r->GetCenter().x, r->GetCenter().y); ui.frame(draw);
        io.AddMouseButtonEvent(0, true); ui.frame(draw);
        io.AddMouseButtonEvent(0, false); ui.frame(draw); ui.frame(draw);
    };
    // a free input binds at once
    assert(cs::bind_or_ask(d, view, kJump, kPadMisc2) && view.pad[kJump] == kPadMisc2 && !d.active());
    // an item button: Move to L1 / Move to d-pad down / Take off
    const int top = view.pad[kX];
    assert(!cs::bind_or_ask(d, view, kJump, top) && d.active());
    for (int i = 0; i < 20; i++) ui.frame(draw);
    assert(up(cs::kDisplaceTitle));
    on_screen(cs::kDisplaceTitle);
    ui.save(dir, "6-displace");
    assert(marks.find("Move to L1") && marks.find("Move to d-pad down") && marks.find("Take off"));
    click("Move to d-pad down");
    assert(changed && view.pad[kJump] == top && view.pad[kX] == kPadDDown && !up(cs::kDisplaceTitle));
    // another game button: Move (to L1)
    changed = false;
    const int bottom = view.pad[kB];
    assert(!cs::bind_or_ask(d, view, kMoonJump, bottom));
    for (int i = 0; i < 20; i++) ui.frame(draw);
    assert(marks.find("Move (to LB)") && !marks.find("Move to d-pad down"));
    click("Move (to LB)");
    assert(changed && view.pad[kMoonJump] == bottom && view.pad[kB] == kPadLB && view.pad[kL] == kPadNone);
    // Cancel and B change nothing
    changed = false;
    const Mapping before = view;
    assert(!cs::bind_or_ask(d, view, kJump, view.pad[kA]));
    for (int i = 0; i < 20; i++) ui.frame(draw);
    click("Cancel");
    assert(!changed && view == before && !d.active());
    assert(!cs::bind_or_ask(d, view, kJump, view.pad[kA]));
    for (int i = 0; i < 20; i++) ui.frame(draw);
    back = true;
    ui.frame(draw); ui.frame(draw);
    assert(!changed && view == before && !d.active() && !up(cs::kDisplaceTitle));
    std::printf("%-26s the Move / Take off question\n", c.name);
}

void details(const Case& c, const char* dir) {
    Ui ui(c);
    std::vector<input::ControllerInfo> list(3);
    list[0].id = 1; list[0].name = "Touch controls"; list[0].is_virtual = true;
    list[1].id = 2; list[1].name = "DualSense Wireless Controller"; list[1].vendor = 0x054C; list[1].product = 0x0CE6;
    list[1].guid = "050000004c050000e60c000000810000"; list[1].battery = 80; list[1].power = "on battery";
    list[1].connection = "wireless"; list[1].buttons = 15; list[1].axes = 6; list[1].active = true;
    list[1].values.assign(kPadCount, 0.f); list[1].values[kPadA] = 1;
    list[2].id = 3; list[2].name = "Xbox Wireless Controller"; list[2].rumble = list[2].gyro = list[2].accel = false;
    assert(cd::ordered(list)[0].id == 2 && cd::ordered(list)[2].is_virtual);
    Mapping m = Mapping::defaults();
    ensure_profile(m, list[1].guid, list[1].name);
    const auto lines = cd::lines(list[1], m);
    bool own = false, battery = false;
    for (const auto& [k, v] : lines) {
        own |= k == "Bindings" && v.find("its own") == 0;
        battery |= k == "Battery" && v.find("80 %") == 0;
    }
    assert(own && battery);
    bool open = true, back = false;
    auto draw = [&] { cd::dialog(open, list, m, ui.android, back); open = back = false; };
    for (int i = 0; i < 20; i++) ui.frame(draw);
    assert(up(cd::kTitle));
    on_screen(cd::kTitle);
    ui.save(dir, "7-details");
    back = true;
    ui.frame(draw); ui.frame(draw);
    assert(!up(cd::kTitle));
    std::printf("%-26s Controller details\n", c.name);
}
}  // namespace

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : nullptr;
    pure();
    const Case cases[] = {
        {"phone-2400x1080", 2400, 1080, 400},
        {"phone-1080x2400", 1080, 2400, 400},
        {"handheld-1920x1080-6in", 1920, 1080, 367},
        {"desktop-1280x752-retina", 2560, 1504, 0},
    };
    for (const Case& c : cases) {
        flows(c, dir);
        displace(c, dir);
        details(c, dir);
    }
    std::printf("controller_setup_test: passed\n");
}
