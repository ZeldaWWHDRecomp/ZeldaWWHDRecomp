#include "mods/mods.h"
// padscore: Wii Remote / Pro Controller (WPAD, KPAD). Only a Pro Controller on channel 0 exists,
// and only while the keyboard/host controllers are set to act as one (Input menu). Struct layouts
// and constants follow Cemu's padscore.
#include "../crashrec.h"
#include "../runtime.h"
#include "../input.h"
#include "../controller_reapply.h"
#include "../mods/story_notebook.h"
#include "../mods/story_bag.h"
#include "../mods/game_settings_page.h"
#include "../mods/story_scenes.h"
#include "../motion/motion.h"
#include "../rumble.h"

namespace interp { bool repeat_input(); uint64_t logic_steps(); }

namespace {
constexpr int32_t kWpadErrNone = 0, kWpadErrNoController = -1;
constexpr int32_t kKpadErrNone = 0, kKpadErrNoController = -2;
constexpr uint8_t kDevURCC = 31, kDevNone = 253, kFormatURCC = 22;

// While the game is switched from the Pro Controller to the GamePad (after a save-state load or a change
// of the host's choice), its Pro Controller stays connected, as when a player changes the controller in
// the game's Options (a missing controller would stop the game at its reconnect prompt before the
// switch completes). It is idle unless the game still reads it (controller_reapply::pro_controller_live).
bool connected(uint32_t chan) { return chan == 0 && (input::pro_controller() || controller_reapply::holds_pro_controller()); }

// VPAD button bits (input::PadState) -> Pro Controller (URCC) bits
uint32_t pro_buttons(uint32_t v) {
    static const uint32_t map[][2] = {
        {input::kUp, 0x1},     {input::kLeft, 0x2},    {input::kZR, 0x4},     {input::kX, 0x8},
        {input::kA, 0x10},     {input::kY, 0x20},      {input::kB, 0x40},     {input::kZL, 0x80},
        {input::kR, 0x200},    {input::kPlus, 0x400},  {input::kHome, 0x800}, {input::kMinus, 0x1000},
        {input::kL, 0x2000},   {input::kDown, 0x4000}, {input::kRight, 0x8000},
        {input::kStickR, 0x10000}, {input::kStickL, 0x20000},
    };
    uint32_t out = 0;
    for (auto& m : map)
        if (v & m[0]) out |= m[1];
    return out;
}
}  // namespace

HLE(padscore, KPADInitEx) {}
HLE(padscore, KPADGetMplsWorkSize) { ret(c, 0x5FE0); }
HLE(padscore, KPADSetMplsWorkarea) {}
HLE(padscore, WPADEnableURCC) {}
HLE(padscore, WPADEnableWiiRemote) {}
HLE(padscore, WPADDisconnect) {}
// The Pro Controller motor: cmd 0 stops it, 1 runs it until the game stops it. The Pro
// Controller is only connected while the keyboard/controllers act as one (see connected).
HLE(padscore, WPADControlMotor) {
    uint32_t chan = arg(c, 0), cmd = arg(c, 1);
    if (connected(chan) && input::pro_controller()) {
        TRACE("[pad] WPADControlMotor(%u, %u) -> %s", chan, cmd, cmd ? "rumble" : "stop");
        rumble::pro_motor(chan, cmd != 0);
    }
    ret(c, (uint32_t)kWpadErrNone);
}
HLE(padscore, WPADGetBatteryLevel) { ret(c, 4); }  // full
HLE(padscore, WPADCanSendStreamData) { ret(c, 0); }
HLE(padscore, WPADSendStreamData) { ret(c, (uint32_t)kWpadErrNoController); }
HLE(padscore, WPADControlSpeaker) { ret(c, (uint32_t)kWpadErrNoController); }

HLE(padscore, WPADProbe) {
    // (chan, uint32* type) -> WPAD error
    uint32_t chan = arg(c, 0), type = arg(c, 1);
    bool on = connected(chan);
    TRACE("[pad] WPADProbe(%u) -> %s", chan, on ? "pro" : "none");
    if (type) st32(type, on ? kDevURCC : kDevNone);
    ret(c, (uint32_t)(on ? kWpadErrNone : kWpadErrNoController));
}

