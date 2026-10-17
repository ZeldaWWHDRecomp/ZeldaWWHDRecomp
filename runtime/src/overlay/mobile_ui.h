// Android Settings sizes: how large the overlay draws, from the screen's physical size (its pixels and
// DPI), not from the device's name. The overlay lays out in "layout pixels" (17 px text) and draws them
// `scale` screen pixels each; a control (button, checkbox, slider, tab, title bar) is the text plus two
// vertical paddings.
//
// Two layouts:
//  - Tablets and handhelds (a 16:9, 16:10 or 4:3 screen, or anything larger than 7.5" across): 16 px
//    paddings, a control 49 layout pixels tall. Automatic and Medium make that 8 mm, Small 7 mm, Large
//    9 mm; the text is 17/49 of it (2.8 mm at 8 mm).
//  - Phones (a long narrow screen, 18:9 to 21:9, at most 7.5" across): 21 px paddings, a control 59
//    layout pixels tall, so the text is smaller beside a control of the same size. Automatic follows
//    Android's own guidance: 48 dp controls (7.6 mm) and 14 sp text (2.2 mm); Small, Medium and Large
//    make the controls 7, 8 and 9 mm (text 2.0, 2.3 and 2.6 mm). Every control stays at least 7 mm tall.
// A UI size the player chose (Small, Medium, Large) always decides over Automatic.
#pragma once
#include <algorithm>
#include <cmath>
namespace mobile_ui {
constexpr float kText = 17.0f;              // the overlay's font size in layout pixels
constexpr float kClassicPad = 16.0f;        // tablets and handhelds
constexpr float kPhonePad = 21.0f;          // phones
constexpr float kPhoneAspect = 1.9f;        // long side / short side: 18:9 = 2.0, 16:9 = 1.78
constexpr float kPhoneDiagonal = 7.5f;      // inches
constexpr float kPhoneAutomaticMm = 7.62f;  // 48 dp
enum Preset { kAutomatic, kSmall, kMedium, kLarge };

// a usable DPI: the screen's own, else Android's density (dp scale), else 160
inline float dpi_of(float dpi, float density) {
    if (std::isfinite(dpi) && dpi >= 72 && dpi <= 1000) return dpi;
    return 160 * std::clamp(std::isfinite(density) ? density : 1.0f, 1.0f, 6.0f);
}
// a phone's screen: long and narrow, and small enough to hold in one hand
inline bool phone_screen(float width, float height, float dpi) {
    const float lo = std::min(width, height), hi = std::max(width, height);
    if (!(lo > 0) || !(dpi > 0)) return false;
    return hi / lo >= kPhoneAspect && std::hypot(width, height) / dpi <= kPhoneDiagonal;
}

struct Layout {
    float scale = 1;  // screen pixels per layout pixel
    float pad_y = kClassicPad;
    bool phone = false;
    float control() const { return kText + 2 * pad_y; }  // layout pixels
    float control_mm(float dpi) const { return control() * scale * 25.4f / dpi; }
    float text_mm(float dpi) const { return kText * scale * 25.4f / dpi; }
};
// width, height: the screen in pixels (0: unknown, the tablet and handheld layout)
inline Layout layout(float dpi, float density, int preset, float width = 0, float height = 0) {
    dpi = dpi_of(dpi, density);
    Layout l;
    l.phone = phone_screen(width, height, dpi);
    l.pad_y = l.phone ? kPhonePad : kClassicPad;
    const float mm = preset == kSmall ? 7.0f : preset == kLarge ? 9.0f : preset == kMedium ? 8.0f
                     : l.phone        ? kPhoneAutomaticMm
                                      : 8.0f;
    l.scale = std::clamp(dpi * mm / (25.4f * l.control()), 1.0f, 12.0f);
    return l;
}
// the tablet and handheld layout's scale (a screen of unknown shape)
inline float scale(float dpi, float density, int preset) { return layout(dpi, density, preset).scale; }

// Desktop (not Android): the window's backing scale (HiDPI), and larger on a large window (a TV across the room)
inline float desktop_scale(float density, float window_height) { return std::max({1.0f, density, window_height / 820.0f}); }

// The touch style of a layout (overlay.cpp, and the offscreen tests that draw like Android)
template <class Style> void apply_touch_style(Style& s, const Layout& l) {
    s.FramePadding.x = 10;
    s.FramePadding.y = l.pad_y;
    s.ItemInnerSpacing.x = 10;
    s.ItemInnerSpacing.y = 8;
    s.ScrollbarSize = l.control();
    s.GrabMinSize = l.control();
}
}  // namespace mobile_ui
