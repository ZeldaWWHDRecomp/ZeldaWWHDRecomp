// Wind Waker HD recompiled: entry point.
#ifndef _WIN32
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif
#include <ctime>
#include <mutex>
#include <csignal>
#include <filesystem>
#include "guest_addr.h"
#include "mods/guest_mods.h"
#include "mods/code_mods.h"
#include "platform/host.h"
#include "platform/appimage_notice.h"
#include "savestate.h"
#ifdef _WIN32
#include <timeapi.h>
#endif

#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <algorithm>

#include "gfx/renderer.h"
#include "gfx/shader_prebuild.h"
#include "overlay/hostui.h"
#include "mods/cemu_pack.h"
#include "mods/content.h"
#include "gx2/gx2.h"
#include "recomp_table.h"
#include "report_header.h"
#include "crash_addr.h"
#include "build_info.h"
#ifdef WWHD_DECOMP
#include "decomp/decomp.h"
#endif
#include "crashrec.h"
#include "crash_context.h"
#include "crash_memory.h"
#include "input.h"
#include "mods/manager.h"
#include "mods/required.h"
#include "mods/mods.h"
#include "platform/https.h"
#include "mods/packages.h"
#include "mods/save_backup.h"
#include "mods/save_folder.h"
#include "runtime.h"
#include "overlay/editor_panel.h"
#include "editor/editor_game.h"
#if defined(WWHD_SDL_HOST) && !defined(__ANDROID__)
#include <SDL3/SDL_messagebox.h>
#endif
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // main() becomes SDL_main, called by SDLActivity
namespace interp { void set_mode(int); }
#endif

#ifdef WWHD_HAS_VULKAN
namespace gfxvk { int renderer_smoke_test(); int warm_headstart_shaders(); }
#ifdef __ANDROID__
namespace gfxvk { int android_display_smoke_test(); }
namespace gx2 { void shutdown_vulkan_caches(); }
#endif
#endif
#ifdef WWHD_HAS_METAL
int gfx_headstart_warm();  // gfx/shader_headstart.mm
#endif

void mem_setup_heaps(uint32_t data_end);
void trace_dump(FILE* f, unsigned last);
void mem_init_data_imports(uint32_t alloc_slot, uint32_t alloc_ex_slot, uint32_t free_slot);

