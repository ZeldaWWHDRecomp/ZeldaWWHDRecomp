// The story notebook's page as a game layout (story_notebook_layout.h).
#include "story_notebook_layout.h"
#include "story_notebook.h"
#include "story_notebook_layout_view.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "lyt_build.h"
#include "guest_png.h"
#include "ppc.h"
#include "runtime.h"
#include "story_lyt_screen.h"
#include "story_notebook_layout_detail.h"
#include "story_lyt_text.h"
#include "story_notebook_page.h"
#include "story_notebook_layout_chart.h"
#include "story_notebook_art.h"

namespace story_notebook_layout {
namespace {
namespace nb = story_notebook;
constexpr const char* kLayout = "StoryNotebook_00";

std::string g_description, g_data_dir;

// the page as the layout shows it (story_notebook_layout_view.h): written by update() on the controller read,
// read by the layout's draw
std::mutex g_mu;
View g_view;            // the latest (g_mu)
detail::DetailView g_detail;  // the quest the detail view shows (g_mu; with g_view.mode 2)
uint64_t g_detail_serial = 0;
chart::ChartView g_chart;  // the detail view's mini chart (g_mu; with g_view.mode 2)
bool g_reset = false;
bool g_drawing = false;  // the layout draws the page (or its closing) now (g_mu)
std::atomic<bool> g_animating{false};  // the page's open or close animation runs (the bag shows the open book)

// ---- the layout's draw (game thread, item screen draw) ----
struct Shown {
    int mode = 0, tab = -1, selected = -2;
    uint64_t serial = ~0ull, detail_serial = ~0ull;
    bool closing = false, opening = false;
    float cursor_y = 0, cursor_from = 0;
    int glide = 0;
    std::map<std::string, std::string> texts;  // what each text pane holds
};
Shown g_shown;

float pane_f(uint32_t pane, uint32_t off) { return pane ? u32_as_f32(ld32(pane + off)) : 0.0f; }

void set_text(Cpu* c, const char* name, const std::string& s) {
    auto& now = g_shown.texts[name];
    if (now == s) return;
    now = s;
    if (const uint32_t p = story_lyt_screen::pane(c, name)) story_lyt_text::set_text(c, p, s);
}
void show(Cpu* c, const char* name, bool on) { story_lyt_screen::set_visible(story_lyt_screen::pane(c, name), on); }
std::string row_name(int i, const char* part) { return "N_Row" + std::to_string(i) + part; }
std::string row_pane(const char* kind, int i, const char* part) { return std::string(kind) + "_Row" + std::to_string(i) + part; }

std::u16string utf16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = (unsigned char)s[i];
        const size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        uint32_t cp = n == 1 ? b : n == 2 ? b & 31 : n == 3 ? b & 15 : b & 7;
        for (size_t k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 63);
        i += n;
        if (cp >= 0x10000) out += char16_t(0xD800 + ((cp - 0x10000) >> 10)), out += char16_t(0xDC00 + ((cp - 0x10000) & 0x3FF));
        else out += char16_t(cp);
    }
    return out;
}

// the text, cut with "..." to its pane's width: measured with the pane's own font at its size (the game's
// measure, story_lyt_text::text_width)
std::string fitted(Cpu* c, const char* name, const std::string& text) {
    const uint32_t pane = story_lyt_screen::pane(c, name);
    const float w = pane_f(pane, 0x3C);
    if (!pane || w <= 0 || story_lyt_text::text_width(c, pane, utf16(text)) <= w) return text;
    std::string best;
    for (size_t i = 0; i < text.size();) {
        const size_t next = std::min(text.size(), i + nb::char_bytes((unsigned char)text[i]));
        if (story_lyt_text::text_width(c, pane, utf16(text.substr(0, next) + "...")) > w) break;
        best = text.substr(0, next);
        i = next;
    }
    return best + "...";
}

// test aid: WWHD_NOTEBOOK_LAYOUT_TEST_PORTRAIT=<png> gives every quest that picture as its portrait
const char* test_portrait() {
    static const char* p = [] { const char* e = getenv("WWHD_NOTEBOOK_LAYOUT_TEST_PORTRAIT"); return e && *e ? e : nullptr; }();
    return p;
}

