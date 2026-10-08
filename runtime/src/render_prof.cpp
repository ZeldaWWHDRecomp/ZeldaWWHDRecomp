// Render-thread profiler (render_prof.h).
#include "render_prof.h"

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

bool enabled() {
    static const bool on = [] {
        const char* e = getenv("WWHD_PROFILE");
        return !e || strcmp(e, "0") != 0;
    }();
    return on;
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

uint64_t op_begin(Op op) {
    if (!enabled()) return 0;
    W.opCount[op]++;
    if ((op == kOpDraw || op == kOpRegs) && (++sampleCounter[op] & kSampleMask)) {
        if (op == kOpDraw) g_draw_sampled = false;  // also after a sampled draw that threw
        return 0;
    }
    uint64_t t = now_ns();
    if (op == kOpDraw) {
        g_draw_sampled = true;
        g_mark = t;
    }
    return t;
}
void op_end(Op op, uint64_t started) {
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

bool fast_class_reg(uint32_t reg) {
    // RegDefines.h: mmSQ_ALU_CONSTANT0_0 0xC000; mmSQ_TEX_RESOURCE_WORD0 0xE000 + 0x7E0 (VS uniform
    // blocks), + 0x250 (PS), + 0xCB0 (GS), + 0x8C0 (vertex attribute buffers), 7 words each
    if (reg >= 0xC000 && reg < 0xC000 + 0x1000) return true;
    for (uint32_t base : {0xE7E0u, 0xE250u, 0xECB0u, 0xE8C0u})
        if (reg >= base && reg < base + 7 * 16) return true;  // includes the vertex stride (pipeline-only)
    return false;
}
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

void add_sync(Sync site, uint64_t ns) {
    syncNs[site].fetch_add(ns, std::memory_order_relaxed);
    syncCount[site].fetch_add(1, std::memory_order_relaxed);
}

std::string latest_report() {
    std::lock_guard<std::mutex> lk(reportMutex);
    return report;
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