#ifndef _WIN32
// Memory crashes (SIGSEGV/SIGBUS/...): the report goes to the terminal and to
// captures/crash-<time>.log (registers, guest return chain, host backtrace, crash recovery's
// automatic state, the last log lines). Only write() and preformatted text after the crash.
static int g_crash_fd = -1;
static void crash_raw(int fd, const char* s, size_t n) {
    if (write(2, s, n) < 0) {}
    if (fd >= 0 && write(fd, s, n) < 0) {}
}
static void crash_log_raw(int fd, const char* s, size_t n) {
    if (fd >= 0 && write(fd, s, n) < 0) {}
}
static void crash_out(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, crash_raw); }
static void crash_log_only(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, crash_log_raw); }
static void crash_handler(int sig, siginfo_t* si, void* uctx) {
    uintptr_t a = (uintptr_t)si->si_addr;
    uintptr_t base = (uintptr_t)PPC_MEM_BASE;
    char path[96];
    {
        mkdir("captures", 0755);
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        strftime(path, sizeof path, "captures/crash-%Y%m%d-%H%M%S.log", &tmv);
        g_crash_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    const int fd = g_crash_fd;
    char buf[256];
    int n;
    if (a >= base && a < base + 0x100000000ull)
        n = snprintf(buf, sizeof buf, "\nCRASH: signal %d at guest address %08X\n", sig, (unsigned)(a - base));
    else
        n = snprintf(buf, sizeof buf, "\nCRASH: signal %d at host address %p\n", sig, si->si_addr);
    crash_out(fd, buf, n);
    // the faulting instruction and the module holding it (a driver, an overlay's layer, the game)
    char where[384], mpath[512] = "", line[1024];
    if (uintptr_t pc = crash_addr::context_pc(uctx)) {
        crash_addr::describe(where, sizeof where, pc, mpath, sizeof mpath);
        n = crash_addr::fit(snprintf(line, sizeof line, "  host pc %p%s\n", (void*)pc, where), sizeof line);
        crash_out(fd, line, n);
        if (mpath[0]) {
            n = crash_addr::fit(snprintf(line, sizeof line, "  module: %s\n", mpath), sizeof line);
            crash_out(fd, line, n);
        }
    }
    // a host fault address inside a module (a write to read-only data, a jump into a data section)
    if (!(a >= base && a < base + 0x100000000ull) && crash_addr::describe(where, sizeof where, a)) {
        n = crash_addr::fit(snprintf(line, sizeof line, "  fault address %p%s\n", si->si_addr, where), sizeof line);
        crash_out(fd, line, n);
    }
    Cpu* c = threads::current();
    crash_memory::note(fd, crash_out, c ? c->r : nullptr);
    if (c) {
        n = snprintf(buf, sizeof buf, "  guest lr=%08X ctr=%08X cr=%08X\n", c->lr, c->ctr, ppc_mfcr(c));
        crash_out(fd, buf, n);
        for (int i = 0; i < 32; i += 8) {
            n = snprintf(buf, sizeof buf, "  r%-2d %08X %08X %08X %08X %08X %08X %08X %08X\n", i, c->r[i], c->r[i + 1],
                         c->r[i + 2], c->r[i + 3], c->r[i + 4], c->r[i + 5], c->r[i + 6], c->r[i + 7]);
            crash_out(fd, buf, n);
        }
        // guest return chain (back-chain words on the guest stack; names: build/names.tsv)
        crash_out(fd, "  guest call chain:", 19);
        uint32_t sp = c->r[1];
        for (int i = 0; i < 24 && sp >= 0x10000000u && sp < 0xF0000000u; i++) {
            uint32_t prev = ld32(sp);
            if (!prev || prev <= sp || prev - sp > 0x100000u) break;
            n = snprintf(buf, sizeof buf, " %08X", ld32(prev + 4));
            crash_out(fd, buf, n);
            sp = prev;
        }
        crash_out(fd, "\n", 1);
    }
    crash_addr::host_backtrace(fd, crash_out, uctx);
    crash_context::note(fd, crash_out);
    crashrec::crash_note(fd, crash_out);
    exception_report::note(fd, crash_out);
    if (fd >= 0) {
        crash_log_only(fd, "\n--- last log lines ---\n", 24);
        log_ring_write(fd, crash_log_only);
        close(fd);
        n = snprintf(buf, sizeof buf, "[crash] wrote %s\n", path);
        crash_out(-1, buf, n);
    }
    if (g_ppc_trace) {
        FILE* f = fopen("trace_dump.txt", "w");
        if (f) { trace_dump(f, 3000); fclose(f); if (write(2, "[trace] wrote trace_dump.txt\n", 29) < 0) {} }
    }
    input::stop_rumble_now();  // controllers keep their last motor level after the process (issue #35)
    _exit(128 + sig);
}

static void install_crash_handler() {
    static char altstack[1 << 16];
    stack_t ss{};
    ss.ss_sp = altstack;
    ss.ss_size = sizeof altstack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    crash_addr::prime();
}

#else
static void win_crash_raw(int fd, const char* s, size_t n) {
    fwrite(s, 1, n, stderr);
    if (fd >= 0) _write(fd, s, (unsigned)n);
}
static void win_crash_log_raw(int fd, const char* s, size_t n) { if (fd >= 0) _write(fd, s, (unsigned)n); }
static void win_crash_out(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, win_crash_raw); }
static void win_crash_log_only(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, win_crash_log_raw); }
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ex) {
    auto code=ex->ExceptionRecord->ExceptionCode;
    std::error_code ec; std::filesystem::create_directories("captures",ec);
    char path[96]; time_t t=time(nullptr); struct tm tmv; localtime_s(&tmv,&t);
    strftime(path,sizeof path,"captures/crash-%Y%m%d-%H%M%S.log",&tmv);
    int fd=_open(path,_O_WRONLY|_O_CREAT|_O_TRUNC|_O_BINARY,_S_IREAD|_S_IWRITE);
    char buf[256]; int n;
    // the module holding the faulting instruction (issue #41: a driver or an overlay's Vulkan layer)
    char where[384], mpath[512]="", line[1024];
    using crash_addr::fit;
    crash_addr::describe(where,sizeof where,(uintptr_t)ex->ExceptionRecord->ExceptionAddress,mpath,sizeof mpath);
    n=fit(snprintf(line,sizeof line,"CRASH: Windows exception %08lX at %p%s\n",code,ex->ExceptionRecord->ExceptionAddress,where),sizeof line); win_crash_out(fd,line,n);
    if(mpath[0]){n=fit(snprintf(line,sizeof line,"  module: %s\n",mpath),sizeof line); win_crash_out(fd,line,n);}
    // access violations (and in-page errors): read / write / execute, and of which address
    const EXCEPTION_RECORD* er=ex->ExceptionRecord;
    if((code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR)&&er->NumberParameters>=2){
        const ULONG_PTR kind=er->ExceptionInformation[0], target=er->ExceptionInformation[1];
        const char* what=kind==0?"read":kind==1?"write":kind==8?"execute (DEP)":"access";
        const uintptr_t gbase=(uintptr_t)PPC_MEM_BASE;
        n=fit(snprintf(line,sizeof line,"  %s: %s of address %p",code==EXCEPTION_ACCESS_VIOLATION?"access violation":"in-page error",
                       what,(void*)target),sizeof line);
        if(target>=gbase&&target<gbase+0x100000000ull) n+=fit(snprintf(line+n,sizeof line-n," (guest address %08X)",(unsigned)(target-gbase)),sizeof line-n);
        else if(crash_addr::describe(where,sizeof where,target)) n+=fit(snprintf(line+n,sizeof line-n,"%s",where),sizeof line-n);
        if(n>(int)sizeof line-2) n=(int)sizeof line-2;
        line[n++]='\n'; win_crash_out(fd,line,n);
    }
    if(Cpu* c=threads::current()){n=snprintf(buf,sizeof buf,"guest lr=%08X ctr=%08X\n",c->lr,c->ctr); win_crash_out(fd,buf,n);}
    Cpu* diagnostic_cpu=threads::current();
    crash_memory::note(fd,win_crash_out,diagnostic_cpu ? diagnostic_cpu->r : nullptr);
    crash_addr::host_backtrace(fd,win_crash_out,ex->ContextRecord);
    crash_context::note(fd,win_crash_out);
    crashrec::crash_note(fd,win_crash_out);
    exception_report::note(fd,win_crash_out);
    if(fd>=0){win_crash_log_only(fd,"\n--- last log lines ---\n",24); log_ring_write(fd,win_crash_log_only); _close(fd); fprintf(stderr,"[crash] wrote %s\n",path);}
    if(g_ppc_trace) { FILE* f=fopen("trace_dump.txt","w"); if(f){trace_dump(f,3000);fclose(f);} }
    input::stop_rumble_now();  // controllers keep their last motor level after the process (issue #35)
    return EXCEPTION_EXECUTE_HANDLER;
}
static void abort_handler(int) {
    // CRT abort is not an SEH fault; route it through our existing Windows crash writer.
    RaiseException(0x40000015u /* STATUS_FATAL_APP_EXIT */, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    _exit(134);
}
static void install_crash_handler() {
    SetUnhandledExceptionFilter(crash_handler);
    std::signal(SIGABRT, abort_handler);
    crash_addr::prime();
}
#endif
static void init_data_imports() {
    uint32_t alloc = 0, alloc_ex = 0, free_ = 0;
    for (unsigned i = 0; i < g_recomp_import_count; i++) {
        const RecompImport& im = g_recomp_imports[i];
        if (im.is_func) continue;
        std::string n = im.name;
        if (n == "MEMAllocFromDefaultHeap") alloc = im.addr;
        else if (n == "MEMAllocFromDefaultHeapEx") alloc_ex = im.addr;
        else if (n == "MEMFreeToDefaultHeap") free_ = im.addr;
        else if (n == "__gh_FOPEN_MAX") st32(im.addr, 20);
        else if (n == "environ") st32(im.addr, im.addr + 0x10);  // empty environment list
    }
    mem_init_data_imports(alloc, alloc_ex, free_);
}

// Portable mode (portable.txt next to the executable, see host::portable_user_dir): the macOS paths
// that do not go through host::config_dir() get their existing overrides pointed into the folder.
// save_folder.h asks the player at start-up (two saves, another computer playing): a message box
// on the SDL host; elsewhere, and in test runs, each question takes its safe answer.
static int ask_player(const std::string& title, const std::string& text, const std::vector<std::string>& buttons) {
#if defined(WWHD_SDL_HOST) && !defined(__ANDROID__)
    const char* hidden = getenv("WWHD_HIDDEN_WINDOWS");
    if (hidden && *hidden && strcmp(hidden, "0") != 0) return -1;
    std::vector<SDL_MessageBoxButtonData> data;
    for (size_t i = 0; i < buttons.size(); i++)
        data.push_back({i == 0 ? (Uint32)SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0u, int(i), buttons[i].c_str()});
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, nullptr, title.c_str(), text.c_str(), int(data.size()), data.data(), nullptr};
    int choice = -1;
    return SDL_ShowMessageBox(&box, &choice) ? choice : -1;
#else
    (void)title; (void)text; (void)buttons;
    return -1;
#endif
}

