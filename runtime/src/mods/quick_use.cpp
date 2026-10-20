// One press draws and uses an item against a native enemy lock. No projectile, actor-state,
// timer is patched. Optional animation acceleration is scoped to native frame operations;
// authored rates are never left changed between calls or written from host reset callbacks.
#include "mods.h"
#include "runtime.h"
#include "guest_addr.h"
#include "input.h"
#include "true60.h"
#include "overlay/overlay.h"
#include <atomic>
#include <cstdlib>
#include <cmath>
#include <mutex>

extern "C" void f_023F32E4_orig(Cpu*);
extern "C" void f_023DE7E8_orig(Cpu*);

namespace mods {
namespace {
uint32_t play_address() { return GD(0x1046F0B0); }
constexpr uint16_t kRope = 0x25, kBoomerang = 0x2D, kHookshot = 0x2F, kBomb = 0x31, kHammer = 0x33;
constexpr uint32_t kButtons[] = {input::kX, input::kY, input::kR};
constexpr uint32_t kAllButtons = input::kX | input::kY | input::kR;
constexpr uint32_t kCancelButtons = input::kA | input::kB | input::kPlus | input::kMinus | input::kStickR;
std::atomic<bool> enabled{[]{
    const char* e = std::getenv("WWHD_MOD_QUICK_USE");
    return e && std::atoi(e) != 0;
}()};
bool valid_speed(float f) { return f == 1.f || f == 1.25f || f == 1.5f; }
std::atomic<float> animation_speed{[]{
    const char* e = std::getenv("WWHD_MOD_QUICK_USE_SPEED");
    if (!e) return 1.f;
    char* end = nullptr;
    const double f = std::strtod(e, &end);
    return end != e && !*end && (f == 1.0 || f == 1.25 || f == 1.5) ? float(f) : 1.f;
}()};
std::mutex mutex;
std::atomic<uint32_t> animated_ctrl{0};
std::atomic<uint64_t> reset_epoch{0};
uint32_t down = 0, armed = kAllButtons, edges = 0;
uint64_t edge_steps[3]{};
enum class Phase { none, equipping, handoff, drawing, releasing, spent };
struct Request {
    Phase phase = Phase::none;
    uint32_t link = 0, target_pid = UINT32_MAX, equip_proc = 0, link_pid = 0;
    uint16_t item = 0;
    unsigned slot = 0;
    uint64_t started = 0, last = 0;
    bool cast_started = false;
    uint32_t held_actor = 0, held_pid = 0;
} request;
// Animation ownership outlives a released input request, but never a new native clip generation.
// Only upper channel 2 is eligible. No guest rate is stored in this host token.
struct Animation {
    Request owner{};
    uint16_t clip = 0;
    uint32_t data = 0, range = 0, authored_rate = 0;
    uint16_t old_item = 0;
    float keys[8]{};
    unsigned key_count = 0;
} animation;

bool bow(uint16_t item) { return item == 0x27 || item == 0x35 || item == 0x36; }
uint32_t move_proc(uint16_t item) {
    return item == kHammer ? 8 : item == kBomb ? 0x73 : bow(item) ? 0x95 : item == kHookshot ? 0x84 : item == kRope ? 0x7D : item == kBoomerang ? 0x81 : 0;
}
bool grounded_proc(uint32_t proc) {
    return proc == 4 || proc == 6 || proc == 7 || proc == 8 || proc == 9 || proc == 0x17 || proc == 0x18;
}
bool manual_proc(uint32_t proc) {
    return proc == 0x76 || proc == 0x80 || proc == 0x83 || proc == 0x94;
}
bool equip_proc(uint32_t proc) {
    // Native item changes leave the old item's MOVE mode before finishing the upper-body
    // equip animation. Ordinary grounded movement/turns are not interruptions.
    return proc == request.equip_proc || grounded_proc(proc);
}
bool item_proc(uint16_t item, uint32_t proc) {
    if (item == kHammer) return grounded_proc(proc) ||
        (request.phase == Phase::spent && (proc == 0x51 || proc == 0x52 || proc == 0x53 || proc == 0x54));
    if (item == kBomb) return grounded_proc(proc) || proc == 0x73 ||
                               (request.phase == Phase::spent && proc == 0x71);
    if (proc == move_proc(item)) return true;
    if (request.phase != Phase::spent) return false;
    // Completion is not a fresh press. Retain hold suppression across the item's own native
    // shot/return cycle, without authorizing any new automatic use in those procedures.
    return (item == kRope && proc == 0x7E) || (item == kHookshot && proc == 0x85) ||
           (item == kBoomerang && grounded_proc(proc));
}
bool recent(uint64_t then, uint64_t now, uint64_t limit) {
    return now >= then && now - then <= limit;
}
bool playable() {
    const uint32_t play = play_address();
    return !overlay::blocks_input() && !ld8(play + 0x5292) && !ld8(play + 0x514C) &&
           !ld32(GD(0x101F36CC)) && !ld8(GD(0x101EA069));
}
void cancel() {
    request = {};
    animation = {};
    animated_ctrl = 0;
    ++reset_epoch;
    edges = 0;
    armed &= ~down; // an interruption or live enable never turns a held button into a new request
}
// Resolve the selected PID every time. A stale Link pointer alone is not an enemy lock; do not
// retain an enemy actor pointer, scan actors or expand the native lock/item eligibility rules.
uint32_t enemy_pid(Cpu* c, uint32_t link) {
    const uint32_t attention = ld32(link + 0x6894);
    if (!attention || ld8(attention + 0x18) != 1) return UINT32_MAX;
    const uint32_t count = ld32(attention + 0xD4), index = ld32(attention + 0xD8);
    if (count > 8 || index >= count) return UINT32_MAX;
    const uint32_t entry = attention + 0x54 + index * 16;
    const uint32_t pid = ld32(entry + 12);
    if (ld32(entry + 8) != 2 || pid == UINT32_MAX) return UINT32_MAX;
    const Cpu saved = *c;
    const uint32_t actor = guest_call(c, GC(0x024EBA14), {entry}); // dAttList_c::getActor
    *c = saved;
    return actor && actor == ld32(link + 0x65B0) && (ld32(actor + 0x39C) & 4) ? pid : UINT32_MAX;
}
bool context(Cpu* c, uint32_t link, uint16_t item, unsigned slot, uint32_t& pid, Phase phase = Phase::drawing) {
    if (!enabled.load(std::memory_order_relaxed) || !playable() || !link || slot >= 3 ||
        ld32(play_address() + 0x5B2C) != link || (phase != Phase::equipping && ld16(link + 0x69B0) != (item == kBomb ? 0x100 : item)) ||
        ld8(play_address() + 0x5BBB + slot) != item ||
        (item != kBomb && (phase != Phase::equipping && phase != Phase::handoff) && ld8(link + 0x68D9) != slot) ||
        ld32(link + 0x8260) || ld16(link + 0x699C) ||
        (ld8(link + 0x690D) & 3)) return false; // native A/B cancel wins
    pid = enemy_pid(c, link);
    return pid != UINT32_MAX;
}
bool validate(Cpu* c, uint32_t link, uint16_t item) {
    uint32_t pid;
    const bool bomb_actor = item != kBomb || request.phase == Phase::equipping ||
        request.phase == Phase::spent ||
        (request.held_actor && ld32(link + 0x65A4) == request.held_actor &&
         ld32(request.held_actor + 4) == request.held_pid && ld16(request.held_actor + 8) == 0x126);
    return bomb_actor && request.phase != Phase::none && request.link == link && request.item == item &&
           (request.phase == Phase::spent || recent(request.started, step(), 90)) &&
           recent(request.last, step(), 2) &&
           ld32(link + 4) == request.link_pid &&
           context(c, link, item, request.slot, pid, request.phase) &&
           pid == request.target_pid &&
           (request.phase == Phase::equipping ?
                equip_proc(ld32(link + 0x65F0)) &&
                    (ld16(link + 0x69B2) == item || ld16(link + 0x69B0) == item) :
                item_proc(item, ld32(link + 0x65F0)));
}
void begin(Cpu* c, uint32_t link, uint16_t item) {
    if (item == kBomb || item == kHammer || !move_proc(item) || request.phase != Phase::none ||
        manual_proc(ld32(link + 0x65F0))) return; // bomb captured only at native equip acceptance
    const unsigned slot = ld8(link + 0x68D9);
    if (slot >= 3 || !(edges & kButtons[slot]) || !recent(edge_steps[slot], step(), 2) ||
        !(ld8(link + 0x690D) & (4u << slot))) return;
    uint32_t pid;
    if (!context(c, link, item, slot, pid)) return;
    edges = 0; // one accepted native dispatch owns this physical press; never queue a second item
    request = {Phase::drawing, link, pid, 0, ld32(link + 4), item, slot, step(), step()};
    trace("quick-use: draw item %02X slot %u target %08X", item, slot, pid);
}
// Native item dispatch, immediately before entering its ordinary ready/movement mode. This also
// catches the first press when equipping; waiting for a second ready-state trigger would miss it.
void dispatch(Cpu* c, uint16_t item) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint32_t link = c->r[3];
    if (request.phase == Phase::equipping) {
        if (request.item != item) return; // an old item's return-to-mode is not the accepted equip
        if (validate(c, link, item) && ld16(link + 0x69B0) == item) {
            request.phase = Phase::handoff;
            request.last = step();
        } else cancel();
    }
    begin(c, link, item);
}
// Native checkItemChange has accepted an actual slot edge and written mReadyItemBtn, but the
// equip animation/model load may take many ticks. Capture here, not only at its later mode entry
// (where the original physical edge has expired). The native pending item and target must stay
// the same, but its ordinary grounded equip transitions are allowed. A rejected/interrupted
// equip is never retried by the mod.
void equip(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    if (request.phase != Phase::none) return;
    const uint32_t link = c->r[3];
    const unsigned slot = ld8(link + 0x68D9);
    if (slot >= 3 || !(edges & kButtons[slot]) || !recent(edge_steps[slot], step(), 2) ||
        !(ld8(link + 0x690D) & (4u << slot))) return;
    const uint16_t item = ld8(play_address() + 0x5BBB + slot);
    uint32_t pid;
    if (!move_proc(item) || (item == kHammer && ld16(link + 0x69B0) == kHammer) ||
        !context(c, link, item, slot, pid, Phase::equipping)) return;
    const uint32_t proc = ld32(link + 0x65F0);
    if (manual_proc(proc)) return; // manual first-person item aiming
    edges = 0;
    request = {Phase::equipping, link, pid, proc, ld32(link + 4), item, slot, step(), step()};
    trace("quick-use: equip item %02X slot %u target %08X", item, slot, pid);
}
bool prepare(Cpu* c, uint32_t link, uint16_t item, bool require_actor = true) {
    // The old item's checker may still run during a model/upper-animation change. Only native
    // mode entry may promote the accepted equip; do not cancel it or override the old checker.
    if (request.phase == Phase::equipping) return false;
    // Native model publication can enter the new MOVE mode while mReadyItemBtn still names
    // the OLD item. Keep the accepted intent, but leave native input untouched until the
    // game's next ready-slot publication agrees. Never interpret the old checker as the new one.
    if (request.phase == Phase::handoff) {
        if (request.item != item) return false;
        if (!validate(c, link, item)) { cancel(); return false; }
        request.last = step();
        // Carrying a native inventory bomb restores the old ready slot permanently. Its
        // checker/throw constructor do not consume that slot; the captured inventory slot,
        // sentinel100 and same carried actor/PID prove ownership instead. Projectile items
        // still require the game's ready-slot publication before any local input override.
        if (item != kBomb && ld8(link + 0x68D9) != request.slot) return false;
        request.phase = Phase::drawing;
    }
    if (request.phase == Phase::none) begin(c, link, item); // already drawn: accept a new native edge
    if (!validate(c, link, item)) {
        if (request.phase != Phase::none) cancel();
        return false;
    }
    const uint32_t actor = ld32(link + 0x6594);
    if (require_actor && item != kBomb && request.phase != Phase::spent && (!actor || ld32(actor + 0xB0) != 0)) { cancel(); return false; }
    request.last = step();
    return true;
}
// Hookshot and rope query results, only inside their idle-actor ready branch. During draw, keep
// the local held result true even for a tap. At countdown zero, take the native release branch.
// Its latch predicate is supplied locally below, in the SAME checker invocation: no synthetic
// armed latch can survive a pause, cancellation, save/load, or return to the guest main loop.
void held_query(Cpu* c, uint16_t item) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint32_t link = c->r[31];
    if (!prepare(c, link, item)) return;
    if (request.phase == Phase::spent) { c->r[3] = 0; return; }
    if (ld16(link + 0x69AC) != 0) c->r[3] = 4u << request.slot;
    else { request.phase = Phase::releasing; c->r[3] = 0; }
}
void release_predicate(Cpu* c, uint16_t item, unsigned reg) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    if (request.phase != Phase::releasing || !validate(c, c->r[31], item)) return;
    c->r[reg] = 1; // native checker immediately clears its latch and performs the shot/throw
    request.phase = Phase::spent;
    trace("quick-use: release item %02X slot %u target %08X", item, request.slot, request.target_pid);
}
// The boomerang owns a different press/hold/release policy: +69AC counts UP to its native
// 12-tick short/long threshold. Change only the local release word / held-query result, not the
// shared pad or the counter, deferred flag, animation, multi-target list or projectile actor.
void boomerang_release(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint32_t link = c->r[31];
    if (!prepare(c, link, kBoomerang)) return;
    constexpr uint32_t masks[] = {8, 16, 0x4000};
    const uint32_t mask = masks[request.slot];
    c->r[8] &= ~mask;
    if (request.phase == Phase::drawing && int16_t(ld16(link + 0x69AC)) >= 12) {
        c->r[8] |= mask;
        request.phase = Phase::spent;
        trace("quick-use: release item %02X slot %u target %08X", kBoomerang, request.slot, request.target_pid);
    }
}
void boomerang_held(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    if (prepare(c, c->r[31], kBoomerang))
        c->r[3] = request.phase == Phase::drawing ? 4u << request.slot : 0;
}
// Hammer's first native slot edge can expire during model/equip104 publication. Complete
// that accepted equip once at the local ready-trigger query, without touching global input,
// the ready slot, body animation or native selected-item equality/side-versus-front choice.
void hammer_trigger(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    if (request.item != kHammer || request.phase == Phase::none) return;
    const uint32_t link = c->r[31];
    if (!validate(c, link, kHammer)) { cancel(); return; }
    request.last = step();
    if (request.phase == Phase::spent) { c->r[3] = 0; return; }
    if (request.phase != Phase::equipping || ld16(link + 0x69B0) != kHammer) return;
    if (ld16(link + 0x69B2) != kHammer) { cancel(); return; }
    const uint16_t clip = ld16(link + 0x5888);
    if (clip == 0x104) return; // native equip is still active; never shorten or skip it
    if (clip != 0xFFFF) { cancel(); return; } // only proven completed upper equip is ready
    if (ld8(link + 0x68D9) != request.slot) return; // strict native slot publication
    c->r[3] = 4u << request.slot;
    request.phase = Phase::spent;
    trace("quick-use: release item %02X slot %u target %08X", kHammer, request.slot, request.target_pid);
}
// Native bomb creation has succeeded, published mGrabActor and consumed ammo. It deliberately
// replaces mEquipItem with 0x100; retain only the accepted inventory31 intent, never a pickup.
void bomb_published(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint32_t link = c->r[31];
    if (request.phase != Phase::equipping || request.item != kBomb) return;
    const uint32_t actor = ld32(link + 0x65A4);
    if (!validate(c, link, kBomb) || ld16(link + 0x69B0) != 0x100 ||
        !actor || ld16(actor + 8) != 0x126) { cancel(); return; }
    request.held_actor = actor;
    request.held_pid = ld32(actor + 4);
    request.phase = Phase::handoff;
    request.last = step();
}
// This native branch has already recognized the held actor as a bomb. Supply only its local
// matching-item trigger predicate; native throw init, collision, animation and release own effect.
void bomb_trigger(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint32_t link = c->r[30];
    if (request.item != kBomb || !prepare(c, link, kBomb)) return;
    c->r[3] = request.phase == Phase::spent ? 0 : 1;
    if (request.phase != Phase::spent)
        trace("quick-use: release item %02X slot %u target %08X", kBomb, request.slot, request.target_pid);
    request.phase = Phase::spent;
}
// Bow redraw checks held input BEFORE native arrow creation. Keep it held through redraw,
// then release once in clip36's ready branch; spent intent blocks another redraw. Never
// override native ammo, animation, collision or actor construction/launch operations.
void bow_draw_query(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint16_t item = ld16(c->r[31] + 0x69B0);
    if (bow(item) && prepare(c, c->r[31], item, false))
        c->r[3] = request.phase == Phase::drawing ? 4u << request.slot : 0;
}
void bow_release(Cpu* c) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    const uint32_t link = c->r[31];
    const uint16_t item = ld16(link + 0x69B0);
    if (!bow(item) || !prepare(c, link, item)) return;
    c->r[3] = request.phase == Phase::spent ? 4u << request.slot : 0;
    if (request.phase != Phase::spent)
        trace("quick-use: release item %02X slot %u target %08X", item, request.slot, request.target_pid);
    request.phase = Phase::spent;
}
void note_animation(Cpu* c, uint32_t link, uint32_t clip, unsigned channel, uint64_t epoch) {
    if (!enabled.load(std::memory_order_relaxed) || channel != 2 || true60::preview()) return;
    std::lock_guard lock(mutex);
    if (epoch != reset_epoch.load()) return; // a host reset during native setup wins
    animation = {}; // even restarting the same clip is a new native generation
    animated_ctrl = 0;
    if (quick_use_speed() == 1.f || request.link != link || !validate(c, link, request.item)) return;
    const bool equip = clip == 0x105 && request.item != kBomb && request.item != kHammer && request.phase == Phase::equipping;
    const bool cast = clip == 0x34 && request.item == kBoomerang && request.phase == Phase::spent && !request.cast_started;
    if (!equip && !cast) return;
    if (cast) request.cast_started = true;
    animation = {request, uint16_t(clip), ld32(link + 0x583C), ld32(link + 0x58E0),
                 ld32(link + 0x58D8), ld16(link + 0x69B0)};
    animated_ctrl = link + 0x58D8;
    trace("quick-use: animation clip %03X speed %.2f item %02X", clip, quick_use_speed(), request.item);
}
float animation_factor(Cpu* c, uint32_t ctrl, bool advance) {
    if (!enabled.load(std::memory_order_relaxed)) return 1.f;
    std::lock_guard lock(mutex);
    const auto& a = animation;
    const auto& r = a.owner;
    if (r.phase == Phase::none || ctrl != r.link + 0x58D8) return 1.f;
    uint32_t pid;
    const bool equip = a.clip == 0x105;
    const uint32_t proc = ld32(r.link + 0x65F0);
    const bool eligible_proc = equip ?
        (grounded_proc(proc) || proc == 0x7D || proc == 0x81 || proc == 0x84 || proc == 0x95) : proc == 0x81;
    if (quick_use_speed() == 1.f || !recent(r.started, step(), 90) ||
        ld32(r.link + 4) != r.link_pid || !eligible_proc ||
        ld16(r.link + 0x5888) != a.clip || ld32(r.link + 0x583C) != a.data ||
        ld32(ctrl + 8) != a.range || ld32(ctrl) != a.authored_rate ||
        !context(c, r.link, r.item, r.slot, pid, equip ? Phase::equipping : Phase::drawing) ||
        pid != r.target_pid || (equip && (ld16(r.link + 0x69B2) != r.item || ld16(r.link + 0x69B0) != a.old_item ||
                                        ld16(r.link + 0x69B0) == r.item)) ||
        (!equip && (!ld32(r.link + 0x6594) || ld32(ld32(r.link + 0x6594) + 0xB0) != 0))) {
        // Guest previews roll back, host tokens do not. A temporary end/stop in a preview
        // must not consume a generation that the authoritative full pass will restore.
        if (!true60::preview()) { animation = {}; animated_ctrl = 0; }
        return 1.f;
    }
    const float rate = u32_as_f32(ld32(ctrl)), frame = u32_as_f32(ld32(ctrl + 4));
    const int start = int16_t(ld16(ctrl + 8)), end = int16_t(ld16(ctrl + 10));
    // The event inventory is proven only for forward, nonwrapping one-shot clips. Other
    // attributes, reversed/stopped or malformed controls retain exactly native behavior.
    if (ld8(ctrl + 14) != 0 || !std::isfinite(rate) || rate <= 0.f ||
        !std::isfinite(frame) || start < 0 || end <= start || frame < start || frame > end || rate > end - start) {
        if (!true60::preview()) { animation = {}; animated_ctrl = 0; }
        return 1.f;
    }
    if (!advance) {
        const float key = float(c->f[1].ps0);
        if (std::isfinite(key) && !true60::preview()) {
            bool found = false;
            for (unsigned i = 0; i < a.key_count; ++i) found |= a.keys[i] == key;
            if (!found) {
                if (a.key_count == 8) { animation = {}; animated_ctrl = 0; }
                else animation.keys[animation.key_count++] = key;
            }
        }
        return 1.f; // NEVER widen the native predictive key window
    }
    // Observe native keys before accelerating. Clamp a faster step to land ON any upcoming
    // key, not past it. Native checkPass still sees its authored [frame, frame+rate) window;
    // stopping acceleration on launch/cancel therefore cannot overlap a widened old window.
    if (!a.key_count) return 1.f;
    float delta = rate * quick_use_speed(), cap = INFINITY;
    for (unsigned i = 0; i < a.key_count; ++i)
        if (a.keys[i] >= frame + rate && a.keys[i] < frame + delta) {
            cap = a.keys[i]; delta = cap - frame;
        }
    float factor = delta / rate;
    // Division/multiplication can round a capped step above its key. Check the actual float
    // destination, backing off representable factors; if uncertain, take the native step.
    for (unsigned n = 0; n < 4 && frame + float(rate * factor) > cap; ++n)
        factor = std::nextafter(factor, 1.f);
    return factor >= 1.f && frame + float(rate * factor) <= cap ? factor : 1.f;
}
void after_input(Cpu* c, uint32_t link) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    if (request.phase != Phase::none) {
        if (!validate(c, link, request.item)) cancel();
        else request.last = step();
    }
}
} // namespace

