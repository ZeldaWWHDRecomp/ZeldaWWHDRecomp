// Save states: snapshot the whole running game into one of 5 slots and restore it, also in a new
// process.
//
// The recompiled game runs on host threads with native call stacks, so a snapshot can only be taken
// (and restored) when every guest thread is at a point whose complete state is guest memory, its Cpu
// registers and the HLE objects:
//   - the game's main thread at the top of the per-frame function (0203593C; interp.cpp calls
//     service() there), which requests the save/load;
//   - every other guest thread parked in an HLE wait (OSReceiveMessage, OSWaitEvent, OSLockMutex,
//     OSSleepThread, OSSleepTicks, GX2WaitForVsync, ...: park_wait in threads.cpp). A parked thread
//     re-checks its condition after waking and only then consumes anything, so the HLE objects
//     decide where it continues;
//   - service threads (alarms, AX frame) idle between their callbacks.
// threads::quiesce() freezes the game in that state (other threads keep running until they park).
//
// Restoring into a running process requires each thread to be parked at the same place it was
// saved at (same stack pointer, return address and guest back chain => same host call stack); the
// main thread always is. Worker threads normally idle at the same wait. If not, the load is retried
// on the following frames and finally refused with a message; nothing is changed then.
//
// Slot file: header (uncompressed) + LZ4 blocks of the payload: tagged sections from each module
// (threads + HLE sync objects + alarms + guest clock, heaps, open files, AX voices, GX2 registers,
// runtime allocations) and guest memory (MEM2, runtime objects, fixed slots, foreground bucket,
// MEM1; all-zero 64 KiB chunks are left out).
#include "savestate.h"

#ifdef __APPLE__
#include <compression.h>
#include <mach-o/ldsyms.h>
#include <mach-o/loader.h>
#else
#include <lz4.h>
#endif
#include "platform/host.h"
#include "gfx/renderer.h"
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "runtime.h"
#include "crashrec.h"
#include "rumble.h"

// module sections
bool threads_ss_save(ss::Writer& w, std::string& why);
bool threads_ss_check(ss::Reader r, std::string& why);
void threads_ss_load(ss::Reader& r);
void threads_ss_targets(ss::Reader r);
void mem_ss_save(ss::Writer& w);
bool mem_ss_check(ss::Reader r, std::string& why);
void mem_ss_load(ss::Reader& r);
void fs_ss_save(ss::Writer& w);
void fs_ss_load(ss::Reader& r);
void ax_ss_save(ss::Writer& w);
bool ax_ss_check(ss::Reader r, std::string& why);
void ax_ss_load(ss::Reader& r);
void gx2_ss_drain();
void gx2_ss_save(ss::Writer& w);
bool gx2_ss_check(ss::Reader r, std::string& why);
void gx2_ss_load(ss::Reader& r);
namespace interp { void ss_reset(); }
namespace aspect { void ss_reset(); }
namespace dispatch { std::vector<std::pair<uint32_t, std::string>> host_functions(); }

namespace interp { uint64_t logic_steps(); }
namespace ss {
namespace {

constexpr char kMagic[8] = {'W', 'W', 'H', 'D', 'S', 'T', 'A', 'T'};
#ifdef __APPLE__
constexpr uint32_t kVersion = 1;
#else
constexpr uint32_t kVersion = 2; // liblz4 raw blocks, not Apple COMPRESSION_LZ4 framing
#endif
constexpr uint32_t kChunk = 0x10000;
constexpr uint32_t kBlock = 8 << 20;  // compression block (raw bytes)

struct Header {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint64_t created;       // unix time
    char area[32];          // stage name
    uint8_t build[16];      // LC_UUID of the executable that wrote it (informational)
    uint64_t game_id;       // hash of cking.rpx
    uint32_t cpu_size;      // sizeof(Cpu)
    uint32_t blocks;        // compressed blocks that follow
    uint64_t raw_size;      // payload bytes
};

enum : uint32_t {
    kSecThreads = 'THRD',
    kSecHeaps = 'HEAP',
    kSecFiles = 'FILE',
    kSecAudio = 'AX  ',
    kSecGx2 = 'GX2 ',
    kSecAllocs = 'RALC',
    kSecDispatch = 'DSPT',
    kSecMemory = 'MEM ',
};

struct Region { uint32_t base, size; };

std::vector<Region> regions() {
    uint32_t top = (mem::runtime_top() + kChunk - 1) & ~(kChunk - 1);
    return {{mem::kMem2Start, mem::kMem2End - mem::kMem2Start},
            {mem::kRuntimeStart, top - mem::kRuntimeStart},
            {mem::kFixedStart, mem::kFixedSize},
            {mem::kFgBucket, mem::kFgBucketSize},
            {mem::kMem1, mem::kMem1Size}};
}

// A snapshot in memory: header + payload (sections); memory chunks point into the payload.
struct Snapshot {
    Header h{};
    std::vector<uint8_t> payload;
    std::map<uint32_t, std::pair<size_t, size_t>> sections;  // tag -> (offset, size)
    std::unordered_map<uint32_t, const uint8_t*> chunks;      // chunk address -> data (absent = zero)
    std::vector<Region> regs;
    int slot = 0;

