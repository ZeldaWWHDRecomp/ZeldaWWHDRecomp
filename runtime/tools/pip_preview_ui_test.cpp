// Exercise the real diagram's ImGui pointer and D-pad handling without a game
// or GPU.
#include "../src/overlay/overlay.cpp"
#include <cassert>
#include <cmath>
#include <initializer_list>
void log_msg(const char *, ...) {}
namespace input {
void set_touch(bool, float, float) {}
} // namespace input
namespace aspect {
void set_window_aspect(float) {}
} // namespace aspect
namespace hostui {
void post(std::function<void()> f) { f(); }
void save_pip_position() {}
} // namespace hostui
static bool near(float a, float b) { return std::fabs(a - b) < 0.002f; }
int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    gfx::g_pip_anchor = gfx::kPipBR;
    auto frame = [&](bool focus = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 500));
        ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::SetCursorScreenPos(ImVec2(50, 50));
        if (focus) {
            auto *window = ImGui::GetCurrentWindow();
            ImGui::SetFocusID(window->GetID("Placement diagram"), window);
            ImGui::GetCurrentContext()->NavCursorVisible = true;
        }
        overlay::pip_diagram();
        ImGui::End();
        overlay::pip_live_drag();
        ImGui::Render();
    };
    // The diagram is 320 x 180 at (50, 50) for a 16:9 picture.
    const gfx::Box diagram{50, 50, 320, 180};
    auto target = [&](int anchor) {
        ImVec2 c;
        gfx::pip_target_centre(diagram, anchor, &c.x, &c.y);
        return c;
    };
    auto tap = [&](ImVec2 at) {
        io.AddMousePosEvent(at.x, at.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    frame();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    // A tap on an empty spot places the picture's centre there (free).
    tap(ImVec2(50 + 320 * 0.4f, 50 + 180 * 0.4f));
    assert(gfx::g_pip_anchor == gfx::kPipCustom);
    assert(near(gfx::g_pip_x, 0.4f) && near(gfx::g_pip_y, 0.4f));
    frame(true);
    for (int i = 0; i < 3; ++i) {
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true);
        frame();
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
        frame();
    }
    assert(near(gfx::g_pip_x, 0.43f));
    // Activating the focused diagram with A (no pointer) neither snaps nor moves it.
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
    frame();
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom && near(gfx::g_pip_x, 0.43f) && near(gfx::g_pip_y, 0.4f));

    // A tap on a preset target snaps to that preset, every one of the nine.
    for (int a = 0; a < gfx::kPipCustom; ++a) {
        gfx::pip_set_position({gfx::kPipCustom, a < 4 ? 0.8f : 0.2f, 0.5f});
        const ImVec2 c = target(a);
        tap(ImVec2(c.x + 3, c.y - 3));  // near the centre is enough
        assert(gfx::g_pip_anchor == a);
    }

    // Dragging the picture exactly onto the bottom-right preset's spot, and on over the bottom-right
    // target, never catches on either; releasing a drag on a target does not snap.
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    gfx::pip_set_position({gfx::kPipCustom, 0.3f, 0.3f});
    auto b = gfx::pip_box(diagram, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    const auto q = gfx::pip_box(diagram, gfx::g_drc_aspect, gfx::g_pip_size, {gfx::kPipBR, 0, 0});
    const ImVec2 start(b.x + b.w / 2, b.y + b.h / 2), spot(q.x + q.w / 2, q.y + q.h / 2), br = target(gfx::kPipBR);
    io.AddMousePosEvent(start.x, start.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    auto dragTo = [&](ImVec2 from, ImVec2 to) {
        for (int i = 1; i <= 20; ++i) {
            const float t = i / 20.0f;
            io.AddMousePosEvent(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
            frame();
            assert(gfx::g_pip_anchor == gfx::kPipCustom);
        }
    };
    dragTo(start, spot);
    b = gfx::pip_box(diagram, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    assert(std::fabs(b.x - q.x) < 1 && std::fabs(b.y - q.y) < 1);  // exactly the preset's spot, still free
    dragTo(spot, br);
    io.AddMouseButtonEvent(0, false);
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom);

    // A click (no drag) on a target covered by a large picture still snaps.
    gfx::g_pip_size = 0.8f;
    gfx::pip_set_position({gfx::kPipCustom, 0.5f, 0.5f});
    tap(target(gfx::kPipTL));
    assert(gfx::g_pip_anchor == gfx::kPipTL);
    // A click inside the picture away from every target changes nothing.
    gfx::pip_set_position({gfx::kPipCustom, 0.5f, 0.5f});
    tap(ImVec2(50 + 320 * 0.62f, 50 + 180 * 0.38f));
    assert(gfx::g_pip_anchor == gfx::kPipCustom && near(gfx::g_pip_x, 0.5f) && near(gfx::g_pip_y, 0.5f));
    gfx::g_pip_size = 0.25f;

    // The live preview over the game drags freely too, across the bottom-right preset.
    gfx::g_pip_preview = true;
    gfx::g_pip_anchor = gfx::kPipBR;
    auto box = gfx::pip_box({0, 0, 1280, 720}, gfx::g_drc_aspect, gfx::g_pip_size, gfx::pip_position());
    io.AddMousePosEvent(box.x + box.w / 2, box.y + box.h / 2);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(box.x + box.w / 2 - 1, box.y + box.h / 2);  // a pixel off the preset
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom);
    io.AddMousePosEvent(box.x + box.w / 2, box.y + box.h / 2);  // back exactly onto it: no snap
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom);
    io.AddMousePosEvent(1280 * 0.6f, 720 * 0.5f);
    frame();
    assert(gfx::g_pip_anchor == gfx::kPipCustom);
    assert(near(gfx::g_pip_x, 0.6f) && near(gfx::g_pip_y, 0.5f));
    io.AddMouseButtonEvent(0, false);
    frame();
    gfx::g_pip_preview = false;

    // The sliders are the only size/opacity controls: 5..100% and 10..100%.
    auto percentFrame = [&](bool focus = false, bool opacity = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 500));
        ImGui::Begin("percent", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::SetCursorScreenPos(ImVec2(50, 50));
        if (focus) {
            auto *window = ImGui::GetCurrentWindow();
            ImGui::FocusWindow(window);
            ImGui::SetFocusID(window->GetID(opacity ? "Opacity (%)" : "Size (%)"), window);
            ImGui::GetCurrentContext()->NavCursorVisible = true;
        }
        ImGui::SetNextItemWidth(200);
        if (opacity)
            overlay::pip_percent("Opacity (%)", gfx::g_pip_opacity, gfx::kPipOpacityMin, gfx::kPipOpacityMax,
                                 [](float value) { gfx::g_pip_opacity = gfx::pip_clamp_opacity(value); });
        else
            overlay::pip_percent("Size (%)", gfx::g_pip_size, gfx::kPipSizeMin, gfx::kPipSizeMax,
                                 [](float value) { gfx::g_pip_size = gfx::pip_clamp_size(value); });
        ImGui::End();
        ImGui::Render();
    };
    // press at x (inside the track), then drag to x2 before releasing
    auto clickSlider = [&](float x, float x2, bool opacity) {
        io.AddMousePosEvent(x, 60);
        percentFrame(false, opacity);
        io.AddMouseButtonEvent(0, true);
        percentFrame(false, opacity);
        io.AddMousePosEvent(x2, 60);
        percentFrame(false, opacity);
        io.AddMouseButtonEvent(0, false);
        percentFrame(false, opacity);
    };
    auto stepRight = [&](bool opacity) {
        percentFrame(true, opacity);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); percentFrame(false, opacity);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); percentFrame(false, opacity);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true); percentFrame(false, opacity);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false); percentFrame(false, opacity);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); percentFrame(false, opacity);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); percentFrame(false, opacity);
    };
    gfx::g_pip_size = 0.25f;
    stepRight(false);
    assert(near(gfx::g_pip_size, 0.26f));
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    clickSlider(150, 0, false);  // dragged past the left end: the minimum
    assert(near(gfx::g_pip_size, 0.05f));
    clickSlider(150, 400, false);  // past the right end: the maximum (100%, the whole TV picture)
    assert(near(gfx::g_pip_size, 1.0f));
    clickSlider(150, 150, false);
    assert(gfx::g_pip_size > 0.45f && gfx::g_pip_size < 0.60f);
    clickSlider(150, 0, true);
    assert(near(gfx::g_pip_opacity, 0.10f));
    clickSlider(150, 400, true);
    assert(near(gfx::g_pip_opacity, 1.0f));
    clickSlider(150, 150, true);
    assert(gfx::g_pip_opacity > 0.50f && gfx::g_pip_opacity < 0.60f);
    const float opacity = gfx::g_pip_opacity;
    stepRight(true);
    assert(near(gfx::g_pip_opacity, opacity + 0.01f));
    // A value saved under the old limits is shown and kept as it is.
    gfx::g_pip_size = 0.33f;
    percentFrame();
    assert(gfx::g_pip_size == 0.33f);

    // Use the production Android control height and every UI scale preset.
    auto &style = ImGui::GetStyle();
    style.FontSizeBase = 17;
    style.FramePadding = ImVec2(10, 16);
    gfx::g_pip_size = 0.25f;
    for (int preset = 0; preset < 4; ++preset) {
        const float scale = mobile_ui::scale(400, 2.5f, preset);
        io.DisplaySize = ImVec2(1920 / scale, 1080 / scale);
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        frame();
        tap(ImVec2(50 + 320 * 0.4f, 50 + 180 * 0.4f));
        assert(gfx::g_pip_anchor == gfx::kPipCustom && near(gfx::g_pip_x, 0.4f) && near(gfx::g_pip_y, 0.4f));
        tap(target(gfx::kPipBC));
        assert(gfx::g_pip_anchor == gfx::kPipBC);
    }
    ImGui::DestroyContext();
}
