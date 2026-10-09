// Run the production glyph hooks on synthetic guest objects. No game files or GPU needed.
#include "button_glyphs.h"
#include "button_glyph_art.h"
#include "button_glyph_font.h"
#include "guest_addr.h"
#include "aspect.h"
#include "gx2/gx2_cmd.h"
#include "input_map.h"
#include "runtime.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

extern "C" {
void hook_0286E9F0(Cpu* c);
void hook_028F7C7C(Cpu* c);
void hook_02874D54(Cpu* c);
void hook_028766CC(Cpu* c);
}

// The real aspect hook is included in this test; only GPU emission and guest originals are
// stubbed. This verifies composition, rather than substituting a fake aspect callback.
void log_msg(const char*, ...) {}
namespace gx2 { void emit(Op, const uint32_t*, uint32_t) {} }
namespace mods::cemu { float aspect_ratio() { return 0; } }
extern "C" void f_02874038_orig(Cpu*) {}
extern "C" void f_02877100_orig(Cpu*) {}
extern "C" void f_028F8250_orig(Cpu*) {}

namespace {
constexpr uint32_t base = 0x15000000, writer = base, list = base + 0x100, picture = base + 0x1000, text_box = base + 0x2000;
constexpr uint32_t first_quad = list + 0xA4;
constexpr std::array<uint32_t, 2> authored = {0xFFFFFFFF, 0xB0804042};
constexpr std::array<uint32_t, 4> corners = {0xFFFFFFFF, 0x80A0C042, 0x00000000, 0xFFFFFF80};
uint32_t printed_character;
bool expect_xbox;
int picture_face;
unsigned prints = 0, text_draws = 0, picture_draws = 0;
uint32_t atlas = 0;
bool expect_frame = true;
bool font_available = true;
float label_centre = 0;
std::array<uint32_t,4> font_records{};
unsigned font_queries = 0;
void layout(input_map::FaceLayout choice);

constexpr uint32_t cluster = base + 0x3000;
constexpr std::array<uint32_t, 5> groups = {base + 0x3200, base + 0x3600, base + 0x3800,
                                         base + 0x3A00, base + 0x3400};  // A/B/X/Y/special A
constexpr std::array<uint32_t, 4> face_panes = {base + 0x3C00, groups[1], base + 0x4000, base + 0x4200};
constexpr uint32_t special_a = base + 0x3E00, item_x = base + 0x4600, trigger = base + 0x4800;

struct Point { float x, y; };

Point position(uint32_t pane) { return {(float)ldf32(pane + 0x54), (float)ldf32(pane + 0x64)}; }
void same_point(Point actual, Point expected) {
    assert(std::fabs(actual.x - expected.x) < 0.001f);
    assert(std::fabs(actual.y - expected.y) < 0.001f);
}

void pane(uint32_t address, const char* name, float x, float y) {
    std::memset(mem::ptr(address), 0, 0x100);
    std::strcpy((char*)mem::ptr(address + 0x80), name);
    st32(address + 0x14, address + 0x14);
    st32(address + 0x18, address + 0x14);
    stf32(address + 0x1C, x);
    stf32(address + 0x20, y);
    stf32(address + 0x34, 1);
    stf32(address + 0x38, 1);
}

void child(uint32_t parent, uint32_t node) {
    const uint32_t tail = ld32(parent + 0x18);
    st32(node, parent + 0x14);
    st32(node + 4, tail);
    st32(tail, node);
    st32(parent + 0x18, node);
    st32(node + 0x0C, parent);
}

// Model Pane::CalculateMtx's cached matrices, parent-dirty propagation and recursive child
// calls. All face placement runs through the production wrapper used by the aspect hook.
void matrix(Cpu* c) {
    const uint32_t address = c->r[3], parent = ld32(address + 0x0C);
    const bool dirty = c->r[5] || (ld8(address + 0x44) & 0x10);
    if (dirty) {
        const float angle = (float)ldf32(address + 0x30) * 0.01745329252f;
        const float sx = (float)ldf32(address + 0x34), sy = (float)ldf32(address + 0x38);
        const float x = (float)ldf32(address + 0x1C), y = (float)ldf32(address + 0x20);
        const float a = std::cos(angle) * sx, b = -std::sin(angle) * sy;
        const float d = std::sin(angle) * sx, e = std::cos(angle) * sy;
        const float pa = parent ? (float)ldf32(parent + 0x48) : 1;
        const float pb = parent ? (float)ldf32(parent + 0x4C) : 0;
        const float pd = parent ? (float)ldf32(parent + 0x58) : 0;
        const float pe = parent ? (float)ldf32(parent + 0x5C) : 1;
        const Point origin = parent ? position(parent) : Point{0, 0};
        stf32(address + 0x48, pa*a + pb*d);
        stf32(address + 0x4C, pa*b + pb*e);
        stf32(address + 0x58, pd*a + pe*d);
        stf32(address + 0x5C, pd*b + pe*e);
        stf32(address + 0x54, origin.x + pa*x + pb*y);
        stf32(address + 0x64, origin.y + pd*x + pe*y);
        st8(address + 0x44, ld8(address + 0x44) & ~0x10);
    }
    const uint32_t sentinel = address + 0x14;
    for (uint32_t node = ld32(sentinel); node != sentinel; node = ld32(node)) {
        Cpu next{};
        next.r[3] = node;
        next.r[5] = dirty;
        hook_028766CC(&next);
    }
    c->r[3] = 0xC004;
}

void calculate(bool dirty) {
    Cpu c{};
    c.r[3] = cluster;
    c.r[5] = dirty;
    hook_028766CC(&c);
    assert(c.r[3] == 0xC004);
}

void positions_test() {
    aspect::set_mode(aspect::kOriginal);
    aspect::on_swap();
    // Actual CommandGuide hierarchy/authoring coordinates, including different nested parents.
    pane(cluster, "N_All_00", 509, 238);
    pane(groups[0], "W_SetSeatA_00", 40, -35);
    pane(groups[1], "P_B_00", -9, -86);
    pane(groups[2], "N_X_00", 10, -1);
    pane(groups[3], "N_Y_00", 10, -1);
    pane(groups[4], "P_SetSeatASpecial_00", 40, -35);
    for (auto node : groups) child(cluster, node);
    pane(face_panes[0], "P_A_00", 0, -1.19055f); child(groups[0], face_panes[0]);
    pane(face_panes[2], "P_X_00", -19, 14); child(groups[2], face_panes[2]);
    pane(face_panes[3], "P_Y_00", -67, -35); child(groups[3], face_panes[3]);
    pane(special_a, "P_ASpecial_00", 0, -1.5f); child(groups[4], special_a);
    pane(item_x, "L_XItemIcon_00", -2, 12); child(face_panes[2], item_x);
    pane(trigger, "P_ZR_00", 66, 96); child(cluster, trigger);

    const std::array<uint32_t, 12> nodes = {cluster, groups[0], groups[1], groups[2], groups[3], groups[4],
        face_panes[0], face_panes[2], face_panes[3], special_a, item_x, trigger};
    std::array<std::array<uint32_t, 8>, nodes.size()> authored_positions;
    for (uint32_t i = 0; i < nodes.size(); i++)
        for (uint32_t j = 0; j < 8; j++) authored_positions[i][j] = ld32(nodes[i] + 0x1C + 4*j);

    layout(input_map::FaceLayout::kPosition);
    calculate(true);
    std::array<Point, 4> original;
    for (uint32_t i = 0; i < 4; i++) original[i] = position(face_panes[i]);
    const Point original_special = position(special_a), original_item = position(item_x), original_trigger = position(trigger);
    // Nintendo's A is east, B south, X north, Y west. Xbox transposes each pair's positions.
    assert(original[0].x > original[1].x && original[0].y > original[1].y);
    assert(original[2].y > original[3].y && original[2].x > original[3].x);
    for (auto mode : {input_map::FaceLayout::kLabels, input_map::FaceLayout::kLabels,
                     input_map::FaceLayout::kPosition, input_map::FaceLayout::kLabels,
                     input_map::FaceLayout::kCustom}) {
        layout(mode);
        calculate(false);  // cached matrices must update without an external dirty flag
        for (uint32_t i = 0; i < 4; i++) {
            const Point expected = original[expect_xbox ? (i ^ 1) : i];
            if (i == 0 && expect_xbox && std::fabs(position(face_panes[i]).y - expected.y) > 0.001f)
                std::fprintf(stderr, "A is not at the bottom: y=%g, expected=%g\n", position(face_panes[i]).y, expected.y);
            same_point(position(face_panes[i]), expected);
        }
        const Point a_delta{original[1].x - original[0].x, original[1].y - original[0].y};
        const Point x_delta{original[3].x - original[2].x, original[3].y - original[2].y};
        same_point(position(special_a), {original_special.x + (expect_xbox ? a_delta.x : 0),
                                        original_special.y + (expect_xbox ? a_delta.y : 0)});
        same_point(position(item_x), {original_item.x + (expect_xbox ? x_delta.x : 0),
                                     original_item.y + (expect_xbox ? x_delta.y : 0)});
        same_point(position(trigger), original_trigger);
        for (uint32_t i = 0; i < nodes.size(); i++)
            for (uint32_t j = 0; j < 8; j++) assert(ld32(nodes[i] + 0x1C + 4*j) == authored_positions[i][j]);
    }

    // Different viewport origin/scale, and animated container scale/rotation: compare the
    // actual centres in common-parent space, not hard-coded screen offsets or cached globals.
    stf32(cluster + 0x1C, 700);
    stf32(cluster + 0x34, 1.5f);
    stf32(cluster + 0x38, 0.75f);
    stf32(groups[0] + 0x34, 1.2f);
    stf32(groups[2] + 0x38, 1.25f);
    stf32(groups[3] + 0x30, 12);
    aspect::layout_root_target(cluster, true);
    aspect::set_mode(aspect::k21x9);
    aspect::on_swap();
    layout(input_map::FaceLayout::kPosition);
    calculate(true);
    std::array<Point, 4> animated;
    for (uint32_t i = 0; i < 4; i++) animated[i] = position(face_panes[i]);
    layout(input_map::FaceLayout::kLabels);
    calculate(false);
    for (uint32_t i = 0; i < 4; i++) same_point(position(face_panes[i]), animated[i ^ 1]);
    // Save states can restore old cached positions: recompute the recognised groups even
    // without a new input generation or an externally dirty matrix.
    for (uint32_t i = 0; i < 4; i++) {
        stf32(face_panes[i] + 0x54, animated[i].x);
        stf32(face_panes[i] + 0x64, animated[i].y);
    }
    calculate(false);
    for (uint32_t i = 0; i < 4; i++) same_point(position(face_panes[i]), animated[i ^ 1]);
    layout(input_map::FaceLayout::kPosition);
    calculate(false);
    for (uint32_t i = 0; i < 4; i++) same_point(position(face_panes[i]), animated[i]);

    // A similarly named but incomplete cluster must not move a standalone prompt or a
    // partial/modded layout. This also excludes the item-assignment screen's linear row.
    std::strcpy((char*)mem::ptr(groups[3] + 0x80), "N_Unrelated_00");
    layout(input_map::FaceLayout::kLabels);
    calculate(true);
    for (uint32_t i = 0; i < 4; i++) same_point(position(face_panes[i]), animated[i]);
    std::strcpy((char*)mem::ptr(groups[3] + 0x80), "N_Y_00");
    std::strcpy((char*)mem::ptr(cluster + 0x80), "N_Other_00");
    calculate(true);
    for (uint32_t i = 0; i < 4; i++) same_point(position(face_panes[i]), animated[i]);

    // The same layout reported on the GamePad is not TV-anchored; the production aspect
    // hook still composes correctly with a live Xbox placement switch there.
    std::strcpy((char*)mem::ptr(cluster + 0x80), "N_All_00");
    aspect::layout_root_target(cluster, false);
    layout(input_map::FaceLayout::kPosition);
    calculate(false);
    std::array<Point, 4> gamepad;
    for (uint32_t i = 0; i < 4; i++) gamepad[i] = position(face_panes[i]);
    assert(std::fabs(gamepad[0].x - animated[0].x) > 1);
    layout(input_map::FaceLayout::kLabels);
    calculate(false);
    for (uint32_t i = 0; i < 4; i++) same_point(position(face_panes[i]), gamepad[i ^ 1]);
    aspect::set_mode(aspect::kOriginal);
    aspect::on_swap();
}

void layout(input_map::FaceLayout choice) {
    auto mapping = input_map::Mapping::defaults();
    input_map::apply_face_layout(mapping, choice);
    if (choice == input_map::FaceLayout::kCustom) mapping.pad[input_map::kX] = input_map::kPadLB;
    input_map::set_current(mapping, false);
    expect_xbox = choice == input_map::FaceLayout::kLabels;
}

void print(uint32_t code) {
    Cpu c{};
    c.r[3] = writer;
    c.r[4] = code;
    printed_character = code;
    hook_0286E9F0(&c);
    assert(c.r[3] == 0xC001);  // preserve the original function's return value
}

void draw() {
    // The real TextBox::DrawSelf branches past vertex generation when list+6 is nonzero.
    // Exercise that gate, rather than calling the vertex generator unconditionally.
    button_glyphs::prepare_text_box(text_box);
    if (ld8(list + 6)) return;
    Cpu c{};
    c.r[4] = list;
    c.r[5] = base + 0x1800;  // an optional outline style; never recoloured
    st32(c.r[5], 0x121212A0);
    hook_028F7C7C(&c);
    assert(c.r[3] == 0xC002);
    assert(ld32(c.r[5]) == 0x121212A0);
}

void unchanged_quads(const std::array<uint8_t, 7 * 0x30>& stock) {
    for (uint32_t i = 0; i < stock.size(); i++) {
        const uint8_t want = stock[i];
        const uint8_t actual = ld8(first_quad + i);
        if (i % 0x30 == 0x2F && button_glyphs::marked_face(want) >= 0)
            assert((actual & ~8u) == want);  // only the recorded rendered-style bit may change
        else assert(actual == want);
    }
}

void shoulders_test() {
    st32(writer + 0x2C, list);
    st32(text_box + 0xFC, list);
    st32(list, 8); st16(list + 4, 0); st8(list + 6, 0);
    layout(input_map::FaceLayout::kPosition);
    for (uint32_t i = 0; i < 4; i++) print(0xE083 + i);
    for (uint32_t i = 0; i < 4; i++)
        assert(button_glyphs::marked_shoulder(ld8(first_quad + i*0x30 + 0x2F)) == int(i));
    std::array<uint8_t, 4*0x30> original;
    std::memcpy(original.data(), mem::ptr(first_quad), original.size());
    for (auto mode : {input_map::FaceLayout::kPosition, input_map::FaceLayout::kLabels,
                     input_map::FaceLayout::kLabels, input_map::FaceLayout::kCustom}) {
        layout(mode);
        draw();
        for (uint32_t i = 0; i < original.size(); i++) {
            const uint8_t value = ld8(first_quad + i);
            assert(i % 0x30 == 0x2F ? (value & ~8u) == original[i] : value == original[i]);
        }
    }
    assert(atlas);
    const uint32_t image = ld32(atlas + 0x10 + 0x24);
    // Lettering comes from the queried font masks, including partial edge coverage. Changing
    // those masks changes the composed result: no hard-coded lettering or fallback bitmap.
    assert(font_queries == 4);
    const auto composed = button_glyphs::shoulder_font_atlas(font_records);
    assert(composed.size() == 128*256*4);
    assert(!std::memcmp(composed.data(),mem::ptr(image),composed.size()));
    bool antialias = false;
    for (uint32_t y = 0; y < 256; y++) for (uint32_t x = 64; x < 128; x++) {
        const uint8_t alpha = composed[(y*128+x)*4+3];
        antialias |= alpha > 0 && alpha < 255;
    }
    assert(antialias);
    const uint32_t pixel = base + 0x5000 + 8192 + 23*256;
    const uint8_t coverage = ld8(pixel);
    st8(pixel,coverage/2);
    assert(button_glyphs::shoulder_font_atlas(font_records) != composed);
    st8(pixel,coverage);
    auto image_data = std::vector<uint8_t>(16384);
    std::memcpy(image_data.data(),mem::ptr(base+0x5000),image_data.size());
    // An asymmetric native-sheet gradient locks down both axes independently: stored Y
    // increases upward, X increases rightward. The composed caption must be top-down,
    // with neither a horizontal mirror nor a reversal of the letter/digit order.
    for (uint32_t glyph = 0; glyph < 4; glyph++) for (uint32_t y = 0; y < 9; y++) for (uint32_t x = 0; x < 6; x++)
        st8(base+0x5000+8192+(23+y)*256+glyph*8+x,20+x*20+y*12);
    const auto oriented = button_glyphs::shoulder_font_atlas(font_records);
    assert(oriented.size() == composed.size());
    auto alpha_at = [&](uint32_t x, uint32_t y) { return oriented[((192+y)*128+64+x)*4+3]; };
    assert(alpha_at(8,10) > alpha_at(8,52));  // top samples high native Y
    assert(alpha_at(20,10) > alpha_at(8,10)); // right samples high native X
    std::memcpy(mem::ptr(base+0x5000),image_data.data(),image_data.size());
    // Packed RG4 alpha sheets use two logical pixels per physical texel, through the
    // sampler component map. This is the core font's packed-alpha path, not just RGBA images.
    st32(base+0xD010+4,16); st32(base+0xD010+0x14,2); st32(base+0xD010+0x84,0x00010203);
    for (uint32_t y = 0; y < 32; y++) for (uint32_t x = 0; x < 16; x++)
        st8(base+0x5000+8192+y*256+x,(image_data[8192+y*256+x*2]&0xF0)|(image_data[8192+y*256+x*2+1]>>4));
    assert(!button_glyphs::shoulder_font_atlas(font_records).empty());
    // The real CKingMain HUD font uses BC4, not an uncompressed alpha bitmap. Exercise
    // compressed blocks, interpolated alpha and a nonzero font page at the same seam.
    st32(base+0xD010+4,32); st32(base+0xD010+0x14,0x34);
    st32(base+0xD010+0x3C,8); st32(base+0xD010+0x20,1024); st32(base+0xD010+0x84,0x05050500);
    for (uint32_t block = 0; block < 128; block++) {
        const uint32_t address = base+0x5000+block*8;
        st8(address,255); st8(address+1,0);
        uint64_t indices = 0;
        for (uint32_t pixel = 0; pixel < 16; pixel++) indices |= uint64_t((pixel+block)%8) << (pixel*3);
        for (uint32_t byte = 0; byte < 6; byte++) st8(address+2+byte,indices>>(byte*8));
    }
    const auto bc4 = button_glyphs::shoulder_font_atlas(font_records);
    assert(bc4.size() == composed.size());
    bool bc4_edges = false;
    for (uint32_t y = 0; y < 256; y++) for (uint32_t x = 64; x < 128; x++)
        bc4_edges |= bc4[(y*128+x)*4+3] > 0 && bc4[(y*128+x)*4+3] < 255;
    assert(bc4_edges);
    st32(base+0xD010+4,32); st32(base+0xD010+0x14,1); st32(base+0xD010+0x84,0x05050500);
    st32(base+0xD010+0x3C,256); st32(base+0xD010+0x20,16384);
    std::memcpy(mem::ptr(base+0x5000),image_data.data(),image_data.size());

    // Plain HUD text is a separate path: R and ZR must become complete R1/R2 labels in
    // their existing backgrounds, without modifying the game's source text or glyph metrics.
    expect_frame = false;
    for (int shoulder = 0; shoulder < 4; shoulder++) {
        const char* text_names[] = {"T_L_00", "T_R_00", "T_ZL_00", "T_ZR_00"};
        const char* parent_names[] = {"P_L_00", "P_R_00", "P_ZL_00", "P_ZR_00"};
        std::strcpy((char*)mem::ptr(text_box + 0x80), text_names[shoulder]);
        std::strcpy((char*)mem::ptr(picture + 0x80), parent_names[shoulder]);
        st32(text_box + 0x0C, picture); stf32(picture + 0x3C, 68);
        st32(text_box + 0xB4, base + 0xE100);
        st32(text_box + 0xA4, base + 0xB000);
        const uint32_t count = shoulder < 2 ? 1 : 2;
        st16(text_box + 0xCE, count);
        st16(base + 0xB000, shoulder < 2 ? (shoulder & 1 ? 'R' : 'L') : 'Z');
        st16(base + 0xB002, shoulder < 2 ? 0 : (shoulder & 1 ? 'R' : 'L'));
        st16(base + 0xB004, 0);
        st16(list + 4, 0); st8(list + 6, 0);
        layout(input_map::FaceLayout::kPosition);
        for (uint32_t i = 0; i < count; i++) print(ld16(base + 0xB000 + i*2));
        // Realistic narrow text glyphs, unlike the square icon-font cells above.
        for (uint32_t i = 0; i < count; i++) {
            stf32(first_quad + i*0x30, count == 1 ? 26 : (i == 0 ? 20 : 24));
            stf32(first_quad + i*0x30 + 4, 40);
            stf32(first_quad + i*0x30 + 8, i*22);
        }
        label_centre = count == 1 ? 13 : 23;
        std::array<uint8_t, 2*0x30> quads;
        std::memcpy(quads.data(), mem::ptr(first_quad), quads.size());
        auto textbox_draw = [](Cpu* c) { draw(); c->r[3] = 0xC005; };
        for (auto mode : {input_map::FaceLayout::kPosition, input_map::FaceLayout::kLabels,
                         input_map::FaceLayout::kLabels, input_map::FaceLayout::kPosition}) {
            layout(mode);
            Cpu c{}; c.r[3] = text_box;
            button_glyphs::draw_text_box(&c, textbox_draw);
            assert(c.r[3] == 0xC005);
            assert(ld16(list + 4) == count);
            assert(ld32(list + 0x0C) == (expect_xbox ? 4 : count*4));
            assert(ld32(text_box + 0xA4) == base + 0xB000 && ld16(text_box + 0xCE) == count);
            for (uint32_t i = 0; i < count*0x30; i++)
                if (i % 0x30 != 0x2F) assert(ld8(first_quad + i) == quads[i]);
        }
        layout(input_map::FaceLayout::kLabels);
        Cpu c{}; c.r[3] = text_box;
        button_glyphs::draw_text_box(&c, textbox_draw);
        st32(list + 0x20, base + 0xC000);  // a cached atlas pointer restored from a previous host
        const unsigned before = text_draws;
        c.r[3] = text_box;
        button_glyphs::draw_text_box(&c, textbox_draw);
        assert(text_draws == before + 1);
    }
    // A mod/ordinary text using "RB" in a similarly named pane is not a Nintendo R label.
    std::strcpy((char*)mem::ptr(text_box + 0x80), "T_R_00");
    std::strcpy((char*)mem::ptr(picture + 0x80), "P_R_00");
    st16(text_box + 0xCE, 2); st16(base + 0xB000, 'R'); st16(base + 0xB002, 'B');
    st16(list + 4, 0); st8(list + 6, 0);
    print('R'); print('B');
    Cpu other{}; other.r[3] = text_box;
    button_glyphs::draw_text_box(&other, [](Cpu*) { draw(); });
    assert(button_glyphs::marked_shoulder(ld8(first_quad + 0x2F)) == -1);
    assert(ld32(list + 0x0C) == 8 && ld16(list + 4) == 2);
    expect_frame = true;
}

void font_fallback_test() {
    st32(writer + 0x2C,list); st32(text_box + 0xFC,list); st32(list,8);
    std::strcpy((char*)mem::ptr(text_box+0x80),"T_ZR_00");
    std::strcpy((char*)mem::ptr(picture+0x80),"P_ZR_00");
    st32(text_box+0x0C,picture); stf32(picture+0x3C,68);
    st32(text_box+0xB4,base+0xE100); st32(text_box+0xA4,base+0xB000);
    st16(text_box+0xCE,2); st16(base+0xB000,'Z'); st16(base+0xB002,'R');
    st16(list+4,0); st8(list+6,0);
    print('Z'); print('R');
    stf32(first_quad,20); stf32(first_quad+0x30,24); stf32(first_quad+0x38,22);
    layout(input_map::FaceLayout::kLabels);
    font_available = false;
    st32(base+0xE108,0); // font is not yet loaded
    auto draw_box = [] { Cpu c{}; c.r[3]=text_box; button_glyphs::draw_text_box(&c,[](Cpu*) { draw(); }); };
    draw_box();
    assert(ld16(list+4)==2 && ld32(list+0x0C)==8);
    unsigned draws=text_draws;
    const unsigned queries=font_queries;
    draw_box();
    assert(text_draws==draws && font_queries==queries);
    st32(list+0x20,base+0xC000); // restored foreign atlas while the font is unavailable
    draw_box();
    assert(text_draws==draws+1 && ld16(list+4)==2 && ld32(list+0x0C)==8);
    assert(ld32(list+0x20)==base+0xD000);
    draws=text_draws;
    // Loaded but unsupported resource: one attempt, retain both original characters and
    // reuse the clean cache thereafter. Switching to a new resource retries successfully.
    st32(base+0xE108,base+0xF100); st32(base+0xD010+0x14,0x3F);
    draw_box();
    assert(ld16(list+4)==2 && ld32(list+0x0C)==8);
    const unsigned failed_queries=font_queries;
    draw_box(); assert(font_queries==failed_queries && text_draws==draws);
    st32(base+0xE108,base+0xF000); st32(base+0xD010+0x14,1);
    font_available = true; expect_frame = false; label_centre = 23;
    draw_box();
    assert(ld16(list+4)==2 && ld32(list+0x0C)==4);
    assert(text_draws==draws+1);
    expect_frame = true;
    std::memset(mem::ptr(text_box+0x80),0,24);
}

}  // namespace

