// SPDX-License-Identifier: MPL-2.0
// Mod SDK additions for live features (QoL track M0; docs/mod-sdk-v2.md "Live options, input filters and
// touches"): pure parts, checked by guest_live_test without guest code.
//
// - Live options: a manifest option with "live": true follows changes made in Settings or the item menu's page
//   while the game runs; wwhd_config_* read the value as it is now, and wwhd_config_changed(id) returns its
//   revision (1 at start, one more per change; 0 for an option the mod doesn't have). Options without "live"
//   keep their start value (the page says the change applies at the next start).
// - Input filters: each mod may register one callback that gets the controller state of each of the game's
//   reads, after the port's own (touch controls, talks by touch), in the mods' load order, and may change it.
// - Touches on the TV picture: a mod claims rectangles (layout units: 1280 x 720 around the centre, +y up);
//   a finger landing in one goes to that mod (down, moves, up) and to nothing else (no stick, no talk); without a
//   claim at that point the port's own rules decide. The newest claim wins where claims overlap.
// - Pinches: a second finger next to a mod's finger (Android), or the mouse wheel over a claim (desktop), goes to
//   that mod as pinch events: the spread now / at the pinch's start, and its end.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace guestlive {

// ---- live options: revisions per mod and option
class Revisions {
public:
    // the option exists for this mod (at load): revision 1
    void declare(const std::string& mod, const std::string& option) { revs_[mod].emplace(option, 1); }
    // a live change: one more (an unknown option is not created)
    bool bump(const std::string& mod, const std::string& option) {
        auto m = revs_.find(mod);
        if (m == revs_.end()) return false;
        auto o = m->second.find(option);
        if (o == m->second.end()) return false;
        o->second++;
        return true;
    }
    uint64_t revision(const std::string& mod, const std::string& option) const {
        auto m = revs_.find(mod);
        if (m == revs_.end()) return 0;
        auto o = m->second.find(option);
        return o == m->second.end() ? 0 : o->second;
    }

private:
    std::map<std::string, std::map<std::string, uint64_t>> revs_;
};

// ---- touches
enum Kind : uint32_t { kDown = 0, kMove = 1, kUp = 2, kCancel = 3, kPinch = 4, kPinchEnd = 5 };
struct Event {
    uint32_t kind = kDown;
    float x = 0, y = 0;
};
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool contains(float x, float y) const { return x1 > x0 && y1 > y0 && x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};
constexpr int kSlots = 8;  // rectangles per mod

class Touch {
public:
    // a mod's rectangle in a slot (x1 <= x0 or y1 <= y0 removes it); `order` grows with each claim call
    void claim(int mod, int slot, Rect r) {
        if (slot < 0 || slot >= kSlots || mod < 0) return;
        if (size_t(mod) >= mods_.size()) mods_.resize(size_t(mod) + 1);
        Claim& c = mods_[size_t(mod)].slots[slot];
        c.rect = r;
        c.order = ++order_;
    }
    void drop(int mod) {
        if (mod >= 0 && size_t(mod) < mods_.size()) mods_[size_t(mod)] = {};
        if (owner_ == mod) owner_ = -1;
        if (pinch_owner_ == mod) pinch_owner_ = -1;
    }
    // the mod a finger landing at (x, y) belongs to, -1 none (the newest claim containing it)
    int owner_at(float x, float y) const {
        int best = -1;
        uint64_t order = 0;
        for (size_t m = 0; m < mods_.size(); m++)
            for (const Claim& c : mods_[m].slots)
                if (c.rect.contains(x, y) && c.order > order) best = int(m), order = c.order;
        return best;
    }
    bool any() const {
        for (const ModClaims& m : mods_)
            for (const Claim& c : m.slots)
                if (c.rect.x1 > c.rect.x0 && c.rect.y1 > c.rect.y0) return true;
        return false;
    }
    // One finger change (the port's single tracked finger). Returns the mod that gets it, -1: not a mod's (the
    // port's own rules have it). A finger that went down in a mod's claim stays that mod's until it lifts.
    int route(const Event& e) {
        if (e.kind == kDown) {
            owner_ = owner_at(e.x, e.y);
            return owner_;
        }
        const int o = owner_;
        if (e.kind == kUp || e.kind == kCancel) owner_ = -1;
        return o;
    }
    int finger_owner() const { return owner_; }
    // One pinch change (kPinch: x = the spread now / at its start; kPinchEnd). The pinch belongs to the mod that has
    // the finger now, else to the one claiming its point (x, y: the desktop's wheel, where the pointer is), until it
    // ends. Returns that mod, -1: not a mod's (the port's own pinch has it).
    int route_pinch(uint32_t kind, float x, float y) {
        if (pinch_owner_ < 0 && kind == kPinch) pinch_owner_ = owner_ >= 0 ? owner_ : owner_at(x, y);
        const int o = pinch_owner_;
        if (kind == kPinchEnd) pinch_owner_ = -1;
        return o;
    }
    bool pinching() const { return pinch_owner_ >= 0; }

private:
    struct Claim {
        Rect rect;
        uint64_t order = 0;
    };
    struct ModClaims {
        Claim slots[kSlots];
    };
    std::vector<ModClaims> mods_;
    uint64_t order_ = 0;
    int owner_ = -1;
    int pinch_owner_ = -1;
};

