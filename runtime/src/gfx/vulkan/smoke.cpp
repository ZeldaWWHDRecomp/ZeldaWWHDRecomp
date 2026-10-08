#include "bc_decode.h"
#include "bc_reference.h"
// No game assets: assertions inspect data returned by the actual Vulkan device.
#include "backend.h"
#include "buffer_cache.h"
#include "render_prof.h"
#include "write_watch.h"
#include "shaders.h"
#include "gx2/gx2.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "runtime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <zlib.h>
#include <stdexcept>
#include <string>

namespace gfxvk {
extern uint64_t g_stat_full_checks, g_stat_uploads;
void request_tv_dump(const std::string&,int);
namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
struct Image {
 Surface s;
 Image(uint32_t w,uint32_t h,uint32_t format,bool depth=false,uint32_t layers=1,uint32_t mips=1) {
  s.width=w;s.height=h;s.pitch=w;s.format=format;s.isDepth=depth;s.slices=layers;s.mips=mips;s.dim=layers>1?5:1;s.fmt=format_info(format,depth);create_surface_image(&s,true);
 }
 ~Image(){destroy_surface_image(&s);}
};
std::vector<uint8_t> read_image(Surface& s,VkImageAspectFlags aspect,uint32_t bytes,uint32_t mip=0,uint32_t layer=0) {
 uint32_t w=std::max(1u,s.extent.width>>mip),h=std::max(1u,s.extent.height>>mip);
 Buffer b=create_readback_buffer(size_t(w)*h*bytes);
 transition_image(&s,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 VkBufferImageCopy copy{};copy.imageSubresource={aspect,mip,layer,1};copy.imageExtent={w,h,1};
 auto cmd=command_buffer();vkCmdCopyImageToBuffer(cmd,s.image,s.layout,b.buffer,1,&copy);
 VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=b.buffer;barrier.size=VK_WHOLE_SIZE;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
 try {flush();std::vector<uint8_t> result(size_t(w)*h*bytes);memcpy(result.data(),b.mapped,result.size());defer_buffer(b);return result;}
 catch(...){defer_buffer(b);throw;}
}
void rgba_is(const std::vector<uint8_t>& data,const uint8_t rgba[4],const char* message) {
 for(size_t i=0;i<data.size();++i)require(data[i]==rgba[i%4],message);
}
void clear_image(Surface& s,const float rgba[4]) {
 transition_image(&s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 VkClearColorValue value{};std::copy(rgba,rgba+4,value.float32);VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,s.mips,0,s.arrayLayers};
 vkCmdClearColorImage(command_buffer(),s.image,s.layout,&value,1,&range);mark_gpu_written(&s);
}
void bc_surface_check() {
 for (uint32_t format : {0x31u,0x431u,0x32u,0x432u,0x33u,0x433u,0x34u,0x234u,0x35u,0x235u}) {
  SurfaceDesc d;d.addr=mem::host_alloc(65536,256);d.mipAddr=mem::host_alloc(65536,256);
  d.width=16;d.height=16;d.pitch=4;d.slices=2;d.mips=2;d.format=format;d.dim=5;
  memset(mem::ptr(d.addr),0,65536);memset(mem::ptr(d.mipAddr),0,65536);
  auto* s=find_or_create_surface(d,false);
  if(!s->bcDecoded)continue;
  upload_surface(s);auto uploads=g_stat_uploads;upload_surface(s);
  require(g_stat_uploads==uploads,"BC upload cache did not hit");
  const unsigned type=(format&63)-0x30,mode=type|((format&0x200)?256:0);
  auto expected=[&](uint8_t fill){std::vector<uint8_t> block(bc::block_bytes(type),fill);return bc::pixel(block.data(),type,mode&256,0);};
  auto zero=expected(0),changed=expected(255);
  for(unsigned level=0;level<2;++level)for(unsigned layer=0;layer<2;++layer)
   rgba_is(read_image(*s,VK_IMAGE_ASPECT_COLOR_BIT,4,level,layer),zero.data(),"BC mip/layer upload differs");
  memset(mem::ptr(d.mipAddr),255,65536);invalidate(2,d.mipAddr+16,4);upload_surface(s);
  require(g_stat_uploads==uploads+1,"BC partial invalidation did not re-upload");
  for(unsigned layer=0;layer<2;++layer){
   rgba_is(read_image(*s,VK_IMAGE_ASPECT_COLOR_BIT,4,0,layer),zero.data(),"BC mip invalidation modified base");
   rgba_is(read_image(*s,VK_IMAGE_ASPECT_COLOR_BIT,4,1,layer),changed.data(),"BC changed mip/layer differs");
  }
 }
 fprintf(stderr,"[renderer smoke] BC production uploads, sRGB/signed views, mip/layer cache and invalidation passed\n");
}
void upload_arena_check() {
 const uint64_t before=R.uploadAllocations;
 auto a=allocate_upload(16,256),b=allocate_upload(16,256);
 require(a.buffer==b.buffer&&a.offset!=b.offset,"arena slices alias or fail pooling");
 require(a.offset%256==0&&b.offset%256==0,"arena alignment failed");
 memset(a.mapped,0x31,16);memset(b.mapped,0x72,16);
 Buffer out=create_readback_buffer(32);
 VkBufferCopy ca{a.offset,0,16},cb{b.offset,16,16};
 auto cmd=command_buffer();vkCmdCopyBuffer(cmd,a.buffer,out.buffer,1,&ca);vkCmdCopyBuffer(cmd,b.buffer,out.buffer,1,&cb);
 VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=out.buffer;barrier.size=VK_WHOLE_SIZE;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);flush();
 auto bytes=static_cast<uint8_t*>(out.mapped);for(int i=0;i<32;i++)require(bytes[i]==(i<16?0x31:0x72),"arena GPU snapshots corrupted");
 auto reuse=allocate_upload(16,256);require(reuse.buffer==a.buffer&&reuse.offset==a.offset,"arena failed fence reuse");
 require(R.uploadAllocations<=before+1,"arena allocated per slice");defer_buffer(out);command_buffer();flush();
 fprintf(stderr,"[renderer smoke] immutable upload arena GPU snapshots and fence reuse passed\n");
}
std::vector<uint8_t> read_buffer(VkBuffer source,VkDeviceSize offset,uint32_t size) {
 Buffer out=create_readback_buffer(size);
 VkBufferCopy copy{offset,0,size};auto cmd=command_buffer();vkCmdCopyBuffer(cmd,source,out.buffer,1,&copy);
 VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=out.buffer;barrier.size=VK_WHOLE_SIZE;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
 try {flush();std::vector<uint8_t> result(static_cast<uint8_t*>(out.mapped),static_cast<uint8_t*>(out.mapped)+size);defer_buffer(out);return result;}
 catch(...){defer_buffer(out);throw;}
}
// Guest buffer cache (WWHD_VK_BUFFER_CACHE=1 only): GPU copies read back from the device after a hit,
// an unannounced CPU write, a GX2Invalidate hint, a save-state reset and for native index data.
void buffer_cache_check() {
 if(!buffer_cache_enabled()){fprintf(stderr,"[renderer smoke] buffer cache off (WWHD_VK_BUFFER_CACHE=1 tests it)\n");return;}
 const uint32_t size=8192,addr=mem::host_alloc(65536,4096);
 for(uint32_t i=0;i<65536;++i)mem::ptr(addr)[i]=uint8_t(i*7+3);
 auto gpu_equals_guest=[&](const UploadSlice& s,uint32_t at,uint32_t n,const char* message) {
  auto bytes=read_buffer(s.buffer,s.offset,n);require(!memcmp(bytes.data(),mem::ptr(at),n),message);
 };
 UploadSlice a,b;
 require(cached_guest_range(addr,size,rprof::kUpVertex,a),"buffer cache refused a static range");
 gpu_equals_guest(a,addr,size,"buffer cache upload differs on the GPU");
 require(cached_guest_range(addr,size,rprof::kUpVertex,b)&&b.buffer==a.buffer&&b.offset==a.offset,"unchanged range was not a hit");
 require(cached_guest_range(addr,size/2,rprof::kUpUbo,b)&&b.offset==a.offset,"sub-range with the same start was not a hit");
 mem::ptr(addr)[size-3]^=0xA5;  // unannounced CPU write: page fault, newer stamp
 require(cached_guest_range(addr,size,rprof::kUpVertex,b)&&b.offset!=a.offset,"written range was not uploaded again");
 gpu_equals_guest(b,addr,size,"re-uploaded range differs on the GPU");
 gpu_equals_guest(b,addr,size,"re-uploaded range changed after a submission");
 a=b;buffer_cache_guest_invalidate(1,addr+16,4);
 require(cached_guest_range(addr,size,rprof::kUpVertex,b)&&b.offset!=a.offset,"GX2Invalidate did not refresh the range");
 a=b;buffer_cache_invalidate_all();
 require(cached_guest_range(addr,size,rprof::kUpVertex,b)&&b.offset!=a.offset,"save-state reset did not refresh the range");
 gpu_equals_guest(b,addr,size,"range after reset differs on the GPU");
 UploadSlice ix;bufcache::Entry* entry=nullptr;
 require(cached_native_indices(addr+size,4096,ix,entry)&&entry&&entry->shadow.size()==4096&&!memcmp(entry->shadow.data(),mem::ptr(addr+size),4096),"index shadow differs");
 gpu_equals_guest(ix,addr+size,4096,"cached index data differs on the GPU");
 command_buffer();flush();require(R.garbageCacheRegions.empty(),"replaced buffer cache regions were not retired");
 fprintf(stderr,"[renderer smoke] buffer cache hit, write fault, GX2Invalidate, reset and index readback passed\n");
}
void asynchronous_submission_check() {
 constexpr uint32_t submissions=10, payloadSize=16, regionSize=payloadSize*3;
 Buffer out=create_readback_buffer(submissions*regionSize);
 std::array<UploadSlice,4> firstSlices{};
 for(uint32_t submission=0;submission<submissions;++submission) {
  auto a=allocate_upload(payloadSize,256),b=allocate_upload(payloadSize,256);
  require(a.buffer==b.buffer&&a.offset!=b.offset,"async submission slices alias");
  if(submission<firstSlices.size())firstSlices[submission]=a;
  else {
   const auto& previous=firstSlices[submission%firstSlices.size()];
   require(a.buffer==previous.buffer&&a.offset==previous.offset,"async slot failed fenced arena reuse");
  }
  for(uint32_t byte=0;byte<payloadSize;++byte) {
   static_cast<uint8_t*>(a.mapped)[byte]=uint8_t(submission*19+byte);
   static_cast<uint8_t*>(b.mapped)[byte]=uint8_t(255-submission*13-byte);
  }
  // A temporary buffer is referenced twice and retired with this submission.
  // Freeing it while queued, or recycling its upload bytes early, corrupts the
  // third output region (and should also trigger Vulkan lifetime validation).
  Buffer temporary=create_buffer(payloadSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  auto cmd=command_buffer();
  VkBufferCopy first{a.offset,submission*regionSize,payloadSize};
  VkBufferCopy second{b.offset,submission*regionSize+payloadSize,payloadSize};
  VkBufferCopy toTemporary{a.offset,0,payloadSize};
  vkCmdCopyBuffer(cmd,a.buffer,out.buffer,1,&first);
  vkCmdCopyBuffer(cmd,b.buffer,out.buffer,1,&second);
  vkCmdCopyBuffer(cmd,a.buffer,temporary.buffer,1,&toTemporary);
  VkBufferMemoryBarrier transfer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  transfer.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  transfer.srcQueueFamilyIndex=transfer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  transfer.buffer=temporary.buffer;transfer.size=VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&transfer,0,nullptr);
  VkBufferCopy fromTemporary{0,submission*regionSize+payloadSize*2,payloadSize};
  vkCmdCopyBuffer(cmd,temporary.buffer,out.buffer,1,&fromTemporary);
  defer_buffer(temporary);
  flush_async();
 }
 VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
 host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
 host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
 host.buffer=out.buffer;host.size=VK_WHOLE_SIZE;
 vkCmdPipelineBarrier(command_buffer(),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
 flush();
 auto* bytes=static_cast<uint8_t*>(out.mapped);
 for(uint32_t submission=0;submission<submissions;++submission)
  for(uint32_t byte=0;byte<payloadSize;++byte) {
   uint32_t offset=submission*regionSize+byte;
   require(bytes[offset]==uint8_t(submission*19+byte),"async upload first snapshot differs");
   require(bytes[offset+payloadSize]==uint8_t(255-submission*13-byte),"async upload second snapshot differs");
   require(bytes[offset+payloadSize*2]==uint8_t(submission*19+byte),"async deferred temporary copy differs");
  }
 for(const auto& slot:R.submissions)
  require(!slot.pending&&slot.garbageBuffers.empty()&&slot.garbageImages.empty(),"async drain left pending resources");
 defer_buffer(out);flush();
 fprintf(stderr,"[renderer smoke] ten async submissions, immutable snapshots, slot wrap and deferred retirement passed\n");
}
// Scaled depth copies without blits (surfaces.cpp draw_depth_copy). Vulkan makes blits of depth/stencil
// formats optional and Adreno drivers report none for some, so resizing a depth target (a resolution or
// aspect-ratio change) threw there and aborted the game. The draws are forced here on any device and
// checked texel by texel against the nearest-filter mapping, through resample (all depth formats, two
// layers, up and down, a partial region) and through the real resize path of an aspect-ratio change.
void depth_copy_check() {
 auto depthAt=[](uint32_t x,uint32_t y,uint32_t layer){return float((x*7+y*13+layer*5)%97)/96.0f;};
 auto d16At=[](uint32_t x,uint32_t y,uint32_t layer){return uint16_t(x*1031+y*7919+layer*3);};
 auto stencilAt=[](uint32_t x,uint32_t y,uint32_t layer){return uint8_t(x*37+y*11+layer*101);};
 // texel bytes of one aspect of the pattern (depth: D16 as 2 bytes, else a float; stencil: 1 byte)
 auto pattern=[&](const Surface& s,VkImageAspectFlags aspect,uint32_t layer,uint32_t w,uint32_t h){
  const uint32_t bytes=aspect==VK_IMAGE_ASPECT_STENCIL_BIT?1:s.fmt.pixel==VK_FORMAT_D16_UNORM?2:4;
  std::vector<uint8_t> out(size_t(w)*h*bytes);
  for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x) {
   uint8_t* p=out.data()+(size_t(y)*w+x)*bytes;
   if(bytes==1)*p=stencilAt(x,y,layer);
   else if(bytes==2){uint16_t v=d16At(x,y,layer);memcpy(p,&v,2);}
   else {float v=depthAt(x,y,layer);memcpy(p,&v,4);}
  }
  return out;
 };
 auto fill=[&](Surface& s,uint32_t layers){
  std::vector<VkImageAspectFlags> aspects{VK_IMAGE_ASPECT_DEPTH_BIT};if(s.fmt.stencil)aspects.push_back(VK_IMAGE_ASPECT_STENCIL_BIT);
  transition_image(&s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  for(uint32_t layer=0;layer<layers;++layer)for(auto aspect:aspects) {
   auto texels=pattern(s,aspect,layer,s.extent.width,s.extent.height);
   Buffer staging=create_buffer(texels.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   require(staging.mapped!=nullptr,"depth copy staging buffer is not mapped");memcpy(staging.mapped,texels.data(),texels.size());
   VkBufferImageCopy copy{};copy.imageSubresource={aspect,0,layer,1};copy.imageExtent={s.extent.width,s.extent.height,1};
   vkCmdCopyBufferToImage(command_buffer(),staging.buffer,s.image,s.layout,1,&copy);defer_buffer(staging);
  }
  mark_gpu_written(&s);
 };
 // dst's region (0,0)-(dstW,dstH) must hold the nearest source texel of (0,0)-(srcW,srcH); outside it, `outside`
 auto check=[&](Surface& dst,uint32_t srcW,uint32_t srcH,uint32_t dstW,uint32_t dstH,uint32_t layers,const char* what,
                float outsideDepth=0,uint8_t outsideStencil=0){
  // dstW/dstH 0: no region, every texel must hold `outside`
  const float sx=dstW?float(srcW)/float(dstW):0,sy=dstH?float(srcH)/float(dstH):0;
  const uint32_t w=dst.extent.width,h=dst.extent.height;
  for(uint32_t layer=0;layer<layers;++layer) {
   const bool d16=dst.fmt.pixel==VK_FORMAT_D16_UNORM;
   auto depths=read_image(dst,VK_IMAGE_ASPECT_DEPTH_BIT,d16?2:4,0,layer);
   std::vector<uint8_t> stencils;if(dst.fmt.stencil)stencils=read_image(dst,VK_IMAGE_ASPECT_STENCIL_BIT,1,0,layer);
   uint32_t bad=0;
   for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x) {
    const size_t i=size_t(y)*w+x;const bool inside=x<dstW&&y<dstH;
    const uint32_t tx=std::min(uint32_t((float(x)+0.5f)*sx),srcW-1),ty=std::min(uint32_t((float(y)+0.5f)*sy),srcH-1);
    if(d16){uint16_t v;memcpy(&v,depths.data()+i*2,2);if(inside&&v!=d16At(tx,ty,layer))++bad;}
    else {float v;memcpy(&v,depths.data()+i*4,4);if(v!=(inside?depthAt(tx,ty,layer):outsideDepth))++bad;}
    if(dst.fmt.stencil&&stencils[i]!=(inside?stencilAt(tx,ty,layer):outsideStencil))++bad;
   }
   if(bad){fprintf(stderr,"[renderer smoke] %s: %u of %u texels differ (layer %u)\n",what,bad,w*h,layer);require(false,what);}
  }
 };
 const auto override=g_depthCopyOverride;g_depthCopyOverride=DepthCopyOverride::Draw;
 const uint32_t sizes[3][2]={{64,37},{17,11},{40,24}};
 for(uint32_t format:{0x05u,0x0Eu,0x11u}) {
  Image src(40,24,format,true,2);require(src.s.extent.width==40&&src.s.extent.height==24,"depth copy source extent differs");
  fill(src.s,2);
  for(auto& size:sizes) {
   Image dst(size[0],size[1],format,true,2);
   resample(&src.s,&dst.s,2);
   check(dst.s,40,24,size[0],size[1],2,"drawn depth copy (resample) differs from the nearest-filter mapping");
  }
  // a partial region from part of the source: the rest of the destination keeps its contents
  if(format!=0x05) {
   Image dst(48,32,format,true,1);
   transition_image(&dst.s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   VkClearDepthStencilValue value{0.75f,0x33};VkImageSubresourceRange range{dst.s.aspect,0,1,0,1};
   vkCmdClearDepthStencilImage(command_buffer(),dst.s.image,dst.s.layout,&value,1,&range);
   resample(&src.s,&dst.s,1,0.5f,0.75f,30,21);
   check(dst.s,20,18,30,21,1,"drawn partial depth copy differs",0.75f,0x33);
  }
 }
 fprintf(stderr,"[renderer smoke] drawn depth copies (D16, D32F, D32F+S8; layers, up/down, partial) passed\n");
 // the last resort, a device that can neither blit nor draw the format: the scaled copy clears a whole
 // destination (depth 1, stencil 0) and leaves a partial one alone, instead of throwing
 g_depthCopyOverride=DepthCopyOverride::Unsupported;
 {
  Image src(40,24,0x11,true,1);fill(src.s,1);
  Image dst(64,37,0x11,true,1);resample(&src.s,&dst.s,1);
  check(dst.s,40,24,0,0,1,"unscalable depth copy did not clear the destination",1.0f,0);
  Image part(48,32,0x11,true,1);
  transition_image(&part.s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkClearDepthStencilValue value{0.75f,0x33};VkImageSubresourceRange range{part.s.aspect,0,1,0,1};
  vkCmdClearDepthStencilImage(command_buffer(),part.s.image,part.s.layout,&value,1,&range);
  resample(&src.s,&part.s,1,0.5f,0.75f,30,21);
  check(part.s,20,18,0,0,1,"unscalable partial depth copy changed the destination",0.75f,0x33);
 }
 g_depthCopyOverride=DepthCopyOverride::Draw;
 fprintf(stderr,"[renderer smoke] unscalable depth copies clear (whole) or keep (partial) the destination\n");
 // the resize itself: a screen-shaped depth/stencil target, then a 21:9 aspect ratio and a 2x resolution
 SurfaceDesc d;d.addr=mem::host_alloc(64*36*8,256);d.width=64;d.height=36;d.pitch=64;d.format=0x11;d.isDepth=true;d.dim=1;d.slices=1;
 Surface* target=find_or_create_surface(d,true);
 require(target&&target->extent.width==64&&target->extent.height==36,"depth target extent differs");
 fill(*target,1);
 set_frame_aspect(21.0f/9.0f);latch_res_scale();
 require(find_or_create_surface(d,true)==target&&target->extent.width==84&&target->extent.height==36,"aspect change did not rescale the depth target");
 check(*target,64,36,84,36,1,"depth target after an aspect-ratio change differs");
 fill(*target,1);
 set_res_scale(2);latch_res_scale();
 require(find_or_create_surface(d,true)==target&&target->extent.width==168&&target->extent.height==72,"resolution change did not rescale the depth target");
 check(*target,84,36,168,72,1,"depth target after a resolution change differs");
 set_frame_aspect(16.0f/9.0f);set_res_scale(1);latch_res_scale();
 require(find_or_create_surface(d,true)==target&&target->extent.width==64,"depth target did not return to its guest size");
 g_depthCopyOverride=override;
 fprintf(stderr,"[renderer smoke] depth/stencil target resized by aspect ratio and resolution without blits passed\n");
}
// Volume render targets (issue #53, the Picto Box): the game renders 8x8x8 colour-grading volumes
// slice by slice (GX2 colour buffers of a 3D surface, the view selecting the slice) and samples them
// as 3D textures. The render target must be a volume whose slices are attachments, and the sampled
// lookup must return it (a 2D render target at that address made the 3D view throw).
void volume_target_check() {
 if(!R.imageView2DOn3DImage){fprintf(stderr,"[renderer smoke] volume render targets skipped (no imageView2DOn3DImage)\n");return;}
 const uint32_t w=8,h=8,depth=4,slice=2;
 std::vector<uint32_t> regs(0x10000,0);
 uint32_t addr=mem::host_alloc(w*h*depth*4,256);
 regs[mmCB_COLOR0_BASE]=addr;regs[mmCB_COLOR0_INFO]=0x1Au<<2;  // RGBA8 unorm
 regs[mmCB_COLOR0_TILE]=w|(depth<<16)|gx2::kColorTarget3D;regs[mmCB_COLOR0_FRAG]=h;regs[mmCB_COLOR0_VIEW]=slice;
 uint32_t selected=~0u;Surface* s=color_target(regs.data(),0,&selected);
 require(s&&s->imageType==VK_IMAGE_TYPE_3D&&s->extent.depth==depth&&s->arrayLayers==1,"volume colour target is not a 3D image");
 require(selected==slice,"volume colour target slice differs");
 const float blue[4]={0,0,1,1};clear_image(*s,blue);
 transition_image(s,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};target.imageView=layer_view(s,selected);target.imageLayout=s->layout;
 target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;target.clearValue.color.float32[1]=1;target.clearValue.color.float32[3]=1;
 VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea.extent={w,h};ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&target;
 auto cmd=command_buffer();vkCmdBeginRendering(cmd,&ri);R.rendering=true;end_encoder();mark_gpu_written(s);
 auto read_slice=[&](uint32_t z) {
  Buffer b=create_readback_buffer(w*h*4);
  transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageOffset={0,0,int32_t(z)};copy.imageExtent={w,h,1};
  auto c=command_buffer();vkCmdCopyImageToBuffer(c,s->image,s->layout,b.buffer,1,&copy);
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=b.buffer;barrier.size=VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
  try {flush();std::vector<uint8_t> result(w*h*4);memcpy(result.data(),b.mapped,result.size());defer_buffer(b);return result;}
  catch(...){defer_buffer(b);throw;}
 };
 const uint8_t greenBytes[4]={0,255,0,255},blueBytes[4]={0,0,255,255};
 rgba_is(read_slice(slice),greenBytes,"volume slice render differs");
 rgba_is(read_slice(slice-1),blueBytes,"volume render wrote outside its slice");
 // sampled as a volume (DIM_3D), it is this render target; a 2D sampled view never gets it
 SurfaceDesc volume;volume.addr=addr;volume.width=w;volume.height=h;volume.slices=depth;volume.pitch=w;volume.format=0x1a;volume.dim=2;
 require(find_or_create_surface(volume,false)==s,"sampled volume is not the rendered volume");
 uint32_t textureWords[7]={2,0,0,0,(0u<<16)|(1u<<19)|(2u<<22)|(3u<<25),0,0};
 require(sampled_texture_view(s,textureWords)!=VK_NULL_HANDLE,"sampled volume view creation failed");
 SurfaceDesc flat=volume;flat.slices=1;flat.dim=1;
 Surface* other=find_or_create_surface(flat,false);
 require(other&&other!=s&&other->imageType==VK_IMAGE_TYPE_2D,"a 2D sampled view got the rendered volume");
 fprintf(stderr,"[renderer smoke] volume render target slice, clear isolation and 3D sampling passed\n");
}
void triangle(Surface& s) {
 struct Resources {
  VkShaderModule vs=VK_NULL_HANDLE,ps=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
  ~Resources(){if(pipeline)vkDestroyPipeline(R.device,pipeline,nullptr);if(layout)vkDestroyPipelineLayout(R.device,layout,nullptr);if(vs)vkDestroyShaderModule(R.device,vs,nullptr);if(ps)vkDestroyShaderModule(R.device,ps,nullptr);}
 } objects;
 auto module=[&](const char* glsl,bool vertex,VkShaderModule& result) {
  std::string error;auto words=vk::compile_glsl(glsl,vertex,&error);if(words.empty())throw std::runtime_error("smoke GLSL compilation: "+error);
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4;ci.pCode=words.data();vk_check(vkCreateShaderModule(R.device,&ci,nullptr,&result),"smoke shader module");
 };
 module("#version 450\nvoid main(){vec2 p[3]=vec2[3](vec2(-0.8,-0.8),vec2(0.8,-0.8),vec2(0,0.8));gl_Position=vec4(p[gl_VertexIndex],0,1);}",true,objects.vs);
 module("#version 450\nlayout(location=0) out vec4 color;void main(){color=vec4(1,0,0,1);}",false,objects.ps);
 VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};vk_check(vkCreatePipelineLayout(R.device,&layout,nullptr,&objects.layout),"smoke pipeline layout");
 VkPipelineShaderStageCreateInfo stages[2]{};
 for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
 stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=objects.vs;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=objects.ps;
 VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
 VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkViewport viewport{0,0,float(s.extent.width),float(s.extent.height),0,1};VkRect2D scissor{{0,0},{s.extent.width,s.extent.height}};
 VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
 VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=0xf;
 VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&attachment;
 VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&s.fmt.pixel;
 VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.pNext=&rendering;info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;info.pViewportState=&vp;info.pRasterizationState=&raster;info.pMultisampleState=&ms;info.pColorBlendState=&blend;info.layout=objects.layout;
 vk_check(vkCreateGraphicsPipelines(R.device,R.pipelineCache,1,&info,nullptr,&objects.pipeline),"smoke triangle pipeline");
 R.pipelineCacheDirty=true;
 R.pipelineCacheChangedFrame=R.frame;
 transition_image(&s,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};target.imageView=layer_view(&s,0);target.imageLayout=s.layout;target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;target.clearValue.color.float32[3]=1;
 VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea=scissor;ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&target;
 auto cmd=command_buffer();vkCmdBeginRendering(cmd,&ri);R.rendering=true;vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,objects.pipeline);vkCmdDraw(cmd,3,1,0,0);end_encoder();mark_gpu_written(&s);flush();
 auto pixels=read_image(s,VK_IMAGE_ASPECT_COLOR_BIT,4);
 size_t center=(size_t(s.extent.height/2)*s.extent.width+s.extent.width/2)*4;
 require(pixels[center]==255&&pixels[center+1]==0&&pixels[center+2]==0&&pixels[center+3]==255,"triangle center pixel differs");
 require(pixels[0]==0&&pixels[1]==0&&pixels[2]==0&&pixels[3]==255,"triangle background pixel differs");
 size_t red=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]==255)++red;
 require(red>size_t(s.extent.width)*s.extent.height/5&&red<size_t(s.extent.width)*s.extent.height/2,"triangle rasterized area differs");
 fprintf(stderr,"[renderer smoke] generated GLSL triangle/readback passed (%zu red pixels)\n",red);
}
void vertex_window_check(Surface& s) {
 struct Resources {
  VkShaderModule vs=VK_NULL_HANDLE,ps=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
  ~Resources(){if(pipeline)vkDestroyPipeline(R.device,pipeline,nullptr);if(layout)vkDestroyPipelineLayout(R.device,layout,nullptr);if(vs)vkDestroyShaderModule(R.device,vs,nullptr);if(ps)vkDestroyShaderModule(R.device,ps,nullptr);}
 } objects;
 auto module=[&](const char* glsl,bool vertex,VkShaderModule& result) {
  std::string error;auto words=vk::compile_glsl(glsl,vertex,&error);if(words.empty())throw std::runtime_error("smoke GLSL compilation: "+error);
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4;ci.pCode=words.data();vk_check(vkCreateShaderModule(R.device,&ci,nullptr,&result),"smoke shader module");
 };
 module("#version 450\nlayout(location=0) in vec3 position;layout(location=1) in vec4 tint;layout(location=0) out vec4 vertexColor;void main(){gl_Position=vec4(position.xy,0,1);vertexColor=gl_VertexIndex==int(position.z)?tint:vec4(0,0,1,1);}",true,objects.vs);
 module("#version 450\nlayout(location=0) in vec4 vertexColor;layout(location=0) out vec4 color;void main(){color=vertexColor;}",false,objects.ps);
 VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};vk_check(vkCreatePipelineLayout(R.device,&layout,nullptr,&objects.layout),"smoke pipeline layout");
 VkPipelineShaderStageCreateInfo stages[2]{};
 for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
 stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=objects.vs;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=objects.ps;
 struct Vertex {float x,y,id,r,g,b,a;};
 VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
 VkVertexInputAttributeDescription attributes[2]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32A32_SFLOAT,12}};
 VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.vertexAttributeDescriptionCount=2;vertex.pVertexAttributeDescriptions=attributes;
 VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;assembly.primitiveRestartEnable=VK_TRUE;
 VkViewport viewport{0,0,float(s.extent.width),float(s.extent.height),0,1};VkRect2D scissor{{0,0},{s.extent.width,s.extent.height}};
 VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
 VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=0xf;
 VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&attachment;
 VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&s.fmt.pixel;
 VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.pNext=&rendering;info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;info.pViewportState=&vp;info.pRasterizationState=&raster;info.pMultisampleState=&ms;info.pColorBlendState=&blend;info.layout=objects.layout;
 vk_check(vkCreateGraphicsPipelines(R.device,R.pipelineCache,1,&info,nullptr,&objects.pipeline),"smoke triangle pipeline");
 R.pipelineCacheDirty=true;
 R.pipelineCacheChangedFrame=R.frame;
 std::array<Vertex,128> data{};
 for(uint32_t i=100;i<108;++i) {const float xy[3][2]={{-0.8f,-0.8f},{0.8f,-0.8f},{0,0.8f}};data[i]={xy[(i-100)%3][0],xy[(i-100)%3][1],float(i),1,0,0,1};}
 const uint32_t address=0x73510000; // Fixture cache identity; data is supplied directly.
 auto sameSlice=[](const UploadSlice& a,const UploadSlice& b){return a.buffer==b.buffer && a.offset==b.offset;};
 const char* reuseEnv=std::getenv("WWHD_VK_REUSE_VERTEX_SNAPSHOTS");
 const bool reuse=reuseEnv && !std::strcmp(reuseEnv,"1");
 const uint32_t reserved=106*sizeof(Vertex),offset=100*sizeof(Vertex);
 auto original=vertex_window_smoke_snapshot(0,address,reserved,offset,reserved-offset,data.data(),true);
 for(uint32_t i=0;i<offset;++i)require(static_cast<uint8_t*>(original.mapped)[i]==0xCD,"vertex window leading poison missing");
 auto repeated=vertex_window_smoke_snapshot(0,address,reserved,offset,reserved-offset,data.data(),true);
 require(sameSlice(original,repeated)==reuse,"vertex window exact reuse mismatch");
 data[1].r=0.25f;
 auto outside=vertex_window_smoke_snapshot(0,address,reserved,offset,reserved-offset,data.data(),true);
 require(sameSlice(repeated,outside)==reuse,"vertex window outside mutation reuse mismatch");
 data[101].g=0.25f;
 auto changed=vertex_window_smoke_snapshot(0,address,reserved,offset,reserved-offset,data.data(),true);
 require(!sameSlice(outside,changed),"vertex window fetched mutation reused stale slice");
 auto shifted=vertex_window_smoke_snapshot(0,address,reserved,offset+sizeof(Vertex),reserved-offset-sizeof(Vertex),data.data(),true);
 require(!sameSlice(changed,shifted),"vertex window shifted key reused stale slice");
 data[101].g=0;
 auto render=[&](bool window,uint32_t first,int32_t base,bool narrow,bool restart,bool converted) {
  const uint32_t end=restart?first+6:first+3;
  const uint32_t reservation=end*sizeof(Vertex),begin=window?first*sizeof(Vertex):0;
  auto vertices=vertex_window_smoke_snapshot(0,address,reservation,begin,reservation-begin,data.data(),true);
  // A freshly copied slice must preserve every fetched byte, including IDs.
  require(!memcmp(static_cast<uint8_t*>(vertices.mapped)+begin,reinterpret_cast<uint8_t*>(data.data())+begin,reservation-begin),"vertex window snapshot bytes differ");
  std::vector<uint32_t> wide={uint32_t(int64_t(first)-base),uint32_t(int64_t(first+1)-base),uint32_t(int64_t(first+2)-base)};
  if(restart){wide.push_back(UINT32_MAX);for(uint32_t i=3;i<6;++i)wide.push_back(uint32_t(int64_t(first+i)-base));}
  // Custom guest marker and endian conversion are normalized before Vulkan.
  if(converted){
   auto swap=[](uint32_t v){return (v<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|(v>>24);};
   if(restart)wide[3]=0x12345678;
   for(auto& value:wide){const uint32_t guestEndian=swap(value);value=swap(guestEndian);if(restart && value==0x12345678)value=UINT32_MAX;}
  }
  auto indices=allocate_upload(wide.size()*(narrow?2:4),4);
  for(size_t i=0;i<wide.size();++i) {if(narrow){uint16_t value=wide[i]==UINT32_MAX?UINT16_MAX:uint16_t(wide[i]);memcpy(static_cast<uint8_t*>(indices.mapped)+i*2,&value,2);}else memcpy(static_cast<uint8_t*>(indices.mapped)+i*4,&wide[i],4);}
  transition_image(&s,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};target.imageView=layer_view(&s,0);target.imageLayout=s.layout;target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;target.clearValue.color.float32[3]=1;
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea=scissor;ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&target;
  auto cmd=command_buffer();vkCmdBeginRendering(cmd,&ri);R.rendering=true;vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,objects.pipeline);
  vkCmdBindVertexBuffers(cmd,0,1,&vertices.buffer,&vertices.offset);vkCmdBindIndexBuffer(cmd,indices.buffer,indices.offset,narrow?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd,wide.size(),1,0,base,0);end_encoder();mark_gpu_written(&s);flush_async();
 };
 for(int test=0;test<12;++test) {
  const uint32_t first=test>=8?101:100;const int32_t base=test%3==0?-5:test%3==1?5:0;
  const bool narrow=test%2==0,restart=test%4>=2,converted=test%4==3;
  if(test==6)data[101].g=0.5f; // Same key, freshly changed fetched bytes.
  if(test==7)data[1].r=0.3f;   // Never fetched; must not affect pixels.
  render(false,first,base,narrow,restart,converted);auto full=read_image(s,VK_IMAGE_ASPECT_COLOR_BIT,4);
  render(true,first,base,narrow,restart,converted);auto window=read_image(s,VK_IMAGE_ASPECT_COLOR_BIT,4);
  require(full==window,"vertex window/full GPU pixels differ");
  const size_t center=(size_t(s.extent.height/2)*s.extent.width+s.extent.width/2)*4;
  require(window[center]>200 && window[center+2]==0,"vertex window position/VertexIndex invariant failed");
 }
 // More than four queued submissions exercise slot retirement and epoch reset.
 for(int i=0;i<10;++i)render(true,100,i%2?-5:5,i%2,false,false);
 auto final=read_image(s,VK_IMAGE_ASPECT_COLOR_BIT,4);
 require(final[(size_t(s.extent.height/2)*s.extent.width+s.extent.width/2)*4]>200,"vertex window ring retirement failed");
 fprintf(stderr,"[renderer smoke] vertex copy windows/full pixels, poison, signed base, restart, mutation and ring retirement passed\n");
}
void dynamic_uniform_check(Surface& s) {
 if(R.properties.limits.maxDescriptorSetUniformBuffersDynamic<3) {
  fprintf(stderr,"[renderer smoke] dynamic UBO ordering fixture skipped (device limit <3)\n");return;
 }
 struct Resources {
  VkShaderModule vs=VK_NULL_HANDLE,ps=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
  VkDescriptorSetLayout sets[2]{};
  ~Resources(){for(auto set:sets)if(set)vkDestroyDescriptorSetLayout(R.device,set,nullptr);if(pipeline)vkDestroyPipeline(R.device,pipeline,nullptr);if(layout)vkDestroyPipelineLayout(R.device,layout,nullptr);if(vs)vkDestroyShaderModule(R.device,vs,nullptr);if(ps)vkDestroyShaderModule(R.device,ps,nullptr);}
 } objects;
 auto module=[&](const char* glsl,bool vertex,VkShaderModule& result) {
  std::string error;auto words=vk::compile_glsl(glsl,vertex,&error);if(words.empty())throw std::runtime_error("smoke GLSL compilation: "+error);
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4;ci.pCode=words.data();vk_check(vkCreateShaderModule(R.device,&ci,nullptr,&result),"smoke shader module");
 };
 module("#version 450\nlayout(set=0,binding=7,std140) uniform Transform{vec4 transform;} ;layout(set=0,binding=1,std140) uniform Tint{vec4 tint;};layout(location=0) out vec4 vcolor;void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexIndex]*transform.xy+transform.zw,0,1);vcolor=tint;}",true,objects.vs);
 module("#version 450\nlayout(set=1,binding=3,std140) uniform Factor{vec4 factor;};layout(location=0) in vec4 vcolor;layout(location=0) out vec4 color;void main(){color=vcolor*factor;}",false,objects.ps);
 VkDescriptorSetLayoutBinding vsBindings[2]={{7,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};
 VkDescriptorSetLayoutBinding psBinding{3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
 VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};setInfo.bindingCount=2;setInfo.pBindings=vsBindings;
 vk_check(vkCreateDescriptorSetLayout(R.device,&setInfo,nullptr,&objects.sets[0]),"smoke dynamic VS layout");
 setInfo.bindingCount=1;setInfo.pBindings=&psBinding;vk_check(vkCreateDescriptorSetLayout(R.device,&setInfo,nullptr,&objects.sets[1]),"smoke dynamic PS layout");
 VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=2;layout.pSetLayouts=objects.sets;
 vk_check(vkCreatePipelineLayout(R.device,&layout,nullptr,&objects.layout),"smoke dynamic pipeline layout");
 VkPipelineShaderStageCreateInfo stages[2]{};
 for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
 stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=objects.vs;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=objects.ps;
 VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
 VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkViewport viewport{0,0,float(s.extent.width),float(s.extent.height),0,1};VkRect2D scissor{{0,0},{s.extent.width,s.extent.height}};
 VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
 VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=0xf;
 VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&attachment;
 VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&s.fmt.pixel;
 VkDynamicState dynamicState=VK_DYNAMIC_STATE_SCISSOR;
 VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=1;dynamic.pDynamicStates=&dynamicState;
 VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.pNext=&rendering;info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;info.pViewportState=&vp;info.pRasterizationState=&raster;info.pMultisampleState=&ms;info.pColorBlendState=&blend;info.layout=objects.layout;info.pDynamicState=&dynamic;
 vk_check(vkCreateGraphicsPipelines(R.device,R.pipelineCache,1,&info,nullptr,&objects.pipeline),"smoke triangle pipeline");
 R.pipelineCacheDirty=true;
 R.pipelineCacheChangedFrame=R.frame;
 // Two rounds force a pool reset between freshly allocated descriptor sets.
 for(uint32_t round=0;round<2;++round) {
  uint64_t generation=R.submissionGeneration;
  const float transform[4]={1,1,0,0},factor[4]={1,1,1,1};
  const float tints[2][2][4]={{{1,0,0,1},{0,1,0,1}},{{0,0,1,1},{1,1,0,1}}};
  std::array<UploadSlice,2> transforms{},tintSlices{},factors{};
  for(uint32_t draw=0;draw<2;++draw) {
   // Prepare binding7 before binding1, matching support uniforms prepared last.
   transforms[draw]=allocate_upload(16,R.properties.limits.minUniformBufferOffsetAlignment);
   factors[draw]=allocate_upload(16,R.properties.limits.minUniformBufferOffsetAlignment);
   tintSlices[draw]=allocate_upload(16,R.properties.limits.minUniformBufferOffsetAlignment);
   memcpy(transforms[draw].mapped,transform,16);memcpy(factors[draw].mapped,factor,16);memcpy(tintSlices[draw].mapped,tints[round][draw],16);
  }
  require(transforms[0].buffer==tintSlices[1].buffer&&transforms[0].buffer==factors[1].buffer,"dynamic UBO fixture requires pooled slices");
  VkDescriptorSet sets[2]{};VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=R.descriptorPool;allocation.descriptorSetCount=2;allocation.pSetLayouts=objects.sets;
  vk_check(vkAllocateDescriptorSets(R.device,&allocation,sets),"smoke dynamic descriptor sets");
  VkDescriptorBufferInfo buffers[3]={{transforms[0].buffer,0,16},{tintSlices[0].buffer,0,16},{factors[0].buffer,0,16}};
  VkWriteDescriptorSet writes[3]{};
  for(uint32_t i=0;i<3;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=sets[i==2?1:0];writes[i].dstBinding=i==0?7:i==1?1:3;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;writes[i].pBufferInfo=&buffers[i];}
  vkUpdateDescriptorSets(R.device,3,writes,0,nullptr);
  transition_image(&s,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};target.imageView=layer_view(&s,0);target.imageLayout=s.layout;target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea=scissor;ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&target;
  auto cmd=command_buffer();vkCmdBeginRendering(cmd,&ri);R.rendering=true;vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,objects.pipeline);
  for(uint32_t draw=0;draw<2;++draw) {
   // Dynamic offsets are ordered by binding within VS set0, then PS set1.
   uint32_t offsets[3]={uint32_t(tintSlices[draw].offset),uint32_t(transforms[draw].offset),uint32_t(factors[draw].offset)};
   vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,objects.layout,0,2,sets,3,offsets);
   VkRect2D half{{int32_t(draw*s.extent.width/2),0},{s.extent.width/2,s.extent.height}};vkCmdSetScissor(cmd,0,1,&half);vkCmdDraw(cmd,3,1,0,0);
  }
  end_encoder();mark_gpu_written(&s);flush();
  require(R.submissionGeneration!=generation,"dynamic descriptor pool reset did not change generation");
  auto pixels=read_image(s,VK_IMAGE_ASPECT_COLOR_BIT,4);
  for(uint32_t y=0;y<s.extent.height;++y)for(uint32_t x=0;x<s.extent.width;++x)for(uint32_t channel=0;channel<4;++channel)
   require(pixels[(size_t(y)*s.extent.width+x)*4+channel]==uint8_t(tints[round][x<s.extent.width/2?0:1][channel]*255),"dynamic UBO reused-set colors or offset order differ");
 }
 fprintf(stderr,"[renderer smoke] dynamic UBO immutable draws, VS/PS binding order and fresh sets after pool reset passed\n");
}
}
int renderer_smoke_test() {
 try {
  mem::init();bc_decode_smoke();bc_surface_check();upload_arena_check();asynchronous_submission_check();buffer_cache_check();set_res_scale(1);latch_res_scale();
  {
   Image upload(16,16,0x1a,false,2,2);
   upload.s.addr=mem::host_alloc(65536,256);upload.s.mipAddr=mem::host_alloc(65536,256);
   const uint8_t rgba[4]={17,34,51,255};
   for(uint32_t i=0;i<65536;++i){mem::ptr(upload.s.addr)[i]=rgba[i%4];mem::ptr(upload.s.mipAddr)[i]=rgba[i%4];}
   upload_surface(&upload.s);
   uint32_t textureWords[7]={5,0,0,0,(2u<<16)|(1u<<19)|(0u<<22)|(5u<<25),0,0};
   auto swizzled=sampled_texture_view(&upload.s,textureWords);require(swizzled!=VK_NULL_HANDLE&&swizzled==sampled_texture_view(&upload.s,textureWords),"sampled array/swizzle view cache differs");
   for(uint32_t mip=0;mip<2;++mip)for(uint32_t layer=0;layer<2;++layer)rgba_is(read_image(upload.s,VK_IMAGE_ASPECT_COLOR_BIT,4,mip,layer),rgba,"guest mip/layer upload differs");
   fprintf(stderr,"[renderer smoke] guest upload, two mips and two layers passed\n");
   // Use the public surface cache so the public invalidation path sees this fixture.
   SurfaceDesc cacheDesc;
   cacheDesc.addr=upload.s.addr;cacheDesc.mipAddr=upload.s.mipAddr;
   cacheDesc.width=16;cacheDesc.height=16;cacheDesc.pitch=16;cacheDesc.slices=2;
   cacheDesc.mips=2;cacheDesc.format=0x1a;cacheDesc.dim=5;
   auto* cached=find_or_create_surface(cacheDesc,false);
   upload_surface(cached);
   uint64_t checks=g_stat_full_checks,uploads=g_stat_uploads;
   upload_surface(cached);
   require(g_stat_full_checks==checks&&g_stat_uploads==uploads,"same-frame upload bypassed texture cache");
   ++R.frame;
   if(((R.frame+(cached->addr>>12))&63)==0)++R.frame;
   upload_surface(cached);
   require(g_stat_full_checks==checks&&g_stat_uploads==uploads,"unchanged next-frame texture performed a full check");
   const uint8_t changedMip[4]={85,102,119,255};
   for(uint32_t i=0;i<65536;++i)mem::ptr(cached->mipAddr)[i]=changedMip[i%4];
   // The invalidated range starts inside the mip, rather than crossing its base.
   invalidate(2,cached->mipAddr+16,4);
   require(cached->dirty&&cached->lastCheckedFrame!=R.frame,"interior mip invalidation did not reset the upload gate");
   upload_surface(cached);
   require(g_stat_full_checks==checks+1&&g_stat_uploads==uploads+1,"same-frame invalidated mip was not uploaded");
   for(uint32_t layer=0;layer<2;++layer) {
    rgba_is(read_image(*cached,VK_IMAGE_ASPECT_COLOR_BIT,4,0,layer),rgba,"mip invalidation changed the base image");
    rgba_is(read_image(*cached,VK_IMAGE_ASPECT_COLOR_BIT,4,1,layer),changedMip,"invalidated mip guest upload differs");
   }
   fprintf(stderr,"[renderer smoke] upload cache and interior mip invalidation/readback passed\n");
   {
    // A buffer sampled (with a two-level descriptor) before anything rendered it, then rendered:
    // the render target must be its own one-level surface, not the sampled texture, and later
    // sampling must find the target (issue #47).
    SurfaceDesc t;t.addr=mem::host_alloc(16*16*4,256);t.mipAddr=mem::host_alloc(8*8*4,256);
    t.width=16;t.height=16;t.pitch=16;t.slices=1;t.mips=2;t.format=0x1a;t.dim=1;
    auto* tex=find_or_create_surface(t,false);
    upload_surface(tex);
    SurfaceDesc rt=t;rt.mipAddr=0;rt.mips=1;
    auto* target=find_or_create_surface(rt,true);
    require(target&&target!=tex&&target->mips==1,"render target adopted a sampled mipmapped texture");
    mark_gpu_written(target);
    require(find_or_create_surface(t,false)==target,"sampling after the render did not find the render target");
    fprintf(stderr,"[renderer smoke] render target after a mipmapped sampled texture passed\n");
    // Bloom renders level 1 separately, then binds the base descriptor with LOD clamped to 1.
    // Distinct GPU colours prove that this reads the rendered mip rather than the base image.
    SurfaceDesc mip=rt;mip.addr=t.mipAddr;mip.width=8;mip.height=8;mip.pitch=8;
    auto* mipTarget=find_or_create_surface(mip,true);
    const float baseColor[4]={0,0,1,1},mipColor[4]={0,1,0,1};
    clear_image(*target,baseColor);clear_image(*mipTarget,mipColor);
    uint32_t words[7]={1u|(1u<<3)|(1u<<8)|(15u<<19),15u|(0x1au<<26),
        t.addr>>8,t.mipAddr>>8,(1u<<19)|(2u<<22)|(3u<<25),1,0};
    auto* sampledMip=sampled_texture(words,false);
    require(sampledMip!=target && sampledMip->mips==2,"GPU-rendered mip chain was not assembled");
    const uint8_t mipBytes[4]={0,255,0,255};
    rgba_is(read_image(*sampledMip,VK_IMAGE_ASPECT_COLOR_BIT,4,1),mipBytes,
        "fixed-LOD rendered mip readback differs");
    const uint8_t baseBytes[4]={0,0,255,255};
    rgba_is(read_image(*sampledMip,VK_IMAGE_ASPECT_COLOR_BIT,4),baseBytes,"mip chain changed the base image");
    const float changedColor[4]={1,0,0,1};const uint8_t changedBytes[4]={255,0,0,255};
    clear_image(*mipTarget,changedColor);
    auto* rebuilt=sampled_texture(words,false);
    rgba_is(read_image(*rebuilt,VK_IMAGE_ASPECT_COLOR_BIT,4,1),changedBytes,"mip-only write did not rebuild the sampled chain");
    fprintf(stderr,"[renderer smoke] GPU-rendered mip chain/base preservation/mip-only refresh passed\n");
   }
   {
    // A texel changed in place, unannounced, between the 256 words the former sampled check read
    // (step 1 KiB here): page write tracking must still upload it on the next frame.
    SurfaceDesc bigDesc;bigDesc.addr=mem::host_alloc(256*256*4,256);
    bigDesc.width=256;bigDesc.height=256;bigDesc.pitch=256;bigDesc.slices=1;bigDesc.mips=1;bigDesc.format=0x1a;bigDesc.dim=1;
    for(uint32_t i=0;i<256*256*4;++i)mem::ptr(bigDesc.addr)[i]=rgba[i%4];
    auto* big=find_or_create_surface(bigDesc,false);
    upload_surface(big);
    ++R.frame;
    if(((R.frame+(big->addr>>12))&63)==0)++R.frame;
    upload_surface(big);
    uint64_t bigUploads=g_stat_uploads;
    const uint8_t texel[4]={200,10,20,255};
    memcpy(mem::ptr(bigDesc.addr)+(256+5)*4,texel,4);  // texel (5,1): bytes 1044..1047, not sampled
    ++R.frame;
    if(((R.frame+(big->addr>>12))&63)==0)++R.frame;
    upload_surface(big);
    require(g_stat_uploads==bigUploads+1,"unannounced in-place texel change was not uploaded");
    auto px=read_image(*big,VK_IMAGE_ASPECT_COLOR_BIT,4);
    require(!memcmp(px.data()+(256+5)*4,texel,4)&&!memcmp(px.data(),rgba,4),"unannounced texel change readback differs");
    fprintf(stderr,"[renderer smoke] unannounced in-place texel change between sampled words uploaded next frame\n");
   }
   Image color(16,16,0x1a);const float green[4]={0,1,0,1};const uint8_t greenBytes[4]={0,255,0,255};clear_image(color.s,green);rgba_is(read_image(color.s,VK_IMAGE_ASPECT_COLOR_BIT,4),greenBytes,"color clear differs");
   Image scaled(32,32,0x1a);resample(&color.s,&scaled.s,1);rgba_is(read_image(scaled.s,VK_IMAGE_ASPECT_COLOR_BIT,4),greenBytes,"scaled blit differs");
   // Retire an image while its commands are pending; replacement must not destroy it early.
   clear_image(color.s,green);destroy_surface_image(&color.s);create_surface_image(&color.s,true);const float blue[4]={0,0,1,1};const uint8_t blueBytes[4]={0,0,255,255};clear_image(color.s,blue);rgba_is(read_image(color.s,VK_IMAGE_ASPECT_COLOR_BIT,4),blueBytes,"deferred image replacement differs");
   fprintf(stderr,"[renderer smoke] clear, scaled blit and deferred image replacement passed\n");
   Image depth(16,16,0x11,true);depth.s.addr=mem::host_alloc(65536,256);
   const uint32_t packedDepth=0x5a800000;for(uint32_t i=0;i<65536;i+=4)memcpy(mem::ptr(depth.s.addr)+i,&packedDepth,4);
   upload_surface(&depth.s);
   textureWords[0]=1;require(sampled_texture_view(&depth.s,textureWords)!=VK_NULL_HANDLE,"sampled depth-only view creation failed");
   auto depths=read_image(depth.s,VK_IMAGE_ASPECT_DEPTH_BIT,4);auto stencils=read_image(depth.s,VK_IMAGE_ASPECT_STENCIL_BIT,1);
   for(size_t i=0;i<stencils.size();++i){float value;memcpy(&value,depths.data()+i*4,4);require(std::abs(value-0.5f)<0.00001f&&stencils[i]==0x5a,"depth/stencil guest upload differs");}
   transition_image(&depth.s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   VkClearDepthStencilValue dv{0.25f,0xa5};VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};vkCmdClearDepthStencilImage(command_buffer(),depth.s.image,depth.s.layout,&dv,1,&range);
   depths=read_image(depth.s,VK_IMAGE_ASPECT_DEPTH_BIT,4);stencils=read_image(depth.s,VK_IMAGE_ASPECT_STENCIL_BIT,1);
   for(size_t i=0;i<stencils.size();++i){float value;memcpy(&value,depths.data()+i*4,4);require(value==0.25f&&stencils[i]==0xa5,"depth/stencil clear differs");}
   fprintf(stderr,"[renderer smoke] depth/stencil upload and clear passed\n");
   depth_copy_check();
   {
    auto peek=std::make_unique<Surface>();
    peek->width=1280;peek->height=720;peek->pitch=1280;peek->format=0x11;peek->isDepth=true;peek->fmt=format_info(0x11,true);
    peek->addr=mem::host_alloc(256,256);create_surface_image(peek.get(),true,VkExtent3D{16,16,1});
    auto* surface=peek.get();auto entry=R.surfaces.emplace(peek->addr,std::move(peek));
    uint32_t previousDepth=R.mainDepthAddr;R.mainDepthAddr=surface->addr;
    uint32_t result=mem::host_alloc(256,256);uint32_t cells[]={320,240,result,0,0,result+4};
    for(float z:{0.25f,1.0f}) {
     transition_image(surface,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
     VkClearDepthStencilValue value{z,0};vkCmdClearDepthStencilImage(command_buffer(),surface->image,surface->layout,&value,1,&range);
     peek_z(cells,6);flush();uint32_t expected=z==1.0f?0xFFFFFFu:0x3FFFFFu;
     require(ld32(result)==expected&&ld32(result+4)==expected,"GPU depth peek differs");
    }
    // A drain retires the newest fence first; an older answer must not replace it.
    for(float z:{0.25f,1.0f}) {
     transition_image(surface,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
     VkClearDepthStencilValue value{z,0};vkCmdClearDepthStencilImage(command_buffer(),surface->image,surface->layout,&value,1,&range);
     peek_z(cells,6);flush_async();
    }
    flush();require(ld32(result)==0xFFFFFFu&&ld32(result+4)==0xFFFFFFu,"older GPU depth answer replaced newer answer");
    R.mainDepthAddr=previousDepth;destroy_surface_image(surface);R.surfaces.erase(entry);
    fprintf(stderr,"[renderer smoke] asynchronous GPU depth peeks passed\n");
   }
   volume_target_check();
   Image rendered(64,64,0x1a);dynamic_uniform_check(rendered.s);vertex_window_check(rendered.s);triangle(rendered.s);
   if(R.tv.scan)destroy_surface_image(R.tv.scan.get());R.tv.scan=std::make_unique<Surface>();auto& scan=*R.tv.scan;scan.width=64;scan.height=64;scan.format=0x1a;scan.fmt=format_info(scan.format,false);create_surface_image(&scan,false);resample(&rendered.s,&scan,1);mark_gpu_written(&scan);
   auto capturePath=std::filesystem::temp_directory_path()/("wwhd-vulkan-smoke-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".png");
   request_tv_dump(capturePath.string(),0);swap();
   std::ifstream capture(capturePath,std::ios::binary);std::vector<uint8_t> png((std::istreambuf_iterator<char>(capture)),std::istreambuf_iterator<char>());
   const uint8_t signature[8]={137,80,78,71,13,10,26,10};require(png.size()>8&&memcmp(png.data(),signature,8)==0,"PNG capture signature differs");
   auto get32=[&](size_t offset){return (uint32_t(png[offset])<<24)|(uint32_t(png[offset+1])<<16)|(uint32_t(png[offset+2])<<8)|png[offset+3];};
   std::vector<uint8_t> compressed;bool header=false;
   for(size_t offset=8;offset+12<=png.size();) {
    uint32_t size=get32(offset);require(size<=png.size()-offset-12,"PNG capture chunk length differs");
    const uint8_t* type=png.data()+offset+4;const uint8_t* data=type+4;
    require(uint32_t(crc32(0,type,size+4))==get32(offset+8+size),"PNG capture chunk CRC differs");
    if(memcmp(type,"IHDR",4)==0){require(size==13&&get32(offset+8)==64&&get32(offset+12)==64,"PNG capture dimensions differ");header=true;}
    if(memcmp(type,"IDAT",4)==0)compressed.insert(compressed.end(),data,data+size);
    offset+=size+12;
   }
   std::vector<uint8_t> decoded((64*4+1)*64);uLongf decodedSize=decoded.size();require(header&&uncompress(decoded.data(),&decodedSize,compressed.data(),compressed.size())==Z_OK&&decodedSize==decoded.size(),"PNG capture decompression differs");
   size_t pngCenter=32*(64*4+1)+1+32*4;require(decoded[pngCenter]==255&&decoded[pngCenter+1]==0&&decoded[pngCenter+2]==0,"PNG capture triangle center differs");
   fprintf(stderr,"[renderer smoke] queued GPU PNG capture passed: %s\n",capturePath.string().c_str());
   require(R.tv.swapchain!=VK_NULL_HANDLE,"smoke presentation did not create a swapchain");fprintf(stderr,"[renderer smoke] scan-buffer swapchain presentation passed\n");
  }
  // Ensure deferred objects left by readback and stack-owned images are actually reclaimed.
  command_buffer();flush();require(R.garbageBuffers.empty()&&R.garbageImages.empty()&&R.garbageCacheRegions.empty(),"deferred Vulkan resources were not reclaimed");
  save_pipeline_cache();
  fprintf(stderr,"[renderer smoke] PASS: actual device upload/clear/blit/depth/triangle/present\n");return 0;
 }catch(const std::exception& e){fprintf(stderr,"[renderer smoke] FAIL: %s\n",e.what());try {command_buffer();flush();}catch(...){}return 1;}
}
} // namespace gfxvk
