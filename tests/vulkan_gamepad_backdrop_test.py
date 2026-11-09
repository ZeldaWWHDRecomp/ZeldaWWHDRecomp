"""Exercise the actual Vulkan capture lifecycle with a recording-only fake GPU.

GPU state syntax and shaders are checked separately; this test asserts capture,
scan publication and retirement policy without opening a window or game.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'runtime/src/gfx/vulkan/gamepad_backdrop.cpp'
SHIM = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include <stdexcept>
using VkImageLayout=int;using VkPipelineStageFlags=int;using VkAccessFlags=int;
constexpr int VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL=1,VK_PIPELINE_STAGE_TRANSFER_BIT=2,VK_ACCESS_TRANSFER_WRITE_BIT=4,VK_IMAGE_ASPECT_COLOR_BIT=8;
struct VkClearColorValue { float float32[4]{}; };
struct VkImageSubresourceRange { int aspect;unsigned a,b,c,d; };
constexpr int VK_FORMAT_UNDEFINED=0,VK_FORMAT_R8G8B8A8_UNORM=37,VK_FORMAT_A2R10G10B10_UNORM_PACK32=58,
 VK_FORMAT_A2B10G10R10_UNORM_PACK32=64,VK_FORMAT_R16G16B16A16_SFLOAT=97;
constexpr unsigned VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT=1,VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT=0x80,
 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT=0x100,VK_FORMAT_FEATURE_TRANSFER_SRC_BIT=0x4000,VK_FORMAT_FEATURE_TRANSFER_DST_BIT=0x8000;
struct VkFormatProperties { unsigned linearTilingFeatures=0,optimalTilingFeatures=0,bufferFeatures=0; };
namespace gfxvk {
struct FormatInfo { int pixel=VK_FORMAT_R8G8B8A8_UNORM;unsigned bytesPerBlock=4,hostBytesPerBlock=4; };
struct Surface {
 uint64_t image=0;unsigned width=0,height=0,format=0x1a;FormatInfo fmt;float scale=0;int layout=0;uint64_t allocationBytes=0;
 struct { unsigned width=0,height=0,depth=1; } extent;
};
static struct {uint64_t frame=0;} R;
static unsigned promotedFeatures=~0u;
FormatInfo format_info(uint32_t gx2,bool) { return gx2==0x20?FormatInfo{VK_FORMAT_R16G16B16A16_SFLOAT,8,8}:FormatInfo{}; }
const VkFormatProperties& format_properties(int format) {
 static VkFormatProperties p;p={};p.optimalTilingFeatures=format==VK_FORMAT_R16G16B16A16_SFLOAT?promotedFeatures:~0u;return p;
}
static unsigned created=0,retired=0,clears=0,allocationAttempts=0;
static bool failAllocation=false;
static bool failDevice=false,failHost=false;
class VulkanFailure:public std::runtime_error {
 bool allocation;
public:explicit VulkanFailure(bool isAllocation):std::runtime_error("fake driver failure"),allocation(isAllocation){}
 bool allocation_failure()const{return allocation;}
};
void end_encoder() {}
void destroy_surface_image(Surface* s) { if(s->image){++retired;s->image=0;} }
void create_surface_image(Surface* s,bool) { ++allocationAttempts;if(failHost)throw std::bad_alloc();if(failAllocation)throw VulkanFailure(true);if(failDevice)throw VulkanFailure(false);s->image=++created;s->extent={s->width,s->height,1};s->allocationBytes=uint64_t(s->width)*s->height*s->fmt.hostBytesPerBlock; }
void transition_image(Surface* s,int layout,int,int) { s->layout=layout; }
int command_buffer() { return 1; }
void mark_gpu_written(Surface*) {}
namespace record { void vkCmdClearColorImage(int,uint64_t,int,const VkClearColorValue* c,int,const VkImageSubresourceRange*) {
 for(float v:c->float32)assert(v==0);++clears;
} }
}
'''
MAIN = r'''
int main() {
 using namespace gfxvk;
 namespace policy=gfx::gamepad_backdrop;
 Surface a,b;a.image=100;a.extent={1280,720,1};b.image=200;b.extent=a.extent;
 policy::set_capture_requested(false);
 gamepad_backdrop_scope(nullptr,true);
 assert(!gamepad_backdrop_begin_replay(&a,true)&&created==0);
 gamepad_backdrop_scope(nullptr,false);
 policy::set_capture_requested(true);
 // Only targets scanned out to the GamePad are captured: these two have been.
 gamepad_backdrop_scan(&a);gamepad_backdrop_scan(&b);gamepad_backdrop_publish();
 auto capture=[&](Surface* source) {
  gamepad_backdrop_scope(nullptr,true);
  assert(gamepad_backdrop_begin_replay(source,true));assert(gamepad_backdrop_replaying());
  assert(gamepad_backdrop_replay_target()!=source);
  gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
 };
 capture(&a);assert(clears==1&&!gamepad_backdrop_image());
 gamepad_backdrop_scan(&b);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 capture(&a);gamepad_backdrop_scan(&a);assert(!gamepad_backdrop_image());
 gamepad_backdrop_publish();auto* first=gamepad_backdrop_image();assert(first);
 capture(&a);gamepad_backdrop_scan(&a);assert(gamepad_backdrop_image()==first);
 gamepad_backdrop_publish();assert(gamepad_backdrop_image()!=first);
 // A new scan without its own pane cancels the old background, not re-present.
 assert(gamepad_backdrop_image());gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 // Nested scopes balance, one transparent clear for all draws of this pane.
 unsigned before=clears;gamepad_backdrop_scope(nullptr,true);gamepad_backdrop_scope(nullptr,true);
 assert(gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_end_replay(true);
 assert(gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_end_replay(true);
 gamepad_backdrop_scope(nullptr,false);gamepad_backdrop_scope(nullptr,false);assert(clears==before+1);
 gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(gamepad_backdrop_image());
 // Incompatible or failed draws invalidate the whole pane, not a partial capture.
 gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_end_replay(false);
 gamepad_backdrop_scope(nullptr,false);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&a,false));assert(!gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 // Clearing a source after its pane invalidates the capture before scanout.
 capture(&a);gamepad_backdrop_invalidate(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 // Reusing the same Surface object with a different VkImage is not a match.
 capture(&a);a.image=101;gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 // Explicit destruction invalidation also protects recycled numeric handles.
 capture(&a);gamepad_backdrop_invalidate(&a);a.image=101;gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();policy::enabled=false;assert(!gamepad_backdrop_image());policy::enabled=true;
 // A later failed pane cannot leave an earlier capture for the same source eligible.
 capture(&a);gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&a,false));gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 // Repeated same-size frames reuse a bounded pool rather than allocate per pane.
 policy::enabled=true;policy::set_capture_requested(true);
 for(unsigned frame=0;frame<8;++frame){capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();}
 const auto stable=created,stableAttempts=allocationAttempts;
 for(unsigned frame=0;frame<120;++frame){capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();}
 assert(created==stable&&allocationAttempts==stableAttempts&&imageCount<=kCaptureImages&&imageBytes<=kCaptureBytes);
 // A huge source is captured at the size cap (aspect kept), never at its own size.
 // Allocator failure falls back without publishing a capture (asked once per pane).
 Surface huge=a;huge.extent={65535,65535,1};auto attempts=allocationAttempts;
 gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&huge,true));
 assert(gamepad_backdrop_replay_target()->extent.width==1080&&gamepad_backdrop_replay_target()->extent.height==1080);
 gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
 assert(allocationAttempts<=attempts+1&&imageBytes<=kCaptureBytes);
 gamepad_backdrop_scan(&huge);gamepad_backdrop_publish();assert(gamepad_backdrop_image()->extent.width==1080);
 gamepad_backdrop_reset();policy::set_capture_requested(true);gamepad_backdrop_scan(&a);failAllocation=true;
 attempts=allocationAttempts;
 gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_scope(nullptr,false);
 assert(allocationAttempts==attempts+1);
 gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());failAllocation=false;
 failHost=true;gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_scope(nullptr,false);failHost=false;
 assert(allocationAttempts==attempts+2);
 // Device loss is not an optional capture miss: propagate to normal backend handling.
 failDevice=true;bool propagated=false;gamepad_backdrop_scope(nullptr,true);
 try { gamepad_backdrop_begin_replay(&a,true); } catch(const VulkanFailure&) { propagated=true; }
 assert(propagated);failDevice=false;gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_reset();assert(!gamepad_backdrop_image()&&!policy::capture_enabled()&&retired==created);
 // Ten-bit sources (2-bit alpha) capture into RGBA16 float, like Metal; the
 // source keeps its format. Eight-bit sources keep theirs.
 policy::set_capture_requested(true);
 Surface ten=a;ten.image=300;ten.format=0x19;ten.fmt={VK_FORMAT_A2B10G10R10_UNORM_PACK32,4,4};
 Surface ten2=ten;ten2.image=301;ten2.fmt.pixel=VK_FORMAT_A2R10G10B10_UNORM_PACK32;
 gamepad_backdrop_scan(&a);gamepad_backdrop_scan(&ten);gamepad_backdrop_scan(&ten2);
 gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&ten,true));
 auto* promoted=gamepad_backdrop_replay_target();
 assert(promoted->fmt.pixel==VK_FORMAT_R16G16B16A16_SFLOAT&&promoted->format==0x20&&promoted->allocationBytes==1280ull*720*8);
 assert(ten.fmt.pixel==VK_FORMAT_A2B10G10R10_UNORM_PACK32&&ten.format==0x19);
 gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&a,true));
 assert(gamepad_backdrop_replay_target()->fmt.pixel==VK_FORMAT_R8G8B8A8_UNORM&&gamepad_backdrop_replay_target()->format==0x1a);
 gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&ten2,true));
 assert(gamepad_backdrop_replay_target()->fmt.pixel==VK_FORMAT_R16G16B16A16_SFLOAT);
 gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
 // Without blendable colour-attachment support the capture keeps the source format.
 gamepad_backdrop_reset();policy::set_capture_requested(true);gamepad_backdrop_scan(&ten);
 promotedFeatures=~0u&~VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
 gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&ten,true));
 assert(gamepad_backdrop_replay_target()->fmt.pixel==VK_FORMAT_A2B10G10R10_UNORM_PACK32&&gamepad_backdrop_replay_target()->format==0x19);
 gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);promotedFeatures=~0u;
 gamepad_backdrop_reset();assert(imageCount==0&&imageBytes==0&&retired==created);
 // Video memory: captures stop (menu closed), images move to the spare pool and
 // go after kIdleFrames presentations without a capture.
 policy::set_capture_requested(true);gamepad_backdrop_scan(&a);R.frame=1000;
 for(unsigned frame=0;frame<4;++frame){capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();gamepad_backdrop_maintain();++R.frame;}
 assert(gamepad_backdrop_image()&&imageCount>0);
 const uint64_t closed=R.frame;
 gamepad_backdrop_scan(&a);gamepad_backdrop_publish();gamepad_backdrop_scan(&a);gamepad_backdrop_publish();
 assert(!gamepad_backdrop_image()&&imageCount>0);
 const uint64_t last=closed-1; // the frame of the last capture
 R.frame=last+policy::kIdleFrames-1;gamepad_backdrop_maintain();assert(imageCount>0);
 R.frame=last+policy::kIdleFrames;gamepad_backdrop_maintain();
 assert(imageCount==0&&imageBytes==0&&retired==created);
 // An idle period never takes the picture still shown (published), only spares.
 capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();auto* shown=gamepad_backdrop_image();assert(shown);
 R.frame+=10*policy::kIdleFrames;gamepad_backdrop_maintain();assert(gamepad_backdrop_image()==shown&&imageCount==1);
 // Option off: every capture goes at once, the published one included.
 capture(&a);gamepad_backdrop_scan(&a);capture(&a);assert(imageCount==3);
 policy::enabled=false;gamepad_backdrop_maintain();
 assert(imageCount==0&&imageBytes==0&&retired==created);
 policy::enabled=true;gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 // ... and the next capture after turning it on again works normally.
 capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(gamepad_backdrop_image());
 // Never inside a pane scope.
 gamepad_backdrop_scope(nullptr,true);policy::enabled=false;gamepad_backdrop_maintain();assert(imageCount>0);
 gamepad_backdrop_scope(nullptr,false);gamepad_backdrop_maintain();assert(imageCount==0);policy::enabled=true;
 // Only the pane's own register file is patched in place.
 uint32_t file[4]{};gamepad_backdrop_scope(file,true);
 assert(gamepad_backdrop_scope_registers(file)&&!gamepad_backdrop_scope_registers(file+1));
 gamepad_backdrop_scope(nullptr,false);assert(!gamepad_backdrop_scope_registers(file));
 // Issue #493, one screen (Off-TV Play) with the item menu open: per frame the
 // menu background is drawn twice into the TV's "Currently playing on Wii U
 // GamePad" screen, a larger target never scanned out to the GamePad, and once
 // into the GamePad's own. Only the GamePad's is captured (one clear per frame),
 // the TV target never gets a private image, and after the first frames no
 // capture image is allocated or retired (sizes as at resolution scale 2, where
 // the TV captures made the pool reallocate an image every frame).
 gamepad_backdrop_reset();policy::set_capture_requested(true);
 Surface tv;tv.image=900;tv.extent={3840,2160,1};Surface pad;pad.image=901;pad.extent={2560,1440,1};
 auto menuFrame=[&] {
  for(Surface* s:{&tv,&tv,&pad}) {
   gamepad_backdrop_scope(nullptr,true);
   if(gamepad_backdrop_begin_replay(s,true)) { assert(s==&pad);gamepad_backdrop_end_replay(true); }
   gamepad_backdrop_scope(nullptr,false);
  }
  gamepad_backdrop_scan(&pad);gamepad_backdrop_publish();gamepad_backdrop_maintain();++R.frame;
 };
 const auto startClears=clears;
 menuFrame();assert(clears==startClears&&!gamepad_backdrop_image()); // first scan of this target: fallback
 for(unsigned frame=0;frame<4;++frame)menuFrame();
 assert(gamepad_backdrop_image()&&gamepad_backdrop_image()->extent.width==1920); // capped
 const auto warmCreated=created,warmRetired=retired,warmAttempts=allocationAttempts,warmClears=clears;
 for(unsigned frame=0;frame<120;++frame) { menuFrame();assert(gamepad_backdrop_image()); }
 assert(clears==warmClears+120);
 assert(created==warmCreated&&retired==warmRetired&&allocationAttempts==warmAttempts);
 assert(imageCount<=3&&imageBytes<=3ull*1920*1080*4);
 // A reset forgets the GamePad targets (device reset, state load).
 gamepad_backdrop_reset();policy::set_capture_requested(true);
 gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&pad,true));gamepad_backdrop_scope(nullptr,false);
 // Resolution scale 3 and 4 (ten-bit targets, captured as RGBA16F): a full-size
 // capture (3840x2160: 66 MB) left no room in the pool for the next one, and the
 // bars showed the edge colour on two of three presentations; at scale 4
 // (5120x2880: 118 MB) never the capture. Captures are at most 1920x1080 (drawn
 // scaled), so the capture is shown on every presentation, with no allocation
 // after the first frames. Smaller sources keep their own size.
 for(unsigned scale:{3u,4u}) {
  gamepad_backdrop_reset();policy::set_capture_requested(true);
  tv.image=910+scale;tv.extent={1920*scale,1080*scale,1};
  pad.image=920+scale;pad.extent={1280*scale,720*scale,1};
  tv.format=pad.format=0x19;tv.fmt=pad.fmt={VK_FORMAT_A2B10G10R10_UNORM_PACK32,4,4};
  for(unsigned frame=0;frame<5;++frame)menuFrame();
  const auto created3=created,retired3=retired,attempts3=allocationAttempts;
  for(unsigned frame=0;frame<120;++frame) {
   menuFrame();auto* shown=gamepad_backdrop_image();
   assert(shown&&shown->extent.width==1920&&shown->extent.height==1080&&shown->fmt.pixel==VK_FORMAT_R16G16B16A16_SFLOAT);
  }
  assert(created==created3&&retired==retired3&&allocationAttempts==attempts3);
  assert(imageBytes<=kCaptureBytes&&imageBytes<=3ull*1920*1080*8);
 }
 {
  gamepad_backdrop_reset();policy::set_capture_requested(true);
  Surface small;small.image=930;small.extent={1000,500,1};gamepad_backdrop_scan(&small);
  gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&small,true));
  assert(gamepad_backdrop_replay_target()->extent.width==1000&&gamepad_backdrop_replay_target()->extent.height==500);
  gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
  // another aspect: the longer limit binds, the aspect is kept
  Surface wide;wide.image=931;wide.extent={5120,1440,1};gamepad_backdrop_scan(&wide);
  gamepad_backdrop_scope(nullptr,true);assert(gamepad_backdrop_begin_replay(&wide,true));
  assert(gamepad_backdrop_replay_target()->extent.width==1920&&gamepad_backdrop_replay_target()->extent.height==540);
  gamepad_backdrop_end_replay(true);gamepad_backdrop_scope(nullptr,false);
 }
 gamepad_backdrop_reset();assert(retired==created);
}
'''

class BackdropLifecycle(unittest.TestCase):
    def test_production_reset_clear_and_pipeline_bindings(self):
        surfaces=(CPP.parent/'surfaces.cpp').read_text()
        self.assertIn('void ss_reset_surfaces() {\n    gamepad_backdrop_reset();',surfaces)
        clear=surfaces[surfaces.index('void clear_color('):surfaces.index('void clear_depth_stencil(')]
        self.assertIn('gamepad_backdrop_invalidate(s);',clear)
        destroy=surfaces[surfaces.index('void destroy_surface_image('):surfaces.index('VkImageView layer_view(')]
        self.assertIn('gamepad_backdrop_invalidate(s);',destroy)
        draw=(CPP.parent/'draw.cpp').read_text()
        self.assertIn('!aoPrivateReplay && !gamepad_backdrop_replaying()',draw)
        present=(CPP.parent/'present.cpp').read_text()
        self.assertIn('auto q=backdrop;q.clipped=true;q.clip=',present)
        self.assertIn('record::vkCmdSetScissor(cmd,0,1,&quadScissor)',present)
        self.assertIn('record::vkCmdSetScissor(cmd,0,1,&scissor);\n if(gamepadOverlay)',present)

    def test_resolved_bindings_survive_guest_mutation(self):
        source=r'''#include <array>
#include <cassert>
#include "gamepad_backdrop_replay.h"
struct Pipeline { unsigned layout;bool dynamicUniforms; };
struct Shader {};
struct Resources { unsigned set;std::array<unsigned,17> dynamicOffsets{};unsigned dynamicOffsetCount=1; };
struct Snapshot {
 bool replay=true,rasterReady=true;Pipeline* pipeline;uint64_t submission=7;Shader *vs,*ps;
 struct Stage { Resources resources; } stages[2];
};
int main() {
 Shader vs,ps;Pipeline original{101,true},capture{202,true};
 unsigned guestUniform=11,guestVertex=22,guestIndex=33;
 // Fake immutable GPU uploads produced while resolving the original draw.
 const std::array<unsigned,3> uploaded={guestUniform,guestVertex,guestIndex};
 Snapshot snapshot{};snapshot.pipeline=&original;snapshot.vs=&vs;snapshot.ps=&ps;
 snapshot.stages[0].resources={41,{uploaded[0]},1};snapshot.stages[1].resources={42,{55},1};
 guestUniform=guestVertex=guestIndex=99;
 Resources v{},p{};
 assert(gfxvk::backdrop_reuse_resources(snapshot,capture,&vs,&ps,7,v,p));
 assert(v.set==41&&p.set==42&&v.dynamicOffsets[0]==11);
 assert(uploaded[1]==22&&uploaded[2]==33); // Geometry replay uses these resolved bindings.
 assert(!gfxvk::backdrop_reuse_resources(snapshot,capture,&vs,&ps,8,v,p));
 capture.dynamicUniforms=false;assert(!gfxvk::backdrop_reuse_resources(snapshot,capture,&vs,&ps,7,v,p));
 capture.dynamicUniforms=true;snapshot.replay=false;assert(!gfxvk::backdrop_reuse_resources(snapshot,capture,&vs,&ps,7,v,p));
 snapshot.replay=true;snapshot.stages[0].resources.dynamicOffsetCount=18;
 assert(!gfxvk::backdrop_reuse_resources(snapshot,capture,&vs,&ps,7,v,p));
 // A capped capture: the render area becomes the capture's, the guest-to-pixel
 // factors shrink with it; an equal-size capture changes nothing.
 struct Targets { unsigned width,height;float sx,sy; };
 Targets t{3840,2160,3.f,3.f};gfxvk::backdrop_scale_targets(t,1920,1080);
 assert(t.width==1920&&t.height==1080&&t.sx==1.5f&&t.sy==1.5f);
 Targets same{1280,720,1.f,1.f};gfxvk::backdrop_scale_targets(same,1280,720);
 assert(same.width==1280&&same.height==720&&same.sx==1.f&&same.sy==1.f);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'bindings.cpp';path.write_text(source)
            exe=Path(directory)/'bindings'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(CPP.parent),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        draw=(CPP.parent/'draw.cpp').read_text()
        self.assertIn('if(backdropReplay) targets=backdropReplay->targets;',draw)
        self.assertIn('backdrop_scale_targets(targets, backdrop->extent.width, backdrop->extent.height);',draw)
        self.assertIn('backdropShadersIn=shadersOut;',draw)
        exact=draw[draw.index('  if(backdropReplay) {\n    // Identical shaders'):draw.index('  if (shadersOut) {\n    shadersOut->targets=targets;')]
        direct=exact[:exact.index('  } else {')]
        self.assertIn('backdrop_reuse_resources',direct)
        self.assertNotIn('bind_stage(',direct)
        self.assertIn('if (replay) {\n    topology = replay->topology;',draw)
        self.assertIn('bindIndex(replay->indexBuffer, replay->indexOffset, replay->indexType)',draw)

    def test_register_patch_and_no_per_draw_copies(self):
        source=r'''#include <cassert>
#include <cstdint>
#include <stdexcept>
#include "gamepad_backdrop_blend.h"
int main() {
 uint32_t regs[8]={1,2,3,4,5,6,7,8};
 const auto blend=gfxvk::backdrop_blend(0x00CC0100,4u|(5u<<8),7);
 assert(blend.supported);
 {
  gfxvk::BackdropRegisterPatch patch(regs,2,4,6,blend);
  assert(regs[2]==blend.control&&regs[4]==(5u|(1u<<8))&&regs[6]==(7u|8u));
  assert(regs[0]==1&&regs[1]==2&&regs[3]==4&&regs[5]==6&&regs[7]==8);
 }
 for(uint32_t i=0;i<8;++i)assert(regs[i]==i+1);
 try { gfxvk::BackdropRegisterPatch patch(regs,2,4,6,blend);throw std::runtime_error("replay"); }
 catch(const std::runtime_error&) {}
 for(uint32_t i=0;i<8;++i)assert(regs[i]==i+1); // restored on exceptions too
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'patch.cpp';path.write_text(source)
            exe=Path(directory)/'patch'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(CPP.parent),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        draw=(CPP.parent/'draw.cpp').read_text()
        tail=draw[draw.index('  if (gamepad_backdrop_begin_replay(colors[0],compatible,captureBlend.opaque)) {'):draw.index('// Setup\'s Vulkan preparation (headstart.cpp')]
        # No full register-file copy and no allocation per captured draw.
        self.assertNotIn('captureRegs',tail)
        self.assertIn('BackdropRegisterPatch patch(regs,REGADDR::CB_BLEND0_CONTROL,REGADDR::CB_COLOR_CONTROL,REGADDR::CB_TARGET_MASK,captureBlend);',tail)
        self.assertIn('if(!gamepad_backdrop_scope_registers(r)) {',tail)
        self.assertNotIn('make_unique<StereoShaders>',draw)
        self.assertIn('backdropSnapshot.reset();shadersOut=&backdropSnapshot;',draw)
        # The capture pipeline is polled, never waited for on the render thread.
        self.assertIn('if (gamepad_backdrop_replaying() && p.pendingCompile) {\n    resolve_pending_pipeline(p, false);',draw)
        self.assertNotIn('if (gamepad_backdrop_replaying() && p.pendingCompile) resolve_pending_pipeline(p);',draw)
        backend=(CPP.parent/'backend.cpp').read_text()
        self.assertIn('backdropDemandFrame.publish();\n  gamepad_backdrop_maintain();',backend)
        metal=(CPP.parent.parent/'metal_main.mm').read_text()
        self.assertIn('present_screens();\n        gamepad_backdrop_maintain();',metal)
        metalDraw=(CPP.parent.parent/'metal_draw.mm').read_text()
        self.assertIn('gamepad_backdrop::fits_pool(backdrop_pool_bytes(), backdrop_bytes(storage->texture), wanted)',metalDraw)
        self.assertIn('gamepad_backdrop::release_idle(on, R.frame, lastBackdropFrame)',metalDraw)

    def test_bars_after_pip_frame(self):
        present=(CPP.parent/'present.cpp').read_text()
        body=present[present.index('static void add_gamepad_backdrop('):present.index('std::vector<ComposeQuad> screen_quads(')]
        self.assertNotIn('quads.insert(quads.begin()',body)
        self.assertEqual(body.count('insert_backdrop_bars(quads,index,bars);'),2)

    def test_demand_published_once_after_both_windows(self):
        source=r'''#include <cassert>
#include "gamepad_backdrop_demand.h"
int main() {
 gfxvk::GamepadBackdropDemand demand;bool published=true,next=false;
 demand.begin();assert(published); // Previous demand survives the prefix/current draws.
 demand.request();assert(published); // First window cannot publish an intermediate result.
 demand.request();assert(published); // Both windows aggregate with OR.
 assert(demand.finish(next)&&next);published=next;
 assert(!demand.finish(next)); // Exactly one publication per scope.
 demand.begin();assert(published); // Disabling bars is not visible until end either.
 assert(demand.finish(next)&&!next);published=next;assert(!published);
 demand.begin();demand.request();demand.cancel();assert(!demand.finish(next));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'demand.cpp';path.write_text(source)
            exe=Path(directory)/'demand'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(CPP.parent),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        backend=(CPP.parent/'backend.cpp').read_text()
        swap=backend[backend.index('void swap() {'):]
        self.assertNotIn('set_capture_requested(false)',swap)
        self.assertLess(swap.index('publish_or_finish_present_prefix'),swap.index('GamepadBackdropDemandFrame backdropDemandFrame'))
        self.assertGreater(swap.index('backdropDemandFrame.publish()'),swap.index('present(R.drc)'))

    def test_capture_coverage_not_squared(self):
        source = r'''#include <cassert>
#include <cmath>
#include "gamepad_backdrop_blend.h"
int main() {
 using namespace gfxvk;
 const unsigned control=0x00CC0100;
 auto b=backdrop_blend(control,4u|(5u<<8),15);
 assert(b.supported&&!b.opaque);
 assert(((b.control>>16)&31)==1&&((b.control>>24)&31)==5);
 // Alpha 0.5 over transparent must be 0.5, not native RGB factor 0.25.
 const float src=0.5f,dst=0.f;
 const float coverage=src*1.f+dst*(1.f-src);
 assert(coverage==0.5f&&src*src==0.25f);
 assert(src*1.f+coverage*(1.f-src)==0.75f);
 auto opaque=backdrop_blend(0x00CC0000,0,15);
 assert(opaque.supported&&opaque.opaque);
 assert(((opaque.control>>16)&31)==0&&((opaque.control>>24)&31)==1);
 assert(backdrop_blend(control,1u|(5u<<8),15).supported);
 assert(!backdrop_blend(control,4u|(1u<<8),15).supported);
 assert(!backdrop_blend(control,4u|(5u<<8)|(1u<<5),15).supported);
 assert(!backdrop_blend(0,0,15).supported);
 assert(!backdrop_blend(control,4u|(5u<<8),3).supported);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'coverage.cpp';path.write_text(source)
            exe=Path(directory)/'coverage'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(CPP.parent),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_capture_lifecycle(self):
        source = CPP.read_text()
        source = source.replace('#include "backend.h"', SHIM)
        source = source.replace('#include "deferred_dispatch.h"', '')
        source = source.replace('#include "vulkan_error.h"', '')
        source = source.replace('#include "gamepad_backdrop.h"', '#include "' + str(CPP.with_suffix('.h')) + '"')
        source = source.replace('#include "../gamepad_backdrop.h"', '#include "' + str(CPP.parent.parent / 'gamepad_backdrop.h') + '"')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'test.cpp';path.write_text(source+MAIN)
            exe=Path(directory)/'test'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == '__main__':
    unittest.main()
