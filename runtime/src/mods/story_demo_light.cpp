// SPDX-License-Identifier: MPL-2.0
// Mod demos: time and weather (story_demo_parts.h).
//
// Retail events change the time and weather with a LIGHT staff (dEvDtStaff_c light 0x025422AC): its
// CHANGE cut calls dKy_instant_timechg(Hour * 15) and, with Weather 0, dKy_instant_rainchg (rain 250,
// rainy colours at once). A demo's time/weather points are CHANGE cuts of a LIGHT staff in the demo's
// own event (demo_event_image.h); the port starts each cut at its demo frame by setting its start flag
// in the event manager's flags.
//
// What retail can't do from a staff is a rain level, no rain, or keeping it on the Great Sea, where the
// environment's execute (dKy_Execute 0x0255D128) rolls its dice weather every frame and would pull the
// rain back within seconds. While a demo holds weather, its execute keeps the demo's rain count and
// rainy/clear colours and leaves the dice out (the "sea" branch then runs as it does while the dice is
// paused: the colour blend start, exeKankyo, wind and sky brightness). Outside a holding demo the
// execute is the game's own.
//
// At the start the time (save +0x44, date +0x48) and the weather state are saved in the demo's guest
// record; at the end, skip or a state load they come back unless the demo keeps them.
#include "story_demo_parts.h"

#include "guest_addr.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace story_demo_parts::light {
namespace {
const uint32_t kPlay = GC(0x025200D4), kEnvLight = GC(0x02555D0C), kRainSet = GC(0x0257E7C0), kFlagSet = GC(0x02540848);
const uint32_t kKyExecute = GC(0x0255D128), kExeKankyo = GC(0x0255C85C), kWindSet = GC(0x0257D398), kAddCalc = GC(0x0200ECD4);
const uint32_t kTimeChange = GC(0x0256076C), kDayProc = GC(0x025542BC);  // dKy_instant_timechg, dKankyo_DayProc
const uint32_t kSave = GD(0x101F84DC);
const uint32_t kZero = GD(0x1004F0B0), kOne = GD(0x1004F0A4), kDarkSky = GD(0x1004F2BC), kFlash = GD(0x1004F214), kSkyStep = GD(0x1004F0BC), kSkyMin = GD(0x1004F178);
constexpr uint32_t kMarker = 0x4C475431;  // 'LGT1'
enum : uint32_t {
    Marker = 0x00, Cursor = 0x04, Hold = 0x08, Rain = 0x0C, Time = 0x10, Date = 0x14, RainA = 0x18, RainB = 0x1C,
    Colours = 0x20,   // env +0x108C..+0x108F (pattern prev/current and their gathers)
    Pattern = 0x24,   // env +0x1092 (weather colour pattern)
    Blend = 0x28, BlendGather = 0x2C, Thunder = 0x30, Sky = 0x34, Keep = 0x38,
    Dice = 0x3C,      // env +0xFC0, +0xFC4, +0x1068, +0x1093..+0x1095
};
Context g_ctx;
std::vector<uint8_t> g_save_copy;  // WWHD_DEMO_SAVE_DIFF: the save data at the demo's start (a test aid)
constexpr uint32_t kSaveDiffBytes = 0x2000;
void (*g_original)(Cpu*, uint32_t) = nullptr;
bool g_installed = false, g_weather = false, g_time = false;

float fl(uint32_t a) { return u32_as_f32(ld32(a)); }
void stf(uint32_t a, float v) { st32(a, f32_as_u32(v)); }
bool recorded(const Context& x) { return x.record && ld32(x.record + Marker) == kMarker; }
uint32_t env(Cpu* c, const Context& x) { return x.call(c, kEnvLight, {}); }
// a game call with float arguments (f1..)
uint32_t callf(Cpu* c, const Context& x, uint32_t fn, std::initializer_list<uint32_t> ints, std::initializer_list<float> floats) {
    const Cpu saved = *c;
    int i = 1;
    for (float f : floats) c->f[i++].ps0 = f;
    const uint32_t r = x.call(c, fn, ints);
    *c = saved;
    return r;
}
bool sea(Cpu* c, const Context& x) {
    const uint32_t play = x.call(c, kPlay, {});
    return play && ld8(play + 0x5134) == 's' && ld8(play + 0x5135) == 'e' && ld8(play + 0x5136) == 'a' && ld8(play + 0x5137) == 0;
}
// the demo's rain count and colour pattern (as the dice sets a pattern: the gather, then the blend starts)
void enforce(Cpu* c, const Context& x, uint32_t e) {
    const uint32_t rain = ld32(x.record + Rain);
    if (ld32(e + 0xA40) != rain || ld32(e + 0x106C) != rain) x.call(c, kRainSet, {rain});
    const uint8_t w = rain ? 1 : 0;
    if (ld8(e + 0x1092) != w) { st8(e + 0x1092, w); st8(e + 0x108F, w); }
}
// dKy_Execute with the sea's dice left out (its "sea" tail, exeKankyo, wind, sky brightness)
void execute_held(Cpu* c, const Context& x, uint32_t e) {
    if (ld8(e + 0x1097) == 0 && ld8(e + 0x1098) == 0 && ld8(e + 0x108F) != 0xFF && ld8(e + 0x108D) != ld8(e + 0x108F)) stf(e + 0xFCC, fl(kZero));
    x.call(c, kExeKankyo, {e});
    x.call(c, kWindSet, {});
    const uint32_t th = e + 0xAB0;
    float target = fl(kOne);
    if (ld8(e + 0x1092) != 0) {
        const uint8_t state = ld8(th + 1);
        const float k = fl(kDarkSky);
        target = k;
        if (state == 0 || state >= 10) stf(e + 0x103C, k * fl(th + 8) + k);
        else stf(e + 0x103C, fl(kFlash) * fl(th + 8) * fl(th + 0xC) + k);
    }
    const float step = fl(kSkyStep);
    callf(c, x, kAddCalc, {e + 0x103C}, {target, step, step, fl(kSkyMin)});
}
// dKy_instant_timechg(t): with t before the current time the game counts a new day and runs
// dKankyo_DayProc (letters delivered or stocked, daily counters, per-day event bits cleared). A mod
// demo's time change only sets the time: its date stays, and the time and date come back at its end.
void instant_timechg(Cpu* c) {
    if (!time_held()) { g_original(c, kTimeChange); return; }
    const uint32_t save = ld32(kSave);
    if (save) st32(save + 0x44, f32_as_u32(float(c->f[1].ps0)));
}
// The day change itself never runs while a mod demo holds time (whatever calls it)
void day_proc(Cpu* c) {
    if (!time_held()) { g_original(c, kDayProc); return; }
    LOG("[demo] light: the game's day change is not run during a mod demo");
}
void ky_execute(Cpu* c) {
    if (!holding()) { g_original(c, kKyExecute); return; }
    const uint32_t e = env(c, g_ctx);
    enforce(c, g_ctx, e);
    if (!sea(c, g_ctx)) { g_original(c, kKyExecute); return; }
    execute_held(c, g_ctx, e);
    c->r[3] = 1;
}
}  // namespace

