// Story progress with the game's own save: see story_store.h.
#include "story_store.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include "guest_addr.h"
#include "runtime.h"

namespace story_store {
namespace {

using story_block::kAreaSize;
using story_block::kQuestLogs;

const uint32_t kSavePtr = GD(0x101F84DC);        // save base pointer (d_com_inf_game)
const uint32_t kCardToMemory = GC(0x025BA7B0);   // dSv_info_c::card_to_memory(card, dataNum)
const uint32_t kMemoryToCard = GC(0x025BA9FC);   // dSv_info_c::memory_to_card(card, dataNum)
const uint32_t kInitdataToCard = GC(0x025BAC50); // dSv_info_c::initdata_to_card(card, dataNum)
constexpr uint32_t kReserve = 0x744;             // dSv_reserve_c in the save (0x50 bytes)

std::mutex g_mu;
uint32_t g_memory = 0;               // guest: [0] the loaded Quest Log's area, [1..3] the saved copies
std::vector<uint8_t> g_unplaced;     // a block read before set_memory (payload bytes)
std::atomic<int> g_quiet{0};
bool g_installed = false;
void (*g_original)(Cpu*, uint32_t) = nullptr;
void (*g_progress_loaded)() = nullptr;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_STORY_STORE_TRACE"); return e && *e == '1'; }();
    return on;
}
uint8_t* saved_copy(unsigned log) { return mem::ptr(g_memory + kAreaSize * (1 + log)); }
std::string base_name(const std::string& p) {
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}
bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// game hooks (the arguments are the game's: r3 this, r4 card, r5 dataNum)
void card_to_memory(Cpu* c) {
    const uint32_t log = c->r[5];
    g_original(c, kCardToMemory);
    if (g_quiet || !g_memory || log >= kQuestLogs) return;
    {
        std::lock_guard lock(g_mu);
        memcpy(area(), saved_copy(log), kAreaSize);
        const uint32_t base = ld32(kSavePtr);
        if (base && story_block::migrate(mem::ptr(base + kReserve), area()))
            LOG("[story] Quest Log %u: story progress moved from the game's reserved save bytes into the story block", log + 1);
        if (trace()) LOG("[story] Quest Log %u loaded (%u records)", log + 1, story_mail::Book(area(), story_block::kAreaRecords).records() -
                                                                              story_mail::Book(area(), story_block::kAreaRecords).free_records());
    }
    if (g_progress_loaded) g_progress_loaded();
}
void memory_to_card(Cpu* c) {
    const uint32_t log = c->r[5];
    if (!g_quiet && g_memory && log < kQuestLogs) {
        std::lock_guard lock(g_mu);
        memcpy(saved_copy(log), area(), kAreaSize);
        if (trace()) LOG("[story] Quest Log %u saved", log + 1);
    }
    g_original(c, kMemoryToCard);
}
// initdata_to_card builds a new Quest Log's initial data in its own 0x420-byte stack frame (stwu r1,-0x420)
// and copies the frame's records to the card buffer whole, padding included (dSv_player_status_c_c's
// status A byte 0x17, once per status C record, and the frame's other gaps). Those bytes are whatever an
// earlier call left on the stack, so the empty Quest Logs' bytes in cking.sav followed which code ran on
// the save thread before: the port's mods (their guest calls) changed them. The frame is cleared first,
// so a new Quest Log's bytes are the game's initial values and zeros, whatever ran before.
constexpr uint32_t kInitdataFrame = 0x420;
void clear_initdata_frame(const Cpu* c) {
    const uint32_t sp = c->r[1];
    if (sp > kInitdataFrame) memset(mem::ptr(sp - kInitdataFrame), 0, kInitdataFrame);
}
void initdata_to_card(Cpu* c) {
    const uint32_t log = c->r[5];
    clear_initdata_frame(c);
    g_original(c, kInitdataToCard);
    if (!g_quiet && g_memory && log < kQuestLogs) {
        std::lock_guard lock(g_mu);
        memset(saved_copy(log), 0, kAreaSize);
        if (trace()) LOG("[story] Quest Log %u is new", log + 1);
    }
}
}  // namespace

void set_memory(uint32_t guest) {
    std::lock_guard lock(g_mu);
    g_memory = guest;
    if (trace()) LOG("[story] story areas at %08X", guest);
    if (g_memory && g_unplaced.size() == story_block::kPayloadSize) {
        memcpy(saved_copy(0), g_unplaced.data(), story_block::kPayloadSize);
        g_unplaced.clear();
    }
}
bool ready() { return g_memory != 0; }
uint8_t* memory_block() { return g_memory ? mem::ptr(g_memory) : nullptr; }
uint8_t* area() { return g_memory ? mem::ptr(g_memory) : nullptr; }
story_mail::Book book() { return story_mail::Book(area(), story_block::kAreaRecords); }

