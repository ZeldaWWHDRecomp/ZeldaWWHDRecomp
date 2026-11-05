// Turnip (Mesa's Adreno Vulkan driver) settings chosen for the GPU before a custom driver loads.
//
// On Adreno 6xx Turnip runs without UBWC (bandwidth compression): with it, Turnip 26.0 to 26.3 above 1x
// resolution restarted a Galaxy S20 FE (Adreno 650, kernel 4.19) at the first frame (a fatal bus error,
// TZBSP_ERR_FATAL_NOC_ERROR); without it 1.5x runs and 1x is as fast (18.2 against 19.5 ms of GPU time per
// frame there). Other Adrenos keep Turnip's defaults. A TU_DEBUG the player set (wwhd.env) wins.
//
// From Nycolazs/WindWakerHD-Android-Plus d2ef429 and b3db777 via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0 (there in MainActivity.java / GpuDrivers.java; here before the driver's dlopen).
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace gfxvk::drivers {
// a Turnip build: by the package's vendor, the usual library name or the package name
inline bool is_turnip(std::string_view library, std::string_view vendor, std::string_view name) {
    auto lower = [](std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return out;
    };
    return library.find("freedreno") != std::string_view::npos || lower(vendor) == "mesa" ||
           lower(name).find("turnip") != std::string::npos;
}
// the kernel's GPU model ("Adreno650v2"): an Adreno 6xx
inline bool adreno_6xx(std::string_view gpuModel) { return gpuModel.substr(0, 7) == "Adreno6"; }
// the TU_DEBUG to set before Turnip loads, "" for none. userSet: TU_DEBUG is in the environment already
inline std::string tu_debug_for(std::string_view gpuModel, bool turnip, bool userSet) {
    if (!turnip || userSet) return {};
    return adreno_6xx(gpuModel) ? "noubwc" : "";
}
}  // namespace gfxvk::drivers
