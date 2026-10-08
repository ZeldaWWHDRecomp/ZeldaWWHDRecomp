#include "renderer.h"
#include <chrono>
extern "C" uint64_t g_shader_state_gen;  // gx2_core.cpp: bumped by shader-relevant register changes
// Draw calls: shader translation (via the vendored decompiler), pipelines,
// resource binding and primitive submission. Binding conventions follow the
// MSL the decompiler emits (as used by Cemu's Metal renderer).
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "gfx/area_sample.h"
#include "gfx/shader_identity.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"
#include "gx2/gx2.h"
#include "render_prof.h"
#include "gx2/gx2_cmd.h"
#include "write_watch.h"
#include "metal.h"
#include "runtime.h"
#include "util/helpers/StringBuf.h"

#include <condition_variable>
#include <mutex>
#include <set>
#include <cstdarg>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

LatteDecompilerShader* FinishDecompiledShader(LatteDecompilerOutput_t& decompilerOutput);
LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash fsHash, uint32* contextRegister,
                                                           uint32* fsProgramCode, uint32 fsProgramSize);

using namespace Latte;

namespace gfx {
static float bitsf_(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

// debug: WWHD_LOG_FRAME=n or n-m logs every draw of those frames
static uint64_t g_log_frame = ~0ull, g_log_frame_end = 0;
static bool g_log_init = [] {
    if (const char* e = getenv("WWHD_LOG_FRAME")) {
        g_log_frame = strtoull(e, (char**)&e, 10);
        g_log_frame_end = *e == '-' ? strtoull(e + 1, nullptr, 10) : g_log_frame;
    }
    return true;
}();
// F12 capture: the next frame's draw log goes to captures/<time>/draws.log and every
// full-screen pass (post-processing / deferred lighting) has its target dumped there
static std::atomic<bool> g_capture_requested{false};
static uint64_t g_capture_frame = ~0ull;
static std::string g_capture_dir;
static FILE* g_capture_log = nullptr;
void request_capture() { g_capture_requested = true; }
static bool capturing() { return R.frame == g_capture_frame; }
bool log_this_frame() { return capturing() || (R.frame >= g_log_frame && R.frame <= g_log_frame_end); }
static void dlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void dlog(const char* fmt, ...) {
    char line[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (capturing() && g_capture_log) fprintf(g_capture_log, "%s\n", line);
    else LOG("%s", line);
}
#define DLOG(...) do { if (log_this_frame()) dlog(__VA_ARGS__); } while (0)

// called from swap() after R.frame advanced; returns the capture directory when this frame is captured
const char* capture_begin_frame() {
    if (g_capture_log) { fclose(g_capture_log); g_capture_log = nullptr; }
    static uint64_t envFrame = getenv("WWHD_CAPTURE") ? strtoull(getenv("WWHD_CAPTURE"), nullptr, 10) : ~0ull;  // scripted F12
    if (!g_capture_requested.exchange(false) && R.frame != envFrame) return nullptr;
    char dir[64];
    time_t t = time(nullptr);
    strftime(dir, sizeof dir, "captures/%Y%m%d-%H%M%S", localtime(&t));
    mkdir("captures", 0755);
    mkdir(dir, 0755);
    g_capture_dir = dir;
    g_capture_frame = R.frame;
    g_capture_log = fopen((g_capture_dir + "/draws.log").c_str(), "w");
    LOG("[gfx] capturing frame %llu to %s", (unsigned long long)R.frame, dir);
    return g_capture_dir.c_str();
}

// fast 64-bit hash over 8-byte words (tail handled bytewise)
static uint64_t hash_bytes(const void* data, size_t n, uint64_t h = 0x9E3779B97F4A7C15ull) {
    const uint8_t* p = (const uint8_t*)data;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h ^ (h >> 29);
}

// shader programs rarely change in place: cache their hash per address, revalidated once per frame
struct ProgramHash {
    uint32_t size;
    uint64_t hash;
    uint64_t frame;
};
static std::unordered_map<uint32_t, ProgramHash> g_program_hashes;
static uint64_t program_hash(uint32_t addr, uint32_t size) {
    auto& e = g_program_hashes[addr];
    if (e.size != size || e.frame != R.frame) {
        e.size = size;
        e.hash = hash_bytes(mem::ptr(addr), size);
        e.frame = R.frame;
    }
    return e.hash;
}
static uint64_t hash_regs(const uint32_t* regs, uint32_t first, uint32_t count, uint64_t h) {
    return hash_bytes(&regs[first], count * 4, h);
}

// ---------------------------------------------------------------- upload pool
// Transient per-draw data (indices, large support buffers) comes from shared
// buffers recycled once the GPU is done with them.
struct Upload {
    id<MTLBuffer> buf;
    uint32_t offset;
};
static std::mutex g_pool_mutex;
static std::vector<id<MTLBuffer>> g_pool_free;
static std::vector<id<MTLBuffer>> g_pool_used;
static id<MTLBuffer> g_pool_cur = nil;
static uint32_t g_pool_pos = 0;
constexpr uint32_t kPoolChunk = 8 << 20;

static Upload upload(const void* data, uint32_t size) {
    size = (size + 255) & ~255u;
    if (!g_pool_cur || g_pool_pos + size > g_pool_cur.length) {
        std::lock_guard<std::mutex> lk(g_pool_mutex);
        if (g_pool_cur) g_pool_used.push_back(g_pool_cur);
        uint32_t want = std::max(kPoolChunk, size);
        g_pool_cur = nil;
        for (size_t i = 0; i < g_pool_free.size(); i++)
            if (g_pool_free[i].length >= want) {
                g_pool_cur = g_pool_free[i];
                g_pool_free.erase(g_pool_free.begin() + i);
                break;
            }
        if (!g_pool_cur) g_pool_cur = [R.device newBufferWithLength:want options:MTLResourceStorageModeShared];
        g_pool_pos = 0;
    }
    Upload u{g_pool_cur, g_pool_pos};
    memcpy((uint8_t*)g_pool_cur.contents + g_pool_pos, data, size);
    rprof::add_upload(size);
    g_pool_pos += size;
    return u;
}

// called before a command buffer commits: buffers used so far are recycled when it completes
void pool_retire(id<MTLCommandBuffer> cmd) {
    std::vector<id<MTLBuffer>> used;
    {
        std::lock_guard<std::mutex> lk(g_pool_mutex);
        if (g_pool_cur) {
            g_pool_used.push_back(g_pool_cur);
            g_pool_cur = nil;
        }
        used.swap(g_pool_used);
    }
    if (used.empty()) return;
    __block std::vector<id<MTLBuffer>> blockUsed = std::move(used);
    [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
        std::lock_guard<std::mutex> lk(g_pool_mutex);
        for (auto& b : blockUsed) g_pool_free.push_back(b);
    }];
}

// ---------------------------------------------------------------- fetch shaders
struct FetchShaderEntry {
    LatteFetchShader* fs;
};
static std::unordered_map<uint64_t, LatteFetchShader*> g_fetch;

static LatteFetchShader* get_fetch_shader(const uint32_t* regs, uint64_t* keyOut) {
    uint32_t prog = regs[mmSQ_PGM_START_FS] << 8;
    if (!prog) return nullptr;
    // either our compact encoding (from GX2InitFetchShaderEx) or real fetch shader microcode shipped with the game
    bool ours = ld32(prog) == 0x57574653;
    uint32_t size = ours ? 16 + ld32(prog + 4) * 16 : regs[mmSQ_PGM_START_FS + 1] << 3;
    if (size > 0x1000) return nullptr;
    uint64_t h = hash_bytes(mem::ptr(prog), size);
    // strides decide between vertex-descriptor fetch and manual fetch in the shader
    for (uint32_t b = 0; b < 16; b++) h = (h ^ ((regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + b * 7 + 2] >> 11) & 0xFFFF)) * 1099511628211ull;
    *keyOut = h;
    auto it = g_fetch.find(h);
    if (it != g_fetch.end()) return it->second;
    LatteFetchShader* fs = ours ? gx2::build_fetch_shader(prog)
                                : LatteShaderRecompiler_createFetchShader(h, (uint32*)regs, (uint32*)mem::ptr(prog), size);
    if (fs && ours) {
        for (auto& g : fs->bufferGroups) {
            uint32_t stride = (regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
            if (stride % 4) fs->mtlFetchVertexManually = true;
            for (sint32 i = 0; i < g.attribCount; i++)
                if (g.attrib[i].offset + GetMtlVertexFormatSize(g.attrib[i].format) > stride) fs->mtlFetchVertexManually = true;
        }
    }
    g_fetch[h] = fs;
    return fs;
}

// ---------------------------------------------------------------- shaders
// Shaders and pipelines compile in the background (WWHD_SYNC_SHADERS=1 waits instead); draws that
// need one still compiling are skipped for those frames rather than stalling the game.
static const bool g_sync_shaders = getenv("WWHD_SYNC_SHADERS") != nullptr;
enum CompileState { CS_PENDING, CS_READY, CS_FAILED, CS_DEFERRED };  // deferred: translated from the cache, not compiled yet

// Metal's completion handlers publish results through compile_done, which wakes the render thread
// if it's blocked in wait_compiled
static std::mutex g_compile_mu;
static std::condition_variable g_compile_cv;
static void compile_done(std::atomic<int>& st, int v) {
    {
        std::lock_guard<std::mutex> lk(g_compile_mu);
        st.store(v, std::memory_order_release);
    }
    g_compile_cv.notify_all();
}

struct Shader {
    uint64_t key = 0;
    gfx::ProgramKind kind = gfx::ProgramKind::Other;
    LatteDecompilerShader* dec = nullptr;
    id<MTLFunction> fn = nil;                 // valid once state == CS_READY
    std::atomic<int> state{CS_PENDING};
    int attempts = 0;                         // Metal compiles started (render thread)
    uint64_t retryFrame = 0;                  // a failed compile is retried from this frame on (0: not scheduled)
};

// A Metal compile or pipeline build can fail for reasons that have nothing to do with the source:
// the compiler service (MTLCompilerService) being interrupted or out of memory, which is more likely
// during the startup burst of cache replay and background builds. A failure used to be final for
// the whole session, so one unlucky compile could drop a pass for good (issue #47: the ambient
// occlusion pass missing left every shadowed area black until the next launch). Failed compiles are
// retried a few times on later frames (backoff kRetryFrames * attempts); translation errors (no
// MSL at all) still fail at once.
// test aid: WWHD_TEST_FAIL_COMPILES=n fails the first n attempts of every shader and pipeline compile
constexpr int kCompileAttempts = 4;
constexpr uint64_t kRetryFrames = 30;
static const int g_test_fail_compiles = getenv("WWHD_TEST_FAIL_COMPILES") ? atoi(getenv("WWHD_TEST_FAIL_COMPILES")) : 0;

// render thread: true when a failed compile with attempts left is due for its retry (schedules the
// retry on the first call after the failure)
// (now: retry at once, for a draw that must not be skipped; see wait_compiled)
template <class T>
static bool retry_due(T* x, bool now = false) {
    if (x->attempts >= kCompileAttempts) return false;
    if (now) { x->retryFrame = 0; return true; }
    if (!x->retryFrame) {
        x->retryFrame = R.frame + kRetryFrames * (uint64_t)x->attempts;
        return false;
    }
    if (R.frame < x->retryFrame) return false;
    x->retryFrame = 0;
    return true;
}
static std::unordered_map<uint64_t, Shader*> g_shaders;
// Metal compiles (shaders and pipelines) started and not finished yet; background work holds back while it's high
static std::atomic<int> g_compiles_in_flight{0};
// time spent per stage, reported with the skip statistics
static double g_t_decompile, g_t_msl, g_t_pipeline;
static double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static void report_compile_error(const char* src, uint64_t key, NSError* err, int attempt, bool libraryBuilt) {
    const char* why = libraryBuilt ? "entry point missing" : err ? err.localizedDescription.UTF8String : "no error given";
    LOG("[gfx] shader %016llx failed to compile (attempt %d of %d%s): %s", (unsigned long long)key, attempt, kCompileAttempts,
        attempt < kCompileAttempts ? ", will retry" : "", why ? why : "?");
    static std::atomic<int> dumped{0};
    if (dumped++ < 3) {
        FILE* f = fopen([NSString stringWithFormat:@"failed_shader_%016llx.metal", (unsigned long long)key].UTF8String, "w");
        if (f) { fputs(src, f); fclose(f); }
    }
}

// Latte addresses texels in fixed point (1/256 texel); Apple GPUs use full float precision. A point
// sample placed exactly on a texel boundary (e.g. the game's 2:1 depth downsample) then picks a
// neighbour depending on interpolation noise, which shows up as streaks in ambient occlusion.
// Snapping 2D float-texture coordinates to the 1/256 grid makes those picks consistent again.
static std::string snap_texcoords(const char* src) {
    static const bool off = getenv("WWHD_NO_UV_SNAP") != nullptr;
    std::string s = src;
    if (off) return s;
    // one pass each over the declarations and the sample calls (this runs on the render thread for
    // every compile); the output is byte-identical to wrapping each texture slot in turn, which keeps
    // the system Metal cache (keyed by source) valid
    static const char kDecl[] = "texture2d<float> tex", kCall[] = ".sample(samplr";
    uint32_t is2d = 0;
    for (size_t p = s.find(kDecl); p != std::string::npos; p = s.find(kDecl, p + 1)) {
        char* end;
        long t = strtol(s.c_str() + p + strlen(kDecl), &end, 10);
        if (end != s.c_str() + p + strlen(kDecl) && t >= 0 && t < 32 && !strncmp(end, " [[", 3)) is2d |= 1u << t;
    }
    if (!is2d) return s;
    // insertion points in the original text: "wwhd_snap(texN, " before float2(, ")" after its close
    std::vector<std::pair<size_t, int>> ins;  // (position, slot), slot < 0: closing paren
    uint32_t broken = 0;
    for (size_t p = s.find(kCall); p != std::string::npos; p = s.find(kCall, p + 1)) {
        size_t d = p;
        while (d > 0 && isdigit((unsigned char)s[d - 1])) d--;
        if (d == p || d < 3 || s.compare(d - 3, 3, "tex")) continue;
        int t = atoi(s.c_str() + d);
        if (t >= 32 || !(is2d >> t & 1) || (broken >> t & 1)) continue;
        char call[32];
        int n = snprintf(call, sizeof call, "%d.sample(samplr%d, float2(", t, t);
        if (s.compare(d, n, call)) continue;
        size_t arg = d + n - 7, e = arg + 6;
        for (int depth = 0; e < s.size(); e++) {
            if (s[e] == '(') depth++;
            else if (s[e] == ')' && --depth == 0) break;
        }
        if (e >= s.size()) { broken |= 1u << t; continue; }
        ins.push_back({arg, t});
        ins.push_back({e + 1, -1});
    }
    if (ins.empty()) return s;
    std::stable_sort(ins.begin(), ins.end(), [](auto& x, auto& y) { return x.first < y.first || (x.first == y.first && x.second < y.second); });
    std::string out =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "static inline float2 wwhd_snap(texture2d<float> t, float2 uv) {\n"
        "    float2 sz = float2(t.get_width(), t.get_height()) * 256.0;\n"
        "    return rint(uv * sz) / sz;\n}\n";
    out.reserve(out.size() + s.size() + ins.size() * 16);
    size_t at = 0;
    for (auto& [pos, t] : ins) {
        out.append(s, at, pos - at);
        at = pos;
        if (t < 0) out += ')';
        else out += "wwhd_snap(tex" + std::to_string(t) + ", ";
    }
    out.append(s, at, std::string::npos);
    return out;
}

static void compile_msl(Shader* sh, const char* rawSrc, uint64_t key) {
    const int attempt = ++sh->attempts;
    const bool injectFailure = attempt <= g_test_fail_compiles;
    MTLCompileOptions* opt = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) {
        opt.mathMode = MTLMathModeSafe;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        opt.fastMathEnabled = NO;  // the pre-15 spelling of MTLMathModeSafe
#pragma clang diagnostic pop
    }
    opt.languageVersion = MTLLanguageVersion3_0;
    std::string snapped = snap_texcoords(rawSrc);
    const char* src = snapped.c_str();
    NSString* source = [NSString stringWithUTF8String:src];
    // test aid: WWHD_MSL_NONCE=<text> changes every source so the system Metal cache misses (fresh install).
    // The entry point is renamed too: the GPU-code cache behind pipeline creation is keyed by the compiled
    // function, which a comment doesn't change.
    NSString* entry = @"main0";
    if (const char* nonce = getenv("WWHD_MSL_NONCE")) {
        std::string name = "main0_";
        for (const char* c = nonce; *c; c++) name += isalnum((unsigned char)*c) ? *c : '_';
        entry = [NSString stringWithUTF8String:name.c_str()];
        source = [[NSString stringWithFormat:@"// %s\n%@", nonce, source]
            stringByReplacingOccurrencesOfString:@" main0(" withString:[NSString stringWithFormat:@" %@(", entry]];
    }
    if (g_sync_shaders) {
        NSError* err = nil;
        id<MTLLibrary> lib = injectFailure ? nil : [R.device newLibraryWithSource:source options:opt error:&err];
        id<MTLFunction> fn = lib ? [lib newFunctionWithName:entry] : nil;
        if (!fn) { report_compile_error(src, key, err, attempt, lib != nil); sh->state = CS_FAILED; return; }
        sh->fn = fn;
        sh->state = CS_READY;
        return;
    }
    std::string copy = src;
    g_compiles_in_flight++;
    [R.device newLibraryWithSource:source options:opt completionHandler:^(id<MTLLibrary> lib, NSError* err) {
        g_compiles_in_flight--;
        if (injectFailure) lib = nil;
        id<MTLFunction> fn = lib ? [lib newFunctionWithName:entry] : nil;
        if (!fn) { report_compile_error(copy.c_str(), key, err, attempt, lib != nil); compile_done(sh->state, CS_FAILED); return; }
        sh->fn = fn;
        compile_done(sh->state, CS_READY);
    }];
}

// registers that influence how a shader stage is translated (gathered, then hashed in one pass)
static uint64_t stage_state_hash(const uint32_t* regs, uint64_t h, uint32_t texBase) {
    // most lookups gather the same words as the previous one for this stage (the state generation
    // also moves for registers that don't end up here): gather into the spare buffer and compare
    // instead of rehashing. Render thread only.
    struct Last { uint64_t in = 0, out = 0; uint32_t n = 0, cur = 0; uint32_t buf[2][400]; };
    static Last last[2];
    Last& L = last[texBase == REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS ? 0 : 1];
    uint32_t* buf = L.buf[L.cur ^ 1];
    uint32_t n = 0;
    auto put = [&](uint32_t first, uint32_t count) { memcpy(&buf[n], &regs[first], count * 4); n += count; };
    put(mmSQ_VTX_SEMANTIC_0, 32);
    put(mmSPI_VS_OUT_ID_0, 10);
    put(mmSPI_VS_OUT_CONFIG, 1);
    put(mmPA_CL_VS_OUT_CNTL, 1);
    put(mmSPI_PS_IN_CONTROL_0, 2);
    put(mmSPI_PS_INPUT_CNTL_0, 32);
    put(REGADDR::SQ_CONFIG, 1);
    put(mmCB_SHADER_MASK, 1);
    put(mmCB_SHADER_CONTROL, 1);
    put(mmDB_SHADER_CONTROL, 1);
    put(mmSPI_INPUT_Z, 1);
    put(REGADDR::SX_ALPHA_TEST_CONTROL, 1);
    put(REGADDR::PA_CL_VTE_CNTL, 1);
    put(REGADDR::PA_CL_CLIP_CNTL, 1);
    put(REGADDR::CB_COLOR_CONTROL, 1);
    put(REGADDR::CB_TARGET_MASK, 1);
    put(mmCB_COLOR0_INFO, 8);
    uint32_t cbBase[8];
    for (int i = 0; i < 8; i++) {
        cbBase[i] = regs[mmCB_COLOR0_BASE + i] & ~0xFFu;
        buf[n++] = cbBase[i] != 0;
    }
    buf[n++] = regs[REGADDR::DB_DEPTH_CONTROL] & 0x83;
    // texture types and formats; sampling a bound render target switches to framebuffer fetch
    for (int t = 0; t < 18; t++) {
        const uint32_t* w = &regs[texBase + t * 7];
        buf[n++] = (w[0] & 7) | (w[4] & 0x300);
        buf[n++] = w[1] & 0x3F00000;
        uint32_t fb = 0, base = w[2] << 8;
        if (base)
            for (int i = 0; i < 8; i++)
                if (base == cbBase[i]) fb = 1 + i;
        buf[n++] = fb;
    }
    // depth-compare samplers
    for (int i = 0; i < 18 * 3; i++) buf[n++] = regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + i * 3] & 0xF8000000;
    if (L.n == n && L.in == h && !memcmp(L.buf[L.cur], buf, n * 4)) return L.out;
    L.cur ^= 1;
    L.in = h;
    L.n = n;
    return L.out = hash_bytes(buf, n * 4, h);
}

