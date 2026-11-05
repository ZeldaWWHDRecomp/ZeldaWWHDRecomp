// GamePad backdrop contract shared by the command producer and presenters.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace gfx::gamepad_backdrop {
inline std::atomic<bool> enabled{true};
// Presenters publish the aggregate need of actual visible DRC outputs once per
// presentation. Captures use the previous presentation's request; the first
// non-native-aspect frame therefore uses the edge-colour fallback. No output,
// backend reset and shutdown must clear the request, independently of the option.
inline std::atomic<bool> capture_requested{false};
inline bool capture_enabled() {
    return enabled.load(std::memory_order_relaxed) &&
        capture_requested.load(std::memory_order_relaxed);
}
inline void set_capture_requested(bool requested) {
    capture_requested.store(requested, std::memory_order_relaxed);
}

// Video memory of private captures, the same on both backends: at most
// kPoolBytes of capture images. Images that no presentation uses go when the
// option is turned off, and after kIdleFrames presentations without a capture.
inline constexpr uint64_t kPoolBytes = 96ull * 1024 * 1024;
inline constexpr uint64_t kIdleFrames = 300;
inline bool release_idle(bool option, uint64_t frame, uint64_t lastCapture) {
    return !option || (frame >= lastCapture && frame - lastCapture >= kIdleFrames);
}
// used: bytes of all pool images; replaced: bytes of the image a new one replaces.
inline bool fits_pool(uint64_t used, uint64_t replaced, uint64_t wanted) {
    const uint64_t kept = used >= replaced ? used - replaced : 0;
    return wanted && wanted <= kPoolBytes && kept <= kPoolBytes - wanted;
}

// Canonical item/map menu layouts both own this 1280x720 picture. Other similarly
// named panes and containers must never duplicate their children or foreground.
inline bool is_background(std::string_view name, float width, float height) {
    return name == "P_BgPattern_00" && width == 1280.f && height == 720.f;
}
struct Rect { float x, y, w, h; };
enum class Edge : unsigned { Top, Bottom, Left, Right };
// Local output-viewport coordinates. Bars partition the viewport outside the
// clipped content and never overlap it or one another. Invalid input draws none.
// Fallback colour contract: sample each native scan edge at fractions 1/8, 3/8,
// 5/8, 7/8 (texel-centred nearest edge coordinates), average those four colours,
// and fill the corresponding bar uniformly. Never stretch foreground UI/map.
inline std::array<Rect, 4> bars(float width, float height, const Rect& content) {
    if (!(width > 0 && height > 0 && content.w > 0 && content.h > 0) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(content.x) || !std::isfinite(content.y) ||
        !std::isfinite(content.w) || !std::isfinite(content.h)) return {};
    const float left = std::clamp(content.x, 0.f, width);
    const float top = std::clamp(content.y, 0.f, height);
    const float right = std::clamp(content.x + content.w, left, width);
    const float bottom = std::clamp(content.y + content.h, top, height);
    return {{{0, 0, width, top}, {0, bottom, width, height - bottom},
             {0, top, left, bottom - top}, {right, top, width - right, bottom - top}}};
}
inline bool has_bars(float width, float height, const Rect& content) {
    return std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0 &&
        content.w > 0 && content.h > 0 &&
        (content.x > .5f || content.y > .5f ||
         content.x + content.w < width - .5f || content.y + content.h < height - .5f);
}
// Only the backdrop uses cover. Original scanout and touch use their existing fit
// rectangle verbatim. Preserve source alpha: clear capture transparent, replay the
// original blend/material state, then blend that texture behind the opaque scanout.
inline Rect cover(float width, float height, float sourceWidth, float sourceHeight) {
    if (!(width > 0 && height > 0 && sourceWidth > 0 && sourceHeight > 0) ||
        !std::isfinite(width + height + sourceWidth + sourceHeight)) return {};
    const float scale = std::fmax(width / sourceWidth, height / sourceHeight);
    const float w = sourceWidth * scale, h = sourceHeight * scale;
    return {(width - w) * .5f, (height - h) * .5f, w, h};
}

// Backend-owned texture tickets, not guest pointers. A capture is eligible ONLY
// for the next scan copy of its exact render-target identity. Re-presenting must
// read committed() WITHOUT calling scan(): presentation cadence is not freshness.
// On OP_COPY_TO_SCAN target4, call scan(source) even if no pane was seen: this
// cancels a previous menu backdrop when a new map/game frame lacks that picture.
// Keep tickets alive through actual command-buffer completion; this helper owns
// no textures and performs no GPU destruction. Reset on state load/device reset.
class ScanCache {
public:
    struct Selection { uint64_t generation = 0, ticket = 0; };
    void capture(uint64_t source, uint64_t ticket) {
        if (!source || !ticket) return;
        for (auto& p : pending_) if (p.source == source) { p.ticket = ticket; return; }
        for (auto& p : pending_) if (!p.source) { p = {source, ticket}; return; }
        // Fail closed under abnormal target churn, never reuse another surface.
        pending_ = {};
        pending_[0] = {source, ticket};
    }
    Selection scan(uint64_t source) {
        ++committed_.generation;
        committed_.ticket = 0;
        for (auto& p : pending_) if (p.source == source && source) {
            committed_.ticket = p.ticket;
            break;
        }
        pending_ = {}; // no capture may leak across a new DRC scan generation
        return committed_;
    }
    Selection committed() const { return committed_; }
    void reset() { pending_ = {}; committed_ = {}; }
private:
    struct Pending { uint64_t source = 0, ticket = 0; };
    std::array<Pending, 16> pending_{};
    Selection committed_{};
};
} // namespace gfx::gamepad_backdrop
