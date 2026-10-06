// coreinit: logging, dynamic loading, system info, and small odds and ends.
#include "../overlay/hostui.h"
#include "../crashrec.h"
#include "../game_languages.h"
#include <cstdlib>
#include "../true60.h"
#include <filesystem>
#include <ctime>
#include <vector>
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../runtime.h"

// ---------------------------------------------------------------- guest printf
// Arguments come either from registers (OSReport: r4.., f1..) or a PPC va_list.
struct GuestArgs {
    Cpu* c = nullptr;
    int gpr = 0, fpr = 0;             // next register index (0-based from r3 / f1)
    uint32_t reg_save = 0, overflow = 0;  // va_list mode when reg_save != 0
    uint32_t next_u32() {
        if (reg_save) {
            if (gpr < 8) return ld32(reg_save + 4 * gpr++);
            uint32_t v = ld32(overflow);
            overflow += 4;
            return v;
        }
        if (gpr < 8) return c->r[3 + gpr++];
        uint32_t v = ld32(overflow);
        overflow += 4;
        return v;
    }
    uint64_t next_u64() {
        if (gpr & 1) gpr++;  // 64-bit values use aligned register pairs
        uint64_t hi = next_u32();
        return hi << 32 | next_u32();
    }
    double next_f64() {
        if (reg_save) {
            if (fpr < 8) return u64_as_f64(ld64(reg_save + 32 + 8 * fpr++));
            overflow = (overflow + 7) & ~7u;
            double v = u64_as_f64(ld64(overflow));
            overflow += 8;
            return v;
        }
        if (fpr < 8) return c->f[1 + fpr++].ps0;
        overflow = (overflow + 7) & ~7u;
        double v = u64_as_f64(ld64(overflow));
        overflow += 8;
        return v;
    }
};

std::string guest_format(const std::string& fmt, GuestArgs& a) {
    std::string out;
    char buf[512];
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        size_t j = i + 1;
        std::string spec = "%";
        while (j < fmt.size() && strchr("-+ #0123456789.*", fmt[j])) spec += fmt[j++];
        int lcount = 0;
        while (j < fmt.size() && strchr("hlLqjzt", fmt[j])) { if (fmt[j] == 'l' || fmt[j] == 'q' || fmt[j] == 'L') lcount++; j++; }
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        i = j;
        switch (conv) {
        case '%': out += '%'; break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c':
            if (lcount >= 2) snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), (long long)a.next_u64());
            else if (conv == 'd' || conv == 'i') snprintf(buf, sizeof buf, (spec + conv).c_str(), (int)a.next_u32());
            else snprintf(buf, sizeof buf, (spec + conv).c_str(), a.next_u32());
            out += buf;
            break;
        case 'p': snprintf(buf, sizeof buf, "0x%08X", a.next_u32()); out += buf; break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            snprintf(buf, sizeof buf, (spec + conv).c_str(), a.next_f64());
            out += buf;
            break;
        case 's': {
            uint32_t p = a.next_u32();
            std::string s = p ? mem::read_cstr(p) : "(null)";
            snprintf(buf, sizeof buf, (spec + "s").c_str(), s.c_str());
            out += buf;
            break;
        }
        default: out += spec + conv; break;
        }
    }
    return out;
}

static void report(const std::string& s) {
    std::string t = s;
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    LOG("[game] %s", t.c_str());
}

// Console language for UCReadSysConfig("cafe.language"): WWHD_LANGUAGE=<Wii U code> (0 ja, 1 en,
// 2 fr, 3 de, 4 it, 5 es, 6 zh, 7 ko, 8 nl, 9 pt, 10 ru, 11 zh-TW), else the setting saved by the
// settings overlay (Language tab, hostui "language"; read once at start). Unset, out of range or not
// a number: English. A language the disc has no pack for (game_languages.h) becomes English (or the
// disc's first language without English), as the USA game itself does with one it doesn't know.
static uint32_t console_language() {
    static const uint32_t lang = [] {
        const char* why = "WWHD_LANGUAGE";
        std::string saved;
        const char* e = getenv("WWHD_LANGUAGE");
        if ((!e || !*e) && hostui::get("language", saved)) {
            e = saved.c_str();
            why = "saved setting";
        }
        long v = 1;
        if (e && *e) {
            char* end = nullptr;
            v = strtol(e, &end, 10);
            if (*end || v < 0 || v >= game_lang::kLanguages) {
                LOG("[config] language %s (%s) is not a language code 0..11; using English", e, why);
                v = 1;
            } else {
                LOG("[config] console language %ld (%s)", v, why);
            }
        }
        const int use = game_lang::usable((int)v);
        if (use != v)
            LOG("[config] %s is not on this disc (%s); the game gets %s", game_lang::name((int)v),
                game_lang::region().c_str(), game_lang::name(use));
        game_lang::set_started(use);
        return (uint32_t)use;
    }();
    return lang;
}

