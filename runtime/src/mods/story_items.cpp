// Mod items (story_items.h, docs/mod-sdk-v2.md "Items").
//
// The game's item tables for the pool numbers (HD decomp d/d_item.cpp; the 2026-10-14 audit):
//   - execItemGet(itemNo) 0254DA38 and checkItemGet(itemNo, default) 0254DA50 call the 256-entry
//     item_func / item_getcheck tables (0x101E3A98, 0x101E3E98) with the number unmasked. For the pool
//     numbers both are the game's empty stub and "return -1". The port answers them instead: a get
//     of a bound number gives Link that item, a check says whether Link has it. No game data names a
//     pool number, so only the port's own paths reach these.
//   - The game's get-item event can't show a pool number (item_resource has no model or message for
//     them): a give shows the item's caption with the game's small get jingle (JA_SE_ME_ITEM_GET_S, the
//     sound of a rupee from a chest) instead.
#include "story_items.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "guest_addr.h"
#include "runtime.h"
#include "story_store.h"

namespace story_items {
namespace {

const uint32_t kSavePtr = GD(0x101F84DC);       // save base pointer (d_com_inf_game)
const uint32_t kExecItemGet = GC(0x0254DA38);   // execItemGet(itemNo)
const uint32_t kCheckItemGet = GC(0x0254DA50);  // checkItemGet(itemNo, default)
const uint32_t kSeStartSystem = GC(0x025E1988); // mDoAud_seStartSystem(id)
constexpr uint32_t kJingle = 0x8F5;             // JA_SE_ME_ITEM_GET_S
constexpr uint32_t kSaveReserve = 0xA6;         // the save's delivery bag places (dSv_player_bag_item_c::mReserve, 8 bytes)

std::mutex g_mu;
prop_rewards::Registry g_prop_markers;
std::vector<Item> g_items;               // declaration order
std::map<uint16_t, unsigned> g_uses;     // use action "event": uses not yet read by the mod (session)
struct Pending {
    std::string title, text;
};
std::deque<Pending> g_pending;  // get captions to show (game thread)
std::string g_reason;
ShowInfo g_show = nullptr;
bool (*g_open_notebook)(const char*) = nullptr;
int32_t (*g_reach)(const std::string&, const std::string&, uint32_t) = nullptr;
bool g_imported = false, g_installed = false;
std::atomic<unsigned> g_pages{kPages};
int (*g_frame)(uint16_t) = nullptr;
void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_ITEM_TRACE"); return e && *e == '1'; }();
    return on;
}
uint32_t save_base() { return ld32(kSavePtr); }
bool have_save() { return save_base() != 0 && story_store::book().usable(); }
Bag bag() { return Bag(story_store::book(), g_pages.load()); }

const Item* find_locked(uint16_t key) {
    for (const Item& i : g_items)
        if (i.key == key) return &i;
    return nullptr;
}
const Item* find_locked(const std::string& mod, const std::string& id) {
    for (const Item& i : g_items)
        if (i.mod == mod && i.id == id) return &i;
    return nullptr;
}
uint64_t file_hash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    uint64_t h = 1469598103934665603ull;
    char buf[4096];
    while (f.read(buf, sizeof buf) || f.gcount())
        for (std::streamsize i = 0; i < f.gcount(); i++) h = (h ^ uint8_t(buf[i])) * 1099511628211ull;
    return h ? h : 1;
}
// what the game's own save holds in a delivery bag place (0..7): 0xFF empty
uint8_t game_reserve(unsigned place) { return have_save() && place < kGamePlaces ? ld8(save_base() + kSaveReserve + place) : 0; }

}  // namespace

namespace { std::function<void(uint16_t)> model_presenter; }
void set_model_presenter(std::function<void(uint16_t)> fn){model_presenter=std::move(fn);}
bool define(const std::string& mod, const std::string& package_dir, const story_notebook::ManifestItem& m, std::string& why) {
    Item it;
    it.key = item_key(mod, m.id), it.mod = mod, it.id = m.id, it.name = m.name, it.description = m.description;
    it.icon_path = (std::filesystem::path(package_dir) / m.icon).string();
    it.icon_hash = file_hash(it.icon_path);
    if (!it.icon_hash) return why = "item " + m.id + ": no icon file " + m.icon, false;
    it.held_model=m.held_model;it.held_frames=m.held_frames;
    it.stack = m.stack, it.use = m.use, it.use_quest = m.use_quest, it.use_step = m.use_step;
    std::lock_guard lock(g_mu);
    for (Item& o : g_items) {
        if (o.mod == mod && o.id == m.id) {  // declared again: replaced
            o = it;
            return true;
        }
        if (o.key == it.key)
            return why = "item " + m.id + " can't be told apart from " + o.mod + "'s item " + o.id + " in the save file (same key); rename one", false;
    }
    g_items.push_back(std::move(it));
    return true;
}

