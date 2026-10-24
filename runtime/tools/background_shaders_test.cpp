// Settings > Graphics > Shader compile (background_shaders.h): which draws Fast may leave out while their
// shaders compile (classify, Tracker), the setting (Off by default, switched at once, WWHD_BG_SHADERS
// wins, the removed "safe" reads as Off), the wait budget and the performance report line.
// Run as: background_shaders_test (setting), ... env-fast, ... env-off, ... env-safe, ... budget.
#include "background_shaders.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// portable environment edits (no setenv/unsetenv on Windows)
static void set_env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key, value, 1); else unsetenv(key);
#endif
}

static std::vector<std::string> logs;
void log_msg(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    logs.push_back(buf);
}
static bool logged(const char* text) {
    for (const auto& l : logs)
        if (l.find(text) != std::string::npos) return true;
    return false;
}
static bool has(const std::string& s, const char* text) { return s.find(text) != std::string::npos; }

using namespace bgshaders;

// Fast: anything may be left out except what the game reads back or keeps
static void test_classify() {
    TargetFacts color, depth, texture, shadow;
    color.redrawn = color.regular = true;      // the picture
    depth.redrawn = depth.regular = true;      // its depth buffer
    texture.redrawn = texture.regular = true;  // a reflection or post-effect target, redrawn every frame
    shadow = texture;                          // a shadow map's depth buffer
    assert(classify(kLevelOff, &color, 1, &depth) == kOff);
    assert(classify(kLevelFast, &color, 1, &depth) == kSkippable);
    assert(classify(kLevelFast, &color, 1, nullptr) == kSkippable);
    assert(classify(kLevelFast, &texture, 1, nullptr) == kSkippable);  // render to texture
    assert(classify(kLevelFast, nullptr, 0, &shadow) == kSkippable);   // shadow map
    TargetFacts mrt[2] = {color, texture};
    assert(classify(kLevelFast, mrt, 2, &depth) == kSkippable);
    // the depth buffer of the sun's glare test: an answer from an incomplete frame would change the game
    TargetFacts peeked = depth;
    peeked.peeked = true;
    assert(classify(kLevelFast, &color, 1, &peeked) == kReadBack);
    assert(classify(kLevelFast, &color, 1, nullptr) == kSkippable);  // a draw that only tests it
    // the first frame after a load: the target was redrawn regularly before
    TargetFacts afterLoad = color;
    afterLoad.redrawn = false;
    assert(classify(kLevelFast, &afterLoad, 1, &depth) == kSkippable);
    // a new target, or one drawn only now and then (a one-off capture: a photo, a snapshot)
    TargetFacts oneOff = texture;
    oneOff.redrawn = oneOff.regular = false;
    assert(classify(kLevelFast, &oneOff, 1, nullptr) == kFirstFrame);
    assert(classify(kLevelFast, nullptr, 0, &oneOff) == kFirstFrame);
    TargetFacts mixed[2] = {color, oneOff};
    assert(classify(kLevelFast, mixed, 2, &depth) == kFirstFrame);
    TargetFacts drawnLastFrame = oneOff;  // a new target in its second frame
    drawnLastFrame.redrawn = true;
    assert(classify(kLevelFast, &drawnLastFrame, 1, nullptr) == kSkippable);
    // read by the CPU, copied from now and then: exact
    TargetFacts read = texture;
    read.readBack = true;
    assert(classify(kLevelFast, &read, 1, nullptr) == kReadBack);
    TargetFacts readDepth = depth;
    readDepth.readBack = true;
    assert(classify(kLevelFast, nullptr, 0, &readDepth) == kReadBack);
    TargetFacts copied = color;
    copied.copiedOccasionally = true;
    assert(classify(kLevelFast, &copied, 1, &depth) == kCopied);
    TargetFacts copiedDepth = depth;
    copiedDepth.copiedOccasionally = true;
    assert(classify(kLevelFast, &color, 1, &copiedDepth) == kCopied);
    for (int v = 0; v < kVerdicts; v++) assert(strcmp(verdict_name(Verdict(v)), "?") != 0);
}

