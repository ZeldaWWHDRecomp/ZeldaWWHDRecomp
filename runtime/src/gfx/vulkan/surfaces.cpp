#include "background_shaders.h"
#include "pass.h"
#include "deferred_dispatch.h"
#include "../../exception_report.h"
#include "gfx/render_mips.h"
#include "gfx/texture_levels.h"
#include "gfx/artistic_mips.h"
#include "texture_budget.h"
#include "bc_decode.h"
#include "mods/cemu_pack.h"
// Guest surfaces backed by Vulkan images. LatteAddrLib supplies guest tiling geometry.
#include "backend.h"
#include "stereo.h"
#include "attachment_view_usage.h"
#include "pipeline_trace.h"
#include "buffer_cache.h"
#include "render_prof.h"
#include "settings.h"
#include "shaders.h"
#include "sparse_hash_memo.h"
#include "write_watch.h"
#include "interval_index.h"
#define XXH_INLINE_ALL
#include "../../../third_party/xxhash/xxhash.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "gx2/gx2.h"
#include "gx2_texture_regs.h"
#include "runtime.h"
#include "dynres.h"
#include <algorithm>
#include <numeric>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

Latte::E_GX2SURFFMT LatteTexture_ReconstructGX2Format(const Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N&, const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N&);
namespace gfxvk {
// Guest layout is immutable between descriptor changes. Invalidation ranges,
// sparse checks and uploads share this metadata; image scale/layout are unrelated.
struct CachedGuestLevel {
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    uint32_t address = 0, swizzle = 0;  // address and swizzle valid with addressValid
    bool infoValid = false, addressValid = false;
    uint64_t rangeBegin = 0, rangeEnd = 0;
    bool rangeValid = false;
};
struct CachedGuestLayout {
    std::array<uint32_t, 10> descriptor{};
    std::vector<CachedGuestLevel> levels;
    uint64_t mipRangeBegin = UINT64_MAX, mipRangeEnd = 0;
    uint32_t mipRangesCached = 0;
    bool mipRangesComplete = false;
};
static CachedGuestLevel& guest_level(const Surface* s, uint32_t level) {
    std::array<uint32_t, 10> descriptor{s->addr,s->mipAddr,s->width,s->height,s->slices,
        s->format,s->dim,s->tileMode,s->swizzle,s->mips};
    // Surface copies may share unchanged guest geometry. Descriptor changes
    // get a fresh cache, preserving metadata belonging to the original surface.
    if (!s->guestLayout || s->guestLayout->descriptor != descriptor ||
        s->guestLayout->levels.empty()) {
        auto layout = std::make_shared<CachedGuestLayout>();
        layout->descriptor = descriptor;
        layout->levels.resize(s->mips);
        s->guestLayout = std::move(layout);
    }
    if (level >= s->guestLayout->levels.size()) exception_report::raise("GX2 mip level exceeds surface");
    return s->guestLayout->levels[level];
}
static const LatteAddrLib::AddrSurfaceInfo_OUT& guest_info(const Surface* s, uint32_t level) {
    auto& cached = guest_level(s, level);
    if (!cached.infoValid) {
        LatteAddrLib::GX2CalculateSurfaceInfo(static_cast<Latte::E_GX2SURFFMT>(s->format),
            s->width,s->height,s->slices,static_cast<Latte::E_DIM>(s->dim),
            Latte::MakeGX2TileMode(static_cast<Latte::E_HWTILEMODE>(s->tileMode)),0,level,&cached.info);
        cached.infoValid = true;
    }
    return cached.info;
}
static void check_vk(VkResult result, const char* what) {
    if (result != VK_SUCCESS) exception_report::raise(std::string("Vulkan surfaces: ")+what+" failed ("+std::to_string(result)+")");
}
static uint64_t content_hash(const uint8_t* p, size_t n) {
    // Full-byte coverage, including unaligned guest ranges and the final tail.
    // Only transient surface hashes use XXH3; disk cache checksums stay stable.
    return XXH3_64bits(p, n);
}
uint64_t next_write_seq() { static uint64_t seq=0; return ++seq; }
static float parse_scale(const char* e) {
    float f = e ? (float)atof(e) : 1.0f;
    return std::clamp(f > 0 ? f : 1.0f, 1.0f, 4.0f);
}
static std::atomic<float> g_res_requested{parse_scale(getenv("WWHD_RES_SCALE"))};
static float g_res_frame = g_res_requested.load();  // render thread: the factor for this frame
static void latch_aspect();
// dynamic resolution (dynres.h): render thread, the factor of the internal resolution for this frame.
// Exactly 1 while off, so every size comes out as before.
static float g_dyn_frame = 1.0f;
static uint64_t g_spare_images = 0;  // images kept in Surface::spares (all surfaces)
static void release_spares(Surface* s);
static bool g_in_precreate=false;  // views made by precreate_levels (debug log WWHD_DYNRES_LOG_VIEWS: the others)
static void log_view(const Surface* s,const char* kind) {
    static const bool on=getenv("WWHD_DYNRES_LOG_VIEWS")!=nullptr;
    if(on&&!g_in_precreate&&dynres::on())
        LOG("[dynres] view created at use: %s %08X %ux%u fmt %X image %ux%u",kind,s->addr,s->width,s->height,s->format,s->extent.width,s->extent.height);
}
static void warm_dynamic_resolution();
static void precreate_levels();
static void latch_dynamic_resolution() {
    g_dyn_frame = dynres::latch(R.frame, g_res_frame);
    if (dynres::on()) { warm_dynamic_resolution(); precreate_levels(); }
    // off: the images kept for scale changes go (a target not drawn any more keeps its own until then)
    if (!dynres::on() && g_spare_images)
        for (auto& [addr, s] : R.surfaces) release_spares(s.get());
}
float res_scale() { return g_res_frame; }
float requested_res_scale() { return g_res_requested.load(std::memory_order_relaxed); }
void set_res_scale(float f) {
    g_res_requested = std::clamp(std::isfinite(f) ? f : 1.0f, 1.0f, 4.0f);
    LOG("[gfx] internal resolution %gx", g_res_requested.load());
}
void latch_res_scale() {
    // test aid: WWHD_RES_SCALE_AT=frame:factor,... switches the factor at those frames
    static std::vector<std::pair<uint64_t, float>> at = [] {
        std::vector<std::pair<uint64_t, float>> v;
        if (const char* e = getenv("WWHD_RES_SCALE_AT"))
            for (char* p = (char*)e; *p;) {
                uint64_t f = strtoull(p, &p, 10);
                if (*p++ != ':') break;
                v.push_back({f, (float)strtod(p, &p)});
                while (*p == ',') p++;
            }
        return v;
    }();
    for (auto& [f, v] : at)
        if (R.frame == f) set_res_scale(v);
    g_res_frame = g_res_requested.load(std::memory_order_relaxed);
    latch_aspect();
    latch_dynamic_resolution();
}

// ---------------------------------------------------------------- aspect ratio
// As the Metal renderer (metal_surfaces.mm): at another aspect ratio (aspect.cpp) the game's
// projections are widened (or made taller) and every screen-shaped render target is allocated that
// much wider (taller) than its guest size: draws keep their guest viewports, which cover the whole
// image, so the picture comes out at the new shape. kx/ky: image pixels per guest pixel on top of
// the internal resolution, (A / (16/9), 1) for wider screens, (1, (16/9) / A) for narrower ones.
// Latched at the frame boundary like the resolution.
static std::atomic<float> g_aspect_requested{16.0f / 9.0f};
static float g_aspect_kx = 1.0f, g_aspect_ky = 1.0f;  // render thread: factors for this frame
void set_frame_aspect(float a) { g_aspect_requested.store(a, std::memory_order_relaxed); }
static void latch_aspect() {
    float a = g_aspect_requested.load(std::memory_order_relaxed), base = 16.0f / 9.0f;
    float kx = a >= base ? a / base : 1.0f, ky = a >= base ? 1.0f : base / a;
    if (kx != g_aspect_kx || ky != g_aspect_ky) LOG("[gfx] aspect %.4f: screen targets x%.4f wide, x%.4f tall", a, kx, ky);
    g_aspect_kx = kx;
    g_aspect_ky = ky;
}
// the game's screen-sized buffers and their reductions (1920x1080 ... 60x33); not the GamePad's
// (854x480, shown in its own window), not shadow maps, mip chains or textures
static bool screen_shaped(uint32_t width, uint32_t height, bool compressed, uint32_t mips, uint32_t slices) {
    if (compressed || mips > 1 || slices > 1 || width < 32) return false;
    for (uint32_t w = 854, h = 480; w >= 32; w >>= 1, h >>= 1)
        if ((width == w || width == w + 1) && height == h) return false;
    float r = (float)width * 9.0f / ((float)height * 16.0f);
    return r > 0.97f && r < 1.03f;
}
bool stereo_screen_target(uint32_t w, uint32_t h) { return screen_shaped(w, h, false, 1, 1); }
enum class ScaledCopy;
static ScaledCopy scaled_copy_mode(const FormatInfo& fmt);
// Half SBS and top-and-bottom eyes are smaller than the guest's surfaces, so CPU uploads into them
// are scaled (upload_surface -> resample). A format this device can neither blit nor draw would be
// cleared there instead of uploaded: then every eye keeps the full size, as in Full SBS, for the rest
// of the session (all of a pass's attachments must keep one size).
static bool g_eyeSizedTargets = true;
static bool eye_sized(const FormatInfo& fmt);
bool target_aspect_factors(uint32_t w, uint32_t h, float& kx, float& ky) {
    bool on = screen_shaped(w, h, false, 1, 1) && (g_aspect_kx != 1.0f || g_aspect_ky != 1.0f);
    kx = on ? g_aspect_kx : 1.0f;
    ky = on ? g_aspect_ky : 1.0f;
    return on;
}
static void target_aspect(const Surface* s, float& kx, float& ky, bool eyeSize = true) {
    uint32_t width,height;
    if(!s->fmt.compressed&&s->mips==1&&mods::cemu::texture_extent(s->width,s->height,s->format,s->slices,s->tileMode,width,height)){
        kx=float(width)/s->width;ky=float(height)/s->height;return;
    }
    bool on = screen_shaped(s->width, s->height, s->fmt.compressed, s->mips, s->slices);
    kx = on ? g_aspect_kx : 1.0f;
    ky = on ? g_aspect_ky : 1.0f;
    // Half SBS and top-and-bottom show each eye at half the width (height): render it at that size
    // rather than drawing twice the pixels and squeezing half of them away. Allocation only: the
    // projections and layouts keep the picture's shape (target_aspect_factors).
    const bool halved = stereo::frame.mode == stereo::HalfSBS || stereo::frame.mode == stereo::TAB;
    if (!eyeSize || !on || !halved || !eye_sized(s->fmt)) return;
    if (stereo::frame.mode == stereo::HalfSBS) kx *= 0.5f;
    else ky *= 0.5f;
}
// The current allocation of s is an eye-sized (halved) stereo target: its scan is shown at twice
// that width (Half SBS) or height (top-and-bottom).
void stereo_target_halves(const Surface* s, bool& halfWidth, bool& halfHeight) {
    float kx, ky;
    target_aspect(s, kx, ky, false);
    halfWidth = s->ax * 2 == kx && kx != 0;
    halfHeight = s->ay * 2 == ky && ky != 0;
}

// the factor a render target gets. Shadow maps (depth arrays: the game's cascades) scale with the
// internal resolution by default. WWHD_SHADOW_SCALE=n gives them their own factor; =1 keeps the
// console's 1024x1024, which uses far less GPU memory at 2x/3x. Issue #67: the hard, crawling
// shadow edges at 2x in v0.2.6-v0.2.8 came mainly from the missing mip chains the game's
// shadow-mask softening samples (restored in v0.2.9); since then both sizes give practically the
// same soft edges, the larger maps only a hair crisper.
static float target_scale(const Surface* s) {
    // the bloom chain: at the console's size its blur and the final bilinear upscale blend the glow
    // smoothly; at the internal resolution the lumps of the blur's sparse taps showed as blocks (#307)
    if (s->sizeClaim.size == gfx::TargetSize::Console) return 1.0f;
    uint32_t width,height;
    if(!s->fmt.compressed&&s->mips==1&&mods::cemu::texture_extent(s->width,s->height,s->format,s->slices,s->tileMode,width,height))return 1.0f;
    if (s->fmt.compressed || s->mips > 1) return 1.0f;
    static const float shadow = getenv("WWHD_SHADOW_SCALE") ? parse_scale(getenv("WWHD_SHADOW_SCALE")) : 0.0f;
    static const bool shadow_logged = [] {  // issue #67: show that the switch was picked up
        if (shadow) LOG("[gfx] WWHD_SHADOW_SCALE=%g: shadow maps at %gx the console's 1024x1024", shadow, shadow);
        return true;
    }();
    (void)shadow_logged;
    if (shadow && s->isDepth && s->slices > 1) return shadow;
    // dynamic resolution: single-layer targets (the screen's buffers and the GamePad's); the shadow
    // cascades (arrays) keep the internal resolution
    if (s->slices > 1) return res_scale();
    return res_scale() * g_dyn_frame;
}
// the dynamic resolution factor in target_scale's result (1 where it does not apply)
static float target_dyn(const Surface* s) {
    if (g_dyn_frame == 1.0f || s->sizeClaim.size != gfx::TargetSize::Internal || s->fmt.compressed || s->mips > 1 || s->slices > 1) return 1.0f;
    uint32_t width,height;
    if(mods::cemu::texture_extent(s->width,s->height,s->format,s->slices,s->tileMode,width,height))return 1.0f;
    return g_dyn_frame;
}
float nominal_width(const Surface* s) { return s ? float(s->extent.width) / s->dyn : 0.0f; }
float nominal_height(const Surface* s) { return s ? float(s->extent.height) / s->dyn : 0.0f; }


// ---------------------------------------------------------------- render targets
// CB_COLORn_BASE holds the full guest address; CB_COLORn_TILE/FRAG hold width/height (our convention).
constexpr uint32_t kDim2D = 1, kDim3D = 2, kDim2DArray = 5;

Surface* color_target(const uint32_t* regs, int i, uint32_t* slice, gfx::SizeRequest claim) {
    uint32_t base = regs[mmCB_COLOR0_BASE + i];
    if (!base) return nullptr;
    uint32_t size = regs[mmCB_COLOR0_SIZE + i], info = regs[mmCB_COLOR0_INFO + i];
    uint32_t pitch = ((size & 0x3FF) + 1) * 8;
    uint32_t height = (((size >> 10) & 0xFFFFF) + 1) * 64 / pitch;
    // our convention (GX2SetColorBuffer, gx2.h kColorTarget3D): TILE = width | slices << 16 | volume flag, FRAG = height
    uint32_t tile = regs[mmCB_COLOR0_TILE + i];
    uint32_t w = tile & 0xFFFF, h = regs[mmCB_COLOR0_FRAG + i];
    uint32_t slices = gx2::color_target_slices(tile);
    bool volume = (tile & gx2::kColorTarget3D) != 0;
    if (slice) *slice = slices > 1 ? std::min<uint32_t>(regs[mmCB_COLOR0_VIEW + i] & 0x7FF, slices - 1) : 0;
    static const uint32_t numberBits[8] = {0, 0x200, 0, 0, 0x100, 0x300, 0x400, 0x800};
    SurfaceDesc d;
    d.addr = base;
    d.width = w ? w : pitch;
    d.height = h ? h : height;
    d.pitch = pitch;
    d.format = ((info >> 2) & 0x3F) | numberBits[(info >> 12) & 7];
    d.tileMode = (info >> 8) & 0xF;
    d.slices = slices;
    if (volume && !R.imageView2DOn3DImage) {
        // the device cannot render into a volume slice (portability subset without imageView2DOn3DImage):
        // render into an array of the same size; sampling it as a volume then reads the guest memory
        static bool logged = false;
        if (!logged) LOG("[gfx] Vulkan: this device cannot render into 3D texture slices; volume render targets stay empty");
        logged = true;
        volume = false;
    }
    d.dim = volume ? kDim3D : slices > 1 ? kDim2DArray : kDim2D;
    d.sizeRequest = claim;
    return find_or_create_surface(d, true);
}

Surface* depth_target(const uint32_t* regs, uint32_t* slice) {
    uint32_t base = regs[mmDB_DEPTH_BASE];
    if (!base) return nullptr;
    uint32_t slices = std::max<uint32_t>(regs[gx2::kDepthSlicesReg], 1);
    if (slice) *slice = slices > 1 ? std::min<uint32_t>(regs[mmDB_DEPTH_VIEW] & 0x7FF, slices - 1) : 0;
    uint32_t size = regs[mmDB_DEPTH_SIZE], info = regs[mmDB_DEPTH_INFO];
    uint32_t pitch = ((size & 0x3FF) + 1) * 8;
    uint32_t height = (((size >> 10) & 0xFFFFF) + 1) * 64 / pitch;
    uint32_t wh = regs[mmDB_HTILE_DATA_BASE];  // our convention: width << 16 | height
    static const uint32_t fmts[8] = {0, 0x005, 0, 0x011, 0, 0x811, 0x80E, 0x81C};
    SurfaceDesc d;
    d.addr = base;
    d.width = wh ? (wh >> 16) : pitch;
    d.height = wh ? (wh & 0xFFFF) : height;
    d.pitch = pitch;
    d.format = fmts[info & 7];
    d.isDepth = true;
    d.slices = slices;
    d.dim = slices > 1 ? kDim2DArray : kDim2D;
    return find_or_create_surface(d, true);
}

Surface* surface_from_color_buffer(uint32_t addr, uint32_t* firstSlice, uint32_t* numSlices) {
    auto* cb = (GX2::GX2ColorBuffer*)mem::ptr(addr);
    SurfaceDesc d;
    bool volume = cb->surface.dim.value() == Latte::E_DIM::DIM_3D;
    uint32_t slices = cb->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(cb->surface.depth, 1)
                      : volume ? std::max<uint32_t>(cb->surface.depth >> cb->viewMip, 1) : 1;
    d.slices = slices;
    d.dim = volume ? kDim3D : slices > 1 ? kDim2DArray : kDim2D;
    if (firstSlice) *firstSlice = std::min<uint32_t>(cb->viewFirstSlice, slices - 1);
    if (numSlices) *numSlices = std::clamp<uint32_t>(cb->viewNumSlices, 1, slices - std::min<uint32_t>(cb->viewFirstSlice, slices - 1));
    d.addr = gx2::color_buffer_address(cb);
    d.width = std::max<uint32_t>(cb->surface.width >> cb->viewMip, 1);
    d.height = std::max<uint32_t>(cb->surface.height >> cb->viewMip, 1);
    d.pitch = cb->surface.pitch;
    d.format = (uint32_t)cb->surface.format.value();
    d.tileMode = (uint32_t)cb->surface.tileMode.value();
    return find_or_create_surface(d, true);
}

Surface* surface_from_depth_buffer(uint32_t addr, uint32_t* firstSlice, uint32_t* numSlices) {
    auto* db = (GX2::GX2DepthBuffer*)mem::ptr(addr);
    SurfaceDesc d;
    uint32_t slices = db->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(db->surface.depth, 1) : 1;
    d.slices = slices;
    d.dim = slices > 1 ? kDim2DArray : kDim2D;
    if (firstSlice) *firstSlice = std::min<uint32_t>(db->viewFirstSlice, slices - 1);
    if (numSlices) *numSlices = std::clamp<uint32_t>(db->viewNumSlices, 1, slices - std::min<uint32_t>(db->viewFirstSlice, slices - 1));
    d.addr = db->surface.imagePtr;
    d.width = db->surface.width;
    d.height = db->surface.height;
    d.pitch = db->surface.pitch;
    d.format = (uint32_t)db->surface.format.value();
    d.tileMode = (uint32_t)db->surface.tileMode.value();
    d.isDepth = true;
    return find_or_create_surface(d, true);
}

// ---------------------------------------------------------------- sampled textures
static uint64_t sparse_hash(Surface* s);
uint64_t g_stat_full_checks, g_stat_uploads, g_stat_invalidates, g_stat_invalidated_surfaces;
uint64_t g_surface_epoch = 1;  // backend.h

// Based on GreenNaugahyde/ZeldaWWHDRecompAndroid, commit 73b54e1 (rendered mip chains); the
// sampler-reach limit and the per-level rebuild are from its v0.9.5 (b1993cf).
static Surface* with_mip_chain(Surface* s, const SurfaceDesc& d, uint32_t maxLevels) {
    static const bool off = getenv("WWHD_NO_RT_MIPS") != nullptr;
    uint32_t mips = d.mips;
    if (off || mips <= 1 || s->fmt.depth || s->fmt.compressed || s->fmt.kind != FormatInfo::FLOAT || s->imageType != VK_IMAGE_TYPE_2D || s->arrayLayers != 1 ||
        s->mips != 1 || !s->image)
        return s;
    const uint32_t w = s->extent.width, h = s->extent.height;
    uint32_t full = 1;
    while ((std::max(w, h) >> full) > 0) full++;
    mips = std::min(mips, full);
    if (mips <= 1) return s;
    // Only the levels the sampler reaches are built (its MAX_LOD plus one: linear mip filtering at a
    // whole LOD may still fetch the next level at weight 0). Each level is a blit and a barrier; tiled
    // GPUs drain on every one. The chain image keeps all levels (another sampler may reach further).
    // WWHD_RT_MIPS_ALL=1: every level, as before.
    static const bool allLevels = getenv("WWHD_RT_MIPS_ALL") != nullptr;
    const uint32_t need = allLevels ? mips : std::min(mips, std::max(maxLevels, 1u));
    static std::unordered_map<VkFormat, bool> blittable;
    auto bl = blittable.find(s->fmt.pixel);
    if (bl == blittable.end()) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(R.physicalDevice, s->fmt.pixel, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        bl = blittable.emplace(s->fmt.pixel, (fp.optimalTilingFeatures & need) == need).first;
    }
    if (!bl->second) return s;
    // the game's own levels, and per level a key of what it is built from: the picture (level 0),
    // the game's own level, or the level below. The game renders its own downsampling chain a level
    // at a time and samples after each: only the levels from the first changed one on are built again.
    // WWHD_RT_MIPS_WHOLE=1: every chain from level 0, as before.
    Surface* levels[16] = {};
    uint64_t keys[16] = {};
    keys[0] = s->writeSeq * 0x9E3779B97F4A7C15ull + 1;
    for (uint32_t l = 1; l < mips && l < 16; l++) {
        levels[l] = ::gfx::render_mips::game_level(d, l, R.surfaces);
        keys[l] = levels[l] ? (levels[l]->writeSeq + 1) * 0xC2B2AE3D27D4EB4Full ^ l : (keys[l - 1] ^ l) * 0xFF51AFD7ED558CCDull;
    }
    Surface* c = s->mipChain.get();
    if (c && (c->mips != mips || c->extent.width != w || c->extent.height != h || c->fmt.pixel != s->fmt.pixel)) {
        destroy_surface_image(c);
        s->mipChain.reset();
        c = nullptr;
    }
    if (!c) {
        auto chain = std::make_shared<Surface>();
        c = chain.get();
        c->addr = s->addr; c->mipAddr = d.mipAddr;
        c->width = s->width; c->height = s->height; c->slices = 1;
        c->mips = mips; c->format = s->format; c->dim = s->dim; c->fmt = s->fmt;
        c->gpuWritten = true;
        create_surface_image(c, false, s->extent);
        s->mipChain = std::move(chain);
        s->mipChainLevels = 0;
    }
    static const bool whole = getenv("WWHD_RT_MIPS_WHOLE") != nullptr;
    uint32_t from = 0;  // the first level to build
    while (from < need && from < s->mipChainLevels && s->mipChainKey[from] == keys[from]) from++;
    if (from >= need) return c;
    if (whole) from = 0;
    for (uint32_t l = from; l < need; l++) s->mipChainKey[l] = keys[l];
    s->mipChainLevels = need;
    if (from == 0) transition_image(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    for (uint32_t l = std::max(from, 1u); l < need && l < 16; l++)
        if (levels[l]) transition_image(levels[l], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    // all levels in TRANSFER_DST: the image's layout is tracked as one, so the kept levels go from
    // TRANSFER_SRC to TRANSFER_DST too (a layout transition keeps their contents)
    transition_image(c, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkCommandBuffer cmd = command_buffer();
    if (from == 0) {
        VkImageCopy cp{};
        cp.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.extent = {w, h, 1};
        record::vkCmdCopyImage(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    }
    auto to_src = [&](uint32_t level) {  // a finished level becomes the next blit's source
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = c->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
        record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    if (from >= 2) {  // the kept levels below the one the first blit reads: back to transfer sources
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = c->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, from - 1, 0, 1};
        record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    uint32_t fromGame = 0;
    for (uint32_t l = std::max(from, 1u); l < need; l++) {
        to_src(l - 1);
        VkImageBlit b{};
        b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        b.dstOffsets[1] = {(int32_t)std::max(w >> l, 1u), (int32_t)std::max(h >> l, 1u), 1};
        Surface* g = l < 16 ? levels[l] : nullptr;
        if (g) {  // the game's own level (blitted: its image size may differ by rounding)
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            b.srcOffsets[1] = {(int32_t)g->extent.width, (int32_t)g->extent.height, 1};
            record::vkCmdBlitImage(cmd, g->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &b, VK_FILTER_LINEAR);
            fromGame++;
        } else {
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 0, 1};
            b.srcOffsets[1] = {(int32_t)std::max(w >> (l - 1), 1u), (int32_t)std::max(h >> (l - 1), 1u), 1};
            record::vkCmdBlitImage(cmd, c->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &b, VK_FILTER_LINEAR);
        }
    }
    to_src(need - 1);
    if (need < mips) {  // the levels not built: the same layout as the rest (one layout per image)
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = c->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, need, mips - need, 0, 1};
        record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    // Per-level transitions above order writes, and the generated levels are
    // also blit sources. Preserve these reads for the next whole-image write.
    derive_dependency(c->use, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, true);
    c->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;  // every level is now a transfer source
    // Another unit (or the vertex stage) may already have bound a source for
    // sampling in this draw. Chain assembly must preserve those read layouts.
    constexpr auto shaderStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (from == 0) transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, shaderStages, VK_ACCESS_SHADER_READ_BIT);
    for (uint32_t l = std::max(from, 1u); l < need && l < 16; ++l)
        if (levels[l]) transition_image(levels[l], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        shaderStages, VK_ACCESS_SHADER_READ_BIT);
    static int logged = 0;
    if (logged++ < 8)
        LOG("[gfx] mip chain for %08X %ux%u: levels %u-%u of %u built, %u of them the game's own", s->addr, w, h, from, need - 1, mips,
            fromGame);
    return c;
}

Surface* sampled_texture(const uint32_t* w, bool isDepthSampler, uint32_t maxLevels) {
    Latte::LATTE_SQ_TEX_RESOURCE_WORD0_N w0;
    Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N w1;
    Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N w4;
    Latte::LATTE_SQ_TEX_RESOURCE_WORD5_N w5;
    memcpy(&w0, &w[0], 4);
    memcpy(&w1, &w[1], 4);
    memcpy(&w4, &w[4], 4);
    memcpy(&w5, &w[5], 4);
    uint32_t addr = w[2] << 8, mipAddr = w[3] << 8;
    if (!addr) return nullptr;
    auto dim = w0.get_DIM();
    uint32_t pitch = (w0.get_PITCH() + 1) << 3;
    uint32_t width = w0.get_WIDTH() + 1;
    uint32_t height = w1.get_HEIGHT() + 1;
    uint32_t depth = w1.get_DEPTH();
    if (dim == Latte::E_DIM::DIM_2D_ARRAY || dim == Latte::E_DIM::DIM_3D || dim == Latte::E_DIM::DIM_2D_ARRAY_MSAA ||
        dim == Latte::E_DIM::DIM_1D_ARRAY)
        depth += 1;
    else {
        if (dim == Latte::E_DIM::DIM_CUBEMAP) depth = 6 * (depth + 1);
        if (depth == 0) depth = 1;
    }
    if (dim == Latte::E_DIM::DIM_1D || dim == Latte::E_DIM::DIM_1D_ARRAY) height = 1;
    auto tileMode = w0.get_TILE_MODE();
    if (Latte::IsCompressedFormat(w1.get_DATA_FORMAT())) pitch /= 4;
    uint32_t swizzle = 0;
    if (Latte::TM_IsMacroTiled(tileMode)) {
        swizzle = addr & 0x700;
        addr &= ~0x700u;
    }
    SurfaceDesc d;
    d.addr = addr;
    d.mipAddr = mipAddr;
    d.width = width;
    d.height = height;
    d.slices = depth;
    d.pitch = pitch;
    d.mips = w5.get_LAST_LEVEL() + 1;
    d.format = (uint32_t)LatteTexture_ReconstructGX2Format(w1, w4);
    d.dim = (uint32_t)dim;
    d.tileMode = (uint32_t)tileMode;
    d.swizzle = swizzle;
    d.isDepth = isDepthSampler;
    // A shadow descriptor can spell the shared memory as RGBA8 although the GPU wrote depth.
    // Prefer that depth image before creating a converted colour comparison texture.
    if (isDepthSampler) {
        Surface* renderedDepth = nullptr;
        auto aliases = stereo_surface_cache(d).equal_range(d.addr);
        for (auto it = aliases.first; it != aliases.second; ++it) {
            auto* candidate = it->second.get();
            if (!candidate->isDepth || !candidate->gpuWritten || candidate->width != d.width ||
                candidate->height != d.height || candidate->slices != d.slices || candidate->dim != d.dim) continue;
            if (!renderedDepth || candidate->writeSeq > renderedDepth->writeSeq) renderedDepth = candidate;
        }
        if (renderedDepth) return renderedDepth;
    }
    if (Surface* shared = stereo_eye0_texture(d)) return shared;  // already uploaded this frame
    Surface* s = find_or_create_surface(d, false);
    upload_surface(s);
    return s && s->gpuWritten && d.mips > 1 && !d.isDepth ? with_mip_chain(s, d, maxLevels) : s;
}


static const CachedGuestLevel& level_in_guest(Surface* s, uint32_t level);
static uint32_t mip_base(Surface* s, uint32_t level);
static void decode_level(Surface* s, uint32_t level, uint32_t base, std::vector<uint8_t>& out, uint32_t& outW,
                         uint32_t& outH, uint32_t& outSlices) {
    const FormatInfo& f = s->fmt;
    uint32_t w = std::max(s->width >> level, 1u), h = std::max(s->height >> level, 1u);
    uint32_t slices = s->dim == (uint32_t)Latte::E_DIM::DIM_3D ? std::max(s->slices >> level, 1u) : s->slices;
    uint32_t bw = f.compressed ? (w + 3) / 4 : w, bh = f.compressed ? (h + 3) / 4 : h;
    outW = w;
    outH = h;
    outSlices = slices;

    // level geometry from the address library
    const auto& info = guest_info(s, level);
    uint32_t pitch = level == 0 && s->pitch ? s->pitch : info.pitch, height = info.height;
    auto tm = (Latte::E_HWTILEMODE)info.hwTileMode;
    uint32_t bpp = f.bytesPerBlock * 8;
    bool depthData = (s->isDepth && f.convert != Convert::RGBA8_DEPTH) || f.convert == Convert::D24_R32F;
    // level 0 is tiled with the base pointer's swizzle, the mip chain with the mip pointer's
    const uint32_t swizzle = level_in_guest(s, level).swizzle;
    uint32_t pipeSwizzle = (swizzle >> 8) & 1, bankSwizzle = (swizzle >> 9) & 3;
    out.assign((size_t)bw * bh * slices * f.hostBytesPerBlock, 0);
    std::vector<uint8_t> row(bw * f.bytesPerBlock);
    const uint8_t* src = mem::ptr(base);
    for (uint32_t z = 0; z < slices; z++) {
        LatteAddrLib::CachedSurfaceAddrInfo ci;
        bool macro = Latte::TM_IsMacroTiled(tm);
        if (macro)
            LatteAddrLib::SetupCachedSurfaceAddrInfo(&ci, z, 0, bpp, pitch, height, slices, 1, tm, depthData, pipeSwizzle, bankSwizzle);
        for (uint32_t y = 0; y < bh; y++) {
            for (uint32_t x = 0; x < bw; x++) {
                uint32_t off;
                if (tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
                    off = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, z, 0, bpp, pitch, height, slices);
                else if (!macro)
                    off = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, z, bpp, pitch, height, tm, depthData);
                else
                    off = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, &ci);
                memcpy(&row[x * f.bytesPerBlock], src + off, f.bytesPerBlock);
            }
            uint8_t* dst = &out[((size_t)z * bh + y) * bw * f.hostBytesPerBlock];
            if (f.convert == Convert::NONE) memcpy(dst, row.data(), row.size());
            else convert_row(f.convert, row.data(), dst, bw);
        }
    }
}


// Fallback change check when write tracking (write_watch.h) is unavailable.
// Sample every mip so CPU changes confined to the mip chain get the same
// immediate detection as base-level changes. Periodic full checks catch writes
// outside these samples when guest code omits a texture invalidation.
static SparseHashStats sparseStats;
SparseHashStats sparse_hash_stats() { return sparseStats; }
static uint64_t sparse_hash(Surface* s) {
    static const bool enabled = [] {
        const char* value = getenv("WWHD_VK_SPARSE_HASH_MEMO");
        return value && !strcmp(value,"1");
    }();
    static const bool collectStats = getenv("WWHD_VK_STATS") != nullptr;
    if(!enabled && !collectStats) {
        // Keep the default path's original streaming loop, without diagnostics.
        uint64_t h = 0xcbf29ce484222325ull;
        for(uint32_t level = 0; level < s->mips; ++level) {
            const auto& info = guest_info(s, level);
            const auto* bytes = mem::ptr(mip_base(s, level));
            size_t size = size_t(info.surfSize), step = std::max<size_t>((size / 256) & ~size_t(7), 8);
            size_t offset = 0;
            for(; offset < size && size - offset >= 8; offset += step) {
                uint64_t value;
                memcpy(&value, bytes + offset, 8);
                h = (h ^ value) * 0x100000001b3ull;
            }
            if(offset < size) {
                uint64_t value = 0;
                memcpy(&value, bytes + offset, size - offset);
                h = (h ^ value) * 0x100000001b3ull;
            }
        }
        return h;
    }
    if(collectStats) ++sparseStats.checks;
    if(!enabled) {
        uint64_t hash = SparseHashMemo<>::basis;
        for(uint32_t level = 0; level < s->mips; ++level) {
            const auto& info = guest_info(s, level);
            const auto counts = sparse_sample_counts(size_t(info.surfSize));
            sparseStats.sampleBytes += counts.bytes;
            sparseStats.mixerWords += counts.words;
            sparse_sample_words(mem::ptr(mip_base(s, level)), size_t(info.surfSize),
                [&](uint64_t value, size_t) {
                    hash = (hash ^ value) * 0x100000001b3ull;
                });
        }
        return hash;
    }
    static const bool largerMemo = [] {
        const char* value = getenv("WWHD_VK_SPARSE_HASH_ENTRIES");
        return value && !strcmp(value,"256");
    }();
    auto runMemo = [&](auto& memo) {
        memo.begin();
        for(uint32_t level = 0; level < s->mips; ++level) {
            const auto& info = guest_info(s, level);
            if(collectStats) sparseStats.sampleBytes += sparse_sample_counts(size_t(info.surfSize)).bytes;
            sparse_sample_words(mem::ptr(mip_base(s, level)), size_t(info.surfSize),
                [&](uint64_t value, size_t) { memo.add(value); });
        }
        SparseHashStats discarded;
        return memo.finish(reinterpret_cast<uintptr_t>(s), collectStats ? sparseStats : discarded);
    };
    if(largerMemo) {
        // Allocate only the selected larger cache; payload storage is bounded
        // to 16 MiB plus 64 KiB scratch and small identity metadata.
        static std::unique_ptr<SparseHashMemo<256>> memo(new SparseHashMemo<256>);
        return runMemo(*memo);
    }
    static SparseHashMemo<> memo;
    return runMemo(memo);
}


static uint32_t level_address(GX2Surface* s, uint32_t level) {
    if (level == 0) return s->imagePtr;
    if (level == 1) return s->mipPtr;
    return s->mipPtr + s->mipOffset[level - 1];
}

static uint32_t element_offset(const LatteAddrLib::AddrSurfaceInfo_OUT& info, Latte::E_HWTILEMODE tm, uint32_t x, uint32_t y,
                               uint32_t slice, uint32_t bpp, uint32_t swizzle, LatteAddrLib::CachedSurfaceAddrInfo* ci, bool depth) {
    if (tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
        return LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, slice, 0, bpp, info.pitch, info.height, info.depth);
    if (!Latte::TM_IsMacroTiled(tm))
        return LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, slice, bpp, info.pitch, info.height, tm, depth);
    return LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, ci);
}


} // namespace gfxvk

