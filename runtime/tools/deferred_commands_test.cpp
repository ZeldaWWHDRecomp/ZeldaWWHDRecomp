// Synthetic Vulkan handles and API callbacks only; never loads a driver or game.
#include "gfx/vulkan/deferred_commands.h"
#include "gfx/vulkan/pod_commands.h"
#include "gfx/vulkan/deferred_dispatch.h"
#include "gfx/vulkan/pipeline_failure.h"
#include <cassert>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <atomic>
#include <cstdlib>
#include <new>
#include <memory>
#include <mutex>

static std::atomic<bool> failNextAllocation{false};
void* operator new(std::size_t size) {
    if(failNextAllocation.exchange(false))throw std::bad_alloc{};
    if(void* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }

namespace gfxvk {
#define DEFINE(name) PFN_##name name=nullptr;
WWHD_VK_DEVICE_FUNCTIONS(DEFINE)
WWHD_VK_RENDERING_FUNCTIONS(DEFINE)
#undef DEFINE
}
namespace {
std::vector<int> calls;
template<class T> T handle(uintptr_t value) {
    if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(value);
    else return static_cast<T>(value);
}
const auto cmd=handle<VkCommandBuffer>(1);
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer c,uint32_t n,uint32_t instances,uint32_t first,uint32_t firstInstance) {
    assert(c==cmd&&n==3&&instances==1&&first==0&&firstInstance==0);calls.push_back(1);
}
VKAPI_ATTR void VKAPI_CALL bind(VkCommandBuffer c,VkPipelineBindPoint point,VkPipeline pipeline) {
    assert(c==cmd&&point==VK_PIPELINE_BIND_POINT_GRAPHICS&&pipeline==handle<VkPipeline>(2));calls.push_back(2);
}
VKAPI_ATTR void VKAPI_CALL rendering(VkCommandBuffer c,const VkRenderingInfo* info) {
    assert(c==cmd&&info&&!info->pNext&&info->colorAttachmentCount==2);
    assert(info->renderArea.extent.width==640&&info->layerCount==1);
    assert(info->pColorAttachments[0].imageView==handle<VkImageView>(3));
    assert(info->pColorAttachments[1].clearValue.color.float32[0]==0.5f);
    assert(info->pDepthAttachment&&info->pStencilAttachment);
    assert(info->pDepthAttachment->clearValue.depthStencil.depth==0.25f);
    assert(info->pStencilAttachment->clearValue.depthStencil.stencil==7);calls.push_back(3);
}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer c,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags flags,
        uint32_t nm,const VkMemoryBarrier* m,uint32_t nb,const VkBufferMemoryBarrier* b,uint32_t ni,const VkImageMemoryBarrier* i) {
    assert(c==cmd&&source==VK_PIPELINE_STAGE_TRANSFER_BIT&&destination==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT&&!flags);
    assert(nm==1&&nb==1&&ni==1&&!m->pNext&&!b->pNext&&!i->pNext);
    assert(m->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&b->offset==128&&i->subresourceRange.baseMipLevel==2);calls.push_back(4);
}
VKAPI_ATTR void VKAPI_CALL vertices(VkCommandBuffer c,uint32_t first,uint32_t count,const VkBuffer* buffers,const VkDeviceSize* offsets) {
    assert(c==cmd&&first==1&&count==2&&buffers[1]==handle<VkBuffer>(5)&&offsets[1]==96);calls.push_back(5);
}
VKAPI_ATTR void VKAPI_CALL descriptors(VkCommandBuffer c,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,
        uint32_t count,const VkDescriptorSet* sets,uint32_t offsetCount,const uint32_t* offsets) {
    assert(c==cmd&&point==VK_PIPELINE_BIND_POINT_GRAPHICS&&layout==handle<VkPipelineLayout>(6)&&first==0);
    assert(count==1&&sets[0]==handle<VkDescriptorSet>(7)&&offsetCount==2&&offsets[0]==32&&offsets[1]==64);calls.push_back(6);
}
VKAPI_ATTR void VKAPI_CALL constants(VkCommandBuffer c,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void* values) {
    assert(c==cmd&&layout==handle<VkPipelineLayout>(6)&&stages==VK_SHADER_STAGE_FRAGMENT_BIT&&offset==4&&size==8);
    const auto* words=static_cast<const uint32_t*>(values);assert(words[0]==123&&words[1]==456);calls.push_back(7);
}
VKAPI_ATTR void VKAPI_CALL imageCopy(VkCommandBuffer c,VkImage source,VkImageLayout sl,VkImage destination,VkImageLayout dl,uint32_t count,const VkImageCopy* regions) {
    assert(c==cmd&&source==handle<VkImage>(8)&&destination==handle<VkImage>(9));
    assert(sl==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&dl==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&count==1&&regions->extent.width==16);calls.push_back(8);
}
VKAPI_ATTR void VKAPI_CALL unknownRendering(VkCommandBuffer,const VkRenderingInfo* info) { assert(info->pNext);calls.push_back(9); }
VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer,uint32_t,uint32_t count,const VkRect2D* values) { assert(count==1&&values->extent.width==42);calls.push_back(10); }
void initialize() {
    gfxvk::vkCmdDraw=draw;gfxvk::vkCmdBindPipeline=bind;gfxvk::vkCmdBeginRendering=rendering;
    gfxvk::vkCmdPipelineBarrier=barrier;gfxvk::vkCmdBindVertexBuffers=vertices;
    gfxvk::vkCmdBindDescriptorSets=descriptors;gfxvk::vkCmdPushConstants=constants;
    gfxvk::vkCmdCopyImage=imageCopy;gfxvk::vkCmdSetScissor=scissor;
}
void inline_dispatch() {
    // First operation in this process: no global recorder has been requested.
    assert(gfxvk::active_deferred_commands==nullptr);
    int immediate=0;
    failNextAllocation=true;
    gfxvk::record::emit_owned([&] { ++immediate; });
    assert(immediate==1&&failNextAllocation); // Exact helper allocates nothing.
    failNextAllocation=false;
    gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    assert((calls==std::vector<int>{1}));calls.clear();
    gfxvk::DeferredCommands recorder,conflict;
    recorder.start_capture();assert(gfxvk::active_deferred_commands==&recorder);
    {
        VkRect2D rect{};rect.extent.width=42;
        gfxvk::record::vkCmdSetScissor(cmd,0,1,&rect);rect.extent.width=0;
    }
    bool rejected=false;
    try { conflict.start_capture(); }catch(const std::logic_error&){rejected=true;}
    assert(rejected&&gfxvk::active_deferred_commands==&recorder&&recorder.command_count()==1&&calls.empty());
    recorder.emit_exact();assert((calls==std::vector<int>{10})&&gfxvk::active_deferred_commands==nullptr);calls.clear();
    gfxvk::record::vkCmdDraw(cmd,3,1,0,0);assert((calls==std::vector<int>{1}));calls.clear();
    { gfxvk::DeferredCommands temporary;temporary.start_capture();assert(gfxvk::active_deferred_commands==&temporary); }
    assert(gfxvk::active_deferred_commands==nullptr);
    recorder.start_capture();recorder.reset();assert(gfxvk::active_deferred_commands==nullptr);
}
void ownership_and_order() {
    gfxvk::DeferredCommands recorder;
    recorder.vkCmdDraw(cmd,3,1,0,0);assert(calls==std::vector<int>{1});calls.clear();
    recorder.start_capture();
    recorder.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
    {
        std::array<VkRenderingAttachmentInfo,2> colors{};
        for(auto& c:colors)c.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colors[0].imageView=handle<VkImageView>(3);colors[1].clearValue.color.float32[0]=0.5f;
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};depth.clearValue.depthStencil={0.25f,7};
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea.extent={640,480};info.layerCount=1;
        info.colorAttachmentCount=2;info.pColorAttachments=colors.data();info.pDepthAttachment=&depth;info.pStencilAttachment=&depth;
        recorder.vkCmdBeginRendering(cmd,&info);
        info.renderArea.extent.width=1;colors[0].imageView=VK_NULL_HANDLE;depth.clearValue.depthStencil={1,0};
        VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};m.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.offset=128;
        VkImageMemoryBarrier i{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};i.subresourceRange.baseMipLevel=2;
        recorder.vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&m,1,&b,1,&i);
        m.srcAccessMask=0;b.offset=0;i.subresourceRange.baseMipLevel=0;
        VkBuffer buffers[]={handle<VkBuffer>(4),handle<VkBuffer>(5)};VkDeviceSize offsets[]={0,96};
        recorder.vkCmdBindVertexBuffers(cmd,1,2,buffers,offsets);buffers[1]=VK_NULL_HANDLE;offsets[1]=0;
        VkDescriptorSet sets[]={handle<VkDescriptorSet>(7)};uint32_t dynamic[]={32,64};
        recorder.vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipelineLayout>(6),0,1,sets,2,dynamic);
        sets[0]=VK_NULL_HANDLE;dynamic[0]=0;
        uint32_t values[]={123,456};recorder.vkCmdPushConstants(cmd,handle<VkPipelineLayout>(6),VK_SHADER_STAGE_FRAGMENT_BIT,4,sizeof values,values);values[0]=0;
        VkImageCopy copy{};copy.extent.width=16;
        recorder.vkCmdCopyImage(cmd,handle<VkImage>(8),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,handle<VkImage>(9),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);copy.extent.width=0;
    }
    recorder.vkCmdDraw(cmd,3,1,0,0);
    assert(calls.empty()&&recorder.command_count()==8&&recorder.payload_bytes()>0);
    bool refused=false;try { recorder.reset(); }catch(const std::logic_error&){refused=true;}assert(refused);
    recorder.emit_exact();assert((calls==std::vector<int>{2,3,4,5,6,7,8,1}));
    assert(!recorder.capturing()&&!recorder.command_count()&&!recorder.payload_bytes());calls.clear();recorder.reset();
}
void fallback_and_bounds() {
    gfxvk::DeferredCommands recorder({1,1024});
    recorder.start_capture([] { calls.push_back(11); });
    recorder.vkCmdDraw(cmd,3,1,0,0);
    recorder.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
    assert((calls==std::vector<int>{11,1,2})&&!recorder.capturing()&&gfxvk::active_deferred_commands==nullptr);calls.clear();
    gfxvk::DeferredCommands small({8,1});small.start_capture([] { calls.push_back(11); });
    small.vkCmdDraw(cmd,3,1,0,0);
    VkRect2D rect{};rect.extent.width=42;small.vkCmdSetScissor(cmd,0,1,&rect);
    assert((calls==std::vector<int>{11,1,10})&&!small.capturing());calls.clear();
    gfxvk::vkCmdBeginRendering=unknownRendering;
    gfxvk::DeferredCommands unknown;
    unknown.start_capture([] { calls.push_back(11); });unknown.vkCmdDraw(cmd,3,1,0,0);
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};VkBaseInStructure extension{};info.pNext=&extension;
    unknown.vkCmdBeginRendering(cmd,&info);
    assert((calls==std::vector<int>{11,1,9})&&!unknown.capturing());calls.clear();gfxvk::vkCmdBeginRendering=rendering;
    bool rejected=false;try { recorder.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,VK_NULL_HANDLE); }catch(const std::invalid_argument&){rejected=true;}
    assert(rejected&&calls.empty());
}
void allocation_failure_falls_back() {
    gfxvk::DeferredCommands recorder;
    recorder.start_capture([] { calls.push_back(11); });recorder.vkCmdDraw(cmd,3,1,0,0);
    VkRect2D rect{};rect.extent.width=42;
    failNextAllocation=true;
    recorder.vkCmdSetScissor(cmd,0,1,&rect);
    assert(!failNextAllocation&& !recorder.capturing());
    assert((calls==std::vector<int>{11,1,10}));calls.clear();
}
void callback_failure_preserves_tail() {
    gfxvk::DeferredCommands recorder({1,1});
    recorder.start_capture([] { throw std::runtime_error("resolve failed"); });recorder.vkCmdDraw(cmd,3,1,0,0);
    bool failed=false;try { recorder.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2)); }catch(const std::runtime_error&){failed=true;}
    assert(failed&&calls.empty()&&recorder.command_count()==1&&gfxvk::active_deferred_commands==nullptr);
    recorder.emit_exact();assert((calls==std::vector<int>{1}));calls.clear();
    std::mutex execution;
    gfxvk::DeferredCommands* main=nullptr;
    { std::lock_guard lock(execution); main=&gfxvk::deferred_commands(); }
    std::thread other([&] {
      std::lock_guard lock(execution);
      assert(&gfxvk::deferred_commands()==main);
    });other.join();
}
void owned_submission_boundaries() {
    gfxvk::DeferredCommands recorder;
    bool retained=false,gpuPending=false;
    std::vector<int> live{17},sealed;
    recorder.start_capture([&] { assert(retained&&!gpuPending&&sealed[0]==17);calls.push_back(11); });
    recorder.vkCmdDraw(cmd,3,1,0,0);
    recorder.owned_boundary([&]() noexcept {
        sealed=std::move(live);retained=true;
    },[&] {
        assert(retained&&!gpuPending&&sealed[0]==17);
        calls.push_back(12);retained=false;gpuPending=true;
    });
    assert(retained&&!gpuPending&&live.empty()&&calls.empty());
    recorder.finish_exact();
    assert(!retained&&gpuPending&&(calls==std::vector<int>{11,1,12}));calls.clear();
    // Reserve/cap fallback resolves the old prefix before sealing the next slot.
    gfxvk::DeferredCommands bounded({1,1024});
    retained=false;gpuPending=false;
    bounded.start_capture([&] { assert(!retained);calls.push_back(11); });
    bounded.vkCmdDraw(cmd,3,1,0,0);
    bounded.owned_boundary([&]() noexcept { retained=true; },[&] {
        assert(retained);retained=false;gpuPending=true;calls.push_back(12);
    });
    assert(!retained&&gpuPending&&(calls==std::vector<int>{11,1,12}));calls.clear();
    // Failure preparing owned storage cannot expose a half-sealed submission.
    gfxvk::DeferredCommands allocation;
    allocation.start_capture([&] { assert(!retained);calls.push_back(11); });
    allocation.vkCmdDraw(cmd,3,1,0,0);
    failNextAllocation=true;
    allocation.owned_boundary([&]() noexcept { retained=true; },[&] {
        assert(retained);retained=false;calls.push_back(12);
    });
    assert(!failNextAllocation&&!retained&&(calls==std::vector<int>{11,1,12}));calls.clear();
    // Resolver failure leaves both the prefix and the next unsealed resources intact.
    gfxvk::DeferredCommands failed({1,1024});
    failed.start_capture([] { throw std::runtime_error("resolution failed"); });
    failed.vkCmdDraw(cmd,3,1,0,0);
    bool thrown=false;
    try { failed.owned_boundary([&]() noexcept { retained=true; },[] { assert(false); }); }
    catch(const std::runtime_error&) { thrown=true; }
    assert(thrown&&!retained&&failed.command_count()==1&&calls.empty());
    failed.emit_exact();assert((calls==std::vector<int>{1}));calls.clear();
}

