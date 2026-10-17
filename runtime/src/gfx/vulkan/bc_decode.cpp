#include "deferred_dispatch.h"
#include "../../exception_report.h"
#include "bc_decode.h"
#include "bc_reference.h"
#include "bc_shader.h"
#include "shaders.h"
#include "runtime.h"
#include "render_prof.h"
#include "worker_metrics.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace gfxvk {
namespace {
VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
VkPipelineLayout layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;
bool env_on(const char* key) { const char* s=std::getenv(key); return s && std::strcmp(s,"0"); }
void initialize() {
    if(pipeline)return;
    if(!R.computeQueue)exception_report::raise("BC fallback requires a compute-capable graphics queue");
    VkDescriptorSetLayoutBinding bindings[2]{};
    for(uint32_t n=0;n<2;++n)bindings[n]={n,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    di.bindingCount=2;di.pBindings=bindings;
    vk_check(vkCreateDescriptorSetLayout(R.device,&di,nullptr,&descriptors),"BC descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount=1;li.pSetLayouts=&descriptors;li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;
    vk_check(vkCreatePipelineLayout(R.device,&li,nullptr,&layout),"BC pipeline layout");
    std::string error;auto spirv=vk::compile_compute(bc::shader,&error);
    if(spirv.empty())exception_report::raise("BC compute compile: "+error);
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=spirv.size()*4;mi.pCode=spirv.data();
    VkShaderModule module=VK_NULL_HANDLE;vk_check(vkCreateShaderModule(R.device,&mi,nullptr,&module),"BC shader module");
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=layout;
    ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
    const bool tracing=stutter::trace().enabled();
    const auto cpuStart=tracing?stutter::thread_cpu_now():0;
    const auto started=tracing?stutter::now():0;
    VkResult result=vkCreateComputePipelines(R.device,R.pipelineCache,1,&ci,nullptr,&pipeline);
    if(tracing) {
        const auto ended=stutter::now();
        stutter::trace().add("bc_decode_compute_pipeline",R.frame,0,started,ended,int(result),
                             stutter::cpu_elapsed(cpuStart));
    }
    vkDestroyShaderModule(R.device,module,nullptr);vk_check(result,"BC compute pipeline");
}
Buffer dispatch(const std::vector<uint8_t>& blocks,uint32_t w,uint32_t h,uint32_t slices,uint32_t mode,bool readback) {
    initialize();
    uint64_t pixels=uint64_t(w)*h*slices;
    if(!pixels||pixels>UINT32_MAX/4||pixels*4>R.properties.limits.maxStorageBufferRange||
       blocks.size()>R.properties.limits.maxStorageBufferRange)
        exception_report::raise("BC decode exceeds device buffer limits");
    // one invocation per 4x4 block, 8x8 blocks per workgroup, one workgroup layer per slice
    const uint32_t gx=((w+3)/4+7)/8,gy=((h+3)/4+7)/8;
    const auto& groupLimit=R.properties.limits.maxComputeWorkGroupCount;
    if(gx>groupLimit[0]||gy>groupLimit[1]||slices>groupLimit[2])exception_report::raise("BC decode dispatch exceeds device limits");
    end_encoder();auto cmd=command_buffer();
    Buffer input=create_buffer(blocks.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer output=create_buffer(pixels*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        readback?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(!input.mapped)exception_report::raise("BC input buffer not mapped");
    std::memcpy(input.mapped,blocks.data(),blocks.size());
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=R.descriptorPool;ai.descriptorSetCount=1;ai.pSetLayouts=&descriptors;
    VkDescriptorSet set=VK_NULL_HANDLE;vk_check(vkAllocateDescriptorSets(R.device,&ai,&set),"BC descriptor set");
    VkDescriptorBufferInfo buffers[2]={{input.buffer,0,blocks.size()},{output.buffer,0,pixels*4}};
    VkWriteDescriptorSet writes[2]{};
    for(uint32_t i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];}
    vkUpdateDescriptorSets(R.device,2,writes,0,nullptr);
    // Coherent host writes are made available by submission. The input is unique to this dispatch.
    record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    record::vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
    uint32_t params[]={w,h,slices,mode};record::vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof params,params);
    record::vkCmdDispatch(cmd,gx,gy,slices);
    derive_dependency(output.use,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    transition_buffer(output,VK_PIPELINE_STAGE_TRANSFER_BIT|(readback?VK_PIPELINE_STAGE_HOST_BIT:0),
                      VK_ACCESS_TRANSFER_READ_BIT|(readback?VK_ACCESS_HOST_READ_BIT:0));
    defer_buffer(input);return output;
}
void verify(Buffer output,const std::vector<uint8_t>& blocks,uint32_t w,uint32_t h,uint32_t slices,uint32_t mode) {
    flush();
    auto reference=bc::decode(blocks,w,h,slices,mode);
    auto bytes=static_cast<const uint8_t*>(output.mapped);
    if(!bytes)exception_report::raise("BC verification buffer not mapped");
    for(size_t n=0;n<reference.size();++n)if(bytes[n]!=reference[n]) {
        LOG("[vulkan BC] mismatch mode=%u size=%ux%ux%u byte=%zu GPU=%u CPU=%u",mode,w,h,slices,n,bytes[n],reference[n]);
        exception_report::raise("BC GPU/CPU verification mismatch");
    }
}
// WWHD_VK_BC_VERIFY in the game: compare once the frame's submission has completed (its completion
// callbacks run before its deferred buffers are freed), without draining the GPU mid-frame, which
// would cut the frame's command buffer under the bound draw state.
void verify_when_complete(Buffer output,const std::vector<uint8_t>& blocks,uint32_t w,uint32_t h,uint32_t slices,uint32_t mode) {
    R.completions.push_back([output,blocks,w,h,slices,mode] {
        static uint32_t verified=0,mismatched=0;
        const auto reference=bc::decode(blocks,w,h,slices,mode);
        const auto* bytes=static_cast<const uint8_t*>(output.mapped);
        ++verified;
        for(size_t n=0;bytes&&n<reference.size();++n)if(bytes[n]!=reference[n]) {
            if(++mismatched<=20)
                LOG("[vulkan BC] mismatch mode=%u size=%ux%ux%u byte=%zu (pixel %zu) GPU=%u CPU=%u",mode,w,h,slices,n,n/4,bytes[n],reference[n]);
            break;
        }
        if(verified==1||verified%200==0)LOG("[vulkan BC] verified %u decoded uploads against the CPU decoder: %u mismatched",verified,mismatched);
    });
}
}
bool bc_decode_required(const FormatInfo& fmt) {
    if(!fmt.compressed)return false;
    if(env_on("WWHD_VK_FORCE_BC_DECODE")||!R.enabledFeatures.textureCompressionBC)return true;
    VkFormatProperties p{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,fmt.pixel,&p);
    constexpr auto needed=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return (p.optimalTilingFeatures&needed)!=needed;
}
void bc_decode_upload(Surface* s,uint32_t level,const std::vector<uint8_t>& blocks,uint32_t w,uint32_t h,uint32_t slices) {
    auto start=std::chrono::steady_clock::now();
    uint32_t type=(s->format&63)-0x30;
    uint32_t mode=type|((type>=4&&(s->format&0x200))?256:0);
    VkBufferImageCopy copy{};bool volume=s->imageType==VK_IMAGE_TYPE_3D;
    copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,volume?1:slices};copy.imageExtent={w,h,volume?slices:1};
    if(R.bcCpuDecode) {
        // driver rule (bc_decoder_choice.h): decode on the CPU and upload RGBA8 like any other texture
        std::vector<uint8_t> rgba;
        try{rgba=bc::decode(blocks,w,h,slices,mode);}
        catch(const std::exception& e){exception_report::raise(std::string("BC CPU decode: ")+e.what());}
        Buffer staging=create_buffer(rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(!staging.mapped){defer_buffer(staging);exception_report::raise("BC CPU staging allocation is not mapped");}
        std::memcpy(staging.mapped,rgba.data(),rgba.size());
        record::vkCmdCopyBufferToImage(command_buffer(),staging.buffer,s->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        defer_buffer(staging);
        rprof::add_upload(rprof::kUpTexture,blocks.size());
        if(env_on("WWHD_VK_BC_TIMING"))LOG("[vulkan BC] mode=%u %ux%ux%u bytes=%zu CPU-decode-us=%.1f",mode,w,h,slices,blocks.size(),
            std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
        return;
    }
    bool check=env_on("WWHD_VK_BC_VERIFY");
    Buffer output=dispatch(blocks,w,h,slices,mode,check);
    record::vkCmdCopyBufferToImage(command_buffer(),output.buffer,s->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    if(check)verify_when_complete(output,blocks,w,h,slices,mode);
    defer_buffer(output);
    rprof::add_upload(rprof::kUpTexture,blocks.size());
    if(env_on("WWHD_VK_BC_TIMING"))LOG("[vulkan BC] mode=%u %ux%ux%u bytes=%zu CPU-upload-us=%.1f%s",mode,w,h,slices,blocks.size(),
        std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count(),check?" (includes the verification copy)":" (dispatch recording only)");
}
void bc_decode_shutdown() {
    if(pipeline)vkDestroyPipeline(R.device,pipeline,nullptr);
    if(layout)vkDestroyPipelineLayout(R.device,layout,nullptr);
    if(descriptors)vkDestroyDescriptorSetLayout(R.device,descriptors,nullptr);
    pipeline=VK_NULL_HANDLE;layout=VK_NULL_HANDLE;descriptors=VK_NULL_HANDLE;
}
void bc_decode_smoke() {
    uint32_t dispatches=0;
    for(uint32_t type=1;type<=5;++type)for(uint32_t sign=0;sign<=(type>=4?1u:0u);++sign)
        for(auto shape:{std::array<uint32_t,3>{4,4,1},{7,5,2},{1,1,1},{70,37,3}}) {  // the last: several workgroups
            auto [w,h,z]=shape;std::vector<uint8_t> blocks(size_t((w+3)/4)*((h+3)/4)*z*bc::block_bytes(type));
            for(uint32_t seed=0;seed<12;++seed) {
                uint32_t random=seed+1;
                for(auto& byte:blocks){random=random*1664525+1013904223;byte=uint8_t(random>>24);}
                if(seed==0)std::fill(blocks.begin(),blocks.end(),0);
                if(seed==1)std::fill(blocks.begin(),blocks.end(),255);
                Buffer output=dispatch(blocks,w,h,z,type|(sign<<8),true);
                verify(output,blocks,w,h,z,type|(sign<<8));defer_buffer(output);++dispatches;
            }
        }
    LOG("[vulkan BC] synthetic GPU/CPU checks PASS (7 formats, tails, layers, workgroups, %u dispatches)",dispatches);
}
}
