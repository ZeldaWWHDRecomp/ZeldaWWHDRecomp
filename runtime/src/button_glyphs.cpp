// Draw-time face styling (Xbox colours and positions, PlayStation shapes) and shoulder labels
// using the game's HUD font, through common game/GX2 path. See docs/button-glyphs.md for hook points and layout semantics.
#include "button_glyphs.h"
#include "button_glyph_font.h"
#include "button_glyph_art.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "input_map.h"
#include "runtime.h"
#include "guest_addr.h"
#include "mods/mods.h"

extern "C" {
void f_0286E9F0_orig(Cpu* c);  // CharWriter::Print(writer, character)
void f_028F7C7C_orig(Cpu* c);  // build vertices from a TextBox's cached glyph list
void f_02874D54_orig(Cpu* c);  // lyt::Picture::DrawSelf
void imp_gx2_GX2InitTextureRegs(Cpu* c);
}

namespace {

using input_map::FaceStyle;
using input_map::PromptStyle;
using input_map::ShoulderStyle;

// Session gate: until a restyled look (by label, or a fixed shoulder name choice) has been on
// once, the hooks must not write game memory at all (no glyph markers, no composed texture).
// Players who never use it then run exactly the stock path. The first activation latches this
// flag; marking then works as in any restyled session, so later changes still reach text cached
// in either mode.
std::atomic<bool> labels_used{false};

void forget_rebuilt();
// The item ring's button names (mods/item_ring.cpp): A1 A2 A3 on the HUD's X, Y and R now. A change
// rebuilds every cached text once; turning them on counts as a restyled look for the session gate.
bool item_names() {
    static std::atomic<int> last{-1};
    const bool on = mods::item_names_on_hud();
    if (last.exchange(on ? 1 : 0, std::memory_order_relaxed) != (on ? 1 : 0)) {
        if (on) labels_used.store(true, std::memory_order_relaxed);
        if (labels_used.load(std::memory_order_relaxed)) forget_rebuilt();
    }
    return on;
}

void forget_rebuilt();

// The look as of the last style change, packed; a different one rebuilds every cached text once.
std::atomic<uint32_t> last_signature{0};
uint32_t signature(const PromptStyle& s) {
    uint32_t buttons = 0;
    for (int i = 0; i < 4; ++i) buttons |= uint32_t(s.buttons[i]) << (10 + 2*i);
    return buttons | 0x100u | (uint32_t)s.face | (s.colours ? 4u : 0u) | (uint32_t)s.shoulders << 3 | (s.positions ? 0x20u : 0u);
}

PromptStyle style() {
    // Per guest thread: layouts can be recorded on multiple cores. Each set_current (and a new pad
    // family) invalidates the cached choice, including transitions through custom bindings. No
    // SDL/AppKit dependency, and nothing beyond a generation compare per call.
    thread_local uint32_t generation = ~0u;
    thread_local PromptStyle current;
    const uint32_t now = input_map::generation();
    if (now != generation) {
        current = input_map::prompt_style(input_map::current(), input_map::pad_family());
        if (current.active()) labels_used.store(true, std::memory_order_relaxed);
        const uint32_t sig = signature(current);
        if (last_signature.exchange(sig, std::memory_order_relaxed) != sig && labels_used.load(std::memory_order_relaxed))
            forget_rebuilt();
        generation = now;
    }
    return current;
}
bool restyled() { return style().active(); }

// Restyled now, or it has been at least once in this session.
bool marking() { return restyled() || item_names() || labels_used.load(std::memory_order_relaxed); }

// The first activation happens against text cached in stock mode, which carries no markers the
// hooks could recognise. Each text box is therefore rebuilt once: the game re-runs Print, which
// marks the new quads. The registry is a one-shot switch (only consulted after activation) and
// never identifies glyphs; it cannot go stale because it only ever forces a rebuild.
std::mutex rebuilt_mutex;
std::unordered_set<uint32_t> rebuilt;  // text boxes rebuilt since by label was first used (or the last state load / style change)
bool first_rebuild(uint32_t text_box) {
    if (!labels_used.load(std::memory_order_relaxed)) return false;
    std::lock_guard lock(rebuilt_mutex);
    return rebuilt.insert(text_box).second;
}
void forget_rebuilt() {
    std::lock_guard lock(rebuilt_mutex);
    rebuilt.clear();
}

struct SavedWord {
    uint32_t address;
    uint32_t value;
};

thread_local int hud_shoulder = -1;
thread_local int hud_face = -1;  // a HUD face-letter pane (T_A_00 on P_A_00, ...)
thread_local uint32_t hud_box = 0;
thread_local int hud_action = -1;
std::string_view pane_name(uint32_t pane);
uint32_t child_named(uint32_t parent, std::string_view name);

uint32_t shoulder_atlas(Cpu* cpu) {
    const uint32_t manager = ld32(GD(0x101F4A50));
    uint32_t font = hud_shoulder >= 0 && hud_box ? ld32(hud_box + 0xB4) : 0;
    if (!font && manager) font = ld32(manager + 0x10);  // font slot 0: CKingMain (HUD A/B/X/Y)
    if (!font || !ld32(font + 8) || !ld32(font + 0x0C) || !ld32(font + 0x44)) return 0;
    struct Atlas { uint32_t font, resource, info; };
    static std::mutex mutex;
    static std::vector<Atlas> atlases;
    const uint32_t resource = ld32(font + 8);
    {
        std::lock_guard lock(mutex);
        for (const auto& atlas : atlases) if (atlas.font == font && atlas.resource == resource) return atlas.info;
    }
    if (!cpu) return 0;  // a cached list can be invalidated and rebuilt with a real CPU context
    const uint32_t table = ld32(font + 4), get_glyph = table ? ld32(table + 0x8C) : 0;
    if (!get_glyph) return 0;
    thread_local uint32_t scratch = mem::host_alloc(button_glyphs::kAtlasGlyphs*0x18, 16);
    std::array<uint32_t,button_glyphs::kAtlasGlyphs> records;
    constexpr uint32_t characters[] = {'L','R','1','2','B','T','A','3'};
    // Guest calls may yield: do not hold the host cache mutex across them. This is the same
    // Font::GetGlyph ABI that the original CharWriter uses, on a copy of the caller's CPU.
    for (uint32_t i = 0; i < records.size(); i++) {
        records[i] = scratch+i*0x18;
        std::memset(mem::ptr(records[i]),0,0x18);
        Cpu get = *cpu;
        get.r[3] = font; get.r[4] = records[i]; get.r[5] = characters[i];
        get.pc = get_glyph; get.ctr = get_glyph;
        ppc_dispatch(&get);
    }
    const auto pixels = button_glyphs::button_atlas(records);
    std::lock_guard lock(mutex);
    for (const auto& atlas : atlases) if (atlas.font == font && atlas.resource == resource) return atlas.info;
    // Loaded font resources are immutable during a session (content mods activate at boot).
    // Cache an unsupported resource too; don't query/decode it again on every HUD frame.
    if (pixels.empty()) { atlases.push_back({font,resource,0}); return 0; }
    // Font-sheet info has a 16-byte header followed by GX2Texture (+0x10), as bound by
    // the game's text draw at 028F86C8. Host-only storage survives guest state restores.
    // Two sheet objects on the same image: the RGBA one (info) and, 0x100 bytes on, a coverage
    // twin (coverage_sheet) for shapes that take the text colours like the HUD's letters.
    const uint32_t info = [&] {
        const uint32_t object = mem::host_alloc(0x200, 256);
        constexpr uint32_t width = button_glyphs::kAtlasWidth, height = button_glyphs::kAtlasHeight;
        const uint32_t image = mem::host_alloc(width * height * 4, 256);
        std::memset(mem::ptr(object), 0, 0xAC);
        std::memcpy(mem::ptr(object), mem::ptr(ld32(records[0] + 0x14)), 16);
        // +0x0E = 0: an RGBA sheet like the game's own icon font (issue #141). The header copied
        // from the HUD font says 1 (a coverage sheet), which the batch at 028F7A4C turns into the
        // text-colour path: it maps the texture through the text box's black/white colours, and
        // a dialog's white text then turned the whole framed ZL/ZR label white.
        st8(object + 0x0E, 0);
        std::memcpy(mem::ptr(image), pixels.data(), pixels.size());
        const uint32_t texture = object + 0x10;
        st32(texture, 5); st32(texture + 4, width); st32(texture + 8, height); st32(texture + 12, 1);  // one-layer font array
        st32(texture + 0x10, 1); st32(texture + 0x14, 0x1A);  // one mip, RGBA8
        st32(texture + 0x1C, 1); st32(texture + 0x20, width*height*4); st32(texture + 0x24, image);
        st32(texture + 0x30, 1); st32(texture + 0x38, height); st32(texture + 0x3C, width);  // linear-aligned
        st32(texture + 0x78, 1); st32(texture + 0x80, 1); st32(texture + 0x84, 0x00010203);
        Cpu init{}; init.r[3] = texture;
        imp_gx2_GX2InitTextureRegs(&init);
        std::memcpy(mem::ptr(object + 0x100), mem::ptr(object), 0xAC);
        st8(object + 0x100 + 0x0E, 1);  // coverage sheet: the text-colour path, as the HUD font's own
        return object;
    }();
    atlases.push_back({font,resource,info});
    return info;
}

void patch(std::vector<SavedWord>& saved, uint32_t address, uint32_t value) {
    saved.push_back({address, ld32(address)});
    st32(address, value);
}

void patch_float(std::vector<SavedWord>& saved, uint32_t address, float value) {
    saved.push_back({address, ld32(address)});
    stf32(address, value);
}

// The atlas sheet that draws through the text box's colours (coverage = the cell's alpha).
constexpr uint32_t coverage_sheet(uint32_t atlas) { return atlas + 0x100; }

// Point a glyph quad at a cell of the composed atlas (a shoulder label or a PlayStation shape).
// `coverage`: keep the quad a coverage glyph on the coverage sheet, so the game colours (and
// dims) it exactly like the letter it replaces; otherwise an RGBA picture (#141).
void atlas_quad(std::vector<SavedWord>& saved, uint32_t quad, int row, bool frame, Cpu* cpu, bool coverage = false) {
    const uint32_t atlas = shoulder_atlas(cpu);
    if (!atlas) return;
    const button_glyphs::Cell cell = button_glyphs::atlas_cell(row, frame);
    patch(saved, quad + 0x28, coverage ? coverage_sheet(atlas) : atlas);
    patch_float(saved, quad + 0x18, cell.u0);
    patch_float(saved, quad + 0x1C, cell.v0);
    patch_float(saved, quad + 0x20, cell.u1);
    patch_float(saved, quad + 0x24, cell.v1);
    // The composed array has one layer, regardless of the source glyph's page. Bit 0 of +0x2F
    // marks a coverage glyph (alpha font, coloured by the text box's colours); a composed RGBA
    // picture clears it like the original icons (issue #141), a coverage shape sets it. The original
    // page and flag are restored with the rest of the authored quad afterwards.
    patch(saved, quad + 0x2C, (ld32(quad + 0x2C) & ~0xFF01u) | (coverage ? 1u : 0u));
}

int shoulder_row(ShoulderStyle names, int shoulder) {
    return names == ShoulderStyle::kXbox ? button_glyphs::xbox_row(shoulder) : button_glyphs::numbered_row(shoulder);
}

// A HUD face letter: T_A_00 holding "A" on its P_A_00 picture (and so on for B, X, Y).
int face_box(uint32_t box) {
    const auto name = pane_name(box);
    const int face = name == "T_A_01" ? 0 : button_glyphs::face_text(name);
    if (face < 0) return -1;
    const uint32_t parent = ld32(box + 0x0C);
    if (!parent) return -1;
    if (button_glyphs::face_picture(pane_name(parent)) != face) {
        // CommandA_00 puts the ordinary letter beside its picture under N_Center_01,
        // unlike CommandGuide's child letter. Require that exact container and a matching
        // sibling picture; never treat arbitrary single-letter text as a button prompt.
        constexpr std::string_view pictures[] = {"P_A_00", "P_B_00", "P_X_00", "P_Y_00"};
        if (pane_name(parent) != "N_Center_01" || !child_named(parent, pictures[face])) return -1;
    }
    const uint32_t text = ld32(box + 0xA4);
    constexpr char letters[] = {'A', 'B', 'X', 'Y'};
    return text && ld16(box + 0xCE) == 1 && ld16(text) == letters[face] ? face : -1;
}

int shoulder_box(uint32_t box) {
    const int shoulder = button_glyphs::shoulder_text(pane_name(box));
    if (shoulder < 0) return -1;
    constexpr std::string_view parents[] = {"P_L_00", "P_R_00", "P_ZL_00", "P_ZR_00"};
    const uint32_t parent = ld32(box + 0x0C);
    if (!parent || pane_name(parent) != parents[shoulder]) return -1;
    const uint32_t text = ld32(box + 0xA4);
    if (!text) return -1;
    const char letter = shoulder & 1 ? 'R' : 'L';
    const uint32_t length = ld16(box + 0xCE);
    if (shoulder < 2) return length == 1 && ld16(text) == letter ? shoulder : -1;
    return length == 2 && ld16(text) == 'Z' && ld16(text + 2) == letter ? shoulder : -1;
}

void tint_word(std::vector<SavedWord>& saved, uint32_t address, int face) {
    const uint32_t rgba = ld32(address);
    saved.push_back({address, rgba});
    st32(address, button_glyphs::tint(rgba, face));
}

void restore(const std::vector<SavedWord>& saved) {
    for (const auto& word : saved) st32(word.address, word.value);
}

std::string_view pane_name(uint32_t pane) {
    const char* name = (const char*)mem::ptr(pane + 0x80);
    return {name, strnlen(name, 24)};
}

uint32_t child_named(uint32_t parent, std::string_view name) {
    const uint32_t sentinel = parent + 0x14;
    uint32_t child = ld32(sentinel);
    for (unsigned guard = 0; child && child != sentinel && guard < 64; guard++, child = ld32(child))
        if (pane_name(child) == name) return child;
    return 0;
}

int face_group(std::string_view name) {
    if (name == "W_SetSeatA_00" || name == "P_SetSeatASpecial_00") return 0;
    if (name == "P_B_00") return 1;
    if (name == "N_X_00") return 2;
    if (name == "N_Y_00") return 3;
    return -1;
}

struct Point { float x, y; };

// The button centres in their common parent's coordinates, from authored locals, not cached
// global matrices (which may already contain last frame's Xbox placement). A/X/Y pictures are
// children of different containers. Include those containers' animated scale and Z rotation.
Point button_centre(uint32_t group, uint32_t picture) {
    Point p{(float)ldf32(group + 0x1C), (float)ldf32(group + 0x20)};
    if (picture != group) {
        const float x = (float)ldf32(picture + 0x1C) * (float)ldf32(group + 0x34);
        const float y = (float)ldf32(picture + 0x20) * (float)ldf32(group + 0x38);
        const float angle = (float)ldf32(group + 0x30) * 0.01745329252f;
        p.x += std::cos(angle) * x - std::sin(angle) * y;
        p.y += std::sin(angle) * x + std::cos(angle) * y;
    }
    return p;
}

}  // namespace

