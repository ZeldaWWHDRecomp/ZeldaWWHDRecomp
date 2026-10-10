// Real ImGui frames and SDL virtual gamepads; synthetic long pages, no game or renderer.
#include "overlay/gamepad_nav.h"
#include "imgui_internal.h"
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace input_map;
using Values = std::array<float, kPadCount>;

struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Joystick* joystick = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    explicit VirtualPad(bool attached) {
        if (!attached) return;
        SDL_VirtualJoystickDesc d; SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT; d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.vendor_id = 0x045e; d.product_id = 0x02ea; d.name = "Xbox overlay regression";
        id = SDL_AttachVirtualJoystick(&d); assert(id);
        joystick = SDL_OpenJoystick(id); gamepad = SDL_OpenGamepad(id);
        assert(joystick && gamepad && SDL_GetGamepadType(gamepad) == SDL_GAMEPAD_TYPE_XBOXONE);
        // SDL virtual axes are bipolar; trigger -32768 maps to gamepad value 0.
        for (auto a : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})
            assert(SDL_SetJoystickVirtualAxis(joystick, a, -32768));
        SDL_UpdateJoysticks();
        assert(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0);
        assert(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 0);
    }
    ~VirtualPad() {
        if (!id) return;
        SDL_CloseGamepad(gamepad); SDL_CloseJoystick(joystick);
        assert(SDL_DetachVirtualJoystick(id));
    }
    void axis(SDL_GamepadAxis a, float v) {
        const Sint16 raw = a >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? Sint16(v * 65535 - 32768) : Sint16(v * 32767);
        assert(SDL_SetJoystickVirtualAxis(joystick, a, raw));
        SDL_UpdateJoysticks();
    }
    void button(SDL_GamepadButton b, bool down) {
        assert(SDL_SetJoystickVirtualButton(joystick, b, down)); SDL_UpdateJoysticks();
    }
    Values read() const {
        Values v{}; if (!gamepad) return v;
        auto stick = [&](SDL_GamepadAxis a, int negative, int positive) {
            float x = SDL_GetGamepadAxis(gamepad, a) / 32768.f;
            v[negative] = std::max(-x, 0.f); v[positive] = std::max(x, 0.f);
        };
        stick(SDL_GAMEPAD_AXIS_LEFTX, kPadLSLeft, kPadLSRight);
        stick(SDL_GAMEPAD_AXIS_LEFTY, kPadLSUp, kPadLSDown);
        stick(SDL_GAMEPAD_AXIS_RIGHTX, kPadRSLeft, kPadRSRight);
        stick(SDL_GAMEPAD_AXIS_RIGHTY, kPadRSUp, kPadRSDown);
        v[kPadLT] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.f;
        v[kPadRT] = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.f;
        const SDL_GamepadButton buttons[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
        const int inputs[] = {kPadA, kPadB, kPadDUp, kPadDDown, kPadLB, kPadRB};
        for (int i = 0; i < 6; ++i) v[inputs[i]] = SDL_GetGamepadButton(gamepad, buttons[i]) ? 1.f : 0.f;
        return v;
    }
};

