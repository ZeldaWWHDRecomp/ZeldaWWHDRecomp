// Screenshots (see screenshot.h): requests, file names, the setting, the encoding worker, PNG writing.
#include "screenshot.h"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "input_map.h"
#include "overlay/hostui.h"
#include "platform/host.h"
#include "runtime.h"
#include "savestate.h"

namespace screenshot {
namespace {

std::atomic<int> g_requests{0};
// never destroyed: the detached worker waits on them until the process ends (destroying a condition
// variable with a waiter at exit hangs on glibc)
std::mutex& g_mu = *new std::mutex;  // names, setting, queue, last file
int g_gamepad = -1;  // -1: not read yet
std::string& g_last_file = *new std::string;
std::set<std::string>& g_reserved = *new std::set<std::string>;  // names handed out whose files are not written yet

struct Job {
    std::string path;
    uint32_t w = 0, h = 0;
    size_t stride = 0;
    const uint8_t* pixels = nullptr;
    std::shared_ptr<const void> owner;
    uint64_t frame = 0;
    bool tv = false;
    bool bgra = false;
    std::chrono::steady_clock::time_point queued;
};
std::deque<Job>& g_jobs = *new std::deque<Job>;
std::condition_variable& g_cv = *new std::condition_variable;
std::condition_variable& g_cv_done = *new std::condition_variable;
bool g_worker = false;
constexpr size_t kMaxQueued = 6;  // pictures waiting to be encoded (about 25 MB each at 2x 16:9)

std::vector<uint64_t> scripted_frames() {
    std::vector<uint64_t> v;
    if (const char* e = getenv("WWHD_TEST_SCREENSHOT"))
        for (const char* p = e; *p;) {
            char* end;
            uint64_t f = strtoull(p, &end, 10);
            if (end == p) break;
            v.push_back(f);
            p = end;
            while (*p == ',') p++;
        }
    return v;
}

void put_be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s));
}
void chunk(FILE* f, const char* type, const uint8_t* data, size_t n, bool& ok) {
    std::vector<uint8_t> head;
    put_be32(head, uint32_t(n));
    head.insert(head.end(), type, type + 4);
    uLong crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)type, 4);
    if (n) crc = crc32(crc, data, uInt(n));
    std::vector<uint8_t> tail;
    put_be32(tail, uint32_t(crc));
    ok = ok && fwrite(head.data(), 1, head.size(), f) == head.size() && (!n || fwrite(data, 1, n, f) == n) &&
         fwrite(tail.data(), 1, tail.size(), f) == tail.size();
}

void worker() {
    host::set_thread_name("screenshot");
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);  // background work: the game threads come first
#endif
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return !g_jobs.empty(); });
            j = std::move(g_jobs.front());
            g_jobs.pop_front();
        }
        auto t0 = std::chrono::steady_clock::now();
        std::string tmp = j.path + ".part";
        bool ok = write_png(tmp, j.w, j.h, j.stride, j.pixels, j.bgra);
        j.owner.reset();  // the renderer's readback memory goes back now
        std::error_code ec;
        if (ok) std::filesystem::rename(tmp, j.path, ec);
        if (!ok || ec) std::filesystem::remove(tmp, ec);
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        std::string name = std::filesystem::path(j.path).filename().string();
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_reserved.erase(j.path);
            if (ok && j.tv) g_last_file = j.path;
        }
        g_cv_done.notify_all();
        if (ok) {
            LOG("[screenshot] wrote %s (%ux%u, frame %llu; %.0f ms after the frame, encoding %.0f ms)", j.path.c_str(), j.w, j.h,
                (unsigned long long)j.frame, ms(j.queued, std::chrono::steady_clock::now()), ms(t0, std::chrono::steady_clock::now()));
            if (j.tv) ss::notice("Screenshot saved: " + name);
        } else {
            LOG("[screenshot] could not write %s", j.path.c_str());
            if (j.tv) ss::notice("Screenshot could not be saved (" + dir() + ")");
        }
    }
}

