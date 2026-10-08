// See console_setup_win.h. Until 0.2.6 the Windows setup ran a PowerShell script (-ExecutionPolicy Bypass)
// that downloaded Python; antivirus heuristics read that as a dropper (issue #58). The release now ships the
// official embeddable Python in tools\python, and this program only starts it.
#ifdef _WIN32

#include "console_setup_win.h"

#include <windows.h>

namespace {

std::wstring wide(const std::string& u) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
    return w;
}

std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u.data(), n, nullptr, nullptr);
    return u;
}

// "The system cannot find the file specified (error 2)"
std::string error_text(DWORD code) {
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
                             nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::string s = n && msg ? utf8(msg) : "";
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return (s.empty() ? std::string("error") : s) + " (error " + std::to_string(code) + ")";
}

// one argument for CreateProcess's command line (the rules CommandLineToArgvW and the C runtime parse)
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (size_t i = 0;; i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') i++, bs++;
        if (i == a.size()) {
            q.append(bs * 2, L'\\');
            break;
        }
        if (a[i] == L'"') q.append(bs * 2 + 1, L'\\');
        else q.append(bs, L'\\');
        q += a[i];
    }
    return q + L"\"";
}

HANDLE g_out = INVALID_HANDLE_VALUE;

void print(const std::string& s) {
    std::string l = s + "\r\n";
    DWORD n = 0, mode = 0;
    if (g_out == INVALID_HANDLE_VALUE) return;
    if (GetConsoleMode(g_out, &mode)) {
        std::wstring w = wide(l);
        WriteConsoleW(g_out, w.data(), (DWORD)w.size(), &n, nullptr);
    } else {
        WriteFile(g_out, l.data(), (DWORD)l.size(), &n, nullptr);
    }
}

// the console keeps Ctrl+C for setup.py (KeyboardInterrupt); this program waits for it to finish
BOOL WINAPI ignore_ctrl(DWORD) { return TRUE; }

// an inheritable copy of a standard handle given to this program, or INVALID_HANDLE_VALUE
HANDLE given(DWORD which) {
    HANDLE h = GetStdHandle(which), dup = INVALID_HANDLE_VALUE;
    if (!h || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN) return INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return INVALID_HANDLE_VALUE;
    return dup;
}

HANDLE open_console(const wchar_t* name) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0,
                       nullptr);
}

}  // namespace

std::string bundled_python(const std::string& pkg) { return pkg + "tools\\python\\python.exe"; }

bool have_bundled_python(const std::string& pkg) {
    return GetFileAttributesW(wide(bundled_python(pkg)).c_str()) != INVALID_FILE_ATTRIBUTES;
}

int console_setup(const std::string& pkg, const std::vector<std::string>& args) {
    // This program is a GUI program: it has no console of its own. Output redirected by the caller (a pipe or
    // a file) is used as it is; otherwise setup.py talks to the console window of the .bat that started it.
    HANDLE in = given(STD_INPUT_HANDLE), out = given(STD_OUTPUT_HANDLE), errh = given(STD_ERROR_HANDLE);
    bool console = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (!console && in == INVALID_HANDLE_VALUE && out == INVALID_HANDLE_VALUE) console = AllocConsole() != 0;
    if (console) {
        if (in == INVALID_HANDLE_VALUE) in = open_console(L"CONIN$");
        if (out == INVALID_HANDLE_VALUE) out = open_console(L"CONOUT$");
        if (errh == INVALID_HANDLE_VALUE) errh = open_console(L"CONOUT$");
    }
    g_out = out;

    if (pkg.empty()) {
        print("Setup could not start: the release files were not found. Keep Wind Waker HD.exe in the unzipped "
              "release folder, next to tools\\.");
        return 1;
    }
    std::string python = bundled_python(pkg);
    if (!have_bundled_python(pkg)) {
        print("Setup could not start: " + python + " is missing. The release is incomplete: unzip it again.");
        return 1;
    }

    SetEnvironmentVariableW(L"PYTHONDONTWRITEBYTECODE", L"1");
    std::wstring cmd = quote_arg(wide(python)) + L" " + quote_arg(wide(pkg + "tools\\installer\\setup.py"));
    for (auto& a : args) cmd += L" " + quote_arg(wide(a));
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = errh;
    PROCESS_INFORMATION pi = {};
    // no console of its own: the output goes where ours goes (a pipe), without a window
    DWORD flags = console ? 0 : CREATE_NO_WINDOW;
    if (!CreateProcessW(wide(python).c_str(), cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi)) {
        print("Setup could not start " + python + ": " + error_text(GetLastError()));
        return 1;
    }
    SetConsoleCtrlHandler(ignore_ctrl, TRUE);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

#endif
