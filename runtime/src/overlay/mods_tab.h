#include "tooltip.h"
// The Mods tab: one list of every mod with an on/off switch (built-in, downloaded, and the catalogue's
// mods that aren't downloaded yet) and More (profiles, files, the mod list's source, code-mod support,
// save backups, cheats). Switching on a mod that needs anything (a download, the GameCube game, a
// preparation, code-mod support, a save backup) goes through one review screen and one progress view
// (mods_model.h Flow) that keeps running while the menu is closed.
//
// What each row says and what a mod's page offers is decided by mods_model.h; this file draws it and
// drives the existing machinery: catalogue staging (mods/catalogue_client.h), the setup run
// (setup_flow.h), native-code trust and experimental save backups (mods/packages.h). Used by the
// overlay (overlay.cpp) and by the offscreen tests (runtime/tools/mods_tab_test.cpp).
#pragma once
#include "catalogue_loader.h"
#include "../mods/required.h"
#include "../mods/mods.h"
#include "../build_info.h"
#include <thread>
#include "experimental_ui.h"
#include "graphics_switch.h"
#include "hostui.h"
#include "mods_model.h"
#include "setup_flow.h"
#include "setup_test.h"
#include "text_entry.h"
#include "../mods/code_mods.h"
#include "../mods/manager.h"
#include "../mods/packages.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace overlay::modsui {
namespace fs = std::filesystem;
using namespace modsmodel;

// ---------------------------------------------------------------- what the host supplies
struct Host {
    std::function<void(bool folder, std::function<void(std::string)>)> choose_game_source, choose_mod_source;
    mods::catalogue::Fetch fetch;                 // HTTPS transport (empty: local catalogues only)
    std::function<bool()> can_restart;            // the game can relaunch itself
    std::function<void(bool code_build)> restart; // relaunch now
    std::function<void(const char* id)> builtin_options;  // a built-in mod's options (overlay.cpp)
    std::function<void()> cheats;                 // the cheats section (More)
    mods::catalogue::Version port_version;
    std::string build_name = "USA";
    bool android = false;
    bool file_picker = true; // the host has file and folder choosers (overlay/extensions.h Features)
    bool code_mods = true;      // code mods can run here (Features::code_mods)
    bool mod_downloads = true;  // the online mod list (Features::mod_downloads)
    bool back = false;       // controller B this frame
    bool b_used = false;     // out: B answered something here (it must not also close the menu)
    bool test = false;       // isolated headless run: WWHD_TEST_* aids apply
    bool expand_all = false; // tests: every collapsed section is drawn open (the ID conflict check)
};

inline constexpr const char* kDefaultCatalogue = "https://raw.githubusercontent.com/ZeldaWWHDRecomp/ZeldaWWHDMods/main/index.json";
// Built-in: the mods compiled into the game; Community: mods made by players (the catalogue, installed or
// not, and packages installed from a file); More: everything else.
enum Area { kBuiltin, kCommunity, kMore, kAreas };
inline const char* const kAreaNames[kAreas] = {"Built-in", "Community", "More"};
inline const char* const kAreaCaptions[kAreas] = {"Included with the game. Switch on or off any time.",
                                                  "Made by players. Downloaded from the mod catalogue.", ""};

// ---------------------------------------------------------------- state
struct Review {
    enum Mode { Install, Update, SwitchOn } mode = Install;
    bool request = false;        // open the popup next frame
    std::string id, name, description;
    bool from_catalogue = false;
    mods::catalogue::Entry entry;
    fs::path fixtures;
    Plan plan;
    std::string source_game;     // a GameCube / Wii U source is needed ("" none)
    std::vector<mods::catalogue::Step> choices;
    std::vector<mods::catalogue::Step> confirms;   // the mod's own statements; Install / Switch on agrees
    std::map<std::string, std::string> chosen;
    std::vector<std::string> code_names;  // code that runs on the computer (native, guest, setup tools)
    bool experimental = false;
    std::string error;
};

struct Job {
    Flow flow;
    Review accepted;
    std::mutex mutex;            // the download worker's results
    std::thread worker;
    bool downloading = false, downloaded = false, cancelled = false, fetched = false;
    std::string download_error, installed_id;
    fs::path download_path;
    bool setup_started = false;
    bool keep_started = false, step_running = false;  // an update that stays off runs its steps itself
    double finished_at = -1;
    ~Job() { if (worker.joinable()) worker.join(); }
};

struct State {
    int area = kBuiltin;     // remembered for the session (State lives as long as the game runs)
    int select_area = -1;        // switch the area tab next frame (L / R, a finished install)
    std::string page;            // the mod whose Settings page is open ("" the list)
    bool page_builtin = false;
    bool page_scroll = false;
    Review review;
    Job job;
    CatalogueLoader loader;
    std::string catalogue_url;
    bool catalogue_initialized = false;
    bool auto_check = true, show_experimental = false;
    std::string error;           // last action's problem (one line at the top)
    std::string message;         // last action's result
    std::string remove_id;       // waiting for the remove confirmation
    uint64_t remove_bytes = 0;
    bool remove_request = false;
    GraphicsSwitch graphics;
    std::vector<std::string> notices;
    std::string toast;           // shown over the game when the menu is closed
    double toast_until = 0;
    std::mutex picker_mutex;
    std::string picked_package;  // More > Install from a file
    std::string source_error;
    char profile_name[65] = {};
    char url_edit[2049] = {};
    char search[128] = {};
    std::map<std::string, ImRect> rects;  // last drawn rectangle of labelled buttons (tests click them)
};
inline State& state() { static State s; return s; }
inline void go_area(int area) { state().area = area; state().select_area = area; }

// ---------------------------------------------------------------- small widgets
inline const char* glyph(unsigned code, const char* text, const char* fallback) {
    ImFont* f = ImGui::GetFont();
    return f && f->IsGlyphInFont((ImWchar)code) ? text : fallback;
}
inline const char* check_mark() { return glyph(0x2713, "\xE2\x9C\x93", "OK"); }
inline const char* cross_mark() { return glyph(0x2717, "\xE2\x9C\x97", "x"); }
inline const ImVec4 kMuted(0.70f, 0.78f, 0.84f, 1.0f), kGood(0.55f, 0.95f, 0.70f, 1.0f), kWarn(1.0f, 0.80f, 0.35f, 1.0f),
    kBad(1.0f, 0.50f, 0.45f, 1.0f), kAccent(0.55f, 0.95f, 0.85f, 1.0f);
inline void text(const std::string& s) { ImGui::TextWrapped("%s", s.c_str()); }
inline void colored(const ImVec4& c, const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
inline void muted(const std::string& s) { colored(kMuted, s); }
inline void tooltip(const std::string& s) {
    if (!s.empty() && overlay::tooltip_hovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", s.c_str());
}
// Put the next item on this line when it fits, else on the next one (narrow windows, phones).
inline void same_line_if_fits(const char* next_label) {
    float w = ImGui::CalcTextSize(next_label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
}
// Lists and choices: touch-sized rows on Android (as the overlay's other lists)
inline bool selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0)) {
#ifdef __ANDROID__
    size.y = std::max(size.y, ImGui::GetFrameHeight());
#endif
    return ImGui::Selectable(label, selected, flags, size);
}
// A collapsed section (tests can open them all: Host::expand_all)
inline bool g_expand_all = false;
inline bool section(const char* label) {
    if (g_expand_all) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader(label);
    state().rects[label] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return open;
}
inline bool tree(const char* label) {
    if (g_expand_all) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    return ImGui::TreeNode(label);
}
inline bool button(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    bool pressed = ImGui::Button(label, size);
    state().rects[label] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return pressed;
}
inline bool small_button(const char* label) {
    bool pressed = ImGui::SmallButton(label);
    state().rects[label] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return pressed;
}
inline bool on_switch(const char* id, bool* on) {
    bool changed = ImGui::Checkbox(id, on);
    state().rects[id] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return changed;
}
inline void badge(const char* label, const ImVec4& colour) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    float w = ImGui::CalcTextSize(label).x + 8;
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}
inline ImVec4 status_colour(Status s) {
    switch (s) {
    case Status::On: return kGood;
    case Status::Error: return kBad;
    case Status::NeedsSetup: case Status::RestartToApply: case Status::UpdateAvailable: return kWarn;
    case Status::Installing: return kAccent;
    default: return kMuted;
    }
}
inline std::string kind_words(const std::string& kind) {
    if (kind == "guest") return "Code mod";
    if (kind == "native") return "Program for your computer";
    if (kind == "cemu") return "Graphics pack";
    if (kind == "content") return "Replacement models, textures or text";
    return "Settings preset";
}
inline constexpr const char* kRunsCode =
    "Contains code that runs on your computer with the game's permissions. Install only mods from people you trust.";
inline constexpr const char* kExperimentalShort =
    "Experimental: untested. It can break the game or damage your save. Your saves are backed up before it is switched on.";

// ---------------------------------------------------------------- catalogue
inline std::string catalogue_url() {
    auto& s = state();
    if (!s.catalogue_initialized) {
        s.catalogue_initialized = true;
        std::string v = kDefaultCatalogue;
        hostui::get("mod.catalogue.url", v);
        if (const char* o = getenv("WWHD_MOD_CATALOGUE")) v = o;
        s.catalogue_url = v;
        snprintf(s.url_edit, sizeof s.url_edit, "%s", v.c_str());
        std::string flag;
        s.auto_check = !(hostui::get("mod.catalogue.auto", flag) && flag == "0");
        s.show_experimental = hostui::get("mod.showExperimental", flag) && flag == "1";
    }
    return s.catalogue_url;
}
inline fs::path catalogue_work() {
    auto dir = mods::packages::directory();
    return dir.empty() ? fs::path() : fs::path(dir).parent_path() / "Catalogue";
}
inline void open_catalogue(const Host& h) {
    auto work = catalogue_work();
    if (work.empty()) return;
    auto& s = state();
    s.loader.open(catalogue_url(), work, h.fetch, ImGui::GetTime(), s.auto_check && h.mod_downloads);
}
inline const mods::catalogue::Entry* catalogue_entry(const CatalogueLoader::Snapshot& snap, const std::string& id) {
    for (const auto& e : snap.catalogue.index.entries) if (e.id == id) return &e;
    return nullptr;
}
inline bool newer(const std::string& a, const std::string& b) {
    try { return mods::catalogue::Version::parse(a) > mods::catalogue::Version::parse(b); } catch (...) { return false; }
}

