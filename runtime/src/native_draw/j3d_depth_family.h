// The depth family core (native_draw/j3d_depth_family.cpp), shared by its users inside native_draw: the proof harness
// (j3d_depth.cpp) and the J3D description depth path (j3d_desc_depth.cpp, WWHD_NATIVE_J3D_DESC_DEPTH). Decomp build fast
// mode, WWHD_NATIVE_DRAW=ON only. Include after the cemu shim (as the gx2 sources get it) and LatteDecompiler.h.
//
// A family is a set of J3D depth programs with the same structure hash (normalised GLSL of the translated program:
// temporaries renumbered, remapped uniform reads named by block location and vec4; WWHD_NATIVE_J3D_DEPTH_BIASFREE=1:
// without the emulated sampler LOD-bias term, so MoltenVK and Adreno hash alike). Hand-written programs exist for three
// vertex structures (rigid, skin2, skin3, with a colour-only pixel program); every other proven (vertex, pixel) pair
// gets a generated program: the member's own arithmetic with the family's interface (whole guest blocks per location,
// the support scalars in one block, the textures in the native set).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "gfx/vulkan/loader.h"

struct LatteFetchShader;
namespace gfxvk { namespace vk { struct Shader; } }

namespace native_draw::j3d_depth::fam {

enum Kind { kRigid, kSkin2, kSkin3 };
struct Family {
    const char* name;
    Kind kind;
    uint64_t vs;  // structure hash of the vertex program
};
// what a depth pipeline is made for: a register image (raster state, attribute strides), the fetch layout, the
// pass's attachment formats. The description path gives the define's image here, never the live register file.
struct PipeIn {
    const uint32_t* regs = nullptr;
    const LatteFetchShader* fs = nullptr;
    std::array<VkFormat, 8> colors{};
    uint32_t ncolor = 0;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    bool stencil = false;
};
struct Generated {
    std::string why;  // empty: made
    std::string vsText;  // the generated vertex program's GLSL (the layered-instance variant is made from it)
    std::vector<uint32_t> vs, ps, psLod0;
    std::vector<uint32_t> vsBlocks, psBlocks, units;
    bool support = false;
    int8_t mapping[LATTE_NUM_MAX_ATTRIBUTE_LOCATIONS]{};
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> bindings;  // (of set)
    VkDescriptorSetLayout setDyn = VK_NULL_HANDLE;  // the uniform buffers dynamic (per-state sets)
    VkPipelineLayout layoutDyn = VK_NULL_HANDLE;
};
// the generated programs' set: vertex blocks at their location, pixel blocks at kPsBlockBase + location, the support
// scalars (alpha test ref, LOD bias, texture scales) at kSupportBinding, texture unit U at kTextureBase + U
constexpr uint32_t kPsBlockBase = 16, kSupportBinding = 32, kTextureBase = 40;
// the hand-written families' set: binding 0 View {view[3], proj[4]} (location 2's rows 0-2 and 27-30), binding 1 the
// model block (rigid: 3 rows) or the whole palette (skin)
struct Native {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::map<std::string, VkPipeline> pipelines;
};
struct NativeDyn { VkDevice device = VK_NULL_HANDLE; VkDescriptorSetLayout set = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; };
extern NativeDyn g_nativeDyn;  // the hand-written families' layout with dynamic uniform buffers (native_dyn())

float f32(uint32_t v);
bool biasfree_on();                                                       // WWHD_NATIVE_J3D_DEPTH_BIASFREE
uint64_t structure_hash(const gfxvk::vk::Shader* s, std::string* textOut);
bool colour_only(const gfxvk::vk::Shader* ps);
std::vector<Family>& families();
const std::set<std::pair<uint64_t, uint64_t>>& proven();           // generated pairs proven with device-specific hashes
const std::set<std::pair<uint64_t, uint64_t>>& proven_biasfree();  // the same 60 pairs, bias-free hashes (Mac = phone)
Generated& generated(const gfxvk::vk::Shader* vs, const gfxvk::vk::Shader* ps, uint64_t vsHash, uint64_t psHash);
std::vector<uint32_t> words(const std::string& src, bool vertex, const char* what);  // GLSL -> SPIR-V (empty: failed)
// the hand-written family's pipeline (DYNLAYOUT: on that layout instead of the plain native one)
// (VSOVERRIDE: another vertex program with the same streams, TAG added to the pipeline key: the layered-instance variant)
VkPipeline pipeline(const PipeIn& d, Kind kind, const char** why, VkPipelineLayout dynLayout,
                    const std::vector<uint32_t>* vsOverride, const char* tag);
// a generated family's pipeline (LOD: its LOD-0 pixel program; DYN: on its dynamic-uniform layout)
// (LAYOUTOVERRIDE, VSOVERRIDE, TAG: as pipeline())
VkPipeline generated_pipeline(Generated& g, const PipeIn& in, uint64_t vsHash, uint64_t psHash, bool lod, const char** why,
                              bool dyn, VkPipelineLayout layoutOverride, const std::vector<uint32_t>* vsOverride,
                              const char* tag);
// a raw pipeline with the family's fixed state (cull, front face, polygon offset, depth clamp from D.regs; LESS + write)
VkPipeline create_pipeline(const PipeIn& d, VkPipelineLayout layout, const std::vector<uint32_t>& vsWords,
                           const std::vector<uint32_t>& psWords, const std::vector<VkVertexInputBindingDescription>& bindings,
                           const std::vector<VkVertexInputAttributeDescription>& attributes, std::string key,
                           const char** why);
// the vertex streams of a generated family at the locations of MAPPING, as the translated pipeline fetches them
bool all_streams(const PipeIn& d, const int8_t* mapping, std::vector<VkVertexInputBindingDescription>& bindings,
                 std::vector<VkVertexInputAttributeDescription>& attributes, std::string& key, const char** why);
std::string vertex_source(Kind k, bool clamp);  // a hand-written family's vertex program
extern const char* kFragment;                   // the empty fragment program
bool dyn_layout(std::vector<VkDescriptorSetLayoutBinding> b, VkDescriptorSetLayout& set, VkPipelineLayout& layout);
bool native_dyn();
VkDescriptorSet slot_set(VkDescriptorSetLayout layout);  // from kept pools (a state's sets live as long as the state)

}  // namespace native_draw::j3d_depth::fam
#endif
