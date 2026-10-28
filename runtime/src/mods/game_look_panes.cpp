// The guest layout helpers of game_look.h (the game binary only: the unit tests that draw pages with a
// Look don't link this file).
#include "game_look.h"

#include <cstring>

#include "runtime.h"

namespace game_look {

bool guest_pointer(uint32_t a) {
    return (a >= mem::kMem2Start && a < mem::kMem2End - 0x200) || (a >= mem::kMem1 && a < mem::kMem1 + mem::kMem1Size - 0x200);
}
uint32_t layout_root(uint32_t screen) {
    const uint32_t layout = guest_pointer(screen) ? ld32(screen + 0x44) : 0, lyt = guest_pointer(layout) ? ld32(layout + 4) : 0;
    const uint32_t root = guest_pointer(lyt) ? ld32(lyt + 0xC) : 0;
    return guest_pointer(root) ? root : 0;
}
std::string pane_name(uint32_t pane) {
    if (!guest_pointer(pane)) return {};
    const char* name = (const char*)mem::ptr(pane + 0x80);
    return {name, strnlen(name, 24)};
}
uint32_t find_pane(uint32_t pane, const std::string& name) {
    struct Walk {
        const std::string& name;
        uint32_t operator()(uint32_t p, int depth) const {
            if (!guest_pointer(p) || depth > 24) return 0;
            if (pane_name(p) == name) return p;
            const uint32_t sentinel = p + 0x14;
            uint32_t child = ld32(sentinel);
            for (unsigned guard = 0; child && child != sentinel && guard < 256; guard++, child = ld32(child))
                if (const uint32_t found = (*this)(child, depth + 1)) return found;
            return 0;
        }
    };
    return Walk{name}(pane, 0);
}
bool visible(uint32_t pane) { return guest_pointer(pane) && (ld8(pane + 0x44) & 1); }
void set_visible(uint32_t pane, bool on) {
    if (!guest_pointer(pane)) return;
    const uint8_t f = ld8(pane + 0x44);
    st8(pane + 0x44, on ? uint8_t(f | 1) : uint8_t(f & ~1));
}
static bool looks_like_texture(uint32_t t) {
    if (!guest_pointer(t)) return false;
    const uint32_t dim = ld32(t), w = ld32(t + 4), h = ld32(t + 8), d = ld32(t + 0xC), mips = ld32(t + 0x10), fmt = ld32(t + 0x14) & 0x3F;
    const uint32_t size = ld32(t + 0x20), image = ld32(t + 0x24);
    const bool format = fmt == 1 || fmt == 2 || fmt == 7 || fmt == 11 || fmt == 0x1A || (fmt >= 0x31 && fmt <= 0x35);
    return dim >= 1 && dim <= 5 && w && w <= 4096 && h && h <= 4096 && d && d <= 64 && mips && mips <= 13 && format && size &&
           guest_pointer(image);
}
bool texture(uint32_t base, std::initializer_list<uint32_t> offsets, game_glyph::Bitmap& out, int w, int h) {
    uint32_t at = base;
    for (uint32_t o : offsets) {
        if (!guest_pointer(at)) return false;
        at = ld32(at + o);
    }
    if (!looks_like_texture(at)) return false;
    if ((w && int(ld32(at + 4)) != w) || (h && int(ld32(at + 8)) != h)) return false;
    return game_glyph::read_texture(at, out);
}

}  // namespace game_look