// ---------------------------------------------------------------- facts from the manager
inline const mods::packages::View* find_view(const std::vector<mods::packages::View>& list, const std::string& id) {
    for (const auto& v : list) if (v.id == id) return &v;
    return nullptr;
}
inline bool needs_setup(const mods::packages::View& v) {
    return setupflow::setup_open(v.id) || !v.game_source_warning.empty();
}
// Switching it on needs the review (questions, preparation, a rebuild or a backup) rather than a plain enable.
inline bool needs_review(const mods::packages::View& v) {
    using namespace mods::packages;
    return needs_setup(v) || !unconfirmed_native(v.id).empty() || !unacknowledged_experimental(v.id).empty() ||
           needs_code_mod_support(v.id);
}
inline bool flow_for(const std::string& id) {
    auto& j = state().job;
    return j.flow.active() && j.flow.id == id;
}
inline bool flow_working(const std::string& id) {
    auto& j = state().job;
    return j.flow.id == id && j.flow.phase == Phase::Working;
}
// "Installing" only while the mod is being downloaded; "Setting up" for everything after that
inline bool flow_downloading() {
    const auto& f = state().job.flow;
    size_t c = f.current();
    return f.phase == Phase::Working && c < f.steps.size() &&
           (f.steps[c].kind == StepKind::Download || f.steps[c].kind == StepKind::Check);
}
inline const char* flow_verb() { return flow_downloading() ? "Installing" : "Setting up"; }
inline Facts facts(const mods::packages::View& v, bool update) {
    Facts f;
    f.enabled = v.enabled;
    f.active = v.active;
    f.pending_restart = v.pending_restart;
    f.compatible = v.compatible;
    f.needs_setup = needs_setup(v);
    f.working = flow_working(v.id);
    f.downloading = f.working && flow_downloading();
    f.percent = f.working ? state().job.flow.overall() * 100 : 0;
    f.failed = state().job.flow.id == v.id && state().job.flow.phase == Phase::Failed;
    // the GameCube warning means "needs setup", not a broken mod
    if (!v.reason.empty() && v.reason != v.game_source_warning) f.error = v.reason;
    f.update_available = update;
    return f;
}

// ---------------------------------------------------------------- the review
inline Plan plan_for_entry(const mods::catalogue::Entry& e) {
    Plan p;
    p.download = true;
    auto it = e.downloads.find(mods::packages::platform_key());
    if (it == e.downloads.end()) it = e.downloads.find("all");
    if (it != e.downloads.end()) p.download_bytes = it->second.size;
    p.backup = e.experimental;
    for (const auto& s : e.setup) {
        if (s.optional) continue;
        if (s.type == "run_tool") p.prepare.push_back(s.title);
        if (s.type == "build_guest_mod") p.build = true;
    }
    p.support = e.kind == "guest" && !mods::packages::has_code_mod_support();
    p.restart = e.kind == "guest" || e.kind == "content" || e.kind == "cemu";
    return p;
}
inline Plan plan_for_installed(const mods::packages::View& v, bool all_steps = false) {
    using namespace mods::packages;
    Plan p;
    p.backup = !unacknowledged_experimental(v.id).empty();
    for (const auto& s : setup_steps(v.id)) {
        if (s.step.optional || (s.satisfied && !all_steps)) continue;
        if (s.step.type == "run_tool") p.prepare.push_back(s.step.title);
        if (s.step.type == "build_guest_mod") p.build = true;
    }
    p.support = needs_code_mod_support(v.id);
    p.restart = v.restart_required;
    return p;
}
// An update ends in the mod's state before it: an off mod is updated and prepared and stays off (its code-mod
// build waits for switching it on when code-mod support is missing: no rebuild for a mod that stays off); a
// mod that is on (only offered where it can be switched off and on while the game runs) is switched off,
// updated and switched on again.
inline Plan plan_for_update(const mods::catalogue::Entry& e, const mods::packages::View& v) {
    Plan p = plan_for_entry(e);
    if (v.enabled) {
        p.switch_off = true;
    } else {
        p.keep_off = true;
        p.backup = false;
        p.support = false;
        p.build = p.build && mods::packages::has_code_mod_support();
        p.restart = false;
    }
    return p;
}
inline void open_review_for_entry(const mods::catalogue::Entry& e, const fs::path& fixtures, bool update,
                                  const mods::packages::View* installed = nullptr) {
    auto& r = state().review;
    r = {};
    r.mode = update ? Review::Update : Review::Install;
    r.request = true;
    r.id = e.id;
    r.name = e.name;
    r.description = e.description;
    r.from_catalogue = true;
    r.entry = e;
    r.fixtures = fixtures;
    r.plan = update && installed ? plan_for_update(e, *installed) : plan_for_entry(e);
    r.experimental = e.experimental && !r.plan.keep_off;  // the warning is for switching it on
    for (const auto& s : e.setup) {
        if (s.optional) continue;
        if (s.type == "game_path") r.source_game = s.game;
        if (s.type == "choice") { r.choices.push_back(s); r.chosen[s.option] = s.choices.front(); }
        if (s.type == "confirm") r.confirms.push_back(s);
    }
    if (e.kind == "native" || e.kind == "guest" || std::any_of(e.setup.begin(), e.setup.end(), [](const auto& s) { return s.type == "run_tool"; }))
        r.code_names.push_back(e.name);
}
inline void open_review_for_installed(const mods::packages::View& v) {
    using namespace mods::packages;
    auto& r = state().review;
    r = {};
    r.mode = Review::SwitchOn;
    r.request = true;
    r.id = v.id;
    r.name = v.name;
    r.description = v.description;
    r.plan = plan_for_installed(v);
    r.experimental = !unacknowledged_experimental(v.id).empty();
    for (const auto& s : setup_steps(v.id)) {
        if (s.step.optional || s.satisfied) continue;
        if (s.step.type == "game_path") r.source_game = s.step.game;
        if (s.step.type == "choice") { r.choices.push_back(s.step); r.chosen[s.step.option] = s.step.choices.front(); }
        if (s.step.type == "confirm") r.confirms.push_back(s.step);
    }
    if (r.source_game.empty() && !v.game_source_warning.empty()) r.source_game = "gc_wind_waker";
    for (const auto& [id, name] : unconfirmed_native(v.id)) r.code_names.push_back(name);
}
// The GameCube game the review needs: valid now?
inline bool source_ready(const Review& r) {
    if (r.source_game.empty()) return true;
    if (r.from_catalogue) {
        std::vector<mods::catalogue::Step> needed;
        for (const auto& s : r.entry.setup) if (s.type == "game_path" && !s.optional) needed.push_back(s);
        return mods::packages::game_source_warning(needed).empty();
    }
    for (const auto& v : mods::packages::list()) {
        if (v.id != r.id) continue;
        const auto steps = mods::packages::setup_steps(v.id);  // one vector: begin and end must belong together
        return v.game_source_warning.empty() &&
               std::none_of(steps.begin(), steps.end(),
                            [](const auto& s) { return s.step.type == "game_path" && !s.satisfied && !s.step.optional; });
    }
    return false;
}

