#include <exception>
#include "shader_compiler.h"
#include "spirv_precision.h"
#include "stutter_trace.h"
#include "worker_metrics.h"
#include "platform/host.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <condition_variable>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace gfxvk::vk {
struct ShaderCompileTicket::State {
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool ready = false, consumed = false;
    ShaderCompileResult result;
    std::exception_ptr error;
};
namespace {
std::atomic<bool> fullPrecision{false};
std::atomic<uint64_t> fullPrecisionModules{0}, fullPrecisionStripped{0};
}
void set_full_precision(bool on) { fullPrecision = on; }
bool full_precision_enabled() { return fullPrecision.load(std::memory_order_relaxed); }
void note_relaxed_precision_stripped(size_t removed) {
    const uint64_t modules = ++fullPrecisionModules;
    const uint64_t total = fullPrecisionStripped += removed;
    // the first module, then every 256th: how many decorations the driver no longer sees
    if (modules == 1 || modules % 256 == 0)
        fprintf(stderr, "[vulkan diagnostics] full precision: %llu SPIR-V modules, %llu RelaxedPrecision decorations removed\n",
                (unsigned long long)modules, (unsigned long long)total);
}
namespace {
std::mutex glslangMutex; // Outlives the function-static worker.
static std::vector<uint32_t> compile_stage(const std::string& source, EShLanguage stage, std::string* error) {
    // Serialize even diagnostic inline calls against background work.
    std::lock_guard lock(glslangMutex);
    static std::once_flag init;
    static bool initialized = false;
    std::call_once(init, [] { initialized = glslang::InitializeProcess(); });
    if (error) error->clear();
    auto fail = [&](const std::string& reason) { if (error) *error = reason; return std::vector<uint32_t>{}; };
    if (!initialized) return fail("glslang initialization failed");
    glslang::TShader shader(stage);
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        return fail(std::string(shader.getInfoLog()) + shader.getInfoDebugLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) return fail(program.getInfoLog());
    std::vector<uint32_t> words;
    glslang::SpvOptions options;
    options.disableOptimizer = true; // no SPIRV-Tools runtime dependency
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &options);
    if (words.empty()) return fail("glslang emitted empty SPIR-V");
    if (full_precision_enabled()) note_relaxed_precision_stripped(strip_relaxed_precision(words));
    return words;
}

