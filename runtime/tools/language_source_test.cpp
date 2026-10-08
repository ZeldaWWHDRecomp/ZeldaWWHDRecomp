// Language sources (game_languages.h, docs/language-packs.md) on a SYNTHETIC game folder: dummy files
// named like the game's language packs (a "SARC" tag and a few made-up bytes; no game data). Checks the
// packs found in data/game-lang/<region>/content/Common/Pack, the start chosen for a language and a
// region, and that the real guest file system HLE reads the source's pack under the name the game
// asks for (open, read, stat by path and handle) while everything else stays as it was.
#include "game_languages.h"
#include "runtime.h"
#include "savestate.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
// hle/fs.cpp reports opened language packs to the right-to-left text support (not under test)
namespace rtl_text { void language_pack_opened(const std::string&) {} }
namespace fs = std::filesystem;
namespace {
std::map<std::string, PpcFunc>& functions() {
    static std::map<std::string, PpcFunc> f;
    return f;
}
constexpr uint32_t base = 0x10000000;
}  // namespace
HleReg::HleReg(const char* lib, const char* name, PpcFunc fn) { functions()[std::string(lib) + ":" + name] = fn; }
bool g_trace_hle = false;
void log_msg(const char*, ...) {}
namespace config { std::string game_dir, save_dir; }
namespace mem {
std::string read_cstr(uint32_t ea) { return reinterpret_cast<const char*>(ptr(ea)); }
void write_cstr(uint32_t ea, const std::string& s, uint32_t max) {
    if (max) {
        auto n = std::min<size_t>(max - 1, s.size());
        memcpy(ptr(ea), s.data(), n);
        ptr(ea)[n] = 0;
    }
}
}  // namespace mem
namespace threads { void block_begin() {} void block_end() {} }
namespace wwatch { void host_write_begin(uint32_t, uint32_t) {} void host_write_end(uint32_t, uint32_t) {} }

static int32_t call(const char* name, std::initializer_list<uint32_t> args) {
    Cpu c{};
    size_t i = 3;
    for (auto a : args) c.r[i++] = a;
    functions().at(name)(&c);
    return int32_t(c.r[3]);
}
static int32_t try_open(const std::string& path, const std::string& mode, uint32_t* handle) {
    mem::write_cstr(base, path, 1024);
    mem::write_cstr(base + 1024, mode, 32);
    int32_t r = call("coreinit:FSOpenFile", {0, 0, base, base + 1024, base + 1100, 0});
    *handle = ld32(base + 1100);
    return r;
}
static std::string read_all(const std::string& path) {
    uint32_t h = 0;
    assert(try_open(path, "rb", &h) == 0);
    auto n = call("coreinit:FSReadFile", {0, 0, base + 8192, 1, 4096, h, 0});
    assert(n >= 0);
    std::string s(reinterpret_cast<char*>(mem::ptr(base + 8192)), size_t(n));
    assert(call("coreinit:FSGetStatFile", {0, 0, h, base + 2048, 0}) == 0);
    assert(ld32(base + 2048 + 0x10) == s.size());
    assert(call("coreinit:FSCloseFile", {0, 0, h}) == 0);
    mem::write_cstr(base, path, 1024);
    assert(call("coreinit:FSGetStat", {0, 0, base, base + 2048, 0}) == 0);
    assert(ld32(base + 2048 + 0x10) == s.size());
    return s;
}
static void put(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
}

