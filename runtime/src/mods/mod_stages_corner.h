// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <vector>
#include "mod_stages_manifest.h"
namespace mod_stages {
struct CornerMarker { float x, z; uint32_t kind; };
// Original current-room corner behavior: compass markers, unopened chests only.
inline std::vector<CornerMarker> corner_markers(const Stage& stage, unsigned room_no, bool compass, uint32_t chests) {
    std::vector<CornerMarker> result;
    if (!compass) return result;
    for (const auto& room : stage.rooms) {
        if (room.room != room_no) continue;
        for (const auto& actor : room.actors)
            if (actor.actor == "chest" && actor.flag < 32 && !(chests & (1u << actor.flag)))
                result.push_back({float(actor.position.x), float(actor.position.z), 1});
    }
    for (const auto& door : stage.doors)
        if (door.lock == "boss" && (door.a == room_no || door.b == room_no))
            result.push_back({float(door.position.x), float(door.position.z), 2});
    return result;
}
}
