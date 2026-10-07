#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string_view>

namespace gfxvk {
// Keys describe values, not Vulkan struct padding or descriptor-info pointers.
// A lookup needs no heap allocation; the map owns a string only on insertion.
template<size_t MaxWrites> class DescriptorKey {
  static constexpr size_t bufferBytes = sizeof(VkBuffer) + 2 * sizeof(VkDeviceSize);
  static constexpr size_t imageBytes = sizeof(VkSampler) + sizeof(VkImageView) + sizeof(VkImageLayout);
  static constexpr size_t writeBytes = sizeof(uint32_t) + sizeof(VkDescriptorType) +
      (bufferBytes > imageBytes ? bufferBytes : imageBytes);
  std::array<char, sizeof(VkDescriptorSetLayout) + sizeof(uint32_t) + MaxWrites * writeBytes> bytes;
  size_t used = 0;
  template<class T> void append(const T& value) {
    std::memcpy(bytes.data() + used, &value, sizeof(value));
    used += sizeof(value);
  }
public:
  DescriptorKey(VkDescriptorSetLayout layout, const VkWriteDescriptorSet* writes, uint32_t count) {
    if (count > MaxWrites) throw std::runtime_error("descriptor key capacity exceeded");
    append(layout);
    append(count);
    for (uint32_t i = 0; i < count; ++i) {
      const auto& write = writes[i];
      append(write.dstBinding);
      append(write.descriptorType);
      if (write.pBufferInfo) {
        const auto& info = *write.pBufferInfo;
        append(info.buffer);
        append(info.range);
        // Dynamic offsets are supplied separately at bind time.
        if (write.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
          append(info.offset);
      } else {
        const auto& info = *write.pImageInfo;
        append(info.sampler);
        append(info.imageView);
        append(info.imageLayout);
      }
    }
  }
  std::string_view view() const { return {bytes.data(), used}; }
};

struct DescriptorKeyHash {
  using is_transparent = void;
  // Leave this potentially throwing so libstdc++ retains each node's hash,
  // as it does for std::hash<string>, instead of rehashing long stored keys.
  size_t operator()(std::string_view key) const {
    return std::hash<std::string_view>{}(key);
  }
};
} // namespace gfxvk
