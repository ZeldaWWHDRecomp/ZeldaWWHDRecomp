#include "stereo.h"
#include "api.h"
#include "../hdr.h"
#include "overlay/hostui.h"
#include <cstdlib>
#include <optional>
#include <string>
namespace gfxvk {
int stereoEye = 0;
Screen stereoRight;
namespace {
decltype(R.surfaces) otherSurfaces;
decltype(R.linearTargets) otherLinearTargets;
uint32_t otherMainDepth = 0;
}
bool stereo_light_atlas(uint32_t width,uint32_t height,uint32_t layers,bool depth) {
    // WWHD's three 1024-square sun cascades are rendered in light space.
    // Scene depth, AO, the screen-space shadow mask and bloom remain per-eye.
    return depth && width==1024 && height==1024 && layers==3;
}
decltype(Renderer::surfaces)& stereo_surface_cache(const SurfaceDesc& desc) {
    if(stereoEye && stereo_light_atlas(desc.width,desc.height,desc.slices,desc.isDepth))
        return otherSurfaces; // canonical light-space atlas belongs to eye 0
    return R.surfaces;
}
decltype(Renderer::surfaces)& stereo_copy_cache(uint32_t addr,uint32_t width,uint32_t height,uint32_t layers) {
    if(stereoEye && stereo_light_atlas(width,height,layers,true)) {
        auto range=otherSurfaces.equal_range(addr);
        for(auto it=range.first;it!=range.second;++it) {
            const auto& s=*it->second;
            if(s.isDepth && s.width==width && s.height==height && s.slices==layers)
                return otherSurfaces;
        }
    }
    return R.surfaces;
}
void stereo_invalidate(uint32_t flags, uint32_t addr, uint32_t size) {
    // Invalidation changes CPU cache metadata only. Keep the active render
    // pass open and invalidate the shared buffer cache just once.
    invalidate(flags,addr,size);
    invalidate_surface_cache(otherSurfaces,flags,addr,size);
}
void stereo_select_eye(int eye) {
    if (stereoEye == eye) return;
    end_encoder();
    R.surfaces.swap(otherSurfaces);
    ++g_surface_set_gen;
    R.linearTargets.swap(otherLinearTargets);
    std::swap(R.mainDepthAddr, otherMainDepth);
    stereo_swap_ao();
    stereoEye = eye;
    // Surface lookups now resolve in the other eye's cache: end the fast path's continuation (backend.h).
    ++g_surface_epoch;
}
void stereo_latch() {
    const bool wasOff=stereo::frame.mode==stereo::Off;
    stereo::latch();
    if (stereo::frame.mode==stereo::Off) {
        if (!wasOff) {
            stereo_select_eye(0);
            // Retire images through the renderer's submission queue so any
            // pending GPU reads finish before their allocations are freed.
            for (auto& [addr,s]:otherSurfaces) destroy_surface_image(s.get());
            otherSurfaces.clear();
            ++g_surface_set_gen;
            otherLinearTargets.clear();
            ++g_surface_epoch;
            otherMainDepth=0;
            stereo_release_ao();
            destroy_surface_image(stereoRight.scan.get());
            destroy_surface_image(stereoRight.buildingScan.get());
            stereoRight.scan.reset();
            stereoRight.buildingScan.reset();
            stereoRight.buildingScanDirty=false;
            stereoRight.buildingScanSrgb=stereoRight.scanSrgb=false;
        }
        return;
    }
    if (!wasOff) return;
    // Switching on during play must preserve GPU-only caches (e.g. shadows
    // which the guest drew once). Guest RAM need not contain those pixels.
    std::vector<Surface*> seeds;
    for (auto& [addr,s]:R.surfaces)
        if(s->gpuWritten && s->image && s->imageType==VK_IMAGE_TYPE_2D && s->mips==1)
            seeds.push_back(s.get());
    stereo_select_eye(1);
    try {
        for (auto* s:seeds) {
            if(stereo_light_atlas(s->width,s->height,s->slices,s->isDepth)) continue;
            SurfaceDesc d;
            d.addr=s->addr;d.mipAddr=s->mipAddr;d.width=s->width;d.height=s->height;
            d.slices=s->slices;d.pitch=s->pitch;d.format=s->format;d.dim=s->dim;
            d.tileMode=s->tileMode;d.swizzle=s->swizzle;d.isDepth=s->isDepth;
            auto* dst=find_or_create_surface(d,true);
            resample(s,dst,s->arrayLayers);
            mark_gpu_written(dst);
            dst->writeSeq=s->writeSeq;
        }
    } catch (...) { stereo_select_eye(0);throw; }
    stereo_select_eye(0);
}
void load_saved_stereo_hdr() {
    std::string v;
    auto num=[&](const char* key) { return ::hostui::get(key,v) ? std::optional<float>(float(atof(v.c_str()))) : std::nullopt; };
    if(auto x=num("stereoMode"))stereo::set_mode(int(*x));
    if(auto x=num("stereoDepth"))stereo::set_depth(*x);
    if(auto x=num("stereoConvergence"))stereo::set_convergence(*x);
    if(auto x=num("stereoSwap"))stereo::requestedSwap=*x!=0;
#ifdef _WIN32
    if(auto x=num("hdrEnabled"))hdr::enabled=*x!=0;
#endif
    if(auto x=num("hdrPaperWhite"))hdr::set(hdr::paperWhite,*x,80,500);
    if(auto x=num("hdrPeak"))hdr::set(hdr::peak,*x,200,4000);
    if(auto x=num("hdrExposure"))hdr::set(hdr::exposure,*x,-3,3);
    if(auto x=num("hdrHighlights"))hdr::set(hdr::highlights,*x,0,1);
    if(auto x=num("hdrMenuWhite"))hdr::set(hdr::menuWhite,*x,80,500);
    stereo::latch();
}
}