void button_glyphs::calculate_pane(Cpu* c, PpcFunc calculate) {
    const uint32_t pane = c->r[3];
    const int face = face_group(pane_name(pane));
    if (face < 0) return calculate(c);
    const uint32_t parent = ld32(pane + 0x0C);
    if (!parent || pane_name(parent) != "N_All_00") return calculate(c);

    const std::array<uint32_t, 4> groups = {child_named(parent, "W_SetSeatA_00"), child_named(parent, "P_B_00"),
                                          child_named(parent, "N_X_00"), child_named(parent, "N_Y_00")};
    for (uint32_t group : groups) if (!group) return calculate(c);
    const std::array<uint32_t, 4> pictures = {child_named(groups[0], "P_A_00"), groups[1],
                                            child_named(groups[2], "P_X_00"), child_named(groups[3], "P_Y_00")};
    for (uint32_t picture : pictures) if (!picture) return calculate(c);

    // A complete four-face cluster identifies CommandGuide, not the standalone floating A
    // prompt or the item screen's linear row. Move the group root: labels, item art and effects
    // inherit the same transform. Force its tree's matrices in both modes so switching back or
    // restoring a state cannot leave the previous layout's cached global positions behind.
    // Only once by label has been used: until then the stock path stays untouched.
    if (marking()) c->r[5] = 1;
    if (!style().positions) return calculate(c);
    const Point from = button_centre(groups[face], pictures[face]);
    const Point to = button_centre(groups[face ^ 1], pictures[face ^ 1]);
    const float dx = to.x - from.x, dy = to.y - from.y;
    if (!std::isfinite(dx) || !std::isfinite(dy)) return calculate(c);
    const float x = (float)ldf32(pane + 0x1C), y = (float)ldf32(pane + 0x20);
    stf32(pane + 0x1C, x + dx);
    stf32(pane + 0x20, y + dy);
    calculate(c);
    // Animation/gameplay still own the local positions. Only the resulting global matrices
    // retain the rearrangement, matching how the aspect hook handles temporary anchoring.
    stf32(pane + 0x1C, x);
    stf32(pane + 0x20, y);
}