bool is_save_file(const std::string& guest_path) {
    if (guest_path.rfind("/vol/save/", 0) != 0) return false;
    const std::string name = base_name(guest_path);
    return name == "cking.sav" || name == "cking.sav.backup";
}

uint64_t opened_for_read(const std::string& guest_path, const std::string& host_path) {
    std::vector<uint8_t> file;
    if (!read_file(host_path, file)) return UINT64_MAX;
    const story_block::Split s = story_block::split(file.data(), file.size());
    if (trace()) LOG("[story] %s opened for reading: %zu bytes, the game's part %zu, block %s", guest_path.c_str(), file.size(),
                     s.game_size, s.found == story_block::Found::Valid ? "valid" : s.found == story_block::Found::Damaged ? "damaged" : "none");
    if (s.found == story_block::Found::Damaged) {
        const std::string aside = host_path + ".story-damaged";
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::u8path(aside), ec)) {
            std::filesystem::copy_file(std::filesystem::u8path(host_path), std::filesystem::u8path(aside), ec);
            LOG("[story] %s: the story block is damaged (checksum); it is not used. The file was copied to %s",
                base_name(guest_path).c_str(), base_name(aside).c_str());
        } else {
            LOG("[story] %s: the story block is damaged (checksum); it is not used", base_name(guest_path).c_str());
        }
    }
    if (base_name(guest_path) == "cking.sav" && s.found == story_block::Found::Valid) {
        std::lock_guard lock(g_mu);
        if (g_memory) memcpy(saved_copy(0), s.payload, story_block::kPayloadSize);
        else g_unplaced.assign(s.payload, s.payload + story_block::kPayloadSize);
    }
    return s.game_size;
}

uint64_t game_size(const std::string& host_path, uint64_t file_size) {
    if (file_size < story_block::kFooterSize) return file_size;
    std::ifstream f(std::filesystem::u8path(host_path), std::ios::binary);
    uint8_t footer[story_block::kFooterSize];
    if (!f.seekg(std::streamoff(file_size - story_block::kFooterSize)) || !f.read(reinterpret_cast<char*>(footer), sizeof footer))
        return file_size;
    // the footer alone decides where the game's part ends (story_block::split)
    const uint32_t length = story_block::be32(footer + 4);
    if (memcmp(footer, story_block::kFooterMagic, 4) != 0 || length > file_size - story_block::kFooterSize) return file_size;
    return file_size - story_block::kFooterSize - length;
}

void written(const std::string& guest_path, const std::string& host_path) {
    std::vector<uint8_t> block;
    {
        std::lock_guard lock(g_mu);
        if (g_memory) {
            if (!story_block::all_zero(saved_copy(0), story_block::kPayloadSize)) block = story_block::build(saved_copy(0));
        } else if (!g_unplaced.empty()) {
            block = story_block::build(g_unplaced.data());
        }
    }
    std::vector<uint8_t> file;
    if (!read_file(host_path, file)) return;
    const story_block::Split s = story_block::split(file.data(), file.size());
    if (block.empty() && s.found == story_block::Found::None) {
        if (trace()) LOG("[story] %s written by the game: no story block", guest_path.c_str());
        return;  // nothing of ours: the game's file as the game wrote it
    }
    if (s.found == story_block::Found::Damaged) LOG("[story] %s: replacing a damaged story block", base_name(guest_path).c_str());
    file.resize(s.game_size);
    file.insert(file.end(), block.begin(), block.end());
    const std::string temp = host_path + ".story-tmp";
    {
        std::ofstream out(std::filesystem::u8path(temp), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        out.flush();
        if (!out) {
            LOG("[story] %s: the story block could not be written (temporary file)", base_name(guest_path).c_str());
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(temp), ec);
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(temp), std::filesystem::u8path(host_path), ec);
    if (ec) {
        LOG("[story] %s: the story block could not be written (%s)", base_name(guest_path).c_str(), ec.message().c_str());
        std::filesystem::remove(std::filesystem::u8path(temp), ec);
        return;
    }
    if (trace()) LOG("[story] %s written by the game: the game's part %zu bytes, story block %zu bytes", guest_path.c_str(), s.game_size,
                     block.size());
}

void set_game_calls(void (*original)(Cpu*, uint32_t)) { g_original = original; }
void set_progress_loaded(void (*callback)()) { g_progress_loaded = callback; }

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_installed || !g_original || !g_memory) return;
    const std::pair<uint32_t, void (*)(Cpu*)> hooks[] = {
        {kCardToMemory, card_to_memory}, {kMemoryToCard, memory_to_card}, {kInitdataToCard, initdata_to_card}};
    for (const auto& [addr, fn] : hooks) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[story] %s replaces %08X: story progress is not saved this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : hooks) replace(addr, fn);
    g_installed = true;
    LOG("[story] story progress is saved with the game's saves");
}
bool installed() { return g_installed; }

Quiet::Quiet() { g_quiet++; }
Quiet::~Quiet() { g_quiet--; }

}  // namespace story_store
