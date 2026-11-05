# TWWHD Randomizer in this project

TWWHD Randomizer by SuperDude88 and contributors (MIT, LICENSE.txt), release 1.2.0 (commit f8d9535,
https://github.com/SuperDude88/TWWHD-Randomizer). Built into libwwhdrando.so (CMakeLists.txt, target
wwhdrando) with runtime/src/android/rando_jni.cpp; the app's Game > Randomizer runs it in a process of
its own (RandoService). Its output folder (changed content files + code/cking.rpx) is played through
packs::set_randomizer (runtime/src/packs.cpp).

Left out: gui/ (Qt and Wii U), platform/, nuspack/ (Wii U channel), main.cpp, logic/tests, libs/AES (nuspack only),
libs/zlib-ng (the app's zlib, libs/zlib-ng.hpp), the libraries' build files, tests and docs.

Link's colours (2026-11-04): customizer/data/Link/color_masks and color_preview as in 1.2.0, for the app's
Game > Randomizer > Link's colours (rando_jni.cpp colors/setColor/setCasual/loadPreset/randomColors; the preview in
LinkColors.java as gui/desktop/player_customization.cpp draws it).

Changes (all marked "WWHD"):
- command/RandoSession.cpp: the output's folders are made when a file is written (only changed files
  go to the output, no copy of the whole game).
- gui/desktop/update_dialog_header.hpp: progress to the app (wwhd_rando::progress_value / _label).
- utility/platform.cpp: platformLog goes to the app.
- utility/path.cpp/.hpp: data and save folder from wwhd_rando::data_path / save_path.
- randomizer.cpp/.hpp: wwhdRandomize(config), one run with the app's config.
- libs/zlib-ng.hpp: zng_* calls mapped to zlib.
- seedgen/config.hpp: PermalinkErrorGetName declared with the type its definition has.

EUR and JPN releases (2026-10-30):
- asm_eur/, asm_jpn/: the patches ported by tools/rando/port_rando.py (patch_diffs, custom_symbols.yaml,
  address_map.txt for the C++ addresses; port_report.txt: every mapped site). The tool maps `.org`
  targets, literal branch targets and asm/linker.ld symbols, checks that each replaced instruction or
  data is the same in that release (branches and relocated fields by their mapped targets), learns
  moved structure fields from the game's own code (tools/rando/offsets.py; JPN: 0x5B60/0x5B64 are
  4 lower), checks the data tables the C++ code indexes, and assembles with asm/assemble.py (free
  space after that release's .text). GNU binutils for PowerPC stand in for devkitPPC (notes).
- utility/wwhd_region.cpp/.hpp: the release from meta.xml, port() for USA addresses, .rela.text
  offsets and .rodata offsets, the language packs (each release's packs with their own language),
  replace_found.
- filetypes/util/elfUtil.cpp: AddressToOffset ports USA addresses; AddressToOffsetRaw for the diffs.
- tweaks.cpp: region asm folder; pack names per release; language loops skip languages without a
  pack; .data start from the section table; .rodata pointers, relocation targets and addends ported;
  USA-only text fixes and JPN's own title sparkle values kept; text edits that look for a phrase leave
  the message as it is where the release words it otherwise.
- randomizer.cpp: the release checks left to the app (set before the run).

German, Italian and Japanese texts (2026-10-31; the same changes are offered upstream with the EUR/JPN
port, branch region-support in the clone outside this repo):
- utility/text.cpp: supported_languages has all six; word_wrap_string breaks Japanese between wide
  characters (two columns each; English, Spanish and French wrap as before).
- utility/string.cpp: toUTF8's buffer holds 3 bytes per UTF-16 unit (it cut names with non-ASCII
  letters, e.g. Spanish cryptic names lost their closing color).
- logic/World.cpp: a language a data file has no entry for gets the English one.
- logic/data/item_data.yaml, area_names.yaml, location_data.yaml: German, Italian and Japanese
  names (the games' own item, island, dungeon, character and enemy names).
- logic/Hints.cpp/.hpp, logic/Location.cpp, text_replacements.cpp, tweaks.cpp: the hints and the
  randomizer's messages in the three languages, worded so names keep their nominative form (German
  "Man sagt, ... sei hier zu finden: ..."); chart numbers go into the item name's color span; Japanese
  counts ("３０本") and the Hero Mode multiplier in full-width digits.

## In ZeldaWWHDRecomp (tools/rando)

Copied from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0) runtime/third_party/twwhd_rando at db65d60, with
everything above. The upstream files keep their MIT license (LICENSE.txt); the changes marked "WWHD" are the fork's
and this project's (MPL-2.0). Built by tools/rando/CMakeLists.txt into the command wwhd-rando (tools/rando/cli/main.cpp,
adapted from the fork's runtime/src/rando/rando_jni.cpp); the game runtime does not link it.

Left out besides the above:
- assets/ (the title logo, subtitle, mask, the meta icon, the key bag, the Tingle statue icons and the key chest
  textures): edited game art, which this project does not ship. tweaks.cpp leaves out what uses them when
  WWHD_NO_GAME_ART is defined (always, except the golden test build WWHD_RANDO_UPSTREAM_ART): the title logo's
  textures and its layout's size and position changes (the "Ver" text stays), meta/iconTex.tga, the key bag icon
  (its texts stay), the Tingle statue icons and the CTMC key chest texture. Content files with those textures differ
  from what the official 1.2.0 makes; the RPX holds the same code and data.
- customizer/data/Link/color_masks and color_preview (the same reason). Link's colours other than the defaults are
  not applied (customizer/model.cpp skips a colour whose mask is missing); the settings and presets yaml stay.
- libs/hash-library and libs/hashing.hpp (no license file; nothing built here used them).
- libs/zlib-ng: the system zlib instead (libs/zlib-ng.hpp, as in the fork). The RPX's sections are the same as the
  official build's once inflated, but zlib packs them into other bytes, so the file's SHA-256 is not the official one.

Changes here (marked "WWHD"):
- filetypes/yaz0.cpp yaz0Encode: the output buffer has room for the encoder's closing flag byte (it wrote up to 2
  bytes past the old bound), and the work buffer has 16 zero bytes before it (encode's second search() runs with
  pos -1 and reads the byte before the buffer). Both crashed on macOS at random (SIGBUS) when a buffer touched an
  unmapped page; upstream has the same code.
