// The story notebook and the game's own resources (story_notebook_game.h).
#include "story_notebook_game.h"
#include "story_notebook_font.h"
#include "story_notebook_style.h"

#include <cstring>
#include <functional>
#include <map>
#include <set>

#include "guest_addr.h"
#include "input_map.h"
#include "runtime.h"

namespace story_notebook {
namespace {

const uint32_t kFontManager = GD(0x101F4A50);   // hdFontMgr_c* (hd_font_mgr: sInstances)
constexpr uint32_t kFonts = 0x10;               // hdFontMgr_c::mpFont[7]
constexpr uint32_t kFontGetGlyph = 0x8C;        // nw::font::Font vtable: GetGlyph(glyph*, code)
const uint32_t kPlaySe = GC(0x02030B38);        // the game's named sound effect (hd sound interface: playSe(name))

bool guest_pointer(uint32_t a) {
    return (a >= mem::kMem2Start && a < mem::kMem2End - 0x200) || (a >= mem::kMem1 && a < mem::kMem1 + mem::kMem1Size - 0x200);
}

uint32_t scratch(uint32_t size) {
    thread_local uint32_t at = 0;
    if (!at) at = mem::host_alloc(0x100, 16);
    return size <= 0x100 ? at : 0;
}

// the game's font in a slot, once it is loaded (as button_glyphs.cpp checks the HUD font)
uint32_t font_in(int slot) {
    const uint32_t manager = ld32(kFontManager);
    if (!manager || slot < 0 || slot > 6) return 0;
    const uint32_t font = ld32(manager + kFonts + 4 * uint32_t(slot));
    if (!font || !guest_pointer(font) || !ld32(font + 8) || !ld32(font + 0x0C) || !ld32(font + 0x44)) return 0;
    const uint32_t table = ld32(font + 4);
    return table && guest_pointer(table) && ld32(table + kFontGetGlyph) ? font : 0;
}

// one glyph record from the game's Font::GetGlyph (the CharWriter's own call)
bool glyph_record(Cpu* c, uint32_t font, uint32_t code, uint32_t record) {
    std::memset(mem::ptr(record), 0, 0x18);
    Cpu call = *c;
    guest_call(&call, ld32(ld32(font + 4) + kFontGetGlyph), {font, record, code});
    return ld32(record + 0x14) != 0;
}

bool glyphs(Cpu* c, int slot, const std::vector<uint32_t>& cps, font::Face& face) {
    const uint32_t f = font_in(slot);
    const uint32_t record = scratch(0x18);
    if (!f || !record) return false;
    // the font's stand-in (what GetGlyph gives for a character it lacks): a code no font maps
    if (!glyph_record(c, f, 0xFFFF, record)) return false;
    uint8_t alternate[0x18];
    std::memcpy(alternate, mem::ptr(record), 0x18);
    for (uint32_t cp : cps) {
        if (face.glyphs.count(cp) || face.missing.count(cp)) continue;
        if (!glyph_record(c, f, cp, record) || (cp != '?' && std::memcmp(alternate + 4, mem::ptr(record + 4), 0x14) == 0)) {
            face.missing[cp] = true;
            continue;
        }
        game_glyph::Glyph g;
        if (!game_glyph::read_glyph(record, g)) {
            face.missing[cp] = true;
            continue;
        }
        if (!face.cell_height) face.cell_height = g.bitmap.height;
        face.glyphs[cp] = std::move(g);
    }
    return true;
}

void sound(Cpu* c, const char* label) {
    // our own copy of the label (the game's sound table looks the name up; no game string is needed)
    thread_local std::map<std::string, uint32_t> labels;
    uint32_t& at = labels[label];
    if (!at) {
        at = mem::host_alloc(64, 16);
        mem::write_cstr(at, label, 64);
    }
    Cpu call = *c;
    guest_call(&call, kPlaySe, {at});
}

// ---- the item menu's art (story_notebook_style.h) ----
std::string pane_name(uint32_t pane);
bool looks_like_texture(uint32_t t);
// a pane of the layout by name (depth first from `pane`)
uint32_t find_pane(uint32_t pane, const std::string& name, int depth = 0) {
    if (!guest_pointer(pane) || depth > 24) return 0;
    if (pane_name(pane) == name) return pane;
    const uint32_t sentinel = pane + 0x14;
    uint32_t child = ld32(sentinel);
    for (unsigned guard = 0; child && child != sentinel && guard < 256; guard++, child = ld32(child))
        if (const uint32_t found = find_pane(child, name, depth + 1)) return found;
    return 0;
}
// a texture `offsets` pointer hops from `base`, decoded; w/h: the size it must have (0: any)
bool texture_at(uint32_t base, std::initializer_list<uint32_t> offsets, game_glyph::Bitmap& out, int w = 0, int h = 0) {
    uint32_t at = base;
    for (uint32_t o : offsets) {
        if (!guest_pointer(at)) return false;
        at = ld32(at + o);
    }
    if (!looks_like_texture(at)) return false;
    if ((w && int(ld32(at + 4)) != w) || (h && int(ld32(at + 8)) != h)) return false;
    return game_glyph::read_texture(at, out);
}
bool art(uint32_t menu, style::Art& out, std::string& why) {
    const uint32_t layout = guest_pointer(menu) ? ld32(menu + 0x44) : 0, lyt = guest_pointer(layout) ? ld32(layout + 4) : 0;
    const uint32_t root = guest_pointer(lyt) ? ld32(lyt + 0xC) : 0;
    if (!guest_pointer(root)) return why = "no menu layout", false;
    out = {};
    // the menu background's torn-paper window: the eight frame pieces of its edge (nw::lyt::Window,
    // frames at +0xD0: {flip, material}; material +0x10: its texture maps, +0x18 the texture)
    if (const uint32_t edge = find_pane(root, "W_BaseEdgeShadow_00")) {
        const uint32_t frames = ld32(edge + 0xD0);
        for (uint32_t k = 0; k < 8; k++) {
            const bool corner = k < 4, side = k == 4 || k == 5;
            if (!texture_at(frames, {4 + 8 * k, 0x10, 0x18}, out.edge[k], corner || side ? 48 : 0, corner || !side ? 48 : 0))
                return why = "W_BaseEdgeShadow_00 frame " + std::to_string(k) + " not readable", false;
        }
    } else {
        return why = "no W_BaseEdgeShadow_00", false;
    }
    if (const uint32_t base = find_pane(root, "W_Base_00")) {
        if (!texture_at(base, {0x10C, 0x44}, out.fibre)) return why = "W_Base_00 content not readable", false;
        out.paper = ld32(base + 0xB4);
    } else {
        return why = "no W_Base_00", false;
    }
    // the menu's Save plate: its rim and its plate with the cloth and the gloss (picture panes,
    // material at +0xF4: texture maps at +0x28 and +0x44)
    const uint32_t save = find_pane(root, "L_MenuSave_00");
    const uint32_t rim = save ? find_pane(save, "P_BtnShadow_00") : 0, plate = save ? find_pane(save, "P_Btn_00") : 0;
    if (!rim || !plate || !texture_at(rim, {0xF4, 0x28}, out.plate_rim, 320, 100) || !texture_at(plate, {0xF4, 0x28}, out.plate, 320, 100)) {
        out.plate_rim = {}, out.plate = {};
        why = "the Save plate is not readable (plain plates)";
    } else if (!texture_at(plate, {0xF4, 0x44}, out.gloss, 320, 100)) {
        out.gloss = {};
    }
    // the Info box (what A shows for an item on the TV): its torn edges and its swirls
    for (const char* name : {"W_TextBase_00", "W_TextBase_01"}) {
        const uint32_t box = find_pane(root, name);
        const uint32_t deco = box ? find_pane(box, std::string(name) == "W_TextBase_00" ? "P_Deco_00" : "P_Deco_01") : 0;
        if (!box || !deco || !texture_at(box, {0x10C, 0x28}, out.info_top, 0, 16) || !texture_at(ld32(box + 0xD0), {0xC, 0x10, 0x18}, out.info_bottom, 0, 16) ||
            !texture_at(deco, {0xF4, 0x44}, out.swirl)) {
            out.info_top = out.info_bottom = out.swirl = {};
            continue;
        }
        break;
    }
    if (!out.info()) why += std::string(why.empty() ? "" : "; ") + "the Info box is not readable (a plain box)";
    return true;
}

bool chart_icons(uint32_t chart, style::Art& out) {
    const uint32_t layout = guest_pointer(chart) ? ld32(chart + 0x44) : 0, lyt = guest_pointer(layout) ? ld32(layout + 4) : 0;
    const uint32_t root = guest_pointer(lyt) ? ld32(lyt + 0xC) : 0;
    if (!guest_pointer(root) || pane_name(root) != "L_Map_00") {
        LOG("[notebook] the sea chart's layout is not where it was expected: no chart icons");
        return false;
    }
    const uint32_t face = find_pane(root, "P_MapLinkFace_00"), ship = find_pane(root, "P_DecoShip_00");
    game_glyph::Bitmap f, sh;
    if (!face || !texture_at(face, {0xF4, 0x28}, f, 64, 64)) {
        LOG("[notebook] the sea chart's Link icon (P_MapLinkFace_00) is not readable");
        return false;
    }
    out.link_face = std::move(f);
    if (ship && texture_at(ship, {0xF4, 0x28}, sh, 32, 32)) out.ship = std::move(sh);
    return true;
}

// ---- research aid: the menu layout's panes and the textures reachable from them (log only) ----
std::string pane_name(uint32_t pane) {
    const char* name = (const char*)mem::ptr(pane + 0x80);
    return {name, strnlen(name, 24)};
}
bool looks_like_texture(uint32_t t) {
    if (!guest_pointer(t)) return false;
    const uint32_t dim = ld32(t), w = ld32(t + 4), h = ld32(t + 8), d = ld32(t + 0xC), mips = ld32(t + 0x10), fmt = ld32(t + 0x14) & 0x3F;
    const uint32_t size = ld32(t + 0x20), image = ld32(t + 0x24);
    const bool format = fmt == 1 || fmt == 2 || fmt == 7 || fmt == 11 || fmt == 0x1A || (fmt >= 0x31 && fmt <= 0x35);
    return dim >= 1 && dim <= 5 && w && w <= 4096 && h && h <= 4096 && d && d <= 64 && mips && mips <= 13 && format && size &&
           guest_pointer(image);
}
void dump(Cpu* c, uint32_t menu, const std::string&) {
    (void)c;
    const uint32_t layout = ld32(menu + 0x44), lyt = layout ? ld32(layout + 4) : 0, root = lyt ? ld32(lyt + 0xC) : 0;
    LOG("[notebook-panes] menu %08X layout %08X lyt %08X root %08X", menu, layout, lyt, root);
    if (!root) return;
    std::function<void(uint32_t, int)> walk = [&](uint32_t pane, int depth) {
        if (!guest_pointer(pane) || depth > 24) return;
        char words[600];
        int k = 0;
        for (uint32_t o = 0x98; o < 0x110; o += 4) k += snprintf(words + k, sizeof words - k, " %08X", ld32(pane + o));
        LOG("[notebook-panes] %*s%s %08X size %.0fx%.0f flags %02X vt? %08X |%s", depth * 2, "", pane_name(pane).c_str(), pane,
            u32_as_f32(ld32(pane + 0x3C)), u32_as_f32(ld32(pane + 0x40)), ld8(pane + 0x44), ld32(pane + 0x4C), words);
        // textures within three pointer hops of the pane
        struct Item { uint32_t at; int hops; std::string path; };
        std::vector<Item> todo = {{pane, 0, ""}};
        std::set<uint32_t> seen;
        for (size_t i = 0; i < todo.size() && i < 400; i++) {
            const Item it = todo[i];
            const uint32_t span = it.hops == 0 ? 0x140 : 0x60;
            for (uint32_t o = 0; o < span; o += 4) {
                const uint32_t p = ld32(it.at + o);
                if (!guest_pointer(p) || (p & 3) || seen.count(p) || p == pane) continue;
                seen.insert(p);
                char step[16];
                snprintf(step, sizeof step, "+%X", o);
                const std::string path = it.path + step;
                if (looks_like_texture(p)) {
                    LOG("[notebook-panes] %*s  texture via %s: %08X %ux%u fmt %X tile %u comp %08X", depth * 2, "", path.c_str(), p, ld32(p + 4),
                        ld32(p + 8), ld32(p + 0x14), ld32(p + 0x2C), ld32(p + 0x84));
                } else if (it.hops < 3) {
                    todo.push_back({p, it.hops + 1, path});
                }
            }
        }
        const uint32_t sentinel = pane + 0x14;
        uint32_t child = ld32(sentinel);
        for (unsigned guard = 0; child && child != sentinel && guard < 256; guard++, child = ld32(child)) walk(child, depth + 1);
    };
    walk(root, 0);
}

font::Prompts prompts() {
    const input_map::PromptStyle look = input_map::active_prompt_style();
    font::Prompts out;
    out.face = look.face == input_map::FaceStyle::kXbox ? font::Prompts::Xbox
             : look.face == input_map::FaceStyle::kPlayStation ? font::Prompts::PlayStation : font::Prompts::Original;
    out.colours = look.colours;
    out.buttons = look.buttons;
    return out;
}

const GameUi kUi = {glyphs, sound, art, chart_icons, prompts, dump};

}  // namespace

const GameUi& game_ui() { return kUi; }

}  // namespace story_notebook