static void test_tracker() {
    Tracker t;
    const uint32_t scene = 0x10000000, photo = 0x30000000, linear = 0x40000000, every = 0x50000000;
    // nothing known: a new target
    TargetFacts f = t.facts(scene, 10);
    assert(!f.redrawn && !f.regular && !f.readBack && !f.peeked && !f.copiedOccasionally);
    t.drawn(scene, 1);
    t.drawn(scene, 2);
    assert(t.facts(scene, 3).redrawn && !t.facts(scene, 3).regular);  // two frames are not yet regular
    t.drawn(scene, 3);
    t.drawn(scene, 3);  // drawn again in the same frame: still one frame
    assert(t.facts(scene, 4).regular && t.facts(scene, 4).redrawn);
    assert(!t.facts(scene, 6).redrawn && t.facts(scene, 6).regular);  // a gap: regular stays known
    // a target drawn every other frame never becomes regular
    for (uint64_t frame = 10; frame < 30; frame += 2) t.drawn(every, frame);
    assert(!t.facts(every, 31).regular);
    // copies in every frame (a refraction copy, an effect's input) refresh their result every frame
    for (uint64_t frame = 20; frame < 24; frame++) {
        t.drawn(scene, frame);
        assert(!t.copied(scene, frame));
    }
    f = t.facts(scene, 24);
    assert(f.redrawn && !f.copiedOccasionally);
    // a single copy (a photo, a menu background) keeps the target exact for kQuietFrames
    for (uint64_t frame = 30; frame < 40; frame++) t.drawn(photo, frame);
    t.copied(photo, 39);
    for (uint64_t frame = 40; frame < 39 + kQuietFrames; frame++) {
        t.drawn(photo, frame);
        assert(t.facts(photo, frame + 1).copiedOccasionally);
    }
    assert(!t.facts(photo, 40 + kQuietFrames).copiedOccasionally);
    // a second copy after a gap starts a new run: occasional again
    t.copied(photo, 300);
    assert(t.facts(photo, 301).copiedOccasionally);
    // the copy of a target in the same frame that left a draw into it out is counted
    reset_counters();
    t.left_out(photo, 302);
    assert(t.left_out_in(photo, 302) && !t.left_out_in(photo, 303) && !t.left_out_in(linear, 302));
    assert(t.copied(photo, 302));
    assert(counters().copiesAfterSkip == 1);
    assert(!t.copied(photo, 303));
    // read by the CPU or depth-peeked: exact for kQuietFrames
    t.drawn(linear, 50);
    t.read_back(linear, 50);
    assert(t.facts(linear, 51).readBack && t.facts(linear, 50 + kQuietFrames).readBack);
    assert(!t.facts(linear, 51 + kQuietFrames).readBack);
    t.peeked(scene, 60);
    assert(t.facts(scene, 61).peeked && !t.facts(scene, 61 + kQuietFrames).peeked);
    // a loaded save state: frame facts go, regular targets are remembered
    t.peeked(scene, 400);
    t.forget();
    f = t.facts(scene, 500);
    assert(f.regular && !f.redrawn && !f.peeked);
    assert(!t.facts(linear, 51).readBack && !t.facts(every, 31).redrawn);
    TargetFacts picture = f;  // the first frame after the load may be left out
    assert(classify(kLevelFast, &picture, 1, nullptr) == kSkippable);
}