void owned_pending_emission() {
    // Worker-result ownership outlives the caller. Exact emission resolves the
    // pending bind before its draw, and neither command may execute twice.
    struct Pending { bool ready=false; };
    gfxvk::DeferredCommands recorder;
    recorder.start_capture();
    auto pending=std::make_shared<Pending>();
    std::weak_ptr<Pending> lifetime=pending;
    gfxvk::record::emit_owned([pending] {
        pending->ready=true;
        gfxvk::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
    });
    gfxvk::record::emit_owned([pending] {
        assert(pending->ready);
        gfxvk::vkCmdDraw(cmd,3,1,0,0);
    });
    pending.reset();assert(!lifetime.expired()&&calls.empty());
    recorder.emit_exact();
    assert((calls==std::vector<int>{2,1})&&lifetime.expired());calls.clear();
    recorder.emit_exact();assert(calls.empty());
}

void cross_thread_checkpoint_and_isolated_dispatch() {
    std::mutex execution;
    gfxvk::DeferredCommands recorder;
    {
      std::lock_guard lock(execution);
      recorder.start_capture();
      gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    }
    // Independent presentation pool must never capture into the guest tail.
    std::thread presentation([&] {
      gfxvk::IsolatedCommandDispatch isolated;
      assert(gfxvk::dispatch_recorder()==nullptr);
      gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    });
    presentation.join();assert(calls==std::vector<int>{1});calls.clear();
    // Models SDL checkpoint/quit after acquiring the GX2 execution mutex.
    std::thread checkpoint([&] {
      std::lock_guard lock(execution);
      assert(gfxvk::active_deferred_commands==&recorder);
      gfxvk::active_deferred_commands->finish_exact();
      assert(calls==std::vector<int>{1});
      calls.push_back(99); // ending/submitting the native buffer follows its tail
    });
    checkpoint.join();assert((calls==std::vector<int>{1,99}));calls.clear();
    assert(!recorder.capturing()&&gfxvk::active_deferred_commands==nullptr);
    // A failed native pipeline is terminal: binding/draw are both suppressed,
    // while subsequent successful prepared commands still emit once.
    assert(gfxvk::recoverable_pipeline_failure(VK_ERROR_UNKNOWN));
    assert(gfxvk::recoverable_pipeline_failure(VK_ERROR_INVALID_SHADER_NV));
    assert(!gfxvk::recoverable_pipeline_failure(VK_SUCCESS));
    assert(!gfxvk::recoverable_pipeline_failure(VK_ERROR_OUT_OF_HOST_MEMORY));
    assert(!gfxvk::recoverable_pipeline_failure(VK_ERROR_OUT_OF_DEVICE_MEMORY));
    assert(!gfxvk::recoverable_pipeline_failure(VK_ERROR_DEVICE_LOST));
    recorder.start_capture();bool pipelineReady=false;
    gfxvk::record::emit_owned([&] { pipelineReady=false; });
    gfxvk::record::emit_owned([&] { if(pipelineReady)gfxvk::vkCmdDraw(cmd,3,1,0,0); });
    gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    recorder.finish_exact();assert(calls==std::vector<int>{1});calls.clear();
}

