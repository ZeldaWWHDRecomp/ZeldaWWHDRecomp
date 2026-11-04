// Only the tagged picture is replayed; foreground/menu pixels never enter this image.
#include "gamepad_backdrop.h"
#include "../gamepad_backdrop.h"
#include "backend.h"
#include "deferred_dispatch.h"
#include <utility>
#include <cstring>
namespace gfxvk {
namespace {
struct Image {
 std::unique_ptr<Surface> surface;
 ~Image() { if(surface) destroy_surface_image(surface.get()); }
};
struct Pending { uint64_t source=0,ticket=0; std::unique_ptr<Image> image; };
gfx::gamepad_backdrop::ScanCache cache;
std::vector<Pending> pending;
std::unique_ptr<Image> active,building,published;
uint64_t sourceIdentity=0,nextTicket=0;
unsigned scopeDepth=0;
bool replaying=false,failed=false,drawn=false,buildingDirty=false,captureOpaque=false;
uint64_t identity(Surface* s) {
 uint64_t value=0;
 if(s) { static_assert(sizeof(s->image)<=sizeof(value));std::memcpy(&value,&s->image,sizeof(s->image)); }
 return value;
}
}
void gamepad_backdrop_reset() {
 end_encoder();active.reset();building.reset();published.reset();pending.clear();cache.reset();
 gfx::gamepad_backdrop::set_capture_requested(false);
 sourceIdentity=0;scopeDepth=0;replaying=failed=drawn=buildingDirty=false;
}
void gamepad_backdrop_invalidate(Surface* source) {
 const auto id=identity(source);if(!id)return;
 if(sourceIdentity==id)failed=true;
 for(auto& p:pending) if(p.source==id) { p.source=0;p.ticket=0;p.image.reset(); }
}
void gamepad_backdrop_scope(const uint32_t*,bool begin) {
 if(begin) { if(scopeDepth++==0) { active.reset();sourceIdentity=0;failed=drawn=false; } return; }
 if(!scopeDepth || --scopeDepth) return;
 if(active && drawn && !failed && gfx::gamepad_backdrop::enabled.load()) {
  const uint64_t ticket=++nextTicket;
  cache.capture(sourceIdentity,ticket);
  if(pending.size()==16) pending.clear();
  pending.push_back({sourceIdentity,ticket,std::move(active)});
 }
 active.reset();
}
bool gamepad_backdrop_replaying() { return replaying; }
bool gamepad_backdrop_capture_active() { return scopeDepth&&!replaying&&gfx::gamepad_backdrop::capture_enabled(); }
Surface* gamepad_backdrop_replay_target() { return replaying&&active ? active->surface.get():nullptr; }
bool gamepad_backdrop_begin_replay(Surface* source,bool compatible,bool opaque) {
 if(replaying||!scopeDepth||!gfx::gamepad_backdrop::capture_enabled()) return false;
 if(!compatible || !source || !source->image || (sourceIdentity&&sourceIdentity!=identity(source))) { failed=true;return false; }
 if(active&&captureOpaque!=opaque)failed=true;
 if(failed) return false;
 if(!active) {
  captureOpaque=opaque;end_encoder();active=std::make_unique<Image>();active->surface=std::make_unique<Surface>();
  auto* s=active->surface.get();s->width=source->extent.width;s->height=source->extent.height;
  s->format=source->format;s->fmt=source->fmt;s->scale=1;
  create_surface_image(s,false);sourceIdentity=identity(source);
  transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkClearColorValue clear{};clear.float32[3]=opaque?1.f:0.f;VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  record::vkCmdClearColorImage(command_buffer(),s->image,s->layout,&clear,1,&range);mark_gpu_written(s);
 }
 end_encoder();replaying=true;return true;
}
void gamepad_backdrop_end_replay(bool success) { end_encoder();replaying=false;drawn|=success;if(!success)failed=true; }
void gamepad_backdrop_scan(Surface* source) {
 auto selection=cache.scan(identity(source));building.reset();
 for(auto& p:pending) if(p.ticket==selection.ticket) { building=std::move(p.image);break; }
 pending.clear();buildingDirty=true;
}
void gamepad_backdrop_publish() { if(buildingDirty) { published.swap(building);buildingDirty=false; } }
Surface* gamepad_backdrop_image() { return gfx::gamepad_backdrop::enabled.load()&&published ? published->surface.get():nullptr; }
}
