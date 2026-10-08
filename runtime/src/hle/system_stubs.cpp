#include "mods/mods.h"
// Libraries the game uses for system integration and online features.
// Online services (Miiverse, SpotPass, accounts) report "unavailable".
#include "../crashrec.h"
#include "../runtime.h"
#include "../input.h"
#include "../rumble.h"
#include "../motion/motion.h"

namespace interp { bool repeat_input(); bool fresh_sticks(); void trace_read(const char*); uint64_t logic_steps(); }

// nn::Result: bit 31 set = failure
static constexpr uint32_t kResultOk = 0;
static constexpr uint32_t kResultFail = 0xA0000000;

// ---- nn_act (accounts)
HLE(nn_act, Initialize__Q2_2nn3actFv) { ret(c, kResultOk); }
HLE(nn_act, Finalize__Q2_2nn3actFv) { ret(c, kResultOk); }
HLE(nn_act, GetSlotNo__Q2_2nn3actFv) { ret(c, 1); }
HLE(nn_act, GetPrincipalId__Q2_2nn3actFv) { ret(c, 0); }
HLE(nn_act, GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc) { st8(arg(c, 0), 1); ret(c, kResultOk); }

// ---- nn_ac (network connection)
HLE(nn_ac, Initialize__Q2_2nn2acFv) { ret(c, kResultOk); }
HLE(nn_ac, Finalize__Q2_2nn2acFv) { ret(c, kResultOk); }
HLE(nn_ac, Connect__Q2_2nn2acFv) { ret(c, kResultFail); }
HLE(nn_ac, Close__Q2_2nn2acFv) { ret(c, kResultOk); }
HLE(nn_ac, GetLastErrorCode__Q2_2nn2acFPUi) { st32(arg(c, 0), 1100); ret(c, kResultOk); }

// ---- nn_boss (SpotPass)
HLE(nn_boss, Initialize__Q2_2nn4bossFv) { ret(c, kResultFail); }
HLE(nn_boss, IsInitialized__Q2_2nn4bossFv) { ret(c, 0); }
HLE(nn_boss, Finalize__Q2_2nn4bossFv) {}
// play report (usage statistics upload): accepted and dropped
HLE(nn_boss, __ct__Q3_2nn4boss17PlayReportSettingFv) { ret(c, arg(c, 0)); }
HLE(nn_boss, __dt__Q3_2nn4boss17PlayReportSettingFv) {}
HLE(nn_boss, Initialize__Q3_2nn4boss17PlayReportSettingFPvUi) {}
HLE(nn_boss, Set__Q3_2nn4boss17PlayReportSettingFUiT1) { ret(c, 1); }
HLE(nn_boss, __ct__Q3_2nn4boss4TaskFv) { ret(c, arg(c, 0)); }
HLE(nn_boss, __dt__Q3_2nn4boss4TaskFv) {}
HLE(nn_boss, Initialize__Q3_2nn4boss4TaskFPCcUi) { ret(c, kResultFail); }
HLE(nn_boss, IsRegistered__Q3_2nn4boss4TaskCFv) { ret(c, 0); }
HLE(nn_boss, Register__Q3_2nn4boss4TaskFRQ3_2nn4boss11TaskSetting) { ret(c, kResultFail); }
HLE(nn_boss, StartScheduling__Q3_2nn4boss4TaskFb) { ret(c, kResultFail); }

// ---- nn_olv (Miiverse)
HLE(nn_olv, Initialize__Q2_2nn3olvFPCQ3_2nn3olv15InitializeParam) { ret(c, kResultFail); }
HLE(nn_olv, IsInitialized__Q2_2nn3olvFv) { ret(c, 0); }
HLE(nn_olv, Finalize__Q2_2nn3olvFv) { ret(c, kResultOk); }

// OliveOperationMgrThread (Miiverse requests: Tingle Bottles, posts, Yeahs) runs a state machine that
// re-posts itself to its own message queue until Miiverse answers, which it never does here: about
// 100k steps a second, a whole host core (on the console it only soaks up idle time on its core).
// A 1 ms nap per step (releasing the guest core) keeps it to ~1000 steps a second.
extern "C" void f_0203DEEC_orig(Cpu* c);
extern "C" void hook_0203DEEC(Cpu* c) {
    threads::park_sleep_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
    f_0203DEEC_orig(c);
}