HLE(coreinit, OSReport) {
    GuestArgs a;
    a.c = c;
    a.gpr = 1;
    a.overflow = c->r[1] + 8;
    report(guest_format(mem::read_cstr(arg(c, 0)), a));
}

HLE(coreinit, OSVReport) {
    uint32_t va = arg(c, 1);
    GuestArgs a;
    a.c = c;
    a.gpr = ld8(va);
    a.fpr = ld8(va + 1);
    a.overflow = ld32(va + 4);
    a.reg_save = ld32(va + 8);
    report(guest_format(mem::read_cstr(arg(c, 0)), a));
}

HLE(coreinit, OSConsoleWrite) {
    report(std::string((const char*)mem::ptr(arg(c, 0)), arg(c, 1)));
}


// On a game halt: captures/crash-<time>.log with the guest call chain (named from build/names.tsv
// when present), the 60 fps pass state and the executing process, for reports from normal play.
namespace interp { const char* phase_name(); }
static void write_crash_log(Cpu* c, const std::string& file, uint32_t line, const std::string& msg) {
    std::unordered_map<uint32_t, std::string> names;
    std::vector<uint32_t> starts;
    if (FILE* f = fopen("build/names.tsv", "r")) {
        char buf[512];
        while (fgets(buf, sizeof buf, f)) {
            uint32_t a;
            char nm[400];
            if (sscanf(buf, "%x\t%399[^\t\n]", &a, nm) == 2) names[a] = nm, starts.push_back(a);
        }
        fclose(f);
        std::sort(starts.begin(), starts.end());
    }
    auto name = [&](uint32_t pc) -> std::string {
        auto it = std::upper_bound(starts.begin(), starts.end(), pc);
        if (it == starts.begin()) return "?";
        uint32_t s0 = *--it;
        char b[32];
        snprintf(b, sizeof b, "+0x%X", pc - s0);
        return pc - s0 < 0x4000 ? names[s0] + b : "?";
    };
    std::error_code ec; std::filesystem::create_directories("captures",ec);
    time_t t = time(nullptr);
    char path[96];
    strftime(path, sizeof path, "captures/crash-%Y%m%d-%H%M%S.log", localtime(&t));
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "halt at %s:%u: %s\n", file.c_str(), line, msg.c_str());
    fprintf(f, "60 fps pass: %s; true 60 %s, half pass %d, executing process %08X", interp::phase_name(),
            true60::enabled() ? "on" : "off", (int)true60::half_pass(), true60::exec_proc());
    if (uint32_t p = true60::exec_proc()) fprintf(f, " (words %08X %08X %08X %08X)", ld32(p), ld32(p + 4), ld32(p + 8), ld32(p + 12));
    fprintf(f, ", dt %.2f\nlr %08X %s\n", true60::dt(), c->lr, name(c->lr).c_str());
    for (uint32_t sp = c->r[1], i = 0; i < 40 && sp; i++) {
        uint32_t prev = ld32(sp);
        if (!prev || prev <= sp) break;
        uint32_t ra = ld32(prev + 4);
        fprintf(f, "  <- %08X %s\n", ra, name(ra).c_str());
        sp = prev;
    }
    static FILE* out_file;
    out_file = f;
    auto out = [](int, const char* t, size_t n) { fwrite(t, 1, n, out_file); };
    crashrec::crash_note(0, out);
    fputs("\n--- last log lines ---\n", f);
    log_ring_write(0, out);
    fclose(f);
    fprintf(stderr, "[crash] wrote %s\n", path);
}

HLE(coreinit, OSPanic) {
    GuestArgs a;
    a.c = c;
    a.gpr = 3;
    a.overflow = c->r[1] + 8;
    std::string msg = guest_format(mem::read_cstr(arg(c, 2)), a);
    write_crash_log(c, mem::read_cstr(arg(c, 0)), arg(c, 1), msg);
    fatal("OSPanic at %s:%d: %s", mem::read_cstr(arg(c, 0)).c_str(), arg(c, 1), msg.c_str());
}

// ---------------------------------------------------------------- OSDynLoad
static std::mutex g_dyn_mutex;
static std::unordered_map<uint32_t, std::string> g_dyn_modules;
static std::unordered_map<std::string, uint32_t> g_dyn_exports;

HLE(coreinit, OSDynLoad_Acquire) {
    std::string name = mem::read_cstr(arg(c, 0));
    if (name.size() > 4 && name.substr(name.size() - 4) == ".rpl") name.resize(name.size() - 4);
    uint32_t h = 0x70000000 | (uint32_t)(std::hash<std::string>()(name) & 0x0FFFFFFF);
    {
        std::lock_guard<std::mutex> lk(g_dyn_mutex);
        g_dyn_modules[h] = name;
    }
    TRACE("[dynload] Acquire(%s) -> %08X", name.c_str(), h);
    st32(arg(c, 1), h);
    ret(c, 0);
}

