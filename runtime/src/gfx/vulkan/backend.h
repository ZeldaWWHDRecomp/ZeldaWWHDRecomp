#pragma once
#include "loader.h"
#ifdef WWHD_SDL_HOST
#include <SDL3/SDL.h>
#endif
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "formats.h"
#include "api.h"
#include "buffer_cache_core.h"
namespace gfxvk {
struct Buffer { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; void* mapped=nullptr; VkDeviceSize size=0;
                VkMemoryPropertyFlags properties=0; /* of the memory type create_buffer chose */ };
struct UploadSlice { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceSize offset=0,size=0; void* mapped=nullptr; };
struct CachedGuestLayout;
struct Surface {
    std::shared_ptr<Surface> mipChain; // sampled companion assembled from GPU-rendered levels
    uint64_t mipChainSeq = ~0ull;
 VkImage image=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE;
 VkImageType imageType=VK_IMAGE_TYPE_2D; VkImageViewType viewType=VK_IMAGE_VIEW_TYPE_2D;
 VkExtent3D extent{}; VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED; VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT; VkImageUsageFlags usage=0; VkImageCreateFlags createFlags=0;
 uint32_t arrayLayers=1; std::vector<VkImageView> layerViews;
 std::unordered_map<uint32_t,VkImageView> sampledViews;
 uint32_t addr=0,mipAddr=0,width=0,height=0,slices=1,pitch=0,mips=1,format=0,dim=1,tileMode=0,swizzle=0;
 bool isDepth=false,gpuWritten=false,dirty=true,bcDecoded=false;
 uint64_t writeSeq=0,contentHash=0,lastCheckedFrame=~0ull,sparseHash=0;
 bool formatViews=false; // another surface at this address has the same texel bits in another format (adopt_newer_alias)
 uint64_t writtenBackSeq=0; // writeSeq when last written back to guest memory (linear surfaces, write_back_linear_targets)
 // CPU textures: write stamp (write_watch.h) of all levels' pages at the last full check
 uint64_t watchStamp=0; bool watched=false;
 uint32_t dataSize=0; FormatInfo fmt;
 mutable std::shared_ptr<CachedGuestLayout> guestLayout;
 float scale=1,sx=1,sy=1;
 float ax=1,ay=1; // aspect-ratio factors of screen-shaped targets (surfaces.cpp)
};
struct SurfaceDesc { uint32_t addr=0,mipAddr=0,width=0,height=0,slices=1,pitch=0,mips=1,format=0,dim=1,tileMode=0,swizzle=0; bool isDepth=false; };
struct Screen {
#ifdef WWHD_SDL_HOST
 SDL_Window* window=nullptr;
#else
 void* window=nullptr; // CAMetalLayer of the AppKit view (display.mm)
#endif
 VkSurfaceKHR surface=VK_NULL_HANDLE; VkSwapchainKHR swapchain=VK_NULL_HANDLE;
 VkFormat swapFormat=VK_FORMAT_UNDEFINED; VkExtent2D swapExtent{};
 std::vector<VkImage> images; std::vector<VkImageLayout> layouts;
 VkSemaphore acquired=VK_NULL_HANDLE,finished=VK_NULL_HANDLE;
 std::unique_ptr<Surface> scan;
 std::atomic<bool> visible{true},srgb{false},resize{false};
 int presentMode=-1,presentWanted=-1; // present mode of the swapchain, and the setting it was made for
 std::atomic<int> width{1280},height{720};
};
struct GpuScopeMetadata {
 uint32_t kind=0; // 0 render pass, 1 feedback copy.
 std::array<VkFormat,8> colors{};
 VkFormat depth=VK_FORMAT_UNDEFINED;
 VkExtent3D extent{};
 uint32_t mips=0,layers=0,aspects=0,colorCount=0;
 std::array<uint32_t,8> slices{};
 uint32_t depthSlice=0;
 bool depthOnly=false;
};
struct GpuScopeToken { uint64_t generation=0; uint32_t index=UINT32_MAX; };
struct Renderer {
 VkInstance instance=VK_NULL_HANDLE; VkPhysicalDevice physicalDevice=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE;
 VkPhysicalDeviceFeatures enabledFeatures{};
 bool computeQueue=false;
 bool dynamicRenderingKHR=false; // VK_KHR_dynamic_rendering (device older than Vulkan 1.3)
 bool portabilitySubset=false,imageViewSwizzle=true,imageViewReinterpretation=true;
 bool imageView2DOn3DImage=true; // 2D views of volume slices (render targets); core Vulkan 1.1, optional in the portability subset
 bool samplerMipLodBias=true,separateStencilMaskRef=true,constantAlphaColorBlendFactors=true,vertexAttributeAccessBeyondStride=true,samplerMirrorClampToEdge=false;
 VkPhysicalDeviceProperties properties{}; VkQueue queue=VK_NULL_HANDLE; uint32_t queueFamily=0;
 bool gpuTimestampsEnabled=false,gpuPassTimestampsEnabled=false;
 uint32_t gpuTimestampValidBits=0;
 struct GpuTimestampStats {
  double intervalNs=0,maxIntervalNs=0;
  uint64_t submissions=0,unavailable=0,zeroIntervals=0;
 } gpuTimestampStats;
 VkPipelineCache pipelineCache=VK_NULL_HANDLE;
 bool pipelineCacheDirty=false;
 uint64_t pipelineCacheChangedFrame=0;
 VkCommandPool commandPool=VK_NULL_HANDLE; VkCommandBuffer cmd=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
 VkDescriptorPool descriptorPool=VK_NULL_HANDLE; bool recording=false,rendering=false;
 // Descriptor reuse must include this epoch: pool handles repeat after reset.
 uint64_t submissionGeneration=0;
 uint64_t vertexBindCalls=0,vertexBindSkips=0;
 uint64_t descriptorLookups=0,descriptorCacheHits=0,descriptorAllocations=0,descriptorFastHits=0;
 struct CpuPreparationStats {
  uint64_t packCalls=0,packBytes=0,packCapacityGrowths=0,packCapacityGrowthBytes=0;
  uint64_t uniformSnapshotCalls=0,uniformSnapshotBytes=0;
  uint64_t samplerRequests=0,samplerMemoHits=0,samplerMapLookups=0;
  uint64_t uniformReuseChecks=0,uniformReuseComparisons=0,uniformReuseHits=0,uniformReuseBytes=0;
  uint64_t descriptorBindCandidates=0,descriptorWholeMatches=0;
  uint64_t descriptorBindCalls=0,descriptorSkippedSets=0;
  uint64_t pipelineLookups=0,pipelineLastHits=0,pipelineLookasideHits=0,pipelineMapLookups=0;
  std::array<uint64_t,2> descriptorStageMatches{};
 } cpuPreparation;
 uint64_t pipelineCreates=0,pipelineCreateNs=0;
 std::array<Surface*,8> passColors{};
 std::array<uint32_t,8> passSlices{};
 uint32_t mainDepthAddr=0;
 Surface* passDepth=nullptr;
 uint32_t passDepthSlice=0,passWidth=0,passHeight=0;
 bool passTracked=false;
 uint64_t renderPassCount=0;
 uint64_t frame=0,drawCount=0; std::atomic<uint64_t> completed{0};
 struct UploadBlock { Buffer buffer; VkDeviceSize used=0; };
 std::vector<UploadBlock> uploadBlocks;
 uint64_t uploadAllocations=0,uploadBytes=0;
 // Upload arena memory is HOST_CACHED|HOST_COHERENT (every block so far): CPU reads of it are as fast as
 // heap reads (Apple silicon/MoltenVK, many UMA drivers). uploadReadsDirect: the snapshot reuse caches
 // and the native index scan may read mapped upload slices instead of keeping CPU copies (auto: when
 // uploadCached; WWHD_VK_UPLOAD_READS=shadow|direct forces a mode). Set by allocate_upload.
 bool uploadCached=false,uploadReadsDirect=false;
 uint64_t vertexHistoryReuseChecks=0,vertexHistoryReuseHits=0,vertexHistoryReuseBytes=0;
 uint64_t vertexHistoryRequests=0,vertexHistoryMatches=0,vertexHistoryBytes=0;
 std::array<uint64_t,7> vertexHistoryDistances{};
 uint64_t vertexDeclaredBytes=0,vertexCopiedBytes=0;
 uint64_t vertexReuseChecks=0,vertexReuseHits=0,vertexReuseBytes=0,vertexReuseCompareNs=0;
 std::vector<std::function<void()>> completions;
 std::vector<Buffer> garbageBuffers;
 std::vector<bufcache::Region> garbageCacheRegions; // buffer cache regions replaced while recording
 struct RetiredImage { VkImage image;VkDeviceMemory memory;std::vector<VkImageView> views; }; std::vector<RetiredImage> garbageImages;
 // Each submission retains its pools, upload bytes and deferred objects until
 // its fence completes. The fields above alias the active recording slot.
 struct Submission {
  VkCommandPool commandPool=VK_NULL_HANDLE;
  VkCommandBuffer cmd=VK_NULL_HANDLE;
  VkFence fence=VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
  VkQueryPool timestampQueries=VK_NULL_HANDLE;
  bool timestampRecorded=false;
  uint64_t timestampFrame=0;
  static constexpr uint32_t maxGpuScopes=256;
  struct GpuScope { GpuScopeMetadata metadata{}; uint64_t draws=0,generation=0; bool ended=false; };
  std::array<GpuScope,maxGpuScopes> gpuScopes{};
  uint32_t gpuScopeCount=0,activeRenderScope=UINT32_MAX;
  bool pending=false;
  uint64_t serial=0; // Submission order on the single graphics queue.
  std::vector<UploadBlock> uploadBlocks;
  std::vector<std::function<void()>> completions;
  std::vector<Buffer> garbageBuffers;
  std::vector<RetiredImage> garbageImages;
  std::vector<bufcache::Region> garbageCacheRegions;
 };
 std::array<Submission,4> submissions{};
 size_t activeSubmission=0;
 Screen tv,drc;
 std::unordered_multimap<uint32_t,std::unique_ptr<Surface>> surfaces;
 std::vector<Surface*> linearTargets; // linear-aligned colour surfaces (never removed, like surfaces): GX2DrawDone write-back
};
extern Renderer R;
// Render-thread checkpoint; failures leave the cache dirty for a later retry.
void save_pipeline_cache();
// Only guest GX2Flush uses this asynchronous path. flush() remains a drain for
// readbacks and renderer tools; final presentation also waits synchronously.
void flush_async();
void reset_pipeline_lookup_cache();
uint64_t draw_batch_submissions();
void vk_check(VkResult result,const char* operation);
uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags properties);
// preferred: extra property flags used when a memory type has them (else the required ones only)
Buffer create_buffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags properties,
                     VkMemoryPropertyFlags preferred=0);
