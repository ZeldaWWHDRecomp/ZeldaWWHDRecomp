// Screenshots (runtime/src/screenshot.cpp): the PNG writer (decoded again here: inflate + unfilter),
// file names, the binding, the request/take cycle and the encoding thread. No game or renderer.
#include <zlib.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "input_map.h"
#include "platform/keycodes.h"
#include "screenshot.h"

#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

// what screenshot.cpp needs from the rest of the runtime
static std::vector<std::string> g_notices;
namespace ss { void notice(const std::string& t) { g_notices.push_back(t); } }
namespace hostui {
bool get(const char*, std::string&) { return false; }
void set(const char*, const std::string&) {}
}
void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// a minimal PNG reader for what write_png writes (8-bit RGB, one or more IDAT chunks)
static bool read_png(const std::string& path, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgb, bool& srgb) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (d.size() < 8 || memcmp(d.data(), sig, 8)) return false;
    std::vector<uint8_t> z;
    srgb = false;
    for (size_t p = 8; p + 12 <= d.size();) {
        uint32_t len = be32(&d[p]);
        std::string type((const char*)&d[p + 4], 4);
        const uint8_t* data = &d[p + 8];
        uLong crc = crc32(crc32(0, Z_NULL, 0), &d[p + 4], len + 4);
        if (crc != be32(&d[p + 8 + len])) return false;
        if (type == "IHDR") {
            w = be32(data);
            h = be32(data + 4);
            if (data[8] != 8 || data[9] != 2) return false;
        } else if (type == "sRGB") srgb = true;
        else if (type == "IDAT") z.insert(z.end(), data, data + len);
        else if (type == "IEND") break;
        p += 12 + len;
    }
    const size_t row = size_t(w) * 3;
    std::vector<uint8_t> raw((row + 1) * h);
    uLongf rn = raw.size();
    if (uncompress(raw.data(), &rn, z.data(), z.size()) != Z_OK || rn != raw.size()) return false;
    rgb.assign(row * h, 0);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* in = &raw[y * (row + 1)];
        uint8_t* out = &rgb[y * row];
        const uint8_t* prev = y ? &rgb[(y - 1) * row] : nullptr;
        for (size_t i = 0; i < row; i++) {
            int a = i >= 3 ? out[i - 3] : 0, b = prev ? prev[i] : 0, c = prev && i >= 3 ? prev[i - 3] : 0, pr;
            switch (in[0]) {
            case 0: pr = 0; break;
            case 1: pr = a; break;
            case 2: pr = b; break;
            case 3: pr = (a + b) / 2; break;
            case 4: {
                int q = a + b - c, pa = abs(q - a), pb = abs(q - b), pc = abs(q - c);
                pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default: return false;
            }
            out[i] = uint8_t(in[1 + i] + pr);
        }
    }
    return true;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("wwhd_screenshot_test_" + std::to_string(time(nullptr)));
    fs::create_directories(dir);
#ifdef _WIN32
    _putenv_s("WWHD_SCREENSHOT_DIR", dir.string().c_str());
    _putenv_s("WWHD_TEST_SCREENSHOT", "7,9");
#else
    setenv("WWHD_SCREENSHOT_DIR", dir.string().c_str(), 1);
    setenv("WWHD_TEST_SCREENSHOT", "7,9", 1);
