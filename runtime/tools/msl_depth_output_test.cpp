// gfx/msl_depth_output.h: the depth output removed from a translated pixel shader's MSL
#include <cassert>
#include <cstdio>
#include <string>

#include "gfx/msl_depth_output.h"

using gfx::msl_depth::has_depth_output;
using gfx::msl_depth::strip_depth_output;

int main() {
    // the shape LatteDecompilerEmitMSL writes (FragmentOut, one depth export in the body)
    const std::string ps =
        "struct FragmentOut {\r\n"
        "float4 passPixelColor0 [[color(0)]];\r\n"
        "float passDepth [[depth(any)]];\r\n"
        "};\r\n\r\n"
        "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\r\n"
        "FragmentOut out;\r\n"
        "out.passPixelColor0 = as_type<float4>(R0i.xyzw);\r\n"
        "out.passDepth = as_type<float4>(R1i).x;\r\n"
        "return out;\r\n"
        "}\r\n";
    assert(has_depth_output(ps));
    std::string s = ps;
    assert(strip_depth_output(s));
    assert(!has_depth_output(s));
    assert(s.find("passDepth") == std::string::npos);
    assert(s.find("out.passPixelColor0 = as_type<float4>(R0i.xyzw);") != std::string::npos);
    assert(s.find("float4 passPixelColor0 [[color(0)]];") != std::string::npos);
    assert(s.find("return out;") != std::string::npos);

    // depth written on several paths
    std::string two = ps;
    two.insert(two.find("return out;"), "if (x) {\r\nout.passDepth = R2f.x;\r\n}\r\n");
    assert(strip_depth_output(two));
    assert(two.find("passDepth") == std::string::npos && two.find("if (x) {") != std::string::npos);

    // no depth output: unchanged
    std::string none = "struct FragmentOut {\r\nfloat4 passPixelColor0 [[color(0)]];\r\n};\r\n";
    const std::string noneCopy = none;
    assert(!has_depth_output(none) && !strip_depth_output(none) && none == noneCopy);

    // an unknown use of the member: refused, unchanged
    std::string odd = ps;
    odd.insert(odd.find("return out;"), "float z = out.passDepth;\r\n");
    const std::string oddCopy = odd;
    assert(!strip_depth_output(odd) && odd == oddCopy);

    // a write without its ';': refused, unchanged
    std::string cut = "float passDepth [[depth(any)]];\r\nout.passDepth = R1f.x";
    const std::string cutCopy = cut;
    assert(!strip_depth_output(cut) && cut == cutCopy);

    std::puts("msl_depth_output: ok");
    return 0;
}