void install(const Context& x, void (*original)(Cpu*, uint32_t), const std::function<std::string(uint32_t)>& replaced_by,
             const std::function<void(uint32_t, void (*)(Cpu*))>& replace, bool weather, bool time) {
    if (g_installed || !original) return;
    g_ctx = x; g_original = original;
    if (time) {
        for (uint32_t fn : {kTimeChange, kDayProc})
            if (!replaced_by(fn).empty()) { LOG("[demo] %s owns %08X; demo time changes are disabled", replaced_by(fn).c_str(), fn); g_installed = true; return; }
        replace(kTimeChange, instant_timechg);
        replace(kDayProc, day_proc);
        g_time = true;
    }
    if (weather) {
        if (!replaced_by(kKyExecute).empty()) LOG("[demo] %s owns %08X; demo weather is not held on the Great Sea", replaced_by(kKyExecute).c_str(), kKyExecute);
        else { replace(kKyExecute, ky_execute); g_weather = true; }
    }
    g_installed = true;
}
bool holding() { return g_weather && recorded(g_ctx) && ld32(g_ctx.record + Hold); }
bool time_held() { return g_time && recorded(g_ctx); }
bool time_ready() { return g_time; }

void begin(Cpu* c, const Context& x, const demo_scene::Demo& d) {
    std::memset(mem::ptr(x.record), 0, kRecordBytes);
    if (d.time.empty() && d.weather.empty()) return;
    const uint32_t e = env(c, x), save = ld32(kSave);
    if (!e || !save) return;
    st32(x.record + Time, ld32(save + 0x44));
    st32(x.record + Date, ld16(save + 0x48));
    st32(x.record + RainA, ld32(e + 0xA40));
    st32(x.record + RainB, ld32(e + 0x106C));
    st32(x.record + Colours, ld32(e + 0x108C));
    st32(x.record + Pattern, ld8(e + 0x1092));
    st32(x.record + Blend, ld32(e + 0xFC8));
    st32(x.record + BlendGather, ld32(e + 0xFCC));
    st32(x.record + Thunder, ld32(e + 0xAB4));
    st32(x.record + Sky, ld32(e + 0x103C));
    st32(x.record + Dice, ld32(e + 0xFC0)); st32(x.record + Dice + 4, ld32(e + 0xFC4)); st32(x.record + Dice + 8, ld32(e + 0x1068));
    st32(x.record + Dice + 12, uint32_t(ld8(e + 0x1093)) << 16 | uint32_t(ld8(e + 0x1094)) << 8 | ld8(e + 0x1095));
    st32(x.record + Keep, (d.keep_time ? 1u : 0u) | (d.keep_weather ? 2u : 0u));
    st32(x.record + Marker, kMarker);
    if (std::getenv("WWHD_DEMO_SAVE_DIFF")) g_save_copy.assign(mem::ptr(save), mem::ptr(save) + kSaveDiffBytes);
    LOG("[demo] light: time %.2f h, rain %u, pattern %u saved (%zu changes)", fl(save + 0x44) / 15.f, ld32(e + 0xA40), ld8(e + 0x1092), demo_scene::light_points(d).size());
}