int main() {
    auto address = mem::ptr(base);
    constexpr size_t memory_size = 65536;
#ifdef _WIN32
    auto memory = VirtualAlloc(address, memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto memory = mmap(address, memory_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(memory == address);
    const fs::path root = fs::temp_directory_path() /
                          ("wwhd-langsrc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    // the installed (USA) game: three packs; a European source with five, one of them also named like a
    // USA pack (ignored), one without the SARC tag (ignored); a Japanese source in another folder name case
    const fs::path game = root / "data" / "game", lang = root / "data" / "game-lang";
    put(game / "content" / "Common" / "Pack" / "permanent_2d_UsEnglish.pack", "SARC synthetic us-en");
    put(game / "content" / "Common" / "Pack" / "permanent_2d_UsFrench.pack", "SARC synthetic us-fr");
    put(game / "content" / "Common" / "Pack" / "permanent_2d_UsSpanish.pack", "SARC synthetic us-es");
    put(game / "content" / "Common" / "Pack" / "permanent_3d.pack", "SARC synthetic 3d");
    put(game / "code" / "app.xml", "synthetic");
    const fs::path eu = lang / "EU" / "content" / "Common" / "Pack";
    for (const char* l : {"English", "French", "German", "Italian", "Spanish"})
        put(eu / (std::string("permanent_2d_Eu") + l + ".pack"), std::string("SARC synthetic eu-") + l);
    put(eu / "permanent_2d_UsEnglish.pack", "SARC synthetic not used");
    put(eu / "permanent_2d_EuRussian.pack", "SARC unknown to the game");
    put(lang / "JP" / "CONTENT" / "common" / "PACK" / "permanent_2d_jpjapanese.pack", "SARC synthetic jp-ja");
    put(lang / "XX" / "content" / "Common" / "Pack" / "permanent_2d_EuGerman.pack", "SARC duplicate");
    put(lang / "bad" / "content" / "Common" / "Pack" / "permanent_2d_EuDutch.pack", "SARC unknown");
    config::game_dir = game.string();
    config::save_dir = (root / "save").string();
    fs::create_directories(root / "save");
#ifdef _WIN32
    _putenv_s("WWHD_LANG_DIR", "");
#else
    unsetenv("WWHD_LANG_DIR");
#endif

    assert(fs::path(game_lang::sources_dir()) == lang);
    assert((game_lang::available() == std::vector<int>{1, 2, 5}));
    assert(game_lang::region() == "USA");
    const auto& packs = game_lang::source_packs();
    assert(packs.size() == 6);  // five European (the EU folder's German first: folders in name order), one Japanese
    assert((game_lang::source_languages(game_lang::kEurope) == std::vector<int>{1, 2, 3, 4, 5}));
    assert((game_lang::source_languages(game_lang::kJapan) == std::vector<int>{0}));
    assert(game_lang::source_languages(game_lang::kUsa).empty());
    const game_lang::Pack* de = game_lang::source_pack(3, game_lang::kEurope);
    assert(de && de->file == "permanent_2d_EuGerman.pack" && de->source && fs::path(de->host).parent_path() == eu);
    assert(game_lang::source_pack(0, game_lang::kJapan)->file == "permanent_2d_JpJapanese.pack");
    assert(!game_lang::source_pack(3, game_lang::kJapan) && !game_lang::source_pack(1, game_lang::kUsa));

    // starts: a source language, a language no source has, the installed game
    game_lang::Start s = game_lang::choose(3, game_lang::kEurope);
    assert(s.pack == de && s.language == 3 && s.region == game_lang::kEurope);
    s = game_lang::choose(1, game_lang::kEurope);
    assert(s.pack && s.pack->file == "permanent_2d_EuEnglish.pack");
    s = game_lang::choose(10, game_lang::kEurope);  // Russian: no pack anywhere
    assert(!s.pack && s.language == 1);
    s = game_lang::choose(0, game_lang::kNoRegion);  // Japanese without the source region: not on this disc
    assert(!s.pack && s.language == 1);
    s = game_lang::choose(0, game_lang::kJapan);
    assert(s.pack && s.language == 0 && s.region == game_lang::kJapan);
    s = game_lang::choose(3, game_lang::kUsa);  // "us" is the installed game
    assert(!s.pack && s.language == 1);

    // before a start, and with the installed game's own text, nothing is redirected
    const std::string guest_de = "/vol/content/Common/Pack/permanent_2d_EuGerman.pack";
    uint32_t h = 0;
    assert(game_lang::redirect(guest_de).empty());
    assert(try_open(guest_de, "rb", &h) != 0);  // the USA game has no such file
    game_lang::set_started(2);
    assert(game_lang::started() == 2 && !game_lang::current().pack && game_lang::redirect(guest_de).empty());
    assert(read_all("/vol/content/Common/Pack/permanent_2d_UsFrench.pack") == "SARC synthetic us-fr");

    // German from the European source: the game's own name for it, any case, relative or absolute
    game_lang::begin(game_lang::choose(3, game_lang::kEurope));
    assert(game_lang::started() == 3 && game_lang::current().region == game_lang::kEurope);
    assert(read_all(guest_de) == "SARC synthetic eu-German");
    assert(read_all("/vol/content/common/pack/PERMANENT_2D_EUGERMAN.PACK") == "SARC synthetic eu-German");
    assert(read_all("Common/Pack/permanent_2d_EuGerman.pack") == "SARC synthetic eu-German");
    assert(game_lang::redirect("/vol/content/Common/Pack/xpermanent_2d_EuGerman.pack").empty());
    assert(game_lang::redirect("/vol/content/Common/Pack/permanent_2d_EuFrench.pack").empty());  // not chosen
    // the installed game's files are untouched
    assert(read_all("/vol/content/Common/Pack/permanent_2d_UsEnglish.pack") == "SARC synthetic us-en");
    assert(read_all("/vol/content/Common/Pack/permanent_3d.pack") == "SARC synthetic 3d");
    // writers never reach the language source
    assert(try_open(guest_de, "r+b", &h) != 0);
    {
        std::ifstream in(de->host, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(in), {}};
        assert(bytes == "SARC synthetic eu-German");
    }

    // Japanese from the Japanese source
    game_lang::begin(game_lang::choose(0, game_lang::kJapan));
    assert(read_all("/vol/content/Common/Pack/permanent_2d_JpJapanese.pack") == "SARC synthetic jp-ja");
    assert(game_lang::redirect(guest_de).empty());

    // the save options' language byte (GameCube PAL order)
    assert(game_lang::options_language(1) == 0 && game_lang::options_language(3) == 1 &&
           game_lang::options_language(2) == 2 && game_lang::options_language(5) == 3 &&
           game_lang::options_language(4) == 4 && game_lang::options_language(0) == 0);

    fs::remove_all(root);
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, memory_size);
#endif
    std::cout << "language_source_test: synthetic European/Japanese packs found, chosen and read through the guest FS\n";
}