extern "C" void f_028766CC_orig(Cpu* c) { matrix(c); }

namespace mem {
uint32_t host_alloc(uint32_t size, uint32_t align) {
    static uint32_t next = base + 0x10000;
    next = (next + align - 1) & ~(align - 1);
    const uint32_t result = next;
    next += size;
    assert(next <= base + 0x40000);
    return result;
}
}
extern "C" void imp_gx2_GX2InitTextureRegs(Cpu* c) {
    assert(ld32(c->r[3]) == 5 && ld32(c->r[3] + 12) == 1);
    assert(ld32(c->r[3] + 4) == 128 && ld32(c->r[3] + 8) == 256);
    assert(ld32(c->r[3] + 0x14) == 0x1A && ld32(c->r[3] + 0x30) == 1);
    assert(ld32(c->r[3] + 0x3C) == 128 && ld32(c->r[3] + 0x84) == 0x00010203);
    atlas = c->r[3] - 16;
}

extern "C" void ppc_dispatch(Cpu* c) {
    assert(c->pc == 0x0286F7BC && c->r[3] == base + 0xE100);
    constexpr uint32_t characters[] = {'L','R','1','2'};
    uint32_t index = 0;
    while (index < 4 && characters[index] != c->r[5]) index++;
    assert(index < 4);
    const uint32_t record = c->r[4];
    font_records[index] = record;
    font_queries++;
    st8(record + 4,index == 1 ? 255 : 0);  // signed left bearing
    st8(record + 5,6); st8(record + 6,8); st8(record + 7,9);
    st16(record + 8,32); st16(record + 10,32);
    st16(record + 12,index*8); st16(record + 14,0);
    st8(record + 0x13,1); st32(record + 0x14,base + 0xD000);
    c->r[3] = 0xF001; c->r[4] = 0xDEAD;  // cloned CPU changes cannot corrupt the draw caller
}

