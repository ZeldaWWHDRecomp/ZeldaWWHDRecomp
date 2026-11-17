#pragma once
// A pixel shader's depth output, removed from its MSL (Metal renderer).
//
// The translation (LatteDecompilerEmitMSLHeader.hpp) gives a pixel shader that exports depth (export
// 61) a `float passDepth [[depth(any)]];` member in FragmentOut whenever the depth buffer is active
// (LatteMRT::GetActiveDepthBufferMask), and writes it once per export as `out.passDepth = <GPR>.x;`.
// A draw can still end up without a depth attachment: its depth buffer is missing or dropped
// (metal_draw.mm), or a recorded pipeline recipe says so. Metal refuses a pipeline whose fragment
// function writes depth without a depth attachment ("depthAttachmentPixelFormat is not valid and
// shader writes to depth"), and it remembers that outcome for the function: built first, the refused
// combination makes every later pipeline of that function fail too, also the ones with a depth
// attachment (seen on macOS 26.6, M-series; the async compiles decide the order, so it varied from
// run to run). Such pipelines get the program without the depth output instead, which is what the
// GPU does with a depth export when no depth buffer is bound.
#include <string>
#include <string_view>

namespace gfx::msl_depth {

constexpr std::string_view kMember = "float passDepth [[depth(any)]];";
constexpr std::string_view kWrite = "out.passDepth = ";

// whether the MSL declares a depth output
inline bool has_depth_output(std::string_view msl) { return msl.find(kMember) != std::string_view::npos; }

// Removes the depth output: the FragmentOut member and every write to it. False (msl unchanged)
// when the text has no depth output or doesn't have the expected shape.
inline bool strip_depth_output(std::string& msl) {
    const size_t member = msl.find(kMember);
    if (member == std::string::npos || msl.find(kMember, member + 1) != std::string::npos) return false;
    std::string out = msl;
    out.erase(member, kMember.size());
    for (size_t at = out.find(kWrite); at != std::string::npos; at = out.find(kWrite, at)) {
        const size_t end = out.find(';', at);
        if (end == std::string::npos) return false;
        out.erase(at, end + 1 - at);
    }
    if (out.find("passDepth") != std::string::npos) return false;  // a use this doesn't know
    msl.swap(out);
    return true;
}

}  // namespace gfx::msl_depth
