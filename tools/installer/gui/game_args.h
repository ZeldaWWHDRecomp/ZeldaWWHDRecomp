// Game options (issue #266): the arguments of Wind Waker HD.exe / Wind Waker HD.app / wind-waker-hd that are
// meant for the game, not for setup. The launcher appends them to the game's command line when it starts the
// game. The same rule as split_game_args in tools/installer/setup.py (test_setup.py compares the lists):
// the flags in kGameFlags, the options in kGameValueOptions with their value (--renderer NAME or
// --renderer=NAME), and everything after "--".
#pragma once

#include <string>
#include <vector>

namespace game_args {

inline const char* const kGameFlags[] = {"--editor", "--trace", "--warm-shaders"};
inline const char* const kGameValueOptions[] = {"--renderer"};

// the game options among args (a launcher's arguments, its own options already removed), in their order
inline std::vector<std::string> of(const std::vector<std::string>& args) {
    std::vector<std::string> game;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        if (a == "--") {
            game.insert(game.end(), args.begin() + i + 1, args.end());
            break;
        }
        bool take = false;
        for (const char* f : kGameFlags) take = take || a == f;
        for (const char* o : kGameValueOptions) {
            std::string eq = std::string(o) + "=";
            if (a.compare(0, eq.size(), eq) == 0) take = true;
            else if (a == o && i + 1 < args.size()) {
                game.push_back(a);
                game.push_back(args[++i]);
                take = false;
                break;
            }
        }
        if (take) game.push_back(a);
    }
    return game;
}

}  // namespace game_args