// Simulate the original writer, including a full list and heap reuse that resets quad flags.
extern "C" void f_0286E9F0_orig(Cpu* c) {
    prints++;
    assert(c->r[4] == printed_character);  // the requested letter must never be swapped
    const uint32_t target = ld32(c->r[3] + 0x2C);
    if (target) {
        const uint32_t count = ld16(target + 4);
        if (count < ld32(target)) {
            const uint32_t quad = target + 0xA4 + 0x30 * count;
            std::memset(mem::ptr(quad), 0, 0x30);
            st32(quad + 0x10, authored[0]);
            st32(quad + 0x14, authored[1]);
            stf32(quad, 54); stf32(quad + 4, 54); stf32(quad + 8, count*60);
            st32(quad + 0x28, base + 0xD000);
            st8(quad + 0x2E,1);  // the source font can put the glyph on a nonzero atlas page
            st8(quad + 0x2F, count & 1);  // test both values of the game's colour-font bit
            st16(target + 4, count + 1);
        }
    }
    c->r[3] = 0xC001;
}

// Inspect the colours the actual draw hook supplies to vertex generation.
extern "C" void f_028F7C7C_orig(Cpu* c) {
    text_draws++;
    bool uses_atlas = false;
    for (uint32_t i = 0; i < ld16(c->r[4] + 4); i++) {
        const uint32_t quad = c->r[4] + 0xA4 + 0x30 * i;
        const int face = button_glyphs::marked_face(ld8(quad + 0x2F));
        const int shoulder = button_glyphs::marked_shoulder(ld8(quad + 0x2F));
        for (uint32_t j = 0; j < 2; j++) {
            const uint32_t want = expect_xbox && face >= 0 ? button_glyphs::tint(authored[j], face) : authored[j];
            assert(ld32(quad + 0x10 + j * 4) == want);
            assert(ld8(quad + 0x13 + j * 4) == (authored[j] & 0xFF));  // fade alpha
        }
        assert((ld8(quad + 0x2F) & 1) == (expect_xbox && font_available && shoulder >= 0 ? 1 : (i & 1)));
        if (expect_xbox && font_available && shoulder >= 0) {
            assert(ld8(quad + 0x2E) == 0);
            assert(ld32(quad + 0x28) == atlas);
            assert(ldf32(quad + 0x18) == (expect_frame ? 0.0f : 0.5f));
            assert(ldf32(quad + 0x1C) == (3-shoulder)*0.25f);
            assert(ldf32(quad + 0x20) == (expect_frame ? 0.5f : 1.0f));
            assert(ldf32(quad + 0x24) == (4-shoulder)*0.25f);
            uses_atlas = true;
            if (!expect_frame) {
                assert(ldf32(quad) <= 68);  // fits the existing shoulder background
                assert(std::fabs(ldf32(quad + 8) + ldf32(quad)*0.5f - label_centre) < 0.001);
            }
        }
    }
    c->r[3] = 0xC002;
    st8(c->r[4] + 6, 1);  // original vertex generator marks this cache valid
    st32(c->r[4] + 0x0C, ld16(c->r[4] + 4)*4);
    st32(c->r[4] + 0xA0, 1);
    st32(c->r[4] + 0x20, uses_atlas ? atlas : base + 0xD000);
}

