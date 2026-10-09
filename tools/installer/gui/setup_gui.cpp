// Wind Waker HD: the program a release starts (SDL3 + Dear ImGui).
//
// The release contains no game code, so the first start prepares the game once (choose the dump,
// keys for a disc image, then extract/translate/compile); later starts launch the built game directly, without a
// window of their own. Holding Shift while starting (macOS, Windows) or --setup opens the setup
// screens again (repair, update, change the game, import saves).
//
// Only a front end. Everything the installation does (keys, extraction, recompiling, compiling,
// the app, save import) is tools/installer/setup.py, which this program runs as a child process
// through the release's own launcher ("Install Wind Waker HD.command" or install.sh; they also fetch Python
// where needed) or, on Windows, directly with the embeddable Python the release ships in tools\python
// (console_setup_win.cpp), with --gui-protocol: JSON lines on the child's stdout (events) and stdin (requests).
// See tools/installer/README.md.
//
// Keys: a pasted Wii U common key is sent once over the stdin pipe and the buffer is cleared;
// it is never shown, logged, stored or put on a command line.
//
// Options (any other arguments are passed on to setup.py, e.g. --data-dir DIR --app-dir DIR):
//   --automate FILE      scripted run for tests (see run_automation below)
//   --screenshots DIR    where --automate / --self-test write PNG screenshots
//   --self-test          start setup.py, wait for its hello, render the welcome screen, exit 0
//   --console-setup ARGS (Windows, first argument only) the setup in the console window this program was
//                        started from: run setup.py ARGS with the bundled Python, exit with its exit code
//                        (tools\Setup in a console window.bat)
// With SDL_VIDEO_DRIVER=offscreen the window is never shown (software rendering).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cerrno>

#ifdef _WIN32
#include <windows.h>

#include "console_setup_win.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

// the release's terminal setup (the fallback), as the player sees it in the release folder
#if defined(__APPLE__)
#define SETUP_IN_TERMINAL "tools/Setup in Terminal.command"
#elif defined(_WIN32)
#define SETUP_IN_TERMINAL "tools\\Setup in a console window.bat"
#else
#define SETUP_IN_TERMINAL "tools/setup-in-terminal.sh"
#endif

// ---------------------------------------------------------------------------------------------
// minimal JSON (the protocol's own messages only)

struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    const J* get(const char* k) const {
        for (auto& kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    std::string str(const char* k, const std::string& def = "") const {
        const J* v = get(k);
        return v && v->t == Str ? v->s : def;
    }
    bool boolean(const char* k) const {
        const J* v = get(k);
        return v && v->t == Bool && v->b;
    }
    double num(const char* k) const {
        const J* v = get(k);
        return v && v->t == Num ? v->n : 0;
    }
    bool is_null(const char* k) const {
        const J* v = get(k);
        return !v || v->t == Null;
    }
};

struct JParser {
    const char* p;
    const char* e;
    bool ok = true;
    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    static void put_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) out += (char)(0xC0 | (cp >> 6)), out += (char)(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            out += (char)(0xE0 | (cp >> 12)), out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
        else
            out += (char)(0xF0 | (cp >> 18)), out += (char)(0x80 | ((cp >> 12) & 0x3F)),
                out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
    }
    unsigned hex4() {
        if (e - p < 4) return ok = false, 0;
        unsigned v = 0;
        for (int i = 0; i < 4; i++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else ok = false;
        }
        return v;
    }
    std::string string() {
        std::string out;
        p++;  // opening quote
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                char c = *p++;
                switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: out += c;
                }
            } else {
                out += *p++;
            }
        }
        if (p < e) p++;
        else ok = false;
        return out;
    }
    J value() {
        J v;
        ws();
        if (p >= e) return ok = false, v;
        if (*p == '{') {
            v.t = J::Obj;
            p++;
            ws();
            if (p < e && *p == '}') return p++, v;
            while (ok && p < e) {
                ws();
                if (p >= e || *p != '"') return ok = false, v;
                std::string k = string();
                ws();
                if (p >= e || *p != ':') return ok = false, v;
                p++;
                v.o.emplace_back(k, value());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                return ok = false, v;
            }
        } else if (*p == '[') {
            v.t = J::Arr;
            p++;
            ws();
            if (p < e && *p == ']') return p++, v;
            while (ok && p < e) {
                v.a.push_back(value());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                return ok = false, v;
            }
        } else if (*p == '"') {
            v.t = J::Str;
            v.s = string();
        } else if (!strncmp(p, "true", 4)) {
            v.t = J::Bool, v.b = true, p += 4;
        } else if (!strncmp(p, "false", 5)) {
            v.t = J::Bool, p += 5;
        } else if (!strncmp(p, "null", 4)) {
            p += 4;
        } else {
            char* end = nullptr;
            v.t = J::Num;
            v.n = SDL_strtod(p, &end);
            if (end == p) ok = false;
            p = end;
        }
        return v;
    }
};

static bool parse_json(const std::string& s, J& out) {
    JParser jp{s.data(), s.data() + s.size()};
    out = jp.value();
    return jp.ok && out.t == J::Obj;
}

static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += (char)c;
        else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += (char)c;
    }
    return o + "\"";
}

// ---------------------------------------------------------------------------------------------
// the setup.py child process

struct Child {
    SDL_Process* proc = nullptr;
    std::string partial;
    std::deque<std::string> lines;
    bool exited = false;
    int exit_code = 0;

    bool start(const std::vector<std::string>& args, const std::string& cwd) {
        std::vector<const char*> argv;
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv.data());
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
#ifdef _WIN32
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);  // no console window
#endif
        proc = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        return proc != nullptr;
    }

    void poll() {
        if (!proc) return;
        SDL_IOStream* out = SDL_GetProcessOutput(proc);
        char buf[16384];
        for (;;) {
            size_t n = out ? SDL_ReadIO(out, buf, sizeof buf) : 0;
            if (n == 0) break;
            partial.append(buf, n);
        }
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string l = partial.substr(0, pos);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
            partial.erase(0, pos + 1);
        }
        if (!exited && SDL_WaitProcess(proc, false, &exit_code)) exited = true;
    }

    bool send(const std::string& json) {
        if (!proc || exited) return false;
        SDL_IOStream* in = SDL_GetProcessInput(proc);
        if (!in) return false;
        std::string l = json + "\n";
        bool ok = SDL_WriteIO(in, l.data(), l.size()) == l.size();
        SDL_FlushIO(in);
        return ok;
    }

    void stop(bool kill) {
        if (!proc) return;
        if (kill && !exited) SDL_KillProcess(proc, true);
        else if (!exited) {
            send("{\"cmd\":\"quit\"}");
            for (int i = 0; i < 50 && !SDL_WaitProcess(proc, false, &exit_code); i++) SDL_Delay(20);
            if (!SDL_WaitProcess(proc, false, &exit_code)) SDL_KillProcess(proc, true);
        }
        SDL_DestroyProcess(proc);
        proc = nullptr;
    }
};

static bool run_quick(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) return false;
    int code = -1;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code == 0;
}

// ---------------------------------------------------------------------------------------------
// starting the built game

static std::string g_exe, g_game_dir, g_data_dir;  // set when the game is ready to start

