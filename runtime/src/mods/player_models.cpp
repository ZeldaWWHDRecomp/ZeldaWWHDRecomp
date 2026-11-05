/* SPDX-License-Identifier: MPL-2.0
 * GameCube player models per save file, live (player_models.h). From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60
 * (v0.9.9, MPL-2.0), runtime/src/mods/player_models.cpp; the memory work is player_models_core.h.
 */
#include "player_models.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "../gcmodel/gcm_io.h"
#include "../runtime.h"
#include "../savestate.h"
#include "game_progress_host.h"
#include "player_models_core.h"

namespace player_models {
namespace {

constexpr uint32_t kDataNum = 0x1290;  // dSv_info_c::mDataNum from the save data: the file played, 0..2
constexpr uint32_t kEvents = 0x624;    // the save's event bits (game_progress.h GP_EVENTS)

const Memory kGuest = [](uint32_t a) { return mem::ptr(a); };

struct Set {
    Bytes game;                   // the game's Link.bfres
    std::vector<Buffer> buffers;  // its vertex/index buffers (the game sets their data pointers at load)
    std::string basePack;         // its pack's stamp
    std::vector<std::shared_ptr<Model>> models;
};

// a file's path, size and time (made again: read again)
std::string stamp(const std::string& p) {
    struct stat st {};
    if (stat(p.c_str(), &st) != 0) return p;
    return p + "\t" + std::to_string((long long)st.st_size) + "\t" + std::to_string((long long)st.st_mtime);
}

bool read_file(const std::string& p, Bytes& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

// Link.bfres out of a Link.szs
bool bfres_of_szs(const Bytes& szs, Bytes& out) {
    try {
        auto files = gcmodel::sarc_read(gcmodel::yaz0_decode(szs));
        auto it = files.find("Link.bfres");
        if (it == files.end()) return false;
        out = std::move(it->second);
        return out.size() > 0x40 && memcmp(out.data(), "FRES", 4) == 0;
    } catch (const std::exception&) {
        return false;
    }
}

bool game_bfres(const std::string& pack, Bytes& out) {
    Bytes p;
    if (!read_file(pack, p)) return false;
    try {
        auto files = gcmodel::sarc_read(p);
        p.clear();
        p.shrink_to_fit();
        auto it = files.find("Link.szs");
        return it != files.end() && bfres_of_szs(it->second, out);
    } catch (const std::exception&) {
        return false;
    }
}

// what the player chose
std::mutex g_mu;
std::string g_basePack;  // the pack the game opened (note_open)
std::array<Choice, kFiles> g_files;
std::function<std::string(const std::string&)> g_resolve;
std::function<void(int)> g_newGame;
std::atomic<uint32_t> g_gen{0};
std::atomic<bool> g_any{false};  // a file has a model

// what the loader made; the main thread takes it
std::shared_ptr<Set> g_set;      // main thread's
std::shared_ptr<Set> g_pending;  // loader -> main thread (under g_mu)
std::atomic<int> g_loading{0};   // 1 while the loader runs
std::atomic<uint32_t> g_areaUsed{0};
std::string g_current;  // main thread
std::mutex g_currentMu;

// Link.bfres in memory, found by its header (the game's first 64 bytes; the game doesn't write there at load)
std::atomic<uint32_t> g_base{0};
std::atomic<int> g_scan{0};  // 0 to do, 1 running, 2 found, 3 not found

struct Want {
    std::string id, szs;
};

void loader(uint32_t gen, std::string basePack, std::vector<Want> wants, std::shared_ptr<Set> had) {
    auto set = std::make_shared<Set>();
    // (the game's file again only when its pack changed)
    set->basePack = stamp(basePack);
    if (had && had->basePack == set->basePack && !had->game.empty()) set->game = had->game;
    else if (!game_bfres(basePack, set->game)) LOG("[playermodel] cannot read Link.bfres of %s", basePack.c_str());
    if (!set->game.empty()) {
        Layout gl = layout(set->game);
        set->buffers = gl.buffers;
        for (const Want& w : wants) {
            std::shared_ptr<Model> m;
            const std::string key = stamp(w.szs);
            if (had && had->basePack == set->basePack)
                for (auto& h : had->models)
                    if (h->id == w.id && h->key == key) m = h;
            if (!m) {
                Bytes szs, b;
                if (w.szs.empty() || !read_file(w.szs, szs) || !bfres_of_szs(szs, b)) {
                    // its file gone (the package removed while the model is shown): the one made before stays, so
                    // its own words can take it out
                    if (had && had->basePack == set->basePack)
                        for (auto& h : had->models)
                            if (h->id == w.id) m = h;
                    if (m) {
                        LOG("[playermodel] %s: its file is gone; kept until it is taken out", w.id.c_str());
                        set->models.push_back(m);
                        continue;
                    }
                    LOG("[playermodel] %s: no converted model at %s", w.id.c_str(), w.szs.c_str());
                    continue;
                }
                std::string why;
                m = make_model(w.id, set->game, gl, b, why);
                if (!m) {
                    LOG("[playermodel] %s: %s", w.id.c_str(), why.c_str());
                    continue;
                }
                m->key = key;
                LOG("[playermodel] %s: %zu runs (%zu KiB), %zu offsets/pointers into its %zu KiB, %zu probes", w.id.c_str(), m->runs.size(),
                    m->bytes.size() / 1024, m->fixes.size(), m->tail.size() / 1024, m->probes.size());
            }
            set->models.push_back(m);
        }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    if (gen == g_gen.load()) g_pending = set;
    g_loading = 0;
}

void scan(Bytes head) {
    uint32_t found = 0;
    for (uint32_t a = mem::kMem2Start; a + head.size() <= mem::kMem2End; a += kStep) {
        const uint8_t* p = mem::ptr(a);
        if (p[0] != 'F' || memcmp(p, head.data(), head.size()) != 0) continue;
        found = a;
        break;
    }
    if (found) LOG("[playermodel] Link.bfres at %08X", found);
    g_base = found;
    g_scan.store(found ? 2 : 3, std::memory_order_release);
}

void set_current(const std::string& id) {
    std::lock_guard<std::mutex> lk(g_currentMu);
    g_current = id;
}

// WWHD_TEST_PLAYER_MODEL=<Link.szs>: every save file shows that model (headless checks)
void test_choice() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* e = getenv("WWHD_TEST_PLAYER_MODEL");
    if (!e || !*e) return;
    std::array<Choice, kFiles> c;
    for (auto& f : c) f = {"test-model", e};
    set_choices(c);
    LOG("[playermodel] test: every save file shows %s", e);
}

}  // namespace

void set_choices(const std::array<Choice, kFiles>& files) {
    std::lock_guard<std::mutex> lk(g_mu);
    bool any = false;
    for (int f = 0; f < kFiles; f++) {
        g_files[f] = files[f].id.empty() ? Choice{} : files[f];
        any |= !g_files[f].id.empty();
    }
    g_any = any;
    g_gen++;
}

void set_resolver(std::function<std::string(const std::string&)> resolve) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_resolve = std::move(resolve);
}

void set_new_game_listener(std::function<void(int)> listener) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_newGame = std::move(listener);
}

