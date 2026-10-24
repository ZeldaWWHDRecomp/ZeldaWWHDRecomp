// Performance report header (runtime/src/report_header.h): driver version decoding per vendor,
// Windows and Linux OS names, the overrides list, the display line and the header format. Prints this build's header
// with this system's OS (and a sample GPU) at the end.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "build_info.h"
#include "report_header.h"

using namespace reporthdr;

static int failures = 0;
static void expect(const std::string& got, const std::string& want, const char* what) {
    if (got != want) {
        fprintf(stderr, "FAIL %s:\n  got  \"%s\"\n  want \"%s\"\n", what, got.c_str(), want.c_str());
        failures++;
    }
}

static uint32_t vk(uint32_t major, uint32_t minor, uint32_t patch) { return (major << 22) | (minor << 12) | patch; }

static std::map<std::string, std::string> g_env;
static const char* fake_env(const char* name) {
    auto it = g_env.find(name);
    return it == g_env.end() ? nullptr : it->second.c_str();
}
// the environment main.cpp leaves: every CPU path 1, nothing else set
static void default_env() {
    g_env.clear();
    for (const char* n : kVulkanCpuPaths) g_env[n] = "1";
}
static std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) s += (s.empty() ? "" : "|") + x;
    return s;
}

int main() {
    // ---- Vulkan versions
    expect(vk_version(vk(1, 3, 287)), "1.3.287", "apiVersion 1.3.287");
    expect(vk_version(vk(1, 2, 0)), "1.2.0", "apiVersion 1.2.0");

    // ---- driver versions, as vulkaninfo shows them
    // NVIDIA 10.8.8.6: 560.94.0.0 (Windows 560.94), 550.120.0.0 (Linux)
    expect(driver_version(0x10DE, (560u << 22) | (94u << 14), true), "560.94.0.0", "NVIDIA Windows");
    expect(driver_version(0x10DE, (550u << 22) | (120u << 14), false), "550.120.0.0", "NVIDIA Linux");
    expect(driver_version(0x10DE, (535u << 22) | (183u << 14) | (1u << 6) | 2u, false), "535.183.1.2", "NVIDIA all fields");
    // AMD (Windows and Linux, AMDVLK and RADV): the Vulkan packing
    expect(driver_version(0x1002, vk(2, 0, 302), true), "2.0.302", "AMD Windows");
    expect(driver_version(0x1002, vk(24, 0, 5), false), "24.0.5", "AMD RADV (Mesa 24.0.5)");
    // Intel: 18.14 on Windows (101.5762 -> 101.5762), the Vulkan packing elsewhere (Mesa ANV)
    expect(driver_version(0x8086, (101u << 14) | 5762u, true), "101.5762", "Intel Windows");
    expect(driver_version(0x8086, vk(24, 2, 8), false), "24.2.8", "Intel Linux (Mesa)");
    // Qualcomm, ARM, Apple (MoltenVK): the Vulkan packing
    expect(driver_version(0x5143, vk(512, 762, 12), false), "512.762.12", "Qualcomm");
    expect(driver_version(0x106B, vk(0, 2, 2019), false), "0.2.2019", "Apple (MoltenVK)");
    expect(vulkan_gpu("AMD Radeon RX 6700 XT", 0x1002, vk(2, 0, 302), vk(1, 3, 287), true),
           "AMD Radeon RX 6700 XT, driver 2.0.302, Vulkan 1.3.287", "Vulkan GPU line");
    expect(vulkan_gpu("", 0x10DE, (560u << 22) | (94u << 14), vk(1, 3, 280), true),
           "unknown GPU, driver 560.94.0.0, Vulkan 1.3.280", "Vulkan GPU without a name");

    // ---- OS names
    expect(windows_name(10, 0, 22631, "23H2", 4317), "Windows 11 23H2 (build 22631.4317)", "Windows 11");
    expect(windows_name(10, 0, 19045, "22H2", 0), "Windows 10 22H2 (build 19045)", "Windows 10");
    expect(windows_name(10, 0, 18363, "1909", 1), "Windows 10 1909 (build 18363.1)", "Windows 10 (ReleaseId)");
    expect(windows_name(6, 1, 7601, "", 0), "Windows 6.1 (build 7601)", "Windows 7");
    expect(os_release_pretty_name("NAME=\"Ubuntu\"\nPRETTY_NAME=\"Ubuntu 24.04.1 LTS\"\nID=ubuntu\n"), "Ubuntu 24.04.1 LTS",
           "os-release, double quotes");
    expect(os_release_pretty_name("ID=arch\nPRETTY_NAME='Arch Linux'\n"), "Arch Linux", "os-release, single quotes");
    expect(os_release_pretty_name("PRETTY_NAME=Gentoo\r\n"), "Gentoo", "os-release, unquoted, CRLF");
    expect(os_release_pretty_name("PRETTY_NAME=\"say \\\"hi\\\"\"\n"), "say \"hi\"", "os-release, escapes");
    expect(os_release_pretty_name("NAME=Foo\n"), "", "os-release without PRETTY_NAME");

    // ---- overrides: only switches away from their defaults
    default_env();
    expect(joined(vulkan_overrides(fake_env)), "", "defaults: no overrides");
    g_env["WWHD_VK_LAZY_DRAW_DONE"] = "1";  // explicit default
    g_env["WWHD_VK_ASYNC_PRESENT"] = "2";   // any nonzero number is on
    expect(joined(vulkan_overrides(fake_env)), "", "explicit defaults: no overrides");
    g_env["WWHD_VK_REUSE_VERTEX_SNAPSHOTS"] = "0";
    g_env["WWHD_VK_SAMPLER_MEMO"] = "yes";  // active only when exactly 1
    g_env.erase("WWHD_VK_FETCH_MEMO");      // not set at all (outside main.cpp)
    g_env["WWHD_VK_LAZY_DRAW_DONE"] = "0";
    g_env["WWHD_VK_ASYNC_PRESENT"] = "off";  // atoi 0: off
    expect(joined(vulkan_overrides(fake_env)),
           "WWHD_VK_FETCH_MEMO unset|WWHD_VK_SAMPLER_MEMO=yes|WWHD_VK_REUSE_VERTEX_SNAPSHOTS=0|"
           "WWHD_VK_LAZY_DRAW_DONE=0|WWHD_VK_ASYNC_PRESENT=off",
           "overrides in list order");
    // every one of the 15 paths is checked
    default_env();
    for (const char* n : kVulkanCpuPaths) g_env[n] = "0";
    expect(std::to_string(vulkan_overrides(fake_env).size()), "15", "all 15 CPU paths off");

    // ---- the two lines
    Info in;
    in.version = "v0.2.5";
    in.commit = "2ff030c";
    in.os = "Windows 11 23H2 (build 22631.4317)";
    in.gpu = "AMD Radeon RX 6700 XT, driver 2.0.302, Vulkan 1.3.287";
    in.renderer = "Vulkan";
    in.host = "SDL";
    in.fps = "60 fps interpolation";
    in.scale = 2.0f;
    in.bufferCache = 0;
    in.overrides = {"WWHD_VK_REUSE_VERTEX_SNAPSHOTS=0"};
    expect(format(in),
           "Wind Waker HD v0.2.5 (2ff030c), Windows 11 23H2 (build 22631.4317), AMD Radeon RX 6700 XT, driver 2.0.302, "
           "Vulkan 1.3.287\n"
           "performance report: renderer Vulkan, host SDL, 60 fps interpolation, internal scale 2.0x, buffer cache off; "
           "overrides: WWHD_VK_REUSE_VERTEX_SNAPSHOTS=0\n",
           "Vulkan header");
    in.overrides.push_back("WWHD_VK_ASYNC_PRESENT=0");
    in.gyro = "controller";
    in.bufferCache = 1;
    expect(format(in).substr(format(in).find('\n') + 1),
           "performance report: renderer Vulkan, host SDL, 60 fps interpolation, internal scale 2.0x, buffer cache on, "
           "gyro controller; overrides: WWHD_VK_REUSE_VERTEX_SNAPSHOTS=0, WWHD_VK_ASYNC_PRESENT=0\n",
           "Vulkan header with gyro and two overrides");
    // frame interpolation: whether "Keep game speed" is on at its rate
    in.overrides.clear();
    in.gyro.clear();
    in.keepGameSpeed = 1;
    expect(format(in).substr(format(in).find('\n') + 1),
           "performance report: renderer Vulkan, host SDL, 60 fps interpolation, keep game speed on, internal scale 2.0x, "
           "buffer cache on\n",
           "keep game speed on");
    in.keepGameSpeed = 0;
    in.fps = "120 fps interpolation";
    expect(format(in).substr(format(in).find('\n') + 1),
           "performance report: renderer Vulkan, host SDL, 120 fps interpolation, keep game speed off, internal scale 2.0x, "
           "buffer cache on\n",
           "keep game speed off at 120 fps");
    in.submissionPool = 8;  // the Vulkan submission ring's size at start (submission_pool.h)
    expect(format(in).substr(format(in).find('\n') + 1),
           "performance report: renderer Vulkan, host SDL, 120 fps interpolation, keep game speed off, internal scale 2.0x, "
           "buffer cache on, submission pool 8\n",
           "submission pool size");
    in.submissionPool = -1;
    Info m;
    m.version = "v0.2.5+3";
    m.commit = "abcdef0-dirty";
    m.os = "macOS 15.6.1 (24G90)";
    m.gpu = "Apple M3 Max";
    m.renderer = "Metal";
    m.host = "AppKit";
    m.fps = "30 fps";
    m.scale = 1.5f;
    expect(format(m),
           "Wind Waker HD v0.2.5+3 (abcdef0-dirty), macOS 15.6.1 (24G90), Apple M3 Max\n"
           "performance report: renderer Metal, host AppKit, 30 fps, internal scale 1.5x\n",
           "Metal header");
    m.gpu.clear();
    expect(format(m).substr(0, format(m).find('\n')), "Wind Waker HD v0.2.5+3 (abcdef0-dirty), macOS 15.6.1 (24G90)",
           "header before the renderer started");

    // ---- the display line: actual rate, Android's request, changes during the report, evenness
    {
        Display d;
        expect(display_line(d), "display: refresh rate unknown", "display unknown");
        d.hz = 120;
        d.requestedHz = 120;
        d.seenLo = d.seenHi = 120;
        d.fps = 40;
        expect(display_line(d), "display: 120 Hz (requested 120 Hz); 40 fps even on it", "40 fps on the requested 120 Hz");
        d.hz = 60;
        d.seenLo = d.seenHi = 60;
        expect(display_line(d),
               "display: 60 Hz (requested 120 Hz, not reached); 40 fps uneven on it (frames shown for 1 and 2 refreshes in turn)",
               "40 fps kept at 60 Hz");
        d.seenHi = 120;  // the vendor dropped the screen to 60 Hz mid-run
        expect(display_line(d),
               "display: 60 Hz (requested 120 Hz, not reached), seen 60-120 Hz since start or reset; 40 fps uneven on it "
               "(frames shown for 1 and 2 refreshes in turn)",
               "40 fps dropped to 60 Hz mid-run");
        d.hz = 120;  // back at 120 Hz, but it was 60 Hz for a while
        expect(display_line(d),
               "display: 120 Hz (requested 120 Hz), seen 60-120 Hz since start or reset; 40 fps even on it, uneven at 60 Hz",
               "40 fps back at 120 Hz after a drop");
        d.hz = 119;  // 119.88 Hz modes, rounded
        d.seenLo = d.seenHi = 119;
        expect(display_line(d), "display: 119 Hz (requested 120 Hz); 40 fps even on it", "119.88 Hz counts as 120");
        d = Display();
        d.hz = 60;
        d.requestedHz = 0;  // WWHD_NO_60HZ: the system's choice
        d.fps = 30;
        expect(display_line(d), "display: 60 Hz (requested: system choice); 30 fps even on it", "Android system choice");
        d.requestedHz = 60;
        d.fps = 60;
        expect(display_line(d), "display: 60 Hz (requested 60 Hz); 60 fps even on it", "Android 60 Hz request");
        // desktop: no request, the actual rate only
        d = Display();
        d.hz = 144;
        d.seenLo = d.seenHi = 144;
        d.fps = 120;
        expect(display_line(d), "display: 144 Hz; 120 fps uneven on it (frames shown for 1 and 2 refreshes in turn)",
               "120 fps on 144 Hz");
        d.fps = 60;
        expect(display_line(d), "display: 144 Hz; 60 fps uneven on it (frames shown for 2 and 3 refreshes in turn)",
               "60 fps on 144 Hz");
        d.hz = 30;
        d.seenLo = d.seenHi = 0;
        expect(display_line(d), "display: 30 Hz; 60 fps uneven on it (some frames never shown)", "60 fps above the display");
        d.hz = 165;
        d.fps = 0;
        expect(display_line(d), "display: 165 Hz", "no evenness without a rate");
        // WWHD_DISPLAY_HZ: the set rate, no request or range shown
        d.hz = 120;
        d.overridden = true;
        d.requestedHz = 120;
        d.seenLo = 60;
        d.seenHi = 120;
        d.fps = 120;
        expect(display_line(d), "display: 120 Hz (set by WWHD_DISPLAY_HZ); 120 fps even on it", "WWHD_DISPLAY_HZ");
        // the third header line
        Info withDisplay = m;
        withDisplay.display = "display: 120 Hz";
        expect(format(withDisplay),
               "Wind Waker HD v0.2.5+3 (abcdef0-dirty), macOS 15.6.1 (24G90)\n"
               "performance report: renderer Metal, host AppKit, 30 fps, internal scale 1.5x\n"
               "display: 120 Hz\n",
               "header with the display line");
    }

    // ---- this build on this system (the GPU line comes from the running renderer; a sample here)
    expect(std::to_string(build::version()[0] != 0 && build::commit()[0] != 0), "1", "build info present");
    const std::string os = os_description();
    expect(std::to_string(!os.empty()), "1", "OS description present");
    Info here = in;
    here.version = build::version();
    here.commit = build::commit();
    here.os = os;
    printf("%s", format(here).c_str());

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("report_header: all checks passed\n");
    return 0;
}