static void apply_portable_mode() {
    if (!host::portable()) return;
    const std::string u = host::portable_user_dir();
    std::error_code ec;
    std::filesystem::create_directories(u, ec);
    auto set = [](const char* k, const std::string& v) {
        if (getenv(k)) return;  // an explicit override wins
#ifdef _WIN32
        _putenv_s(k, v.c_str());
#else
        setenv(k, v.c_str(), 0);
#endif
    };
    set("WWHD_STATE_DIR", u + "/states");
#ifdef __APPLE__
    set("WWHD_DISPLAY_SETTINGS", u + "/display.plist");
#endif
}

// The Vulkan renderer's validated opt-in CPU paths (docs/vulkan.md, "Opt-in CPU experiments"), on by
// default on every platform: on a Galaxy S25 Ultra they took the render thread from about 40 to
// 30 ms per frame; on an M3 Max (MoltenVK) together they cut render-thread CPU by 8-14% with no
// measurable cost from any single one (docs/performance.md, 2026-10-07). NAME=0 turns one off.
static void default_vulkan_cpu_paths() {
    for (const char* name : reporthdr::kVulkanCpuPaths) {  // the list: report_header.cpp
#ifdef _WIN32
        if (!getenv(name)) _putenv_s(name, "1");
#else
        setenv(name, "1", 0);
#endif
    }
}