// ---------------------------------------------------------------- the flow (runs every frame, menu open or not)
inline void notify(const std::string& text) {
    auto& s = state();
    s.toast = text;
    s.toast_until = ImGui::GetTime() + 6;
}
inline std::string plain_failure(StepKind k) {
    switch (k) {
    case StepKind::Download: return "The download failed. Check your internet connection and try again.";
    case StepKind::Check: return "The downloaded mod didn't pass the check. Try again later.";
    case StepKind::Backup: return "Your saves couldn't be backed up, so the mod stays off.";
    case StepKind::Prepare: return "Preparing the mod's files failed.";
    case StepKind::Support: return "Adding code-mod support failed.";
    case StepKind::Build: return "Building the code mod failed.";
    default: return "The mod couldn't be switched on.";
    }
}
inline void start_download(const Host& h) {
    auto& j = state().job;
    if (j.worker.joinable()) j.worker.join();
    {
        std::lock_guard g(j.mutex);
        j.downloading = true;
        j.downloaded = j.cancelled = j.fetched = false;
        j.download_error.clear();
        j.download_path.clear();
    }
    auto entry = j.accepted.entry;
    auto fixtures = j.accepted.fixtures;
    auto fetch = h.fetch;
    auto port = h.port_version;
    auto build = h.build_name;
    j.worker = std::thread([entry, fixtures, fetch, port, build] {
        auto& j = state().job;
        std::string error, id;
        try {
            mods::catalogue::Fetch observed = [fetch](const std::string& url, const fs::path& path, uint64_t limit) {
                { std::lock_guard g(state().job.mutex); state().job.download_path = path; }
                mods::catalogue::require(bool(fetch), "HTTPS transport is unavailable");
                fetch(url, path, limit);
                std::lock_guard g(state().job.mutex);
                state().job.fetched = true;  // the bytes arrived; a failure from here on is the check
            };
            mods::catalogue::StagedPackage package(entry, port, build, mods::packages::platform_key(), catalogue_work(), fixtures,
                                                  observed);
            mods::catalogue::require(mods::packages::install(package.path().string(), error, &id), error);
            mods::packages::mark_experimental(id, entry.experimental);
            fprintf(stderr, "[catalogue] installed %s\n", id.c_str());
        } catch (const std::exception& e) {
            error = e.what();
            if (error.empty()) error = "Install failed";
        }
        std::lock_guard g(j.mutex);
        j.downloading = false;
        j.downloaded = error.empty();
        j.download_error = error;
        j.installed_id = id;
    });
}
inline void begin(const Host& h, double now) {
    auto& s = state();
    auto& j = s.job;
    if (j.worker.joinable()) j.worker.join();
    j.accepted = s.review;
    j.flow = Flow::make(s.review.id, s.review.name, s.review.plan);
    j.setup_started = j.keep_started = j.step_running = false;
    j.finished_at = -1;
    j.installed_id.clear();
    j.flow.accept(now);
    if (s.review.from_catalogue && !s.review.plan.switch_off) start_download(h);  // else after switching it off
    go_area(kCommunity);
    s.page = s.review.id;
    s.page_builtin = false;
    s.page_scroll = true;
    fprintf(stderr, "[mods] %s %s\n", s.review.mode == Review::Install ? "install" : s.review.mode == Review::Update ? "update" : "switch on",
            s.review.id.c_str());
}
// Mark flow steps done from the manager's own records (receipts, built modules, the switch).
inline void sync_steps(Flow& f, const mods::packages::View& v, double now) {
    using namespace mods::packages;
    auto steps = setup_steps(v.id);
    std::vector<std::string> prepared;
    bool built = true;
    for (const auto& st : steps) {
        if (st.step.optional) continue;
        if (st.step.type == "run_tool" && st.satisfied) prepared.push_back(st.step.title);
        if (st.step.type == "build_guest_mod") built = built && st.satisfied;
    }
    size_t first_open = f.steps.size();
    for (size_t i = 0; i < f.steps.size(); i++) {
        const auto& step = f.steps[i];
        bool done = false;
        switch (step.kind) {
        case StepKind::SwitchOff: case StepKind::Download: case StepKind::Check: done = true; break;  // installed
        case StepKind::Backup: done = unacknowledged_experimental(v.id).empty() || v.enabled; break;
        case StepKind::Prepare: done = std::find(prepared.begin(), prepared.end(), step.title) != prepared.end(); break;
        case StepKind::Support: done = !needs_code_mod_support(v.id); break;
        case StepKind::Build: done = built; break;
        case StepKind::SwitchOn: done = v.enabled; break;
        }
        if (!done) { first_open = i; break; }
    }
    if (first_open > 0 && first_open <= f.steps.size() && f.current() < first_open) f.done(first_open - 1, now);
}
// After a restart (code-mod support), the setup run continues: show it as this flow again.
inline void adopt_resumed_run(double now) {
    auto& j = state().job;
    if (j.flow.active() || setupflow::setup_run.id.empty()) return;
    for (const auto& v : mods::packages::list()) {
        if (v.id != setupflow::setup_run.id) continue;
        if (j.worker.joinable()) j.worker.join();
        j.accepted = {};
        j.flow = Flow::make(v.id, v.name, plan_for_installed(v, true));
        j.flow.accept(now);
        j.setup_started = true;
        j.accepted.id = v.id;
        j.accepted.name = v.name;
        state().page = v.id;
        go_area(kCommunity);
    }
}
// An update of a mod that is off: prepare what its setup needs (trust from the review, choices, tools, the
// code-mod build when support is there) without switching it on.
inline void keep_off_steps(Flow& f, const mods::packages::View& v, double now) {
    using namespace mods::packages;
    auto& j = state().job;
    std::string error;
    if (!j.keep_started) {
        if (!unconfirmed_native(f.id).empty() && !confirm_native(f.id, error)) { f.fail(plain_failure(StepKind::Prepare), error); return; }
        for (const auto& st : setup_steps(f.id)) {
            if (st.satisfied || st.step.optional) continue;
            bool ok = true;
            if (st.step.type == "confirm") ok = configure(f.id, st.step.option, true, error);
            else if (st.step.type == "choice") {
                auto chosen = j.accepted.chosen.find(st.step.option);
                ok = chosen != j.accepted.chosen.end() && configure(f.id, st.step.option, chosen->second, error);
            }
            if (!ok) { f.fail(plain_failure(StepKind::Prepare), error); return; }
        }
        j.keep_started = true;
    }
    auto& worker = setupflow::setup_work();
    if (j.step_running) {
        std::string output;
        {
            std::lock_guard g(worker.mutex);
            if (worker.running) return;
            error = worker.error;
            output = worker.output;
        }
        j.step_running = false;
        sync_steps(f, v, now);
        if (!error.empty()) {
            size_t c = f.current();
            f.fail(plain_failure(c < f.steps.size() ? f.steps[c].kind : StepKind::Prepare), output.empty() ? error : error + "\n\n" + output);
            return;
        }
    }
    sync_steps(f, v, now);
    for (const auto& st : setup_steps(f.id)) {
        if (st.satisfied || st.step.optional) continue;
        if (st.step.type == "game_path") { f.fail("Choose your GameCube game first.", ""); return; }
        bool build = st.step.type == "build_guest_mod";
        if (build && needs_code_mod_support(f.id)) continue;  // built when it is switched on
        if (st.step.type == "run_tool" || build) {
            j.step_running = setupflow::start_setup(f.id, st.step);
            return;
        }
    }
    f.finish(false, now);
    j.finished_at = now;
    fprintf(stderr, "[mods] %s updated; it stays off\n", f.id.c_str());
}
inline void tick(const Host& h, bool menu_open) {
    using namespace mods::packages;
    auto& s = state();
    auto& j = s.job;
    auto& f = j.flow;
    const double now = ImGui::GetTime();
    adopt_resumed_run(now);
    if (f.phase != Phase::Working) return;
    f.tick(now);
    size_t cur = f.current();
    StepKind kind = cur < f.steps.size() ? f.steps[cur].kind : StepKind::SwitchOn;
    // 0. an update of a mod that is on: switch it off and wait until it is unloaded
    if (kind == StepKind::SwitchOff) {
        for (const auto& v : mods::packages::list()) {
            if (v.id != f.id) continue;
            std::string error;
            if (v.enabled && !enable(f.id, false, error)) { f.fail("The mod couldn't be switched off for the update.", error); return; }
            if (!v.enabled && !v.active) { f.done(StepKind::SwitchOff, now); start_download(h); }
        }
        return;
    }
    // 1. download and check (catalogue installs and updates)
    if (j.accepted.from_catalogue && (kind == StepKind::Download || kind == StepKind::Check)) {
        bool downloading, downloaded;
        std::string error;
        fs::path path;
        { std::lock_guard g(j.mutex); downloading = j.downloading; downloaded = j.downloaded; error = j.download_error; path = j.download_path; }
        if (downloading) {
            std::error_code ec;
            auto size = path.empty() ? 0 : fs::file_size(path, ec);
            const uint64_t total = j.accepted.plan.download_bytes;
            if (!ec && total > 0) f.report(StepKind::Download, double(size) / total);
            if (!ec && total > 0 && size >= total && kind == StepKind::Download) f.done(StepKind::Download, now);
            return;
        }
        if (!error.empty()) {
            bool fetched;
            { std::lock_guard g(j.mutex); fetched = j.fetched || (!j.accepted.entry.downloads.empty() &&
                  !mods::catalogue::https_url(j.accepted.entry.downloads.begin()->second.url)); }
            if (fetched && kind == StepKind::Download) f.done(StepKind::Download, now);
            f.fail(plain_failure(fetched ? StepKind::Check : StepKind::Download), error);
            if (!menu_open) notify(f.name + ": " + f.error);
            return;
        }
        if (!downloaded) return;
        f.done(StepKind::Check, now);
        if (j.cancelled) { f.cancel(); return; }
    }
    auto list = mods::packages::list();
    const auto* view = find_view(list, f.id);
    if (!view) {
        if (!j.accepted.from_catalogue) { f.fail("The mod is no longer installed.", ""); }
        return;
    }
    if (f.keep_off) { keep_off_steps(f, *view, now); return; }
    // 2. back up saves (experimental), then hand the rest to the setup run
    if (!j.setup_started) {
        std::string error;
        // The review's one confirmation covers the experimental warning: the saves are backed up now, before
        // anything is switched on. (The step is looked up afresh: the download and check may have finished in
        // this same tick, so the step found at its start is out of date.)
        const bool backup_step = std::any_of(f.steps.begin(), f.steps.end(), [](const FlowStep& st) {
            return st.kind == StepKind::Backup && st.state != StepState::Done;
        });
        if (!unacknowledged_experimental(f.id).empty()) {
            if (!backup_step) {  // the package turned out experimental, but the review didn't say so
                f.fail("This mod is experimental. Switch it on again to read the warning first.", "");
                return;
            }
            if (!acknowledge_experimental(f.id, error)) {
                // the reason itself (disk full, ...), which also says the mod was not turned on
                f.fail("Your saves couldn't be backed up: " + error, error);
                return;
            }
        }
        if (backup_step) f.done(StepKind::Backup, now);
        // Trust: the player confirmed this exact catalogue version (its download is checked against the
        // catalogue's SHA-256) or this installed package in the review; begin_setup_run records it.
        auto identity = setup_identity(f.id);
        if (!begin_setup_run(f.id, identity, j.accepted.chosen, error)) {
            f.fail(plain_failure(StepKind::SwitchOn), error);
            return;
        }
        setupflow::setup_run = {f.id, f.name, "Starting setup...", "", "", false, false, now, current_profile(), identity};
        j.setup_started = true;
        return;
    }
    // 3. follow the setup run
    auto& run = setupflow::setup_run;
    if (run.id.empty()) {
        if (view->enabled) {
            sync_steps(f, *view, now);
            auto code = mods::code::status();
            bool restart = view->pending_restart || (code.ready && code.mod == f.id);
            f.finish(restart, now);
            j.finished_at = now;
            fprintf(stderr, "[mods] %s is on%s\n", f.id.c_str(), restart ? " after a restart" : "");
            if (!menu_open) notify(f.name + (restart ? " is ready. Restart the game to switch it on." : " is on."));
        } else f.cancel();
        return;
    }
    if (run.id != f.id) return;
    if (!run.error.empty()) {
        sync_steps(f, *view, now);
        std::string details = run.output.empty() ? run.error : run.error + "\n\n" + run.output;
        size_t c = f.current();
        f.fail(plain_failure(c < f.steps.size() ? f.steps[c].kind : StepKind::SwitchOn), details);
        if (!menu_open) notify(f.name + ": " + f.error);
        return;
    }
    sync_steps(f, *view, now);
    if (run.rebuilding) {
        auto code = mods::code::status();
        if (code.total) f.report(StepKind::Support, double(code.done) / code.total);
    }
}
inline void retry(const Host& h) {
    auto& j = state().job;
    auto& f = j.flow;
    const double now = ImGui::GetTime();
    if (f.phase != Phase::Failed) return;
    size_t cur = f.current();
    StepKind kind = cur < f.steps.size() ? f.steps[cur].kind : StepKind::SwitchOn;
    if (kind == StepKind::Download || kind == StepKind::Check) {
        f.retry(now);
        start_download(h);
        return;
    }
    if (kind == StepKind::SwitchOff || f.keep_off) { j.step_running = false; f.retry(now); return; }
    auto& run = setupflow::setup_run;
    if (!j.setup_started || run.id != f.id) {  // failed before the setup run started (backup, trust)
        j.setup_started = false;
        f.retry(now);
        return;
    }
    std::string error;
    if (run.profile != mods::packages::current_profile() || run.identity != mods::packages::setup_identity(run.id)) {
        f.error = "The mod or the profile changed. Cancel and start again.";
        return;
    }
    if (!mods::packages::save_setup_run(run.id, true, error)) { f.error = error; return; }
    bool restart = run.rebuilding && mods::code::status().ready;
    run.retry();
    run.rebuilding = restart;
    f.retry(now);
}
inline void cancel_flow() {
    auto& j = state().job;
    auto& run = setupflow::setup_run;
    if (j.setup_started && run.id == j.flow.id) {
        if (run.rebuilding) { mods::code::cancel(); mods::code::dismiss(); }
        std::string error;
        mods::packages::save_setup_run(run.id, false, error);  // a running tool finishes; its result is kept
        run = {};
    }
    { std::lock_guard g(j.mutex); j.cancelled = true; }  // a download in flight installs the mod (off)
    j.flow.cancel();
    j.setup_started = false;
}

