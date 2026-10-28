// Shader head start: shaders pre-translated from the game's own shader archives (tools/shaderprep.py
// builds game/shadercache/headstart.bin from the game files and a template of recorded render
// states). Records use the runtime cache's recipe format:
//   type 1 ("known"): programs and states seen in play; translated at startup, compiled on first use or
//                     gradually in the background (like the user cache)
//   type 2 (pipeline): pipeline recipes seen in play, referencing type 1 shaders by key; built in the
//                     background once their shaders are compiled (like the user cache's)
//   type 3 (speculative): other archive programs with states of the same shader family; only
//                     `wwhd --warm-shaders` compiles them, to fill the macOS Metal shader cache
// `wwhd --warm-shaders` compiles all three kinds.
// WWHD_HEADSTART=<file> overrides the location, WWHD_HEADSTART=0 disables it.
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zlib.h>

#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "metal.h"
#include "warm_progress.h"
#include "vulkan/headstart_shader_record.h"
#include "runtime.h"

namespace gfx {

bool headstart_translate(const uint32_t* regs, bool vertex, bool compileNow, uint64_t* sharedKey = nullptr,
                         uint64_t* wideKey = nullptr);  // metal_draw.mm
void headstart_counts(size_t& shaders, size_t& pipelines);                     // metal_draw.mm
size_t headstart_failures();
size_t headstart_compiling();                                                   // metal_draw.mm
bool headstart_queue_pipeline(const uint8_t* raw, size_t size);                 // metal_draw.mm
size_t headstart_build_pipelines(int maxInFlight, size_t& built, size_t& dropped);  // metal_draw.mm

namespace {
constexpr uint32_t kRecShader = 1, kRecPipeline = 2, kRecSpeculative = 3;

std::string headstart_path() {
    if (const char* e = getenv("WWHD_HEADSTART")) return e;
    return config::game_dir + "/shadercache/headstart.bin";
}

// guest copies of programs and fetch shaders, shared by all records that use them
uint32_t guest_copy(std::unordered_map<std::string, uint32_t>& pool, const uint8_t* p, uint32_t size) {
    std::string key((const char*)p, size);
    auto it = pool.find(key);
    if (it != pool.end()) return it->second;
    uint32_t addr = mem::host_alloc(size, 0x100);
    memcpy(mem::ptr(addr), p, size);
    pool.emplace(std::move(key), addr);
    return addr;
}

struct Stats {
    size_t records = 0, translated = 0, failed = 0, pipelines = 0;
};

// counts the head start's shader records (types 1, 3 and 6) and Metal pipeline records (type 2)
// from the record headers alone, for `--warm-shaders` progress
void count_records(size_t& shaders, size_t& pipelines) {
    shaders = pipelines = 0;
    FILE* f = fopen(headstart_path().c_str(), "rb");
    if (!f) return;
    uint32_t hdr[3];
    while (fread(hdr, sizeof hdr, 1, f) == 1) {
        if (hdr[0] == kRecShader || hdr[0] == 6 || hdr[0] == kRecSpeculative) shaders++;
        else if (hdr[0] == kRecPipeline) pipelines++;
        if (fseek(f, hdr[2], SEEK_CUR)) break;
    }
    fclose(f);
}

// replays the records of the requested types; returns false if there is no head start
// (onShader: called after each shader record while warming)
bool replay(bool speculative, bool compileNow, Stats& st, void (*onShader)(const Stats&) = nullptr) {
    std::string path = headstart_path();
    if (path == "0") return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    static std::unordered_map<std::string, uint32_t> programs, fetch;
    static std::vector<uint32_t> regs(0x10000);
    uint32_t hdr[3];
    std::vector<uint8_t> comp, raw;
    while (fread(hdr, sizeof hdr, 1, f) == 1) {
        bool want = hdr[0] == kRecShader || hdr[0] == 6 || hdr[0] == kRecPipeline || (speculative && hdr[0] == kRecSpeculative);
        if (!want) {
            fseek(f, hdr[2], SEEK_CUR);
            continue;
        }
        comp.resize(hdr[2]);
        raw.resize(hdr[1]);
        if (fread(comp.data(), 1, hdr[2], f) != hdr[2]) break;
        uLongf rsize = hdr[1];
        if (uncompress(raw.data(), &rsize, comp.data(), hdr[2]) != Z_OK || rsize != hdr[1] || rsize < 16) break;
        if (hdr[0] == kRecPipeline) {
            st.pipelines += headstart_queue_pipeline(raw.data(), raw.size());
            continue;
        }
        gfxvk::HeadstartShaderRecord record;
        try { record = gfxvk::decode_headstart_shader_record(raw, hdr[0]); }
        catch (const std::exception& error) {
            LOG("[warm] invalid shader record: %s", error.what());
            fclose(f); return false;
        }
        const uint32_t vertex = record.vertex, size = record.program.size(), fsSize = record.fetch.size();
        const uint32_t n = record.registers.size() / 8;
        const uint8_t* p = record.program.data();
        std::fill(regs.begin(), regs.end(), 0);
        const uint8_t* rp = record.registers.data();
        for (uint32_t i = 0; i < n; i++) {
            uint32_t r, v;
            memcpy(&r, rp + i * 8, 4);
            memcpy(&v, rp + i * 8 + 4, 4);
            if (r < regs.size()) regs[r] = v;
        }
        const uint32_t stage = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
        regs[stage] = guest_copy(programs, p, size) >> 8;
        regs[stage + 1] = size >> 3;
        if (fsSize) regs[mmSQ_PGM_START_FS] = guest_copy(fetch, record.fetch.data(), fsSize) >> 8;
        st.records++;
        uint64_t shared = 0, wide = 0;
        const bool ok = headstart_translate(regs.data(), vertex != 0, compileNow, &shared, &wide);
        if (ok) st.translated++;
        else st.failed++;
        // debug: WWHD_HEADSTART_MAP=file lists per shader record (in file order) the shader it translates to
        // ("M <record> <vertex> <shared key or 0 when it failed> <own wide key>"), for template distilling
        static FILE* map = [] { const char* p = getenv("WWHD_HEADSTART_MAP"); return p && *p ? fopen(p, "w") : nullptr; }();
        if (map) fprintf(map, "M %zu %u %016llx %016llx\n", st.records - 1, vertex, ok ? (unsigned long long)shared : 0ull,
                         (unsigned long long)wide);
        // warming: keep the number of Metal compiles in flight bounded
        if (compileNow && st.records % 64 == 0)
            while (headstart_compiling() > 128) {
                if (onShader) onShader(st);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        if (onShader) onShader(st);
    }
    fclose(f);
    return true;
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

// called from cache_load (render thread, before the first draw): known shaders only
void headstart_load() {
    auto t0 = std::chrono::steady_clock::now();
    Stats st;
    if (replay(false, false, st))
        LOG("[gfx] shader head start: %zu records, %zu translated, %zu failed, %zu pipelines queued (%.0f ms) from %s",
            st.records, st.translated, st.failed, st.pipelines, ms_since(t0), headstart_path().c_str());
}

// `wwhd --warm-shaders`: translate and compile every head-start shader and pipeline once, so the macOS
// Metal shader cache holds the compiled code before the game first asks for it. Returns when done.
namespace {
size_t g_warm_shaders = 0, g_warm_total = 0;
// shaders whose compile has finished, while the records are replayed and compiled
void warm_shader_progress(const Stats& st) {
    const size_t compiling = headstart_compiling();
    warmprogress::report(st.records > compiling ? st.records - compiling : 0, g_warm_total, "shaders and pipelines");
}
}  // namespace

int headstart_warm() {
    auto t0 = std::chrono::steady_clock::now();
    size_t pipelineRecords = 0;
    count_records(g_warm_shaders, pipelineRecords);
    g_warm_total = g_warm_shaders + pipelineRecords;
    warmprogress::report(0, g_warm_total, "shaders and pipelines", true);
    Stats st;
    if (!replay(true, true, st, warm_shader_progress)) {
        LOG("[warm] no shader head start at %s (build it with tools/shaderprep.py build)", headstart_path().c_str());
        return 1;
    }
    // records the counting pass saw but the replay did not reach count as done
    g_warm_shaders = std::min(g_warm_shaders, st.records);
    g_warm_total = g_warm_shaders + st.pipelines;
    LOG("[warm] %zu records translated (%zu failed) in %.0f ms, compiling...", st.translated, st.failed, ms_since(t0));
    for (size_t left; (left = headstart_compiling()) > 0;) {
        static auto last = std::chrono::steady_clock::now();
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(5)) {
            last = std::chrono::steady_clock::now();
            LOG("[warm] %zu compiles pending", left);
        }
        warm_shader_progress(st);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    double tShaders = ms_since(t0);
    // pipelines: their shaders are all compiled (or failed) now
    size_t built = 0, dropped = 0;
    auto last = std::chrono::steady_clock::now();
    while (headstart_build_pipelines(128, built, dropped) > 0 || headstart_compiling() > 0) {
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(5)) {
            last = std::chrono::steady_clock::now();
            LOG("[warm] %zu of %zu pipelines started, %zu compiles pending", built, st.pipelines, headstart_compiling());
        }
        const size_t finished = built + dropped, compiling = headstart_compiling();
        warmprogress::report(g_warm_shaders + (finished > compiling ? finished - compiling : 0), g_warm_total,
                             "shaders and pipelines");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    warmprogress::report(g_warm_total, g_warm_total, "shaders and pipelines", true);
    LOG("[warm] %zu pipelines built (%zu unresolved) in %.1f s", built, dropped, (ms_since(t0) - tShaders) / 1000.0);
    const size_t compileFailures = headstart_failures();
    size_t distinctShaders = 0, distinctPipelines = 0;
    headstart_counts(distinctShaders, distinctPipelines);
    LOG("[warm] distinct: %zu shaders, %zu pipelines (from %zu shader records, %zu pipeline records)", distinctShaders,
        distinctPipelines, st.records, st.pipelines);
    LOG("[warm] done in %.1f s (%zu failed shader/pipeline compiles)", ms_since(t0) / 1000.0, compileFailures);
    return st.failed || compileFailures ? 1 : 0;
}

}  // namespace gfx

int gfx_headstart_warm() { return gfx::headstart_warm(); }  // main.cpp: --warm-shaders