// captures/wwhd.log: the whole log of this run (the previous run's is kept as wwhd-previous.log), so
// players can attach it to an issue; on Windows the console output of the game is otherwise lost.
// User paths are redacted as in crash logs. WWHD_LOG_FILE=<path> writes elsewhere, =0 turns it off
// (Android: on by default). The file stops at 64 MiB.
static int g_log_fd = -1;
static size_t g_log_bytes = 0;
static constexpr size_t kLogFileMax = 64u << 20;
static void log_file_raw(int fd, const char* s, size_t n) {
#ifdef _WIN32
    if (fd >= 0) _write(fd, s, (unsigned)n);
#else
    if (fd >= 0 && write(fd, s, n) < 0) {}
#endif
}
static void log_file_line(int fd, const char* s, size_t n) {
    crash_context::redact(fd, {s, n}, log_file_raw);
    if (n == 0 || s[n - 1] != '\n') log_file_raw(fd, "\n", 1);
}
static void log_file_sink(const char* s, size_t n) {
    if (g_log_fd < 0 || g_log_bytes > kLogFileMax) return;
    log_file_line(g_log_fd, s, n);
    g_log_bytes += n + 1;
    if (g_log_bytes > kLogFileMax) {
        static const char note[] = "[log] the log file reached 64 MiB; later lines go to the console only\n";
        log_file_raw(g_log_fd, note, sizeof note - 1);
    }
}
// Crash backtraces name the game function of an address in game code (crash_addr.h): the compiled
// functions' host entries, sorted, with their game addresses. Built once; never freed.
static void index_game_functions() {
    std::vector<std::pair<uintptr_t, uint32_t>> v;
    v.reserve(g_recomp_func_count);
    for (unsigned i = 0; i < g_recomp_func_count; i++)
        if (g_recomp_funcs[i].fn) v.push_back({(uintptr_t)g_recomp_funcs[i].fn, g_recomp_funcs[i].addr});
    std::sort(v.begin(), v.end());
    auto* host = new uintptr_t[v.size()];
    auto* guest = new uint32_t[v.size()];
    for (size_t i = 0; i < v.size(); i++) host[i] = v[i].first, guest[i] = v[i].second;
    crash_addr::set_game_functions(host, guest, v.size());
}