// ---------------------------------------------------------------- the progress view
inline void progress_view(const Host& h, bool android) {
    auto& j = state().job;
    auto& f = j.flow;
    const double now = ImGui::GetTime();
    ImGui::PushID("progress");
    if (f.phase == Phase::Done) {
        colored(kGood, std::string(check_mark()) + " " + f.name +
                           (f.keep_off ? " is updated. It stays off." : f.restart_needed ? " is ready." : " is on."));
        if (f.restart_needed) {
            if (android) text("Close and reopen the app to switch it on.");
            else if (h.can_restart && h.can_restart()) {
                text("Restart the game to switch it on. Progress since your last save is lost.");
                if (button("Restart now##flow")) { auto code = mods::code::status(); if (h.restart) h.restart(code.ready); }
                same_line_if_fits("Later");
                if (button("Later##flow")) f.cancel();
            } else text("Restart the game to switch it on.");
            if (android || !(h.can_restart && h.can_restart())) { if (button("OK##flow")) f.cancel(); }
        } else if (button("OK##flow")) f.cancel();
        ImGui::PopID();
        return;
    }
    // overall bar, then the steps with check marks
    char overlay_text[64];
    snprintf(overlay_text, sizeof overlay_text, "%d%%", int(f.overall() * 100));
    ImGui::ProgressBar(float(f.overall()), ImVec2(-FLT_MIN, 0), overlay_text);
    for (size_t i = 0; i < f.steps.size(); i++) {
        const auto& st = f.steps[i];
        std::string line;
        ImVec4 c = kMuted;
        switch (st.state) {
        case StepState::Done: line = std::string(check_mark()) + "  " + st.title; c = kGood; break;
        case StepState::Working: line = "\xE2\x80\xA2  " + st.title + " — " + percent_text(st.fraction * 100); c = kAccent; break;
        case StepState::Failed: line = std::string(cross_mark()) + "  " + st.title; c = kBad; break;
        default: line = "    " + st.title; break;
        }
        colored(c, line);
        if (st.state == StepState::Working) ImGui::ProgressBar(float(st.fraction), ImVec2(-FLT_MIN, 4), "");
    }
    if (f.phase == Phase::Working) {
        muted(elapsed_text(f.elapsed(now)) + " so far · " + duration_text(f.remaining(now)) + " left");
        muted("You can close the menu and keep playing.");
        if (button("Cancel##flow")) cancel_flow();
    } else if (f.phase == Phase::Failed) {
        colored(kBad, f.error);
        if (!f.details.empty() && tree("Show details")) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(f.details.c_str());
            ImGui::PopTextWrapPos();
            if (small_button("Copy details")) ImGui::SetClipboardText(f.details.c_str());
            ImGui::TreePop();
        }
        if (button("Try again##flow")) retry(h);
        same_line_if_fits("Cancel");
        if (button("Cancel##flow")) cancel_flow();
        muted("Steps already done are kept.");
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------- review popup
inline void game_source_picker(const Host& h, const std::string& game) {
    auto& s = state();
    auto src = mods::packages::game_source();
    auto choose = [&h](bool folder) {
        if (!h.choose_game_source) return;
        h.choose_game_source(folder, [](std::string path) {
            if (path.empty()) return;
            std::string error;
            mods::packages::set_game_source("gc_wind_waker", path, error);
            std::lock_guard g(state().picker_mutex);
            state().source_error = error;
        });
    };
    (void)game;
    if (!src.path.empty()) {
        ImGui::AlignTextToFramePadding();
        colored(src.valid ? kGood : kBad, std::string(src.valid ? check_mark() : cross_mark()) + "  " + file_name(src.path));
        tooltip(src.path);
        if (!src.valid) muted(src.result.empty() ? "This isn't a supported GameCube Wind Waker disc or folder." : src.result);
        if (h.file_picker) {
            if (button("Change##source")) choose(false);
            same_line_if_fits("Choose folder…");
            if (button("Choose folder…##source")) choose(true);
        }
    } else if (h.file_picker) {
        text("Choose your GameCube Wind Waker: a disc image (ISO or RVZ) or an extracted folder. It stays on your computer.");
        if (button("Choose disc image…##source")) choose(false);
        same_line_if_fits("Choose folder…");
        if (button("Choose folder…##source")) choose(true);
    } else {
        text("Some mods need your GameCube Wind Waker (a disc image or an extracted folder); it can't be chosen here: "
             "this system has no file chooser.");
    }
    std::lock_guard g(s.picker_mutex);
    if (!s.source_error.empty()) colored(kBad, s.source_error);
}
inline void review_popup(Host& h) {
    auto& s = state();
    auto& r = s.review;
    const char* id = "###mods_review";
    std::string title = (r.mode == Review::Install ? "Install " : r.mode == Review::Update ? "Update " : "Switch on ") + r.name + id;
    if (r.request) { ImGui::OpenPopup(id); r.request = false; }
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(vp.x * 0.92f, ImGui::GetFontSize() * 22), 0), ImVec2(vp.x * 0.94f, vp.y * 0.9f));
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::PushTextWrapPos(std::min(ImGui::GetFontSize() * 30, vp.x * 0.85f));
    if (!r.description.empty()) text(first_sentence(r.description, 240));
    ImGui::Spacing();
    // what it needs, all at once
    if (r.experimental) colored(kWarn, kExperimentalShort);
    if (!r.source_game.empty()) {
        ImGui::SeparatorText("Your GameCube game");
        game_source_picker(h, r.source_game);
    }
    for (const auto& step : r.choices) {
        ImGui::SeparatorText(step.title.c_str());
        auto& value = r.chosen[step.option];
        const float width = std::min(ImGui::GetFontSize() * 18, vp.x * 0.7f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        s.rects["##choice_" + step.option] = ImRect(at, ImVec2(at.x + width, at.y + ImGui::GetFrameHeight()));
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo(("##choice_" + step.option).c_str(), value.c_str())) {
            for (const auto& c : step.choices) {
                if (selectable(c.c_str(), c == value)) value = c;
                s.rects["choice_" + step.option + "_" + c] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            }
            ImGui::EndCombo();
        }
        if (!step.explanation.empty()) muted(step.explanation);
    }
    bool code = !r.code_names.empty();
    if (code || r.plan.support || r.plan.restart || r.plan.keep_off || r.plan.switch_off || !r.confirms.empty())
        ImGui::SeparatorText("Good to know");
    for (const auto& step : r.confirms) {
        text(step.title);
        if (!step.explanation.empty()) muted(step.explanation);
    }
    if (code) text(kRunsCode);
    if (r.plan.support)
        text("This mod needs code-mod support. It is added once: the game code is rebuilt (about 5 minutes, it can cost "
             "a little performance) and the game restarts by itself.");
    else if (r.plan.restart) text("It starts working after a restart.");
    if (r.plan.keep_off) text("It stays off. Switch it on when you want to use it.");
    if (r.plan.switch_off) text("It is switched off for the update and on again afterwards.");
    ImGui::Spacing();
    Flow preview = Flow::make(r.id, r.name, r.plan);
    std::string summary;
    if (r.plan.download) summary = "Download " + size_text(r.plan.download_bytes) + " · ";
    summary += "takes " + duration_text(preview.total_expected());
    muted(summary);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    bool ready = source_ready(r);
    bool busy = state().job.flow.phase == Phase::Working;
    const char* go = r.mode == Review::Install ? "Install##review" : r.mode == Review::Update ? "Update##review" : "Switch on##review";
    ImGui::BeginDisabled(!ready || busy);
    bool accept = button(go, ImVec2(ImGui::GetFontSize() * 7, 0));
    ImGui::EndDisabled();
    same_line_if_fits("Cancel");
    bool cancel = button("Cancel##review", ImVec2(ImGui::GetFontSize() * 7, 0));
    ImGui::SetItemDefaultFocus();  // keyboard and controller start on Cancel
    if (!ready) muted("Choose your GameCube game first.");
    else if (busy) muted("Wait until " + state().job.flow.name + " has finished.");
    // headless runs: a test that switches a mod on (WWHD_TEST_MOD_SETUP / _MOD_ENABLE / _CATALOGUE_INSTALL) accepts its review
    if (h.test && ready && !busy)
        for (const char* key : {"WWHD_TEST_MOD_SETUP", "WWHD_TEST_MOD_ENABLE", "WWHD_TEST_CATALOGUE_INSTALL"})
            if (const char* t = getenv(key); t && r.id == t) accept = true;
    if (h.back || ImGui::IsKeyPressed(ImGuiKey_Escape)) { cancel = true; accept = false; h.b_used = h.b_used || h.back; }
    if (accept && ready && !busy) {
        begin(h, ImGui::GetTime());
        ImGui::CloseCurrentPopup();
    } else if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- the story system's required package