namespace gfxvk {
void create_surface_image(Surface* s, bool forRendering, VkExtent3D explicitExtent, uint32_t sharedQueueFamily) {
    if (!s || !s->width || !s->height || !s->slices || !s->mips)
        exception_report::raise("Vulkan surface has empty dimensions");
    if (s->image) exception_report::raise("Vulkan surface image must be retired before replacement");
    if (s->fmt.pixel == VK_FORMAT_UNDEFINED)
        exception_report::raise("Unsupported GX2 surface format " + std::to_string(s->format));
    auto dim=static_cast<Latte::E_DIM>(s->dim);
    if (dim==Latte::E_DIM::DIM_2D_MSAA || dim==Latte::E_DIM::DIM_2D_ARRAY_MSAA)
        exception_report::raise("Vulkan GX2 multisample surfaces require an explicit sample count");
    bool oneD=dim==Latte::E_DIM::DIM_1D || dim==Latte::E_DIM::DIM_1D_ARRAY;
    bool threeD=dim==Latte::E_DIM::DIM_3D;
    bool cube=dim==Latte::E_DIM::DIM_CUBEMAP;
    s->imageType=oneD?VK_IMAGE_TYPE_1D:threeD?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;
    s->arrayLayers=threeD?1:s->slices;
    s->viewType=threeD?VK_IMAGE_VIEW_TYPE_3D:oneD?(s->slices>1?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D):
        cube?(s->slices>6?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE):
        (s->slices>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D);
    s->scale=forRendering&&!oneD&&!threeD&&!cube?target_scale(s):1.0f;
    s->dyn=forRendering&&!oneD&&!threeD&&!cube?target_dyn(s):1.0f;
    s->ax=s->ay=1.0f;
    if(forRendering&&!oneD&&!threeD&&!cube)target_aspect(s,s->ax,s->ay);
    s->extent={uint32_t(std::ceil(s->width*s->scale*s->ax-0.01f)),oneD?1:uint32_t(std::ceil(s->height*s->scale*s->ay-0.01f)),threeD?s->slices:1};
    if(explicitExtent.width || explicitExtent.height || explicitExtent.depth) {
        if(s->imageType!=VK_IMAGE_TYPE_2D || !explicitExtent.width ||
           !explicitExtent.height || explicitExtent.depth!=1 ||
           explicitExtent.width>R.properties.limits.maxImageDimension2D ||
           explicitExtent.height>R.properties.limits.maxImageDimension2D)
            exception_report::raise("Invalid explicit private surface extent");
        s->extent=explicitExtent;
    }
    s->sx=float(s->extent.width)/s->width; s->sy=float(s->extent.height)/s->height;
    s->aspect=s->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
    if(s->fmt.stencil)s->aspect|=VK_IMAGE_ASPECT_STENCIL_BIT;
    if(cube&&(s->extent.width!=s->extent.height||s->slices%6))exception_report::raise("Invalid GX2 cube surface dimensions");
    uint32_t maxDim=std::max({s->extent.width,s->extent.height,s->extent.depth});
    uint32_t maxMips=1; while(maxDim>1){maxDim>>=1;++maxMips;}
    if(s->mips>maxMips)exception_report::raise("GX2 surface requests too many mip levels");
    if (s->fmt.compressed) s->bcDecoded = bc_decode_required(format_info(s->format,s->isDepth));
    if (s->bcDecoded) {
        const bool sign = (s->format & 0x200) && (s->format & 0x3f) >= 0x34;
        s->fmt.pixel = sign ? VK_FORMAT_R8G8B8A8_SNORM : (s->format & 0x400) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    }
    const auto& properties = format_properties(s->fmt.pixel);
    s->linearFiltering = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    auto features=properties.optimalTilingFeatures;
    VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    auto attachment=s->fmt.depth?VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if(forRendering)required|=attachment;
    if((features&required)!=required)exception_report::raise("Vulkan device lacks required features for GX2 format "+std::to_string(s->format));
    VkImageUsageFlags usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(!s->fmt.compressed&&(features&attachment))usage|=s->fmt.depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags=(cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0)|VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    // volumes are render targets too (the game renders its colour-grading volumes slice by slice): their
    // slices get 2D attachment views (layer_view)
    if(threeD&&R.imageView2DOn3DImage&&(usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))imageInfo.flags|=VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    imageInfo.imageType=s->imageType; imageInfo.format=s->fmt.pixel; imageInfo.extent=s->extent;
    imageInfo.mipLevels=s->mips;imageInfo.arrayLayers=s->arrayLayers;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;imageInfo.usage=usage;s->usage=usage;s->createFlags=imageInfo.flags;imageInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    const uint32_t queueFamilies[]={R.queueFamily,sharedQueueFamily};
    if(sharedQueueFamily!=VK_QUEUE_FAMILY_IGNORED && sharedQueueFamily!=R.queueFamily) {
        imageInfo.sharingMode=VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount=2;
        imageInfo.pQueueFamilyIndices=queueFamilies;
    }
    imageInfo.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    try {
        check_vk(vkCreateImage(R.device,&imageInfo,nullptr,&s->image),"create image");
        VkMemoryRequirements needs{};vkGetImageMemoryRequirements(R.device,s->image,&needs);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=needs.size;
        allocation.memoryTypeIndex=memory_type(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check_vk(vkAllocateMemory(R.device,&allocation,nullptr,&s->memory),"allocate image memory");
        s->allocationBytes=needs.size;
        check_vk(vkBindImageMemory(R.device,s->image,s->memory,0),"bind image memory");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};viewInfo.image=s->image;
        viewInfo.viewType=s->viewType;viewInfo.format=s->fmt.pixel;
        viewInfo.subresourceRange={VkImageAspectFlags(s->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,s->mips,0,s->arrayLayers};
        check_vk(vkCreateImageView(R.device,&viewInfo,nullptr,&s->view),"create sampling image view");
        s->layout=VK_IMAGE_LAYOUT_UNDEFINED;s->use={};
        if(dynres::on()) { dynres::note_image();log_view(s,"IMAGE"); }
    } catch(...) {
        if(s->view)vkDestroyImageView(R.device,s->view,nullptr);
        if(s->image)vkDestroyImage(R.device,s->image,nullptr);
        if(s->memory)vkFreeMemory(R.device,s->memory,nullptr);
        s->view=VK_NULL_HANDLE;s->image=VK_NULL_HANDLE;s->memory=VK_NULL_HANDLE;s->allocationBytes=0;
        throw;
    }
}
void destroy_surface_image(Surface* s) {
    if (!s) return;
    if (s->mipChain) { destroy_surface_image(s->mipChain.get()); s->mipChain.reset(); }
    if (!s->spares.empty()) release_spares(s);
    if(!s->image)return;
    ++g_surface_epoch;
    auto views=std::move(s->layerViews);if(s->view)views.push_back(s->view);
    for(auto& [key,view]:s->sampledViews)if(view)views.push_back(view);s->sampledViews.clear();
    defer_surface_image(s->image,s->memory,std::move(views));
    s->image=VK_NULL_HANDLE;s->memory=VK_NULL_HANDLE;s->view=VK_NULL_HANDLE;s->allocationBytes=0;s->layout=VK_IMAGE_LAYOUT_UNDEFINED;s->use={};
    s->layerViews.clear();
}
VkImageView layer_view(Surface* s,uint32_t layer) {
    // a volume's slices are its depth (2D views of a 2D-array-compatible 3D image, one mip)
    bool volume=s&&s->imageType==VK_IMAGE_TYPE_3D;
    uint32_t layers=!s?0:volume?s->extent.depth:s->arrayLayers;
    if(!s||!s->image||layer>=layers)exception_report::raise("Vulkan attachment layer is out of range");
    if(volume&&!(s->createFlags&VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT))exception_report::raise("Rendering into this GX2 volume slice is unsupported");
    if(s->layerViews.size()<layers)s->layerViews.resize(layers,VK_NULL_HANDLE);
    if(!s->layerViews[layer]) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=s->image;
        info.viewType=s->imageType==VK_IMAGE_TYPE_1D?VK_IMAGE_VIEW_TYPE_1D:VK_IMAGE_VIEW_TYPE_2D;info.format=s->fmt.pixel;
        info.subresourceRange={s->aspect,0,1,layer,1};
        // issue #144: name the attachment usage (attachment_view_usage.h)
        VkImageViewUsageCreateInfo usage{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
        usage.usage=R.attachmentViewUsage?attachment_view_usage(s->usage,s->fmt.depth):0;
        if(usage.usage)info.pNext=&usage;
        check_vk(vkCreateImageView(R.device,&info,nullptr,&s->layerViews[layer]),"create attachment layer view");
        dynres::note_view();log_view(s,"attachment");
    }
    return s->layerViews[layer];
}
VkImageView sampled_texture_view(Surface* s,const uint32_t* texWords) {
    if(!s||!s->image||!texWords)exception_report::raise("Vulkan sampled view requires a surface and texture descriptor");
    Latte::LATTE_SQ_TEX_RESOURCE_WORD0_N w0;Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N w4;
    memcpy(&w0,texWords,4);memcpy(&w4,texWords+4,4);
    auto dim=w0.get_DIM();VkImageViewType type;
    switch(dim) {
    case Latte::E_DIM::DIM_1D:type=VK_IMAGE_VIEW_TYPE_1D;break;
    case Latte::E_DIM::DIM_1D_ARRAY:type=VK_IMAGE_VIEW_TYPE_1D_ARRAY;break;
    case Latte::E_DIM::DIM_2D:type=VK_IMAGE_VIEW_TYPE_2D;break;
    case Latte::E_DIM::DIM_2D_ARRAY:type=VK_IMAGE_VIEW_TYPE_2D_ARRAY;break;
    case Latte::E_DIM::DIM_3D:type=VK_IMAGE_VIEW_TYPE_3D;break;
    case Latte::E_DIM::DIM_CUBEMAP:type=s->arrayLayers>6?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE;break;
    default:exception_report::raise("Unsupported Vulkan sampled texture dimension");
    }
    bool oneD=type==VK_IMAGE_VIEW_TYPE_1D||type==VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    bool threeD=type==VK_IMAGE_VIEW_TYPE_3D;
    bool cube=type==VK_IMAGE_VIEW_TYPE_CUBE||type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    if((oneD&&s->imageType!=VK_IMAGE_TYPE_1D)||(threeD!=(s->imageType==VK_IMAGE_TYPE_3D))||(!oneD&&!threeD&&s->imageType!=VK_IMAGE_TYPE_2D))
        exception_report::raise("Vulkan sampled texture dimension does not match the backing image");
    if(cube&&(s->viewType!=VK_IMAGE_VIEW_TYPE_CUBE&&s->viewType!=VK_IMAGE_VIEW_TYPE_CUBE_ARRAY))
        exception_report::raise("Vulkan sampled cube requires a cube-compatible backing image");
    uint32_t selectors[4]={uint32_t(w4.get_DST_SEL_X()),uint32_t(w4.get_DST_SEL_Y()),uint32_t(w4.get_DST_SEL_Z()),uint32_t(w4.get_DST_SEL_W())};
    static const VkComponentSwizzle mapping[8]={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO};
    (void)mapping;
    uint32_t key=uint32_t(type)<<12;for(unsigned i=0;i<4;++i)key|=(s->fmt.depth?i:selectors[i])<<(i*3);
    auto found=s->sampledViews.find(key);if(found!=s->sampledViews.end())return found->second;
    return make_sampled_view(s,key);
}
// the sampled view a key stands for (view type << 12 | four 3-bit component selectors); also used to give an
// image kept for dynamic resolution the views its surface already uses (precreate_levels)
VkImageView make_sampled_view(Surface* s,uint32_t key) {
    static const VkComponentSwizzle mapping[8]={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO};
    const VkImageViewType type=VkImageViewType(key>>12);
    const bool threeD=type==VK_IMAGE_VIEW_TYPE_3D;
    uint32_t selectors[4];for(unsigned i=0;i<4;++i)selectors[i]=(key>>(i*3))&7;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=s->image;info.viewType=type;info.format=s->fmt.pixel;
    // Comparison samplers require a depth-only, identity-component view.
    if(!s->fmt.depth)info.components={mapping[selectors[0]],mapping[selectors[1]],mapping[selectors[2]],mapping[selectors[3]]};
    uint32_t layers=(type==VK_IMAGE_VIEW_TYPE_1D||type==VK_IMAGE_VIEW_TYPE_2D||threeD)?1:type==VK_IMAGE_VIEW_TYPE_CUBE?6:s->arrayLayers;
    info.subresourceRange={VkImageAspectFlags(s->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,s->mips,0,layers};
    VkImageView view=VK_NULL_HANDLE;check_vk(vkCreateImageView(R.device,&info,nullptr,&view),"create sampled texture view");s->sampledViews.emplace(key,view);
    dynres::note_view();log_view(s,"sampled");
    return view;
}
// ---------------------------------------------------------------- scaled copies without blits
// Vulkan requires no depth/stencil format to support blits (BLIT_SRC/BLIT_DST are optional for all of
// them), and Adreno drivers report none for some (D16_UNORM and D32_SFLOAT on the Adreno 830, issue
// #72). Scaling such a surface (rescale after a resolution or aspect-ratio change, a scaled
// GX2CopySurface) draws instead: each destination pixel takes the source texel vkCmdBlitImage with
// VK_FILTER_NEAREST would take, written with gl_FragDepth; stencil is copied one bit per pass (the
// pass writes its bit where the source has it, the depth pass zeroes all bits), so no
// VK_EXT_shader_stencil_export is needed. Devices that can blit keep the blit.
// Test aid: WWHD_VK_DEPTH_COPY=draw draws scaled depth copies on every device (the renderer smoke test
// forces it too); WWHD_VK_DEPTH_COPY=none takes neither the blit nor the draw for depth, which shows
// the last resort (clear_unscalable).
DepthCopyOverride g_depthCopyOverride=[]{
    const char* e=getenv("WWHD_VK_DEPTH_COPY");
    return !e?DepthCopyOverride::None:!strcmp(e,"draw")?DepthCopyOverride::Draw:!strcmp(e,"none")?DepthCopyOverride::Unsupported:DepthCopyOverride::None;
}();
enum class ScaledCopy { Blit, Draw, Unsupported };
// how a scaled copy between two surfaces of this format is done on this device (optimal tiling, as
// every surface image is created)
static ScaledCopy scaled_copy_mode(const FormatInfo& fmt) {
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,fmt.pixel,&properties);
    const auto features=properties.optimalTilingFeatures;
    const bool blit=(features&VK_FORMAT_FEATURE_BLIT_SRC_BIT)&&(features&VK_FORMAT_FEATURE_BLIT_DST_BIT);
    // the draw samples the source (both aspects of a combined format) and renders into the destination
    const bool draw=fmt.depth&&(features&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)&&(features&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    ScaledCopy mode=blit?ScaledCopy::Blit:draw?ScaledCopy::Draw:ScaledCopy::Unsupported;
    if(fmt.depth&&g_depthCopyOverride==DepthCopyOverride::Draw&&draw)mode=ScaledCopy::Draw;
    if(fmt.depth&&g_depthCopyOverride==DepthCopyOverride::Unsupported)mode=ScaledCopy::Unsupported;
    if(mode!=ScaledCopy::Blit) {
        static std::vector<VkFormat> logged;
        if(std::find(logged.begin(),logged.end(),fmt.pixel)==logged.end()) {
            logged.push_back(fmt.pixel);
            const char* why=blit?" (WWHD_VK_DEPTH_COPY)":"";
            if(mode==ScaledCopy::Draw)LOG("[gfx] Vulkan: format %d: scaled depth copies are drawn, not blitted%s",int(fmt.pixel),why);
            else LOG("[gfx] Vulkan: format %d can be neither blitted nor drawn%s; scaled copies of it are cleared or skipped",int(fmt.pixel),why);
        }
    }
    return mode;
}
static uint32_t g_eyeSizeGeneration = 0;
void stereo_reset_eye_sized_targets() { g_eyeSizedTargets = true; ++g_eyeSizeGeneration; }
uint32_t stereo_eye_size_generation() { return g_eyeSizeGeneration; }
static bool eye_sized(const FormatInfo& fmt) {
    if (!g_eyeSizedTargets) return false;
    if (scaled_copy_mode(fmt) != ScaledCopy::Unsupported) return true;
    g_eyeSizedTargets = false;
    ++g_eyeSizeGeneration;
    ++g_surface_epoch;  // continued draws hold the eye-sized targets' extents
    LOG("[stereo] Vulkan format %d can be neither blitted nor drawn on this device: Half SBS and "
        "top-and-bottom now render each eye at full size", int(fmt.pixel));
    return false;
}
namespace {
struct DepthCopyParams { float scaleX,scaleY;int32_t lastX,lastY;uint32_t bit; };
struct DepthCopyResources {
    VkDevice device=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    std::unordered_map<uint64_t,VkPipeline> pipelines;  // (format, stencil bit pass)
} depthCopy;
const char* depthCopyVertex=R"glsl(#version 450
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)glsl";
const char* depthCopyFragment=R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D depthSource;
layout(push_constant) uniform Params { vec2 scale; ivec2 last; uint bit; } p;
void main() {
    ivec2 texel=min(ivec2(gl_FragCoord.xy*p.scale),p.last);
    gl_FragDepth=texelFetch(depthSource,texel,0).r;
}
)glsl";
const char* stencilCopyFragment=R"glsl(#version 450
layout(set=0,binding=1) uniform usampler2D stencilSource;
layout(push_constant) uniform Params { vec2 scale; ivec2 last; uint bit; } p;
void main() {
    ivec2 texel=min(ivec2(gl_FragCoord.xy*p.scale),p.last);
    if((texelFetch(stencilSource,texel,0).r&(1u<<p.bit))==0u)discard;
}
)glsl";
VkShaderModule depth_copy_module(const char* source,bool vertex) {
    std::string error;auto words=vk::compile_glsl(source,vertex,&error);
    if(words.empty()||!error.empty())exception_report::raise("Vulkan depth copy shader: "+error);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4;ci.pCode=words.data();
    VkShaderModule module=VK_NULL_HANDLE;check_vk(vkCreateShaderModule(R.device,&ci,nullptr,&module),"create depth copy shader");return module;
}
void depth_copy_resources() {
    if(depthCopy.device&&depthCopy.device!=R.device)depthCopy={};  // a new device: the old one's objects went with it
    depthCopy.device=R.device;
    if(!depthCopy.descriptors) {
        VkDescriptorSetLayoutBinding bindings[2]{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                                                 {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.bindingCount=2;ci.pBindings=bindings;
        check_vk(vkCreateDescriptorSetLayout(R.device,&ci,nullptr,&depthCopy.descriptors),"create depth copy descriptors");
    }
    if(!depthCopy.layout) {
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(DepthCopyParams)};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=1;ci.pSetLayouts=&depthCopy.descriptors;
        ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
        check_vk(vkCreatePipelineLayout(R.device,&ci,nullptr,&depthCopy.layout),"create depth copy layout");
    }
    if(!depthCopy.sampler) {
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.minFilter=ci.magFilter=VK_FILTER_NEAREST;ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check_vk(vkCreateSampler(R.device,&ci,nullptr,&depthCopy.sampler),"create depth copy sampler");
    }
}
// the depth pass (writes depth and, with stencil, zeroes it) or a stencil bit pass
VkPipeline depth_copy_pipeline(const FormatInfo& fmt,bool stencilPass) {
    depth_copy_resources();
    const uint64_t key=uint64_t(fmt.pixel)<<1|uint64_t(stencilPass);
    if(auto it=depthCopy.pipelines.find(key);it!=depthCopy.pipelines.end())return it->second;
    VkShaderModule vs=VK_NULL_HANDLE,fs=VK_NULL_HANDLE;VkPipeline result=VK_NULL_HANDLE;
    try {
        vs=depth_copy_module(depthCopyVertex,true);fs=depth_copy_module(stencilPass?stencilCopyFragment:depthCopyFragment,false);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(int i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=i?fs:vs;stages[i].pName="main";}
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dss.depthTestEnable=dss.depthWriteEnable=stencilPass?VK_FALSE:VK_TRUE;dss.depthCompareOp=VK_COMPARE_OP_ALWAYS;
        dss.stencilTestEnable=fmt.stencil?VK_TRUE:VK_FALSE;
        // reference and write mask are dynamic: 0 / all bits in the depth pass, all bits / one bit in a bit pass
        dss.front={VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_REPLACE,VK_COMPARE_OP_ALWAYS,0xFF,0xFF,0};dss.back=dss.front;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ds.dynamicStateCount=fmt.stencil?4:2;ds.pDynamicStates=states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.depthAttachmentFormat=fmt.pixel;rendering.stencilAttachmentFormat=fmt.stencil?fmt.pixel:VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipeline_targets(ci,rendering);ci.stageCount=2;ci.pStages=stages;
        ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vp;ci.pRasterizationState=&rs;ci.pMultisampleState=&ms;
        ci.pDepthStencilState=&dss;ci.pColorBlendState=&bs;ci.pDynamicState=&ds;ci.layout=depthCopy.layout;
        check_vk(stutter::create_graphics(R.frame, "surface_pipeline", R.device,R.pipelineCache,1,&ci,nullptr,&result, [] {
            begin_pipeline_pending_frame();
            present_completed_frame_while_pipeline_pending();
        }),"create depth copy pipeline");
        depthCopy.pipelines.emplace(key,result);++R.pipelineCreates;R.pipelineCacheDirty=true;R.pipelineCacheChangedFrame=R.frame;
    } catch(...) {
        if(result)vkDestroyPipeline(R.device,result,nullptr);
        if(vs)vkDestroyShaderModule(R.device,vs,nullptr);if(fs)vkDestroyShaderModule(R.device,fs,nullptr);
        throw;
    }
    vkDestroyShaderModule(R.device,vs,nullptr);vkDestroyShaderModule(R.device,fs,nullptr);
    return result;
}
// a 2D view of one level and layer of a depth/stencil surface, for sampling one aspect; kept with the
// surface's other sampled views (destroyed with its image) under keys sampled_texture_view never makes
VkImageView depth_copy_source_view(Surface* s,VkImageAspectFlags aspect,uint32_t level,uint32_t layer) {
    const uint32_t key=0x80000000u|(aspect==VK_IMAGE_ASPECT_STENCIL_BIT?0x40000000u:0)|(level<<20)|layer;
    if(auto it=s->sampledViews.find(key);it!=s->sampledViews.end())return it->second;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=s->image;info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.format=s->fmt.pixel;
    info.subresourceRange={aspect,level,1,layer,1};
    VkImageView view=VK_NULL_HANDLE;check_vk(vkCreateImageView(R.device,&info,nullptr,&view),"create depth copy source view");
    s->sampledViews.emplace(key,view);return view;
}
}
// Copies layers [srcLayer, srcLayer+layers) of level srcLevel of src, the region (0,0)-(srcW,srcH), to
// level 0 of dst from dstLayer on, the region (0,0)-(dstW,dstH), as a nearest-filter blit would. Leaves
// both surfaces in SHADER_READ_ONLY_OPTIMAL.
static void draw_depth_copy(Surface* src,uint32_t srcLevel,uint32_t srcLayer,uint32_t srcW,uint32_t srcH,
                            Surface* dst,uint32_t dstLayer,uint32_t dstW,uint32_t dstH,uint32_t layers) {
    if(src->image==dst->image)exception_report::raise("Vulkan depth copy source and destination alias");
    if(src->fmt.pixel!=dst->fmt.pixel||!src->fmt.depth||src->imageType!=VK_IMAGE_TYPE_2D||dst->imageType!=VK_IMAGE_TYPE_2D)
        exception_report::raise("Vulkan depth copy requires 2D depth surfaces of one format");
    if(!(src->usage&VK_IMAGE_USAGE_SAMPLED_BIT)||!(dst->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        exception_report::raise("Vulkan depth copy requires a sampled source and a depth attachment destination");
    if(srcLevel>=src->mips||srcLayer+layers>src->arrayLayers||dstLayer+layers>dst->arrayLayers||!srcW||!srcH||!dstW||!dstH||
       dstW>dst->extent.width||dstH>dst->extent.height)
        exception_report::raise("Invalid Vulkan depth copy region");
    const FormatInfo& fmt=src->fmt;
    VkPipeline depthPass=depth_copy_pipeline(fmt,false),bitPass=fmt.stencil?depth_copy_pipeline(fmt,true):VK_NULL_HANDLE;
    end_encoder();
    transition_image(src,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    transition_image(dst,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    auto cmd=command_buffer();
    DepthCopyParams params{float(srcW)/float(dstW),float(srcH)/float(dstH),int32_t(srcW)-1,int32_t(srcH)-1,0};
    VkViewport viewport{0,0,float(dstW),float(dstH),0,1};VkRect2D area{{0,0},{dstW,dstH}};
    for(uint32_t i=0;i<layers;++i) {
        VkDescriptorSet set=VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=R.descriptorPool;
        allocation.descriptorSetCount=1;allocation.pSetLayouts=&depthCopy.descriptors;
        check_vk(vkAllocateDescriptorSets(R.device,&allocation,&set),"allocate depth copy descriptors");
        VkDescriptorImageInfo images[2]{{depthCopy.sampler,depth_copy_source_view(src,VK_IMAGE_ASPECT_DEPTH_BIT,srcLevel,srcLayer+i),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {depthCopy.sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for(uint32_t b=0;b<2;++b){writes[b].dstSet=set;writes[b].dstBinding=b;writes[b].descriptorCount=1;writes[b].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[b].pImageInfo=&images[b];}
        if(fmt.stencil)images[1].imageView=depth_copy_source_view(src,VK_IMAGE_ASPECT_STENCIL_BIT,srcLevel,srcLayer+i);
        vkUpdateDescriptorSets(R.device,fmt.stencil?2:1,writes,0,nullptr);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView=layer_view(dst,dstLayer+i);attachment.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        // every pixel of the render area is written (depth, and all stencil bits), and nothing outside it is touched
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea=area;rendering.layerCount=1;
        rendering.pDepthAttachment=&attachment;rendering.pStencilAttachment=fmt.stencil?&attachment:nullptr;
        begin_pass(cmd,rendering,[&] {
            PassBegin pass;pass.formats.depth=dst->fmt.pixel;pass.formats.stencil=dst->fmt.stencil;
            pass.layouts[8]=dst->layout;return pass;
        });++R.renderPassCount;
        record::vkCmdSetViewport(cmd,0,1,&viewport);record::vkCmdSetScissor(cmd,0,1,&area);
        record::vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,depthCopy.layout,0,1,&set,0,nullptr);
        record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,depthPass);
        if(fmt.stencil) {
            record::vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0);
            record::vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0xFF);
        }
        params.bit=0;record::vkCmdPushConstants(cmd,depthCopy.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);
        record::vkCmdDraw(cmd,3,1,0,0);
        if(fmt.stencil) {
            record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,bitPass);
            record::vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0xFF);
            for(uint32_t bit=0;bit<8;++bit) {
                record::vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,1u<<bit);
                params.bit=bit;record::vkCmdPushConstants(cmd,depthCopy.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);
                record::vkCmdDraw(cmd,3,1,0,0);
            }
        }
        end_pass(cmd);
    }
    transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
// Last resort for a scaled copy the device can neither blit nor draw: dst is cleared (depth to the far
// plane, stencil and colour to zero) instead of aborting the game; the game redraws its targets every
// frame, so at worst one frame shows the cleared contents. Only a whole destination is cleared, a
// partial one keeps its contents.
static void clear_unscalable(Surface* dst,uint32_t dstW,uint32_t dstH);
// Dynamic resolution: an image made ahead is cleared once right away, so a driver's first-use work (Adreno:
// several ms of render-thread time on the first frames at a new level) happens then, not at the change.
void touch_image(Surface* s) { if(s&&s->image)clear_unscalable(s,s->extent.width,s->extent.height); }
static void clear_unscalable(Surface* dst,uint32_t dstW,uint32_t dstH) {
    if(dstW!=dst->extent.width||dstH!=dst->extent.height)return;
    end_encoder();
    transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageSubresourceRange range{dst->aspect,0,dst->mips,0,dst->arrayLayers};
    if(dst->fmt.depth) {
        VkClearDepthStencilValue value{1.0f,0};
        record::vkCmdClearDepthStencilImage(command_buffer(),dst->image,dst->layout,&value,1,&range);
    } else {
        VkClearColorValue value{};
        record::vkCmdClearColorImage(command_buffer(),dst->image,dst->layout,&value,1,&range);
    }
    transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
// Dynamic resolution: the first scaled copy of a render-target format can be slow (the driver may make a
// pipeline for it: MoltenVK draws scaled blits, about 100 ms the first time; a depth format the device
// cannot blit gets our depth-copy draw, also made on first use). While dynamic resolution is on, one
// tiny scaled copy per format of the existing render targets is made ahead, so the first scale change
// does not wait for it.
static void warm_dynamic_resolution() {
    static std::vector<VkFormat> warmed;
    static uint64_t checked = ~0ull;
    if (checked == R.surfaces.size()) return;  // cheap: look again only when surfaces were added
    checked = R.surfaces.size();
    for (auto& [addr, s] : R.surfaces) {
        const auto attachment = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (!s->image || !(s->usage & attachment) || s->imageType != VK_IMAGE_TYPE_2D || s->fmt.compressed ||
            std::find(warmed.begin(), warmed.end(), s->fmt.pixel) != warmed.end())
            continue;
        warmed.push_back(s->fmt.pixel);
        Surface a, b;
        for (Surface* t : {&a, &b}) {
            t->width = t->height = t == &a ? 8 : 4;
            t->format = s->format; t->isDepth = s->isDepth; t->fmt = s->fmt;
        }
        try {
            create_surface_image(&a, false);
            create_surface_image(&b, false);
            resample(&a, &b, 1, 1, 1, 0, 0);
        } catch (const std::exception& e) {
            LOG("[dynres] could not prepare scaled copies of format %d: %s", int(s->fmt.pixel), e.what());
        }
        destroy_surface_image(&a);  // retired with the submission
        destroy_surface_image(&b);
    }
}
void resample(Surface* src,Surface* dst,uint32_t slices,float uMax,float vMax,uint32_t dstW,uint32_t dstH) {
    if(!src||!dst||!src->image||!dst->image)exception_report::raise("Vulkan resample requires allocated surfaces");
    if(src->image==dst->image)exception_report::raise("Vulkan resample source and destination alias");
    if(src->fmt.pixel!=dst->fmt.pixel||src->imageType!=dst->imageType||src->fmt.compressed)
        exception_report::raise("Vulkan resample requires matching uncompressed surface formats and dimensions");
    if(slices>src->arrayLayers||slices>dst->arrayLayers)exception_report::raise("Vulkan resample layer range exceeds surface");
    if(!dstW)dstW=dst->extent.width;if(!dstH)dstH=dst->extent.height;
    if(dstW>dst->extent.width||dstH>dst->extent.height||!(uMax>0&&uMax<=1&&vMax>0&&vMax<=1))
        exception_report::raise("Invalid Vulkan resample extent");
    const uint32_t srcW=std::max(1u,uint32_t(std::lround(src->extent.width*uMax))),srcH=std::max(1u,uint32_t(std::lround(src->extent.height*vMax)));
    const ScaledCopy mode=scaled_copy_mode(src->fmt);
    // volumes keep their guest size (create_surface_image) and are never drawn into here
    const bool drawable=mode==ScaledCopy::Draw&&src->imageType==VK_IMAGE_TYPE_2D&&(src->usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&
                        (dst->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    const bool sameSize=srcW==dstW&&srcH==dstH&&src->extent.depth==dst->extent.depth;
    if(drawable&&!sameSize) {
        draw_depth_copy(src,0,0,srcW,srcH,dst,0,dstW,dstH,slices);
        return;
    }
    if(mode!=ScaledCopy::Blit&&!sameSize) {
        clear_unscalable(dst,dstW,dstH);
        return;
    }
    end_encoder();transition_image(src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    if(mode!=ScaledCopy::Blit) {
        // the same size without blits: a plain copy (transfer support is required of every surface format)
        VkImageCopy region{};region.srcSubresource={src->aspect,0,0,slices};region.dstSubresource=region.srcSubresource;
        region.extent={dstW,dstH,dst->extent.depth};
        record::vkCmdCopyImage(command_buffer(),src->image,src->layout,dst->image,dst->layout,1,&region);
    } else {
        VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,src->fmt.pixel,&properties);
        const auto features=properties.optimalTilingFeatures;
        VkImageBlit region{};region.srcSubresource={VkImageAspectFlags(src->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,0,slices};
        region.dstSubresource=region.srcSubresource;
        region.srcOffsets[1]={int32_t(srcW),int32_t(srcH),int32_t(src->extent.depth)};
        region.dstOffsets[1]={int32_t(dstW),int32_t(dstH),int32_t(dst->extent.depth)};
        VkFilter filter=!src->fmt.depth&&src->fmt.kind==FormatInfo::FLOAT&&(features&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
        record::vkCmdBlitImage(command_buffer(),src->image,src->layout,dst->image,dst->layout,1,&region,filter);
        if(src->fmt.stencil) {
            region.srcSubresource.aspectMask=region.dstSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
            record::vkCmdBlitImage(command_buffer(),src->image,src->layout,dst->image,dst->layout,1,&region,VK_FILTER_NEAREST);
        }
    }
    transition_image(src,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
// the image of a surface (handle, memory, views, size, layout and the factors it was made at), swapped
// between a surface and a kept image (dynamic resolution) or a replacement
static void swap_image(Surface& a,Surface& b) {
    std::swap(a.image,b.image);std::swap(a.memory,b.memory);std::swap(a.view,b.view);std::swap(a.allocationBytes,b.allocationBytes);
    std::swap(a.extent,b.extent);std::swap(a.layout,b.layout);std::swap(a.use,b.use);std::swap(a.usage,b.usage);
    std::swap(a.createFlags,b.createFlags);std::swap(a.layerViews,b.layerViews);std::swap(a.sampledViews,b.sampledViews);
    std::swap(a.scale,b.scale);std::swap(a.dyn,b.dyn);std::swap(a.ax,b.ax);std::swap(a.ay,b.ay);std::swap(a.sx,b.sx);std::swap(a.sy,b.sy);
    std::swap(a.mipChain,b.mipChain);a.mipChainLevels=b.mipChainLevels=0;  // a sampled mip chain of that size goes with it (rebuilt)
}
static void release_spares(Surface* s) {
    auto spares=std::move(s->spares);s->spares.clear();
    for(auto& k:spares) { destroy_surface_image(k.get());--g_spare_images; }
}
// Dynamic resolution: a render target keeps images of the levels next to the current one (one step up
// and one step down; Surface::spares). The controller changes one level at a time, so a change takes an
// image that exists and only copies into it. The neighbours are made ahead (precreate_levels, a few a
// frame); when the level changes, the new neighbour is made and the level now two steps away goes.
// Keeping every level would double the image memory, a bad trade on the memory-tight devices
// dynamic resolution is for. Off: none are kept.
static size_t spare_cap() { return 4; }  // two neighbours plus images of a former neighbour not yet released (precreate_levels)
static bool take_spare(Surface* s,Surface& into,float wanted,float ax,float ay) {
    for(size_t i=0;i<s->spares.size();++i) {
        Surface& k=*s->spares[i];
        if(k.scale!=wanted||k.ax!=ax||k.ay!=ay||!k.image)continue;
        swap_image(into,k);
        s->spares.erase(s->spares.begin()+i);--g_spare_images;
        return true;
    }
    return false;
}
static std::shared_ptr<Surface> spare_like(const Surface& old) {
    auto k=std::make_shared<Surface>();
    k->width=old.width;k->height=old.height;k->slices=old.slices;k->mips=old.mips;k->dim=old.dim;k->format=old.format;k->tileMode=old.tileMode;
    k->isDepth=old.isDepth;k->fmt=old.fmt;k->imageType=old.imageType;k->viewType=old.viewType;k->arrayLayers=old.arrayLayers;k->aspect=old.aspect;
    k->linearFiltering=old.linearFiltering;
    return k;
}
static void keep_spare(Surface* s,Surface& old) {
    auto k=spare_like(old);
    swap_image(*k,old);
    s->spares.insert(s->spares.begin(),std::move(k));++g_spare_images;
    while(s->spares.size()>spare_cap()) { destroy_surface_image(s->spares.back().get());s->spares.pop_back();--g_spare_images; }
}
// a render target that dynamic resolution scales (as target_dyn with a factor below 1)
static bool dyn_target(const Surface* s) {
    if(!s->image||s->sizeClaim.size!=gfx::TargetSize::Internal||s->imageType!=VK_IMAGE_TYPE_2D||s->viewType!=VK_IMAGE_VIEW_TYPE_2D||s->fmt.compressed||s->mips>1||s->slices>1)return false;
    if(!(s->usage&(s->fmt.depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)))return false;
    uint32_t width,height;
    return !mods::cemu::texture_extent(s->width,s->height,s->format,s->slices,s->tileMode,width,height);
}
// the level of a factor (dynres::level_factor), -1 if none
static int level_of(float f) {
    for(int l=0;l<dynres::levels();++l) if(dynres::level_factor(l)==f) return l;
    return -1;
}
// Makes up to dynres::kPrecreatePerFrame missing neighbour images (render thread, at the frame
// boundary) and drops kept images of levels that are no neighbours any more. The cost of image creation
// (about 10 ms for a whole set on an Adreno phone) is spread over frames instead of landing on the frame
// of a scale change.
static void precreate_levels() {
    static uint64_t epoch=~0ull;static float doneBase=0,doneKx=0,doneKy=0,doneDyn=0;static int doneLevels=0;static bool complete=false;
    static size_t doneViews=0;
    size_t views=0;
    for(auto& [addr,sp]:R.surfaces) views+=sp->sampledViews.size();  // a surface sampled through a new view: its kept images get it too
    if(complete&&epoch==R.surfaces.size()&&doneBase==g_res_frame&&doneLevels==dynres::levels()&&doneKx==g_aspect_kx&&doneKy==g_aspect_ky&&doneDyn==g_dyn_frame&&doneViews==views)return;
    complete=false;epoch=R.surfaces.size();doneViews=views;doneBase=g_res_frame;doneLevels=dynres::levels();doneKx=g_aspect_kx;doneKy=g_aspect_ky;doneDyn=g_dyn_frame;
    const int current=std::max(level_of(g_dyn_frame),0);
    // wanted: the current level (a target not drawn since the change still holds its old image) and its neighbours
    auto wanted=[&](float scale) {
        for(int l=std::max(current-1,0);l<=std::min(current+1,dynres::levels()-1);++l)
            if(scale==g_res_frame*dynres::level_factor(l)) return true;
        return false;
    };
    const auto started=std::chrono::steady_clock::now();
    int made=0,dropped=0;
    const float saved=g_dyn_frame;
    struct Mark { Mark(){g_in_precreate=true;} ~Mark(){g_in_precreate=false;} } mark;
    for(auto& [addr,sp]:R.surfaces) {
        Surface* s=sp.get();
        if(!dyn_target(s))continue;
        float ax,ay;target_aspect(s,ax,ay);
        // images of a level that is no neighbour any more, or of another internal resolution or aspect ratio, go,
        // a few a frame as they are made: freeing a whole level at once cost the phone several ms on one frame
        for(size_t i=0;i<s->spares.size();) {
            Surface& k=*s->spares[i];
            if(k.ax!=ax||k.ay!=ay||!wanted(k.scale)) {
                if(dropped>=dynres::kPrecreatePerFrame) { complete=false;++i;continue; }
                destroy_surface_image(&k);s->spares.erase(s->spares.begin()+i);--g_spare_images;++dropped;
            } else ++i;
        }
        for(int l=std::max(current-1,0);l<=std::min(current+1,dynres::levels()-1);++l) {
            const float f=dynres::level_factor(l),want=g_res_frame*f;
            if(s->scale==want&&s->ax==ax&&s->ay==ay)continue;
            bool have=false;
            for(auto& k:s->spares) if(k->scale==want&&k->ax==ax&&k->ay==ay) have=true;
            if(have)continue;
            if(made>=dynres::kPrecreatePerFrame) { g_dyn_frame=saved;dynres::note_precreated(made,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());return; }
            auto k=spare_like(*s);
            g_dyn_frame=f;  // create_surface_image sizes it as target_scale does at that level
            try { create_surface_image(k.get(),true); } catch(...) { g_dyn_frame=saved;throw; }
            g_dyn_frame=saved;
            touch_image(k.get());
            s->spares.push_back(std::move(k));++g_spare_images;++made;
        }
        // the kept images get the views the surface uses (attachment view, sampled views), so the first frames at a
        // new level create none (Adreno: several ms for a set)
        for(auto& k:s->spares) {
            if(!k->image)continue;
            if(k->layerViews.empty()||!k->layerViews[0]) { layer_view(k.get(),0);++made; }
            // the sampled mip chain the surface has (with_mip_chain): one of the kept image's size, so the first
            // sampling at the new level only fills it
            if(Surface* sc=s->mipChain.get(); sc&&sc->image&&!k->mipChain) {
                auto chain=std::make_shared<Surface>();
                Surface* c=chain.get();
                c->addr=sc->addr;c->mipAddr=sc->mipAddr;c->width=sc->width;c->height=sc->height;c->slices=1;
                c->mips=sc->mips;c->format=sc->format;c->dim=sc->dim;c->fmt=sc->fmt;c->gpuWritten=true;
                uint32_t full=1;while((std::max(k->extent.width,k->extent.height)>>full)>0)full++;
                if(c->mips<=full) {
                    create_surface_image(c,false,k->extent);
                    touch_image(c);
                    for(auto& [key,view]:sc->sampledViews) make_sampled_view(c,key);
                    k->mipChain=std::move(chain);k->mipChainLevels=0;++made;
                }
            }
            for(auto& [key,view]:s->sampledViews)
                if(!k->sampledViews.count(key)) { make_sampled_view(k.get(),key);++made; }
            if(made>=dynres::kPrecreatePerFrame*4) { complete=false;dynres::note_precreated(made,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());return; }
        }
    }
    g_dyn_frame=saved;
    if(dropped<dynres::kPrecreatePerFrame) complete=true;  // (else stale images are left for the next frames)
    if(made)dynres::note_precreated(made,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
    static bool logged=false;
    if(!logged) { logged=true;LOG("[dynres] render target images of the neighbouring levels made ahead (%llu kept images)",(unsigned long long)g_spare_images); }
}
static Surface* rescale(Surface* s) {
    float wanted=target_scale(s),ax,ay;
    target_aspect(s,ax,ay);
    auto attachment=s->fmt.depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(s->imageType!=VK_IMAGE_TYPE_2D||s->viewType==VK_IMAGE_VIEW_TYPE_CUBE||s->viewType==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
        // 1D, volume and cube images keep the guest size (create_surface_image): nothing to rescale
        if(s->usage&attachment)return s;
        exception_report::raise("Vulkan GX2 surface cannot be a render target");
    }
    if(s->scale==wanted&&s->ax==ax&&s->ay==ay&&(s->usage&attachment))return s;
    const bool keep=dynres::on()&&(s->usage&attachment);  // a render target already: worth keeping
    if(!dynres::on()&&!s->spares.empty())release_spares(s);
    Surface replacement;
    replacement.width=s->width;replacement.height=s->height;replacement.slices=s->slices;replacement.mips=s->mips;
    replacement.dim=s->dim;replacement.format=s->format;replacement.isDepth=s->isDepth;replacement.fmt=s->fmt;
    replacement.sizeClaim=s->sizeClaim;
    using clock=std::chrono::steady_clock;
    const auto t0=clock::now();
    const bool reused=keep&&take_spare(s,replacement,wanted,ax,ay);
    if(reused) {
        replacement.imageType=s->imageType;replacement.viewType=s->viewType;replacement.arrayLayers=s->arrayLayers;replacement.aspect=s->aspect;
        replacement.linearFiltering=s->linearFiltering;
        dynres::count_reused();
    } else {
        create_surface_image(&replacement,true);
        if(dynres::on())dynres::count_created();
    }
    const auto t1=clock::now();
    try { resample(s,&replacement,s->arrayLayers,1,1,0,0); }
    catch(...) { destroy_surface_image(&replacement);throw; }
    if(keep) {
        ++g_surface_epoch;  // as destroy_surface_image: the surface's image changes
        keep_spare(s,*s);   // (its mip chain goes with it)
    } else destroy_surface_image(s);
    if(dynres::on())dynres::note_rescale(!reused,std::chrono::duration<double,std::milli>(t1-t0).count(),
                                         std::chrono::duration<double,std::milli>(clock::now()-t1).count());
    s->image=replacement.image;s->memory=replacement.memory;s->view=replacement.view;s->allocationBytes=replacement.allocationBytes;
    s->layerViews=std::move(replacement.layerViews);s->sampledViews=std::move(replacement.sampledViews);
    s->mipChain=std::move(replacement.mipChain);s->mipChainLevels=0;
    s->extent=replacement.extent;s->layout=replacement.layout;s->use=replacement.use;s->usage=replacement.usage;s->createFlags=replacement.createFlags;s->scale=replacement.scale;s->dyn=replacement.dyn;s->ax=replacement.ax;s->ay=replacement.ay;s->sx=replacement.sx;s->sy=replacement.sy;
    if(getenv("WWHD_LOG_RESCALE"))
        LOG("[gfx] rescaled %08X %ux%u to %ux%u (%s)",s->addr,s->width,s->height,s->extent.width,s->extent.height,reused?"kept image":"new image");
    forget_texture_views();return s;
}
// One guest surface can be rendered and sampled through views of different formats with the same
// texel bits, e.g. RGBA8 and RGBA8 sRGB: the Picto Box draws its picture through the sRGB view and
// then through the plain one (issue #53). Each format gets its own image here, so before one is
// used, take over the texels of a more recent compatible one (a raw copy, as the memory is shared).
static Surface* adopt_newer_alias(Surface* s) {
    if(!s||!s->formatViews||!s->image)return s;  // the common case: one view per address, nothing to do
    Surface* newest=nullptr;
    auto range=R.surfaces.equal_range(s->addr);
    for(auto it=range.first;it!=range.second;++it) {
        Surface* o=it->second.get();
        if(o==s||!o->gpuWritten||!o->image||o->isDepth||o->fmt.compressed||o->fmt.convert!=Convert::NONE)continue;
        if(s->gpuWritten&&o->writeSeq<=s->writeSeq)continue;
        if((o->format&0x3F)!=(s->format&0x3F)||o->fmt.hostBytesPerBlock!=s->fmt.hostBytesPerBlock)continue;
        if(o->imageType!=s->imageType||o->arrayLayers!=s->arrayLayers||o->extent.width!=s->extent.width||
           o->extent.height!=s->extent.height||o->extent.depth!=s->extent.depth)continue;
        if(!newest||o->writeSeq>newest->writeSeq)newest=o;
    }
    if(!newest)return s;
    end_encoder();
    transition_image(newest,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    std::vector<VkImageCopy> regions;
    for(uint32_t mip=0;mip<std::min(s->mips,newest->mips);++mip) {
        VkImageCopy region{};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,s->arrayLayers};region.dstSubresource=region.srcSubresource;
        region.extent={std::max(1u,s->extent.width>>mip),std::max(1u,s->extent.height>>mip),std::max(1u,s->extent.depth>>mip)};
        regions.push_back(region);
    }
    record::vkCmdCopyImage(command_buffer(),newest->image,newest->layout,s->image,s->layout,uint32_t(regions.size()),regions.data());
    transition_image(newest,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);transition_image(s,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mark_gpu_written(s);
    return s;
}
static void restore_evicted(Surface* s);  // (texture budget, below)
Surface* find_or_create_surface(const SurfaceDesc& d,bool forRendering) {
    if(!d.addr)return nullptr;
    auto& cache=stereo_surface_cache(d);
    auto range=cache.equal_range(d.addr);
    Surface* exact=nullptr;Surface* rendered=nullptr;
    auto score=[&](Surface* s){return std::make_tuple(s->width==d.width&&s->height==d.height,s->slices==d.slices,s->writeSeq);};
    // a rendered surface can stand in for a sampled view only with the same image type
    // (sampled_texture_view cannot view a 2D image as a volume or the other way round)
    auto dimType=[](uint32_t dim){auto e=static_cast<Latte::E_DIM>(dim);
        return e==Latte::E_DIM::DIM_1D||e==Latte::E_DIM::DIM_1D_ARRAY?VK_IMAGE_TYPE_1D:e==Latte::E_DIM::DIM_3D?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;};
    auto consider=[&](Surface* s){
        if(s->imageType!=dimType(d.dim))return;
        if(!rendered||score(s)>score(rendered))rendered=s;};
    for(auto it=range.first;it!=range.second;++it) {
        auto* s=it->second.get();
        if(s->evicted)restore_evicted(s);  // (texture budget)
        if(!forRendering&&s->isDepth&&!d.isDepth&&s->gpuWritten&&s->width==d.width&&s->height==d.height)consider(s);
        if(s->isDepth!=d.isDepth)continue;
        // a render target is one level: don't adopt a texture made first by sampling the address
        // with a mip chain (rendering would define level 0 only; see metal_surfaces.mm, issue #47)
        if(forRendering&&s->mips>1)continue;
        // a volume and a 2D array of the same size are different images (a volume's slices are its depth)
        bool sameVolume=(s->imageType==VK_IMAGE_TYPE_3D)==(dimType(d.dim)==VK_IMAGE_TYPE_3D);
        if(s->width==d.width&&s->height==d.height&&s->format==d.format&&s->slices==d.slices&&sameVolume&&
           (forRendering||s->mips>=d.mips||s->gpuWritten)) {
            if(forRendering) { s->sizeClaim.request(d.sizeRequest,R.frame); return adopt_newer_alias(rescale(s)); }
            if(!exact||s->writeSeq>exact->writeSeq)exact=s;
        } else if(!forRendering&&s->gpuWritten&&(s->format&0x3f)==(d.format&0x3f))consider(s);
    }
    if(exact&&(exact->gpuWritten||!rendered||exact->writeSeq>rendered->writeSeq))return exact->gpuWritten?adopt_newer_alias(exact):exact;
    if(rendered)return rendered;if(exact)return exact;
    auto s=std::make_unique<Surface>();
    s->addr=d.addr;s->mipAddr=d.mipAddr;s->width=std::max(d.width,1u);s->height=std::max(d.height,1u);
    s->slices=std::max(d.slices,1u);s->pitch=d.pitch;s->mips=forRendering?1:std::max(d.mips,1u);
    s->format=d.format;s->dim=d.dim;s->tileMode=d.tileMode;s->swizzle=d.swizzle;s->isDepth=d.isDepth;
    s->fmt=d.isDepth&&!forRendering?comparison_format_info(d.format):format_info(d.format,d.isDepth);
    if(forRendering)s->sizeClaim.request(d.sizeRequest,R.frame);
    create_surface_image(s.get(),forRendering);
    auto* raw=s.get();cache.emplace(d.addr,std::move(s));++g_surface_epoch;++g_surface_set_gen;
    // format views of one guest surface (adopt_newer_alias): flag them once, so lookups stay cheap
    if(!raw->isDepth&&!raw->fmt.compressed&&raw->fmt.convert==Convert::NONE) {
        auto views=cache.equal_range(d.addr);
        for(auto it=views.first;it!=views.second;++it) {
            Surface* o=it->second.get();
            if(o!=raw&&!o->isDepth&&!o->fmt.compressed&&o->fmt.convert==Convert::NONE&&o->format!=raw->format&&(o->format&0x3F)==(raw->format&0x3F)&&
               o->fmt.hostBytesPerBlock==raw->fmt.hostBytesPerBlock)
                o->formatViews=raw->formatViews=true;
        }
    }
    if(!raw->isDepth&&static_cast<Latte::E_HWTILEMODE>(raw->tileMode)==Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)R.linearTargets.push_back(raw);
    return forRendering?adopt_newer_alias(raw):raw;
}
// level 1 and up start at the mip pointer without its swizzle bits and are tiled with that swizzle
// (texture_levels.h, issue #297)
static const CachedGuestLevel& level_in_guest(Surface* s,uint32_t level) {
    auto& cached = guest_level(s, level);
    if (cached.addressValid) return cached;
    if(level&&!s->mipAddr)exception_report::raise("GX2 texture mip chain address is missing");
    auto l=::gfx::texture_levels::level(s->addr,s->mipAddr,s->format,s->width,s->height,s->slices,s->dim,s->tileMode,s->swizzle,level);
    cached.address=l.address;cached.swizzle=l.swizzle;cached.addressValid=true;
    return cached;
}
static uint32_t mip_base(Surface* s,uint32_t level) { return level_in_guest(s,level).address; }
// Texture staging (WWHD_VK_TEXTURE_STAGING): "ring" copies each level through the recording submission's
// upload arena (backend.cpp allocate_upload; reused once that submission's fence completes) when it fits in
// a block the arena already has and the submission's texture staging stays within
// WWHD_VK_TEXTURE_STAGING_MB (default 16); anything else, and "buffer" (the default), gets its own staging
// buffer, freed after the submission. Idea after aurora's per-frame staging (MIT; lib/gfx/texture.cpp,
// recording.cpp); no code taken.
int g_texture_staging_override=-1;
uint64_t g_stat_staging_ring, g_stat_staging_ring_bytes, g_stat_staging_buffers, g_stat_staging_buffer_bytes;
static UploadSlice texture_staging_slice(const Surface* s,size_t size) {
    static const bool ring=[]{const char* e=getenv("WWHD_VK_TEXTURE_STAGING");
        if(e&&*e)LOG("[vulkan] texture staging: %s (WWHD_VK_TEXTURE_STAGING=%s)",strcmp(e,"ring")?"own buffers":"upload arena",e);
        return e&&!strcmp(e,"ring");}();
    static const VkDeviceSize budget=[]{const char* e=getenv("WWHD_VK_TEXTURE_STAGING_MB");
        return VkDeviceSize(e&&*e?std::max(0,atoi(e)):16)<<20;}();
    if(!(g_texture_staging_override<0?ring:g_texture_staging_override==1)||R.textureStagingBytes+size>budget)return UploadSlice{};
    // copy offsets: a multiple of the texel block size and of 4 (Vulkan's bufferOffset rules)
    const VkDeviceSize alignment=std::lcm<VkDeviceSize>(16,std::max<VkDeviceSize>(1,s->fmt.hostBytesPerBlock));
    rprof::UploadKind kind(rprof::kUpTexture);
    UploadSlice slice=allocate_upload_no_grow(size,alignment);
    if(slice.buffer){R.textureStagingBytes+=size;++g_stat_staging_ring;g_stat_staging_ring_bytes+=size;}
    return slice;
}
// ---------------------------------------------------------------- texture budget
// WWHD_VK_TEXTURE_BUDGET=1: the images of textures the CPU wrote at the guest size (never of surfaces the GPU wrote:
// their only copy is the image) are released, least recently looked up first, while their bytes exceed the budget
// (WWHD_VK_TEXTURE_BUDGET_MB, default a quarter of the device's memory budget, VK_EXT_memory_budget), and with
// WWHD_VK_TEXTURE_IDLE_FRAMES=n also once unused for n frames. Textures looked up in the last WWHD_VK_TEXTURE_PIN_FRAMES
// (default 3) frames are pinned. A released texture keeps its Surface (every pointer to it stays valid); its next
// lookup makes the image again and uploads it from guest memory. Release goes through destroy_surface_image (the
// image is freed when the submissions using it have finished). Idea after aurora's byte-budget LRU with pinning and
// idle sweep (MIT; lib/gfx/lru_cache.hpp, lib/gx/texture.cpp); no code taken.
uint64_t g_stat_tex_evictions, g_stat_tex_evicted_bytes, g_stat_tex_restores, g_stat_tex_resident_bytes;
// CPU-written textures at the guest size (a surface the GPU drew into, or one made at an internal resolution for
// rendering, is never released: the image is its only copy or not the guest's shape)
static bool evictable(const Surface* s) {
    return s->image && !s->gpuWritten && !s->evicted && !s->isDepth && s->sx == 1 && s->sy == 1 &&
           s->extent.width == s->width && (s->imageType == VK_IMAGE_TYPE_1D || s->extent.height == s->height);
}
void release_texture_image(Surface* s) {
    if (!evictable(s)) return;
    ++g_stat_tex_evictions; g_stat_tex_evicted_bytes += s->allocationBytes;
    rprof::add_texture_release(s->allocationBytes);
    destroy_surface_image(s);
    s->evicted = true;
}
static void restore_evicted(Surface* s) {
    if (!s->evicted) return;
    s->evicted = false;
    create_surface_image(s, false);
    s->dirty = true; s->contentHash = 0; s->watched = false; s->lastCheckedFrame = ~0ull;
    ++g_surface_epoch; ++g_stat_tex_restores;
    rprof::add_texture_remake();
}
void texture_budget_step() {
    static const bool on = [] { const char* e = getenv("WWHD_VK_TEXTURE_BUDGET"); return e && !strcmp(e, "1"); }();
    if (!on || R.frame % 30) return;
    extern uint64_t vulkan_device_memory_budget();
    static const uint64_t budget = [] {
        const char* e = getenv("WWHD_VK_TEXTURE_BUDGET_MB");
        const uint64_t bytes = e && *e ? uint64_t(std::max(0, atoi(e))) << 20 : vulkan_device_memory_budget() / 4;
        LOG("[vulkan] texture budget: %.0f MiB (WWHD_VK_TEXTURE_BUDGET_MB=%s)", bytes / 1048576.0, e && *e ? e : "a quarter of the device budget");
        return bytes;
    }();
    static const uint64_t idle = [] { const char* e = getenv("WWHD_VK_TEXTURE_IDLE_FRAMES"); return e && *e ? uint64_t(std::max(0, atoi(e))) : 0; }();
    static const uint64_t pin = [] { const char* e = getenv("WWHD_VK_TEXTURE_PIN_FRAMES"); return e && *e ? uint64_t(std::max(1, atoi(e))) : 3; }();
    std::vector<Surface*> surfaces;
    std::vector<texture_budget::Candidate> candidates;
    uint64_t total = 0;
    for (auto& [addr, owned] : R.surfaces) {
        Surface* s = owned.get();
        if (!evictable(s)) continue;
        total += s->allocationBytes;
        candidates.push_back({s->lastUsedFrame, s->allocationBytes, surfaces.size()});
        surfaces.push_back(s);
    }
    for (size_t i : texture_budget::select(std::move(candidates), total, R.frame, budget, idle, pin)) {
        total -= std::min<uint64_t>(total, surfaces[i]->allocationBytes);
        release_texture_image(surfaces[i]);
    }
    g_stat_tex_resident_bytes = total;
}
void upload_surface(Surface* s) {
    if(!s)return;
    s->lastUsedFrame=R.frame;
    if(s->evicted)restore_evicted(s);
    if(!s->image||s->gpuWritten)return;
    // All callers, including render attachments, share the once-per-frame
    // check. Invalidation resets lastCheckedFrame for writes within a frame.
    if (s->lastCheckedFrame == R.frame) return;
    s->lastCheckedFrame = R.frame;
    // Has the CPU changed it since the last check? Exact with write tracking (write_watch.h): the
    // pages of every level were write-protected at that check, so any write since (guest code, HLE
    // copies, a save-state restore) has stamped them. Changes the game announces (GX2Invalidate on the
    // range, GX2CopySurface into it, save-state loads) set dirty. Without write tracking (page
    // protection unavailable on the host): sampled words of every level each frame plus a full check
    // every 64 frames, which can show a changed texture late.
    uint32_t levels=s->mips;
    std::array<std::pair<uint32_t,uint32_t>,16> ranges{};
    if(levels>ranges.size())exception_report::raise("GX2 texture has too many mip levels");
    for(uint32_t level=0;level<levels;++level) {
        uint32_t base=mip_base(s,level);
        ranges[level]={base,uint32_t(std::min<uint64_t>(guest_info(s,level).surfSize,0x100000000ull-base))};
    }
    s->dataSize=ranges[0].second;
    bool full = s->dirty || !s->watched;
    if (wwatch::active()) {
        for(uint32_t level=0;level<levels&&!full;++level)full=wwatch::written_since(ranges[level].first,ranges[level].second,s->watchStamp);
        if (!full) return;
        // arm before reading: a write from now on faults and stamps the pages after this stamp
        uint64_t stamp=~0ull;
        for(uint32_t level=0;level<levels;++level)stamp=std::min(stamp,wwatch::arm(ranges[level].first,ranges[level].second));
        s->watchStamp=stamp;
    } else {
        full = full || ((R.frame + (s->addr >> 12)) & 63) == 0;
        uint64_t sparse = sparse_hash(s);
        if (!full && sparse == s->sparseHash) return;
        s->sparseHash = sparse;
    }
    s->watched = true;
    ++g_stat_full_checks;
    uint64_t hash=1469598103934665603ull,hashedBytes=0;
    for(uint32_t level=0;level<levels;++level) {
        hash=(hash^content_hash(mem::ptr(ranges[level].first),size_t(ranges[level].second)))*1099511628211ull;
        hashedBytes+=ranges[level].second;
    }
    if(!s->dirty&&hash==s->contentHash){rprof::add_texture_check(hashedBytes,false);return;}
    rprof::add_texture_check(hashedBytes,true);
    // A render target at another size than the guest's (stereo renders each eye narrower, stereo.h):
    // upload at the guest size, then scale into it.
    Surface guestSized;
    Surface* target=s;
    const bool smaller=s->mips==1&&s->imageType==VK_IMAGE_TYPE_2D&&!s->fmt.compressed&&
                       (s->extent.width<s->width||s->extent.height<s->height);
    // an eye allocated before full-size eyes were chosen (eye_sized): full size now, no scaling
    if(smaller&&!eye_sized(s->fmt)&&(s->usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
        rescale(s);
    if(s->mips==1&&s->imageType==VK_IMAGE_TYPE_2D&&!s->fmt.compressed&&
       (s->extent.width<s->width||s->extent.height<s->height)) {
        guestSized.width=s->width;guestSized.height=s->height;guestSized.slices=s->slices;guestSized.dim=s->dim;
        guestSized.format=s->format;guestSized.fmt=s->fmt;guestSized.isDepth=s->isDepth;
        create_surface_image(&guestSized,false);
        target=&guestSized;
    }
    end_encoder();transition_image(target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    // hand-drawn mip levels (gfx/artistic_mips.h): the levels are kept for the check after the upload
    const bool checkMips=::gfx::artistic_mips::enabled()&&s->mips>1&&::gfx::artistic_mips::supported(s->format);
    std::vector<std::vector<uint8_t>> levelsForCheck;
    for(uint32_t level=0;level<s->mips;++level) {
        std::vector<uint8_t> data;uint32_t w,h,slices;decode_level(s,level,mip_base(s,level),data,w,h,slices);
        if(checkMips)levelsForCheck.push_back(data);
        if (s->bcDecoded) { bc_decode_upload(target,level,data,w,h,slices); continue; }
        std::vector<VkBufferImageCopy> copies;std::vector<uint8_t> packed;
        bool threeD=s->imageType==VK_IMAGE_TYPE_3D;
        uint32_t layers=threeD?1:slices;
        if(s->fmt.stencil) {
            // Vulkan buffer/image copies use separate tightly packed depth and stencil aspects.
            size_t count=size_t(w)*h*slices;
            packed.resize(count*5);
            for(size_t i=0;i<count;++i) {memcpy(packed.data()+i*4,data.data()+i*8,4);packed[count*4+i]=data[i*8+4];}
            VkBufferImageCopy depth{};depth.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,level,0,layers};depth.imageExtent={w,h,threeD?slices:1};
            copies.push_back(depth);auto stencil=depth;stencil.bufferOffset=count*4;stencil.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;copies.push_back(stencil);
        } else {
            packed=std::move(data);
            VkBufferImageCopy copy{};copy.imageSubresource={s->aspect,level,0,layers};copy.imageExtent={w,h,threeD?slices:1};copies.push_back(copy);
        }
        const UploadSlice ring=texture_staging_slice(s,packed.size());
        if(ring.buffer) {
            memcpy(ring.mapped,packed.data(),packed.size());
            for(auto& copy:copies)copy.bufferOffset+=ring.offset;
            record::vkCmdCopyBufferToImage(command_buffer(),ring.buffer,target->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(copies.size()),copies.data());
            continue;
        }
        Buffer staging=create_buffer(packed.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(!staging.mapped){defer_buffer(staging);exception_report::raise("Vulkan texture staging allocation is not mapped");}
        memcpy(staging.mapped,packed.data(),packed.size());
        rprof::add_upload(rprof::kUpTexture,packed.size());
        ++g_stat_staging_buffers;g_stat_staging_buffer_bytes+=packed.size();
        record::vkCmdCopyBufferToImage(command_buffer(),staging.buffer,target->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(copies.size()),copies.data());
        defer_buffer(staging);
    }
    if(target!=s) {
        resample(target,s,s->arrayLayers);
        destroy_surface_image(target);
    }
    transition_image(s,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if(checkMips) {
        const auto v=::gfx::artistic_mips::check(s->format,s->width,s->height,uint32_t(levelsForCheck.size()),
            [&](uint32_t n,const uint8_t*& p,size_t& size){
                if(n>=levelsForCheck.size())return false;
                p=levelsForCheck[n].data();size=levelsForCheck[n].size();return true;});
        if(::gfx::artistic_mips::log_enabled()&&v.artistic!=s->artisticMips)
            LOG("[artistic mips] %08X %ux%u fmt %X mips %u: %s",s->addr,s->width,s->height,s->format,s->mips,
                v.artistic?"hand-drawn":"downscaled");
        s->artisticMips=v.artistic;
    }
    s->contentHash=hash;s->writeSeq=next_write_seq();s->dirty=false;++g_stat_uploads;++g_surface_epoch;
}
void clear_color(const uint32_t*,uint32_t cb,const float rgba[4]) {
    uint32_t first,num;auto* s=surface_from_color_buffer(cb,&first,&num);if(!s)return;
    if(s->fmt.depth||s->fmt.compressed)exception_report::raise("GX2 clear color requires an uncompressed color surface");
    VkClearColorValue value{};
    for(unsigned i=0;i<4;++i) {
        double integerValue = std::isnan(rgba[i]) ? 0.0 : double(rgba[i]);
        if(s->fmt.kind==FormatInfo::UINT)value.uint32[i]=uint32_t(std::clamp(integerValue,0.0,4294967295.0));
        else if(s->fmt.kind==FormatInfo::SINT)value.int32[i]=int32_t(std::clamp(integerValue,-2147483648.0,2147483647.0));
        else value.float32[i]=rgba[i];
    }
    end_encoder();
    if(s->imageType==VK_IMAGE_TYPE_3D&&(first||num<s->extent.depth)) {
        // part of a volume: a transfer clear covers all of its depth, so clear each slice as an attachment
        transition_image(s,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        for(uint32_t z=first;z<first+num;++z) {
            VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            a.imageView=layer_view(s,z);a.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.clearValue.color=value;
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea.extent={s->extent.width,s->extent.height};ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&a;
            auto cmd=command_buffer();
            begin_pass(cmd,ri,[&] { return color_pass(s->fmt.pixel,s->layout); });end_pass(cmd);
        }
        mark_gpu_written(s);return;
    }
    transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    // a volume has one layer; its depth is in the extent
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,s->imageType==VK_IMAGE_TYPE_3D?0:first,s->imageType==VK_IMAGE_TYPE_3D?1:num};
    record::vkCmdClearColorImage(command_buffer(),s->image,s->layout,&value,1,&range);mark_gpu_written(s);
}
void clear_depth_stencil(const uint32_t*,uint32_t db,float depth,uint32_t stencil,uint32_t flags) {
    uint32_t first,num;auto* s=surface_from_depth_buffer(db,&first,&num);if(!s)return;
    VkImageAspectFlags aspects=0;if(flags&1)aspects|=VK_IMAGE_ASPECT_DEPTH_BIT;if((flags&2)&&s->fmt.stencil)aspects|=VK_IMAGE_ASPECT_STENCIL_BIT;
    if(!aspects)return;
    end_encoder();transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearDepthStencilValue value{depth,stencil};VkImageSubresourceRange range{aspects,0,1,first,num};
    record::vkCmdClearDepthStencilImage(command_buffer(),s->image,s->layout,&value,1,&range);mark_gpu_written(s);
}
// ---------------------------------------------------------------- write-back to guest memory
// Render results stay on the GPU, except where the game reads them with the CPU: render targets and
// GX2CopySurface destinations with a linear tile mode (as Cemu reads back linear surfaces). The Picto
// Box renders its picture into a linear-aligned target and JPEG-encodes it from guest memory (issue
// #53). Texels are written at the guest size; only formats whose host texels are the guest's bytes.
static bool linear_tile_mode(Latte::E_HWTILEMODE tm) {
    return tm==Latte::E_HWTILEMODE::TM_LINEAR_GENERAL||tm==Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED;
}
static bool can_write_back(const Surface* img) {
    const FormatInfo& f=img->fmt;
    bool ok=!f.compressed&&!f.depth&&f.convert==Convert::NONE&&f.hostBytesPerBlock==f.bytesPerBlock&&img->imageType==VK_IMAGE_TYPE_2D;
    if(!ok) {
        static bool logged=false;
        if(!logged)LOG("[gfx] Vulkan: linear surface %08X (format %X) is not written back to guest memory",img->addr,img->format);
        logged=true;
    }
    return ok;
}
// the texels of one layer of img at the guest size w x h, read from the GPU (waits for it)
static std::vector<uint8_t> read_guest_texels(Surface* img,uint32_t layer,uint32_t w,uint32_t h) {
    const uint32_t bytes=img->fmt.bytesPerBlock;
    Surface temp;Surface* src=img;
    if(img->extent.width!=img->width||img->extent.height!=img->height) {
        // a render target at the internal resolution: filter it down to the guest size first
        temp.width=img->width;temp.height=img->height;temp.slices=std::max(img->slices,1u);temp.dim=img->dim;temp.format=img->format;temp.fmt=img->fmt;
        create_surface_image(&temp,false);
        src=&temp;
    }
    Buffer b=create_readback_buffer(VkDeviceSize(w)*h*bytes);
    try {
        if(src==&temp)resample(img,&temp,temp.arrayLayers,1,1,0,0);
        transition_image(src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,layer,1};region.imageExtent={w,h,1};
        auto cmd=command_buffer();record::vkCmdCopyImageToBuffer(cmd,src->image,src->layout,b.buffer,1,&region);
        derive_dependency(b.use,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        transition_buffer(b,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
        transition_image(src,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        flush();  // waits for the GPU
        const auto* raw=static_cast<const uint8_t*>(b.mapped);
        std::vector<uint8_t> texels(raw,raw+size_t(w)*h*bytes);
        defer_buffer(b);if(src==&temp)destroy_surface_image(&temp);
        return texels;
    } catch(...) { defer_buffer(b);if(src==&temp)destroy_surface_image(&temp);throw; }
}
// GX2CopySurface done on the GPU into a linear destination: the game reads it once the call returns
static void write_back_linear_copy(Surface* img,uint32_t layer,GX2Surface* d,uint32_t dbase,uint32_t dstMip,uint32_t dstSlice,uint32_t w,uint32_t h) {
    // Guest memory has one canonical picture. The other eye stays GPU-local.
    extern int stereoEye;
    if (stereoEye) return;
    LatteAddrLib::AddrSurfaceInfo_OUT di{};
    LatteAddrLib::GX2CalculateSurfaceInfo(d->format,d->width,d->height,d->depth,d->dim,d->tileMode,d->aa,dstMip,&di);
    auto dtm=static_cast<Latte::E_HWTILEMODE>(di.hwTileMode);
    if(!linear_tile_mode(dtm)||dstSlice>=di.depth||!can_write_back(img))return;
    const uint32_t bytes=img->fmt.bytesPerBlock;
    const uint64_t r0=rprof::now_ns();
    auto texels=read_guest_texels(img,layer,w,h);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
        memcpy(mem::ptr(dbase+element_offset(di,dtm,x,y,dstSlice,bytes*8,d->swizzle,nullptr,false)),texels.data()+(size_t(y)*w+x)*bytes,bytes);
    img->writtenBackSeq=img->writeSeq;
    rprof::add_write_back(0,1,texels.size(),rprof::now_ns()-r0);
}
// GX2DrawDone: linear render targets drawn since their last write-back
void write_back_linear_targets() {
    const uint64_t t0=rprof::now_ns();uint64_t readNs=0,bytesDone=0;uint32_t count=0;
    // linear aligned only: a tile mode of 0 can also be GX2's "default" (a tiled copy destination)
    for(Surface* s:R.linearTargets) {
        if(!s->gpuWritten||s->writtenBackSeq==s->writeSeq||!s->image)continue;
        s->writtenBackSeq=s->writeSeq;
        if(s->arrayLayers!=1||!can_write_back(s))continue;
        if(bgshaders::enabled())bgshaders::tracker().read_back(s->addr,R.frame);  // read by the CPU: draws stay exact
        const uint32_t addr=s->addr;
        const uint32_t bytes=s->fmt.bytesPerBlock,pitch=std::max(s->pitch,s->width);
        const uint64_t r0=rprof::now_ns();
        auto texels=read_guest_texels(s,0,s->width,s->height);
        for(uint32_t y=0;y<s->height;++y)
            memcpy(mem::ptr(addr+y*pitch*bytes),texels.data()+size_t(y)*s->width*bytes,size_t(s->width)*bytes);
        readNs+=rprof::now_ns()-r0;bytesDone+=texels.size();++count;
    }
    rprof::add_write_back(std::max<uint64_t>(1,rprof::now_ns()-t0-readNs),count,bytesDone,readNs);
}
void copy_surface_impl(uint32_t srcAddr,uint32_t srcMip,uint32_t srcSlice,uint32_t dstAddr,uint32_t dstMip,uint32_t dstSlice) {
    auto* s=reinterpret_cast<GX2Surface*>(mem::ptr(srcAddr));auto* d=reinterpret_cast<GX2Surface*>(mem::ptr(dstAddr));
    if(srcMip>=uint32_t(s->numLevels)||dstMip>=uint32_t(d->numLevels))exception_report::raise("GX2CopySurface mip is out of range");
    uint32_t sbase=level_address(s,srcMip),dbase=level_address(d,dstMip);
    uint32_t w=std::max<uint32_t>(uint32_t(s->width)>>srcMip,1),h=std::max<uint32_t>(uint32_t(s->height)>>srcMip,1);
    uint32_t dw=std::max<uint32_t>(uint32_t(d->width)>>dstMip,1),dh=std::max<uint32_t>(uint32_t(d->height)>>dstMip,1);
    uint32_t cw=std::min(w,dw),ch=std::min(h,dh);
    Surface* gpuSrc=nullptr;uint32_t gpuLevel=0;
    // the most recent GPU image of the source; of its format views, the one in the source's format
    // (a newer view in another format is copied into it first, adopt_newer_alias)
    const uint32_t srcFormat=uint32_t(s->format.value());
    auto better=[&](Surface* image){return !gpuSrc||std::make_pair(image->format==srcFormat,image->writeSeq)>std::make_pair(gpuSrc->format==srcFormat,gpuSrc->writeSeq);};
    for(auto& [addr,image]:stereo_copy_cache(sbase,w,h,s->depth)) {
        if(!image->gpuWritten)continue;
        if(addr==sbase&&image->width==w&&image->height==h) {if(better(image.get())){gpuSrc=image.get();gpuLevel=0;}}
        else if(addr==uint32_t(s->imagePtr)&&image->mips>srcMip&&std::max(image->width>>srcMip,1u)==w&&std::max(image->height>>srcMip,1u)==h)
            if(better(image.get())){gpuSrc=image.get();gpuLevel=srcMip;}
    }
    if(gpuSrc&&gpuLevel==0)gpuSrc=adopt_newer_alias(gpuSrc);
    // Shader compile (background_shaders.h): a target copied from now and then keeps its draws exact
    if(gpuSrc&&bgshaders::enabled())bgshaders::tracker().copied(gpuSrc->addr,R.frame);
    if(gpuSrc) {
        if(gpuSrc->imageType==VK_IMAGE_TYPE_3D)exception_report::raise("Vulkan GPU GX2CopySurface volume slices are unsupported");
        SurfaceDesc dd;dd.addr=dbase;dd.width=dw;dd.height=dh;dd.pitch=d->pitch;dd.format=uint32_t(d->format.value());
        dd.tileMode=uint32_t(d->tileMode.value());dd.swizzle=d->swizzle;dd.isDepth=gpuSrc->isDepth;
        dd.dim=uint32_t(d->dim.value());dd.slices=std::max<uint32_t>(d->depth,1);
        if(dd.dim==uint32_t(Latte::E_DIM::DIM_2D)||dd.dim==uint32_t(Latte::E_DIM::DIM_1D))dd.slices=1;
        auto* dst=find_or_create_surface(dd,true);
        if(!dst||dst->fmt.pixel!=gpuSrc->fmt.pixel)exception_report::raise("Vulkan GPU GX2CopySurface format conversion is unsupported");
        if(srcSlice>=gpuSrc->arrayLayers||dstSlice>=dst->arrayLayers)exception_report::raise("GX2CopySurface array slice is out of range");
        if(gpuSrc==dst&&gpuLevel==0&&srcSlice==dstSlice)return;
        bool self=gpuSrc->image==dst->image;
        uint32_t sw=std::min(uint32_t(std::lround(cw*gpuSrc->sx)),std::max(gpuSrc->extent.width>>gpuLevel,1u));
        uint32_t sh=std::min(uint32_t(std::lround(ch*gpuSrc->sy)),std::max(gpuSrc->extent.height>>gpuLevel,1u));
        uint32_t tw=std::min(uint32_t(std::lround(cw*dst->sx)),dst->extent.width),th=std::min(uint32_t(std::lround(ch*dst->sy)),dst->extent.height);
        const ScaledCopy mode=sw!=tw||sh!=th?scaled_copy_mode(dst->fmt):ScaledCopy::Blit;
        if(mode!=ScaledCopy::Blit) {
            // a scaled copy the device cannot blit: drawn for depth (draw_depth_copy); otherwise, or
            // within one image, skipped (logged once by scaled_copy_mode), the destination keeping
            // its contents, rather than aborting the game
            if(mode!=ScaledCopy::Draw||self||!(gpuSrc->usage&VK_IMAGE_USAGE_SAMPLED_BIT)||
               !(dst->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))return;
            draw_depth_copy(gpuSrc,gpuLevel,srcSlice,sw,sh,dst,dstSlice,tw,th,1);
            mark_gpu_written(dst);
            write_back_linear_copy(dst,dstSlice,d,dbase,dstMip,dstSlice,cw,ch);
            return;
        }
        end_encoder();
        transition_image(gpuSrc,self?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT|(self?VK_ACCESS_TRANSFER_WRITE_BIT:0));
        if(!self)transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        for(auto aspect:{VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_ASPECT_STENCIL_BIT}) {
            if(!(gpuSrc->aspect&aspect))continue;
            if(sw==tw&&sh==th) {
                VkImageCopy region{};region.srcSubresource={VkImageAspectFlags(aspect),gpuLevel,srcSlice,1};region.dstSubresource={VkImageAspectFlags(aspect),0,dstSlice,1};region.extent={sw,sh,1};
                record::vkCmdCopyImage(command_buffer(),gpuSrc->image,gpuSrc->layout,dst->image,dst->layout,1,&region);
            } else {
                VkFormatProperties fp{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,dst->fmt.pixel,&fp);  // blits supported (above)
                VkImageBlit region{};region.srcSubresource={VkImageAspectFlags(aspect),gpuLevel,srcSlice,1};region.dstSubresource={VkImageAspectFlags(aspect),0,dstSlice,1};
                region.srcOffsets[1]={int32_t(sw),int32_t(sh),1};region.dstOffsets[1]={int32_t(tw),int32_t(th),1};
                VkFilter filter=aspect==VK_IMAGE_ASPECT_COLOR_BIT&&dst->fmt.kind==FormatInfo::FLOAT&&(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
                record::vkCmdBlitImage(command_buffer(),gpuSrc->image,gpuSrc->layout,dst->image,dst->layout,1,&region,filter);
            }
        }
        transition_image(gpuSrc,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);if(!self)transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mark_gpu_written(dst);
        write_back_linear_copy(dst,dstSlice,d,dbase,dstMip,dstSlice,cw,ch);
        return;
    }
    auto sf=format_info(uint32_t(s->format.value()),bool(uint32_t(s->format.value())&0x800));
    auto df=format_info(uint32_t(d->format.value()),bool(uint32_t(d->format.value())&0x800));
    if(sf.pixel==VK_FORMAT_UNDEFINED||df.pixel==VK_FORMAT_UNDEFINED||sf.bytesPerBlock!=df.bytesPerBlock||sf.compressed!=df.compressed)
        exception_report::raise("Unsupported CPU GX2CopySurface format layout");
    LatteAddrLib::AddrSurfaceInfo_OUT si{},di{};
    LatteAddrLib::GX2CalculateSurfaceInfo(s->format,s->width,s->height,s->depth,s->dim,s->tileMode,s->aa,srcMip,&si);
    LatteAddrLib::GX2CalculateSurfaceInfo(d->format,d->width,d->height,d->depth,d->dim,d->tileMode,d->aa,dstMip,&di);
    if(srcSlice>=si.depth||dstSlice>=di.depth)exception_report::raise("CPU GX2CopySurface slice is out of range");
    auto stm=static_cast<Latte::E_HWTILEMODE>(si.hwTileMode),dtm=static_cast<Latte::E_HWTILEMODE>(di.hwTileMode);
    uint32_t bpp=sf.bytesPerBlock*8,bw=sf.compressed?(cw+3)/4:cw,bh=sf.compressed?(ch+3)/4:ch;
    uint32_t sswz=s->swizzle,dswz=d->swizzle;LatteAddrLib::CachedSurfaceAddrInfo sci{},dci{};
    if(Latte::TM_IsMacroTiled(stm))LatteAddrLib::SetupCachedSurfaceAddrInfo(&sci,srcSlice,0,bpp,si.pitch,si.height,si.depth,1,stm,(sf.depth||sf.convert==Convert::D24_R32F),(sswz>>8)&1,(sswz>>9)&3);
    if(Latte::TM_IsMacroTiled(dtm))LatteAddrLib::SetupCachedSurfaceAddrInfo(&dci,dstSlice,0,bpp,di.pitch,di.height,di.depth,1,dtm,(df.depth||df.convert==Convert::D24_R32F),(dswz>>8)&1,(dswz>>9)&3);
    // Gather before storing so overlapping guest ranges survive a change in tiling geometry.
    std::vector<uint8_t> rows(size_t(bw)*bh*sf.bytesPerBlock);
    for(uint32_t y=0;y<bh;++y)for(uint32_t x=0;x<bw;++x) {
        uint32_t offset=element_offset(si,stm,x,y,srcSlice,bpp,sswz,&sci,(sf.depth||sf.convert==Convert::D24_R32F));
        memcpy(rows.data()+(size_t(y)*bw+x)*sf.bytesPerBlock,mem::ptr(sbase+offset),sf.bytesPerBlock);
    }
    for(uint32_t y=0;y<bh;++y)for(uint32_t x=0;x<bw;++x) {
        uint32_t offset=element_offset(di,dtm,x,y,dstSlice,bpp,dswz,&dci,(df.depth||df.convert==Convert::D24_R32F));
        memcpy(mem::ptr(dbase+offset),rows.data()+(size_t(y)*bw+x)*sf.bytesPerBlock,sf.bytesPerBlock);
    }
    for(auto& [address,image]:stereo_copy_cache(dbase,dw,dh,d->depth))if(address==dbase){image->gpuWritten=false;image->dirty=true;image->lastCheckedFrame=~0ull;}
    ++g_surface_epoch;
}
void copy_surface(uint32_t src,uint32_t srcMip,uint32_t srcSlice,uint32_t dst,uint32_t dstMip,uint32_t dstSlice) {
    copy_surface_impl(src,srcMip,srcSlice,dst,dstMip,dstSlice);
}
void invalidate(uint32_t flags,uint32_t addr,uint32_t size) {
    ++g_stat_invalidates; // once per guest invalidate; the stereo eye cache is a second pass of the same one
    buffer_cache_guest_invalidate(flags,addr,size);
    invalidate_surface_cache(R.surfaces,flags,addr,size);
}
// One cached surface against an invalidated range [addr, end): whether the range reaches the
// surface's base bytes or one of its mip levels. Fills the surface's cached mip ranges as it goes.
static bool invalidation_hits(Surface* s,uint32_t base,uint32_t addr,uint64_t end) {
    if(s->gpuWritten||(base>=0xF4000000&&base<0xF6000000))return false;
    // Already pending a fresh upload/check: another write cannot further
    // invalidate it. Count only range checks that actually reset state.
    if(s->dirty&&s->lastCheckedFrame==~0ull)return false;
    uint64_t bytes=std::max<uint64_t>(s->dataSize,uint64_t(s->pitch)*s->height*s->fmt.bytesPerBlock);
    bool baseHit=uint64_t(base)<end&&uint64_t(addr)<uint64_t(base)+bytes;
    bool mipHit=false;
    if(!baseHit&&s->mipAddr&&s->mips>1) {
        // Validate all guest geometry before reading shared cached ranges.
        // Base bytes stay dynamic: dataSize and pitch can change separately.
        guest_level(s,0);
        auto& layout=*s->guestLayout;
        if(layout.mipRangesComplete) {
            if(layout.mipRangeBegin<end&&uint64_t(addr)<layout.mipRangeEnd)
                for(uint32_t level=1;level<s->mips&&!mipHit;++level) {
                    const auto& range=layout.levels[level];
                    mipHit=range.rangeBegin<end&&uint64_t(addr)<range.rangeEnd;
                }
        } else {
            // Preserve the old early-hit loop while ranges are incomplete.
            // A full traversal publishes the coarse interval atomically.
            for(uint32_t level=1;level<s->mips&&!mipHit;++level) {
                const auto& info=guest_info(s,level);
                uint64_t mip=mip_base(s,level);
                auto& range=layout.levels[level];
                if(!range.rangeValid) {
                    range.rangeBegin=mip;range.rangeEnd=mip+info.surfSize;
                    range.rangeValid=true;++layout.mipRangesCached;
                    layout.mipRangeBegin=std::min(layout.mipRangeBegin,range.rangeBegin);
                    layout.mipRangeEnd=std::max(layout.mipRangeEnd,range.rangeEnd);
                }
                mipHit=mip<end&&uint64_t(addr)<mip+info.surfSize;
            }
            layout.mipRangesComplete=layout.mipRangesCached==s->mips-1;
        }
    }
    return baseHit||mipHit;
}
// Every guest byte invalidation_hits can test for a surface: its base bytes at their largest (the
// level-0 size before the first upload sets dataSize) and all mip levels. The geometry of a cached
// surface never changes after find_or_create_surface made it, so the footprint stays valid while the
// surface set is the same. Anything unexpected gives the whole address space (always checked).
static void invalidation_footprint(Surface* s,uint32_t base,uint64_t& lo,uint64_t& hi) {
    lo=0;hi=~uint64_t{0};
    try {
        uint64_t bytes=std::max<uint64_t>(s->dataSize,uint64_t(s->pitch)*s->height*s->fmt.bytesPerBlock);
        bytes=std::max<uint64_t>(bytes,guest_info(s,0).surfSize);
        uint64_t l=base,h=uint64_t(base)+bytes;
        if(s->mipAddr&&s->mips>1)
            for(uint32_t level=1;level<s->mips;++level) {
                const uint64_t mip=mip_base(s,level);
                l=std::min(l,mip);h=std::max(h,mip+guest_info(s,level).surfSize);
            }
        lo=l;hi=h;
    } catch(...) {}
}
uint64_t g_surface_set_gen=0;
void invalidate_surface_cache(decltype(Renderer::surfaces)& surfaces,
                              uint32_t flags,uint32_t addr,uint32_t size) {
    if(!(flags&2)||size>=0x10000000)return;
    uint64_t end=uint64_t(addr)+size;
    auto mark=[](Surface* s){s->dirty=true;s->lastCheckedFrame=~0ull;++g_stat_invalidated_surfaces;};
    // WWHD_VK_INVALIDATE_INDEX=0: walk every surface (the previous way)
    static const bool indexed=[]{const char* e=getenv("WWHD_VK_INVALIDATE_INDEX");return !e||strcmp(e,"0")!=0;}();
    if(!indexed) {
        for(auto& [base,s]:surfaces) if(invalidation_hits(s.get(),base,addr,end)) mark(s.get());
        return;
    }
    // footprint index per surface cache (the eye-0 and eye-1 caches), rebuilt when the set changes
    struct Index { const void* map=nullptr; uint64_t gen=~uint64_t{0}; size_t size=0;
                   IntervalIndex<std::pair<Surface*,uint32_t>> index; };
    static Index indexes[2];
    Index* x=indexes[0].map==&surfaces?&indexes[0]:indexes[1].map==&surfaces?&indexes[1]:nullptr;
    if(!x) { x=indexes[0].map&&!indexes[1].map?&indexes[1]:&indexes[0]; x->map=&surfaces; x->gen=~uint64_t{0}; }
    if(x->gen!=g_surface_set_gen||x->size!=surfaces.size()) {
        std::vector<IntervalIndex<std::pair<Surface*,uint32_t>>::Entry> entries;
        entries.reserve(surfaces.size());
        for(auto& [base,s]:surfaces) {
            uint64_t lo,hi;invalidation_footprint(s.get(),base,lo,hi);
            entries.push_back({lo,hi,{s.get(),base}});
        }
        x->index.build(std::move(entries));
        x->gen=g_surface_set_gen;x->size=surfaces.size();
    }
    // WWHD_VK_INVALIDATE_VERIFY=1 (diagnostics): every surface the full walk would reset must be in the
    // index's answer
    static const bool verify=getenv("WWHD_VK_INVALIDATE_VERIFY")!=nullptr;
    if(verify) {
        static uint64_t checks=0,misses=0;
        std::vector<Surface*> found;
        x->index.query(addr,end,[&](const std::pair<Surface*,uint32_t>& p){found.push_back(p.first);});
        for(auto& [base,s]:surfaces) {
            ++checks;
            if(invalidation_hits(s.get(),base,addr,end)&&std::find(found.begin(),found.end(),s.get())==found.end()) {
                if(misses++<20) LOG("[vulkan invalidate] VERIFY MISS: surface %08X (%ux%u, %u mips) not in the index for %08X+%u",
                                    base,s->width,s->height,s->mips,addr,size);
            }
        }
        static uint64_t reported=0;
        if(R.frame>=reported+600) { reported=R.frame; LOG("[vulkan invalidate] verify: %llu surface checks, %llu misses",
                                                         (unsigned long long)checks,(unsigned long long)misses); }
    }
    x->index.query(addr,end,[&](const std::pair<Surface*,uint32_t>& p) {
        if(invalidation_hits(p.first,p.second,addr,end)) mark(p.first);
    });
}
void ss_reset_surfaces() {
    R.mainDepthAddr = 0;
    bgshaders::tracker().forget();
    ++g_surface_epoch;
    reset_ao_private_cache();
    for(auto& [addr,s]:R.surfaces){s->guestLayout.reset();s->dirty=true;s->lastCheckedFrame=~0ull;}
}
} // namespace gfxvk