bool quick_use() { return enabled.load(std::memory_order_relaxed); }
float quick_use_speed() { return animation_speed.load(std::memory_order_relaxed); }
void set_quick_use_speed(float factor) {
    if (valid_speed(factor) && animation_speed.exchange(factor, std::memory_order_relaxed) != factor)
        quick_use_reset();
}
void quick_use_animation_call(Cpu* c, void (*native)(Cpu*), bool advance) {
    const uint32_t ctrl = c->r[3];
    if (ctrl != animated_ctrl.load(std::memory_order_relaxed) || !ctrl) { native(c); return; }
    const float factor = animation_factor(c, ctrl, advance);
    if (factor == 1.f) { native(c); return; }
    const uint32_t authored = ld32(ctrl);
    const float rate = u32_as_f32(authored);
    if (!std::isfinite(rate) || rate <= 0.f) { native(c); return; }
    const uint32_t scaled = f32_as_u32(rate * factor), before = ld32(ctrl + 4);
    // Called only on the emulation thread, around an existing native frame operation. true60's
    // dt/preview logic runs INSIDE this scope. Key crossings remain entirely native. Sound
    // pitch, save capture and every other guest caller continue to see the authored rate.
    st32(ctrl, scaled);
    native(c);
    if (ld32(ctrl) == scaled) st32(ctrl, authored); // preserve a native stop/replacement
    if (ld32(ctrl + 4) != before)
        trace("quick-use: animation advance frame %.3f -> %.3f rate %.3f speed %.2f",
              u32_as_f32(before), u32_as_f32(ld32(ctrl + 4)), rate, factor);
}
void set_quick_use(bool on) {
    if (enabled.exchange(on, std::memory_order_relaxed) != on) quick_use_reset();
}
void quick_use_reset() {
    std::lock_guard lock(mutex);
    cancel();
    armed = 0;
}
void quick_use_filter_pad(input::PadState& s) {
    std::lock_guard lock(mutex);
    const uint32_t next = s.buttons & kAllButtons;
    armed |= kAllButtons & ~next;
    if (!quick_use() || !playable() || (s.buttons & kCancelButtons)) {
        down = next;
        cancel();
        return;
    }
    const uint32_t pressed = next & ~down & armed;
    if (pressed) { animation = {}; animated_ctrl = 0; } // never inherit a throw's speed
    if (request.phase != Phase::none && (pressed & ~kButtons[request.slot])) cancel();
    if (request.phase == Phase::spent && !(next & kButtons[request.slot])) request = {};
    // Presses during a pending draw are not a second queued action (including a fast double tap).
    const uint32_t accepted = request.phase == Phase::none ? pressed : 0;
    edges |= accepted;
    for (unsigned slot = 0; slot < 3; ++slot)
        if (accepted & kButtons[slot]) edge_steps[slot] = step();
    down = next;
    // Observe mapped physical input only. Never synthesize global buttons for unrelated actions.
}
} // namespace mods