// Replaces this process with the game (macOS, Linux; the Dock keeps showing "Wind Waker HD") or starts
// it and returns (Windows). The game runs in the data folder (its crash logs go to data/captures).
static bool launch_game() {
    std::string save = g_data_dir + "/save";
#ifdef _WIN32
    auto wide = [](const std::string& u) {
        int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
        return w;
    };
    std::wstring cmd = L"\"" + wide(g_exe) + L"\" --game \"" + wide(g_game_dir) + L"\" --save \"" + wide(save) + L"\"";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    std::wstring cwd = wide(g_data_dir);
    if (!CreateProcessW(wide(g_exe).c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    if (chdir(g_data_dir.c_str()) != 0) return false;
    const char* argv[] = {g_exe.c_str(), "--game", g_game_dir.c_str(), "--save", save.c_str(), nullptr};
    execv(g_exe.c_str(), (char* const*)argv);
    return false;  // only reached when exec failed
#endif
}

static bool shift_held() {
#if defined(__APPLE__)
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskShift) != 0;
#elif defined(_WIN32)
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    return false;  // Linux: start with --setup (also a menu action of the shortcut)
#endif
}

// ---------------------------------------------------------------------------------------------
// state

enum class Screen { Starting, NeedCLT, Menu, Welcome, Source, Keys, Installing, Error, Save, Done, Fatal };

static const char* screen_name(Screen s) {
    switch (s) {
    case Screen::Starting: return "starting";
    case Screen::NeedCLT: return "clt";
    case Screen::Menu: return "menu";
    case Screen::Welcome: return "welcome";
    case Screen::Source: return "source";
    case Screen::Keys: return "keys";
    case Screen::Installing: return "installing";
    case Screen::Error: return "error";
    case Screen::Save: return "save";
    case Screen::Done: return "done";
    case Screen::Fatal: return "fatal";
    }
    return "?";
}

struct Step {
    std::string id, title;
    int state = 0;  // 0 waiting, 1 running, 2 done, 3 failed
    double done = 0, total = 0;
    std::string detail;
};

struct App {
    std::string pkg;                   // the unpacked release folder
    std::vector<std::string> passthru;  // arguments for setup.py
    Child child;
    Screen screen = Screen::Starting;
    std::string fatal;         // what failed (shown under "Setup could not continue")
    std::string fatal_hint;    // what to do about it
    std::string fatal_folder;  // a folder the fatal screen offers to show (e.g. where the app really is)
    std::string fatal_log;     // where the failure was written down

    // hello
    bool hello = false;
    std::string version, data_dir, app_dir, log_path, platform;
    bool installed = false, game_files = false, save_exists = false;
    std::string installed_version;

    // requests
    int next_id = 1, pending_id = 0;
    std::string pending_cmd;

    // source and keys
    std::string source_path, source_kind;  // kind: image | archive (Cemu .wua, no keys) | folder
    std::string probe_msg;
    bool probe_ok = false;
    std::string disc_key_found, disc_key_file, common_key_found, common_key_file;
    int common_mode = 0;  // 0 key file, 1 paste
    char paste[128] = {};
    std::string keys_msg;

    // install
    std::vector<Step> steps;
    int current = -1;
    std::string install_source;
    std::string error_msg, error_back;  // error_back: screen name for Retry
    std::string result_app;

    // portable release
    bool portable = false, legacy = false;
    std::string package, game_dir;
    double free_bytes = 0, source_bytes = 0, toolchain_bytes = 0;
    bool opt_remove_toolchain = false, opt_shortcut = false;
    std::deque<std::pair<std::string, std::string>> queue;  // requests to send one after another
    std::string after;                                       // then: play | quit | open
    bool exec_game = false;                                  // start the game when the window has closed

    // save import
    int save_kind = 0;  // 0 none, 1 HD folder, 2 GameCube .gci, 3 earlier installation, 4 another folder
    std::string save_path, save_msg;
    bool confirm_replace = false;

    // log
    std::vector<std::string> log;
    bool log_scroll = false;
    std::string toast;
    double toast_until = 0;

    // file dialogs answer on any thread
    std::mutex dlg_mu;
    std::string dlg_target, dlg_result;
    bool dlg_done = false;

    // CLT (macOS)
    double clt_next_check = 0;
    bool clt_install_started = false;

    // automation
    bool self_test = false;
    std::string shots_dir;
    J script;
    size_t script_pos = 0;
    int stable_frames = 0, ready_frames = 0;
    bool shot_taken = false;
    double step_started = 0;
    std::string autoclick;
    std::string shot_pending;
    int exit_code = -1;
    std::string last_screen;
};

static App A;

static void addlog(const std::string& s) {
    A.log.push_back(s);
    A.log_scroll = true;
}

static void go(Screen s) {
    A.screen = s;
}

static void fail(const std::string& what, const std::string& todo, const std::string& folder = "");

static int request(const std::string& cmd, const std::string& fields) {
    int id = A.next_id++;
    std::string json = "{\"cmd\":" + jstr(cmd) + ",\"id\":" + std::to_string(id) + (fields.empty() ? "" : "," + fields) + "}";
    A.pending_id = id;
    A.pending_cmd = cmd;
    if (!A.child.send(json))
        fail("The setup process is not running any more (request \"" + cmd + "\" could not be sent" +
                 (A.child.exited ? ", it ended with exit code " + std::to_string(A.child.exit_code) : std::string()) + ").",
             "Open Wind Waker HD again; finished steps are kept. If it happens again, click \"Copy log\" and attach the "
             "log to a bug report.");
    return id;
}

static bool busy() { return A.pending_id != 0; }

// ---------------------------------------------------------------------------------------------
// look

static ImFont* g_font = nullptr;
static const ImVec4 ACCENT(0.11f, 0.47f, 0.76f, 1.0f);
static const ImVec4 GOOD(0.16f, 0.60f, 0.30f, 1.0f);
static const ImVec4 BAD(0.80f, 0.20f, 0.18f, 1.0f);
static const ImVec4 MUTED(0.42f, 0.45f, 0.50f, 1.0f);

static void load_fonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
#if defined(__APPLE__)
        "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf",
#elif defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf",
#else
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", "/usr/share/fonts/cantarell/Cantarell-Regular.otf",
#endif
    };
    (void)scale;
    for (const char* c : candidates) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(c, &info) && info.type == SDL_PATHTYPE_FILE) {
            g_font = io.Fonts->AddFontFromFileTTF(c, 17.0f);
            if (g_font) break;
        }
    }
    if (!g_font) {
        ImFontConfig cfg;
        cfg.SizePixels = 17.0f;
        g_font = io.Fonts->AddFontDefaultVector(&cfg);
    }
}

static void apply_style(float scale) {
    ImGui::StyleColorsLight();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(28, 22);
    st.FramePadding = ImVec2(12, 7);
    st.ItemSpacing = ImVec2(10, 10);
    st.FrameRounding = 6;
    st.GrabRounding = 6;
    st.ChildRounding = 8;
    st.PopupRounding = 8;
    st.WindowRounding = 0;
    st.FrameBorderSize = 1;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.975f, 0.98f, 0.985f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_Border] = ImVec4(0.82f, 0.85f, 0.88f, 1);
    c[ImGuiCol_Text] = ImVec4(0.12f, 0.14f, 0.17f, 1);
    c[ImGuiCol_Button] = ImVec4(0.92f, 0.94f, 0.96f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.85f, 0.90f, 0.95f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.78f, 0.86f, 0.94f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_PlotHistogram] = ACCENT;
    c[ImGuiCol_CheckMark] = ACCENT;
    c[ImGuiCol_Header] = ImVec4(0.90f, 0.93f, 0.96f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.90f, 0.95f, 1);
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;
}

static void heading(const char* text, float size = 27.0f) {
    ImGui::PushFont(nullptr, size);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

static void muted(const std::string& t) {
    ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

static void colored(const ImVec4& col, const std::string& t) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

// a status mark drawn with lines (independent of the font's glyphs): 0 waiting, 1 running, 2 ok, 3 failed
static void mark(int state) {
    float sz = ImGui::GetFontSize();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + sz * 0.5f, p.y + sz * 0.55f);
    float r = sz * 0.45f;
    if (state == 2) {
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(GOOD));
        dl->AddLine(ImVec2(c.x - r * 0.45f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), IM_COL32_WHITE, sz * 0.12f);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.5f, c.y - r * 0.35f), IM_COL32_WHITE, sz * 0.12f);
    } else if (state == 3) {
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(BAD));
        float d = r * 0.4f;
        dl->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), IM_COL32_WHITE, sz * 0.12f);
        dl->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d), IM_COL32_WHITE, sz * 0.12f);
    } else if (state == 1) {
        float t = (float)ImGui::GetTime() * 6.0f;
        dl->PathArcTo(c, r * 0.85f, t, t + 4.4f, 24);
        dl->PathStroke(ImGui::GetColorU32(ACCENT), sz * 0.14f);
    } else {
        dl->AddCircle(c, r * 0.85f, ImGui::GetColorU32(MUTED), 24, sz * 0.08f);
    }
    ImGui::Dummy(ImVec2(sz * 1.1f, sz));
    ImGui::SameLine();
}

// a button that the automation can also press by its label
static bool button(const char* label, const ImVec2& size = ImVec2(0, 0), bool primary = false, bool enabled = true) {
    if (!enabled) ImGui::BeginDisabled();
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.53f, 0.83f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.40f, 0.66f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    bool pressed = ImGui::Button(label, size);
    if (primary) ImGui::PopStyleColor(4);
    if (!enabled) ImGui::EndDisabled();
    std::string visible = label;
    if (size_t h = visible.find("##"); h != std::string::npos) visible = visible.substr(0, h);
    if (enabled && !A.autoclick.empty() && A.autoclick == visible) {
        A.autoclick.clear();
        pressed = true;
    }
    return pressed;
}

// radio buttons with the regular text height (the frame padding of the big buttons makes them huge)
static bool radio(const char* label, int* v, int value) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    bool r = ImGui::RadioButton(label, v, value);
    ImGui::PopStyleVar();
    return r;
}

static bool checkbox(const char* label, bool* v) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    bool r = ImGui::Checkbox(label, v);
    ImGui::PopStyleVar();
    return r;
}

static float button_w(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2 + 24;
}

// right-aligned row of buttons at the bottom of the window; returns the index pressed or -1
static int footer(std::initializer_list<const char*> labels, int primary = -1, int disabled_mask = 0) {
    float h = ImGui::GetFrameHeight() + 8;
    float y = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - h;
    float total = 0;
    for (const char* l : labels) total += button_w(l) + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - total, y));
    int i = 0, hit = -1;
    for (const char* l : labels) {
        if (i) ImGui::SameLine();
        if (button(l, ImVec2(button_w(l), h), i == primary, !(disabled_mask & (1 << i)))) hit = i;
        i++;
    }
    return hit;
}

static void show_toast(const std::string& t) {
    A.toast = t;
    A.toast_until = ImGui::GetTime() + 2.5;
}

// ---------------------------------------------------------------------------------------------
// actions

static std::string read_file(const std::string& path) {
    size_t n = 0;
    void* d = path.empty() ? nullptr : SDL_LoadFile(path.c_str(), &n);
    if (!d) return "";
    std::string s((const char*)d, n);
    SDL_free(d);
    return s;
}