static Shader* get_shader_uncached(const uint32_t* regs, bool vertex, LatteFetchShader* fs, uint64_t fsKey);

static Shader* get_shader(const uint32_t* regs, bool vertex, LatteFetchShader* fs, uint64_t fsKey) {
    // nothing shader-relevant changed since the previous draw: same shader
    struct Last { uint64_t gen = 0, frame = ~0ull, fsKey = 0; Shader* s = nullptr; };
    static Last last[2];
    Last& L = last[vertex ? 0 : 1];
    if (L.gen == g_shader_state_gen && L.frame == R.frame && (!vertex || L.fsKey == fsKey)) return L.s;
    Shader* s = get_shader_uncached(regs, vertex, fs, fsKey);
    L = Last{g_shader_state_gen, R.frame, fsKey, s};
    return s;
}

static void cache_record_shader(const uint32_t* regs, bool vertex);
static bool g_defer_compiles = false;
static std::vector<Shader*> g_deferred_shaders;

static void compile_deferred(Shader* s) {
    if (s->state != CS_DEFERRED) return;
    s->state = CS_PENDING;
    compile_msl(s, s->dec->strBuf_shaderSource->c_str(), s->key);
}

// render thread: start the retry of a failed Metal compile when it is due (see kCompileAttempts).
// Shaders that failed to translate (no dec) never compiled and are not retried.
static void retry_failed_compile(Shader* s, bool now = false) {
    if (!s || !s->dec || s->state.load(std::memory_order_acquire) != CS_FAILED || !retry_due(s, now)) return;
    LOG("[gfx] retrying shader %016llx compile (attempt %d of %d)", (unsigned long long)s->key, s->attempts + 1, kCompileAttempts);
    s->state = CS_PENDING;
    compile_msl(s, s->dec->strBuf_shaderSource->c_str(), s->key);
}

static Shader* get_shader_uncached(const uint32_t* regs, bool vertex, LatteFetchShader* fs, uint64_t fsKey) {
    uint32_t addr = regs[vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS] << 8;
    uint32_t size = regs[(vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS) + 1] << 3;
    if (!addr || !size) return nullptr;
    uint64_t key = program_hash(addr, size) ^ (vertex ? 0x1111 : 0x2222);
    uint64_t base = key;
    key = stage_state_hash(regs, key, vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS);
    if (vertex) key ^= fsKey * 31;
    auto it = g_shaders.find(key);
    if (it != g_shaders.end()) return it->second;

    auto* s = new Shader();
    s->kind = gfx::program_kind(mem::ptr(addr), size, vertex);
    s->key = key;
    g_shaders[key] = s;
    double t0 = now_ms();
    LatteShader_UpdatePSInputs((uint32*)regs);
    LatteDecompilerOptions opt;
    if (!vertex) opt.areaSampledTextures = gfx::area_sample::units_for_pixel_shader(mem::ptr(addr), size);
    LatteDecompilerOutput_t out{};
    if (vertex)
        LatteDecompiler_DecompileVertexShader(base, (uint32*)regs, mem::ptr(addr), size, fs, opt, &out);
    else
        LatteDecompiler_DecompilePixelShader(base, (uint32*)regs, mem::ptr(addr), size, opt, &out);
    if (!out.shader || out.shader->hasError || !out.shader->strBuf_shaderSource) {
        LOG("[gfx] %s shader %08X failed to translate", vertex ? "vertex" : "pixel", addr);
        s->state = CS_FAILED;
        return s;
    }
    s->dec = FinishDecompiledShader(out);
    if (opt.areaSampledTextures) {
        std::string src = s->dec->strBuf_shaderSource->c_str();
        if (gfx::area_sample::rewrite(src, opt.areaSampledTextures, true) > 0) {
            s->dec->strBuf_shaderSource->reset();
            s->dec->strBuf_shaderSource->add(std::string_view(src));
        } else {
            LOG("[gfx] pixel shader %08X: area-sampled taps not applied", addr);
        }
    }
    cache_record_shader(regs, vertex);
    double t1 = now_ms();
    g_t_decompile += t1 - t0;
    if (getenv("WWHD_DUMP_SHADERS")) {
        mkdir("shaders", 0755);
        char name[96];
        snprintf(name, sizeof name, "shaders/%s_%08X_%016llx.metal", vertex ? "vs" : "ps", addr, (unsigned long long)key);
        if (FILE* f = fopen(name, "w")) { fputs(s->dec->strBuf_shaderSource->c_str(), f); fclose(f); }
    }
    if (g_defer_compiles) {
        s->state = CS_DEFERRED;  // cache replay: compile on first use or gradually in the background
        g_deferred_shaders.push_back(s);
    } else {
        compile_msl(s, s->dec->strBuf_shaderSource->c_str(), key);
    }
    g_t_msl += now_ms() - t1;
    static uint32_t count = 0;
    if (++count % 100 == 0) LOG("[gfx] %u shaders translated", count);
    return s;
}

// ---------------------------------------------------------------- conversions
static MTLBlendFactor blend_factor(uint32_t f) {
    switch (f) {
    case 0x00: return MTLBlendFactorZero;
    case 0x01: return MTLBlendFactorOne;
    case 0x02: return MTLBlendFactorSourceColor;
    case 0x03: return MTLBlendFactorOneMinusSourceColor;
    case 0x04: case 0x0B: return MTLBlendFactorSourceAlpha;
    case 0x05: case 0x0C: return MTLBlendFactorOneMinusSourceAlpha;
    case 0x06: return MTLBlendFactorDestinationAlpha;
    case 0x07: return MTLBlendFactorOneMinusDestinationAlpha;
    case 0x08: return MTLBlendFactorDestinationColor;
    case 0x09: return MTLBlendFactorOneMinusDestinationColor;
    case 0x0A: return MTLBlendFactorSourceAlphaSaturated;
    case 0x0D: return MTLBlendFactorBlendColor;
    case 0x0E: return MTLBlendFactorOneMinusBlendColor;
    case 0x0F: return MTLBlendFactorSource1Color;
    case 0x10: return MTLBlendFactorOneMinusSource1Color;
    case 0x11: return MTLBlendFactorSource1Alpha;
    case 0x12: return MTLBlendFactorOneMinusSource1Alpha;
    case 0x13: return MTLBlendFactorBlendAlpha;
    case 0x14: return MTLBlendFactorOneMinusBlendAlpha;
    default: return MTLBlendFactorOne;
    }
}
static MTLBlendOperation blend_op(uint32_t f) {
    switch (f) {
    case 1: return MTLBlendOperationSubtract;
    case 2: return MTLBlendOperationMin;
    case 3: return MTLBlendOperationMax;
    case 4: return MTLBlendOperationReverseSubtract;
    default: return MTLBlendOperationAdd;
    }
}
static MTLCompareFunction compare_func(uint32_t f) {
    static const MTLCompareFunction t[8] = {MTLCompareFunctionNever, MTLCompareFunctionLess, MTLCompareFunctionEqual,
                                            MTLCompareFunctionLessEqual, MTLCompareFunctionGreater, MTLCompareFunctionNotEqual,
                                            MTLCompareFunctionGreaterEqual, MTLCompareFunctionAlways};
    return t[f & 7];
}
static MTLStencilOperation stencil_op(uint32_t f) {
    static const MTLStencilOperation t[8] = {MTLStencilOperationKeep, MTLStencilOperationZero, MTLStencilOperationReplace,
                                             MTLStencilOperationIncrementClamp, MTLStencilOperationDecrementClamp,
                                             MTLStencilOperationInvert, MTLStencilOperationIncrementWrap,
                                             MTLStencilOperationDecrementWrap};
    return t[f & 7];
}