std::vector<Item> installed() {
    std::lock_guard lock(g_mu);
    return g_items;
}
std::optional<Item> find(uint16_t key) {
    std::lock_guard lock(g_mu);
    const Item* i = find_locked(key);
    return i ? std::optional<Item>(*i) : std::nullopt;
}
std::optional<Item> find(const std::string& mod, const std::string& id) {
    std::lock_guard lock(g_mu);
    const Item* i = find_locked(mod, id);
    return i ? std::optional<Item>(*i) : std::nullopt;
}

uint8_t count(uint16_t key) { return have_save() ? bag().count(key) : 0; }
uint8_t place(uint16_t key) { return have_save() ? bag().place(key) : 0; }
uint32_t number(uint16_t key) { return have_save() ? bag().number(key) : 0; }
std::optional<Item> by_number(uint32_t n) {
    if (!pool_number(n) || !have_save()) return std::nullopt;
    const uint16_t key = bag().with_number(uint8_t(n));
    return key ? find(key) : std::nullopt;
}
uint16_t at_game_place(unsigned p) {
    if (p >= kGamePlaces || !have_save()) return 0;
    const uint16_t key = bag().at(uint8_t(p + 1));
    return key && find(key) ? key : 0;  // an uninstalled mod's item stays hidden
}
uint16_t on_button(unsigned button) {
    if (!have_save()) return 0;
    const uint16_t key = bag().on_button(button);
    return key && find(key) ? key : 0;
}

std::vector<uint16_t> owned() {
    std::vector<uint16_t> out;
    if (!have_save()) return out;
    for (uint16_t k : bag().owned())
        if (find(k)) out.push_back(k);
    return out;
}
std::vector<uint16_t> unplaced() {
    std::vector<uint16_t> out;
    if (!have_save()) return out;
    for (uint16_t k : bag().unplaced())
        if (find(k)) out.push_back(k);
    return out;
}
uint32_t number_at_game_place(unsigned p) {
    const uint16_t key = at_game_place(p);
    if (!key) return 0;
    Bag b = bag();
    return b.bind(key);
}
void set_page_limit(unsigned pages) { g_pages = std::clamp(pages, 1u, kMaxPages); }
unsigned page_limit() { return g_pages.load(); }
void set_frame_source(int (*f)(uint16_t)) { g_frame = f; }
int frame(uint16_t key) { return g_frame ? g_frame(key) : -1; }

bool notebook_on_button(unsigned button) { return have_save() && button < kButtons && (bag().notebook_buttons() >> button & 1); }
bool assign_notebook(int button) {
    if (!have_save()) return false;
    Bag b = bag();
    if (button < 0) {
        for (unsigned btn = 0; btn < kButtons; btn++) b.set_notebook_button(btn, false);
        return true;
    }
    return b.set_notebook_button(unsigned(button), true);
}