extern "C" void hook_023DE7E8(Cpu* c) {
    if (!mods::quick_use()) { f_023DE7E8_orig(c); return; }
    const uint32_t link = c->r[3], clip = c->r[4], channel = c->r[5];
    const uint64_t epoch = mods::reset_epoch.load();
    f_023DE7E8_orig(c);
    mods::note_animation(c, link, clip, channel, epoch);
}
extern "C" void hook_023F32E4(Cpu* c) {
    if (!mods::quick_use()) { f_023F32E4_orig(c); return; }
    const uint32_t link = c->r[3];
    f_023F32E4_orig(c);
    mods::after_input(c, link);
}
// Observation only: the existing native branch still owns the actual throw and actor writes.
extern "C" void site_023F05EC(Cpu* c) { mods::hammer_trigger(c); }
extern "C" void site_0243CB70(Cpu* c) {
    if (ld32(c->r[30] + 0x65F0) == 0x53) mods::trace("quick-use: native launch item 33");
}
extern "C" void site_0243C6F4(Cpu*) { mods::trace("quick-use: native launch item 33"); }
extern "C" void site_023DF7C0(Cpu* c) { mods::bomb_published(c); }
extern "C" void site_023EA474(Cpu* c) { mods::bomb_trigger(c); }
extern "C" void site_023E88B0(Cpu* c) { mods::dispatch(c, ld16(c->r[3] + 0x69B0)); }
extern "C" void site_023EB2B0(Cpu* c) { mods::bow_release(c); }
extern "C" void site_023EB434(Cpu* c) { mods::bow_draw_query(c); }
extern "C" void site_023EB2C4(Cpu* c) {
    mods::trace("quick-use: native launch item %02X", ld16(c->r[31] + 0x69B0));
}
extern "C" void site_0242E14C(Cpu* c) {
    const uint32_t actor = c->r[27];
    if (actor && ld16(actor + 8) == 0x126) mods::trace("quick-use: native launch item 31");
}
extern "C" void site_0242E198(Cpu* c) { site_0242E14C(c); }
extern "C" void site_023FB49C(Cpu*) { mods::trace("quick-use: native launch item 2D"); }
extern "C" void site_023EFB44(Cpu* c) { mods::equip(c); }
extern "C" void site_023F0624(Cpu* c) { mods::dispatch(c, 0x2F); }
extern "C" void site_023F0670(Cpu* c) { mods::dispatch(c, 0x25); }
extern "C" void site_023F06AC(Cpu* c) { mods::dispatch(c, 0x2D); }
extern "C" void site_023F1764(Cpu* c) { mods::dispatch(c, 0x2F); }
extern "C" void site_023F1784(Cpu* c) { mods::dispatch(c, 0x25); }
extern "C" void site_023F1728(Cpu* c) { mods::dispatch(c, 0x2D); }
extern "C" void site_023EAE30(Cpu* c) { mods::held_query(c, 0x2F); }
extern "C" void site_023EAE78(Cpu* c) { mods::held_query(c, 0x2F); }
extern "C" void site_023EC12C(Cpu* c) { mods::held_query(c, 0x25); }
extern "C" void site_023EC178(Cpu* c) { mods::held_query(c, 0x25); }
extern "C" void site_023EAE84(Cpu* c) { mods::release_predicate(c, 0x2F, 11); }
extern "C" void site_023EC184(Cpu* c) { mods::release_predicate(c, 0x25, 0); }
extern "C" void site_023EB548(Cpu* c) { mods::boomerang_release(c); }
extern "C" void site_023EB614(Cpu* c) { mods::boomerang_held(c); }
