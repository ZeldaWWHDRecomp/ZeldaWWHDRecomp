#include "atomic_temporary.h"
// Offline replay of backend-neutral GX2 shader records from shaderprep.py.
// Metal pipeline recipes (type 2) are intentionally never interpreted here.
#include "shaders.h"
#include "headstart.h"
#include "headstart_shader_record.h"
#include "pipeline_worker.h"
#include "backend.h"
#include "runtime.h"
#include "ppc.h"
#include "platform/host.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "../warm_progress.h"
#include "../headstart_limits.h"
#include <zlib.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace gfxvk {
namespace {
struct Recording {
  const std::optional<std::string> pathStorage = path_environment_utf8("WWHD_VK_RECIPE_RECORD");
  const char* path = pathStorage ? pathStorage->c_str() : nullptr;
  std::mutex mutex;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> records;
  size_t bytes = 0, dropped = 0;
  bool stopped = false;
};
Recording& recording() { static Recording value; return value; }
void append_word(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}
uint32_t word(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
struct ProgramPool {
  std::unordered_map<std::string, uint32_t> addresses;
  size_t bytes = 0;
  uint32_t copy(const uint8_t* data, uint32_t size) {
    std::string key(reinterpret_cast<const char*>(data), size);
    auto found = addresses.find(key);
    if (found != addresses.end()) return found->second;
    if (size > 128 * 1024 * 1024 - bytes)
      throw std::runtime_error("head-start guest program pool exceeds 128 MiB");
    uint32_t address = mem::host_alloc(size, 256);
    if (!address) throw std::runtime_error("head-start guest allocation failed");
    std::memcpy(mem::ptr(address), data, size);
    bytes += size;
    addresses.emplace(std::move(key), address);
    return address;
  }
};
}

void record_headstart_shader(const uint32_t* regs, bool vertex, bool linkedVertex) {
  auto& recorder = recording();
  if (!recorder.path || !*recorder.path || !std::strcmp(recorder.path, "0")) return;
  const uint32_t start = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
  const uint32_t address = regs[start] << 8, size = regs[start + 1] << 3;
  const uint32_t fetchAddress = regs[mmSQ_PGM_START_FS] << 8;
  if (!size || size > 1024 * 1024 || uint64_t(address) + size > 0x100000000ull) return;
  linkedVertex &= !vertex;
  const uint32_t vsAddress = regs[mmSQ_PGM_START_VS] << 8;
  const uint32_t vsSize = linkedVertex ? regs[mmSQ_PGM_START_VS + 1] << 3 : 0;
  if (linkedVertex && (!vsSize || vsSize > 1024 * 1024 || uint64_t(vsAddress) + vsSize > 0x100000000ull)) return;
  uint32_t fetchSize = 0;
  if (fetchAddress) {
    if (ld32(fetchAddress) == 0x57574653) {
      uint32_t count = ld32(fetchAddress + 4);
      if (count > 64) return;
      fetchSize = 16 + count * 16;
    } else fetchSize = regs[mmSQ_PGM_START_FS + 1] << 3;
    if (!fetchSize || fetchSize > 4096 || uint64_t(fetchAddress) + fetchSize > 0x100000000ull) return;
  }
  std::vector<uint8_t> raw;
  uint32_t count = 0;
  for (uint32_t i = 0; i < 0x10000; ++i) count += regs[i] != 0;
  raw.reserve(20 + size + fetchSize + vsSize + count * 8);
  append_word(raw, vertex); append_word(raw, size); append_word(raw, fetchSize); append_word(raw, count);
  if (linkedVertex) append_word(raw, vsSize);
  raw.insert(raw.end(), mem::ptr(address), mem::ptr(address) + size);
  if (fetchSize) raw.insert(raw.end(), mem::ptr(fetchAddress), mem::ptr(fetchAddress) + fetchSize);
  if (linkedVertex) raw.insert(raw.end(), mem::ptr(vsAddress), mem::ptr(vsAddress) + vsSize);
  for (uint32_t i = 0; i < 0x10000; ++i)
    if (regs[i]) { append_word(raw, i); append_word(raw, regs[i]); }
  std::lock_guard lock(recorder.mutex);
  if (recorder.stopped) return;
  if (raw.size() > 128 * 1024 * 1024 - recorder.bytes) { ++recorder.dropped; return; }
  recorder.bytes += raw.size();
  recorder.records.emplace_back(linkedVertex ? 6 : 1, std::move(raw));
}

void record_headstart_pipeline(const std::array<uint8_t, 48>& fingerprint,
    const std::shared_ptr<PipelineRecipe>& recipe) {
  auto& recorder = recording();
  if (!recorder.path || !*recorder.path || !std::strcmp(recorder.path, "0")) return;
  try {
    auto raw = encode_pipeline_recipes(fingerprint, {recipe});
    std::lock_guard lock(recorder.mutex);
    if (recorder.stopped) return;
    if (raw.size() > 128 * 1024 * 1024 - recorder.bytes) { ++recorder.dropped; return; }
    recorder.bytes += raw.size();
    recorder.records.emplace_back(4, std::move(raw));
  } catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan headstart] pipeline recording skipped: %s\n", error.what());
  }
}

