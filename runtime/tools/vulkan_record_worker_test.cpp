// Synthetic entry points only: no Vulkan device, game, guest bytes or GPU.
#include "gfx/vulkan/record_worker.h"
#include "gfx/vulkan/submission_cpu_ack.h"
#include "gfx/vulkan/deferred_dispatch.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <thread>
#include <type_traits>
namespace gfxvk {
#define DEFINE(name) PFN_##name name=nullptr;
WWHD_VK_DEVICE_FUNCTIONS(DEFINE)
WWHD_VK_RENDERING_FUNCTIONS(DEFINE)
#undef DEFINE
}
namespace {
std::mutex mutex;
std::condition_variable changed;
std::vector<int> calls;
std::vector<std::thread::id> threads;
bool blockSubmit=false, submitEntered=false, releaseSubmit=false, failEnd=false;
bool blockDraw=false, drawEntered=false, releaseDraw=false;
template<class T> T handle(uintptr_t n) {
    if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(n);
    else return static_cast<T>(n);
}
void note(int value) {
    std::lock_guard lock(mutex);calls.push_back(value);threads.push_back(std::this_thread::get_id());
}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer,uint32_t n,uint32_t,uint32_t,uint32_t) {
    note(int(n));
    if (n==44) {
        std::unique_lock lock(mutex);
        if(blockDraw) { drawEntered=true;changed.notify_all();changed.wait(lock,[]{return releaseDraw;}); }
    }
}
VKAPI_ATTR void VKAPI_CALL beginRendering(VkCommandBuffer,const VkRenderingInfo* info) {
    assert(info && info->sType==VK_STRUCTURE_TYPE_RENDERING_INFO);note(70);
}
VKAPI_ATTR void VKAPI_CALL beginPass(VkCommandBuffer,const VkRenderPassBeginInfo* info,VkSubpassContents) {
    assert(info && info->clearValueCount==1 && info->pClearValues[0].color.uint32[0]==91);note(71);
}
VKAPI_ATTR void VKAPI_CALL endPass(VkCommandBuffer) { note(72); }
VKAPI_ATTR void VKAPI_CALL endRendering(VkCommandBuffer) { note(73); }
VKAPI_ATTR void VKAPI_CALL pushConstants(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,
    uint32_t offset,uint32_t size,const void* values) {
    uint32_t first=0;std::memcpy(&first,values,sizeof(first));
    assert(offset==0 && size==128 && first==93);note(74);
}
VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo* info) {
    assert(info->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);note(80);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer) { note(81);return failEnd?VK_ERROR_DEVICE_LOST:VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL reset(VkDevice,uint32_t n,const VkFence* fence) {
    assert(n==1 && (*fence==handle<VkFence>(4) || *fence==handle<VkFence>(5)));note(82);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue,uint32_t n,const VkSubmitInfo* info,VkFence fence) {
    assert(n==1 && (fence==handle<VkFence>(4) || fence==handle<VkFence>(5)));assert(info->commandBufferCount==1);
    note(83);
    std::unique_lock lock(mutex);submitEntered=true;changed.notify_all();
    if(blockSubmit)changed.wait(lock,[]{return releaseSubmit;});
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL oldSignaledFence(VkDevice,VkFence) { note(85);return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue,const VkPresentInfoKHR*) { note(84);return VK_SUCCESS; }
void setup() {
    gfxvk::vkCmdDraw=draw;gfxvk::vkCmdBeginRendering=beginRendering;
    gfxvk::vkCmdEndRendering=endRendering;gfxvk::vkCmdPushConstants=pushConstants;
    gfxvk::vkCmdBeginRenderPass=beginPass;gfxvk::vkCmdEndRenderPass=endPass;
    gfxvk::vkBeginCommandBuffer=begin;gfxvk::vkEndCommandBuffer=end;
    gfxvk::vkResetFences=reset;gfxvk::vkQueueSubmit=submit;
    gfxvk::vkGetFenceStatus=oldSignaledFence;gfxvk::vkQueuePresentKHR=present;
}
}
int main(int argc,char** argv) {
    const bool off=argc>1 && !std::strcmp(argv[1],"off");
    const bool publication32=argc>1 && !std::strcmp(argv[1],"publication32");
    const bool noBoost=argc>1 && !std::strcmp(argv[1],"no_boost");
    auto option=[](const char* key,const char* value) {
#ifdef _WIN32
        assert(_putenv_s(key,value)==0);
#else
        assert(setenv(key,value,1)==0);
#endif
    };
    option("WWHD_R5_NO_RECORD_BOOST",noBoost?"1":"0");
    option("WWHD_R5_SLOW_RENDER","0");
#ifdef _WIN32
    _putenv_s("WWHD_VK_RECORD_WORKER",off?"0":"1");
#else
    setenv("WWHD_VK_RECORD_WORKER",off?"0":"1",1);
#endif
    using namespace gfxvk;
    setup();const auto cmd=handle<VkCommandBuffer>(1);const auto producer=std::this_thread::get_id();
    if(off) {
        const auto statsPath=std::filesystem::temp_directory_path()/ ("wwhd-worker-off-stats-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
        assert(!std::filesystem::exists(statsPath));
        option("WWHD_VK_FALLBACK_STATS",statsPath.string().c_str());
        start_record_worker();assert(!record_worker_active() && !active_deferred_commands);
        assert(!fallback_stats::initialized && !fallback_stats::storage);
        record::vkCmdDraw(cmd,3,1,0,0);assert(calls==std::vector<int>{3} && threads[0]==producer);
        stop_record_worker();assert(!fallback_stats::storage && !std::filesystem::exists(statsPath));return 0;
    }
    if(publication32) {
        // Small draws and BOTH pass ends must remain owned, not auto-published.
        // The fixed production policy has no 64-draw or pass-end trigger.
        start_record_worker();assert(record_worker_active());
        for(unsigned i=0;i<64;++i)record::vkCmdDraw(cmd,46,1,0,0);
        assert(!record_worker_commands()->empty());
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        record::vkCmdBeginRendering(cmd,&rendering);record::vkCmdDraw(cmd,48,1,0,0);
        record::vkCmdEndRendering(cmd);assert(!record_worker_commands()->empty());
        VkClearValue clear{};clear.color.uint32[0]=91;
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.clearValueCount=1;pass.pClearValues=&clear;
        record::vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
        record::vkCmdDraw(cmd,49,1,0,0);record::vkCmdEndRenderPass(cmd);
        assert(!record_worker_commands()->empty());
        {std::lock_guard lock(mutex);assert(calls.empty());}
        // Cross 32KiB using immutable copied constants. Merely appending them
        // must not publish; the next completed draw is the ownership boundary.
        std::array<uint32_t,32> constants;constants.fill(93);unsigned copies=0;
        while(record_worker_commands()->payload_bytes()<32768) {
            assert(copies<512);
            record::vkCmdPushConstants(cmd,handle<VkPipelineLayout>(7),VK_SHADER_STAGE_VERTEX_BIT,
                0,uint32_t(sizeof(constants)),constants.data());++copies;
        }
        constants.fill(94);
        assert(!record_worker_commands()->empty());
        {std::lock_guard lock(mutex);assert(calls.empty());}
        record::vkCmdDraw(cmd,41,1,0,0);assert(record_worker_commands()->empty());
        drain_record_commands();stop_record_worker();
        std::vector<int> expected(64,46);
        expected.insert(expected.end(),{70,48,73,71,49,72});
        expected.insert(expected.end(),copies,74);expected.push_back(41);
        assert(calls==expected);
        for(auto thread:threads)assert(thread!=producer);
        return 0;
    }
    // If facade installation fails after constructing a candidate worker, its
    // RAII owner must join that idle thread and retain the existing recorder.
    {
        DeferredCommands existing;existing.start_capture();
        bool failed=false;try{start_record_worker();}catch(const std::logic_error&){failed=true;}
        assert(failed && !record_worker_active() && active_deferred_commands==&existing);
        existing.finish_exact();
    }
    // Explicit CPU acknowledgment must wait for the worker's actual QueueSubmit,
    // not for an old signaled GPU fence belonging to the reused slot.
    {
        RecordWorker worker;PodCommands commands;
        assert(commands.beginCommandBuffer(cmd,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
        assert(commands.vkCmdDraw(cmd,10,1,0,0));
        assert(commands.submit(handle<VkDevice>(2),handle<VkQueue>(3),cmd,handle<VkFence>(4)));
        blockSubmit=true;const auto serial=worker.publish(commands);assert(commands.empty());
        {std::unique_lock lock(mutex);changed.wait(lock,[]{return submitEntered;});}
        // A second independent slot starts with an old signaled fence too.
        // Waiting for its exact serial must include BOTH queued submissions.
        PodCommands second;const auto secondCmd=handle<VkCommandBuffer>(6);
        assert(second.beginCommandBuffer(secondCmd,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
        assert(second.vkCmdDraw(secondCmd,11,1,0,0));
        assert(second.submit(handle<VkDevice>(2),handle<VkQueue>(3),secondCmd,handle<VkFence>(5)));
        const auto secondSerial=worker.publish(second);assert(secondSerial>serial);
        // Nonblocking DRC/screenshot polling must not inspect either old fence.
        assert(!worker.emitted(serial) && !worker.emitted(secondSerial));
        std::atomic<bool> acknowledged=false,waiterReady=false;
        std::thread waiter([&]{
            uint64_t retainedSerial=secondSerial;
            waiterReady=true;
            acknowledge_submission_cpu(retainedSerial,[&](uint64_t exact) {
                assert(exact==secondSerial);worker.wait(exact);
            });
            assert(retainedSerial==0);
            assert(vkGetFenceStatus(handle<VkDevice>(2),handle<VkFence>(5))==VK_SUCCESS);
            VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            assert(vkQueuePresentKHR(handle<VkQueue>(3),&info)==VK_SUCCESS);
            acknowledged=true;
        });
        while(!waiterReady.load())std::this_thread::yield();
        assert(!acknowledged.load());
        {std::lock_guard lock(mutex);
         assert(std::find(calls.begin(),calls.end(),85)==calls.end());
         assert(std::find(calls.begin(),calls.end(),84)==calls.end());}
        {std::lock_guard lock(mutex);releaseSubmit=true;changed.notify_all();}
        waiter.join();assert(acknowledged.load());
        assert(worker.emitted(serial) && worker.emitted(secondSerial));worker.stop();
        assert((calls==std::vector<int>{80,10,81,82,83,80,11,81,82,83,85,84}));
        for(size_t i=0;i<10;++i)assert(threads[i]!=producer && threads[i]==threads[0]);
        assert(threads[10]!=threads[0] && threads[11]==threads[10]);
    }
    // Failed acknowledgement retains the authoritative serial. Empty slots
    // never invoke the wait callback or issue an unrelated fence operation.
    {
        uint64_t serial=999;bool threw=false;
        try { acknowledge_submission_cpu(serial,[](uint64_t exact) {
            assert(exact==999);throw std::runtime_error("synthetic ack failure");
        }); } catch(const std::runtime_error&) { threw=true; }
        assert(threw && serial==999);
        serial=0;bool called=false;
        acknowledge_submission_cpu(serial,[&](uint64_t) { called=true; });
        assert(!called && serial==0);
    }
    calls.clear();threads.clear();blockSubmit=false;
    // Stereo publishes a detached eye behind the producer prefix, with no
    // synchronous drain. Block eye 0 to prove publication can return before
    // replay, then verify both eyes and the following draw use one worker.
    start_record_worker();
    {
        DeferredCommands eye;
        PodCommands eyeBytes;
        eye.hold_in(&eyeBytes);
        eye.start_detached_capture([] { throw std::logic_error("unexpected exact fallback"); });
        blockDraw=true;
        record::vkCmdDraw(cmd,44,1,0,0);
        eye.vkCmdDraw(cmd,45,1,0,0);
        assert(eye.publish_held());
        assert(!eye.command_count() && eyeBytes.empty());
        {std::unique_lock lock(mutex);changed.wait(lock,[]{return drawEntered;});}
        record::vkCmdDraw(cmd,46,1,0,0);
        {std::lock_guard lock(mutex);assert((calls==std::vector<int>{44}));releaseDraw=true;changed.notify_all();}
        drain_record_commands();
        assert((calls==std::vector<int>{44,45,46}));
        for(auto thread:threads)assert(thread!=producer && thread==threads[0]);
        // A custom command between POD runs must not be skipped or reordered.
        eye.vkCmdDraw(cmd,47,1,0,0);
        eye.emit_owned([] { note(48); });
        eye.vkCmdDraw(cmd,49,1,0,0);
        assert(!eye.publish_held() && eye.command_count());
        for(auto& action:eye.take()) action();
        assert((calls==std::vector<int>{44,45,46,47,48,49}));
        eye.reset();
    }
    stop_record_worker();blockDraw=false;drawEntered=false;releaseDraw=false;
    calls.clear();threads.clear();
    start_record_worker();assert(record_worker_active() && dispatch_recorder()->pod_mode());
    // Invalid producer input must fail synchronously without enqueueing a
    // null handle, submitting an older prefix, or invoking raw Vulkan.
    record::vkCmdDraw(cmd,20,1,0,0);
    const auto callsBeforeNull=calls.size();
    bool nullRejected=false;
    try { record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,VK_NULL_HANDLE); }
    catch(const std::invalid_argument&) { nullRejected=true; }
    assert(nullRejected && calls.size()==callsBeforeNull);
    VkClearValue clear{};clear.color.uint32[0]=91;
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.clearValueCount=1;pass.pClearValues=&clear;
    record::vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
    clear.color.uint32[0]=0;record::vkCmdEndRenderPass(cmd);
    drain_record_commands();assert((calls==std::vector<int>{20,71,72}));
    for(auto thread:threads)assert(thread!=producer);
    // Unsupported pNext must drain the exact older POD prefix before immediate
    // fallback while the caller's original nested arguments still exist.
    record::vkCmdDraw(cmd,21,1,0,0);
    VkRenderingInfo unsupported{VK_STRUCTURE_TYPE_RENDERING_INFO};unsupported.pNext=&pass;
    record::vkCmdBeginRendering(cmd,&unsupported);
    assert(calls[calls.size()-2]==21 && calls.back()==70 && threads.back()==producer);
    // Independent DRC dispatch bypasses the facade before touching its pointer.
    std::thread isolated([&]{IsolatedCommandDispatch bypass;record::vkCmdDraw(cmd,22,1,0,0);});
    isolated.join();assert(calls.back()==22);
    record::vkCmdDraw(cmd,23,1,0,0);stop_record_worker();
    assert(calls.back()==23 && !record_worker_active() && !active_deferred_commands);
    // Presentation must finish an older captured prefix before recording on
    // the producer. Both direct command-buffer begin routing and facade calls
    // must bypass the worker, then restore before its end/reset/submit packet.
    calls.clear();threads.clear();start_record_worker();
    record::vkCmdDraw(cmd,30,1,0,0);
    {
        ProducerPresentationRecording presentation;
        assert(!record_worker_commands() && !dispatch_recorder());
        assert(calls==std::vector<int>{30} && threads[0]!=producer);
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        assert(vkBeginCommandBuffer(cmd,&info)==VK_SUCCESS);
        record::vkCmdDraw(cmd,31,1,0,0);
        assert(threads[1]==producer && threads[2]==producer);
    }
    assert(record_worker_commands() && dispatch_recorder()->pod_mode());
    record::vkCmdDraw(cmd,32,1,0,0);
    const auto presentationSerial=submit_record_commands(handle<VkDevice>(2),handle<VkQueue>(3),cmd,handle<VkFence>(4),VK_NULL_HANDLE,VK_NULL_HANDLE);
    wait_record_commands(presentationSerial,handoff_trace::Reason::Present);
    assert((calls==std::vector<int>{30,80,31,32,81,82,83}));
    for(size_t i=3;i<calls.size();++i)assert(threads[i]!=producer);
    bool restoredAfterThrow=false;
    try { ProducerPresentationRecording presentation;throw std::runtime_error("synthetic presentation failure"); }
    catch(const std::runtime_error&) { restoredAfterThrow=record_worker_commands() && dispatch_recorder()->pod_mode(); }
    assert(restoredAfterThrow);
    stop_record_worker();
    // A complete producer presentation owns end/reset/submit/present too.
    // Older worker commands must finish first, and subsequent game commands
    // return to the worker only after the scope has ended.
    calls.clear();threads.clear();start_record_worker();
    record::vkCmdDraw(cmd,60,1,0,0);
    {
        ProducerPresentationRecording presentation;
        assert(producer_presentation_recording() && !record_worker_commands());
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        assert(vkBeginCommandBuffer(cmd,&beginInfo)==VK_SUCCESS);
        record::vkCmdDraw(cmd,61,1,0,0);
        assert(vkEndCommandBuffer(cmd)==VK_SUCCESS);
        const auto fence=handle<VkFence>(4);
        assert(vkResetFences(handle<VkDevice>(2),1,&fence)==VK_SUCCESS);
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount=1;submitInfo.pCommandBuffers=&cmd;
        assert(vkQueueSubmit(handle<VkQueue>(3),1,&submitInfo,fence)==VK_SUCCESS);
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        assert(vkQueuePresentKHR(handle<VkQueue>(3),&presentInfo)==VK_SUCCESS);
        assert((calls==std::vector<int>{60,80,61,81,82,83,84}));
        assert(threads[0]!=producer);
        for(size_t i=1;i<threads.size();++i)assert(threads[i]==producer);
    }
    assert(!producer_presentation_recording() && record_worker_commands());
    record::vkCmdDraw(cmd,62,1,0,0);stop_record_worker();
    assert(calls.back()==62 && threads.back()!=producer);
    // A 32 KiB completed-draw prefix reaches the worker before the producer
    // finishes its tail. Derive the draw count from actual packet bytes rather
    // than assuming a host handle size; retain the real-overlap assertion.
    calls.clear();threads.clear();blockDraw=true;drawEntered=false;releaseDraw=false;
    start_record_worker();
    unsigned prefixDraws=0;
    do {
        assert(prefixDraws<4096);
        record::vkCmdDraw(cmd,44,1,0,0);++prefixDraws;
    } while(!record_worker_commands()->empty());
    {std::unique_lock lock(mutex);changed.wait(lock,[]{return drawEntered;});}
    record::vkCmdDraw(cmd,45,1,0,0);
    assert(!record_worker_commands()->empty());
    {std::lock_guard lock(mutex);assert(calls==std::vector<int>{44});releaseDraw=true;changed.notify_all();}
    drain_record_commands();stop_record_worker();
    assert(calls.size()==prefixDraws+1 && calls.back()==45);
    for(size_t i=0;i<prefixDraws;++i)assert(calls[i]==44 && threads[i]!=producer);
    blockDraw=false;
    // Appending immutable data alone cannot publish. The next complete draw
    // crossing 32 KiB does. A later 65-draw short tail must stay pending.
    calls.clear();threads.clear();start_record_worker();
    std::array<uint32_t,32> constants;constants.fill(93);unsigned copies=0;
    while(record_worker_commands()->payload_bytes()<32*1024) {
        assert(copies<512);
        record::vkCmdPushConstants(cmd,handle<VkPipelineLayout>(7),VK_SHADER_STAGE_VERTEX_BIT,
            0,uint32_t(sizeof(constants)),constants.data());++copies;
    }
    constants.fill(94);
    assert(!record_worker_commands()->empty());
    record::vkCmdDraw(cmd,41,1,0,0);assert(record_worker_commands()->empty());
    for(unsigned i=0;i<64;++i)record::vkCmdDraw(cmd,46,1,0,0);
    assert(!record_worker_commands()->empty());
    record::vkCmdDraw(cmd,47,1,0,0);assert(!record_worker_commands()->empty());
    drain_record_commands();stop_record_worker();
    assert(calls.size()==copies+66 && calls[copies]==41 && calls.back()==47);
    for(size_t i=0;i<copies;++i)assert(calls[i]==74);
    // Neither dynamic nor classic pass-end packets publish short tails.
    calls.clear();threads.clear();start_record_worker();
    VkRenderingInfo shortRendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    record::vkCmdBeginRendering(cmd,&shortRendering);
    record::vkCmdDraw(cmd,48,1,0,0);assert(!record_worker_commands()->empty());
    record::vkCmdEndRendering(cmd);assert(!record_worker_commands()->empty());
    clear.color.uint32[0]=91;
    record::vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
    record::vkCmdDraw(cmd,49,1,0,0);assert(!record_worker_commands()->empty());
    record::vkCmdEndRenderPass(cmd);assert(!record_worker_commands()->empty());
    {std::lock_guard lock(mutex);assert(calls.empty());}
    drain_record_commands();stop_record_worker();
    assert((calls==std::vector<int>{70,48,73,71,49,72}));
    for(auto thread:threads)assert(thread!=producer);
    // An active job plus four queued jobs is the existing hard queue bound.
    // The sixth publication must wait, then retain FIFO ordering after release.
    calls.clear();threads.clear();blockDraw=true;drawEntered=false;releaseDraw=false;
    {
        RecordWorker worker;PodCommands first;assert(first.vkCmdDraw(cmd,44,1,0,0));worker.publish(first);
        {std::unique_lock lock(mutex);changed.wait(lock,[]{return drawEntered;});}
        for(unsigned n=46;n<=49;++n) { PodCommands job;assert(job.vkCmdDraw(cmd,n,1,0,0));worker.publish(job); }
        std::atomic<bool> publishStarted=false,publishFinished=false;
        uint64_t tailSerial=0;
        std::thread publisher([&] {
            PodCommands tail;assert(tail.vkCmdDraw(cmd,50,1,0,0));publishStarted=true;changed.notify_all();
            tailSerial=worker.publish(tail);publishFinished=true;changed.notify_all();
        });
        {std::unique_lock lock(mutex);changed.wait(lock,[&]{return publishStarted.load();});
         assert(!changed.wait_for(lock,std::chrono::milliseconds(10),[&]{return publishFinished.load();}));
         releaseDraw=true;changed.notify_all();}
        publisher.join();assert(publishFinished);worker.wait(tailSerial);worker.stop();
        assert((calls==std::vector<int>{44,46,47,48,49,50}));
    }
    blockDraw=false;
    // Worker errors propagate; even then shutdown joins and withdraws the facade.
    start_record_worker();failEnd=true;
    submit_record_commands(handle<VkDevice>(2),handle<VkQueue>(3),cmd,handle<VkFence>(4),VK_NULL_HANDLE,VK_NULL_HANDLE);
    bool failed=false;try{drain_record_commands();}catch(const std::runtime_error&){failed=true;}assert(failed);
    failed=false;try{stop_record_worker();}catch(const std::runtime_error&){failed=true;}assert(failed);
    assert(!record_worker_active() && !active_deferred_commands);
}