void button_glyphs::prepare_text_box(uint32_t text_box, Cpu* cpu) {
    const uint32_t list = ld32(text_box + 0xFC);
    if (!list) return;
    // First: a style change forgets the rebuilt boxes, before this one is recorded below.
    const PromptStyle look = style();
    const bool active = look.active();
    if (!ld8(list + 6)) {
        // The game rebuilds this text anyway; remember the box so it is not forced next frame.
        first_rebuild(text_box);
        return;
    }
    // The first restyled frame rebuilds each cached text once: it carries no identity bits yet,
    // so the game has to re-run Print before the draw hooks can style it.
    if (first_rebuild(text_box)) { st8(list + 6, 0); return; }
    const int hud = shoulder_box(text_box);
    if (hud >= 0 && ld16(list + 4) && marking()) {
        const uint32_t flag = list + 0xA4 + 0x2F;
        if (marked_shoulder(ld8(flag)) < 0) st8(flag, mark_shoulder(ld8(flag), hud));
    }
    const int letter = face_box(text_box);
    if (letter >= 0 && ld16(list + 4) && marking()) {
        const uint32_t flag = list + 0xA4 + 0x2F;
        if (marked_face(ld8(flag)) < 0) st8(flag, mark(ld8(flag), letter));
    }
    for (uint32_t i = 0, count = ld16(list + 4); i < count; i++) {
        const uint8_t flags = ld8(list + 0xA4 + i * 0x30 + 0x2F);
        const int face = marked_face(flags), shoulder = marked_shoulder(flags);
        if ((face >= 0 || shoulder >= 0) && styled(flags) != active) {
            st8(list + 6, 0);  // the original DrawSelf rebuilds the text and its vertex buffer
            return;
        }
        // quads drawn from the composed atlas: restyled shoulder labels and PlayStation shapes
        const bool named = item_names() && ((letter >= 2 && face >= 2) || (hud == 1 && shoulder == 1));
        if ((shoulder >= 0 && look.shoulders != ShoulderStyle::kOriginal) ||
            (face >= 0 && look.face == FaceStyle::kPlayStation) || named) {
            // Restoring a state in a fresh process can bring cached batch pointers from the
            // previous host allocation. Rebuild unless it binds this process's live atlas.
            const uint32_t atlas = shoulder_atlas(cpu);
            bool bound = false;
            for (uint32_t batch = 0, n = ld32(list + 0xA0); batch < n && batch < 8; batch++)
                bound |= ld32(list + 0x20 + batch*0x10) == atlas || ld32(list + 0x20 + batch*0x10) == coverage_sheet(atlas);
            if (atlas && !bound) { st8(list + 6, 0); return; }
            if (!atlas) {
                // A clean stock fallback is reusable. A foreign cached atlas is not, even
                // when this process cannot yet create its replacement font resource.
                const uint32_t batches = ld32(list + 0xA0);
                if (batches > 8) { st8(list + 6,0); return; }
                for (uint32_t batch = 0; batch < batches; batch++) {
                    const uint32_t sheet = ld32(list + 0x20 + batch*0x10);
                    bool stock = false;
                    for (uint32_t glyph = 0; glyph < count; glyph++)
                        stock |= sheet == ld32(list + 0xA4 + glyph*0x30 + 0x28);
                    if (!stock) { st8(list + 6,0); return; }
                }
            }
        }
    }
}

