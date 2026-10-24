// The get moment of a mod item in a treasure chest: its icon over Link's head (story_item_get_icon.h).
#include "story_item_get_icon.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "guest_addr.h"
#include "guest_hud.h"
#include "runtime.h"
#include "story_item_models.h"
#include "story_treasure_chest.h"

namespace story_item_get_icon {

bool screen_point(const Camera& c, const float p[3], float world_size, float& x, float& y, float& size) {
    if (!c.valid) return false;
    auto normalize = [](float v[3]) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(l > 1e-6f)) return false;
        for (int i = 0; i < 3; i++) v[i] /= l;
        return true;
    };
    float back[3], right[3], up[3];
    for (int i = 0; i < 3; i++) back[i] = c.eye[i] - c.center[i];
    if (!normalize(back)) return false;
    right[0] = c.up[1] * back[2] - c.up[2] * back[1];
    right[1] = c.up[2] * back[0] - c.up[0] * back[2];
    right[2] = c.up[0] * back[1] - c.up[1] * back[0];
    if (!normalize(right)) return false;
    up[0] = back[1] * right[2] - back[2] * right[1];
    up[1] = back[2] * right[0] - back[0] * right[2];
    up[2] = back[0] * right[1] - back[1] * right[0];
    // the scales of the game's projection (its matrix when it is a plain perspective one, as the editor's view)
    float sx, sy;
    if (c.proj[14] == -1 && c.proj[0] > 0 && c.proj[5] > 0) sx = c.proj[0], sy = c.proj[5];
    else {
        if (!(c.fovy > 0 && c.fovy < 180)) return false;
        sy = 1.0f / std::tan(c.fovy * 3.14159265f / 360.0f);
        sx = sy / std::max(0.1f, c.aspect);
    }
    const float d[3] = {p[0] - c.eye[0], p[1] - c.eye[1], p[2] - c.eye[2]};
    const float vx = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
    const float vy = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
    const float z = -(d[0] * back[0] + d[1] * back[1] + d[2] * back[2]);
    if (!(z > 1.0f)) return false;
    // NDC to the HUD's units: 720 units high; the width follows the projection's shape
    const float half_h = 360.0f, half_w = 360.0f * sy / sx;
    x = 640.0f + sx * vx / z * half_w;
    y = 360.0f - sy * vy / z * half_h;
    size = world_size * sy / z * half_h;
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(size) && std::abs(x) < 60000 && std::abs(y) < 60000 && size > 0;
}

namespace {

namespace hud = guestmods::hud;
const char* const kOwner = "the port's item get";
const uint32_t kDemoItemDraw = GC(0x02123B60);  // daDitem_Draw(daDitem_c*)
constexpr uint32_t kActorPos = 0x314;          // fopAc_ac_c::current.pos
constexpr uint32_t kDemoItemNo = 0x74E;        // daDitem_c::m_itemNo
constexpr uint32_t kDemoItemBytes = 0x770;

std::mutex g_mu;
void (*g_original)(Cpu*, uint32_t) = nullptr;
Camera (*g_camera)() = nullptr;
Picture (*g_load)(const std::string&) = nullptr;
bool g_installed = false;
uint16_t g_key = 0;
std::string g_png;
hud::Handle g_image = 0;      // the HUD picture of g_key (0: not made yet)
bool g_failed = false;        // the picture could not be made: the stand-in's card is drawn
bool g_published = false;     // a list is up
uint64_t g_drawn_step = 0, g_published_step = ~0ull;

bool guest_span(uint32_t p, uint32_t n) { return p >= 0x10000000u && p < 0x68000000u && n <= 0x68000000u - p; }

void drop_locked() {
    if (g_published) hud::store().drop(kOwner);
    g_published = false;
    g_published_step = ~0ull;
}
void forget_image_locked() {
    if (g_image) hud::store().release(kOwner, g_image);
    g_image = 0;
}

// the icon at the demo item's place: true when it is up (the card is not drawn)
bool publish(uint32_t item) {
    std::lock_guard lock(g_mu);
    if (!g_key || g_failed) return false;
    if (!g_image) {
        Picture px;
        try {
            if (g_load) px = g_load(g_png);
        } catch (const std::exception& e) {
            LOG("[items] item get: the icon %s can't be shown (%s): the game's stand-in instead", g_png.c_str(), e.what());
        }
        if (px.size() == 128u * 128u * 4u) g_image = hud::store().create_image(kOwner, 128, 128, 128 * 4, px.data(), px.size());
        if (!g_image) {
            g_failed = true;
            return false;
        }
    }
    const uint64_t step = hud::store().step();
    g_drawn_step = step;
    if (g_published && g_published_step == step) return true;  // once per logic step (the HUD layer interpolates)
    const float pos[3] = {u32_as_f32(ld32(item + kActorPos)), u32_as_f32(ld32(item + kActorPos + 4)), u32_as_f32(ld32(item + kActorPos + 8))};
    float x, y, size;
    const Camera cam = g_camera ? g_camera() : Camera{};
    if (!screen_point(cam, pos, kWorldSize, x, y, size)) {
        drop_locked();
        return true;  // behind the camera: nothing to show (the card would not show either)
    }
    size = std::min(size, 600.0f);
    const hud::Handle list = hud::store().begin(kOwner, 0);
    if (!list) return true;
    hud::Command cmd;
    cmd.x = x - size / 2, cmd.y = y - size / 2, cmd.w = size, cmd.h = size;
    cmd.anchor = hud::Anchor::Center;
    if (!hud::store().picture(kOwner, list, g_image, cmd)) {
        hud::store().cancel(kOwner, list);
        return true;
    }
    hud::store().commit(kOwner, list);
    if (!g_published) LOG("[items] item get: the icon of item %04X over Link's head", g_key);
    g_published = true;
    g_published_step = step;
    return true;
}

}  // namespace