// a file name not in use: WindWakerHD_YYYY-MM-DD_HH-MM-SS[_n]
std::string reserve_base() {
    char stamp[64];
    time_t t = time(nullptr);
    strftime(stamp, sizeof stamp, "WindWakerHD_%Y-%m-%d_%H-%M-%S", localtime(&t));
    const std::string d = dir() + "/";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    std::lock_guard<std::mutex> lk(g_mu);
    for (int n = 1;; n++) {
        std::string base = d + stamp + (n > 1 ? "_" + std::to_string(n) : std::string());
        if (g_reserved.count(base + ".png") || std::filesystem::exists(base + ".png", ec) ||
            std::filesystem::exists(base + "_GamePad.png", ec))
            continue;
        g_reserved.insert(base + ".png");
        return base;
    }
}

int bound_pad() {
    return input_map::current().pad[input_map::kScreenshot];
}

}  // namespace

std::string dir() {
    static const std::string d = [] {
        std::string p;
        if (const char* e = getenv("WWHD_SCREENSHOT_DIR"); e && *e) p = e;
        else if (host::portable()) p = host::portable_user_dir() + "/screenshots";
        else {
#ifdef __APPLE__
            // next to the states folder (savestate.cpp state_dir)
            p = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/Library/Application Support/wwhd/screenshots";
#else
            p = host::config_dir() + "/screenshots";
#endif
        }
        return p;
    }();
    return d;
}

void finish() {
    std::unique_lock<std::mutex> lk(g_mu);
    if (g_reserved.empty()) return;
    LOG("[screenshot] waiting for %zu screenshot(s) to be written", g_reserved.size());
    if (!g_cv_done.wait_for(lk, std::chrono::seconds(10), [] { return g_reserved.empty(); }))
        LOG("[screenshot] gave up waiting: %zu not written", g_reserved.size());
}

std::string last_file() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_last_file;
}

void request() {
    g_requests.fetch_add(1);
}

bool key_down(int code) {
    if (code < 0 || code > 255) return false;
    for (int k : input_map::current().keys[input_map::kScreenshot])
        if (k == code) {
            request();
            return true;
        }
    return false;
}

void poll_controller(const float* values) {
    static bool prev = false;
    const int p = bound_pad();
    const bool now = p > input_map::kPadNone && p < input_map::kPadCount && values && values[p] > 0.5f;
    if (now && !prev) request();
    prev = now;
}

bool gamepad_too() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_gamepad < 0) {
        if (const char* e = getenv("WWHD_SCREENSHOT_GAMEPAD"); e && *e) g_gamepad = atoi(e) != 0;
        else {
            std::string v;
            g_gamepad = hostui::get("screenshotGamePad", v) && v == "1";
        }
    }
    return g_gamepad == 1;
}

void set_gamepad_too(bool on) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_gamepad = on;
    }
    hostui::set("screenshotGamePad", on ? "1" : "0");
}

// the render thread's swap-to-swap intervals around each screenshot (log: the capture must not make a
// frame late): the 6 before it and the 6 after it
namespace {
double g_intervals[6];
int g_interval_n = 0, g_after = -1;
uint64_t g_shot_frame = 0;
std::string g_interval_log;
void note_swap(uint64_t frame) {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    if (g_after >= 0) {
        char b[32];
        snprintf(b, sizeof b, " %.1f", ms);
        g_interval_log += b;
        if (++g_after == 6) {
            LOG("[screenshot] frame %llu: swap intervals (ms) before%s | after%s", (unsigned long long)g_shot_frame,
                [] {
                    std::string s;
                    for (int i = 0; i < 6; i++) {
                        char b[32];
                        snprintf(b, sizeof b, " %.1f", g_intervals[(g_interval_n + i) % 6]);
                        s += b;
                    }
                    return s;
                }().c_str(),
                g_interval_log.c_str());
            g_after = -1;
        }
        return;
    }
    g_intervals[g_interval_n] = ms;
    g_interval_n = (g_interval_n + 1) % 6;
    (void)frame;
}
}  // namespace

bool take(uint64_t frame, std::string& tv, std::string& gamepad) {
    static const std::vector<uint64_t> scripted = scripted_frames();
    note_swap(frame);
    bool want = g_requests.exchange(0) > 0;
    if (!scripted.empty() && std::find(scripted.begin(), scripted.end(), frame) != scripted.end()) want = true;
    if (!want) return false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_jobs.size() >= kMaxQueued) {
            LOG("[screenshot] frame %llu: skipped, %zu pictures are still being saved", (unsigned long long)frame, g_jobs.size());
            ss::notice("Screenshot skipped: still saving the previous ones");
            return false;
        }
    }
    std::string base = reserve_base();
    tv = base + ".png";
    gamepad = gamepad_too() ? base + "_GamePad.png" : std::string();
    if (!gamepad.empty()) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_reserved.insert(gamepad);
    }
    LOG("[screenshot] frame %llu: taking %s%s", (unsigned long long)frame, tv.c_str(), gamepad.empty() ? "" : " (+ GamePad)");
    if (g_after < 0) {
        g_after = 0;
        g_shot_frame = frame;
        g_interval_log.clear();
    }
    return true;
}