// Android defaults on; elsewhere the player turns on Graphics > "Write a log file" (logFile=1),
// or WWHD_LOG_FILE is set: 1 = the default file, a path = that file, 0 = off.
static void start_log_file(bool error = false) {
    const char* e = getenv("WWHD_LOG_FILE");
    if (error) e = "1"; // preserve diagnostics even when continuous logging is disabled
    if (e && !strcmp(e, "0")) return;
    const bool chosen = e && *e && strcmp(e, "1");  // a path
    std::string saved;
    const char* overlay_trace = getenv("WWHD_OVERLAY_TRACE");
    const bool tracing_overlay = overlay_trace && !strcmp(overlay_trace,"1");
    const bool hasSaved = hostui::get("logFile", saved);
#ifdef __ANDROID__
    const bool enabledBySetting = !hasSaved || saved != "0";
#else
    const bool enabledBySetting = hasSaved && saved == "1";
#endif
    if (!(e && *e) && !tracing_overlay && !enabledBySetting) return;
    std::string path = chosen ? e : "captures/wwhd.log";
    std::error_code ec;
    if (!chosen) {
        std::filesystem::create_directories("captures", ec);
        std::filesystem::remove("captures/wwhd-previous.log", ec);
        std::filesystem::rename(path, "captures/wwhd-previous.log", ec);
    }
#ifdef _WIN32
    g_log_fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    g_log_fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
    if (g_log_fd < 0) { LOG("[log] cannot write %s", path.c_str()); return; }
    log_ring_write(g_log_fd, log_file_line);  // the lines logged before (only the boot so far)
    log_set_sink(log_file_sink);
    LOG("[log] writing %s", path.c_str());
}

extern "C" void synthetic_game_frame(Cpu*, PpcFunc);

int main(int argc, char** argv) {
    std::string shader_prebuild_root;
    // Android's isolated preparation activity and normal SDL startup share the
    // same private user directory and verified native library identity.
    for (int i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "--user-dir")) {
#ifdef _WIN32
            _putenv_s("WWHD_USER_DIR", argv[++i]);
#else
            setenv("WWHD_USER_DIR", argv[++i], 1);
#endif
        } else if (!strcmp(argv[i], "--shader-prebuild-root")) shader_prebuild_root = argv[++i];
        else if (!strcmp(argv[i], "--shader-prebuild-result")) shaderprebuild::resultPath = argv[++i];
        else if (!strcmp(argv[i], "--shader-prebuild-library")) shaderprebuild::runtimeLibrary = argv[++i];
    }
#ifdef WWHD_DECOMP
    mods::code::startup(argc, argv, true);  // the decomp version: never swapped for the normal game's code-mod build
#else
    mods::code::startup(argc, argv);
#endif
    apply_portable_mode();
    default_vulkan_cpu_paths();
#ifdef _WIN32
    // Windows sleeps in steps of the system timer (15.6 ms by default): sleep_for(1 ms) took
    // 15.7 ms, the 3 ms AX frame loop ran in bursts and vsync waits alternated 15.7 / 31.5 ms.
    // 1 ms resolution for the whole process; Windows restores it when the process exits.
    timeBeginPeriod(1);
#endif
#ifdef __ANDROID__
    // Everything lives in the app's external files folder (Android/data/<package>/files), which a
    // computer can reach over USB: game/ (the extracted game: code, content, meta), save/, and the
    // settings and shader caches in config/. Relative paths (captures/) resolve there too.
    if (const char* dir = SDL_GetAndroidExternalStoragePath()) {
        if (chdir(dir) != 0) fprintf(stderr, "cannot enter %s\n", dir);
        setenv("XDG_CONFIG_HOME", (std::string(dir) + "/config").c_str(), 1);
        // draw batching (the default everywhere since; kept explicit) and the CPU paths
        // (default_vulkan_cpu_paths) are on; env.txt can turn any off
        setenv("WWHD_VK_DRAW_BATCH", "2048", 0);
        // 60 fps (frame interpolation) is on unless chosen otherwise (settings.ini, read when the
        // renderer starts); platform/perf_hint.cpp pauses it where the phone cannot keep up
        if (!getenv("WWHD_INTERP") && !getenv("WWHD_TRUE60")) interp::set_mode(1);
        // env.txt there: one NAME=value per line (the WWHD_ options of the README); # comments
        if (FILE* f = fopen("env.txt", "r")) {
            char line[512];
            while (fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                char* eq = strchr(line, '=');
                if (line[0] == '#' || !eq || eq == line) continue;
                *eq = 0;
                setenv(line, eq + 1, 1);
                LOG("[boot] env.txt: %s=%s", line, eq + 1);
            }
            fclose(f);
        }
    }