HLE(coreinit, OSDynLoad_Release) {}
HLE(coreinit, OSDynLoad_SetAllocator) { ret(c, 0); }

HLE(coreinit, OSDynLoad_FindExport) {
    uint32_t h = arg(c, 0), is_data = arg(c, 1), out = arg(c, 3);
    std::string sym = mem::read_cstr(arg(c, 2));
    std::lock_guard<std::mutex> lk(g_dyn_mutex);
    std::string lib = g_dyn_modules.count(h) ? g_dyn_modules[h] : "?";
    std::string key = lib + "." + sym;
    auto it = g_dyn_exports.find(key);
    if (it == g_dyn_exports.end()) {
        uint32_t addr = 0;
        if (!is_data) {
            PpcFunc fn = hle_find(lib.c_str(), sym.c_str());
            if (fn) addr = dispatch::register_host(fn, strdup(key.c_str()));
        }
        if (!addr) {
            LOG("[dynload] FindExport(%s, %s) not implemented", key.c_str(), is_data ? "data" : "func");
            // hand out a stub that logs when called, so the game can still look it up
            static void (*stub)(Cpu*) = [](Cpu* c) { log_msg("[dynload] call to unimplemented dynamic export (lr=%08X)", c->lr); c->r[3] = 0; };
            addr = is_data ? mem::runtime_alloc(0x100) : dispatch::register_host(stub, strdup(key.c_str()));
        }
        it = g_dyn_exports.emplace(key, addr).first;
    }
    st32(out, it->second);
    ret(c, 0);
}

// ---------------------------------------------------------------- system info
HLE(coreinit, OSGetSystemInfo) {
    static uint32_t info = 0;
    if (!info) {
        info = mem::runtime_alloc(0x20);
        st32(info + 0x00, 248625000);   // bus clock
        st32(info + 0x04, 1243125000);  // core clock
        st64(info + 0x08, 0);           // base time
        st32(info + 0x10, 0);
    }
    ret(c, info);
}

HLE(coreinit, OSGetSharedData) { ret(c, 0); }
HLE(coreinit, OSIsDebuggerPresent) { ret(c, 0); }
HLE(coreinit, OSIsDebuggerInitialized) { ret(c, 0); }
HLE(coreinit, OSEnableHomeButtonMenu) { ret(c, 1); }
HLE(coreinit, OSSavesDone_ReadyToRelease) {}
HLE(coreinit, IMDisableDim) { ret(c, 0); }
HLE(coreinit, IMEnableDim) { ret(c, 0); }
HLE(coreinit, IMIsDimEnabled) { if (arg(c, 0)) st32(arg(c, 0), 0); ret(c, 0); }
HLE(coreinit, ENVGetEnvironmentVariable) { if (arg(c, 1) && arg(c, 2)) st8(arg(c, 1), 0); ret(c, 1); }
HLE(coreinit, __gh_set_errno) {}

HLE(coreinit, exit) { LOG("[game] exit(%d)", (int)arg(c, 0)); std::exit((int)arg(c, 0)); }
HLE(coreinit, _Exit) { LOG("[game] _Exit(%d)", (int)arg(c, 0)); std::_Exit((int)arg(c, 0)); }

// ---------------------------------------------------------------- UC (system settings)
// UCSysConfig entries are 0x54 bytes: name[64], access u32, dataType u32, error s32, dataSize u32, dataPtr u32
HLE(coreinit, UCOpen) { ret(c, 1); }
HLE(coreinit, UCClose) { ret(c, 0); }
HLE(coreinit, UCReadSysConfig) {
    uint32_t count = arg(c, 1), items = arg(c, 2);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = items + i * 0x54;
        std::string name = mem::read_cstr(e);
        uint32_t size = ld32(e + 0x4C), data = ld32(e + 0x50);
        uint32_t value = 0;
        if (name == "cafe.language") value = console_language();  // WWHD_LANGUAGE or saved, default English
        else if (name == "cafe.cntry_reg") value = 49;   // USA
        else if (name == "cafe.eula_agree") value = 1;
        else if (name == "cafe.initial_launch") value = 2;
        else if (name == "parent.enable") value = 0;
        else if (name == "p_acct1.network_launcher") value = 0;
        TRACE("[uc] read %s (size %u) -> %u", name.c_str(), size, value);
        if (data && size) {
            memset(mem::ptr(data), 0, size);
            if (size == 1) st8(data, value);
            else if (size == 2) st16(data, value);
            else if (size >= 4) st32(data, value);
        }
        st32(e + 0x48, 0);
    }
    ret(c, 0);
}
