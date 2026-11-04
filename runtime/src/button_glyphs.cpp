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
#include <unordered_map>
#include <vector>

#include "input_map.h"
#include "runtime.h"
#include "guest_addr.h"
#include "mods/mods.h"
#include "shared_patch.h"

extern "C" {
void f_0286E9F0_orig(Cpu* c);  // CharWriter::Print(writer, character)
void f_028F7C7C_orig(Cpu* c);  // build vertices from a TextBox's cached glyph list
void f_02874D54_orig(Cpu* c);  // lyt::Picture::DrawSelf
void f_026F8CD0_orig(Cpu* c);  // hdParts_c::calcLayout
void imp_gx2_GX2InitTextureRegs(Cpu* c);
void imp_gx2_GX2Invalidate(Cpu* c);
void imp_gx2_GX2DrawDone(Cpu* c);
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

PromptStyle style();
void forget_rebuilt();
// The item ring's button names (mods/item_ring.cpp): A1 A2 A3 on the HUD's X, Y and R now. A change
// rebuilds every cached text once; turning them on counts as a restyled look for the session gate.
bool item_names() {
    static std::atomic<int> last{-1};
    const bool on = style().face != FaceStyle::kKeyboard && mods::item_names_on_hud();
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
    for (int i = 0; i < 4; ++i) buttons |= uint32_t(s.buttons[i]) << (10 + 2*i) | uint32_t(s.spots[i]) << (18 + 2*i);
    return buttons | 0x100u | (uint32_t)s.face | (s.colours ? 8u : 0u) | (uint32_t)s.shoulders << 4 |
           (s.positions ? 0x40u : 0u) | (s.text_colours ? 0x80u : 0u);
}

PromptStyle style() {
    // Per guest thread: layouts can be recorded on multiple cores. Each set_current (and a new pad
    // family or controller in use) invalidates the cached choice, including transitions through
    // custom bindings. The bindings are the controller in use's own when it has them
    // (input_map::active_view). No SDL/AppKit dependency, and nothing beyond a generation compare
    // per call.
    thread_local uint32_t generation = ~0u;
    thread_local PromptStyle current;
    const uint32_t now = input_map::generation();
    if (now != generation) {
        current = input_map::active_prompt_style();
        if (current.active()) labels_used.store(true, std::memory_order_relaxed);
        const uint32_t sig = signature(current);
        if (last_signature.exchange(sig, std::memory_order_relaxed) != sig && labels_used.load(std::memory_order_relaxed))
            forget_rebuilt();
        if (current.face == FaceStyle::kKeyboard) forget_rebuilt();
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
// A font can need more than the native eight vertex batches after substitution. Keep
// that one text's authored fallback until its quads, batches or prompt generation change.
// Exact snapshots also reject a stale list recycled for another text or restored state.
struct StockFallback {
    std::vector<uint8_t> quads;
    std::array<uint8_t, 0x84> batches;
};
std::mutex fallback_mutex;
std::unordered_map<uint32_t, StockFallback> stock_fallbacks;
bool stock_fallback(uint32_t list, bool remember = false) {
    const size_t bytes = size_t(ld16(list+4))*0x30;
    std::lock_guard lock(fallback_mutex);
    if (remember) {
        auto& entry=stock_fallbacks[list];
        entry.quads.assign(mem::ptr(list+0xA4),mem::ptr(list+0xA4)+bytes);
        std::memcpy(entry.batches.data(),mem::ptr(list+0x20),entry.batches.size());
        return true;
    }
    const auto it=stock_fallbacks.find(list);
    return it!=stock_fallbacks.end() && it->second.quads.size()==bytes &&
        !std::memcmp(it->second.quads.data(),mem::ptr(list+0xA4),bytes) &&
        !std::memcmp(it->second.batches.data(),mem::ptr(list+0x20),it->second.batches.size());
}

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
    std::lock_guard fallback_lock(fallback_mutex);
    stock_fallbacks.clear();
}

// Draw-time changes to game memory go through shared_patch: two game threads can build and draw
// the same text at once (#326).
using shared_patch::Held;

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


void patch(std::vector<Held>& saved, uint32_t address, uint32_t value) {
    shared_patch::hold_value(saved, address, 4, value);
}

void patch_float(std::vector<Held>& saved, uint32_t address, float value) {
    shared_patch::hold_float(saved, address, value);
}

// The atlas sheet that draws through the text box's colours (coverage = the cell's alpha).
constexpr uint32_t coverage_sheet(uint32_t atlas) { return atlas + 0x100; }

// Point a glyph quad at a cell of the composed atlas (a shoulder label or a PlayStation shape).
// `coverage`: keep the quad a coverage glyph on the coverage sheet, so the game colours (and
// dims) it exactly like the letter it replaces; otherwise an RGBA picture (#141).
void atlas_quad(std::vector<Held>& saved, uint32_t quad, int row, bool frame, Cpu* cpu, bool coverage = false) {
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
    shared_patch::hold(saved, quad + 0x2C, 4, [coverage](uint32_t word) { return (word & ~0xFF01u) | (coverage ? 1u : 0u); });
}

// A group uses four directional bindings in a cross, in the same order as the
// Mapping action enum: up, down, left, right. Values below kActionCount are single keys.
constexpr int key_group(int first) { return input_map::kActionCount+first; }
int input_glyph_action(int index) {
    using namespace input_map;
    constexpr int actions[] = {key_group(kDUp),kPlus,kMinus,kDUp,kDDown,kDLeft,kDRight,
        // E080 is an unlabelled stick, not evidence of a left-stick binding.
        // Keep its native artwork until its context identifies which stick it means.
        -1,key_group(kLUp),key_group(kRUp),kLUp,kLDown,kLLeft,kLRight,
        kRUp,kRDown,kRLeft,kRRight,kA};
    return index>=0 && index<int(std::size(actions)) ? actions[index] : -1;
}

// Cache the controls snapshot once per binding generation, only on the keyboard path.
const input_map::Mapping& keyboard_mapping() {
    thread_local uint32_t generation=~0u;
    thread_local input_map::Mapping mapping;
    const uint32_t now=input_map::generation();
    if (now!=generation) { mapping=input_map::current(); generation=now; }
    return mapping;
}

// One immutable key atlas and three reusable directional diagrams per native font.
// Updating a diagram uses the same GX2 texture invalidation as CPU-written game textures.
uint32_t keycap_sheet(Cpu* cpu, int action) {
    const auto& mapping = keyboard_mapping();
    const bool group = action >= input_map::kActionCount;
    const int first = action-input_map::kActionCount;
    const std::string caption = group ? "Group:"+std::to_string(first) : "@Keys";
    std::string bindings;
    if (group) for (int i=0;i<4;++i) bindings += input_map::prompt_key_label(mapping,first+i)+"/";
    const uint32_t manager = ld32(GD(0x101F4A50));
    const uint32_t font = manager ? ld32(manager + 0x10) : 0;
    if (!font || !cpu || !ld32(font + 8) || !ld32(font + 0x0C) || !ld32(font + 0x44)) return 0;
    const uint32_t resource = ld32(font + 8);
    struct Entry { uint32_t font, resource, sheet; std::string caption, bindings; };
    static std::mutex mutex;
    static std::vector<Entry> cache;
    {
        std::lock_guard lock(mutex);
        for (const auto& e : cache) if (e.font == font && e.resource == resource && e.caption == caption && e.bindings == bindings) return e.sheet;
    }
    std::vector<uint8_t> pixels;
    uint32_t source_sheet = 0;
    const uint32_t width = group ? 64 : 1024, height = group ? 64 : 1088;
    if (group) {
        pixels.resize(64*64*4);
        constexpr int positions[4][2] = {{21,0},{21,42},{0,21},{42,21}};
        for (int direction=0;direction<4;++direction) {
            const uint32_t sheet = keycap_sheet(cpu,first+direction);
            if (!sheet) return 0;
            source_sheet = sheet;
            const auto* source = mem::ptr(ld32(sheet+0x34));
            const int code = input_map::prompt_key_code(mapping,first+direction);
            const int index = code < 0 ? input_map::kInputCount : code;
            const int cell_x = (index%16)*64, cell_y=(index/16)*64;
            for (int y=0;y<22;++y) for (int x=0;x<22;++x) {
                const int sx=std::min(63,(x*64+32)/22), sy=std::min(63,(y*64+32)/22);
                const int dest=((positions[direction][1]+y)*64+positions[direction][0]+x)*4;
                std::copy_n(source+((cell_y+sy)*1024+cell_x+sx)*4,4,pixels.data()+dest);
            }
        }
    } else {
        const uint32_t table = ld32(font + 4), get_glyph = table ? ld32(table + 0x8C) : 0;
        if (!get_glyph) return 0;
        thread_local uint32_t scratch = mem::host_alloc(128*0x18,16);
        std::array<uint32_t,128> glyphs{};
        pixels.resize(width*height*4);
        unsigned missing=0;
        std::string first_missing;
        auto labels = mapping;
        for (int index=0;index<=input_map::kInputCount;++index) {
            labels.keys[input_map::kA] = {index<input_map::kInputCount ? index : input_map::kNoKey,input_map::kNoKey};
            const auto label = input_map::prompt_key_label(labels,input_map::kA);
            std::vector<uint32_t> records;
            for (unsigned char ch : label) {
                if (ch>=128) return 0;
                if (!glyphs[ch]) {
                    const uint32_t record=scratch+ch*0x18;
                    std::memset(mem::ptr(record),0,0x18);
                    Cpu get=*cpu;
                    get.r[3]=font; get.r[4]=record; get.r[5]=ch; get.pc=get.ctr=get_glyph;
                    ppc_dispatch(&get);
                    glyphs[ch]=record;
                }
                records.push_back(glyphs[ch]);
            }
            auto cell = button_glyphs::keycap_pixels(records);
            if (!cell.empty() && !source_sheet) source_sheet=ld32(records[0]+0x14);
            if (cell.empty()) {
                if (missing++ == 0) first_missing=label;
                // A missing caption must not disable unrelated bindings. A native question
                // mark is preferable; an explicit crossed keycap also works if '?' is absent.
                const uint32_t record=scratch+'?'*0x18;
                if (!glyphs['?']) {
                    std::memset(mem::ptr(record),0,0x18);
                    Cpu get=*cpu; get.r[3]=font; get.r[4]=record; get.r[5]='?';
                    get.pc=get.ctr=get_glyph; ppc_dispatch(&get); glyphs['?']=record;
                }
                cell=button_glyphs::keycap_pixels(std::span(&record,1));
                if (!cell.empty() && !source_sheet) source_sheet=ld32(record+0x14);
                if (cell.empty()) {
                    cell.resize(64*64*4);
                    for (int y=0;y<64;++y) for (int x=0;x<64;++x) {
                        const uint32_t bg=button_glyphs::shoulder_background(x,y);
                        const bool cross=x>=22 && x<=42 && y>=22 && y<=42 &&
                            (std::abs(x-y)<=1 || std::abs(x+y-64)<=1);
                        const size_t p=(y*64+x)*4;
                        cell[p]=cell[p+1]=cell[p+2]=cross ? 36 : bg>>24;
                        cell[p+3]=cross ? 255 : bg&255;
                    }
                }
            }
            for (int y=0;y<64;++y)
                std::copy_n(cell.data()+y*64*4,64*4,pixels.data()+(((index/16)*64+y)*width+(index%16)*64)*4);
        }
        if (missing) LOG("[button-prompts] font %08X: %u key captions unavailable (first: %s); using visible fallback keycaps", font, missing, first_missing.c_str());
    }
    std::lock_guard lock(mutex);
    for (const auto& e : cache) if (e.font == font && e.resource == resource && e.caption == caption && e.bindings == bindings) return e.sheet;
    if (group) for (auto& e : cache) if (e.font==font && e.resource==resource && e.caption==caption) {
        // Drain existing consumers before reusing CPU texture storage. This happens
        // only on a directional rebind, never on ordinary pane draws.
        Cpu done=*cpu; imp_gx2_GX2DrawDone(&done);
        const uint32_t image=ld32(e.sheet+0x34);
        std::memcpy(mem::ptr(image),pixels.data(),pixels.size());
        Cpu invalidate=*cpu; invalidate.r[3]=2; invalidate.r[4]=image; invalidate.r[5]=pixels.size();
        imp_gx2_GX2Invalidate(&invalidate);
        e.bindings=bindings;
        return e.sheet;
    }
    if (!source_sheet) {
        LOG("[button-prompts] font %08X has no readable glyph sheet; retaining native prompts",font);
        cache.push_back({font,resource,0,caption,bindings});
        return 0;
    }
    const uint32_t object = mem::host_alloc(0x100,256), image = mem::host_alloc(width*height*4,256);
    std::memset(mem::ptr(object),0,0x100);
    std::memcpy(mem::ptr(object),mem::ptr(source_sheet),16);
    st8(object+0x0E,0);
    std::memcpy(mem::ptr(image),pixels.data(),pixels.size());
    const uint32_t texture=object+0x10;
    st32(texture,5); st32(texture+4,width); st32(texture+8,height); st32(texture+12,1);
    st32(texture+16,1); st32(texture+20,0x1A); st32(texture+28,1);
    st32(texture+32,width*height*4); st32(texture+36,image); st32(texture+48,1);
    st32(texture+56,height); st32(texture+60,width);
    st32(texture+0x78,1); st32(texture+0x80,1); st32(texture+0x84,0x00010203);
    Cpu init{}; init.r[3]=texture; imp_gx2_GX2InitTextureRegs(&init);
    cache.push_back({font,resource,object,caption,bindings});
    return object;
}

void keycap_quad(std::vector<Held>& saved, uint32_t quad, uint32_t sheet, int action) {
    patch(saved,quad+0x28,sheet);
    const int code = input_map::prompt_key_code(keyboard_mapping(),action);
    const int index = code < 0 ? input_map::kInputCount : code;
    const bool group = action >= input_map::kActionCount;
    patch_float(saved,quad+0x18,group ? 0.f : (index%16)/16.f);
    patch_float(saved,quad+0x1C,group ? 0.f : (index/16)/17.f);
    patch_float(saved,quad+0x20,group ? 1.f : (index%16+1)/16.f);
    patch_float(saved,quad+0x24,group ? 1.f : (index/16+1)/17.f);
    shared_patch::hold(saved,quad+0x2C,4,[](uint32_t word) { return word & ~0xFF01u; });
    for (uint32_t corner : {0x10u,0x14u})
        shared_patch::hold(saved,quad+corner,4,[](uint32_t rgba) { return rgba | 0xFFFFFF00u; });
}

int shoulder_row(ShoulderStyle names, int shoulder) {
    return names == ShoulderStyle::kXbox ? button_glyphs::xbox_row(shoulder)
         : names == ShoulderStyle::kGameCube ? button_glyphs::gamecube_shoulder_row(shoulder)
         : button_glyphs::numbered_row(shoulder);
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
    // The authored letter, also while another thread draws it swapped (draw_text_box).
    return text && ld16(box + 0xCE) == 1 && shared_patch::authored(text, 2) == letters[face] ? face : -1;
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

void tint_word(std::vector<Held>& saved, uint32_t address, int face) {
    shared_patch::hold(saved, address, 4, [face](uint32_t rgba) { return button_glyphs::tint(rgba, face); });
}

void restore(std::vector<Held>& saved) { shared_patch::release(saved); }

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
// global matrices (which may already contain last frame's Xbox placement), and never another
// thread's moved group. A/X/Y pictures are children of different containers. Include those
// containers' animated scale and Z rotation.
Point button_centre(uint32_t group, uint32_t picture) {
    Point p{shared_patch::authored_float(group + 0x1C), shared_patch::authored_float(group + 0x20)};
    if (picture != group) {
        const float x = shared_patch::authored_float(picture + 0x1C) * (float)ldf32(group + 0x34);
        const float y = shared_patch::authored_float(picture + 0x20) * (float)ldf32(group + 0x38);
        const float angle = (float)ldf32(group + 0x30) * 0.01745329252f;
        p.x += std::cos(angle) * x - std::sin(angle) * y;
        p.y += std::sin(angle) * x + std::cos(angle) * y;
    }
    return p;
}

struct GuideLayout {
    bool known = false;
    bool menu_pair = false;
    int single_face = -1;
};
thread_local GuideLayout guide_layout;

}  // namespace

void button_glyphs::calculate_parts(Cpu* c, PpcFunc calculate) {
    const auto previous = guide_layout;
    const uint32_t parts = c->r[3];
    const uint32_t vtable = ld32(parts + 4);
    guide_layout = {};
    guide_layout.known = vtable == GD(0x100EE5F8) || vtable == GD(0x100EE378);
    if (guide_layout.known) {
        // Retained buttons describe the outgoing layout until its animation finishes.
        const uint8_t buttons = ld8(parts + 0xDD);
        guide_layout.menu_pair = vtable == GD(0x100EE5F8) && (buttons & 0x1F) == 3;
        for (int i = 0; i < 4; ++i)
            if (buttons == (1u << i)) guide_layout.single_face = i;
    }
    calculate(c);
    guide_layout = previous;
}

extern "C" void hook_026F8CD0(Cpu* c) {
    button_glyphs::calculate_parts(c, f_026F8CD0_orig);
}

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
    // Only once a restyled look has been used: until then the stock path stays untouched.
    if (marking()) c->r[5] = 1;
    const auto look = style();
    // A lone face prompt stays where the game draws it, whatever its binding.
    const int spot = guide_layout.single_face == face ? face : look.spots[face];
    const bool menu_pair = face < 2 && look.face != FaceStyle::kOriginal && guide_layout.menu_pair;
    const Point from = button_centre(groups[face], pictures[face]);
    Point to = button_centre(groups[spot], pictures[spot]);
    if (look.face == FaceStyle::kGameCube && guide_layout.single_face < 0 && !guide_layout.menu_pair) {
        const Point a = button_centre(groups[0], pictures[0]);
        const Point b = button_centre(groups[1], pictures[1]);
        const Point x = button_centre(groups[2], pictures[2]);
        const Point y = button_centre(groups[3], pictures[3]);
        // Keep A at the centre-right anchor; place B down-left, X to its right and Y above.
        const auto gc = button_glyphs::gamecube_cluster({a.x,a.y},{b.x,b.y},{x.x,x.y},{y.x,y.y});
        to = {gc[face].x, gc[face].y};
    } else if (spot == face && !menu_pair) return calculate(c);
    if (menu_pair) {
        const int first = look.spots[0], second = look.spots[1];
        const Point right = button_centre(groups[0], pictures[0]);
        const Point bottom = button_centre(groups[1], pictures[1]);
        // Fit OK/Back into the original menu area: right-align, then remove empty rows.
        if (first != 0 && second != 0) to.x += right.x - bottom.x;
        if (first != 1 && second != 1) to.y = spot == 2 ? right.y : bottom.y;
        // The top/bottom pair keeps one-third of the empty row for label clearance.
        else if ((first == 2 || second == 2) && spot == 2) to.y = right.y + (to.y - right.y) / 3.0f;
    }
    const float dx = to.x - from.x, dy = to.y - from.y;
    if (!std::isfinite(dx) || !std::isfinite(dy)) return calculate(c);
    // Animation/gameplay still own the local positions. Only the resulting global matrices
    // retain the rearrangement; the move is held (shared_patch: two threads can calculate the
    // same cluster, #326) only while the pane's tree is calculated.
    std::vector<shared_patch::Held> held;
    shared_patch::hold_offset(held, pane + 0x1C, dx);
    shared_patch::hold_offset(held, pane + 0x20, dy);
    calculate(c);
    shared_patch::release(held);
}

namespace {
// The CommandGuide cluster being drawn with its action words held back (draw_pane), and those words'
// panes with the Pane::Draw they came through, to draw once the whole cluster is down.
thread_local uint32_t deferring_cluster = 0;
thread_local std::vector<std::pair<uint32_t, PpcFunc>> deferred_words;

bool command_word(uint32_t box) {
    const auto name = pane_name(box);
    return name == "T_CommandA_00" || name == "T_CommandB_00" || name == "T_CommandX_00" || name == "T_CommandY_00";
}

int shoulder_action(uint32_t box) {
    constexpr std::string_view words[]={"T_CommandL_00","T_CommandR_00","T_CommandZL_00","T_CommandZR_00"};
    constexpr std::string_view plates[]={"P_L_00","P_R_00","P_ZL_00","P_ZR_00"};
    const auto name=pane_name(box);
    if (!name.starts_with("T_Command")) return -1;
    const uint32_t parent=ld32(box+0x0C);
    for (int i=0;parent && i<4;++i)
        if (name==words[i] && pane_name(parent)==plates[i]) return i;
    return -1;
}

int word_action(uint32_t box) {
    if (const int shoulder=shoulder_action(box); shoulder>=0) return 4+shoulder;
    const auto name=pane_name(box);
    if (!name.starts_with("T_Command")) return -1;
    constexpr std::string_view words[]={"T_CommandA_00","T_CommandB_00","T_CommandX_00","T_CommandY_00"};
    constexpr std::string_view pictures[]={"P_A_00","P_B_00","P_X_00","P_Y_00"};
    const uint32_t parent=ld32(box+0x0C);
    for (int i=0;parent && i<4;++i)
        if (name==words[i] && (button_glyphs::face_picture(pane_name(parent))==i || child_named(parent,pictures[i]))) return i;
    return -1;
}

void draw_gamecube_word(Cpu* c,PpcFunc draw) {
    const auto look=style();
    if (look.face!=FaceStyle::kGameCube || !look.text_colours) return draw(c);
    const uint32_t box=c->r[3];
    const int action=word_action(box);
    if (action<0 || (action>=4 && look.shoulders!=ShoulderStyle::kGameCube)) return draw(c);
    // Actual text drawing (028787FC) reads this live black/white ramp, independently
    // of cached vertices. The native blue ramp would otherwise tint every outline blue.
    const uint32_t colors=ld32(box+0xF8);
    const bool valid=(colors>=mem::kMem2Start && colors<=mem::kMem2End-8) ||
                     (colors>=mem::kMem1 && colors<=mem::kMem1+mem::kMem1Size-8);
    if (!valid) return draw(c);
    std::vector<Held> held;
    for (uint32_t offset : {0u,4u})
        shared_patch::hold(held,colors+offset,4,[](uint32_t rgba) {
            const uint32_t shade=(((rgba>>24)&255)*54+((rgba>>16)&255)*183+((rgba>>8)&255)*19)>>8;
            return shade*0x01010100u | (rgba&255);
        });
    draw(c);
    shared_patch::release(held);
}

bool command_cluster(uint32_t pane) {
    if (pane_name(pane)!="N_All_00") return false;
    for (const char* group : {"W_SetSeatA_00","P_B_00","N_X_00","N_Y_00"})
        if (!child_named(pane,group)) return false;
    return true;
}
bool in_command_cluster(uint32_t pane) {
    for (unsigned depth=0;pane && depth<16;++depth,pane=ld32(pane+0x0C))
        if (command_cluster(pane)) return true;
    return false;
}

bool inside(uint32_t pane, uint32_t ancestor) {
    for (unsigned depth = 0; pane && depth < 16; depth++, pane = ld32(pane + 0x0C))
        if (pane == ancestor) return true;
    return false;
}
}  // namespace

// Pane::Draw draws a pane, then its children through Pane::Draw again, so the cluster's groups come
// in the tree's child order (Y's group first, then R, B, A, X), each with its action word inside. The
// authored layout keeps every word clear of the other buttons, but moved groups can reach into a
// neighbour, which then covered the word when it came later. With a restyled look, the words of a
// complete cluster are held back and drawn after the rest of it, with the same DrawInfo.
void button_glyphs::draw_pane(Cpu* c, PpcFunc draw) {
    const uint32_t pane = c->r[3];
    // CommandGuide's A disc is a Window, not a Picture. Pane::Draw tests +0x46
    // only for DrawSelf, then still draws the children. Suppress the stock frame
    // while preserving its letter, item, action text and their inherited alpha.
    const auto name = pane_name(pane);
    int keyboard_action = (name == "W_SetSeatA_00" || name == "P_SetSeatASpecial_00") ? input_map::kA : -1;
    constexpr std::string_view plates[] = {"P_L_00","P_R_00","P_ZL_00","P_ZR_00"};
    for (int i=0;i<4;++i) if (name == plates[i]) keyboard_action = input_map::kL+i;
    const auto look = style();
    const bool gc_plate = look.face == FaceStyle::kGameCube && keyboard_action >= 0 &&
        (keyboard_action == input_map::kA || look.shoulders == ShoulderStyle::kGameCube) && shoulder_atlas(c);
    if ((look.face == FaceStyle::kKeyboard && keyboard_action >= 0 && keycap_sheet(c,keyboard_action)) || gc_plate) {
        std::vector<Held> held;
        shared_patch::hold_value(held,pane+0x46,1,0);
        draw(c);
        shared_patch::release(held);
        return;
    }
    if (deferring_cluster) {
        if ((command_word(pane) || (style().face == FaceStyle::kGameCube && shoulder_action(pane)>=0) || ((style().face == FaceStyle::kKeyboard || style().face == FaceStyle::kGameCube) &&
             (face_box(pane)>=0 || shoulder_box(pane)>=0))) && inside(pane, deferring_cluster)) {
            deferred_words.emplace_back(pane, draw);
            return;
        }
        return draw_gamecube_word(c,draw);
    }
    if (!marking() || style().face == FaceStyle::kOriginal || !command_cluster(pane)) return draw_gamecube_word(c,draw);
    const Cpu entry = *c;
    deferring_cluster = pane;
    deferred_words.clear();
    draw(c);
    deferring_cluster = 0;
    const auto words = std::move(deferred_words);
    deferred_words.clear();
    for (const auto& [word, pane_draw] : words) {
        Cpu w = entry;
        w.r[3] = word;
        draw_gamecube_word(&w,pane_draw);
    }
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
    if (look.face == FaceStyle::kKeyboard && stock_fallback(list)) return;
    for (uint32_t i = 0, count = ld16(list + 4); i < count; i++) {
        const uint8_t flags = ld8(list + 0xA4 + i * 0x30 + 0x2F);
        const int face = marked_face(flags), shoulder = marked_shoulder(flags);
        if ((face >= 0 || shoulder >= 0 || button_glyphs::marked_cstick(flags) || button_glyphs::marked_start(flags) || button_glyphs::marked_input(flags)>=0) && styled(flags) != active) {
            st8(list + 6, 0);  // the original DrawSelf rebuilds the text and its vertex buffer
            return;
        }
        if (look.face == FaceStyle::kKeyboard) {
            const int action = face>=0 ? input_map::kA+face : shoulder>=0 ? input_map::kL+shoulder
                             : input_glyph_action(button_glyphs::marked_input(flags));
            if (action >= 0) {
                const uint32_t sheet = keycap_sheet(cpu,action);
                const uint32_t expected = sheet ? sheet : ld32(list+0xA4+i*0x30+0x28);
                bool bound = false;
                for (uint32_t batch=0,n=ld32(list+0xA0); batch<n && batch<8; ++batch)
                    bound |= ld32(list+0x20+batch*0x10)==expected;
                if (!bound) { st8(list+6,0); return; }
            }
        }
        // quads drawn from the composed atlas: restyled shoulder labels and PlayStation shapes
        const bool named = item_names() && ((letter >= 2 && face >= 2) || (hud == 1 && shoulder == 1));
        if ((shoulder >= 0 && look.shoulders != ShoulderStyle::kOriginal) ||
            (face >= 0 && (look.face == FaceStyle::kPlayStation || look.face == FaceStyle::kGameCube)) ||
            (button_glyphs::marked_cstick(flags) && look.face == FaceStyle::kGameCube) ||
            (button_glyphs::marked_start(flags) && look.face == FaceStyle::kGameCube) || named) {
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
    hud_action=word_action(hud_box);
    prepare_text_box(hud_box, c);
    // HUD letters can be emitted through the font's direct glyph path, bypassing Print.
    // Supply the mapped letter to that rebuild too, then restore the authored Wii U text.
    const auto look = style();
    const uint32_t text = hud_face >= 0 ? ld32(hud_box + 0xA4) : 0;
    std::vector<Held> held;
    if (text && look.face == FaceStyle::kXbox && look.buttons[hud_face] != hud_face)
        shared_patch::hold_value(held, text, 2, "ABXY"[look.buttons[hud_face]]);
    draw(c);
    shared_patch::release(held);
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
    const int extra = style().face == FaceStyle::kKeyboard ? button_glyphs::input_character(c->r[4]) : -1;
    const bool cstick = button_glyphs::cstick_character(c->r[4]) && style().face == FaceStyle::kGameCube;
    const bool start = button_glyphs::start_character(c->r[4]) && style().face == FaceStyle::kGameCube;
    const uint32_t list = face >= 0 || shoulder >= 0 || cstick || start || extra >= 0 ? ld32(c->r[3] + 0x2C) : 0;
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
        st8(flags, face >= 0 ? button_glyphs::mark(ld8(flags), face)
                 : shoulder >= 0 ? button_glyphs::mark_shoulder(ld8(flags), shoulder)
                 : extra >= 0 ? button_glyphs::mark_input(ld8(flags),extra)
                 : cstick ? button_glyphs::mark_cstick(ld8(flags))
                 : start ? button_glyphs::mark_start(ld8(flags)) : ld8(flags));
    }
}

// Style only marked icon quads when the game generates GPU vertices, then restore their authored
// colours. Record the rendered style in bit 3 of the same private marker, so DrawSelf can detect
// a stale vertex cache on the next setting change or save-state restore. Ordinary text and
// unrelated icons are not styled; the optional outline pass uses the game's outline colours.
// The marker byte quad+0x2F sits inside the word atlas_quad holds through shared_patch. Writing it
// only when it changes keeps a second thread's identical marker write from landing between another
// thread's hold and release, where shared_patch would take it for a game write (review of #319).
static inline void set_flag_byte(uint32_t addr, uint8_t value) {
    if (ld8(addr) != value) st8(addr, value);
}

extern "C" void hook_028F7C7C(Cpu* c) {
    const PromptStyle look = style();
    const bool active = look.active();
    const bool mark = marking();
    const bool shapes = look.face == FaceStyle::kPlayStation || look.face == FaceStyle::kGameCube;
    const uint32_t list = c->r[4];
    std::vector<Held> saved;
    // Read through shared_patch: another thread may be building this text with its changes in.
    const uint16_t original_count = (uint16_t)shared_patch::authored(list + 4, 2);
    uint16_t count = original_count;
    if (look.face == FaceStyle::kKeyboard) {
        const int hud = hud_face >= 0 ? input_map::kA + hud_face
                      : hud_shoulder >= 0 ? input_map::kL + hud_shoulder : -1;
        const uint32_t hud_sheet = hud >= 0 && original_count ? keycap_sheet(c,hud) : 0;
        if (hud_sheet) {
            const uint32_t quad = list+0xA4;
            const uint8_t flags = ld8(quad+0x2F);
            set_flag_byte(quad+0x2F, hud_face >= 0 ? button_glyphs::mark(flags,hud_face)
                                                : button_glyphs::mark_shoulder(flags,hud_shoulder));
            const float w = shared_patch::authored_float(quad), h = shared_patch::authored_float(quad+4);
            const float x = shared_patch::authored_float(quad+8);
            if (std::isfinite(w) && std::isfinite(h) && h>0) {
                patch_float(saved,quad,h*1.5f);
                patch_float(saved,quad+8,x+(w-h*1.5f)*.5f);
                // TV item pictures occupy the original button letters. Put X/Y
                // keycaps to their left and R to its right; keep action words clear too.
                // Item-menu slots use a different tree and keep their authored positions.
                if (in_command_cluster(hud_box)) {
                    const float y=shared_patch::authored_float(quad+0x0C);
                    if (hud_face>=2)
                        patch_float(saved,quad+8,x+(w-h*1.5f)*.5f-h*1.5f);
                    else if (hud_shoulder==1) {
                        patch_float(saved,quad+8,x+(w-h*1.5f)*.5f+h*1.5f);
                        if (std::isfinite(y)) patch_float(saved,quad+0x0C,y+h*.45f);
                    } else if (std::isfinite(y) && hud_shoulder>=0)
                        patch_float(saved,quad+0x0C,y-h*.5f);
                }
            }
            shared_patch::hold_value(saved,list+4,2,count=1);
        }
        for (uint32_t i=0;i<count;++i) {
            const uint32_t quad=list+0xA4+i*0x30;
            const uint8_t flags=ld8(quad+0x2F);
            const int face=button_glyphs::marked_face(flags), shoulder=button_glyphs::marked_shoulder(flags);
            const int action=face>=0 ? input_map::kA+face : shoulder>=0 ? input_map::kL+shoulder
                           : button_glyphs::marked_start(flags) ? input_map::kPlus
                           : input_glyph_action(button_glyphs::marked_input(flags));
            const uint32_t sheet = hud_sheet ? hud_sheet : action>=0 ? keycap_sheet(c,action) : 0;
            if (hud_sheet || action>=0 || button_glyphs::marked_input(flags)>=0) set_flag_byte(quad+0x2F,button_glyphs::rendered(flags,true));
            if (!sheet) continue;
            keycap_quad(saved,quad,sheet,hud_sheet ? hud : action);
        }
        std::vector<std::pair<uint32_t,uint8_t>> sheets;
        for (uint32_t i=0;i<count;++i) {
            const uint32_t quad=list+0xA4+i*0x30;
            const auto key=std::pair{ld32(quad+0x28),ld8(quad+0x2E)};
            if (std::find(sheets.begin(),sheets.end(),key)==sheets.end()) sheets.push_back(key);
        }
        const bool fallback = sheets.size()>8;
        if (fallback) restore(saved); // keep native glyphs within the fixed batch budget
        f_028F7C7C_orig(c);
        restore(saved);
        if (fallback) stock_fallback(list,true);
        return;
    }
    bool needs_shoulder = hud_shoulder >= 0 && original_count;
    bool needs_shape = shapes && hud_face >= 0 && original_count;
    bool needs_cstick = false;
    bool needs_start = false;
    for (uint32_t i = 0; i < original_count; i++) {
        const uint8_t flags = ld8(list + 0xA4 + i*0x30 + 0x2F);
        needs_shoulder |= button_glyphs::marked_shoulder(flags) >= 0;
        needs_shape |= shapes && button_glyphs::marked_face(flags) >= 0;
        needs_cstick |= look.face == FaceStyle::kGameCube && button_glyphs::marked_cstick(flags);
        needs_start |= look.face == FaceStyle::kGameCube && button_glyphs::marked_start(flags);
    }
    // the item ring's names: X and Y letters (HUD face 2, 3) become A1 A2, the R plate's R becomes A3
    const bool names = item_names() && original_count && (hud_face >= 2 || hud_shoulder == 1) && shoulder_atlas(c);
    const bool shoulder_ready = (look.shoulders != ShoulderStyle::kOriginal || (names && hud_shoulder == 1)) &&
                                needs_shoulder && shoulder_atlas(c);
    const bool gc_face = look.face == FaceStyle::kGameCube && hud_face >= 0;
    const bool gc_kidney = gc_face && hud_face >= 2;
    const bool gc_shoulder = look.face == FaceStyle::kGameCube && look.shoulders == ShoulderStyle::kGameCube;
    const bool shape_ready = needs_shape && shoulder_atlas(c) && (!(names && hud_face >= 2) || gc_kidney);
    const bool cstick_ready = needs_cstick && shoulder_atlas(c);
    const bool start_ready = needs_start && shoulder_atlas(c);
    if (hud_shoulder >= 0 && original_count) {
        const uint32_t quad = list + 0xA4;
        if (mark) set_flag_byte(quad + 0x2F, button_glyphs::mark_shoulder(ld8(quad + 0x2F), hud_shoulder));
        if (shoulder_ready) {
            float left = shared_patch::authored_float(quad + 8), right = left + shared_patch::authored_float(quad);
            for (uint32_t i = 1; i < original_count; i++) {
                const uint32_t next = quad + i*0x30;
                left = std::min(left, shared_patch::authored_float(next + 8));
                right = std::max(right, shared_patch::authored_float(next + 8) + shared_patch::authored_float(next));
            }
            float width = right - left;
            const uint32_t parent = ld32(hud_box + 0x0C);
            if (hud_shoulder < 2 && parent) width = std::max(width, (float)ldf32(parent + 0x3C) * 0.7f);
            if (gc_shoulder && parent) {
                const float side = shared_patch::authored_float(parent + 0x3C);
                const float h = shared_patch::authored_float(quad + 4);
                const float y = shared_patch::authored_float(quad + 0x0C);
                if (std::isfinite(side) && side > 0) {
                    width = side;
                    patch_float(saved,quad+4,side);
                    patch_float(saved,quad+0x0C,y+(h-side)*.5f);
                }
            }
            if (std::isfinite(width) && width > 0) {
                patch_float(saved, quad, width);
                patch_float(saved, quad + 8, (left + right - width) * 0.5f);
            }
            shared_patch::hold_value(saved, list + 4, 2, count = 1);  // one complete R1/R2/L1/L2 label, not the old separate Z and R/L
        }
    }
    if (hud_face >= 0 && original_count && mark && !look.positions)
        set_flag_byte(list + 0xA4 + 0x2F, button_glyphs::mark(ld8(list + 0xA4 + 0x2F), hud_face));
    if (names && hud_face >= 2) {
        // "A1" / "A2" on the X / Y picture: a coverage glyph (the letter's own colours and dimming),
        // a little wider than the letter's height, centred on it
        const uint32_t quad = list + 0xA4;
        if (mark) set_flag_byte(quad + 0x2F, button_glyphs::mark(ld8(quad + 0x2F), hud_face));
        const float w = shared_patch::authored_float(quad), h = shared_patch::authored_float(quad + 4);
        const float x = shared_patch::authored_float(quad + 8), y = shared_patch::authored_float(quad + 0x0C);
        const float side = h * 1.1f;
        if (std::isfinite(side) && side > 0 && std::isfinite(w) && std::isfinite(x) && std::isfinite(y)) {
            patch_float(saved, quad, side);
            patch_float(saved, quad + 4, side);
            patch_float(saved, quad + 8, x + (w - side) * 0.5f);
            patch_float(saved, quad + 0x0C, y + (h - side) * 0.5f);
        }
        shared_patch::hold_value(saved, list + 4, 2, count = 1);
    }
    if (hud_face >= 0 && original_count && shape_ready) {
        // The HUD letter becomes the shape, square and centred on the letter, on the game's own
        // button picture. Coloured: white vertex colours, the shape carries its own colour.
        // Without colours the shape is a coverage glyph: the game's own letter colours and
        // dimming apply to it unchanged.
        const uint32_t quad = list + 0xA4;
        if (mark) set_flag_byte(quad + 0x2F, button_glyphs::mark(ld8(quad + 0x2F), hud_face));
        const float w = shared_patch::authored_float(quad), h = shared_patch::authored_float(quad + 4);
        const float x = shared_patch::authored_float(quad + 8), y = shared_patch::authored_float(quad + 0x0C);
        float side = h * 0.8f;
        if (gc_face) {
            const uint32_t parent = ld32(hud_box + 0x0C);
            if (parent) {
                const float pw = shared_patch::authored_float(parent + 0x3C);
                const float ph = shared_patch::authored_float(parent + 0x40);
                if (std::isfinite(pw) && pw > 0) side = pw;
                if (std::isfinite(ph) && ph > side) side = ph;
            }
        }
        if (std::isfinite(side) && side > 0 && std::isfinite(w) && std::isfinite(x) && std::isfinite(y)) {
            patch_float(saved, quad, side);
            patch_float(saved, quad + 4, side);
            patch_float(saved, quad + 8, x + (w - side) * 0.5f);
            patch_float(saved, quad + 0x0C, y + (h - side) * 0.5f);
        }
        if (look.colours || gc_face)
            for (uint32_t corner = 0x10; corner <= 0x14; corner += 4)
                shared_patch::hold(saved, quad + corner, 4, [](uint32_t ink) { return ink | 0xFFFFFF00u; });
        shared_patch::hold_value(saved, list + 4, 2, count = 1);
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t quad = list + 0xA4 + i * 0x30;
        const int face = button_glyphs::marked_face(ld8(quad + 0x2F));
        const int shoulder = button_glyphs::marked_shoulder(ld8(quad + 0x2F));
        const bool cstick = button_glyphs::marked_cstick(ld8(quad + 0x2F));
        const bool start = button_glyphs::marked_start(ld8(quad + 0x2F));
        // Action words take their button's prompt colour, independently of icon colours; with
        // text colours off they keep the game's own.
        if (hud_action >= 0 && look.text_colours &&
            (hud_action < 4 || (look.face == FaceStyle::kGameCube && look.shoulders == ShoulderStyle::kGameCube))) {
            const bool gc_shoulder_word = hud_action >= 4;
            const int button = gc_shoulder_word ? hud_action-4 : look.buttons[hud_action];
            const uint32_t rgb = gc_shoulder_word ? (button==1 ? button_glyphs::kGameCubeZ : button_glyphs::kGameCubeShoulder)
                              : look.face == FaceStyle::kPlayStation ? button_glyphs::kShapeColours[button]
                              : look.face == FaceStyle::kGameCube ? button_glyphs::gamecube_colour(button)
                              : button_glyphs::colour(button);
            for (uint32_t offset : {0x10u, 0x14u})
                shared_patch::hold(saved, quad + offset, 4, [rgb,gc_shoulder_word](uint32_t ink) {
                    uint32_t tinted = ink & 255;
                    const uint32_t shade=std::max({(ink>>24)&255,(ink>>16)&255,(ink>>8)&255});
                    for (int shift = 24; shift > 0; shift -= 8)
                        tinted |= ((gc_shoulder_word ? shade : ((ink >> shift) & 255)) * ((rgb >> shift) & 255) / 255) << shift;
                    return tinted;
                });
        }
        if (face < 0 && shoulder < 0 && !cstick && !start) continue;
        if (mark) set_flag_byte(quad + 0x2F, button_glyphs::rendered(ld8(quad + 0x2F), active));
        if (face >= 0 && hud_face < 0) {  // dialog/icon-font glyphs; HUD letters keep their ink
            if (look.face == FaceStyle::kXbox && look.colours) {
                tint_word(saved, quad + 0x10, look.buttons[face]);
                tint_word(saved, quad + 0x14, look.buttons[face]);
            } else if (shape_ready) {
                atlas_quad(saved, quad, look.face == FaceStyle::kGameCube ? button_glyphs::gamecube_row(face)
                               : button_glyphs::shape_row(look.buttons[face], look.colours), true, c);
            }
        }
        if (face >= 0 && hud_face >= 0 && shape_ready)
            atlas_quad(saved, quad, look.face == FaceStyle::kGameCube ? button_glyphs::gamecube_row(face)
                       : button_glyphs::shape_row(look.buttons[face], look.colours),
                       look.face == FaceStyle::kGameCube, c, look.face == FaceStyle::kPlayStation && !look.colours);
        if (names && face >= 2 && hud_face >= 2 && !gc_kidney)
            atlas_quad(saved, quad, button_glyphs::item_name_row(hud_face - 2), false, c, true);
        // Dialogs: the framed label is an RGBA picture (#141). HUD shoulder plates: the bare
        // caption is a coverage glyph like the plate's own "R"/"ZR", so the game draws it in the
        // plate's dark letter colour and dims it with the button (#143); as a picture it was
        // white on the white plate.
        if (gc_shoulder && hud_shoulder >= 0 && shoulder >= 0)
            for (uint32_t offset : {0x10u,0x14u})
                shared_patch::hold(saved,quad+offset,4,[](uint32_t rgba) { return rgba | 0xFFFFFF00u; });
        if (shoulder_ready && shoulder >= 0)
            atlas_quad(saved, quad, names && hud_shoulder == 1 ? button_glyphs::item_name_row(2) : shoulder_row(look.shoulders, shoulder),
                       hud_shoulder < 0 || gc_shoulder, c, hud_shoulder >= 0 && look.shoulders != ShoulderStyle::kGameCube);
        if (cstick && cstick_ready) atlas_quad(saved, quad, button_glyphs::gamecube_cstick_row(), true, c);
        if (start && start_ready) atlas_quad(saved, quad, button_glyphs::gamecube_start_row(), true, c);
    }
    f_028F7C7C_orig(c);
    restore(saved);  // also the glyph count, if the label merge changed it
}

// CommandGuide, the floating CommandA prompt and IconSlideArea use the same ABXY background
// texture, with separate text panes for the letters. Style the picture's four vertex colours
// instead of swapping that shared texture (which cannot distinguish A from B/X/Y).
extern "C" void hook_02874D54(Cpu* c) {
    const uint32_t picture = c->r[3];
    const int face = button_glyphs::face_picture(pane_name(picture));
    const PromptStyle look = style();
    if (face >= 0 && look.face == FaceStyle::kKeyboard && keycap_sheet(c,input_map::kA+face)) {
        std::vector<Held> held;
        for (uint32_t i=0;i<4;++i)
            shared_patch::hold(held,picture+0xA8+4*i,4,[](uint32_t rgba) { return rgba & 0xFFFFFF00u; });
        f_02874D54_orig(c);
        shared_patch::release(held);
        return;
    }
    if (face < 0 || (look.face != FaceStyle::kXbox && look.face != FaceStyle::kGameCube) ||
        (look.face == FaceStyle::kXbox && !look.colours)) return f_02874D54_orig(c);
    // Through shared_patch: in "play on GamePad" mode the item screen draws these pictures on two
    // threads at once, so neither a texture swap nor a tint may persist after DrawSelf (#326).
    std::vector<shared_patch::Held> held;
    if (look.face == FaceStyle::kGameCube && shoulder_atlas(c)) {
        // Complete GC glyphs replace every stock disc; its separate white rim must not remain.
        for (uint32_t i = 0; i < 4; ++i)
            shared_patch::hold(held, picture + 0xA8 + 4 * i, 4, [](uint32_t rgba) { return rgba & 0xFFFFFF00u; });
    } else if (look.face == FaceStyle::kXbox) {
        const int button = look.buttons[face];
        for (uint32_t i = 0; i < 4; i++)
            shared_patch::hold(held, picture + 0xA8 + 4 * i, 4, [button](uint32_t rgba) {
                return button_glyphs::tint(rgba, button);
            });
    }
    f_02874D54_orig(c);
    shared_patch::release(held);
}

namespace button_glyphs {
// A loaded save state brings back text boxes cached at addresses that may already be in the
// one-shot rebuild set: forget it, so each is rebuilt (and marked) again when first drawn.
void ss_reset() { forget_rebuilt(); }
}  // namespace button_glyphs
