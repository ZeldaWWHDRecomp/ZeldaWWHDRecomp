#include "settings.h"
#include "runtime.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include "../../interp.h"
#include "gx2/gx2.h"
namespace gfxvk {
namespace {
int normalize(int v) { return (v % 3 + 3) % 3; }
struct Settings {
    std::atomic<int> ao{std::getenv("WWHD_AO_MODE") ? normalize(std::atoi(std::getenv("WWHD_AO_MODE"))) : std::getenv("WWHD_NO_AO_QUIRK") ? 0 : 2};
    // full-size occlusion depth: on by default, off on Android, where the phone GPU is the limit in heavy views
    // (issue #56: halves the worst GPU wait on an Adreno 830); a saved choice or WWHD_AO_HIRES wins
#ifdef __ANDROID__
    static constexpr bool kHiresDefault = false;
#else
    static constexpr bool kHiresDefault = true;
#endif
    std::atomic<bool> hires{std::getenv("WWHD_AO_HIRES") ? std::atoi(std::getenv("WWHD_AO_HIRES")) != 0 : kHiresDefault};
    std::atomic<bool> aniso{std::getenv("WWHD_ANISO") && std::atoi(std::getenv("WWHD_ANISO")) != 0};
    std::atomic<bool> fxaa{std::getenv("WWHD_FXAA") && std::atoi(std::getenv("WWHD_FXAA")) != 0};
    std::atomic<int> filter{[] { const char* e = std::getenv("WWHD_SCALE_FILTER"); return e && !std::strcmp(e,"sharp") ? 1 : e && !std::strcmp(e,"integer") ? 2 : 0; }()};
    std::atomic<bool> available[static_cast<int>(GraphicsFeature::Count)]{};
};
Settings& settings() { static Settings s; return s; }
int env_present_mode() {
    const char* e = std::getenv("WWHD_VK_PRESENT_MODE");
    if (!e || !*e) return -1;
    return !std::strcmp(e, "mailbox") ? kPresentMailbox : !std::strcmp(e, "immediate") ? kPresentImmediate : kPresentFifo;
}
std::atomic<int> g_present{env_present_mode() >= 0 ? env_present_mode() : kPresentFifo};
std::atomic<unsigned> g_offered{1u << kPresentFifo};  // FIFO is always available
}
int present_mode() { return g_present.load(std::memory_order_relaxed); }
void set_present_mode(int m) {
    if (m < 0 || m >= kPresentModes) m = kPresentFifo;
    if (g_present.exchange(m) != m) LOG("[vulkan] present mode %s requested", present_mode_name(m));
}
bool present_mode_from_env() { return env_present_mode() >= 0; }
bool present_mode_offered(int m) { return m >= 0 && m < kPresentModes && (g_offered.load() >> m & 1); }
void set_present_modes_offered(unsigned mask) { g_offered = mask | 1u << kPresentFifo; }
int effective_present_mode() {
    if (!gx2::uncapped()) return present_mode();
    return present_mode_offered(kPresentImmediate) ? kPresentImmediate : present_mode_offered(kPresentMailbox) ? kPresentMailbox : kPresentFifo;
}
const char* present_mode_name(int m) { return m == kPresentMailbox ? "mailbox" : m == kPresentImmediate ? "immediate" : "fifo"; }
int ao_mode() { return settings().ao.load(std::memory_order_relaxed); }
void set_ao_mode(int v) { settings().ao.store(normalize(v),std::memory_order_relaxed); LOG("[gfx] AO mode %d",ao_mode()); }
bool ao_hires_enabled() { return settings().hires.load(std::memory_order_relaxed); }
void set_ao_hires(bool v) { settings().hires.store(v,std::memory_order_relaxed); LOG("[gfx] AO full-size depth %s",v?"on":"off"); }
bool aniso_enabled() { return settings().aniso.load(std::memory_order_relaxed); }
void set_aniso(bool v) { settings().aniso.store(v,std::memory_order_relaxed); LOG("[gfx] anisotropy %s",v?"on":"off"); }
bool fxaa_enabled() { return settings().fxaa.load(std::memory_order_relaxed); }
void set_fxaa(bool v) { settings().fxaa.store(v,std::memory_order_relaxed); LOG("[gfx] FXAA %s",v?"on":"off"); }
int scale_filter() { return settings().filter.load(std::memory_order_relaxed); }
void set_scale_filter(int v) { settings().filter.store(normalize(v),std::memory_order_relaxed); }
bool graphics_feature_available(GraphicsFeature f) { int i=static_cast<int>(f); return i>=0 && i<static_cast<int>(GraphicsFeature::Count) && settings().available[i].load(std::memory_order_relaxed); }
void set_graphics_feature_available(GraphicsFeature f,bool v) { int i=static_cast<int>(f); if(i>=0 && i<static_cast<int>(GraphicsFeature::Count)) settings().available[i].store(v,std::memory_order_relaxed); }
bool graphics_hotkey(char k,bool activate) {
    GraphicsFeature f;
    switch(k) {
    case 'R': if(activate) { constexpr float scales[]={1,1.5f,2,3}; float cur=requested_res_scale(); unsigned next=0; for(unsigned i=0;i<4;++i) if(cur<scales[i]-0.01f) { next=i; break; } set_res_scale(scales[next]); } return true;
    case '6': if(activate) interp::toggle_fps(60); return true;  // 60 fps frame interpolation on/off
    case '7': if(activate) interp::set_mode(interp::mode()==2?0:2); return true;
    case 'O': f=GraphicsFeature::AO; break;
    case 'M': f=GraphicsFeature::AOHires; break;
    case 'N': f=GraphicsFeature::Anisotropy; break;
    case '8': f=GraphicsFeature::FXAA; break;
    default: return false;
    }
    if(activate && graphics_feature_available(f)) switch(k) {
    case 'O': set_ao_mode(ao_mode()+1); break;
    case 'M': set_ao_hires(!ao_hires_enabled()); break;
    case 'N': set_aniso(!aniso_enabled()); break;
    case '8': set_fxaa(!fxaa_enabled()); break;
    }
    return true;
}
}
