#pragma once
// Characters a name the game keeps may not contain. The game formats names as printf-style format
// strings (setCardName, mods/player_name.cpp), on the name screen and at every start when the names
// come back from cking.sav, so a "%" could read or write anywhere. Used by the system keyboard
// (hle/swkbd.cpp, the new-game name) and the rename (mods/player_name.cpp).
#include <string>

namespace game_name {

inline const std::u16string kRefused = u"%";

inline std::u16string without_refused(std::u16string s) {
    std::erase_if(s, [](char16_t ch) { return kRefused.find(ch) != std::u16string::npos; });
    return s;
}

}  // namespace game_name
