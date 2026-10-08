#include "crash_context.h"
#include "crash_redact.h"
#include "gfx/renderer.h"
#include "input.h"
#include "interp.h"
#include "mods/manager.h"
#include "mods/packages.h"
#include <atomic>
#include <cstdlib>
#include <sstream>
#include <string>
#ifdef __APPLE__
#include <crt_externs.h>
#elif !defined(_WIN32)
extern char** environ;
#endif

namespace crash_context {
namespace {
constexpr size_t capacity = 32768;
static_assert(std::atomic<char>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic<char> snapshot[capacity];
std::atomic<unsigned> revision{0}, length{0};
Redactor redactor;
void publish(const std::string& text) {
    revision.fetch_add(1, std::memory_order_seq_cst);
    auto size = text.size() < capacity - 1 ? text.size() : capacity - 1;
    for (size_t i = 0; i < size; ++i) snapshot[i].store(text[i], std::memory_order_seq_cst);
    length.store(unsigned(size), std::memory_order_seq_cst);
    revision.fetch_add(1, std::memory_order_seq_cst);
}
void environment(std::ostringstream& out) {
#ifdef __APPLE__
    char** env = *_NSGetEnviron();
#elif defined(_WIN32)
    char** env = _environ;
#else
    char** env = environ;
#endif
    for (char** e = env; e && *e; ++e) {
        std::string_view line(*e);
        if (!line.starts_with("WWHD_")) continue;
        auto equal = line.find('=');
        auto key = line.substr(0, equal);
        // Settings are useful; key/token material must never enter a crash artifact.
        if (key.find("KEY") != key.npos || key.find("TOKEN") != key.npos || key.find("SECRET") != key.npos || key.find("PASSWORD") != key.npos)
            out << key << "=<redacted>\n";
        else {
            for (char c : line) out << (c == '\n' || c == '\r' ? ' ' : c);
            out << '\n';
        }
    }
}
}
void initialize() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("USERNAME");
    redactor.initialize(home, user);
    std::ostringstream out; out << "\n--- crash context (startup) ---\n"; environment(out);
    publish(out.str());
}
void refresh() {
    std::ostringstream out;
    out << "\n--- crash context (latest game-thread snapshot) ---\n";
    out << "renderer=" << render::api_name(render::active()) << "\nresolution=" << (render::g_backend ? render::res_scale() : 1.f)
        << "\nframe_mode=" << interp::mode_name() << "\nframe_target=" << interp::fps()
        << "\ncontroller=" << (input::pro_controller() ? "Pro" : "GamePad") << '\n';
    for (const auto& entry : mods::manager::entries()) out << "builtin." << entry.id << '=' << entry.enabled() << '\n';
    for (const auto& package : mods::packages::list()) if (package.enabled || package.active)
        out << "package=" << package.id << " version=" << package.version << " enabled=" << package.enabled << " active=" << package.active << '\n';
    environment(out); publish(out.str());
}
void redact(int fd, std::string_view text, Output out) { redactor.write(fd, text, out); }
void note(int fd, Output out) {
    // Atomic bytes avoid a data race; no retry/spin if a crash interrupts the publisher.
    char text[capacity];
    auto before = revision.load(std::memory_order_seq_cst);
    if (before & 1) { out(fd, "\ncrash context: update interrupted\n", 34); return; }
    auto size = length.load(std::memory_order_seq_cst);
    for (unsigned i = 0; i < size; ++i) text[i] = snapshot[i].load(std::memory_order_seq_cst);
    if (before == revision.load(std::memory_order_seq_cst)) out(fd, text, size);
    else out(fd, "\ncrash context: update interrupted\n", 34);
}
}
