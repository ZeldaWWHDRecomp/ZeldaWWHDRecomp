// SPDX-License-Identifier: MPL-2.0
// The borrowed walks of the villagers without one (story_resident_types.h kBorrowedWalks), built from
// the player's own game files (read only; nothing is written or copied anywhere): the source villager's
// walk clip and both skeletons are read from their archives (the permanent packs or Object/<name>.szs)
// and retargeted (story_resident_walk_clip.h). Any thread.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace story_resident_walk_files {

using Bytes = std::vector<uint8_t>;
// A SARC's member `name` (by file name, any folder); empty when it has none or on bad data.
Bytes sarc_member(const Bytes& sarc, const std::string& name);
// Yaz0-decoded data (data that isn't Yaz0 comes back as it is; empty on bad data).
Bytes yaz0(const Bytes& data);
// The BFRES of archive `archive` (e.g. "Ub") in the game folder `game_dir`; empty when it isn't there.
Bytes archive_bfres(const std::string& game_dir, const std::string& archive);

struct Clip {
    Bytes fska;          // the native animation, its skeleton not yet bound (story_resident_walk_clip::kSkeletonField)
    unsigned frames = 0;  // its frame count
    double stride = 0;    // body units per cycle
    double largest_step = 0;
};
// Builds borrowed walk `kind` (an index into kBorrowedWalks); false with a reason.
bool build(const std::string& game_dir, unsigned kind, Clip& out, std::string& why);

}  // namespace story_resident_walk_files
