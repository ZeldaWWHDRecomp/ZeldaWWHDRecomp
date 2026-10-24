// SPDX-License-Identifier: MPL-2.0
// Exercise the actual cloth and rope upload hooks with guest-memory packets and an
// upload spy. Dead stripping removes unrelated effects/game functions. Matrix
// blending is stubbed here: real-game captures and interp_pacing_test cover its
// rotation arithmetic; this test covers packet history, fractions and restoration.
#include <cassert>
#include <sys/mman.h>

#include "../src/interp_fx.cpp"

namespace {
bool active = false, recording = false, executing = false, blend_rec = false;
bool true_mode = false;
uint64_t stamp = 1;
float fraction = .5f;
int draws = 0;
std::vector<PacketField> observed;
std::vector<uint32_t> uploaded;
}

namespace interp {
bool enabled() { return active; }
bool in_execute() { return executing; }
bool blend_draw() { return active && !recording; }
bool record_pass() { return recording; }
uint64_t hold_pass_count() { return stamp; }
bool record_blended() { return blend_rec; }
uint64_t record_stamp() { return stamp + (blend_rec ? 1 : 0); }
bool display_record_blended() { return blend_rec; }
uint64_t display_record_stamp() { return record_stamp(); }
float pass_t() { return fraction; }
void blend_world_matrix(const float* a, const float* b, float* out, float t) {
    for (int i = 0; i < 12; i++) out[i] = a[i] + (b[i] - a[i]) * t;
}
}
void log_msg(const char*, ...) {}
namespace true60 { bool enabled() { return true_mode; } }

static void upload(Cpu*) {
    draws++;
    uploaded.clear();
    for (auto f : observed) {
        auto* w = reinterpret_cast<uint32_t*>(ppc_ptr(f.addr));
        uploaded.insert(uploaded.end(), w, w + f.words);
    }
}
extern "C" void f_021625DC_orig(Cpu* c) { upload(c); }
extern "C" void f_0245F638_orig(Cpu* c) { upload(c); }
extern "C" void f_021B89B4_orig(Cpu* c) { upload(c); }
extern "C" void f_0232B624_orig(Cpu* c) { upload(c); }
extern "C" void f_0232B984_orig(Cpu* c) { upload(c); }
extern "C" void f_023D0E68_orig(Cpu* c) { upload(c); }
extern "C" void f_021BBC34_orig(Cpu* c) { upload(c); }
extern "C" void f_025EC62C_orig(Cpu* c) { upload(c); }
extern "C" void f_025ED1BC_orig(Cpu* c) { upload(c); }

