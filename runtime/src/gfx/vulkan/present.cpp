#ifdef __ANDROID__
#include "android_driver.h"
#endif
#include "pass.h"
#include "overlay/window_target.h"
#include "deferred_dispatch.h"
// Shader presentation matches Metal's source-grid FXAA and scaling filters.
#include "../../exception_report.h"
#include "present.h"
#include "stereo.h"
#include "../hdr.h"
#include "../headless_compose.h"
#include "imgui.h"
#include "backend.h"
#include "pipeline_trace.h"
#include "shaders.h"
#include "settings.h"
#include "mods/climb.h"
#include "mods/mods.h"  // MoveHud: the run/swim boost's bar (draw_mod_overlay)
#include "overlay/guest_hud.h"
#include "gfx/display_modes.h"
#include "platform/dual_display.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
namespace gfxvk {
namespace {
ImDrawData* overlayDraw=nullptr;
overlay::window_target::DrawFrame overlayFrame;
const char* vertexSource = R"glsl(#version 450
layout(location=0) out vec2 uv;
void main() {
 vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
 uv=p;
 gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)glsl";
const char* fragmentSource = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform Params { int aa; int conv; float sharp; float foot; float alpha; float hdrWhite; float hdrPeak; float hdrExposure; float hdrHighlights; int pq; int hdrSdrCurve; } params;
float luma(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
vec3 fxaa(vec2 at) {
 vec2 rcp=1.0/vec2(textureSize(image,0));
 vec3 nw=texture(image,at+vec2(-1,-1)*rcp).rgb, ne=texture(image,at+vec2(1,-1)*rcp).rgb;
 vec3 sw=texture(image,at+vec2(-1,1)*rcp).rgb, se=texture(image,at+vec2(1,1)*rcp).rgb;
 vec3 m=texture(image,at).rgb;
 float lnw=luma(nw),lne=luma(ne),lsw=luma(sw),lse=luma(se),lm=luma(m);
 float lmin=min(lm,min(min(lnw,lne),min(lsw,lse))),lmax=max(lm,max(max(lnw,lne),max(lsw,lse)));
 if(lmax-lmin<max(0.0312,lmax*0.125))return m;
 vec2 dir=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
 float reduce=max((lnw+lne+lsw+lse)*(0.25/8.0),1.0/128.0);
 dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-8.0),vec2(8.0))*rcp;
 vec3 a=0.5*(texture(image,at+dir*(1.0/3.0-0.5)).rgb+texture(image,at+dir*(2.0/3.0-0.5)).rgb);
 vec3 b=a*0.5+0.25*(texture(image,at-dir*0.5).rgb+texture(image,at+dir*0.5).rgb);
 float lb=luma(b);return (lb<lmin||lb>lmax)?a:b;
}
vec3 toLinear(vec3 c) {
 return mix(pow((c+0.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(0.04045)));
}
vec3 toSrgb(vec3 c) {
 return mix(1.055*pow(c,vec3(1.0/2.4))-0.055,c*12.92,lessThanEqual(c,vec3(0.0031308)));
}
void main() {
 vec2 size=vec2(textureSize(image,0)),at=uv;
 if(params.sharp>1.0) {
  vec2 t=at*size-0.5,i=floor(t),f=t-i;
  f=clamp((f-0.5)*params.sharp+0.5,0.0,1.0);at=(i+f+0.5)/size;
 }
 vec3 c;
 if(params.aa!=0)c=fxaa(at);
 else if(params.foot>1.25) {
  vec2 d=0.25*min(params.foot,4.0)/size;
  c=0.25*(texture(image,at+vec2(-d.x,-d.y)).rgb+texture(image,at+vec2(d.x,-d.y)).rgb+
          texture(image,at+vec2(-d.x,d.y)).rgb+texture(image,at+vec2(d.x,d.y)).rgb);
 }else c=texture(image,at).rgb;
 if(params.conv==1) {
  if(params.hdrWhite>0.0 && params.hdrSdrCurve!=2)
   c=pow(max(c,vec3(0.0)),vec3(params.hdrSdrCurve==1?2.4:2.2));
  else c=toLinear(clamp(c,0.0,1.0));
 }
 else if(params.conv==2)c=toSrgb(clamp(c,0.0,1.0));
 else if(params.hdrWhite>0.0 && params.hdrSdrCurve!=2) {
  // sRGB texture views (and linear scan buffers) already decoded the SDR
  // picture. Reconstruct its code values before applying the selected curve.
  c=pow(max(toSrgb(max(c,vec3(0.0))),vec3(0.0)),vec3(params.hdrSdrCurve==1?2.4:2.2));
 }
 if(params.hdrWhite>0.0) {
  c=max(c,vec3(0.0))*exp2(params.hdrExposure);
  float m=max(c.r,max(c.g,c.b));
  if(m>0.0) {
   float t=clamp((m-0.5)*2.0,0.0,1.0);
   float n=m*params.hdrWhite+(params.hdrPeak-params.hdrWhite)*params.hdrHighlights*t*t*(3.0-2.0*t);
   float knee=min(params.hdrWhite,params.hdrPeak*0.75);
   if(n>knee)n=knee+(params.hdrPeak-knee)*(1.0-exp(-(n-knee)/(params.hdrPeak-knee)));
   c*=n/(m*80.0);
  }
 }
 if(params.pq!=0) {
  // HDR10 (hdr::encode_pq): scRGB BT.709 -> BT.2020, ST 2084 of cd/m^2 / 10000
  const mat3 bt2020=mat3(0.6274040,0.0690970,0.0163916,0.3292820,0.9195400,0.0880132,0.0433136,0.0113612,0.8955950);
  vec3 y=pow(clamp(bt2020*c*(80.0/10000.0),0.0,1.0),vec3(0.1593017578125));
  c=pow((0.8359375+18.8515625*y)/(1.0+18.6875*y),vec3(78.84375));
 }
 result=vec4(c,params.alpha);
}
)glsl";
// a filled rectangle (GamePad overlay frame)
const char* solidSource = R"glsl(#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform Params { vec4 color; } params;
void main() { result=params.color; }
)glsl";
// stamina wheel of the "climb any wall" mod (as mods/mod_hud.mm), premultiplied
const char* hudSource = R"glsl(#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform Params { float stamina; float alpha; float exhausted; float pad; } u;
void main() {
 vec2 p=vec2(uv.x*2.0-1.0,1.0-uv.y*2.0);
 float r=length(p);
 float aa=max(fwidth(r),1e-4);
 float ring=smoothstep(0.58-aa,0.58+aa,r)*(1.0-smoothstep(0.92-aa,0.92+aa,r));
 float outline=smoothstep(0.50-aa,0.50+aa,r)*(1.0-smoothstep(1.0-2.0*aa,1.0,r));
 float ang=atan(p.x,p.y);
 float frac=(ang<0.0?ang+6.28318530718:ang)/6.28318530718;
 float filled=1.0-smoothstep(u.stamina-0.004,u.stamina+0.004,frac);
 vec3 green=vec3(0.30,0.90,0.35),yellow=vec3(1.0,0.80,0.15),red=vec3(0.95,0.20,0.15);
 vec3 full=u.exhausted>0.5?red:(u.stamina<0.3?mix(red,yellow,u.stamina/0.3):green);
 vec3 empty=u.exhausted>0.5?vec3(0.55,0.08,0.06):vec3(0.10,0.10,0.10);
 vec3 col=mix(empty,full,filled);
 float fa=ring*mix(0.45,0.95,filled);
 float oa=outline*0.45;
 float a=fa+oa*(1.0-fa);
 result=vec4(col*fa,a)*u.alpha;
}
)glsl";
// the run/swim boost's ring (runtime/src/mods/mods.cpp move_hud), BotW style like the climb wheel,
// premultiplied
const char* moveHudSource = R"glsl(#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform Params { float stamina; float alpha; float state; float pad; } u;
void main() {
 vec2 p=vec2(uv.x*2.0-1.0,1.0-uv.y*2.0);
 float r=length(p);
 float aa=max(fwidth(r),1e-4);
 float ring=smoothstep(0.58-aa,0.58+aa,r)*(1.0-smoothstep(0.92-aa,0.92+aa,r));
 float outline=smoothstep(0.50-aa,0.50+aa,r)*(1.0-smoothstep(1.0-2.0*aa,1.0,r));
 float ang=atan(p.x,p.y);
 float frac=(ang<0.0?ang+6.28318530718:ang)/6.28318530718;
 float filled=1.0-smoothstep(u.stamina-0.004,u.stamina+0.004,frac);
 // 0 running (green), 1 swimming (blue), 2 recharging (amber); the colour runs to red when low
 vec3 green=vec3(0.32,0.88,0.38),blue=vec3(0.30,0.62,0.95),amber=vec3(0.95,0.62,0.18);
 vec3 full=mix(vec3(0.95,0.20,0.15),u.state>1.5?amber:u.state>0.5?blue:green,min(1.0,u.stamina/0.3));
 vec3 empty=u.state>1.5?vec3(0.38,0.24,0.07):vec3(0.10,0.10,0.10);
 vec3 col=mix(empty,full,filled);
 float fa=ring*mix(0.45,0.95,filled);
 float oa=outline*0.45;
 float a=fa+oa*(1.0-fa);
 result=vec4(col*fa,a)*u.alpha;
}
)glsl";
struct PresentRect { float x,y,width,height,scale; };
using PresentParams=PresentImageParams;
static_assert(sizeof(PresentParams)==48);
PresentRect present_rect(VkExtent3D source,VkExtent2D target,int filter) {
 float scale=std::min(float(target.width)/source.width,float(target.height)/source.height);
 if(filter==2&&scale>=1)scale=std::floor(scale+1e-3f);
 const float width=source.width*scale,height=source.height*scale;
 return {(target.width-width)*0.5f,(target.height-height)*0.5f,width,height,scale};
}
PresentParams present_params(bool aa,int filter,float scale,bool sourceLinear,bool targetLinear) {
 return {aa?1:0,sourceLinear==targetLinear?0:targetLinear?1:2,
  filter==0||scale<=1?1:filter==1?scale:1e4f,scale<1?1/scale:1};
}
// pipeline kinds: the scaled picture (opaque / with opacity), a filled rectangle, the mod HUD
enum Kind : uint32_t { kImage, kImageBlend, kSolid, kHud, kMoveHud };
struct ScreenResources {
 bool drawable=false,captureTransfer=false; std::vector<VkImageView> views;
 Surface hdrTarget{}; // HDR10 output: the linear FP16 composition before PQ encoding
};
struct PresentCapture {
 std::string path;
 uint64_t frame=0;
 bool enabled=false,done=false;
 Buffer buffer{};
 VkExtent2D extent{};
 VkFormat format=VK_FORMAT_UNDEFINED;
};
PresentCapture& present_capture() {
 static PresentCapture capture=[] {
  PresentCapture result;
  const char* path=std::getenv("WWHD_PRESENT_CAPTURE_PATH");
  const char* frame=std::getenv("WWHD_PRESENT_CAPTURE_FRAME");
  if(!path||!*path||!frame||!*frame)return result;
  uint64_t value=0;
  for(const char* p=frame;*p;++p) {
   if(*p<'0'||*p>'9')return result;
   const uint64_t digit=uint64_t(*p-'0');
   if(value>(UINT64_MAX-digit)/10)return result;
   value=value*10+digit;
  }
  result.path=path;result.frame=value;result.enabled=true;return result;
 }();
 return capture;
}
// Each logical device owns its dispatch table. Loading a second presentation
// device must never replace the renderer's global device entry points.
struct PresentApi {
#define PRESENT_FUNCTIONS(X) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
 X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
 X(vkCreateSampler) X(vkDestroySampler) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
 X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdPipelineBarrier) \
 X(vkCmdSetScissor) X(vkCmdSetViewport) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
 X(vkCmdPushConstants) X(vkCmdDraw)
