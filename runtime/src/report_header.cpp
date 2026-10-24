// Performance report header (report_header.h).
#include "report_header.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "interp_pacing.h"
#include "platform/android_display_rate.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__ANDROID__)
#include <sys/system_properties.h>
#else
#include <sys/utsname.h>
#endif

namespace reporthdr {

const char* const kVulkanCpuPaths[kVulkanCpuPathCount] = {
    "WWHD_VK_REUSE_UNIFORM_SNAPSHOTS", "WWHD_VK_REUSE_FEEDBACK_IMAGES", "WWHD_VK_SKIP_REDUNDANT_BINDS",
    "WWHD_VK_DESCRIPTOR_RANKS",        "WWHD_VK_PIPELINE_LOOKASIDE",    "WWHD_VK_SHADER_ADDRESS_MEMO",
    "WWHD_VK_FETCH_MEMO",              "WWHD_VK_SPECIALIZE_INDICES",    "WWHD_VK_SHADER_STATE_MEMO",
    "WWHD_VK_SKIP_VERTEX_BINDS",       "WWHD_VK_SAMPLER_MEMO",          "WWHD_VK_SPARSE_HASH_MEMO",
    "WWHD_VK_SHADER_KEY_DIRTY",        "WWHD_VK_REUSE_VERTEX_SNAPSHOTS", "WWHD_VK_VERTEX_HISTORY_REUSE"};

std::string vk_version(uint32_t v) {
    // VK_API_VERSION_MAJOR / MINOR / PATCH (the variant, top 3 bits, is 0 for Vulkan)
    return std::to_string((v >> 22) & 0x7f) + "." + std::to_string((v >> 12) & 0x3ff) + "." + std::to_string(v & 0xfff);
}

std::string driver_version(uint32_t vendorID, uint32_t v, bool windows) {
    if (vendorID == 0x10DE)  // NVIDIA: 10.8.8.6
        return std::to_string(v >> 22) + "." + std::to_string((v >> 14) & 0xff) + "." +
               std::to_string((v >> 6) & 0xff) + "." + std::to_string(v & 0x3f);
    if (vendorID == 0x8086 && windows)  // Intel on Windows: 18.14
        return std::to_string(v >> 14) + "." + std::to_string(v & 0x3fff);
    // VK_MAKE_VERSION's 10.10.12 bits (a 10-bit major: Qualcomm reports 512.x)
    return std::to_string(v >> 22) + "." + std::to_string((v >> 12) & 0x3ff) + "." + std::to_string(v & 0xfff);
}

std::string vulkan_gpu(const char* deviceName, uint32_t vendorID, uint32_t driverVersion, uint32_t apiVersion,
                       bool windows) {
    return std::string(deviceName && *deviceName ? deviceName : "unknown GPU") + ", driver " +
           driver_version(vendorID, driverVersion, windows) + ", Vulkan " + vk_version(apiVersion);
}

std::string windows_name(uint32_t major, uint32_t minor, uint32_t build, const std::string& displayVersion,
                         uint32_t ubr) {
    std::string s;
    if (major == 10 && minor == 0)
        s = build >= 22000 ? "Windows 11" : "Windows 10";  // Windows 11 still reports 10.0
    else
        s = "Windows " + std::to_string(major) + "." + std::to_string(minor);
    if (!displayVersion.empty()) s += " " + displayVersion;
    s += " (build " + std::to_string(build);
    if (ubr) s += "." + std::to_string(ubr);
    return s + ")";
}

std::string os_release_pretty_name(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.compare(0, 12, "PRETTY_NAME=") != 0) continue;
        std::string v = line.substr(12);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        // shell-style quoting (os-release(5)): "..." or '...', backslash escapes inside double quotes
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) {
            const char q = v[0];
            std::string out;
            for (size_t i = 1; i + 1 < v.size(); i++) {
                if (q == '"' && v[i] == '\\' && i + 2 < v.size()) i++;
                out += v[i];
            }
            v = out;
        }
        return v;
    }
    return "";
}

#if defined(_WIN32)
static std::string reg_string(const wchar_t* name) {
    wchar_t buf[128];
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name, RRF_RT_REG_SZ,
                     nullptr, buf, &size) != ERROR_SUCCESS)
        return "";
    char out[128];
    const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof out, nullptr, nullptr);
    return n > 0 ? std::string(out) : std::string();
}
static uint32_t reg_dword(const wchar_t* name) {
    DWORD v = 0, size = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name, RRF_RT_REG_DWORD,
                     nullptr, &v, &size) != ERROR_SUCCESS)
        return 0;
    return v;
}
#endif

std::string os_description() {
#if defined(_WIN32)
    // RtlGetVersion: the real version (GetVersionEx answers what the manifest declares)
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof vi;
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtl = ntdll ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))
                     : nullptr;
    if (!rtl || rtl(&vi) != 0) return "Windows";
    std::string display = reg_string(L"DisplayVersion");  // "23H2" (Windows 10 20H2 and later)
    if (display.empty()) display = reg_string(L"ReleaseId");  // "1909" (older Windows 10)
    return windows_name(vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber, display, reg_dword(L"UBR"));
#elif defined(__APPLE__)
    char ver[64] = "", build[64] = "";
    size_t n = sizeof ver;
    if (sysctlbyname("kern.osproductversion", ver, &n, nullptr, 0) != 0) ver[0] = 0;
    n = sizeof build;
    if (sysctlbyname("kern.osversion", build, &n, nullptr, 0) != 0) build[0] = 0;
    std::string s = std::string("macOS ") + (ver[0] ? ver : "(unknown version)");
    if (build[0]) s += std::string(" (") + build + ")";
    return s;