void button_glyphs::draw_text_box(Cpu* c, PpcFunc draw) {
    if (!marking()) return draw(c);  // no pane recognition or writes in a never-restyled session
    const int previous = hud_shoulder;
    const uint32_t previous_box = hud_box;
    const int previous_face = hud_face;
    const int previous_action = hud_action;
    hud_box = c->r[3];
    hud_shoulder = shoulder_box(hud_box);
    hud_face = face_box(hud_box);
    hud_action = -1;
    const auto name = pane_name(hud_box);
    constexpr std::string_view commands[] = {"T_CommandA_00", "T_CommandB_00", "T_CommandX_00", "T_CommandY_00"};
    constexpr std::string_view pictures[] = {"P_A_00", "P_B_00", "P_X_00", "P_Y_00"};
    const uint32_t parent = ld32(hud_box + 0x0C);
    for (int i = 0; i < 4; ++i)
        if (name == commands[i] && parent && (button_glyphs::face_picture(pane_name(parent)) == i || child_named(parent, pictures[i])))
            hud_action = i;
    prepare_text_box(hud_box, c);
    // HUD letters can be emitted through the font's direct glyph path, bypassing Print.
    // Supply the mapped letter to that rebuild too, then restore the authored Wii U text.
    const auto look = style();
    const uint32_t text = hud_face >= 0 ? ld32(hud_box + 0xA4) : 0;
    const uint16_t authored_letter = text ? ld16(text) : 0;
    const bool swap_letter = text && look.face == FaceStyle::kXbox && look.buttons[hud_face] != hud_face;
    if (swap_letter) st16(text, "ABXY"[look.buttons[hud_face]]);
    draw(c);
    if (swap_letter) st16(text, authored_letter);
    hud_shoulder = previous;
    hud_face = previous_face;
    hud_box = previous_box;
    hud_action = previous_action;
}

