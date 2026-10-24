// The world editor's live item icons (#270, runtime/src/mods/item_icons*.cpp, "live icons"): the spare
// frames a new item takes, an icon drawn again in its frame, and the textures written into the game's
// BtnItemIcon_00 archive in (synthetic) guest memory, only where the texture is as built.
#include "mods/item_icons.h"
#include "mods/lyt_build.h"
#include "runtime.h"

#include <zlib.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void log_msg(const char*, ...) {}

namespace {
using item_icons::Bytes;
constexpr uint32_t kArchive = 0x20000000, kMgr = 0x20F00000, kName = 0x20F00100;

void map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
#endif
}
void put32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
}
void chunk(Bytes& o, const char* type, const Bytes& data) {
    put32(o, uint32_t(data.size()));
    Bytes td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    o.insert(o.end(), td.begin(), td.end());
    put32(o, uint32_t(crc32(0, td.data(), uInt(td.size()))));
}
// a 16 x 16 PNG of one colour
std::string png(const std::filesystem::path& dir, const char* name, uint8_t r, uint8_t g, uint8_t b) {
    Bytes raw;
    for (int y = 0; y < 16; y++) {
        raw.push_back(0);
        for (int x = 0; x < 16; x++) raw.insert(raw.end(), {r, g, b, 255});
    }
    uLongf n = compressBound(uLong(raw.size()));
    Bytes z(n);
    compress(z.data(), &n, raw.data(), uLong(raw.size()));
    z.resize(n);
    Bytes o{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}, ihdr;
    put32(ihdr, 16), put32(ihdr, 16);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(o, "IHDR", ihdr), chunk(o, "IDAT", z), chunk(o, "IEND", {});
    const std::filesystem::path p = dir / name;
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(o.data()), std::streamsize(o.size()));
    return p.string();
}
Bytes icon(const Bytes& px) { return lyt_build::texture(px, 128, 128, "rgba8", "tiled", 0); }
Bytes glow(const Bytes& px) { return lyt_build::texture(item_icons::glow(px), 128, 128, "bc5la", "tiled", 0); }
// the texture `file` of the archive in guest memory (its image bytes)
Bytes image_in_memory(const std::map<std::string, Bytes>& files, const std::string& file) {
    const Bytes sarc = item_icons::archive(files);
    const std::map<std::string, Bytes> back = item_icons::read_sarc(Bytes(mem::ptr(kArchive), mem::ptr(kArchive) + sarc.size()));
    return back.at(file);
}
uint32_t game_call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    assert(fn == 0x026FBB24 && args.size() == 2 && *args.begin() == kMgr && *(args.begin() + 1) == kName);
    return kArchive;
}
}  // namespace

int main() {
    map(kArchive, 0x100000), map(0x101F0000, 0x10000), map(kMgr, 0x1000);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wwhd_item_icons_live_test";
    std::filesystem::create_directories(dir);
    const std::string red = png(dir, "red.png", 255, 0, 0), green = png(dir, "green.png", 0, 255, 0), blue = png(dir, "blue.png", 0, 0, 255);

    // one item from the start (frame 56) and two spare frames (57, 58), not served yet: nothing live
    item_icons::set_items({{0x11, red}}, dir, 2);
    assert(!item_icons::live_icons() && item_icons::live_icon(0x22, green) < 0 && item_icons::free_spares() == 0);
    item_icons::served_for_tests();
    assert(item_icons::live_icons() && item_icons::free_spares() == 2 && item_icons::frame(0x11) == 56 && !item_icons::has_frame(0x22));
    // a new item takes the first free spare; asking again keeps its frame; a third new item: none left
    assert(item_icons::live_icon(0x22, green) == 57 && item_icons::live_icon(0x22, green) == 57 && item_icons::frame(0x22) == 57);
    assert(item_icons::live_icon(0x33, blue) == 58 && item_icons::free_spares() == 0 && item_icons::live_icon(0x44, blue) < 0);
    // an icon change of the start item: its own frame
    assert(item_icons::live_icon(0x11, blue) == 56);

    // the game's archive in memory: frames 57 and 58 empty (as built), frame 56 of another format
    const Bytes empty(128 * 128 * 4, 0);
    std::map<std::string, Bytes> files = {{item_icons::icon_file(57), icon(empty)}, {item_icons::glow_file(57), glow(empty)},
                                          {item_icons::icon_file(58), icon(empty)}, {item_icons::glow_file(58), glow(empty)},
                                          {item_icons::icon_file(56), lyt_build::texture(empty, 128, 128, "bc3", "tiled", 0)}};
    const Bytes sarc = item_icons::archive(files);
    std::memcpy(mem::ptr(kArchive), sarc.data(), sarc.size());
    // not served before the game calls are set, nor while the archive isn't cached
    Cpu c{};
    item_icons::service(&c);
    assert(item_icons::pending_icons().size() == 3);
    item_icons::set_game_calls(game_call, kName);
    item_icons::service(&c);  // no layout resource manager yet: later
    assert(item_icons::pending_icons().size() == 3);
    st32(0x101F7274, kMgr);
    item_icons::service(&c);
    assert(item_icons::pending_icons().empty());
    // frames 57 and 58 show the new pictures (icon and glow); frame 56 is left as it is (not as built)
    const Bytes g = item_icons::picture(green), b = item_icons::picture(blue);
    const Bytes in57 = image_in_memory(files, item_icons::icon_file(57)), want57 = icon(g);
    assert(std::memcmp(in57.data(), want57.data(), want57.size() - 0x28) == 0);
    const Bytes glow57 = image_in_memory(files, item_icons::glow_file(57)), want_glow = glow(g);
    assert(std::memcmp(glow57.data(), want_glow.data(), want_glow.size() - 0x28) == 0);
    const Bytes in58 = image_in_memory(files, item_icons::icon_file(58)), want58 = icon(b);
    assert(std::memcmp(in58.data(), want58.data(), want58.size() - 0x28) == 0);
    assert(image_in_memory(files, item_icons::icon_file(56)) == files[item_icons::icon_file(56)]);
    // the same picture again: nothing to write; another one: written
    assert(item_icons::live_icon(0x22, green) == 57);
    item_icons::service(&c);
    assert(item_icons::pending_icons().empty());
    assert(item_icons::live_icon(0x22, red) == 57);
    item_icons::service(&c);
    const Bytes now57 = image_in_memory(files, item_icons::icon_file(57)), want_red = icon(item_icons::picture(red));
    assert(std::memcmp(now57.data(), want_red.data(), want_red.size() - 0x28) == 0);
    // write_texture refuses another size, a missing file and no archive
    assert(!item_icons::write_texture(kArchive, item_icons::icon_file(57), lyt_build::texture(Bytes(64 * 64 * 4, 0), 64, 64, "rgba8", "tiled", 0)));
    assert(!item_icons::write_texture(kArchive, "timg/none.bflim", icon(empty)));
    assert(!item_icons::write_texture(0, item_icons::icon_file(57), icon(empty)));
    std::filesystem::remove_all(dir);
    std::puts("item_icons_live_test: ok");
    return 0;
}
