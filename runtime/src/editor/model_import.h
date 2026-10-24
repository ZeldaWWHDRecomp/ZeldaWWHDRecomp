// The world editor's model import (issue #280): a static .glb as Blender and other exporters write it, made to fit the
// game's model rules (static_models::validate_glb) where that can be done without the mod maker going back to Blender.
//
// What it does, each with a note for the mod maker:
// - several mesh objects (Blender's "name.001" copies, a model of parts): merged into one model, their node transforms
//   applied; a mesh object named "*_col", or one whose materials are all col_* surfaces, stays collision;
// - triangle strips and fans become triangles; loose edges and points (lines and points primitives) are left out;
// - vertex colours, second UV sets, tangents and other attributes are left out (the model draws without them);
// - a blended material (alphaMode BLEND) becomes a cut-out (MASK at 0.5) when its texture has alpha, else opaque;
// - normal, occlusion, emissive and metal/roughness textures and optional extensions are left out;
// - textures: a JPEG or another PNG kind is converted, sizes the game can't take are rounded (width a multiple of 64,
//   height of 8, at most 512), and the model's textures are scaled down together until they fit in 1 MiB.
// A file that already fits is kept byte for byte. Skinned models (skins, animations) and required extensions are left
// to the validator, which says what to change.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace editor::model_import {
struct Result {
    std::vector<uint8_t> glb;        // the file as the package keeps it
    std::vector<std::string> notes;  // what was changed, one sentence each
    std::vector<std::string> warnings;  // what looks wrong but is kept as it is (normals pointing into the model)
    bool changed = false;            // false: `glb` is the input
};
// False (with `why`) for a file that isn't a GLB 2.0 the importer can read; `out` then holds the input unchanged.
bool fix(std::span<const uint8_t> input, Result& out, std::string& why);
// The texture sizes the game takes for images of `widths` x `heights`: each at most 512 a side, its width a multiple of
// 64 and its height of 8, all of them together at most `budget` bytes of RGBA8 (halving the largest first).
void texture_sizes(const std::vector<unsigned>& widths, const std::vector<unsigned>& heights, size_t budget, std::vector<unsigned>& out_widths,
                   std::vector<unsigned>& out_heights);
// A kit model's file with the textures of its game materials removed (issue #281): Blender keeps a material's image for
// its preview, but a material named after a kit room's draws the game's own texture, and a package never carries the
// game's pictures. The materials `strip` marks (by index) lose their texture slots (base colour, metal/roughness,
// normal, occlusion, emissive); the textures, images and binary data nothing uses any more go with them. `removed`:
// how many of those materials had a texture. False (with `why`) for a file it can't read; `out` is the input when
// nothing was removed.
bool strip_material_textures(std::span<const uint8_t> input, const std::vector<bool>& strip, std::vector<uint8_t>& out, unsigned& removed, std::string& why);
}  // namespace editor::model_import
