// Depth clipping in shaders (panvk, R.clipEmulation; shaders.cpp clip_emulation_wanted_regs): the GLSL
// rewrite of both stages and the rule that keeps a draw's two stages and its pipeline consistent.
// Header-only, so runtime/tools/clip_emulation_test.cpp checks it without a renderer.
#pragma once
#include <string>

namespace gfxvk::vk::clip_emulation {

// location 31: draws using all 32 pixel shader inputs are left out (clip_emulation_wanted_regs)
constexpr const char* kVarying = "layout(location = 31) ";

// The vertex shader passes (z, w - z, w) of the Vulkan clip position, interpolated perspective-correctly
// like the point on the triangle; the pixel shader discards outside 0 <= z <= w (and NaN, as at w = 0).
// Needs the decompiler's SET_POSITION define (vertex) and main(); a graphics-pack shader without them
// can't take it. On failure the text is unchanged: it is then exactly the normal variant's text.
inline bool rewrite(std::string& glsl, bool vertex) {
    const size_t main = glsl.find("void main()");
    if (main == std::string::npos) return false;
    if (vertex) {
        static const std::string def = "#define SET_POSITION(_v) ";
        const size_t d = glsl.find(def);
        const size_t eol = d == std::string::npos ? d : glsl.find_first_of("\r\n", d);
        if (eol == std::string::npos || eol > main) return false;
        glsl.insert(main, std::string(kVarying) + "out vec3 wwhdClip;\r\n");
        glsl.insert(eol, "; wwhdClip = vec3(gl_Position.z, gl_Position.w - gl_Position.z, gl_Position.w)");
        return true;
    }
    const size_t brace = glsl.find('{', main);
    if (brace == std::string::npos) return false;
    // a little relative slack: far-plane geometry sits within rounding of z = w
    glsl.insert(brace + 1, "\r\nif (!(wwhdClip.x >= -1e-6 * wwhdClip.z && wwhdClip.y >= -1e-6 * wwhdClip.z)) discard;");
    glsl.insert(main, std::string(kVarying) + "in vec3 wwhdClip;\r\n");
    return true;
}

// A draw clips in its shaders (and its pipeline clamps depth instead of letting panvk cull) only when
// both stages carry the rewrite (Shader::clipVarying).
template <class S>
bool pair_clips(const S* vs, const S* ps) {
    return vs && ps && vs->clipVarying && ps->clipVarying;
}

// The draw's two stages. translate(vertex, clipAllowed, linkedVs) translates one stage; with
// clipAllowed it takes the clipping variant when the draw's state wants one. When only one stage could
// take the rewrite, both are translated again without it: the draw then runs the normal path (no
// clamp, no shader clipping) instead of a half-rewritten pair (a pixel shader reading a varying its
// vertex shader doesn't write) or a failed draw. Returns true when it fell back.
template <class S, class Translate>
bool translate_stages(Translate&& translate, S*& vs, S*& ps) {
    vs = translate(true, true, static_cast<S*>(nullptr));
    ps = translate(false, true, vs && vs->ready() ? vs : static_cast<S*>(nullptr));
    if (!vs || !ps || !vs->ready() || !ps->ready() || vs->clipVarying == ps->clipVarying) return false;
    vs = translate(true, false, static_cast<S*>(nullptr));
    ps = translate(false, false, vs && vs->ready() ? vs : static_cast<S*>(nullptr));
    return true;
}

}  // namespace gfxvk::vk::clip_emulation