void configure(void (*original)(Cpu*, uint32_t), Camera (*camera)(), Picture (*load)(const std::string&)) {
    std::lock_guard lock(g_mu);
    g_original = original, g_camera = camera, g_load = load;
}
void show(uint16_t key, const std::string& icon_png) {
    std::lock_guard lock(g_mu);
    if (key != g_key || icon_png != g_png) forget_image_locked(), g_failed = false;
    drop_locked();
    g_key = key, g_png = icon_png;
}
void hide() {
    std::lock_guard lock(g_mu);
    drop_locked();
    forget_image_locked();
    g_key = 0, g_png.clear(), g_failed = false;
}
uint16_t showing() {
    std::lock_guard lock(g_mu);
    return g_key;
}
bool install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_installed) return true;
    if (!g_original) return false;
    const std::string other = replaced_by(kDemoItemDraw);
    if (!other.empty()) {
        LOG("[items] %s replaces %08X: a mod item from a chest shows the game's stand-in", other.c_str(), kDemoItemDraw);
        return false;
    }
    replace(kDemoItemDraw, hooks::demo_item_draw);
    g_installed = true;
    return true;
}
bool installed() { return g_installed; }
void logic(uint64_t step) {
    std::lock_guard lock(g_mu);
    // the demo item is drawn every step it shows; two steps without a draw: it is hidden or gone
    if (g_published && step > g_drawn_step + 2) drop_locked();
}
void state_loaded() {
    std::lock_guard lock(g_mu);
    // the HUD layer was reset (guest_mods::state_loaded): its lists and pictures are gone
    g_image = 0, g_published = false, g_published_step = ~0ull, g_failed = false;
}
void reset() {
    std::lock_guard lock(g_mu);
    g_original = nullptr, g_camera = nullptr, g_load = nullptr, g_installed = false;
    g_key = 0, g_png.clear(), g_image = 0, g_failed = false, g_published = false, g_drawn_step = 0, g_published_step = ~0ull;
}

namespace hooks {
void demo_item_draw(Cpu* c) {
    if (story_item_models::draw_demo_item(c)) return;  // a held model in the stand-in's place
    const uint32_t item = c->r[3];
    if (showing() && guest_span(item, kDemoItemBytes) && ld8(item + kDemoItemNo) == story_treasure_chest::kStandIn && publish(item)) {
        c->r[3] = 1;  // drawn (the stand-in's card is not)
        return;
    }
    g_original(c, kDemoItemDraw);
}
}  // namespace hooks

}  // namespace story_item_get_icon