void stereo_eye_stream() {
    // Stereo (gfx/vulkan/stereo.cpp): eye 1's commands are held in a detached recorder, then
    // replayed in order through the renderer's own recorder at a splice point.
    gfxvk::DeferredCommands renderer,eye;
    renderer.start_capture();
    eye.start_detached_capture([] { throw std::logic_error("held out of order"); });
    assert(gfxvk::active_deferred_commands==&renderer&&eye.capturing());
    gfxvk::record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
    gfxvk::eye_deferred_commands=&eye;
    gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    {
        gfxvk::IsolatedCommandDispatch isolated;
        assert(gfxvk::dispatch_recorder()==nullptr);
    }
    assert(renderer.command_count()==1&&eye.command_count()==1&&calls.empty());
    gfxvk::eye_deferred_commands=nullptr;
    auto held=eye.take();
    assert(held.size()==1&&!eye.command_count()&&eye.capturing()&&gfxvk::active_deferred_commands==&renderer);
    for(auto& command:held)gfxvk::record::emit_owned(std::move(command));
    assert(renderer.command_count()==2&&calls.empty());
    // A nested record:: call during the renderer's emission reaches the API even while the
    // eye stream is attached (a deferred pipeline bind).
    gfxvk::record::emit_owned([] { gfxvk::record::vkCmdDraw(cmd,3,1,0,0); });
    gfxvk::eye_deferred_commands=&eye;
    renderer.emit_exact();
    assert((calls==std::vector<int>{2,1,1})&&!eye.command_count()&&gfxvk::eye_deferred_commands==&eye);calls.clear();
    gfxvk::eye_deferred_commands=nullptr;
    // A detached tail cannot be emitted in place of the commands recorded before it.
    eye.vkCmdDraw(cmd,3,1,0,0);
    bool refused=false;
    try { eye.finish_exact(); }catch(const std::logic_error&){refused=true;}
    assert(refused&&calls.empty()&&eye.command_count()==1);
    eye.take();
}