#ifdef __ANDROID__
#define FIELD(name) drivers::ObservedFunction<PFN_##name> name;
#else
#define FIELD(name) PFN_##name name=nullptr;
#endif
 PRESENT_FUNCTIONS(FIELD)
#undef FIELD
 PassApi passes;
 PFN_vkCmdBeginRendering vkCmdBeginRendering=nullptr;
 PFN_vkCmdEndRendering vkCmdEndRendering=nullptr;
 void init(VkDevice device,bool khr,bool renderPasses) {
#define LOAD(name) name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name)); \
  if(!name)throw std::runtime_error("Missing presentation function " #name);
  PRESENT_FUNCTIONS(LOAD)
#undef LOAD
  if(renderPasses) {
#define LOAD_PASS(field,name) passes.field=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name)); \
   if(!passes.field)throw std::runtime_error("Missing presentation function " #name);
   LOAD_PASS(createRenderPass,vkCreateRenderPass)
   LOAD_PASS(destroyRenderPass,vkDestroyRenderPass)
   LOAD_PASS(createFramebuffer,vkCreateFramebuffer)
   LOAD_PASS(destroyFramebuffer,vkDestroyFramebuffer)
   LOAD_PASS(beginRenderPass,vkCmdBeginRenderPass)
   LOAD_PASS(endRenderPass,vkCmdEndRenderPass)
#undef LOAD_PASS
   return;
  }
  vkCmdBeginRendering=reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(device,khr?"vkCmdBeginRenderingKHR":"vkCmdBeginRendering"));
  vkCmdEndRendering=reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(device,khr?"vkCmdEndRenderingKHR":"vkCmdEndRendering"));
  if(!vkCmdBeginRendering || !vkCmdEndRendering)throw std::runtime_error("Missing presentation dynamic rendering functions");
 }
#undef PRESENT_FUNCTIONS
};
struct PresentResources {
 PresentApi api;
 std::unique_ptr<PassCaches> ownedPasses;
 PassCaches* passes=nullptr;
 VkDevice device=VK_NULL_HANDLE;
 VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
 VkPipelineLayout layout=VK_NULL_HANDLE;
 VkSampler linear=VK_NULL_HANDLE,nearest=VK_NULL_HANDLE;
 std::unordered_map<uint64_t,VkPipeline> pipelines; // (format, kind)
 std::unordered_map<Screen*,ScreenResources> screens;
 Surface diagnosticTarget{};
 gfx::headless_compose::Counter diagnosticCounter;
};
PresentResources resources;
bool srgb_format(VkFormat format) {
 return format==VK_FORMAT_R8G8B8A8_SRGB || format==VK_FORMAT_B8G8R8A8_SRGB ||
        format==VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}
