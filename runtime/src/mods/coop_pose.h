// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op (docs/online-coop.md): Link's pose as he is drawn, and puppets that show another player's pose.
// A pose is every joint matrix of every model Link drew in one logic step, relative to his place and heading.
// A puppet is our own copy of each of Link's models (made beside his, on his heap, as he is created), its joint
// matrices written from a pose and entered for drawing after him: no player logic, no process of its own.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Cpu;

namespace mods::coop {

// puppets: other players drawn (the direct transport and a party use 3; strangers more, GAPS C11); each made only
// when shown (its kit on a heap of its own over the port's memory)
constexpr int kPuppets = 31;  // (32 players an instance: GAPS C11)

struct Joint { int16_t q[4], t[3]; uint8_t s; };  // rotation (x32767), translation (x8), uniform scale (x64)
constexpr uint8_t kHeld = 0xFF;  // a model's k: one Link makes as he takes it out (his sword, an item), by its file
struct Model {
    uint8_t k = 0;            // Link's model by creation order, or kHeld
    uint16_t file = 0;        // kHeld: its file in Link's archive, made with these differed-list flags
    uint32_t diff = 0;
    uint32_t shown = ~0u;     // its materials shown (the sheath's blade...)
    float brk = -1;           // kHeld: a bomb's fuse warning, the frame of Link's bomb brk it drew with (-1: none)
    std::vector<Joint> j;
};
struct Pose {
    char stage[8] = {};
    int8_t room = 0;
    uint8_t flags = 0;           // kOnDeck: pos is ship-local (step 3)
    float pos[3] = {};
    int16_t yaw = 0;
    uint16_t proc = 0, equip = 0;
    std::vector<Model> models;
    // the sword's trail (daPy_swBlur_c, Link +0x73EC) while it shows: its fade, index, colour, top rate, then 60 base
    // and 60 tip points relative to his place and heading (x4)
    int32_t trail = 0, trail_idx = 0, trail_color = 0;
    float trail_top = 0;
    std::vector<int16_t> trail_pts;
    // the hookshot's chain while it flies (daHookshot_c): its links, their root (Link +0x72F0) relative to his place and
    // heading (x4), their angle (x; y relative to his heading)
    uint16_t chain = 0;
    int16_t chain_root[3] = {}, chain_angle[2] = {};
    // the grappling hook's rope (daHimo2's line, +0x204C): its width and points relative to his place and heading (x4)
    float rope_width = 0;
    std::vector<int16_t> rope_pts;
};

// the bytes of a pose (the recorder's frame and the network's), and back
void pack(const Pose& p, std::string& out);
bool unpack(const uint8_t* d, size_t n, Pose& p, size_t* used = nullptr);

// a pose between two, t 0..1: the place, the heading the short way, and each joint of the models both have
void blend(const Pose& a, const Pose& b, float t, Pose& out);

// the real Link's pose from his last draw (step() asks for a new one each logic step); false until one is taken
bool own_pose(Pose& out);
// what puppet i shows: a pose drawn at a place and heading (nullptr: hidden), in a tunic (0 green, 1 red, 2 blue, 3 purple)
// see_through: drawn at half opacity, after the opaque world (a player outside the party, when the player chose so)
void show(int i, const Pose* p, const float pos[3], int16_t yaw, int tunic = 0, bool see_through = false);
// puppet i's player left: hidden, and its kit (its copies of Link's models, on a heap of its own) given back at the
// next step
void release(int i);
// a full save state was loaded (savestate.cpp, after the game's memory is put back): what the puppets keep in the game's
// memory is forgotten; they are drawn again once Link is made again
void state_loaded();
// this game's own Link's tunic while a session is on (-1: none, his own clothes)
void own_tunic(int tunic);
// who this game plays as (0 Link, 1 Tetra, 2 Medli, 3 Princess Zelda): drawn in Link's place here, and the others see
// it (a pose's flags, bits 4-5)
constexpr int kCharacters = 4;
constexpr const char* kCharacterNames[kCharacters] = {"Link", "Tetra", "Medli", "Zelda"};
void own_character(int ch);
int own_char();
// a sound effect Link's voice starts (JA_SE_LK_V_*: 0x1800 + the variant daPy_lk_c::voiceStart picks) as character `ch`
// says it: hers (Medli's MD_*; Tetra and Zelda Zelda's ZL_*), 0 none (she has nothing for it); any other sound as it is
uint32_t char_voice(uint32_t id, int ch);
// a character's model whose view this draw already calculated (for her face, before her entry): its entry's view
// calc is skipped (interp.cpp's J3DModel::viewCalc hook)
bool view_done(uint32_t model);

// a particle effect Link started (dPa_control_c::set while he executes), relative to his place and heading
struct Effect { uint16_t id; int16_t at[3], angle[3]; };
void link_executes(uint32_t link);       // camera.cpp's daPy_Execute hook: Link (0 when he is done)
uint32_t executing();                    // Link while his execute runs, else 0
void particle(Cpu* c);                   // extras.cpp's dPa_control_c::set hook, before it runs
std::vector<Effect> take_effects();      // this step's, for the other players
void effect_at(Cpu* c, const Effect& e, const float pos[3], int16_t yaw);  // one of theirs, at a puppet

// each logic step (extras.cpp): the test aids WWHD_COOP_RECORD=<file> and WWHD_COOP_GHOST=<file>[@dx,dz]
void step(Cpu* c, uint32_t link, bool on);
// fopAc_Draw(actor) (interp.cpp's hook calls it in place of the original): Link's items into his pose, the puppets'
// hookshot chains
void actor_draw(Cpu* c);

}  // namespace mods::coop
