#include "utility/wwhd_region.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include <command/Log.hpp>
#include <filetypes/msbt.hpp>
#include <libs/tinyxml2.hpp>
#include <utility/file.hpp>
#include <utility/platform.hpp>
#include <utility/text.hpp>

using namespace std::literals;

namespace wwhd_rando {
namespace {

Region g_region = Region::USA;
std::map<uint32_t, uint32_t> g_at, g_rela;  // explicit addresses, .rela.text offsets
std::map<uint32_t, int32_t> g_data;         // data: from this USA address on, this offset (until the next)
struct Func {
    uint32_t other, size;
};
std::map<uint32_t, Func> g_funcs;  // matched functions: USA entry -> this release's entry, size
uint32_t g_usaTextEnd = 0x028F87F4, g_textEnd = 0x028F87F4;
bool g_failed = false;
std::string g_errors;
std::vector<Pack> g_packs;

void fail(const std::string& what) {
    g_failed = true;
    if (g_errors.size() < 2000) g_errors += what + "\n";
    ErrorLog::getInstance().log(what);
}

bool load_map(const fspath& path, std::string& err) {
    std::ifstream f(path);
    if (!f) return err = "missing " + path.string(), false;
    g_at.clear(), g_rela.clear(), g_data.clear(), g_funcs.clear();
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string kind, a, b, c;
        in >> kind >> a >> b >> c;
        auto hex = [](const std::string& s) { return (uint32_t)std::stoul(s, nullptr, 16); };
        if (kind == "at") g_at[hex(a)] = hex(b);
        else if (kind == "rela") g_rela[hex(a)] = hex(b);
        else if (kind == "data") g_data[hex(a)] = (int32_t)std::stol(b, nullptr, 16);
        else if (kind == "func") g_funcs[hex(a)] = {hex(b), hex(c)};
        else if (kind == "text_end") g_usaTextEnd = hex(a), g_textEnd = hex(b);
    }
    if (g_at.empty() || g_funcs.empty()) return err = "empty address map " + path.string(), false;
    return true;
}

}  // namespace

bool set_region(const fspath& gameBaseDir, std::string& err) {
    tinyxml2::XMLDocument meta;
    if (LoadXML(meta, gameBaseDir / "meta" / "meta.xml") != tinyxml2::XMLError::XML_SUCCESS || !meta.RootElement() ||
        !meta.RootElement()->FirstChildElement("title_id"))
        return err = "cannot read the game's meta/meta.xml", false;
    const std::string id = meta.RootElement()->FirstChildElement("title_id")->GetText();
    g_failed = false;
    g_errors.clear();
    if (id == "0005000010143500") {
        g_region = Region::USA;
        g_packs = {{"UsEnglish", "English"}, {"UsSpanish", "Spanish"}, {"UsFrench", "French"}};
        return true;
    }
    if (id == "0005000010143600") {
        g_region = Region::EUR;
        g_packs = {{"EuEnglish", "English"}, {"EuSpanish", "Spanish"}, {"EuFrench", "French"}, {"EuGerman", "German"}, {"EuItalian", "Italian"}};
    } else if (id == "0005000010143400") {
        g_region = Region::JPN;
        g_packs = {{"JpJapanese", "Japanese"}};
    } else {
        return err = "not a known release of the game (title id " + id + ")", false;
    }
    return load_map(asm_dir() / "address_map.txt", err);
}

Region region() { return g_region; }
const char* region_name() { return g_region == Region::USA ? "USA" : g_region == Region::EUR ? "EUR" : "JPN"; }

fspath asm_dir() {
    switch (g_region) {
        case Region::EUR: return Utility::get_data_path() / "asm_eur";
        case Region::JPN: return Utility::get_data_path() / "asm_jpn";
        default: return Utility::get_data_path() / "asm";
    }
}

uint32_t port(uint32_t usa) {
    if (g_region == Region::USA) return usa;
    if (auto it = g_at.find(usa); it != g_at.end()) return it->second;
    if (usa >= g_usaTextEnd && usa < 0x03000000) return usa;  // the randomizer's own code (this release's addresses)
    if (usa >= 0x02000000 && usa < g_usaTextEnd) {
        auto it = g_funcs.upper_bound(usa);
        if (it != g_funcs.begin()) {
            --it;
            if (usa < it->first + it->second.size) return it->second.other + (usa - it->first);
        }
    } else if (usa >= 0x10000000 && usa < 0x10800000) {
        auto it = g_data.upper_bound(usa);
        if (it != g_data.begin()) return (uint32_t)((int64_t)usa + std::prev(it)->second);
    }
    char t[96];
    snprintf(t, sizeof t, "no %s address for USA %08X", region_name(), usa);
    fail(t);
    return usa;
}

uint32_t port_rela_text(uint32_t usaOffset) {
    if (g_region == Region::USA) return usaOffset;
    if (auto it = g_rela.find(usaOffset); it != g_rela.end()) return it->second;
    char t[96];
    snprintf(t, sizeof t, "no %s .rela.text entry for USA +%X", region_name(), usaOffset);
    fail(t);
    return usaOffset;
}

uint32_t port_rodata_offset(uint32_t usaOffset) { return port(0x10000000 + usaOffset) - 0x10000000; }

bool replace_found(std::u16string& message, const std::u16string& what, const std::u16string& with) {
    const size_t at = message.find(what);
    if (at == std::u16string::npos) return false;
    message.replace(at, what.size(), with);
    return true;
}

bool port_failed() { return g_failed; }
const std::string& port_errors() { return g_errors; }

const std::vector<Pack>& packs() { return g_packs; }

std::string pack_for(const std::string& language) {
    for (const Pack& p : g_packs)
        if (p.language == language) return p.pack;  // (the first: EuEnglish for English)
    return "";
}

}  // namespace wwhd_rando