// ---------------------------------------------------------------- pipelines
struct Pipeline {
    id<MTLRenderPipelineState> state = nil;   // valid once status == CS_READY
    std::atomic<int> status{CS_PENDING};
    int attempts = 0;                         // builds started (render thread)
    uint64_t retryFrame = 0;                  // a failed build is retried from this frame on (0: not scheduled)
};
static std::unordered_map<uint64_t, Pipeline*> g_pipelines;

// Ambient-occlusion quirks, switchable in game (Graphics menu, O cycles; WWHD_AO_MODE=0..2 sets the start):
//   0 = as the hardware renders it
//   1 = centre depth sampled bilinear (removes the every-third-row lines)
//   2 = 1 + noise tiled per 960x540 pixel instead of per 640x360 pixel (removes the remaining
//       uneven noise bands the game's blur can't average out)
static std::atomic<int> g_ao_mode{[] {
    if (const char* e = getenv("WWHD_AO_MODE")) return atoi(e) % 3;
    return getenv("WWHD_NO_AO_QUIRK") ? 0 : 2;
}()};
int ao_mode() { return g_ao_mode.load(std::memory_order_relaxed); }
void set_ao_mode(int m) { g_ao_mode = m % 3; LOG("[gfx] ambient occlusion mode %d", m % 3); }

// Draws whose shaders or pipeline are still compiling used to be skipped, which makes objects blink
// for a few frames after entering a new area. Instead wait for the compile, up to a per-frame budget
// (WWHD_COMPILE_WAIT_MS, default 25; the game's frame is 33 ms and the GPU needs ~4 ms of it).
// Pipelines built ahead of use (cache replay, head start) never wait.
// Skipping is only harmless for targets the game redraws every frame. A draw into a target that is
// new or wasn't drawn in the previous frame may be the only one its result gets (a buffer rendered
// once, at a load or after a photo, or a pass's first frame), and a skipped one would leave that
// result missing for as long as the game keeps using it (issue #47: the light buffer sampled before
// its first render, black shadows for the session; the Picto Box colour-grading volume). Those draws
// wait for their compile without the budget, like the Vulkan renderer does for every draw.
static bool g_building_ahead = false;
static bool g_draw_must_run = false;  // the current draw writes a target that isn't redrawn every frame
static std::unordered_map<uint32_t, uint64_t> g_target_drawn;  // render target address -> frame of its last draw
static uint64_t g_must_run_waits = 0;
static double g_must_run_wait_ms = 0;
static bool wait_compiled(const std::atomic<int>& st) {
    static const double budgetMs = getenv("WWHD_COMPILE_WAIT_MS") ? atof(getenv("WWHD_COMPILE_WAIT_MS")) : 25.0;
    if (g_building_ahead) return st.load(std::memory_order_acquire) == CS_READY;
    static uint64_t frame = ~0ull;
    static double spent = 0;
    if (frame != R.frame) { frame = R.frame; spent = 0; }
    if (st.load(std::memory_order_acquire) != CS_PENDING) return st.load(std::memory_order_acquire) == CS_READY;
    if (g_draw_must_run) {
        double t0 = now_ms();
        {
            std::unique_lock<std::mutex> lk(g_compile_mu);
            g_compile_cv.wait(lk, [&] { return st.load(std::memory_order_acquire) != CS_PENDING; });
        }
        double dt = now_ms() - t0;
        spent += dt;
        g_must_run_waits++;
        g_must_run_wait_ms += dt;
        static int logged = 0;
        if (dt >= 30 && logged++ < 50)
            LOG("[gfx] frame %llu: waited %.0f ms for a compile (draw into a target not redrawn every frame)", (unsigned long long)R.frame, dt);
        return st.load(std::memory_order_acquire) == CS_READY;
    }
    if (spent >= budgetMs) return false;
    double t0 = now_ms();
    {
        std::unique_lock<std::mutex> lk(g_compile_mu);
        g_compile_cv.wait_for(lk, std::chrono::duration<double, std::milli>(budgetMs - spent),
                              [&] { return st.load(std::memory_order_acquire) != CS_PENDING; });
    }
    spent += now_ms() - t0;
    return st.load(std::memory_order_acquire) == CS_READY;
}

// Render target formats a pipeline is built for (GX2 surface formats; 0 = no attachment)
struct TargetFormats {
    uint32_t color[8] = {};
    uint32_t depth = 0;
};
static void cache_record_pipeline(const uint32_t* regs, Shader* vs, Shader* ps, uint64_t fsKey, const TargetFormats& t);