inline void install_story_notebook(const Host& h) {
    mods::required::Context c;
    c.catalogue = mods::required::catalogue_url(build::version());
    c.fetch = h.fetch, c.port = h.port_version, c.build = h.build_name, c.work = catalogue_work();
    mods::required::install_in_background(c);
}

// ---------------------------------------------------------------- switching
inline void switch_package(const mods::packages::View& v, bool on, const Host* h = nullptr) {
    auto& s = state();
    // a quest mod while the story system's required package is missing: that one first
    if (on && h && mods::story_system() && v.id != mods::required::kStoryNotebook &&
        std::any_of(v.dependencies.begin(), v.dependencies.end(), [](const std::string& d) { return d.rfind(mods::required::kStoryNotebook + ">=", 0) == 0; }) &&
        !find_view(mods::packages::list(), mods::required::kStoryNotebook)) {
        install_story_notebook(*h);
        s.error = "This mod needs the Story Notebook: it is being installed; switch this mod on again afterwards.";
        return;
    }
    if (!on) {
        if (!mods::packages::enable(v.id, false, s.error)) return;
        s.error.clear();
        return;
    }
    if (needs_review(v)) { open_review_for_installed(v); return; }
    if (!v.graphics_conflicts.empty()) { s.graphics = {v.id, v.name, v.graphics_conflicts, true}; return; }
    if (mods::packages::enable(v.id, true, s.error)) s.error.clear();
}