void guest_thread_exit_retains_global_recorder() {
    std::mutex execution;
    gfxvk::DeferredCommands* started=nullptr;
    std::thread guest([&] {
      std::lock_guard lock(execution);
      auto& recorder=gfxvk::deferred_commands();started=&recorder;
      recorder.start_capture();gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    });
    guest.join(); // A TLS recorder used to destroy and silently lose this tail.
    std::lock_guard lock(execution);
    assert(gfxvk::active_deferred_commands==started);
    assert(&gfxvk::deferred_commands()==started&&calls.empty());
    gfxvk::deferred_commands().finish_exact();
    assert(calls==std::vector<int>{1});calls.clear();
}

void stereo_pod_tail_ownership() {
    gfxvk::PodCommands bytes;
    gfxvk::DeferredCommands eye;
    eye.hold_in(&bytes);
    eye.start_detached_capture([] { throw std::logic_error("held out of order"); });
    eye.vkCmdDraw(cmd,3,1,0,0);
    assert(eye.command_count()==1 && calls.empty());
    bool refused=false;
    try { eye.reset(); } catch(const std::logic_error&) { refused=true; }
    assert(refused && eye.capturing() && eye.command_count()==1);
    // Owned callbacks split byte runs, preserving order and argument ownership.
    eye.emit_owned([] { calls.push_back(99); });
    eye.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
    auto tail=eye.take();
    assert(tail.size()==3 && eye.command_count()==0 && calls.empty());
    for(auto& command:tail) command();
    assert((calls==std::vector<int>{1,99,2})); calls.clear();
    eye.reset();
    eye.start_detached_capture({});
    eye.vkCmdDraw(cmd,3,1,0,0);
    eye.emit_exact();
    assert(calls==std::vector<int>{1} && eye.command_count()==0); calls.clear();
    eye.reset();
}
void draw_batch_flush_order() {
    gfxvk::DeferredCommands renderer;
    renderer.start_capture();
    gfxvk::before_record_command = [] {
        gfxvk::before_record_command = nullptr;
        gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
    };
    // A separate WSI/recording thread cannot flush a producer's batch.
    std::thread([] { assert(gfxvk::before_record_command == nullptr); }).join();
    {
        gfxvk::IsolatedCommandDispatch isolated;
        assert(gfxvk::dispatch_recorder() == nullptr);
        assert(gfxvk::before_record_command != nullptr);
    }
    gfxvk::record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
    assert(gfxvk::before_record_command == nullptr);
    assert(renderer.command_count() == 2 && calls.empty());
    renderer.finish_exact();
    assert((calls == std::vector<int>{1,2}));
    calls.clear();
}

