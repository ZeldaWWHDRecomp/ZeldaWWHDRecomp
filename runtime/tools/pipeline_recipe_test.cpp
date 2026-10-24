#include "gfx/vulkan/pipeline_recipe.h"
#include <string>
#include "pipeline_state_fixture.h"
#include <cassert>
#include <thread>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <unordered_set>
#include <zlib.h>
#ifndef _WIN32
#include <sys/resource.h>
#endif

using gfxvk::GraphicsPipelineState;
using pipeline_test::state;

using namespace gfxvk;
template<class F> static void rejects(F operation) {
  bool rejected = false;
  try { operation(); } catch (const std::runtime_error&) { rejected = true; }
  assert(rejected);
}
int main(int argc, char** argv) {
  if ((argc == 3 || argc == 4 || argc == 5) &&
      (std::string(argv[1]) == "--profile" || std::string(argv[1]) == "--profile-decode")) {
    std::ifstream file(argv[2], std::ios::binary);
    file.seekg(0,std::ios::end); const auto size=file.tellg();
    assert(size>=64 && uint64_t(size)<=kPipelineRecipeMaxBytes);
    file.seekg(0); std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()); assert(file.good());
    assert(bytes.size() >= 64);
    PipelineRecipeFingerprint fingerprint;
    std::copy_n(bytes.begin()+8, fingerprint.size(), fingerprint.begin());
    if (argc >= 4) {
      auto source=decode_pipeline_recipes(fingerprint,bytes);
      const size_t count=std::stoul(argv[3]);
      std::vector<std::shared_ptr<PipelineRecipe>> population;
      for(size_t i=0;i<count;++i) {
        auto copy=decode_pipeline_recipe(encode_pipeline_recipe(*source[i%source.size()]));
        for(size_t b=0;b<8;++b) copy->key[b]=uint8_t(i>>(b*8));
        population.push_back(std::move(copy));
      }
      bytes=encode_pipeline_recipes(fingerprint,population);
      if (argc == 5) {
        std::ofstream output(argv[4],std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        assert(output.good()); return 0;
      }
    }
    const auto start=std::chrono::steady_clock::now();
    auto recipes=decode_pipeline_recipes(fingerprint,bytes);
    size_t words=0,uniqueWords=0;
    std::unordered_set<const void*> unique;
    for(const auto& recipe:recipes) for(size_t stage=0;stage<2;++stage) {
      const size_t count=recipe->word_count(stage); words+=count;
      const void* id=recipe->sharedSpirv[stage] ? static_cast<const void*>(recipe->sharedSpirv[stage].get()) : recipe->spirv[stage].data();
      if(unique.insert(id).second) uniqueWords+=count;
    }
    auto encoded=std::string(argv[1]) == "--profile-decode" ? std::vector<uint8_t>{} : encode_local_pipeline_recipes(fingerprint,recipes);
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    uint64_t rss=0;
#ifndef _WIN32
    rusage usage{}; getrusage(RUSAGE_SELF,&usage); rss=usage.ru_maxrss;
#ifndef __APPLE__
    rss*=1024;
#endif
#endif
    std::printf("recipes=%zu bytes=%zu local_v2_bytes=%zu spirv_bytes=%zu unique_spirv_bytes=%zu decode_encode_us=%lld peak_rss_bytes=%llu\n",recipes.size(),bytes.size(),encoded.size(),words*4,uniqueWords*4,(long long)elapsed,(unsigned long long)rss);
    return 0;
  }
  auto recipe = std::make_shared<PipelineRecipe>();
  recipe->state = std::make_unique<GraphicsPipelineState>(state());
  recipe->key.resize(208);
  for (size_t i = 0; i < recipe->key.size(); ++i) recipe->key[i] = uint8_t(i);
  // Structurally valid minimal SPIR-V is enough for serialization tests;
  // driver validation is tested separately with actual translated shaders.
  for (auto& code : recipe->spirv) code = {0x07230203, 0x00010300, 0, 1, 0, 0x00010000};
  recipe->dynamicUniforms = true;
  for (int stage = 0; stage < 2; ++stage)
    recipe->descriptors[stage].push_back({7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
      VkShaderStageFlags(stage ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT), nullptr});
  const auto fixed = encode_pipeline_fixed_state(*recipe->state);
  auto fixed_variant = [&](auto mutate) {
    auto snapshot = *recipe->state;
    auto ci = *snapshot.get();
    mutate(ci);
    GraphicsPipelineState changed(ci);
    assert(encode_pipeline_fixed_state(changed) != fixed);
  };
  // Same shaders and descriptor layout cannot authorize stale fixed state.
  fixed_variant([](auto& ci) { const_cast<VkPipelineDepthStencilStateCreateInfo*>(ci.pDepthStencilState)->depthWriteEnable = VK_FALSE; });
  fixed_variant([](auto& ci) { const_cast<VkPipelineColorBlendAttachmentState*>(ci.pColorBlendState->pAttachments)->colorWriteMask = 15; });
  fixed_variant([](auto& ci) { const_cast<VkVertexInputBindingDescription*>(ci.pVertexInputState->pVertexBindingDescriptions)->stride = 32; });
  fixed_variant([](auto& ci) { auto* rc = const_cast<VkPipelineRenderingCreateInfo*>(static_cast<const VkPipelineRenderingCreateInfo*>(ci.pNext)); const_cast<VkFormat*>(rc->pColorAttachmentFormats)[0] = VK_FORMAT_B8G8R8A8_UNORM; });
  auto identical = *recipe->state;
  identical.set_stage_module(0, reinterpret_cast<VkShaderModule>(uintptr_t(17)));
  assert(encode_pipeline_fixed_state(identical) == fixed); // handles never enter identity
  auto encoded = encode_pipeline_recipe(*recipe);
  // Classic compatibility handles stay process-local. The recipe's format
  // metadata and fixed-state identity are identical on both rendering paths.
  auto classicSource = *recipe->state;
  auto classicInfo = *classicSource.get();
  const auto classicTargets = classicSource.rendering_targets();
  classicInfo.pNext = nullptr;
  classicInfo.renderPass = reinterpret_cast<VkRenderPass>(uintptr_t(123));
  recipe->state = std::make_unique<GraphicsPipelineState>(classicInfo, &classicTargets);
  assert(encode_pipeline_fixed_state(*recipe->state) == fixed);
  assert(encode_pipeline_recipe(*recipe) == encoded);
  auto classicDecoded = decode_pipeline_recipe(encoded);
  recipe->state = std::move(classicDecoded->state);
  assert(recipe->state && !recipe->state->get()->renderPass);
  // Portable state has the exact fixed-state wire fields, with no shader words.
  auto portable = encoded;
  size_t offset = 4 + 208 * 4 + 4;
  for (int stage = 0; stage < 2; ++stage) {
    portable.erase(portable.begin() + offset + 4, portable.begin() + offset + 4 + 6 * 4);
    for (int i = 0; i < 4; ++i) portable[offset+i] = 0;
    offset += 4 + 4 + 4 * 4; // empty code, one descriptor
  }
  portable.insert(portable.begin(), {'W','W','V','K','S','T','0','1'});
  auto codeFree = decode_code_free_pipeline_recipe(portable);
  assert(codeFree->spirv[0].empty() && codeFree->spirv[1].empty());
  assert(codeFree->key == recipe->key);
  assert(codeFree->descriptors[1][0].binding == 7);
  assert(codeFree->state->get()->pVertexInputState->pVertexBindingDescriptions[0].stride == 24);
  auto codeCarrying = encoded;
  codeCarrying.insert(codeCarrying.begin(), {'W','W','V','K','S','T','0','1'});
  rejects([&] { decode_code_free_pipeline_recipe(codeCarrying); });
  auto portableTrailing = portable; portableTrailing.push_back(0);
  rejects([&] { decode_code_free_pipeline_recipe(portableTrailing); });
  auto portableTruncated = portable; portableTruncated.pop_back();
  rejects([&] { decode_code_free_pipeline_recipe(portableTruncated); });
  rejects([&] { decode_pipeline_recipe(portable); });
  codeFree->spirv = recipe->spirv; // Only locally resolved words make a native recipe.
  assert(encode_pipeline_recipe(*codeFree) == encoded);
  RecipeShaderPool pool;
  auto sharedA=decode_pipeline_recipe(encoded), sharedB=decode_pipeline_recipe(encoded);
  pool.freeze(*sharedA); pool.freeze(*sharedB);
  assert(sharedA->sharedSpirv[0] == sharedB->sharedSpirv[0]);
  assert(sharedA->spirv[0].empty() && sharedB->spirv[1].empty());
  assert(encode_pipeline_recipe(*sharedA)==encoded);
  auto distinct=decode_pipeline_recipe(encoded);
  distinct->spirv[0][3]=2; pool.freeze(*distinct);
  assert(distinct->sharedSpirv[0] != sharedA->sharedSpirv[0]);
  assert(distinct->sharedSpirv[1] == sharedA->sharedSpirv[1]);
  // Packed shaders unpack to the same words and stay shared by recipes with the same code.
  assert(sharedA->words(0) == codeFree->spirv[0] && sharedA->word_count(1) == codeFree->spirv[1].size());
  assert(distinct->words(0)[3] == 2);
  auto restored = decode_pipeline_recipe(encoded);
  recipe.reset(); // No pointer in the decoded state may borrow from the original.
  assert(restored->key[207] == 207);
  assert(restored->dynamicUniforms);
  assert(restored->state->get()->pRasterizationState->depthBiasConstantFactor == 2.5f);
  assert(restored->state->get()->pVertexInputState->pVertexBindingDescriptions[0].stride == 24);
  assert(encode_pipeline_recipe(*restored) == encoded);
  PipelineRecipeFingerprint fingerprint{};
  auto database = encode_pipeline_recipes(fingerprint, {restored});
  assert(decode_pipeline_recipes(fingerprint, database).size() == 1);
  // Full-game populations exceed the old 512-route limit, but remain bounded.
  std::vector<std::shared_ptr<PipelineRecipe>> tooMany(16385, restored);
  rejects([&] { encode_pipeline_recipes(fingerprint, tooMany); });
  std::vector<std::shared_ptr<PipelineRecipe>> fullGame;
  for (size_t i = 0; i < 513; ++i) {
    auto entry = decode_pipeline_recipe(encoded);
    entry->key[0] = uint8_t(i); entry->key[1] = uint8_t(i >> 8);
    fullGame.push_back(std::move(entry));
  }
  auto fullGameDatabase = encode_pipeline_recipes(fingerprint, fullGame);
  assert(decode_pipeline_recipes(fingerprint, fullGameDatabase).size() == 513);
  auto duplicate = encode_pipeline_recipes(fingerprint, {restored, restored});
  rejects([&] { decode_pipeline_recipes(fingerprint, duplicate); });
  fingerprint[0] = 1;
  rejects([&] { decode_pipeline_recipes(fingerprint, database); });
  fingerprint[0] = 0;
  // The log names the part of the fingerprint that changed.
  {
    auto reason = [&](size_t at) {
      PipelineRecipeFingerprint stored = fingerprint;
      stored[at] ^= 1;
      const char* text = pipeline_recipe_mismatch(fingerprint, stored.data());
      return std::string(text ? text : "");
    };
    assert(!pipeline_recipe_mismatch(fingerprint, fingerprint.data()));
    assert(reason(0).find("another GPU") != std::string::npos);
    assert(reason(4).find("another GPU") != std::string::npos);
    assert(reason(8).find("driver") != std::string::npos);
    assert(reason(12).find("driver") != std::string::npos);  // pipeline cache UUID
    assert(reason(27).find("driver") != std::string::npos);
    assert(reason(28).find("runtime build") != std::string::npos);
    assert(reason(35).find("runtime build") != std::string::npos);
    assert(reason(36).find("capabilities") != std::string::npos);
    assert(reason(44).find("capabilities") != std::string::npos);
    assert(reason(40).find("recipe format") != std::string::npos);
    PipelineRecipeFingerprint rebuilt = fingerprint;  // schema wins over everything else
    rebuilt[40] ^= 1; rebuilt[0] ^= 1; rebuilt[30] ^= 1;
    assert(std::string(pipeline_recipe_mismatch(fingerprint, rebuilt.data())).find("recipe format") != std::string::npos);
    PipelineRecipeFingerprint other = fingerprint;
    other[29] ^= 0x80;
    std::string message;
    try { decode_pipeline_recipes(other, database); }
    catch (const std::runtime_error& error) { message = error.what(); }
    assert(message.find("incompatible pipeline recipe database: they come from another runtime build") == 0);
  }
  for (size_t size : {size_t(0), size_t(63), database.size() - 1}) {
    auto truncated = database; truncated.resize(size);
    rejects([&] { decode_pipeline_recipes(fingerprint, truncated); });
  }
  auto corrupt = database; corrupt.back() ^= 1;
  rejects([&] { decode_pipeline_recipes(fingerprint, corrupt); });
  auto trailing = database; trailing.push_back(0);
  rejects([&] { decode_pipeline_recipes(fingerprint, trailing); });
  // Version 2 (local database): shaders once, groups kept; version 1 still reads (migration on the next save).
  {
    std::vector<std::shared_ptr<PipelineRecipe>> grouped;
    for (size_t i = 0; i < 300; ++i) {
      auto entry = decode_pipeline_recipe(encoded);
      entry->key[0] = uint8_t(i); entry->key[1] = uint8_t(i >> 8);
      if (i % 3 == 1) entry->spirv[1][3] = 2;  // a second fragment shader
      if (i % 2) { assert(entry->add_group(0x1234u + uint32_t(i % 5))); assert(!entry->add_group(0x1234u + uint32_t(i % 5))); }
      if (i == 7) for (uint32_t g = 1; g <= 9; ++g) entry->add_group(0x9000u + g);
      grouped.push_back(std::move(entry));
    }
    assert(grouped[7]->groups.size() == kPipelineRecipeMaxGroups);
    auto local = encode_local_pipeline_recipes(fingerprint, grouped);
    assert(std::equal(local.begin(), local.begin() + 8, "WWVKPR02"));
    auto v1 = encode_pipeline_recipes(fingerprint, grouped);
    assert(local[64] == 2 && !local[65] && local.size() < v1.size());  // 2 distinct shaders instead of 600 copies
    auto back = decode_pipeline_recipes(fingerprint, local);
    assert(back.size() == grouped.size());
    for (size_t i = 0; i < back.size(); ++i) {
      assert(back[i]->key == grouped[i]->key && back[i]->groups == grouped[i]->groups);
      assert(back[i]->words(0) == grouped[i]->spirv[0] && back[i]->words(1) == grouped[i]->spirv[1]);
      assert(back[i]->sharedSpirv[0] == back[0]->sharedSpirv[0]);
      assert(encode_pipeline_recipe(*back[i]) == encode_pipeline_recipe(*grouped[i]));
    }
    assert(encode_local_pipeline_recipes(fingerprint, back) == local);
    // an old (version 1) database: read, then written as version 2 without its groups (it had none)
    auto migrated = decode_pipeline_recipes(fingerprint, v1);
    assert(migrated.size() == grouped.size() && migrated[1]->groups.empty());
    assert(decode_pipeline_recipes(fingerprint, encode_local_pipeline_recipes(fingerprint, migrated)).size() == grouped.size());
    for (size_t size : {size_t(63), size_t(70), local.size() - 1}) {
      auto truncated = local; truncated.resize(size);
      rejects([&] { decode_pipeline_recipes(fingerprint, truncated); });
    }
    auto bad = local; bad[100] ^= 1;  // inside the first packed shader: checksum
    rejects([&] { decode_pipeline_recipes(fingerprint, bad); });
    // a damaged shader with a matching checksum is still rejected (it does not unpack to its digest)
    auto damaged = local; damaged[90] ^= 0x40;
    uint32_t crc = uint32_t(crc32(0, damaged.data() + 64, damaged.size() - 64));
    for (int i = 0; i < 4; ++i) damaged[60 + i] = uint8_t(crc >> (8 * i));
    rejects([&] { decode_pipeline_recipes(fingerprint, damaged); });
    auto other = fingerprint; other[3] ^= 1;
    rejects([&] { decode_pipeline_recipes(other, local); });
    auto twice = grouped[1]; twice->groups.push_back(twice->groups[0]);
    rejects([&] { encode_local_pipeline_recipes(fingerprint, {twice}); });
  }
  auto shortened = encoded; shortened.pop_back();
  rejects([&] { decode_pipeline_recipe(shortened); });
  restored->descriptors[0].push_back(restored->descriptors[0].front());
  rejects([&] { encode_pipeline_recipe(*restored); });
  restored->descriptors[0].pop_back();
  restored->spirv[0].back() = 0;
  rejects([&] { encode_pipeline_recipe(*restored); });
  // Oversized byte scalars must not truncate to a valid key byte.
  auto noncanonical = encoded; noncanonical[5] = 1;
  rejects([&] { decode_pipeline_recipe(noncanonical); });
}
