// SPDX-License-Identifier: MPL-2.0
// Compile journal (compile_journal.h). From Nycolazs/WindWakerHD-Android-Plus fc5ddee via
// GreenNaugahyde/ZeldaWWHDRecompAndroid v0.9.9 (db65d60, runtime/src/vk/vk_draw.cpp), MPL-2.0.
#include "compile_journal.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gfxvk::compile_journal {
namespace {
struct Entry {
    uint32_t type, crashed;
    uint64_t id;
};
static_assert(sizeof(Entry) == 16);
// one slot of compiling.bin (async-signal-safe: one pwrite)
#ifdef _WIN32
inline void put_slot(int, const Entry&, int) {}
#else
inline void put_slot(int fd, const Entry& e, int slot) { (void)!pwrite(fd, &e, sizeof e, off_t(slot) * off_t(sizeof e)); }
#endif
constexpr int kSlots = 256;
std::atomic<int> g_fd{-1};
std::atomic<int> g_next{0};
thread_local int t_slot = -1;
thread_local Entry t_entry{};
std::shared_mutex g_mutex;
std::unordered_set<uint64_t> g_bad;  // under g_mutex
std::atomic<bool> g_any{false};
uint64_t key_of(uint32_t type, uint64_t id) { return id ^ uint64_t(type) * 0x9E3779B97F4A7C15ull; }
std::atomic<Logger> g_logger{nullptr};
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char line[256];
    va_list a;
    va_start(a, fmt);
    vsnprintf(line, sizeof line, fmt, a);
    va_end(a);
    if (Logger l = g_logger.load()) l(line);
    else fprintf(stderr, "%s\n", line);
}
const char* name(uint32_t type) {
    return type == kModule ? "shader module" : type == kGlslang ? "shader translation (glslang)" : "pipeline";
}
}  // namespace

void set_logger(Logger logger) { g_logger.store(logger); }

uint64_t hash_bytes(const void* data, size_t size, uint64_t seed) {
    uint64_t h = 14695981039346656037ull ^ seed;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h ? h : 1;
}

size_t open(const std::string& dir, const std::string& key) {
#ifdef _WIN32
    (void)dir, (void)key;  // POSIX files and signal handler only (Windows: no journal)
    return 0;
#else
    if (g_fd.load() >= 0) return g_bad.size();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    const std::string cur = dir + "/compiling.bin", failed = dir + "/compile_failed.bin", keyPath = dir + "/compile_failed.key";
    {
        std::string old;
        if (FILE* f = fopen(keyPath.c_str(), "rb")) {
            char b[512];
            const size_t n = fread(b, 1, sizeof b, f);
            old.assign(b, n);
            fclose(f);
        }
        if (old != key) {
            if (remove(failed.c_str()) == 0)
                say("[vulkan] compile journal: another GPU, driver or build: the shaders and pipelines left out are tried again");
            if (FILE* f = fopen(keyPath.c_str(), "wb")) {
                fwrite(key.data(), 1, key.size(), f);
                fclose(f);
            }
        }
    }
    if (FILE* f = fopen(cur.c_str(), "rb")) {
        Entry e;
        FILE* out = nullptr;
        while (fread(&e, sizeof e, 1, f) == 1) {
            if (!e.crashed || !e.type) continue;
            if (!out) out = fopen(failed.c_str(), "ab");
            if (out) fwrite(&e, sizeof e, 1, out);
            say("[vulkan] compile journal: the last run crashed while compiling %s %016llx: it is left out from now on",
                name(e.type), (unsigned long long)e.id);
        }
        if (out) fclose(out);
        fclose(f);
    }
    size_t count = 0;
    {
        std::unique_lock lock(g_mutex);
        g_bad.clear();
        if (FILE* f = fopen(failed.c_str(), "rb")) {
            Entry e;
            while (fread(&e, sizeof e, 1, f) == 1)
                if (e.type) g_bad.insert(key_of(e.type, e.id));
            fclose(f);
        }
        count = g_bad.size();
        g_any.store(count > 0, std::memory_order_release);
    }
    if (count) say("[vulkan] compile journal: %zu shaders / pipelines left out (they crashed while compiling)", count);
    g_fd.store(::open(cur.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
    return count;
#endif
}

void close() {
    const int fd = g_fd.exchange(-1);
#ifndef _WIN32
    if (fd >= 0) ::close(fd);
#else
    (void)fd;
#endif
    std::unique_lock lock(g_mutex);
    g_bad.clear();
    g_any.store(false);
    g_next.store(0);
}

bool active() { return g_fd.load(std::memory_order_relaxed) >= 0; }

bool left_out(Type type, uint64_t id) {
    if (!g_any.load(std::memory_order_acquire)) return false;
    std::shared_lock lock(g_mutex);
    return g_bad.count(key_of(type, id)) != 0;
}

void begin(Type type, uint64_t id) {
    const int fd = g_fd.load(std::memory_order_relaxed);
    if (fd < 0) return;
    if (t_slot < 0) t_slot = g_next.fetch_add(1);
    if (t_slot >= kSlots) return;
    t_entry = {type, 0, id};
    put_slot(fd, t_entry, t_slot);
    static const char* test = getenv("WWHD_TEST_COMPILE_CRASH");
    if (test) {
        unsigned tt = 0;
        unsigned long long tid = 0;
        if (sscanf(test, "%u:%llx", &tt, &tid) == 2 && tt == type && (tid == id || tid == 0)) {
            say("[vulkan] compile journal: test crash while compiling %s %016llx (WWHD_TEST_COMPILE_CRASH)", name(type),
                (unsigned long long)id);
            abort();
        }
    }
}

void end() {
    const int fd = g_fd.load(std::memory_order_relaxed);
    if (fd < 0 || t_slot < 0 || t_slot >= kSlots || !t_entry.type) return;
    t_entry = {};
    put_slot(fd, t_entry, t_slot);
}

}  // namespace gfxvk::compile_journal

extern "C" void wwhd_compile_journal_on_crash() {
    using namespace gfxvk::compile_journal;
    const int fd = g_fd.load(std::memory_order_relaxed);
    if (fd < 0 || t_slot < 0 || t_slot >= kSlots || !t_entry.type) return;
    Entry e = t_entry;
    e.crashed = 1;
    put_slot(fd, e, t_slot);
}
