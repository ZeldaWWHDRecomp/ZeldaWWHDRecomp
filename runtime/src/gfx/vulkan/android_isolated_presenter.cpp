#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#include "android_isolated_presenter.h"
#include "android_shared_image.h"
#include "present.h"
#include "present_worker.h"
#include "backend.h"
#include "runtime.h"
#include <android/native_window.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace gfxvk {
namespace {
struct Failure : std::runtime_error {
 VkResult result;
 Failure(VkResult value,const char* operation):std::runtime_error(operation),result(value) {}
};
void check(VkResult value,const char* operation) { if(value!=VK_SUCCESS)throw Failure(value,operation); }
struct StageTimer {
 size_t stage;bool enabled;std::chrono::steady_clock::time_point started;
 explicit StageTimer(size_t index):stage(index),enabled(R.secondaryProfile.load(std::memory_order_relaxed)) {
  if(enabled)started=std::chrono::steady_clock::now();
 }
 ~StageTimer() {
  if(!enabled)return;
  R.secondaryStageNs[stage].fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-started).count()),std::memory_order_relaxed);
  R.secondaryStageCalls[stage].fetch_add(1,std::memory_order_relaxed);
 }
};
struct Window {
 ANativeWindow* value=nullptr;
 explicit Window(ANativeWindow* pointer):value(pointer) {}
 ~Window() { if(value)ANativeWindow_release(value); }
};
}
struct AndroidIsolatedPresenter::Impl {
 VkInstance instance;VkPhysicalDevice physical;VkDevice primary,device=VK_NULL_HANDLE;
 VkQueue queue=VK_NULL_HANDLE;uint32_t family;
#define FUNCTIONS(X) X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) X(vkQueueWaitIdle) \
 X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) \
 X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
 X(vkAllocateCommandBuffers) X(vkResetCommandPool) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
 X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) \
 X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit) X(vkCmdPipelineBarrier)
#define FIELD(name) PFN_##name name=nullptr;
 FUNCTIONS(FIELD)
#undef FIELD
 std::unique_ptr<IndependentPresenter> presenter;
 std::unique_ptr<AndroidSharedImage> shared;
 VkExtent2D sharedExtent{};
 VkImageView sourceView=VK_NULL_HANDLE;
 bool snapshotUsed=false,returned=false,framePrepared=false,framePublished=false;
 const bool generalLayout=getenv("WWHD_VK_SHARED_GENERAL") && !strcmp(getenv("WWHD_VK_SHARED_GENERAL"),"1");
 bool pending=false,hasAcquired=false;
 std::unique_ptr<PresentWorker<Result>> worker;
 std::shared_ptr<Window> window;
 VkSurfaceKHR surface=VK_NULL_HANDLE;
 VkSwapchainKHR swapchain=VK_NULL_HANDLE;
 VkExtent2D extent{},requestedExtent{};VkFormat format=VK_FORMAT_UNDEFINED;
 std::vector<VkImage> images;
 std::vector<VkImageView> views;
 std::vector<VkImageLayout> layouts;
 uint32_t imageIndex=0;
 VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cmd=VK_NULL_HANDLE;
 VkDescriptorPool descriptors=VK_NULL_HANDLE;
 VkFence acquiredFence=VK_NULL_HANDLE,drawFence=VK_NULL_HANDLE;
 Impl(VkInstance i,VkPhysicalDevice p,VkDevice main,uint32_t f):instance(i),physical(p),primary(main),family(f) {}
 ~Impl() {
  // Explicit shutdown may wait. Ordinary frames never enter this path.
  R.secondaryPresentHeld=false;R.secondaryAcquireHeld=false;
  worker.reset();
  if(device && vkDeviceWaitIdle)vkDeviceWaitIdle(device);
  clear_swapchain();
  if(sourceView)vkDestroyImageView(device,sourceView,nullptr);
  shared.reset();R.secondarySharedSnapshots=0;presenter.reset();
  if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);
  if(pool)vkDestroyCommandPool(device,pool,nullptr);
  if(acquiredFence)vkDestroyFence(device,acquiredFence,nullptr);
  if(drawFence)vkDestroyFence(device,drawFence,nullptr);
  if(device && vkDestroyDevice)vkDestroyDevice(device,nullptr);
 }
 void clear_swapchain() {
  for(auto view:views)vkDestroyImageView(device,view,nullptr);
  views.clear();images.clear();layouts.clear();
  if(swapchain) { vkDestroySwapchainKHR(device,swapchain,nullptr);++R.secondaryRetired; }
  R.secondaryDeviceSwapchains=0;
  swapchain=VK_NULL_HANDLE;
  if(surface)gfxvk::vkDestroySurfaceKHR(instance,surface,nullptr);
  surface=VK_NULL_HANDLE;window.reset();
 }
 void init(bool khr) {
  std::vector<const char*> extensions={VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      "VK_ANDROID_external_memory_android_hardware_buffer","VK_KHR_external_semaphore_fd","VK_EXT_queue_family_foreign"};
  if(khr) {
   extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
   VkPhysicalDeviceProperties properties{};gfxvk::vkGetPhysicalDeviceProperties(physical,&properties);
   if(properties.apiVersion<VK_API_VERSION_1_2) {
    extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
   }
  }
  float priority=.5f;
  VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
  VkPhysicalDeviceDynamicRenderingFeatures rendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};rendering.dynamicRendering=VK_TRUE;
  VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};create.pNext=&rendering;
  create.queueCreateInfoCount=1;create.pQueueCreateInfos=&q;
  create.enabledExtensionCount=extensions.size();create.ppEnabledExtensionNames=extensions.data();
  check(gfxvk::vkCreateDevice(physical,&create,nullptr,&device),"create secondary logical device");