HLE(padscore, KPADReadEx) {
    // (chan, KPADStatus* buf, count, int32* err) -> samples written
    uint32_t chan = arg(c, 0), st = arg(c, 1), count = arg(c, 2), err = arg(c, 3);
    TRACE("[pad] KPADReadEx(%u, %08X, %u) connected=%d", chan, st, count, connected(chan));
    if (!connected(chan) || !st || (int32_t)count <= 0) {
        if (err) st32(err, (uint32_t)(connected(chan) ? kKpadErrNone : kKpadErrNoController));
        ret(c, 0);
        return;
    }
    static uint32_t last = 0;
    static input::PadState last_p;
    const bool repeat = interp::repeat_input();
    input::PadState p = repeat ? last_p : crashrec::read(1);  // see interp.cpp; crash recovery records/replays it
    if (!repeat) mods::pad_play_rules(p);  // L and d-pad down by whether Link plays (input_map::play_buttons)
    const uint32_t live_virt = p.virt;  // as VPADRead: the virtual inputs as read now
    const bool live = !repeat;
    if (input::pro_controller() && chan == 0) mods::item_ring_pad(p, repeat);  // X / Y / R: tap, or hold for the item ring
    if (input::pro_controller() && chan == 0) mods::context_touch_pad(p, repeat);  // touch play: items by tap, put-away, ZL latch
    if (input::pro_controller() && chan == 0) mods::touch_talk_pad(p, repeat);  // talks, answers, shops by touch; hold to go on
    if (input::pro_controller() && chan == 0) mods::guest_pad_filter(c, p, repeat);  // code mods: their touches and input filters
    last_p = p;
    // held connected for a controller switch: idle, unless the game still reads it (pause menu)
    if (!input::pro_controller() && !controller_reapply::pro_controller_live()) p = input::PadState{};
    story_notebook::mute(p);  // the notebook's page has the controller (VPADRead drives it)
    story_scenes::mute(p);
    story_bag::mute(p);       // so do the Delivery Bag's extra pages
    game_settings_page::mute(p);  // and the settings page
    // the host's extras follow the host's choice: while a held Pro Controller is live, VPADRead has them
    input::PadState idle{};
    input::PadState& extras = input::pro_controller() ? p : idle;
    mods::moon_jump_input(extras);
    mods::move_speed_input(extras.buttons);
    if (live && input::pro_controller()) mods::jump_input(live_virt);
    mods::fast_forward_input(extras.buttons);
    if (!repeat) motion::right_stick(extras.rx, extras.ry);  // the Pro Controller's stick decides gyro use too (motion.h)
    uint32_t hold = pro_buttons(p.buttons);
    memset(mem::ptr(st), 0, 0xF0);
    st8(st + 0x5C, kDevURCC);     // devType
    st8(st + 0x5D, 0);            // wpadErr
    st8(st + 0x5F, kFormatURCC);  // data_format
    st32(st + 0x60, hold);                // ex_status.uc.hold
    st32(st + 0x64, hold & ~last);        // trig
    st32(st + 0x68, last & ~hold);        // release
    last = hold;
    stf32(st + 0x6C, p.lx); stf32(st + 0x70, p.ly);  // lstick
    stf32(st + 0x74, p.rx); stf32(st + 0x78, p.ry);  // rstick
    st32(st + 0x7C, 1);                   // charge
    st32(st + 0x80, 1);                   // cable
    // Optional read-by-read diagnostics, including the data actually written to guest memory.
    static FILE* trace = getenv("WWHD_KPAD_TRACE") ? fopen(getenv("WWHD_KPAD_TRACE"), "w") : nullptr;
    if (trace) {
        fprintf(trace, "%llu %d %08X %08X %08X %.6f %.6f %.6f %.6f\n",
                (unsigned long long)interp::logic_steps(), (int)repeat, hold,
                ld32(st + 0x64), ld32(st + 0x68), ldf32(st + 0x6C), ldf32(st + 0x70),
                ldf32(st + 0x74), ldf32(st + 0x78));
        fflush(trace);
    }
    if (err) st32(err, kKpadErrNone);
    ret(c, 1);
}
