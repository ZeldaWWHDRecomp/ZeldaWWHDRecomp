// Synthetic API entry points and handles only; no driver, game or guest data.
#include "gfx/vulkan/pod_commands.h"
#include "gfx/vulkan/handoff_trace.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace gfxvk {
#define DEFINE(name) PFN_##name name = nullptr;
WWHD_VK_DEVICE_FUNCTIONS(DEFINE)
WWHD_VK_RENDERING_FUNCTIONS(DEFINE)
#undef DEFINE
} // namespace gfxvk
namespace {
std::vector<int> calls;
template <class T> T handle(uintptr_t n) {
  if constexpr (std::is_pointer_v<T>)
    return reinterpret_cast<T>(n);
  else
    return static_cast<T>(n);
}
const auto cmd = handle<VkCommandBuffer>(1);
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer c, uint32_t n, uint32_t i,
                                uint32_t f, uint32_t fi) {
  assert(c == cmd && n == 3 && i == 1 && f == 2 && fi == 0);
  calls.push_back(1);
}
VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer c, uint32_t first,
                                    uint32_t count, const VkViewport *v) {
  assert(c == cmd && first == 0 && count == 1 && v[0].width == 640);
  calls.push_back(2);
}
VKAPI_ATTR void VKAPI_CALL vertices(VkCommandBuffer, uint32_t first, uint32_t n,
                                    const VkBuffer *b, const VkDeviceSize *o) {
  assert(first == 1 && n == 2 && b[1] == handle<VkBuffer>(3) && o[1] == 96);
  calls.push_back(3);
}
VKAPI_ATTR void VKAPI_CALL descriptors(VkCommandBuffer, VkPipelineBindPoint,
                                       VkPipelineLayout, uint32_t, uint32_t n,
                                       const VkDescriptorSet *s, uint32_t no,
                                       const uint32_t *o) {
  assert(n == 1 && s[0] == handle<VkDescriptorSet>(4) && no == 2 && o[1] == 64);
  calls.push_back(4);
}
VKAPI_ATTR void VKAPI_CALL constants(VkCommandBuffer, VkPipelineLayout,
                                     VkShaderStageFlags, uint32_t off,
                                     uint32_t n, const void *p) {
  assert(off == 4 && n == 8);
  std::array<uint32_t, 2> v{};
  std::memcpy(v.data(), p, n);
  assert(v[0] == 123 && v[1] == 456);
  calls.push_back(5);
}
VKAPI_ATTR void VKAPI_CALL clear(VkCommandBuffer, VkImage, VkImageLayout,
                                 const VkClearColorValue *v, uint32_t n,
                                 const VkImageSubresourceRange *ranges) {
  assert(v->float32[0] == .5f && n == 1 && ranges[0].baseMipLevel == 2);
  calls.push_back(6);
}
VKAPI_ATTR void VKAPI_CALL rendering(VkCommandBuffer,
                                     const VkRenderingInfo *v) {
  assert(!v->pNext && v->colorAttachmentCount == 1 &&
         v->pColorAttachments[0].imageView == handle<VkImageView>(5));
  assert(v->pDepthAttachment == v->pStencilAttachment &&
         v->pDepthAttachment->clearValue.depthStencil.stencil == 7);
  calls.push_back(7);
}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags,
                                   VkPipelineStageFlags, VkDependencyFlags,
                                   uint32_t nm, const VkMemoryBarrier *m,
                                   uint32_t nb, const VkBufferMemoryBarrier *b,
                                   uint32_t ni, const VkImageMemoryBarrier *i) {
  assert(nm == 1 && nb == 1 && ni == 1 && !m->pNext && !b->pNext && !i->pNext &&
         b->offset == 128 && i->subresourceRange.baseMipLevel == 2);
  calls.push_back(8);
}
VKAPI_ATTR void VKAPI_CALL renderpass(VkCommandBuffer,
                                      const VkRenderPassBeginInfo *info,
                                      VkSubpassContents c) {
  assert(!info->pNext && info->clearValueCount == 1 &&
         info->pClearValues[0].color.uint32[0] == 99 &&
         c == VK_SUBPASS_CONTENTS_INLINE);
  calls.push_back(9);
}
VKAPI_ATTR void VKAPI_CALL endpass(VkCommandBuffer) { calls.push_back(10); }
VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer c,
                                     const VkCommandBufferBeginInfo *i) {
  assert(c == cmd && i->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT &&
         !i->pNext && !i->pInheritanceInfo);
  calls.push_back(11);
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer c) {
  assert(c == cmd);
  calls.push_back(12);
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL reset(VkDevice, uint32_t n, const VkFence *f) {
  assert(n == 1 && *f == handle<VkFence>(6));
  calls.push_back(13);
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue, uint32_t n,
                                      const VkSubmitInfo *s, VkFence) {
  assert(n == 1 && s->commandBufferCount == 1 && *s->pCommandBuffers == cmd &&
         s->waitSemaphoreCount == 2 && s->signalSemaphoreCount == 2);
  assert(s->pWaitSemaphores[0] == handle<VkSemaphore>(7) &&
         s->pWaitSemaphores[1] == handle<VkSemaphore>(9));
  assert(s->pSignalSemaphores[0] == handle<VkSemaphore>(8) &&
         s->pSignalSemaphores[1] == handle<VkSemaphore>(10));
  assert(s->pWaitDstStageMask[0] == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT &&
         s->pWaitDstStageMask[1] == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  calls.push_back(14);
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL failedEnd(VkCommandBuffer) {
  return VK_ERROR_DEVICE_LOST;
}
void malformed(const std::vector<std::byte> &b) {
  calls.clear();
  bool failed = false;
  try {
    gfxvk::PodCommands::replay(b);
  } catch (const std::runtime_error &) {
    failed = true;
  }
  assert(failed && calls.empty());
}

std::vector<std::vector<std::byte>> expectedPayloads;
size_t payloadIndex = 0;
VKAPI_ATTR void VKAPI_CALL variableConstants(VkCommandBuffer c,
    VkPipelineLayout, VkShaderStageFlags, uint32_t offset,
    uint32_t size, const void *data) {
  assert(c == cmd && offset == 0 && payloadIndex < expectedPayloads.size());
  const auto &expected = expectedPayloads[payloadIndex++];
  assert(size == expected.size() && std::memcmp(data, expected.data(), size) == 0);
  calls.push_back(15);
}
// Locate the uniquely tagged synthetic structure without assuming packet offsets.
// Mutate its encoded pNext, not the caller input: encoder rejection cannot test
// the dry replay pass's nested-chain validation.
template <class T>
void malformedNestedChain(std::vector<std::byte> stream, const T &original) {
  const auto *first = reinterpret_cast<const std::byte *>(&original);
  auto found = std::search(stream.begin(), stream.end(), first, first + sizeof(T));
  assert(found != stream.end());
  assert(std::search(found + sizeof(T), stream.end(), first, first + sizeof(T)) == stream.end());
  const void *unsupported = reinterpret_cast<const void *>(uintptr_t(0x1234));
  std::memcpy(&*found + offsetof(T, pNext), &unsupported, sizeof(unsupported));
  malformed(stream); // Must reject before dispatching the preceding valid draw.
}
void variablePayloadAndLateChainRegressions() {
  using gfxvk::PodCommands;
  // Fixed encoded scalar fields precede the variable bytes. Exercise writer
  // payload sizes 252/256/260 plus a larger payload, while testing API ownership.
  const size_t fixed = sizeof(VkCommandBuffer) + sizeof(VkPipelineLayout) +
      sizeof(VkShaderStageFlags) + 2 * sizeof(uint32_t);
  assert(fixed < 252 && fixed % 4 == 0);
  expectedPayloads.clear(); payloadIndex = 0;
  PodCommands payloads;
  assert(payloads.vkCmdDraw(cmd, 3, 1, 2, 0));
  for (size_t packetBytes : {size_t(252), size_t(256), size_t(260), size_t(4096)}) {
    std::vector<std::byte> input(packetBytes - fixed);
    for (size_t i = 0; i < input.size(); ++i) input[i] = std::byte((i * 17 + packetBytes) & 255);
    expectedPayloads.push_back(input);
    assert(payloads.vkCmdPushConstants(cmd, {}, VK_SHADER_STAGE_FRAGMENT_BIT,
                                      0, uint32_t(input.size()), input.data()));
    std::fill(input.begin(), input.end(), std::byte{0});
  }
  gfxvk::vkCmdPushConstants = variableConstants;
  calls.clear(); PodCommands::replay(payloads.take());
  assert(payloadIndex == 4 && (calls == std::vector<int>{1,15,15,15,15}));
  gfxvk::vkCmdPushConstants = constants;

  // Derive the complete encoded budget from an ordinary stream, preserving the
  // valid prefix on a one-byte-short budget and rejecting another append at full.
  std::vector<std::byte> large(4096, std::byte{0x5a});
  PodCommands sizing;
  assert(sizing.vkCmdDraw(cmd, 3, 1, 2, 0));
  assert(sizing.vkCmdPushConstants(cmd, {}, 1, 0, uint32_t(large.size()), large.data()));
  const auto exact = sizing.snapshot().size();
  PodCommands fits({4, exact}), shortByOne({4, exact - 1});
  for (auto *p : {&fits, &shortByOne}) assert(p->vkCmdDraw(cmd, 3, 1, 2, 0));
  const auto prefix = shortByOne.snapshot();
  assert(!shortByOne.vkCmdPushConstants(cmd, {}, 1, 0, uint32_t(large.size()), large.data()));
  assert(shortByOne.snapshot() == prefix && shortByOne.command_count() == 1);
  assert(fits.vkCmdPushConstants(cmd, {}, 1, 0, uint32_t(large.size()), large.data()));
  const auto full = fits.snapshot();
  assert(full == sizing.snapshot());
  assert(!fits.vkCmdDraw(cmd, 3, 1, 2, 0) && fits.snapshot() == full);

  VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = handle<VkImageView>(0x9876);
  VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
  info.layerCount = 1; info.colorAttachmentCount = 1; info.pColorAttachments = &color;
  PodCommands renderingStream;
  assert(renderingStream.vkCmdDraw(cmd, 3, 1, 2, 0));
  assert(renderingStream.vkCmdBeginRendering(cmd, &info));
  malformedNestedChain(renderingStream.snapshot(), color);

  VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; memory.srcAccessMask = 0x123;
  VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; buffer.offset = 0x8765;
  VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; image.image = handle<VkImage>(0x4567);
  PodCommands barriers;
  assert(barriers.vkCmdDraw(cmd, 3, 1, 2, 0));
  assert(barriers.vkCmdPipelineBarrier(cmd, 1, 2, 0, 1, &memory, 1, &buffer, 1, &image));
  const auto wire = barriers.snapshot();
  malformedNestedChain(wire, memory);
  malformedNestedChain(wire, buffer);
  malformedNestedChain(wire, image);
}

} // namespace
// Rejection metadata changes on failures only; no packet bytes or replay calls.
static void failure_reason_regressions() {
  using namespace gfxvk;
  const VkCommandBuffer cmd{};
  PodCommands commands({0, 4096});
  assert(!commands.vkCmdDraw(cmd, 3, 1, 0, 0));
  assert(commands.failure_reason() == PodFailureReason::CommandLimit);
  assert(commands.snapshot().empty());
  PodCommands bytes({100, 0});
  assert(!bytes.vkCmdDraw(cmd, 3, 1, 0, 0));
  assert(bytes.failure_reason() == PodFailureReason::ByteBudgetOrOverflow);
  PodCommands p;
  assert(!p.vkCmdSetViewport(cmd, 0, 1, nullptr));
  assert(p.failure_reason() == PodFailureReason::InvalidInput);
  assert(p.vkCmdDraw(cmd, 3, 1, 0, 0));
  const auto prefix = p.snapshot();
  VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
  info.pNext = &info;
  assert(!p.vkCmdBeginRendering(cmd, &info));
  assert(p.failure_reason() == PodFailureReason::UnsupportedPNext);
  assert(p.snapshot() == prefix && p.command_count() == 1);
  assert(!p.vkCmdSetViewport(cmd, 0, 1, nullptr));
  assert(p.failure_reason() == PodFailureReason::InvalidInput);
  assert(p.snapshot() == prefix && p.command_count() == 1);
}


void directScalarEquivalenceAndAtomicity() {
  using gfxvk::PodCommands;
  for (uint32_t n=0;n<64;++n) {
    PodCommands old, direct; direct.set_direct_scalars(true);
    auto encode=[&](PodCommands& p) {
      assert(p.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(n+7)));
      assert(p.vkCmdBindIndexBuffer(cmd,handle<VkBuffer>(n+11),VkDeviceSize(n)*1024,VK_INDEX_TYPE_UINT16));
      assert(p.vkCmdDraw(cmd,n,1,n+2,0));
      assert(p.vkCmdDrawIndexed(cmd,n,2,n+3,-int32_t(n),4));
    };
    encode(old);encode(direct);assert(old.snapshot()==direct.snapshot());assert(direct.command_count()==4);
  }
  // A stream can mix original and direct encoders without changing packet order.
  PodCommands legacy, mixed;
  for (unsigned n=0;n<12;++n) {
    mixed.set_direct_scalars(n%2);
    assert(legacy.vkCmdDrawIndexed(cmd,n+3,1,n,-int32_t(n),0));
    assert(mixed.vkCmdDrawIndexed(cmd,n+3,1,n,-int32_t(n),0));
    assert(legacy.snapshot()==mixed.snapshot());
  }
  PodCommands reference;assert(reference.vkCmdDraw(cmd,3,1,2,0));
  const size_t onePacket=reference.payload_bytes();
  for (auto limits : {PodCommands::Limits{1,4096},PodCommands::Limits{8,onePacket}}) {
    PodCommands bounded(limits);bounded.set_direct_scalars(true);
    assert(bounded.vkCmdDraw(cmd,3,1,2,0));const auto prefix=bounded.snapshot();
    assert(!bounded.vkCmdDraw(cmd,3,1,2,0));
    assert(bounded.snapshot()==prefix&&bounded.command_count()==1);
  }
  for (auto limits : {PodCommands::Limits{0,4096},PodCommands::Limits{1,1}}) {
    PodCommands direct(limits);direct.set_direct_scalars(true);auto before=direct.snapshot();
    assert(!direct.vkCmdDraw(cmd,3,1,2,0));assert(direct.snapshot()==before&&direct.command_count()==0);
  }
  PodCommands direct;direct.set_direct_scalars(true);assert(direct.vkCmdDraw(cmd,3,1,2,0));
  const auto before=direct.snapshot();const auto count=direct.command_count();
  direct.fail_next_direct_growth_for_test();assert(!direct.vkCmdDrawIndexed(cmd,3,1,0,-2,0));
  assert(direct.failure_reason()==gfxvk::PodFailureReason::AllocationFailure&&direct.snapshot()==before&&direct.command_count()==count);
  assert(direct.vkCmdDrawIndexed(cmd,3,1,0,-2,0));
}
int main() {
  directScalarEquivalenceAndAtomicity();
  failure_reason_regressions();
  using namespace gfxvk;
  vkCmdDraw = draw;
  vkCmdSetViewport = viewport;
  vkCmdBindVertexBuffers = vertices;
  vkCmdBindDescriptorSets = descriptors;
  vkCmdPushConstants = constants;
  vkCmdClearColorImage = clear;
  vkCmdBeginRendering = rendering;
  vkCmdPipelineBarrier = barrier;
  vkCmdBeginRenderPass = renderpass;
  vkCmdEndRenderPass = endpass;
  vkBeginCommandBuffer = begin;
  vkEndCommandBuffer = end;
  vkResetFences = reset;
  vkQueueSubmit = submit;
  {
    PodCommands stream;
    std::vector<std::byte> spare;
    spare.reserve(4096);
    const auto* allocation = spare.data();
    assert(stream.take(std::move(spare)).empty());
    assert(stream.vkCmdDraw(cmd, 3, 1, 2, 0));
    auto first = stream.take();
    assert(first.data() == allocation && stream.empty());
    const auto saved = first;
    assert(stream.vkCmdDraw(cmd, 3, 1, 2, 0));
    assert(first == saved); // producer never modifies the consumer's storage
    auto second = stream.take(std::move(first));
    assert(stream.empty() && stream.command_count() == 0);
    assert(stream.vkCmdDraw(cmd, 3, 1, 2, 0));
    auto third = stream.take();
    assert(third.data() == allocation && third == second);
    calls.clear(); PodCommands::replay(third);
    assert(calls == std::vector<int>{1}); calls.clear();
  }
  PodCommands p;
  assert(p.vkCmdDraw(cmd, 3, 1, 2, 0));
  {
    VkViewport v{};
    v.width = 640;
    assert(p.vkCmdSetViewport(cmd, 0, 1, &v));
    v.width = 1;
    VkBuffer b[]{handle<VkBuffer>(2), handle<VkBuffer>(3)};
    VkDeviceSize offsets[]{32, 96};
    assert(p.vkCmdBindVertexBuffers(cmd, 1, 2, b, offsets));
    b[1] = {};
    offsets[1] = 0;
    VkDescriptorSet sets[]{handle<VkDescriptorSet>(4)};
    uint32_t dynamic[]{32, 64};
    assert(p.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, {},
                                     0, 1, sets, 2, dynamic));
    dynamic[1] = 0;
    uint32_t values[]{123, 456};
    assert(p.vkCmdPushConstants(cmd, {}, VK_SHADER_STAGE_FRAGMENT_BIT, 4,
                                sizeof values, values));
    values[0] = 0;
    VkClearColorValue color{};
    color.float32[0] = .5f;
    VkImageSubresourceRange range{};
    range.baseMipLevel = 2;
    assert(p.vkCmdClearColorImage(cmd, {}, VK_IMAGE_LAYOUT_GENERAL, &color, 1,
                                  &range));
    color.float32[0] = 0;
    VkRenderingAttachmentInfo attachment{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = handle<VkImageView>(5);
    VkRenderingAttachmentInfo ds{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ds.clearValue.depthStencil.stencil = 7;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &attachment;
    info.pDepthAttachment = info.pStencilAttachment = &ds;
    assert(p.vkCmdBeginRendering(cmd, &info));
    attachment.imageView = {};
    ds.clearValue.depthStencil.stencil = 0;
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    buffer.offset = 128;
    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image.subresourceRange.baseMipLevel = 2;
    assert(p.vkCmdPipelineBarrier(cmd, 1, 2, 0, 1, &memory, 1, &buffer, 1,
                                  &image));
    buffer.offset = 0;
    VkClearValue cv{};
    cv.color.uint32[0] = 99;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.clearValueCount = 1;
    rp.pClearValues = &cv;
    assert(p.vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE));
    cv.color.uint32[0] = 0;
    assert(p.vkCmdEndRenderPass(cmd));
  }
  assert(
      p.beginCommandBuffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
  assert(p.submit({}, {}, cmd, handle<VkFence>(6), handle<VkSemaphore>(7),
                  handle<VkSemaphore>(8), handle<VkSemaphore>(9),
                  handle<VkSemaphore>(10)));
  auto bytes = p.snapshot();
  assert(p.command_count() == 12);
  p.clear();
  assert(p.empty());
  PodCommands::replay(bytes);
  assert((calls ==
          std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}));
  // A malformed late command cannot emit even the valid prefix.
  auto bad = bytes;
  bad.push_back(std::byte{0});
  malformed(bad);
  bad = bytes;
  uint32_t unknown = UINT32_MAX;
  std::memcpy(bad.data(), &unknown, 4);
  malformed(bad);
  bad = bytes;
  bad.pop_back();
  malformed(bad);
  PodCommands bounded({1, 1024});
  assert(bounded.vkCmdDraw(cmd, 3, 1, 2, 0));
  auto before = bounded.snapshot();
  assert(!bounded.vkCmdDraw(cmd, 3, 1, 2, 0));
  assert(bounded.snapshot() == before);
  assert(bounded.take() == before && bounded.empty() &&
         bounded.command_count() == 0);
  PodCommands tiny({10, 1});
  assert(!tiny.vkCmdDraw(cmd, 3, 1, 2, 0) && tiny.empty());
  PodCommands reject;
  assert(reject.vkCmdDraw(cmd, 3, 1, 2, 0));
  before = reject.snapshot();
  VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
  info.colorAttachmentCount = 1;
  assert(!reject.vkCmdBeginRendering(cmd, &info));
  VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  info.pColorAttachments = &a;
  a.pNext = &a;
  assert(!reject.vkCmdBeginRendering(cmd, &info));
  a.pNext = nullptr;
  info.pNext = &a;
  assert(!reject.vkCmdBeginRendering(cmd, &info));
  info.pNext = nullptr;
  info.colorAttachmentCount = UINT32_MAX;
  assert(!reject.vkCmdBeginRendering(cmd, &info));
  VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  m.pNext = &m;
  assert(!reject.vkCmdPipelineBarrier(cmd, 1, 2, 0, 1, &m, 0, nullptr, 0,
                                      nullptr));
  m.pNext = nullptr;
  assert(!reject.vkCmdPipelineBarrier(cmd, 1, 2, 0, UINT32_MAX, &m, 0, nullptr,
                                      0, nullptr));
  assert(!reject.vkCmdBindVertexBuffers(cmd, 0, 1, nullptr, nullptr));
  assert(!reject.vkCmdSetViewport(cmd, 0, UINT32_MAX, nullptr));
  assert(!reject.vkCmdPushConstants(cmd, {}, 0, 0, 4, nullptr));
  assert(reject.snapshot() == before);
  // Inaccessible nested inputs prove budget rejection occurs before chain
  // reads.
#if defined(__APPLE__) || defined(__linux__)
  const size_t pageSize = size_t(sysconf(_SC_PAGESIZE));
  void *inaccessible =
      mmap(nullptr, pageSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  assert(inaccessible != MAP_FAILED);
  const auto *unreadMemory = static_cast<const VkMemoryBarrier *>(inaccessible);
  const auto *unreadBuffer =
      static_cast<const VkBufferMemoryBarrier *>(inaccessible);
  const auto *unreadImage =
      static_cast<const VkImageMemoryBarrier *>(inaccessible);
  const auto *unreadAttachment =
      static_cast<const VkRenderingAttachmentInfo *>(inaccessible);
  constexpr size_t headerBytes = 2 * sizeof(uint32_t);
  const size_t barrierPacket =
      headerBytes + sizeof(cmd) + 2 * sizeof(VkPipelineStageFlags) +
      sizeof(VkDependencyFlags) + 3 * sizeof(uint32_t) +
      sizeof(VkMemoryBarrier) + sizeof(VkBufferMemoryBarrier) +
      sizeof(VkImageMemoryBarrier);
  VkMemoryBarrier validMemory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  VkBufferMemoryBarrier validBuffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  VkImageMemoryBarrier validImage{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  for (int position = 0; position < 3; ++position) {
    PodCommands combined({8, barrierPacket - 1});
    auto original = combined.snapshot();
    assert(!combined.vkCmdPipelineBarrier(
        cmd, 1, 2, 0, 1, position == 0 ? unreadMemory : &validMemory, 1,
        position == 1 ? unreadBuffer : &validBuffer, 1,
        position == 2 ? unreadImage : &validImage));
    assert(combined.snapshot() == original && combined.command_count() == 0);
  }
  // Individually valid packet also rejects when the existing stream consumes
  // space.
  PodCommands sample;
  assert(sample.vkCmdDraw(cmd, 3, 1, 2, 0));
  PodCommands occupied({8, barrierPacket + sample.payload_bytes() - 1});
  assert(occupied.vkCmdDraw(cmd, 3, 1, 2, 0));
  auto occupiedBefore = occupied.snapshot();
  assert(!occupied.vkCmdPipelineBarrier(cmd, 1, 2, 0, 1, unreadMemory, 1,
                                        unreadBuffer, 1, unreadImage));
  assert(occupied.snapshot() == occupiedBefore &&
         occupied.command_count() == 1);
  const size_t renderingPacket = headerBytes + sizeof(cmd) +
                                 sizeof(VkRenderingInfo) + sizeof(uint32_t) +
                                 3 * sizeof(VkRenderingAttachmentInfo);
  VkRenderingAttachmentInfo validAttachment{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  for (int position = 0; position < 3; ++position) {
    PodCommands combined({8, renderingPacket - 1});
    VkRenderingInfo value{VK_STRUCTURE_TYPE_RENDERING_INFO};
    value.colorAttachmentCount = 1;
    value.pColorAttachments =
        position == 0 ? unreadAttachment : &validAttachment;
    value.pDepthAttachment =
        position == 1 ? unreadAttachment : &validAttachment;
    value.pStencilAttachment =
        position == 2 ? unreadAttachment : &validAttachment;
    assert(!combined.vkCmdBeginRendering(cmd, &value) && combined.empty());
  }
  PodCommands commandFull({0, 4096});
  assert(!commandFull.vkCmdBeginRendering(
      cmd, static_cast<const VkRenderingInfo *>(inaccessible)));
  assert(!commandFull.vkCmdPipelineBarrier(cmd, 1, 2, 0, 1, unreadMemory, 1,
                                           unreadBuffer, 1, unreadImage));
  PodCommands overflow;
  assert(!overflow.vkCmdPipelineBarrier(cmd, 1, 2, 0, UINT32_MAX, unreadMemory,
                                        UINT32_MAX, unreadBuffer, UINT32_MAX,
                                        unreadImage));
  assert(overflow.empty());
  assert(munmap(inaccessible, pageSize) == 0);
#endif
  variablePayloadAndLateChainRegressions();
  PodCommands failing;
  assert(failing.submit({}, {}, cmd, handle<VkFence>(6)));
  vkEndCommandBuffer = failedEnd;
  calls.clear();
  bool failed = false;
  try {
    PodCommands::replay(failing.take());
  } catch (const std::runtime_error &) {
    failed = true;
  }
  assert(failed && calls.empty());
  if (handoff_trace::enabled()) assert(handoff_trace::flush());
}