// Group-3 tags print U+E000..E003 for A/B/X/Y and U+E083..E086 for L/R/ZL/ZR. The
// existing RTL PrintGlyph hook still runs inside the original. Once by label has been used
// (or is live), mark even in by-position mode: switching the setting must also affect text
// built before that switch. Until then the flag byte is left untouched (stock path).
extern "C" void hook_0286E9F0(Cpu* c) {
    const int face = button_glyphs::face_character(c->r[4]);
    const int shoulder = button_glyphs::shoulder_character(c->r[4]);
    const uint32_t list = face >= 0 || shoulder >= 0 ? ld32(c->r[3] + 0x2C) : 0;
    const uint32_t count = list ? ld16(list + 4) : 0;
    const auto look = style();
    const int identity = face >= 0 ? face : hud_face;
    if (identity >= 0 && look.face == FaceStyle::kXbox) {
        constexpr uint32_t letters[] = {'A', 'B', 'X', 'Y'};
        c->r[4] = face >= 0 ? 0xE000 + look.buttons[identity] : letters[look.buttons[identity]];
    }
    f_0286E9F0_orig(c);
    if (list && ld16(list + 4) == count + 1 && marking()) {
        const uint32_t flags = list + 0xA4 + count * 0x30 + 0x2F;
        st8(flags, face >= 0 ? button_glyphs::mark(ld8(flags), face) : button_glyphs::mark_shoulder(ld8(flags), shoulder));
    }
}

