// The pause menu's extra focus stop. The original Left is always delivered to the game.
// Only an absent neighbour AND an unchanged focus after the game has processed it can
// offer the stop. No coordinate thresholds, slot indices or page numbers select it.
//
// USA runtime observations (2026-11-02, each Left delivered unchanged):
// L_BtnMenu_00 (ITEMS), L_MenuSave_00, L_MenuOption_00, L_BtnItemIcon_05
// and the bottom-left L_BtnItemIcon_12 all have a null Left link and stay put.
// L_BtnItemIcon_13 (the adjacent bottom slot) links Left to _12 and moves there.
// The former x < -300 rule incorrectly intercepted that live _13 -> _12 move too.
#pragma once
#include <cstdint>

namespace game_settings_page {

inline bool entry_exists(bool enabled, bool right, bool left, bool back) {
    return enabled && right && !left && !back;
}

struct EntryPosition {
    uint32_t menu = 0, receiver = 0, selected = 0, mode = 0;
    float offset = 0, x = 0, y = 0;
    bool operator==(const EntryPosition&) const = default;
};

class EntryFocus {
public:
    // `frame` is the game's TV frame, not the number of controller reads (which can repeat).
    // Wait for release and four frames so a queued navigation event has time to run.
    bool update(bool eligible, bool left, bool neighbour, const EntryPosition& position, uint64_t frame) {
        const bool pressed = left && !left_;
        left_ = left;
        if (!eligible || !position.receiver) { clear(); return false; }
        if (focused_ && position != origin_) clear();
        if (pending_ && (position != origin_ || neighbour)) pending_ = false;
        if (pending_ && !left && frame >= frame_ + 4) {
            pending_ = false;
            focused_ = true;
            return true;
        }
        if (!focused_ && pressed && !neighbour) {
            origin_ = position;
            frame_ = frame;
            pending_ = true;
        }
        return false;
    }
    bool focused() const { return focused_; }
    void clear() { pending_ = focused_ = false; }
private:
    bool left_ = false, pending_ = false, focused_ = false;
    EntryPosition origin_;
    uint64_t frame_ = 0;
};

} // namespace game_settings_page