static void test_budget_and_counters() {
    reset_counters();
    const uint64_t budget = uint64_t(wait_budget_ms() * 1e6);
    assert(budget_left_ns(7) == budget);
    spent(7, kFastWaitNs / 2, false);  // a fast result (a warm cache) uses none of the budget
    assert(budget_left_ns(7) == budget);
    spent(7, budget / 2, false);
    assert(budget_left_ns(7) == budget - budget / 2);
    spent(7, budget, true);
    assert(budget_left_ns(7) == 0);  // the frame's budget is used up: later draws are left out at once
    assert(budget_left_ns(8) == budget);  // a new frame, a new budget
    assert(!skipped_in_frame(8));
    skipped(8);
    skipped(8);
    skipped(9);
    assert(skipped_in_frame(9) && !skipped_in_frame(8));
    peek_withheld();
    exact(kFirstFrame);
    exact(kReadBack);
    exact(kReadBack);
    const Counters c = counters();
    assert(c.skipped == 3 && c.skipFrames == 2 && c.budgetWaits == 3 && c.budgetRanOut == 1 && c.peeksWithheld == 1);
    assert(c.exact[kFirstFrame] == 1 && c.exact[kReadBack] == 2);
    const std::string line = report_line();
    assert(has(line, "Draws left out while compiling: 3 in 2 frames"));
    assert(has(line, "read back 2") && has(line, "new or one-off target 1") && has(line, "depth peeks kept: 1"));
    reset_counters();
    assert(has(report_line(), "Exact waits: none"));
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "budget") {  // WWHD_BG_SHADER_WAIT_MS sets the frame's budget
        set_env("WWHD_BG_SHADER_WAIT_MS", "2.5");
        assert(wait_budget_ms() == 2.5);
        reset_counters();
        assert(budget_left_ns(1) == 2500000);
        return 0;
    }
    if (mode == "env-fast" || mode == "env-off" || mode == "env-safe") {
        const char* value = mode == "env-fast" ? "fast" : mode == "env-off" ? "0" : "safe";
        const Level want = mode == "env-fast" ? kLevelFast : kLevelOff;
        set_env("WWHD_BG_SHADERS", value);
        start(want == kLevelFast ? kLevelOff : kLevelFast);  // the saved setting differs: the variable wins
        assert(level() == want && env_override());
        assert(logged((std::string("shader compile ") + level_name(want) + " (WWHD_BG_SHADERS=" + value).c_str()));
        if (mode == "env-safe") assert(logged("the Safe level was removed; using 0 (Off)"));
        set_level(want);                                          // a change of the setting is logged ...
        set_level(want == kLevelFast ? kLevelOff : kLevelFast);   // ... but the variable still decides
        assert(level() == want);
        assert(logged("decides for this start"));
        assert(has(report_line(), (std::string("Shader compile: ") + level_name(want) + " (WWHD_BG_SHADERS=").c_str()));
        return 0;
    }
    set_env("WWHD_BG_SHADERS", nullptr);
    set_env("WWHD_BG_SHADER_WAIT_MS", nullptr);
    assert(!strcmp(kSettingKey, "backgroundShaders"));
    // saved values: "0" or "fast"; never saved, and the removed "safe", mean Off
    assert(parse_level("fast") == kLevelFast && parse_level("0") == kLevelOff);
    assert(parse_level("safe") == kLevelOff && parse_level("1") == kLevelOff);
    assert(parse_level(nullptr) == kLevelOff && parse_level("") == kLevelOff);
    for (int l = 0; l < kLevels; l++) assert(parse_level(level_value(Level(l))) == Level(l));
    // default: never saved means Off
    assert(!env_override() && level() == kLevelOff && !enabled());
    start(parse_level(nullptr));
    assert(level() == kLevelOff && logged("shader compile off (setting)"));
    assert(wait_budget_ms() == kDefaultWaitMs);
    // switched at once, each change logged once
    logs.clear();
    set_level(kLevelFast);
    assert(level() == kLevelFast && enabled() && logged("shader compile fast (setting)"));
    set_level(kLevelFast);
    assert(logs.size() == 1);
    set_level(kLevelOff);
    assert(!enabled() && logged("shader compile off (setting)"));
    // a saved level is in effect from the start
    start(kLevelFast);
    assert(level() == kLevelFast && requested() == kLevelFast);
    start(kLevelOff);
    test_classify();
    test_tracker();
    test_budget_and_counters();
    assert(has(report_line(), "Shader compile: off (setting)"));
    puts("background_shaders_test: ok");
    return 0;
}
