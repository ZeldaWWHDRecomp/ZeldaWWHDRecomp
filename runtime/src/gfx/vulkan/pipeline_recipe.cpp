#include <string>
#include "pipeline_recipe.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <zlib.h>

namespace gfxvk {
namespace {
constexpr size_t maxBytes = kPipelineRecipeMaxBytes;
struct Writer {
  std::vector<uint8_t> bytes;
  template<class T> void scalar(T value) {
    uint32_t word;
    if constexpr (std::is_same_v<T, float>) word = std::bit_cast<uint32_t>(value);
    else word = uint32_t(value);
    if (bytes.size() > maxBytes - 4) throw std::runtime_error("pipeline recipe size limit");
    for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(word >> (8 * i)));
  }
  void raw(const uint8_t* data, size_t size) {  // padded to whole words
    if (size > maxBytes - 4 || bytes.size() > maxBytes - 4 - size) throw std::runtime_error("pipeline recipe size limit");
    bytes.insert(bytes.end(), data, data + size);
    while (bytes.size() % 4) bytes.push_back(0);
  }
};
struct Reader {
  const std::vector<uint8_t>& bytes;
  size_t at = 0;
  template<class T> void scalar(T& value) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("truncated pipeline recipe");
    uint32_t word = 0;
    for (int i = 0; i < 4; ++i) word |= uint32_t(bytes[at++]) << (8 * i);
    if constexpr (std::is_same_v<T, float>) {
      value = std::bit_cast<float>(word);
      if (!std::isfinite(value)) throw std::runtime_error("nonfinite pipeline recipe state");
    } else {
      if constexpr (std::is_same_v<T, uint8_t>)
        if (word > 255) throw std::runtime_error("invalid pipeline byte scalar");
      value = T(word);
    }
  }
  uint32_t count(uint32_t max) {
    uint32_t value; scalar(value);
    if (value > max) throw std::runtime_error("pipeline recipe count limit");
    return value;
  }
  void raw(uint8_t* data, size_t size) {
    const size_t padded = (size + 3) & ~size_t(3);
    if (at > bytes.size() || bytes.size() - at < padded) throw std::runtime_error("truncated pipeline recipe");
    std::memcpy(data, bytes.data() + at, size);
    for (size_t i = size; i < padded; ++i) if (bytes[at + i]) throw std::runtime_error("nonzero pipeline recipe padding");
    at += padded;
  }
};
template<class A, class T> void vector(A& archive, std::vector<T>& values, uint32_t limit) {
  if constexpr (std::is_same_v<A, Reader>) values.resize(archive.count(limit));
  else {
    if (values.size() > limit) throw std::runtime_error("pipeline recipe count limit");
    archive.scalar(uint32_t(values.size()));
  }
  for (auto& value : values) archive.scalar(value);
}
bool valid_spirv(const std::vector<uint32_t>& code) {
  if (code.size() < 6 || code.size() > 262144 || code[0] != 0x07230203 ||
      code[1] != 0x00010300 || !code[3] || code[3] > 0x400000 || code[4]) return false;
  for (size_t at = 5; at < code.size();) {
    size_t length = code[at] >> 16;
    if (!length || length > code.size() - at) return false;
    at += length;
  }
  return true;
}
// Scalar fields are visited explicitly. Struct padding, pNext and all pointer
// members are excluded from the wire format.
template<class A, class S> void raster_fields(A& a, S& s) {
#define F(x) a.scalar(s.x)
  F(flags); F(depthClampEnable); F(rasterizerDiscardEnable); F(polygonMode);
  F(cullMode); F(frontFace); F(depthBiasEnable); F(depthBiasConstantFactor);
  F(depthBiasClamp); F(depthBiasSlopeFactor); F(lineWidth);
#undef F
}
template<class A, class S> void stencil_fields(A& a, S& s) {
#define F(x) a.scalar(s.x)
  F(failOp); F(passOp); F(depthFailOp); F(compareOp); F(compareMask); F(writeMask); F(reference);
#undef F
}
template<class A, class S> void depth_fields(A& a, S& s) {
#define F(x) a.scalar(s.x)
  F(flags); F(depthTestEnable); F(depthWriteEnable); F(depthCompareOp); F(depthBoundsTestEnable);
  F(stencilTestEnable); stencil_fields(a, s.front); stencil_fields(a, s.back);
  F(minDepthBounds); F(maxDepthBounds);
#undef F
}
template<class A, class S> void attachment_fields(A& a, S& s) {
#define F(x) a.scalar(s.x)
  F(blendEnable); F(srcColorBlendFactor); F(dstColorBlendFactor); F(colorBlendOp);
  F(srcAlphaBlendFactor); F(dstAlphaBlendFactor); F(alphaBlendOp); F(colorWriteMask);
#undef F
}
struct Parts {
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  VkPipelineMultisampleStateCreateInfo multi{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  std::vector<VkFormat> formats;
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  std::vector<VkPipelineColorBlendAttachmentState> attachments;
  std::vector<VkDynamicState> dynamics;
  Parts() = default;
  explicit Parts(const VkGraphicsPipelineCreateInfo* ci, const VkPipelineRenderingCreateInfo& targets) {
    // The persisted guest schema has dynamic viewports/scissors and mandatory
    // depth/dynamic structures. Utility snapshots are not guest recipes.
    if (!ci->pDepthStencilState || !ci->pDynamicState ||
        ci->pViewportState->pViewports || ci->pViewportState->pScissors ||
        ci->pMultisampleState->pSampleMask)
      throw std::runtime_error("unsupported guest recipe state");
    rendering = targets;
    vertex = *ci->pVertexInputState; assembly = *ci->pInputAssemblyState;
    viewport = *ci->pViewportState; raster = *ci->pRasterizationState;
    multi = *ci->pMultisampleState; depth = *ci->pDepthStencilState;
    blend = *ci->pColorBlendState; dynamic = *ci->pDynamicState;
    auto copy = [](auto* p, size_t n, auto& out) { if (n) out.assign(p, p + n); };
    copy(rendering.pColorAttachmentFormats, rendering.colorAttachmentCount, formats);
    copy(vertex.pVertexBindingDescriptions, vertex.vertexBindingDescriptionCount, bindings);
    copy(vertex.pVertexAttributeDescriptions, vertex.vertexAttributeDescriptionCount, attributes);
    copy(blend.pAttachments, blend.attachmentCount, attachments);
    copy(dynamic.pDynamicStates, dynamic.dynamicStateCount, dynamics);
  }
  template<class A> void visit(A& a) {
    a.scalar(rendering.viewMask); a.scalar(rendering.depthAttachmentFormat); a.scalar(rendering.stencilAttachmentFormat);
    vector(a, formats, 8);
    uint32_t count = uint32_t(bindings.size());
    if constexpr (std::is_same_v<A, Reader>) { count = a.count(16); bindings.resize(count); }
    else { if (count > 16) throw std::runtime_error("binding limit"); a.scalar(count); }
    for (auto& b : bindings) { a.scalar(b.binding); a.scalar(b.stride); a.scalar(b.inputRate); }
    count = uint32_t(attributes.size());
    if constexpr (std::is_same_v<A, Reader>) { count = a.count(64); attributes.resize(count); }
    else { if (count > 64) throw std::runtime_error("attribute limit"); a.scalar(count); }
    for (auto& b : attributes) { a.scalar(b.location); a.scalar(b.binding); a.scalar(b.format); a.scalar(b.offset); }
    a.scalar(assembly.flags); a.scalar(assembly.topology); a.scalar(assembly.primitiveRestartEnable);
    a.scalar(viewport.flags); a.scalar(viewport.viewportCount); a.scalar(viewport.scissorCount);
    raster_fields(a, raster);
    a.scalar(multi.flags); a.scalar(multi.rasterizationSamples); a.scalar(multi.sampleShadingEnable);
    a.scalar(multi.minSampleShading); a.scalar(multi.alphaToCoverageEnable); a.scalar(multi.alphaToOneEnable);
    depth_fields(a, depth);
    a.scalar(blend.flags); a.scalar(blend.logicOpEnable); a.scalar(blend.logicOp);
    for (auto& v : blend.blendConstants) a.scalar(v);
    count = uint32_t(attachments.size());
    if constexpr (std::is_same_v<A, Reader>) { count = a.count(8); attachments.resize(count); }
    else { if (count > 8) throw std::runtime_error("attachment limit"); a.scalar(count); }
    for (auto& b : attachments) attachment_fields(a, b);
    vector(a, dynamics, 4);
  }
  void validate() const {
    auto boolean = [](uint32_t v) { return v <= 1; };
    auto format = [](VkFormat v) { return uint32_t(v) <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK; };
    if (rendering.viewMask || !format(rendering.depthAttachmentFormat) || !format(rendering.stencilAttachmentFormat) ||
        assembly.flags || uint32_t(assembly.topology) > VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN || !boolean(assembly.primitiveRestartEnable) ||
        viewport.flags || viewport.viewportCount != 1 || viewport.scissorCount != 1 ||
        raster.flags || !boolean(raster.depthClampEnable) || raster.rasterizerDiscardEnable ||
        raster.polygonMode != VK_POLYGON_MODE_FILL || raster.cullMode > 3 || uint32_t(raster.frontFace) > 1 ||
        !boolean(raster.depthBiasEnable) || raster.lineWidth != 1 || multi.flags ||
        multi.rasterizationSamples != VK_SAMPLE_COUNT_1_BIT || multi.sampleShadingEnable ||
        multi.alphaToCoverageEnable || multi.alphaToOneEnable || depth.flags ||
        !boolean(depth.depthTestEnable) || !boolean(depth.depthWriteEnable) || uint32_t(depth.depthCompareOp) > 7 ||
        depth.depthBoundsTestEnable || !boolean(depth.stencilTestEnable) || blend.flags ||
        !boolean(blend.logicOpEnable) || uint32_t(blend.logicOp) > 15 || attachments.size() != formats.size())
      throw std::runtime_error("unsupported pipeline recipe state");
    for (auto v : formats) if (!format(v)) throw std::runtime_error("invalid pipeline format");
    for (auto& b : bindings) if (b.binding >= 16 || b.stride > 65535 || uint32_t(b.inputRate) > 1) throw std::runtime_error("invalid vertex binding");
    for (auto& b : attributes) if (b.location >= 64 || b.binding >= 16 || !b.format || !format(b.format) || b.offset > 65535) throw std::runtime_error("invalid vertex attribute");
    for (auto s : {depth.front, depth.back})
      if (uint32_t(s.failOp) > 7 || uint32_t(s.passOp) > 7 || uint32_t(s.depthFailOp) > 7 || uint32_t(s.compareOp) > 7)
        throw std::runtime_error("invalid stencil state");
    for (auto& b : attachments)
      if (!boolean(b.blendEnable) || uint32_t(b.srcColorBlendFactor) > 18 || uint32_t(b.dstColorBlendFactor) > 18 ||
          uint32_t(b.srcAlphaBlendFactor) > 18 || uint32_t(b.dstAlphaBlendFactor) > 18 ||
          uint32_t(b.colorBlendOp) > 4 || uint32_t(b.alphaBlendOp) > 4 || b.colorWriteMask > 15)
        throw std::runtime_error("invalid blend state");
    const std::vector<VkDynamicState> expected{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    if (dynamics != expected) throw std::runtime_error("unsupported pipeline dynamic state");
  }
  std::unique_ptr<GraphicsPipelineState> snapshot() {
    rendering.colorAttachmentCount = formats.size(); rendering.pColorAttachmentFormats = formats.data();
    vertex.vertexBindingDescriptionCount = bindings.size(); vertex.pVertexBindingDescriptions = bindings.data();
    vertex.vertexAttributeDescriptionCount = attributes.size(); vertex.pVertexAttributeDescriptions = attributes.data();
    blend.attachmentCount = attachments.size(); blend.pAttachments = attachments.data();
    dynamic.dynamicStateCount = dynamics.size(); dynamic.pDynamicStates = dynamics.data();
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& s : stages) { s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; s.pName = "main"; }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &rendering; ci.stageCount = 2; ci.pStages = stages; ci.pVertexInputState = &vertex;
    ci.pInputAssemblyState = &assembly; ci.pViewportState = &viewport; ci.pRasterizationState = &raster;
    ci.pMultisampleState = &multi; ci.pDepthStencilState = &depth; ci.pColorBlendState = &blend; ci.pDynamicState = &dynamic;
    return std::make_unique<GraphicsPipelineState>(ci);
  }
};
// Native: SPIR-V words inline (version 1); CodeFree: empty words (portable state); Indexed (version 2): the key as
// raw bytes, each stage's shader as an index into the database's shader table, the recipe's groups at the end.
enum class Layout { Native, CodeFree, Indexed };
struct ShaderTable {
  std::vector<std::shared_ptr<const RecipeSpirv>> shaders;             // read
  std::unordered_map<const RecipeSpirv*, uint32_t> index;              // write
};
template<class A> void recipe_fields(A& a, PipelineRecipe& recipe, Parts& parts, Layout layout = Layout::Native,
                                     ShaderTable* table = nullptr) {
  if (layout == Layout::Indexed) {
    if constexpr (std::is_same_v<A, Reader>) recipe.key.resize(208);
    if (recipe.key.size() != 208) throw std::runtime_error("incompatible pipeline key schema");
    a.raw(recipe.key.data(), 208);
  } else vector(a, recipe.key, 208);
  if (recipe.key.size() != 208) throw std::runtime_error("incompatible pipeline key schema");
  uint32_t dynamic = recipe.dynamicUniforms;
  a.scalar(dynamic);
  if (dynamic > 1) throw std::runtime_error("invalid uniform layout flag");
  if constexpr (std::is_same_v<A, Reader>) recipe.dynamicUniforms = dynamic != 0;
  for (size_t stage = 0; stage < 2; ++stage) {
    if (layout == Layout::Indexed) {
      if constexpr (std::is_same_v<A, Reader>) {
        uint32_t index = a.count(uint32_t(table->shaders.size()) - 1);
        if (table->shaders.empty()) throw std::runtime_error("pipeline recipe shader index");
        recipe.sharedSpirv[stage] = table->shaders[index];
      } else {
        auto found = recipe.sharedSpirv[stage] ? table->index.find(recipe.sharedSpirv[stage].get()) : table->index.end();
        if (found == table->index.end()) throw std::runtime_error("pipeline recipe shader missing from table");
        a.scalar(found->second);
      }
    } else if constexpr (std::is_same_v<A, Reader>) vector(a, recipe.spirv[stage], 262144);
    else {
      const auto& words=recipe.words(stage);
      if(words.size()>262144) throw std::runtime_error("shader word limit");
      a.scalar(uint32_t(words.size()));
      for(auto word:words) a.scalar(word);
    }
    if (layout == Layout::CodeFree ? !recipe.words(stage).empty() :
        layout == Layout::Native ? !valid_spirv(recipe.words(stage)) : false) throw std::runtime_error("invalid recipe SPIR-V");
    auto& bindings = recipe.descriptors[stage];
    uint32_t count = bindings.size();
    if constexpr (std::is_same_v<A, Reader>) { count = a.count(128); bindings.resize(count); }
    else { if (count > 128) throw std::runtime_error("descriptor count limit"); a.scalar(count); }
    std::vector<uint32_t> seen;
    for (auto& b : bindings) {
      a.scalar(b.binding); a.scalar(b.descriptorType); a.scalar(b.descriptorCount); a.scalar(b.stageFlags);
      if (b.binding >= 256 || b.descriptorCount != 1 || b.stageFlags != (stage ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT) ||
          (b.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           b.descriptorType != (dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)))
        throw std::runtime_error("invalid recipe descriptor layout");
      for (auto previous : seen) if (previous == b.binding) throw std::runtime_error("duplicate recipe descriptor binding");
      seen.push_back(b.binding);
    }
  }
  parts.visit(a); parts.validate();
  if (layout == Layout::Indexed) {
    vector(a, recipe.groups, uint32_t(kPipelineRecipeMaxGroups));
    for (size_t i = 0; i < recipe.groups.size(); ++i) {
      if (!recipe.groups[i]) throw std::runtime_error("invalid pipeline recipe group");
      for (size_t j = 0; j < i; ++j) if (recipe.groups[j] == recipe.groups[i]) throw std::runtime_error("duplicate pipeline recipe group");
    }
  }
}
}
std::vector<uint8_t> encode_pipeline_fixed_state(const GraphicsPipelineState& state) {
  auto snapshot = state;
  Parts parts(snapshot.get(), snapshot.rendering_targets());
  Writer out;
  out.scalar(snapshot.get()->flags);
  out.scalar(snapshot.get()->pVertexInputState->flags);
  parts.visit(out); parts.validate();
  return std::move(out.bytes);
}
std::vector<uint8_t> encode_pipeline_recipe(PipelineRecipe& recipe) {
  if (!recipe.state) throw std::runtime_error("missing pipeline recipe state");
  // get() rebinds internal pointers. Keep serialization read-only with respect
  // to a recipe also held by the recorder/startup map.
  auto snapshot = *recipe.state;
  Parts parts(snapshot.get(), snapshot.rendering_targets());
  Writer out; recipe_fields(out, recipe, parts); return std::move(out.bytes);
}
std::shared_ptr<PipelineRecipe> decode_pipeline_recipe(const std::vector<uint8_t>& bytes) {
  if (bytes.size() > 4 * 1024 * 1024) throw std::runtime_error("pipeline record size limit");
  auto recipe = std::make_shared<PipelineRecipe>();
  Reader in{bytes}; Parts parts; recipe_fields(in, *recipe, parts);
  if (in.at != bytes.size()) throw std::runtime_error("trailing pipeline recipe bytes");
  recipe->state = parts.snapshot(); return recipe;
}
std::shared_ptr<PipelineRecipe> decode_code_free_pipeline_recipe(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < 8 || bytes.size() > 4 * 1024 * 1024 ||
      std::memcmp(bytes.data(), "WWVKST01", 8))
    throw std::runtime_error("invalid code-free pipeline state envelope");
  auto recipe = std::make_shared<PipelineRecipe>();
  Reader in{bytes, 8}; Parts parts; recipe_fields(in, *recipe, parts, Layout::CodeFree);
  if (in.at != bytes.size()) throw std::runtime_error("trailing code-free pipeline state");
  recipe->state = parts.snapshot(); return recipe;
}
std::vector<uint8_t> encode_pipeline_recipes(const PipelineRecipeFingerprint& fingerprint,
    const std::vector<std::shared_ptr<PipelineRecipe>>& recipes) {
  if (recipes.size() > kPipelineRecipeMaxCount) throw std::runtime_error("pipeline recipe database count limit");
  Writer out;
  // One output buffer: the previous body+header concatenation temporarily
  // duplicated a whole-game database during periodic saves.
  size_t estimate = 64;
  for (const auto& recipe : recipes) {
    size_t size = 8192 + recipe->key.size() * 4;
    for (size_t stage=0;stage<2;++stage)
      size += recipe->words(stage).size()*4 + recipe->descriptors[stage].size()*16;
    estimate += std::min(size, maxBytes - std::min(estimate,maxBytes));
  }
  out.bytes.reserve(estimate); out.bytes.resize(64);
  std::memcpy(out.bytes.data(), "WWVKPR01", 8);
  std::memcpy(out.bytes.data()+8, fingerprint.data(), fingerprint.size());
  for (auto& recipe : recipes) {
    auto bytes = encode_pipeline_recipe(*recipe);
    if (out.bytes.size() + bytes.size() + 4 > maxBytes) throw std::runtime_error("pipeline database size limit");
    out.scalar(uint32_t(bytes.size())); out.bytes.insert(out.bytes.end(), bytes.begin(), bytes.end());
  }
  auto word = [&](size_t at, uint32_t value) {
    for (int i=0;i<4;++i) out.bytes[at+i]=uint8_t(value>>(8*i));
  };
  word(56,uint32_t(recipes.size()));
  word(60,uint32_t(crc32(0,out.bytes.data()+64,out.bytes.size()-64)));
  return std::move(out.bytes);
}
const char* pipeline_recipe_mismatch(const PipelineRecipeFingerprint& current, const uint8_t* stored) {
  auto differs = [&](size_t at, size_t size) { return std::memcmp(current.data() + at, stored + at, size) != 0; };
  // Schema first: an older layout may give the other fields another meaning.
  if (differs(40, 4)) return "they use another recipe format; they will be rebuilt";
  if (differs(0, 8)) return "they come from another GPU; they will be rebuilt";
  if (differs(8, 20)) return "they come from another graphics driver version; they will be rebuilt";
  if (differs(36, 4) || differs(44, 4)) return "they come from other device capabilities or vertex settings; they will be rebuilt";
  if (differs(28, 8)) return "they come from another runtime build (or other enabled device features); they will be rebuilt";
  return nullptr;
}
namespace {
constexpr uint32_t kMaxDatabaseShaders = 2 * kPipelineRecipeMaxCount;
void finish_header(std::vector<uint8_t>& bytes, uint32_t count) {
  auto word = [&](size_t at, uint32_t value) {
    for (int i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(8*i));
  };
  word(56,count);
  word(60,uint32_t(crc32(0,bytes.data()+64,bytes.size()-64)));
}
}
std::vector<uint8_t> encode_local_pipeline_recipes(const PipelineRecipeFingerprint& fingerprint,
    const std::vector<std::shared_ptr<PipelineRecipe>>& recipes) {
  if (recipes.size() > kPipelineRecipeMaxCount) throw std::runtime_error("pipeline recipe database count limit");
  // Shaders not packed yet (a recipe that never went through a pool) are packed for this file only.
  RecipeShaderPool pool;
  std::vector<std::shared_ptr<PipelineRecipe>> frozen;
  frozen.reserve(recipes.size());
  for (const auto& recipe : recipes) {
    if (recipe->sharedSpirv[0] && recipe->sharedSpirv[1]) { frozen.push_back(recipe); continue; }
    auto copy = std::make_shared<PipelineRecipe>();
    copy->key = recipe->key; copy->descriptors = recipe->descriptors; copy->dynamicUniforms = recipe->dynamicUniforms;
    copy->groups = recipe->groups;
    if (!recipe->state) throw std::runtime_error("missing pipeline recipe state");
    copy->state = std::make_unique<GraphicsPipelineState>(*recipe->state);
    for (size_t stage = 0; stage < 2; ++stage) {
      if (recipe->sharedSpirv[stage]) copy->sharedSpirv[stage] = recipe->sharedSpirv[stage];
      else {
        if (!valid_spirv(recipe->spirv[stage])) throw std::runtime_error("invalid recipe SPIR-V");
        copy->spirv[stage] = recipe->spirv[stage];
      }
    }
    pool.freeze(*copy);
    frozen.push_back(std::move(copy));
  }
  ShaderTable table;
  std::vector<const RecipeSpirv*> order;
  for (const auto& recipe : frozen)
    for (const auto& shader : recipe->sharedSpirv)
      if (table.index.emplace(shader.get(), uint32_t(order.size())).second) order.push_back(shader.get());
  if (order.size() > kMaxDatabaseShaders) throw std::runtime_error("pipeline recipe shader count limit");
  Writer out;
  size_t estimate = 64 + 4 + frozen.size() * 2048;
  for (auto* shader : order) estimate += 16 + shader->packed.size() + 3;
  out.bytes.reserve(std::min(estimate, maxBytes)); out.bytes.resize(64);
  std::memcpy(out.bytes.data(), "WWVKPR02", 8);
  std::memcpy(out.bytes.data()+8, fingerprint.data(), fingerprint.size());
  out.scalar(uint32_t(order.size()));
  for (auto* shader : order) {
    out.scalar(shader->wordCount); out.scalar(uint32_t(shader->packed.size()));
    out.scalar(uint32_t(shader->hash)); out.scalar(uint32_t(shader->hash >> 32));
    out.raw(shader->packed.data(), shader->packed.size());
  }
  for (auto& recipe : frozen) {
    if (!recipe->state) throw std::runtime_error("missing pipeline recipe state");
    auto snapshot = *recipe->state;
    Parts parts(snapshot.get(), snapshot.rendering_targets());
    Writer record; recipe_fields(record, *recipe, parts, Layout::Indexed, &table);
    if (out.bytes.size() + record.bytes.size() + 4 > maxBytes) throw std::runtime_error("pipeline database size limit");
    out.scalar(uint32_t(record.bytes.size())); out.bytes.insert(out.bytes.end(), record.bytes.begin(), record.bytes.end());
  }
  finish_header(out.bytes, uint32_t(frozen.size()));
  return std::move(out.bytes);
}
std::vector<std::shared_ptr<PipelineRecipe>> decode_pipeline_recipes(
    const PipelineRecipeFingerprint& fingerprint, const std::vector<uint8_t>& bytes) {
  const bool indexed = bytes.size() >= 8 && !std::memcmp(bytes.data(), "WWVKPR02", 8);
  if (bytes.size() < 64 || bytes.size() > maxBytes || (!indexed && std::memcmp(bytes.data(), "WWVKPR01", 8)))
    throw std::runtime_error("incompatible pipeline recipe database: not a recipe file of this format");
  if (auto reason = pipeline_recipe_mismatch(fingerprint, bytes.data() + 8))
    throw std::runtime_error(std::string("incompatible pipeline recipe database: ") + reason);
  Reader in{bytes, 56}; uint32_t count = in.count(kPipelineRecipeMaxCount), checksum; in.scalar(checksum);
  if (checksum != crc32(0, bytes.data() + 64, bytes.size() - 64)) throw std::runtime_error("pipeline recipe checksum mismatch");
  std::vector<std::shared_ptr<PipelineRecipe>> recipes;
  std::unordered_set<std::string> keys;
  RecipeShaderPool shaderPool;
  ShaderTable table;
  if (indexed) {
    const uint32_t shaders = in.count(kMaxDatabaseShaders);
    table.shaders.reserve(shaders);
    for (uint32_t i = 0; i < shaders; ++i) {
      auto shader = std::make_shared<RecipeSpirv>();
      uint32_t low, high, packed;
      in.scalar(shader->wordCount); in.scalar(packed); in.scalar(low); in.scalar(high);
      if (shader->wordCount > 262144 || packed > 4 * 1024 * 1024) throw std::runtime_error("pipeline recipe shader bounds");
      shader->hash = uint64_t(low) | uint64_t(high) << 32;
      shader->packed.resize(packed);
      in.raw(shader->packed.data(), packed);
      // Checked once here: words that do not unpack, match their digest or form a SPIR-V module are rejected.
      const auto words = shader->words();
      if (RecipeSpirv::digest(words) != shader->hash || !valid_spirv(words)) throw std::runtime_error("invalid recipe SPIR-V");
      shaderPool.adopt(shader);
      table.shaders.push_back(std::move(shader));
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    size_t size = in.count(4 * 1024 * 1024);
    if (size > bytes.size() - in.at) throw std::runtime_error("truncated pipeline database record");
    std::shared_ptr<PipelineRecipe> recipe;
    if (indexed) {
      recipe = std::make_shared<PipelineRecipe>();
      std::vector<uint8_t> record(bytes.begin() + in.at, bytes.begin() + in.at + size);
      Reader r{record}; Parts parts; recipe_fields(r, *recipe, parts, Layout::Indexed, &table);
      if (r.at != record.size()) throw std::runtime_error("trailing pipeline recipe bytes");
      recipe->state = parts.snapshot();
    } else recipe = decode_pipeline_recipe({bytes.begin() + in.at, bytes.begin() + in.at + size});
    if (!keys.emplace(reinterpret_cast<const char*>(recipe->key.data()), recipe->key.size()).second)
      throw std::runtime_error("duplicate pipeline recipe key");
    shaderPool.freeze(*recipe);
    recipes.push_back(std::move(recipe));
    in.at += size;
  }
  if (in.at != bytes.size()) throw std::runtime_error("trailing pipeline database bytes");
  return recipes;
}
namespace {
uint32_t swap_bytes(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }
}
std::vector<uint32_t> RecipeSpirv::words() const {
  std::vector<uint32_t> out(wordCount);
  uLongf size = uLongf(wordCount) * 4;
  if (wordCount && (uncompress(reinterpret_cast<Bytef*>(out.data()), &size, packed.data(), uLong(packed.size())) != Z_OK ||
                    size != uLongf(wordCount) * 4)) throw std::runtime_error("damaged recipe SPIR-V");
  if constexpr (std::endian::native == std::endian::big)
    for (auto& word : out) word = swap_bytes(word);
  return out;
}
uint64_t RecipeSpirv::digest(const std::vector<uint32_t>& words) {
  uint64_t hash=1469598103934665603ull;
  for(auto word:words) hash=(hash ^ word)*1099511628211ull;
  return hash;
}
std::shared_ptr<const RecipeSpirv> RecipeSpirv::pack(const std::vector<uint32_t>& words) {
  auto shader = std::make_shared<RecipeSpirv>();
  shader->hash = digest(words); shader->wordCount = uint32_t(words.size());
  std::vector<uint32_t> little = words;
  if constexpr (std::endian::native == std::endian::big)
    for (auto& word : little) word = swap_bytes(word);
  uLongf size = compressBound(uLong(little.size() * 4));
  shader->packed.resize(size);
  // Level 1: ~0.2 ms per shader on the thread that records a new pipeline; ~3x smaller than the words.
  if (compress2(shader->packed.data(), &size, reinterpret_cast<const Bytef*>(little.data()), uLong(little.size() * 4), 1) != Z_OK)
    throw std::runtime_error("recipe SPIR-V compression failed");
  shader->packed.resize(size); shader->packed.shrink_to_fit();
  return shader;
}
}
