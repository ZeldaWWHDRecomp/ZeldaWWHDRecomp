#pragma once
#import <Metal/Metal.h>
#include <cstdint>
namespace gfx {
// Keep the actual source image alive while a pane waits for its scanout. A
// mutable Surface can replace its texture; its object address is not identity.
struct MetalBackdropSource {
  id<MTLTexture> texture = nil;
  bool valid = true;
  uint64_t key() const {
    return uint64_t(reinterpret_cast<uintptr_t>((__bridge void *)texture));
  }
  bool matches(id<MTLTexture> candidate) const {
    return valid && texture && texture == candidate;
  }
  bool clear(id<MTLTexture> candidate) {
    if (!matches(candidate))
      return false;
    valid = false;
    return true;
  }
};
inline uint64_t metal_backdrop_source_key(id<MTLTexture> texture) {
  return uint64_t(reinterpret_cast<uintptr_t>((__bridge void *)texture));
}
} // namespace gfx