    Reader section(uint32_t tag) const {
        auto it = sections.find(tag);
        if (it == sections.end()) return Reader(nullptr, 0);
        return Reader(payload.data() + it->second.first, it->second.second);
    }
    bool parse() {
        Reader r(payload.data(), payload.size());
        while (r.ok && !r.at_end()) {
            uint32_t tag = r.u32();
            uint64_t n = r.u64();
            size_t off = r.p - payload.data();
            if (!r.bytes(nullptr, n)) return false;
            sections[tag] = {off, (size_t)n};
        }
        if (!r.ok || !sections.count(kSecMemory)) return false;
        Reader m = section(kSecMemory);
        uint32_t nr = m.u32();
        for (uint32_t i = 0; i < nr && m.ok; i++) {
            Region g{m.u32(), m.u32()};
            regs.push_back(g);
            uint32_t present = m.u32();
            for (uint32_t k = 0; k < present && m.ok; k++) {
                uint32_t idx = m.u32();
                chunks[g.base + idx * kChunk] = m.p;
                if (!m.bytes(nullptr, kChunk)) return false;
            }
        }
        return m.ok;
    }
};

std::mutex g_mu;  // messages, pending requests
std::mutex g_io;  // one slot file operation at a time
std::string g_message;
std::chrono::steady_clock::time_point g_message_time;
std::atomic<int> g_save_req{0};
std::shared_ptr<Snapshot> g_load_ready;   // decompressed, waiting for the frame boundary
std::atomic<bool> g_loading{false};       // a slot file is being read
int g_attempts = 0;
const Snapshot* g_check = nullptr;        // snapshot whose memory snap_ld32 reads

void message(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void message(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    LOG("[savestate] %s", buf);
    std::lock_guard<std::mutex> lk(g_mu);
    g_message = buf;
    g_message_time = std::chrono::steady_clock::now();
}

std::string state_dir() {
    static const std::string dir = [] {
        std::string d;
        if (const char* e = getenv("WWHD_STATE_DIR")) d = e;
        else {
#ifdef __APPLE__
            d=std::string(getenv("HOME")?getenv("HOME"):".")+"/Library/Application Support/wwhd/states";
#else
            d=host::config_dir()+"/states";
#endif
        }
        std::error_code ec; std::filesystem::create_directories(d,ec);
        return d;
    }();
    return dir;
}
bool is_auto(int slot) { return slot > crashrec::kAutoBase && slot <= crashrec::kAutoBase + crashrec::kAutoSlots; }
std::string slot_label(int slot) {
    return is_auto(slot) ? "Automatic state " + std::to_string(slot - crashrec::kAutoBase) : "Slot " + std::to_string(slot);
}
std::string slot_path(int slot, const char* ext = "bin") {
    if (is_auto(slot)) return state_dir() + "/auto/auto" + std::to_string(slot - crashrec::kAutoBase) + "." + ext;  // crashrec.cpp
    return state_dir() + "/slot" + std::to_string(slot) + "." + ext;
}

void build_uuid(uint8_t out[16]) {
    memset(out, 0, 16);
#ifdef __APPLE__
    const auto* h = (const mach_header_64*)&_mh_execute_header;
    const uint8_t* p = (const uint8_t*)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const auto* lc = (const load_command*)p;
        if (lc->cmd == LC_UUID) { memcpy(out, ((const uuid_command*)lc)->uuid, 16); return; }
        p += lc->cmdsize;
    }
#else
    // Hash the complete executable so an incompatible rebuild is never accepted merely because
    // its CPU structure size happens to match. Addresses are ASLR independent.
    uint64_t first=0xcbf29ce484222325ull,second=0x84222325cbf29ce4ull;
    FILE* f=fopen(host::executable_path().c_str(),"rb");
    if(!f) { LOG("[savestate] cannot identify executable; state compatibility is unavailable"); return; }
    unsigned char buf[65536];size_t n;
    while((n=fread(buf,1,sizeof buf,f)))for(size_t i=0;i<n;i++){first=(first^buf[i])*0x100000001b3ull;second=(second+buf[i])*0x100000001b3ull;}
    fclose(f);memcpy(out,&first,8);memcpy(out+8,&second,8);
#endif
}

uint64_t game_id() {
    static const uint64_t id = [] {
        uint64_t h = 0xcbf29ce484222325ull;
        FILE* f = fopen((config::game_dir + "/code/cking.rpx").c_str(), "rb");
        if (!f) return h;
        std::vector<uint8_t> buf(1 << 20);
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
            for (size_t i = 0; i < n; i++) h = (h ^ buf[i]) * 0x100000001b3ull;
        fclose(f);
        return h;
    }();
    return id;
}

// current stage (dComIfG_gameInfo.play: the start stage, 8 chars)
constexpr uint32_t kStageName = 0x1046F0B0 + 0x5134;  // dStage_startStage_c (next stage at +0x5140)
std::string stage_name() {
    if (const char* e = getenv("WWHD_STATE_STAGE_ADDR")) {
        uint32_t a = (uint32_t)strtoul(e, nullptr, 16);
        return std::string((const char*)mem::ptr(a), strnlen((const char*)mem::ptr(a), 8));
    }
    const char* p = (const char*)mem::ptr(kStageName);
    size_t n = strnlen(p, 8);
    for (size_t i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return "";
    return std::string(p, n);
}

// ---------------------------------------------------------------- memory
void capture_memory(Writer& w) {
    auto rs = regions();
    w.u32((uint32_t)rs.size());
    for (auto& g : rs) {
        w.u32(g.base);
        w.u32(g.size);
        size_t count_at = w.b.size();
        w.u32(0);
        uint32_t present = 0;
        for (uint32_t off = 0; off < g.size; off += kChunk) {
            uint8_t* p = mem::ptr(g.base + off);
            // untouched pages are zero: skip them without reading (reading would commit them)
            if (!host::memory_touched(p,kChunk)) continue;
            const uint64_t* q = (const uint64_t*)p;
            bool zero = true;
            for (size_t i = 0; i < kChunk / 8 && zero; i++) zero = q[i] == 0;
            if (zero) continue;
            w.u32(off / kChunk);
            w.bytes(p, kChunk);
            present++;
        }
        memcpy(&w.b[count_at], &present, 4);
    }
}

void restore_memory(const Snapshot& s) {
    for (auto& g : s.regs) {
        for (uint32_t off = 0; off < g.size; off += kChunk) {
            uint32_t a = g.base + off;
            uint8_t* p = mem::ptr(a);
            auto it = s.chunks.find(a);
            if (it != s.chunks.end()) {
                memcpy(p, it->second, kChunk);
                continue;
            }
            if (!host::memory_touched(p,kChunk)) continue;
            const uint64_t* q = (const uint64_t*)p;
            for (size_t i = 0; i < kChunk / 8; i++)
                if (q[i]) { memset(p, 0, kChunk); break; }
        }
    }
}

// ---------------------------------------------------------------- slot files
bool write_slot(int slot, const Header& h0, const std::vector<uint8_t>& payload) {
    Header h = h0;
    size_t nblocks = (payload.size() + kBlock - 1) / kBlock;
    h.blocks = (uint32_t)nblocks;
    h.raw_size = payload.size();
    std::vector<std::vector<uint8_t>> out(nblocks);
    std::vector<std::thread> pool;
    std::atomic<size_t> next{0};
    unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (unsigned t = 0; t < nt; t++)
        pool.emplace_back([&] {
#ifdef __APPLE__
            std::vector<uint8_t> scratch(compression_encode_scratch_buffer_size(COMPRESSION_LZ4));
#endif
            for (size_t i; (i = next++) < nblocks;) {
                size_t raw = std::min<size_t>(kBlock, payload.size() - i * kBlock);
                std::vector<uint8_t>& o = out[i];
                o.resize(8 + raw + raw / 8 + 1024);
#ifdef __APPLE__
                size_t n = compression_encode_buffer(o.data() + 8, o.size() - 8, payload.data() + i * kBlock, raw, scratch.data(), COMPRESSION_LZ4);
#else
                size_t n = (size_t)LZ4_compress_default((const char*)payload.data()+i*kBlock,(char*)o.data()+8,(int)raw,(int)o.size()-8);
#endif
                uint32_t hdr[2] = {(uint32_t)raw, (uint32_t)n};
                if (!n || n >= raw) {  // incompressible: stored
                    memcpy(o.data() + 8, payload.data() + i * kBlock, raw);
                    hdr[1] = 0;
                    n = raw;
                }
                memcpy(o.data(), hdr, 8);
                o.resize(8 + n);
            }
        });
    for (auto& t : pool) t.join();
    std::string tmp = slot_path(slot, "tmp");
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(&h, sizeof h, 1, f) == 1;
    for (auto& o : out) ok = ok && fwrite(o.data(), 1, o.size(), f) == o.size();
    ok = fclose(f) == 0 && ok;
    if (ok) ok = host::replace_file(tmp,slot_path(slot));
    else remove(tmp.c_str());
    return ok;
}

bool read_header(FILE* f, Header& h, std::string& why) {
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, kMagic, 8) != 0) { why = "not a save state"; return false; }
    if (h.version != kVersion || h.header_size != sizeof(Header)) { why = "saved by another version"; return false; }
    if (h.cpu_size != sizeof(Cpu)) { why = "saved by an incompatible build"; return false; }
    if (h.game_id != game_id()) { why = "saved with a different game executable"; return false; }
    return true;
}

std::shared_ptr<Snapshot> read_slot(int slot, std::string& why) {
    FILE* f = fopen(slot_path(slot).c_str(), "rb");
    if (!f) { why = "empty"; return nullptr; }
    auto s = std::make_shared<Snapshot>();
    s->slot = slot;
    if (!read_header(f, s->h, why)) { fclose(f); return nullptr; }
    std::vector<std::vector<uint8_t>> blocks(s->h.blocks);
    std::vector<uint32_t> raws(s->h.blocks), comps(s->h.blocks);
    bool ok = true;
    for (uint32_t i = 0; i < s->h.blocks && ok; i++) {
        uint32_t hdr[2];
        ok = fread(hdr, 8, 1, f) == 1 && hdr[0] <= kBlock;
        if (!ok) break;
        raws[i] = hdr[0];
        comps[i] = hdr[1];
        blocks[i].resize(hdr[1] ? hdr[1] : hdr[0]);
        ok = fread(blocks[i].data(), 1, blocks[i].size(), f) == blocks[i].size();
    }
    fclose(f);
    if (!ok) { why = "file is truncated"; return nullptr; }
    s->payload.resize(s->h.raw_size);
    std::atomic<size_t> next{0};
    std::atomic<bool> bad{false};
    std::vector<std::thread> pool;
    unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (unsigned t = 0; t < nt; t++)
        pool.emplace_back([&] {
#ifdef __APPLE__
            std::vector<uint8_t> scratch(compression_decode_scratch_buffer_size(COMPRESSION_LZ4));
#endif
            for (size_t i; (i = next++) < blocks.size();) {
                uint8_t* dst = s->payload.data() + i * (size_t)kBlock;
                if ((size_t)i * kBlock + raws[i] > s->payload.size()) { bad = true; continue; }
                if (!comps[i]) { memcpy(dst, blocks[i].data(), raws[i]); continue; }
#ifdef __APPLE__
                size_t n = compression_decode_buffer(dst, raws[i], blocks[i].data(), comps[i], scratch.data(), COMPRESSION_LZ4);
#else
                int decoded = LZ4_decompress_safe((const char*)blocks[i].data(),(char*)dst,(int)comps[i],(int)raws[i]);
                size_t n = decoded<0?0:(size_t)decoded;
#endif
                if (n != raws[i]) bad = true;
            }
        });
    for (auto& t : pool) t.join();
    if (bad || !s->parse()) { why = "file is corrupt"; return nullptr; }
    return s;
}

// ---------------------------------------------------------------- save / load at the frame boundary
void put_section(Writer& w, uint32_t tag, const Writer& sec) {
    w.u32(tag);
    w.u64(sec.b.size());
    w.bytes(sec.b.data(), sec.b.size());
}

void save_allocs(Writer& w) {
    auto log = mem::runtime_alloc_log();
    w.u32(mem::runtime_top());
    w.u32((uint32_t)log.size());
    for (auto& a : log) w.pod(a);
}

bool check_allocs(Reader r, std::string& why) {
    uint32_t top = r.u32();
    (void)top;
    uint32_t n = r.u32();
    auto cur = mem::runtime_alloc_log();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        mem::AllocRec a = r.pod<mem::AllocRec>();
        if (i >= cur.size()) break;  // allocated later in the saved session: beyond our top
        const mem::AllocRec& b = cur[i];
        // (the allocating code address, a.tag, changes with every rebuild: only logged)
        if (a.tag != b.tag && i < 4) LOG("[savestate] runtime object #%u allocated by different code (rebuilt?)", i);
        if (a.addr != b.addr || a.size != b.size) {
            char buf[160];
            snprintf(buf, sizeof buf, "runtime objects are laid out differently (#%u: %08X+%X vs %08X+%X)", i, a.addr, a.size, b.addr,
                     b.size);
            why = buf;
            return false;
        }
    }
    return r.ok;
}

