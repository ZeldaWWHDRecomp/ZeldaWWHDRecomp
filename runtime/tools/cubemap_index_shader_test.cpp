// SET_CUBEMAP_INDEX in a pixel shader whose texture unit is (or is not) bound to a cube map: the GLSL
// the Vulkan renderer gets compiles either way. A head-start state may pair a program that sets the
// cube map array index with a 2D texture on that unit; the translation then wrote cubeMapArrayIndex0
// without declaring it ("'cubeMapArrayIndex0' : undeclared identifier", 11 records failed).
// A synthetic Latte program only (no game data): one TEX clause (SET_CUBEMAP_INDEX from R1.x, SAMPLE
// into R0) and EXPORT_DONE of R0 to colour output 0.
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteShaderAssembly.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#ifdef WWHD_TEST_MSL
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#endif
#include "gfx/vulkan/shader_compiler.h"
#include "util/helpers/StringBuf.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

std::unique_ptr<Renderer> g_renderer = std::make_unique<VulkanRenderer>();
void cemu_shim_log(const std::string& msg) { std::fprintf(stderr, "[decompiler] %s\n", msg.c_str()); }

namespace {
std::vector<uint32_t> program() {
    std::vector<uint32_t> w;
    // CF 0: TEX clause at 64-bit word 2, two instructions
    w.push_back(2);
    w.push_back((GPU7_CF_INST_TEX << 23) | ((2 - 1) << 10));
    // CF 1: EXPORT_DONE pixel output 0 from R0.xyzw, end of program
    w.push_back((0u << 15) | (0u << 13) | 0u);
    w.push_back((GPU7_CF_INST_EXPORT_DONE << 23) | (1u << 21) | (0u << 0) | (1u << 3) | (2u << 6) | (3u << 9));
    // TEX 0: SET_CUBEMAP_INDEX texture 0 <- R1.x
    w.push_back(GPU7_TEX_INST_SET_CUBEMAP_INDEX | (0u << 8) | (1u << 16));
    w.push_back(0);
    w.push_back((0u << 20) | (7u << 23) | (7u << 26) | (7u << 29));
    w.push_back(0);
    // TEX 1: SAMPLE texture 0 / sampler 0, R0.xyzw <- tex(R0.xyz), normalized coordinates
    w.push_back(GPU7_TEX_INST_SAMPLE | (0u << 8) | (0u << 16));
    w.push_back(0u | (0u << 9) | (1u << 12) | (2u << 15) | (3u << 18) | (0xFu << 28));
    w.push_back((0u << 20) | (1u << 23) | (2u << 26) | (3u << 29));
    w.push_back(0);
    return w;
}

std::string translate(Latte::E_DIM dim) {
    std::vector<uint32_t> regs(LATTE_MAX_REGISTER, 0);
    regs[Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS] = uint32_t(dim);  // unit 0's DIM (bits 0-2)
    regs[mmCB_SHADER_MASK] = 0xF;  // one colour output (RGBA)
    regs[mmCB_SHADER_CONTROL] = 1;
    auto code = program();
    LatteShader_UpdatePSInputs(regs.data());
    LatteDecompilerOptions options;
    LatteDecompilerOutput_t output{};
    LatteDecompiler_DecompilePixelShader(0x1234, regs.data(), reinterpret_cast<uint8*>(code.data()),
                                         uint32(code.size() * 4), options, &output);
    assert(output.shader && !output.shader->hasError && output.shader->strBuf_shaderSource);
    return output.shader->strBuf_shaderSource->c_str();
}

void compiles(const std::string& glsl, const char* what) {
    auto result = gfxvk::vk::compile_shader(glsl, gfxvk::vk::ShaderStage::Fragment, ~uint64_t{0}, 0, {}, false);
    if (!result.error.empty() || result.words.empty()) {
        std::fprintf(stderr, "%s: %s\n%s\n", what, result.error.c_str(), glsl.c_str());
        assert(false);
    }
}
}  // namespace

int main() {
    // the failing case: the program sets the cube map index of a unit bound to a 2D texture
    const std::string flat = translate(Latte::E_DIM::DIM_2D);
    assert(flat.find("float cubeMapArrayIndex0 = 0.0;") != std::string::npos);
    assert(flat.find("cubeMapArrayIndex0 = R1f.x;") != std::string::npos);
    assert(flat.find("samplerCubeArray") == std::string::npos);
    compiles(flat, "2D texture");

    // a cube map on that unit: declared once, and the sample reads the array layer the program set
    const std::string cube = translate(Latte::E_DIM::DIM_CUBEMAP);
    const size_t declared = cube.find("float cubeMapArrayIndex0 = 0.0;");
    assert(declared != std::string::npos);
    assert(cube.find("float cubeMapArrayIndex0", declared + 1) == std::string::npos);
    const size_t set = cube.find("cubeMapArrayIndex0 = R1f.x;");
    assert(set != std::string::npos && set > declared);
    assert(cube.find("samplerCubeArray") != std::string::npos);
    assert(cube.find(",cubeMapArrayIndex0)", set) != std::string::npos);
    compiles(cube, "cube map");

#ifdef WWHD_TEST_MSL
    // the Metal translation declares it the same way
    g_renderer = std::make_unique<MetalRenderer>();
    const std::string msl = translate(Latte::E_DIM::DIM_2D);
    assert(msl.find("float cubeMapArrayIndex0 = 0.0;") != std::string::npos);
    assert(msl.find("cubeMapArrayIndex0 = R1f.x;") != std::string::npos);
    g_renderer = std::make_unique<VulkanRenderer>();
#endif
    gfxvk::vk::finish_shader_compiler();
    std::puts("cubemap_index_shader: ok");
    return 0;
}