// Thread-safe freeze: called at the scripted endpoint or under the renderer's
// orderly shutdown lock. Compression and writes happen after recording stops.
void save_headstart_recording() {
  save_pipeline_recipes();
  auto& recorder = recording();
  if (!recorder.path || !*recorder.path || !std::strcmp(recorder.path, "0")) return;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> records;
  size_t dropped;
  {
    std::lock_guard lock(recorder.mutex);
    if (recorder.stopped) return;
    recorder.stopped = true;
    records.swap(recorder.records);
    dropped = recorder.dropped;
  }
  std::string temporary;
  try {
    auto parent = std::filesystem::u8path(recorder.path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::unique_ptr<FILE, decltype(&std::fclose)> file(open_atomic_temporary(recorder.path, temporary), &std::fclose);
    if (!file) throw std::runtime_error("cannot open recording output");
    for (const auto& [type, raw] : records) {
      uLongf size = compressBound(raw.size());
      std::vector<uint8_t> compressed(size), header;
      if (compress2(compressed.data(), &size, raw.data(), raw.size(), 6) != Z_OK)
        throw std::runtime_error("recording compression failed");
      append_word(header, type); append_word(header, raw.size()); append_word(header, size);
      if (std::fwrite(header.data(), 1, header.size(), file.get()) != header.size() ||
          std::fwrite(compressed.data(), 1, size, file.get()) != size)
        throw std::runtime_error("recording write failed");
    }
    if (std::fclose(file.release())) throw std::runtime_error("recording close failed");
    if (!host::replace_file(temporary, recorder.path)) throw std::runtime_error("recording replacement failed");
    std::fprintf(stderr, "[vulkan headstart] recorded %zu shader/pipeline records, %zu dropped -> %s\n",
        records.size(), dropped, recorder.path);
  } catch (const std::exception& error) {
    std::error_code ec; std::filesystem::remove(std::filesystem::u8path(temporary), ec);
    std::fprintf(stderr, "[vulkan headstart] recording failed: %s\n", error.what());
  }
}

int warm_headstart_shaders() {
  const auto override = path_environment_utf8("WWHD_HEADSTART");
  const std::string path = override ? *override : config::game_dir + "/shadercache/headstart.bin";
  if (path == "0") {
    std::fprintf(stderr, "[vulkan headstart] disabled by WWHD_HEADSTART=0\n");
    return 1;
  }
  try {
    if (std::filesystem::file_size(std::filesystem::u8path(path)) > kHeadstartMaxFileBytes)
      throw std::runtime_error("head-start file exceeds 2 GiB");
    std::unique_ptr<FILE, decltype(&std::fclose)> file(open_utf8_file(path, "rb"), &std::fclose);
    if (!file) throw std::runtime_error("cannot open head-start file");
    // progress: shader records (1, 3, 6) and portable pipeline records (5), counted from the headers
    size_t progressShaders = 0, progressTotal = 0, progressDone = 0;
    {
      std::array<uint8_t, 12> peek;
      while (std::fread(peek.data(), 1, peek.size(), file.get()) == peek.size()) {
        const uint32_t type = word(peek.data());
        progressShaders += type == 1 || type == 3 || type == 6;
        progressTotal += type == 1 || type == 3 || type == 5 || type == 6;
        if (std::fseek(file.get(), word(peek.data() + 8), SEEK_CUR)) break;
      }
      std::clearerr(file.get());
      if (std::fseek(file.get(), 0, SEEK_SET)) throw std::runtime_error("cannot rewind head-start file");
      warmprogress::report(0, progressTotal, "shaders and pipelines", true);
    }
    ProgramPool pool;
    std::array<uint32_t, 0x10000> regs{};
    std::vector<std::shared_ptr<PipelineRecipe>> portable;
    size_t known = 0, speculative = 0, failed = 0, metalPipelines = 0, vulkanPipelines = 0, skippedPipelines = 0;
    uint64_t count = 0, rawBytes = 0;
    std::array<uint8_t, 12> header;
    for (;;) {
      size_t got = std::fread(header.data(), 1, header.size(), file.get());
      if (!got && !std::ferror(file.get())) break;
      if (got != header.size()) throw std::runtime_error("truncated record header");
      uint32_t type = word(header.data()), size = word(header.data() + 4), compressed = word(header.data() + 8);
      if (++count > kHeadstartMaxRecords || !size || size > kHeadstartMaxRecordBytes ||
          !compressed || compressed > kHeadstartMaxRecordBytes || size > kHeadstartMaxRawBytes - rawBytes)
        throw std::runtime_error("invalid head-start record bounds");
      rawBytes += size;
      std::vector<uint8_t> encoded(compressed), raw(size);
      if (std::fread(encoded.data(), 1, encoded.size(), file.get()) != encoded.size())
        throw std::runtime_error("truncated record payload");
      uLongf decoded = size;
      if (uncompress(raw.data(), &decoded, encoded.data(), encoded.size()) != Z_OK || decoded != size)
        throw std::runtime_error("invalid head-start compressed payload");
      if (type == 5) {
        // the startup worker holds at most kPipelineRecipeMaxCount recipes: keep the first ones
        if (portable.size() >= kPipelineRecipeMaxCount) { ++skippedPipelines; continue; }
        portable.push_back(decode_code_free_pipeline_recipe(raw)); ++vulkanPipelines; continue;
      }
      if (type == 2) { ++metalPipelines; continue; }
      if (type == 4) { ++vulkanPipelines; continue; } // Owned startup worker validates/compiles these.
      const auto record = decode_headstart_shader_record(raw, type);
      const bool vertex = record.vertex;
      const uint32_t codeSize = record.program.size(), fetchSize = record.fetch.size();
      const uint32_t nregs = record.registers.size() / 8, vsSize = record.linkedVertex.size();
      regs.fill(0);
      const uint8_t* values = record.registers.data();
      for (uint32_t i = 0; i < nregs; ++i) {
        uint32_t index = word(values + i * 8);
        if (index >= regs.size()) throw std::runtime_error("invalid shader register index");
        regs[index] = word(values + i * 8 + 4);
      }
      uint32_t start = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
      regs[start] = pool.copy(record.program.data(), codeSize) >> 8;
      regs[start + 1] = codeSize >> 3;
      regs[mmSQ_PGM_START_FS] = fetchSize ? pool.copy(record.fetch.data(), fetchSize) >> 8 : 0;
      regs[mmSQ_PGM_START_FS + 1] = fetchSize >> 3;
      uint64_t fetchKey = 0;
      auto* fetch = vk::get_fetch_shader(regs.data(), &fetchKey);
      if (vertex && !fetch) { ++failed; continue; }
      const vk::Shader* linkedVs = nullptr;
      if (type == 6) {
        regs[mmSQ_PGM_START_VS] = pool.copy(record.linkedVertex.data(), vsSize) >> 8;
        regs[mmSQ_PGM_START_VS + 1] = vsSize >> 3;
        if (!fetch) { ++failed; continue; }
        linkedVs = vk::translate(regs.data(), true, fetch, fetchKey);
        if (!linkedVs || !linkedVs->ready()) { ++failed; continue; }
      }
      auto* shader = vk::translate(regs.data(), vertex != 0, fetch, fetchKey, ~uint64_t{0}, ~uint64_t{0}, linkedVs);
      if (!shader || !shader->ready()) ++failed;
      else if (type == 1 || type == 6) ++known;
      else ++speculative;
      warmprogress::report(known + speculative + failed, progressTotal, "shaders and pipelines");
    }
    progressDone = std::min(progressShaders, known + speculative + failed);
    (void)finish_pipeline_warmup();
    std::vector<std::shared_ptr<PipelineRecipe>> resolved;
    RecipeShaderPool resolvedShaderPool;
    size_t unresolved = 0, absentVs = 0, absentPs = 0, uniformMismatch = 0, descriptorMismatch = 0, missingWords = 0, remapped = 0;
    for (auto& recipe : portable) {
      const auto id = [&](size_t offset) {
        uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i) value |= uint64_t(recipe->key[offset+i]) << (8*i);
        return value;
      };
      const vk::Shader* shaders[2]{vk::find_pipeline_shader(id(0), true), vk::find_pipeline_shader(id(8), false)};
      absentVs += !shaders[0]; absentPs += !shaders[1];
      bool compatible = shaders[0] && shaders[1];
      uint32_t uniforms = 0;
      for (auto* shader : shaders) if (shader) {
        uniforms += shader->mapping.uniformVarsBufferBindingPoint >= 0;
        for (auto binding : shader->mapping.uniformBuffersBindingPoint) uniforms += binding >= 0;
      }
      const bool uniformCompatible = recipe->dynamicUniforms == (uniforms <= R.properties.limits.maxDescriptorSetUniformBuffersDynamic);
      if (compatible && !uniformCompatible) ++uniformMismatch;
      compatible &= uniformCompatible;
      for (size_t stage = 0; compatible && stage < 2; ++stage) {
        const auto& m = shaders[stage]->mapping;
        std::vector<VkDescriptorSetLayoutBinding> expected;
        auto add = [&](int binding, VkDescriptorType type) {
          if (binding >= 0) expected.push_back({uint32_t(binding), type, 1,
              VkShaderStageFlags(stage ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT), nullptr});
        };
        auto uniform = recipe->dynamicUniforms ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        add(m.uniformVarsBufferBindingPoint, uniform);
        for (auto binding : m.uniformBuffersBindingPoint) add(binding, uniform);
        for (int unit = 0; unit < m.textureUnitCount; ++unit) add(m.textureUnitBaseBindingPoint + unit, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        compatible = m.tfStorageBindingPoint < 0 && expected.size() == recipe->descriptors[stage].size();
        for (size_t i = 0; compatible && i < expected.size(); ++i) {
          const auto& a = expected[i]; const auto& b = recipe->descriptors[stage][i];
          compatible = a.binding == b.binding && a.descriptorType == b.descriptorType &&
              a.descriptorCount == b.descriptorCount && a.stageFlags == b.stageFlags;
        }
        if (!compatible) ++descriptorMismatch;
        if (compatible) {
          recipe->spirv[stage] = shaders[stage]->binary ? vk::shader_binary_words(shaders[stage]->binary) : shaders[stage]->spirv();
          compatible = !recipe->spirv[stage].empty();
          missingWords += !compatible;
        }
      }
      if (compatible) {
        bool changed = false;
        for (size_t stage = 0; stage < 2; ++stage) {
          changed |= id(stage * 8) != shaders[stage]->pipelineId;
          for (size_t i = 0; i < 8; ++i) recipe->key[stage*8+i] = uint8_t(shaders[stage]->pipelineId >> (8*i));
        }
        remapped += changed;
        resolvedShaderPool.freeze(*recipe);
        resolved.push_back(std::move(recipe));
      } else ++unresolved;
    }
    if (!portable.empty()) {
      std::fprintf(stderr, "[vulkan state warmup] identity resolution: absent VS %zu PS %zu; uniform mismatch %zu descriptor mismatch %zu missing SPIR-V %zu; canonical remaps %zu\n", absentVs, absentPs, uniformMismatch, descriptorMismatch, missingWords, remapped);
      // unresolved recipes are done; the compiled ones report as they finish
      const size_t base = progressDone + unresolved;
      warmprogress::report(base, progressTotal, "shaders and pipelines", true);
      const auto batch = warm_resolved_pipeline_recipes(std::move(resolved), [&](size_t finished) {
        warmprogress::report(base + finished, progressTotal, "shaders and pipelines");
      });
      std::fprintf(stderr, "[vulkan state warmup] %zu ready, %zu failed, %zu unresolved identities/layouts (on-demand fallback)\n", batch.ready, batch.failed, unresolved);
    }
    const auto pipelines = finish_pipeline_warmup();
    warmprogress::report(progressTotal, progressTotal, "shaders and pipelines", true);
    save_pipeline_cache();
    const bool dirty = vk::shader_cache_dirty();
    bool saved = vk::save_shader_cache();
    auto stats = vk::shader_stats();
    std::fprintf(stderr, "[vulkan headstart] known %zu speculative %zu failed %zu; skipped %zu Metal pipeline recipes; SPIR-V compiles %llu disk hits %llu saves %llu; cache checkpoint %s\n",
        known, speculative, failed, metalPipelines, (unsigned long long)stats.spirvCompiles,
        (unsigned long long)stats.diskHits, (unsigned long long)stats.diskSaves,
        !dirty ? "unchanged" : saved ? "ok" : "failed");
    std::fprintf(stderr, "[vulkan headstart] %zu Vulkan pipeline records; %zu ready, %zu failed; %zu over the %zu limit skipped\n",
        vulkanPipelines, pipelines.ready, pipelines.failed, skippedPipelines, kPipelineRecipeMaxCount);
    return failed || pipelines.failed || (dirty && !saved) || !(known + speculative + pipelines.ready) ? 1 : 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan headstart] %s: %s\n", path.c_str(), error.what());
    return 1;
  }
}
}