VkShaderModule module(PresentResources& resources,const char* source,bool vertex) {
 std::string error;auto words=vk::compile_glsl(source,vertex,&error);
 if(words.empty()||!error.empty())exception_report::raise("Vulkan presentation shader: "+error);
 VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4;ci.pCode=words.data();
 VkShaderModule result;vk_check(resources.api.vkCreateShaderModule(resources.device,&ci,nullptr,&result),"presentation shader");return result;
}
void ensure_resources(PresentResources& resources,VkDevice device,bool khr,bool renderPasses=false) {
 if(resources.device && resources.device!=device)
  exception_report::raise("Vulkan presentation resources must be reset before device replacement");
 if(!resources.device) {
  resources.api.init(device,khr,renderPasses);resources.device=device;
  if(renderPasses) {
   if(device==R.device)resources.passes=&pass_caches();
   else {
    resources.ownedPasses=std::make_unique<PassCaches>(device,resources.api.passes);
    resources.passes=resources.ownedPasses.get();
   }
  }
 }
 if(!resources.descriptors) {
  VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.bindingCount=1;ci.pBindings=&binding;
  vk_check(resources.api.vkCreateDescriptorSetLayout(resources.device,&ci,nullptr,&resources.descriptors),"presentation descriptors");
 }
 if(!resources.layout) {
  VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(PresentParams)};
  VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=1;ci.pSetLayouts=&resources.descriptors;ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
  vk_check(resources.api.vkCreatePipelineLayout(resources.device,&ci,nullptr,&resources.layout),"presentation layout");
 }
 for(auto filter:{VK_FILTER_LINEAR,VK_FILTER_NEAREST}) {
  auto& sampler=filter==VK_FILTER_LINEAR?resources.linear:resources.nearest;
  if(sampler)continue;
  VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.minFilter=ci.magFilter=filter;ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
  ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;ci.maxLod=0;
  vk_check(resources.api.vkCreateSampler(resources.device,&ci,nullptr,&sampler),"presentation sampler");
 }
}
VkPipeline pipeline(PresentResources& resources,VkFormat format,Kind kind=kImage) {
 const uint64_t key=uint64_t(format)<<8|kind;
 if(auto it=resources.pipelines.find(key);it!=resources.pipelines.end())return it->second;
 VkShaderModule vs=VK_NULL_HANDLE,fs=VK_NULL_HANDLE;VkPipeline result=VK_NULL_HANDLE;
 try {
  vs=module(resources,vertexSource,true);fs=module(resources,kind==kSolid?solidSource:kind==kHud?hudSource:kind==kMoveHud?moveHudSource:fragmentSource,false);
  VkPipelineShaderStageCreateInfo stages[2]{};
  for(int i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=i?fs:vs;stages[i].pName="main";}
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
  if(kind==kImageBlend||kind==kSolid) {
   // overlay opacity: source alpha over the picture underneath
   blend.blendEnable=VK_TRUE;blend.colorBlendOp=blend.alphaBlendOp=VK_BLEND_OP_ADD;
   blend.srcColorBlendFactor=blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
   blend.dstColorBlendFactor=blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  } else if(kind==kHud||kind==kMoveHud) {
   // premultiplied colour; the image keeps its alpha
   blend.blendEnable=VK_TRUE;blend.colorBlendOp=blend.alphaBlendOp=VK_BLEND_OP_ADD;
   blend.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
  }
  VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};bs.attachmentCount=1;bs.pAttachments=&blend;
  VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ds.dynamicStateCount=2;ds.pDynamicStates=states;
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&format;
  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.pNext=&rendering;
  if(resources.passes) { ci.pNext=nullptr;ci.renderPass=resources.passes->render_pass(pass_formats(rendering),VK_ATTACHMENT_LOAD_OP_LOAD); }
  ci.stageCount=2;ci.pStages=stages;ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vp;ci.pRasterizationState=&rs;ci.pMultisampleState=&ms;ci.pColorBlendState=&bs;ci.pDynamicState=&ds;ci.layout=resources.layout;
  const auto started=std::chrono::steady_clock::now();
  VkResult compiled;
  if(resources.device==R.device)
    compiled=stutter::create_graphics(R.frame,"present_pipeline",resources.device,R.pipelineCache,1,&ci,nullptr,&result);
  else {
    // A separate presentation device loads its own entry points: same driver, same serial rule.
    const auto serial=pipeline_serial_lock();
    compiled=resources.api.vkCreateGraphicsPipelines(resources.device,VK_NULL_HANDLE,1,&ci,nullptr,&result);
  }
  vk_check(compiled,"presentation pipeline");
  if(resources.device==R.device) R.pipelineCreateNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
  resources.pipelines.emplace(key,result);
  if(resources.device==R.device) { ++R.pipelineCreates;R.pipelineCacheDirty=true;R.pipelineCacheChangedFrame=R.frame; }
 }catch(...){if(result)resources.api.vkDestroyPipeline(resources.device,result,nullptr);if(vs)resources.api.vkDestroyShaderModule(resources.device,vs,nullptr);if(fs)resources.api.vkDestroyShaderModule(resources.device,fs,nullptr);throw;}
 resources.api.vkDestroyShaderModule(resources.device,vs,nullptr);resources.api.vkDestroyShaderModule(resources.device,fs,nullptr);return result;
}
void ensure_resources() { ensure_resources(resources,R.device,R.dynamicRenderingKHR,R.renderPasses); }
VkPipeline pipeline(VkFormat format,Kind kind=kImage) {
 ensure_resources();return pipeline(resources,format,kind);
}
void destroy_resources(PresentResources& resources) {
 for(auto [key,p]:resources.pipelines)resources.api.vkDestroyPipeline(resources.device,p,nullptr);
 if(resources.linear)resources.api.vkDestroySampler(resources.device,resources.linear,nullptr);
 if(resources.nearest)resources.api.vkDestroySampler(resources.device,resources.nearest,nullptr);
 if(resources.layout)resources.api.vkDestroyPipelineLayout(resources.device,resources.layout,nullptr);
 if(resources.descriptors)resources.api.vkDestroyDescriptorSetLayout(resources.device,resources.descriptors,nullptr);
 resources={};
}

}
std::vector<VkImageView> take_present_screen_views(Screen& screen) {
 auto found=resources.screens.find(&screen);if(found==resources.screens.end())return {};
 if(found->second.hdrTarget.image)destroy_surface_image(&found->second.hdrTarget);
 auto views=std::move(found->second.views);resources.screens.erase(found);return views;
}
void reset_present_screen(Screen& screen) {
 auto views=take_present_screen_views(screen);
 forget_framebuffers(views.data(),views.size(),false);
 for(auto view:views)vkDestroyImageView(resources.device,view,nullptr);
}
void prepare_present_screen(Screen& screen,bool colorAttachmentSupported,bool captureTransferSupported) {
 ensure_resources();reset_present_screen(screen);auto& state=resources.screens[&screen];state.drawable=colorAttachmentSupported;state.captureTransfer=captureTransferSupported;
 // Capability publication happens at draw time, after a guest scan image
 // exists, and refreshes when the guest replaces that image's format.
 if(!state.drawable)return;
 state.views.reserve(screen.images.size());
 try {
  for(auto image:screen.images) {
   VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=image;ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=screen.swapFormat;ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   VkImageView view;vk_check(vkCreateImageView(R.device,&ci,nullptr,&view),"presentation image view");state.views.push_back(view);
  }
 }catch(...){reset_present_screen(screen);throw;}
}
void reset_present_resources() {
 if(present_capture().buffer.buffer) { defer_buffer(present_capture().buffer);present_capture().buffer={}; }
 // Caller has drained the device; command/descriptor pools must not execute
 // references to these process-lifetime presentation objects afterwards.
 for(auto& [screen,state]:resources.screens) {
  forget_framebuffers(state.views.data(),state.views.size(),false);
  for(auto view:state.views)vkDestroyImageView(resources.device,view,nullptr);
  if(state.hdrTarget.image)destroy_surface_image(&state.hdrTarget);
 }
 if(resources.diagnosticTarget.image)destroy_surface_image(&resources.diagnosticTarget);
 destroy_resources(resources);
 reset_overlay_resources();
}
// ---------------------------------------------------------------- composition
namespace {
const gfx::PresentPlan* currentPlan=nullptr;
bool linear_filtering(VkFormat format) {
 const auto& properties=format_properties(format);
 return properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
}
bool sampleable(const Surface& source) {
 const auto& properties=format_properties(source.fmt.pixel);
 return (properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) && (source.usage&VK_IMAGE_USAGE_SAMPLED_BIT);
}
// draw the quads into `view` (cleared to black first); `layout` is the target image's current layout
void compose(VkImage image,VkImageView view,VkImageLayout& layout,VkExtent2D extent,VkFormat format,
             const std::vector<ComposeQuad>& quads,VkImageLayout finalLayout,int filter,bool fxaa,ImDrawData* overlay=nullptr,ResourceUse* use=nullptr,bool hdrOutput=false,
             bool pqEncode=false) {
 end_encoder();
 ImDrawData* gamepadOverlay=nullptr;
 // The GamePad scan may also be shown inside the TV composition (PIP or Off-TV).
 if(R.tv.scan&&R.drc.scan&&!quads.empty()&&quads.front().image==R.tv.scan.get())for(const auto& q:quads)
  if(q.image==R.drc.scan.get()) {
   gamepadOverlay=overlay::guesthud::gamepad_region(float(extent.width),float(extent.height),q.box.x,q.box.y,q.box.w,q.box.h,q.alpha);
   break;
  }
 if(gamepadOverlay)overlay_prepare(gamepadOverlay);
 if(overlay)overlay_prepare(overlay);
 for(auto& q:quads)
  if(q.image)transition_image(q.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
 ResourceUse initial;
 if(!use) use=&initial; // acquired swap image: acquire semaphore owns prior presentation
 auto dependency=derive_dependency(*use,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
 auto cmd=command_buffer();VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=layout;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 barrier.srcAccessMask=narrow_barriers()?dependency.sourceAccess:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
 record::vkCmdPipelineBarrier(cmd,narrow_barriers()?dependency.source:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};attachment.imageView=view;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 attachment.clearValue.color.float32[3]=1;
 VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea.extent=extent;rendering.layerCount=1;rendering.colorAttachmentCount=1;rendering.pColorAttachments=&attachment;
 layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 begin_pass(cmd,rendering,[&] { return color_pass(format,layout); });R.rendering=true;R.passTracked=false;
 VkRect2D scissor{{0,0},extent};record::vkCmdSetScissor(cmd,0,1,&scissor);
 const bool targetLinear=srgb_format(format)||hdrOutput||pqEncode;
 const float whiteScale=hdrOutput?std::min(hdr::menuWhite.load(),hdr::parameters().peak)/80.0f:1.0f;
 for(auto& q:quads) {
  if(q.box.w<=0||q.box.h<=0)continue;
  VkViewport viewport{q.box.x,q.box.y,q.box.w,q.box.h,0,1};record::vkCmdSetViewport(cmd,0,1,&viewport);
  if(q.solid) {
   record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(format,kSolid));
   PresentParams params{};float color[4];memcpy(color,q.color,16);
   if(hdrOutput)for(int i=0;i<3;++i)color[i]*=whiteScale;
   memcpy(&params,color,16);
   record::vkCmdPushConstants(cmd,resources.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);record::vkCmdDraw(cmd,3,1,0,0);
   continue;
  }
  if(!q.image||!q.image->view)continue;
  const bool blend=q.alpha<0.999f;
  record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(format,blend?kImageBlend:kImage));
  VkDescriptorSet set;VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=R.descriptorPool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&resources.descriptors;
  vk_check(vkAllocateDescriptorSets(R.device,&allocation,&set),"presentation descriptor set");
  const bool linear=linear_filtering(q.image->fmt.pixel);
  VkDescriptorImageInfo info{linear?resources.linear:resources.nearest,q.image->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&info;
  vkUpdateDescriptorSets(R.device,1,&write,0,nullptr);record::vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,resources.layout,0,1,&set,0,nullptr);
  // target pixels per source texel, as the Metal composition (display.mm draw_image)
  const float scale=std::min(q.box.w/q.image->extent.width,q.box.h/q.image->extent.height);
  // sRGB texture views decode on sampling; the scan flag also identifies linear
  // scan values held in a UNORM image. sRGB targets encode shader linear output.
  const bool sourceLinear=q.sourceLinear||srgb_format(q.image->fmt.pixel);
  // dynamic resolution (dynres.h): a picture below the internal resolution is always scaled filtered.
  // Integer scaling would show its pixels as blocks (it can't be a whole multiple any more): smooth;
  // sharp scaling takes its sharpness from the picture's full size, so the extra upscale is bilinear.
  int qfilter=filter;float qscale=scale;
  if(q.image->dyn<1.0f) {
   if(qfilter==2)qfilter=0;
   else if(qfilter==1)qscale=std::min(q.box.w/nominal_width(q.image),q.box.h/nominal_height(q.image));
  }
  auto params=present_params(fxaa&&linear,qfilter,qscale,sourceLinear,targetLinear);params.alpha=q.alpha;
  if(hdrOutput) {
   const auto h=hdr::parameters();params.hdrWhite=h.paper;params.hdrPeak=h.peak;
   params.hdrExposure=h.exposure;params.hdrHighlights=h.highlights;
   params.hdrSdrCurve=hdr::sdrCurve.load();
  }
  params.pq=pqEncode;
  record::vkCmdPushConstants(cmd,resources.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);record::vkCmdDraw(cmd,3,1,0,0);
 }
 if(gamepadOverlay)overlay_draw(gamepadOverlay,cmd,format,extent,targetLinear,-1,whiteScale);
 if(overlay) {
  if (overlay == overlayDraw && overlayFrame.stereo_packed(stereo::frame.mode != stereo::Off, bool(stereoRight.scan))) {
   overlay_draw(overlay,cmd,format,extent,targetLinear,0,whiteScale);
   overlay_draw(overlay,cmd,format,extent,targetLinear,1,whiteScale);
  } else overlay_draw(overlay,cmd,format,extent,targetLinear,-1,whiteScale);
 }
 end_pass(cmd);
 barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=finalLayout;barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=0;
 record::vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);layout=finalLayout;
 R.rendering=false;R.passTracked=false;
}
}  // namespace