PropMarkerCheckpoint prop_marker_checkpoint() {
    std::lock_guard lock(g_mu);
    return g_prop_markers.checkpoint();
}
bool rollback_prop_markers(PropMarkerCheckpoint checkpoint) {
    std::lock_guard lock(g_mu);
    return g_prop_markers.rollback(checkpoint);
}
PropMarkerHandle register_prop_marker(const std::string& mod, const std::string& actor_id,
                                     const std::string& broken_flag, std::string& why) {
    std::lock_guard lock(g_mu);
    return g_prop_markers.add(mod, actor_id, broken_flag, why);
}
bool prop_broken(PropMarkerHandle handle) {
    std::lock_guard lock(g_mu);
    const auto* marker = g_prop_markers.get(handle);
    return marker && have_save() && story_store::book().get(prop_rewards::kBroken, marker->key) == 1;
}
EnemyMarkerHandle register_enemy_marker(const std::string& mod, const std::string& actor_id,
                                       const std::string& defeat_flag, std::string& why) {
    std::lock_guard lock(g_mu);
    return {g_prop_markers.add(mod, actor_id, defeat_flag, why, prop_rewards::kDefeated)};
}
bool forget_prop_marker(PropMarkerHandle handle) {
    std::lock_guard lock(g_mu);
    return g_prop_markers.remove(handle);
}
bool enemy_defeated(EnemyMarkerHandle handle) {
    std::lock_guard lock(g_mu);
    const auto* marker = g_prop_markers.get(handle.value, prop_rewards::kDefeated);
    return marker && have_save() && story_store::book().get(prop_rewards::kDefeated, marker->key) == 1;
}
static PropClaimStatus claim_marker_drops(PropMarkerHandle handle, uint8_t marker_kind, const std::string& mod,
                                const std::vector<PropDrop>& drops, bool caption) try {
    std::lock_guard lock(g_mu); // no guest/native calls or callbacks in this transaction
    const auto* marker = g_prop_markers.get(handle, marker_kind);
    if (!marker || marker->mod != mod || drops.size() > 8) return PropClaimStatus::InvalidDefinition;
    if (!have_save()) return PropClaimStatus::StoreUnavailable;
    std::vector<prop_rewards::Drop> resolved;
    std::vector<const Item*> items;
    for (const auto& d : drops) {
        if (!story_mail::valid_id(d.item)) return PropClaimStatus::InvalidDefinition;
        const auto* it = find_locked(mod, d.item);
        if (!it) return PropClaimStatus::InvalidDefinition;
        resolved.push_back({it->key, d.quantity, it->stack}); items.push_back(it);
    }
    const uint32_t loaded_save = save_base();
    uint8_t* const area = story_store::area();
    const auto plan = prop_rewards::stage(area, marker->key, resolved, g_pages.load(), marker_kind);
    if (plan.status != PropClaimStatus::Committed) return plan.status;
    // All allocating caption preparation precedes commit; notification itself may fail afterwards.
    std::vector<Pending> pending;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!caption && plan.placed[i]) continue;
        const auto& it = *items[i];
        std::string text = it.description;
        if (!plan.placed[i]) text += "\n\nThe Delivery Bag is full: it is kept until a place is free.";
        pending.push_back({it.name + (plan.counts[i] > 1 && it.stack > 1 ?
            " (" + std::to_string(plan.counts[i]) + ")" : ""), std::move(text)});
    }
    if (save_base() != loaded_save || story_store::area() != area || g_prop_markers.get(handle, marker_kind) != marker)
        return PropClaimStatus::Stale;
    const auto result = prop_rewards::commit(area, plan, [&] {
        for (auto& message : pending) g_pending.push_back(std::move(message));
    });
    if (result == PropClaimStatus::Committed) try {
        // Evidence reads the authoritative loaded bytes after commit, never planned counts.
        const char* trace = std::getenv("WWHD_NATIVE_ACTOR_TRACE");
        static unsigned reports = 0; // serialized game-logic caller, host-only bounded diagnostic
        if (trace && std::strcmp(trace, "1") == 0) {
            const story_mail::Book live(area, prop_rewards::kRecords);
            if (reports < 512) {
                ++reports;
                LOG("[%s] %s:%s marker_key=%04X %s=%u", marker_kind == prop_rewards::kBroken ? "native_prop_reward" : "native_enemy_reward", mod.c_str(), marker->actor.c_str(),
                    marker->key, marker_kind == prop_rewards::kBroken ? "broken" : "defeated", unsigned(live.get(marker_kind, marker->key)));
            }
            for (size_t i = 0; i < items.size() && reports < 512; ++i) {
                ++reports;
                LOG("[%s] %s:%s item=%s item_key=%04X count=%u", marker_kind == prop_rewards::kBroken ? "native_prop_reward" : "native_enemy_reward", mod.c_str(), marker->actor.c_str(),
                    items[i]->id.c_str(), items[i]->key, unsigned(live.get(kRecordItemCount, items[i]->key)));
            }
        }
    } catch (...) { /* diagnostics cannot turn a committed grant into a retry */ }
    return result;
} catch (...) {
    // Allocation/preparation failure precedes commit; postcommit notifications are caught inside commit.
    return PropClaimStatus::StoreUnavailable;
}

PropClaimStatus claim_prop_drops(PropMarkerHandle handle, const std::string& mod,
                                const std::vector<PropDrop>& drops, bool caption) {
    return claim_marker_drops(handle, prop_rewards::kBroken, mod, drops, caption);
}
EnemyClaimStatus claim_enemy_drops(EnemyMarkerHandle handle, const std::string& mod,
                                  const std::vector<PropDrop>& drops, bool caption) {
    return claim_marker_drops(handle.value, prop_rewards::kDefeated, mod, drops, caption);
}

