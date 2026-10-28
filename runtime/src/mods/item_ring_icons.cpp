// SPDX-License-Identifier: MPL-2.0
// The item ring's pictures (item_ring_icons.h).
#include "item_ring_icons.h"

#include <cstdio>
#include <map>
#include <string>

#include "game_layouts.h"
#include "guest_addr.h"
#include "runtime.h"

namespace item_ring_icons {
namespace {
struct Icon {
    guestmods::hud::Handle handle = 0;
    uint64_t generation = 0;
    bool failed = false;
};
std::map<uint8_t, Icon> g_icons;  // game thread
}  // namespace

int frame_of(uint8_t item) {
    const uint32_t table = GD(0x100E7AB8);
    for (uint32_t e = table; e < table + 57 * 16; e += 16)
        if (ld32(e) == item) {
            const uint32_t f = ld32(e + 4);
            return f < 55 ? int(f) : -1;
        }
    return -1;
}

guestmods::hud::Handle handle(Cpu*, uint8_t item, const char* owner) {
    auto& store = guestmods::hud::store();
    Icon& icon = g_icons[item];
    const uint64_t gen = store.state_generation();
    if (icon.failed) return 0;
    if (icon.handle && icon.generation == gen) return icon.handle;
    char spec[64];
    // the Wind Waker and the sails have no item-button frame: their collection-screen icons (the
    // Wind Waker's is also the d-pad's), as the game's BtnCollectIcon_00 has them
    const char* collect = item == 0x22 ? "15" : item == 0x78 ? "06" : item == 0x77 ? "16" : nullptr;
    const int f = collect ? 0 : frame_of(item);
    if (collect) snprintf(spec, sizeof spec, "BtnCollectIcon_00/CollectIcon118_%s^l", collect);
    else snprintf(spec, sizeof spec, "BtnItemIcon_00/Icon128_%02d^l", f);
    if (f < 0) {
        icon.failed = true;
        LOG("[ring] item %02X has no icon frame: its name is shown", item);
        return 0;
    }
    game_layouts::Image img;
    std::string error;
    if (!game_layouts::texture(spec, img, error)) {
        icon.failed = true;
        LOG("[ring] item %02X: no icon (%s: %s): its name is shown", item, spec, error.c_str());
        return 0;
    }
    icon.handle = store.create_image(owner, img.width, img.height, img.width * 4, img.rgba.data(), img.rgba.size());
    icon.generation = gen;
    return icon.handle;
}
void keep_only(const uint8_t* items, int n, const char* owner) {
    int live = 0;
    for (const auto& [item, icon] : g_icons) live += icon.handle != 0;
    if (live + n <= 30) return;  // the HUD's 32 images per owner
    auto& store = guestmods::hud::store();
    for (auto& [item, icon] : g_icons) {
        bool keep = false;
        for (int i = 0; i < n; i++) keep |= items[i] == item;
        if (!keep && icon.handle) store.release(owner, icon.handle), icon.handle = 0;  // (a list drawn keeps its own)
    }
}
}  // namespace item_ring_icons