// A quest's portrait in a portrait slot of the layout (NbPortrait<slot>_00: 64 x 64), written into the
// slot texture once per quest (story_lyt_screen::set_texture_pixels); false if it can't be shown
constexpr uint32_t kPortraitSize = 64;
uint16_t g_slot_key[8] = {};
}  // namespace
bool portrait_into(int slot, uint16_t key) {
    if (slot < 0 || slot >= 8) return false;
    if (g_slot_key[slot] == key) return true;
    guestmods::hud::Image img;
    if (const char* t = test_portrait()) {
        try {
            const auto px = guestmods::hud::load_png(std::filesystem::path(t).parent_path(), std::filesystem::path(t).filename().string());
            img.width = px.width, img.height = px.height, img.rgba = px.rgba;
        } catch (const std::exception&) {
        }
    } else if (!nb::portrait_pixels(key, img)) {
        return false;
    }
    if (!img.width || !img.height) return false;
    game_glyph::Bitmap b;
    b.width = int(img.width), b.height = int(img.height), b.rgba = img.rgba;
    const game_glyph::Bitmap fit = nb::art::resized(b, int(kPortraitSize), int(kPortraitSize));
    const std::string file = "NbPortrait" + std::to_string(slot) + "_00^l.bflim";
    if (!story_lyt_screen::set_texture_pixels(file, fit.rgba.data(), kPortraitSize, kPortraitSize)) {
        LOG("[notebook] layout: portrait %04X not written into %s", key, file.c_str());
        return false;
    }
    g_slot_key[slot] = key;
    return true;
}
namespace {

void apply(Cpu* c, const View& v) {
    for (int i = 0; i < 2; i++) {
        const bool on = i == v.tab;
        const std::string n = std::to_string(i);
        show(c, ("P_Tab" + n + "On").c_str(), on);
        show(c, ("P_Tab" + n + "Off").c_str(), !on);
        show(c, ("T_Tab" + n + "On").c_str(), on);
        show(c, ("T_Tab" + n + "Off").c_str(), !on);
        set_text(c, ("T_Tab" + n + "On").c_str(), v.tab_labels[i]);
        set_text(c, ("T_Tab" + n + "Off").c_str(), v.tab_labels[i]);
    }
    for (int i = 0; i < kRows; i++) {
        const bool has = i < int(v.rows.size());
        show(c, row_name(i, "").c_str(), has);
        if (!has) continue;
        const RowView& r = v.rows[size_t(i)];
        const std::string title = row_pane("T", i, r.dim ? "TitleDim" : "Title"), next = row_pane("T", i, r.dim ? "NextDim" : "Next");
        show(c, row_pane("T", i, "Title").c_str(), !r.dim);
        show(c, row_pane("T", i, "TitleDim").c_str(), r.dim);
        show(c, row_pane("T", i, "Next").c_str(), !r.dim);
        show(c, row_pane("T", i, "NextDim").c_str(), r.dim);
        set_text(c, title.c_str(), fitted(c, title.c_str(), r.title));
        set_text(c, next.c_str(), fitted(c, next.c_str(), r.next));
        set_text(c, row_pane("T", i, "Status").c_str(), r.status);
        set_text(c, row_pane("T", i, "Initial").c_str(), r.initial);
        const bool portrait = r.portrait && portrait_into(i, r.portrait);
        show(c, row_pane("P", i, "Portrait").c_str(), portrait);
        show(c, row_pane("T", i, "Initial").c_str(), r.initial_shown || (r.portrait && !portrait));
        show(c, row_pane("P", i, "PillNew").c_str(), r.pill == 1);
        show(c, row_pane("P", i, "PillNotYet").c_str(), r.pill == 2);
        show(c, row_pane("P", i, "PillDone").c_str(), r.pill == 3);
        show(c, row_pane("P", i, "Ring").c_str(), r.tracked);
        show(c, row_pane("P", i, "Wash").c_str(), i == v.selected);
    }
    show(c, "T_Empty1", v.empty);
    show(c, "T_Empty2", v.empty && !v.empty2.empty());
    set_text(c, "T_Empty1", v.empty1);
    set_text(c, "T_Empty2", v.empty2);
    show(c, "T_Up", v.up);
    show(c, "T_Down", v.down);
    show(c, "N_PlateTrack", v.track_plate);
    show(c, "N_Cursor", v.selected >= 0);
}

// where the cursor (its null pane, in N_Content) is for row i: its built place (row 0), one row step down a row
float g_cursor_y0 = 0, g_row_step = 0;
float cursor_y_for(int i) { return g_cursor_y0 - g_row_step * float(i); }

bool frame_inner(Cpu* c, uint32_t);
// the frame, and whether the page's open or close animation still runs
bool frame(Cpu* c, uint32_t lyt) {
    const bool drawn = frame_inner(c, lyt);
    if (g_shown.opening && (!drawn || story_lyt_screen::ended(c, "Open"))) g_shown.opening = false;
    g_animating.store(drawn && (g_shown.opening || g_shown.closing), std::memory_order_relaxed);
    return drawn;
}

bool frame_inner(Cpu* c, uint32_t) {
    View v;
    bool drawing;
    detail::DetailView dv;
    uint64_t dserial;
    chart::ChartView cv;
    {
        std::lock_guard lock(g_mu);
        if (g_reset) g_shown = Shown{}, g_reset = false;
        v = g_view;
        drawing = g_drawing;
        dv = g_detail;
        dserial = g_detail_serial;
        cv = g_chart;
    }
    if (!drawing && !g_shown.closing) return false;
    // the detail view: N_Detail instead of the list's content and tabs, the plates' prompts for it
    auto list_parts = [&](bool on) {
        show(c, "N_Content", on);
        for (const char* n : {"P_Tab0On", "P_Tab0Off", "P_Tab1On", "P_Tab1Off", "T_Tab0On", "T_Tab0Off", "T_Tab1On", "T_Tab1Off"})
            if (!on) show(c, n, false);
        show(c, "N_Detail", !on);
    };
    if (v.mode == 2) {
        if (g_shown.mode != 2) {
            if (g_shown.mode == 0) story_lyt_screen::play(c, "Open"), g_shown.opening = true;
            list_parts(false);
            story_lyt_screen::play(c, "DetailIn");
            story_lyt_screen::play(c, "TrackBreathe");
            g_shown.closing = false;
            g_shown.detail_serial = ~0ull;
        }
        if (dserial != g_shown.detail_serial) {
            detail::apply(c, dv);
            detail::place_prompts(c, dv.back_label, dv.track_label, dv.track_plate);
            show(c, "N_PlateTrack", dv.track_plate);
            g_shown.detail_serial = dserial;
        }
        chart::apply(c, cv);  // Link and the place move every frame (only changes are written)
        g_shown.mode = 2;
        return true;
    }
    if (v.mode == 1 && g_shown.mode != 1) {  // the page opens (or comes back from the detail view)
        list_parts(true);
        detail::place_prompts(c, "Close", "Open", false);
        g_shown.serial = ~0ull;  // the tabs and the plates as the list has them
        if (g_shown.mode == 0) {
            story_lyt_screen::play(c, "Open"), g_shown.opening = true;
            story_lyt_screen::play(c, "Breathe");
        } else {
            story_lyt_screen::play(c, "TurnLeft");
        }
        g_shown.closing = false;
        g_shown.selected = -2;
    }
    if (v.mode == 0 && (g_shown.mode == 1 || g_shown.mode == 2)) {  // it closes: the closing animation, then nothing
        story_lyt_screen::play(c, "Close");
        g_shown.closing = true;
        g_shown.mode = 0;
    }
    if (v.mode == 0 && g_shown.closing && story_lyt_screen::ended(c, "Close")) {
        g_shown.closing = false;
        g_shown.mode = 0;
        std::lock_guard lock(g_mu);
        g_drawing = false;
        return false;
    }
    if (v.mode == 1) {
        if (g_shown.mode == 1 && v.tab != g_shown.tab) story_lyt_screen::play(c, v.tab > g_shown.tab ? "TurnRight" : "TurnLeft");
        if (v.serial != g_shown.serial) apply(c, v), g_shown.serial = v.serial;
        // the cursor glides to the selected row (0.1 s); the row bumps
        if (v.selected >= 0) {
            const uint32_t cursor = story_lyt_screen::pane(c, "N_Cursor");
            const float to = cursor_y_for(v.selected);
            if (v.selected != g_shown.selected) {
                if (g_shown.selected >= 0) story_lyt_screen::play(c, "Bump" + std::to_string(v.selected));
                g_shown.cursor_from = g_shown.selected >= 0 ? g_shown.cursor_y : to;
                g_shown.glide = 0;
                g_shown.selected = v.selected;
            }
            const int steps = 3;
            float t = std::min(1.0f, float(++g_shown.glide) / steps);
            t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            g_shown.cursor_y = g_shown.cursor_from + (to - g_shown.cursor_from) * t;
            story_lyt_screen::set_translate(cursor, pane_f(cursor, 0x1C), g_shown.cursor_y);
        }
        g_shown.tab = v.tab;
    }
    if (v.mode != 0) g_shown.mode = v.mode;
    return g_shown.mode == 1 || g_shown.closing;
}

void built(Cpu* c, uint32_t) {
    g_shown = Shown{};
    for (uint16_t& k : g_slot_key) k = 0;  // a new archive in memory: its slots are empty
    detail::built(c);
    chart::built(c);
    show(c, "N_Detail", false);
    g_cursor_y0 = pane_f(story_lyt_screen::pane(c, "N_Cursor"), 0x20);
    g_row_step = pane_f(story_lyt_screen::pane(c, "N_Row0"), 0x20) - pane_f(story_lyt_screen::pane(c, "N_Row1"), 0x20);
    // the game sizes every text pane's buffer from its width when it builds the layout: the row texts
    // and labels get room for what the page puts there
    auto room = [&](const std::string& name, uint32_t chars) { story_lyt_text::set_capacity(c, story_lyt_screen::pane(c, name.c_str()), chars); };
    for (int i = 0; i < kRows; i++) {
        for (const char* part : {"Title", "TitleDim", "Next", "NextDim"}) room(row_pane("T", i, part), 160);
        room(row_pane("T", i, "Status"), 24);
        room(row_pane("T", i, "Initial"), 4);
    }
    for (const char* name : {"T_Tab0On", "T_Tab0Off", "T_Tab1On", "T_Tab1Off"}) room(name, 32);
    room("T_Empty1", 64), room("T_Empty2", 96);
}

}  // namespace

