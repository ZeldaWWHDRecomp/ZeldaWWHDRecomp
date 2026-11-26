// The sound system on a general step length (t60_audio.h). Decomp build only.
#include "t60/t60_audio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "runtime.h"
#include "t60/t60.h"

namespace interp { bool enabled(); bool hold_pass(); uint64_t logic_steps(); }

namespace t60::audio {
namespace {
const bool g_switch = [] {
    const char* e = getenv("WWHD_T60_AUD");
    return !e || atoi(e) != 0;
}();
const bool g_stats = getenv("WWHD_T60_AUD_STATS") != nullptr;
bool on() { return g_switch && active(); }  // (true 60 runs this 30 Hz frame at N > 1)
// once per 30 Hz frame: the frame (record step) the work last ran for
struct Once {
    uint32_t frame = ~0u;
    bool take() {
        if (frame == wwhd_t60_frame) return false;
        frame = wwhd_t60_frame;
        return true;
    }
};
Once g_ctl, g_player, g_camtimer, g_camgroup, g_cammap;
int g_ui_depth = 0;
uint32_t g_n[kWhatN][2][4];  // [what][ran][pass kind]
uint64_t g_t0 = 0;
int pass_kind() {
    if (!interp::enabled()) return 0;
    if (!interp::hold_pass()) return 1;  // (the logic pass: the tick30 step)
    return sub_step() ? 2 : 3;
}
}  // namespace

bool ctl_runs(bool interp_rule) {
    bool ran = interp_rule;
    if (on()) ran = tick30() && g_ctl.take();
    count(kCtl, ran);
    return ran;
}
bool player_runs() {
    // N = 1 with frame interpolation: once per logic step (the frame loop calls it on every displayed pass, so the fades
    // ran at double speed at 60 fps; the normal port's PR #627 hook_0202FA3C rule, here in the decompiled function's
    // gate). debug: WWHD_SND_PLAYER_EVERY_PASS=1 restores the every-pass behaviour.
    static const bool every = getenv("WWHD_SND_PLAYER_EVERY_PASS") != nullptr;
    static uint64_t done_for = ~0ull;
    bool ran = true;
    if (on()) {
        ran = tick30() && g_player.take();
    } else if (interp::enabled() && !every) {
        ran = done_for != interp::logic_steps();
        if (ran) done_for = interp::logic_steps();
    }
    count(kPlayer, ran);
    return ran;
}
bool camera_timer_counts() {
    // JAIZelBasic::getCameraInfo's mNoCameraTimer: one count per camera hand-over of the original's frame. The listener
    // takes the camera twice per logic step while interpolating (the exact pre-run and the record pass, interp_listener.h),
    // and true 60's tick30 draw likewise: counted once per 30 Hz frame (true 60) / once per logic step (interpolation)
    static const bool every = getenv("WWHD_SND_PLAYER_EVERY_PASS") != nullptr;
    static uint64_t done_for = ~0ull;
    bool ran = true;
    if (on()) {
        ran = g_camtimer.take();
    } else if (interp::enabled() && !every) {
        ran = done_for != interp::logic_steps();
        if (ran) done_for = interp::logic_steps();
    }
    count(kCamTimer, ran);
    return ran;
}
bool camera_group_counts() {
    // JAIZelBasic::setCameraGroupInfo (camera_draw's audio pass): the camera's ground group can switch the audio scene;
    // the same double hand-over as the camera timer (exact pre-run + record pass): taken on the frame's first one only
    static const bool every = getenv("WWHD_SND_PLAYER_EVERY_PASS") != nullptr;
    static uint64_t done_for = ~0ull;
    bool ran = true;
    if (on()) {
        ran = g_camgroup.take();
    } else if (interp::enabled() && !every) {
        ran = done_for != interp::logic_steps();
        if (ran) done_for = interp::logic_steps();
    }
    count(kCamGroup, ran);
    return ran;
}
bool camera_map_counts() {
    // JAIZelBasic::getCameraMapInfo (camera_draw's audio pass: the material sound id of the ground under the eye). On a
    // fallback frame with interpolation the record pass draws a BLENDED camera after the exact pre-run, and its ground
    // flipped the map info back for a frame (mdT2 trace, frame 369: pre-run sound 3, record pass 0xB at a boundary):
    // the frame's first hand-over only, as the camera timer / group
    static const bool every = getenv("WWHD_SND_PLAYER_EVERY_PASS") != nullptr;
    static uint64_t done_for = ~0ull;
    bool ran = true;
    if (on()) {
        ran = g_cammap.take();
    } else if (interp::enabled() && !every) {
        ran = done_for != interp::logic_steps();
        if (ran) done_for = interp::logic_steps();
    }
    count(kCamMap, ran);
    return ran;
}
bool se_step_logic() { return g_ui_depth > 0 && sub_step(); }
void ui_step(bool enter) { g_ui_depth += enter ? 1 : -1; }

void count(What w, bool ran) {
    if (!g_stats) return;
    g_n[w][ran ? 1 : 0][pass_kind()]++;
    const uint64_t t = timebase::now();
    if (!g_t0) g_t0 = t;
    if (t - g_t0 < 5 * timebase::kTicksPerSec) return;
    static const char* names[kWhatN] = {"ctl", "player", "listener", "polygon", "se", "se-held", "camtimer", "camgroup", "cammap"};
    char buf[900];
    int k = snprintf(buf, sizeof buf, "[t60] audio per 5 s, ran/skipped [30Hz tick30 step plain] N %u:", wwhd_t60_steps);
    for (int i = 0; i < kWhatN; i++)
        k += snprintf(buf + k, sizeof buf - k, " %s=%u/%u %u/%u %u/%u %u/%u", names[i], g_n[i][1][0], g_n[i][0][0], g_n[i][1][1],
                      g_n[i][0][1], g_n[i][1][2], g_n[i][0][2], g_n[i][1][3], g_n[i][0][3]);
    LOG("%s", buf);
    memset(g_n, 0, sizeof g_n);
    g_t0 = t;
}
}  // namespace t60::audio

extern "C" __attribute__((visibility("default"))) int wwhd_t60_audio_player() { return t60::audio::player_runs() ? 1 : 0; }
extern "C" __attribute__((visibility("default"))) int wwhd_t60_audio_camtimer() { return t60::audio::camera_timer_counts() ? 1 : 0; }
extern "C" __attribute__((visibility("default"))) int wwhd_t60_audio_camgroup() { return t60::audio::camera_group_counts() ? 1 : 0; }
extern "C" __attribute__((visibility("default"))) int wwhd_t60_audio_cammap() { return t60::audio::camera_map_counts() ? 1 : 0; }
