#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#include <android/hardware_buffer.h>
#include "android_shared_image.h"
#include <array>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace gfxvk {
namespace {
void check(VkResult result,const char* operation) {
  if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": Vulkan result "+std::to_string(result));
}
template<class Function> Function load(VkDevice device,const char* name) {
  auto function=reinterpret_cast<Function>(vkGetDeviceProcAddr(device,name));
  if(!function)throw std::runtime_error(std::string("Missing shared-image function ")+name);
  return function;
}
constexpr VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
    VK_IMAGE_USAGE_SAMPLED_BIT;
struct Device {
  VkDevice handle=VK_NULL_HANDLE;
#define FUNCTIONS(X) X(vkCreateImage) X(vkDestroyImage) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) \
  X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkGetAndroidHardwareBufferPropertiesANDROID) \
  X(vkGetSemaphoreFdKHR) X(vkImportSemaphoreFdKHR)
#define FIELD(name) PFN_##name name=nullptr;
  FUNCTIONS(FIELD)
#undef FIELD
  void init(VkDevice device) {
    handle=device;
#define LOAD(name) name=load<PFN_##name>(device,#name);
    FUNCTIONS(LOAD)
#undef LOAD
  }
#undef FUNCTIONS
};
}
struct AndroidSharedImage::Impl {
  struct Imported {
    Device device;
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkSemaphore ready=VK_NULL_HANDLE,returned=VK_NULL_HANDLE;
  };
  std::array<Imported,2> imported;
  AHardwareBuffer* buffer=nullptr;
  ~Impl() {
    for(auto& value:imported) {
      auto& api=value.device;
      if(value.ready)api.vkDestroySemaphore(api.handle,value.ready,nullptr);
      if(value.returned)api.vkDestroySemaphore(api.handle,value.returned,nullptr);
      if(value.image)api.vkDestroyImage(api.handle,value.image,nullptr);
      if(value.memory)api.vkFreeMemory(api.handle,value.memory,nullptr);
    }
    if(buffer)AHardwareBuffer_release(buffer);
  }
  void image(Imported& value,VkExtent2D extent) {
    auto& api=value.device;
    VkAndroidHardwareBufferFormatPropertiesANDROID format{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID properties{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    properties.pNext=&format;
    check(api.vkGetAndroidHardwareBufferPropertiesANDROID(api.handle,buffer,&properties),"query imported AHB");
    if(format.format!=VK_FORMAT_R8G8B8A8_UNORM || !properties.memoryTypeBits)
      throw std::runtime_error("Shared AHB has no compatible RGBA8 allocation");
    VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.pNext=&external;
    image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;
    image.extent={extent.width,extent.height,1};image.mipLevels=1;image.arrayLayers=1;
    image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=usage;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    check(api.vkCreateImage(api.handle,&image,nullptr,&value.image),"create shared AHB image");
    VkImportAndroidHardwareBufferInfoANDROID importInfo{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    importInfo.buffer=buffer;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.pNext=&importInfo;dedicated.image=value.image;
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.pNext=&dedicated;
    memory.allocationSize=properties.allocationSize;
    while(!(properties.memoryTypeBits&(1u<<memory.memoryTypeIndex)))++memory.memoryTypeIndex;
    check(api.vkAllocateMemory(api.handle,&memory,nullptr,&value.memory),"import shared AHB allocation");
    check(api.vkBindImageMemory(api.handle,value.image,value.memory,0),"bind shared AHB allocation");
  }
  void semaphore(Imported& owner,VkSemaphore& value,bool exported) {
    VkExportSemaphoreCreateInfo external{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    external.handleTypes=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if(exported)create.pNext=&external;
    check(owner.device.vkCreateSemaphore(owner.device.handle,&create,nullptr,&value),"create shared GPU semaphore");
  }
  void transfer(Imported& source,Imported& target,VkSemaphore exported,VkSemaphore imported) {
    VkSemaphoreGetFdInfoKHR get{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    get.semaphore=exported;get.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd=-1;
    check(source.device.vkGetSemaphoreFdKHR(source.device.handle,&get,&fd),"export GPU semaphore sync fd");
    VkImportSemaphoreFdInfoKHR take{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
    take.semaphore=imported;take.handleType=get.handleType;take.fd=fd;
    take.flags=VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
    const auto status=target.device.vkImportSemaphoreFdKHR(target.device.handle,&take);
    if(status!=VK_SUCCESS && fd>=0)close(fd); // successful import transfers fd ownership
    check(status,"import GPU semaphore sync fd");
  }
};
AndroidSharedImage::AndroidSharedImage(VkInstance instance,VkPhysicalDevice physical,
    VkDevice producer,VkDevice consumer,VkExtent2D extent):impl(std::make_unique<Impl>()) {
  if(!extent.width || !extent.height || producer==consumer)
    throw std::runtime_error("Shared GPU image requires two devices and a nonempty extent");
  auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
      vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceImageFormatProperties2"));
  auto semaphores=reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
      vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceExternalSemaphoreProperties"));
  if(!query || !semaphores)throw std::runtime_error("Shared GPU images require Vulkan 1.1 capability queries");
  VkPhysicalDeviceExternalSemaphoreInfo semaphore{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
  semaphore.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  VkExternalSemaphoreProperties supported{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
  semaphores(physical,&semaphore,&supported);
  constexpr auto required=VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT|VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
  if((supported.externalSemaphoreFeatures&required)!=required)
    throw std::runtime_error("Device cannot share sync-fd GPU semaphores");
  VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
  VkPhysicalDeviceImageFormatInfo2 image{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  image.pNext=&external;image.format=VK_FORMAT_R8G8B8A8_UNORM;image.type=VK_IMAGE_TYPE_2D;
  image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=usage;
  VkAndroidHardwareBufferUsageANDROID android{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID};
  VkExternalImageFormatProperties memory{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};memory.pNext=&android;
  VkImageFormatProperties2 properties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};properties.pNext=&memory;
  check(query(physical,&image,&properties),"query shareable RGBA8 image");
  if(!(memory.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
      extent.width>properties.imageFormatProperties.maxExtent.width || extent.height>properties.imageFormatProperties.maxExtent.height)
    throw std::runtime_error("Device cannot import requested RGBA8 GPU image");
  AHardwareBuffer_Desc description{};description.width=extent.width;description.height=extent.height;
  description.layers=1;description.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  description.usage=android.androidHardwareBufferUsage;
  if(AHardwareBuffer_allocate(&description,&impl->buffer)!=0)
    throw std::runtime_error("Cannot allocate shared Android GPU buffer");
  impl->imported[0].device.init(producer);impl->imported[1].device.init(consumer);
  impl->image(impl->imported[0],extent);impl->image(impl->imported[1],extent);
  impl->semaphore(impl->imported[0],impl->imported[0].ready,true);
  impl->semaphore(impl->imported[1],impl->imported[1].ready,false);
  impl->semaphore(impl->imported[1],impl->imported[1].returned,true);
  impl->semaphore(impl->imported[0],impl->imported[0].returned,false);
}
AndroidSharedImage::~AndroidSharedImage()=default;
void AndroidSharedImage::transfer_ready() {
  impl->transfer(impl->imported[0],impl->imported[1],impl->imported[0].ready,impl->imported[1].ready);
}
void AndroidSharedImage::transfer_returned() {
  impl->transfer(impl->imported[1],impl->imported[0],impl->imported[1].returned,impl->imported[0].returned);
}
AndroidSharedImage::Endpoint AndroidSharedImage::producer() const {
  auto& value=impl->imported[0];return {value.image,value.ready,value.returned};
}
AndroidSharedImage::Endpoint AndroidSharedImage::consumer() const {
  auto& value=impl->imported[1];return {value.image,value.ready,value.returned};
}
}
#endif