#endif
    bool warm_shaders = false;
    bool save_arg = false;  // --save: the command line wins over a save folder chosen in the Saves tab
    bool install_story_notebook = false;
#ifdef WWHD_HAS_VULKAN
    bool renderer_smoke = false;
#ifdef __ANDROID__
    bool display_smoke = false;
#endif
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc) config::game_dir = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) config::save_dir = argv[++i], save_arg = true;
        else if (!strcmp(argv[i], "--trace")) g_trace_hle = true;
        else if (!strcmp(argv[i], "--warm-shaders")) warm_shaders = true;
        else if (!strcmp(argv[i], "--install-story-notebook")) install_story_notebook = true;  // setup's step (mods/required.h)
        else if (!strcmp(argv[i], "--editor")) editor::set_launch_mode(true);  // the world editor (docs/mod-editor.md)
#ifdef WWHD_HAS_VULKAN
        else if (!strcmp(argv[i], "--renderer-smoke")) renderer_smoke = true;
#ifdef __ANDROID__
        else if (!strcmp(argv[i], "--shared-general-smoke")) setenv("WWHD_VK_SHARED_GENERAL", "1", 1);
        else if (!strcmp(argv[i], "--isolated-secondary-smoke")) setenv("WWHD_VK_ISOLATED_SECONDARY", "1", 1);
        else if (!strcmp(argv[i], "--display-smoke")) {
            renderer_smoke = true; display_smoke = true;
            setenv("WWHD_VK_GPU_TIMESTAMPS", "1", 0); // optional, game-free measurements
            setenv("WWHD_VK_STATS", "1", 0); // attribute acquire/present/submission delays
        }
#endif
#endif
    }
    crash_context::initialize();
    editor_ui::register_tab();  // the world editor's Editor tab (only with --editor)
    index_game_functions();
    install_crash_handler();
    start_log_file();
    exception_report::sink.store(+[](const char* message) noexcept {
        static std::once_flag error_log;
        std::call_once(error_log, [] { if (g_log_fd < 0) start_log_file(true); });
        LOG("[runtime error] %s", message);
    }, std::memory_order_release);
    if (const char* mode = getenv("WWHD_TEST_RUNTIME_ERROR")) {
        if (!strcmp(mode, "fatal")) fatal("synthetic fatal message");
        synthetic_game_frame(nullptr, +[](Cpu*) {
            const char* mode = getenv("WWHD_TEST_RUNTIME_ERROR");
            if (!strcmp(mode, "explicit")) exception_report::raise("synthetic explicit message");
            if (!strcmp(mode, "library")) throw std::out_of_range("synthetic library message");
            throw 42;
        });
    }
    // which build on which system: also in crash logs (their last log lines)
    LOG("[boot] Wind Waker HD %s (%s)%s%s, %s", build::version(), build::commit(), *build::variant() ? " " : "",
        build::variant(), reporthdr::os_description().c_str());
#if defined(__linux__) && !defined(__ANDROID__)
    // a v0.2.13 "<AppImage>.data" folder is no longer used (issue #55): once per start, in the log and
    // as the overlay's short notice
    if (const std::string hint = appimage::legacy_data_notice(getenv("APPIMAGE")); !hint.empty()) {
        LOG("[appimage] %s", hint.c_str());
        ss::notice(hint);
    }
#endif
    // test aid: WWHD_TEST_HOST_CRASH=1 crashes inside a system library (strlen of a bad pointer), so
    // the crash log's module names can be checked (CTest crash_log_module, runtime/tools/crash_log_test.cmake)
    if (getenv("WWHD_TEST_HOST_CRASH")) {
        LOG("[boot] WWHD_TEST_HOST_CRASH: crashing on purpose in the C library");
        size_t (*volatile len)(const char*) = strlen;
#ifndef _WIN32
        // the C library's own strlen: zig links its own copy into the executable (Linux releases)
        if (void* f = dlsym(RTLD_DEFAULT, "strlen")) len = (size_t (*)(const char*))f;
#endif
        LOG("%zu", len((const char*)(uintptr_t)16));
    }
    // Metal, Vulkan or a registered platform backend: --renderer=, WWHD_RENDERER_RUNTIME, Graphics > Renderer
    // (gfx/renderer.h, gfx/renderer_ext.h)
    render::choose(argc, argv);
