#pragma once
/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
// GameCube player models in Wind Waker HD: a player model pack made for the GameCube game (Link.arc, J3D
// models) converted into the game's own
// content/Common/Pack/permanent_3d.pack (Link.szs > Link.bfres). What the pack changed is found against the game's
// original (gcm_baseline.inc); those parts get the pack's meshes on HD's skeleton (HD's materials, shaders and
// animations stay), their textures upscaled to HD's sizes and formats. The mod manager's import calls it
// (mods/packages.cpp); the fork's tools/gcmodels has the PC tools this was worked out with.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gcmodel {

struct Result {
    std::string error;   // empty when done
    std::string report;  // a line per part and texture, for the pack's details
};

// arc: Link.arc (RARC, Yaz0 or not); pack: the game's permanent_3d.pack; linkSzs: the converted Link.szs (Yaz0),
// the one file of the pack that changes. progress(percent, step) now and then.
Result convert(const std::vector<uint8_t>& arc, const std::vector<uint8_t>& pack, std::vector<uint8_t>& linkSzs,
               const std::function<void(int, const std::string&)>& progress = {});
// the game's permanent_3d.pack with Link.szs replaced; empty and `error` set when it can't be done
std::vector<uint8_t> assemble(const std::vector<uint8_t>& pack, const std::vector<uint8_t>& linkSzs, std::string& error);
// Link.szs out of a permanent_3d.pack (empty if none)
std::vector<uint8_t> link_szs(const std::vector<uint8_t>& pack);

// What a Link.bfres holds, for tools and checks (no data): per model its bones (world translation of the bind
// pose), shapes (material, bone, skin weights) and materials (their textures)
struct PartInfo {
    struct Bone {
        std::string name;
        int parent = -1;
        double world[3] = {0, 0, 0};
    };
    struct Shape {
        std::string name;
        int material = 0, bone = 0, skin = 0;
    };
    std::string name;
    std::vector<Bone> bones;
    std::vector<Shape> shapes;
    std::vector<std::string> materials;
    std::vector<std::vector<std::string>> materialTextures;
};
std::vector<PartInfo> describe(const std::vector<uint8_t>& linkBfres);  // throws on a damaged file
// the GameCube name of an HD material (ear_2_ -> ear(2))
std::string gc_material(const std::string& hd);

}  // namespace gcmodel