int32_t give(const std::string& mod, const std::string& id, uint32_t n, const char* how, bool caption) {
    const std::optional<Item> it = find(mod, id);
    if (!it || n == 0 || n > kMaxCount) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    Bag b = bag();
    const uint8_t before = b.count(it->key);
    bool placed = false;
    const int now = b.give(it->key, n, it->stack, placed);
    if (now < 0) {
        LOG("[items] %s: no room for item \"%s\" in this save file's story block", mod.c_str(), id.c_str());
        return kResultNoRoom;
    }
    if (now != before) {
        LOG("[items] %s: Link gets item \"%s\" (%s): %d now%s", mod.c_str(), id.c_str(), how, now,
            placed ? "" : "; the bag's pages are full, it waits for a free place");
        std::string text = it->description;
        if (!placed) text += "\n\nThe Delivery Bag is full: it is kept until a place is free.";
        if (caption || !placed) {  // a quiet get still says when the bag is full
            std::lock_guard lock(g_mu);
            g_pending.push_back({it->name + (now > 1 && it->stack > 1 ? " (" + std::to_string(now) + ")" : ""), text});
        }
        // test aid: WWHD_ITEM_TEST_PLACE=mod:item:place puts a new item in one of the game's places (0..7),
        // as the bag page would
        static const char* test_place = getenv("WWHD_ITEM_TEST_PLACE");
        if (!before && test_place && *test_place) {
            const std::string spec = test_place, want = mod + ":" + id + ":";
            if (spec.rfind(want, 0) == 0)
                LOG("[items] test: %s:%s to the game's place %s: %s", mod.c_str(), id.c_str(), spec.c_str() + want.size(),
                    act(it->key, Action::MoveToGamePlace, std::atoi(spec.c_str() + want.size())) ? "done" : reason().c_str());
        }
    }
    return now;
}
int32_t take(const std::string& mod, const std::string& id, uint32_t n) {
    const std::optional<Item> it = find(mod, id);
    if (!it || n == 0 || n > kMaxCount) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    Bag b = bag();
    const int now = b.take(it->key, n);
    if (now < 0) return kResultNotEnough;
    if (trace()) LOG("[items] %s: item \"%s\" taken: %d left", mod.c_str(), id.c_str(), now);
    // an item waiting for a place gets the one freed
    if (!now)
        for (uint16_t k : b.owned())
            if (!b.place(k) && find(k) && b.place_on_pages(k)) break;
    return now;
}
int32_t count(const std::string& mod, const std::string& id) {
    const std::optional<Item> it = find(mod, id);
    if (!it) return kResultInvalid;
    return have_save() ? bag().count(it->key) : 0;
}
int32_t used(const std::string& mod, const std::string& id) {
    const std::optional<Item> it = find(mod, id);
    if (!it) return kResultInvalid;
    std::lock_guard lock(g_mu);
    auto u = g_uses.find(it->key);
    if (u == g_uses.end()) return 0;
    const int32_t n = int32_t(u->second);
    g_uses.erase(u);
    return n;
}

std::string reason() {
    std::lock_guard lock(g_mu);
    return g_reason;
}