#ifdef WWHD_HAS_VULKAN
    if(renderer_smoke) {
        // GPU self-test of the Vulkan renderer (no game files): always Vulkan, no fallback
        int result = 1;
        host::with_autorelease_pool([&] {
            render::g_backend = &render::vulkan_backend();
            try {
                render::g_backend->init();
            } catch (const std::exception& e) {
                LOG("[renderer smoke] FAIL: Vulkan could not start: %s", e.what());
                return;
            }
#ifdef __ANDROID__
            if (display_smoke) {
                result = gfxvk::android_display_smoke_test();
                gx2::shutdown_vulkan_caches();
            }
            else
#endif
                result = gfxvk::renderer_smoke_test();
        });
        return result;
    }
#endif
    // Save backups (Mods tab): a restore chosen there is applied now, before the game reads saves.
    // A shader warm-up run (setup, Android's preparation activity) never touches saves.
#ifndef __ANDROID__
    // A save folder chosen in Settings > Saves (save_folder.h), for example inside a cloud folder:
    // a change made there is applied now, and the folder is checked before the game reads its save.
    if (!save_arg && !warm_shaders) {
        const auto chosen = save_folder::startup(config::save_dir, host::config_dir(), ask_player,
                                                 [](const std::string& line) { LOG("%s", line.c_str()); });
        if (chosen.quit) {
            fflush(stderr);
            _Exit(0);
        }
        config::save_dir = chosen.save_dir.string();
    }
#endif
    mods::saves::startup(config::save_dir, [](const std::string& line) { LOG("%s", line.c_str()); }, !warm_shaders,
                         save_folder::safe_writes() ? save_folder::state().backups : std::filesystem::path());
    mods::manager::load_saved();  // player choices, before the game starts
    mods::cemu::set_vulkan(render::requested()==render::Api::Vulkan);
    mods::content::set_game_root(config::game_dir);  // loose imports (fan translations) find their game path
    mods::packages::set_code_mod_support(guestmods::hooks_built() && mods::code::enabled());
    mods::packages::initialize();
    mem::init();
    auto valid_mod_memory = [](uint32_t address, size_t size) {
        if (size > 1024 * 1024) return false;
        uint64_t end = uint64_t(address) + size;
        return (address >= mem::kMem2Start && end <= mem::kMem2End) ||
               (address >= mem::kMem1 && end <= uint64_t(mem::kMem1) + mem::kMem1Size) ||
               (address >= mem::kFgBucket && end <= uint64_t(mem::kFgBucket) + mem::kFgBucketSize);
    };
    // Store noncapturing callbacks: native mods operate on guest data, on the game thread.
    static auto valid_memory = valid_mod_memory;
    mods::packages::set_memory_access(
        [](uint32_t a, void* out, size_t n) -> int {
            if (!out || !valid_memory(a, n)) return 0;
            memcpy(out, mem::ptr(a), n); return 1;
        },
        [](uint32_t a, const void* in, size_t n) -> int {
            if (!in || !valid_memory(a, n)) return 0;
            memcpy(mem::ptr(a), in, n); return 1;
        });

    LoadedModule m{};
    std::string rpx = config::game_dir + "/code/cking.rpx";
    if (!load_rpx(rpx, m)) fatal("cannot load %s", rpx.c_str());
    if (m.entry != g_recomp_entry_point) fatal("%s does not match the recompiled code", rpx.c_str());
    LOG("[boot] loaded %s (%s build, title %s): entry %08X sda %08X sda2 %08X data end %08X", rpx.c_str(),
        g_guest_build_name, g_guest_build_title_id, m.entry, m.sda_base, m.sda2_base, m.data_end);

    dispatch::init();
    // the story system's required package (mods/required.h): what is installed, no network at the start.
    // Setup's step (--install-story-notebook) downloads it here and exits.
    if (install_story_notebook) {
        mods::required::Context rc;
        std::string version = build::version();
        rc.catalogue = mods::required::catalogue_url(version);
        rc.port = mods::catalogue::port_version_from_build(version);  // ahead of a release: the next one
        rc.fetch = host::download_https;
        rc.build = g_guest_build_name;
        const std::string dir = mods::packages::directory();
        if (!dir.empty()) rc.work = std::filesystem::path(dir).parent_path() / "Catalogue";
        const bool ok = !dir.empty() && mods::story_system() && mods::required::install(rc);
        const auto st = mods::required::status();
        std::string message = ok ? "The story notebook is installed." : !mods::story_system() ? "The story system is off." : st.text, quoted;
        for (unsigned char ch : message) {  // one JSON line for setup: escape quotes, backslashes and control bytes
            if (ch == '"' || ch == '\\') quoted += '\\', quoted += char(ch);
            else if (ch < 0x20) quoted += ' ';
            else quoted += char(ch);
        }
        printf("{\"ok\": %s, \"message\": \"%s\"}\n", ok ? "true" : "false", quoted.c_str());
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }
    if (mods::packages::has_code_mod_support() && mods::required::check_local()) mods::packages::refresh_guest_startup();
    guestmods::set_package_observer(editor::register_package);  // the world editor's inspector
    guestmods::init();  // trusted manager packages, before guest threads start