static void copy_log() {
    std::string text = read_file(A.log_path);
    text += "\n---- installer window ----\n";
    if (!A.fatal.empty()) text += "Setup could not continue: " + A.fatal + "\n" + A.fatal_hint + "\n";
    for (auto& l : A.log) text += l + "\n";
    SDL_SetClipboardText(text.c_str());
    show_toast("The log is on the clipboard.");
}

static std::string file_url(const std::string& path) {
    std::string u = "file://";
#ifdef _WIN32
    u += "/";
#endif
    for (unsigned char c : path) {
        if (isalnum(c) || strchr("/-_.~:", c)) u += (char)(c == '\\' ? '/' : c);
        else if (c == '\\') u += '/';
        else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            u += b;
        }
    }
    return u;
}

static void open_folder(const std::string& path) {
    SDL_OpenURL(file_url(path).c_str());
}

static void SDLCALL dialog_cb(void*, const char* const* files, int) {
    std::lock_guard<std::mutex> lk(A.dlg_mu);
    A.dlg_result = (files && files[0]) ? files[0] : "";
    A.dlg_done = true;
}

static SDL_Window* g_window = nullptr;

static void choose_file(const std::string& target, const SDL_DialogFileFilter* filters, int nf) {
    {
        std::lock_guard<std::mutex> lk(A.dlg_mu);
        A.dlg_target = target;
        A.dlg_done = false;
    }
    SDL_ShowOpenFileDialog(dialog_cb, nullptr, g_window, filters, nf, nullptr, false);
}

static void choose_folder(const std::string& target) {
    {
        std::lock_guard<std::mutex> lk(A.dlg_mu);
        A.dlg_target = target;
        A.dlg_done = false;
    }
    SDL_ShowOpenFolderDialog(dialog_cb, nullptr, g_window, nullptr, false);
}

static void set_source(const std::string& path) {
    A.source_path = path;
    A.probe_ok = false;
    A.probe_msg = "Checking...";
    A.disc_key_found.clear();
    A.common_key_found.clear();
    A.disc_key_file.clear();
    A.keys_msg.clear();
    request("probe", "\"path\":" + jstr(path));
}

// Any change to the key inputs makes the result of the last check stale: drop its message (neutral
// state until the next "Check keys"). The same for the save screen's inputs.
static void keys_changed() {
    if (A.pending_cmd != "check_keys") A.keys_msg.clear();
}

static void set_disc_key_file(const std::string& p) { A.disc_key_file = p, keys_changed(); }
static void set_common_key_file(const std::string& p) { A.common_key_file = p, keys_changed(); }
static void set_common_mode(int m) {
    if (A.common_mode != m) A.common_mode = m, keys_changed();
}
static void set_save_path(const std::string& p) { A.save_path = p, A.save_msg.clear(); }
static void set_save_kind(int k) {
    if (A.save_kind != k) A.save_kind = k, A.save_path.clear(), A.save_msg.clear();
}

static void apply_dialog(const std::string& target, const std::string& path) {
    if (path.empty()) return;
    if (target == "source") set_source(path);
    else if (target == "disc_key") set_disc_key_file(path);
    else if (target == "common_key") set_common_key_file(path);
    else if (target == "save") set_save_path(path);
}

static void start_install(const std::string& source) {
    A.install_source = source;
    A.steps.clear();
    A.current = -1;
    std::string f = "\"source\":" + jstr(source);
    if (source != "installed") f += ",\"path\":" + jstr(A.source_path);
    request("install", f);
    go(Screen::Installing);
}

static void check_keys() {
    std::string f = "\"image\":" + jstr(A.source_path);
    if (!A.disc_key_file.empty()) f += ",\"disc_key_file\":" + jstr(A.disc_key_file);
    if (A.common_mode == 1 && A.paste[0]) f += ",\"common_key_hex\":" + jstr(A.paste);
    else if (A.common_mode == 0 && !A.common_key_file.empty()) f += ",\"common_key_file\":" + jstr(A.common_key_file);
    A.keys_msg = "Checking the keys...";
    request("check_keys", f);
    f.assign(f.size(), '\0');                              // the request held the pasted key
    SDL_memset(A.paste, 0, sizeof A.paste);                // sent once; never kept
}

// ---------------------------------------------------------------------------------------------
// protocol events

static int step_index(const std::string& id) {
    for (size_t i = 0; i < A.steps.size(); i++)
        if (A.steps[i].id == id) return (int)i;
    return -1;
}

static void handle_reply(const J& ev) {
    std::string cmd = ev.str("cmd");
    bool ok = ev.boolean("ok");
    if ((int)ev.num("id") == A.pending_id) A.pending_id = 0, A.pending_cmd.clear();
    if (cmd == "probe") {
        A.probe_ok = ok;
        if (!ok) {
            A.probe_msg = ev.str("message");
            return;
        }
        A.source_kind = ev.str("kind");
        A.source_path = ev.str("path", A.source_path);
        A.disc_key_found = ev.str("disc_key");
        A.common_key_found = ev.str("common_key");
        A.source_bytes = ev.num("bytes");
        if (A.source_kind == "archive")  // setup.py says which title is used and why
            A.probe_msg = ev.str("message");
        else if (A.source_kind == "folder")
            A.probe_msg = ev.boolean("in_place") ? "Extracted game folder: The Wind Waker HD (USA). It is used where it is; "
                                                   "nothing is copied."
                                                 : "Extracted game folder: The Wind Waker HD (USA)";
        else
            A.probe_msg = "Wii U disc image. The game files are extracted from it into this folder (about 1.7 GB).";
    } else if (cmd == "check_keys") {
        if (ok) {
            A.keys_msg.clear();
            start_install("image");
        } else {
            A.keys_msg = ev.str("message");
        }
    } else if (cmd == "install") {
        if (ok) {
            for (auto& s : A.steps)
                if (s.state != 3) s.state = 2;
            A.result_app = ev.str("app");
            A.save_exists = ev.boolean("save_exists");
            A.toolchain_bytes = ev.num("toolchain_bytes");
            A.game_dir = ev.str("game_dir", A.game_dir);
            g_exe = ev.str("exe"), g_game_dir = A.game_dir, g_data_dir = ev.str("data_dir", A.data_dir);
            A.installed = true;
            addlog("Setup finished.");
            go(A.save_exists ? Screen::Done : Screen::Save);
        } else {
            if (A.current >= 0 && A.current < (int)A.steps.size()) A.steps[A.current].state = 3;
            A.error_msg = ev.str("message");
            A.error_back = A.install_source == "installed" ? "menu" : "source";
            go(Screen::Error);
        }
    } else if (cmd == "remove_toolchain" || cmd == "shortcut") {
        if (!ok) addlog(ev.str("message"));
    } else if (cmd == "import_save" || cmd == "import_existing") {
        if (ok) {
            A.save_msg = ev.str("message");
            A.save_exists = true;
            go(Screen::Done);
        } else if (ev.str("problem") == "exists") {
            A.confirm_replace = true;
        } else {
            A.save_msg = ev.str("message");
        }
    }
}

static void handle_event(const J& ev) {
    std::string e = ev.str("event");
    if (e == "hello") {
        A.hello = true;
        if (A.version.empty()) A.version = ev.str("version");
        else if (ev.str("version") != A.version)
            addlog("note: setup.py reports version " + ev.str("version") + ", the package says " + A.version);
        A.data_dir = ev.str("data_dir");
        A.app_dir = ev.str("app_dir");
        A.log_path = ev.str("log");
        A.platform = ev.str("platform");
        A.game_files = ev.boolean("game_files");
        A.save_exists = ev.boolean("save_exists");
        A.portable = ev.boolean("portable");
        A.legacy = ev.boolean("legacy");
        A.package = ev.str("package");
        A.game_dir = ev.str("game_dir");
        A.free_bytes = ev.num("free_bytes");
        const J* inst = ev.get("installed");
        A.installed = inst && inst->t == J::Obj;
        A.installed_version = A.installed ? inst->str("version") : "";
        if (A.installed) {
            A.result_app = inst->str("app");
            g_exe = inst->str("exe"), g_game_dir = inst->str("game_dir"), g_data_dir = A.data_dir;
        }
        go(A.installed ? Screen::Menu : Screen::Welcome);
    } else if (e == "log") {
        addlog(ev.str("text"));
    } else if (e == "plan") {
        A.steps.clear();
        if (const J* st = ev.get("steps"))
            for (auto& s : st->a) {
                Step x;
                x.id = s.str("id");
                x.title = s.str("title");
                A.steps.push_back(x);
            }
    } else if (e == "step") {
        int i = step_index(ev.str("id"));
        for (int k = 0; k < i; k++) A.steps[k].state = 2;
        if (i >= 0) A.steps[i].state = 1, A.current = i;
    } else if (e == "progress") {
        if (A.current >= 0) {
            Step& s = A.steps[A.current];
            s.done = ev.num("done");
            s.total = ev.num("total");
            s.detail = ev.str("detail");
        }
    } else if (e == "reply") {
        handle_reply(ev);
    } else if (e == "fatal") {
        fail(ev.str("message"), A.log_path.empty() ? "Fix what the message says and open Wind Waker HD again."
                                                   : "Fix what the message says and open Wind Waker HD again. The setup log is " +
                                                         A.log_path + ".");
    }
}