void save_dispatch(Writer& w) {
    auto v = dispatch::host_functions();
    w.u32((uint32_t)v.size());
    for (auto& [a, n] : v) {
        w.u32(a);
        w.str(n);
    }
}

bool check_dispatch(Reader r, std::string& why) {
    std::unordered_map<uint32_t, std::string> cur;
    for (auto& [a, n] : dispatch::host_functions()) cur[a] = n;
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t a = r.u32();
        std::string name = r.str();
        auto it = cur.find(a);
        if (it == cur.end() || it->second != name) {
            why = "host function " + name + " is not registered at the same address";
            return false;
        }
    }
    return r.ok;
}

std::string area_label(const char* stage) {
    static const std::map<std::string, std::string> names = {
        {"sea", "Great Sea"}, {"LinkRM", "Link's House"}, {"MajyuE", "Forsaken Fortress"}, {"M_NewD2", "Dragon Roost Cavern"},
        {"kindan", "Forbidden Woods"}, {"Siren", "Tower of the Gods"}, {"Asoko", "Tetra's Ship"},
    };
    auto it = names.find(stage);
    return it == names.end() ? std::string(stage) : it->second;
}

// true when done (saved or failed for good); false to retry on the next frame
bool do_save(int slot) {
    std::string busy, why;
    auto t0 = std::chrono::steady_clock::now();
    if (!threads::quiesce(250, busy, 1, 30)) {
        threads::thaw();
        if (++g_attempts < 30) return false;
        if (!is_auto(slot)) message("Slot %d: not saved (game busy:%s)", slot, busy.c_str());
        return true;
    }
    gx2_ss_drain();
    Writer threads_w;
    if (!threads_ss_save(threads_w, why)) {
        threads::thaw();
        if (++g_attempts < 30) return false;
        if (!is_auto(slot)) message("Slot %d: not saved (%s)", slot, why.c_str());
        return true;
    }
    auto payload = std::make_shared<Writer>();
    payload->b.reserve(512u << 20);
    put_section(*payload, kSecThreads, threads_w);
    Writer w;
    mem_ss_save(w);
    put_section(*payload, kSecHeaps, w);
    w = Writer();
    fs_ss_save(w);
    put_section(*payload, kSecFiles, w);
    w = Writer();
    ax_ss_save(w);
    put_section(*payload, kSecAudio, w);
    w = Writer();
    gx2_ss_save(w);
    put_section(*payload, kSecGx2, w);
    w = Writer();
    save_allocs(w);
    put_section(*payload, kSecAllocs, w);
    w = Writer();
    save_dispatch(w);
    put_section(*payload, kSecDispatch, w);
    // memory last, written straight into the payload
    payload->u32(kSecMemory);
    size_t len_at = payload->b.size();
    payload->u64(0);
    capture_memory(*payload);
    uint64_t mlen = payload->b.size() - len_at - 8;
    memcpy(&payload->b[len_at], &mlen, 8);
    Header h{};
    memcpy(h.magic, kMagic, 8);
    h.version = kVersion;
    h.header_size = sizeof(Header);
    h.created = (uint64_t)time(nullptr);
    std::string stage = stage_name();
    snprintf(h.area, sizeof h.area, "%s", stage.c_str());
    build_uuid(h.build);
    h.game_id = game_id();
    h.cpu_size = sizeof(Cpu);
    threads::thaw();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    LOG("[savestate] slot %d: captured %.1f MB in %.1f ms (stage %s)", slot, payload->b.size() / 1048576.0, ms, stage.c_str());
    if (!is_auto(slot)) render::request_tv_dump(slot_path(slot, "png"), 0);
    else crashrec::on_auto_saved(slot - crashrec::kAutoBase);
    // compress and write in the background; the game continues
    std::thread([slot, h, payload, stage] {
        auto t1 = std::chrono::steady_clock::now();
        bool ok;
        {
            std::lock_guard<std::mutex> io(g_io);
            ok = write_slot(slot, h, payload->b);
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        struct stat st{};
        stat(slot_path(slot).c_str(), &st);
        LOG("[savestate] slot %d: %s (%.1f MB on disk, %.0f ms)", slot, ok ? "written" : "WRITE FAILED", st.st_size / 1048576.0, ms);
        if (is_auto(slot)) return;  // automatic states stay quiet
        std::lock_guard<std::mutex> lk(g_mu);
        g_message = ok ? "Saved to slot " + std::to_string(slot) + (stage.empty() ? "" : " (" + area_label(stage.c_str()) + ")")
                       : "Slot " + std::to_string(slot) + ": write failed";
        g_message_time = std::chrono::steady_clock::now();
    }).detach();
    return true;
}

bool do_load(const std::shared_ptr<Snapshot>& s) {
    std::string busy, why;
    auto t0 = std::chrono::steady_clock::now();
    threads_ss_targets(s->section(kSecThreads));
    if (!threads::quiesce(250, busy, 2, 0)) {
        threads::thaw();
        if (++g_attempts < 30) return false;
        message("%s: not loaded (game busy:%s)", slot_label(s->slot).c_str(), busy.c_str());
        return true;
    }
    gx2_ss_drain();
    g_check = s.get();
    bool ok = check_allocs(s->section(kSecAllocs), why) && check_dispatch(s->section(kSecDispatch), why) &&
              mem_ss_check(s->section(kSecHeaps), why) && ax_ss_check(s->section(kSecAudio), why) &&
              gx2_ss_check(s->section(kSecGx2), why);
    bool layout_ok = ok;
    ok = ok && threads_ss_check(s->section(kSecThreads), why);
    g_check = nullptr;
    if (!ok) {
        threads::thaw();
        // threads at other waits right now may be where they were saved on a later frame
        if (layout_ok && ++g_attempts < 30) {
            if (g_attempts == 1) LOG("[savestate] slot %d: waiting for the game threads (%s)", s->slot, why.c_str());
            return false;
        }
        message("%s: cannot load here (%s)", slot_label(s->slot).c_str(), why.c_str());
        return true;
    }
    restore_memory(*s);
    Reader r = s->section(kSecAllocs);
    mem::raise_runtime_top(r.u32());
    r = s->section(kSecHeaps);
    mem_ss_load(r);
    r = s->section(kSecFiles);
    fs_ss_load(r);
    r = s->section(kSecAudio);
    ax_ss_load(r);
    r = s->section(kSecGx2);
    gx2_ss_load(r);
    interp::ss_reset();
    aspect::ss_reset();
    rumble::reset();     // an effect running before the load is not the restored game's
    r = s->section(kSecThreads);
    threads_ss_load(r);  // last: wakes the parked threads (they continue after the thaw)
    threads::thaw();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::string area = s->h.area[0] ? " (" + area_label(s->h.area) + ")" : "";
    message("Loaded %s%s", is_auto(s->slot) ? slot_label(s->slot).c_str() : ("slot " + std::to_string(s->slot)).c_str(), area.c_str());
    LOG("[savestate] slot %d: restored in %.1f ms", s->slot, ms);
    return true;
}

// test aid: WWHD_STATE_SAVE_AT=frame:slot,...  WWHD_STATE_LOAD_AT=frame:slot,... (TV frames)
struct Timed { uint64_t frame; int slot; };
std::vector<Timed> parse_timed(const char* var) {
    std::vector<Timed> v;
    const char* e = getenv(var);
    unsigned long long f;
    int s, n;
    while (e && sscanf(e, "%llu:%d%n", &f, &s, &n) == 2) {
        v.push_back({f, s});
        e += n;
        if (*e != ',') break;
        e++;
    }
    return v;
}

}  // namespace