#ifdef WWHD_DECOMP
    decomp::init();  // decomp build: which functions run decompiled (runtime/src/decomp), after the mods
#endif
    init_data_imports();
    mem_setup_heaps(m.data_end);
    threads::init(m);

    uint32_t argv_arr = mem::runtime_alloc(16);
    uint32_t arg0 = mem::runtime_alloc(16);
    mem::write_cstr(arg0, "cking.rpx", 16);
    st32(argv_arr, arg0);
    // the game runs on its own threads; the process main thread belongs to the window system
    const std::string shader_renderer = render::requested() == render::Api::Metal ? "metal" : "vulkan";
    // a platform backend (gfx/renderer_ext.h) keeps its own shader caches: Metal's and Vulkan's prepared
    // shaders are not activated for it, and a warm-up run has nothing to prepare
    const bool own_shader_caches = render::requested() == render::Api::Platform;
    if (own_shader_caches) {
        LOG("[shaders] %s renderer: prepared Metal / Vulkan shaders not used (it keeps its own caches)",
            render::api_name(render::requested()));
        if (warm_shaders) return 3;  // tools/installer/shader_prebuild.py: RENDERER_UNAVAILABLE
    }
    bool pending_shader_warm = false;
#ifdef __ANDROID__
    if (warm_shaders && (shader_prebuild_root.empty() || std::filesystem::path(shader_prebuild_root).lexically_normal() == shaderprebuild::root().lexically_normal()))
        pending_shader_warm = shaderprebuild::activate_pending(config::game_dir, shader_renderer);
    if (warm_shaders && !shader_prebuild_root.empty() && !pending_shader_warm) {
        shaderprebuild::finish_pending(false); return 1;
    }
#endif
    if (warm_shaders) shaderprebuild::bind_setup_live_caches();
    if (!warm_shaders && !pending_shader_warm && !own_shader_caches) shaderprebuild::activate(config::game_dir, shader_renderer);
    render::init();
    if (warm_shaders && render::active() != render::requested()) {
        // the requested renderer could not start (e.g. no MoltenVK) and the game fell back:
        // warming the fallback would claim the wrong renderer's caches as prepared
        fprintf(stderr, "[shaders] %s renderer unavailable; nothing to prepare for it\n",
                render::api_name(render::requested()));
        return 3;  // tools/installer/shader_prebuild.py: RENDERER_UNAVAILABLE
    }
    mods::cemu::set_vulkan(render::active()==render::Api::Vulkan);
    if (warm_shaders || pending_shader_warm) {
        int warm_result = 1;
        // compile the shader head start once (fills the macOS Metal shader cache), then quit
#ifdef WWHD_HAS_METAL
        if (render::active() == render::Api::Metal) warm_result = gfx_headstart_warm();
#endif
#ifdef WWHD_HAS_VULKAN
        if (render::active() == render::Api::Vulkan) warm_result = gfxvk::warm_headstart_shaders();
#endif
        if (pending_shader_warm && !shaderprebuild::finish_pending(warm_result == 0)) warm_result = 1;
        if (warm_shaders) return warm_result;
        if (warm_result) fprintf(stderr, "[shaders] Preparation incomplete; continuing with exact on-demand compilation.\n");
    }
    static LoadedModule mod = m;
    static uint32_t args = argv_arr;
    std::thread([] {
        threads::run_main(mod, 1, args);
        LOG("[boot] game main thread returned");
        std::exit(0);
    }).detach();
    render::run_main_loop();
    return 0;
}
