// Setup's title-hosted model sweep (dsweep.cpp, WWHD_DSWEEP=title): getting to the title and keeping it (#690).
//
// The boot's notice screen waits for A, so the sweep presses A (pad_buttons) until the title is up. The title
// (daTitle, decomp wwhd_src/d/actor/d_a_title.cpp) takes any trigger in its intro (daTitle_proc_c::proc_execute,
// mEnterMode ENTER_INTRO, TITLE_TRIGGER_MASK 0x861B) as "enter the game": ENTER_LEAVING, and daTitle_Execute then
// changes the scene to the file select (titleChangeScene(..., TITLE_NAME_SCENE, ...), the "Name" stage). The old
// pulses (6 frames in every 60) went on until the first actor draw in the title's stage, and the title executes
// (reads the pad) a step before that draw: a pulse that began in that step opened the file select, about one start
// in 60 (where the pulse falls depends on how fast the start-up ran), and the sweep never began. The stage's name
// is the title's ("sea_T") from the play scene's phase_1 on (decomp d_s_play_4.cpp, setStartStage), long before
// the title actor exists, so the pulses now end there.
#pragma once
#include <cstdint>
#include <string>

namespace dsweep_title {

struct BootPulse {
    uint64_t first = 120;  // frames before the first pulse
    uint64_t period = 60, width = 6;  // A in the first `width` frames of every `period` (frames: TV frames)
    bool gate = true;  // #690: no A once the stage is the title's; false: until the title's first actor draw (old)
};

// A on the way to the title (the boot's notice screen), before the title's first actor draw (title_drawn).
inline bool boot_a(const BootPulse& p, uint64_t frame, const std::string& stage, bool title_drawn) {
    if (title_drawn || frame < p.first || p.period == 0 || frame % p.period >= p.width) return false;
    return !(p.gate && stage == "sea_T");
}

// The file select ("Name") came up in a title sweep that did not open it itself.
enum class Lost {
    None,
    EndAttempt,  // before the sweep began: end the process; setup starts it again (nothing to skip)
    Restart,     // after: an orderly restart ("restart" in progress.txt); the next attempt resumes where it was
};
// title_seen: the title stage drew an actor once; exempt: a phase that handles stage changes on its own (the file
// select, the visits, the actors: an actor that changes the stage is skipped) or the sweep is over; begun: the sweep
// started (its "start" line in progress.txt)
inline Lost title_lost(bool title_seen, const std::string& stage, bool exempt, bool begun) {
    if (!title_seen || stage != "Name" || exempt) return Lost::None;
    return begun ? Lost::Restart : Lost::EndAttempt;
}

}  // namespace dsweep_title
