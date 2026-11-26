// See windows_hdr.h. dxgi.dll is loaded at run time, so the game needs no extra import library.
#include "windows_hdr.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>

#include <utility>
#include <vector>

namespace windows_hdr {
namespace {
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string utf8(const wchar_t* s) {
    if (!s || !*s) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string r(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, r.data(), n, nullptr, nullptr);
    return r;
}

using CreateFactory1 = HRESULT(WINAPI*)(REFIID, void**);
CreateFactory1 create_factory() {
    static CreateFactory1 fn = [] {
        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        return dxgi ? reinterpret_cast<CreateFactory1>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")))
                    : nullptr;
    }();
    return fn;
}

// The monitor's friendly name and the Windows SDR content brightness ("SDR content brightness"
// under Settings > System > Display > HDR), as SDL reads it (SDL_windowsmodes.c).
void display_config(HMONITOR monitor, Display& d) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result;
    do {
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return;
        paths.resize(np);
        modes.resize(nm);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr);
        paths.resize(np);
    } while (result == ERROR_INSUFFICIENT_BUFFER);
    if (result != ERROR_SUCCESS) return;
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, info.szDevice) != 0)
            continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) d.monitor = utf8(target.monitorFriendlyDeviceName);
        struct SdrWhiteLevel {  // DISPLAYCONFIG_SDR_WHITE_LEVEL, missing from older SDK headers
            DISPLAYCONFIG_DEVICE_INFO_HEADER header;
            ULONG SDRWhiteLevel;  // 1000 = 80 nits
        } white{};
        white.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(11);  // ..._GET_SDR_WHITE_LEVEL
        white.header.size = sizeof(white);
        white.header.adapterId = path.targetInfo.adapterId;
        white.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0)
            d.sdrWhite = white.SDRWhiteLevel * 80.0f / 1000.0f;
        return;
    }
}

Display describe(IDXGIFactory1* factory, HMONITOR monitor) {
    Display d;
    Com<IDXGIAdapter1> adapter;
    for (UINT a = 0; !d.found && factory->EnumAdapters1(a, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++a) {
        if (!adapter) continue;
        Com<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.put()) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (!output || FAILED(output->GetDesc(&desc)) || desc.Monitor != monitor) continue;
            Com<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc1{};
            if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(output6.put()))) ||
                FAILED(output6->GetDesc1(&desc1)))
                break;  // no IDXGIOutput6 (Windows 10 before 1703): unknown
            d.found = true;
            d.colorSpace = unsigned(desc1.ColorSpace);
            d.bitsPerColor = desc1.BitsPerColor;
            d.maxLuminance = desc1.MaxLuminance;
            d.maxFullFrameLuminance = desc1.MaxFullFrameLuminance;
            d.minLuminance = desc1.MinLuminance;
            d.device = utf8(desc1.DeviceName);
            DXGI_ADAPTER_DESC1 ad{};
            if (SUCCEEDED(adapter->GetDesc1(&ad))) d.adapter = utf8(ad.Description);
            break;
        }
    }
    display_config(monitor, d);
    return d;
}

bool same(const Display& a, const Display& b) {
    return a.found == b.found && a.colorSpace == b.colorSpace && a.bitsPerColor == b.bitsPerColor &&
           a.maxLuminance == b.maxLuminance && a.maxFullFrameLuminance == b.maxFullFrameLuminance &&
           a.minLuminance == b.minLuminance && a.sdrWhite == b.sdrWhite && a.device == b.device &&
           a.monitor == b.monitor && a.adapter == b.adapter;
}

IDXGIFactory1* factory = nullptr;  // kept for the process (not released during static destruction)
HMONITOR lastMonitor = nullptr;
}  // namespace

bool refresh(void* hwnd, bool force, Display& out) {
    HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST);
    // A DXGI factory reports outputs as they were at its creation; IsCurrent() turns false when
    // the display configuration changes, which includes switching Windows HDR on or off.
    const bool current = factory && factory->IsCurrent();
    if (!force && current && monitor == lastMonitor) return false;
    if (!current) {
        if (factory) factory->Release();
        factory = nullptr;
        if (auto create = create_factory())
            if (FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) factory = nullptr;
    }
    lastMonitor = monitor;
    Display d = factory && monitor ? describe(factory, monitor) : Display{};
    const bool changed = force || !same(d, out);
    out = std::move(d);
    return changed;
}

const char* color_space_name(unsigned c) {
    switch (c) {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "RGB_FULL_G22_NONE_P709 (SDR)";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "RGB_FULL_G10_NONE_P709 (scRGB)";
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "RGB_FULL_G2084_NONE_P2020 (HDR)";
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020: return "RGB_FULL_G22_NONE_P2020";
    default: return "other";
    }
}
}  // namespace windows_hdr