static id<MTLRenderPipelineState> get_pipeline(const uint32_t* regs, Shader* vs, Shader* ps, LatteFetchShader* fs,
                                               uint64_t fsKey, const TargetFormats& tf) {
    FormatInfo cfmt[8], dfmt;
    for (int i = 0; i < 8; i++)
        if (tf.color[i]) cfmt[i] = format_info(tf.color[i], false);
    if (tf.depth) dfmt = format_info(tf.depth, true);
    uint64_t h = hash_bytes(&vs, sizeof(vs));
    h = hash_bytes(&ps, sizeof(ps), h);
    h ^= fsKey;
    for (int i = 0; i < 8; i++) {
        uint32_t f = tf.color[i] ? (uint32_t)cfmt[i].pixel : 0;
        h = hash_bytes(&f, 4, h);
    }
    uint32_t df = tf.depth ? (uint32_t)dfmt.pixel : 0;
    h = hash_bytes(&df, 4, h);
    h = hash_regs(regs, REGADDR::CB_BLEND0_CONTROL, 8, h);
    h = hash_regs(regs, REGADDR::CB_COLOR_CONTROL, 1, h);
    h = hash_regs(regs, REGADDR::CB_TARGET_MASK, 1, h);
    auto it = g_pipelines.find(h);
    Pipeline* pl;
    if (it != g_pipelines.end()) {
        pl = it->second;
        // a failed build is retried a few times on later frames (see kCompileAttempts)
        if (pl->status.load(std::memory_order_acquire) != CS_FAILED || !retry_due(pl, g_draw_must_run))
            return wait_compiled(pl->status) ? pl->state : nil;
        LOG("[gfx] retrying pipeline %016llx (attempt %d of %d)", (unsigned long long)h, pl->attempts + 1, kCompileAttempts);
        pl->status = CS_PENDING;
    } else {
        pl = new Pipeline();
        g_pipelines[h] = pl;
        cache_record_pipeline(regs, vs, ps, fsKey, tf);
    }
    const int attempt = ++pl->attempts;
    const bool injectFailure = attempt <= g_test_fail_compiles;
    auto pipelineFailed = [h, attempt](NSError* err) {
        const char* why = err ? err.localizedDescription.UTF8String : "no error given";
        LOG("[gfx] pipeline %016llx creation failed (attempt %d of %d%s): %s", (unsigned long long)h, attempt, kCompileAttempts,
            attempt < kCompileAttempts ? ", will retry" : "", why ? why : "?");
    };

    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vs->fn;
    d.fragmentFunction = ps->fn;
    if (!fs->mtlFetchVertexManually) {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (auto& g : fs->bufferGroups) {
            uint32_t minStride = 0;
            bool instanced = false;
            for (sint32 j = 0; j < g.attribCount; j++) {
                auto& a = g.attrib[j];
                sint32 loc = vs->dec->resourceMapping.attributeMapping[a.semanticId];
                if (loc < 0) continue;
                vd.attributes[loc].offset = a.offset;
                vd.attributes[loc].bufferIndex = GET_MTL_VERTEX_BUFFER_INDEX(a.attributeBufferIndex);
                vd.attributes[loc].format = (MTLVertexFormat)GetMtlVertexFormat(a.format);
                minStride = std::max(minStride, a.offset + GetMtlVertexFormatSize(a.format));
                if (a.fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA) instanced = true;
            }
            uint32_t stride = (regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
            auto layout = vd.layouts[GET_MTL_VERTEX_BUFFER_INDEX(g.attributeBufferIndex)];
            if (stride == 0) {
                stride = minStride;
                layout.stepFunction = MTLVertexStepFunctionConstant;
                layout.stepRate = 0;
            } else {
                layout.stepFunction = instanced ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
            }
            layout.stride = (stride + 3) & ~3u;
        }
        d.vertexDescriptor = vd;
    }
    uint32_t blendMask = (regs[REGADDR::CB_COLOR_CONTROL] >> 8) & 0xFF;
    uint32_t targetMask = regs[REGADDR::CB_TARGET_MASK];
    for (int i = 0; i < 8; i++) {
        if (!tf.color[i]) continue;
        auto ca = d.colorAttachments[i];
        ca.pixelFormat = cfmt[i].pixel;
        uint32_t m = (targetMask >> (4 * i)) & 0xF;
        ca.writeMask = ((m & 1) ? MTLColorWriteMaskRed : 0) | ((m & 2) ? MTLColorWriteMaskGreen : 0) |
                       ((m & 4) ? MTLColorWriteMaskBlue : 0) | ((m & 8) ? MTLColorWriteMaskAlpha : 0);
        if ((blendMask & (1 << i)) && cfmt[i].kind == FormatInfo::FLOAT) {
            LATTE_CB_BLENDN_CONTROL b;
            uint32_t raw = regs[REGADDR::CB_BLEND0_CONTROL + i];
            memcpy(&b, &raw, 4);
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = blend_op((uint32_t)b.get_COLOR_COMB_FCN());
            ca.sourceRGBBlendFactor = blend_factor((uint32_t)b.get_COLOR_SRCBLEND());
            ca.destinationRGBBlendFactor = blend_factor((uint32_t)b.get_COLOR_DSTBLEND());
            if (b.get_SEPARATE_ALPHA_BLEND()) {
                ca.alphaBlendOperation = blend_op((uint32_t)b.get_ALPHA_COMB_FCN());
                ca.sourceAlphaBlendFactor = blend_factor((uint32_t)b.get_ALPHA_SRCBLEND());
                ca.destinationAlphaBlendFactor = blend_factor((uint32_t)b.get_ALPHA_DSTBLEND());
            } else {
                ca.alphaBlendOperation = ca.rgbBlendOperation;
                ca.sourceAlphaBlendFactor = ca.sourceRGBBlendFactor;
                ca.destinationAlphaBlendFactor = ca.destinationRGBBlendFactor;
            }
        }
    }
    if (tf.depth) {
        d.depthAttachmentPixelFormat = dfmt.pixel;
        if (dfmt.stencil) d.stencilAttachmentPixelFormat = dfmt.pixel;
    }
    if (g_sync_shaders) {
        NSError* err = nil;
        double t0 = now_ms();
        pl->state = injectFailure ? nil : [R.device newRenderPipelineStateWithDescriptor:d error:&err];
        g_t_pipeline += now_ms() - t0;
        if (!pl->state) pipelineFailed(err);
        pl->status = pl->state ? CS_READY : CS_FAILED;
        return pl->state;
    }
    g_compiles_in_flight++;
    [R.device newRenderPipelineStateWithDescriptor:d completionHandler:^(id<MTLRenderPipelineState> p, NSError* err) {
        g_compiles_in_flight--;
        if (injectFailure) p = nil;
        if (!p) pipelineFailed(err);
        pl->state = p;
        compile_done(pl->status, p ? CS_READY : CS_FAILED);
    }];
    return wait_compiled(pl->status) ? pl->state : nil;
}

static std::unordered_map<uint64_t, id<MTLDepthStencilState>> g_depth_states;

static id<MTLDepthStencilState> get_depth_state(const uint32_t* regs, bool hasDepth) {
    LATTE_DB_DEPTH_CONTROL dc;
    uint32_t raw = regs[REGADDR::DB_DEPTH_CONTROL];
    memcpy(&dc, &raw, 4);
    uint32_t key[3] = {raw, regs[REGADDR::DB_STENCILREFMASK] & 0xFFFF00, regs[REGADDR::DB_STENCILREFMASK_BF] & 0xFFFF00};
    uint64_t h = hash_bytes(key, 12) ^ hasDepth;
    auto it = g_depth_states.find(h);
    if (it != g_depth_states.end()) return it->second;
    MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
    if (hasDepth && dc.get_Z_ENABLE()) {
        d.depthCompareFunction = compare_func((uint32_t)dc.get_Z_FUNC());
        d.depthWriteEnabled = dc.get_Z_WRITE_ENABLE();
    }
    if (hasDepth && dc.get_STENCIL_ENABLE()) {
        LATTE_DB_STENCILREFMASK f;
        LATTE_DB_STENCILREFMASK_BF b;
        uint32_t rf = regs[REGADDR::DB_STENCILREFMASK], rb = regs[REGADDR::DB_STENCILREFMASK_BF];
        memcpy(&f, &rf, 4);
        memcpy(&b, &rb, 4);
        MTLStencilDescriptor* front = [MTLStencilDescriptor new];
        front.stencilCompareFunction = compare_func((uint32_t)dc.get_STENCIL_FUNC_F());
        front.stencilFailureOperation = stencil_op((uint32_t)dc.get_STENCIL_FAIL_F());
        front.depthFailureOperation = stencil_op((uint32_t)dc.get_STENCIL_ZFAIL_F());
        front.depthStencilPassOperation = stencil_op((uint32_t)dc.get_STENCIL_ZPASS_F());
        front.readMask = f.get_STENCILMASK_F();
        front.writeMask = f.get_STENCILWRITEMASK_F();
        d.frontFaceStencil = front;
        if (dc.get_BACK_STENCIL_ENABLE()) {
            MTLStencilDescriptor* back = [MTLStencilDescriptor new];
            back.stencilCompareFunction = compare_func((uint32_t)dc.get_STENCIL_FUNC_B());
            back.stencilFailureOperation = stencil_op((uint32_t)dc.get_STENCIL_FAIL_B());
            back.depthFailureOperation = stencil_op((uint32_t)dc.get_STENCIL_ZFAIL_B());
            back.depthStencilPassOperation = stencil_op((uint32_t)dc.get_STENCIL_ZPASS_B());
            back.readMask = b.get_STENCILMASK_B();
            back.writeMask = b.get_STENCILWRITEMASK_B();
            d.backFaceStencil = back;
        } else {
            d.backFaceStencil = front;
        }
    }
    id<MTLDepthStencilState> s = [R.device newDepthStencilStateWithDescriptor:d];
    g_depth_states[h] = s;
    return s;
}

// ---------------------------------------------------------------- samplers and textures
static std::unordered_map<uint64_t, id<MTLSamplerState>> g_samplers;

static MTLSamplerAddressMode address_mode(uint32_t c) {
    switch (c) {
    case 0: return MTLSamplerAddressModeRepeat;
    case 1: return MTLSamplerAddressModeMirrorRepeat;
    case 2: return MTLSamplerAddressModeClampToEdge;
    case 3: case 5: case 7: return MTLSamplerAddressModeMirrorClampToEdge;
    default: return MTLSamplerAddressModeClampToBorderColor;
    }
}

// enhancement, toggled in game (Graphics menu or N; off by default, WWHD_ANISO=1 starts with it on): 16x anisotropic filtering on
// mipmapped, linearly filtered textures. Sharpens ground and water seen at shallow angles.
static std::atomic<bool> g_aniso{[] { const char* e = getenv("WWHD_ANISO"); return e && atoi(e) != 0; }()};
bool aniso_enabled() { return g_aniso.load(std::memory_order_relaxed); }
void set_aniso(bool v) { g_aniso = v; LOG("[gfx] anisotropic filtering %s", v ? "on" : "off"); }

// allowAniso: the bound texture is a real mipmapped asset (not a buffer the GPU rendered); the game also
// reads its screen-sized lighting/occlusion buffers through mip-filtering samplers, and anisotropy there
// blurs the screen-space effects
static id<MTLSamplerState> get_sampler(const uint32_t* w, bool allowAniso = false) {
    const bool g_aniso_force = allowAniso && aniso_enabled();
    uint64_t h = hash_bytes(w, 12) ^ (g_aniso_force ? 0x5A5A5A5A5A5A5A5Aull : 0);
    auto it = g_samplers.find(h);
    if (it != g_samplers.end()) return it->second;
    LATTE_SQ_TEX_SAMPLER_WORD0_0 w0;
    LATTE_SQ_TEX_SAMPLER_WORD1_0 w1;
    memcpy(&w0, &w[0], 4);
    memcpy(&w1, &w[1], 4);
    MTLSamplerDescriptor* d = [MTLSamplerDescriptor new];
    d.sAddressMode = address_mode((uint32_t)w0.get_CLAMP_X());
    d.tAddressMode = address_mode((uint32_t)w0.get_CLAMP_Y());
    d.rAddressMode = address_mode((uint32_t)w0.get_CLAMP_Z());
    // E_XY_FILTER: only POINT (0) and ANISO_POINT (4) are nearest; bilinear/bicubic/aniso-bilinear filter
    auto xy = [](uint32_t f) { return (f == 0 || f == 4) ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear; };
    d.magFilter = xy((uint32_t)w0.get_XY_MAG_FILTER());
    d.minFilter = xy((uint32_t)w0.get_XY_MIN_FILTER());
    uint32_t mip = (uint32_t)w0.get_MIP_FILTER();
    d.mipFilter = mip == 0 ? MTLSamplerMipFilterNotMipmapped : mip == 1 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
    uint32_t aniso = w0.get_MAX_ANISO_RATIO();
    if (aniso) d.maxAnisotropy = std::min(1u << aniso, 16u);
    if (g_aniso_force && mip != 0 && d.minFilter == MTLSamplerMinMagFilterLinear && (uint32_t)w0.get_DEPTH_COMPARE_FUNCTION() == 0)
        d.maxAnisotropy = 16;
    d.lodMinClamp = w1.get_MIN_LOD() / 64.0f;
    d.lodMaxClamp = w1.get_MAX_LOD() / 64.0f;
    uint32_t border = (uint32_t)w0.get_BORDER_COLOR_TYPE();
    d.borderColor = border == 1 ? MTLSamplerBorderColorOpaqueBlack : border == 2 ? MTLSamplerBorderColorOpaqueWhite
                                                                                  : MTLSamplerBorderColorTransparentBlack;
    uint32_t cmp = (uint32_t)w0.get_DEPTH_COMPARE_FUNCTION();
    if (cmp) d.compareFunction = compare_func(cmp);
    id<MTLSamplerState> s = [R.device newSamplerStateWithDescriptor:d];
    g_samplers[h] = s;
    return s;
}

static MTLTextureType texture_type_for_dim(Latte::E_DIM dim) {
    switch (dim) {
    case E_DIM::DIM_1D: return MTLTextureType1D;
    case E_DIM::DIM_3D: return MTLTextureType3D;
    case E_DIM::DIM_CUBEMAP: return MTLTextureTypeCube;
    case E_DIM::DIM_1D_ARRAY: return MTLTextureType1DArray;
    case E_DIM::DIM_2D_ARRAY: case E_DIM::DIM_2D_ARRAY_MSAA: return MTLTextureType2DArray;
    default: return MTLTextureType2D;
    }
}

static id<MTLTexture> null_texture(MTLTextureType type) {
    static std::unordered_map<int, id<MTLTexture>> cache;
    auto it = cache.find((int)type);
    if (it != cache.end()) return it->second;
    MTLTextureDescriptor* d = [MTLTextureDescriptor new];
    d.textureType = type;
    d.pixelFormat = MTLPixelFormatRGBA8Unorm;
    d.width = 1;
    d.height = 1;
    d.depth = 1;
    d.arrayLength = 1;
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [R.device newTextureWithDescriptor:d];
    uint32_t zero = 0;
    for (int i = 0; i < (type == MTLTextureTypeCube ? 6 : 1); i++)
        [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:i withBytes:&zero bytesPerRow:4 bytesPerImage:4];
    cache[(int)type] = t;
    return t;
}

// texture view with the shader's expected type and the resource's component swizzle
static std::unordered_map<uint64_t, id<MTLTexture>> g_views;
void forget_texture_views() { g_views.clear(); }

static id<MTLTexture> texture_view(Surface* s, MTLTextureType type, uint32_t word4) {
    LATTE_SQ_TEX_RESOURCE_WORD4_N w4;
    memcpy(&w4, &word4, 4);
    uint32_t sel[4] = {(uint32_t)w4.get_DST_SEL_X(), (uint32_t)w4.get_DST_SEL_Y(), (uint32_t)w4.get_DST_SEL_Z(), (uint32_t)w4.get_DST_SEL_W()};
    uint64_t key = (uint64_t)(uintptr_t)(__bridge void*)s->tex ^ ((uint64_t)type << 56) ^ ((uint64_t)(sel[0] | sel[1] << 4 | sel[2] << 8 | sel[3] << 12) << 40);
    auto it = g_views.find(key);
    if (it != g_views.end()) return it->second;
    static const MTLTextureSwizzle sw[8] = {MTLTextureSwizzleRed, MTLTextureSwizzleGreen, MTLTextureSwizzleBlue, MTLTextureSwizzleAlpha,
                                            MTLTextureSwizzleZero, MTLTextureSwizzleOne, MTLTextureSwizzleZero, MTLTextureSwizzleZero};
    MTLTextureSwizzleChannels ch = MTLTextureSwizzleChannelsMake(sw[sel[0]], sw[sel[1]], sw[sel[2]], sw[sel[3]]);
    NSUInteger slices = s->tex.textureType == MTLTextureTypeCube ? 6 : s->tex.arrayLength;
    if (type == MTLTextureTypeCube && slices < 6) type = s->tex.textureType;
    if ((type == MTLTextureType2D || type == MTLTextureType1D) && slices > 1) slices = 1;
    if (type == MTLTextureType2DArray && s->tex.textureType == MTLTextureType2D) slices = 1;
    if (type == MTLTextureType3D && s->tex.textureType != MTLTextureType3D) return nil;
    if (s->tex.textureType == MTLTextureType3D && type != MTLTextureType3D) return nil;
    id<MTLTexture> v;
    if (s->fmt.depth) {
        // depth formats can't be swizzled; samplers read .x
        v = [s->tex newTextureViewWithPixelFormat:s->tex.pixelFormat
                                      textureType:type
                                           levels:NSMakeRange(0, s->tex.mipmapLevelCount)
                                           slices:NSMakeRange(0, slices)];
    } else {
        v = [s->tex newTextureViewWithPixelFormat:s->tex.pixelFormat
                                      textureType:type
                                           levels:NSMakeRange(0, s->tex.mipmapLevelCount)
                                           slices:NSMakeRange(0, slices)
                                          swizzle:ch];
    }
    if (!v) {
        static int warned = 0;
        if (warned++ < 10)
            LOG("[gfx] texture view failed: %ux%u pixel %lu type %lu -> %lu", s->width, s->height, (unsigned long)s->tex.pixelFormat,
                (unsigned long)s->tex.textureType, (unsigned long)type);
    }
    g_views[key] = v;
    return v;
}

// WWHD_SNAPSHOT=1 copies uniform blocks and small vertex buffers at draw time instead of reading
// guest memory when the GPU runs (costly; the per-core scheduler removed the race it guarded against)
static const bool g_snapshot = getenv("WWHD_SNAPSHOT") != nullptr;

// ---------------------------------------------------------------- per-stage resources
// enhancement, toggled in game (Graphics menu or M; WWHD_AO_HIRES=0 starts with it off): the game
// downsamples depth to 640x360 (PS 3BB9DE00) and computes ambient occlusion from it at 960x540
// (PS 44BDFD00). The size mismatch is what leaves lines in shadowed grass. With this on, the
// downsample is drawn a second time into a private 960x540 copy and the occlusion pass reads that
// copy, one depth texel per pixel. Every other reader keeps the game's 640x360 buffer.
static std::atomic<bool> g_ao_hires{[] { const char* e = getenv("WWHD_AO_HIRES"); return !e || atoi(e) != 0; }()};
bool ao_hires_enabled() { return g_ao_hires.load(std::memory_order_relaxed); }
void set_ao_hires(bool v) { g_ao_hires = v; LOG("[gfx] full-size occlusion depth %s", v ? "on" : "off"); }
static bool g_hires_redraw = false;          // inside the second draw of the downsample
static uint32_t g_hires_src = 0;             // guest address of the game's 640x360 buffer
static uint64_t g_hires_frame = ~0ull;       // frame the private copy was last drawn
static Surface g_hires_color, g_hires_depth;

static Surface* hires_surface(Surface& dst, const Surface* like) {
    uint32_t w = like->width * 3 / 2, h = like->height * 3 / 2;
    // texture: 1.5x the game's buffer as allocated (which may already be scaled for the internal resolution)
    uint32_t pw = (uint32_t)like->tex.width * 3 / 2, ph = (uint32_t)like->tex.height * 3 / 2;
    if (!dst.tex || dst.width != w || dst.height != h || dst.tex.width != pw || dst.tex.height != ph ||
        dst.fmt.pixel != like->fmt.pixel) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:like->fmt.pixel width:pw height:ph
                                                                                 mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
        d.storageMode = MTLStorageModePrivate;
        dst = *like;
        dst.mipChain.reset();
        dst.tex = [R.device newTextureWithDescriptor:d];
        dst.addr = 0;  // private: never found by address lookups
        dst.width = w;
        dst.height = h;
        dst.slices = 1;
        dst.mips = 1;
        dst.sx = (float)pw / w;
        dst.sy = (float)ph / h;
        // private textures start with undefined contents and every pass on them loads: clear once
        // (colour 0, depth 1) so nothing the redraw doesn't cover reads leftover GPU memory
        end_encoder();
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        if (dst.fmt.depth) {
            rp.depthAttachment.texture = dst.tex;
            rp.depthAttachment.loadAction = MTLLoadActionClear;
            rp.depthAttachment.clearDepth = 1.0;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            if (dst.fmt.stencil) {
                rp.stencilAttachment.texture = dst.tex;
                rp.stencilAttachment.loadAction = MTLLoadActionClear;
                rp.stencilAttachment.clearStencil = 0;
                rp.stencilAttachment.storeAction = MTLStoreActionStore;
            }
        } else {
            rp.colorAttachments[0].texture = dst.tex;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        }
        [[command_buffer() renderCommandEncoderWithDescriptor:rp] endEncoding];
    }
    return &dst;
}

// pixels of the current render target per guest pixel (viewport/scissor/fragment-coordinate scale)
static float g_target_kx = 1.0f, g_target_ky = 1.0f;