static std::string format_size(double b) {
    char t[32];
    if (b >= 1e9) snprintf(t, sizeof t, "%.1f GB", b / 1e9);
    else snprintf(t, sizeof t, "%.0f MB", b / 1e6);
    return t;
}

static void run_queue() {
    if (busy()) return;
    if (!A.queue.empty()) {
        auto r = A.queue.front();
        A.queue.pop_front();
        request(r.first, r.second);
        return;
    }
    if (A.after.empty()) return;
    std::string a = A.after;
    A.after.clear();
    if (a == "play" && (!A.portable || g_exe.empty())) {
        request("launch", "");  // setup.py starts the game (non-portable installs)
        A.after = "quit";
    } else if (a == "play") A.exec_game = true, A.exit_code = 0;
    else if (a == "quit") A.exit_code = 0;
}

static void pump_child() {
    run_queue();
    A.child.poll();
    while (!A.child.lines.empty()) {
        std::string l = A.child.lines.front();
        A.child.lines.pop_front();
        J ev;
        if (!l.empty() && l[0] == '{' && parse_json(l, ev)) handle_event(ev);
        else if (!l.empty()) addlog(l);  // the launcher's own output (e.g. fetching Python)
    }
    if (A.child.exited && A.screen != Screen::Fatal && A.exit_code < 0 && A.child.proc) {
        std::string code = "exit code " + std::to_string(A.child.exit_code);
        if (A.hello)
            fail("The setup process (tools/installer/setup.py) ended unexpectedly (" + code + ").",
                 "Open Wind Waker HD again; finished steps are kept. The last lines it wrote are below" +
                     (A.log_path.empty() ? std::string(".") : ", its full log is " + A.log_path + ".") +
                     " If it happens again, click \"Copy log\" and attach the log to a bug report.");
        else
            fail("The setup process ended before it was ready (" + code + "; its output is below).",
                 "Its output usually says what is missing. You can also run the setup in a terminal: " SETUP_IN_TERMINAL
                 " in the release folder. If that fails too, click \"Copy log\" and attach the log to a bug report.");
    }
}

// ---------------------------------------------------------------------------------------------
// screens

static bool details_open = false;  // automation: show the log pane in screenshots

static bool details_header() {
    if (details_open) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    return ImGui::CollapsingHeader("Details");
}

// height left above the footer buttons (the footer is drawn at the bottom of the window)
static float space_above_footer() {
    return ImGui::GetContentRegionAvail().y - (ImGui::GetFrameHeight() + 8) - ImGui::GetStyle().ItemSpacing.y * 3;
}

static void page_header(const char* title, const std::string& sub = "") {
    heading(title);
    if (!sub.empty()) muted(sub);
    ImGui::Spacing();
}

static void log_pane(float height) {
    ImGui::BeginChild("log", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.28f, 0.32f, 1));
    for (auto& l : A.log) ImGui::TextUnformatted(l.c_str());
    ImGui::PopStyleColor();
    if (A.log_scroll) ImGui::SetScrollHereY(1.0f), A.log_scroll = false;
    ImGui::EndChild();
}

static void screen_starting() {
    page_header("Wind Waker HD");
    ImGui::Spacing();
    mark(1);
    ImGui::TextUnformatted("Preparing the installer...");
    if (!A.log.empty()) log_pane(260);
}

static bool clt_ok() { return run_quick({"/usr/bin/xcode-select", "-p"}) && run_quick({"/usr/bin/xcrun", "--find", "clang"}); }

static void start_child();

static void screen_clt() {
    page_header("Apple's Command Line Tools are needed");
    ImGui::TextWrapped("Setup compiles the game on your Mac with Apple's free Command Line Tools (compiler and "
                       "Python). They are not installed yet.");
    ImGui::Spacing();
    if (!A.clt_install_started) {
        ImGui::TextWrapped("Click the button: Apple's installer opens. Click \"Install\" there and accept the license; it "
                           "takes a few minutes. Setup continues by itself afterwards.");
    } else {
        mark(1);
        ImGui::TextUnformatted("Waiting for the Command Line Tools installation to finish...");
    }
    if (ImGui::GetTime() > A.clt_next_check) {
        A.clt_next_check = ImGui::GetTime() + 3.0;
        if (clt_ok()) start_child();
    }
    int b = footer({"Quit", A.clt_install_started ? "Open Apple's installer again" : "Install Command Line Tools"}, 1);
    if (b == 0) A.exit_code = 0;
    if (b == 1) {
        run_quick({"/usr/bin/xcode-select", "--install"});
        A.clt_install_started = true;
    }
}

static std::string home_folder() { return A.portable && !A.package.empty() ? A.package : A.data_dir; }

