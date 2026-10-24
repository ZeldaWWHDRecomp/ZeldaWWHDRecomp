#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gfxvk::vk {
enum class ShaderStage { Vertex, Fragment, Compute };
struct ShaderCompileResult {
    std::vector<uint32_t> words;
    std::string error;
    uint64_t compileNs = 0, thread = 0;
};
// CPU-only owned result. Polls are repeatable; successful consumption is once.
class ShaderCompileTicket {
public:
    struct State;
    explicit ShaderCompileTicket(std::shared_ptr<State> state);
    ShaderCompileTicket(ShaderCompileTicket&&) noexcept = default;
    ShaderCompileTicket& operator=(ShaderCompileTicket&&) noexcept = default;
    ShaderCompileTicket(const ShaderCompileTicket&) = delete;
    ShaderCompileTicket& operator=(const ShaderCompileTicket&) = delete;
    bool ready() const;
    // Move a still queued job to the front: something now waits for it (Shader compile Fast queues
    // the work of draws that were left out ahead of the ones that must wait).
    void prioritize() const;
    std::optional<ShaderCompileResult> wait_for(uint64_t budgetNs);
    ShaderCompileResult consume();
private:
    std::shared_ptr<State> state_;
};
// Empty admission means eight queued jobs already exist; caller must use Exact
// admission fallback, never omit a draw because this queue is full.
std::optional<ShaderCompileTicket> request_shader_compile(const std::string& source,
    ShaderStage stage, uint64_t frame, uint64_t key, bool background = true);
// Only the immutable source and stage enter the compiler. No guest memory,
// register state, shader-cache maps, or Vulkan handles are borrowed by a job.
ShaderCompileResult compile_shader(const std::string& source, ShaderStage stage,
    uint64_t frame, uint64_t key, const std::function<void()>& whilePending = {},
    bool background = true);
bool async_shaders_enabled();
// issue #207 diagnostic: strip RelaxedPrecision from every module (spirv_precision.h); set once at start
void set_full_precision(bool on);
bool full_precision_enabled();
void note_relaxed_precision_stripped(size_t removed);
bool shader_compiler_active();
// Drains and joins CPU-only work. A later request can restart the worker.
void finish_shader_compiler();
}