ShaderCompileResult execute(const std::string& source, ShaderStage stage, uint64_t frame, uint64_t key) {
    EShLanguage language;
    switch (stage) {
    case ShaderStage::Vertex: language = EShLangVertex; break;
    case ShaderStage::Fragment: language = EShLangFragment; break;
    case ShaderStage::Compute: language = EShLangCompute; break;
    default: throw std::runtime_error("invalid shader compiler stage");
    }
    ShaderCompileResult result;
    const auto cpuStart = stutter::trace().enabled() ? stutter::thread_cpu_now() : 0;
    const auto start = stutter::now();
    result.words = compile_stage(source, language, &result.error);
    const auto end = stutter::now();
    result.compileNs = end - start;
    result.thread = uint64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    stutter::trace().add(frame == ~uint64_t{0} ? "utility_spirv_compile" : "spirv_compile",
                         frame, key, start, end, result.words.empty() ? -1 : 0, stutter::cpu_elapsed(cpuStart));
    return result;
}
struct Job {
    std::string source;
    ShaderStage stage;
    uint64_t frame, key;
    std::shared_ptr<ShaderCompileTicket::State> done;
};
struct Compiler {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Job>> queue;
    std::thread thread;
    bool stopped = false;
    std::atomic<unsigned> active{0};
    void finish() {
        { std::lock_guard lock(mutex); stopped = true; }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }
    ~Compiler() { finish(); }
};
Compiler& compiler() {
    (void)stutter::trace(); // Trace outlives the worker at process exit.
    static Compiler value;
    return value;
}
void run(Compiler& c) {
    host::set_thread_name("Vulkan SPIR-V");
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock lock(c.mutex);
            c.wake.wait(lock, [&] { return c.stopped || !c.queue.empty(); });
            if (c.queue.empty()) return;
            job = std::move(c.queue.front()); c.queue.pop_front();
        }
        ShaderCompileResult result;
        std::exception_ptr error;
        try { result = execute(job->source, job->stage, job->frame, job->key); }
        catch (...) { error = std::current_exception(); }
        {
            std::lock_guard lock(job->done->mutex);
            job->done->result = std::move(result);
            job->done->error = error;
            job->done->ready = true;
        }
        c.active.fetch_sub(1, std::memory_order_release);
        job->done->wake.notify_all();
        c.wake.notify_all();
    }
}
}
ShaderCompileTicket::ShaderCompileTicket(std::shared_ptr<State> state) : state_(std::move(state)) {}
void ShaderCompileTicket::prioritize() const {
    if (!state_) return;
    auto& c = compiler();
    std::lock_guard lock(c.mutex);
    for (auto it = c.queue.begin(); it != c.queue.end(); ++it)
        if ((*it)->done == state_) {
            if (it != c.queue.begin()) { auto job = std::move(*it); c.queue.erase(it); c.queue.push_front(std::move(job)); }
            return;
        }
}
bool ShaderCompileTicket::ready() const {
    if (!state_) throw std::logic_error("empty shader compile ticket");
    std::lock_guard lock(state_->mutex);
    return state_->ready;
}
std::optional<ShaderCompileResult> ShaderCompileTicket::wait_for(uint64_t budgetNs) {
    if (!state_) throw std::logic_error("empty shader compile ticket");
    std::unique_lock lock(state_->mutex);
    if (state_->consumed) throw std::logic_error("shader result already consumed");
    if (!state_->ready && budgetNs)
        state_->wake.wait_for(lock, std::chrono::nanoseconds(std::min<uint64_t>(budgetNs,
            uint64_t(std::numeric_limits<int64_t>::max()))), [&] { return state_->ready; });
    if (!state_->ready) return std::nullopt;
    if (state_->consumed) throw std::logic_error("shader result already consumed");
    state_->consumed = true;
    if (state_->error) std::rethrow_exception(state_->error);
    return std::move(state_->result);
}
ShaderCompileResult ShaderCompileTicket::consume() {
    if (!state_) throw std::logic_error("empty shader compile ticket");
    std::unique_lock lock(state_->mutex);
    state_->wake.wait(lock, [&] { return state_->ready; });
    if (state_->consumed) throw std::logic_error("shader result already consumed");
    state_->consumed = true;
    if (state_->error) std::rethrow_exception(state_->error);
    return std::move(state_->result);
}
std::optional<ShaderCompileTicket> request_shader_compile(const std::string& source,
    ShaderStage stage, uint64_t frame, uint64_t key, bool background) {
    if (source.size() > 4 * 1024 * 1024)
        throw std::runtime_error("shader compiler source exceeds 4 MiB");
    if (!background) {
        auto state = std::make_shared<ShaderCompileTicket::State>();
        try { state->result = execute(source, stage, frame, key); }
        catch (...) { state->error = std::current_exception(); }
        state->ready = true;
        return ShaderCompileTicket(std::move(state));
    }
    auto& c = compiler();
    std::lock_guard lock(c.mutex);
    if (c.stopped && c.thread.joinable())
        throw std::runtime_error("shader compiler shutdown in progress");
    if (c.queue.size() >= 8) return std::nullopt;
    auto state = std::make_shared<ShaderCompileTicket::State>();
    auto job = std::make_shared<Job>(Job{source, stage, frame, key, state});
    if (!c.thread.joinable()) { c.stopped = false; c.thread = std::thread(run, std::ref(c)); }
    c.queue.push_back(std::move(job));
    c.active.fetch_add(1, std::memory_order_release);
    if (stutter::trace().enabled()) try {
        const auto now = stutter::now();
        stutter::trace().add("shader_request", frame, key, now, now, int(c.queue.size()));
    } catch (...) {} // Trace allocation cannot strand an admitted job.
    c.wake.notify_one();
    return ShaderCompileTicket(std::move(state));
}
ShaderCompileResult compile_shader(const std::string& source, ShaderStage stage,
    uint64_t frame, uint64_t key, const std::function<void()>& whilePending, bool background) {
    const auto requested = stutter::trace().enabled() ? stutter::now() : 0;
    std::exception_ptr presentationError;
    auto service = [&] {
        if (!presentationError && whilePending) try { whilePending(); }
        catch (...) { presentationError = std::current_exception(); }
    };
    std::optional<ShaderCompileTicket> ticket;
    std::optional<std::string> admissionSource;
    while (!(ticket = request_shader_compile(admissionSource ? *admissionSource : source,
                                            stage, frame, key, background))) {
        // A presentation callback may mutate the original caller string. Keep
        // the requested bytes stable even while waiting for queue admission.
        if (!admissionSource) admissionSource.emplace(source);
        service();
        auto& c = compiler();
        std::unique_lock lock(c.mutex);
        c.wake.wait_for(lock, std::chrono::milliseconds(1));
    }
    std::optional<ShaderCompileResult> result;
    while (!(result = ticket->wait_for(1000000))) service();
    if (requested && background) try {
        stutter::trace().add("shader_pending", frame, key, requested, stutter::now(), result->words.empty() ? -1 : 0);
    } catch (...) {}
    if (presentationError) std::rethrow_exception(presentationError);
    return std::move(*result);
}
bool async_shaders_enabled() {
    static const bool enabled = [] {
        const auto* value = std::getenv("WWHD_VK_ASYNC_SHADERS");
        return !value || std::strcmp(value, "0");
    }();
    return enabled;
}
bool shader_compiler_active() { return compiler().active.load(std::memory_order_acquire) != 0; }
void finish_shader_compiler() { compiler().finish(); }
}