#define LOAD(name) name=reinterpret_cast<PFN_##name>(gfxvk::vkGetDeviceProcAddr(device,#name)); \
  if(!name)throw std::runtime_error("Missing isolated presenter function " #name);
  FUNCTIONS(LOAD)
#undef LOAD
  vkGetDeviceQueue(device,family,0,&queue);
  if(device==primary || queue==R.queue)throw std::runtime_error("Secondary device/queue is not independent");
  presenter=std::make_unique<IndependentPresenter>(device,khr);
  VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commandPool.queueFamilyIndex=family;
  check(vkCreateCommandPool(device,&commandPool,nullptr,&pool),"secondary-device command pool");
  VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  commands.commandPool=pool;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
  check(vkAllocateCommandBuffers(device,&commands,&cmd),"secondary-device commands");
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
  VkDescriptorPoolCreateInfo descriptorPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  descriptorPool.maxSets=1;descriptorPool.poolSizeCount=1;descriptorPool.pPoolSizes=&size;
  check(vkCreateDescriptorPool(device,&descriptorPool,nullptr,&descriptors),"secondary-device descriptors");
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  check(vkCreateFence(device,&fence,nullptr,&acquiredFence),"secondary-device acquire fence");
  check(vkCreateFence(device,&fence,nullptr,&drawFence),"secondary-device draw fence");
  VkFormatProperties properties{};gfxvk::vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&properties);
  constexpr auto required=VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if((properties.optimalTilingFeatures&required)!=required)throw std::runtime_error("RGBA8 sharing requires blit destination and linear sampling");
  shared=std::make_unique<AndroidSharedImage>(instance,physical,primary,device,VkExtent2D{1,1});sharedExtent={1,1};R.secondarySharedSnapshots=1;
  worker=std::make_unique<PresentWorker<Result>>();
 }