extern "C" void f_02874D54_orig(Cpu* c) {
    picture_draws++;
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t want = expect_xbox && picture_face >= 0 ? button_glyphs::tint(corners[i], picture_face) : corners[i];
        assert(ld32(c->r[3] + 0xA8 + i * 4) == want);
    }
    c->r[3] = 0xC003;
}

int main() {
#ifdef _WIN32
    auto memory = VirtualAlloc(mem::ptr(base), 0x40000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto memory = mmap(mem::ptr(base), 0x40000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(memory == mem::ptr(base));
#ifdef _WIN32
    auto globals = VirtualAlloc(mem::ptr(0x101F0000), 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto globals = mmap(mem::ptr(0x101F0000), 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(globals == mem::ptr(0x101F0000));
    st32(GD(0x101F4A50),base+0xE000); st32(base+0xE010,base+0xE100);
    st32(base+0xE104,base+0xE200); st32(base+0xE108,base+0xF000);
    st32(base+0xE10C,base+0xF080); st32(base+0xE144,base+0x5000);
    st32(base+0xE200+0x8C,0x0286F7BC);
    const uint32_t texture = base+0xD010;
    st32(texture,5); st32(texture+4,32); st32(texture+8,32); st32(texture+12,2);
    st32(texture+0x10,1); st32(texture+0x14,1);
    st32(texture+0x20,16384); st32(texture+0x24,base+0x5000);
    st32(texture+0x30,1); st32(texture+0x3C,256);
    st32(texture+0x80,2); st32(texture+0x84,0x05050500);
    // Synthetic antialiased masks, deliberately unlike the old 5x7 fallback. Page zero is
    // blank, so composing visible captions also verifies the native glyph page is honoured.
    for (uint32_t i = 0; i < 4; i++) for (uint32_t y = 0; y < 9; y++) for (uint32_t x = 0; x < 6; x++)
        st8(base+0x5000+8192+(23+y)*256+i*8+x,
            x == 0 || x == (i+1)%6 || y == 8 ? 80+i*30+y*5 : 0);

    // Standard Xbox palette, verified independently of tint's implementation.
    assert(button_glyphs::tint(0xFFFFFFFF, 0) == 0x58BE36FF);
    assert(button_glyphs::tint(0xFFFFFFFF, 1) == 0xE34B49FF);
    assert(button_glyphs::tint(0xFFFFFFFF, 2) == 0x3B91E5FF);
    assert(button_glyphs::tint(0xFFFFFFFF, 3) == 0xEBC52EFF);
    assert(button_glyphs::tint(0x000000A0, 0) == 0x000000A0);
    assert(button_glyphs::tint(0xFEDCBA00, -1) == 0xFEDCBA00);
    for (uint32_t code : {uint32_t('A'), uint32_t('B'), 0xE081u, 0x2665u, 0xE0030u})
        assert(button_glyphs::face_character(code) == -1);
    for (uint8_t flag : {uint8_t(0), uint8_t(1)}) {
        for (int face = 0; face < 4; face++) {
            auto tagged = button_glyphs::mark(flag, face);
            assert(button_glyphs::marked_face(tagged) == face);
            assert((tagged & 1) == flag);
        }
    }
    assert(button_glyphs::mark(0x40, 1) == 0x40);  // don't overwrite another mod's flags
    assert(button_glyphs::marked_face(0x40) == -1);

    st32(writer + 0x2C, list);
    st32(text_box + 0xFC, list);
    st32(list, 8);
    layout(input_map::FaceLayout::kPosition);
    for (uint32_t face = 0; face < 4; face++) print(0xE000 + face);
    print(0xE081);  // left stick icon
    print('A');     // ordinary text containing a letter A, including keyboard instructions
    print(0x2665);  // heart picture
    assert(ld16(list + 4) == 7);
    for (uint32_t i = 0; i < 7; i++) {
        const int face = button_glyphs::marked_face(ld8(first_quad + i * 0x30 + 0x2F));
        assert(face == (i < 4 ? int(i) : -1));
    }
    std::array<uint8_t, 7 * 0x30> stock;
    std::memcpy(stock.data(), mem::ptr(first_quad), stock.size());

    // No new Print calls: switching styles must also change an already-visible cached dialog.
    for (auto mode : {input_map::FaceLayout::kPosition, input_map::FaceLayout::kLabels,
                     input_map::FaceLayout::kPosition, input_map::FaceLayout::kLabels,
                     input_map::FaceLayout::kCustom}) {
        layout(mode);
        const unsigned before = prints;
        const unsigned draws = text_draws;
        draw();
        assert(prints == before);
        assert(text_draws == draws + 1);
        unchanged_quads(stock);
        // A same-style mapping generation or another frame must reuse the clean vertex cache.
        layout(mode);
        draw();
        assert(text_draws == draws + 1);
    }

    // Saved/restored cached quads retain identity, without any host-side address registry.
    layout(input_map::FaceLayout::kLabels);
    draw();
    std::array<uint8_t, 7 * 0x30> saved_xbox;
    std::memcpy(saved_xbox.data(), mem::ptr(first_quad), saved_xbox.size());
    layout(input_map::FaceLayout::kPosition);
    draw();
    assert(ld8(list + 6) == 1);
    std::memset(mem::ptr(first_quad), 0, stock.size());
    std::memcpy(mem::ptr(first_quad), saved_xbox.data(), saved_xbox.size());
    // The restored vertices are Xbox-coloured but the current choice is by position.
    const unsigned draws = text_draws;
    draw();
    assert(text_draws == draws + 1);
    assert(!std::memcmp(stock.data(), mem::ptr(first_quad), stock.size()));

    // Full list does not accidentally mark the previous quad; reuse clears old markers.
    st32(list, 7);
    print(0xE000);
    assert(!std::memcmp(stock.data(), mem::ptr(first_quad), stock.size()));
    st16(list + 4, 0);
    st8(list + 6, 0);
    print('A');
    assert(button_glyphs::marked_face(ld8(first_quad + 0x2F)) == -1);
    draw();
    st32(writer + 0x2C, 0);
    print(0xE000);  // a writer with no display list
    st32(text_box + 0xFC, 0);
    button_glyphs::prepare_text_box(text_box);

    for (auto name : {"P_A_00", "P_B_00", "P_X_00", "P_Y_00", "P_ASpecial_00", "P_ZR_00",
                      "P_R_00", "P_XGlow_00", "T_A_00", "P_A_000", "P_Unrelated_00"}) {
        std::memset(mem::ptr(picture), 0, 0xC0);
        std::strcpy((char*)mem::ptr(picture + 0x80), name);
        picture_face = button_glyphs::face_picture(name);
        for (uint32_t i = 0; i < 4; i++) st32(picture + 0xA8 + i * 4, corners[i]);
        std::array<uint8_t, 0xC0> stock_picture;
        std::memcpy(stock_picture.data(), mem::ptr(picture), stock_picture.size());
        for (auto mode : {input_map::FaceLayout::kPosition, input_map::FaceLayout::kLabels,
                         input_map::FaceLayout::kPosition, input_map::FaceLayout::kCustom}) {
            layout(mode);
            Cpu c{};
            c.r[3] = picture;
            hook_02874D54(&c);
            assert(c.r[3] == 0xC003);
            assert(!std::memcmp(stock_picture.data(), mem::ptr(picture), stock_picture.size()));
        }
    }

    positions_test();
    shoulders_test();
    font_fallback_test();

#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
    VirtualFree(globals, 0, MEM_RELEASE);
#else
    munmap(memory, 0x40000);
    munmap(globals, 0x10000);
#endif
    std::printf("button_glyphs_test: passed (%u prints, %u cached text draws, %u HUD draws)\n", prints, text_draws, picture_draws);
}