View view_of(const nb::Page& page, const std::vector<nb::Row>& all) {
    View v;
    v.mode = page.mode() == nb::Page::Mode::List ? 1 : page.mode() == nb::Page::Mode::Detail ? 2 : 0;
    v.tab = page.tab();
    size_t done = 0;
    for (const nb::Row& r : all) done += r.status == nb::Status::Done;
    v.tab_labels[0] = "Quests";
    v.tab_labels[1] = "Archive" + (done ? " (" + std::to_string(done) + ")" : std::string());
    const std::vector<nb::Row> rows = page.shown(all);
    const unsigned top = page.top();
    for (unsigned i = 0; i < unsigned(kRows) && top + i < rows.size(); i++) {
        const nb::Row& r = rows[top + i];
        const nb::Quest& q = *r.quest;
        RowView rv;
        rv.title = q.title;
        rv.next = r.status == nb::Status::Done ? "The story is told." : r.status == nb::Status::NotAvailable ? "Not yet available: " + q.condition : q.next;
        rv.dim = r.status == nb::Status::Done || r.status == nb::Status::NotAvailable;
        rv.status = r.status == nb::Status::NotAvailable ? "Not yet" : nb::status_text(r.status);
        rv.pill = r.status == nb::Status::New ? 1 : r.status == nb::Status::NotAvailable ? 2 : r.status == nb::Status::Done ? 3 : 0;
        rv.tracked = r.tracked;
        rv.initial_shown = q.portrait.empty();
        rv.portrait = q.portrait.empty() ? 0 : q.key;
        if (test_portrait()) rv.portrait = q.key, rv.initial_shown = false;
        rv.initial = q.mod_name.empty() ? "?" : q.mod_name.substr(0, 1);
        if (q.key == page.selected_key()) v.selected = int(i);
        v.rows.push_back(std::move(rv));
    }
    v.up = top > 0;
    v.down = top + kRows < rows.size();
    v.empty = rows.empty();
    v.empty1 = v.tab == 0 ? "No stories yet." : "No finished stories yet.";
    v.empty2 = v.tab == 0 ? "When a story mod writes to you, its quests are kept here." : "";
    v.track_plate = !rows.empty();
    return v;
}

