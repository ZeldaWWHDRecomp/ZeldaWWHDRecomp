// Exercise the real runtime hook sites with a synthetic guest and native predicate contracts.
// No game assets, generated game bodies or proprietary code are needed by this fixture.
#include "mods/mods.h"
#include "runtime.h"
#include "guest_addr.h"
#include "input.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#ifdef QUICK_USE_SHIFTED_MAP
extern "C" {
const GuestStep g_guest_code_steps[] = {{0x02000000, 4}};
const unsigned g_guest_code_step_count = 1;
const GuestStep g_guest_data_steps[] = {{0x10000000, 0x100}};
const unsigned g_guest_data_step_count = 1;
const uint32_t g_guest_code_lo = 0, g_guest_code_hi = 0, g_guest_data_lo = 0, g_guest_data_hi = 0;
const GuestChanged g_guest_changed_code[] = {{0, 0}};
const unsigned g_guest_changed_code_count = 0;
const char g_guest_build_name[] = "shifted-fixture", g_guest_build_title_id[] = "test";
}
#endif
extern "C" {
void hook_023F32E4(Cpu*); void hook_023DE7E8(Cpu*); void site_023EFB44(Cpu*);
void site_023F0624(Cpu*); void site_023F0670(Cpu*); void site_023F06AC(Cpu*);
void site_023F1764(Cpu*); void site_023F1784(Cpu*); void site_023F1728(Cpu*);
void site_023EAE30(Cpu*); void site_023EAE78(Cpu*); void site_023EC12C(Cpu*); void site_023EC178(Cpu*);
void site_023EAE84(Cpu*); void site_023EC184(Cpu*);
void site_023EB548(Cpu*); void site_023EB614(Cpu*);
void site_023E88B0(Cpu*); void site_023EB2B0(Cpu*); void site_023EB434(Cpu*);
void site_023DF7C0(Cpu*); void site_023EA474(Cpu*);
void site_023F05EC(Cpu*);
}
namespace {
constexpr uint32_t player_addr = 0x11000000, attention = 0x11010000, enemy = 0x11020000, actor = 0x11030000;
constexpr uint32_t buttons[] = {input::kX, input::kY, input::kR};
constexpr uint32_t release_bits[] = {8, 16, 0x4000};
uint64_t now = 0;
bool blocked = false, live = true, preview = false;
unsigned resolves = 0, native_inputs = 0, native_setups = 0, automatic_releases = 0;
bool reset_during_setup = false;
uint16_t item;
unsigned slot;
bool bow(uint16_t id) { return id == 0x27 || id == 0x35 || id == 0x36; }
uint32_t proc(uint16_t id) { return bow(id) ? 0x95 : id == 0x2F ? 0x84 : id == 0x25 ? 0x7D : 0x81; }
void map(uint32_t address) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
Cpu cpu() {
    Cpu c{};
    c.r[0] = 0xABCDEF; c.r[1] = player_addr + 0xF000; c.r[3] = player_addr; c.r[31] = player_addr;
    c.r[8] = 0xFFFF; c.r[11] = 0xCAFE; c.f[1].ps0 = 42; c.lr = 0x1234;
    return c;
}
void pad(uint32_t b) {
    input::PadState s{}; s.buttons = b; s.rx = .5f; s.touch = true; s.tx = .7f;
    const auto original = s;
    mods::quick_use_filter_pad(s);
    assert(!memcmp(&s, &original, sizeof s)); // observation, never global input synthesis
}
void input_tick() {
    Cpu c = cpu();
    hook_023F32E4(&c);
    assert(c.r[3] == 123 && c.r[4] == 456 && c.f[1].ps0 == 789);
}
void setup(uint16_t id, unsigned which = 0) {
    mods::quick_use_reset();
    now += 100;
    memset(mem::ptr(player_addr), 0, 65536);
    memset(mem::ptr(attention), 0, 65536);
    memset(mem::ptr(enemy), 0, 65536);
    memset(mem::ptr(actor), 0, 65536);
    item = id; slot = which; blocked = false; live = true; automatic_releases = 0;
    st8(GD(0x1046F0B0) + 0x5292, 0); st8(GD(0x1046F0B0) + 0x514C, 0);
    st32(GD(0x101F36CC), 0); st8(GD(0x101EA069), 0);
    st32(GD(0x1046F0B0) + 0x5B2C, player_addr);
    st8(GD(0x1046F0B0) + 0x5BBB + slot, id);
    st32(player_addr + 0x6894, attention); st32(player_addr + 0x65B0, enemy); st32(player_addr + 0x6594, actor);
    st8(attention + 0x18, 1); st32(attention + 0xD4, 1); st32(attention + 0xD8, 0);
    st32(attention + 0x54 + 8, 2); st32(attention + 0x54 + 12, 50);
    st32(enemy + 0x39C, 4);
    st16(player_addr + 0x69B0, id); st8(player_addr + 0x68D9, slot);
    st32(player_addr + 0x65F0, proc(id));
    st16(player_addr + 0x69AC, id == 0x2D ? 0 : 10);
    mods::set_quick_use(true);
    pad(0);
}
void press(bool alternate = false) {
    pad(buttons[slot]);
    st8(player_addr + 0x690D, 4u << slot);
    Cpu c = cpu(), before = c;
    auto site = bow(item) ? site_023E88B0 : item == 0x2F ? (alternate ? site_023F1764 : site_023F0624) :
                item == 0x25 ? (alternate ? site_023F1784 : site_023F0670) :
                               (alternate ? site_023F1728 : site_023F06AC);
    site(&c);
    assert(!memcmp(&c, &before, sizeof c));
}
uint32_t held_query(uint32_t native, bool retry = false) {
    Cpu c = cpu(); c.r[3] = native;
    const Cpu before = c;
    if (item == 0x2F) (retry ? site_023EAE78 : site_023EAE30)(&c);
    else (retry ? site_023EC178 : site_023EC12C)(&c);
    Cpu expected = before; expected.r[3] = c.r[3];
    assert(!memcmp(&c, &expected, sizeof c));
    return c.r[3];
}
uint32_t release_predicate() {
    Cpu c = cpu();
    const unsigned reg = item == 0x2F ? 11 : 0;
    c.r[reg] = ld16(player_addr + 0x69AE);
    const Cpu before = c;
    (item == 0x2F ? site_023EAE84 : site_023EC184)(&c);
    Cpu expected = before; expected.r[reg] = c.r[reg];
    assert(!memcmp(&c, &expected, sizeof c));
    return c.r[reg];
}
// Only a contract model of the idle ready check: latch/actor/animation transitions belong to
// native code. The actual mod must never write these fields, including during its release tick.
bool grappling_tick(bool physical_hold) {
    const auto count = ld16(player_addr + 0x69AC), latch = ld16(player_addr + 0x69AE);
    const auto state = ld32(actor + 0xB0);
    const uint32_t held = held_query(physical_hold ? 4u << slot : 0);
    assert(ld16(player_addr + 0x69AC) == count && ld16(player_addr + 0x69AE) == latch && ld32(actor + 0xB0) == state);
    bool fired = false;
    if (!held) {
        fired = release_predicate() != 0;
        assert(ld16(player_addr + 0x69AE) == latch && ld32(actor + 0xB0) == state);
        if (fired) { st16(player_addr + 0x69AE, 0); st32(actor + 0xB0, 1); }
    } else if (count == 0) st16(player_addr + 0x69AE, 1);
    if (!fired && count) st16(player_addr + 0x69AC, count - 1);
    st8(player_addr + 0x690D, 0);
    return fired;
}
uint32_t boomerang_release(uint32_t native) {
    Cpu c = cpu(); c.r[8] = native;
    const Cpu before = c;
    const auto counter = ld16(player_addr + 0x69AC), latch = ld16(player_addr + 0x69AE);
    const auto deferred = ld8(player_addr + 0x827C);
    site_023EB548(&c);
    Cpu expected = before; expected.r[8] = c.r[8];
    assert(!memcmp(&c, &expected, sizeof c));
    assert(ld16(player_addr + 0x69AC) == counter && ld16(player_addr + 0x69AE) == latch && ld8(player_addr + 0x827C) == deferred);
    return c.r[8];
}
uint32_t boomerang_hold(uint32_t native) {
    Cpu c = cpu(); c.r[3] = native;
    const Cpu before = c;
    site_023EB614(&c);
    Cpu expected = before; expected.r[3] = c.r[3];
    assert(!memcmp(&c, &expected, sizeof c));
    return c.r[3];
}
}
namespace overlay { bool blocks_input() { return blocked; } }
namespace true60 { bool preview() { return ::preview; } }
namespace mods {
uint64_t step() { return now; }
void trace(const char* format, ...) {
    char message[256];
    va_list args; va_start(args, format);
    std::vsnprintf(message, sizeof message, format, args); va_end(args);
    if (!std::strncmp(message, "quick-use: release item ", 24)) ++automatic_releases;
}
}
uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    assert(fn == GC(0x024EBA14)); assert(args.size() == 1 && *args.begin() == attention + 0x54);
    ++resolves;
    c->r[3] = 0xDEADBEEF; c->r[4] = 0xABCDEF; c->r[31] = 0; c->f[1].ps0 = -1; c->cr[2] = 1;
    return live ? enemy : 0;
}
extern "C" void f_023F32E4_orig(Cpu* c) {
    ++native_inputs;
    c->r[3] = 123; c->r[4] = 456; c->f[1].ps0 = 789;
}
// Native animation setup/update contract, not generated game code.
extern "C" void f_023DE7E8_orig(Cpu* c) {
    ++native_setups;
    if (reset_during_setup) mods::quick_use_reset();
    const uint32_t heap = c->r[3] + 0x5868 + c->r[5] * 16;
    const uint32_t fc = c->r[3] + 0x58B8 + c->r[5] * 16;
    st16(heap, c->r[4]); st32(c->r[3] + 0x581C + c->r[5] * 16, actor + 0x800 + c->r[4] * 4);
    st32(fc, f32_as_u32(float(c->f[1].ps0))); st32(fc + 4, f32_as_u32(0.f));
    st16(fc + 8, 0); st16(fc + 10, 30); st16(fc + 12, 0); st8(fc + 14, 0); st8(fc + 15, 0);
    c->r[3] = 0x12345678; c->r[4] = 0xABCD; c->f[1].ps0 = 321;
}
namespace {
void animation(uint16_t clip, unsigned channel = 2, float rate = 1.f) {
    Cpu c = cpu(); c.r[4] = clip; c.r[5] = channel; c.f[1].ps0 = rate;
    Cpu expected = c; expected.r[3] = 0x12345678; expected.r[4] = 0xABCD; expected.f[1].ps0 = 321;
    hook_023DE7E8(&c);
    assert(!memcmp(&c, &expected, sizeof c));
}
float seen_rate;
void check_key(Cpu* c) {
    const auto fc = c->r[3];
    const float frame = u32_as_f32(ld32(fc + 4)), rate = u32_as_f32(ld32(fc));
    c->r[3] = frame <= c->f[1].ps0 && c->f[1].ps0 < frame + rate;
}
bool key(float target) {
    Cpu c = cpu(); c.r[3] = player_addr + 0x58D8; c.f[1].ps0 = target;
    Cpu expected = c;
    mods::quick_use_animation_call(&c, check_key, false);
    expected.r[3] = c.r[3];
    assert(!memcmp(&c, &expected, sizeof c));
    return c.r[3] != 0;
}
void advance_frame(Cpu* c) {
    const auto fc = c->r[3];
    seen_rate = u32_as_f32(ld32(fc));
    st32(fc + 4, f32_as_u32(u32_as_f32(ld32(fc + 4)) + seen_rate));
    c->r[3] = 0xFEDCBA; c->f[1].ps0 = 777; // callback output must survive
}
void stop_frame(Cpu* c) { st32(c->r[3], 0); }
void replace_rate(Cpu* c) { st32(c->r[3], f32_as_u32(1.75f)); }
void reset_in_frame(Cpu* c) { mods::quick_use_reset(); advance_frame(c); }
void advance_animation(unsigned channel = 2) {
    Cpu c = cpu(); c.r[3] = player_addr + 0x58B8 + channel * 16;
    Cpu expected = c; expected.r[3] = 0xFEDCBA; expected.f[1].ps0 = 777;
    mods::quick_use_animation_call(&c, advance_frame);
    assert(!memcmp(&c, &expected, sizeof c));
}
void accepted_equip() {
    st16(player_addr + 0x69B0, 0xFF); st16(player_addr + 0x69B2, item); st32(player_addr + 0x65F0, 4);
    pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
    Cpu c = cpu(); site_023EFB44(&c); pad(0); st8(player_addr + 0x690D, 0);
}
}
int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--speed-startup")) {
        assert(mods::quick_use_speed() == std::strtof(argv[2], nullptr)); return 0;
    }
    if (argc == 3) { assert(mods::quick_use() == (std::atoi(argv[2]) != 0)); return 0; }
    map(player_addr); map(attention); map(enemy); map(actor);
    map(0x10460000); map(0x10470000); map(0x101E0000); map(0x101F0000);
    assert(!mods::quick_use());
    // Disabled wrappers preserve native outputs and call through exactly once, even
    // with a speed-only preference. Hook handlers must not resolve guest targets.
    for (float speed : {1.f, 1.25f, 1.5f}) {
        mods::set_quick_use_speed(speed);
        const auto inputs = native_inputs, setups = native_setups, targets = resolves;
        input_tick(); animation(0x105);
        assert(native_inputs == inputs + 1 && native_setups == setups + 1 && resolves == targets);
    }
    mods::set_quick_use_speed(1.5f);
    setup(0x2D); accepted_equip();
    reset_during_setup = true; animation(0x105); reset_during_setup = false;
    assert(!key(20.f)); advance_animation(); assert(seen_rate == 1.f);
    mods::set_quick_use(false); mods::set_quick_use_speed(1.f);
    if (argc == 2 && !std::strcmp(argv[1], "--replay-animation-speed")) {
        assert(mods::quick_use_speed() == 1.f);
        for (float speed : {1.f, 1.25f, 1.5f})
        for (uint16_t id : {0x25u, 0x2Du, 0x2Fu}) for (unsigned which = 0; which < 3; ++which) {
            mods::set_quick_use_speed(speed);
            setup(id, which); accepted_equip(); animation(0x105);
            const auto count = ld16(player_addr + 0x69AC), latch = ld16(player_addr + 0x69AE);
            assert(!key(20.f)); // native predictive keys are observed, never widened
            advance_animation();
            assert(seen_rate == speed && u32_as_f32(ld32(player_addr + 0x58DC)) == speed);
            assert(u32_as_f32(ld32(player_addr + 0x58D8)) == 1.f); // snapshot has authored rate
            assert(ld16(player_addr + 0x69AC) == count && ld16(player_addr + 0x69AE) == latch);
            advance_animation(0); assert(seen_rate == 0); // no lower/other upper animation boost
        }
        mods::set_quick_use_speed(1.5f);
        setup(0x2D); accepted_equip(); animation(0x105);
        advance_animation(); assert(seen_rate == 1.f); // no acceleration before native key observation
        assert(!key(20.f));
        preview = true; blocked = true; advance_animation(); assert(seen_rate == 1.f);
        preview = false; blocked = false; st32(player_addr + 0x58DC, 0); // native preview rollback
        advance_animation(); assert(seen_rate == 1.5f); // preview did not consume host ownership
        mods::quick_use_reset(); advance_animation(); assert(seen_rate == 1.f);
        for (unsigned interruption = 0; interruption < 13; ++interruption) {
            setup(0x2D); accepted_equip(); animation(0x105); assert(!key(20.f));
            switch (interruption) {
            case 0: blocked = true; break;
            case 1: live = false; break;
            case 2: st32(player_addr + 4, 99); break;
            case 3: st16(player_addr + 0x69B2, 0x25); break;
            case 4: st16(player_addr + 0x69B0, item); break; // native publication ends equip speed
            case 5: st32(player_addr + 0x65F0, 0x66); break;
            case 6: st8(GD(0x1046F0B0) + 0x5BBB, 0x25); break;
            case 7: st32(player_addr + 0x583C, 123); break;
            case 8: animation(0x99); break;
            case 9: mods::set_quick_use(false); break;
            case 10: mods::set_quick_use_speed(1.f); break;
            case 11: mods::quick_use_reset(); break;
            case 12: st16(player_addr + 0x69B0, 0x103); break; // unexpected old-item change could add an unseen sound key
            }
            advance_animation(); assert(seen_rate == 1.f);
            mods::set_quick_use_speed(1.5f);
        }
        for (float native_rate : {0.f, -1.f, 2.f}) {
            setup(0x2D); accepted_equip(); animation(0x105, 2, native_rate); assert(!key(20.f));
            advance_animation(); assert(seen_rate == (native_rate == 2.f ? 3.f : native_rate));
            assert(u32_as_f32(ld32(player_addr + 0x58D8)) == native_rate); // compose once, no accumulation
        }
        setup(0x2D); accepted_equip(); animation(0x105); assert(!key(20.f));
        animation(0x105); advance_animation(); assert(seen_rate == 1.f); // same-pointer restart loses old observations
        setup(0x2D); accepted_equip(); animation(0x105); assert(!key(20.f));
        st32(player_addr + 0x58D8, f32_as_u32(2.f)); advance_animation(); assert(seen_rate == 2.f);
        st32(player_addr + 0x58D8, f32_as_u32(1.f)); advance_animation(); assert(seen_rate == 1.f); // no stale ramp/key owner resumes
        setup(0x2D); accepted_equip(); animation(0x105);
        st32(player_addr + 0x58DC, f32_as_u32(4.f)); assert(!key(5.f));
        advance_animation(); assert(u32_as_f32(ld32(player_addr + 0x58DC)) == 5.f && key(5.f)); // equality is NOT already fired
        setup(0x2D); accepted_equip(); animation(0x105, 2, 1.5928449630737305f);
        st32(player_addr + 0x58DC, f32_as_u32(0.7120791077613831f));
        assert(!key(2.4071550369262695f)); advance_animation();
        assert(u32_as_f32(ld32(player_addr + 0x58DC)) <= 2.4071550369262695f);
        assert(key(2.4071550369262695f)); // rounded divide/multiply must not step past the key
        for (uint16_t bad_attribute : {1u, 2u, 3u, 4u}) {
            setup(0x2D); accepted_equip(); animation(0x105); assert(!key(20.f));
            st8(player_addr + 0x58E6, bad_attribute); advance_animation(); assert(seen_rate == 1.f);
        }
        setup(0x2D); accepted_equip(); animation(0x105);
        for (unsigned k = 5; k < 14; ++k) assert(!key(float(k)));
        advance_animation(); assert(seen_rate == 1.f); // key inventory overflow fails closed
        for (auto callback : {stop_frame, replace_rate, reset_in_frame}) {
            setup(0x2D); accepted_equip(); animation(0x105); assert(!key(20.f));
            Cpu c = cpu(); c.r[3] = player_addr + 0x58D8;
            mods::quick_use_animation_call(&c, callback);
            assert(u32_as_f32(ld32(player_addr + 0x58D8)) ==
                   (callback == stop_frame ? 0.f : callback == replace_rate ? 1.75f : 1.f));
        }
        // Fractional event regression: widened predictive windows could fire at F=1.5,
        // then fire AGAIN at F=2.5 after acceleration stopped. Keep native queries unchanged
        // and cap the advance to land on the key. Simulate launch only in the native adapter.
        setup(0x2D); press(); st16(player_addr + 0x69AC, 12);
        assert(boomerang_release(0) == release_bits[slot]); animation(0x34);
        pad(0); // input spent lifetime ends, but native wind-up is still owned
        unsigned launches = 0;
        for (int tick = 0; tick < 8; ++tick) {
            if (key(2.75f)) { ++launches; st32(actor + 0xB0, 1); st16(player_addr + 0x69B0, 0x100); }
            advance_animation();
            if (tick == 0) assert(seen_rate == 1.5f);
            if (tick == 1) assert(seen_rate == 1.25f); // land ON key, don't skip its native window
        }
        assert(launches == 1 && u32_as_f32(ld32(player_addr + 0x58D8)) == 1.f);
        mods::set_quick_use_speed(1.2f); assert(mods::quick_use_speed() == 1.5f);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--replay-spent-flight")) {
        setup(0x25); press(); st16(player_addr + 0x69AC, 0); assert(grappling_tick(true));
        st8(player_addr + 0x690D, 0); st32(player_addr + 0x65F0, 0x7E);
        for (int tick = 0; tick < 10; ++tick) { ++now; input_tick(); }
        st32(player_addr + 0x65F0, proc(item)); st32(actor + 0xB0, 0);
        assert(!grappling_tick(true));
        pad(0); assert(!grappling_tick(false)); // same hold must not arm a second native throw
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--replay-ready-slot-handoff")) {
        // Captured native checkpoint replay: boomerang (slot 1) -> rope (slot 2).
        // Native code restores the OLD ready slot before publishing the new equipped item.
        setup(0x25, 2); st16(player_addr + 0x69B0, 0x2D); st16(player_addr + 0x69B2, item);
        st32(player_addr + 0x65F0, 8);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0);
        ++now; input_tick(); st8(player_addr + 0x68D9, 1);
        ++now; input_tick();
        st16(player_addr + 0x69B0, item);
        { Cpu c = cpu(); site_023F0670(&c); } // old slot 1 STILL published at actual dispatch
        st32(player_addr + 0x65F0, proc(item)); st16(player_addr + 0x69AC, 0);
        assert(!grappling_tick(false)); // no synthetic release against the old slot
        st8(player_addr + 0x68D9, slot); ++now; input_tick();
        assert(grappling_tick(false));
        return 0;
    }
    if (argc == 2 && (!std::strcmp(argv[1], "--replay-item-switch") ||
                      !std::strcmp(argv[1], "--replay-equip-movement"))) {
        const bool item_switch = !std::strcmp(argv[1], "--replay-item-switch");
        // Replay the observed session at steps 6710..6714: rope MOVE -> grounded lock-on
        // movement -> boomerang MOVE during an accepted, uninterrupted native item change.
        // The second replay is the observed 9 -> 8 idle/move transition during initial equip.
        setup(0x2D, 1);
        st16(player_addr + 0x69B0, item_switch ? 0x25 : 0xFF); st16(player_addr + 0x69B2, item);
        st32(player_addr + 0x65F0, item_switch ? 0x7D : 9);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0); st32(player_addr + 0x65F0, item_switch ? 9 : 8);
        for (int tick = 0; tick < 4; ++tick) { ++now; input_tick(); }
        st16(player_addr + 0x69B0, item);
        { Cpu c = cpu(); site_023F06AC(&c); }
        st32(player_addr + 0x65F0, proc(item));
        assert(boomerang_hold(0) == (4u << slot));
        st16(player_addr + 0x69AC, 12);
        assert(boomerang_release(0) == release_bits[slot]);
        return 0;
    }
    // Hammer replay: first edge equips the model, but native dispatch later lacks that
    // edge. Complete exactly one local trigger only after upper equip104 is finished.
    for (unsigned which = 0; which < 3; ++which) for (bool hold : {false, true}) {
        setup(0x33, which); st32(player_addr + 0x65F0, 8); st16(player_addr + 0x69B0, 0x2D);
        st16(player_addr + 0x69B2, item); st16(player_addr + 0x5888, 0x105);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        { Cpu c = cpu(); site_023EFB44(&c); }
        if (!hold) pad(0);
        st8(player_addr + 0x690D, 0); st16(player_addr + 0x69B0, item); st16(player_addr + 0x5888, 0x104);
        st32(player_addr + 0x6594, 0); // hammer uses no idle projectile actor
        ++now; input_tick();
        Cpu c = cpu(); c.r[3] = 0; site_023F05EC(&c); assert(c.r[3] == 0);
        st8(player_addr + 0x68D9, (slot + 1) % 3); st16(player_addr + 0x5888, 0xFFFF);
        ++now; input_tick(); c.r[3] = 0; site_023F05EC(&c); assert(c.r[3] == 0);
        st8(player_addr + 0x68D9, slot); ++now; input_tick();
        c = cpu(); c.r[3] = 0; Cpu expected = c; expected.r[3] = 4u << slot;
        site_023F05EC(&c); assert(!memcmp(&c, &expected, sizeof c));
        assert(automatic_releases == 1);
        // Native getReadyItem equality and side/front choice follow this predicate;
        // the fixture models one accepted branch, not native strike/collision effects.
        unsigned native_actions = 1;
        for (uint32_t mode : {0x52u, 0x53u, 0x54u, 0x51u, 8u}) {
            st32(player_addr + 0x65F0, mode); ++now; input_tick();
            c.r[3] = 0; site_023F05EC(&c); native_actions += c.r[3] != 0;
        }
        assert(native_actions == 1 && automatic_releases == 1);
        assert(ld16(player_addr + 0x69AE) == 0 && ld8(player_addr + 0x68D9) == slot);
    }
    for (unsigned reason = 0; reason < 10; ++reason) {
        setup(0x33); st32(player_addr + 0x65F0, 8); st16(player_addr + 0x69B0, 0x2D);
        st16(player_addr + 0x69B2, item); pad(buttons[slot]); st8(player_addr + 0x690D, 4);
        { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0); st16(player_addr + 0x69B0, item); st16(player_addr + 0x5888, 0xFFFF);
        switch (reason) {
        case 0: st32(player_addr + 0x65F0, 0x66); break; // actual native damage102
        case 1: st16(player_addr + 0x69B2, 0x34); break;
        case 2: st16(player_addr + 0x5888, 0xAB); break; // an unrelated/native attack clip is not equip readiness
        case 3: st16(player_addr + 0x69B0, 0x34); break;
        case 4: st32(attention + 0x54 + 12, 51); break;
        case 5: blocked = true; break;
        case 6: st8(GD(0x1046F0B0) + 0x5BBB, 0x34); break;
        case 7: mods::quick_use_reset(); break;
        case 8: now += 91; break;
        case 9: pad(buttons[1]); break;
        }
        ++now; input_tick(); Cpu c = cpu(); c.r[3] = 0; site_023F05EC(&c); assert(c.r[3] == 0);
        // Cancellation is never retried when damage/clip/identity becomes eligible again.
        st32(player_addr + 0x65F0, 8); st16(player_addr + 0x5888, 0xFFFF); ++now; input_tick();
        c.r[3] = 0; site_023F05EC(&c); assert(c.r[3] == 0 && automatic_releases == 0);
    }
    // Already-held hammer: its genuine native trigger stays native, with no second owned use.
    setup(0x33); st32(player_addr + 0x65F0, 8); st16(player_addr + 0x69B2, item); st16(player_addr + 0x5888, 0xFFFF);
    pad(buttons[slot]); st8(player_addr + 0x690D, 4); { Cpu c = cpu(); site_023EFB44(&c); }
    { Cpu c = cpu(); c.r[3] = 4; site_023F05EC(&c); assert(c.r[3] == 4);
      c.r[3] = 0; site_023F05EC(&c); assert(c.r[3] == 0 && automatic_releases == 0); }
    // Bomb creation is native and replaces equipped item31 with no-item100. Only the
    // accepted slot intent may follow that publication to the same held bomb process.
    for (unsigned which = 0; which < 3; ++which) for (bool hold : {false, true}) {
        setup(0x31, which); st16(player_addr + 0x69B0, 0xFF); st16(player_addr + 0x69B2, item);
        st32(player_addr + 0x65F0, 4); pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        { Cpu c = cpu(); site_023EFB44(&c); }
        if (!hold) pad(0);
        st8(player_addr + 0x690D, 0); st16(player_addr + 0x69B0, 0x100);
        st32(player_addr + 0x65A4, actor); st16(actor + 8, 0x126); st32(actor + 4, 91);
        { Cpu c = cpu(); site_023DF7C0(&c); }
        // Native carry workflow restores the old ready slot permanently; it is not a
        // projectile-mode handoff and must not await publication of the accepted slot.
        const unsigned old_slot = (slot + 1) % 3;
        st8(player_addr + 0x68D9, old_slot);
        ++now; input_tick();
        Cpu c = cpu(); c.r[30] = player_addr; c.r[3] = 0; site_023EA474(&c);
        assert(c.r[3] == 1); // native matching-bomb button predicate -> ordinary throw
        assert(automatic_releases == 1);
        c.r[3] = 0; site_023EA474(&c); assert(c.r[3] == 0); // cannot duplicate
        assert(automatic_releases == 1);
        assert(ld8(player_addr + 0x68D9) == old_slot); // local predicate never writes the guest slot
        assert(ld16(player_addr + 0x69AE) == 0 && ld32(actor + 0xB0) == 0);
    }
    for (unsigned reason = 0; reason < 9; ++reason) {
        setup(0x31, 2); st16(player_addr + 0x69B0, 0xFF); st16(player_addr + 0x69B2, item);
        st32(player_addr + 0x65F0, 4); pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0); st16(player_addr + 0x69B0, 0x100);
        st32(player_addr + 0x65A4, actor); st16(actor + 8, 0x126); st32(actor + 4, 91);
        { Cpu c = cpu(); site_023DF7C0(&c); }
        switch (reason) {
        case 0: st32(actor + 4, 92); break; // actor address reused by another PID
        case 1: st32(player_addr + 0x65A4, 0); break;
        case 2: st16(actor + 8, 0x127); break; // not the created inventory bomb
        case 3: st32(attention + 0x54 + 12, 51); break;
        case 4: blocked = true; break;
        case 5: st32(player_addr + 0x65F0, 0x1E); break;
        case 6: st8(GD(0x1046F0B0) + 0x5BBB + slot, 0x34); break;
        case 7: mods::quick_use_reset(); break;
        case 8: now += 91; break;
        }
        ++now; input_tick();
        Cpu c = cpu(); c.r[30] = player_addr; c.r[3] = 0; site_023EA474(&c); assert(c.r[3] == 0);
    }
    for (uint16_t id : {0x27u, 0x35u, 0x36u}) for (uint32_t manual : {0x76u, 0x80u, 0x83u, 0x94u}) {
        setup(id); st32(player_addr + 0x65F0, manual); st16(player_addr + 0x69B2, id);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        Cpu c = cpu(); site_023EFB44(&c); site_023E88B0(&c);
        c.r[3] = 123; site_023EB2B0(&c); assert(c.r[3] == 123);
        c.r[3] = 456; site_023EB434(&c); assert(c.r[3] == 456);
        pad(0); st8(player_addr + 0x690D, 0); st32(player_addr + 0x65F0, proc(id));
        c.r[3] = 123; site_023EB2B0(&c); assert(c.r[3] == 123); // cannot resurrect intent
    }
    // Native single-action tools/consumables and inherently manual views have no intent:
    // their local input results remain untouched, rather than adding duplicate uses.
    for (uint16_t id : {0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x29u, 0x2Au, 0x30u,
                        0x33u, 0x34u, 0x50u, 0x51u, 0x52u, 0x53u, 0x54u, 0x56u, 0x57u, 0x58u, 0x59u}) {
        setup(id); pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        Cpu c = cpu(); site_023EFB44(&c); c.r[3] = 123;
        site_023EB2B0(&c); assert(c.r[3] == 123);
        site_023EB434(&c); assert(c.r[3] == 123);
        c.r[30] = player_addr; site_023EA474(&c); assert(c.r[3] == 123);
    }
    // Bow release queries occur only after native animation readiness. The fixture models
    // that branch boundary, not native ammo consumption or projectile spawning.
    for (uint16_t id : {0x27u, 0x35u, 0x36u}) for (unsigned which = 0; which < 3; ++which)
    for (bool hold : {false, true}) for (bool retry : {false, true}) {
        setup(id, which); press(); st8(player_addr + 0x690D, 0);
        if (!hold) pad(0);
        Cpu c = cpu();
        if (retry) {
            st16(player_addr + 0x5888, 0x0E); st32(player_addr + 0x6594, 0);
            c.r[3] = 0; site_023EB434(&c); assert(c.r[3] == (4u << slot));
            st32(player_addr + 0x6594, actor); // native creates the next readied arrow
        }
        st16(player_addr + 0x5888, 0x36); st16(player_addr + 0x69AC, 0);
        c.r[3] = 4u << slot; site_023EB2B0(&c);
        assert(c.r[3] == 0); // one native release, including a held physical press
        assert(automatic_releases == 1);
        c = cpu(); c.r[3] = 0; site_023EB2B0(&c);
        assert(c.r[3] == (4u << slot)); // spent hold cannot repeatedly release arrows
        assert(automatic_releases == 1);
        c.r[3] = 4u << slot; site_023EB434(&c); assert(c.r[3] == 0); // no native redraw
        assert(ld16(player_addr + 0x69AE) == 0 && ld32(actor + 0xB0) == 0);
        mods::quick_use_reset(); c.r[3] = 0;
        site_023EB2B0(&c); assert(c.r[3] == 0);
    }
    // Short taps, long holds, all slots and both draw-dispatch paths. Full/half/duplicate logic
    // observations cannot produce a second release or advance a native countdown in a hook.
    for (uint16_t id : {0x2Fu, 0x25u}) for (unsigned which = 0; which < 3; ++which)
    for (bool hold : {false, true}) for (bool alternate : {false, true}) {
        setup(id, which); press(alternate);
        st8(player_addr + 0x690D, 0);
        if (!hold) pad(0);
        unsigned shots = 0;
        for (int tick = 0; tick <= 10; ++tick) {
            ++now; input_tick(); shots += grappling_tick(hold);
            assert(shots == unsigned(tick == 10));
        }
        assert(shots == 1 && ld16(player_addr + 0x69AE) == 0 && ld32(actor + 0xB0) == 1);
        // A release predicate is single-use, even with a duplicate native observation this step.
        assert(release_predicate() == 0);
        st32(actor + 0xB0, 0);
        for (int tick = 0; tick < 100; ++tick) { ++now; input_tick(); assert(!grappling_tick(hold)); }
        pad(0); ++now; press(); st16(player_addr + 0x69AC, 0);
        assert(grappling_tick(true)); // a genuine next press can use the already-readied item
    }
    for (unsigned which = 0; which < 3; ++which) for (bool hold : {false, true}) {
        setup(0x2D, which); press(); st8(player_addr + 0x690D, 0);
        if (!hold) pad(0);
        for (unsigned tick = 0; tick < 12; ++tick) {
            ++now; input_tick();
            assert(boomerang_release(release_bits[slot] | 0x80000000) == 0x80000000);
            assert(boomerang_hold(0) == (4u << slot));
            st16(player_addr + 0x69AC, ld16(player_addr + 0x69AC) + 1); // native counter, not a mod timer
        }
        assert(boomerang_release(0x80000000) == (release_bits[slot] | 0x80000000));
        assert(boomerang_hold(4u << slot) == 0);
        assert(boomerang_release(release_bits[slot]) == 0); // never replay one physical release
        pad(0); st16(player_addr + 0x69AC, 0); assert(boomerang_hold(0) == 0);
    }
    // Initial equip is asynchronous: preserve an accepted native slot choice through its model/
    // animation delay, without retaining a raw physical edge or requiring a second trigger.
    for (uint16_t id : {0x2Fu, 0x25u, 0x2Du, 0x27u, 0x35u, 0x36u}) for (unsigned which = 0; which < 3; ++which)
    for (uint32_t origin : {4u, 6u, 7u, 8u, 9u, 0x17u, 0x18u, 0x77u, 0x7Du, 0x81u, 0x84u})
    for (bool hold : {false, true}) {
        setup(id, which);
        const uint16_t old_item = origin == 0x77 ? 0x27 : origin == 0x7D ? 0x25 :
                                  origin == 0x81 ? 0x2D : origin == 0x84 ? 0x2F : 0xFF;
        st16(player_addr + 0x69B0, old_item); st16(player_addr + 0x69B2, id); st32(player_addr + 0x65F0, origin);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        Cpu c = cpu(), before = c; site_023EFB44(&c); assert(!memcmp(&c, &before, sizeof c));
        if (!hold) pad(0);
        st8(player_addr + 0x690D, 0);
        for (int tick = 0; tick < 20; ++tick) {
            if (tick == 2) {
                st32(player_addr + 0x65F0, 9); // native exit from the old item
                if (old_item != 0xFF) st8(player_addr + 0x68D9, (slot + 1) % 3);
            }
            if (tick == 8) st32(player_addr + 0x65F0, 8); // stopping while locked
            if (tick == 12) st32(player_addr + 0x65F0, 6); // ordinary walking is not damage
            ++now; input_tick();
            if (tick == 5) {
                Cpu old = cpu(); old.r[3] = 0;
                (id == 0x2F ? site_023EC12C : site_023EAE30)(&old);
                assert(old.r[3] == 0); // an old item checker cannot hijack the accepted equip
                old = cpu(); (id == 0x2F ? site_023F0670 : site_023F0624)(&old);
            }
        }
        st16(player_addr + 0x69B0, id);
        c = cpu();
        (bow(id) ? site_023E88B0 : id == 0x2F ? site_023F0624 : id == 0x25 ? site_023F0670 : site_023F06AC)(&c);
        st32(player_addr + 0x65F0, proc(id));
        if (old_item != 0xFF) {
            if (bow(id)) { c = cpu(); c.r[3] = 0; site_023EB434(&c); assert(c.r[3] == 0); }
            else assert(id == 0x2D ? boomerang_hold(0) == 0 : held_query(0) == 0);
            Cpu old = cpu(); old.r[3] = 0;
            (id == 0x2F ? site_023EC12C : site_023EAE30)(&old);
            assert(old.r[3] == 0); // an old checker during the handoff does not cancel new intent
            st8(player_addr + 0x68D9, slot); ++now; input_tick();
        }
        if (bow(id)) {
            c = cpu(); c.r[3] = 0; site_023EB434(&c); assert(c.r[3] == (4u << slot));
            st16(player_addr + 0x69AC, 0); c.r[3] = 4u << slot; site_023EB2B0(&c); assert(c.r[3] == 0);
        } else if (id == 0x2D) {
            assert(boomerang_hold(0) == (4u << slot));
            st16(player_addr + 0x69AC, 12); assert(boomerang_release(0) == release_bits[slot]);
        } else {
            assert(held_query(0) == (4u << slot)); st16(player_addr + 0x69AC, 0);
            assert(grappling_tick(false));
        }
    }
    // Damage, first-person aiming and item flight are real interruptions, unlike ordinary
    // grounded movement. A later native mode entry must not resurrect the accepted equip.
    for (uint32_t interruption : {0x1Eu, 0x66u, 0x76u, 0x7Cu, 0x7Eu, 0x80u, 0x83u, 0x85u, 0x94u, 0xFFFFu}) {
        setup(0x2F); st16(player_addr + 0x69B0, 0xFF); st16(player_addr + 0x69B2, item); st32(player_addr + 0x65F0, 4);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4); { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0); st32(player_addr + 0x65F0, interruption); ++now; input_tick();
        st16(player_addr + 0x69B0, item); st32(player_addr + 0x65F0, proc(item)); st16(player_addr + 0x69AC, 0);
        { Cpu c = cpu(); site_023F0624(&c); } assert(!grappling_tick(false));
    }
    // An interruption or stale context between the input callback and the native dispatch
    // must not sneak past the equipping guard in that same logic observation.
    for (bool stale : {false, true}) {
        setup(0x2F); st16(player_addr + 0x69B0, 0xFF); st16(player_addr + 0x69B2, item); st32(player_addr + 0x65F0, 4);
        pad(buttons[slot]); st8(player_addr + 0x690D, 4); { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0); st16(player_addr + 0x69B0, item);
        if (stale) now += 3; else st32(player_addr + 0x65F0, 0x66);
        { Cpu c = cpu(); site_023F0624(&c); }
        st32(player_addr + 0x65F0, proc(item)); st16(player_addr + 0x69AC, 0);
        assert(!grappling_tick(false));
    }
    // An actual dispatch owns the handoff, but it is still cancellable and cannot wait
    // indefinitely for native publication of the requested slot.
    for (unsigned reason = 0; reason < 6; ++reason) {
        setup(0x25, 2); st16(player_addr + 0x69B0, 0x2D); st16(player_addr + 0x69B2, item);
        st32(player_addr + 0x65F0, 8); pad(buttons[slot]); st8(player_addr + 0x690D, 4u << slot);
        { Cpu c = cpu(); site_023EFB44(&c); }
        pad(0); st8(player_addr + 0x690D, 0); st8(player_addr + 0x68D9, 1); st16(player_addr + 0x69B0, item);
        { Cpu c = cpu(); site_023F0670(&c); }
        st32(player_addr + 0x65F0, proc(item)); st16(player_addr + 0x69AC, 0);
        switch (reason) {
        case 0: st32(attention + 0x54 + 12, 51); break;
        case 1: st16(player_addr + 0x69B0, 0x2D); break;
        case 2: st8(GD(0x1046F0B0) + 0x5BBB + slot, 0x2D); break;
        case 3: st32(player_addr + 0x65F0, 0x66); break;
        case 4: pad(buttons[0]); break;
        case 5: for (int tick = 0; tick < 92; ++tick) { ++now; input_tick(); assert(held_query(0) == 0); } break;
        }
        ++now; input_tick(); st8(player_addr + 0x68D9, slot);
        assert(!grappling_tick(false));
    }
    // Already drawn: no mode-init dispatch is required when a new native press reaches a checker.
    setup(0x2F); pad(buttons[slot]); st8(player_addr + 0x690D, 4); st16(player_addr + 0x69AC, 0);
    assert(grappling_tick(true));
    setup(0x2D); pad(buttons[slot]); st8(player_addr + 0x690D, 4);
    assert(boomerang_release(8) == 0 && boomerang_hold(0) == 4);
    // A second tap while still drawing is not queued for after the first throw.
    setup(0x2F); press(); pad(0); st8(player_addr + 0x690D, 0);
    assert(!grappling_tick(false)); pad(buttons[slot]); st8(player_addr + 0x690D, 4);
    st16(player_addr + 0x69AC, 0); assert(grappling_tick(true)); pad(0); st32(actor + 0xB0, 0);
    assert(!grappling_tick(false));
    setup(0x2D); press(); st16(player_addr + 0x69AC, 0xFFFF);
    assert(boomerang_release(release_bits[slot]) == 0); // native threshold comparison is signed
    // The boomerang's checker lacks the grapple checker's own busy-actor gate; our shared
    // readiness guard must refuse both a missing actor and an already-in-flight actor.
    for (bool missing : {false, true}) {
        setup(0x2D); press(); st16(player_addr + 0x69AC, 12);
        if (missing) st32(player_addr + 0x6594, 0); else st32(actor + 0xB0, 1);
        assert(boomerang_release(0) == 0 && boomerang_hold(0) == 0);
    }
    // Reset between held-query and release-predicate cannot leave a guest armed latch behind.
    setup(0x2F); press(); st16(player_addr + 0x69AC, 0); assert(held_query(4) == 0);
    mods::quick_use_reset(); assert(release_predicate() == 0 && ld16(player_addr + 0x69AE) == 0);
    // Cancellation and native ineligible contexts: no synthetic predicate escapes the scope.
    for (unsigned reason = 0; reason < 21; ++reason) {
        setup(0x2F); press(); st8(player_addr + 0x690D, 0);
        switch (reason) {
        case 0: blocked = true; break;
        case 1: st8(GD(0x1046F0B0) + 0x5292, 1); break;
        case 2: st8(GD(0x1046F0B0) + 0x514C, 1); break;
        case 3: st32(GD(0x101F36CC), 1); break;
        case 4: st8(GD(0x101EA069), 1); break;
        case 5: st8(attention + 0x18, 0); break;
        case 6: st32(attention + 0x54 + 8, 0); break;
        case 7: st32(attention + 0x54 + 12, 51); break;
        case 8: live = false; break;
        case 9: st32(enemy + 0x39C, 0); break;
        case 10: st32(player_addr + 0x65F0, 0x83); break; // first person, damage, ship etc. are not MOVE
        case 11: st8(GD(0x1046F0B0) + 0x5BBB, 0x25); break;
        case 12: st16(player_addr + 0x69B0, 0x25); break;
        case 13: st32(player_addr + 0x8260, 1); break;
        case 14: st16(player_addr + 0x699C, 1); break;
        case 15: st8(player_addr + 0x690D, 1); break;
        case 16: st32(attention + 0xD4, 9); break;
        case 17: st32(player_addr + 0x65B0, 0); break;
        case 18: st32(player_addr + 4, 99); break; // reused Link memory is a different process
        case 19: st32(GD(0x1046F0B0) + 0x5B2C, 0); break;
        case 20: st8(player_addr + 0x68D9, 1); break;
        }
        assert(held_query(0) == 0);
    }
    setup(0x2F); press(); st8(player_addr + 0x690D, 0); now += 3;
    assert(held_query(0) == 0); // missed logic observations expire
    setup(0x2F); press(); st8(player_addr + 0x690D, 0);
    for (int tick = 0; tick <= 91; ++tick) { ++now; input_tick(); }
    assert(held_query(0) == 0); // a stalled draw cannot live indefinitely
    setup(0x2F); press(); st32(actor + 0xB0, 1); assert(held_query(0) == 0);
    setup(0x2F); press(); st32(player_addr + 0x6594, 0); assert(held_query(0) == 0);
    // Enable/reset while physically held must wait for a real release and new native trigger.
    setup(0x2F); mods::set_quick_use(false); pad(buttons[slot]); mods::set_quick_use(true);
    st8(player_addr + 0x690D, 4); st16(player_addr + 0x69AC, 0); assert(!grappling_tick(false));
    pad(0); press(); assert(grappling_tick(true));
    setup(0x2F); pad(input::kA | buttons[slot]); st8(player_addr + 0x690D, 4);
    assert(held_query(0) == 0); // simultaneous native cancel wins
    setup(0x2F); mods::set_quick_use(false); press();
    const auto before_resolves = resolves;
    assert(held_query(4) == 4 && held_query(0, true) == 0);
    assert(release_predicate() == 0 && resolves == before_resolves);
    assert(native_inputs > 0);
}
