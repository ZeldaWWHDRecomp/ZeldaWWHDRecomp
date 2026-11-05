// The compile journal (gfx/vulkan/compile_journal.h): a compile that crashed is left out at the next start, nothing
// else is; another GPU / driver / build tries everything again; ids are stable.
#include "gfx/vulkan/compile_journal.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
void log_msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vfprintf(stderr, fmt, a);
    std::fputc('\n', stderr);
    va_end(a);
}
int main(int argc, char** argv) {
    namespace cj = gfxvk::compile_journal;
    namespace fs = std::filesystem;
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "wwhd_compile_journal_test";
    fs::remove_all(dir);
    const std::string key = "5143 43050A01 8020D000 v0.2.11 abc123 rt1";
    // ids: stable, and different for other content or kinds
    const uint32_t a[3] = {0x07230203, 1, 2}, b[3] = {0x07230203, 1, 3};
    assert(cj::module_id(a, sizeof a) == cj::module_id(a, sizeof a) && cj::module_id(a, sizeof a) != cj::module_id(b, sizeof b));
    assert(cj::glslang_id("void main(){}", 0) != cj::glslang_id("void main(){}", 4));
    const uint64_t m[2] = {cj::module_id(a, sizeof a), cj::module_id(b, sizeof b)}, r[2] = {m[1], m[0]};
    assert(cj::pipeline_id(m, 2) != cj::pipeline_id(r, 2) && cj::pipeline_id(m, 2) == cj::pipeline_id(m, 2));
    // closed: nothing is left out, notes do nothing
    assert(!cj::active() && !cj::left_out(cj::kPipeline, 42));
    cj::begin(cj::kPipeline, 42);
    cj::end();
#ifdef _WIN32
    // Windows has no journal (compile_journal.cpp): open() keeps it closed, so nothing is ever left out there
    assert(cj::open(dir.string(), key) == 0 && !cj::active() && !cj::left_out(cj::kPipeline, 42));
    std::puts("compile journal: off on Windows, as designed");
    return 0;
#endif
    // first start: nothing left out
    assert(cj::open(dir.string(), key) == 0 && cj::active());
    // a compile that ended normally, then one on another thread that "crashes" (the handler marks it)
    { cj::Scope s(cj::kModule, 7); }
    std::thread([] {
        cj::begin(cj::kPipeline, 0xABCDEF);
        wwhd_compile_journal_on_crash();  // what main.cpp's crash handler does
    }).join();
    // a crash outside any compile marks nothing
    std::thread([] {
        { cj::Scope s(cj::kGlslang, 99); }
        wwhd_compile_journal_on_crash();
    }).join();
    cj::close();
    // next start: only the crashed pipeline is left out
    assert(cj::open(dir.string(), key) == 1);
    assert(cj::left_out(cj::kPipeline, 0xABCDEF));
    assert(!cj::left_out(cj::kModule, 0xABCDEF) && !cj::left_out(cj::kModule, 7) && !cj::left_out(cj::kGlslang, 99));
    // a glslang crash in this run adds to the list
    std::thread([] { cj::begin(cj::kGlslang, 5); wwhd_compile_journal_on_crash(); }).join();
    cj::close();
    assert(cj::open(dir.string(), key) == 2 && cj::left_out(cj::kGlslang, 5) && cj::left_out(cj::kPipeline, 0xABCDEF));
    cj::close();
    // the same start again without crashes: the list stays
    assert(cj::open(dir.string(), key) == 2);
    cj::close();
    // another driver (or build): everything is tried again
    assert(cj::open(dir.string(), "5143 43050A01 8020E000 v0.2.11 abc123 rt1") == 0);
    assert(!cj::left_out(cj::kPipeline, 0xABCDEF));
    cj::close();
    fs::remove_all(dir);
    std::puts("compile_journal_test: ok");
}
