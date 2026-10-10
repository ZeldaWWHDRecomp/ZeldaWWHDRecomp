#ifdef __ANDROID__
#include "android_shared_image.h"
#include "backend.h"
#include "present.h"
#include "runtime.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gfxvk {
namespace {
void check(VkResult value,const char* operation) {
  if(value!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": "+std::to_string(value));
}
#define FUNCTIONS(X) X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) \
  X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandPool) \
  X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) X(vkCmdClearColorImage) \
  X(vkCmdCopyImageToBuffer) X(vkQueueSubmit) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) \
  X(vkResetFences) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
  X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) \
  X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory)
struct TestDevice {
  VkDevice device=VK_NULL_HANDLE;
  VkQueue queue=VK_NULL_HANDLE;
  VkCommandPool pool=VK_NULL_HANDLE;
  VkCommandBuffer commands=VK_NULL_HANDLE;
  VkFence fence=VK_NULL_HANDLE;
  VkBuffer readback=VK_NULL_HANDLE;
  VkDeviceMemory memory=VK_NULL_HANDLE;
  void* mapped=nullptr;
  VkImage target=VK_NULL_HANDLE;
  VkDeviceMemory targetMemory=VK_NULL_HANDLE;
  VkImageView sourceView=VK_NULL_HANDLE,targetView=VK_NULL_HANDLE;
  VkDescriptorPool descriptors=VK_NULL_HANDLE;
#define FIELD(name) PFN_##name name=nullptr;
  FUNCTIONS(FIELD)
#undef FIELD
  ~TestDevice() {
    if(!device)return;
    if(vkDeviceWaitIdle)vkDeviceWaitIdle(device);
    if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);
    if(sourceView)vkDestroyImageView(device,sourceView,nullptr);
    if(targetView)vkDestroyImageView(device,targetView,nullptr);
    if(target)vkDestroyImage(device,target,nullptr);
    if(targetMemory)vkFreeMemory(device,targetMemory,nullptr);
    if(mapped)vkUnmapMemory(device,memory);
    if(readback)vkDestroyBuffer(device,readback,nullptr);
    if(memory)vkFreeMemory(device,memory,nullptr);
    if(fence)vkDestroyFence(device,fence,nullptr);
    if(pool)vkDestroyCommandPool(device,pool,nullptr);
    if(vkDestroyDevice)vkDestroyDevice(device,nullptr);
  }
  void init() {
    std::vector<const char*> extensions={"VK_ANDROID_external_memory_android_hardware_buffer",
                             "VK_KHR_external_semaphore_fd","VK_EXT_queue_family_foreign"};
    if(R.dynamicRenderingKHR) {
      extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
      if(R.properties.apiVersion<VK_API_VERSION_1_2) {
        extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
      }
    }
    VkPhysicalDeviceDynamicRenderingFeatures rendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    rendering.dynamicRendering=VK_TRUE;
    float priority=1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex=R.queueFamily;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create.queueCreateInfoCount=1;create.pQueueCreateInfos=&queueInfo;
    create.enabledExtensionCount=std::size(extensions);create.ppEnabledExtensionNames=extensions.data();create.pNext=&rendering;
    check(gfxvk::vkCreateDevice(R.physicalDevice,&create,nullptr,&device),"create isolated probe device");
#define LOAD(name) name=reinterpret_cast<PFN_##name>(gfxvk::vkGetDeviceProcAddr(device,#name)); \
    if(!name)throw std::runtime_error("Missing isolated probe function " #name);
    FUNCTIONS(LOAD)
#undef LOAD
    vkGetDeviceQueue(device,R.queueFamily,0,&queue);
    VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPool.queueFamilyIndex=R.queueFamily;
    check(vkCreateCommandPool(device,&commandPool,nullptr,&pool),"create isolated probe command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
    check(vkAllocateCommandBuffers(device,&allocate,&commands),"allocate isolated probe commands");
    VkFenceCreateInfo completion{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device,&completion,nullptr,&fence),"create isolated probe fence");
  }
  void make_readback(VkDeviceSize bytes) {
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size=bytes;create.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device,&create,nullptr,&readback),"create isolated probe readback");
    VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,readback,&requirements);
    VkPhysicalDeviceMemoryProperties properties{};gfxvk::vkGetPhysicalDeviceMemoryProperties(R.physicalDevice,&properties);
    constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t index=0;
    while(index<properties.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<index)) ||
        (properties.memoryTypes[index].propertyFlags&flags)!=flags))++index;
    if(index==properties.memoryTypeCount)throw std::runtime_error("No coherent probe readback memory");
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=index;
    check(vkAllocateMemory(device,&allocate,nullptr,&memory),"allocate isolated probe readback");
    check(vkBindBufferMemory(device,readback,memory,0),"bind isolated probe readback");
    check(vkMapMemory(device,memory,0,bytes,0,&mapped),"map isolated probe assertion buffer");
  }
  void make_target(VkImage source,VkExtent2D extent) {
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType=VK_IMAGE_TYPE_2D;create.format=VK_FORMAT_R8G8B8A8_UNORM;
    create.extent={extent.width,extent.height,1};create.mipLevels=1;create.arrayLayers=1;
    create.samples=VK_SAMPLE_COUNT_1_BIT;create.tiling=VK_IMAGE_TILING_OPTIMAL;
    create.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device,&create,nullptr,&target),"create independent presentation target");
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,target,&requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize=requirements.size;
    while(!(requirements.memoryTypeBits&(1u<<memory.memoryTypeIndex)))++memory.memoryTypeIndex;
    check(vkAllocateMemory(device,&memory,nullptr,&targetMemory),"allocate independent presentation target");
    check(vkBindImageMemory(device,target,targetMemory,0),"bind independent presentation target");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};view.image=target;
    check(vkCreateImageView(device,&view,nullptr,&targetView),"view independent presentation target");
    view.image=source;
    check(vkCreateImageView(device,&view,nullptr,&sourceView),"view independent presentation source");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&size;
    check(vkCreateDescriptorPool(device,&pool,nullptr,&descriptors),"independent presentation descriptor pool");
  }
  void begin(bool reset) {
    if(reset) {
      check(vkResetFences(device,1,&fence),"reset isolated probe fence");
      check(vkResetCommandPool(device,pool,0),"reset isolated probe pool");
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commands,&begin),"begin isolated probe commands");
  }
  void image_barrier(VkImage image,VkImageLayout from,VkImageLayout to,uint32_t source,uint32_t target,
      VkAccessFlags sourceAccess,VkAccessFlags targetAccess,VkPipelineStageFlags sourceStage,VkPipelineStageFlags targetStage) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout=from;barrier.newLayout=to;barrier.srcQueueFamilyIndex=source;barrier.dstQueueFamilyIndex=target;
    barrier.srcAccessMask=sourceAccess;barrier.dstAccessMask=targetAccess;barrier.image=image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(commands,sourceStage,targetStage,0,0,nullptr,0,nullptr,1,&barrier);
  }
  void submit(VkSemaphore wait,VkSemaphore signal) {
    check(vkEndCommandBuffer(commands),"end isolated probe commands");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if(wait){submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&wait;submit.pWaitDstStageMask=&stage;}
    submit.commandBufferCount=1;submit.pCommandBuffers=&commands;
    submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&signal;
    check(vkQueueSubmit(queue,1,&submit,fence),"submit isolated probe commands");
  }
  void finish() {check(vkWaitForFences(device,1,&fence,VK_TRUE,10'000'000'000ull),"isolated GPU probe timed out");}
};
#undef FUNCTIONS
}
void android_external_memory_capabilities() {
    auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
        gfxvk::vkGetInstanceProcAddr(R.instance,"vkGetPhysicalDeviceImageFormatProperties2"));
    auto buffers=reinterpret_cast<PFN_vkGetPhysicalDeviceExternalBufferProperties>(
        gfxvk::vkGetInstanceProcAddr(R.instance,"vkGetPhysicalDeviceExternalBufferProperties"));
    if(!query || !buffers)throw std::runtime_error("Missing external memory capability query functions");
    for(auto handle:{VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                    VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID}) {
      VkPhysicalDeviceExternalBufferInfo info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
      info.handleType=handle;info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      VkExternalBufferProperties properties{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
      buffers(R.physicalDevice,&info,&properties);
      LOG("[vulkan] external buffer capability handle=%u features=%u compatible=%u",unsigned(handle),
          properties.externalMemoryProperties.externalMemoryFeatures,properties.externalMemoryProperties.compatibleHandleTypes);
    }
    for(auto handle:{VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT}) {
      for(VkImageUsageFlags usage:{VkImageUsageFlags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
          VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
          VkImageUsageFlags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT),
          VkImageUsageFlags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT)}) {
        VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};external.handleType=handle;
        VkPhysicalDeviceImageFormatInfo2 image{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
        image.pNext=&external;image.format=VK_FORMAT_R8G8B8A8_UNORM;image.type=VK_IMAGE_TYPE_2D;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=usage;
        VkExternalImageFormatProperties memory{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
        VkImageFormatProperties2 properties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};properties.pNext=&memory;
        const auto result=query(R.physicalDevice,&image,&properties);
        LOG("[vulkan] external image capability handle=%u usage=%u result=%d features=%u compatible=%u",unsigned(handle),usage,
            int(result),memory.externalMemoryProperties.externalMemoryFeatures,memory.externalMemoryProperties.compatibleHandleTypes);
      }
    }
}
void android_shared_image_smoke(bool generalLayout) {
  std::array<TestDevice,2> devices;
  devices[0].init();devices[1].init();
  if(devices[0].device==devices[1].device || devices[0].queue==devices[1].queue)
    throw std::runtime_error("Probe did not receive independent logical devices/queues");
  constexpr VkExtent2D extent{64,36};
  const auto copyLayout=generalLayout?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  const auto sampleLayout=generalLayout?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  auto& producer=devices[0];auto& consumer=devices[1];
  consumer.make_readback(extent.width*extent.height*4);
  AndroidSharedImage shared(R.instance,R.physicalDevice,producer.device,consumer.device,extent);
  const auto out=shared.producer(),in=shared.consumer();
  struct SourceViewGuard {
    TestDevice& owner;
    ~SourceViewGuard() {
      if(owner.sourceView)owner.vkDestroyImageView(owner.device,owner.sourceView,nullptr);
      owner.sourceView=VK_NULL_HANDLE;
    }
  } sourceViewGuard{consumer};
  consumer.make_target(in.image,extent);
  IndependentPresenter presenter(consumer.device,R.dynamicRenderingKHR);
  const std::array<std::array<float,4>,3> colors{{{.25f,.5f,.75f,1},{1,0,0,1},{0,1,0,1}}};
  // Always drain before destroying imported images, even when an assertion
  // throws. These waits belong to an explicit debug probe, never normal frames.
  try {
    for(size_t round=0;round<colors.size();++round) {
      producer.begin(round!=0);
      producer.image_barrier(out.image,round?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,
          copyLayout,round?VK_QUEUE_FAMILY_EXTERNAL:VK_QUEUE_FAMILY_IGNORED,
          round?R.queueFamily:VK_QUEUE_FAMILY_IGNORED,0,VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkClearColorValue value{};std::copy(colors[round].begin(),colors[round].end(),value.float32);
      VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      producer.vkCmdClearColorImage(producer.commands,out.image,copyLayout,&value,1,&range);
      producer.image_barrier(out.image,copyLayout,VK_IMAGE_LAYOUT_GENERAL,
          R.queueFamily,VK_QUEUE_FAMILY_EXTERNAL,VK_ACCESS_TRANSFER_WRITE_BIT,0,
          VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      producer.submit(round?out.returned:VK_NULL_HANDLE,out.ready);
      shared.transfer_ready();
      consumer.begin(round!=0);
      consumer.image_barrier(in.image,VK_IMAGE_LAYOUT_GENERAL,sampleLayout,
          VK_QUEUE_FAMILY_EXTERNAL,R.queueFamily,0,VK_ACCESS_SHADER_READ_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      if(round)check(consumer.vkResetDescriptorPool(consumer.device,consumer.descriptors,0),"reset independent presentation descriptors");
      auto draw=presenter.prepare(consumer.sourceView,extent,false,consumer.target,
          consumer.targetView,VK_FORMAT_R8G8B8A8_UNORM,extent,
          round?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
          int(round),round==2,VK_IMAGE_LAYOUT_GENERAL);
      draw.sourceLayout=sampleLayout;
      presenter.record(draw,consumer.commands,consumer.descriptors);
      consumer.image_barrier(consumer.target,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
      copy.imageExtent={extent.width,extent.height,1};
      consumer.vkCmdCopyImageToBuffer(consumer.commands,consumer.target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,consumer.readback,1,&copy);
      consumer.image_barrier(in.image,sampleLayout,VK_IMAGE_LAYOUT_GENERAL,
          R.queueFamily,VK_QUEUE_FAMILY_EXTERNAL,VK_ACCESS_SHADER_READ_BIT,0,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
      VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
      host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      host.buffer=consumer.readback;host.size=VK_WHOLE_SIZE;
      consumer.vkCmdPipelineBarrier(consumer.commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
          0,0,nullptr,1,&host,0,nullptr);
      consumer.submit(in.ready,in.returned);
      consumer.finish();producer.finish();
      shared.transfer_returned();
      const auto* pixels=static_cast<const uint8_t*>(consumer.mapped);
      for(size_t pixel=0;pixel<extent.width*extent.height*4;++pixel)
        if(std::abs(int(pixels[pixel])-int(std::lround(colors[round][pixel%4]*255)))>1)
          throw std::runtime_error("Two-device Android GPU image bytes differ");
    }
  } catch(...) {
    producer.vkDeviceWaitIdle(producer.device);consumer.vkDeviceWaitIdle(consumer.device);
    throw;
  }
}
}
#endif