#undef FUNCTIONS
 template<class Function> void dispatch(Operation operation,Function task) {
  if(pending)throw std::logic_error("Secondary-device request already pending");
  worker->submit([this,operation,task=std::move(task)] {
   const auto start=std::chrono::steady_clock::now();
   VkResult status=VK_SUCCESS;
   try { status=task(); } catch(const Failure& failure) { status=failure.result; }
   return Result{operation,status,extent,uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
       std::chrono::steady_clock::now()-start).count())};
  });
  pending=true;
 }
 void make_surface(std::shared_ptr<Window> next,VkExtent2D requested,int mode) {
  // Draw/acquire fences were waited inside the last worker request. No WSI
  // semaphore is retained by the presentation engine: present waits on none.
  ++R.secondaryLocalQueueWaits;
  check(vkQueueWaitIdle(queue),"secondary-device replacement queue idle");
  clear_swapchain();window=std::move(next);requestedExtent=requested;extent={};
  if(!window || !window->value || !requested.width || !requested.height)return;
  auto create=reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(gfxvk::vkGetInstanceProcAddr(instance,"vkCreateAndroidSurfaceKHR"));
  if(!create)throw std::runtime_error("Missing Android surface creation");
  VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};info.window=window->value;
  check(create(instance,&info,nullptr,&surface),"secondary-device surface");
  VkBool32 supported=VK_FALSE;
  check(gfxvk::vkGetPhysicalDeviceSurfaceSupportKHR(physical,family,surface,&supported),"secondary-device present support");
  if(!supported)throw Failure(VK_ERROR_INITIALIZATION_FAILED,"Secondary queue cannot present to surface");
  if(R.secondaryInjectQueryLoss.exchange(false))throw Failure(VK_ERROR_SURFACE_LOST_KHR,"authored secondary-device query loss");
  VkSurfaceCapabilitiesKHR caps{};
  check(gfxvk::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"secondary-device capabilities");
  uint32_t count=0;check(gfxvk::vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"secondary-device format count");
  std::vector<VkSurfaceFormatKHR> formats(count);
  check(gfxvk::vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"secondary-device formats");formats.resize(count);
  bool found=false;VkSurfaceFormatKHR chosen{};
  for(auto candidate:formats) {
   if(candidate.format==VK_FORMAT_UNDEFINED)candidate.format=VK_FORMAT_B8G8R8A8_SRGB;
   VkFormatProperties properties{};gfxvk::vkGetPhysicalDeviceFormatProperties(physical,candidate.format,&properties);
   if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))continue;
   if(!found || candidate.format==VK_FORMAT_B8G8R8A8_SRGB) { chosen=candidate;found=true; }
  }
  if(!found || !(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
   throw Failure(VK_ERROR_FORMAT_NOT_SUPPORTED,"No secondary-device color attachment format");
  extent={std::clamp(requested.width,caps.minImageExtent.width,caps.maxImageExtent.width),
          std::clamp(requested.height,caps.minImageExtent.height,caps.maxImageExtent.height)};
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=surface;
  ci.minImageCount=std::max(caps.minImageCount,2u);if(caps.maxImageCount)ci.minImageCount=std::min(ci.minImageCount,caps.maxImageCount);
  ci.imageFormat=chosen.format;ci.imageColorSpace=chosen.colorSpace;ci.imageExtent=extent;ci.imageArrayLayers=1;
  ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform=(caps.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)?VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:caps.currentTransform;
  for(auto alpha:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                 VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
   if(caps.supportedCompositeAlpha&alpha) { ci.compositeAlpha=alpha;break; }
  check(gfxvk::vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,nullptr),"secondary-device mode count");
  std::vector<VkPresentModeKHR> modes(count);
  check(gfxvk::vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,modes.data()),"secondary-device modes");modes.resize(count);
  const VkPresentModeKHR wanted=mode==1?VK_PRESENT_MODE_MAILBOX_KHR:mode==2?VK_PRESENT_MODE_IMMEDIATE_KHR:VK_PRESENT_MODE_FIFO_KHR;
  ci.presentMode=std::find(modes.begin(),modes.end(),wanted)!=modes.end()?wanted:VK_PRESENT_MODE_FIFO_KHR;
  check(vkCreateSwapchainKHR(device,&ci,nullptr,&swapchain),"secondary-device swapchain");R.secondaryDeviceSwapchains=1;format=chosen.format;
  check(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"secondary-device image count");images.resize(count);
  check(vkGetSwapchainImagesKHR(device,swapchain,&count,images.data()),"secondary-device images");images.resize(count);
  layouts.assign(count,VK_IMAGE_LAYOUT_UNDEFINED);
  for(auto image:images) {
   VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image;
   view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   VkImageView handle;check(vkCreateImageView(device,&view,nullptr,&handle),"secondary-device image view");views.push_back(handle);
  }
  LOG("[vulkan] isolated secondary swapchain %ux%u",extent.width,extent.height);
 }
 void barrier(VkCommandBuffer commands,VkImage image,VkImageLayout from,VkImageLayout to,
     uint32_t source,uint32_t target,VkAccessFlags before,VkAccessFlags after,
     VkPipelineStageFlags sourceStage,VkPipelineStageFlags targetStage,bool producer=false) {
  VkImageMemoryBarrier value{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  value.oldLayout=from;value.newLayout=to;value.srcQueueFamilyIndex=source;value.dstQueueFamilyIndex=target;
  value.srcAccessMask=before;value.dstAccessMask=after;value.image=image;value.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  if(producer)gfxvk::vkCmdPipelineBarrier(commands,sourceStage,targetStage,0,0,nullptr,0,nullptr,1,&value);
  else vkCmdPipelineBarrier(commands,sourceStage,targetStage,0,0,nullptr,0,nullptr,1,&value);
 }
 Result consume(Result value) {
  pending=false;
  if(value.operation==Operation::Acquire)hasAcquired=value.status==VK_SUCCESS || value.status==VK_SUBOPTIMAL_KHR;
  return value;
 }
};
AndroidIsolatedPresenter::AndroidIsolatedPresenter(VkInstance instance,VkPhysicalDevice physical,VkDevice primary,
    uint32_t family,bool khr):impl(std::make_unique<Impl>(instance,physical,primary,family)) { impl->init(khr); }
