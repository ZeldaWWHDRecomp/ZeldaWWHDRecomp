#pragma once
#import <Metal/Metal.h>
#include <cstdint>
#include <utility>
#include <vector>
namespace gfx {
// A tagged draw's actual resolved resources, not addresses to reread from guest
// memory. ARC retains buffers/views/samplers; small inline uniform bytes are
// copied because Metal's set*Bytes consumes the original temporary immediately.
struct MetalDrawBindings {
  struct Binding {
    enum Kind { Buffer, Bytes, Texture, Sampler } kind;
    bool vertex;
    NSUInteger index, offset = 0;
    id<MTLBuffer> buffer = nil;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
    std::vector<uint8_t> bytes;
  };
  std::vector<Binding> bindings;
  void buffer(bool vertex, id<MTLBuffer> resource, NSUInteger offset,
              NSUInteger index) {
    Binding b{Binding::Buffer, vertex, index};
    b.buffer = resource;
    b.offset = offset;
    bindings.push_back(std::move(b));
  }
  void bytes(bool vertex, const void *data, NSUInteger length,
             NSUInteger index) {
    Binding b{Binding::Bytes, vertex, index};
    const auto *p = static_cast<const uint8_t *>(data);
    b.bytes.assign(p, p + length);
    bindings.push_back(std::move(b));
  }
  void texture(bool vertex, id<MTLTexture> resource, NSUInteger index) {
    Binding b{Binding::Texture, vertex, index};
    b.texture = resource;
    bindings.push_back(std::move(b));
  }
  void sampler(bool vertex, id<MTLSamplerState> resource, NSUInteger index) {
    Binding b{Binding::Sampler, vertex, index};
    b.sampler = resource;
    bindings.push_back(std::move(b));
  }
  void apply(id<MTLRenderCommandEncoder> encoder) const {
    for (const auto &b : bindings)
      switch (b.kind) {
      case Binding::Buffer:
        if (b.vertex)
          [encoder setVertexBuffer:b.buffer offset:b.offset atIndex:b.index];
        else
          [encoder setFragmentBuffer:b.buffer offset:b.offset atIndex:b.index];
        break;
      case Binding::Bytes:
        if (b.vertex)
          [encoder setVertexBytes:b.bytes.data()
                           length:b.bytes.size()
                          atIndex:b.index];
        else
          [encoder setFragmentBytes:b.bytes.data()
                             length:b.bytes.size()
                            atIndex:b.index];
        break;
      case Binding::Texture:
        if (b.vertex)
          [encoder setVertexTexture:b.texture atIndex:b.index];
        else
          [encoder setFragmentTexture:b.texture atIndex:b.index];
        break;
      case Binding::Sampler:
        if (b.vertex)
          [encoder setVertexSamplerState:b.sampler atIndex:b.index];
        else
          [encoder setFragmentSamplerState:b.sampler atIndex:b.index];
        break;
      }
  }
};
} // namespace gfx