#elif defined(__ANDROID__)
    char release[PROP_VALUE_MAX] = "", sdk[PROP_VALUE_MAX] = "", maker[PROP_VALUE_MAX] = "", model[PROP_VALUE_MAX] = "";
    __system_property_get("ro.build.version.release", release);
    __system_property_get("ro.build.version.sdk", sdk);
    __system_property_get("ro.product.manufacturer", maker);
    __system_property_get("ro.product.model", model);
    std::string s = std::string("Android ") + (release[0] ? release : "(unknown version)");
    if (sdk[0]) s += std::string(" (API ") + sdk + ")";
    if (model[0]) s += std::string(", ") + (maker[0] ? std::string(maker) + " " : std::string()) + model;
    return s;
#else
    std::string name;
    for (const char* path : {"/etc/os-release", "/usr/lib/os-release"}) {
        if (FILE* f = fopen(path, "r")) {
            std::string text;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            fclose(f);
            name = os_release_pretty_name(text);
            if (!name.empty()) break;
        }
    }
    struct utsname u;
    const bool haveUname = uname(&u) == 0;
    if (name.empty()) name = haveUname ? u.sysname : "Linux";
    if (haveUname) name += std::string(", kernel ") + u.release;
    return name;
#endif
}

std::vector<std::string> vulkan_overrides(GetEnv env) {
    std::vector<std::string> out;
    for (const char* name : kVulkanCpuPaths) {
        const char* v = env(name);
        if (!v) out.push_back(std::string(name) + " unset");
        else if (strcmp(v, "1") != 0) out.push_back(std::string(name) + "=" + v);
    }
    // on unless set to a value that reads as 0 (gx2_core.cpp lazy_draw_done, backend.cpp async_present)
    for (const char* name : {"WWHD_VK_LAZY_DRAW_DONE", "WWHD_VK_ASYNC_PRESENT"}) {
        const char* v = env(name);
        if (v && atoi(v) == 0) out.push_back(std::string(name) + "=" + v);
    }
    return out;
}

// how a frame rate fps looks on a hz display (interp_pacing.h even_on_display): "60 fps even on it",
// "40 fps uneven on it (frames shown for 1 and 2 refreshes in turn)"
static std::string evenness(int fps, int hz) {
    char s[128];
    if (interp::pacing::even_on_display(fps, hz)) snprintf(s, sizeof s, "%d fps even on it", fps);
    else if (hz < fps) snprintf(s, sizeof s, "%d fps uneven on it (some frames never shown)", fps);
    else snprintf(s, sizeof s, "%d fps uneven on it (frames shown for %d and %d refreshes in turn)", fps, hz / fps, hz / fps + 1);
    return s;
}

std::string display_line(const Display& d) {
    if (d.hz <= 0) return "display: refresh rate unknown";
    char buf[96];
    snprintf(buf, sizeof buf, "display: %d Hz", d.hz);
    std::string s = buf;
    if (d.overridden) s += " (set by WWHD_DISPLAY_HZ)";
    else if (d.requestedHz == 0) s += " (requested: system choice)";
    else if (d.requestedHz > 0) {
        snprintf(buf, sizeof buf, " (requested %d Hz%s)", d.requestedHz,
                 android_display_rate::reached(d.hz, d.requestedHz) ? "" : ", not reached");
        s += buf;
    }
    const bool changed = !d.overridden && d.seenLo > 0 && d.seenHi > d.seenLo;
    if (changed) {
        snprintf(buf, sizeof buf, ", seen %d-%d Hz since start or reset", d.seenLo, d.seenHi);
        s += buf;
    }
    if (d.fps > 0) {
        s += "; " + evenness(d.fps, d.hz);
        // the rate dropped (or rose) to one the frames look uneven on, while even now
        if (changed && interp::pacing::even_on_display(d.fps, d.hz)) {
            for (int other : {d.seenLo, d.seenHi}) {
                if (other == d.hz || interp::pacing::even_on_display(d.fps, other)) continue;
                snprintf(buf, sizeof buf, ", uneven at %d Hz", other);
                s += buf;
            }
        }
    }
    return s;
}

std::string format(const Info& in) {
    std::string s = "Wind Waker HD " + in.version + " (" + in.commit + ")" + (in.variant.empty() ? "" : " " + in.variant) +
                    ", " + in.os;
    if (!in.gpu.empty()) s += ", " + in.gpu;
    char scale[32];
    snprintf(scale, sizeof scale, "%.1fx", in.scale);
    s += "\nperformance report: renderer " + in.renderer + ", host " + in.host + ", " + in.fps;
    if (in.keepGameSpeed >= 0) s += in.keepGameSpeed ? ", keep game speed on" : ", keep game speed off";
    s += std::string(", internal scale ") + scale;
    if (in.bufferCache >= 0) s += in.bufferCache ? ", buffer cache on" : ", buffer cache off";
    if (in.submissionPool > 0) s += ", submission pool " + std::to_string(in.submissionPool);
    if (!in.gyro.empty()) s += ", gyro " + in.gyro;
    for (size_t i = 0; i < in.overrides.size(); i++) s += (i ? ", " : "; overrides: ") + in.overrides[i];
    s += "\n";
    if (!in.display.empty()) s += in.display + "\n";
    return s;
}

}  // namespace reporthdr
