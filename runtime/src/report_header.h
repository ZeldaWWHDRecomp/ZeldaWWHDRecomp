// The header of the overlay's "Copy performance report" (overlay.cpp): which build, system, GPU and
// rendering-path switches a report comes from, so reports from different versions and setups can't
// be mixed up (issue #44). Two lines, and the display's refresh rate when the host reports one:
//
//   Wind Waker HD v0.2.5 (2ff030c), Windows 11 23H2 (build 22631.4317), AMD Radeon RX 6700 XT, driver 2.0.302, Vulkan 1.3.287
//   performance report: renderer Vulkan, host SDL, 60 fps interpolation, internal scale 2.0x, buffer cache off, submission pool 4; overrides: WWHD_VK_REUSE_VERTEX_SNAPSHOTS=0
//   display: 60 Hz (requested 120 Hz, not reached); 40 fps uneven on it (frames shown for 1 and 2 refreshes in turn)
//
// The formatting is pure (tested by runtime/tools/report_header_test.cpp); os_description() asks the
// running system.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace reporthdr {

// The Vulkan renderer's CPU paths (docs/vulkan.md, "CPU paths"): main.cpp sets each to 1 at start
// unless the environment sets it; a path is active only when its value is exactly 1.
constexpr int kVulkanCpuPathCount = 15;
extern const char* const kVulkanCpuPaths[kVulkanCpuPathCount];

// VK_API_VERSION packing: "1.3.287"
std::string vk_version(uint32_t v);
// VkPhysicalDeviceProperties::driverVersion decoded as vulkaninfo does: NVIDIA (0x10DE) 10.8.8.6 bits,
// Intel (0x8086) on Windows 18.14 bits, everything else (AMD, Mesa, Qualcomm, ARM, MoltenVK...)
// VK_MAKE_VERSION's 10.10.12 bits
std::string driver_version(uint32_t vendorID, uint32_t driverVersion, bool windows);
// "AMD Radeon RX 6700 XT, driver 2.0.302, Vulkan 1.3.287"
std::string vulkan_gpu(const char* deviceName, uint32_t vendorID, uint32_t driverVersion, uint32_t apiVersion,
                       bool windows);

// this system: "macOS 15.6.1 (24G90)", "Windows 11 23H2 (build 22631.4317)",
// "Ubuntu 24.04.1 LTS, kernel 6.8.0-45-generic", "Android 15 (API 35), samsung SM-S938B"
std::string os_description();
// the parts, for the tests
std::string windows_name(uint32_t major, uint32_t minor, uint32_t build, const std::string& displayVersion,
                         uint32_t ubr);
std::string os_release_pretty_name(const std::string& osRelease);  // "" if absent

// Vulkan rendering-path switches set away from their defaults, as NAME=value: the CPU paths (default
// 1; an unset one is listed as "NAME unset"), WWHD_VK_LAZY_DRAW_DONE and WWHD_VK_ASYNC_PRESENT (default
// on; listed when set to a value that turns them off)
using GetEnv = const char* (*)(const char* name);
std::vector<std::string> vulkan_overrides(GetEnv env);

// The display line: the refresh rate the game ran at, so a report shows whether e.g. 40 fps had the
// 120 Hz it needs to look even.
//   display: 120 Hz (requested 120 Hz); 40 fps even on it
//   display: 60 Hz (requested 120 Hz, not reached); 40 fps uneven on it (frames shown for 1 and 2 refreshes in turn)
//   display: 120 Hz (requested 120 Hz), seen 60-120 Hz since start or reset; 40 fps even on it, uneven at 60 Hz
//   display: 144 Hz; 60 fps uneven on it (frames shown for 2 and 3 refreshes in turn)     (desktop: no request)
struct Display {
    int hz = 0;            // interp::display_hz(); 0 unknown
    bool overridden = false;  // WWHD_DISPLAY_HZ sets hz
    int requestedHz = -1;  // Android's request (interp::requested_display_hz): 0 the system's choice, -1 none
    int seenLo = 0, seenHi = 0;  // lowest and highest rate since start or the averages' reset (0: none)
    int fps = 0;           // the drawn frame rate, checked for evenness (0: not checked)
};
std::string display_line(const Display& d);  // without '\n'

struct Info {
    std::string version, commit;   // build::version(), build::commit()
    std::string variant;           // build::variant(): "decomp" in the decomp build, else ""
    std::string os;                // os_description()
    std::string gpu;               // vulkan_gpu() or the Metal device name; "" when unknown
    std::string renderer, host, fps;
    int keepGameSpeed = -1;        // frame interpolation's "Keep game speed" at its rate: 0 / 1; -1 not shown
    float scale = 1;
    int bufferCache = -1;          // Vulkan: 0 / 1; -1 not shown (Metal)
    int submissionPool = -1;       // Vulkan: submission ring slots at start; -1 not shown (Metal)
    std::string gyro;              // gyro source id, "" when off
    std::vector<std::string> overrides;
    std::string display;           // display_line(); "" leaves the line out
};
// the lines, each ending in '\n'
std::string format(const Info& info);

}  // namespace reporthdr