// Style only marked icon quads when the game generates GPU vertices, then restore their authored
// colours. Record the rendered style in bit 3 of the same private marker, so DrawSelf can detect
// a stale vertex cache on the next setting change or save-state restore. Ordinary text and
// unrelated icons are not styled; the optional outline pass uses the game's outline colours.
extern "C" void hook_028F7C7C(Cpu* c) {
    const PromptStyle look = style();
    const bool active = look.active();
    const bool mark = marking();
    const bool shapes = look.face == FaceStyle::kPlayStation;
    const uint32_t list = c->r[4];
    std::vector<SavedWord> saved;
    const uint16_t original_count = ld16(list + 4);
    bool needs_shoulder = hud_shoulder >= 0 && original_count;
    bool needs_shape = shapes && hud_face >= 0 && original_count;
    for (uint32_t i = 0; i < original_count && !(needs_shoulder && (needs_shape || !shapes)); i++) {
        const uint8_t flags = ld8(list + 0xA4 + i*0x30 + 0x2F);
        needs_shoulder |= button_glyphs::marked_shoulder(flags) >= 0;
        needs_shape |= shapes && button_glyphs::marked_face(flags) >= 0;
    }
    // the item ring's names: X and Y letters (HUD face 2, 3) become A1 A2, the R plate's R becomes A3
    const bool names = item_names() && original_count && (hud_face >= 2 || hud_shoulder == 1) && shoulder_atlas(c);
    const bool shoulder_ready = (look.shoulders != ShoulderStyle::kOriginal || (names && hud_shoulder == 1)) &&
                                needs_shoulder && shoulder_atlas(c);
    const bool shape_ready = needs_shape && shoulder_atlas(c) && !(names && hud_face >= 2);
    if (hud_shoulder >= 0 && original_count) {
        const uint32_t quad = list + 0xA4;
        if (mark) st8(quad + 0x2F, button_glyphs::mark_shoulder(ld8(quad + 0x2F), hud_shoulder));
        if (shoulder_ready) {
            float left = (float)ldf32(quad + 8), right = left + (float)ldf32(quad);
            for (uint32_t i = 1; i < original_count; i++) {
                const uint32_t next = quad + i*0x30;
                left = std::min(left, (float)ldf32(next + 8));
                right = std::max(right, (float)(ldf32(next + 8) + ldf32(next)));
            }
            float width = right - left;
            const uint32_t parent = ld32(hud_box + 0x0C);
            if (hud_shoulder < 2 && parent) width = std::max(width, (float)ldf32(parent + 0x3C) * 0.7f);
            if (std::isfinite(width) && width > 0) {
                patch_float(saved, quad, width);
                patch_float(saved, quad + 8, (left + right - width) * 0.5f);
            }
            st16(list + 4, 1);  // one complete R1/R2/L1/L2 label, not the old separate Z and R/L
        }
    }
    if (hud_face >= 0 && original_count && mark && !look.positions)
        st8(list + 0xA4 + 0x2F, button_glyphs::mark(ld8(list + 0xA4 + 0x2F), hud_face));
    if (names && hud_face >= 2) {
        // "A1" / "A2" on the X / Y picture: a coverage glyph (the letter's own colours and dimming),
        // a little wider than the letter's height, centred on it
        const uint32_t quad = list + 0xA4;
        if (mark) st8(quad + 0x2F, button_glyphs::mark(ld8(quad + 0x2F), hud_face));
        const float w = (float)ldf32(quad), h = (float)ldf32(quad + 4);
        const float x = (float)ldf32(quad + 8), y = (float)ldf32(quad + 0x0C);
        const float side = h * 1.1f;
        if (std::isfinite(side) && side > 0 && std::isfinite(w) && std::isfinite(x) && std::isfinite(y)) {
            patch_float(saved, quad, side);
            patch_float(saved, quad + 4, side);
            patch_float(saved, quad + 8, x + (w - side) * 0.5f);
            patch_float(saved, quad + 0x0C, y + (h - side) * 0.5f);
        }
        st16(list + 4, 1);
    }
    if (hud_face >= 0 && original_count && shape_ready) {
        // The HUD letter becomes the shape, square and centred on the letter, on the game's own
        // button picture. Coloured: white vertex colours, the shape carries its own colour.
        // Without colours the shape is a coverage glyph: the game's own letter colours and
        // dimming apply to it unchanged.
        const uint32_t quad = list + 0xA4;
        if (mark) st8(quad + 0x2F, button_glyphs::mark(ld8(quad + 0x2F), hud_face));
        const float w = (float)ldf32(quad), h = (float)ldf32(quad + 4);
        const float x = (float)ldf32(quad + 8), y = (float)ldf32(quad + 0x0C);
        const float side = h * 0.8f;
        if (std::isfinite(side) && side > 0 && std::isfinite(w) && std::isfinite(x) && std::isfinite(y)) {
            patch_float(saved, quad, side);
            patch_float(saved, quad + 4, side);
            patch_float(saved, quad + 8, x + (w - side) * 0.5f);
            patch_float(saved, quad + 0x0C, y + (h - side) * 0.5f);
        }
        if (look.colours)
            for (uint32_t corner = 0x10; corner <= 0x14; corner += 4) patch(saved, quad + corner, ld32(quad + corner) | 0xFFFFFF00u);
        st16(list + 4, 1);
    }
    for (uint32_t i = 0, count = ld16(list + 4); i < count; i++) {
        const uint32_t quad = list + 0xA4 + i * 0x30;
        const int face = button_glyphs::marked_face(ld8(quad + 0x2F));
        const int shoulder = button_glyphs::marked_shoulder(ld8(quad + 0x2F));
        // Action words belong to the same physical button as their neighbouring icon.
        // Keep By label's authored words byte-identical; new position/custom styles tint ink.
        if (hud_action >= 0 && !look.positions && look.face != FaceStyle::kOriginal && look.colours) {
            const int button = look.buttons[hud_action];
            const uint32_t rgb = look.face == FaceStyle::kPlayStation ? button_glyphs::kShapeColours[button]
                                                                    : button_glyphs::colour(button);
            for (uint32_t offset : {0x10u, 0x14u}) {
                const uint32_t ink = ld32(quad + offset);
                uint32_t tinted = ink & 255;
                for (int shift = 24; shift > 0; shift -= 8)
                    tinted |= (((ink >> shift) & 255) * ((rgb >> shift) & 255) / 255) << shift;
                patch(saved, quad + offset, tinted);
            }
        }
        if (face < 0 && shoulder < 0) continue;
        if (mark) st8(quad + 0x2F, button_glyphs::rendered(ld8(quad + 0x2F), active));
        if (face >= 0 && hud_face < 0) {  // dialog/icon-font glyphs; HUD letters keep their ink
            if (look.face == FaceStyle::kXbox && look.colours) {
                tint_word(saved, quad + 0x10, look.buttons[face]);
                tint_word(saved, quad + 0x14, look.buttons[face]);
            } else if (shape_ready) {
                atlas_quad(saved, quad, button_glyphs::shape_row(look.buttons[face], look.colours), true, c);
            }
        }
        if (face >= 0 && hud_face >= 0 && shape_ready)
            atlas_quad(saved, quad, button_glyphs::shape_row(look.buttons[face], look.colours), false, c, !look.colours);
        if (names && face >= 2 && hud_face >= 2)
            atlas_quad(saved, quad, button_glyphs::item_name_row(hud_face - 2), false, c, true);
        // Dialogs: the framed label is an RGBA picture (#141). HUD shoulder plates: the bare
        // caption is a coverage glyph like the plate's own "R"/"ZR", so the game draws it in the
        // plate's dark letter colour and dims it with the button (#143); as a picture it was
        // white on the white plate.
        if (shoulder_ready && shoulder >= 0)
            atlas_quad(saved, quad, names && hud_shoulder == 1 ? button_glyphs::item_name_row(2) : shoulder_row(look.shoulders, shoulder),
                       hud_shoulder < 0, c, hud_shoulder >= 0);
    }
    f_028F7C7C_orig(c);
    restore(saved);
    if (ld16(list + 4) != original_count) st16(list + 4, original_count);  // only if the label merge changed it
}

// CommandGuide, the floating CommandA prompt and IconSlideArea use the same ABXY background
// texture, with separate text panes for the letters. Style the picture's four vertex colours
// instead of swapping that shared texture (which cannot distinguish A from B/X/Y).
extern "C" void hook_02874D54(Cpu* c) {
    const uint32_t picture = c->r[3];
    const int face = button_glyphs::face_picture(pane_name(picture));
    const PromptStyle look = style();
    if (face < 0 || look.face != FaceStyle::kXbox || !look.colours) return f_02874D54_orig(c);
    std::array<uint32_t, 4> saved;
    for (uint32_t i = 0; i < saved.size(); i++) {
        const uint32_t address = picture + 0xA8 + 4 * i;
        saved[i] = ld32(address);
        st32(address, button_glyphs::tint(saved[i], look.buttons[face]));
    }
    f_02874D54_orig(c);
    for (uint32_t i = 0; i < saved.size(); i++) st32(picture + 0xA8 + 4 * i, saved[i]);
}

namespace button_glyphs {
// A loaded save state brings back text boxes cached at addresses that may already be in the
// one-shot rebuild set: forget it, so each is rebuilt (and marked) again when first drawn.
void ss_reset() { forget_rebuilt(); }
}  // namespace button_glyphs
