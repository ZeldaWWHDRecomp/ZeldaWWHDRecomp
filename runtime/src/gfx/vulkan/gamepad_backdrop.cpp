// Only the tagged picture is replayed; foreground/menu pixels never enter this image.
#include "gamepad_backdrop.h"
#include "../gamepad_backdrop.h"
#include "backend.h"
#include "vulkan_error.h"
#include "deferred_dispatch.h"
#include <utility>
#include <cstring>
#include <cstdio>
#include <cstdlib>
namespace gfxvk {
namespace {
constexpr unsigned kCaptureImages=6;
constexpr uint64_t kCaptureBytes=96ull*1024*1024;
unsigned imageCount=0;
uint64_t imageBytes=0;
struct Image {
 std::unique_ptr<Surface> surface;
 uint64_t bytes=0;
 ~Image() { if(surface) destroy_surface_image(surface.get());if(bytes){--imageCount;imageBytes-=bytes;} }

};
struct Pending { uint64_t source=0,ticket=0; std::unique_ptr<Image> image; };
gfx::gamepad_backdrop::ScanCache cache;
std::vector<Pending> pending;
std::unique_ptr<Image> active,building,published;
std::array<std::unique_ptr<Image>,kCaptureImages> spare;
uint64_t sourceIdentity=0,nextTicket=0;
unsigned scopeDepth=0;
bool replaying=false,failed=false,drawn=false,buildingDirty=false,captureOpaque=false;
void recycle(std::unique_ptr<Image>& image) {
 if(!image)return;
 for(auto& slot:spare)if(!slot){slot=std::move(image);return;}
 image.reset();
}
void recycle_pending() { for(auto& p:pending)recycle(p.image);pending.clear(); }
void discard_pending(uint64_t id) {
 if(!id)return;
 for(auto& p:pending)if(p.source==id){p.source=0;p.ticket=0;recycle(p.image);}
}
std::unique_ptr<Image> acquire(Surface* source) {
 for(auto& slot:spare)if(slot && slot->surface->format==source->format &&
     slot->surface->fmt.pixel==source->fmt.pixel &&
     slot->surface->extent.width==source->extent.width && slot->surface->extent.height==source->extent.height)
  return std::move(slot);
 // Only idle images may be retired for a format/extent change. Pending,
 // building and published images retain exclusive ownership throughout use.
 for(auto& slot:spare)slot.reset();
 const uint64_t estimate=uint64_t(source->extent.width)*source->extent.height*
     std::max(1u,source->fmt.hostBytesPerBlock?source->fmt.hostBytesPerBlock:source->fmt.bytesPerBlock);
 if(!estimate||estimate>kCaptureBytes||imageCount>=kCaptureImages||estimate>kCaptureBytes-imageBytes)return {};
 try {
  auto image=std::make_unique<Image>();image->surface=std::make_unique<Surface>();
  auto* s=image->surface.get();s->width=source->extent.width;s->height=source->extent.height;
  s->format=source->format;s->fmt=source->fmt;s->scale=1;
  create_surface_image(s,false);
  const uint64_t bytes=s->allocationBytes;
  if(!bytes||bytes>kCaptureBytes-imageBytes)return {};
  image->bytes=bytes;++imageCount;imageBytes+=bytes;
  return image;
 } catch(const std::bad_alloc&) { return {}; }
 catch(const VulkanFailure& failure) { if(failure.allocation_failure())return {};throw; }
}
uint64_t identity(Surface* s) {
 uint64_t value=0;
 if(s) { static_assert(sizeof(s->image)<=sizeof(value));std::memcpy(&value,&s->image,sizeof(s->image)); }
 return value;
}
}
void gamepad_backdrop_debug(const char* event,uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
 static const uint64_t first=[] {const char* e=std::getenv("WWHD_GAMEPAD_BACKDROP_LOG");return e&&*e?std::strtoull(e,nullptr,10):~uint64_t{0};}();
 static unsigned emitted=0;
 if(R.frame<first||emitted>=100)return;
 ++emitted;
 std::fprintf(stderr,"[vk backdrop] frame=%llu event=%s a=%llx b=%llx c=%llx d=%llx\n",
  (unsigned long long)R.frame,event,(unsigned long long)a,(unsigned long long)b,(unsigned long long)c,(unsigned long long)d);
}
void gamepad_backdrop_reset() {
 end_encoder();active.reset();building.reset();published.reset();pending.clear();for(auto& slot:spare)slot.reset();cache.reset();
 gfx::gamepad_backdrop::set_capture_requested(false);
 sourceIdentity=0;scopeDepth=0;replaying=failed=drawn=buildingDirty=false;
}
void gamepad_backdrop_invalidate(Surface* source) {
 const auto id=identity(source);if(!id)return;
 if(sourceIdentity==id)failed=true;
 discard_pending(id);
}
void gamepad_backdrop_scope(const uint32_t*,bool begin) {
 gamepad_backdrop_debug(begin?"scope-begin":"scope-end",scopeDepth,gfx::gamepad_backdrop::capture_enabled(),sourceIdentity,uint64_t(drawn)|(uint64_t(failed)<<1));
 if(begin) { if(scopeDepth++==0) { recycle(active);sourceIdentity=0;failed=drawn=false; } return; }
 if(!scopeDepth || --scopeDepth) return;
 if(failed)discard_pending(sourceIdentity);
 gamepad_backdrop_debug("scope-result",sourceIdentity,drawn,failed,active?identity(active->surface.get()):0);
 if(active && drawn && !failed && gfx::gamepad_backdrop::enabled.load()) {
  const uint64_t ticket=++nextTicket;
  cache.capture(sourceIdentity,ticket);
  if(pending.size()==16) recycle_pending();
  pending.push_back({sourceIdentity,ticket,std::move(active)});
 }
 recycle(active);
}
bool gamepad_backdrop_replaying() { return replaying; }
bool gamepad_backdrop_capture_active() { return scopeDepth&&!replaying&&gfx::gamepad_backdrop::capture_enabled(); }
Surface* gamepad_backdrop_replay_target() { return replaying&&active ? active->surface.get():nullptr; }
bool gamepad_backdrop_begin_replay(Surface* source,bool compatible,bool opaque) {
 gamepad_backdrop_debug("begin-replay",identity(source),compatible,uint64_t(replaying)|(uint64_t(failed)<<1),scopeDepth);
 if(replaying||!scopeDepth||!gfx::gamepad_backdrop::capture_enabled()) return false;
 if(!compatible || !source || !source->image || (sourceIdentity&&sourceIdentity!=identity(source))) { failed=true;discard_pending(identity(source));return false; }
 if(active&&captureOpaque!=opaque)failed=true;
 if(failed) return false;
 if(!active) {
  captureOpaque=opaque;sourceIdentity=identity(source);end_encoder();
  active=acquire(source);
  if(!active){failed=true;discard_pending(sourceIdentity);return false;}
  auto* s=active->surface.get();sourceIdentity=identity(source);
  transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkClearColorValue clear{};clear.float32[3]=opaque?1.f:0.f;VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  record::vkCmdClearColorImage(command_buffer(),s->image,s->layout,&clear,1,&range);mark_gpu_written(s);
 }
 end_encoder();replaying=true;return true;
}
void gamepad_backdrop_end_replay(bool success) { gamepad_backdrop_debug("end-replay",success,sourceIdentity,active?identity(active->surface.get()):0); end_encoder();replaying=false;drawn|=success;if(!success)failed=true; }
void gamepad_backdrop_scan(Surface* source) {
 auto selection=cache.scan(identity(source));
 if(selection.ticket||!pending.empty())gamepad_backdrop_debug("scan",identity(source),selection.ticket,pending.size(),selection.generation);
 recycle(building);
 for(auto& p:pending) if(p.ticket==selection.ticket) { building=std::move(p.image);break; }
 recycle_pending();buildingDirty=true;
}
void gamepad_backdrop_publish() { if(buildingDirty) { if(building||published)gamepad_backdrop_debug("publish",building?identity(building->surface.get()):0,published?identity(published->surface.get()):0); published.swap(building);buildingDirty=false; } }
Surface* gamepad_backdrop_image() { return gfx::gamepad_backdrop::enabled.load()&&published ? published->surface.get():nullptr; }
}