bool act(uint16_t key, Action action, int arg) {
    auto refuse = [](const char* why) {
        std::lock_guard lock(g_mu);
        g_reason = why;
        if (trace()) LOG("[items] refused: %s", why);
        return false;
    };
    const std::optional<Item> it = find(key);
    if (!it) return refuse("This item's mod is not enabled.");
    if (!have_save() || !bag().count(key)) return refuse("Link doesn't have this item.");
    Bag b = bag();
    switch (action) {
    case Action::Info:
        return g_show && g_show(it->name, it->description, "the item's Info") ? true : refuse("The item's Info can't be shown now.");
    case Action::Use: {
        auto used=[&](bool ok){if(ok&&model_presenter&&!it->held_model.empty())model_presenter(key);return ok;};
        switch (it->use) {
        case story_notebook::ManifestItem::Use::None: return refuse("This item can't be used.");
        case story_notebook::ManifestItem::Use::Show:
            return used(g_show && g_show(it->name, it->description, "the item's use") ? true : refuse("The item can't be shown now."));
        case story_notebook::ManifestItem::Use::OpenNotebook:
            return used(g_open_notebook && g_open_notebook("an item's use") ? true : refuse("The notebook can't be opened now."));
        case story_notebook::ManifestItem::Use::QuestStep:
            if (!g_reach || g_reach(it->mod, it->use_quest, it->use_step) < 0) return refuse("Nothing happens.");
            LOG("[items] %s: item \"%s\" used: quest \"%s\" step %u", it->mod.c_str(), it->id.c_str(), it->use_quest.c_str(), it->use_step);
            return used(true);
        case story_notebook::ManifestItem::Use::Event: {
            std::lock_guard lock(g_mu);
            unsigned& n = g_uses[key];
            n = std::min(n + 1, 255u);
            if (trace()) LOG("[items] %s: item \"%s\" used (%u unread)", it->mod.c_str(), it->id.c_str(), n);
            return used(true);
        }
        }
        return false;
    }
    case Action::Assign:
        if (arg >= 0 && !it->usable()) return refuse("This item can't go on a button.");
        if (arg < 0) {
            for (unsigned btn = 0; btn < kButtons; btn++) b.set_button(key, btn, false);
            return true;
        }
        if (unsigned(arg) >= kButtons) return refuse("No such button.");
        if (!b.bind(key)) return refuse("Too many items are in use: take one off a button or out of the game's places first.");
        return b.set_button(key, unsigned(arg), true) ? true : refuse("The save file has no room left for this.");
    case Action::MoveToGamePlace: {
        if (arg < 0 || unsigned(arg) >= kGamePlaces) return refuse("No such place.");
        const uint8_t to = uint8_t(arg + 1);
        if (b.at(to) && b.at(to) != key) return refuse("That place is taken.");
        if (game_reserve(unsigned(arg)) != 0xFF) return refuse("That place is taken.");
        if (!b.bind(key)) return refuse("Too many items are in use: take one off a button or out of the game's places first.");
        return b.move(key, to, true) ? true : refuse("The save file has no room left for this.");
    }
    case Action::MoveToPage: {
        const uint8_t to = arg > 0 ? uint8_t(arg) : b.free_page_place();
        if (!page_place(to)) return refuse("The bag's pages are full.");
        return b.move(key, to, false) ? true : refuse("That place is taken.");
    }
    }
    return false;
}

void set_presenter(ShowInfo show) { g_show = show; }
void set_use_calls(bool (*open_notebook)(const char*), int32_t (*reach)(const std::string&, const std::string&, uint32_t)) {
    g_open_notebook = open_notebook, g_reach = reach;
}

bool present(Cpu* c) {
    Pending p;
    {
        std::lock_guard lock(g_mu);
        if (g_pending.empty() || !g_show) return false;
        p = g_pending.front();
    }
    if (!g_show(p.title, p.text, "an item get")) return false;  // shown later (the page or a caption is open)
    {
        std::lock_guard lock(g_mu);
        if (!g_pending.empty()) g_pending.pop_front();
    }
    if (c && g_call) {
        Cpu call = *c;
        g_call(&call, kSeStartSystem, {kJingle});
    }
    return true;
}

void note_import() { g_imported = true; }
bool imported() { return g_imported; }
bool installed_hooks() { return g_installed; }
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original, g_call = call;
}

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original) return;
    // execItemGet is the letter service's hook (story_mail.cpp): it hands the mod items' numbers here
    // (story_mail::set_item_get_next); checkItemGet is ours
    const std::pair<uint32_t, void (*)(Cpu*)> table[] = {{kCheckItemGet, hooks::check_item_get}};
    for (const auto& [addr, fn] : table) {
        const std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[items] %s replaces %08X: mod items are not used this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : table) replace(addr, fn);
    g_installed = true;
    LOG("[items] the game's item tables answer for the port's item numbers");
}

void reset() {
    std::lock_guard lock(g_mu);
    g_items.clear(), g_uses.clear(), g_pending.clear(), g_reason.clear();
    g_prop_markers = prop_rewards::Registry{};
}

namespace hooks {

// execItemGet(itemNo): a pool number gives Link its bound item; other numbers are the game's
void exec_item_get(Cpu* c) {
    const uint32_t n = c->r[3];
    if (!pool_number(n)) return g_original(c, kExecItemGet);
    if (const std::optional<Item> it = by_number(n)) give(it->mod, it->id, 1, "the game's item get");
    else LOG("[items] the game gets item %02X, a number of the port's pool bound to no item: nothing happens", n);
}

// checkItemGet(itemNo, default): for a pool number, whether Link has its bound item
void check_item_get(Cpu* c) {
    const uint32_t n = c->r[3];
    if (!pool_number(n)) return g_original(c, kCheckItemGet);
    const std::optional<Item> it = by_number(n);
    c->r[3] = it && count(it->key) ? 1 : 0;
}

}  // namespace hooks
}  // namespace story_items
