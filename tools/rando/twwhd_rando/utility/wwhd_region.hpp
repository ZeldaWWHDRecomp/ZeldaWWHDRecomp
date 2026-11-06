#pragma once

// WWHD: the randomizer on the EUR and JPN releases. The C++ code keeps the USA addresses it was written with; port()
// gives the release's own (asm_<region>/address_map.txt, made by tools/rando/port_rando.py), the release's patch diffs
// and custom symbols are in asm_<region>/, and the text goes into the release's language packs.
#include <cstdint>
#include <string>
#include <vector>

#include <filetypes/msbt.hpp>
#include <utility/path.hpp>

namespace wwhd_rando {

enum class Region { USA, EUR, JPN };

// from the game's meta.xml title id; false (and `err`) for an unknown game or a release without data
bool set_region(const fspath& gameBaseDir, std::string& err);
Region region();
const char* region_name();

// the folder with patch_diffs/ and custom_symbols.yaml for this release
fspath asm_dir();

// USA address -> this release's; the randomizer's own code area (after the USA .text) stays. An address without a
// mapping is logged, fails the run (port_failed) and comes back unchanged.
uint32_t port(uint32_t usa);
// a byte offset into .rela.text (USA) -> this release's
uint32_t port_rela_text(uint32_t usaOffset);
// a .rodata offset (relocation addend) -> this release's
uint32_t port_rodata_offset(uint32_t usaOffset);
bool port_failed();
const std::string& port_errors();

// language packs: {pack file name part ("UsEnglish", "EuGerman", ...), text language ("English", "German", ...)}
struct Pack {
    std::string pack, language;
};
const std::vector<Pack>& packs();
// the pack for a text language ("English" -> "EuEnglish"), "" if this release has none for it
std::string pack_for(const std::string& language);
// `what` in `message` replaced by `with`; false (message as it is) when the release's text doesn't have it
bool replace_found(std::u16string& message, const std::u16string& what, const std::u16string& with);

}  // namespace wwhd_rando
