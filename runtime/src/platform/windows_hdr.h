#pragma once
// Windows: the HDR state of the monitor showing a window, read from DXGI (issue #593). SDL's
// HDR flag is not this: it is set only when the monitor's reported peak is above the Windows
// SDR content brightness, and it is not updated when the window moves to another monitor.
// The decision from these values is hdr::dxgi_display_state / hdr::windows_display (gfx/hdr.h).
#ifdef _WIN32
#include <string>

namespace windows_hdr {
struct Display {
    bool found = false;       // a DXGI output (IDXGIOutput6) matched the window's monitor
    unsigned colorSpace = 0;  // DXGI_COLOR_SPACE_TYPE; 12 (G2084 P2020) while Windows HDR is on
    unsigned bitsPerColor = 0;
    float maxLuminance = 0, maxFullFrameLuminance = 0, minLuminance = 0;  // nits, as reported
    float sdrWhite = 0;       // the Windows SDR content brightness in nits; 0 when unavailable
    std::string device;       // GDI name, e.g. \\.\DISPLAY1
    std::string monitor;      // the monitor's friendly name, when Windows knows it
    std::string adapter;
};
// Main thread. Re-reads DXGI when forced, when the window is on another monitor than at the
// last read, or when the DXGI factory is no longer current (display configuration or HDR mode
// changed); cheap otherwise, so it can be polled. Returns true when forced or when `out` changed.
bool refresh(void* hwnd, bool force, Display& out);
const char* color_space_name(unsigned colorSpace);
}
#endif
