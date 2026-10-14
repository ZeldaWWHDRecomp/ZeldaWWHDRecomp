// Experimental partial implementation of #197: Leaf gust plus grounded initial item facing.
// See docs/camera-directed-items-research.md for native evidence and unsupported item matrix.
#include "mods.h"
#include "../runtime.h"
#include "../input.h"
#include "../overlay/overlay.h"
#include "../true60.h"
#include "guest_addr.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

extern "C" void f_024076CC_orig(Cpu*);
namespace mods {
namespace {
std::atomic<bool> enabled{[] {
    const char* value = std::getenv("WWHD_MOD_CAMERA_DIRECTED_ITEMS");
    return value && value[0] == '1' && value[1] == '\0';
}()};
struct Direction { uint16_t yaw, pitch; };
bool object(uint32_t address, uint32_t size) {
    return !(address & 3) && address >= mem::kMem2Start && address <= mem::kMem2End-size;
}
// Pinned public camera metadata: type rows contain 20 signed style IDs, style rows contain
// their algorithm at +4. Bounds/count globals are the native ones (024F727C / 024FA170).
bool type_style(int32_t type, uint32_t mode, int32_t& style) {
    if (type < 0 || uint32_t(type) >= ld32(GD(0x10049348)) || mode >= 20) return false;
    const uint64_t address = uint64_t(GD(0x10049350)) + uint32_t(type)*0x40ull + 0x18 + mode*2;
    if (address > UINT32_MAX || !object(uint32_t(address)&~3u,4)) return false;
    style = int16_t(ld16(uint32_t(address)));
    return style >= 0 && uint32_t(style) < ld32(GD(0x10044878));
}
bool style_algorithm(int32_t style, uint32_t& algorithm) {
    if (style < 0 || uint32_t(style) >= ld32(GD(0x10044878))) return false;
    const uint64_t address = uint64_t(GD(0x1004487C)) + uint32_t(style)*0x84ull + 4;
    if (address > UINT32_MAX || !object(uint32_t(address),4)) return false;
    algorithm = ld32(uint32_t(address));
    return true;
}
bool field_camera(uint32_t cam) {
    const int32_t type = int32_t(ld32(cam+0x51C)), field = int32_t(ld32(cam+0x8B0));
    const uint32_t mode = ld32(cam+0x13C);
    if (type != int32_t(ld32(cam+0x520)) || (mode != 0 && mode != 12) ||
        mode != ld32(cam+0x140) || (type != field && type != int32_t(ld32(cam+0x524)))) return false;
    for (const uint32_t offset : {0x8B4u,0x8B8u,0x8C0u,0x8C4u,0x8C8u,0x8CCu,0x8D0u})
        if (type == int32_t(ld32(cam+offset))) return false;
    // Rooms select custom types (e.g. DungeonUp), not necessarily the named FIELD type.
    // Require the same default AND active-mode engine as FIELD, not any map-tool/cinematic row.
    int32_t normal, room, expected, active;
    uint32_t normal_alg, room_alg, expected_alg, active_alg;
    return type_style(field,0,normal) && type_style(type,0,room) &&
        type_style(field,mode,expected) && type_style(type,mode,active) &&
        active == int32_t(ld32(cam+0x514)) && style_algorithm(normal,normal_alg) &&
        style_algorithm(room,room_alg) && style_algorithm(expected,expected_alg) &&
        style_algorithm(active,active_alg) && normal_alg == room_alg && expected_alg == active_alg;
}
// Game-thread read at the native collision/use boundary, never a retained camera/player pointer.
// Process +DC/+E8 and dCamera +3C ViewCache are render inputs, NOT our gameplay direction.
bool direction(uint32_t link, Direction& out) {
    if (!camera_directed_items() || true60::preview() || overlay::blocks_input() ||
        !input::gameplay_focused() || !object(link,0x8000)) return false;
    const auto play = GD(0x1046F0B0);
    if (ld32(play+0x5B34)!=link || ld8(play+0x5292) || ld32(play+0x52E4) || ld16(link+0x420) ||
        ld32(link+0x65B0)) return false;
    const auto process = ld32(play+0x5AF8);
    if (!object(process,0x248+0x8E0)) return false;
    const auto cam = process+0x248;
    // Native event ownership is bit 0x20000000 (024FA448); 0x20 is an ordinary mode-12
    // marker (024FE7FC), not a cinematic. Keep forced-type and lock/transition exclusions.
    if (!ld8(cam+4) || ld8(cam+5) || ld32(cam+0x128)!=link ||
        ld32(cam+0x12C) || ld32(cam+0x134) || ld32(cam+0x138) || ld8(cam+0x19C) ||
        (ld32(cam+0x510)&0x20000000) || ld8(cam+0x518) || ld32(cam+0x528)!=0xFF ||
        !field_camera(cam)) return false;
    const double x = ldf32(cam+0x10)-ldf32(cam+0x1C);
    const double y = ldf32(cam+0x14)-ldf32(cam+0x20);
    const double z = ldf32(cam+0x18)-ldf32(cam+0x24);
    const double horizontal = std::hypot(x,z);
    // A vertical/degenerate view has no meaningful yaw; refuse rather than guessing.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || horizontal<.001) return false;
    constexpr double units = 32768.0/3.14159265358979323846;
    out.yaw = uint16_t(int32_t(std::lround(std::atan2(x,z)*units)));
    // Native Leaf target pitch and collision ray use -sin(X) for world Y. atan2 bounds X to
    // [-0x4000,+0x4000], the native target-angle domain; do not invent a camera/physics clamp.
    out.pitch = uint16_t(int32_t(std::lround(-std::atan2(y,horizontal)*units)));
    return true;
}
bool grounded_leaf(uint32_t link) {
    return object(link,0x8000) && ld32(link+0x65F0)==0x92 && ld16(link+0x69B0)==0x34 &&
        (ld32(link+0x834)&0x20) && !(ld32(link+0x6A70)&0x80);
}
// Native item-local direction slots, not Link's current/shape rotation. Native setCollision
// may refine pitch after its BG check; leave that refinement active THROUGH the native call,
// then put the pre-call slots back even if the mod is disabled/throws before callback exit.
struct ScopedGustDirection {
    uint32_t link;
    uint16_t yaw, pitch;
    ScopedGustDirection(uint32_t l, Direction d):link(l),yaw(ld16(l+0x6986)),pitch(ld16(l+0x6988)) {
        st16(link+0x6986,d.yaw); st16(link+0x6988,d.pitch);
    }
    ~ScopedGustDirection() { st16(link+0x6986,yaw); st16(link+0x6988,pitch); }
};
}
bool camera_directed_items() { return enabled.load(std::memory_order_relaxed); }
void set_camera_directed_items(bool on) {
    enabled.store(on,std::memory_order_relaxed);
    LOG("[mods] camera-directed items (prototype: Leaf gust + initial item facing): %s",on?"on":"off");
}
void leaf_start(Cpu* c) {
    // Instruction boundary after native proc92/animation acceptance, before fan-model setup.
    // One durable action-local yaw change, not a per-frame facing override. The native proc
    // later owns movement/recoil (including current yaw = shape yaw + 0x8000); never undo it.
    const auto link = c->r[30];
    Direction d;
    if (camera_directed_items() && grounded_leaf(link) && direction(link,d)) {
        st16(link+0x322,d.yaw);
        st16(link+0x32A,d.yaw);
    }
}
// Initial facing only: native aim/camera handoff, resource checks and projectile pitch
// remain authoritative. These sites are accepted procedure entries, not shot guarantees.
namespace {
enum class Use { bow, boomerang, hookshot, rope, bomb, hammer, bottle };
void item_start(Cpu* c, unsigned actor_register, uint32_t proc, Use use) {
    if (!camera_directed_items()) return;
    const auto link = c->r[actor_register];
    if (!object(link,0x8000) || ld32(link+0x65F0)!=proc || !(ld32(link+0x834)&0x20) ||
        (ld32(link+0x6A70)&0x80)) return;
    const auto item = ld16(link+0x69B0);
    bool accepted = false;
    switch (use) {
    case Use::bow: accepted = item==0x27 || item==0x35 || item==0x36; break;
    case Use::boomerang: accepted = item==0x2D; break;
    case Use::hookshot: accepted = item==0x2F; break;
    case Use::rope: accepted = item==0x25; break;
    case Use::hammer: accepted = item==0x33; break;
    case Use::bottle: accepted = item==0x50 && c->r[29]==0; break;
    case Use::bomb: {
        const auto actor = ld32(link+0x65A4), pid = ld32(link+0x65A0);
        // Native carry sentinel, pending inventory intent and the kept actor identity:
        // never reinterpret every contextual carried actor as an inventory bomb.
        accepted = item==0x100 && ld16(link+0x69B2)==0x31 && c->r[31]==0 &&
            object(actor,0x3AC) && pid!=0xFFFFFFFF && ld32(actor+4)==pid && ld16(actor+8)==0x126;
        break;
    }
    }
    Direction d;
    if (accepted && direction(link,d)) {
        st16(link+0x322,d.yaw); st16(link+0x32A,d.yaw);
    }
}
} // namespace
extern "C" void site_023E7CF4(Cpu* c) { item_start(c,31,0x94,Use::bow); }
extern "C" void site_023E88F4(Cpu* c) { item_start(c,31,0x95,Use::bow); }
extern "C" void site_023E8D34(Cpu* c) { item_start(c,31,0x80,Use::boomerang); }
extern "C" void site_023E8E40(Cpu* c) { item_start(c,31,0x81,Use::boomerang); }
extern "C" void site_023E8A58(Cpu* c) { item_start(c,31,0x83,Use::hookshot); }
extern "C" void site_023E8B20(Cpu* c) { item_start(c,31,0x84,Use::hookshot); }
extern "C" void site_023E902C(Cpu* c) { item_start(c,31,0x76,Use::rope); }
extern "C" void site_023E9130(Cpu* c) { item_start(c,31,0x7D,Use::rope); }
extern "C" void site_023E9E98(Cpu* c) { item_start(c,30,0x71,Use::bomb); }
extern "C" void site_023EDFC4(Cpu* c) { item_start(c,31,0x51,Use::hammer); }
extern "C" void site_0243C830(Cpu* c) { item_start(c,31,0x53,Use::hammer); }
extern "C" void site_023EDE00(Cpu* c) { item_start(c,30,0xA5,Use::bottle); }
void leaf_collision(Cpu* c) {
    const auto link = c->r[3];
    Direction d;
    if (camera_directed_items() && grounded_leaf(link) && int16_t(ld16(link+0x6984))>0 && direction(link,d)) {
        ScopedGustDirection scope(link,d);
        f_024076CC_orig(c);
    } else f_024076CC_orig(c);
}
} // namespace mods
// Native accepted gust timer, range, origin, BG ray, capsule registration, effects and refusal
// remain native. No synthetic input, new actors, camera rotation, continuous facing, or retained host state.
extern "C" void hook_024076CC(Cpu* c) { mods::leaf_collision(c); }
extern "C" void site_023E7A70(Cpu* c) { mods::leaf_start(c); }
