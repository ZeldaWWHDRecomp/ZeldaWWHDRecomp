// Story UI layouts: the port's own writer of game layout archives (Layout/<name>.szs) from a layout
// description (JSON) and the package's PNG art, built on the player's machine when a package that has a
// layout loads. It mirrors the reference generator tools/story_mods/lyt (Python, docs in its modules):
// for the same description and art both write the same bytes (tools/story_mods/test_lyt.py runs
// lyt_build_test against the Python output).
//
// Supported: the description format of tools/story_mods/lyt/schema.json with RGBA8, BC1 and BC3 textures
// (encoded here with the generator's integer coders, tile modes linear, 1d, tiled and the swizzle), "fill"
// textures and PNG art at its own size. Not supported here (the description is refused, the caller falls
// back): BC4 / BC5 and resizing art ("width" / "height" other than the PNG's own size).
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace lyt_build {

using Bytes = std::vector<uint8_t>;

// The archive's files: blyt/<name>.bflyt, timg/<texture>^l.bflim, anim/<name>_<animation>.bflan.
// art_root: where the description's "png" paths are (the package folder). Throws std::runtime_error
// with a short reason for a description it can't write.
std::map<std::string, Bytes> build_files(const std::string& description_json, const std::filesystem::path& art_root);
// The archive: a Yaz0 (stored) SARC of build_files(), as tools/story_mods/notebook_model.py writes it.
// Refused (std::runtime_error) above 12 MB: the game's layout cache crashes on archives near 16 MB.
Bytes build(const std::string& description_json, const std::filesystem::path& art_root);
// RGBA8 texels (w x h) placed as a .bflim's image of that size, tile mode (1 linear, 2 1d, 4 tiled) and
// swizzle (0-7) would hold them (the image's size: as the archive's .bflim); for writing a texture of a
// built layout at run time (story_lyt_screen::set_texture_pixels).
Bytes tile_rgba8(const Bytes& rgba, uint32_t w, uint32_t h, int tile_mode, uint32_t swizzle);
// The inverse of the tiling above: a GX2 surface (ew x eh elements of bpp bits; texels, or 4 x 4 blocks
// for BC) in tile mode 1, 2 or 4 with swizzle 0-7, as row-major elements; empty for an unsupported or short
// surface. For reading the game's own layout textures (game_layouts.h).
Bytes untile(const Bytes& surface, uint32_t ew, uint32_t eh, uint32_t bpp, int mode, uint32_t swizzle);
// A .bflim of RGBA texels (w x h): format rgba8, bc1, bc3 or bc5la (luminance + alpha, for item_icons.h),
// tile "linear", "1d" or "tiled", swizzle 0-7 (tiled only); as the generator writes it. Throws
// std::runtime_error on bad input.
Bytes texture(const Bytes& rgba, uint32_t w, uint32_t h, const std::string& format, const std::string& tile, uint32_t swizzle);
// A SARC of the files (names sorted by their hash, data aligned), as the archives above.
Bytes sarc_of(const std::map<std::string, Bytes>& files, uint32_t align = 0x2000);
// Data as a Yaz0 stream of literal bytes only (no compression), as the archives above.
Bytes yaz0_store(const Bytes& data);
// The layout name ("name") of a description, or "" if it has none.
std::string layout_name(const std::string& description_json);

// Builds the archive for a description file once and keeps it in cache_dir, named after a hash of the
// description, the art it uses and this writer's version: the path of the archive, or "" (logged) if
// the description can't be built.
std::string build_cached(const std::filesystem::path& description, const std::filesystem::path& cache_dir);

}  // namespace lyt_build