static void bind_stage(id<MTLRenderCommandEncoder> enc, const uint32_t* regs, Shader* sh, bool vertex, Surface* const* colors) {
    LatteDecompilerShader* dec = sh->dec;
    float texScale[18][2];
    for (auto& t : texScale) t[0] = t[1] = 1.0f;
    auto& rm = dec->resourceMapping;
    uint32_t texBase = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    uint32_t samplerBase = vertex ? SAMPLER_BASE_INDEX_VERTEX : SAMPLER_BASE_INDEX_PIXEL;

    // textures and samplers
    for (sint32 i = 0; i < rm.getTextureCount(); i++) {
        sint32 unit = rm.getRelativeTextureUnitFromRelativeBindingPoint(i);
        if (unit < 0) continue;
        if (!vertex && dec->textureRenderTargetIndex[unit] != 255) continue;  // read through framebuffer fetch
        uint32_t binding = rm.getTextureBaseBindingPoint() + i;
        MTLTextureType type = texture_type_for_dim(dec->textureUnitDim[unit]);
        const uint32_t* tw = &regs[texBase + unit * 7];
        Surface* s = sampled_texture(tw, dec->textureUsesDepthCompare[unit]);
        if (!vertex && s && g_hires_src && s->addr == g_hires_src && g_hires_frame == R.frame && ao_hires_enabled() &&
            sh->kind == gfx::ProgramKind::OcclusionPixel)
            s = &g_hires_color;
        id<MTLTexture> tex = s && s->tex ? texture_view(s, type, tw[4]) : nil;
        if (s && unit < 18) { texScale[unit][0] = s->sx; texScale[unit][1] = s->sy; }
        uint32_t samplerIdx = dec->textureUnitSamplerAssignment[unit];
        DLOG("[draw]   %s tex%u %08X %ux%u fmt %X gpu=%d view=%d smp %08X %08X %08X", vertex ? "VS" : "PS", unit, tw[2] << 8,
             s ? s->width : 0, s ? s->height : 0, s ? s->format : 0, s ? s->gpuWritten : -1, tex != nil,
             regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + (samplerIdx + samplerBase) * 3],
             regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + (samplerIdx + samplerBase) * 3 + 1],
             regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + (samplerIdx + samplerBase) * 3 + 2]);
        if (!tex) tex = null_texture(type);
        const uint32_t* sw = &regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + (samplerIdx + samplerBase) * 3];
        // quirk: the ambient-occlusion pass (PS 44BDFD00) point-samples its centre depth from a 640x360
        // buffer while drawing 960x540; every third row lands half a texel off and shows as screen-fixed
        // lines on sloped ground in shadow. Bilinear for that one fetch matches its neighbour fetches.
        uint32_t patched[3];
        if (ao_mode() >= 1 && !vertex && unit == 0 && sh->kind == gfx::ProgramKind::OcclusionPixel) {
            memcpy(patched, sw, sizeof patched);
            patched[0] = (patched[0] & ~0x7E00u) | (1u << 9) | (1u << 12);  // XY mag/min filter: bilinear
            sw = patched;
        }
        id<MTLSamplerState> smp = get_sampler(sw, s && !s->gpuWritten && s->mips > 1);
        if (vertex) {
            [enc setVertexTexture:tex atIndex:binding];
            [enc setVertexSamplerState:smp atIndex:binding];
        } else {
            [enc setFragmentTexture:tex atIndex:binding];
            [enc setFragmentSamplerState:smp atIndex:binding];
        }
    }

    // support buffer (uniform registers, remapped uniforms, helper values)
    if (rm.uniformVarsBufferBindingPoint >= 0) {
        uint32_t size = std::max<uint32_t>(dec->uniform.uniformRangeSize, 16);
        static thread_local std::vector<uint8_t> buf;
        buf.assign((size + 15) & ~15u, 0);
        float* f = (float*)buf.data();
        auto at = [&](sint32 loc) { return f + loc / 4; };
        if (dec->uniform.loc_alphaTestRef >= 0) {
            LATTE_SX_ALPHA_REF ref;
            uint32_t raw = regs[REGADDR::SX_ALPHA_REF];
            memcpy(&ref, &raw, 4);
            *at(dec->uniform.loc_alphaTestRef) = ref.get_ALPHA_TEST_REF();
        }
        if (dec->uniform.loc_pointSize >= 0) {
            float pw = (float)(regs[REGADDR::PA_SU_POINT_SIZE] & 0xFFFF) / 8.0f;
            *at(dec->uniform.loc_pointSize) = (pw == 0 ? 1.0f / 8.0f : pw) * g_target_kx;
        }
        uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (vertex ? 0x400 : 0);
        uint32_t blockBase = vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
        if (dec->uniform.loc_remapped >= 0) {
            uint8_t* dst = (uint8_t*)at(dec->uniform.loc_remapped);
            // the decompiler fills only the list matching the shader's uniform mode
            for (auto& e : dec->list_remappedUniformEntries_register)
                memcpy(dst + e.mappedIndexOffset, &regs[aluBase + e.indexOffset / 4], 16);
            for (auto& g : dec->list_remappedUniformEntries_bufferGroups) {
                uint32_t addr = regs[blockBase + g.kcacheBankIdOffset / 4];
                if (!addr) continue;
                for (auto& e : g.entries) memcpy(dst + e.mappedIndexOffset, mem::ptr(addr + e.indexOffset), 16);
            }
            // Apply once before the Gaussian/mip chain, preserving the game's threshold and haze.
            if (!vertex && (regs[mmSQ_PGM_START_PS] << 8) == 0x44F91200)
                render::scale_bloom_uniforms(dst, buf.size() - dec->uniform.loc_remapped);
            // AO mode 2: the occlusion pass's VS (44BDF900) scales its noise coordinates by remapped[0].w
            // for a 640x360 grid; the pass draws 960x540, so tile the 4x4 noise per output pixel instead
            if (vertex && ao_mode() == 2 && sh->kind == gfx::ProgramKind::OcclusionVertex)
                ((float*)dst)[3] *= 1.5f;
        }
        if (dec->uniform.loc_uniformRegister >= 0)
            memcpy(at(dec->uniform.loc_uniformRegister), &regs[aluBase], dec->uniform.count_uniformRegister * 16);
        if (dec->uniform.loc_windowSpaceToClipSpaceTransform >= 0) {
            float vw = 2.0f * gx2::bitsf(regs[REGADDR::PA_CL_VPORT_XSCALE]);
            float vh = -2.0f * gx2::bitsf(regs[REGADDR::PA_CL_VPORT_YSCALE]);
            float* v = at(dec->uniform.loc_windowSpaceToClipSpaceTransform);
            v[0] = vw != 0 ? 2.0f / vw : 0;
            v[1] = vh != 0 ? 2.0f / vh : 0;
        }
        if (dec->uniform.loc_fragCoordScale >= 0) {
            // the shader sees guest pixel positions
            at(dec->uniform.loc_fragCoordScale)[0] = 1.0f / g_target_kx;
            at(dec->uniform.loc_fragCoordScale)[1] = 1.0f / g_target_ky;
        }
        for (auto& e : dec->uniform.list_ufTexRescale) {
            // integer texel coordinates are guest texels: scale them to the texture as allocated
            bool ok = e.texUnit < 18;
            at(e.uniformLocation)[0] = ok ? texScale[e.texUnit][0] : 1.0f;
            at(e.uniformLocation)[1] = ok ? texScale[e.texUnit][1] : 1.0f;
        }
        for (int t = 0; t < 18; t++) {
            if (dec->uniform.loc_framebufferFetchSize[t] < 0) continue;
            sint32* sz = (sint32*)at(dec->uniform.loc_framebufferFetchSize[t]);
            sz[0] = (regs[REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + t * 7] >> 19) + 1;
            sz[1] = (regs[REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + t * 7 + 1] & 0x1FFF) + 1;
        }
        if (buf.size() <= 4096) {
            if (vertex) [enc setVertexBytes:buf.data() length:buf.size() atIndex:rm.uniformVarsBufferBindingPoint];
            else [enc setFragmentBytes:buf.data() length:buf.size() atIndex:rm.uniformVarsBufferBindingPoint];
        } else {
            Upload u = upload(buf.data(), (uint32_t)buf.size());
            if (vertex) [enc setVertexBuffer:u.buf offset:u.offset atIndex:rm.uniformVarsBufferBindingPoint];
            else [enc setFragmentBuffer:u.buf offset:u.offset atIndex:rm.uniformVarsBufferBindingPoint];
        }
    }

    // uniform blocks: snapshot at draw time. Our GPU runs a frame after the CPU built it, and games
    // rewrite uniform memory between draws, so binding guest memory directly shows later values.
    uint32_t blockBase = vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    for (int i = 0; i < 16; i++) {
        sint32 binding = rm.uniformBuffersBindingPoint[i];
        if (binding < 0) continue;
        uint32_t addr = regs[blockBase + i * 7];
        uint32_t size = std::min<uint32_t>(regs[blockBase + i * 7 + 1] + 1, 0x10000);
        uint32_t off = 0;
        id<MTLBuffer> gb = addr ? guest_buffer(addr, &off) : nil;
        if (!gb) continue;
        if (g_snapshot) {
            Upload u = upload(mem::ptr(addr), size);
            if (vertex) [enc setVertexBuffer:u.buf offset:u.offset atIndex:binding];
            else [enc setFragmentBuffer:u.buf offset:u.offset atIndex:binding];
        } else {
            if (vertex) [enc setVertexBuffer:gb offset:off atIndex:binding];
            else [enc setFragmentBuffer:gb offset:off atIndex:binding];
        }
    }
}

// ---------------------------------------------------------------- render pass
static bool ensure_pass(Surface* const* colors, const uint32_t* colorSlices, Surface* depth, uint32_t depthSlice) {
    if (R.enc) {
        bool same = R.passDepth == depth && (!depth || R.passDepthSlice == depthSlice);
        for (int i = 0; i < 8 && same; i++)
            same = R.passColor[i] == colors[i] && (!colors[i] || R.passColorSlice[i] == colorSlices[i]);
        if (same) return true;
    }
    end_encoder();
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    bool any = false;
    for (int i = 0; i < 8; i++) {
        if (!colors[i]) continue;
        rp.colorAttachments[i].texture = colors[i]->tex;
        // a volume's slice is a depth plane
        if (colors[i]->tex.textureType == MTLTextureType3D) rp.colorAttachments[i].depthPlane = colorSlices[i];
        else rp.colorAttachments[i].slice = colorSlices[i];
        rp.colorAttachments[i].loadAction = MTLLoadActionLoad;
        rp.colorAttachments[i].storeAction = MTLStoreActionStore;
        mark_gpu_written(colors[i]);
        any = true;
    }
    if (depth) {
        rp.depthAttachment.texture = depth->tex;
        rp.depthAttachment.slice = depthSlice;
        rp.depthAttachment.loadAction = MTLLoadActionLoad;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        if (depth->fmt.stencil) {
            rp.stencilAttachment.texture = depth->tex;
            rp.stencilAttachment.slice = depthSlice;
            rp.stencilAttachment.loadAction = MTLLoadActionLoad;
            rp.stencilAttachment.storeAction = MTLStoreActionStore;
        }
        mark_gpu_written(depth);
        any = true;
    }
    if (!any) return false;
    R.enc = [command_buffer() renderCommandEncoderWithDescriptor:rp];
    for (int i = 0; i < 8; i++) {
        R.passColor[i] = colors[i];
        R.passColorSlice[i] = colorSlices[i];
    }
    R.passDepth = depth;
    R.passDepthSlice = depthSlice;
    return R.enc != nil;
}

// ---------------------------------------------------------------- indices
// Converts guest indices (possibly big-endian, possibly a primitive type Metal lacks)
// into a 32-bit little-endian index list. Returns the Metal primitive type.
// one pass into a presized buffer; the index format is resolved once per draw, not per index
template <class Idx>
static bool expand_indices(uint32_t prim, uint32_t count, bool indexed, Idx idx, std::vector<uint32_t>& out,
                           MTLPrimitiveType& type) {
    out.clear();
    uint32_t* o;
    switch (prim) {
    case 1: type = MTLPrimitiveTypePoint; break;
    case 2: type = MTLPrimitiveTypeLine; break;
    case 3: type = MTLPrimitiveTypeLineStrip; break;
    case 4: type = MTLPrimitiveTypeTriangle; break;
    case 6: type = MTLPrimitiveTypeTriangleStrip; break;
    case 5:  // triangle fan -> list
        type = MTLPrimitiveTypeTriangle;
        out.resize(count > 2 ? size_t(count - 2) * 3 : 0);
        o = out.data();
        for (uint32_t i = 2; i < count; i++) { *o++ = idx(0); *o++ = idx(i - 1); *o++ = idx(i); }
        return true;
    case 0x13:  // quads -> list
        type = MTLPrimitiveTypeTriangle;
        out.resize(size_t(count / 4) * 6);
        o = out.data();
        for (uint32_t q = 0; q + 3 < count; q += 4) {
            uint32_t a = idx(q), b = idx(q + 1), c = idx(q + 2), d = idx(q + 3);
            *o++ = a; *o++ = b; *o++ = c; *o++ = a; *o++ = c; *o++ = d;
        }
        return true;
    case 0x14:  // quad strip -> list
        type = MTLPrimitiveTypeTriangle;
        out.resize(count >= 4 ? size_t((count - 4) / 2 + 1) * 6 : 0);
        o = out.data();
        for (uint32_t q = 0; q + 3 < count; q += 2) {
            uint32_t a = idx(q), b = idx(q + 1), c = idx(q + 2), d = idx(q + 3);
            *o++ = a; *o++ = b; *o++ = d; *o++ = a; *o++ = d; *o++ = c;
        }
        return true;
    case 0x12:  // line loop
        type = MTLPrimitiveTypeLineStrip;
        out.resize(count ? count + 1 : 0);
        for (uint32_t i = 0; i < count; i++) out[i] = idx(i);
        if (count) out[count] = idx(0);
        return true;
    default:
        return false;  // rects and adjacency primitives: not supported yet
    }
    if (indexed) {
        out.resize(count);
        for (uint32_t i = 0; i < count; i++) out[i] = idx(i);
    }
    return true;
}