void set_present_plan(const gfx::PresentPlan* plan) { currentPlan=plan; }
void set_overlay_draw(overlay::window_target::DrawFrame frame) { overlayDraw=frame.draw; overlayFrame=frame; }
static ImDrawData* screen_overlay(Screen& screen, uint32_t width, uint32_t height) {
 if(auto* draw=overlayFrame.for_window(&screen==&R.drc))return draw;
 return &screen==&R.tv?nullptr:overlay::guesthud::gamepad_frame(width,height);
}

// Exercise the actual presentation shader and read FP32 values so SDR clipping
// or a missing HDR push constant cannot pass a CPU-only colour test.
void test_hdr_presentation() {
 Surface source,target,pqTarget,linearTarget,sdrTarget;Buffer readback;
 const int savedCurve=hdr::sdrCurve.load();
 auto init=[](Surface& s,uint32_t f) {
  s.width=s.height=s.pitch=4;s.slices=s.mips=1;s.dim=1;s.format=f;s.fmt=format_info(f,false);
  create_surface_image(&s,true,VkExtent3D{4,4,1});
 };
 try {
  init(source,0x823);init(target,0x823);init(pqTarget,0x823);init(linearTarget,0x820);init(sdrTarget,0x41a);readback=create_readback_buffer(4*4*16);
  const auto hp=hdr::parameters();
  // An SDR sRGB attachment also requests conv==1. Its decode must remain
  // piecewise sRGB regardless of the saved HDR curve (the attachment encodes it).
  for(auto c:{std::array<float,3>{0,0,0},{.05f,.05f,.05f},{.5f,.5f,.5f},{1,1,1},{.05f,.4f,1}}) {
   std::array<uint8_t,64> baseline{};
   for(int curve=0;curve<3;++curve) {
    hdr::sdrCurve=curve;
    transition_image(&source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue value{{c[0],c[1],c[2],1}};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    record::vkCmdClearColorImage(command_buffer(),source.image,source.layout,&value,1,&range);
    mark_gpu_written(&source);
    ComposeQuad q;q.image=&source;q.sourceLinear=false;q.box={0,0,4,4};
    compose(sdrTarget.image,sdrTarget.view,sdrTarget.layout,{4,4},sdrTarget.fmt.pixel,{q},VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,false,nullptr,&sdrTarget.use);
    transition_image(&sdrTarget,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    transition_buffer(readback,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={4,4,1};
    record::vkCmdCopyImageToBuffer(command_buffer(),sdrTarget.image,sdrTarget.layout,readback.buffer,1,&copy);
    transition_buffer(readback,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);flush();
    const auto* pixels=static_cast<const uint8_t*>(readback.mapped);
    for(size_t i=0;i<baseline.size();++i) {
     if(curve==0)baseline[i]=pixels[i];
     else if(pixels[i]!=baseline[i])exception_report::raise("HDR curve changed SDR sRGB output bytes");
     if(i%4<3 && std::abs(int(pixels[i])-int(std::lround(c[i%4]*255)))>1)
      exception_report::raise("SDR sRGB presentation did not preserve encoded input");
    }
   }
  }
  for(bool sourceLinear:{false,true})for(int curve=0;curve<3;++curve)for(bool pq:{false,true})
  for(auto c:{std::array<float,3>{0,0,0},{0.05f,0.05f,0.05f},{0.5f,0.5f,0.5f},{1,1,1},{0.05f,0.4f,1},{1.2f,0.4f,0.05f}}) {
   if(sourceLinear && c[0]>1)continue; // sRGB scans contain bounded encoded SDR
   hdr::sdrCurve=curve;
   transition_image(&source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   const auto sampled=sourceLinear?hdr::decode(c,hdr::SdrCurve::Srgb):c;
   VkClearColorValue value{{sampled[0],sampled[1],sampled[2],1}};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   record::vkCmdClearColorImage(command_buffer(),source.image,source.layout,&value,1,&range);
   mark_gpu_written(&source);
   ComposeQuad q;q.image=&source;q.sourceLinear=sourceLinear;q.box={0,0,4,4};
   auto& mapped=pq?linearTarget:target;
   compose(mapped.image,mapped.view,mapped.layout,{4,4},mapped.fmt.pixel,{q},VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,false,nullptr,&mapped.use,true);
   Surface* output=&mapped;
   if(pq) {
    ComposeQuad encoded;encoded.image=&mapped;encoded.sourceLinear=true;encoded.box={0,0,4,4};
    compose(pqTarget.image,pqTarget.view,pqTarget.layout,{4,4},pqTarget.fmt.pixel,{encoded},VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,false,nullptr,&pqTarget.use,false,true);
    output=&pqTarget;
   }
   transition_image(output,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   transition_buffer(readback,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={4,4,1};
   record::vkCmdCopyImageToBuffer(command_buffer(),output->image,output->layout,readback.buffer,1,&copy);
   transition_buffer(readback,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);flush();
   auto expected=hdr::map(hdr::decode(c,static_cast<hdr::SdrCurve>(curve)),hp);
   if(pq)expected=hdr::encode_pq(expected);
   auto* pixels=static_cast<const float*>(readback.mapped);
   if(!sourceLinear && !pq && c[0]==c[1] && (c[0]==0.05f || c[0]==0.5f))
    fprintf(stderr,"[renderer smoke] SDR curve=%d code=%.2f scRGB=%.9f\n",curve,c[0],pixels[0]);
   for(int i=0;i<16;++i)for(int k=0;k<3;++k)
    if(!std::isfinite(pixels[i*4+k]) || std::abs(pixels[i*4+k]-expected[k])>(pq?0.0002f:0.00002f))
     exception_report::raise("HDR shader readback differs from calibrated scRGB mapping");
  }
  // HDR10: the final pass encodes linear scRGB (0, 80, 1000 and 10000+ nits, a hue) to BT.2020 PQ.
  for(auto c:{std::array<float,3>{0,0,0},{1,1,1},{12.5f,12.5f,12.5f},{200,200,200},{0.05f,0.4f,1}}) {
   transition_image(&source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   VkClearColorValue value{{c[0],c[1],c[2],1}};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   record::vkCmdClearColorImage(command_buffer(),source.image,source.layout,&value,1,&range);
   mark_gpu_written(&source);
   ComposeQuad q;q.image=&source;q.sourceLinear=true;q.box={0,0,4,4};
   compose(target.image,target.view,target.layout,{4,4},target.fmt.pixel,{q},VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,false,nullptr,&target.use,false,true);
   transition_image(&target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   transition_buffer(readback,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={4,4,1};
   record::vkCmdCopyImageToBuffer(command_buffer(),target.image,target.layout,readback.buffer,1,&copy);
   transition_buffer(readback,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);flush();
   auto expected=hdr::encode_pq(c);auto* pixels=static_cast<const float*>(readback.mapped);
   for(int i=0;i<16;++i)for(int k=0;k<3;++k)
    if(!std::isfinite(pixels[i*4+k]) || std::abs(pixels[i*4+k]-expected[k])>0.002f)
     exception_report::raise("HDR10 shader readback differs from the PQ encoding");
  }
  fprintf(stderr,"[renderer smoke] HDR three SDR curves: black/near-black/grey/white/hue through scRGB and two-pass HDR10 PQ readback passed\n");
 } catch(...) {
  hdr::sdrCurve=savedCurve;
  if(pqTarget.image)destroy_surface_image(&pqTarget);
  if(linearTarget.image)destroy_surface_image(&linearTarget);
  if(sdrTarget.image)destroy_surface_image(&sdrTarget);
  if(readback.buffer)defer_buffer(readback);
  if(source.image)destroy_surface_image(&source);if(target.image)destroy_surface_image(&target);throw;
 }
 hdr::sdrCurve=savedCurve;
 defer_buffer(readback);destroy_surface_image(&source);destroy_surface_image(&target);destroy_surface_image(&pqTarget);destroy_surface_image(&linearTarget);destroy_surface_image(&sdrTarget);
}

// WWHD_HDR_CURVE_DUMP accompanies a scheduled scan dump. This offscreen path
// works without an HDR display and never changes the swapchain or saved setting.
void dump_hdr_curves(Screen& screen,const std::string& path) {
 if(!screen.scan || !screen.scan->image)return;
 Surface target;Buffer readback;
 const int savedCurve=hdr::sdrCurve.load();
 try {
  const auto scanExtent=scan_display_extent(screen);
  const VkExtent2D extent{scanExtent.width,scanExtent.height};
  target.width=target.pitch=extent.width;target.height=extent.height;
  target.slices=target.mips=1;target.dim=1;target.format=0x823;target.fmt=format_info(0x823,false);
  create_surface_image(&target,true,VkExtent3D{extent.width,extent.height,1});
  const size_t bytes=size_t(extent.width)*extent.height*4*sizeof(float);
  readback=create_readback_buffer(bytes);
  ComposeQuad q;q.image=screen.scan.get();q.sourceLinear=screen.scanSrgb;
  q.box={0,0,float(extent.width),float(extent.height)};
  const char* names[]={"gamma22","gamma24","srgb"};
  for(int curve=0;curve<3;++curve) {
   hdr::sdrCurve=curve;
   compose(target.image,target.view,target.layout,extent,target.fmt.pixel,{q},
           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,false,nullptr,&target.use,true);
   transition_image(&target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   transition_buffer(readback,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
   copy.imageExtent={extent.width,extent.height,1};
   record::vkCmdCopyImageToBuffer(command_buffer(),target.image,target.layout,readback.buffer,1,&copy);
   transition_buffer(readback,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);flush();
   const auto filename=path+"."+names[curve]+".rgba32f";
   std::ofstream output(filename,std::ios::binary);
   output.write(static_cast<const char*>(readback.mapped),bytes);
   output.close();
   if(!output)exception_report::raise("Cannot save HDR curve diagnostic");
   fprintf(stderr,"[HDR curve dump] %s: %ux%u RGBA32F scRGB (1 = 80 nits)\n",filename.c_str(),extent.width,extent.height);
  }
 } catch(...) {
  hdr::sdrCurve=savedCurve;
  if(readback.buffer)defer_buffer(readback);
  if(target.image)destroy_surface_image(&target);
  throw;
 }
 hdr::sdrCurve=savedCurve;
 defer_buffer(readback);destroy_surface_image(&target);
}

Screen& presentation_source_screen(Screen& physical) {
#ifdef __ANDROID__
 int index=dual_display::source_index(&physical==&R.drc ? 1 : 0,
                                     gfx::g_has_drc_window,dual_display::active_swap);
 return index==1 ? R.drc : R.tv;
#else
 return physical;
#endif
}

std::vector<ComposeQuad> screen_quads_mono(Screen& screen,VkExtent2D target,int& filter) {
 std::vector<ComposeQuad> quads;
 filter=scale_filter();
 Screen& source=presentation_source_screen(screen);
 if(!source.scan||!source.scan->image)return quads;
#ifdef __ANDROID__
 if(gfx::g_has_drc_window) {
  ComposeQuad q;q.image=source.scan.get();q.sourceLinear=source.scanSrgb;
  const auto display=scan_display_extent(source);
  q.box=gfx::display_layout(float(target.width),float(target.height),float(display.width),float(display.height));
  quads.push_back(q);return quads;
 }
#endif
 if(currentPlan) {
  // both window hosts (gfx/display_modes.cpp): the same layout as the Metal renderer
  filter=currentPlan->filter;
  if(&screen==&R.drc) {
   ComposeQuad q;q.image=R.drc.scan.get();q.sourceLinear=R.drc.scanSrgb;
   q.box=gfx::display_layout(float(target.width),float(target.height),nominal_width(q.image),nominal_height(q.image));
   quads.push_back(q);return quads;
  }
  gfx::PresentPlan p=*currentPlan;
  Surface* drc=R.drc.scan&&R.drc.scan->image?R.drc.scan.get():nullptr;
  if(p.dw!=float(target.width)||p.dh!=float(target.height))
   p=gfx::display_plan_for(p,float(target.width),float(target.height),float(scan_display_extent(screen).width),float(scan_display_extent(screen).height),
                          nominal_width(drc),nominal_height(drc));
  ComposeQuad tv;tv.image=screen.scan.get();tv.sourceLinear=screen.scanSrgb;tv.box=p.tv;quads.push_back(tv);
  if(p.pip_on&&drc) {
   const float op=p.pip_opacity,bw=std::max(1.0f,std::round(p.pip.w/200.0f));
   ComposeQuad frame;frame.solid=true;frame.color[3]=0.6f*op;
   frame.box={p.pip.x-bw,p.pip.y-bw,p.pip.w+2*bw,p.pip.h+2*bw};quads.push_back(frame);
   ComposeQuad pip;pip.image=drc;pip.sourceLinear=R.drc.scanSrgb;pip.box=p.pip;pip.alpha=op;quads.push_back(pip);
  }
  if(p.button.w>0) {
   // touch screens' view button (display_modes.h): a dark square with two light "screens" (rhemfur's
   // Android design) and the host's state dot (Android: 60 fps on / paused)
   const gfx::Box& t=p.button;
   ComposeQuad b;b.solid=true;b.color[3]=0.45f;b.box={t.x,t.y,t.w,t.h};quads.push_back(b);
   for(int i=0;i<2;i++) {
    ComposeQuad s;s.solid=true;s.color[0]=s.color[1]=s.color[2]=0.9f;s.color[3]=0.8f;
    s.box={t.x+t.w*(i?0.45f:0.15f),t.y+t.h*(i?0.45f:0.2f),t.w*0.4f,t.h*0.32f};quads.push_back(s);
   }
   if(p.button_dot) {
    const bool on=p.button_dot==1;
    ComposeQuad d;d.solid=true;d.color[0]=on?0.2f:0.95f;d.color[1]=on?0.85f:0.8f;d.color[2]=on?0.3f:0.15f;d.color[3]=0.95f;
    d.box={t.x+t.w*0.72f,t.y+t.h*0.06f,t.w*0.22f,t.h*0.22f};quads.push_back(d);
   }
  }
  return quads;
 }
 // no plan (outside swap): the picture scaled to fit (Codex's presentation)
 const auto rect=present_rect(scan_display_extent(screen),target,filter);
 ComposeQuad q;q.image=screen.scan.get();q.sourceLinear=screen.scanSrgb;q.box={rect.x,rect.y,rect.width,rect.height};quads.push_back(q);
 return quads;
}

std::vector<ComposeQuad> screen_quads(Screen& screen,VkExtent2D target,int& filter) {
 if (&screen != &R.tv || stereo::frame.mode == stereo::Off)
  return screen_quads_mono(screen,target,filter);
 // A selected stereo mode must never silently present a mono frame.
 // Wait for both eye scan buffers to be available.
 if (!stereoRight.scan) return {};
 auto logical = target;
 if (stereo::frame.mode == stereo::FullSBS) logical.width = std::max(1u,target.width/2);
 auto mono = screen_quads_mono(screen,logical,filter);
 std::vector<ComposeQuad> result;
 for (int eye=0;eye<2;++eye) for (auto q:mono) {
  if (q.image == screen.scan.get() && (eye != int(stereo::frame.swap))) q.image = stereoRight.scan.get();
  if (stereo::frame.mode == stereo::FullSBS) q.box.x += eye * logical.width;
  else {
   auto r=stereo::pack({q.box.x,q.box.y,q.box.w,q.box.h},float(target.width),float(target.height),stereo::frame.mode,eye);
   q.box={r.x,r.y,r.w,r.h};
  }
  result.push_back(q);
 }
 return result;
}

bool draw_present_screen(Screen& screen,uint32_t imageIndex) {
 auto found=resources.screens.find(&screen);
 if(found==resources.screens.end()||!found->second.drawable) {
  // HDR needs the shader path: present this frame by blit (BLIT_DST is part of
  // hdr::formatSupported) and make the next swapchain SDR, as resolve_output does.
  if(screen.hdrOutput){hdr::fall_back_to_sdr();screen.resize=true;}
  if(&screen==&R.tv) {
   set_graphics_feature_available(GraphicsFeature::FXAA,false);
   set_graphics_feature_available(GraphicsFeature::ScaleFilter,false);
  }
  if(fxaa_enabled())exception_report::raise("FXAA requires color-attachment presentation support");
  if(scale_filter()!=0)exception_report::raise("Sharp/integer filtering requires shader presentation support");
  return false;
 }
 auto& source=*presentation_source_screen(screen).scan;
 if(!sampleable(source)) {
  if(&screen==&R.tv) {
   set_graphics_feature_available(GraphicsFeature::FXAA,false);
   set_graphics_feature_available(GraphicsFeature::ScaleFilter,false);
  }
  exception_report::raise("Scan-buffer format cannot be sampled for shader presentation");
 }
 const bool linear=linear_filtering(source.fmt.pixel);
 if(&screen==&R.tv) {
  set_graphics_feature_available(GraphicsFeature::FXAA,linear);
  set_graphics_feature_available(GraphicsFeature::ScaleFilter,linear);
 }
 if(fxaa_enabled()&&!linear)exception_report::raise("FXAA requires linear scan-buffer filtering");
 int filter=0;auto quads=screen_quads(screen,screen.swapExtent,filter);
 auto* overlay=screen_overlay(screen,screen.swapExtent.width,screen.swapExtent.height);
 if(screen.hdrPq) {
  // HDR10: compose and blend in linear FP16 scRGB exactly as for scRGB output, then
  // encode the whole picture to PQ (blending PQ-encoded values would not be linear).
  auto& linear=found->second.hdrTarget;
  const auto extent=screen.swapExtent;
  if(linear.image&&(linear.width!=extent.width||linear.height!=extent.height))destroy_surface_image(&linear);
  if(!linear.image) {
   linear.width=linear.pitch=extent.width;linear.height=extent.height;linear.slices=linear.mips=1;linear.dim=1;
   linear.format=0x820;linear.fmt=format_info(0x820,false); // GX2 FLOAT_R16_G16_B16_A16
   create_surface_image(&linear,true,VkExtent3D{extent.width,extent.height,1});
  }
  compose(linear.image,linear.view,linear.layout,extent,linear.fmt.pixel,quads,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          filter,fxaa_enabled(),overlay,&linear.use,true);
  ComposeQuad q;q.image=&linear;q.sourceLinear=true;q.box={0,0,float(extent.width),float(extent.height)};
  compose(screen.images.at(imageIndex),found->second.views.at(imageIndex),screen.layouts.at(imageIndex),extent,screen.swapFormat,
          {q},VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,0,false,nullptr,&screen.imageUses.at(imageIndex),false,true);
  return true;
 }
 compose(screen.images.at(imageIndex),found->second.views.at(imageIndex),screen.layouts.at(imageIndex),screen.swapExtent,screen.swapFormat,
         quads,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,filter,fxaa_enabled(),overlay,&screen.imageUses.at(imageIndex),screen.hdrOutput);
 return true;
}

// Test-only headless encoding, once per swap. No image readback or additional queue wait.
void compose_headless_diagnostic(Screen& screen) {
 const auto& diagnostic=gfx::headless_compose::policy();
 if(!diagnostic.enabled()||!screen.scan||!screen.scan->image)return;
 if(!currentPlan||currentPlan->dw!=gfx::headless_compose::width||currentPlan->dh!=gfx::headless_compose::height)
  exception_report::raise("headless composition diagnostic requires WWHD_SIM_SCREEN=1280x720");
 ensure_resources();
 auto& target=resources.diagnosticTarget;auto& counter=resources.diagnosticCounter;
 counter.require_capacity(diagnostic);
 const uint32_t format=screen.scanSrgb?0x41a:0x1a;
 if(target.image&&target.format!=format)destroy_surface_image(&target);
 if(!target.image) {
  target.width=gfx::headless_compose::width;target.height=gfx::headless_compose::height;
  target.format=format;target.fmt=format_info(format,false);
  create_surface_image(&target,true,VkExtent3D{target.width,target.height,1});
 }
 const VkExtent2D extent{target.width,target.height};
 int filter=0;auto quads=screen_quads(screen,extent,filter);
 compose(target.image,target.view,target.layout,extent,target.fmt.pixel,quads,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,filter,fxaa_enabled(),overlayDraw,&target.use);
 unsigned commands=0;
 if(overlayDraw)for(const auto* list:overlayDraw->CmdLists)commands+=list->CmdBuffer.Size;
 const unsigned vertices=overlayDraw?overlayDraw->TotalVtxCount:0,indices=overlayDraw?overlayDraw->TotalIdxCount:0;
 if(counter.encoded(diagnostic,vertices,indices,commands))
  fprintf(stderr,"[headless-compose] Vulkan encoded_frames=%u last_vertices=%u last_indices=%u last_commands=%u total_vertices=%llu total_indices=%llu total_commands=%llu\n",counter.frames,vertices,indices,commands,
   (unsigned long long)counter.vertices,(unsigned long long)counter.indices,(unsigned long long)counter.commands);
}

static void prepare_present_pass(PresentResources&,PresentImageDraw&,VkFormat);
std::optional<PresentImageDraw> prepare_present_image(Screen& screen,uint32_t imageIndex,
    const Surface& snapshot,bool sourceLinear,int filter,bool fxaa) {
 auto found=resources.screens.find(&screen);
 if(found==resources.screens.end() || !found->second.drawable)return {};
 if(!snapshot.view || !sampleable(snapshot))
  throw std::runtime_error("Secondary snapshot cannot be sampled for presentation");
 const bool linear=linear_filtering(snapshot.fmt.pixel);
 if(fxaa&&!linear)throw std::runtime_error("FXAA requires linear snapshot filtering");
 PresentImageDraw draw;
 draw.device=R.device;draw.pipeline=pipeline(screen.swapFormat);
 draw.layout=resources.layout;draw.descriptors=resources.descriptors;
 draw.sampler=linear?resources.linear:resources.nearest;draw.source=snapshot.view;
 draw.target=screen.images.at(imageIndex);draw.targetView=found->second.views.at(imageIndex);
 draw.oldLayout=screen.layouts.at(imageIndex);draw.extent=screen.swapExtent;
 const auto box=gfx::display_layout(float(draw.extent.width),float(draw.extent.height),
     float(snapshot.extent.width),float(snapshot.extent.height));
 draw.viewport={box.x,box.y,box.w,box.h,0,1};
 const float scale=std::min(box.w/snapshot.extent.width,box.h/snapshot.extent.height);
 draw.params=present_params(fxaa&&linear,filter,scale,
     sourceLinear||srgb_format(snapshot.fmt.pixel),srgb_format(screen.swapFormat));
 prepare_present_pass(resources,draw,screen.swapFormat);
 return draw;
}

static void prepare_present_pass(PresentResources& resources,PresentImageDraw& draw,VkFormat format) {
 if(!resources.passes)return;
 VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
 attachment.imageView=draw.targetView;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 attachment.clearValue.color.float32[3]=1;
 VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
 rendering.renderArea.extent=draw.extent;rendering.layerCount=1;
 rendering.colorAttachmentCount=1;rendering.pColorAttachments=&attachment;
 const auto prepared=prepare_pass(*resources.passes,rendering,color_pass(format,attachment.imageLayout));
 draw.renderPass=prepared.info.renderPass;draw.framebuffer=prepared.info.framebuffer;
}
static void record_present_image(const PresentApi& api,const PresentImageDraw& draw,VkCommandBuffer cmd,VkDescriptorPool pool) {
 VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
 barrier.oldLayout=draw.oldLayout;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
 barrier.image=draw.target;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
 barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
 api.vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
     0,0,nullptr,0,nullptr,1,&barrier);
 VkDescriptorSet set;
 VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
 allocation.descriptorPool=pool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&draw.descriptors;
 vk_check(api.vkAllocateDescriptorSets(draw.device,&allocation,&set),"secondary presentation descriptors");
 VkDescriptorImageInfo image{draw.sampler,draw.source,draw.sourceLayout};
 VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
 write.dstSet=set;write.dstBinding=0;write.descriptorCount=1;
 write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;
 api.vkUpdateDescriptorSets(draw.device,1,&write,0,nullptr);
 VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
 attachment.imageView=draw.targetView;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 attachment.clearValue.color.float32[3]=1;
 VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
 rendering.renderArea.extent=draw.extent;rendering.layerCount=1;
 rendering.colorAttachmentCount=1;rendering.pColorAttachments=&attachment;
 if(draw.renderPass) {
  VkClearValue clear{};clear.color.float32[3]=1;
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass=draw.renderPass;begin.framebuffer=draw.framebuffer;
  begin.renderArea=rendering.renderArea;begin.clearValueCount=1;begin.pClearValues=&clear;
  api.passes.beginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
 } else api.vkCmdBeginRendering(cmd,&rendering);
 VkRect2D scissor{{0,0},draw.extent};api.vkCmdSetScissor(cmd,0,1,&scissor);
 api.vkCmdSetViewport(cmd,0,1,&draw.viewport);
 api.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.pipeline);
 api.vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.layout,0,1,&set,0,nullptr);
 api.vkCmdPushConstants(cmd,draw.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(draw.params),&draw.params);
 api.vkCmdDraw(cmd,3,1,0,0);
 if(draw.renderPass)api.passes.endRenderPass(cmd);else api.vkCmdEndRendering(cmd);
 barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=draw.finalLayout;
 barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=0;
 api.vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
     0,0,nullptr,0,nullptr,1,&barrier);
}

void record_present_image(const PresentImageDraw& draw,VkCommandBuffer cmd,VkDescriptorPool pool) {
 record_present_image(resources.api,draw,cmd,pool);
}
struct IndependentPresenter::Impl {
 PresentResources resources;
 ~Impl() { destroy_resources(resources); }
};
IndependentPresenter::IndependentPresenter(VkDevice device,bool dynamicRenderingKHR,bool renderPasses):impl(std::make_unique<Impl>()) {
 ensure_resources(impl->resources,device,dynamicRenderingKHR,renderPasses);
}
IndependentPresenter::~IndependentPresenter()=default;
void IndependentPresenter::forget_views(const VkImageView* views,size_t count) {
 if(auto* cache=impl->resources.passes)cache->destroy_framebuffers(cache->forget_views(views,count));
}
PresentImageDraw IndependentPresenter::prepare(VkImageView source,VkExtent2D sourceExtent,bool sourceLinear,
    VkImage target,VkImageView targetView,VkFormat targetFormat,VkExtent2D targetExtent,
    VkImageLayout oldLayout,int filter,bool fxaa,VkImageLayout finalLayout) {
 if(!source || !target || !targetView || !sourceExtent.width || !sourceExtent.height ||
    !targetExtent.width || !targetExtent.height)throw std::runtime_error("Invalid independent presentation image");
 auto& resources=impl->resources;
 PresentImageDraw draw;
 draw.device=resources.device;draw.pipeline=pipeline(resources,targetFormat);
 draw.layout=resources.layout;draw.descriptors=resources.descriptors;
 draw.sampler=resources.linear;draw.source=source;
 draw.target=target;draw.targetView=targetView;draw.oldLayout=oldLayout;
 draw.finalLayout=finalLayout;draw.extent=targetExtent;
 const auto box=gfx::display_layout(float(targetExtent.width),float(targetExtent.height),
     float(sourceExtent.width),float(sourceExtent.height));
 draw.viewport={box.x,box.y,box.w,box.h,0,1};
 const float scale=std::min(box.w/sourceExtent.width,box.h/sourceExtent.height);
 draw.params=present_params(fxaa,filter,scale,sourceLinear,srgb_format(targetFormat));
 prepare_present_pass(resources,draw,targetFormat);
 return draw;
}
void IndependentPresenter::record(const PresentImageDraw& draw,VkCommandBuffer cmd,VkDescriptorPool pool) const {
 if(draw.device!=impl->resources.device)throw std::runtime_error("Independent presentation device mismatch");
 record_present_image(impl->resources.api,draw,cmd,pool);
}

// the composition into an offscreen RGBA8 image, read back (present dumps, captures)
std::vector<uint8_t> compose_offscreen(Screen& screen,uint32_t width,uint32_t height,bool srgb) {
 if(!width||!height)return {};
 Surface target;target.width=width;target.height=height;target.format=srgb?0x41a:0x1a;target.fmt=format_info(target.format,false);
 create_surface_image(&target,true,VkExtent3D{width,height,1});
 std::vector<uint8_t> rgba;
 try {
  int filter=0;auto quads=screen_quads(screen,VkExtent2D{width,height},filter);
  compose(target.image,target.view,target.layout,VkExtent2D{width,height},target.fmt.pixel,quads,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,filter,fxaa_enabled(),screen_overlay(screen,width,height),&target.use);
  rgba=read_surface_rgba(target,false);  // display-encoded already (sRGB target, or encoded values)
 }catch(...){destroy_surface_image(&target);throw;}
 destroy_surface_image(&target);
 return rgba;
}

bool record_screenshot(Screen& screen,Buffer& buffer,uint32_t& width,uint32_t& height,bool& bgra) {
 if(!screen.scan||!screen.scan->image||!sampleable(*screen.scan))return false;
 const auto display=scan_display_extent(screen);
 width=display.width;height=display.height;
 if(&screen==&R.tv&&stereo::frame.mode==stereo::FullSBS)width*=2;
 if(!width||!height)return false;
 const bool srgb=screen.scanSrgb;
 Surface target;target.width=width;target.height=height;target.format=srgb?0x41a:0x1a;target.fmt=format_info(target.format,false);
 // the TV window's format for both pictures: its pipeline exists (the GamePad window may have none)
 const VkFormat sf=R.tv.swapchain?R.tv.swapFormat:VK_FORMAT_UNDEFINED;
 if(sf==VK_FORMAT_B8G8R8A8_UNORM||sf==VK_FORMAT_B8G8R8A8_SRGB||sf==VK_FORMAT_R8G8B8A8_UNORM||sf==VK_FORMAT_R8G8B8A8_SRGB)
  target.fmt.pixel=sf;
 bgra=target.fmt.pixel==VK_FORMAT_B8G8R8A8_UNORM||target.fmt.pixel==VK_FORMAT_B8G8R8A8_SRGB;
 create_surface_image(&target,true,VkExtent3D{width,height,1});
 try {
  int filter=0;
  ComposeQuad q;q.image=screen.scan.get();q.sourceLinear=screen.scanSrgb;q.box={0,0,float(width),float(height)};
  auto quads=&screen==&R.tv&&stereo::frame.mode!=stereo::Off
      ? screen_quads(screen,{width,height},filter) : std::vector<ComposeQuad>{q};
  compose(target.image,target.view,target.layout,VkExtent2D{width,height},target.fmt.pixel,quads,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,fxaa_enabled(),nullptr,&target.use);
  buffer=create_readback_buffer(VkDeviceSize(width)*height*4);
  transition_image(&target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  auto cmd=command_buffer();
  VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={width,height,1};
  record::vkCmdCopyImageToBuffer(cmd,target.image,target.layout,buffer.buffer,1,&region);
  derive_dependency(buffer.use,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  transition_buffer(buffer,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
 }catch(...){destroy_surface_image(&target);if(buffer.buffer){defer_buffer(buffer);buffer={};}throw;}
 destroy_surface_image(&target);  // deferred: freed when this submission retired
 return true;
}

// ---------------------------------------------------------------- gameplay mod HUDs
// The climb mod's stamina wheel and the run/swim boost's bar, drawn into the TV scan image (the same
// two the Metal renderer draws in mods/mod_hud.mm).
void draw_mod_overlay(Surface& scan, VkExtent2D displayExtent) {
 // both step aside (fade out) while a game menu, an event or a message is up (mods/hud_gate.cpp)
 const float shown=mods::game_ui_hud_alpha();
 mods::ClimbHud climb=mods::climb_hud_drawn();climb.alpha*=shown;
 mods::MoveHud move=mods::move_hud_drawn();move.alpha*=shown;
 const bool wheel=mods::climb_enabled()&&climb.alpha>0.0f, bar=move.alpha>0.0f;
 if((!wheel&&!bar)||!scan.image||!scan.view||!(scan.usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))return;
 transition_image(&scan,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 auto cmd=command_buffer();
 VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};attachment.imageView=scan.view;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 const VkExtent2D extent{scan.extent.width,scan.extent.height};
 VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea.extent=extent;rendering.layerCount=1;rendering.colorAttachmentCount=1;rendering.pColorAttachments=&attachment;
 begin_pass(cmd,rendering,[&] { return color_pass(scan.fmt.pixel,scan.layout); });R.rendering=true;R.passTracked=false;
 const float w=float(extent.width),h=float(extent.height);
 const float xRatio=displayExtent.width&&displayExtent.height
     ? (w/h)/(float(displayExtent.width)/displayExtent.height) : 1.0f;
 VkRect2D scissor{{0,0},extent};
 if(wheel) {
  // upper right of the screen centre, where the follow camera keeps Link (mod_hud.mm)
  const float cx=0.60f*w,cy=0.38f*h,rad=0.05f*h;
  VkViewport viewport{cx-rad*xRatio,cy-rad,2*rad*xRatio,2*rad,0,1};
  record::vkCmdSetViewport(cmd,0,1,&viewport);record::vkCmdSetScissor(cmd,0,1,&scissor);
  record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(scan.fmt.pixel,kHud));
  PresentParams params{};const float u[4]={climb.stamina,climb.alpha,climb.exhausted?1.0f:0.0f,0};memcpy(&params,u,16);
  record::vkCmdPushConstants(cmd,resources.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);record::vkCmdDraw(cmd,3,1,0,0);
 }
 if(bar) {
  // a ring under the wheel's spot, same shape; 0 running, 1 swimming, 2 recharging
  const float cx=0.60f*w,cy=0.49f*h,rad=0.042f*h;
  VkViewport viewport{cx-rad*xRatio,cy-rad,2*rad*xRatio,2*rad,0,1};
  record::vkCmdSetViewport(cmd,0,1,&viewport);record::vkCmdSetScissor(cmd,0,1,&scissor);
  record::vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(scan.fmt.pixel,kMoveHud));
  PresentParams params{};const float u[4]={move.stamina,move.alpha,move.exhausted?2.0f:move.swimming?1.0f:0.0f,0};memcpy(&params,u,16);
  record::vkCmdPushConstants(cmd,resources.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);record::vkCmdDraw(cmd,3,1,0,0);
 }
 end_pass(cmd);R.rendering=false;R.passTracked=false;
}

bool present_capture_requested() { return present_capture().enabled; }
void record_present_capture(Screen& screen,uint32_t imageIndex) {
 auto& capture=present_capture();
 if(!capture.enabled||capture.done||&screen!=&R.tv||R.frame<capture.frame)return;
 capture.done=true;
 auto found=resources.screens.find(&screen);
 if(found==resources.screens.end()||!found->second.captureTransfer) {
  std::fprintf(stderr,"[vulkan present] capture unavailable: swapchain lacks transfer-source support\n");return;
 }
 if(screen.swapFormat!=VK_FORMAT_R8G8B8A8_UNORM && screen.swapFormat!=VK_FORMAT_R8G8B8A8_SRGB &&
    screen.swapFormat!=VK_FORMAT_B8G8R8A8_UNORM && screen.swapFormat!=VK_FORMAT_B8G8R8A8_SRGB) {
  std::fprintf(stderr,"[vulkan present] capture unsupported swap format %d\n",int(screen.swapFormat));return;
 }
 try {
  const size_t width=screen.swapExtent.width,height=screen.swapExtent.height;
  if(!width||!height||width>std::numeric_limits<size_t>::max()/height/4)
   exception_report::raise("invalid swap-image capture dimensions");
  capture.buffer=create_readback_buffer(width*height*4);
  capture.extent=screen.swapExtent;capture.format=screen.swapFormat;
  auto cmd=command_buffer();VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  image.oldLayout=screen.layouts.at(imageIndex);image.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  image.srcQueueFamilyIndex=image.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  image.image=screen.images.at(imageIndex);image.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  auto dependency=derive_dependency(screen.imageUses.at(imageIndex),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,true);
  image.srcAccessMask=narrow_barriers()?dependency.sourceAccess:VK_ACCESS_MEMORY_WRITE_BIT;image.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  record::vkCmdPipelineBarrier(cmd,narrow_barriers()?dependency.source:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&image);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={uint32_t(width),uint32_t(height),1};
  record::vkCmdCopyImageToBuffer(cmd,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,capture.buffer.buffer,1,&copy);
  derive_dependency(capture.buffer.use,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  transition_buffer(capture.buffer,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
  image.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;image.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  image.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;image.dstAccessMask=0;
  record::vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&image);
  screen.layouts[imageIndex]=image.newLayout;
 }catch(const std::exception& error) {
  if(capture.buffer.buffer){defer_buffer(capture.buffer);capture.buffer={};}
  std::fprintf(stderr,"[vulkan present] capture failed: %s\n",error.what());
 }
}
void finish_present_capture(Screen& screen) {
 auto& capture=present_capture();if(&screen!=&R.tv||!capture.buffer.buffer)return;
 // Called after normal submit waited for its fence. Swapchain acquire was
 // waited in that same submission, and finished semaphore remains for present.
 try {
  const size_t count=size_t(capture.extent.width)*capture.extent.height;
  const auto* raw=static_cast<const uint8_t*>(capture.buffer.mapped);std::vector<uint8_t> rgba(count*4);
  const bool bgra=capture.format==VK_FORMAT_B8G8R8A8_UNORM||capture.format==VK_FORMAT_B8G8R8A8_SRGB;
  for(size_t i=0;i<count;++i){rgba[4*i]=raw[4*i+(bgra?2:0)];rgba[4*i+1]=raw[4*i+1];rgba[4*i+2]=raw[4*i+(bgra?0:2)];rgba[4*i+3]=255;}
  // Swap-image bytes are already display encoded; never apply capture gamma.
  write_rgba_png(capture.path,capture.extent.width,capture.extent.height,rgba);
  std::fprintf(stderr,"[vulkan present] wrote actual swap image %s (%ux%u, frame %llu, FXAA %d, filter %d)\n",
      capture.path.c_str(),capture.extent.width,capture.extent.height,(unsigned long long)R.frame,int(fxaa_enabled()),scale_filter());
 }catch(const std::exception& error){std::fprintf(stderr,"[vulkan present] capture failed: %s\n",error.what());}
 defer_buffer(capture.buffer);capture.buffer={};
}

}
