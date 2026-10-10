#include "gfx/vulkan/formats.h"
#include <array>
#include <cassert>
#include <cstring>

int main() {
    using namespace gfxvk;
    auto color = format_info(0x1A, false);
    assert(color.pixel == VK_FORMAT_R8G8B8A8_UNORM && !color.depth);
    assert(format_info(0x1A, true).pixel == VK_FORMAT_UNDEFINED); // not a depth attachment
    auto compare = comparison_format_info(0x1A);
    assert(compare.pixel == VK_FORMAT_D32_SFLOAT && compare.depth && !compare.stencil);
    assert(compare.bytesPerBlock == 4 && compare.hostBytesPerBlock == 4);
    assert(compare.convert == Convert::RGBA8_DEPTH);
    const uint8_t rgba[] = {0, 255, 255, 255, 128, 0, 255, 0, 255, 0, 0, 0};
    std::array<uint8_t, 20> converted;
    converted.fill(0xA5);
    convert_row(compare.convert, rgba, converted.data()+4, 3);
    float values[3]; std::memcpy(values, converted.data()+4, sizeof values);
    assert(values[0] == 0 && values[1] > .5019f && values[1] < .5020f && values[2] == 1);
    for (unsigned i : {0u,1u,2u,3u,16u,17u,18u,19u}) assert(converted[i] == 0xA5);
    for (uint32_t fmt : {0x05u,0x0Eu,0x11u,0x1Cu}) {
        auto native = format_info(fmt, true), sampled = comparison_format_info(fmt);
        assert(sampled.pixel == native.pixel && sampled.convert == native.convert);
        assert(sampled.stencil == native.stencil && sampled.bytesPerBlock == native.bytesPerBlock);
    }
    for (uint32_t fmt : {0x11Au,0x21Au,0x41Au,0x3Fu})
        assert(comparison_format_info(fmt).pixel == VK_FORMAT_UNDEFINED);
}