struct Page {
    overlay::GamepadNavigation navigation;
    Values previous{};
    int clicks = 0, frames = 0, tab = 0, backs = 0;
    ImVec2 visible_button;
    bool enabled = true;
    explicit Page() {
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DisplaySize = {1000, 700}; io.DeltaTime = 1.f / 60;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_RendererHasTextures;
    }
    ~Page() { ImGui::DestroyContext(); }
    float frame(const Values& values, bool mouse = false) {
        auto& io = ImGui::GetIO();
        navigation.feed(io, values.data(), previous.data(), enabled, mouse);
        if (navigation.pressed(values.data(), previous.data(), kPadLB)) --tab;
        if (navigation.pressed(values.data(), previous.data(), kPadRB)) ++tab;
        if (navigation.pressed(values.data(), previous.data(), kPadB)) ++backs;
        previous = values;
        ImGui::NewFrame(); ImGui::SetNextWindowPos({50, 50}); ImGui::SetNextWindowSize({860, 620});
        if (!frames) ImGui::SetNextWindowFocus();
        ImGui::Begin("Settings"); ImGui::BeginChild("page", {}, ImGuiChildFlags_NavFlattened);
        for (int i = 0; i < 90; ++i) {
            char label[40]; snprintf(label, sizeof label, "Option %d", i);
            if (ImGui::Button(label)) ++clicks;
            if (ImGui::IsItemVisible()) {
                const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                if (a.y > 90 && b.y < 600) visible_button = ImVec2((a.x+b.x)/2, (a.y+b.y)/2);
            }
        }
        float y = ImGui::GetScrollY();
        ImGui::EndChild(); ImGui::End(); ImGui::Render(); ++frames; return y;
    }
    void settle(const VirtualPad& pad, int n = 5) { for (int i = 0; i < n; ++i) frame(pad.read()); }
    float wheel(const VirtualPad& pad) {
        ImGui::GetIO().AddMousePosEvent(450, 350); frame(pad.read(), true);
        ImGui::GetIO().AddMouseWheelEvent(0, -5); frame(pad.read(), true);
        return frame(pad.read());
    }
};

