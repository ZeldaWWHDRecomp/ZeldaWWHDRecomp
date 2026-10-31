#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string_view>

// Hosts with HDR output (Vulkan, SDL host): Windows and desktop Linux.
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#define WWHD_HDR_OUTPUT 1
#endif

// Explicit HDR output: scRGB FP16, or HDR10 PQ (BT.2020 + ST 2084).
// Auto prefers scRGB; a saved preference can select either supported encoding. The guest scan is SDR; this is a controlled highlight
// expansion, not recovery of the game's pre-tonemap lighting.
namespace hdr {
// Saved integer values are stable; invalid settings use the defaults.
enum class SdrCurve { Gamma22, Gamma24, Srgb };
enum class Format { Auto, Scrgb, Pq };
enum class Output { Sdr, Scrgb, Pq };
inline std::atomic<int> sdrCurve{0}, format{0}, activeFormatPreference{0};
inline int setting(std::string_view value) {
    return value=="1" ? 1 : value=="2" ? 2 : 0;
}
// Pure surface-selection policy, shared with the exhaustive unit test.
inline Output choose_format(Format preference, bool scrgb, bool pq) {
    if (preference==Format::Pq && pq) return Output::Pq;
    if (scrgb) return Output::Scrgb;
    if (pq) return Output::Pq;
    return Output::Sdr;
}
inline float decode(float c, SdrCurve curve) {
    c=std::max(c,0.0f);
    if(curve==SdrCurve::Gamma22) return std::pow(c,2.2f);
    if(curve==SdrCurve::Gamma24) return std::pow(c,2.4f);
    c=std::min(c,1.0f);
    return c<=0.04045f ? c/12.92f : std::pow((c+0.055f)/1.055f,2.4f);
}
inline std::array<float,3> decode(std::array<float,3> c, SdrCurve curve) {
    for(auto& v:c) v=decode(v,curve);
    return c;
}
inline std::atomic<bool> enabled{false}, displayEnabled{false}, formatSupported{false};
inline std::atomic<bool> fellBackToSdr{false};
// Per-encoding surface support (formatSupported is either); Auto prefers scRGB.
inline std::atomic<bool> scrgbSupported{false}, pqSupported{false};
inline std::atomic<bool> pqOutput{false}; // the TV swapchain presents HDR10 PQ
// Linux: SDL chose XWayland in a Wayland session. X11 surfaces are SDR only; a saved
// HDR choice makes the next start use SDL's Wayland driver (backend.cpp init).
inline std::atomic<bool> waylandAfterRestart{false};
// Whether HDR needs SDL's Wayland driver instead of XWayland. Not when the user chose a video
// driver, outside Wayland, or under gamescope (Steam Deck), which gives X11 surfaces HDR itself
// and whose frame pacing must not change.
inline bool wayland_for_hdr(bool driverChosen, bool waylandSession, bool gamescope) {
    return !driverChosen && waylandSession && !gamescope;
}
// The saved choice stays HDR; this session presents SDR and the overlay says so.
inline void fall_back_to_sdr() {
    enabled = false;
    fellBackToSdr = true;
}
inline bool resolve_output() {
    if (!enabled.load()) return false;
    if (displayEnabled.load() && formatSupported.load()) {
        fellBackToSdr = false;
        return true;
    }
    fall_back_to_sdr();
    return false;
}
inline std::atomic<float> paperWhite{240}, peak{1025}, exposure{-0.32f}, highlights{0.32f}, menuWhite{160};
inline std::atomic<bool> peakAuto{false};
inline std::atomic<float> headroom{0}; // SDL ratio relative to SDR white; <=1 means unavailable
inline bool valid_headroom(float value) { return std::isfinite(value) && value>1.0f; }
inline float automatic_peak(float paper, float ratio) {
    // Bound implausible reports to PQ's absolute range. Missing headroom uses the old default.
    return valid_headroom(ratio) ? std::clamp(paper*ratio,paper,10000.0f) : 1025.0f;
}
inline bool automatic_peak_setting(bool hasMode, std::string_view mode) {
    return hasMode && mode=="1";
}
inline void set(std::atomic<float>& dst, float value, float lo, float hi) {
    if (std::isfinite(value)) dst = std::clamp(value,lo,hi);
}
struct Parameters { float paper, peak, exposure, highlights; };
inline Parameters parameters() {
    const float paper=paperWhite.load();
    const float maxNits=peakAuto.load() ? automatic_peak(paper,headroom.load()) : peak.load();
    return {std::min(paper,maxNits),maxNits,exposure.load(),highlights.load()};
}
// Max-channel scaling preserves hue, including saturated highlights; zero stays
// zero. The smooth shoulder limits all RGB channels to the selected peak.
inline std::array<float,3> map(std::array<float,3> c, Parameters p) {
    for(auto& v:c) v=std::max(0.0f,v)*std::exp2(p.exposure);
    const float m=std::max({c[0],c[1],c[2]});
    if(m<=0) return {};
    const float t=std::clamp((m-0.5f)*2.0f,0.0f,1.0f);
    float n=m*p.paper+(p.peak-p.paper)*p.highlights*t*t*(3-2*t);
    const float knee=std::min(p.paper,p.peak*0.75f);
    if(n>knee) n=knee+(p.peak-knee)*(1-std::exp(-(n-knee)/(p.peak-knee)));
    for(auto& v:c) v*=n/(m*80.0f); // Windows scRGB: 1.0 = 80 cd/m^2.
    return c;
}
// Static HDR metadata (VK_EXT_hdr_metadata) for the mapped picture. Without it a compositor must guess the
// content's range: KWin clamps scRGB at 80 cd/m^2 and tone maps HDR10 as 10000 cd/m^2 content. The peak
// slider is the mastering maximum, not necessarily the content maximum. Estimate the latter from
// mapped SDR white and the overlay white, so compositors don't tone map unused headroom. The guest
// picture is BT.709 (mastering primaries, set by the caller).
struct Metadata { float maxCll, maxFall, maxMastering, minMastering; };
inline Metadata metadata(Parameters p, float menuWhite) {
    const float white=map({1,1,1},p)[0]*80.0f;
    const float maxCll=std::max(white,std::min(menuWhite,p.peak));
    return {maxCll,std::min(white,p.peak),p.peak,0.0f};
}
inline bool operator==(const Metadata& a, const Metadata& b) {
    return a.maxCll==b.maxCll && a.maxFall==b.maxFall && a.maxMastering==b.maxMastering && a.minMastering==b.minMastering;
}
// HDR10 from map()'s scRGB values: BT.709 -> BT.2020 primaries, then the
// ST 2084 inverse EOTF of absolute luminance (1.0 = 10000 cd/m^2).
inline float pq(float nits) {
    const float m1=0.1593017578125f, m2=78.84375f, c1=0.8359375f, c2=18.8515625f, c3=18.6875f;
    const float y=std::pow(std::clamp(nits/10000.0f,0.0f,1.0f),m1);
    return std::pow((c1+c2*y)/(1+c3*y),m2);
}
inline std::array<float,3> encode_pq(std::array<float,3> scrgb) {
    const auto& c=scrgb;
    std::array<float,3> bt2020{0.6274040f*c[0]+0.3292820f*c[1]+0.0433136f*c[2],
                               0.0690970f*c[0]+0.9195400f*c[1]+0.0113612f*c[2],
                               0.0163916f*c[0]+0.0880132f*c[1]+0.8955950f*c[2]};
    for(auto& v:bt2020) v=pq(v*80.0f);
    return bt2020;
}
}
