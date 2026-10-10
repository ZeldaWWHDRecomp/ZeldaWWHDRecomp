#pragma once
#ifdef __ANDROID__
#include "loader.h"
#include <memory>

namespace gfxvk {
// One Android GPU allocation imported into two logical devices on the same
// physical device. No CPU mapping/copying. Caller owns queue synchronization,
// image layouts and EXTERNAL queue-family ownership transfers, and must drain
// both devices' uses before destruction. The devices must enable the required
// AHB/external-semaphore extensions.
class AndroidSharedImage {
public:
  struct Endpoint { VkImage image; VkSemaphore ready,returned; };
  AndroidSharedImage(VkInstance,VkPhysicalDevice,VkDevice producer,VkDevice consumer,VkExtent2D);
  ~AndroidSharedImage();
  AndroidSharedImage(const AndroidSharedImage&)=delete;
  AndroidSharedImage& operator=(const AndroidSharedImage&)=delete;
  Endpoint producer() const;
  Endpoint consumer() const;
  // Signal must have been submitted before exporting its sync fd. Importing
  // endpoint semaphore must not still be in use by a previous queue operation.
  void transfer_ready();
  void transfer_returned();
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
// Authored GPU clear/readback assertions only; never used to drive a display.
// Creates separate logical devices, each requesting only queue zero.
void android_shared_image_smoke(bool generalLayout=false);
// Read-only capability queries; does not allocate, import or render shared memory.
void android_external_memory_capabilities();
}
#endif
