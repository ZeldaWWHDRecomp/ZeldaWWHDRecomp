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
namespace gfxvk {
struct Surface {
 uint64_t image=0;unsigned width=0,height=0,format=0;struct {int pixel=1;unsigned hostBytesPerBlock=4,bytesPerBlock=4;} fmt;float scale=0;int layout=0;uint64_t allocationBytes=0;
 struct { unsigned width=0,height=0,depth=1; } extent;
};
static struct {uint64_t frame=0;} R;
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
void create_surface_image(Surface* s,bool) { ++allocationAttempts;if(failHost)throw std::bad_alloc();if(failAllocation)throw VulkanFailure(true);if(failDevice)throw VulkanFailure(false);s->image=++created;s->extent={s->width,s->height,1};s->allocationBytes=uint64_t(s->width)*s->height*4; }
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
 const auto stable=created;
 for(unsigned frame=0;frame<120;++frame){capture(&a);gamepad_backdrop_scan(&a);gamepad_backdrop_publish();}
 assert(created==stable&&imageCount<=kCaptureImages&&imageBytes<=kCaptureBytes);
 // Oversized images and allocator failure fall back without publishing a capture.
 Surface huge=a;huge.extent={65535,65535,1};
 gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&huge,true));gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_scan(&huge);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());
 gamepad_backdrop_reset();policy::set_capture_requested(true);failAllocation=true;
 gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_scan(&a);gamepad_backdrop_publish();assert(!gamepad_backdrop_image());failAllocation=false;
 failHost=true;gamepad_backdrop_scope(nullptr,true);assert(!gamepad_backdrop_begin_replay(&a,true));gamepad_backdrop_scope(nullptr,false);failHost=false;
 // Device loss is not an optional capture miss: propagate to normal backend handling.
 failDevice=true;bool propagated=false;gamepad_backdrop_scope(nullptr,true);
 try { gamepad_backdrop_begin_replay(&a,true); } catch(const VulkanFailure&) { propagated=true; }
 assert(propagated);failDevice=false;gamepad_backdrop_scope(nullptr,false);
 gamepad_backdrop_reset();assert(!gamepad_backdrop_image()&&!policy::capture_enabled()&&retired==created);
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
        self.assertIn('if (gamepad_backdrop_replaying() && p.pendingCompile) resolve_pending_pipeline(p);',draw)
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
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'bindings.cpp';path.write_text(source)
            exe=Path(directory)/'bindings'
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(CPP.parent),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        draw=(CPP.parent/'draw.cpp').read_text()
        self.assertIn('if(backdropReplay) targets=backdropReplay->targets;',draw)
        self.assertIn('backdropShadersIn=shadersOut;',draw)
        exact=draw[draw.index('  if(backdropReplay) {\n    // Identical shaders'):draw.index('  if (shadersOut) {\n    shadersOut->targets=targets;')]
        direct=exact[:exact.index('  } else {')]
        self.assertIn('backdrop_reuse_resources',direct)
        self.assertNotIn('bind_stage(',direct)
        self.assertIn('if (replay) {\n    topology = replay->topology;',draw)
        self.assertIn('bindIndex(replay->indexBuffer, replay->indexOffset, replay->indexType)',draw)

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