static bool build_indices(uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr, std::vector<uint32_t>& out,
                          MTLPrimitiveType& type) {
    if (!indexAddr) return expand_indices(prim, count, false, [](uint32_t i) { return i; }, out, type);
    const uint8_t* p = mem::ptr(indexAddr);
    switch (indexType) {
    case 0: return expand_indices(prim, count, true, [p](uint32_t i) -> uint32_t { return ((const uint16_t*)p)[i]; }, out, type);
    case 1: return expand_indices(prim, count, true, [p](uint32_t i) -> uint32_t { return ((const uint32_t*)p)[i]; }, out, type);
    case 9:
        return expand_indices(prim, count, true, [p](uint32_t i) { uint32_t v; memcpy(&v, p + 4 * i, 4); return __builtin_bswap32(v); },
                              out, type);
    default:  // 4 and anything else: big-endian 16-bit
        return expand_indices(prim, count, true, [p](uint32_t i) -> uint32_t { uint16_t v; memcpy(&v, p + 2 * i, 2); return __builtin_bswap16(v); },
                              out, type);
    }
}

// ---------------------------------------------------------------- draw

// ---------------------------------------------------------------- persistent shader cache
// Every newly translated shader and every new pipeline is appended to a recipe file: the shader
// microcode plus the register state that shaped its translation. At startup the recipes are
// replayed, so shaders and pipelines are ready before the game asks for them (macOS keeps the
// compiled Metal code in its own cache, so the replay is fast after the first time).
// WWHD_SHADER_CACHE=<file> overrides the location, WWHD_SHADER_CACHE=0 disables it.
namespace {
constexpr uint32_t kRecShader = 1, kRecPipeline = 2;
FILE* g_cache_out = nullptr;
bool g_cache_replaying = false;
bool g_cache_disabled = false;

struct PipelineRecipe {
    uint64_t vsKey, psKey, fsKey;
    TargetFormats tf;
    uint32_t blend[8], colorControl, targetMask, strides[16];
};
std::vector<PipelineRecipe> g_pending_pipelines;
// hashes of the pipeline recipes already in the cache file or head start, so a pipeline the game asks
// for before its queued recipe is built isn't appended again
std::unordered_set<uint64_t> g_known_pipelines;

std::string cache_path() {
    if (const char* e = getenv("WWHD_SHADER_CACHE")) return e;
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/Library/Caches/wwhd";
    mkdir(dir.c_str(), 0755);
    return dir + "/shaders.bin";
}

// registers saved with a shader: everything except ALU constants and unused space
bool cache_saves_reg(uint32_t r) {
    return (r >= 0x2000 && r < 0x4000) || (r >= 0xA000 && r < 0xC000) || (r >= 0xE000 && r < 0x10000);
}

void cache_write(uint32_t type, const std::vector<uint8_t>& raw) {
    if (!g_cache_out) return;
    uLongf csize = compressBound(raw.size());
    std::vector<uint8_t> comp(csize);
    if (compress2(comp.data(), &csize, raw.data(), raw.size(), 6) != Z_OK) return;
    uint32_t hdr[3] = {type, (uint32_t)raw.size(), (uint32_t)csize};
    fwrite(hdr, sizeof hdr, 1, g_cache_out);
    fwrite(comp.data(), 1, csize, g_cache_out);
    fflush(g_cache_out);
}

template <class T> void put(std::vector<uint8_t>& v, const T& x) {
    const uint8_t* p = (const uint8_t*)&x;
    v.insert(v.end(), p, p + sizeof(T));
}
template <class T> bool get(const uint8_t*& p, const uint8_t* end, T& x) {
    if (p + sizeof(T) > end) return false;
    memcpy(&x, p, sizeof(T));
    p += sizeof(T);
    return true;
}

uint32_t fetch_shader_size(const uint32_t* regs) {
    uint32_t prog = regs[mmSQ_PGM_START_FS] << 8;
    if (!prog) return 0;
    return ld32(prog) == 0x57574653 ? 16 + ld32(prog + 4) * 16 : regs[mmSQ_PGM_START_FS + 1] << 3;
}
}  // namespace

static void cache_record_shader(const uint32_t* regs, bool vertex) {
    if (g_cache_replaying || !g_cache_out) return;
    uint32_t pgm = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
    uint32_t addr = regs[pgm] << 8, size = regs[pgm + 1] << 3;
    uint32_t fsAddr = vertex ? regs[mmSQ_PGM_START_FS] << 8 : 0, fsSize = vertex ? fetch_shader_size(regs) : 0;
    std::vector<uint8_t> v;
    put(v, (uint32_t)vertex);
    put(v, size);
    put(v, fsSize);
    uint32_t n = 0;
    for (uint32_t r = 0; r < 0x10000; r++)
        if (regs[r] && cache_saves_reg(r)) n++;
    put(v, n);
    v.insert(v.end(), mem::ptr(addr), mem::ptr(addr) + size);
    if (fsSize) v.insert(v.end(), mem::ptr(fsAddr), mem::ptr(fsAddr) + fsSize);
    for (uint32_t r = 0; r < 0x10000; r++)
        if (regs[r] && cache_saves_reg(r)) { put(v, r); put(v, regs[r]); }
    cache_write(kRecShader, v);
}

static void cache_record_pipeline(const uint32_t* regs, Shader* vs, Shader* ps, uint64_t fsKey, const TargetFormats& t) {
    if (g_cache_replaying || !g_cache_out) return;
    PipelineRecipe r{};
    r.vsKey = vs->key;
    r.psKey = ps->key;
    r.fsKey = fsKey;
    r.tf = t;
    for (int i = 0; i < 8; i++) r.blend[i] = regs[REGADDR::CB_BLEND0_CONTROL + i];
    r.colorControl = regs[REGADDR::CB_COLOR_CONTROL];
    r.targetMask = regs[REGADDR::CB_TARGET_MASK];
    for (int i = 0; i < 16; i++) r.strides[i] = regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + i * 7 + 2];
    std::vector<uint8_t> v;
    put(v, r);
    if (!g_known_pipelines.insert(hash_bytes(v.data(), v.size())).second) return;
    cache_write(kRecPipeline, v);
}

// load and replay the recipes; runs once on the render thread before the first draw
static void cache_load() {
    std::string path = cache_path();
    if (path == "0") { g_cache_disabled = true; return; }
    double t0 = now_ms();
    size_t shaders = 0, pipelines = 0;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        static std::vector<uint32_t> regs(0x10000);
        std::unordered_map<std::string, uint32_t> copies;
        auto guest_copy = [&](const uint8_t* src, uint32_t size) {
            uint32_t& addr = copies[std::string((const char*)src, size)];
            if (!addr) {
                addr = mem::host_alloc(size, 0x100);
                memcpy(mem::ptr(addr), src, size);
            }
            return addr;
        };
        g_cache_replaying = true;
        g_defer_compiles = true;
        uint32_t hdr[3];
        while (fread(hdr, sizeof hdr, 1, f) == 1) {
            std::vector<uint8_t> comp(hdr[2]), raw(hdr[1]);
            if (fread(comp.data(), 1, hdr[2], f) != hdr[2]) break;
            uLongf rsize = hdr[1];
            if (uncompress(raw.data(), &rsize, comp.data(), hdr[2]) != Z_OK || rsize != hdr[1]) break;
            const uint8_t* p = raw.data();
            const uint8_t* end = p + raw.size();
            if (hdr[0] == kRecPipeline) {
                PipelineRecipe r;
                if (get(p, end, r) && g_known_pipelines.insert(hash_bytes(raw.data(), raw.size())).second) {
                    g_pending_pipelines.push_back(r);
                    pipelines++;
                }
                continue;
            }
            if (hdr[0] != kRecShader) continue;
            uint32_t vertex, size, fsSize, n;
            if (!get(p, end, vertex) || !get(p, end, size) || !get(p, end, fsSize) || !get(p, end, n)) break;
            if (p + size + fsSize + n * 8 > end) break;
            // the microcode goes to guest memory so the normal translation path can read it; one copy per
            // distinct program (records repeat the same few thousand programs under many register states)
            uint32_t prog = guest_copy(p, size);
            p += size;
            uint32_t fsProg = 0;
            if (fsSize) {
                fsProg = guest_copy(p, fsSize);
                p += fsSize;
            }
            std::fill(regs.begin(), regs.end(), 0);
            for (uint32_t i = 0; i < n; i++) {
                uint32_t r, val;
                get(p, end, r);
                get(p, end, val);
                regs[r] = val;
            }
            uint32_t pgm = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
            regs[pgm] = prog >> 8;
            if (fsSize) regs[mmSQ_PGM_START_FS] = fsProg >> 8;
            uint64_t fsKey = 0;
            LatteFetchShader* fs = vertex ? get_fetch_shader(regs.data(), &fsKey) : nullptr;
            if (vertex && !fs) continue;
            get_shader_uncached(regs.data(), vertex, fs, fsKey);
            shaders++;
        }
        fclose(f);
        g_cache_replaying = false;
        g_defer_compiles = false;
    }
    void headstart_load();  // shader_headstart.mm
    headstart_load();
    g_cache_out = fopen(path.c_str(), "ab");
    if (shaders || pipelines)
        LOG("[gfx] shader cache: replayed %zu shaders, %zu pipelines queued (%.0f ms) from %s", shaders, pipelines,
            now_ms() - t0, path.c_str());
}

// build queued pipelines whose shaders have finished compiling, at most `budget` of them and only
// while fewer than `maxInFlight` compiles are running; recipes that can't be resolved are dropped
static size_t g_recipes_built, g_recipes_dropped;
static void build_pending_pipelines(int budget, int maxInFlight) {
    if (g_pending_pipelines.empty()) return;
    static std::vector<uint32_t> regs(0x10000);
    g_cache_replaying = true;
    g_building_ahead = true;
    // the queue holds ~10^5 recipes at startup; per-frame calls check a window of it and resume from a
    // cursor next frame instead of rescanning everything (`--warm-shaders` passes INT_MAX: full scan)
    static size_t cursor = 0;
    size_t scan = budget == INT_MAX ? SIZE_MAX : 2048;
    size_t i = scan != SIZE_MAX && cursor < g_pending_pipelines.size() ? cursor : 0;
    for (; i < g_pending_pipelines.size() && scan > 0 && budget > 0 && g_compiles_in_flight < maxInFlight; scan--) {
        PipelineRecipe& r = g_pending_pipelines[i];
        auto vi = g_shaders.find(r.vsKey), pi = g_shaders.find(r.psKey);
        auto fi = g_fetch.find(r.fsKey);
        auto finallyFailed = [](Shader* sh) { return sh->state == CS_FAILED && (!sh->dec || sh->attempts >= kCompileAttempts); };
        if (vi == g_shaders.end() || pi == g_shaders.end() || fi == g_fetch.end() || !fi->second ||
            finallyFailed(vi->second) || finallyFailed(pi->second)) {
            g_pending_pipelines[i] = g_pending_pipelines.back();  // unusable recipe
            g_pending_pipelines.pop_back();
            g_recipes_dropped++;
            continue;
        }
        retry_failed_compile(vi->second);
        retry_failed_compile(pi->second);
        if (vi->second->state != CS_READY || pi->second->state != CS_READY) { i++; continue; }
        for (int k = 0; k < 8; k++) regs[REGADDR::CB_BLEND0_CONTROL + k] = r.blend[k];
        regs[REGADDR::CB_COLOR_CONTROL] = r.colorControl;
        regs[REGADDR::CB_TARGET_MASK] = r.targetMask;
        for (int k = 0; k < 16; k++) regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + k * 7 + 2] = r.strides[k];
        get_pipeline(regs.data(), vi->second, pi->second, fi->second, r.fsKey, r.tf);
        g_pending_pipelines[i] = g_pending_pipelines.back();
        g_pending_pipelines.pop_back();
        g_recipes_built++;
        budget--;
    }
    cursor = i;
    g_building_ahead = false;
    g_cache_replaying = false;
}

// build cached pipelines whose shaders have finished compiling; a few per frame
void cache_warm_step() {
    // a few background compiles per frame keep the startup burst from starving the game, and none
    // start while the game's own compiles keep the compiler busy (WWHD_BG_COMPILES, default 8 in flight)
    static const int maxInFlight = getenv("WWHD_BG_COMPILES") ? atoi(getenv("WWHD_BG_COMPILES")) : 8;
    for (int n = 0; n < 16 && !g_deferred_shaders.empty() && g_compiles_in_flight < maxInFlight;) {
        Shader* s = g_deferred_shaders.back();
        g_deferred_shaders.pop_back();
        if (s->state == CS_DEFERRED) { compile_deferred(s); n++; }
    }
    static bool queued = false;
    if (!g_pending_pipelines.empty()) queued = true;
    build_pending_pipelines(16, maxInFlight);
    if (queued && g_pending_pipelines.empty()) {
        queued = false;
        LOG("[gfx] background pipelines done by frame %llu: %zu built, %zu unresolved", (unsigned long long)R.frame,
            g_recipes_built, g_recipes_dropped);
    }
}

enum Skip { SK_GS, SK_RASTER_KILL, SK_NO_FETCH, SK_NO_SHADER, SK_NO_TARGET, SK_PRIM, SK_PASS, SK_PIPELINE, SK_CULL, SK_SCISSOR, SK_COMPILING,
            SK_DROPPED_DEPTH, SK_DROPPED_COLOR, SK_PIPE_COMPILING, SK_COUNT };
