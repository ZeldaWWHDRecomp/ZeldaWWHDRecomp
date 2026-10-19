#include "stereo.h"
#include "api.h"
#include "deferred_dispatch.h"
#include "record_worker.h"
#include "../hdr.h"
#include "overlay/hostui.h"
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
namespace gfxvk {
int stereoEye = 0;
Screen stereoRight;
namespace {
decltype(R.surfaces) otherSurfaces;
decltype(R.linearTargets) otherLinearTargets;
uint32_t otherMainDepth = 0;
// The unselected eye's render pass (R.rendering and R.pass*).
struct EyePass {
    bool rendering = false, tracked = false;
    std::array<Surface*,8> colors{};
    std::array<uint32_t,8> slices{};
    Surface* depth = nullptr;
    uint32_t depthSlice = 0, width = 0, height = 0;
    uint32_t scope = UINT32_MAX; // GPU pass timestamp scope
} otherPass;
// Eye 1's held commands. Unbounded: a limit would emit them in place, inside eye 0's pass.
DeferredCommands eyeCommands{DeferredCommands::Limits{std::numeric_limits<size_t>::max(),
                                                      std::numeric_limits<size_t>::max()}};
int inOrderScopes = 0;
bool streaming() { return stereo::frame.mode != stereo::Off && !inOrderScopes; }
void swap_pass() {
    auto& slot = R.submissions[R.activeSubmission];
    std::swap(R.rendering, otherPass.rendering);
    std::swap(R.passTracked, otherPass.tracked);
    std::swap(R.passColors, otherPass.colors);
    std::swap(R.passSlices, otherPass.slices);
    std::swap(R.passDepth, otherPass.depth);
    std::swap(R.passDepthSlice, otherPass.depthSlice);
    std::swap(R.passWidth, otherPass.width);
    std::swap(R.passHeight, otherPass.height);
    std::swap(slot.activeRenderScope, otherPass.scope);
}
DeferredCommands* eye_stream() {
    if (!eyeCommands.capturing())
        eyeCommands.start_detached_capture([] {
            throw std::logic_error("stereo eye commands cannot be emitted out of order");
        });
    return &eyeCommands;
}
void attach_eye_stream() { eye_deferred_commands = stereoEye && streaming() ? eye_stream() : nullptr; }
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
    // One command stream (stereo off, StereoInOrder): the pass cannot stay open across a switch.
    if (!streaming()) end_encoder();
    swap_pass();
    R.surfaces.swap(otherSurfaces);
    R.linearTargets.swap(otherLinearTargets);
    std::swap(R.mainDepthAddr, otherMainDepth);
    stereoEye = eye;
    attach_eye_stream();
}
void stereo_flush() {
    // Eye 1's pass is open only while its begin is still held.
    if (!eyeCommands.command_count()) return;
    // End eye 1's pass at the end of its held commands, then eye 0's in the renderer's stream.
    if (stereoEye == 0) swap_pass();
    eye_deferred_commands = &eyeCommands;
    end_encoder();
    swap_pass();
    eye_deferred_commands = nullptr;
    end_encoder();
    if (stereoEye == 1) swap_pass();
    auto held = eyeCommands.take();
    try {
        // The record worker owns the renderer's stream; held commands follow its queued work.
        if (auto* recorder = active_deferred_commands; recorder && recorder->pod_mode())
            drain_record_commands();
        // Through the renderer's recorder, so they stay ordered behind a capture it holds.
        for (auto& command : held) record::emit_owned(std::move(command));
    } catch (...) { attach_eye_stream(); throw; }
    attach_eye_stream();
}
StereoInOrder::StereoInOrder() {
    stereo_flush();
    ++inOrderScopes;
    eye_deferred_commands = nullptr;
}
StereoInOrder::~StereoInOrder() {
    // The selected eye's pass may be open in the renderer's stream: eye 1's must not stay open
    // there once its commands are held again.
    if (stereoEye) end_encoder();
    --inOrderScopes;
    attach_eye_stream();
}
void stereo_latch() {
    // Frame boundary: the swap submitted eye 1's commands; nothing held may outlive it.
    stereo_flush();
    const bool wasOff=stereo::frame.mode==stereo::Off;
    stereo::latch();
    if (stereo::frame.mode==stereo::Off) {
        if (!wasOff) {
            stereo_select_eye(0);
            // Retire images through the renderer's submission queue so any
            // pending GPU reads finish before their allocations are freed.
            for (auto& [addr,s]:otherSurfaces) destroy_surface_image(s.get());
            otherSurfaces.clear();
            otherLinearTargets.clear();
            ++g_surface_epoch; // eye 1's fast-path continuation names released surfaces (backend.h)
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
    // The copies read eye 0's images: they must not wait in eye 1's held commands.
    StereoInOrder ordered;
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
