// The Game Over save prompt on the GamePad. The game draws its "Would you like to save?" window into the GamePad
// picture as well as on the TV, then one more quad over the whole GamePad picture: a solid black pane (the white
// 8x8 BC4 texture panes are drawn with, coloured black, opaque; the GamePad UI has no depth buffer, so it covers
// the window). The prompt is routed to the TV (routing 4) and takes only the controller's buttons: the GamePad's
// touch buttons are never read. While the save window is up at Game Over:
// - both renderers leave that pane's draw out, and the GamePad shows the window (skip());
// - a tap on its Cancel or Save becomes the controller input the prompt reads: Left or Right onto that button
//   (one already selected stays), then A (Tap).
// Host-only: the flag is the guest thread's reading at the swap (gamepad_ui_state.cpp); both renderers ask skip()
// with the draw's registers before it is recorded; VPADRead runs Tap (hle/system_stubs.cpp).
#pragma once
#include <atomic>
#include <cstdint>

namespace gfx::gameover_pane {
inline std::atomic<bool> g_save_prompt{false};

// The pane: into the 854x480 GamePad picture, a pixel shader reading one texture only, an 8x8 BC4 one (the save
// window's own 8x8 panes also read their 64x64 frame texture). `units` are the shader's texture units;
// `ps_resources` the pixel shader resource registers (SQ_TEX_RESOURCE_WORD0_N_PS: 7 words per unit).
inline bool skip(uint32_t target_w, uint32_t target_h, int unit_count, const uint8_t* units, const uint32_t* ps_resources) {
    if (!g_save_prompt.load(std::memory_order_relaxed) || target_w != 854 || target_h != 480 || unit_count != 1) return false;
    const uint32_t* t = ps_resources + units[0] * 7;
    const uint32_t width = (t[0] >> 19) + 1, height = (t[1] & 0x1FFF) + 1, format = t[1] >> 26;
    return width == 8 && height == 8 && format == 0x34;
}

// The window's buttons on the GamePad screen (0..1 from the top left; measured on the drawn window, with a margin).
enum Button { kNone = -1, kCancel = 0, kSave = 1 };
inline Button button_at(float x, float y) {
    if (y < 0.62f || y > 0.77f) return kNone;
    if (x >= 0.22f && x <= 0.48f) return kCancel;
    if (x >= 0.50f && x <= 0.80f) return kSave;
    return kNone;
}

// One pad read per logic step: a tap that starts and ends on the same button gives that button's direction for
// kHold reads, nothing for kHold, then A for kHold (the game's menus act on a press, not a held button).
class Tap {
public:
    static constexpr int kHold = 3;
    static constexpr uint32_t kA = 0x8000, kLeft = 0x0800, kRight = 0x0400;  // VPAD buttons (input.h)
    uint32_t step(bool prompt, bool touch, float x, float y) {
        if (!prompt) { *this = Tap{}; return 0; }
        if (touch) {
            if (!down_) pressed_ = button_at(x, y);
            down_ = true, x_ = x, y_ = y;
        } else if (down_) {
            down_ = false;
            if (pressed_ != kNone && button_at(x_, y_) == pressed_ && !reads_) choice_ = pressed_, reads_ = 3 * kHold;
        }
        if (!reads_) return 0;
        const int left = reads_--;  // 3*kHold .. 1
        if (left > 2 * kHold) return choice_ == kCancel ? kLeft : kRight;
        if (left > kHold) return 0;
        return kA;
    }
private:
    bool down_ = false;
    float x_ = 0, y_ = 0;
    Button pressed_ = kNone, choice_ = kNone;
    int reads_ = 0;
};
}  // namespace gfx::gameover_pane
