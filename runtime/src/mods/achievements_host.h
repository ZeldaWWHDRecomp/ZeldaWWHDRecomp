// SPDX-License-Identifier: MPL-2.0
// Built-in mod "Achievements" (achievements.cpp has the details): the switch, its options, what the pages show,
// and the game thread's service.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "achievements_goals.h"

struct Cpu;

namespace achievements {

bool enabled();
void set_enabled(bool on);

struct Settings {
    bool popup = true;        // the pop-up on screen
    bool link = true;         // ... with Link holding it up
    bool jingle = true;       // ... and the item fanfare
    float volume = 0.8f;      // its volume (0..1)
    bool each_file = true;    // pop-ups in every save game (off: only what no save game had before)
    bool with_cheats = false; // "Earn with cheats": cheats don't pause them
};
Settings settings();
void set_settings(const Settings& s);  // (the caller saves them: mod.achievements.*)

// what the pages show (any thread)
struct View {
    struct Row {
        std::string key, title, desc;
        Group group;
        unsigned points;
        int64_t all = 0, file = 0;  // when earned (ms since 1970) for all save games / the one in play; 0 not yet
    };
    std::vector<Row> rows;
    std::string file;                  // the save game in play, empty none
    std::vector<std::string> pausing;  // what pauses them now
};
View view();
// every achievement open again (the record for all save games and for each); main or game thread
void reset();

// the game opened a file (fs.cpp): remember the host path of its sound archive (the fanfare)
void note_open(const std::string& guest_path, const std::string& host_path);
// once per full logic step (interp.cpp, game thread)
void service(Cpu* c);

}  // namespace achievements