void note_open(const std::string& guest, const std::string& host) {
    static const char kPack[] = "common/pack/permanent_3d.pack";
    const size_t n = sizeof kPack - 1;
    if (guest.size() < n) return;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)guest[guest.size() - n + i]) != kPack[i]) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_basePack == host) return;
    g_basePack = host;
    g_gen++;
}

uint32_t area(uint32_t* used) {
    *used = g_areaUsed.load();
    return kArea;
}

std::string current() {
    std::lock_guard<std::mutex> lk(g_currentMu);
    return g_current;
}

void service() {
    static uint32_t gen = 0;
    static uint64_t loads = ~0ull;
    static int frame = 0, lastScan = -100000;
    static int current = -2;   // -2 not known, -1 the game's Link, -3 a model that isn't installed, else g_set->models' index
    static std::string extra;  // a model a save state brought that no file has (to take it out again)
    static bool newGame[kFiles] = {};
    test_choice();
    frame++;
    // a save state brought a model in (the area's note): known only while a set is made
    const uint64_t lc = ss::last_load_frame() ^ (ss::last_arrival_frame() << 32);
    const bool loaded = lc != loads;
    if (loaded && !g_set && !g_any.load(std::memory_order_relaxed)) {
        loads = lc;
        const std::string n = noted(kGuest);
        if (!n.empty() && extra != n) {
            LOG("[playermodel] a save state brought %s: loading it", n.c_str());
            extra = n;
            g_gen++;
        }
    }
    // the choice changed: models made (on a thread; those made before are kept)
    uint32_t g = g_gen.load();
    if (g != gen && !g_loading.load()) {
        std::vector<Want> wants;
        std::string basePack;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            basePack = g_basePack;
            auto add = [&](const std::string& id, const std::string& szs) {
                if (id.empty()) return;
                for (auto& w : wants)
                    if (w.id == id) return;
                wants.push_back({id, szs});
            };
            for (auto& f : g_files) add(f.id, f.szs);
            // (the model in memory stays loaded until it's taken out; one only a save state had, too)
            for (const std::string& keep : {g_set && current >= 0 && current < (int)g_set->models.size() ? g_set->models[current]->id : std::string(), extra}) {
                if (keep.empty()) continue;
                std::string szs;
                if (g_set)
                    for (auto& m : g_set->models)
                        if (m->id == keep) szs = m->key.substr(0, m->key.find('\t'));
                if (szs.empty() && g_resolve) szs = g_resolve(keep);
                add(keep, szs);
            }
        }
        // nothing chosen and nothing in: nothing to do (the pack not opened yet: later)
        if (basePack.empty() || (wants.empty() && current < 0 && !g_set)) return;
        gen = g;
        g_loading = 1;
        std::thread(loader, g, basePack, wants, g_set).detach();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_pending) {
            // the model in memory, if any, stays known under the new set (the same one: same id and file)
            std::shared_ptr<Model> was = g_set && current >= 0 && current < (int)g_set->models.size() ? g_set->models[current] : nullptr;
            g_set = std::move(g_pending);
            int now = -2;
            if (current == -1) now = -1;
            for (size_t i = 0; i < g_set->models.size() && was; i++) {
                if (g_set->models[i]->id != was->id) continue;
                if (g_set->models[i] == was) {
                    now = (int)i;
                } else if (g_scan.load() == 2 && g_base.load()) {
                    // converted again while shown: the old one out first (its words, not the new one's)
                    write_out(kGuest, g_base.load(), *was);
                    g_areaUsed = 0;
                    set_current("");
                    LOG("[playermodel] %s was made again: the old one taken out", was->id.c_str());
                    now = -1;
                }
            }
            if (was && now == -2) LOG("[playermodel] %s in memory but no longer loaded", was->id.c_str());
            current = now;
        }
    }
    if (!g_set || g_set->game.empty()) return;
    // Link.bfres in memory (loaded with the game's first files; a save state keeps it where it was)
    const uint32_t base = g_base.load();
    int s = g_scan.load(std::memory_order_acquire);
    if (s == 1) return;
    if (s == 2 && memcmp(mem::ptr(base), g_set->game.data(), 64) != 0) s = 0;
    if (s != 2) {
        if (frame - lastScan < 60) return;
        lastScan = frame;
        g_scan = 1;
        std::thread(scan, Bytes(g_set->game.begin(), g_set->game.begin() + 64)).detach();
        current = -2;
        return;
    }
    // the game done loading it: its data pointers set (unpacked but not yet set up: the game would write over a model)
    if (g_set->buffers.empty()) return;
    for (const Buffer* b : {&g_set->buffers.front(), &g_set->buffers.back()})
        if (be32(mem::ptr(base + b->at)) == 0) return;
    // after a save state: whatever it brought
    if (lc != loads) {
        loads = lc;
        current = -2;
    }
    auto& models = g_set->models;
    if (current == -2) {
        // the area's note first, then the game's own, then any model loaded
        const std::string n = noted(kGuest);
        int found = -2;
        for (size_t i = 0; i < models.size() && found == -2; i++)
            if (models[i]->id == n && holds(kGuest, base, *models[i], true)) found = (int)i;
        const bool known = n.empty() || std::any_of(models.begin(), models.end(), [&](auto& m) { return m->id == n; });
        if (found == -2 && known && std::all_of(models.begin(), models.end(), [&](auto& m) { return holds(kGuest, base, *m, false); })) found = -1;
        for (size_t i = 0; i < models.size() && found == -2; i++)
            if (holds(kGuest, base, *models[i], true)) found = (int)i;
        if (found == -2 && !n.empty() && extra != n) {
            LOG("[playermodel] memory holds %s: loading it", n.c_str());
            extra = n;
            g_gen++;
            return;
        }
        if (found == -2 && !known) {
            // (a model no longer installed, from a save state: only its own words could take it out, so it stays
            // as the state had it, and in the next states, until the game loads Link again)
            LOG("[playermodel] memory holds %s, which isn't installed: left as the save state had it", n.c_str());
            current = -3;
            g_areaUsed = kHeader + std::min(be32(mem::ptr(kArea + 4)), kAreaSize - kHeader);
            set_current(n);
            return;
        }
        if (found == -2) {
            LOG("[playermodel] what Link.bfres holds is not known: taken as the game's");
            found = -1;
        }
        current = found;
        g_areaUsed = current >= 0 ? kHeader + (uint32_t)models[current]->tail.size() : 0;
        set_current(current >= 0 ? models[current]->id : "");
        LOG("[playermodel] in memory: %s", current >= 0 ? models[current]->id.c_str() : "the game's Link");
    }
    if (current == -3) return;  // (a model that isn't installed: left in memory, above)
    // the save file played decides
    const uint32_t save = game_progress::save_address();
    if (!save) return;
    const int file = *(const uint8_t*)mem::ptr(save + kDataNum);
    if (file < 0 || file >= kFiles) return;
    std::string want;
    std::function<void(int)> listener;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        want = g_files[file].id;
        listener = g_newGame;
    }
    // (a new game in that file: the game's Link; the choice is forgotten)
    const bool fresh = new_game((const uint8_t*)mem::ptr(save + kEvents));
    if (fresh && !want.empty()) {
        want.clear();
        if (!newGame[file]) {
            newGame[file] = true;
            LOG("[playermodel] save file %d: a new game, HD Link", file + 1);
            if (listener) listener(file);
        }
    } else if (!fresh) {
        newGame[file] = false;
    }
    int target = -1;
    if (!want.empty()) {
        target = -2;
        for (size_t i = 0; i < models.size(); i++)
            if (models[i]->id == want) target = (int)i;
        if (target == -2) {
            if (g_loading.load() || g_gen.load() != gen) return;  // (still being made)
            target = -1;  // (it couldn't be made: the log says why)
        }
    }
    if (target == current) return;
    if (current >= 0) write_out(kGuest, base, *models[current]);
    if (target >= 0) write_in(kGuest, base, *models[target]);
    g_areaUsed = target >= 0 ? kHeader + (uint32_t)models[target]->tail.size() : 0;
    LOG("[playermodel] save file %d: %s (was %s)", file + 1, target >= 0 ? models[target]->id.c_str() : "the game's Link",
        current >= 0 ? models[current]->id.c_str() : "the game's Link");
    current = target;
    set_current(target >= 0 ? models[target]->id : "");
    // (a model only a save state had: let go once out)
    if (!extra.empty() && target < 0) extra.clear();
}

}  // namespace player_models