// host-visible TRANSFER_DST buffer the CPU reads back, host-cached where available
Buffer create_readback_buffer(VkDeviceSize size);
UploadSlice allocate_upload(VkDeviceSize size,VkDeviceSize alignment);
// Renderer smoke tests exercise the production snapshot helper with host data.
UploadSlice vertex_window_smoke_snapshot(uint32_t binding,uint32_t address,
    uint32_t reservation,uint32_t windowOffset,uint32_t windowLength,
    const void* data,bool poisonUnused);
void defer_buffer(Buffer buffer);
void defer_surface_image(VkImage image,VkDeviceMemory memory,std::vector<VkImageView> views);
VkCommandBuffer command_buffer();
void end_encoder();
void gpu_begin_render_scope(const std::array<Surface*,8>&,Surface*,const uint32_t*,uint32_t,uint32_t,uint32_t);
void gpu_count_render_draw();
GpuScopeToken gpu_begin_feedback_scope(const Surface&);
void gpu_end_feedback_scope(GpuScopeToken);
void transition_image(Surface*,VkImageLayout,VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VkAccessFlags access=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
void forget_texture_views();
void service_captures();
void request_tv_dump(const std::string&, int);
void create_surface_image(Surface*,bool forRendering,VkExtent3D explicitExtent = {});
void reset_ao_private_cache();
void destroy_surface_image(Surface*);
VkImageView layer_view(Surface*,uint32_t slice);
VkImageView sampled_texture_view(Surface*,const uint32_t* texWords);
Surface* find_or_create_surface(const SurfaceDesc&,bool forRendering);
Surface* color_target(const uint32_t*,int,uint32_t* slice=nullptr);
Surface* depth_target(const uint32_t*,uint32_t* slice=nullptr);
Surface* surface_from_color_buffer(uint32_t,uint32_t* firstSlice=nullptr,uint32_t* numSlices=nullptr);
Surface* surface_from_depth_buffer(uint32_t,uint32_t* firstSlice=nullptr,uint32_t* numSlices=nullptr);
Surface* sampled_texture(const uint32_t*,bool);
void upload_surface(Surface*);
void resample(Surface*,Surface*,uint32_t slices,float uMax=1,float vMax=1,uint32_t dstW=0,uint32_t dstH=0);
// scaled depth copies: blitted where the device can, else drawn (surfaces.cpp, issue #72);
// WWHD_VK_DEPTH_COPY=draw / =none override this for tests
enum class DepthCopyOverride { None, Draw, Unsupported };
extern DepthCopyOverride g_depthCopyOverride;
float res_scale();void set_res_scale(float);void latch_res_scale();
uint64_t next_write_seq();
inline void mark_gpu_written(Surface* s){s->gpuWritten=true;s->writeSeq=next_write_seq();}
}

namespace gfxvk { void reset_feedback_images(); } // Call before device teardown, then drain retirements.
