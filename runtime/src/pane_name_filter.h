// SPDX-License-Identifier: MPL-2.0
// Which layout panes the per-pane checks of Pane::CalculateMtx and Pane::Draw (aspect.cpp) can be about.
//
// Those hooks run for every pane of every layout, every frame (about 600 matrix calculations and up to 300
// draws per frame on the game thread), and each asked several features by name: the HUD fades, One screen's
// hidden guide, the large Picto Box frame, the touch controls' cluster, the play context's panes and the
// button prompts. All of them compare the pane's name with fixed names, so a pane whose name is none of them
// gets "no" from every one. Here the name is read once (three 8-byte words, no string calls) and looked up
// in a table of every fixed name those checks use; a pane not in it skips the checks.
//
// kNames must hold every name the checks compare (a superset is harmless: such a pane only runs the
// checks as before). pane_name_filter_test checks the table, and scans the checks' source files so that a new
// name added there without adding it here fails the test.
// WWHD_PANE_NAME_FILTER=0 turns the filter off (every pane runs every check, as before).
#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace pane_name_filter {

inline constexpr std::string_view kNames[] = {
    // HUD fades (mods/hud_fade.cpp kPanes) and the touch controls' hidden cluster (kCluster)
    "L_HeartAll_00", "L_Rupy_00", "L_MagicBar_00",
    "N_X_00", "N_Y_00", "N_R_00", "P_B_00", "P_ZR_00", "L_CommandA_00", "W_SetSeatA_00", "P_SetSeatASpecial_00",
    // the large Picto Box frame (hud_fade.cpp) and One screen's slide guide (mods/one_screen.cpp)
    "N_Frame_00", "N_SlideInput_00",
    // the play context's panes (mods/play_context.h classify_pane)
    "L_BtnSeqenceL_00", "L_BtnSeqenceR_00", "L_BtnSeqenceC_00", "L_KeyIcon_00", "W_Choise2_00", "W_Choise3_00",
    "W_Cursor_00", "P_SelNumber_00", "P_MsgWindow_00", "P_Paper_00", "P_Stone_00", "P_Wood_00",
    "P_WindDirectionBase_00", "N_Finder_00", "N_PictographAll_00", "N_Zoom_00", "P_Bar_00", "P_TelescopeBar_00",
    // button prompts (button_glyphs.cpp / button_glyphs.h): face groups, plates, letters, the cluster
    "N_All_00", "N_Center_01", "P_L_00", "P_R_00", "P_ZL_00", "P_ZR_00",
    "P_A_00", "P_ASpecial_00", "P_X_00", "P_Y_00",
    "T_A_00", "T_A_01", "T_B_00", "T_X_00", "T_Y_00", "T_L_00", "T_R_00", "T_ZL_00", "T_ZR_00",
};
// names checked by prefix (button_glyphs.cpp: the action words T_CommandA_00 ... T_CommandZR_00)
inline constexpr std::string_view kPrefix = "T_Command";

struct Key {
    uint64_t w[3];
    bool operator==(const Key&) const = default;
};

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "key_of finds the first NUL with little-endian words");

// a 24-byte name field as the checks see it (up to its first NUL, at most 24 bytes), the bytes after it zero
inline Key key_of(const char* name24) {
    Key k;
    std::memcpy(k.w, name24, 24);
    constexpr uint64_t kOnes = 0x0101010101010101ull, kHighs = 0x8080808080808080ull;
    for (int i = 0; i < 3; i++) {
        const uint64_t zero = (k.w[i] - kOnes) & ~k.w[i] & kHighs;  // the lowest set bit marks the first zero byte
        if (!zero) continue;
        const int bit = __builtin_ctzll(zero) - 7;  // that byte's first bit
        k.w[i] &= bit ? (~0ull >> (64 - bit)) : 0;
        for (int r = i + 1; r < 3; r++) k.w[r] = 0;
        break;
    }
    return k;
}

inline Key key_of(std::string_view name) {
    char buf[24] = {};
    std::memcpy(buf, name.data(), name.size() < 24 ? name.size() : 24);
    return key_of(buf);
}

inline uint32_t slot_of(const Key& k) {
    const uint64_t h = (k.w[0] * 0x9E3779B97F4A7C15ull) ^ (k.w[1] * 0xC2B2AE3D27D4EB4Full) ^ (k.w[2] * 0x165667B19E3779F9ull);
    return uint32_t(h >> 57);  // 128 slots
}

struct Table {
    std::array<Key, 128> keys{};
    std::array<bool, 128> used{};
    Key prefix{};
    uint64_t prefix_mask0 = 0;
    unsigned char prefix_byte8 = 0;
    Table() {
        for (std::string_view n : kNames) {
            const Key k = key_of(n);
            uint32_t s = slot_of(k);
            while (used[s] && !(keys[s] == k)) s = (s + 1) & 127;
            keys[s] = k, used[s] = true;
        }
        static_assert(kPrefix.size() == 9, "the prefix test reads 8 bytes and one more");
        prefix = key_of(kPrefix);
        prefix_byte8 = (unsigned char)kPrefix[8];
    }
};
inline const Table& table() {
    static const Table t;
    return t;
}

// Could one of the checks be about a pane with this name field? Exact: a name in kNames, or kPrefix + more.
inline bool known(const char* name24) {
    const Table& t = table();
    if (std::memcmp(name24, &t.prefix.w[0], 8) == 0 && (unsigned char)name24[8] == t.prefix_byte8) return true;
    const Key k = key_of(name24);
    for (uint32_t s = slot_of(k);; s = (s + 1) & 127) {
        if (!t.used[s]) return false;
        if (t.keys[s] == k) return true;
    }
}

inline bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WWHD_PANE_NAME_FILTER");
        return !(e && *e == '0');
    }();
    return on;
}

// true: none of the checks is about this pane name, they can be skipped
inline bool skip(const char* name24) { return enabled() && !known(name24); }

}  // namespace pane_name_filter
