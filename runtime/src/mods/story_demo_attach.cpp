// SPDX-License-Identifier: MPL-2.0
// Mod demos: one actor carried on another's joint (story_demo_parts.h).
//
// The game's own case is Demo07: daDk_Execute takes the Helmaroc King's jaw matrix (getAnmMtx of
// joint 25), adds an offset, and writes the result into the translation and rotation of Link's
// demo slot; Link's demo tool procedure takes his position and angles from that slot, with his
// demo byte +0x68DE at 0xC (the animation's root translation zeroed). Here any demo actor's slot
// takes the carrier's joint matrix times the attachment's offset, after dDemo_manager_c::update:
//  - game actors that read their demo slot (dDemo_setDemoData with translation and rotation) and
//    Link in his demo clips take it in their own execute;
//  - mod-placed actors take it right after (story_demo.cpp apply_characters).
// The carrier's joint matrices are from its last calc; its own movement since then (its demo slot's
// translation against its model's base matrix) is added, so a flying carrier doesn't leave its load
// a frame behind.
#include "story_demo_parts.h"

#include "demo_attach_math.h"

#include "guest_addr.h"
#include "mod_actor.h"
#include "runtime.h"

#include <cmath>
#include <cstring>

namespace story_demo_parts::attach {
namespace {
const uint32_t kPlay = GC(0x025200D4), kDemoActor = GC(0x02526E70), kJointTree = GC(0x027F68FC), kNameIndex = GC(0x027DF9B0);
const uint32_t kDemoPool = GD(0x101D5FFC);
const uint32_t kGroundCross = GC(0x02008974);  // dBgS::GroundCross(bgs, gndchk) -> f1
constexpr uint32_t kBgs = 0x12A0, kLinkGndChk = 0xB14, kGndChkBytes = 0x54;  // dComIfG_Bgsp(); daPy_lk_c::mGndChk (probe point +0x24)
constexpr float kGravity = -3.f, kTerminal = -50.f;
constexpr uint32_t kPlayer = 0x5B34, kLinkModel = 0x448, kLinkHeldMode = 0x68DE, kHeld = 0xC;
// record entry (0x20 each)
enum : uint32_t { State = 0x00, Carried = 0x04, CarriedId = 0x08, Carrier = 0x0C, CarrierId = 0x10, Joint = 0x14, Waited = 0x18, IsLink = 0x1C };
constexpr uint32_t Velocity = Waited;  // after the release: the falling speed
enum : uint32_t { Waiting = 0, Carrying = 1, Released = 2, Skipped = 3, Falling = 4 };
constexpr uint32_t kEntryBytes = 0x20, kMaxEntries = kRecordBytes / kEntryBytes, kWaitFrames = 30;

float fl(uint32_t a) { return u32_as_f32(ld32(a)); }
uint32_t link_actor(Cpu* c, const Context& x) { const uint32_t play = x.call(c, kPlay, {}); return play ? ld32(play + kPlayer) : 0; }
bool alive(uint32_t actor, uint32_t id) { return actor && id && ld32(actor + 4) == id; }
uint32_t slot_of(Cpu* c, const Context& x, uint32_t actor) {
    const uint32_t index = ld8(actor + 0x2DC), pool = ld32(kDemoPool);
    return index && index <= 32 && pool ? x.call(c, kDemoActor, {pool, index}) : 0;
}
// The model whose joints carry: the one the demo system knows (dDemo_actor_c +0x48, set by the
// actor's dDemo_setDemoData and by Link), else Link's or a mod actor's own.
uint32_t model_of(Cpu* c, const Context& x, uint32_t actor) {
    if (const uint32_t slot = slot_of(c, x, actor)) if (const uint32_t m = ld32(slot + 0x48)) return m;
    if (actor == link_actor(c, x)) return ld32(actor + kLinkModel);
    mod_actor::Identity id;
    return mod_actor::identify(c, actor, id) ? mod_actor::model_address(c, id) : 0;
}
// The model's anm (joint world) matrices. Reading them marks the buffer (+4 |= 0x10) as the game's
// own getAnmMtx does in HD (daDk_Execute in Demo07, dDemo_actor_c::setMatrix): without the mark the
// buffer isn't kept up to date for the CPU.
uint32_t anm_matrices(uint32_t model, uint32_t& count) {
    const uint32_t block = model ? ld32(model + 0x2C) : 0;
    count = block ? ld16(block + 0x2C) : 0;
    if (!block) return 0;
    const uint32_t matrices = ld32(block + 0x10);
    st16(block + 4, uint16_t(ld16(block + 4) | 0x10));
    return matrices;
}
// J3DModelData::getJointName()->getIndex(name) as HD inlines it (joint tree header +0x10: a
// self-relative offset to the JUTNameTab)
uint32_t name_table(Cpu* c, const Context& x, uint32_t model) {
    const uint32_t data = ld32(model + 0xAC);
    if (!data) return 0;
    const uint32_t tree = x.call(c, kJointTree, {data});
    const uint32_t off = tree ? ld32(tree + 0x10) : 0;
    return off ? tree + 0x10 + off : 0;
}
int32_t joint_index(Cpu* c, const Context& x, uint32_t model, const std::string& name) {
    const uint32_t table = name_table(c, x, model);
    if (!table) return -1;
    std::memset(mem::ptr(x.scratch), 0, 32);
    std::memcpy(mem::ptr(x.scratch), name.c_str(), std::min<size_t>(name.size(), 31));
    return int32_t(x.call(c, kNameIndex, {table, x.scratch}));
}
// for the log when a joint name is wrong: the model's joint names (JUTNameTab: u16 count, then
// {u16 key, u16 offset} entries; offsets from the table)
std::string joint_names(Cpu* c, const Context& x, uint32_t model) {
    const uint32_t table = name_table(c, x, model);
    if (!table) return "(none)";
    const uint32_t count = ld16(table);
    std::string out;
    for (uint32_t k = 0; k < count && k < 96; ++k) {
        const uint32_t at = table + ld16(table + 4 + k * 4 + 2);
        std::string n;
        for (uint32_t i = 0; i < 32; ++i) { const char ch = char(ld8(at + i)); if (!ch) break; n += ch; }
        out += (k ? " " : "") + n;
    }
    return out;
}

using demo_attach_math::M34;
using demo_attach_math::pose;
uint32_t entry(const Context& x, size_t i) { return x.record + uint32_t(i) * kEntryBytes; }
void stop(Cpu*, const Context& x, size_t i, uint32_t state) {
    const uint32_t e = entry(x, i), carried = ld32(e + Carried);
    if (alive(carried, ld32(e + CarriedId)) && ld32(e + IsLink)) st8(carried + kLinkHeldMode, 0);
    st32(e + State, state);
}
// After a detach the demo system would put the actor back on its track's current value (a track holds
// its last key; an actor without one is held at the demo's origin): until its next position key or its
// next attach, its slot takes the actor's own place and angles, so it stays where it was let go and its
// own code moves it on (a villager falls to the ground).
uint32_t follow_until(const demo_scene::Demo& d, size_t i) {
    const auto& a = d.attach[i];
    uint32_t until = demo_scene::kNoRelease;
    for (const auto& actor : d.actors)
        if (actor.id == a.actor)
            for (const auto& k : actor.position.keys) if (k.frame > a.release && k.frame < until) { until = k.frame; break; }
    for (size_t k = i + 1; k < d.attach.size(); ++k) if (d.attach[k].actor == a.actor && d.attach[k].frame < until) until = d.attach[k].frame;
    return until;
}
// The ground under a point: dBgS::GroundCross with Link's own ground check (as climb.cpp uses it).
// The whole check (cBgS_GndChk, 0x54 bytes: probe point, the polygon found and its room) is put back
// afterwards: Link takes his room from that polygon, so a stale one would move him to another room.
float ground(Cpu* c, const Context& x, float px, float py, float pz) {
    const uint32_t play = x.call(c, kPlay, {}), link = play ? ld32(play + kPlayer) : 0;
    if (!link || !x.raw) return -1e30f;
    const uint32_t check = link + kLinkGndChk, probe = check + 0x24;
    uint8_t saved[kGndChkBytes];
    std::memcpy(saved, mem::ptr(check), kGndChkBytes);
    st32(probe, f32_as_u32(px)); st32(probe + 4, f32_as_u32(py)); st32(probe + 8, f32_as_u32(pz));
    const Cpu cpu = *c;
    x.raw(c, kGroundCross, {play + kBgs, check});
    const float g = float(c->f[1].ps0);
    *c = cpu;
    std::memcpy(mem::ptr(check), saved, kGndChkBytes);
    return std::isfinite(g) ? g : -1e30f;
}
// a released actor with "fall": gravity until the ground under it
void fall(Cpu* c, const Context& x, uint32_t e) {
    const uint32_t actor = ld32(e + Carried);
    if (!alive(actor, ld32(e + CarriedId))) return;
    const float px = fl(actor + 0x314), py = fl(actor + 0x318), pz = fl(actor + 0x31C);
    const float v = std::fmax(kTerminal, u32_as_f32(ld32(e + Velocity)) + kGravity);
    const float g = ground(c, x, px, py + 50.f, pz);
    float y = py + v;
    if (g > -1e29f && y <= g) { y = g; st32(e + State, Released); }
    else st32(e + Velocity, f32_as_u32(v));
    for (uint32_t off : {0x314u, 0x300u}) st32(actor + off + 4, f32_as_u32(y));
}
void follow(Cpu* c, const Context& x, uint32_t e) {
    const uint32_t actor = ld32(e + Carried);
    if (!alive(actor, ld32(e + CarriedId))) return;
    const uint32_t slot = slot_of(c, x, actor);
    if (!slot) return;
    for (uint32_t k = 0; k < 12; k += 4) st32(slot + 8 + k, ld32(actor + 0x314 + k));
    for (uint32_t k = 0; k < 6; k += 2) st16(slot + 0x20 + k, ld16(actor + 0x328 + k));
    st16(slot + 4, uint16_t(ld16(slot + 4) | 0x0A));
}
}  // namespace

void begin(const Context& x) { std::memset(mem::ptr(x.record), 0, kRecordBytes); }

void update(Cpu* c, const Context& x, const demo_scene::Demo& d, uint32_t frame, const ActorOf& actor_of) {
    for (size_t i = 0; i < d.attach.size() && i < kMaxEntries; ++i) {
        const auto& a = d.attach[i];
        const uint32_t e = entry(x, i);
        uint32_t state = ld32(e + State);
        if (state == Waiting) {
            if (frame < a.frame) continue;
            if (frame >= a.release) { st32(e + State, Skipped); continue; }
            const uint32_t carried = actor_of(a.actor), carrier = actor_of(a.to);
            const uint32_t model = carrier ? model_of(c, x, carrier) : 0;
            if (!carried || !carrier || !model || !slot_of(c, x, carried)) {
                // a carrier's model reaches the demo system with its first demo execute: wait a little
                const uint32_t waited = ld32(e + Waited) + 1;
                st32(e + Waited, waited);
                if (waited >= kWaitFrames) {
                    LOG("[demo] attach %s to %s: %s; not carried", a.actor.c_str(), a.to.c_str(),
                        !carried ? "the carried actor is not here" : !carrier ? "the carrier is not here" : !model ? "the carrier has no joints the port can read" : "the carried actor has no demo slot");
                    st32(e + State, Skipped);
                }
                continue;
            }
            uint32_t count = 0;
            anm_matrices(model, count);
            const int32_t joint = joint_index(c, x, model, a.joint);
            if (joint < 0 || uint32_t(joint) >= count) {
                LOG("[demo] attach %s to %s: no joint \"%s\" (joints: %s); not carried", a.actor.c_str(), a.to.c_str(), a.joint.c_str(), joint_names(c, x, model).c_str());
                st32(e + State, Skipped);
                continue;
            }
            st32(e + Carried, carried); st32(e + CarriedId, ld32(carried + 4));
            st32(e + Carrier, carrier); st32(e + CarrierId, ld32(carrier + 4));
            st32(e + Joint, uint32_t(joint));
            st32(e + IsLink, carried == link_actor(c, x) ? 1 : 0);
            st32(e + State, state = Carrying);
            LOG("[demo] %s carried on %s joint %s (%d) from frame %u", a.actor.c_str(), a.to.c_str(), a.joint.c_str(), joint, frame);
        }
        if (state == Released || state == Falling) {
            if (frame >= follow_until(d, i)) continue;
            if (state == Falling) fall(c, x, e);
            follow(c, x, e);
            continue;
        }
        if (state != Carrying) continue;
        if (frame >= a.release) { stop(c, x, i, a.fall ? Falling : Released); st32(e + Velocity, 0); follow(c, x, e); LOG("[demo] %s released by %s at frame %u", a.actor.c_str(), a.to.c_str(), frame); continue; }
        const uint32_t carried = ld32(e + Carried), carrier = ld32(e + Carrier);
        if (!alive(carried, ld32(e + CarriedId)) || !alive(carrier, ld32(e + CarrierId))) { stop(c, x, i, Skipped); LOG("[demo] attach %s to %s: an actor left; released", a.actor.c_str(), a.to.c_str()); continue; }
        const uint32_t model = model_of(c, x, carrier), slot = slot_of(c, x, carried);
        uint32_t count = 0;
        const uint32_t matrices = anm_matrices(model, count);
        if (!matrices || ld32(e + Joint) >= count || !slot) continue;
        M34 j;
        for (int k = 0; k < 12; ++k) j[k] = fl(matrices + ld32(e + Joint) * 0x30 + uint32_t(k) * 4);
        // the carrier's move since its joints were calculated: its demo slot's translation (taken in
        // its coming execute) against its model's base matrix (used for those joints)
        if (const uint32_t cs = slot_of(c, x, carrier); cs && (ld16(cs + 4) & 2)) {
            const float dx = fl(cs + 8) - fl(model + 0xC8 + 12), dy = fl(cs + 12) - fl(model + 0xC8 + 28), dz = fl(cs + 16) - fl(model + 0xC8 + 44);
            if (std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) && std::fabs(dx) + std::fabs(dy) + std::fabs(dz) < 2000.f) { j[3] += dx; j[7] += dy; j[11] += dz; }
        }
        if (std::getenv("WWHD_DEMO_TRACE") && frame % 30 == 0)
            LOG("[demo] carry %s: was at %.0f,%.0f,%.0f; carrier at %.0f,%.0f,%.0f joint at %.0f,%.0f,%.0f", a.actor.c_str(), fl(carried + 0x314), fl(carried + 0x318), fl(carried + 0x31C),
                fl(carrier + 0x314), fl(carrier + 0x318), fl(carrier + 0x31C), j[3], j[7], j[11]);
        float p[3];
        int16_t r[3];
        if (!pose(j, a.offset, a.rotation, p, r, a.upright)) continue;
        for (int k = 0; k < 3; ++k) st32(slot + 8 + uint32_t(k) * 4, f32_as_u32(p[k]));
        for (int k = 0; k < 3; ++k) st16(slot + 0x20 + uint32_t(k) * 2, uint16_t(r[k]));
        st16(slot + 4, uint16_t(ld16(slot + 4) | 0x0A));  // translation and rotation enabled
        if (ld32(e + IsLink)) st8(carried + kLinkHeldMode, kHeld);
        if (std::getenv("WWHD_DEMO_TRACE") && frame % 30 == 0)
            LOG("[demo] carry %s: %.0f,%.0f,%.0f angles %d,%d,%d (frame %u)", a.actor.c_str(), p[0], p[1], p[2], r[0], r[1], r[2], frame);
    }
}

void release(Cpu* c, const Context& x) {
    for (uint32_t i = 0; i < kMaxEntries; ++i)
        if (ld32(entry(x, i) + State) == Carrying || ld32(entry(x, i) + State) == Falling) stop(c, x, i, Released);
    std::memset(mem::ptr(x.record), 0, kRecordBytes);
}
}  // namespace story_demo_parts::attach
