// Render-thread profiler (render_prof.h).
#include "render_prof.h"
#include "perf_metrics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

namespace rprof {

namespace {
std::atomic<bool> g_active{false};     // collecting (written by the render thread at a swap, or start())
std::atomic<bool> g_requested{false};  // the setting
}
const char* env_override() {
    static const char* const e = getenv("WWHD_PROFILE");
    return e;
}
static bool env_on() { return env_override() && strcmp(env_override(), "0") != 0; }
bool enabled() { return g_active.load(std::memory_order_relaxed); }
bool requested() { return env_override() ? env_on() : g_requested.load(std::memory_order_relaxed); }
static const char* source() { return env_override() ? (env_on() ? "WWHD_PROFILE=1" : "WWHD_PROFILE=0") : "setting"; }
void set_requested(bool on) {
    if (g_requested.exchange(on) == on) return;
    if (env_override()) LOG("[prof] detailed performance profiling setting %s; %s decides for this start", on ? "on" : "off", source());
}
void start(bool saved) {
    g_requested = saved;
    g_active = requested();
    LOG("[prof] detailed performance profiling %s (%s)", enabled() ? "on" : "off", source());
}
static bool log_reports() {
    static const bool on = [] {
        const char* p = getenv("WWHD_PROFILE");
        const char* c = getenv("WWHD_VK_CPU_ONLY_STATS");
        return (p && !strcmp(p, "1")) || getenv("WWHD_VK_STATS") || (c && !strcmp(c, "1"));
    }();
    return on;
}

uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
uint64_t thread_cpu_ns() {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    auto v = [](FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32 | f.dwLowDateTime) * 100; };
    return v(k) + v(u);
#else
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

// ---------------------------------------------------------------- render-thread state
bool g_draw_sampled = false;
bool g_sub_sampled = false;
uint64_t g_mark = 0;
int g_upload_kind = kUpOther;
bool g_track_unique = false;
uint32_t g_reg_dirty = 0;

namespace {
constexpr uint32_t kSampleMask = 63;  // draws and register writes: one call in 64 is timed
struct Window {
    uint64_t frames = 0, holdFrames = 0;
    uint64_t opCount[kOps]{}, opTimed[kOps]{}, opNs[kOps]{};
    uint64_t phaseNs[kDrawPhases]{};
    uint64_t subNs[kSubPhases]{}, subDraws = 0;
    uint64_t upload[kUploadKinds]{}, uploadHold[kUploadKinds]{};
    uint64_t uniqueFrames = 0, unique[kUploadKinds]{}, uniqueAll = 0, uniqueCopied[kUploadKinds]{};
    uint64_t waitNs[kWaits]{}, waitCount[kWaits]{};
    uint64_t idleNs = 0;
    uint64_t draws = 0, drawsSame = 0, drawsFast = 0, drawsOther = 0;
};
Window W;
uint64_t frameUpload[kUploadKinds]{};
std::array<std::vector<std::pair<uint64_t, uint64_t>>, kUploadKinds> reads;
std::vector<uint32_t> otherRegs(0x10000);  // changed-value writes of non-buffer registers, this window
uint64_t windowStart = 0, cpuStart = 0, frameCounter = 0, stepsStart = 0;
uint32_t sampleCounter[kOps]{};

struct ShaderStats {
    uint64_t variants = 0, newPrograms = 0, unusedOnly = 0;
    std::map<std::string, uint64_t> groups;
};
ShaderStats shaderStats;  // cumulative (render thread)

std::atomic<uint64_t> syncNs[kSyncs], syncCount[kSyncs];
std::mutex reportMutex;
std::string report;
}  // namespace

void mark_slow(Phase p) {
    uint64_t t = now_ns();
    W.phaseNs[p] += t - g_mark;
    g_mark = t;
}

// the cost of one now_ns() call, taken off each sub-phase interval (each holds one call's cost: the
// parts are short, a few tens of ns per stage, so the timer would otherwise be a good part of them)
static uint64_t timer_cost_ns() {
    static const uint64_t cost = [] {
        uint64_t best = ~0ull;
        for (int round = 0; round < 8; ++round) {
            const uint64_t t0 = now_ns();
            for (int i = 0; i < 256; ++i) (void)now_ns();
            best = std::min(best, (now_ns() - t0) / 257);
        }
        return best;
    }();
    return cost;
}
void sub_end_slow(SubPhase p, uint64_t started) {
    const uint64_t t = now_ns() - started, cost = timer_cost_ns();
    W.subNs[p] += t > cost ? t - cost : 0;
}

uint64_t op_begin(Op op) {
    if (!enabled()) return g_sub_sampled = false, 0;
    W.opCount[op]++;
    if ((op == kOpDraw || op == kOpRegs) && (++sampleCounter[op] & kSampleMask)) {
        if (op == kOpDraw) {
            g_draw_sampled = false;  // also after a sampled draw that threw
            // the sub-phase sample: half way between two timed draws
            if ((g_sub_sampled = (sampleCounter[op] & kSampleMask) == (kSampleMask + 1) / 2)) W.subDraws++;
        }
        return 0;
    }
    if (op == kOpDraw) g_sub_sampled = false;
    uint64_t t = now_ns();
    if (op == kOpDraw) {
        g_draw_sampled = true;
        g_mark = t;
    }
    return t;
}
void op_end(Op op, uint64_t started) {
    if (op == kOpDraw) g_sub_sampled = false;
    if (!started) return;
    W.opTimed[op]++;
    W.opNs[op] += now_ns() - started;
    if (op == kOpDraw) g_draw_sampled = false;
}

void add_upload(uint64_t bytes) { frameUpload[g_upload_kind] += bytes; }
void add_upload(Upload kind, uint64_t bytes) { frameUpload[kind] += bytes; }
void guest_read_slow(Upload kind, uint32_t addr, uint64_t size) {
    if (size) reads[kind].push_back({addr, (uint64_t)addr + size});
}
static uint64_t union_bytes(std::vector<std::pair<uint64_t, uint64_t>>& v) {
    std::sort(v.begin(), v.end());
    uint64_t total = 0, end = 0;
    for (auto& [a, b] : v) {
        if (b <= end) continue;
        total += b - std::max(a, end);
        end = b;
    }
    return total;
}

void add_wait(Wait w, uint64_t ns) {
    W.waitNs[w] += ns;
    W.waitCount[w]++;
}
void add_idle(uint64_t ns) { W.idleNs += ns; }

void note_other_reg(uint32_t reg) {
    g_reg_dirty |= 2;
    if (reg < otherRegs.size()) otherRegs[reg]++;
}
void classify_draw() {
    W.draws++;
    if (!g_reg_dirty) W.drawsSame++;
    else if (g_reg_dirty & 2) W.drawsOther++;
    else W.drawsFast++;
    g_reg_dirty = 0;
}

void shader_variant(bool newProgram, bool onlyUnusedUnits, const char* const* groups, int groupCount) {
    shaderStats.variants++;
    if (newProgram) shaderStats.newPrograms++;
    if (onlyUnusedUnits) shaderStats.unusedOnly++;
    for (int i = 0; i < groupCount; i++) shaderStats.groups[groups[i]]++;
}

std::atomic<uint64_t> wbWalkNs, wbWalks, wbCount, wbBytes, wbReadNs;
void add_write_back(uint64_t walkNs, uint32_t surfaces, uint64_t bytes, uint64_t readNs) {
    if (walkNs) {
        wbWalkNs.fetch_add(walkNs, std::memory_order_relaxed);
        wbWalks.fetch_add(1, std::memory_order_relaxed);
    }
    wbCount.fetch_add(surfaces, std::memory_order_relaxed);
    wbBytes.fetch_add(bytes, std::memory_order_relaxed);
    wbReadNs.fetch_add(readNs, std::memory_order_relaxed);
}

std::atomic<uint64_t> texChecks, texChanged, texHashed, texReleases, texReleasedBytes, texRemakes;
void add_texture_check(uint64_t hashedBytes, bool changed) {
    if (!enabled()) return;
    texChecks.fetch_add(1, std::memory_order_relaxed);
    texChanged.fetch_add(changed, std::memory_order_relaxed);
    texHashed.fetch_add(hashedBytes, std::memory_order_relaxed);
}
void add_texture_release(uint64_t bytes) {
    if (!enabled()) return;
    texReleases.fetch_add(1, std::memory_order_relaxed);
    texReleasedBytes.fetch_add(bytes, std::memory_order_relaxed);
}
void add_texture_remake() {
    if (enabled()) texRemakes.fetch_add(1, std::memory_order_relaxed);
}
std::mutex gpuSpanMutex;
double gpuLastEnd = 0, gpuBusyNs = 0, gpuIdleNs = 0, gpuMaxIdleNs = 0;
uint64_t gpuSpans = 0;
void add_gpu_span(double startNs, double endNs) {
    if (!enabled() || endNs < startNs) return;
    std::lock_guard<std::mutex> lk(gpuSpanMutex);
    gpuBusyNs += endNs - startNs;
    // a gap of more than a second is a pause (loading, a hidden window), not idle time between passes
    if (gpuLastEnd > 0 && startNs > gpuLastEnd && startNs - gpuLastEnd < 1e9) {
        gpuIdleNs += startNs - gpuLastEnd;
        gpuMaxIdleNs = std::max(gpuMaxIdleNs, startNs - gpuLastEnd);
    }
    gpuLastEnd = std::max(gpuLastEnd, endNs);
    ++gpuSpans;
}

void add_sync(Sync site, uint64_t ns) {
    syncNs[site].fetch_add(ns, std::memory_order_relaxed);
    syncCount[site].fetch_add(1, std::memory_order_relaxed);
}

std::string latest_report() {
    std::lock_guard<std::mutex> lk(reportMutex);
    return report;
}
std::string report_section() {
    std::string r = latest_report();
    if (!r.empty()) return r;
    if (enabled() || requested())
        return "Detailed profiling is on; its first report is ready a few seconds after it was turned on. Copy again then.\n";
    if (env_override())
        return "Detailed sections (timings, uploads, draw classes) are off: WWHD_PROFILE=0 is set for this start.\n";
    return "Detailed sections (timings, uploads, draw classes) are off. Turn on Settings > Graphics > "
           "Detailed performance profiling, play a few seconds, then copy again.\n";
}

// a change of the setting, at a swap: no op is being timed and no draw sampled then
static void apply_requested() {
    const bool on = requested();
    if (on == enabled()) return;
    W = Window{};
    std::fill(otherRegs.begin(), otherRegs.end(), 0);
    for (auto& v : frameUpload) v = 0;
    for (auto& r : reads) r.clear();
    for (auto& c : sampleCounter) c = 0;
    for (int i = 0; i < kSyncs; i++) syncNs[i] = 0, syncCount[i] = 0;
    wbWalkNs = wbWalks = wbCount = wbBytes = wbReadNs = 0;
    texChecks = texChanged = texHashed = texReleases = texReleasedBytes = texRemakes = 0;
    {
        std::lock_guard<std::mutex> lk(gpuSpanMutex);
        gpuLastEnd = gpuBusyNs = gpuIdleNs = gpuMaxIdleNs = 0;
        gpuSpans = 0;
    }
    g_draw_sampled = false;
    g_track_unique = false;
    g_reg_dirty = 0;
    windowStart = 0;
    frameCounter = 0;
    if (!on) {
        std::lock_guard<std::mutex> lk(reportMutex);
        report.clear();
    }
    g_active = on;
    LOG("[prof] detailed performance profiling %s (%s)", on ? "on" : "off", source());
}

// names of frequently changing context registers (others are printed as hex addresses)
static std::string reg_name(uint32_t r) {
    struct Range { uint32_t first, count; const char* name; };
    static const Range ranges[] = {
        {0xE000, 0x1000, "SQ_TEX_RESOURCE"}, {0xF000, 0x100, "SQ_TEX_SAMPLER"},
        {0xA210, 0x30, "SQ_PGM"}, {0xA010, 0x50, "CB_COLOR"}, {0xA000, 0x10, "DB_DEPTH"},
        {0xA100, 0x10, "CB_BLEND/STENCILREF"}, {0xA10F, 0x30, "PA_CL_VPORT"}, {0xA1E0, 0x8, "CB_BLEND_CONTROL"},
        {0xA200, 0x10, "DB/CB/PA control"}, {0xA080, 0x20, "PA_SC scissor"}, {0xA0C0, 0x40, "SQ_VTX_SEMANTIC"},
        {0xA180, 0x40, "SPI_PS_INPUT"}, {0x2200, 0x100, "VGT"},
    };
    char buf[64];
    for (const auto& g : ranges)
        if (r >= g.first && r < g.first + g.count) {
            snprintf(buf, sizeof buf, "%s(%04X)", g.name, r);
            return buf;
        }
    snprintf(buf, sizeof buf, "%04X", r);
    return buf;
}

void frame_end(bool hold) {
    perf::render_frame();
    static const bool metricDebug = getenv("WWHD_PERF_METRICS_DEBUG") != nullptr;
    static unsigned metricFrames = 0;
    if (metricDebug && ++metricFrames % 120 == 0) {
        auto c = perf::counters();
        LOG("[perf metrics] active %d, CPU reads %llu, GPU submissions %llu, timestamp writes %llu, query reads %llu, minimum age %llu frames\n%s",
            perf::enabled(), (unsigned long long)c.cpu_reads, (unsigned long long)c.gpu_submissions,
            (unsigned long long)c.timestamp_writes, (unsigned long long)c.query_reads,
            (unsigned long long)c.minimum_query_age, perf::summary().c_str());
    }
    apply_requested();
    if (!enabled()) return;
    const uint64_t t = now_ns();
    if (!windowStart) {
        windowStart = t;
        cpuStart = thread_cpu_ns();
        stepsStart = interp::logic_steps();
        for (auto& v : frameUpload) v = 0;
        for (auto& r : reads) r.clear();
        return;
    }
    W.frames++;
    if (hold) W.holdFrames++;
    for (int k = 0; k < kUploadKinds; k++) (hold ? W.uploadHold : W.upload)[k] += frameUpload[k];
    if (g_track_unique) {
        W.uniqueFrames++;
        std::vector<std::pair<uint64_t, uint64_t>> all;
        for (int k = 0; k < kUploadKinds; k++) {
            all.insert(all.end(), reads[k].begin(), reads[k].end());
            W.unique[k] += union_bytes(reads[k]);
            W.uniqueCopied[k] += frameUpload[k];
            reads[k].clear();
        }
        W.uniqueAll += union_bytes(all);
    }
    for (auto& v : frameUpload) v = 0;
    g_track_unique = (++frameCounter % 16) == 0;
    if (W.frames < 120) return;

    // ---- report
    const double frames = (double)W.frames, wallMs = (t - windowStart) / 1e6;
    const uint64_t cpu = thread_cpu_ns(), steps = interp::logic_steps();
    auto opMs = [&](int op) {  // estimated ms per frame (sampled ops scaled up)
        return W.opTimed[op] ? W.opNs[op] / 1e6 * ((double)W.opCount[op] / W.opTimed[op]) / frames : 0.0;
    };
    std::string out;
    char line[1024];
    auto add = [&](const char* fmt, auto... args) {
        snprintf(line, sizeof line, fmt, args...);
        out += line;
        out += '\n';
    };
    double busy = 0;
    for (int op = 0; op < kOps; op++) busy += opMs(op);
    add("[prof] frame %llu: %.0f frames (%.0f hold), %.2f ms/frame, %.1f swaps/s, %.1f logic steps/s; render thread "
        "CPU %.2f ms/frame, in ops %.2f ms/frame, idle (waiting for commands) %.2f ms/frame",
        (unsigned long long)frameCounter, frames, (double)W.holdFrames, wallMs / frames, frames / (wallMs / 1e3),
        (steps - stepsStart) / (wallMs / 1e3), (cpu - cpuStart) / 1e6 / frames, busy, W.idleNs / 1e6 / frames);
    static const char* opNames[kOps] = {"regs", "draw", "clear", "copy", "scan", "invalidate", "flush", "drawdone", "swap", "other"};
    std::string ops;
    for (int op = 0; op < kOps; op++) {
        if (!W.opCount[op]) continue;
        snprintf(line, sizeof line, " %s %.2f (%.0f/frame)", opNames[op], opMs(op), W.opCount[op] / frames);
        ops += line;
    }
    add("[prof] ops ms/frame:%s", ops.c_str());
    if (W.opTimed[kOpDraw]) {
        static const char* phaseNames[kDrawPhases] = {"shader", "indices", "targets", "pipeline", "uniforms", "textures",
                                                      "descriptors", "pass", "record", "vertex", "submit"};
        const double scale = (double)W.opCount[kOpDraw] / W.opTimed[kOpDraw] / frames / 1e6;
        std::string ph;
        double sum = 0;
        for (int p = 0; p < kDrawPhases; p++) {
            snprintf(line, sizeof line, " %s %.2f", phaseNames[p], W.phaseNs[p] * scale);
            ph += line;
            sum += W.phaseNs[p] * scale;
        }
        add("[prof] draw phases ms/frame (1 draw in %u sampled):%s; untracked %.2f; %.2f us/draw", kSampleMask + 1,
            ph.c_str(), std::max(0.0, opMs(kOpDraw) - sum), opMs(kOpDraw) * 1e3 * frames / std::max<uint64_t>(1, W.opCount[kOpDraw]));
        if (W.subDraws) {
            const double subScale = (double)W.opCount[kOpDraw] / W.subDraws / frames / 1e6;
            add("[prof] uniforms parts ms/frame (other 1 draw in %u sampled, timer cost %llu ns taken off): stage setup "
                "%.2f pack %.2f blocks %.2f support upload %.2f", kSampleMask + 1, (unsigned long long)timer_cost_ns(), W.subNs[kSubUniformSetup] * subScale,
                W.subNs[kSubUniformPack] * subScale, W.subNs[kSubUniformBlocks] * subScale,
                W.subNs[kSubUniformSupportUpload] * subScale);
        }
    }
    static const char* syncNames[kSyncs] = {"DrawDone", "CopySurface", "flip", "other"};
    std::string syncs;
    for (int s = 0; s < kSyncs; s++) {
        uint64_t n = syncCount[s].exchange(0), ns = syncNs[s].exchange(0);
        if (!n) continue;
        snprintf(line, sizeof line, " %s %.2f ms (%.2f/frame)", syncNames[s], ns / 1e6 / frames, n / frames);
        syncs += line;
    }
    add("[prof] render thread waits ms/frame: GPU %.2f (%.2f/frame) acquire %.2f present %.2f; game thread waits for the "
        "render thread:%s",
        W.waitNs[kWaitGpu] / 1e6 / frames, W.waitCount[kWaitGpu] / frames, W.waitNs[kWaitAcquire] / 1e6 / frames,
        W.waitNs[kWaitPresent] / 1e6 / frames, syncs.empty() ? " none" : syncs.c_str());
    {
        uint64_t walks = wbWalks.exchange(0), walkNs = wbWalkNs.exchange(0), n = wbCount.exchange(0), bytes = wbBytes.exchange(0),
                 readNs = wbReadNs.exchange(0);
        add("[prof] write-back to guest memory: %llu surfaces, %.1f KiB, read %.3f ms; DrawDone checks %.2f/frame, %.2f us each",
            (unsigned long long)n, bytes / 1024.0, readNs / 1e6, walks / frames, walks ? walkNs / 1e3 / walks : 0.0);
    }
    {
        const uint64_t checks = texChecks.exchange(0), changed = texChanged.exchange(0), hashed = texHashed.exchange(0),
                       releases = texReleases.exchange(0), releasedBytes = texReleasedBytes.exchange(0),
                       remakes = texRemakes.exchange(0);
        add("[prof] texture cache: %.2f full checks/frame (%.2f MiB/frame hashed), %llu served from the cache, %llu "
            "uploaded again; budget: %llu released (%.1f MiB), %llu made again",
            checks / frames, hashed / frames / 1048576.0, (unsigned long long)(checks - changed), (unsigned long long)changed,
            (unsigned long long)releases, releasedBytes / 1048576.0, (unsigned long long)remakes);
        std::lock_guard<std::mutex> lk(gpuSpanMutex);
        if (gpuSpans)
            add("[prof] GPU timeline: busy %.2f ms/frame, idle between command buffers %.2f ms/frame (longest gap %.2f ms), "
                "%.1f command buffers/frame",
                gpuBusyNs / 1e6 / frames, gpuIdleNs / 1e6 / frames, gpuMaxIdleNs / 1e6, gpuSpans / frames);
        gpuBusyNs = gpuIdleNs = gpuMaxIdleNs = 0;
        gpuSpans = 0;
    }
    static const char* upNames[kUploadKinds] = {"other", "vertex", "index", "ubo", "uniforms", "texture"};
    const double logicFrames = std::max(1.0, frames - W.holdFrames), holdFrames = std::max(1.0, (double)W.holdFrames);
    std::string up, upHold, uniq;
    double upTotal = 0;
    for (int k = 0; k < kUploadKinds; k++) {
        snprintf(line, sizeof line, " %s %.2f", upNames[k], W.upload[k] / logicFrames / 1048576.0);
        up += line;
        upTotal += (W.upload[k] + W.uploadHold[k]) / frames / 1048576.0;
        snprintf(line, sizeof line, " %s %.2f", upNames[k], W.uploadHold[k] / holdFrames / 1048576.0);
        upHold += line;
    }
    add("[prof] uploads MiB/frame %.2f; logic/30 fps frames:%s%s%s", upTotal, up.c_str(),
        W.holdFrames ? "; hold frames:" : "", W.holdFrames ? upHold.c_str() : "");
    if (W.uniqueFrames) {
        const double uf = (double)W.uniqueFrames;
        for (int k : {kUpVertex, kUpIndex, kUpUbo}) {
            snprintf(line, sizeof line, " %s %.2f of %.2f copied", upNames[k], W.unique[k] / uf / 1048576.0,
                     W.uniqueCopied[k] / uf / 1048576.0);
            uniq += line;
        }
        add("[prof] unique guest MiB/frame (%.0f frames sampled): all %.2f;%s", uf, W.uniqueAll / uf / 1048576.0, uniq.c_str());
    }
    if (W.draws) {
        std::vector<std::pair<uint32_t, uint32_t>> top;
        for (uint32_t r = 0; r < otherRegs.size(); r++)
            if (otherRegs[r]) top.push_back({otherRegs[r], r});
        std::sort(top.rbegin(), top.rend());
        std::string regs;
        for (size_t i = 0; i < top.size() && i < 8; i++) {
            snprintf(line, sizeof line, " %s %.0f", reg_name(top[i].second).c_str(), top[i].first / frames);
            regs += line;
        }
        add("[prof] draw classes: %.1f%% same registers, %.1f%% only buffer pointers/ALU constants, %.1f%% other "
            "(%.0f draws/frame); top other writes/frame:%s",
            100.0 * W.drawsSame / W.draws, 100.0 * W.drawsFast / W.draws, 100.0 * W.drawsOther / W.draws, W.draws / frames,
            regs.c_str());
    }
    if (shaderStats.variants) {
        std::vector<std::pair<uint64_t, std::string>> g;
        for (auto& [k, v] : shaderStats.groups) g.push_back({v, k});
        std::sort(g.rbegin(), g.rend());
        std::string groups;
        for (size_t i = 0; i < g.size() && i < 10; i++) {
            snprintf(line, sizeof line, " %s %llu", g[i].second.c_str(), (unsigned long long)g[i].first);
            groups += line;
        }
        add("[prof] shader translations so far %llu: %llu new programs, %llu differ from an existing variant only in "
            "unused texture units/samplers; differing words vs. nearest variant:%s",
            (unsigned long long)shaderStats.variants, (unsigned long long)shaderStats.newPrograms,
            (unsigned long long)shaderStats.unusedOnly, groups.c_str());
    }
    if (log_reports()) {
        size_t p = 0;
        while (p < out.size()) {
            size_t e = out.find('\n', p);
            LOG("%s", out.substr(p, e - p).c_str());
            p = e + 1;
        }
    }
    {
        std::lock_guard<std::mutex> lk(reportMutex);
        report = out;
    }
    W = Window{};
    std::fill(otherRegs.begin(), otherRegs.end(), 0);
    windowStart = t;
    cpuStart = cpu;
    stepsStart = steps;
}

}  // namespace rprof
