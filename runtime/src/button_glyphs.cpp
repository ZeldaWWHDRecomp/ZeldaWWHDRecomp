// Draw-time face styling and shoulder labels using the game's HUD font, through common game/GX2
// path. See docs/button-glyphs.md for hook points and layout semantics.
#include "button_glyphs.h"
#include "button_glyph_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "input_map.h"
#include "runtime.h"
#include "guest_addr.h"

extern "C" {
void f_0286E9F0_orig(Cpu* c);  // CharWriter::Print(writer, character)
void f_028F7C7C_orig(Cpu* c);  // build vertices from a TextBox's cached glyph list
void f_02874D54_orig(Cpu* c);  // lyt::Picture::DrawSelf
void imp_gx2_GX2InitTextureRegs(Cpu* c);
}

namespace {

bool xbox_style() {
    // Per guest thread: layouts can be recorded on multiple cores. Each set_current invalidates
    // the cached choice, including transitions through custom bindings. No SDL/AppKit dependency.
    thread_local uint32_t generation = ~0u;
    thread_local bool enabled = false;
    const uint32_t now = input_map::generation();
    if (now != generation) {
        enabled = input_map::face_layout(input_map::current()) == input_map::FaceLayout::kLabels;
        generation = now;
    }
    return enabled;
}

struct SavedWord {
    uint32_t address;
    uint32_t value;
};

thread_local int hud_shoulder = -1;
thread_local uint32_t hud_box = 0;
std::string_view pane_name(uint32_t pane);

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
    thread_local uint32_t scratch = mem::host_alloc(4*0x18, 16);
    std::array<uint32_t,4> records;
    constexpr uint32_t characters[] = {'L','R','1','2'};
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
    const auto pixels = button_glyphs::shoulder_font_atlas(records);
    std::lock_guard lock(mutex);
    for (const auto& atlas : atlases) if (atlas.font == font && atlas.resource == resource) return atlas.info;
    // Loaded font resources are immutable during a session (content mods activate at boot).
    // Cache an unsupported resource too; don't query/decode it again on every HUD frame.
    if (pixels.empty()) { atlases.push_back({font,resource,0}); return 0; }
    // Font-sheet info has a 16-byte header followed by GX2Texture (+0x10), as bound by
    // the game's text draw at 028F86C8. Host-only storage survives guest state restores.
    const uint32_t info = [&] {
        const uint32_t object = mem::host_alloc(0xAC, 256);
        const uint32_t image = mem::host_alloc(128 * 256 * 4, 256);
        std::memset(mem::ptr(object), 0, 0xAC);
        std::memcpy(mem::ptr(object), mem::ptr(ld32(records[0] + 0x14)), 16);
        st8(object + 0x0E, 1);  // colour sheet: preserve RGBA, rather than treating it as an alpha font
        std::memcpy(mem::ptr(image), pixels.data(), pixels.size());
        const uint32_t texture = object + 0x10;
        st32(texture, 5); st32(texture + 4, 128); st32(texture + 8, 256); st32(texture + 12, 1);  // one-layer font array
        st32(texture + 0x10, 1); st32(texture + 0x14, 0x1A);  // one mip, RGBA8
        st32(texture + 0x1C, 1); st32(texture + 0x20, 128*256*4); st32(texture + 0x24, image);
        st32(texture + 0x30, 1); st32(texture + 0x38, 256); st32(texture + 0x3C, 128);  // linear-aligned
        st32(texture + 0x78, 1); st32(texture + 0x80, 1); st32(texture + 0x84, 0x00010203);
        Cpu init{}; init.r[3] = texture;
        imp_gx2_GX2InitTextureRegs(&init);
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

void shoulder_quad(std::vector<SavedWord>& saved, uint32_t quad, int shoulder, bool frame, Cpu* cpu) {
    const uint32_t atlas = shoulder_atlas(cpu);
    if (!atlas) return;
    patch(saved, quad + 0x28, atlas);
    patch_float(saved, quad + 0x18, frame ? 0.0f : 0.5f);
    patch_float(saved, quad + 0x1C, (3-shoulder) * 0.25f);
    patch_float(saved, quad + 0x20, frame ? 0.5f : 1.0f);
    patch_float(saved, quad + 0x24, (4-shoulder) * 0.25f);
    // The composed array has one layer, regardless of the source glyph's page. Restore its
    // original page and colour-font flag with the rest of the authored quad afterwards.
    patch(saved, quad + 0x2C, (ld32(quad + 0x2C) & ~0xFF00u) | 1);
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

void style(std::vector<SavedWord>& saved, uint32_t address, int face) {
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
    c->r[5] = 1;
    if (!xbox_style()) return calculate(c);
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
    if (!list || !ld8(list + 6)) return;
    const bool xbox = xbox_style();
    const int hud = shoulder_box(text_box);
    if (hud >= 0 && ld16(list + 4)) {
        const uint32_t flag = list + 0xA4 + 0x2F;
        if (marked_shoulder(ld8(flag)) < 0) st8(flag, mark_shoulder(ld8(flag), hud));
    }
    for (uint32_t i = 0, count = ld16(list + 4); i < count; i++) {
        const uint8_t flags = ld8(list + 0xA4 + i * 0x30 + 0x2F);
        const int shoulder = marked_shoulder(flags);
        if ((marked_face(flags) >= 0 || shoulder >= 0) && styled(flags) != xbox) {
            st8(list + 6, 0);  // the original DrawSelf rebuilds the text and its vertex buffer
            return;
        }
        if (shoulder >= 0 && xbox) {
            // Restoring a state in a fresh process can bring cached batch pointers from the
            // previous host allocation. Rebuild unless it binds this process's live atlas.
            const uint32_t atlas = shoulder_atlas(cpu);
            bool bound = false;
            for (uint32_t batch = 0, n = ld32(list + 0xA0); batch < n && batch < 8; batch++)
                bound |= ld32(list + 0x20 + batch*0x10) == atlas;
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
    const int previous = hud_shoulder;
    const uint32_t previous_box = hud_box;
    hud_box = c->r[3];
    hud_shoulder = shoulder_box(hud_box);
    prepare_text_box(hud_box, c);
    draw(c);
    hud_shoulder = previous;
    hud_box = previous_box;
}

// Group-3 tags print U+E000..E003 for A/B/X/Y and U+E083..E086 for L/R/ZL/ZR. The
// existing RTL PrintGlyph hook still runs inside the original. Mark even in by-position mode:
// switching the setting must also affect text whose glyph list was built before that switch.
extern "C" void hook_0286E9F0(Cpu* c) {
    const int face = button_glyphs::face_character(c->r[4]);
    const int shoulder = button_glyphs::shoulder_character(c->r[4]);
    const uint32_t list = face >= 0 || shoulder >= 0 ? ld32(c->r[3] + 0x2C) : 0;
    const uint32_t count = list ? ld16(list + 4) : 0;
    f_0286E9F0_orig(c);
    if (list && ld16(list + 4) == count + 1) {
        const uint32_t flags = list + 0xA4 + count * 0x30 + 0x2F;
        st8(flags, face >= 0 ? button_glyphs::mark(ld8(flags), face) : button_glyphs::mark_shoulder(ld8(flags), shoulder));
    }
}

// Style only marked icon quads when the game generates GPU vertices, then restore their authored
// colours. Record the rendered style in bit 3 of the same private marker, so DrawSelf can detect
// a stale vertex cache on the next setting change or save-state restore. Ordinary text and
// unrelated icons are not styled; the optional outline pass uses the game's outline colours.
extern "C" void hook_028F7C7C(Cpu* c) {
    const bool xbox = xbox_style();
    const uint32_t list = c->r[4];
    std::vector<SavedWord> saved;
    const uint16_t original_count = ld16(list + 4);
    bool needs_shoulder = hud_shoulder >= 0 && original_count;
    for (uint32_t i = 0; !needs_shoulder && i < original_count; i++)
        needs_shoulder = button_glyphs::marked_shoulder(ld8(list + 0xA4 + i*0x30 + 0x2F)) >= 0;
    const bool shoulder_ready = xbox && needs_shoulder && shoulder_atlas(c);
    if (hud_shoulder >= 0 && original_count) {
        const uint32_t quad = list + 0xA4;
        st8(quad + 0x2F, button_glyphs::mark_shoulder(ld8(quad + 0x2F), hud_shoulder));
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
    for (uint32_t i = 0, count = ld16(list + 4); i < count; i++) {
        const uint32_t quad = list + 0xA4 + i * 0x30;
        const int face = button_glyphs::marked_face(ld8(quad + 0x2F));
        const int shoulder = button_glyphs::marked_shoulder(ld8(quad + 0x2F));
        if (face < 0 && shoulder < 0) continue;
        st8(quad + 0x2F, button_glyphs::rendered(ld8(quad + 0x2F), xbox));
        if (xbox && face >= 0) {
            style(saved, quad + 0x10, face);
            style(saved, quad + 0x14, face);
        }
        if (shoulder_ready && shoulder >= 0) shoulder_quad(saved, quad, shoulder, hud_shoulder < 0, c);
    }
    f_028F7C7C_orig(c);
    restore(saved);
    st16(list + 4, original_count);
}

// CommandGuide, the floating CommandA prompt and IconSlideArea use the same ABXY background
// texture, with separate text panes for the letters. Style the picture's four vertex colours
// instead of swapping that shared texture (which cannot distinguish A from B/X/Y).
extern "C" void hook_02874D54(Cpu* c) {
    const uint32_t picture = c->r[3];
    const int face = button_glyphs::face_picture(pane_name(picture));
    if (face < 0 || !xbox_style()) return f_02874D54_orig(c);
    std::array<uint32_t, 4> saved;
    for (uint32_t i = 0; i < saved.size(); i++) {
        const uint32_t address = picture + 0xA8 + 4 * i;
        saved[i] = ld32(address);
        st32(address, button_glyphs::tint(saved[i], face));
    }
    f_02874D54_orig(c);
    for (uint32_t i = 0; i < saved.size(); i++) st32(picture + 0xA8 + 4 * i, saved[i]);
}
