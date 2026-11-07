// SPDX-License-Identifier: MPL-2.0
// A randomized game made or imported from inside the game (Settings > Randomizer), while the game keeps running.
// The work is setup's own: on the desktop the installed setup runs in the background (setup.py --randomize or
// --import-seed with --code-mods-status, the same way the in-game code-mod rebuild runs it: code_mods.cpp);
// on Android the phone's setup service runs a randomizer job (WwhdActivity.requestRandomizer, the same job the
// setup screen's Randomized game... creates). Either way the seed is made, checked by the compatibility check,
// its game code is built, and it becomes the game the next start plays (with its own saves).
#pragma once
#include <string>

namespace randomizer_job {

struct Request {
    bool import = false;     // a seed made with the official randomizer (folder), else a new game
    std::string preset;      // beginner, standard, everything (a new game without a permalink)
    std::string seed;        // the seed name ("" a new random one)
    std::string permalink;   // instead of preset and seed: its settings and seed
    std::string folder;      // import (desktop): the official randomizer's output folder
};

struct Status {
    bool running = false;    // setup is working on it (Android: also while the folder picker is open)
    bool ready = false;      // finished: the next start plays the new game
    std::string stage;       // what setup does now
    unsigned done = 0, total = 0;
    std::string error;       // finished with this failure (the game the next start plays is unchanged)
    std::string seed, hash;  // ready: the seed's id and hash words ("" for an imported seed)
    bool imported = false;
};

// "" when this game can make randomized games itself; else why not, for the player
std::string unavailable();
// Starts setup's work (the request's fields as described above). False and why when it can't start now.
bool start(const Request& request, std::string& why);
Status status();
// Stops at setup's next step (desktop; on Android the setup screen and its notification pause the job).
void cancel();
// Forgets a finished result (ready or failed) once the player has seen it.
void dismiss();

}  // namespace randomizer_job