namespace {
bool same(const View& a, const View& b) {
    if (a.mode != b.mode || a.tab != b.tab || a.selected != b.selected || a.up != b.up || a.down != b.down || a.empty != b.empty ||
        a.track_plate != b.track_plate || a.tab_labels[1] != b.tab_labels[1] || a.rows.size() != b.rows.size())
        return false;
    for (size_t i = 0; i < a.rows.size(); i++) {
        const RowView &x = a.rows[i], &y = b.rows[i];
        if (x.title != y.title || x.next != y.next || x.status != y.status || x.initial != y.initial || x.dim != y.dim ||
            x.tracked != y.tracked || x.initial_shown != y.initial_shown || x.pill != y.pill || x.portrait != y.portrait)
            return false;
    }
    return true;
}
}  // namespace

bool configure() {
    if (const char* e = getenv("WWHD_NOTEBOOK_LAYOUT"); e && *e) {
        g_description = e;
        g_data_dir.clear();
    }
    if (g_description.empty()) return false;
    namespace fs = std::filesystem;
    const fs::path cache = g_data_dir.empty() ? fs::path(g_description).parent_path() / ".layout-cache" : fs::path(g_data_dir) / "layout-cache";
    const std::string archive = lyt_build::build_cached(g_description, cache);
    if (archive.empty()) {
        LOG("[notebook] the layout %s isn't usable: the page is drawn on the HUD", g_description.c_str());
        return false;
    }
    story_lyt_screen::Client client;
    client.layout = kLayout;
    client.archive = archive;
    client.owner = "the story notebook";
    // the animation entries: each with the slot of its group (animations on the same panes replace each other)
    const std::vector<std::pair<std::string, std::string>> anims = {
        {"Open", "G_Open"},       {"Close", "G_Open"},       {"TurnLeft", "G_Content"}, {"TurnRight", "G_Content"},
        {"Breathe", "G_Cursor"},  {"Bump0", "G_Row0"},       {"Bump1", "G_Row1"},       {"Bump2", "G_Row2"},
        {"Bump3", "G_Row3"},      {"Bump4", "G_Row4"},       {"PressBack", "G_Back"},   {"PressTrack", "G_Track"},
        {"DetailIn", "G_Detail"}, {"DetailOut", "G_Detail"},   {"TrackBreathe", "G_TrackCursor"}};
    std::vector<std::pair<std::string, std::string>> all = anims;
    for (const auto& a : chart::anims()) all.push_back(a);  // the mini chart's glow
    std::map<std::string, uint32_t> slots;
    for (const auto& [name, group] : all) {
        const uint32_t slot = slots.emplace(group, uint32_t(slots.size())).first->second;
        client.anims.push_back({name, group, slot});
    }
    client.covers = [] { return drawing(); };
    client.before = [] {  // the menu's floating icon is drawn with the menu's screens, before our layout
        nb::hide_floating_notebook_icon();
    };
    client.after = nb::restore_notebook_icons;
    client.finalized = nb::state_loaded;
    client.game_archives = {chart::kGameArchive};  // the sea chart's Link icon (story_notebook_layout_chart.h)
    client.built = built;
    client.frame = frame;
    story_lyt_screen::set_client(std::move(client));
    LOG("[notebook] the page is drawn as the layout %s (%s)", kLayout, archive.c_str());
    return true;
}

