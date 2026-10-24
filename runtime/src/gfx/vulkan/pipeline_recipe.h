#pragma once
#include "pipeline_state.h"
#include <array>
#include <memory>
#include <unordered_map>

namespace gfxvk {
inline constexpr size_t kPipelineRecipeMaxBytes = 256 * 1024 * 1024;
inline constexpr size_t kPipelineRecipeMaxCount = 16384;
// Device/driver/features and schema compatibility are checked before a recipe
// reaches Vulkan. Neither Vulkan handles nor host pointers are serialized.
using PipelineRecipeFingerprint = std::array<uint8_t, 48>;
// The SPIR-V of one shader, compressed (zlib) and immutable; recipes that use the same shader share one.
// A whole-game database names ~1,400 distinct shaders in ~5,500 recipes: ~11 MB compressed instead of
// ~200 MB of per-recipe copies.
struct RecipeSpirv {
  uint64_t hash = 0;        // FNV-1a of the words
  uint32_t wordCount = 0;
  std::vector<uint8_t> packed;
  std::vector<uint32_t> words() const;  // throws on damaged data
  static uint64_t digest(const std::vector<uint32_t>& words);
  static std::shared_ptr<const RecipeSpirv> pack(const std::vector<uint32_t>& words);
};
// Recipe groups: the object archives (pipeline_groups::group_of their name) whose models the setup's model sweep drew
// with this pipeline. A grouped recipe is compiled ahead when one of its archives is loaded in play; an ungrouped one
// at start.
inline constexpr size_t kPipelineRecipeMaxGroups = 8;
struct PipelineRecipe {
  std::vector<uint8_t> key;
  std::array<std::vector<uint32_t>, 2> spirv;  // until frozen into sharedSpirv
  std::array<std::vector<VkDescriptorSetLayoutBinding>, 2> descriptors;
  std::unique_ptr<GraphicsPipelineState> state;
  bool dynamicUniforms = false;
  std::array<std::shared_ptr<const RecipeSpirv>,2> sharedSpirv;
  std::vector<uint32_t> groups;
  std::vector<uint32_t> words(size_t stage) const {
    return sharedSpirv[stage] ? sharedSpirv[stage]->words() : spirv[stage];
  }
  size_t word_count(size_t stage) const {
    return sharedSpirv[stage] ? sharedSpirv[stage]->wordCount : spirv[stage].size();
  }
  std::array<std::vector<uint32_t>,2> shader_words() const { return {words(0),words(1)}; }
  bool add_group(uint32_t group) {
    if (!group || groups.size() >= kPipelineRecipeMaxGroups) return false;
    for (auto g : groups) if (g == group) return false;
    groups.push_back(group); return true;
  }
};
// Hash collisions are checked by full word equality, never by digest alone.
class RecipeShaderPool {
  std::unordered_multimap<uint64_t,std::weak_ptr<const RecipeSpirv>> entries;
public:
  // Bytes of the shaders this pool packed for the first time (a recipe database's distinct shader bytes).
  size_t packedBytes = 0;
  void freeze(PipelineRecipe& recipe) {
    if(entries.size()>32768) std::erase_if(entries,[](const auto& entry){ return entry.second.expired(); });
    for(size_t stage=0;stage<2;++stage) {
      if(recipe.sharedSpirv[stage]) { adopt(recipe.sharedSpirv[stage]); continue; } // Published recipes remain immutable.
      const auto& words = recipe.spirv[stage];
      if(words.empty()) continue;
      const uint64_t hash=RecipeSpirv::digest(words);
      std::shared_ptr<const RecipeSpirv> shared;
      auto [first,last]=entries.equal_range(hash);
      for(auto it=first;it!=last;) {
        auto candidate=it->second.lock();
        if(!candidate) { it=entries.erase(it); continue; }
        if(candidate->wordCount==words.size() && candidate->words()==words) { shared=std::move(candidate); break; }
        ++it;
      }
      if(!shared) {
        shared=RecipeSpirv::pack(words);
        packedBytes+=shared->packed.size();
        entries.emplace(hash,shared);
      }
      recipe.sharedSpirv[stage]=std::move(shared);
      std::vector<uint32_t>().swap(recipe.spirv[stage]);
    }
  }
  // A shader packed elsewhere (a decoded database): later recipes with the same words share it.
  void adopt(const std::shared_ptr<const RecipeSpirv>& shared) {
    auto [first,last]=entries.equal_range(shared->hash);
    for(auto it=first;it!=last;++it) if(it->second.lock()==shared) return;
    entries.emplace(shared->hash,shared);
  }
};
// Strict portable state only: shader words must both be empty. Resolve locally before Vulkan.
std::shared_ptr<PipelineRecipe> decode_code_free_pipeline_recipe(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> encode_pipeline_fixed_state(const GraphicsPipelineState& state);
std::vector<uint8_t> encode_pipeline_recipe(PipelineRecipe& recipe);
std::shared_ptr<PipelineRecipe> decode_pipeline_recipe(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> encode_pipeline_recipes(const PipelineRecipeFingerprint& fingerprint,
    const std::vector<std::shared_ptr<PipelineRecipe>>& recipes);
// Version 1 ("WWVKPR01"): every recipe carries its own SPIR-V; the head start's pipeline records use it.
// Version 2 ("WWVKPR02"): the local database; each distinct shader once (packed), recipes name it by index and
// carry their groups. decode_pipeline_recipes reads both (an old local database is migrated on its next save).
std::vector<uint8_t> encode_local_pipeline_recipes(const PipelineRecipeFingerprint& fingerprint,
    const std::vector<std::shared_ptr<PipelineRecipe>>& recipes);
std::vector<std::shared_ptr<PipelineRecipe>> decode_pipeline_recipes(
    const PipelineRecipeFingerprint& fingerprint, const std::vector<uint8_t>& bytes);
// Why a stored fingerprint (backend.cpp layout) differs from the current one,
// for the log; nullptr when they match. Bytes 28-35 mix the enabled-feature
// checksum with the runtime build identity and cannot be told apart.
const char* pipeline_recipe_mismatch(const PipelineRecipeFingerprint& current, const uint8_t* stored);
}
