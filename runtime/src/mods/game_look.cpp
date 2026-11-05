// The game's look for the port's own pages (game_look.h). Moved here from story_notebook.cpp so the
// notebook and the item menu's settings page share it.
#include "game_look.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "runtime.h"

namespace game_look {

float Look::measure(const std::string& text, float size) const {
    const int h = main.cell_height;
    return h ? float(font::Atlas::line_width(text, main, &fallback, h)) * size * font_scale / float(h) : 0.0f;
}

bool Look::load_glyphs(Cpu* c, const std::vector<std::string>& lines) {
    if (!ui || !ui->glyphs || !c || font_failed) return false;
    bool became_ready = false;
    std::vector<uint32_t> want;
    for (const std::string& l : lines)
        for (uint32_t cp : font::codes(l))
            if (!font::Atlas::button(cp) && !font::style_code(cp) && !main.glyphs.count(cp) && !main.missing.count(cp) &&
                std::find(want.begin(), want.end(), cp) == want.end())
                want.push_back(cp);
    if (!main.cell_height) want.push_back('?');  // the stand-in for characters no font has
    bool message_wanted = false;
    for (const std::string& l : lines)
        if (font::has_style(l, font::kMessage))
            for (uint32_t cp : font::codes(l))
                message_wanted |= !font::style_code(cp) && !font::Atlas::button(cp) && !fallback.glyphs.count(cp) && !fallback.missing.count(cp);
    if (!want.empty() || message_wanted) {
        const size_t before = main.glyphs.size();
        if (!ui->glyphs(c, 0, want, main) || !main.ready()) {
            font_failed = true;
            LOG("[%s] the game's menu font can't be read: the page uses the overlay font", tag.c_str());
            return false;
        }
        std::vector<uint32_t> lacking;
        for (uint32_t cp : want)
            if (main.missing.count(cp) && !fallback.glyphs.count(cp) && !fallback.missing.count(cp)) lacking.push_back(cp);
        // the lines in the message font: all their characters from it
        for (const std::string& l : lines)
            if (font::has_style(l, font::kMessage))
                for (uint32_t cp : font::codes(l))
                    if (!font::Atlas::button(cp) && !font::style_code(cp) && !fallback.glyphs.count(cp) && !fallback.missing.count(cp) &&
                        std::find(lacking.begin(), lacking.end(), cp) == lacking.end())
                        lacking.push_back(cp);
        if (!lacking.empty()) ui->glyphs(c, 1, lacking, fallback);
        if (main.glyphs.size() != before || !lacking.empty()) fonts_key++;
        if (before == 0) {
            became_ready = true;
            LOG("[%s] the page uses the game's menu font (cell height %d)", tag.c_str(), main.cell_height);
        }
    }
    if (!icons_probed) {  // the button pictures: the font that has the game's A/B icons
        icons_probed = true;
        for (int slot : {1, 4, 0}) {
            font::Face f;
            if (ui->glyphs(c, slot, {font::kButtonA, font::kButtonB, font::kButtonA + 2, font::kButtonA + 3}, f) && f.glyphs.count(font::kButtonA) &&
                f.glyphs.count(font::kButtonB)) {
                icons = std::move(f);
                fonts_key++;
                LOG("[%s] the button pictures come from font slot %d (%s)", tag.c_str(), slot,
                    icons.glyphs[font::kButtonA].colour ? "colour icons" : "coverage");
                break;
            }
        }
        if (!icons.ready()) LOG("[%s] no game font has the A/B icons: the page draws its own", tag.c_str());
    }
    return became_ready;
}

std::vector<Command> Look::render(std::vector<Command> cmds, std::vector<Handle>& images, uint32_t screen) {
    auto& store = guestmods::hud::store();
    const uint64_t generation = store.state_generation();
    cmds = style::resolve(cmds, &art);  // each box uses its art when that piece was read
    const bool have_font = main.ready();
    // screen pixels per page unit (the page is laid out in TV units; the GamePad shows 854 / 1280 of them)
    const float pixels = store.pixels(screen) * (screen == 1 ? 854.0f / 1280.0f : 1.0f);
    static float traced = -1;
    if (trace && trace() && std::fabs(pixels - traced) > 0.01f)
        LOG("[%s] text composed at %.2f screen pixels per page unit (screen %u)", tag.c_str(), traced = pixels, screen);
    if (have_font) {
        const std::vector<std::string> lines = font::lines_of(cmds, font_scale, pixels);
        atlas.update(lines, main, &fallback, icons.ready() ? &icons : nullptr, prompts, fonts_key, atlas_scale, atlas_budget);
        if (atlas.too_tall() && !told_too_tall) {
            told_too_tall = true;
            LOG("[%s] a line is taller than a text page (%d pixels): it is drawn scaled up", tag.c_str(), font::Atlas::kMaxHeight);
        }
        sync_pages(generation);
    }
    std::map<const guestmods::hud::Image*, Handle> page_handle;
    for (const PageImage& p : page_images)
        if (p.handle) page_handle[p.image.get()] = p.handle;
    auto drawable = [&](int page) { return page_handle.count(atlas.image(size_t(page)).get()) != 0; };
    cmds = font::to_pictures(cmds, have_font ? &atlas : nullptr, font_scale, pixels, drawable);
    images.assign(cmds.size(), 0);
    for (size_t i = 0; i < cmds.size(); i++) {
        Command& c = cmds[i];
        if (c.kind != Command::Picture) continue;
        if (c.image) {  // a line of text: its atlas page's image
            auto it = page_handle.find(c.image.get());
            images[i] = it == page_handle.end() ? 0 : it->second;
            c.image.reset();
            continue;
        }
        if (c.text.rfind("art:", 0) != 0) {
            if (picture_handle && !c.text.empty()) images[i] = picture_handle(c.text), c.text.clear();
            continue;
        }
        int bw, bh;  // the size the box was laid out at (the animations scale only the picture)
        style::box_size(c.text, bw, bh);
        const std::string name = "nb:" + style::box_name(c.text).substr(4);
        const float res = box_resolution ? box_resolution(name) : 0.75f;
        const int w = int(std::lround(bw * res)), h = int(std::lround(bh * res));
        const std::string key = name + ":" + std::to_string(w) + "x" + std::to_string(h);
        Box& box = boxes[key];
        if (!box.image) box.image = style::compose(art, name, w, h);
        if (box.image && (!box.handle || box.store_generation != generation)) {
            box.handle = store.create_image(owner, box.image->width, box.image->height, box.image->width * 4, box.image->rgba.data(),
                                            box.image->rgba.size());
            box.store_generation = generation;
        }
        images[i] = box.handle;
        c.text.clear();
    }
    return cmds;
}

// The store's images of the atlas pages: a dropped page's image released first, then a new page's
// created (a state load: the store forgot them all, so every page's again)
void Look::sync_pages(uint64_t generation) {
    auto& store = guestmods::hud::store();
    const bool forgotten = store_generation != generation;
    store_generation = generation;
    // room for the pages and as much again still on the screen (guest_hud.h caps it at kMaxPortPixels)
    store.raise_pixel_quota(owner, guestmods::hud::kMaxPixels + 2 * atlas.bytes());
    page_images.resize(std::max(page_images.size(), atlas.pages()));
    for (size_t i = 0; i < page_images.size(); i++) {
        PageImage& p = page_images[i];
        const auto img = atlas.image(i);
        if (p.image == img && !forgotten) continue;
        if (p.handle && !forgotten) store.release(owner, p.handle);
        p = {img, 0};
    }
    for (size_t i = 0; i < page_images.size(); i++) {
        PageImage& p = page_images[i];
        if (!p.image || p.handle) continue;
        p.handle = store.create_image(owner, p.image->width, p.image->height, p.image->width * 4, p.image->rgba.data(), p.image->rgba.size());
        if (!p.handle && !told_quota) {
            told_quota = true;
            LOG("[%s] text page %zu (%ux%u) doesn't fit the HUD image quota now: its lines use the overlay font until it does", tag.c_str(), i,
                p.image->width, p.image->height);
        }
    }
    while (!page_images.empty() && !page_images.back().image) page_images.pop_back();
}

void Look::append(Handle list, std::vector<Command>& cmds, const std::vector<Handle>& images) const {
    auto& store = guestmods::hud::store();
    for (size_t i = 0; i < cmds.size(); i++) {
        if (cmds[i].kind == Command::Picture) {
            if (images[i]) store.picture(owner, list, images[i], std::move(cmds[i]));
            continue;
        }
        store.append(owner, list, std::move(cmds[i]));
    }
}

}  // namespace game_look