uint32_t snap_ld32(uint32_t ea) {
    if (!g_check) return ld32(ea);
    uint32_t base = ea & ~(kChunk - 1);
    auto it = g_check->chunks.find(base);
    if (it == g_check->chunks.end()) {
        for (auto& g : g_check->regs)
            if (ea - g.base < g.size) return 0;
        return ld32(ea);  // not part of the snapshot (code, host-only memory)
    }
    uint32_t v;
    memcpy(&v, it->second + (ea - base), 4);
    return __builtin_bswap32(v);
}

SlotInfo slot_info(int slot) {
    SlotInfo info;
    FILE* f = fopen(slot_path(slot).c_str(), "rb");
    if (!f) return info;
    Header h;
    std::string why;
    info.used = true;
    info.compatible = read_header(f, h, why);
    fclose(f);
    if (info.compatible || memcmp(h.magic, kMagic, 8) == 0) {
        time_t t = (time_t)h.created;
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv,&t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[64];
        strftime(buf, sizeof buf, "%b %d %H:%M:%S", &tmv);
        info.when = buf;
        h.area[sizeof h.area - 1] = 0;
        if (h.area[0]) info.area = area_label(h.area);
    }
    return info;
}

void request_save(int slot) {
    if ((slot < 1 || slot > kSlots) && !is_auto(slot)) return;
    g_save_req = slot;
}