void pending_pipeline_precedes_unrecorded_batch() {
    for (bool direct : {false, true}) {
        gfxvk::DeferredCommands renderer;
        renderer.start_capture();
        // A pending pipeline binds during replay. A later producer batch must
        // be appended to the captured tail before replay starts, never flushed
        // by that older bind's nested record:: call.
        renderer.emit_owned([] {
            gfxvk::record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipeline>(2));
        });
        renderer.vkCmdDraw(cmd,3,1,0,0);
        gfxvk::before_record_command = [] {
            gfxvk::before_record_command = nullptr;
            gfxvk::record::vkCmdDraw(cmd,3,1,0,0);
        };
        if (direct) renderer.emit_exact(); else renderer.finish_exact();
        assert((calls == std::vector<int>{2,1,1}));
        assert(gfxvk::before_record_command == nullptr);
        calls.clear();
    }
}

}
int main() { initialize();inline_dispatch();ownership_and_order();fallback_and_bounds();allocation_failure_falls_back();callback_failure_preserves_tail();owned_submission_boundaries();owned_pending_emission();cross_thread_checkpoint_and_isolated_dispatch();stereo_eye_stream();guest_thread_exit_retains_global_recorder();stereo_pod_tail_ownership();draw_batch_flush_order();pending_pipeline_precedes_unrecorded_batch(); }