static uint64_t g_skip[SK_COUNT];
static const char* kSkipNames[SK_COUNT] = {"geometry-shader", "raster-kill", "no-fetch-shader", "shader-failed", "no-target",
                                           "unsupported-prim", "no-pass", "pipeline-failed", "cull-all", "empty-scissor",
                                           "compiling", "dropped-depth", "dropped-color", "pipeline-compiling"};
void report_skips() {
    char buf[512];
    int n = 0;
    for (int i = 0; i < SK_COUNT; i++)
        if (g_skip[i]) n += snprintf(buf + n, sizeof buf - n, " %s=%llu", kSkipNames[i], (unsigned long long)g_skip[i]);
    if (n) LOG("[gfx] skipped draws:%s", buf);
    LOG("[gfx] %zu shaders, %zu pipelines; ms decompile %.0f, msl %.0f, pipeline %.0f", g_shaders.size(), g_pipelines.size(),
        g_t_decompile, g_t_msl, g_t_pipeline);
    if (g_must_run_waits)
        LOG("[gfx] draws that waited for their compile (targets not redrawn every frame): %llu, %.0f ms in all",
            (unsigned long long)g_must_run_waits, g_must_run_wait_ms);
    extern uint64_t g_stat_full_checks, g_stat_uploads, g_stat_invalidates, g_stat_invalidated_surfaces;
    extern uint64_t g_stat_hashed_bytes;
    uint64_t faults, protectedPages;
    wwatch::take_stats(faults, protectedPages);
    LOG("[gfx] last 300 frames: %llu full texture checks (%.1f MiB hashed), %llu uploads, %llu invalidates marking %llu surfaces, "
        "%llu texture page write faults, %llu pages protected",
        (unsigned long long)g_stat_full_checks, g_stat_hashed_bytes / 1048576.0, (unsigned long long)g_stat_uploads,
        (unsigned long long)g_stat_invalidates, (unsigned long long)g_stat_invalidated_surfaces, (unsigned long long)faults,
        (unsigned long long)protectedPages);
    g_stat_full_checks = g_stat_uploads = g_stat_invalidates = g_stat_invalidated_surfaces = g_stat_hashed_bytes = 0;
}