AndroidIsolatedPresenter::~AndroidIsolatedPresenter()=default;
VkQueue AndroidIsolatedPresenter::queue() const { return impl->queue; }
bool AndroidIsolatedPresenter::busy() const { return impl->pending; }
bool AndroidIsolatedPresenter::acquired() const { return impl->hasAcquired; }
bool AndroidIsolatedPresenter::prepared() const { return impl->framePrepared; }
bool AndroidIsolatedPresenter::published() const { return impl->framePublished; }
void AndroidIsolatedPresenter::replace_surface(ANativeWindow* next,uint32_t w,uint32_t h,int mode) {
 auto window=std::make_shared<Window>(next);
 if(impl->hasAcquired || impl->framePrepared)throw std::logic_error("Secondary image must be consumed before replacement");
 impl->dispatch(Operation::Surface,[this,window,w,h,mode] { impl->make_surface(window,{w,h},mode);return VK_SUCCESS; });
}
void AndroidIsolatedPresenter::resize(int mode) {
 auto window=impl->window;auto extent=impl->requestedExtent;
 impl->dispatch(Operation::Surface,[this,window,extent,mode] { impl->make_surface(window,extent,mode);return VK_SUCCESS; });
}
void AndroidIsolatedPresenter::acquire() {
 impl->dispatch(Operation::Acquire,[this] {
  while(R.secondaryAcquireHeld.load())std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if(R.secondaryInjectAcquireLoss.exchange(false))return VK_ERROR_SURFACE_LOST_KHR;
  auto& s=*impl;
  if(!s.swapchain)return VK_NOT_READY;
  check(s.vkResetFences(s.device,1,&s.acquiredFence),"reset secondary-device acquire fence");
  VkResult status;
  { StageTimer timer(0);status=s.vkAcquireNextImageKHR(s.device,s.swapchain,0,VK_NULL_HANDLE,s.acquiredFence,&s.imageIndex); }
  if(status==VK_SUCCESS || status==VK_SUBOPTIMAL_KHR) {
   StageTimer timer(1);
   check(s.vkWaitForFences(s.device,1,&s.acquiredFence,VK_TRUE,UINT64_MAX),"secondary-device acquisition completion");
  }
  return status;
 });
}
std::optional<AndroidIsolatedPresenter::Result> AndroidIsolatedPresenter::poll() {
 if(!impl->pending)return {};
 if(auto result=impl->worker->poll())return impl->consume(*result);
 return {};
}
AndroidIsolatedPresenter::Result AndroidIsolatedPresenter::wait() { return impl->consume(impl->worker->wait()); }
void AndroidIsolatedPresenter::record_snapshot(VkCommandBuffer cmd,VkImage source,VkFormat sourceFormat,VkExtent2D extent) {
 StageTimer timer(8);
 auto& s=*impl;
 if(s.pending || !s.hasAcquired || s.framePrepared)throw std::logic_error("Secondary snapshot is not available");
 if(!s.shared || s.sharedExtent.width!=extent.width || s.sharedExtent.height!=extent.height) {
  if(s.sourceView)s.vkDestroyImageView(s.device,s.sourceView,nullptr);
  s.sourceView=VK_NULL_HANDLE;
  s.shared.reset();R.secondarySharedSnapshots=0;s.snapshotUsed=false;s.returned=false;
  s.shared=std::make_unique<AndroidSharedImage>(s.instance,s.physical,s.primary,s.device,extent);s.sharedExtent=extent;R.secondarySharedSnapshots=1;
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=s.shared->consumer().image;
  view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=VK_FORMAT_R8G8B8A8_UNORM;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  check(s.vkCreateImageView(s.device,&view,nullptr,&s.sourceView),"view shared secondary snapshot");
 }
 const auto out=s.shared->producer();
 const auto copyLayout=s.generalLayout?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
 s.barrier(cmd,out.image,s.snapshotUsed?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,copyLayout,
     s.snapshotUsed?VK_QUEUE_FAMILY_EXTERNAL:VK_QUEUE_FAMILY_IGNORED,s.snapshotUsed?s.family:VK_QUEUE_FAMILY_IGNORED,
     0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,true);
 if(sourceFormat==VK_FORMAT_R8G8B8A8_UNORM) {
  VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;
  copy.extent={extent.width,extent.height,1};
  gfxvk::vkCmdCopyImage(cmd,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,out.image,copyLayout,1,&copy);
 } else {
  VkImageBlit copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;
  copy.srcOffsets[1]=copy.dstOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};
  gfxvk::vkCmdBlitImage(cmd,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,out.image,copyLayout,1,&copy,VK_FILTER_NEAREST);
 }
 s.barrier(cmd,out.image,copyLayout,VK_IMAGE_LAYOUT_GENERAL,s.family,VK_QUEUE_FAMILY_EXTERNAL,
     VK_ACCESS_TRANSFER_WRITE_BIT,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,true);
 s.framePrepared=true;
}
VkSemaphore AndroidIsolatedPresenter::snapshot_wait() const { return impl->returned?impl->shared->producer().returned:VK_NULL_HANDLE; }
VkSemaphore AndroidIsolatedPresenter::snapshot_signal() const { return impl->shared->producer().ready; }
void AndroidIsolatedPresenter::primary_submitted() {
 impl->framePublished=true;impl->snapshotUsed=true;impl->returned=false;
}
void AndroidIsolatedPresenter::present(bool sourceLinear,int filter,bool fxaa) {
 auto& s=*impl;
 if(!s.framePublished || !s.hasAcquired)throw std::logic_error("Secondary snapshot was not submitted");
 s.hasAcquired=false;s.framePrepared=false;s.framePublished=false;
 s.dispatch(Operation::Present,[this,sourceLinear,filter,fxaa] {
  auto& s=*impl;
  // Export/import may involve driver work. The primary signal is already queued,
  // and this worker exclusively owns the hand-off until its result is consumed.
  { StageTimer timer(2);s.shared->transfer_ready(); }
  { StageTimer timer(3);
  check(s.vkResetCommandPool(s.device,s.pool,0),"reset secondary-device commands");
  check(s.vkResetDescriptorPool(s.device,s.descriptors,0),"reset secondary-device descriptors");
  check(s.vkResetFences(s.device,1,&s.drawFence),"reset secondary-device draw fence");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(s.vkBeginCommandBuffer(s.cmd,&begin),"begin secondary-device draw");
  const auto in=s.shared->consumer();
  const auto sampleLayout=s.generalLayout?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  s.barrier(s.cmd,in.image,VK_IMAGE_LAYOUT_GENERAL,sampleLayout,VK_QUEUE_FAMILY_EXTERNAL,s.family,
      0,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  auto draw=s.presenter->prepare(s.sourceView,s.sharedExtent,sourceLinear,s.images.at(s.imageIndex),s.views.at(s.imageIndex),
      s.format,s.extent,s.layouts.at(s.imageIndex),filter,fxaa);
  draw.sourceLayout=sampleLayout;
  s.presenter->record(draw,s.cmd,s.descriptors);
  s.barrier(s.cmd,in.image,sampleLayout,VK_IMAGE_LAYOUT_GENERAL,s.family,VK_QUEUE_FAMILY_EXTERNAL,
      VK_ACCESS_SHADER_READ_BIT,0,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  check(s.vkEndCommandBuffer(s.cmd),"end secondary-device draw");
  }
  const auto in=s.shared->consumer();
  VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&in.ready;submit.pWaitDstStageMask=&stage;
  submit.commandBufferCount=1;submit.pCommandBuffers=&s.cmd;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&in.returned;
  { StageTimer timer(4);check(s.vkQueueSubmit(s.queue,1,&submit,s.drawFence),"submit secondary-device draw"); }
  { StageTimer timer(5);check(s.vkWaitForFences(s.device,1,&s.drawFence,VK_TRUE,UINT64_MAX),"secondary-device drawing completion"); }
  { StageTimer timer(6);s.shared->transfer_returned(); }
  s.returned=true;
  s.layouts[s.imageIndex]=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  while(R.secondaryPresentHeld.load())std::this_thread::sleep_for(std::chrono::milliseconds(2));
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.swapchainCount=1;present.pSwapchains=&s.swapchain;present.pImageIndices=&s.imageIndex;
  VkResult result;
  { StageTimer timer(7);result=s.vkQueuePresentKHR(s.queue,&present); }
  if(R.secondaryInjectPresentLoss.exchange(false) && (result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR))return VK_ERROR_SURFACE_LOST_KHR;
  return result;
 });
}
}
#endif