void update(Cpu* c, const Context& x, const demo_scene::Demo& d, uint32_t frame, uint32_t manager) {
    if (!recorded(x) || !manager) return;
    const auto points = demo_scene::light_points(d);
    uint32_t cursor = ld32(x.record + Cursor);
    while (cursor < points.size() && points[cursor].frame <= frame) {
        const auto& p = points[cursor];
        x.call(c, kFlagSet, {manager + 0x3C, demo_scene::kLightStartFlag0 + cursor});
        if (p.rain) {
            st32(x.record + Rain, uint32_t(std::lround(*p.rain * 250.f)));
            st32(x.record + Hold, 1);
        }
        LOG("[demo] light change %u at frame %u:%s%s", cursor, frame, p.hour ? (" hour " + std::to_string(*p.hour)).c_str() : "",
            p.rain ? (" rain " + std::to_string(ld32(x.record + Rain))).c_str() : "");
        st32(x.record + Cursor, ++cursor);
    }
}

void release(Cpu* c, const Context& x) {
    if (!recorded(x)) return;
    const uint32_t keep = ld32(x.record + Keep), e = env(c, x), save = ld32(kSave);
    if (e && save) {
        if (!(keep & 1)) {
            st32(save + 0x44, ld32(x.record + Time));
            st16(save + 0x48, uint16_t(ld32(x.record + Date)));
            st32(e + 0x1020, ld32(x.record + Time));
            st16(e + 0x108A, uint16_t(ld32(x.record + Date)));
        }
        if (!(keep & 2)) {
            st32(e + 0xA40, ld32(x.record + RainA));
            st32(e + 0x106C, ld32(x.record + RainB));
            st32(e + 0x108C, ld32(x.record + Colours));
            st8(e + 0x1092, uint8_t(ld32(x.record + Pattern)));
            st32(e + 0xFC8, ld32(x.record + Blend));
            st32(e + 0xFCC, ld32(x.record + BlendGather));
            st32(e + 0xAB4, ld32(x.record + Thunder));
            st32(e + 0x103C, ld32(x.record + Sky));
            st32(e + 0xFC0, ld32(x.record + Dice)); st32(e + 0xFC4, ld32(x.record + Dice + 4)); st32(e + 0x1068, ld32(x.record + Dice + 8));
            const uint32_t dice = ld32(x.record + Dice + 12);
            st8(e + 0x1093, uint8_t(dice >> 16)); st8(e + 0x1094, uint8_t(dice >> 8)); st8(e + 0x1095, uint8_t(dice));
        }
        if (g_save_copy.size() == kSaveDiffBytes) {
            std::string diff;
            unsigned count = 0;
            for (uint32_t k = 0; k < kSaveDiffBytes; ++k)
                if (ld8(save + k) != g_save_copy[k] && count++ < 32) { char t[32]; std::snprintf(t, sizeof t, " +%X:%02X>%02X", k, g_save_copy[k], ld8(save + k)); diff += t; }
            // the mailbox (letter states are event registers) and the event bits/registers: +0x644..+0x744;
            // the temp event bits: +0x1178..+0x1278 (dKankyo_DayProc's ev_l / tmp_l); the save data ends below 0x1300
            auto changed = [&](uint32_t from, uint32_t to) { unsigned n = 0; for (uint32_t k = from; k < to; ++k) n += ld8(save + k) != g_save_copy[k]; return n; };
            LOG("[demo] light: after the demo: events/mailbox +644..+744: %u bytes differ, temp bits +1178..+1278: %u, save +0..+1300: %u, all 0..0x%X: %u%s",
                changed(0x644, 0x744), changed(0x1178, 0x1278), changed(0, 0x1300), kSaveDiffBytes, count, diff.c_str());
            g_save_copy.clear();
        }
        LOG("[demo] light: %s%s", keep & 1 ? "time kept" : "time restored", keep & 2 ? ", weather kept" : ", weather restored");
    }
    std::memset(mem::ptr(x.record), 0, kRecordBytes);
}
}  // namespace story_demo_parts::light
