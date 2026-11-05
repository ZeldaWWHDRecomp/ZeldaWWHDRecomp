// SPDX-License-Identifier: MPL-2.0
// Compile journal: a shader or pipeline whose compile crashes the process (the GPU driver's compiler, glslang) is
// left out at the next start instead of crashing it again. Without it such a shader is compiled again at every start
// (the head start, the cache replay), so the game could never start again (GreenNaugahyde/ZeldaWWHDRecompAndroid
// issue #53: Huawei Maleoon 910, an abort in the driver's shader compiler).
//
// Each compiling thread notes what it compiles in compiling.bin (one 16-byte slot per thread, written with pwrite
// before and cleared after the compile); the crash handler (main.cpp) marks the slot of a thread that crashes while
// compiling (async-signal-safe: one pwrite). The next start adds the marked slots to compile_failed.bin, and what is
// listed there is not compiled again: a shader module is handed out as VK_NULL_HANDLE and a pipeline with such a
// module, or one listed itself, fails as an ordinary pipeline failure (its draws are left out). The list starts over
// with another GPU, driver or runtime build (compile_failed.key): an update may fix it.
//
// The ids are stable across runs: glslang by the GLSL text and stage, a shader module by its SPIR-V, a pipeline by
// the SPIR-V of its stages (the driver compiles the shaders there; the fork keys it by its recipe instead).
//
// From Nycolazs/WindWakerHD-Android-Plus fc5ddee via GreenNaugahyde/ZeldaWWHDRecompAndroid v0.9.9 (db65d60,
// runtime/src/vk/vk_draw.cpp "compile journal", main.cpp), MPL-2.0; here at the Vulkan entry points (loader.cpp) and
// glslang (shader_compiler.cpp), so every compile path of this renderer is covered.
//
// Test only: WWHD_TEST_COMPILE_CRASH=<type>:<hex id> aborts while compiling that one (as a driver would); id 0: the
// first one of that type not left out already. Types: 1 shader module, 2 pipeline, 3 glslang.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace gfxvk::compile_journal {

enum Type : uint32_t { kModule = 1, kPipeline = 2, kGlslang = 3 };

// opens the journal in `dir` (created if needed) for this GPU / driver / build `key`: takes over what crashed the last
// run, loads the list of what is left out, starts a new compiling.bin. Returns the number left out. Any thread, once.
size_t open(const std::string& dir, const std::string& key);
void close();  // tests: forget everything (the files stay)
// where the journal's few lines go (the runtime's log; default: stderr). No other dependency, so the shader compiler's
// tests link it as it is.
using Logger = void (*)(const char* line);
void set_logger(Logger logger);
bool active();

bool left_out(Type type, uint64_t id);
// around one compile on this thread (no nesting: an inner begin replaces the outer note)
void begin(Type type, uint64_t id);
void end();
struct Scope {
    Scope(Type type, uint64_t id) { begin(type, id); }
    ~Scope() { end(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// the ids
uint64_t hash_bytes(const void* data, size_t size, uint64_t seed);
inline uint64_t module_id(const uint32_t* words, size_t bytes) { return hash_bytes(words, bytes, 0x6D6F64756C65ull); }
inline uint64_t glslang_id(const std::string& source, int stage) {
    return hash_bytes(source.data(), source.size(), 0x676C736C00ull + uint64_t(stage));
}
// a pipeline from its stages' module ids (in stage order)
inline uint64_t pipeline_id(const uint64_t* modules, size_t count) {
    return hash_bytes(modules, count * sizeof(uint64_t), 0x7069706500ull);
}

}  // namespace gfxvk::compile_journal

// the crash handler (main.cpp): marks this thread's slot when it was compiling (async-signal-safe)
extern "C" void wwhd_compile_journal_on_crash();
