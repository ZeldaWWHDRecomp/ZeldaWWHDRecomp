// SPDX-License-Identifier: MPL-2.0
// Depth clipping in shaders (panvk; gfx/vulkan/clip_emulation.h): the GLSL rewrite of both stages, and
// the fallback when a stage can't take it (a graphics-pack vertex shader without the decompiler's
// SET_POSITION): the draw runs the normal path, both stages without the rewrite and a pipeline without
// the clamp, instead of failing (the draw disappearing) or pairing a pixel shader that reads the clip
// varying with a vertex shader that doesn't write it. The rewritten pair is compiled with glslang.
// Synthetic shaders only (no game data), shaped like the decompiler's GLSL.
#include "gfx/vulkan/clip_emulation.h"
#include "gfx/vulkan/shader_compiler.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using namespace gfxvk::vk;
namespace {
const std::string kDecompiledVs =
    "#version 450\r\n"
    "#define SET_POSITION(_v) gl_Position = _v\r\n"
    "layout(location = 0) in vec4 attrDataSem0;\r\n"
    "layout(location = 0) out vec4 passParameterSem0;\r\n"
    "void main()\r\n{\r\nSET_POSITION(attrDataSem0);\r\npassParameterSem0 = attrDataSem0;\r\n}\r\n";
// a graphics pack's own vertex shader: writes gl_Position directly, no SET_POSITION define
const std::string kPackVs =
    "#version 450\r\n"
    "layout(location = 0) in vec4 attrDataSem0;\r\n"
    "layout(location = 0) out vec4 passParameterSem0;\r\n"
    "void main()\r\n{\r\ngl_Position = attrDataSem0;\r\npassParameterSem0 = attrDataSem0;\r\n}\r\n";
const std::string kDecompiledPs =
    "#version 450\r\n"
    "layout(location = 0) in vec4 passParameterSem0;\r\n"
    "layout(location = 0) out vec4 passPixelColor0;\r\n"
    "void main()\r\n{\r\npassPixelColor0 = passParameterSem0;\r\n}\r\n";

// Stand-in for vk::Shader and vk::translate: the rewrite runs when the draw wants clipping (as
// clip_emulation_wanted_regs) and the caller allows it, exactly as decompile() applies it.
struct FakeShader {
    std::string glsl;
    bool clipVarying = false;
    bool ready() const { return !glsl.empty(); }
};
struct FakeDraw {
    std::string vsSource, psSource;
    bool wanted = true;  // clip_emulation_wanted_regs: panvk, no depth writes
    std::deque<FakeShader> made;  // stable addresses, like the shader cache
    struct Call { bool vertex, clipAllowed; const FakeShader* linkedVs; };
    std::vector<Call> calls;
    FakeShader* operator()(bool vertex, bool clipAllowed, FakeShader* linkedVs) {
        calls.push_back({vertex, clipAllowed, linkedVs});
        FakeShader s;
        s.glsl = vertex ? vsSource : psSource;
        if (wanted && clipAllowed && clip_emulation::rewrite(s.glsl, vertex)) s.clipVarying = true;
        made.push_back(s);
        return &made.back();
    }
};
bool compiles(const std::string& glsl, bool vertex) {
    auto result = compile_shader(glsl, vertex ? ShaderStage::Vertex : ShaderStage::Fragment, 1, 1, {}, false);
    if (!result.error.empty()) std::fprintf(stderr, "%s\n", result.error.c_str());
    return !result.words.empty() && result.error.empty();
}
bool has_clip(const std::string& glsl) { return glsl.find("wwhdClip") != std::string::npos; }
}  // namespace

int main() {
    // The rewrite itself: decompiled stages take it and compile.
    {
        std::string vs = kDecompiledVs, ps = kDecompiledPs;
        assert(clip_emulation::rewrite(vs, true) && clip_emulation::rewrite(ps, false));
        assert(vs.find("layout(location = 31) out vec3 wwhdClip;") != std::string::npos);
        assert(ps.find("layout(location = 31) in vec3 wwhdClip;") != std::string::npos);
        assert(ps.find("discard;") != std::string::npos);
        assert(compiles(vs, true) && compiles(ps, false));
    }
    // A vertex shader without SET_POSITION can't take it; its text stays the normal one.
    {
        std::string vs = kPackVs;
        assert(!clip_emulation::rewrite(vs, true));
        assert(vs == kPackVs);
        std::string noMain = "#version 450\r\nvoid other() {}\r\n";
        assert(!clip_emulation::rewrite(noMain, false) && noMain == "#version 450\r\nvoid other() {}\r\n");
    }
    // Both stages rewritten: the draw clips in its shaders, its pipeline clamps; one translation each.
    {
        FakeDraw draw{kDecompiledVs, kDecompiledPs};
        FakeShader *vs = nullptr, *ps = nullptr;
        assert(!clip_emulation::translate_stages(draw, vs, ps));
        assert(draw.calls.size() == 2 && draw.calls[1].linkedVs == vs);
        assert(vs->clipVarying && ps->clipVarying && clip_emulation::pair_clips(vs, ps));
        assert(has_clip(vs->glsl) && has_clip(ps->glsl));
    }
    // A graphics-pack vertex shader without SET_POSITION: no failure; both stages come back without the
    // rewrite and the pipeline doesn't clamp (the normal path), and that pair compiles.
    {
        FakeDraw draw{kPackVs, kDecompiledPs};
        FakeShader *vs = nullptr, *ps = nullptr;
        assert(clip_emulation::translate_stages(draw, vs, ps));
        assert(vs && ps && vs->ready() && ps->ready());
        assert(!vs->clipVarying && !ps->clipVarying && !clip_emulation::pair_clips(vs, ps));
        assert(vs->glsl == kPackVs && ps->glsl == kDecompiledPs && !has_clip(ps->glsl));
        assert(draw.calls.size() == 4 && !draw.calls[2].clipAllowed && !draw.calls[3].clipAllowed &&
               draw.calls[3].linkedVs == vs);
        assert(compiles(vs->glsl, true) && compiles(ps->glsl, false));
    }
    // The pixel stage can't take it (no main to find): the vertex shader is translated again without it.
    {
        FakeDraw draw{kDecompiledVs, "#version 450\r\nvoid other() {}\r\n"};
        FakeShader *vs = nullptr, *ps = nullptr;
        assert(clip_emulation::translate_stages(draw, vs, ps));
        assert(!vs->clipVarying && !ps->clipVarying && !has_clip(vs->glsl) && vs->glsl == kDecompiledVs);
        assert(!clip_emulation::pair_clips(vs, ps));
    }
    // Neither stage can take it, or the draw doesn't want it (WWHD_VK_CLIP_EMULATION=0, depth writes):
    // the normal pair at once, no second translation.
    for (bool wanted : {true, false}) {
        FakeDraw draw{wanted ? kPackVs : kDecompiledVs, wanted ? "#version 450\r\nvoid other() {}\r\n" : kDecompiledPs};
        draw.wanted = wanted;
        FakeShader *vs = nullptr, *ps = nullptr;
        assert(!clip_emulation::translate_stages(draw, vs, ps));
        assert(draw.calls.size() == 2 && !clip_emulation::pair_clips(vs, ps));
    }
    finish_shader_compiler();
    std::puts("clip_emulation: ok");
    return 0;
}