void write_async(const std::string& path, uint32_t width, uint32_t height, size_t stride, const uint8_t* pixels,
                 std::shared_ptr<const void> owner, uint64_t frame, bool tv, bool bgra) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_worker) {
        std::thread(worker).detach();
        g_worker = true;
    }
    if (!pixels || !width || !height) {
        g_reserved.erase(path);
        g_cv_done.notify_all();
        return;
    }
    g_jobs.push_back({path, width, height, stride, pixels, std::move(owner), frame, tv, bgra, std::chrono::steady_clock::now()});
    g_cv.notify_one();
}

bool write_png(const std::string& path, uint32_t width, uint32_t height, size_t stride, const uint8_t* rgba, bool bgra) {
    if (!width || !height || !rgba) return false;
    // scanlines: one filter byte + RGB; per row the filter (None, Sub, Up, Average, Paeth) with the
    // smallest sum of absolute differences (the usual heuristic: well compressible, cheap)
    const size_t rowBytes = size_t(width) * 3;
    std::vector<uint8_t> raw(rowBytes * 2), filtered((rowBytes + 1) * height);
    uint8_t* prev = raw.data();
    uint8_t* cur = raw.data() + rowBytes;
    memset(prev, 0, rowBytes);
    std::vector<uint8_t> cand[5];
    for (auto& c : cand) c.resize(rowBytes);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src = rgba + size_t(y) * stride;
        if (bgra)
            for (uint32_t x = 0; x < width; x++) {
                cur[x * 3] = src[x * 4 + 2];
                cur[x * 3 + 1] = src[x * 4 + 1];
                cur[x * 3 + 2] = src[x * 4];
            }
        else
            for (uint32_t x = 0; x < width; x++) memcpy(cur + x * 3, src + x * 4, 3);
        uint64_t best = ~0ull;
        int bestF = 0;
        for (int f = 0; f < 5; f++) {
            uint8_t* o = cand[f].data();
            uint64_t sum = 0;
            for (size_t i = 0; i < rowBytes; i++) {
                int a = i >= 3 ? cur[i - 3] : 0, b = prev[i], c = i >= 3 ? prev[i - 3] : 0, v = cur[i], pr;
                switch (f) {
                case 0: pr = 0; break;
                case 1: pr = a; break;
                case 2: pr = b; break;
                case 3: pr = (a + b) / 2; break;
                default: {
                    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                    pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                }
                }
                uint8_t r = uint8_t(v - pr);
                o[i] = r;
                sum += r < 128 ? r : 256 - r;
            }
            if (sum < best) { best = sum; bestF = f; }
        }
        uint8_t* out = filtered.data() + size_t(y) * (rowBytes + 1);
        out[0] = uint8_t(bestF);
        memcpy(out + 1, cand[bestF].data(), rowBytes);
        std::swap(prev, cur);
    }
    uLongf zn = compressBound(uLong(filtered.size()));
    std::vector<uint8_t> z(zn);
    // level 4: a third of level 6's time on game pictures (about 0.2 s at 2x 21:9), 7% larger
    if (compress2(z.data(), &zn, filtered.data(), uLong(filtered.size()), 4) != Z_OK) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    bool ok = fwrite(sig, 1, 8, f) == 8;
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, width);
    put_be32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bit, RGB, deflate, adaptive filtering, no interlace
    chunk(f, "IHDR", ihdr.data(), ihdr.size(), ok);
    const uint8_t srgb = 0;  // sRGB, perceptual intent: the values are display-encoded sRGB
    chunk(f, "sRGB", &srgb, 1, ok);
    chunk(f, "IDAT", z.data(), zn, ok);
    chunk(f, "IEND", nullptr, 0, ok);
    ok = fclose(f) == 0 && ok;
    return ok;
}

}  // namespace screenshot