// ---- touches on the GamePad picture: claims in its touch coordinates (854 x 480 from the top left, +y down) with
// the TV picture's rules (a Touch of their own). The GamePad has one finger, read with the controller (PadState touch,
// tx/ty 0..1 from the top left), wherever it shows: a GamePad window, a phone, One screen mode.
constexpr float kGamepadWidth = 854, kGamepadHeight = 480;
constexpr uint32_t kGamepadFlag = 0x100;  // WWHD_TOUCH_GAMEPAD: in a claim's slot, and in the kind of its events
struct PadFinger {
    bool down = false, claimed = false;
    float x = 0, y = 0;
    // One controller read. Returns true while the finger must not reach the game (it landed in a claim; it stays hidden
    // until it lifts, even if the claiming mod goes away meanwhile). `owner` >= 0 with `e`: that mod gets an event now.
    bool step(Touch& claims, bool touch, float tx, float ty, int& owner, Event& e) {
        owner = -1;
        const float nx = tx * kGamepadWidth, ny = ty * kGamepadHeight;
        uint32_t kind;
        if (touch && !down) kind = kDown;
        else if (touch && (nx != x || ny != y)) kind = kMove;
        else if (!touch && down) kind = kUp;
        else return touch && claimed;
        if (touch) x = nx, y = ny;
        down = touch;
        owner = claims.route({kind, x, y});
        if (kind == kDown) claimed = owner >= 0;
        if (owner >= 0) e = {kind | kGamepadFlag, x, y};
        if (!touch) claimed = false;
        return touch && claimed;
    }
};

// ---- the drawn camera as a projection to layout units (wwhd_screen_point): eye, center, up of the camera and k, layout units
// per unit of x / z (aspect::layout_scale); +x right, +y up, 0 at the picture's centre
struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 norm(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a;
}
struct View {
    V3 eye, f, r, u;
    float k = 0;
    bool ok = false;
    bool set(V3 e, V3 center, V3 up, float scale) {
        eye = e;
        f = norm(sub(center, e));
        r = norm(cross(f, up));
        u = cross(r, f);
        k = scale;
        ok = std::isfinite(k) && k > 0 && dot(f, f) > 0.5f && dot(r, r) > 0.5f;
        return ok;
    }
    bool project(V3 p, float& x, float& y) const {
        if (!ok) return false;
        const V3 d = sub(p, eye);
        const float z = dot(d, f);
        if (!(z > 1)) return false;
        x = dot(d, r) / z * k;
        y = dot(d, u) / z * k;
        return std::isfinite(x) && std::isfinite(y);
    }
};

}  // namespace guestlive