int main() {
    setenv("WWHD_INTERP_FX", "16", 1);
    setenv("WWHD_INTERP_FX_TRACE", "0", 1);
    constexpr uint32_t p = 0x10000000, size = 0x4000;
    void* mapped = mmap(ppc_ptr(p), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    assert(mapped == ppc_ptr(p));
    Cpu c{};
    c.r[3] = p;
    struct Case {
        void (*hook)(Cpu*);
        std::vector<PacketField> fields;
        uint32_t index = 0, stride = 0;
        bool index32 = false;
    };
    // Independent packet fixtures: each matrix and position/normal array is filled
    // with known big-endian floats. The untouched bytes catch writes outside them.
    std::vector<Case> cases = {
        {hook_021625DC, {{p+0xAC, 12, true}, {p+0xE0, 85*3*3}}},
        {hook_0245F638, {{p+0x1294, 12, true}, {p+0x1328, 84*3}, {p+0x1EF8, 84*3}, {p+0x26D8, 84*3}}, p+0x2EBE, 0x3F0},
        {hook_021B89B4, {{p+0x1290, 12, true}, {p+0x1344, 21*3}, {p+0x153C, 21*3}, {p+0x1734, 21*3}}, p+0x1A2E, 0xFC},
        {hook_0232B624, {{p+0xC4C, 12, true}, {p+0x9C, 35*3*3}}, p+0xC18, 0x4EC, true},
        {hook_0232B984, {{p+0xC1C, 12, true}}},
        {hook_023D0E68, {{p+0x98, 12, true}, {p+0xCC, 25*3}, {p+0x324, 25*3}, {p+0x57C, 25*3}}, p+0x906, 0x12C},
        {hook_021BBC34, {{p+0x98, 12, true}, {p+0xC8, 12, true}, {p+0xFC, 81*3*2}}},
    };
    for (auto test : cases) {
        memset(ppc_ptr(p), 0x55, size);
        g_packet_poses.clear();
        observed = test.fields;
        auto index = [&](uint32_t i) {
            if (test.index32) st32(test.index, i);
            else if (test.index) st8(test.index, i);
        };
        index(0);
        auto fill = [&](float value) {
            for (auto f : observed) {
                for (uint32_t i = 0; i < f.words; i++)
                    stf32(f.addr + 4*i, f.matrix ? (i%5 == 0 ? 1.f : 0.f) : value);
                if (f.matrix) stf32(f.addr + 12, value);
            }
        };
        auto expect_upload = [&](float value) {
            uint32_t offset = 0;
            for (auto f : observed) {
                for (uint32_t i = 0; i < f.words; i++) {
                    float expected = f.matrix ? (i == 3 ? value : (i%5 == 0 ? 1.f : 0.f)) : value;
                    assert(std::fabs(wf(uploaded[offset+i]) - expected) < .0001f);
                }
                offset += f.words;
            }
        };
        auto checked_draw = [&] {
            std::vector<uint8_t> exact(ppc_ptr(p), ppc_ptr(p) + size);
            int old = draws;
            test.hook(&c);
            assert(draws == old + 1);
            assert(memcmp(exact.data(), ppc_ptr(p), size) == 0);
        };
        fill(10.f);
        active = false;
        recording = executing = false;
        checked_draw();  // 30 fps: every guest byte and uploaded float stays exact
        expect_upload(10.f);
        assert(g_packet_poses.empty());
        active = executing = true;
        checked_draw();  // an Execute-time upload must also remain exact
        expect_upload(10.f);
        assert(g_packet_poses.empty());
        executing = false;
        recording = true;
        stamp = 1;
        checked_draw();
        expect_upload(10.f);
        fill(20.f);
        recording = false;
        for (int denominator : {2, 4, 8}) {
            for (int phase = 1; phase < denominator; phase++) {
                fraction = float(phase)/denominator;
                checked_draw();
                expect_upload(10.f + 10.f*fraction);
            }
        }
        stamp = 4;
        checked_draw();  // not drawn on recent record passes
        expect_upload(20.f);
        stamp = 1;
        if (test.index) {
            for (auto& f : observed) if (!f.matrix) f.addr += test.stride;
            index(1);
            fill(20.f);
            fraction = .5f;
            checked_draw();  // toggling native double buffers keeps the same history
            expect_upload(15.f);
            index(2);
            checked_draw();  // invalid buffer index is passed through
            expect_upload(20.f);
            index(1);
        }
        fill(1000.f);
        checked_draw();  // teleport: exact upload
        expect_upload(1000.f);
        fill(20.f);
        stf32(observed[0].addr, NAN);
        checked_draw();  // invalid rotation: exact upload, no persistent mutation
        assert(std::isnan(wf(uploaded[0])));
        assert(wf(uploaded[3]) == 20.f);
        g_packet_poses.clear();
        fill(30.f);
        checked_draw();  // new packet / state-load history reset
        expect_upload(30.f);
        // 40 fps blended record pass: drawn blended from the record before it (a second draw in the
        // pass too), and recorded for the next step's frames
        recording = true;
        stamp = 1;
        checked_draw();
        recording = false;
        fill(40.f);
        blend_rec = true;
        fraction = .75f;
        checked_draw();
        expect_upload(37.5f);
        checked_draw();  // drawn twice in the pass: the same "before"
        expect_upload(37.5f);
        blend_rec = false;
        g_packet_before.clear();  // (fx_pass_start)
        stamp = 2;
        fill(50.f);
        fraction = .5f;
        checked_draw();  // the next step blends from the step the blended record pass recorded
        expect_upload(45.f);
        stamp = 1;
    }
    // Both line upload overloads use the same history and restore all inputs.
    for (auto hook : {hook_025EC62C, hook_025ED1BC}) {
        memset(ppc_ptr(p), 0, size);
        g_packet_poses.clear();
        g_line_layouts.clear();
        c.r[3] = p; c.r[4] = 3;
        st16(p+0x17C, 2); st16(p+0x17E, 3); st32(p+0x184, p+0x200);
        st32(p+0x200, p+0x300); st32(p+0x210, p+0x400);
        observed = {{p+0x300, 9}, {p+0x400, 9}};
        auto fill_lines = [&](float x) {
            for (auto f : observed) for (uint32_t i = 0; i < f.words; ++i) stf32(f.addr+4*i, x);
        };
        auto draw_lines = [&](float expected) {
            std::vector<uint8_t> exact(ppc_ptr(p), ppc_ptr(p)+size);
            hook(&c);
            assert(memcmp(exact.data(), ppc_ptr(p), size) == 0);
            for (auto w : uploaded) assert(std::fabs(wf(w)-expected)<.0001f);
        };
        fill_lines(10);
        active = recording = executing = false;
        draw_lines(10);
        assert(g_packet_poses.empty() && g_line_layouts.empty());
        active = true; true_mode = true;
        draw_lines(10);
        assert(g_packet_poses.empty() && g_line_layouts.empty());
        true_mode = false; executing = true;
        draw_lines(10);
        assert(g_packet_poses.empty() && g_line_layouts.empty());
        executing = false;
        active = recording = true; stamp = 1;
        draw_lines(10);
        recording = false;
        fill_lines(20);
        for (int denominator : {2, 4, 8}) for (int phase = 1; phase < denominator; ++phase) {
            fraction = float(phase)/denominator;
            draw_lines(10+10*fraction);
        }
        stamp = 2;
        draw_lines(20); // absent on even one exact record pass: no stable match
        stamp = 1;
        fill_lines(1000);
        draw_lines(1000); // teleport
        fill_lines(20);
        st32(p+0x210, p+0x500);
        observed[1].addr = p+0x500;
        fill_lines(20);
        draw_lines(20); // reused packet with different control-point storage
        assert(g_packet_poses.empty());
        recording = true;
        draw_lines(20);
        recording = false;
        stf32(observed[1].addr, NAN);
        std::vector<uint8_t> exact(ppc_ptr(p), ppc_ptr(p)+size);
        hook(&c);
        assert(memcmp(exact.data(), ppc_ptr(p), size) == 0);
        assert(std::isnan(wf(uploaded[9])));
    }
    munmap(mapped, size);
    puts("interp_cloth: seven cloth and two line uploads, identity, fractions, buffers, restore and discontinuities PASS");
}