int main() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    {
        VirtualPad pad(true);
        // Closed menu: use the stick once, then release to exactly zero.
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -1);
        assert(pad.read()[kPadLSUp] > 0.99f);
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
        for (float v : pad.read()) assert(v == 0);
        Page page; page.previous = pad.read(); page.settle(pad);
        const auto position = page.visible_button;
        ImGui::GetIO().AddMousePosEvent(position.x, position.y); page.frame(pad.read(), true);
        ImGui::GetIO().AddMouseButtonEvent(0, true); page.frame(pad.read(), true);
        ImGui::GetIO().AddMouseButtonEvent(0, false); page.frame(pad.read(), true);
        assert(page.clicks == 1);
        // A click must hand off all gamepad navigation even with exact-zero axes.
        // The old feed also scrolled stably at zero in our trace; the ownership
        // assertion verifies the handoff policy, not reproduction of that report.
        assert(!page.navigation.controller_active());
        assert(!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad));
        float y = page.wheel(pad); assert(y > 0);
        for (int f = 0; f < 180; ++f) {
            for (float v : pad.read()) assert(v == 0);
            assert(page.frame(pad.read()) == y);
        }
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true); page.frame(pad.read());
        assert(page.navigation.controller_active());
        assert(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad);
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false); page.settle(pad);
    }
    auto check_rest = [](int axis, float rest, bool touched) {
        VirtualPad pad(axis != -2);
        if (touched) { pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -1); pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0); }
        if (axis >= 0) pad.axis(SDL_GamepadAxis(axis), axis < 4 ? -rest : rest);
        Page page; page.previous = pad.read(); page.settle(pad);
        assert(page.frame(pad.read()) == 0);
        float scrolled = page.wheel(pad); assert(scrolled > 200);
        for (int f = 0; f < 180; ++f) {
            float y = page.frame(pad.read()); assert(std::abs(y - scrolled) < 0.01f);
            if (f == 0 || f == 60 || f == 179)
                printf("axis=%d rest=%.2f frame=%d scroll=%.1f\n", axis, rest, f, y);
        }
    };
    // Reporter order first: connect, move left stick with the menu closed, release
    // off centre, open, then wheel. The shipped feed scrolls at 0.36..0.40 even
    // though its corresponding navigation key has Down=false.
    check_rest(SDL_GAMEPAD_AXIS_LEFTY, 0.4f, true);
    check_rest(-2, 0, false); // no controller
    check_rest(-1, 0, false); // controller attached but never touched
    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
        for (float rest : {0.f, 0.1f, 0.34f, 0.36f, 0.4f}) check_rest(axis, rest, true);
    {
        VirtualPad pad(true); Page page; page.settle(pad);
        // A held input must not take control back from a wheel, movement, or click.
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -0.8f); page.settle(pad);
        float y = page.wheel(pad); assert(!page.navigation.controller_active());
        for (int f = 0; f < 30; ++f) assert(page.frame(pad.read()) == y);
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0); page.settle(pad);
        // A new left-stick gesture restores scrolling; right stick never takes over.
        pad.axis(SDL_GAMEPAD_AXIS_RIGHTY, -1); page.settle(pad); assert(!page.navigation.controller_active());
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0.8f); page.settle(pad, 30);
        assert(page.navigation.controller_active() && page.frame(pad.read()) > y);
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0); page.settle(pad);
        ImGui::GetIO().AddMousePosEvent(451, 350); page.frame(pad.read(), true);
        assert(!page.navigation.controller_active());
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_UP, true); page.frame(pad.read());
        assert(page.navigation.controller_active());
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_UP, false); page.settle(pad);
        ImGui::GetIO().AddMouseButtonEvent(0, true); page.frame(pad.read(), true);
        assert(!page.navigation.controller_active());
        ImGui::GetIO().AddMouseButtonEvent(0, false); page.frame(pad.read(), true);
        for (auto button : {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                            SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH}) {
            pad.button(button, true); page.frame(pad.read()); assert(page.navigation.controller_active());
            pad.button(button, false); page.frame(pad.read(), true);
        }
        assert(page.tab == 0 && page.backs == 1);
        // Scrollbar drag also keeps a held stick from reclaiming navigation.
        pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -0.8f); page.settle(pad);
        ImGuiWindow* child = nullptr;
        // Derive the child name from ImGui rather than depending on its hashed suffix.
        for (auto* w : ImGui::GetCurrentContext()->Windows)
            if (w->Flags & ImGuiWindowFlags_ChildWindow) child = w;
        assert(child && child->ScrollbarY);
        auto bar = ImGui::GetWindowScrollbarRect(child, ImGuiAxis_Y);
        ImGui::GetIO().AddMousePosEvent(bar.GetCenter().x, bar.Max.y-20); page.frame(pad.read(), true);
        ImGui::GetIO().AddMouseButtonEvent(0, true); page.frame(pad.read(), true);
        ImGui::GetIO().AddMousePosEvent(bar.GetCenter().x, bar.Max.y-5); page.frame(pad.read(), true);
        ImGui::GetIO().AddMouseButtonEvent(0, false); page.frame(pad.read(), true);
        y = page.frame(pad.read()); assert(y > 0);
        for (int f = 0; f < 30; ++f) assert(page.frame(pad.read()) == y);
    }
    {
        VirtualPad pad(true); Page page; page.settle(pad);
        // Actual ImGui navigation and activation, after mouse ownership.
        page.wheel(pad);
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true); page.frame(pad.read());
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false); page.settle(pad);
        auto before = ImGui::GetCurrentContext()->NavId;
        assert(before != 0);
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true); page.frame(pad.read());
        pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false); page.settle(pad);
        assert(ImGui::GetCurrentContext()->NavId != before);
        pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true); page.frame(pad.read());
        pad.button(SDL_GAMEPAD_BUTTON_SOUTH, false); page.settle(pad); assert(page.clicks == 1);
        // Mouse wheel remains available after controller navigation.
        float y = page.wheel(pad); assert(y > 200);
        for (int i = 0; i < 30; ++i) assert(page.frame(pad.read()) == y);
        before = ImGui::GetCurrentContext()->NavId;
        ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true); page.frame(pad.read());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false); page.settle(pad);
        assert(ImGui::GetCurrentContext()->NavId != before);
        assert(!page.navigation.controller_active());
        // A mouse click still activates a visible item after controller use.
        ImGui::GetIO().AddMousePosEvent(page.visible_button.x, page.visible_button.y); page.frame(pad.read(), true);
        int clicks = page.clicks;
        ImGui::GetIO().AddMouseButtonEvent(0, true); page.frame(pad.read(), true);
        ImGui::GetIO().AddMouseButtonEvent(0, false); page.settle(pad);
        assert(page.clicks == clicks + 1);
    }
    SDL_Quit(); puts("overlay_scroll_test passed");
}
