#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>
namespace gfxvk {
// Preserve the existing runtime_error interface while retaining the driver code.
class VulkanFailure : public std::runtime_error {
public:
 VkResult result;
 VulkanFailure(VkResult value,const char* operation)
  : std::runtime_error(std::string(operation)+": Vulkan result "+std::to_string(value)),result(value) {}
 bool allocation_failure() const noexcept {
  return result==VK_ERROR_OUT_OF_HOST_MEMORY||result==VK_ERROR_OUT_OF_DEVICE_MEMORY;
 }
};
}