void draw(const uint32_t* regs, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr, uint32_t baseVertex,
          uint32_t instances) {
    static bool cacheLoaded = (cache_load(), true);
    (void)cacheLoaded;
    R.drawCount++;
    extern uint32_t g_draws_since_commit;
    g_draws_since_commit++;
    DLOG("[draw] prim %X count %u idx %u@%08X VS %08X PS %08X CB0 %08X info %08X DB %08X depthctl %08X blend %08X mask %08X",
         prim, count, indexType, indexAddr, regs[mmSQ_PGM_START_VS] << 8, regs[mmSQ_PGM_START_PS] << 8, regs[mmCB_COLOR0_BASE],
         regs[mmCB_COLOR0_INFO], regs[mmDB_DEPTH_BASE], regs[REGADDR::DB_DEPTH_CONTROL], regs[REGADDR::CB_COLOR_CONTROL],
         regs[REGADDR::CB_TARGET_MASK]);
    if (!count || !instances) return;
    ((uint32_t*)regs)[REGADDR::VGT_PRIMITIVE_TYPE] = prim;
    if (regs[REGADDR::VGT_GS_MODE] & 3) { g_skip[SK_GS]++; return; }  // geometry shaders: not supported yet
    if (regs[REGADDR::PA_CL_CLIP_CNTL] & (1 << 22)) { g_skip[SK_RASTER_KILL]++; return; }  // rasterization disabled

    uint64_t fsKey = 0;
    LatteFetchShader* fs = get_fetch_shader(regs, &fsKey);
    if (!fs) {
        if (g_skip[SK_NO_FETCH]++ < 5) {
            uint32_t prog = regs[mmSQ_PGM_START_FS] << 8;
            LOG("[gfx] no fetch shader: FS=%08X size=%X words %08X %08X %08X %08X", prog, regs[mmSQ_PGM_START_FS + 1] << 3,
                prog ? ld32(prog) : 0, prog ? ld32(prog + 4) : 0, prog ? ld32(prog + 8) : 0, prog ? ld32(prog + 12) : 0);
        }
        return;
    }
    Shader* vs = get_shader(regs, true, fs, fsKey);
    Shader* ps = get_shader(regs, false, fs, fsKey);
    if (!vs || !ps || !vs->dec || !ps->dec) { g_skip[SK_NO_SHADER]++; return; }  // translation failed
    // must this draw run even if its compile takes longer than the budget? (see wait_compiled)
    const LatteContextRegister& lcr = *(const LatteContextRegister*)regs;
    uint8_t mask = LatteMRT::GetActiveColorBufferMask(ps->dec, lcr);
    {
        auto notRedrawn = [](uint32_t addr) {
            auto it = g_target_drawn.find(addr);
            return it == g_target_drawn.end() || it->second + 1 < R.frame;
        };
        bool must = false;
        for (int i = 0; i < 8 && !must; i++)
            if ((mask & (1 << i)) && regs[mmCB_COLOR0_BASE + i]) must = notRedrawn(regs[mmCB_COLOR0_BASE + i]);
        if (!must && LatteMRT::GetActiveDepthBufferMask(lcr) && regs[mmDB_DEPTH_BASE]) must = notRedrawn(regs[mmDB_DEPTH_BASE]);
        g_draw_must_run = must;
    }
    struct MustRunReset { ~MustRunReset() { g_draw_must_run = false; } } mustRunReset;
    retry_failed_compile(vs, g_draw_must_run);
    retry_failed_compile(ps, g_draw_must_run);
    if (vs->state == CS_FAILED || ps->state == CS_FAILED) { g_skip[SK_NO_SHADER]++; return; }
    compile_deferred(vs);
    compile_deferred(ps);
    if (!wait_compiled(vs->state) || !wait_compiled(ps->state)) {
        g_skip[SK_COMPILING]++;
        return;
    }
    rprof::mark(rprof::kShader);

    Surface* colors[8] = {};
    uint32_t colorSlices[8] = {}, depthSlice = 0;
    for (int i = 0; i < 8; i++)
        if (mask & (1 << i)) colors[i] = color_target(regs, i, &colorSlices[i]);
    Surface* depth = LatteMRT::GetActiveDepthBufferMask(lcr) ? depth_target(regs, &depthSlice) : nullptr;
    if (depth && depth->width == 1280 && depth->height == 720) R.mainDepthAddr = depth->addr;
    uint32_t guestW = colors[0] ? colors[0]->width : 0;  // the game's target size (viewport registers refer to it)
    uint32_t guestH = colors[0] ? colors[0]->height : 0;
    if (g_hires_redraw) {
        g_hires_src = colors[0]->addr;
        colors[0] = hires_surface(g_hires_color, colors[0]);
        colorSlices[0] = 0;
        if (depth) { depth = hires_surface(g_hires_depth, depth); depthSlice = 0; }
    }
    // Metal requires matching attachment sizes; drop mismatching ones. Sizes here are the textures'
    // (internal resolution); kx/ky = texture pixels per guest pixel of the target.
    uint32_t w = 0, h = 0;
    float kx = 1.0f, ky = 1.0f;
    for (auto* c : colors)
        if (c) { w = (uint32_t)c->tex.width; h = (uint32_t)c->tex.height; kx = c->sx; ky = c->sy; break; }
    if (depth && w && (depth->tex.width < w || depth->tex.height < h)) { depth = nullptr; g_skip[SK_DROPPED_DEPTH]++; }
    if (!w && depth) { w = (uint32_t)depth->tex.width; h = (uint32_t)depth->tex.height; kx = depth->sx; ky = depth->sy; }
    for (auto& c : colors)
        if (c && (c->tex.width != w || c->tex.height != h)) { c = nullptr; g_skip[SK_DROPPED_COLOR]++; }
    if (!w) { g_skip[SK_NO_TARGET]++; return; }
    if (g_hires_redraw && guestW) {  // the viewport registers describe the game's smaller buffer
        kx = (float)colors[0]->tex.width / guestW;
        ky = (float)colors[0]->tex.height / guestH;  // differs from kx at other aspect ratios
    }
    g_target_kx = kx;
    g_target_ky = ky;
    rprof::mark(rprof::kTargets);

    // textures must be uploaded before the render encoder opens
    static std::vector<uint32_t> indices;  // render thread only; keeps its capacity between draws
    MTLPrimitiveType ptype;
    if (!build_indices(prim, count, indexType, indexAddr, indices, ptype)) { g_skip[SK_PRIM]++; return; }
    rprof::mark(rprof::kIndices);
    for (Shader* sh : {vs, ps}) {
        uint32_t texBase = sh == vs ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
        for (int u = 0; u < sh->dec->textureUnitListCount; u++) {
            uint8_t unit = sh->dec->textureUnitList[u];
            Surface* s = sampled_texture(&regs[texBase + unit * 7], sh->dec->textureUsesDepthCompare[unit]);
            (void)s;
        }
    }

    rprof::mark(rprof::kTextures);
    if (!ensure_pass(colors, colorSlices, depth, depthSlice)) { g_skip[SK_PASS]++; return; }
    rprof::mark(rprof::kPass);
    TargetFormats tf;
    for (int i = 0; i < 8; i++)
        if (colors[i]) tf.color[i] = colors[i]->format;
    if (depth) tf.depth = depth->format;
    id<MTLRenderPipelineState> pipe = get_pipeline(regs, vs, ps, fs, fsKey, tf);
    if (!pipe) { g_skip[SK_PIPE_COMPILING]++; return; }
    rprof::mark(rprof::kPipeline);
    id<MTLRenderCommandEncoder> enc = R.enc;
    [enc setRenderPipelineState:pipe];
    [enc setDepthStencilState:get_depth_state(regs, depth != nullptr)];
    LATTE_DB_STENCILREFMASK sf;
    LATTE_DB_STENCILREFMASK_BF sb;
    uint32_t rf = regs[REGADDR::DB_STENCILREFMASK], rb = regs[REGADDR::DB_STENCILREFMASK_BF];
    memcpy(&sf, &rf, 4);
    memcpy(&sb, &rb, 4);
    [enc setStencilFrontReferenceValue:sf.get_STENCILREF_F() backReferenceValue:sb.get_STENCILREF_B()];
    const float* bc = (const float*)&regs[REGADDR::CB_BLEND_RED];
    [enc setBlendColorRed:bc[0] green:bc[1] blue:bc[2] alpha:bc[3]];

    // rasterizer
    LATTE_PA_SU_SC_MODE_CNTL pm;
    uint32_t pmr = regs[REGADDR::PA_SU_SC_MODE_CNTL];
    memcpy(&pm, &pmr, 4);
    bool cf = pm.get_CULL_FRONT(), cb = pm.get_CULL_BACK();
    if (cf && cb) { g_skip[SK_CULL]++; return; }
    [enc setCullMode:cf ? MTLCullModeFront : cb ? MTLCullModeBack : MTLCullModeNone];
    [enc setFrontFacingWinding:pm.get_FRONT_FACE() == LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW ? MTLWindingCounterClockwise
                                                                                                  : MTLWindingClockwise];
    if (pm.get_OFFSET_FRONT_ENABLED()) {
        float scale = gx2::bitsf(regs[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE]) / 16.0f;
        float offset = gx2::bitsf(regs[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET]);
        float clampv = gx2::bitsf(regs[REGADDR::PA_SU_POLY_OFFSET_CLAMP]);
        [enc setDepthBias:offset slopeScale:scale clamp:clampv];
    } else {
        [enc setDepthBias:0 slopeScale:0 clamp:0];
    }
    LATTE_PA_CL_CLIP_CNTL clipCntl;
    uint32_t clipRaw = regs[REGADDR::PA_CL_CLIP_CNTL];
    memcpy(&clipCntl, &clipRaw, 4);
    bool zclip = !clipCntl.get_ZCLIP_FAR_DISABLE();
    [enc setDepthClipMode:zclip ? MTLDepthClipModeClip : MTLDepthClipModeClamp];

    // viewport and scissor
    float xs = gx2::bitsf(regs[REGADDR::PA_CL_VPORT_XSCALE]), xo = gx2::bitsf(regs[REGADDR::PA_CL_VPORT_XOFFSET]);
    float ys = gx2::bitsf(regs[REGADDR::PA_CL_VPORT_YSCALE]), yo = gx2::bitsf(regs[REGADDR::PA_CL_VPORT_YOFFSET]);
    float zs = gx2::bitsf(regs[REGADDR::PA_CL_VPORT_ZSCALE]), zo = gx2::bitsf(regs[REGADDR::PA_CL_VPORT_ZOFFSET]);
    bool halfZ = clipCntl.get_DX_CLIP_SPACE_DEF();
    MTLViewport vp{xo - xs, yo + ys, xs * 2.0f, ys * -2.0f, halfZ ? zo : zo - zs, zs + zo};
    vp.originX *= kx; vp.originY *= ky; vp.width *= kx; vp.height *= ky;
    [enc setViewport:vp];
    uint32_t tl = regs[REGADDR::PA_SC_GENERIC_SCISSOR_TL], br = regs[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
    auto lo = [](uint32_t v, float k, uint32_t lim) { return std::min<uint32_t>((uint32_t)std::floor(v * k + 0.01f), lim); };
    auto hi = [](uint32_t v, float k, uint32_t lim) { return std::min<uint32_t>((uint32_t)std::ceil(v * k - 0.01f), lim); };
    uint32_t sx = lo(tl & 0x7FFF, kx, w), sy = lo((tl >> 16) & 0x7FFF, ky, h);
    uint32_t ex = hi(br & 0x7FFF, kx, w), ey = hi((br >> 16) & 0x7FFF, ky, h);
    if (ex <= sx || ey <= sy) { g_skip[SK_SCISSOR]++; return; }
    [enc setScissorRect:MTLScissorRect{sx, sy, ex - sx, ey - sy}];
    rprof::mark(rprof::kRecord);
    rprof::UploadKind vertexUploads(rprof::kUpVertex);

    // vertex buffers: small ones (UI, particles, dynamic geometry) are snapshotted like uniforms;
    // large static meshes are read from guest memory directly
    static const uint32_t kSnapshotLimit = getenv("WWHD_VB_SNAPSHOT") ? (uint32_t)atoi(getenv("WWHD_VB_SNAPSHOT")) : 256 * 1024;
    for (auto& g : fs->bufferGroups) {
        uint32_t addr = regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7];
        uint32_t size = regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 1] + 1;
        uint32_t off = 0;
        id<MTLBuffer> b = guest_buffer(addr, &off);
        if (!b) continue;
        if (g_snapshot && size <= kSnapshotLimit) {
            Upload u = upload(mem::ptr(addr), size);
            [enc setVertexBuffer:u.buf offset:u.offset atIndex:GET_MTL_VERTEX_BUFFER_INDEX(g.attributeBufferIndex)];
        } else {
            [enc setVertexBuffer:b offset:off atIndex:GET_MTL_VERTEX_BUFFER_INDEX(g.attributeBufferIndex)];
        }
    }
    rprof::mark(rprof::kVertex);
    {
        rprof::UploadKind uploads(rprof::kUpUbo);  // Metal: uniform snapshots and bindings of both stages
        R.binding = true;
        bind_stage(enc, regs, vs, true, colors);
        bind_stage(enc, regs, ps, false, colors);
        R.binding = false;
    }
    rprof::mark(rprof::kUniforms);

    static uint64_t drawInFrame = 0, lastFrame = 0;
    if (lastFrame != R.frame) { lastFrame = R.frame; drawInFrame = 0; }
    uint64_t thisDraw = drawInFrame++;
    DLOG("[draw]   #%llu", (unsigned long long)thisDraw);
    DLOG("[draw]   -> target %ux%u fmt %lu depth %d (slice %u of %u) vp %.0f,%.0f %.0fx%.0f", w, h,
         colors[0] ? (unsigned long)colors[0]->fmt.pixel : 0, depth != nullptr, depthSlice, depth ? depth->slices : 0, vp.originX,
         vp.originY, vp.width, vp.height);
    if (indices.empty()) {
        [enc drawPrimitives:ptype vertexStart:baseVertex vertexCount:count instanceCount:instances];
    } else {
        rprof::UploadKind uploads(rprof::kUpIndex);
        Upload u = upload(indices.data(), (uint32_t)(indices.size() * 4));
        [enc drawIndexedPrimitives:ptype
                        indexCount:indices.size()
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:u.buf
                 indexBufferOffset:u.offset
                     instanceCount:instances
                        baseVertex:baseVertex
                      baseInstance:0];
    }
    for (int i = 0; i < 8; i++)
        if (colors[i] && regs[mmCB_COLOR0_BASE + i]) g_target_drawn[regs[mmCB_COLOR0_BASE + i]] = R.frame;
    if (depth && regs[mmDB_DEPTH_BASE]) g_target_drawn[regs[mmDB_DEPTH_BASE]] = R.frame;
    // debug: WWHD_DUMP_DRAWS=frame:i,j,k dumps color target 0 after those draws
    static uint64_t dumpFrame = ~0ull;
    static std::set<uint64_t> dumpDraws = [] {
        std::set<uint64_t> d;
        if (const char* e = getenv("WWHD_DUMP_DRAWS")) {
            char* p;
            dumpFrame = strtoull(e, &p, 10);
            while (*p == ':' || *p == ',') { p++; d.insert(strtoull(p, &p, 10)); }
        }
        return d;
    }();
    // debug: WWHD_TRACE_PS=addr[:first-last] logs one compact line per matching draw (cheap enough to keep timing)
    static uint32_t tracePS = 0;
    static uint64_t traceFrom = 0, traceTo = ~0ull;
    static bool traceInit = [] {
        if (const char* e = getenv("WWHD_TRACE_PS")) {
            char* p;
            tracePS = (uint32_t)strtoul(e, &p, 16);
            if (*p == ':') { traceFrom = strtoull(p + 1, &p, 10); traceTo = *p == '-' ? strtoull(p + 1, nullptr, 10) : traceFrom; }
        }
        return true;
    }();
    if ((tracePS && R.frame >= traceFrom && R.frame <= traceTo && (regs[mmSQ_PGM_START_PS] << 8) == tracePS) ||
        (capturing() && count <= 6)) {
        uint32_t vb = regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START];
        uint32_t stride = (regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + 2] >> 11) & 0xFFFF;
        char line[4096];
        int n = snprintf(line, sizeof line, "[trace] f%llu #%llu tex %08X vb %08X v0 %.1f,%.1f v1 %.1f,%.1f v2 %.1f,%.1f v3 %.1f,%.1f |",
                         (unsigned long long)R.frame, (unsigned long long)thisDraw, regs[REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + 2] << 8, vb,
                         bitsf_(ld32(vb)), bitsf_(ld32(vb + 4)), bitsf_(ld32(vb + stride)), bitsf_(ld32(vb + stride + 4)),
                         bitsf_(ld32(vb + 2 * stride)), bitsf_(ld32(vb + 2 * stride + 4)), bitsf_(ld32(vb + 3 * stride)),
                         bitsf_(ld32(vb + 3 * stride + 4)));
        int k = 0;
        for (auto& e : vs->dec->list_remappedUniformEntries_register) {
            if (k++ >= 4) break;
            const float* f = (const float*)&regs[mmSQ_ALU_CONSTANT0_0 + 0x400 + e.indexOffset / 4];
            n += snprintf(line + n, sizeof line - n, " r%X(%.3f %.3f %.3f %.3f)", e.indexOffset, f[0], f[1], f[2], f[3]);
        }
        for (auto& g : vs->dec->list_remappedUniformEntries_bufferGroups) {
            uint32_t addr = regs[mmSQ_VTX_UNIFORM_BLOCK_START + g.kcacheBankIdOffset / 4];
            for (auto& e : g.entries) {
                if (k++ >= 4) break;
                const float* f = (const float*)mem::ptr(addr + e.indexOffset);
                n += snprintf(line + n, sizeof line - n, " u%08X+%X(%.3f %.3f %.3f %.3f)", addr, e.indexOffset, f[0], f[1], f[2], f[3]);
            }
        }
        // pixel shader remapped uniforms (index: value) too
        n += snprintf(line + n, sizeof line - n, " | PS:");
        for (auto& e : ps->dec->list_remappedUniformEntries_register) {
            if (n > (int)sizeof line - 80) break;
            const float* f = (const float*)&regs[mmSQ_ALU_CONSTANT0_0 + e.indexOffset / 4];
            n += snprintf(line + n, sizeof line - n, " [%u]%.3f,%.3f,%.3f,%.3f", e.mappedIndexOffset / 16, f[0], f[1], f[2], f[3]);
        }
        for (auto& g : ps->dec->list_remappedUniformEntries_bufferGroups) {
            uint32_t addr = regs[mmSQ_PS_UNIFORM_BLOCK_START + g.kcacheBankIdOffset / 4];
            for (auto& e : g.entries) {
                if (n > (int)sizeof line - 80) break;
                const float* f = (const float*)mem::ptr(addr + e.indexOffset);
                n += snprintf(line + n, sizeof line - n, " [%u]%.4f,%.4f,%.4f,%.4f", e.mappedIndexOffset / 16, f[0], f[1], f[2], f[3]);
            }
        }
        if (capturing()) dlog("%s", line); else LOG("%s", line);
    }
    static uint32_t dumpPS = getenv("WWHD_DUMP_PS") ? (uint32_t)strtoul(getenv("WWHD_DUMP_PS"), nullptr, 16) : 0;
    bool psMatch = dumpPS && log_this_frame() && (regs[mmSQ_PGM_START_PS] << 8) == dumpPS;
    if (psMatch) {
        uint32_t vb = regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START];
        uint32_t stride = (regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + 2] >> 11) & 0xFFFF;
        LOG("[dbg] vb %08X stride %u: (%f %f %f) (%f %f %f) (%f %f %f)", vb, stride, bitsf_(ld32(vb)), bitsf_(ld32(vb + 4)),
            bitsf_(ld32(vb + 8)), bitsf_(ld32(vb + stride)), bitsf_(ld32(vb + stride + 4)), bitsf_(ld32(vb + stride + 8)),
            bitsf_(ld32(vb + 2 * stride)), bitsf_(ld32(vb + 2 * stride + 4)), bitsf_(ld32(vb + 2 * stride + 8)));
        bool dx9 = regs[REGADDR::SQ_CONFIG] & 1;
        LOG("[dbg] dx9consts=%d cull=%08X remapped entries reg=%zu bufgroups=%zu idx %u@%08X first idx %u %u %u", dx9,
            regs[REGADDR::PA_SU_SC_MODE_CNTL], vs->dec->list_remappedUniformEntries_register.size(),
            vs->dec->list_remappedUniformEntries_bufferGroups.size(), indexType, indexAddr, ld16(indexAddr), ld16(indexAddr + 2), ld16(indexAddr + 4));
        for (auto& g : vs->dec->list_remappedUniformEntries_bufferGroups) {
            uint32_t addr = regs[mmSQ_VTX_UNIFORM_BLOCK_START + g.kcacheBankIdOffset / 4];
            for (auto& e : g.entries) {
                const float* f = (const float*)mem::ptr(addr + e.indexOffset);
                LOG("[dbg]   ubo %08X+%X: %f %f %f %f", addr, e.indexOffset, f[0], f[1], f[2], f[3]);
            }
        }
        for (auto& e : vs->dec->list_remappedUniformEntries_register) {
            const float* f = (const float*)&regs[mmSQ_ALU_CONSTANT0_0 + 0x400 + e.indexOffset / 4];
            LOG("[dbg]   reg +%X: %f %f %f %f", e.indexOffset, f[0], f[1], f[2], f[3]);
        }
        void dump_texture(id<MTLTexture>, const char*, bool, bool);
        char name[64];
        snprintf(name, sizeof name, "ps_%08X_%llu.png", dumpPS, (unsigned long long)thisDraw);
        for (auto* c : colors) if (c) { dump_texture(c->tex, name, false, false); break; }
    }
    if (capturing() && count <= 6 && colors[0]) {  // full-screen pass
        void dump_texture(id<MTLTexture>, const char*, bool, bool);
        char name[160];
        snprintf(name, sizeof name, "%s/draw_%04llu_PS%08X_CB%08X.png", g_capture_dir.c_str(), (unsigned long long)thisDraw,
                 regs[mmSQ_PGM_START_PS] << 8, regs[mmCB_COLOR0_BASE]);
        dump_texture(colors[0]->tex, name, false, false);
    }
    if (R.frame == dumpFrame && dumpDraws.count(thisDraw)) {
        void dump_texture(id<MTLTexture>, const char*, bool, bool);
        char name[64];
        snprintf(name, sizeof name, "draw_%llu_%llu.png", (unsigned long long)R.frame, (unsigned long long)thisDraw);
        for (auto* c : colors) if (c) { dump_texture(c->tex, name, false, false); break; }
        if (depth) {
            snprintf(name, sizeof name, "draw_%llu_%llu_depth.png", (unsigned long long)R.frame, (unsigned long long)thisDraw);
            dump_texture(depth->tex, name, false, false);
        }
    }
    if (g_hires_redraw) { g_hires_frame = R.frame; return; }  // the private copy is ready for the occlusion pass
    if (ao_hires_enabled() && colors[0] && ps->kind == gfx::ProgramKind::DepthDownsample) {
        g_hires_redraw = true;
        draw(regs, prim, count, indexType, indexAddr, baseVertex, instances);
        g_hires_redraw = false;
    }
}

// ---------------------------------------------------------------- shader head start hooks (shader_headstart.mm)
// Translate one program from a head-start record: regs hold its state, with the program (and the
// fetch shader) already placed in guest memory. Not recorded into the user cache. compileNow starts
// the Metal compile right away instead of deferring it to first use.
bool headstart_translate(const uint32_t* regs, bool vertex, bool compileNow) {
    bool replaying = g_cache_replaying, defer = g_defer_compiles;
    g_cache_replaying = true;
    g_defer_compiles = !compileNow;
    uint64_t fsKey = 0;
    LatteFetchShader* fs = vertex ? get_fetch_shader(regs, &fsKey) : nullptr;
    Shader* s = (!vertex || fs) ? get_shader_uncached(regs, vertex, fs, fsKey) : nullptr;
    if (s && compileNow) compile_deferred(s);
    g_cache_replaying = replaying;
    g_defer_compiles = defer;
    return s && s->state != CS_FAILED;
}

// Metal compiles (shaders and pipelines) that haven't finished yet
size_t headstart_compiling() { return std::max(0, g_compiles_in_flight.load()); }

// queue a head-start pipeline recipe (a raw kRecPipeline record); built like the user cache's, once
// its shaders are compiled
bool headstart_queue_pipeline(const uint8_t* raw, size_t size) {
    if (size != sizeof(PipelineRecipe)) return false;
    PipelineRecipe r;
    memcpy(&r, raw, size);
    if (g_known_pipelines.insert(hash_bytes(raw, size)).second) g_pending_pipelines.push_back(r);
    return true;
}

// `--warm-shaders`: build the queued pipelines whose shaders are compiled, keeping at most
// maxInFlight compiles running. Returns the number still queued (waiting for their shaders).
size_t headstart_build_pipelines(int maxInFlight, size_t& built, size_t& dropped) {
    build_pending_pipelines(INT_MAX, maxInFlight);
    built = g_recipes_built;
    dropped = g_recipes_dropped;
    return g_pending_pipelines.size();
}

}  // namespace gfx
