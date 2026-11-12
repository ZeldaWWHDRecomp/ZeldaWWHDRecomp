// pane_name_filter.h: the lookup is exact (the same answer as comparing the names as strings), and the table
// holds every pane name the per-pane checks compare. argv[1]: the source tree's runtime/src, scanned for the
// names those checks use.
#include "pane_name_filter.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <string>

using namespace pane_name_filter;

// the reference: what the checks do (the name up to its first NUL, at most 24 bytes; equal or T_Command...)
static bool reference(const char* name24) {
    const std::string_view n(name24, strnlen(name24, 24));
    if (n.starts_with(kPrefix)) return true;
    for (std::string_view k : kNames)
        if (n == k) return true;
    return false;
}

static bool check(const char* name24) {
    const bool got = known(name24), want = reference(name24);
    if (got != want) std::fprintf(stderr, "mismatch for '%.24s': filter %d, strings %d\n", name24, got, want);
    return got == want;
}

int main(int argc, char** argv) {
    // every listed name, with any garbage after its NUL
    std::mt19937 rng(7);
    for (std::string_view k : kNames) {
        char f[24];
        for (int round = 0; round < 64; round++) {
            for (char& ch : f) ch = char(rng());
            std::memcpy(f, k.data(), k.size());
            if (k.size() < 24) f[k.size()] = 0;
            assert(known(f) && check(f));
            // one byte more or less, one byte changed
            char g[24];
            std::memcpy(g, f, 24);
            if (k.size() < 23) { g[k.size()] = '0'; g[k.size() + 1] = 0; assert(check(g)); }
            std::memcpy(g, f, 24);
            g[k.size() - 1] = 0;
            assert(check(g));
            std::memcpy(g, f, 24);
            g[rng() % k.size()] ^= 1 << (rng() % 7);
            assert(check(g));
        }
    }
    // the prefix, a 24-byte name without NUL, empty
    const char* fixed[] = {"T_CommandA_00", "T_CommandZR_00", "T_Command", "T_Comman", "T_Commanx", "",
                           "ABCDEFGHIJKLMNOPQRSTUVWX", "P_B_00\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"};
    for (const char* n : fixed) {
        char f[24] = {};
        std::memcpy(f, n, strnlen(n, 24));
        assert(check(f));
    }
    // random names from the names' alphabet
    const char alphabet[] = "LNPTW_ABCDEFGHIRSXYZaeimnorstu0123\0";
    for (int i = 0; i < 2000000; i++) {
        char f[24];
        const int len = 1 + int(rng() % 24);
        for (int j = 0; j < 24; j++) f[j] = j < len ? alphabet[rng() % (sizeof alphabet - 1)] : char(rng());
        if (len < 24) f[len] = 0;
        assert(check(f));
    }
    // the checks' source files: every pane-like name literal they compare is in the table (a name added there
    // without adding it to kNames fails here)
    if (argc > 1) {
        const char* files[] = {"mods/hud_fade.cpp", "mods/one_screen.cpp", "mods/play_context.h", "button_glyphs.cpp",
                               "button_glyphs.h"};
        const std::regex literal("\"([LNPTW]_[A-Za-z0-9_]+)\"");
        int names = 0;
        for (const char* file : files) {
            std::ifstream in(std::string(argv[1]) + "/" + file);
            assert(in && "source file missing");
            std::stringstream text;
            text << in.rdbuf();
            const std::string s = text.str();
            for (auto it = std::sregex_iterator(s.begin(), s.end(), literal); it != std::sregex_iterator(); ++it) {
                const std::string name = (*it)[1];
                char f[24] = {};
                std::memcpy(f, name.data(), name.size() < 24 ? name.size() : 24);
                if (!known(f)) {
                    std::fprintf(stderr, "%s compares pane name \"%s\", which pane_name_filter.h kNames lacks\n", file, name.c_str());
                    return 1;
                }
                names++;
            }
        }
        assert(names > 50);
    }
    std::puts("pane_name_filter: ok");
}
