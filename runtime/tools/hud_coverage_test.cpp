// 60 fps (generated): the HUD coverage variant of a pixel shader (runtime/src/gfx/vulkan/hud_coverage.h,
// from the GreenNaugahyde/Nycolazs forks, MPL-2.0). The fixtures are glslangValidator -V output of
//   hud: out vec4 o (location 0) written on two returns;  two: outputs at locations 0 and 1.
// The variant gets a new output at the first free location, stored before every return of main.
// HUD_COVERAGE_TEST_OUT=dir writes the variants (hud.spv, two.spv) for spirv-val.
#include "../src/gfx/vulkan/hud_coverage.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
static const uint32_t kHud[] = {0x07230203,0x00010000,0x0008000b,0x0000001a,0x00000000,0x00020011,0x00000001,0x0006000b,0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x00000015,0x00030010,0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00030005,0x00000009,0x00000063,0x00030005,0x00000015,0x0000006f,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x00000015,0x0000001e,0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000001,0x00000007,0x0004003b,0x00000008,0x00000009,0x00000001,0x00040015,0x0000000a,0x00000020,0x00000000,0x0004002b,0x0000000a,0x0000000b,0x00000000,0x00040020,0x0000000c,0x00000001,0x00000006,0x0004002b,0x00000006,0x0000000f,0x3f000000,0x00020014,0x00000010,0x00040020,0x00000014,0x00000003,0x00000007,0x0004003b,0x00000014,0x00000015,0x00000003,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x0000000c,0x0000000d,0x00000009,0x0000000b,0x0004003d,0x00000006,0x0000000e,0x0000000d,0x000500ba,0x00000010,0x00000011,0x0000000e,0x0000000f,0x000300f7,0x00000013,0x00000000,0x000400fa,0x00000011,0x00000012,0x00000013,0x000200f8,0x00000012,0x0004003d,0x00000007,0x00000016,0x00000009,0x0003003e,0x00000015,0x00000016,0x000100fd,0x000200f8,0x00000013,0x0004003d,0x00000007,0x00000018,0x00000009,0x0005008e,0x00000007,0x00000019,0x00000018,0x0000000f,0x0003003e,0x00000015,0x00000019,0x000100fd,0x00010038};
static const uint32_t kTwo[] = {0x07230203,0x00010000,0x0008000b,0x0000000f,0x00000000,0x00020011,0x00000001,0x0006000b,0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,0x0008000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000b,0x0000000d,0x00030010,0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00030005,0x00000009,0x0000006f,0x00030005,0x0000000b,0x00000063,0x00030005,0x0000000d,0x0000316f,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000b,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,0x00000001,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040020,0x0000000a,0x00000001,0x00000007,0x0004003b,0x0000000a,0x0000000b,0x00000001,0x0004003b,0x00000008,0x0000000d,0x00000003,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003d,0x00000007,0x0000000c,0x0000000b,0x0003003e,0x00000009,0x0000000c,0x0004003d,0x00000007,0x0000000e,0x0000000b,0x0003003e,0x0000000d,0x0000000e,0x000100fd,0x00010038};
struct Counts { size_t entryWords = 0, stores = 0, returns = 0; std::vector<uint32_t> locations; };
Counts count(const std::vector<uint32_t>& w) {
    Counts c;
    for (size_t at = 5; at < w.size();) {
        const uint32_t n = w[at] >> 16, op = w[at] & 0xFFFF;
        assert(n && at + n <= w.size());
        if (op == 15) c.entryWords = n;            // OpEntryPoint
        if (op == 62) c.stores++;                  // OpStore
        if (op == 253) c.returns++;                // OpReturn
        if (op == 71 && w[at + 2] == 30) c.locations.push_back(w[at + 3]);  // OpDecorate Location
        at += n;
    }
    return c;
}
void dump(const char* name, const std::vector<uint32_t>& w) {
    const char* dir = getenv("HUD_COVERAGE_TEST_OUT");
    if (!dir) return;
    if (FILE* f = fopen((std::string(dir) + "/" + name).c_str(), "wb")) {
        fwrite(w.data(), 4, w.size(), f);
        fclose(f);
    }
}
bool has(const std::vector<uint32_t>& v, uint32_t x) {
    for (uint32_t y : v) if (y == x) return true;
    return false;
}
}  // namespace

int main() {
    {
        std::vector<uint32_t> w(std::begin(kHud), std::end(kHud));
        const Counts before = count(w);
        uint32_t location = 0;
        assert(hud_coverage::add_output(w, location));
        assert(location == 1);
        const Counts after = count(w);
        assert(after.entryWords == before.entryWords + 1);              // the new output is an interface variable
        assert(after.stores == before.stores + before.returns);         // one store before each return
        assert(has(after.locations, 1) && !has(before.locations, 1));
        assert(w[3] > kHud[3]);                                          // the id bound grew
        dump("hud.spv", w);
        printf("hud: coverage at location 1, %zu returns, %zu stores (was %zu)\n", after.returns, after.stores, before.stores);
    }
    {
        std::vector<uint32_t> w(std::begin(kTwo), std::end(kTwo));
        uint32_t location = 0;
        assert(hud_coverage::add_output(w, location));
        assert(location == 2);  // 0 and 1 are the shader's own
        dump("two.spv", w);
        printf("two: coverage at location 2\n");
    }
    {
        std::vector<uint32_t> w = {1, 2, 3, 4, 5, 6};
        uint32_t location = 0;
        assert(!hud_coverage::add_output(w, location) && w[0] == 1 && w.size() == 6);
        printf("not SPIR-V: left as it is\n");
    }
    printf("hud_coverage: ok\n");
    return 0;
}