// ---------------------------------------------------------------- the list
inline void open_page(const std::string& id, bool builtin) {
    auto& s = state();
    s.page = id;
    s.page_builtin = builtin;
    s.page_scroll = true;
}
// One row: the switch, then the name (it opens the mod's page) with its status under it.
inline void row_line(const std::string& id, const std::string& name, const Row& r, const std::string& note, bool experimental,
                     bool builtin, const std::function<void(bool)>& toggle) {
    ImGui::PushID(id.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    bool on = r.switch_on;
    ImGui::BeginDisabled(!r.switch_enabled);
    if (on_switch(("##on_" + id).c_str(), &on)) toggle(on);
    ImGui::EndDisabled();
    tooltip(r.switch_reason);
    ImGui::TableNextColumn();
    std::string label = name + "  " + glyph(0x203A, "\xE2\x80\xBA", ">") + "##open_" + id;
    if (selectable(label.c_str())) open_page(id, builtin);
    state().rects["open_" + id] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (experimental) badge("Experimental", experimental::kBadgeColour);
    std::string status = r.text + (note.empty() ? "" : " · " + note);
    colored(status_colour(r.status), status);
    if (!r.switch_enabled && !r.switch_reason.empty()) muted(r.switch_reason);
    ImGui::PopID();
}
inline bool begin_list(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) return false;
    ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("mod", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
// Why a catalogue mod can't be switched on here ("" when it can).
inline std::string unavailable_reason(const Host& h, const mods::catalogue::Entry& e) {
    if (e.kind == "guest" && !h.code_mods) return "Code mods don't work on this platform.";
    bool compatible = false;
    try { compatible = e.compatible(h.port_version, h.build_name, mods::packages::platform_key()); } catch (...) {}
    return compatible ? "" : "Not available for this version of the game.";
}
// Built-in: the mods compiled into the game, each with its switch.
inline void builtin_list(Host& h) {
    muted(kAreaCaptions[kBuiltin]);
    if (!begin_list("builtin")) return;
    for (const auto& e : mods::manager::entries()) {
        // the story system needs the Story Notebook package, which only the mod list provides
        if (e.id == "story-system" && !h.mod_downloads && !find_view(mods::packages::list(), mods::required::kStoryNotebook))
            continue;
        Facts fx;
        fx.builtin = true;
        fx.enabled = e.enabled();
        Row r = row(fx);
        std::string id = e.id;
        row_line(id, e.name, r, e.restart_required ? "applies after a restart" : "", false, true,
                 [id, &h](bool on) {
                     hostui::post([id, on] { mods::manager::set_enabled(id, on); });
                     // the story system comes with its required package: switching it on installs that
                     if (id == "story-system" && on && !find_view(mods::packages::list(), mods::required::kStoryNotebook)) install_story_notebook(h);
                 });
    }
    ImGui::EndTable();
}
// Community: the installed packages (catalogue, file, graphics packs), then the catalogue's mods that aren't
// downloaded yet. Every row has the same switch: switching on a mod that isn't downloaded yet downloads,
// sets up and switches it on (after the review, when there is anything to decide or know).
inline void community_list(Host& h) {
    using namespace mods::packages;
    auto& s = state();
    muted(h.mod_downloads ? kAreaCaptions[kCommunity] : "Mods you add to the mods folder (More > Mods folder).");
    auto list = mods::packages::list();
    auto snap = s.loader.snapshot();
    static const char* test_install = h.test ? getenv("WWHD_TEST_CATALOGUE_INSTALL") : nullptr;
    auto& f = s.job.flow;
    const bool downloading_row = f.active() && !find_view(list, f.id);
    if ((downloading_row || !list.empty()) && begin_list("mods")) {
        // a download in progress that is not a package yet
        if (downloading_row) {
            Facts fx;
            fx.working = f.phase == Phase::Working;
            fx.downloading = fx.working && flow_downloading();
            fx.failed = f.phase == Phase::Failed;
            fx.percent = f.overall() * 100;
            Row r = row(fx);
            r.switch_on = true;
            r.switch_enabled = false;
            r.switch_reason.clear();
            row_line(f.id, f.name, r, "", s.job.accepted.experimental, false, [](bool) {});
        }
        for (const auto& v : list) {
            const auto* e = catalogue_entry(snap, v.id);
            bool update = e && newer(e->version, v.version);
            Row r = row(facts(v, update));
            auto view = v;
            std::string note;
            if (v.id == mods::required::kStoryNotebook && mods::story_system()) {  // part of the story system: locked
                r.switch_enabled = false;
                r.switch_reason = "Part of the story system: turn off the story system (Built-in) to turn it off.";
                note = "Part of the story system";
            }
            const Host* host = &h;
            row_line(v.id, v.name, r, note, v.experimental, false, [view, host](bool on) { switch_package(view, on, host); });
        }
        ImGui::EndTable();
    }
    // the story system's required package (mods/required.h): its status, Install while it is missing
    if (const auto required = mods::required::status(); !required.text.empty()) {
        colored(required.working || required.restart ? kMuted : kWarn, required.text);
        if (h.mod_downloads && !required.working && !find_view(list, mods::required::kStoryNotebook) && mods::story_system()) {
            ImGui::SameLine();
            if (small_button("Install##story_notebook")) install_story_notebook(h);
        }
    }
    // available to download
    if (!h.mod_downloads) return;
    ImGui::SeparatorText("Available to download");
    auto line = catalogue_line(snap.have_list, snap.checking, snap.failed, s.auto_check);
    if (!line.text.empty()) {
        muted(line.text);
        if (line.retry) { ImGui::SameLine(); if (small_button("Retry")) s.loader.refresh(catalogue_url(), catalogue_work(), h.fetch, ImGui::GetTime()); }
    }
    if (!snap.have_list) return;
    size_t hidden = 0, shown = 0;
    if (begin_list("available")) {
        for (const auto& e : snap.catalogue.index.entries) {
            if (find_view(list, e.id) || (s.job.flow.active() && s.job.flow.id == e.id)) continue;
            if (!mods::catalogue::visible(e, s.show_experimental, false)) { hidden++; continue; }
            shown++;
            Facts fx;
            Row r = row(fx);
            auto reason = unavailable_reason(h, e);
            r.switch_enabled = reason.empty();
            r.switch_reason = reason;
            r.text = "Not downloaded";
            auto entry = e;
            auto fixtures = snap.catalogue.fixture_root;
            bool code = e.kind == "guest" || e.kind == "native";
            row_line(e.id, e.name, r, size_text(plan_for_entry(e).download_bytes) + (code ? " · code mod" : ""), e.experimental, false,
                     [entry, fixtures](bool on) { if (on) open_review_for_entry(entry, fixtures, false); });
            if (test_install && e.id == test_install && reason.empty() && !s.job.flow.active()) {
                test_install = nullptr;
                open_review_for_entry(e, snap.catalogue.fixture_root, false);
            }
        }
        ImGui::EndTable();
    }
    if (!shown && !hidden) muted("You have every mod in the list.");
    if (hidden) muted(std::to_string(hidden) + (hidden == 1 ? " experimental mod is hidden." : " experimental mods are hidden."));
    if (ImGui::Checkbox("Show experimental mods", &s.show_experimental))
        hostui::post([on = s.show_experimental] { hostui::set("mod.showExperimental", on ? "1" : "0"); });
    if (!snap.checking) {
        ImGui::SameLine();
        if (small_button(glyph(0x21BB, "\xE2\x86\xBB##refresh", "Refresh##refresh"))) s.loader.refresh(catalogue_url(), catalogue_work(), h.fetch, ImGui::GetTime());
        tooltip("Check for new mods now");
    }
}

// ---------------------------------------------------------------- a mod's page
inline void option_controls(const mods::packages::View& v, bool locked) {
    std::set<std::string> setup_options;
    for (const auto& st : mods::packages::setup_steps(v.id)) if (!st.step.option.empty()) setup_options.insert(st.step.option);
    bool any = false;
    for (const auto& o : v.options) {
        auto os = option_state(setup_options.contains(o.id), locked, v.restart_required);
        if (!os.shown) continue;
        if (!any) { ImGui::SeparatorText("Options"); any = true; }
        ImGui::PushID(o.id.c_str());
        std::string error;
        ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 14, ImGui::GetContentRegionAvail().x * 0.6f));
        if (o.type == "bool") {
            bool value = o.value.boolean;
            if (ImGui::Checkbox(o.name.c_str(), &value)) mods::packages::configure(v.id, o.id, value, error);
        } else if (o.type == "number") {
            double value = o.value.number;
            if (ImGui::SliderScalar(o.name.c_str(), ImGuiDataType_Double, &value, &o.minimum, &o.maximum, "%.3g"))
                mods::packages::configure(v.id, o.id, value, error);
        } else if (o.type == "enum") {
            if (ImGui::BeginCombo(o.name.c_str(), o.value.text.c_str())) {
                for (const auto& c : o.choices)
                    if (selectable(c.c_str(), c == o.value.text)) mods::packages::configure(v.id, o.id, c, error);
                ImGui::EndCombo();
            }
        } else {
            char value[1025];
            snprintf(value, sizeof value, "%s", o.value.text.c_str());
            if (text_entry::field(o.name.c_str(), value, sizeof value, ImGuiInputTextFlags_EnterReturnsTrue))
                mods::packages::configure(v.id, o.id, std::string(value), error);
        }
        tooltip(o.description);
        if (!error.empty()) colored(kBad, error);
        ImGui::PopID();
    }
    if (any && v.restart_required) muted("Changes apply after a restart.");
}
inline void lock_line(const Host& h, const Page& p) {
    if (p.lock_line.empty()) return;
    colored(kWarn, p.lock_line);
    if (p.offer_restart && button("Restart now##page")) { if (h.restart) h.restart(mods::code::status().ready); }
    if (!p.restart_hint.empty()) muted(p.restart_hint);
}
inline void troubleshooting(const Host& h, const mods::packages::View& v, const Page& p) {
    using namespace mods::packages;
    if (!section("Troubleshooting")) return;
    ImGui::Indent();
    auto steps = setup_steps(v.id);
    bool busy = false;
    {
        auto& w = setupflow::setup_work();
        std::lock_guard g(w.mutex);
        busy = w.running;
    }
    busy = busy || !setupflow::setup_run.id.empty() || state().job.flow.phase == Phase::Working;
    if (!steps.empty()) {
        ImGui::SeparatorText("Setup");
        if (!p.offer_run_again) lock_line(h, p);
        for (const auto& st : steps) {
            ImGui::PushID(st.step.id.c_str());
            StepState ss = st.satisfied ? StepState::Done : StepState::NotDone;
            if (flow_for(v.id))
                for (const auto& fs_ : state().job.flow.steps)
                    if (fs_.title == st.step.title && fs_.state != StepState::Done) ss = fs_.state;
            std::string line = std::string(ss == StepState::Done ? check_mark() : ss == StepState::Failed ? cross_mark() : "\xE2\x80\xA2") +
                               "  " + st.step.title + " — " + step_text(ss);
            colored(ss == StepState::Done ? kGood : ss == StepState::Failed ? kBad : kMuted, line);
            if (st.step.type == "game_path" && st.step.game.starts_with("gc_")) {
                auto src = game_source();
                if (!src.path.empty()) { muted("Uses " + file_name(src.path)); tooltip(src.path); }
            }
            bool runnable = st.step.type == "run_tool" || st.step.type == "build_guest_mod";
            if (runnable && p.offer_run_again && !busy && v.game_source_warning.empty()) {
                if (small_button("Run again")) {
                    if (!unconfirmed_native(v.id).empty() || (st.step.type == "build_guest_mod" && needs_code_mod_support(v.id)))
                        open_review_for_installed(v);  // the review asks what is missing first
                    else setupflow::start_setup(v.id, st.step);
                }
            }
            ImGui::PopID();
        }
        std::string failure, output;
        {
            auto& w = setupflow::setup_work();
            std::lock_guard g(w.mutex);
            if (w.id == v.id) { failure = w.error; output = w.output; }
        }
        if (!failure.empty()) { colored(kBad, failure); if (!output.empty()) muted(output); }
        if (busy) muted("A setup is running. Run again is available when it has finished.");
    }
    if (!v.status.empty()) { ImGui::SeparatorText("Status"); muted(v.status); }
    if (!v.reason.empty() && v.reason != v.game_source_warning) colored(kBad, v.reason);
    for (const auto& [path, hash] : v.content_hashes) muted("SHA-256 " + path + ": " + hash);
    ImGui::Unindent();
}
inline void remove_popup(const Host& h) {
    auto& s = state();
    const char* id = "Remove mod##mods_remove";
    if (s.remove_request) { ImGui::OpenPopup(id); s.remove_request = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    std::string name = s.remove_id;
    for (const auto& v : mods::packages::list()) if (v.id == s.remove_id) name = v.name;
    ImGui::PushTextWrapPos(std::min(ImGui::GetFontSize() * 26, ImGui::GetMainViewport()->Size.x * 0.85f));
    text("Remove " + name + "?");
    text("This deletes the mod and the files it prepared (" + size_text(s.remove_bytes) + "). Your saves are not touched.");
    ImGui::PopTextWrapPos();
    bool accept = button("Remove##confirm", ImVec2(ImGui::GetFontSize() * 6, 0));
    same_line_if_fits("Cancel");
    bool cancel = button("Cancel##remove", ImVec2(ImGui::GetFontSize() * 6, 0));
    ImGui::SetItemDefaultFocus();
    if (h.test) if (const char* t = getenv("WWHD_TEST_MOD_REMOVE"); t && s.remove_id == t) accept = true;
    if (h.back || ImGui::IsKeyPressed(ImGuiKey_Escape)) { cancel = true; accept = false; }
    if (accept) {
        std::string error;
        if (mods::packages::remove(s.remove_id, error)) {
            s.message = name + " was removed.";
            s.page.clear();
            fprintf(stderr, "[mods] removed %s\n", s.remove_id.c_str());
        } else s.error = error;
    }
    if (accept || cancel) { s.remove_id.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
inline void package_page(Host& h, const mods::packages::View& v) {
    using namespace mods::packages;
    auto& s = state();
    auto snap = s.loader.snapshot();
    const auto* entry = catalogue_entry(snap, v.id);
    bool update = entry && newer(entry->version, v.version);
    PageFacts pf;
    pf.enabled = v.enabled;
    pf.active = v.active;
    auto steps = setup_steps(v.id);
    pf.has_setup = std::any_of(steps.begin(), steps.end(), [](const auto& st) { return !st.step.optional; });
    pf.setup_done = !needs_setup(v);
    pf.working = flow_working(v.id);
    pf.can_restart = !h.android && h.can_restart && h.can_restart();
    pf.update_available = update;
    pf.live = v.kind == "native" || v.kind == "settings";
    Page p = page(pf);
    Row r = row(facts(v, update));
    // name, switch and status
    ImGui::TextUnformatted(v.name.c_str());
    if (v.experimental) badge("Experimental", experimental::kBadgeColour);
    bool on = r.switch_on;
    ImGui::BeginDisabled(!r.switch_enabled);
    if (on_switch("##on_page", &on)) switch_package(v, on);
    ImGui::EndDisabled();
    ImGui::SameLine();
    colored(status_colour(r.status), r.text);
    if (!r.switch_enabled && !r.switch_reason.empty()) muted(r.switch_reason);
    if (!v.description.empty()) text(v.description);
    if (v.experimental) colored(kWarn, kExperimentalShort);
    if (!v.reason.empty() && v.reason != v.game_source_warning) colored(kBad, "This mod has a problem: " + v.reason);
    const bool flow_shown = flow_for(v.id);
    if (flow_shown) {
        const auto& f = s.job.flow;
        if (f.phase != Phase::Done) ImGui::SeparatorText(f.phase == Phase::Failed ? "Stopped" : flow_verb());
        else ImGui::Separator();
        progress_view(h, h.android);
        ImGui::Separator();
    }
    // setup
    if (pf.has_setup && !flow_for(v.id)) {
        if (p.setup_done) colored(kGood, std::string(check_mark()) + " " + p.setup_summary);
        else {
            Plan plan = plan_for_installed(v);
            Flow preview = Flow::make(v.id, v.name, plan);
            colored(kWarn, p.setup_summary + " — " + duration_text(preview.total_expected()));
            // the switch is the one way in: switching it on asks what is needed and sets it up
            if (p.offer_set_up) muted("Switch it on to set it up.");
        }
    }
    if (!flow_shown) lock_line(h, p);  // the progress view says what happens next
    option_controls(v, p.setup_locked);
    // update and remove
    ImGui::Spacing();
    if (update) {
        colored(kWarn, "Version " + entry->version + " is available.");
        if (p.offer_update) {
            if (button("Update##page")) open_review_for_entry(*entry, snap.catalogue.fixture_root, true, &v);
        } else if (!p.update_line.empty() && p.update_line != p.lock_line) muted(p.update_line);
    }
    if (p.offer_remove && !flow_shown) {
        if (button("Remove##page")) { s.remove_id = v.id; s.remove_bytes = disk_use(v.id); s.remove_request = true; }
    } else if (!flow_shown && !p.remove_line.empty() && p.remove_line != p.lock_line) muted(p.remove_line);
    if (h.test) if (const char* t = getenv("WWHD_TEST_MOD_REMOVE"); t && v.id == t && p.offer_remove && s.remove_id.empty()) {
        s.remove_id = v.id; s.remove_bytes = disk_use(v.id); s.remove_request = true;
    }
    // details
    if (section("Details")) {
        ImGui::Indent();
        muted("Version " + v.version);
        if (!v.author.empty()) muted("By " + v.author);
        muted(kind_words(v.kind));
        if (v.kind == "native" || v.kind == "guest" || !v.setup_tools.empty())
            muted(v.native_confirmed ? "Contains code that runs on your computer. You allowed this version."
                                     : "Contains code that runs on your computer. Switching it on asks you first.");
        if (entry) for (const auto& l : entry->licences) muted("Licence: " + l);
        for (const auto& d : v.dependencies) muted("Needs " + d);
        for (const auto& c : v.conflicts) muted("Can't be used with " + c);
        for (const auto& c : v.graphics_conflicts) muted("Can't be used with " + c.name + " (" + c.reason + ")");
        ImGui::Unindent();
    }
    troubleshooting(h, v, p);
}
inline void builtin_page(Host& h, const mods::manager::Entry& e) {
    ImGui::TextUnformatted(e.name);
    bool on = e.enabled();
    std::string id = e.id;
    if (on_switch("##on_builtin", &on)) {
        hostui::post([id, on] { mods::manager::set_enabled(id, on); });
        if (id == "story-system" && on && !find_view(mods::packages::list(), mods::required::kStoryNotebook)) install_story_notebook(h);
    }
    ImGui::SameLine();
    colored(on ? kGood : kMuted, on ? "On" : "Off");
    text(e.description);
    if (e.restart_required) muted("Changes apply after a restart.");
    if (const char* o = getenv(e.startup_env)) muted(std::string(e.startup_env) + "=" + o + " overrides the saved choice at startup.");
    if (h.builtin_options) h.builtin_options(e.id);
    muted("Built into the game. Your choices are saved in the current profile.");
}
inline void installing_page(Host& h) {
    auto& f = state().job.flow;
    ImGui::TextUnformatted(f.name.c_str());
    if (state().job.accepted.experimental) badge("Experimental", experimental::kBadgeColour);
    if (!state().job.accepted.description.empty()) text(first_sentence(state().job.accepted.description, 240));
    ImGui::SeparatorText(f.phase == Phase::Failed ? "Stopped" : flow_verb());
    progress_view(h, h.android);
}
inline void available_page(Host& h, const mods::catalogue::Entry& e, const fs::path& fixtures);
inline void mod_page(Host& h) {
    auto& s = state();
    if (button((std::string(glyph(0x2039, "\xE2\x80\xB9", "<")) + " " + kAreaNames[s.area] + "##back").c_str()) || (h.back && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))) {
        if (h.back) h.b_used = true;
        s.page.clear();
        return;
    }
    if (s.page_scroll) { ImGui::SetScrollHereY(0); s.page_scroll = false; }
    ImGui::Separator();
    if (s.page_builtin) {
        if (const auto* e = mods::manager::find(s.page)) builtin_page(h, *e);
        else s.page.clear();
        return;
    }
    auto list = mods::packages::list();
    if (const auto* v = find_view(list, s.page)) package_page(h, *v);
    else if (state().job.flow.active() && state().job.flow.id == s.page) installing_page(h);
    else if (auto snap = s.loader.snapshot(); const auto* e = catalogue_entry(snap, s.page)) available_page(h, *e, snap.catalogue.fixture_root);
    else s.page.clear();
}

// ---------------------------------------------------------------- a mod that isn't downloaded yet
inline void available_page(Host& h, const mods::catalogue::Entry& e, const fs::path& fixtures) {
    ImGui::TextUnformatted(e.name.c_str());
    if (e.experimental) badge("Experimental", experimental::kBadgeColour);
    auto reason = unavailable_reason(h, e);
    bool on = false;
    ImGui::BeginDisabled(!reason.empty());
    if (on_switch("##on_available", &on) && on) open_review_for_entry(e, fixtures, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    muted("Not downloaded · " + size_text(plan_for_entry(e).download_bytes));
    if (!reason.empty()) muted(reason);
    else muted("Switch it on to download it, set it up and use it.");
    text(e.description);
    if (e.experimental) colored(kWarn, kExperimentalShort);
    if (section("Details")) {
        ImGui::Indent();
        muted("Version " + e.version);
        for (const auto& a : e.authors) muted("By " + a);
        for (const auto& l : e.licences) muted("Licence: " + l);
        muted(kind_words(e.kind));
        bool code = e.kind == "guest" || e.kind == "native" ||
                    std::any_of(e.setup.begin(), e.setup.end(), [](const auto& st) { return st.type == "run_tool"; });
        if (code) muted(kRunsCode);
        if (std::any_of(e.setup.begin(), e.setup.end(), [](const auto& st) { return st.type == "game_path"; }))
            muted("Uses files from your GameCube copy of the game.");
        for (const auto& d : e.dependencies) muted("Needs " + d);
        ImGui::Unindent();
    }
}

// More > Install from a file or folder (the picker answers later, from the host): install it, open its page
// and, when switching it on needs anything, its review. Runs whatever area is shown.
inline void install_picked(Host& h) {
    using namespace mods::packages;
    auto& s = state();
    std::string picked;
    { std::lock_guard g(s.picker_mutex); picked = std::move(s.picked_package); s.picked_package.clear(); }
    static const char* test_local = h.test ? getenv("WWHD_TEST_MOD_INSTALL") : nullptr;
    if (test_local) { picked = test_local; test_local = nullptr; }
    if (!picked.empty()) {
        std::string id, error;
        if (install(picked, error, &id)) {
            s.error.clear();
            for (const auto& v : list()) if (v.id == id) {
                s.message = v.name + " was installed.";
                go_area(kCommunity);
                open_page(id, false);
                if (v.compatible) {  // straight on to switching it on: the review asks what it needs
                    if (needs_review(v)) open_review_for_installed(v);
                }
            }
        } else s.error = error;
    }
}

// ---------------------------------------------------------------- More
inline void more(Host& h) {
    using namespace mods::packages;
    auto& s = state();
    if (button("Switch all mods off")) hostui::post([] { mods::packages::disable_all(); });
    muted("Switches off every mod, built-in ones included, in the current profile.");
    bool busy = !setupflow::setup_run.id.empty() || s.job.flow.phase == Phase::Working;
    if (section("Profiles")) {
        ImGui::Indent();
        muted("A profile remembers which mods are on and their options.");
        if (busy) muted("Profiles can be changed when the current setup has finished.");
        ImGui::BeginDisabled(busy);
        auto current = current_profile();
        ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 14, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("Profile", current.c_str())) {
            for (const auto& name : profiles())
                if (selectable(name.c_str(), name == current)) select_profile(name, s.error);
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 14, ImGui::GetContentRegionAvail().x));
        text_entry::field("##new_profile", s.profile_name, sizeof s.profile_name, 0, "New profile name");
        same_line_if_fits("Copy current");
        ImGui::BeginDisabled(!s.profile_name[0]);
        if (button("Copy current") && create_profile(s.profile_name, s.error)) s.profile_name[0] = 0;
        ImGui::EndDisabled();
        for (const auto& name : profiles()) {
            if (name == current) continue;
            ImGui::PushID(name.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine();
            if (small_button("Delete")) delete_profile(name, s.error);
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::Unindent();
    }
    if (h.file_picker && section("Install from a file or folder")) {
        ImGui::Indent();
        muted("A mod package (.wwhdmod or ZIP), a mod folder, or a replacement .pack file.");
        auto pick = [&h](bool folder) {
            if (!h.choose_mod_source) return;
            h.choose_mod_source(folder, [](std::string path) {
                std::lock_guard g(state().picker_mutex);
                state().picked_package = std::move(path);
            });
        };
        if (button("Choose file…##package")) pick(false);
        same_line_if_fits("Choose folder…");
        if (button("Choose folder…##package")) pick(true);
        ImGui::Unindent();
    }
    if (h.file_picker && section("GameCube game for mods")) {  // (chosen with a file picker)
        ImGui::Indent();
        muted("Some mods use files from the GameCube version of the game. They stay on your computer.");
        game_source_picker(h, "gc_wind_waker");
        auto src = game_source();
        if (!src.path.empty() && small_button("Forget it")) { std::string e; set_game_source("gc_wind_waker", "", e); }
        ImGui::Unindent();
    }
    if (h.code_mods && section("Code-mod support")) {
        ImGui::Indent();
        bool code = mods::code::enabled();
        // not the section's own label: two items with one ID
        if (ImGui::Checkbox("Use code mods##code_mods_switch", &code)) mods::code::request(code);
        muted((std::string("Code mods need the game code rebuilt with mod support (") + mods::code::rebuild_duration() +
               ") and a restart. Installing a code mod turns this on by itself.").c_str());
        ImGui::Unindent();
    }
    if (section("Save backups")) {
        ImGui::Indent();
        if (experimental::backups_section(h.back)) h.b_used = true;
        ImGui::Unindent();
    }
    if (h.mod_downloads && section("Mod list")) {
        ImGui::Indent();
        if (ImGui::Checkbox("Check for new mods automatically", &s.auto_check))
            hostui::post([on = s.auto_check] { hostui::set("mod.catalogue.auto", on ? "1" : "0"); });
        muted("Checking only downloads the public list of mods (no personal data, no game data). Mods are downloaded "
              "only when you press Install or Update.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field("##catalogue_url", s.url_edit, sizeof s.url_edit);
        bool changed = std::string(s.url_edit) != s.catalogue_url;
        ImGui::BeginDisabled(!changed);
        if (button("Use this list")) {
            s.catalogue_url = s.url_edit;
            if (!getenv("WWHD_MOD_CATALOGUE")) hostui::post([u = s.catalogue_url] { hostui::set("mod.catalogue.url", u); });
            s.loader.refresh(s.catalogue_url, catalogue_work(), h.fetch, ImGui::GetTime());
        }
        ImGui::EndDisabled();
        same_line_if_fits("Reset to default");
        ImGui::BeginDisabled(s.catalogue_url == kDefaultCatalogue && !changed);
        if (button("Reset to default")) {
            snprintf(s.url_edit, sizeof s.url_edit, "%s", kDefaultCatalogue);
            s.catalogue_url = kDefaultCatalogue;
            if (!getenv("WWHD_MOD_CATALOGUE")) hostui::post([] { hostui::set("mod.catalogue.url", std::string(kDefaultCatalogue)); });
            s.loader.refresh(s.catalogue_url, catalogue_work(), h.fetch, ImGui::GetTime());
        }
        ImGui::EndDisabled();
        auto snap = s.loader.snapshot();
        if (snap.failed && !snap.error.empty()) muted("Last check: " + snap.error);
        ImGui::Unindent();
    }
    if (h.cheats && section("Cheats")) {
        ImGui::Indent();
        h.cheats();
        ImGui::Unindent();
    }
    if (section("Mods folder")) {
        ImGui::Indent();
        auto dir = directory();
        if (!dir.empty()) { muted(dir); tooltip(dir); }
        if (small_button("Look for changes")) refresh(s.error);
        muted("After adding or changing mod folders by hand while all mods are off.");
        ImGui::Unindent();
    }
}

// ---------------------------------------------------------------- the tab
// Controller L / R: previous / next area; returns false at the ends (the overlay switches tabs then).
inline bool shoulder(int direction) {
    auto& s = state();
    int next = s.area + direction;
    if (next < 0 || next >= kAreas) return false;
    go_area(next);
    s.page.clear();
    return true;
}
inline void apply_test_view(Host& h) {
    static bool done = false;
    if (done || !h.test) return;
    done = true;
    const char* v = getenv("WWHD_TEST_MODS_VIEW");  // mods | more | page:<id>
    if (!v) return;
    std::string view = v;
    if (view == "more") go_area(kMore);
    else if (view == "community") go_area(kCommunity);
    else if (view.starts_with("page:")) {
        bool builtin = mods::manager::find(view.substr(5)) != nullptr;
        go_area(builtin ? kBuiltin : kCommunity);
        open_page(view.substr(5), builtin);
    }
}
inline void test_switches(Host& h) {
    if (!h.test) return;
    static const char* enable = getenv("WWHD_TEST_MOD_ENABLE");
    static const char* disable = getenv("WWHD_TEST_MOD_DISABLE");
    for (const auto& v : mods::packages::list()) {
        if (enable && v.id == enable) {
            // with WWHD_TEST_GAME_SOURCES the test sets the GameCube game the review would ask for
            if (const char* sources = getenv("WWHD_TEST_GAME_SOURCES")) {
                auto plan = diagnostic::read_setup_plan(true, getenv("WWHD_MOD_MANAGER_DIR"), v.id.c_str(), sources);
                std::string error;
                if (plan.error.empty())
                    for (const auto& [game, path] : plan.sources.object) mods::packages::set_game_source(game, path.string(), error);
            }
            enable = nullptr;
            if (!v.enabled) switch_package(v, true);
        }
        if (disable && v.id == disable) { disable = nullptr; switch_package(v, false); }
        if (state().job.flow.phase == Phase::Done && state().job.flow.id == v.id && v.enabled) {
            static std::string reported;
            if (reported != v.id) { reported = v.id; fprintf(stderr, "[setup test] ready %s\n", v.id.c_str()); }
        }
    }
    // with WWHD_TEST_CATALOGUE_INSTALL the catalogue's review needs the sources before Install
    static bool sources_set = false;
    if (!sources_set && getenv("WWHD_TEST_CATALOGUE_INSTALL") && getenv("WWHD_TEST_GAME_SOURCES")) {
        sources_set = true;
        auto plan = diagnostic::read_setup_plan(true, getenv("WWHD_MOD_MANAGER_DIR"), getenv("WWHD_TEST_CATALOGUE_INSTALL"),
                                                getenv("WWHD_TEST_GAME_SOURCES"));
        std::string error;
        if (plan.error.empty())
            for (const auto& [game, path] : plan.sources.object) mods::packages::set_game_source(game, path.string(), error);
    }
}
inline void restart_bar(const Host& h, const std::string& summary) {
    if (summary.empty()) return;
    colored(kWarn, "Restart to apply: " + summary + ".");
    if (h.android) muted("Close and reopen the app to apply.");
    else if (h.can_restart && h.can_restart()) {
        ImGui::SameLine();
        if (small_button("Restart now##bar") && h.restart) h.restart(mods::code::status().ready);
    } else muted("Restart the game to apply.");
}
// The whole tab. restart_summary: what a restart would apply (restart_notice.h summary), "" for nothing.
inline void draw(Host& h, const std::string& restart_summary) {
    auto& s = state();
    g_expand_all = h.expand_all;
    apply_test_view(h);
    test_switches(h);
    install_picked(h);
    open_catalogue(h);  // shows the cached list at once; checks for new mods once per session
    // one Restart now at a time: the end of an install and the page of a mod that is off but still loaded
    // offer their own
    bool page_restart = false;
    if (!s.page.empty() && !s.page_builtin)
        for (const auto& v : mods::packages::list()) if (v.id == s.page) page_restart = v.active && !v.enabled;
    if (s.job.flow.phase != Phase::Done && !page_restart) restart_bar(h, restart_summary);
    if (!s.error.empty()) {
        colored(kBad, s.error);
        ImGui::SameLine();
        if (small_button("OK##error")) s.error.clear();
    }
    // the manager's notices (a graphics pack switched off, a changed GameCube game): one bar, one OK
    for (auto& n : mods::packages::take_notices()) s.notices.push_back(std::move(n));
    if (!s.notices.empty()) {
        for (const auto& n : s.notices) colored(kWarn, n);
        if (small_button("OK##notices")) s.notices.clear();
    }
    if (!s.message.empty()) {
        muted(s.message);
        ImGui::SameLine();
        if (small_button("OK##message")) s.message.clear();
    }
    // the three areas (L / R on a controller)
    if (ImGui::BeginTabBar("mods_areas", ImGuiTabBarFlags_FittingPolicyShrink)) {
        for (int i = 0; i < kAreas; i++) {
            ImGuiTabItemFlags f = s.select_area == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kAreaNames[i], nullptr, f)) {
                if (s.area != i && s.select_area < 0) { s.area = i; s.page.clear(); }  // clicked
                ImGui::EndTabItem();
            }
        }
        s.select_area = -1;
        ImGui::EndTabBar();
    }
    ImGui::BeginChild("mods_area", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    ImGui::PushTextWrapPos(0);
    switch (s.area) {
    case kBuiltin: if (s.page.empty()) builtin_list(h); else mod_page(h); break;
    case kCommunity: if (s.page.empty()) community_list(h); else mod_page(h); break;
    default: more(h); break;
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    review_popup(h);
    remove_popup(h);
    auto action = graphics_switch_dialog(s.graphics, h.back);
    if (action == GraphicsSwitchAction::Switch) mods::packages::enable(s.graphics.id, true, s.error, true);
    if (action == GraphicsSwitchAction::Cancel && h.back) h.b_used = true;
}
// Over the game while the menu is closed: the running install and its end.
inline void background(const Host& h) {
    auto& s = state();
    auto& f = s.job.flow;
    const double now = ImGui::GetTime();
    std::string line;
    if (f.phase == Phase::Working) line = std::string(flow_verb()) + " " + f.name + "… " + percent_text(f.overall() * 100);
    else if (now < s.toast_until) line = s.toast;
    else return;
    (void)h;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(std::max(200.0f, ds.x - 32), FLT_MAX));
    if (ImGui::Begin("##mods_background", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoInputs)) {
        ImGui::PushTextWrapPos(std::min(ds.x - 48, ImGui::GetFontSize() * 26));
        ImGui::TextUnformatted(line.c_str());
        if (f.phase == Phase::Working) ImGui::ProgressBar(float(f.overall()), ImVec2(ImGui::GetFontSize() * 12, 4), "");
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
// Something to show over the game (the frame must be drawn even with the menu closed).
inline bool wants_background() {
    auto& s = state();
    return s.job.flow.phase == Phase::Working || !setupflow::setup_run.id.empty() || ImGui::GetTime() < s.toast_until;
}
}  // namespace overlay::modsui
