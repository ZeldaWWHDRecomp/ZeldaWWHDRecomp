#include "file_select_quit.h"
#include "button_glyph_art.h"
#include "button_glyphs.h"
#include "game_glyph.h"
#include "game_languages.h"
#include "overlay/game_font.h"
#include "guest_addr.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "input_map.h"
#include "mods/game_layouts.h"
#include "overlay/hostui.h"
#include "overlay/overlay.h"
#include "runtime.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

namespace file_select_quit {
namespace {
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::atomic<double> last_idle{0};
std::atomic<bool> ask{false}, modal{false};
std::atomic<uint32_t> presses{0};
std::atomic<bool> tv_visible{true};
std::mutex view_mutex;
Rect tv_region{0, 0, 0, 0};
View presented_view;
std::mutex input_mutex;
Routing routing;
struct Letter {
    int width = 0, height = 0, left = 0, advance = 0;
};
struct Font {
    static constexpr int cell = 128, width = 2048, height = 896;
    std::array<Letter, 108> letters{};
    std::vector<uint8_t> pixels;
    int cell_height = 1, nominal_width = 1, nominal_height = 1;
    bool icons = false;
};
std::mutex font_mutex;
std::shared_ptr<const Font> font;
std::shared_ptr<const Font> rendered_font;
std::unique_ptr<ImTextureData> texture;
std::array<std::unique_ptr<ImTextureData>, 9> art;
std::set<ImTextureData *> srgb_art;
std::atomic<bool> loading_font{false};
game_layouts::TextStyle button_style{42, 48, 0, 2, true};
// Bake the native picture's vertex gradient too: its cyan lower vertices
// are what turn the authored green button plate darker toward the bottom.
void apply_vertices(game_layouts::Image &image, const game_layouts::Pane &pane) {
    auto decode = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
    auto encode = [](float v) { return v <= .0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - .055f; };
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const float u = (x + .5f) / image.width, v = (y + .5f) / image.height;
            auto component = [](const game_layouts::Colour &c, int channel) {
                return float(channel == 0 ? c.r : channel == 1 ? c.g : channel == 2 ? c.b : c.a) / 255;
            };
            for (int channel = 0; channel < 4; ++channel) {
                const float top =
                    component(pane.colours[0], channel) * (1 - u) + component(pane.colours[1], channel) * u;
                const float bottom =
                    component(pane.colours[2], channel) * (1 - u) + component(pane.colours[3], channel) * u;
                auto &pixel = image.rgba[(y * image.width + x) * 4 + channel];
                float value = pixel / 255.f;
                if (channel < 3 && image.srgb)
                    value = decode(value);
                value *= top * (1 - v) + bottom * v;
                if (channel < 3 && image.srgb)
                    value = encode(value);
                pixel = uint8_t(std::clamp(std::lround(value * 255), 0l, 255l));
            }
        }
}
std::unique_ptr<ImTextureData> native_image(const char *spec, const char *pane_name = nullptr,
                                            const char *modulator = nullptr) {
    game_layouts::Image image;
    std::string error;
    if (!game_layouts::texture(spec, image, error)) {
        LOG("[file-select-quit] native art: %s", error.c_str());
        return {};
    }
    game_layouts::Pane pane;
    if (pane_name && game_layouts::pane(pane_name, pane))
        apply_vertices(image, pane);
    // A selected native plate multiplies its cloth/mask by a second,
    // coloured highlight texture. Bake those two samplers into one host image.
    if (modulator) {
        game_layouts::Image light;
        if (!game_layouts::texture(modulator, light, error)) {
            LOG("[file-select-quit] native highlight: %s", error.c_str());
            return {};
        }
        auto linear = [](float v, bool srgb) {
            return !srgb ? v : v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
        };
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x) {
                const float u = (x + .5f) * light.width / image.width - .5f;
                const float v = (y + .5f) * light.height / image.height - .5f;
                const int ix = int(std::floor(u)), iy = int(std::floor(v));
                const float fx = u - ix, fy = v - iy;
                for (int channel = 0; channel < 4; ++channel) {
                    float sample = 0;
                    for (int tap = 0; tap < 4; ++tap) {
                        const int lx = std::clamp(ix + (tap & 1), 0, int(light.width) - 1);
                        const int ly = std::clamp(iy + (tap >> 1), 0, int(light.height) - 1);
                        const float value = light.rgba[(ly * light.width + lx) * 4 + channel] / 255.f;
                        sample += linear(value, channel < 3 && light.srgb) *
                                  ((tap & 1) ? fx : 1 - fx) * ((tap >> 1) ? fy : 1 - fy);
                    }
                    auto& pixel = image.rgba[(y * image.width + x) * 4 + channel];
                    float value = linear(pixel / 255.f, channel < 3 && image.srgb) * sample;
                    if (channel < 3 && image.srgb)
                        value = value <= .0031308f ? value * 12.92f : 1.055f * std::pow(value, 1 / 2.4f) - .055f;
                    pixel = uint8_t(std::clamp(std::lround(value * 255), 0l, 255l));
                }
            }
    }
    auto result = std::make_unique<ImTextureData>();
    result->Create(ImTextureFormat_RGBA32, image.width, image.height);
    result->UseColors = true;
    std::memcpy(result->GetPixels(), image.rgba.data(), image.rgba.size());
    if (image.srgb)
        srgb_art.insert(result.get());
    ImGui::RegisterUserTexture(result.get());
    return result;
}
void load_art() {
    if (art[0])
        return;
    game_layouts::Pane text;
    if (game_layouts::pane("BtnSequence_00/T_Btn_00", text) && text.text.height > 0)
        button_style = text.text;
    art[0] = native_image("BtnSequence_00/BtnSequenceShadow_00^t#P_BtnShadow_00", "BtnSequence_00/P_BtnShadow_00");
    art[1] = native_image("BtnSequence_00/BtnSequencePattern_00^t#P_Btn_00", "BtnSequence_00/P_Btn_00");
    art[2] = native_image("BtnSequence_00/BtnSequenceSpec_00^q", "BtnSequence_00/P_Btn_00");
    art[3] = native_image("SequenceWindow_00/PaperPattern_00^t#W_Base_00C");
    art[4] = native_image("SequenceWindow_00/WaveWindowLT_00^t#W_Base_00LT");
    art[5] = native_image("SequenceWindow_00/WaveWindowT_00^t#W_Base_00LT");
    art[6] = native_image("SequenceWindow_00/WaveWindowL_00^t#W_Base_00LT");
    art[7] = native_image("Cursor_00/Crsor_00^t#W_Cursor_00LT");
    art[8] = native_image("BtnSequence_00/BtnSequencePattern_00^t#P_BtnSelect_00",
                          "BtnSequence_00/P_BtnSelect_00", "BtnSequence_00/BtnSelect_00^q");
}
bool object(uint32_t p) { return p >= mem::kMem2Start && p < mem::kMem2End - 0x400; }
bool state_is(Cpu *c, uint32_t machine, uint32_t expected) {
    uint32_t state = guest_call(c, GC(0x02006478), {machine});
    if (!object(state))
        return false;
    uint32_t vt = ld32(state + 8), ev = ld32(expected + 8);
    if (!object(vt) || !object(ev))
        return false;
    return guest_call(c, ld32(vt + 0x14), {state}) == guest_call(c, ld32(ev + 0x14), {expected});
}
void load_font(Cpu *cpu) {
    {
        std::lock_guard lock(font_mutex);
        if (font)
            return;
    }
    if (loading_font.exchange(true))
        return;
    struct Loading {
        ~Loading() { loading_font = false; }
    } loading;
    const uint32_t manager = ld32(GD(0x101F4A50));
    if (!object(manager)) {
        LOG("[file-select-quit] no font manager %08x", manager);
        return;
    }
    const uint32_t native = ld32(manager + 0x14); // CKingMsg: the native BtnSequence button font
    if (!object(native) || !object(ld32(native + 4))) {
        LOG("[file-select-quit] no native font %08x", native);
        return;
    }
    const uint32_t get = ld32(ld32(native + 4) + 0x8C);
    if (!get)
        return;
    thread_local uint32_t scratch = mem::host_alloc(0x18, 16);
    auto next = std::make_shared<Font>();
    next->pixels.resize(Font::width * Font::height * 4);
    for (unsigned ch = 32; ch < 127; ++ch) {
        if (!std::strchr("Quit the game?YesNoABXY", ch))
            continue;
        std::memset(mem::ptr(scratch), 0, 0x18);
        Cpu c = *cpu;
        c.r[3] = native;
        c.r[4] = scratch;
        c.r[5] = ch;
        c.pc = get;
        c.ctr = get;
        ppc_dispatch(&c);
        game_glyph::Glyph glyph;
        if (!game_glyph::read_glyph(scratch, glyph) || glyph.bitmap.width > Font::cell ||
            glyph.bitmap.height > Font::cell)
            return;
        const unsigned i = ch - 32, x0 = (i % 16) * Font::cell, y0 = (i / 16) * Font::cell;
        next->letters[i] = {glyph.bitmap.width, glyph.bitmap.height, glyph.left, glyph.advance};
        next->cell_height = std::max(next->cell_height, glyph.bitmap.height);
        for (int y = 0; y < glyph.bitmap.height; ++y)
            for (int x = 0; x < glyph.bitmap.width; ++x) {
                auto *p = next->pixels.data() + ((y0 + y) * Font::width + x0 + x) * 4;
                std::memcpy(p, glyph.bitmap.rgba.data() + (y * glyph.bitmap.width + x) * 4, 4);
                // The message font encodes its border at half coverage. The
                // plain button lettering uses the upper half of that mask;
                // tinting the entire mask also paints a heavy coloured border.
                p[0] = p[1] = p[2] = 255;
                p[3] = uint8_t(std::clamp((int(p[3]) - 128) * 255 / 127, 0, 255));
            }
    }
    // Native icon glyphs come from the loaded icon font, just as the game's
    // A/B prompts do. PlayStation cells use the port's shared glyph artwork.
    for (int slot = 2; slot <= 6 && !next->icons; ++slot) {
        const uint32_t icon_font = ld32(manager + 0x10 + slot * 4);
        if (!object(icon_font) || !object(ld32(icon_font + 4)))
            continue;
        const uint32_t icon_get = ld32(ld32(icon_font + 4) + 0x8C);
        if (!icon_get)
            continue;
        bool complete = true;
        for (unsigned face = 0; face < 4; ++face) {
            Cpu c = *cpu;
            c.r[3] = icon_font;
            c.r[4] = scratch;
            c.r[5] = 0xE000 + face;
            c.pc = icon_get;
            c.ctr = icon_get;
            ppc_dispatch(&c);
            game_glyph::Glyph glyph;
            if (!game_glyph::read_glyph(scratch, glyph) || !glyph.colour || glyph.bitmap.width > Font::cell ||
                glyph.bitmap.height > Font::cell) {
                complete = false;
                break;
            }
            const unsigned i = 96 + face, x0 = (i % 16) * Font::cell, y0 = (i / 16) * Font::cell;
            next->letters[i] = {glyph.bitmap.width, glyph.bitmap.height, 0, 0};
            for (int y = 0; y < glyph.bitmap.height; ++y)
                for (int x = 0; x < glyph.bitmap.width; ++x)
                    std::memcpy(next->pixels.data() + ((y0 + y) * Font::width + x0 + x) * 4,
                                glyph.bitmap.rgba.data() + (y * glyph.bitmap.width + x) * 4, 4);
        }
        next->icons = complete;
    }
    for (unsigned face = 0; face < 8; ++face) {
        const unsigned i = 100 + face, x0 = (i % 16) * Font::cell, y0 = (i / 16) * Font::cell;
        next->letters[i] = {64, 64, 0, 0};
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const uint32_t rgba = button_glyphs::face_shape(face % 4, face >= 4, true, x, y);
                auto *p = next->pixels.data() + ((y0 + y) * Font::width + x0 + x) * 4;
                p[0] = rgba >> 24;
                p[1] = rgba >> 16;
                p[2] = rgba >> 8;
                p[3] = rgba;
            }
    }
    const auto metrics = game_font::message_metrics(game_lang::started());
    next->nominal_width = metrics.width > 0 ? metrics.width : next->cell_height;
    next->nominal_height = metrics.height > 0 ? metrics.height : next->cell_height;
    {
        std::lock_guard lock(font_mutex);
        font = std::move(next);
    }
    LOG("[file-select-quit] loaded runtime menu font");
}
float text_width(const char *s, float size) {
    float w = 0;
    for (; *s; ++s)
        if (*s >= 32 && *s < 127)
            w += rendered_font->letters[*s - 32].advance;
    return w * size / rendered_font->nominal_width;
}
void label(ImDrawList *d, const char *s, ImVec2 center, float size, ImU32 color, float aspect = 1) {
    float x = center.x - text_width(s, size) * aspect * .5f;
    const float scale = size / rendered_font->nominal_height;
    const float sx = size * aspect / rendered_font->nominal_width;
    for (; *s; ++s) {
        if (*s < 32 || *s >= 127)
            continue;
        const unsigned i = *s - 32;
        const auto &g = rendered_font->letters[i];
        ImVec2 a(x + g.left * sx, center.y - g.height * scale * .5f),
            b(a.x + g.width * sx, a.y + g.height * scale);
        ImVec2 uv((i % 16) * float(Font::cell) / Font::width, (i / 16) * float(Font::cell) / Font::height);
        ImVec2 end(uv.x + g.width / float(Font::width), uv.y + g.height / float(Font::height));
        d->AddImage(texture->GetTexRef(), a, b, uv, end, color);
        x += g.advance * sx;
    }
}
// Layout colours are linear; the overlay's packed vertex colours are sRGB.
ImU32 text_colour(game_layouts::Colour c) {
    auto srgb = [](uint8_t v) {
        const float f = v / 255.f;
        return int(std::lround(255 * (f <= .0031308f ? f * 12.92f : 1.055f * std::pow(f, 1 / 2.4f) - .055f)));
    };
    return IM_COL32(srgb(c.r), srgb(c.g), srgb(c.b), c.a);
}
void button_label(ImDrawList* d, const char* name, ImVec2 center, float scale) {
    const float size = button_style.height * scale;
    const float aspect = button_style.width / button_style.height;
    if (button_style.shadow)
        label(d, name, ImVec2(center.x + button_style.shadow_x * scale, center.y + button_style.shadow_y * scale),
              size, text_colour(button_style.shade), aspect);
    label(d, name, center, size, text_colour(button_style.ink), aspect);
}
using Box = Rect;
constexpr Box quit_box = kQuitBox;
bool button(ImDrawList *d, Box b, const char *name, bool selected, float scale, float ox, float oy) {
    auto p = [&](float x, float y) { return ImVec2(ox + x * scale, oy + y * scale); };
    ImGui::SetCursorScreenPos(p(b.x, b.y));
    const bool clicked = ImGui::InvisibleButton(name, ImVec2(b.w * scale, b.h * scale));
    selected |= ImGui::IsItemHovered();
    // Native sequence buttons grow around their centre when selected. This
    // changes only the picture; keep the established pointer target above.
    if (selected) {
        b.x -= b.w * .1f;
        b.y -= b.h * .1f;
        b.w *= 1.2f;
        b.h *= 1.2f;
    }
    const ImVec2 start = p(b.x, b.y), end = p(b.x + b.w, b.y + b.h);
    if (art[0])
        d->AddImage(art[0]->GetTexRef(), start, end);
    if (art[1])
        d->AddImage(art[1]->GetTexRef(), start, end);
    if (art[2])
        d->AddImage(art[2]->GetTexRef(), start, end, ImVec2(0, 0), ImVec2(1, 1),
                    IM_COL32(255, 255, 255, 50));
    if (selected && art[8])
        d->AddImage(art[8]->GetTexRef(), start, end, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 100));
    ImVec2 corners[] = {p(b.x + 8, b.y + 12), p(b.x + b.w - 8, b.y + 8), p(b.x + b.w - 8, b.y + b.h - 12),
                        p(b.x + 8, b.y + b.h - 8)};
    if (selected) {
        for (int i = 0; i < 4; ++i) {
            auto c = corners[i];
            float dx = (i == 0 || i == 3) ? 1 : -1, dy = i < 2 ? 1 : -1;
            const float extent = 32 * scale;
            if (art[7])
                d->AddImage(art[7]->GetTexRef(), ImVec2(c.x - extent * .5f, c.y - extent * .5f),
                            ImVec2(c.x + extent * .5f, c.y + extent * .5f), ImVec2(dx > 0 ? 0 : 1, dy > 0 ? 0 : 1),
                            ImVec2(dx > 0 ? 1 : 0, dy > 0 ? 1 : 0));
        }
    }
    button_label(d, name, p(b.x + b.w * .5f, b.y + b.h * .5f), scale * (selected ? 1.2f : 1.f));
    return clicked;
}
void prompt(ImDrawList *d, ImVec2 center, float radius) {
    const auto look = input_map::active_prompt_style();
    const int face = look.buttons[2];
    if (rendered_font->icons || look.face == input_map::FaceStyle::kPlayStation) {
        const bool shapes = look.face == input_map::FaceStyle::kPlayStation;
        const unsigned i = shapes ? 100 + face + (look.colours ? 4 : 0) : 96 + face;
        const auto &g = rendered_font->letters[i];
        const float w = radius * 2 * g.width / g.height;
        ImVec2 uv((i % 16) * float(Font::cell) / Font::width, (i / 16) * float(Font::cell) / Font::height);
        const uint32_t rgba = button_glyphs::colour(face);
        const ImU32 tint =
            !shapes && look.colours ? IM_COL32(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, 255) : IM_COL32_WHITE;
        d->AddImage(texture->GetTexRef(), ImVec2(center.x - w * .5f, center.y - radius),
                    ImVec2(center.x + w * .5f, center.y + radius), uv,
                    ImVec2(uv.x + g.width / float(Font::width), uv.y + g.height / float(Font::height)), tint);
        return;
    }
    d->AddCircleFilled(center, radius, IM_COL32(228, 229, 193, 255), 40);
    d->AddCircleFilled(
        center, radius * .84f,
        look.face == input_map::FaceStyle::kOriginal ? IM_COL32(8, 14, 6, 255) : IM_COL32(244, 244, 231, 255), 40);
    const char name[] = {"ABXY"[face], 0};
    const uint32_t rgba = button_glyphs::colour(face);
    const ImU32 color = look.colours ? IM_COL32(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, 255)
                        : look.face == input_map::FaceStyle::kOriginal ? IM_COL32(235, 237, 215, 255)
                                                                       : IM_COL32(58, 58, 58, 255);
    label(d, name, center, radius * 1.3f, color);
}
} // namespace
void observe(Cpu *cpu) {
    if (!input_map::file_select_quit())
        return;
    const char *stage = reinterpret_cast<const char *>(mem::ptr(GD(0x1046F0B0) + 0x5134));
    if (std::strncmp(stage, "Name", 8)) {
        last_idle = 0;
        return;
    }
    const uint32_t owner = ld32(GD(0x101F82EC));
    if (!object(owner)) {
        last_idle = 0;
        return;
    }
    Cpu saved = *cpu;
    const uint32_t screen = ld32(owner + 0x134);
    const bool owner_active = object(screen) && state_is(cpu, owner + 0x10, GD(0x1049EA58));
    if (std::getenv("WWHD_TEST_FILE_QUIT_TRACE")) {
        static uint32_t last = ~0u;
        const uint32_t value = object(screen) ? ld32(screen + 0x27C) : ~0u;
        const uint32_t substep = object(screen) ? ld32(screen + 0x270) : 0;
        const bool wait = object(screen) && state_is(cpu, screen + 0x18, GD(0x1049A5E4));
        const uint32_t key = value | (owner_active ? 0x10000 : 0) | (wait ? 0x20000 : 0) | (substep ? 0x40000 : 0);
        if (key != last) {
            LOG("[file-select-quit] native active=%d caption=%u wait=%d step=%u state=%08x flags=%u/%u/%u",
                owner_active, value, wait, substep,
                object(screen) ? guest_call(cpu, GC(0x02006478), {screen + 0x18}) : 0,
                object(screen) ? ld8(screen + 0x1CC) : 0, object(screen) ? ld8(screen + 0x1CD) : 0,
                object(screen) ? ld8(screen + 0x1DC) : 0);
            last = key;
        }
    }
    // SelectSaveData (enter 026CCFD8, execute 026CD740). Its
    // substep stays zero until the native in-animation checker 026CCC94
    // completes. Copy/delete/confirmation use different states and captions.
    const bool idle = object(screen) && owner_active && state_is(cpu, screen + 0x18, GD(0x1049A5E4)) &&
                      ld32(screen + 0x270) != 0 && ld32(screen + 0x27C) == 1 && !ld8(screen + 0x1CC) &&
                      !ld8(screen + 0x1CD) && !ld8(screen + 0x1DC);
    *cpu = saved;
    last_idle = idle ? now() : 0;
    if (idle) {
        load_font(cpu);
        *cpu = saved;
    }
}
static bool context_valid() {
    return input_map::file_select_quit() && !overlay::is_open() && tv_visible.load() && last_idle.load() > 0;
}
bool visible() {
    if (!context_valid())
        return false;
    const double age = now() - last_idle.load();
    if (age < 0 || age > .25)
        return false;
    std::lock_guard lock(font_mutex);
    return bool(font);
}
bool confirming() { return modal.load(); }
void filter(input::PadState &pad) {
    std::lock_guard lock(input_mutex);
    const auto event = routing.route(pad, context_valid(), visible(), modal.load());
    if (event.dismiss) {
        ask = false;
        modal = false;
        presses = 0;
        return;
    }
    if (event.open) {
        ask = true;
        modal = true;
    }
    presses.fetch_or(event.pressed);
}
bool pointer(float x, float y) {
    if (!visible())
        return false;
    std::lock_guard lock(view_mutex);
    return modal.load() || presented_view.contains(x, y);
}
void set_tv_region(float x, float y, float width, float height, bool shown) {
    std::lock_guard lock(view_mutex);
    tv_region = {x, y, width, height};
    tv_visible = shown && width > 0 && height > 0;
}
bool image_texture(uint64_t id) {
    for (const auto *image : srgb_art)
        if (uint64_t(image->GetTexID()) == id)
            return true;
    return false;
}
void backend_destroyed() {
    if (texture && ImGui::GetCurrentContext()) {
        ImGui::GetPlatformIO().Textures.find_erase(texture.get());
        ImGui::UnregisterUserTexture(texture.get());
    }
    for (auto &image : art)
        if (image) {
            if (ImGui::GetCurrentContext()) {
                ImGui::GetPlatformIO().Textures.find_erase(image.get());
                ImGui::UnregisterUserTexture(image.get());
            }
            image.reset();
        }
    texture.reset();
    srgb_art.clear();
    rendered_font.reset();
}
void frame(float width, float height) {
    if (!visible()) {
        if (!context_valid()) {
            ask = false;
            modal = false;
            presses = 0;
        }
        return;
    }
    if (!texture) {
        {
            std::lock_guard lock(font_mutex);
            rendered_font = font;
        }
        texture = std::make_unique<ImTextureData>();
        texture->Create(ImTextureFormat_RGBA32, Font::width, Font::height);
        std::memcpy(texture->GetPixels(), rendered_font->pixels.data(), rendered_font->pixels.size());
        ImGui::RegisterUserTexture(texture.get());
    }
    load_art();
    View view;
    {
        std::lock_guard lock(view_mutex);
        const auto &pixels = ImGui::GetIO().DisplayFramebufferScale;
        Rect region = tv_region;
        if (region.w > 0 && region.h > 0) {
            region.x /= pixels.x;
            region.w /= pixels.x;
            region.y /= pixels.y;
            region.h /= pixels.y;
        } else {
            region = {0, 0, width, height};
        }
        view = {width, height, region};
        presented_view = view;
    }
    const float scale = view.scale(), ox = view.origin_x(), oy = view.origin_y();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##file_select_quit", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    auto *d = ImGui::GetWindowDrawList();
    d->PushClipRect(ImVec2(view.tv.x, view.tv.y), ImVec2(view.tv.x + view.tv.w, view.tv.y + view.tv.h), true);
    const bool opening = ask.exchange(false);
    static bool yes = false;
    if (opening) {
        yes = false;
        presses = 0;
        LOG("[file-select-quit] asking");
    }
    if (!modal.load()) {
        if (button(d, quit_box, "Quit", false, scale, ox, oy)) {
            modal = true;
            yes = false;
            presses = 0;
            LOG("[file-select-quit] mouse: asking");
        }
        prompt(d, ImVec2(ox + (quit_box.x - 30) * scale, oy + (quit_box.y + quit_box.h * .5f) * scale), 23 * scale);
    } else {
        d->AddRectFilled(ImVec2(view.tv.x, view.tv.y), ImVec2(view.tv.x + view.tv.w, view.tv.y + view.tv.h),
                         IM_COL32(0, 0, 0, 100));
        // The same wave-edged paper textures as the native sequence dialogs.
        auto image = [&](int index, float x, float y, float w, float h, bool flip_x = false, bool flip_y = false) {
            if (art[index])
                d->AddImage(art[index]->GetTexRef(), ImVec2(ox + x * scale, oy + y * scale),
                            ImVec2(ox + (x + w) * scale, oy + (y + h) * scale), ImVec2(flip_x ? 1 : 0, flip_y ? 1 : 0),
                            ImVec2(flip_x ? 0 : 1, flip_y ? 0 : 1), IM_COL32(255, 255, 255, 255));
        };
        image(3, 350, 255, 580, 200);
        image(4, 330, 235, 40, 40);
        image(4, 910, 235, 40, 40, true);
        image(4, 330, 435, 40, 40, false, true);
        image(4, 910, 435, 40, 40, true, true);
        image(5, 370, 235, 540, 40);
        image(5, 370, 435, 540, 40, false, true);
        image(6, 330, 275, 40, 160);
        image(6, 910, 275, 40, 160, true);
        button_label(d, "Quit the game?", ImVec2(ox + 640 * scale, oy + 300 * scale), scale);
        const uint32_t pressed = presses.exchange(0);
        if (pressed & input::kLeft)
            yes = false;
        if (pressed & input::kRight)
            yes = true;
        bool cancel = button(d, {385, 355, 240, 100}, "No", !yes, scale, ox, oy);
        const bool quit = button(d, {655, 355, 240, 100}, "Yes", yes, scale, ox, oy) || ((pressed & input::kA) && yes);
        cancel |= (pressed & input::kB) || ((pressed & input::kA) && !yes) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (cancel) {
            modal = false;
            LOG("[file-select-quit] answer: No");
        } else if (quit) {
            modal = false;
            LOG("[file-select-quit] answer: Yes");
            hostui::post([] { hostui::quit(); });
        }
    }
    d->PopClipRect();
    ImGui::End();
    ImGui::PopStyleVar(2);
}
} // namespace file_select_quit