bool update(const nb::Page& page, const std::vector<nb::Row>& all) {
    if (!story_lyt_screen::has_client() || !story_lyt_screen::ready()) return false;
    View v = view_of(page, all);
    std::lock_guard lock(g_mu);
    if (v.mode == 2) {  // the detail view: the selected quest
        for (const nb::Row& r : page.shown(all))
            if (r.quest->key == page.selected_key()) {
                detail::DetailView d = detail::view_of(page, r);
                if (!detail::same(d, g_detail)) g_detail = std::move(d), g_detail_serial++;
                g_chart = chart::view_of(page, *r.quest);
            }
    }
    if (v.mode != 0 && !g_drawing) g_animating.store(true, std::memory_order_relaxed);  // opening: until the layout's Open has run
    if (!same(v, g_view)) v.serial = g_view.serial + 1, g_view = std::move(v);
    g_drawing = g_view.mode != 0 || g_drawing;
    return g_view.mode != 0;
}

bool animating() { return g_animating.load(std::memory_order_relaxed); }

void reset() {
    std::lock_guard lock(g_mu);
    g_view = {};
    g_reset = true;
    g_drawing = false;
    g_animating = false;
}

bool drawing() {
    std::lock_guard lock(g_mu);
    return g_drawing;
}

bool update_closed() {
    if (!story_lyt_screen::has_client()) return false;
    std::lock_guard lock(g_mu);
    if (g_view.mode != 0) g_view.mode = 0, g_view.serial++;
    return g_drawing;
}

}  // namespace story_notebook_layout

namespace story_notebook {
namespace {
std::vector<uint8_t> g_icon_open;
}
void set_icon_open(const std::string& png_path) {
    std::string path = png_path;
    if (const char* e = getenv("WWHD_NOTEBOOK_ICON_OPEN"); e && *e) path = e;
    g_icon_open.clear();
    if (path.empty()) return;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() > 8 && bytes.size() <= 4u << 20) g_icon_open = std::move(bytes);
    LOG("[notebook] the notebook's open icon: %s (%s)", path.c_str(), g_icon_open.empty() ? "not read" : "read");
}
const std::vector<uint8_t>& icon_open_png() { return g_icon_open; }

void set_layout_description(const std::string& path, const std::string& data_dir) {
    story_notebook_layout::g_description = path;  // NOLINT: same translation unit
    story_notebook_layout::g_data_dir = data_dir;
}
}  // namespace story_notebook
