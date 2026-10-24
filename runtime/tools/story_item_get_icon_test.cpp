// The get moment of a mod item in a chest (#269): runtime/src/mods/story_item_get_icon.* on synthetic
// memory. The projection, and the demo item's draw: the stand-in of a chest's mod item shows the item's icon
// (a HUD picture at the demo item's place) instead of its card; every other demo item is the game's.
#include "mods/guest_hud.h"
#include "mods/story_item_get_icon.h"
#include "mods/story_treasure_chest.h"
#include "runtime.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void log_msg(const char*, ...) {}

namespace {
namespace I = story_item_get_icon;
namespace hud = guestmods::hud;
constexpr uint32_t kArena = 0x20000000, kItem = kArena + 0x1000;

void map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
#endif
}

unsigned originals = 0, loads = 0;
void original(Cpu* c, uint32_t fn) {
    assert(fn == 0x02123B60);
    ++originals;
    c->r[3] = 1;
}
// the camera at (0, 0, 1000) looking at the origin, y up, a plain perspective projection with 16:9
I::Camera camera() {
    I::Camera c;
    c.valid = true;
    c.eye[2] = 1000, c.up[1] = 1;
    c.proj[0] = 1.0f / 1.7777778f, c.proj[5] = 1.0f, c.proj[14] = -1;
    return c;
}
I::Picture load(const std::string& png) {
    ++loads;
    if (png == "bad.png") throw std::runtime_error("not a PNG");
    return I::Picture(128 * 128 * 4, 0x7F);
}
void place(float x, float y, float z) {
    st32(kItem + 0x314, f32_as_u32(x)), st32(kItem + 0x318, f32_as_u32(y)), st32(kItem + 0x31C, f32_as_u32(z));
}
Cpu draw() {
    Cpu c{};
    c.r[3] = kItem;
    I::hooks::demo_item_draw(&c);
    return c;
}
// the item get's HUD list on the TV: its one picture, or nullptr
const hud::Command* picture(std::vector<std::shared_ptr<const hud::List>>& keep) {
    keep = hud::store().snapshot(0);
    for (const auto& l : keep)
        if (l->owner == "the port's item get" && l->commands.size() == 1 && l->commands[0].kind == hud::Command::Picture) return &l->commands[0];
    return nullptr;
}

void test_projection() {
    const I::Camera c = camera();
    float x, y, s;
    const float centre[3] = {0, 0, 0};
    assert(I::screen_point(c, centre, 50, x, y, s));
    assert(std::fabs(x - 640) < 0.01f && std::fabs(y - 360) < 0.01f && std::fabs(s - 50 * 360.0f / 1000) < 0.01f);
    // up and right on the screen; at the edge of the view (x = z / sx * ...) the picture's edge
    const float up[3] = {0, 100, 0}, right[3] = {1000 * 1.7777778f, 0, 0};
    assert(I::screen_point(c, up, 50, x, y, s) && y < 360 && std::fabs(x - 640) < 0.01f);
    assert(I::screen_point(c, right, 50, x, y, s) && std::fabs(x - 1280) < 0.5f);
    // behind the camera, or no camera: nothing
    const float behind[3] = {0, 0, 2000};
    assert(!I::screen_point(c, behind, 50, x, y, s));
    assert(!I::screen_point(I::Camera{}, centre, 50, x, y, s));
    // without the projection matrix: fovy and aspect (90 degrees: 1 / tan(45) = 1)
    I::Camera f = c;
    std::memset(f.proj, 0, sizeof f.proj);
    f.fovy = 90, f.aspect = 1.7777778f;
    assert(I::screen_point(f, up, 50, x, y, s) && std::fabs(y - (360 - 100.0f / 1000 * 360)) < 0.01f);
}

void test_draw() {
    I::reset();
    hud::store().reset();
    I::configure(original, camera, load);
    std::map<uint32_t, void (*)(Cpu*)> hooks;
    assert(!I::install([](uint32_t) { return std::string("another mod"); }, [&](uint32_t a, void (*f)(Cpu*)) { hooks[a] = f; }) && hooks.empty());
    assert(I::install([](uint32_t) { return std::string(); }, [&](uint32_t a, void (*f)(Cpu*)) { hooks[a] = f; }));
    assert(hooks.size() == 1 && hooks.count(0x02123B60));
    st8(kItem + 0x74E, story_treasure_chest::kStandIn);
    place(0, 0, 0);
    std::vector<std::shared_ptr<const hud::List>> keep;

    // no mod item shown: the game's stand-in (its card)
    Cpu c = draw();
    assert(originals == 1 && c.r[3] == 1 && !picture(keep));
    // a chest's mod item: its icon at the demo item's place, the card not drawn
    I::show(0x1234, "coin.png");
    hud::store().set_step(10);
    c = draw();
    assert(originals == 1 && c.r[3] == 1 && loads == 1);
    const hud::Command* p = picture(keep);
    assert(p && std::fabs(p->x + p->w / 2 - 640) < 0.01f && std::fabs(p->y + p->h / 2 - 360) < 0.01f && p->w > 0 && p->w == p->h);
    // once per logic step; the next step follows the item (and the picture is made once)
    place(0, 100, 0);
    draw();
    assert(picture(keep)->y + picture(keep)->h / 2 == 360);
    hud::store().set_step(11);
    draw();
    assert(picture(keep)->y + picture(keep)->h / 2 < 360 && loads == 1);
    // another demo item (the game's item): the game's draw
    st8(kItem + 0x74E, 0x01);
    draw();
    assert(originals == 2);
    st8(kItem + 0x74E, story_treasure_chest::kStandIn);
    // the demo item stops drawing (hidden): its icon goes after two steps
    I::logic(12);
    assert(picture(keep));
    I::logic(14);
    assert(!picture(keep));
    hud::store().set_step(15);
    draw();
    assert(picture(keep));
    // a state load resets the HUD layer: the picture is made again
    hud::store().reset();
    I::state_loaded();
    draw();
    assert(picture(keep) && loads == 2);
    // the item is given: the icon goes at once
    I::hide();
    assert(!picture(keep) && !I::showing());
    draw();
    assert(originals == 3);
    // an icon that can't be read: the game's stand-in, asked once
    I::show(0x1234, "bad.png");
    draw();
    draw();
    assert(originals == 5 && loads == 3 && !picture(keep));
    I::hide();
}

}  // namespace

int main() {
    map(kArena, 0x10000);
    map(0x10000000, 0x600000);
    test_projection();
    test_draw();
    std::puts("story_item_get_icon_test: ok");
    return 0;
}