void request_load(int slot) {
    if ((slot < 1 || slot > kSlots) && !is_auto(slot)) return;
    if (g_loading.exchange(true)) return;
    std::thread([slot] {
        auto t0 = std::chrono::steady_clock::now();
        std::string why;
        std::shared_ptr<Snapshot> s;
        {
            std::lock_guard<std::mutex> io(g_io);
            s = read_slot(slot, why);
        }
        if (!s) {
            message("%s: %s", slot_label(slot).c_str(), why.c_str());
            g_loading = false;
            return;
        }
        LOG("[savestate] slot %d: read and decompressed %.1f MB in %.0f ms", slot, s->payload.size() / 1048576.0,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        std::lock_guard<std::mutex> lk(g_mu);
        g_load_ready = s;
    }).detach();
}

std::string last_message() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_message.empty() || std::chrono::steady_clock::now() - g_message_time > std::chrono::seconds(4)) return "";
    return g_message;
}

std::atomic<uint64_t> g_last_load_frame{0}, g_last_load_step{0};
std::atomic<uint32_t> g_last_load_counter{0};
uint32_t last_load_counter() { return g_last_load_counter.load(); }
uint64_t last_load_frame() { return g_last_load_frame.load(); }
uint64_t last_load_step() { return g_last_load_step.load(); }

