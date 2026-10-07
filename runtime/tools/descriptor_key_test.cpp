// CPU-only checks for the Vulkan cache's exact key and allocation-free lookup.
#include "gfx/vulkan/descriptor_key.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>

static size_t allocations = 0;
void* operator new(size_t size) {
  ++allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

template<class T> T handle(uintptr_t value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
  else return static_cast<T>(value);
}

// The former serializer is the oracle: preserving these bytes keeps cache
// identity unchanged, including the deliberate omission of dynamic offsets.
std::string reference(VkDescriptorSetLayout layout, const VkWriteDescriptorSet* writes, uint32_t count) {
  std::string key;
  auto append = [&](const auto& v) { key.append(reinterpret_cast<const char*>(&v), sizeof(v)); };
  append(layout); append(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& w = writes[i];
    append(w.dstBinding); append(w.descriptorType);
    if (w.pBufferInfo) {
      append(w.pBufferInfo->buffer); append(w.pBufferInfo->range);
      if (w.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) append(w.pBufferInfo->offset);
    } else {
      append(w.pImageInfo->sampler); append(w.pImageInfo->imageView); append(w.pImageInfo->imageLayout);
    }
  }
  return key;
}

int main() {
  constexpr uint32_t capacity = 17 + 18; // Uniforms + Latte texture units per stage.
  using Key = gfxvk::DescriptorKey<capacity>;
  std::array<VkWriteDescriptorSet, capacity> writes{};
  std::array<VkDescriptorBufferInfo, capacity> buffers{};
  std::array<VkDescriptorImageInfo, capacity> images{};
  auto layout = handle<VkDescriptorSetLayout>(0x1234);
  for (uint32_t i = 0; i < capacity; ++i) {
    auto& w = writes[i];
    w.dstBinding = i;
    w.descriptorType = i % 3 == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC :
        i % 3 == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    buffers[i] = {handle<VkBuffer>(i + 1), VkDeviceSize(i * 256), VkDeviceSize(32 + i)};
    images[i] = {handle<VkSampler>(i + 2), handle<VkImageView>(i + 3), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (i % 3 != 2) w.pBufferInfo = &buffers[i]; else w.pImageInfo = &images[i];
  }
  for (uint32_t count = 0; count <= capacity; ++count) {
    const Key key(layout, writes.data(), count);
    assert(key.view() == reference(layout, writes.data(), count));
  }
  auto largest = writes;
  for (uint32_t i = 0; i < capacity; ++i) {
    largest[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    largest[i].pBufferInfo = &buffers[i];
    largest[i].pImageInfo = nullptr;
  }
  assert(Key(layout, largest.data(), capacity).view() == reference(layout, largest.data(), capacity));
  bool overflowRejected = false;
  try { Key overflow(layout, writes.data(), capacity + 1); }
  catch (const std::runtime_error&) { overflowRejected = true; }
  assert(overflowRejected);

  const std::string original(Key(layout, writes.data(), capacity).view());
  auto differs = [&] { return Key(layout, writes.data(), capacity).view() != original; };
  writes[0].dstSet = handle<VkDescriptorSet>(99); assert(!differs());
  ++buffers[0].offset; assert(!differs()); --buffers[0].offset;
  ++buffers[0].range; assert(differs()); --buffers[0].range;
  buffers[0].buffer = handle<VkBuffer>(99); assert(differs()); buffers[0].buffer = handle<VkBuffer>(1);
  ++buffers[1].offset; assert(differs()); --buffers[1].offset;
  images[2].sampler = handle<VkSampler>(99); assert(differs()); images[2].sampler = handle<VkSampler>(4);
  images[2].imageView = handle<VkImageView>(99); assert(differs()); images[2].imageView = handle<VkImageView>(5);
  images[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL; assert(differs()); images[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  ++writes[0].dstBinding; assert(differs()); --writes[0].dstBinding;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; assert(differs());
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  assert(Key(handle<VkDescriptorSetLayout>(99), writes.data(), capacity).view() != original);

  std::unordered_map<std::string, int, gfxvk::DescriptorKeyHash, std::equal_to<>> cache;
  {
    const Key transient(layout, writes.data(), capacity);
    cache.emplace(std::string(transient.view()), 42);
  } // The map must not reference the stack serializer.
  const size_t before = allocations;
  for (unsigned i = 0; i < 10000; ++i) {
    const Key hit(layout, writes.data(), capacity);
    const auto found = cache.find(hit.view());
    assert(found != cache.end() && found->second == 42);
    const Key miss(handle<VkDescriptorSetLayout>(99), writes.data(), capacity);
    assert(cache.find(miss.view()) == cache.end());
  }
  assert(allocations == before);
  cache.clear();
  assert(cache.find(original) == cache.end());
  std::cout << "PASS: exact descriptor identity, owned keys, 20000 allocation-free lookups\n";
}