// ---- sockets / curl
HLE(nsysnet, socket_lib_init) { ret(c, 0); }
HLE(nsysnet, socket_lib_finish) { ret(c, 0); }
HLE(nsysnet, NSSLInit) { ret(c, 0); }
HLE(nsysnet, NSSLFinish) { ret(c, 0); }
HLE(nlibcurl, curl_global_init_mem) { ret(c, 0); }
HLE(nlibcurl, curl_global_cleanup) {}

// ---- proc_ui (foreground/background lifecycle)
HLE(proc_ui, ProcUIInit) {}
HLE(proc_ui, ProcUIShutdown) {}
HLE(proc_ui, ProcUIRegisterCallback) {}
HLE(proc_ui, ProcUIDrawDoneRelease) {}
HLE(proc_ui, ProcUIProcessMessages) { ret(c, 0); }  // PROCUI_STATUS_IN_FOREGROUND

// ---- vpad (GamePad): keyboard / host controllers
HLE(vpad, VPADRead) {
    // (chan, VPADStatus* buf, count, int32* error) -> samples written
    uint32_t chan = arg(c, 0), st = arg(c, 1), count = arg(c, 2), err = arg(c, 3);
    // debug: WWHD_TRACE_VPAD=n logs the guest call chain of the first n reads
    static int trace = getenv("WWHD_TRACE_VPAD") ? atoi(getenv("WWHD_TRACE_VPAD")) : 0;
    if (trace > 0) {
        trace--;
        char buf[256];
        int n = snprintf(buf, sizeof buf, "[vpad] read from lr=%08X", c->lr);
        for (uint32_t sp = c->r[1], i = 0; i < 8 && sp; i++) {
            uint32_t prev = ld32(sp);
            if (!prev || prev <= sp) break;
            n += snprintf(buf + n, sizeof buf - n, " <- %08X", ld32(prev + 4));
            sp = prev;
        }
        LOG("%s", buf);
    }
    if (chan != 0 || !st || (int32_t)count <= 0) {
        if (err) st32(err, (uint32_t)-2);  // VPAD_READ_INVALID_CONTROLLER
        ret(c, 0);
        return;
    }
    static uint32_t last_hold = 0;
    interp::trace_read("VPAD");
    // frame interpolation: the read after a logic pass repeats the last sample (interp.cpp)
    static input::PadState last_p;
    const bool repeat = interp::repeat_input();
    input::PadState p = repeat ? last_p : crashrec::read(0);  // live input, recorded/replayed by crash recovery
    if (repeat && interp::fresh_sticks()) {  // true 60: sticks every pass, buttons on full passes
        input::PadState f = input::read();
        p.lx = f.lx; p.ly = f.ly; p.rx = f.rx; p.ry = f.ry;
    }
    last_p = p;
    if (!input::pro_controller()) mods::move_speed_input(p.buttons);
    if (input::pro_controller()) {  // GamePad on the table: screen and touch only
        p.buttons = 0;
        p.lx = p.ly = p.rx = p.ry = 0;
    } else if (!repeat) {
        motion::right_stick(p.rx, p.ry);  // the game ignores the gyro while it is pushed (gyro diagnostics)
    }
    uint32_t hold = p.buttons;
    auto stick_dirs = [&](float x, float y, uint32_t up, uint32_t down, uint32_t left, uint32_t right) {
        // stick-as-button bits with hysteresis, like the real VPAD library
        auto dir = [&](float v, bool held, uint32_t bit) { if (v >= 0.5f || (held && v >= 0.1f)) hold |= bit; };
        dir(-x, last_hold & left, left); if (!(hold & left)) dir(x, last_hold & right, right);
        dir(-y, last_hold & down, down); if (!(hold & down)) dir(y, last_hold & up, up);
    };
    stick_dirs(p.lx, p.ly, 0x10000000, 0x08000000, 0x40000000, 0x20000000);
    stick_dirs(p.rx, p.ry, 0x01000000, 0x00800000, 0x04000000, 0x02000000);
    if (repeat) hold = last_hold;  // buttons (and the stick-as-button bits) change on full passes only

    memset(mem::ptr(st), 0, 0xAC);
    st32(st + 0x00, hold);
    st32(st + 0x04, hold & ~last_hold);   // trig
    st32(st + 0x08, last_hold & ~hold);   // release
    last_hold = hold;
    {  // debug: WWHD_PAD_TRACE=path logs each read: logic step, repeated, buttons, trigger
        static FILE* pt = getenv("WWHD_PAD_TRACE") ? fopen(getenv("WWHD_PAD_TRACE"), "w") : nullptr;
        if (pt) { fprintf(pt, "%llu %d %08X %08X\n", (unsigned long long)interp::logic_steps(), (int)repeat, hold, ld32(st + 4)); fflush(pt); }
    }
    stf32(st + 0x0C, p.lx); stf32(st + 0x10, p.ly);
    stf32(st + 0x14, p.rx); stf32(st + 0x18, p.ry);
    {  // motion sensors (motion/motion.h). WWHD reads only the direction matrix (0x6C..0x8F): its
       // first-person camera turns by the change from one frame to the next (dCamera_c::CalcSubjectAngle)
        const motion::VpadMotion m = motion::vpad(repeat);
        auto vec = [&](uint32_t at, const motion::Vec3& v) { stf32(at, v.x); stf32(at + 4, v.y); stf32(at + 8, v.z); };
        vec(st + 0x1C, m.acc);
        stf32(st + 0x28, m.acc_magnitude);
        stf32(st + 0x2C, m.acc_variation);
        stf32(st + 0x30, m.acc_xy[0]); stf32(st + 0x34, m.acc_xy[1]);
        vec(st + 0x38, m.gyro);
        vec(st + 0x44, m.angle);
        for (int i = 0; i < 3; i++) vec(st + 0x6C + i * 0xC, m.dir[i]);
    }
    // touch panel, raw coordinates as the hardware reports them (mapping from Cemu)
    static uint16_t last_tx = 0, last_ty = 0;
    if (p.touch) {
        last_tx = (uint16_t)(p.tx * 3883.0f + 92.0f);
        last_ty = (uint16_t)(4095.0f - p.ty * 3694.0f - 254.0f);
    }
    for (uint32_t tp = 0x52; tp <= 0x62; tp += 8) {
        st16(st + tp + 0, last_tx);
        st16(st + tp + 2, last_ty);
        st16(st + tp + 4, p.touch ? 1 : 0);
        st16(st + tp + 6, p.touch ? 0 : 3);  // validity: 0 = valid, 3 = invalid XY
    }
    st8(st + 0xA0, 0xFF); st8(st + 0xA3, 0xFF);                // slide volume
    st8(st + 0xA1, 0xC0);                                      // battery full
    if (err) st32(err, 0);
    ret(c, 1);
}
// raw touch coordinates -> 1280x720 screen space
static void tp_to_screen(uint32_t out, uint32_t raw, int w, int h) {
    int x = std::max<int>(ld16(raw) - 92, 0), y = std::max<int>(4095 - (int)ld16(raw + 2) - 254, 0);
    st16(out, (uint16_t)(x / 3883.0 * w));
    st16(out + 2, (uint16_t)(y / 3694.0 * h));
    st16(out + 4, ld16(raw + 4));
    st16(out + 6, ld16(raw + 6));
}
HLE(vpad, VPADGetTPCalibratedPoint) { tp_to_screen(arg(c, 1), arg(c, 2), 1280, 720); }
HLE(vpad, VPADGetTPCalibratedPointEx) {
    int res = (int)arg(c, 1);  // 0 = 1920x1080, 1 = 1280x720, 2 = 854x480
    tp_to_screen(arg(c, 2), arg(c, 3), res == 0 ? 1920 : res == 2 ? 854 : 1280, res == 0 ? 1080 : res == 2 ? 480 : 720);
}
// The GamePad motor: the game sends a pattern of up to 120 bits (not bytes), played at 120 bits a
// second; it stops by itself at the end, and an empty pattern or VPADStopMotor stops it at once.
// rumble.h turns that into what the host controllers' motors do.
HLE(vpad, VPADControlMotor) {
    // (chan, uint8* pattern, uint8 length in bits) -> int32 error
    uint32_t chan = arg(c, 0), pattern = arg(c, 1), nbits = std::min<uint32_t>(arg(c, 2) & 0xFF, rumble::Motor::kMaxBits);
    uint8_t bits[rumble::Motor::kMaxBits / 8] = {};
    for (uint32_t i = 0; pattern && i < (nbits + 7) / 8; i++) bits[i] = ld8(pattern + i);
    TRACE("[pad] VPADControlMotor(%u, %u bits)", chan, nbits);
    rumble::gamepad_pattern(chan, pattern ? bits : nullptr, nbits);
    ret(c, 0);
}
HLE(vpad, VPADStopMotor) { rumble::gamepad_stop(arg(c, 0)); }
HLE(vpadbase, VPADBASEGetHeadphoneStatus) { ret(c, 0); }

// ---- padscore (Wii Remote / Pro Controller): see padscore.cpp

// ---- sysapp
HLE(sysapp, SYSLaunchSettings) { ret(c, 0); }
HLE(sysapp, SYSLaunchAccount) { ret(c, 0); }