static void screen_welcome() {
    page_header("Welcome", "The Legend of Zelda: The Wind Waker HD, native PC port " + A.version);
    if (A.portable) {
        ImGui::TextWrapped("The first start prepares the game once: it reads your own dump of the game and builds it for "
                           "this computer (about two minutes). Releases never contain game code, so this happens here, "
                           "once. After that, starting Wind Waker HD starts the game directly.");
    } else {
        ImGui::TextWrapped("This installer builds the game on your computer from your own dump of the game. The "
                           "download contains no game files, no game code and no keys.");
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("You need:");
    ImGui::Bullet();
    ImGui::TextWrapped("your Wind Waker HD (USA): a disc image (.wud/.wux), a Cemu archive (.wua), or an extracted "
                       "folder (code, content, meta);");
    ImGui::Bullet();
    ImGui::TextWrapped("for a disc image, its disc key and the Wii U common key (from your console). A Cemu archive "
                       "or an extracted folder needs no keys.");
    ImGui::Spacing();
    if (A.portable) {
        ImGui::TextUnformatted("Space, all of it in this folder:");
        ImGui::Bullet();
        ImGui::TextWrapped("an extracted game folder is used where it is: nothing is copied;");
        ImGui::Bullet();
        ImGui::TextWrapped("a disc image or Cemu archive is extracted into the folder: about 1.7 GB;");
        ImGui::Bullet();
        ImGui::TextWrapped("while preparing, about 1 GB more (removed afterwards)%s.",
#if defined(_WIN32) || (!defined(__APPLE__))
                           ", plus the compiler download, which you can remove at the end"
#else
                           ""
#endif
        );
        ImGui::Spacing();
        muted("Everything stays in " + home_folder() + " (free: " + format_size(A.free_bytes) +
              "). Nothing is written to your user folders unless you ask for a shortcut.");
    } else {
        muted("The game will be installed in " + A.data_dir + ". Your saves and settings there are never changed "
              "without asking.");
    }
    int b = footer({"Quit", "Continue"}, 1);
    if (b == 0) A.exit_code = 0;
    if (b == 1) go(Screen::Source);
}

static void screen_menu() {
    bool update = A.installed_version != A.version;
    page_header(A.portable ? "Wind Waker HD" : "Wind Waker HD is installed",
                update ? "Prepared with " + A.installed_version + ". This release: " + A.version + "."
                       : "Version " + A.version + ", in " + home_folder());
    float w = 520;
    if (update) {
        if (button("Update to this release", ImVec2(w, 0), true)) start_install("installed");
        muted("Rebuilds the game with this release. Your game files and saves are kept; no keys needed.");
    }
    if (button("Play", ImVec2(w, 0), !update)) A.after = "play";
    if (button("Repair", ImVec2(w, 0))) start_install("installed");
    muted("Rebuilds the game code from the installed game files.");
    if (button("Change the game (disc image, Cemu archive or game folder)", ImVec2(w, 0))) go(Screen::Source);
    if (button("Import saves or settings...", ImVec2(w, 0))) go(Screen::Save);
    if (button("Open the folder", ImVec2(w, 0))) open_folder(home_folder());
    if (footer({"Quit"}) == 0) A.exit_code = 0;
}

static void screen_source() {
    page_header("Choose your game",
                "A Wii U disc image (.wux or .wud), a Cemu archive (.wua) or an already extracted game folder.");
    static const SDL_DialogFileFilter filters[] = {{"Wii U disc image or Cemu archive (.wux, .wud, .wua)", "wux;wud;wua"},
                                                   {"Wii U disc image (.wux, .wud)", "wux;wud"},
                                                   {"Cemu Wii U archive (.wua)", "wua"},
                                                   {"All files", "*"}};
    if (button("Choose disc image or archive...", ImVec2(320, 0), A.source_path.empty()))
        choose_file("source", filters, 4);
    ImGui::SameLine();
    if (button("Choose extracted game folder...", ImVec2(320, 0))) choose_folder("source");
    ImGui::Spacing();
    if (!A.source_path.empty()) {
        ImGui::BeginChild("src", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        if (A.pending_cmd == "probe") mark(1);
        else mark(A.probe_ok ? 2 : 3);
        ImGui::TextWrapped("%s", A.probe_msg.c_str());
        muted(A.source_path);
        ImGui::EndChild();
    }
    int b = footer({"Back", "Next"}, 1, (A.probe_ok && !busy()) ? 0 : 2);
    if (b == 0) go(A.installed ? Screen::Menu : Screen::Welcome);
    if (b == 1) {
        if (A.source_kind == "folder" || A.source_kind == "archive") start_install(A.source_kind);  // no keys
        else go(Screen::Keys);
    }
}

static std::string base_name(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

static void screen_keys() {
    page_header("Keys", base_name(A.source_path));
    static const SDL_DialogFileFilter keyf[] = {{"Key files (.key, .bin, .txt)", "key;bin;txt"}, {"All files", "*"}};

    ImGui::BeginChild("disc", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    heading("Disc key", 20);
    if (!A.disc_key_file.empty()) {
        mark(2);
        ImGui::TextWrapped("Key file: %s", base_name(A.disc_key_file).c_str());
    } else if (!A.disc_key_found.empty()) {
        mark(2);
        ImGui::TextWrapped("Found next to the image: %s", base_name(A.disc_key_found).c_str());
    } else {
        mark(3);
        ImGui::TextWrapped("Not found next to the image. Choose the .key file that was dumped with this disc.");
    }
    if (button(A.disc_key_found.empty() && A.disc_key_file.empty() ? "Choose key file...##disc" : "Choose a different key file...##disc"))
        choose_file("disc_key", keyf, 2);
    ImGui::EndChild();
    ImGui::Spacing();

    ImGui::BeginChild("common", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    heading("Wii U common key", 20);
    muted("16 bytes, the same on every Wii U console, dumped from your own console.");
    if (!A.common_key_found.empty() && A.common_key_file.empty() && A.common_mode == 0) {
        mark(2);
        ImGui::TextWrapped("Found: %s", A.common_key_found.c_str());
    }
    int mode = A.common_mode;
    radio("Key file", &mode, 0);
    ImGui::SameLine(0, 30);
    radio("Paste the key", &mode, 1);
    set_common_mode(mode);
    if (A.common_mode == 0) {
        if (button("Choose key file...##common")) choose_file("common_key", keyf, 2);
        if (!A.common_key_file.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(base_name(A.common_key_file).c_str());
        }
    } else {
        ImGui::SetNextItemWidth(460);
        if (ImGui::InputTextWithHint("##paste", "32 hex digits", A.paste, sizeof A.paste,
                                     ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsNoBlank))
            keys_changed();
        muted("The key is hidden, used once to check and decrypt, and never saved.");
    }
    ImGui::EndChild();

    if (!A.keys_msg.empty()) {
        ImGui::Spacing();
        if (A.pending_cmd == "check_keys") {
            mark(1);
            ImGui::TextUnformatted(A.keys_msg.c_str());
        } else {
            colored(BAD, A.keys_msg);
        }
    }
    bool have_disc = !A.disc_key_found.empty() || !A.disc_key_file.empty();
    bool have_common = A.common_mode == 1 ? strlen(A.paste) > 0 : (!A.common_key_file.empty() || !A.common_key_found.empty());
    int b = footer({"Back", "Check keys and install"}, 1, (have_disc && have_common && !busy()) ? 0 : 2);
    if (b == 0) go(Screen::Source);
    if (b == 1) check_keys();
}

static double step_weight(const std::string& id) {
    static const std::map<std::string, double> w = {{"keys", 1},      {"archive", 1},   {"folder", 1},
                                                    {"compiler", 3},  {"extract", 15},  {"copy", 8},
                                                    {"translate", 8}, {"compile", 60},  {"app", 4}};
    auto it = w.find(id);
    return it == w.end() ? 1 : it->second;
}

static void screen_installing() {
    if (A.portable) page_header("Preparing the game", "One time, about two minutes. You can keep using your computer.");
    else page_header("Installing", "This takes a few minutes. You can keep using your computer.");
    double tw = 0, dw = 0;
    for (auto& s : A.steps) {
        double w = step_weight(s.id);
        tw += w;
        if (s.state == 2) dw += w;
        else if (s.state == 1 && s.total > 0) dw += w * std::min(1.0, s.done / s.total);
    }
    ImGui::TextUnformatted("Overall");
    ImGui::ProgressBar(tw > 0 ? (float)(dw / tw) : 0.0f, ImVec2(-1, 0));
    ImGui::Spacing();
    ImGui::BeginChild("steps", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    for (auto& s : A.steps) {
        mark(s.state);
        std::string t = s.title;
        if (size_t paren = t.find(" ("); paren != std::string::npos) t = t.substr(0, paren);
        if (s.state == 0) ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
        ImGui::TextUnformatted(t.c_str());
        if (s.state == 0) ImGui::PopStyleColor();
        if (s.state == 1) {
            ImGui::SameLine(380);
            float frac = s.total > 0 ? (float)std::min(1.0, s.done / s.total) : -1.0f * (float)ImGui::GetTime();
            ImGui::ProgressBar(frac, ImVec2(-1, 0), s.detail.empty() ? (s.total > 0 ? nullptr : "") : s.detail.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    if (details_header()) log_pane(std::max(60.0f, space_above_footer()));
}

static void screen_error() {
    ImGui::PushStyleColor(ImGuiCol_Text, BAD);
    heading("Setup stopped");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    // the first line says what failed; the rest (tool output) goes into a small scrolling box
    std::string head = A.error_msg, rest;
    if (size_t nl = head.find('\n'); nl != std::string::npos) rest = head.substr(nl + 1), head = head.substr(0, nl);
    if (!head.empty()) head[0] = (char)toupper((unsigned char)head[0]);
    ImGui::TextWrapped("%s", head.c_str());
    if (!rest.empty()) {
        float h = std::min(ImGui::GetTextLineHeightWithSpacing() * 8, std::max(60.0f, space_above_footer() * 0.5f));
        ImGui::BeginChild("errdetail", ImVec2(0, h), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.28f, 0.32f, 1));
        ImGui::TextUnformatted(rest.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }
    ImGui::Spacing();
    muted("Nothing that was finished is lost. \"Copy log\" puts the details on the clipboard for a bug report; the log "
          "contains no keys.");
    if (details_header()) log_pane(std::max(60.0f, space_above_footer()));
    int b = footer({"Quit", "Copy log", "Retry"}, 2);
    if (b == 0) A.exit_code = 1;
    if (b == 1) copy_log();
    if (b == 2) {
        A.keys_msg.clear();
        if (A.error_back == "menu") go(Screen::Menu);
        else {
            if (!A.source_path.empty()) set_source(A.source_path);
            go(Screen::Source);
        }
    }
}

static void send_import(bool replace) {
    std::string r = replace ? ",\"replace\":true" : "";
    if (A.save_kind == 3) request("import_existing", r.empty() ? "" : r.substr(1));
    else if (A.save_kind == 4) request("import_existing", "\"path\":" + jstr(A.save_path) + r);
    else request("import_save", std::string("\"kind\":") + (A.save_kind == 2 ? "\"gc\"" : "\"hd\"") + ",\"path\":" +
                                    jstr(A.save_path) + r);
}

static void screen_save() {
    page_header("Your save (optional)", "Use a save you already have, or start a new game.");
    static const SDL_DialogFileFilter gci[] = {{"GameCube save (.gci)", "gci"}, {"All files", "*"}};
    if (A.save_exists) colored(MUTED, "A save is already installed. Importing replaces it; the current save is backed up first.");
    int kind = A.save_kind;
    radio("Start with a new save", &kind, 0);
    radio("Wind Waker HD save (a folder with cking.sav, from Cemu or a Wii U)", &kind, 1);
    if (A.save_kind == 1) {
        ImGui::Indent();
        if (button("Choose folder...##hd")) choose_folder("save");
        if (!A.save_path.empty()) ImGui::SameLine(), ImGui::TextUnformatted(base_name(A.save_path).c_str());
        ImGui::Unindent();
    }
    radio("GameCube Wind Waker save (.gci), converted to HD", &kind, 2);
    if (A.legacy) radio("Copy saves and settings from my earlier installation (copied, not moved)", &kind, 3);
    radio("Copy saves and settings from another Wind Waker HD folder", &kind, 4);
    set_save_kind(kind);
    if (A.save_kind == 2) {
        ImGui::Indent();
        if (button("Choose .gci file...##gc")) choose_file("save", gci, 2);
        if (!A.save_path.empty()) ImGui::SameLine(), ImGui::TextUnformatted(base_name(A.save_path).c_str());
        muted("Items, progress, songs and charts are carried over (tools/savegame/gc2hd.py).");
        ImGui::Unindent();
    }
    if (A.save_kind == 4) {
        ImGui::Indent();
        if (button("Choose folder...##other")) choose_folder("save");
        if (!A.save_path.empty()) ImGui::SameLine(), ImGui::TextUnformatted(base_name(A.save_path).c_str());
        muted("The folder of an earlier Wind Waker HD release (it has a data folder).");
        ImGui::Unindent();
    }
    if (!A.save_msg.empty()) colored(BAD, A.save_msg);
    if (A.confirm_replace) {
        ImGui::OpenPopup("Replace save?");
        A.confirm_replace = false;
    }
    if (ImGui::BeginPopupModal("Replace save?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A save is already installed.");
        ImGui::TextUnformatted("Replace it? The current save is moved to a backup folder first.");
        if (button("Replace", ImVec2(160, 0), true)) {
            send_import(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (button("Keep my save", ImVec2(160, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    bool can = A.save_kind == 0 || A.save_kind == 3 || !A.save_path.empty();
    int b = footer({A.save_kind == 0 ? "Continue" : "Import"}, 0, (can && !busy()) ? 0 : 1);
    if (b == 0) {
        if (A.save_kind == 0) go(Screen::Done);
        else send_import(false);
    }
}

static void screen_done() {
    page_header("Ready to play");
    mark(2);
    ImGui::TextWrapped(A.portable ? "The game is prepared." : "The Wind Waker HD is installed.");
    ImGui::Spacing();
    if (A.portable) {
        muted("Everything is in " + home_folder() + ".");
        muted("To play, start Wind Waker HD again: it starts the game directly. To repair, update or change the "
#ifdef __linux__
              "game, start it with --setup (or use the shortcut's Setup action)."
#else
              "game, hold Shift while starting it."
#endif
        );
    } else {
#ifdef __APPLE__
        if (!A.result_app.empty()) muted("The game: " + A.result_app + " (also in Launchpad and Spotlight).");
#elif defined(_WIN32)
        muted("The game: Start menu and desktop shortcut \"Wind Waker HD\".");
#else
        muted("The game: your applications menu, or " + A.data_dir + "/play.sh.");
#endif
        muted("Game files and saves: " + A.data_dir);
    }
    if (!A.save_msg.empty()) muted(A.save_msg);
    if (A.portable) {
        ImGui::Spacing();
        if (A.toolchain_bytes > 0) {
            checkbox(("Remove the downloaded compiler (" + format_size(A.toolchain_bytes) + ")").c_str(),
                            &A.opt_remove_toolchain);
            muted("Keep it for guest mod builds and repairs. Run setup again to restore it if removed.");
        }
        checkbox(
#if defined(__APPLE__)
            "Add Wind Waker HD to my Applications folder (a link)",
#elif defined(_WIN32)
            "Add Wind Waker HD to the Start menu",
#else
            "Add Wind Waker HD to my applications menu",
#endif
            &A.opt_shortcut);
        muted("Off by default: then nothing is written outside this folder.");
    }
    int b = footer({"Quit", "Open folder", "Play"}, 2, busy() || !A.after.empty() ? 7 : 0);
    auto queue_options = [] {
        if (A.portable && A.toolchain_bytes > 0 && A.opt_remove_toolchain) A.queue.push_back({"remove_toolchain", ""});
        if (A.portable && A.opt_shortcut) A.queue.push_back({"shortcut", ""});
        A.toolchain_bytes = 0, A.opt_shortcut = false;  // once
    };
    if (b == 0) queue_options(), A.after = "quit";
    if (b == 1) open_folder(home_folder());
    if (b == 2) queue_options(), A.after = "play";
}

static void screen_fatal() {
    ImGui::PushStyleColor(ImGuiCol_Text, BAD);
    heading("Setup could not continue");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", A.fatal.c_str());
    if (!A.fatal_hint.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("What to do:");
        ImGui::TextWrapped("%s", A.fatal_hint.c_str());
    }
    if (!A.fatal_log.empty()) {
        ImGui::Spacing();
        muted("This was written to " + A.fatal_log);
    }
    if (!A.log.empty()) log_pane(std::max(60.0f, space_above_footer()));
#ifdef __APPLE__
    const char* show = "Show in Finder";
#else
    const char* show = "Open the folder";
#endif
    int b = A.fatal_folder.empty() ? footer({"Quit", "Copy log"}) : footer({"Quit", "Copy log", show});
    if (b == 0) A.exit_code = 1;
    if (b == 1) copy_log();
    if (b == 2) open_folder(A.fatal_folder);
}

// ---------------------------------------------------------------------------------------------
// startup

// Where the release folder is, and why it was not found (for the message when it isn't).
struct PackageSearch {
    std::string pkg;           // the release folder (ends in a separator), empty when not found
    std::string start;          // the folder the program is in (macOS: the folder containing the original app)
    std::string checked;        // start + tools/installer/setup.py, where it belongs
    std::string error;          // why it is not there (strerror)
    int err = 0;                // errno of that check (0 on Windows)
    bool translocated = false;  // macOS started a temporary read-only copy of the app
    bool original_known = true; // ... and said where the original is
};

#ifdef __APPLE__
// A downloaded (quarantined) app opened from Finder runs from a random read-only copy ("App Translocation",
// /private/var/folders/.../AppTranslocation/<id>/d/Wind Waker HD.app) that contains only the app, not
// the release folder around it. Security.framework says where the original is.
//
// Ask about the bundle itself: SecTranslocateCreateOriginalPathForURL fails for paths that do not exist,
// and SDL_GetBasePath() is the bundle's Contents/Resources, which the release app does not have (issue #48:
// v0.2.3 asked about Contents/Resources, got nothing back and stopped with "must stay in the unpacked
// release folder").
static bool untranslocate_bundle(const std::string& bundle, std::string& original, bool& known) {
    original = bundle;
    known = true;
    SDL_SharedObject* sec = SDL_LoadObject("/System/Library/Frameworks/Security.framework/Security");
    SDL_SharedObject* cf = SDL_LoadObject("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation");
    using CreateURL = const void* (*)(const void*, const unsigned char*, long, unsigned char);
    using IsTranslocated = unsigned char (*)(const void*, bool*, void**);
    using Original = const void* (*)(const void*, void**);
    using GetFS = unsigned char (*)(const void*, unsigned char, unsigned char*, long);
    using Release = void (*)(const void*);
    auto create = cf ? (CreateURL)SDL_LoadFunction(cf, "CFURLCreateFromFileSystemRepresentation") : nullptr;
    auto getfs = cf ? (GetFS)SDL_LoadFunction(cf, "CFURLGetFileSystemRepresentation") : nullptr;
    auto release = cf ? (Release)SDL_LoadFunction(cf, "CFRelease") : nullptr;
    auto is_tl = sec ? (IsTranslocated)SDL_LoadFunction(sec, "SecTranslocateIsTranslocatedURL") : nullptr;
    auto orig_fn = sec ? (Original)SDL_LoadFunction(sec, "SecTranslocateCreateOriginalPathForURL") : nullptr;
    // without the functions (a future macOS) the path still tells
    bool translocated = bundle.find("/AppTranslocation/") != std::string::npos;
    if (create && getfs && release) {
        const void* url = create(nullptr, (const unsigned char*)bundle.c_str(), (long)bundle.size(), 1);
        if (url) {
            bool t = false;
            if (is_tl && is_tl(url, &t, nullptr)) translocated = t;
            if (translocated) {
                known = false;
                const void* o = orig_fn ? orig_fn(url, nullptr) : nullptr;
                unsigned char buf[4096];
                if (o && getfs(o, 1, buf, sizeof buf)) original = (const char*)buf, known = true;
                if (o) release(o);
            }
            release(url);
        }
    } else if (translocated) {
        known = false;
    }
    if (translocated) SDL_Log("App Translocation: started from %s, original %s", bundle.c_str(),
                              known ? original.c_str() : "unknown");
    return translocated;
}
#endif

static PackageSearch find_package() {
    PackageSearch r;
    if (const char* e = SDL_getenv("WWHD_SETUP_PKG")) return r.pkg = e, r;
    std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    r.start = base;
#ifdef __APPLE__
    // base is <bundle>/Contents/Resources/ (SDL), or the executable's folder outside a bundle
    size_t app = base.rfind(".app/Contents/");
    if (app != std::string::npos) {
        std::string bundle = base.substr(0, app + 4), original;
        r.translocated = untranslocate_bundle(bundle, original, r.original_known);
        base = original + base.substr(bundle.size());
        r.start = original.substr(0, original.find_last_of('/') + 1);  // the folder containing the app
    }
#endif
    r.checked = r.start + "tools/installer/setup.py";
    for (int up = 0; up < 5; up++) {
        std::string candidate = base + "tools/installer/setup.py";
        SDL_PathInfo info;
        if (SDL_GetPathInfo(candidate.c_str(), &info)) return r.pkg = base, r;
        if (base == r.start) {  // why it is not next to the app (the place it belongs)
#ifndef _WIN32
            struct stat sb;
            r.err = stat(candidate.c_str(), &sb) == 0 ? 0 : errno;
            r.error = r.err ? strerror(r.err) : "unreadable";
#else
            r.error = SDL_GetError();
#endif
            if (r.err == EPERM || r.err == EACCES) break;  // not allowed to look: that is the answer
        }
        // go one directory up
        if (base.size() > 1) base.pop_back();
        size_t s = base.find_last_of("/\\");
        if (s == std::string::npos) break;
        base = base.substr(0, s + 1);
    }
    return r;
}

// "Setup could not continue" when the release folder was not found: what happened, what to do
static void fail_no_package(const PackageSearch& r) {
    std::string folder = r.start;
    if (folder.size() > 1 && (folder.back() == '/' || folder.back() == '\\')) folder.pop_back();
#ifdef __APPLE__
    if (r.translocated && !r.original_known) {
        fail("macOS started Wind Waker HD from a temporary read-only copy (App Translocation, because the downloaded "
             "folder is still marked as quarantined), and did not say where the original is, so the release folder "
             "around the app cannot be found.",
             "In Finder, drag \"Wind Waker HD.app\" out of the unzipped folder (for example onto the Desktop) and back "
             "into the same folder, then open it again: an app moved with Finder is started where it is. Or, in "
             "Terminal: xattr -dr com.apple.quarantine followed by the path of the unzipped folder. Keep the app in "
             "that folder: it needs tools/, sdk/ and portable.txt next to it.");
        return;
    }
    if (r.err == EPERM || r.err == EACCES) {
        fail("macOS did not let Wind Waker HD read its release folder " + folder + " (checking " + r.checked + ": " +
                 r.error + ").",
             "If you answered \"Don't Allow\" when macOS asked about access to a folder (Downloads, Desktop, "
             "Documents, an external drive), allow Wind Waker HD in System Settings > Privacy & Security > Files and "
             "Folders, or move the whole unzipped folder to another place (for example your home folder or a Games "
             "folder) and open the app from there.",
             folder);
        return;
    }
    fail("Wind Waker HD could not find its release files: " + r.checked + " is missing (" + r.error + ").",
         "Keep \"Wind Waker HD.app\" inside the unzipped release folder, next to tools/, sdk/ and portable.txt. If you "
         "moved only the app (for example into Applications), move it back; to keep the game somewhere else, move "
         "the whole folder.",
         folder);
#else
    fail("Wind Waker HD could not find its release files: " + r.checked + " is missing or unreadable (" + r.error + ").",
         "Keep this program inside the unzipped release folder, next to tools/, sdk/ and portable.txt. To keep the "
         "game somewhere else, move the whole folder; if files are missing, unzip the release again.",
         folder);
#endif
}

// The game, saves and settings go into the data folder (in the release folder unless --data-dir):
// check it can be written before anything starts, so a read-only place (a disk image, a read-only drive or
// share, a folder of another user) is reported as such and not as a failure halfway through the setup.
static std::string data_dir_of(const std::string& pkg, const std::vector<std::string>& passthru) {
    std::string data = pkg + "data";
    for (size_t i = 0; i + 1 < passthru.size(); i++)
        if (passthru[i] == "--data-dir") data = passthru[i + 1];
    return data;
}

static bool check_writable(const std::string& data) {
    std::string probe = data + "/.write-test";
    std::string what;
    if (!SDL_CreateDirectory(data.c_str())) what = "creating the folder " + data;
    else {
        SDL_IOStream* f = SDL_IOFromFile(probe.c_str(), "wb");
        if (!f) what = "creating the file " + probe;
        else {
            bool ok = SDL_WriteIO(f, "ok\n", 3) == 3;
            ok = SDL_CloseIO(f) && ok;
            if (!ok) what = "writing the file " + probe;
            SDL_RemovePath(probe.c_str());
        }
    }
    if (what.empty()) return true;
    std::string err = SDL_GetError();
    fail("The release folder cannot be written to: " + what + " failed (" + err + "). Setup keeps the game, saves "
         "and settings in " + data + ".",
         "Copy the whole unzipped folder to a place you can write to (for example your home folder or a Games "
         "folder; not a disk image, a read-only drive or another user's folder) and open Wind Waker HD from there.",
         A.pkg);
    return false;
}

// the failure goes to a log file too: data/setup-window.log in the release folder, or (no release folder or
// not writable) ~/Library/Logs (macOS), %TEMP% (Windows), $TMPDIR or /tmp
static std::string fatal_log_file() {
    if (!A.pkg.empty()) {
        std::string data = data_dir_of(A.pkg, A.passthru);
        if (SDL_CreateDirectory(data.c_str())) {
            std::string p = data + "/setup-window.log";
            if (SDL_IOStream* f = SDL_IOFromFile(p.c_str(), "ab")) return SDL_CloseIO(f), p;
        }
    }
#if defined(__APPLE__)
    const char* home = SDL_getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/Library/Logs";
    if (!home || !SDL_CreateDirectory(dir.c_str())) dir = "/tmp";
    return dir + "/Wind Waker HD setup.log";
#elif defined(_WIN32)
    const char* t = SDL_getenv("TEMP");
    return std::string(t ? t : ".") + "\\Wind Waker HD setup.log";
#else
    const char* t = SDL_getenv("TMPDIR");
    return std::string(t && *t ? t : "/tmp") + "/wind-waker-hd-setup.log";
#endif
}

static void fail(const std::string& what, const std::string& todo, const std::string& folder) {
    A.fatal = what;
    A.fatal_hint = todo;
    A.fatal_folder = folder;
    go(Screen::Fatal);
    SDL_Log("Setup could not continue: %s", what.c_str());
    SDL_Log("What to do: %s", todo.c_str());
    std::string path = fatal_log_file();
    if (SDL_IOStream* f = SDL_IOFromFile(path.c_str(), "ab")) {
        char when[64] = "";
        SDL_Time t;
        SDL_DateTime dt;
        if (SDL_GetCurrentTime(&t) && SDL_TimeToDateTime(t, &dt, true))
            snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                     dt.second);
        std::string text = std::string("==== ") + when + " Wind Waker HD" + (A.version.empty() ? "" : " " + A.version) +
                           ": setup could not continue\n" +
                           what + "\nWhat to do: " + todo + "\n";
        const char* bp = SDL_GetBasePath();
        text += std::string("Program: ") + (bp ? bp : "?") + "\nRelease folder: " + (A.pkg.empty() ? "(not found)" : A.pkg) + "\n";
        if (!A.log_path.empty()) text += "Setup log: " + A.log_path + "\n";
        for (auto& l : A.log) text += "  " + l + "\n";
        SDL_WriteIO(f, text.data(), text.size());
        SDL_CloseIO(f);
        A.fatal_log = path;
    } else {
        A.fatal_log.clear();
    }
}

// the release's version, as package.py wrote it (setup.py reports the same file in its hello)
static std::string package_version() {
    J m;
    if (!parse_json(read_file(A.pkg + "sdk/manifest.json"), m)) return "";
    return m.str("version");
}

static void start_child() {
    std::vector<std::string> args;
#if defined(__APPLE__)
    args = {"/bin/bash", A.pkg + "tools/Setup in Terminal.command"};
#elif defined(_WIN32)
    // the official embeddable Python shipped in the release (tools\python; no download, no script host)
    if (!have_bundled_python(A.pkg)) {
        fail("Setup could not start: " + bundled_python(A.pkg) + " is missing.",
             "The release is incomplete: unzip it again (the whole zip, keeping its folders) and start Wind Waker "
             "HD.exe from the unzipped folder.",
             A.pkg);
        return;
    }
    args = {bundled_python(A.pkg), A.pkg + "tools\\installer\\setup.py"};
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PYTHONDONTWRITEBYTECODE", "1", true);
#else
    args = {"/bin/sh", A.pkg + "tools/setup-in-terminal.sh"};
#endif
    args.push_back("--gui-protocol");
    for (auto& a : A.passthru) args.push_back(a);
    go(Screen::Starting);
    if (!A.child.start(args, A.pkg)) {
        std::string err = SDL_GetError();
        std::string cmd;
        for (auto& a : args) cmd += (cmd.empty() ? "" : " ") + a;
        fail("Could not start the setup process (" + cmd + "): " + err + ".",
             "Check that the release folder is complete (unzip it again if files are missing). You can also run the "
             "setup in a terminal: " SETUP_IN_TERMINAL " in the release folder.",
             A.pkg);
    }
}

// ---------------------------------------------------------------------------------------------
// automation (tests): a JSON array of steps, each run when its screen is showing:
//   {"screen": "keys", "set": {"common_mode": "file", "common_key_file": "..."}, "when_step": "compile",
//    "idle": true, "shot": "03-keys.png", "click": "Check keys and install"}
// "set" fields: source (as if chosen in the dialog), disc_key_file, common_key_file, common_mode
// (file|paste), save_kind (none|hd|gc|legacy|other), save_path, open_details (true). Screenshots are PNG files
// of the window. The run ends (exit 0) after the last step, or with exit 2 on a 45-minute timeout.


static void automation_frame() {
    if (A.script.t != J::Arr) return;
    if (A.script_pos >= A.script.a.size()) {
        if (A.shot_pending.empty()) A.exit_code = 0;
        return;
    }
    const J& st = A.script.a[A.script_pos];
    double now = SDL_GetTicks() / 1000.0;
    if (A.step_started == 0) A.step_started = now;
    if (now - A.step_started > 45 * 60) {
        SDL_Log("automation: timeout waiting for screen %s (at %s)", st.str("screen").c_str(), screen_name(A.screen));
        A.exit_code = 2;
        return;
    }
    if (st.str("screen") != screen_name(A.screen)) {
        A.stable_frames = A.ready_frames = 0;
        A.shot_taken = false;
        return;
    }
    if (A.stable_frames == 0 && st.get("set")) {
        const J& s = *st.get("set");
        for (auto& kv : s.o) {
            const std::string& k = kv.first;
            const std::string v = kv.second.t == J::Str ? kv.second.s : "";
            if (k == "source") set_source(v);
            else if (k == "disc_key_file") set_disc_key_file(v);
            else if (k == "common_key_file") set_common_key_file(v);
            else if (k == "common_mode") set_common_mode(v == "paste" ? 1 : 0);
            else if (k == "save_kind")
                set_save_kind(v == "hd" ? 1 : v == "gc" ? 2 : v == "legacy" ? 3 : v == "other" ? 4 : 0);
            else if (k == "save_path") set_save_path(v);
            else if (k == "open_details") details_open = true;
        }
    }
    A.stable_frames++;  // frames on this screen (the "set" above runs once, at 0)
    if (st.boolean("idle") && busy()) return;
    if (!st.str("when_step").empty()) {
        int i = step_index(st.str("when_step"));
        if (i < 0 || A.steps[i].state != 1 || A.steps[i].done <= 0) return;
    }
    if (++A.ready_frames < 20) return;  // conditions met: let the layout settle
    if (!st.str("shot").empty() && !A.shot_taken) {
        A.shot_pending = st.str("shot");
        A.shot_taken = true;
        return;
    }
    if (!A.shot_pending.empty()) return;
    A.ready_frames = 0;
    A.shot_taken = false;
    if (!st.str("click").empty()) A.autoclick = st.str("click");
    A.script_pos++;
    A.stable_frames = 0;
    A.step_started = now;
}

static void save_shot(SDL_Renderer* r, const std::string& name) {
    SDL_Surface* s = SDL_RenderReadPixels(r, nullptr);
    if (!s) return;
    std::string path = A.shots_dir + "/" + name;
    if (!SDL_SavePNG(s, path.c_str())) SDL_Log("screenshot %s: %s", path.c_str(), SDL_GetError());
    SDL_DestroySurface(s);
}

// The game in this folder is built for this release and its game files are there: start it directly.
static bool game_ready(const std::string& pkg, const std::vector<std::string>& passthru) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo((pkg + "portable.txt").c_str(), &info)) return false;  // only portable releases
    std::string data = pkg + "data";
    for (size_t i = 0; i + 1 < passthru.size(); i++)
        if (passthru[i] == "--data-dir") data = passthru[i + 1];
    J st, man;
    if (!parse_json(read_file(data + "/install.json"), st) || !parse_json(read_file(pkg + "sdk/manifest.json"), man))
        return false;
    if (st.str("version") != man.str("version") || st.boolean("placeholder_code")) return false;
    auto resolve = [&](std::string p) {  // install.json keeps paths inside data/ relative to it
        bool abs = !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
        return p.empty() || abs ? p : data + "/" + p;
    };
    std::string exe = resolve(st.str("exe")), game = resolve(st.str("game_dir"));
    if (exe.empty() || game.empty() || !SDL_GetPathInfo(exe.c_str(), &info) ||
        !SDL_GetPathInfo((game + "/code/cking.rpx").c_str(), &info))
        return false;
    g_exe = exe, g_game_dir = game, g_data_dir = data;
    return true;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 1 && !strcmp(argv[1], "--console-setup")) {
        return console_setup(find_package().pkg, std::vector<std::string>(argv + 2, argv + argc));
    }
#endif
    std::string automate;
    bool want_setup = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--automate" && i + 1 < argc) automate = argv[++i];
        else if (a == "--screenshots" && i + 1 < argc) A.shots_dir = argv[++i];
        else if (a == "--self-test") A.self_test = true;
        else if (a == "--setup") want_setup = true;
        else if (a.rfind("-psn_", 0) == 0) continue;  // macOS Finder's process serial number
        else A.passthru.push_back(a);
    }
    if (!automate.empty()) {
        std::string text = read_file(automate);
        JParser jp{text.data(), text.data() + text.size()};
        A.script = jp.value();
        if (!jp.ok || A.script.t != J::Arr) {
            SDL_Log("automation script %s is not a JSON array", automate.c_str());
            return 2;
        }
    }

    // prepared already: start the game right away, no window of our own (Shift / --setup: the setup)
    if (!want_setup && automate.empty() && !A.self_test && !shift_held()) {
        std::string pkg = find_package().pkg;
        if (!pkg.empty() && game_ready(pkg, A.passthru) && launch_game()) return 0;
    }

    SDL_SetHint(SDL_HINT_APP_NAME, "Wind Waker HD");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    bool offscreen = SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "offscreen");
    if (offscreen) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");  // tests and CI: no GPU, nothing on screen
    float scale = offscreen ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1.0f;
    {
        SDL_PropertiesID wp = SDL_CreateProperties();
        SDL_SetStringProperty(wp, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Wind Waker HD");
        SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, (int)(960 * scale));
        SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, (int)(640 * scale));
        SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        // offscreen (tests): no OpenGL default on macOS; the software renderer needs no context
        if (offscreen) SDL_SetBooleanProperty(wp, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
        g_window = SDL_CreateWindowWithProperties(wp);
        SDL_DestroyProperties(wp);
    }
    if (!g_window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(g_window, (int)(760 * scale), (int)(560 * scale));
    SDL_Renderer* renderer = SDL_CreateRenderer(g_window, offscreen ? "software" : nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(g_window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    apply_style(scale);
    load_fonts(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(g_window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    PackageSearch found = find_package();
    A.pkg = found.pkg;
    if (!A.pkg.empty()) {
        A.version = package_version();
        if (!A.version.empty()) SDL_SetWindowTitle(g_window, ("Wind Waker HD " + A.version).c_str());
        if (found.translocated)
            addlog("macOS started a temporary copy of the app (App Translocation); the release folder is " + A.pkg);
    }
    if (A.pkg.empty()) {
        fail_no_package(found);
    } else if (!check_writable(data_dir_of(A.pkg, A.passthru))) {
        // fail() has shown why
    } else {
#ifdef __APPLE__
        if (!clt_ok()) go(Screen::NeedCLT);
        else start_child();
#else
        start_child();
#endif
    }

    double self_test_deadline = SDL_GetTicks() / 1000.0 + 600;
    int self_test_frames = 0;
    bool quit_requested = false;
    while (A.exit_code < 0) {
        SDL_Event ev;
        bool idle = A.script.t != J::Arr && !A.self_test && A.screen != Screen::Installing && A.screen != Screen::Starting &&
                    !busy() && A.screen != Screen::NeedCLT;
        if (idle) SDL_WaitEventTimeout(nullptr, 100);
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(g_window)))
                quit_requested = true;
        }
        pump_child();
        {
            std::string target, result;
            bool done = false;
            {
                std::lock_guard<std::mutex> lk(A.dlg_mu);
                if (A.dlg_done) done = true, target = A.dlg_target, result = A.dlg_result, A.dlg_done = false;
            }
            if (done) apply_dialog(target, result);
        }
        automation_frame();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        switch (A.screen) {
        case Screen::Starting: screen_starting(); break;
        case Screen::NeedCLT: screen_clt(); break;
        case Screen::Menu: screen_menu(); break;
        case Screen::Welcome: screen_welcome(); break;
        case Screen::Source: screen_source(); break;
        case Screen::Keys: screen_keys(); break;
        case Screen::Installing: screen_installing(); break;
        case Screen::Error: screen_error(); break;
        case Screen::Save: screen_save(); break;
        case Screen::Done: screen_done(); break;
        case Screen::Fatal: screen_fatal(); break;
        }
        if (!A.toast.empty() && ImGui::GetTime() < A.toast_until) {
            ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x,
                                       ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight()));
            colored(GOOD, A.toast);
        }
        if (quit_requested) {
            if (A.screen == Screen::Installing) ImGui::OpenPopup("Stop setup?");
            else A.exit_code = 0;
            quit_requested = false;
        }
        if (ImGui::BeginPopupModal("Stop setup?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Setup is still working. Stop it now?");
            muted("You can run Setup again later; finished steps are kept.");
            if (button("Stop setup", ImVec2(180, 0), true)) {
                A.child.stop(true);
                A.exit_code = 1;
            }
            ImGui::SameLine();
            if (button("Continue", ImVec2(180, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::Render();

        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 248, 250, 251, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!A.shot_pending.empty()) {
            if (!A.shots_dir.empty()) save_shot(renderer, A.shot_pending);
            A.shot_pending.clear();
        }
        if (A.self_test) {
            if (A.screen == Screen::Welcome || A.screen == Screen::Menu) {
                if (++self_test_frames == 30) {
                    if (!A.shots_dir.empty()) save_shot(renderer, std::string("selftest-") + screen_name(A.screen) + ".png");
                    SDL_Log("self-test: setup.py answered (%s %s), %s screen rendered", A.version.c_str(),
                            A.platform.c_str(), screen_name(A.screen));
                    A.exit_code = 0;
                }
            } else if (A.screen == Screen::Fatal || A.screen == Screen::NeedCLT ||
                       SDL_GetTicks() / 1000.0 > self_test_deadline) {
                SDL_Log("self-test failed at the %s screen: %s", screen_name(A.screen), A.fatal.c_str());
                if (!A.fatal_hint.empty()) SDL_Log("  what to do: %s", A.fatal_hint.c_str());
                for (auto& l : A.log) SDL_Log("  %s", l.c_str());
                A.exit_code = 1;
            }
        }
        SDL_RenderPresent(renderer);
        if (A.last_screen != screen_name(A.screen)) {
            A.last_screen = screen_name(A.screen);
            if (A.script.t == J::Arr || A.self_test) SDL_Log("screen: %s", A.last_screen.c_str());
        }
    }
    SDL_memset(A.paste, 0, sizeof A.paste);
    A.child.stop(false);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    if (A.exec_game && !launch_game()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Wind Waker HD", ("Could not start " + g_exe).c_str(), nullptr);
        return 1;
    }
    return A.exit_code;
}
