// SPDX-License-Identifier: MPL-2.0
// World editor: the names behind the demo step's pickers (sounds, particles, actors, Link's modes). Pure
// tables, no game data: the sound and particle names are the decompilation's source-code names with our
// own group words; the actors are our own short list of characters a demo commonly uses (the editor adds
// the NPC catalogue's residents and the game objects at run time). Anything not listed can still be
// entered by its number or name.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace editor {

struct NamedId {
    uint32_t id;
    const char* group;  // ours: "object", "voice", "Link", ... (sounds); "everywhere" or "stage" (particles)
    const char* name;   // readable words made from the source name
    const char* code;   // the source-code name (JA_SE_..., ID_...)
};
std::span<const NamedId> sound_names();
std::span<const NamedId> particle_names();
// "object: fall water s (0x6918)"; for an id not in the list, "0x1234"
std::string sound_label(uint32_t id);
std::string particle_label(uint32_t id);

// A game actor a demo can name: its stage actor name, our words for it, its object archive when we know
// it ("" when unknown: the editor then tries the stage name), the id the editor suggests in a demo.
struct DemoActorName {
    std::string actor, label, archive, id, group;
};
const std::vector<DemoActorName>& demo_actor_names();

// Link's demo modes by name (demo_scene::link_mode), in the order the picker shows them.
std::span<const char* const> link_mode_names();

// An id for a new demo actor made from `base` ("Helmaroc King" -> "helmaroc-king"), unique among `taken`
// ("gull", "gull-2", ...); at most 24 characters, a-z, 0-9 and '-'.
std::string suggest_demo_id(const std::string& base, const std::vector<std::string>& taken);

// Case-insensitive: every space-separated word of `query` is in `text`.
bool matches_query(const std::string& text, const std::string& query);

}  // namespace editor