std::string states_dir() { return state_dir(); }

void service(Cpu* c) {
    (void)c;
    crashrec::service();
    static const std::vector<Timed> save_at = parse_timed("WWHD_STATE_SAVE_AT"), load_at = parse_timed("WWHD_STATE_LOAD_AT");
    if (!save_at.empty() || !load_at.empty()) {
        static uint64_t last = 0;
        uint64_t f = render::frame_count();
        for (auto& t : save_at)
            if (t.frame > last && t.frame <= f) request_save(t.slot);
        for (auto& t : load_at)
            if (t.frame > last && t.frame <= f) request_load(t.slot);
        last = f;
    }
    // test aid: WWHD_STATE_DUMP=n dumps the n TV frames after each save/load (state_<save|load><slot>_<k>.png)
    static const int dump = getenv("WWHD_STATE_DUMP") ? atoi(getenv("WWHD_STATE_DUMP")) : 0;
    if (int slot = g_save_req.load()) {
        if (do_save(slot)) {
            g_save_req = 0;
            g_attempts = 0;
            for (int k = 1; k <= dump; k++)
                render::request_tv_dump("state_save" + std::to_string(slot) + "_" + std::to_string(k) + ".png", k);
        }
        return;
    }
    std::shared_ptr<Snapshot> s;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        s = g_load_ready;
    }
    if (s && do_load(s)) {
        g_last_load_frame = render::frame_count();
        g_last_load_step = interp::logic_steps();
        g_last_load_counter = ld32(0x101FF560);  // g_Counter.mTimer: the game's own step counter, part of the state
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_load_ready.reset();
        }
        g_loading = false;
        g_attempts = 0;
        for (int k = 1; k <= dump; k++)
            render::request_tv_dump("state_load" + std::to_string(s->slot) + "_" + std::to_string(k) + ".png", k);
    }
}

}  // namespace ss