#endif

    // 1. the PNG writer: an image with gradients, noise, flat areas and a row stride; RGBA and BGRA
    const uint32_t W = 301, H = 77;
    const size_t stride = W * 4 + 12;
    std::vector<uint8_t> px(stride * H, 0xEE);
    uint32_t seed = 1;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t* p = &px[y * stride + x * 4];
            seed = seed * 1664525u + 1013904223u;
            p[0] = uint8_t(x);
            p[1] = uint8_t(y * 3);
            p[2] = x > 200 ? uint8_t(seed >> 24) : 40;
            p[3] = 255;
        }
    for (bool bgra : {false, true}) {
        const std::string path = (dir / (bgra ? "bgra.png" : "rgba.png")).string();
        CHECK(screenshot::write_png(path, W, H, stride, px.data(), bgra));
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> rgb;
        bool srgb;
        CHECK(read_png(path, w, h, rgb, srgb));
        CHECK(w == W && h == H && srgb);
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < W; x++) {
                const uint8_t* s = &px[y * stride + x * 4];
                const uint8_t* d = &rgb[(size_t(y) * W + x) * 3];
                CHECK(d[0] == s[bgra ? 2 : 0] && d[1] == s[1] && d[2] == s[bgra ? 0 : 2]);
            }
    }
    CHECK(!screenshot::write_png((dir / "missing" / "x.png").string(), W, H, stride, px.data()));

    // 2. the binding: F10 by default, rebindable; other keys are not taken
    input_map::set_current(input_map::Mapping::defaults(), false);
    CHECK(screenshot::key_down(kVK_F10));
    CHECK(!screenshot::key_down(kVK_F9) && !screenshot::key_down(kVK_ANSI_P) && !screenshot::key_down(-1));
    {
        auto m = input_map::current();
        m.keys[input_map::kScreenshot] = {kVK_F9, input_map::kNoKey};
        m.pad[input_map::kScreenshot] = input_map::kPadR3;
        input_map::set_current(m, false);
    }
    CHECK(!screenshot::key_down(kVK_F10) && screenshot::key_down(kVK_F9));

    // 3. requests: one per press (the two key presses above are one pending request), scripted frames,
    //    names WindWakerHD_YYYY-MM-DD_HH-MM-SS[_n].png, the GamePad file only with the option
    std::string tv, gp, tv2, gp2;
    CHECK(screenshot::take(1, tv, gp));
    CHECK(!screenshot::take(2, tv2, gp2));
    float pad[input_map::kPadCount] = {};
    pad[input_map::kPadR3] = 1;
    screenshot::poll_controller(pad);  // pressed: one request
    screenshot::poll_controller(pad);  // held: nothing more
    CHECK(screenshot::take(3, tv2, gp2) && !screenshot::take(4, tv2, gp2));
    pad[input_map::kPadR3] = 0;
    screenshot::poll_controller(pad);
    const std::string name = fs::path(tv).filename().string();
    CHECK(name.rfind("WindWakerHD_", 0) == 0 && name.size() >= 35 && name[16] == '-' && name[22] == '_');
    CHECK(fs::path(tv).parent_path() == dir && gp.empty());
    CHECK(tv2 != tv);  // still reserved (not written yet): the next name
    screenshot::set_gamepad_too(true);
    std::string tv7, gp7, tv3, gp3;
    CHECK(screenshot::take(7, tv7, gp7) && !screenshot::take(8, tv3, gp3) && screenshot::take(9, tv3, gp3));
    CHECK(!gp7.empty() && tv7 != tv3);
    CHECK(gp3.size() > 12 && gp3.substr(gp3.size() - 12) == "_GamePad.png" && gp3.substr(0, gp3.size() - 12) + ".png" == tv3);

    // 4. writing on the worker thread; the TV file shows a notice; finish() waits for all of them
    auto owned = std::make_shared<std::vector<uint8_t>>(px);
    for (const std::string& p : {tv, tv2, tv3, gp3})
        screenshot::write_async(p, W, H, stride, owned->data(), owned, 1, p != gp3);
    // take(7)'s pictures never came (a renderer releases the names): nothing written, no notice
    screenshot::write_async(tv7, 0, 0, 0, nullptr, nullptr, 7, true);
    screenshot::write_async(gp7, 0, 0, 0, nullptr, nullptr, 7, false);
    screenshot::finish();
    for (const std::string& p : {tv, tv2, tv3, gp3}) CHECK(fs::exists(p) && !fs::exists(p + ".part"));
    CHECK(!fs::exists(tv7) && !fs::exists(gp7));
    CHECK(screenshot::last_file() == tv3 || screenshot::last_file() == tv2 || screenshot::last_file() == tv);
    CHECK(g_notices.size() == 3 && g_notices[0].rfind("Screenshot saved: WindWakerHD_", 0) == 0);

    // 5. encoding time of a large picture (2x internal resolution at 21:9), for the log
    {
        const uint32_t bw = 3414, bh = 1440;
        std::vector<uint8_t> big(size_t(bw) * bh * 4);
        for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t((i * 7) ^ (i >> 11));
        auto t0 = std::chrono::steady_clock::now();
        CHECK(screenshot::write_png((dir / "big.png").string(), bw, bh, bw * 4, big.data()));
        printf("screenshot_test: %ux%u encoded in %.0f ms (%.1f MB)\n", bw, bh,
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
               fs::file_size(dir / "big.png") / 1048576.0);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    puts("screenshot_test: PNG writer, binding, requests, names, worker passed");
    return 0;
}
