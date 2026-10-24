// Mod items: the live icons of the world editor (item_icons.h, "live icons"): the game side. The pending
// pictures go into the game's BtnItemIcon_00 in its layout resource cache, in place (the renderer's texture
// checks see the change, as with story_lyt_screen::set_texture_pixels).
#include <cstring>
#include <filesystem>

#include "guest_addr.h"
#include "item_icons.h"
#include "lyt_build.h"
#include "runtime.h"

namespace item_icons {
namespace {
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
uint32_t g_name = 0;  // a sead SafeString "BtnItemIcon_00"
}  // namespace

void set_game_calls(uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>), uint32_t archive_name) {
    g_call = call, g_name = archive_name;
}

bool write_texture(uint32_t sarc, const std::string& file, const Bytes& flim) {
    if (!sarc || ld32(sarc) != 0x53415243 || flim.size() < 0x28) return false;  // "SARC"
    uint32_t hash = 0;
    for (unsigned char ch : file) hash = hash * 0x65 + ch;  // the SARC name hash (key 0x65)
    const uint32_t data = sarc + ld32(sarc + 0x0C), sfat = sarc + ld16(sarc + 0x04);
    const uint32_t count = ld16(sfat + 0x06);
    const size_t ours = flim.size() - 0x28;  // our footer: "FLIM" header + "imag"
    const uint32_t image = uint32_t(flim[ours + 0x24]) << 24 | uint32_t(flim[ours + 0x25]) << 16 | uint32_t(flim[ours + 0x26]) << 8 | flim[ours + 0x27];
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t e = sfat + 0x0C + 16 * i;
        if (ld32(e) != hash) continue;
        const uint32_t start = data + ld32(e + 8), end = data + ld32(e + 12);
        if (end < start + 0x28) return false;
        const uint32_t footer = end - 0x28;
        // the same picture size, alignment, format, tiling and image size
        if (ld32(footer) != 0x464C494D || memcmp(mem::ptr(footer + 0x1C), &flim[ours + 0x1C], 0x0C) != 0) return false;
        if (image > flim.size() || start + image > footer) return false;
        memcpy(mem::ptr(start), flim.data(), image);
        return true;
    }
    return false;
}

void service(Cpu* c) {
    if (!g_call || !g_name || !c) return;
    const std::vector<std::pair<int, std::string>> pending = pending_icons();
    if (pending.empty()) return;
    const uint32_t mgr = ld32(GD(0x101F7274));  // the layout resource manager
    if (!mgr) return;
    Cpu call = *c;
    const uint32_t sarc = g_call(&call, GC(0x026FBB24), {mgr, g_name});  // hdLayoutResMgr_c::find
    if (!sarc || ld32(sarc) != 0x53415243) return;                      // not cached (yet): later
    for (const auto& [f, png] : pending) {
        bool ok = false;
        uint64_t h = 0;
        std::string why;
        try {
            const Bytes px = picture(png);
            h = picture_hash(px);
            ok = icon_written(f, h);  // that picture is there already
            if (!ok) {
                ok = write_texture(sarc, icon_file(f), lyt_build::texture(px, kIconSize, kIconSize, "rgba8", "tiled", 0)) &&
                     write_texture(sarc, glow_file(f), lyt_build::texture(glow(px), kIconSize, kIconSize, "bc5la", "tiled", 0));
                if (!ok) why = "the frame's textures in memory are not as built";
            }
        } catch (const std::exception& e) {
            why = e.what();
        }
        LOG("[items] item icons: frame %d %s%s", f, ok ? "shows " : "not changed: ", ok ? std::filesystem::path(png).filename().string().c_str() : why.c_str());
        icon_done(f, png, h, ok);
    }
}
}  // namespace item_icons
