// Shader binding: copy the register values stored in GX2 shader structures
// into the register file. Structure layouts and register sequences follow
// Cemu's GX2 implementation (GX2_Shader.cpp / GX2_shader_legacy.cpp).
#include "gx2_regs.h"
#include "ppc.h"

namespace gx2 {

// GX2VertexShader
constexpr uint32 VS_PGM_RESOURCES = 0x00, VS_PRIMITIVEID_EN = 0x04, VS_OUT_CONFIG = 0x08, VS_OUT_ID_COUNT = 0x0C,
                 VS_OUT_ID = 0x10, VS_PA_CL_VS_OUT_CNTL = 0x38, VS_SEMANTIC_COUNT = 0x40, VS_SEMANTIC = 0x44,
                 VS_SHADER_SIZE = 0xD0, VS_SHADER_PTR = 0xD4, VS_SHADER_MODE = 0xD8, VS_RBUFFER = 0x124;
// GX2PixelShader
constexpr uint32 PS_REGS = 0x00, PS_SHADER_SIZE = 0xA4, PS_SHADER_PTR = 0xA8, PS_RBUFFER = 0xD8;
// GX2RBuffer: +0 flags, +4 elementSize, +8 elementCount, +C ptr

static uint32 rbuffer_addr(uint32 rb, uint32* size) {
    if (size) *size = ld32(rb + 4) * ld32(rb + 8);
    return ld32(rb + 0xC);
}

uint32 vertex_shader_program(uint32 vs, uint32* size) {
    if (uint32 p = ld32(vs + VS_SHADER_PTR)) {
        *size = ld32(vs + VS_SHADER_SIZE);
        return p;
    }
    return rbuffer_addr(vs + VS_RBUFFER, size);
}

uint32 pixel_shader_program(uint32 ps, uint32* size) {
    if (uint32 p = ld32(ps + PS_SHADER_PTR)) {
        *size = ld32(ps + PS_SHADER_SIZE);
        return p;
    }
    return rbuffer_addr(ps + PS_RBUFFER, size);
}

template <class Regs>
static void bind_vs(Regs& regs, uint32 vs) {
    uint32 size = 0;
    uint32 prog = vertex_shader_program(vs, &size);
    bool gs_mode = ld32(vs + VS_SHADER_MODE) == 2;  // GX2_SHADER_MODE_GEOMETRY_SHADER
    if (gs_mode) {
        // with a geometry shader the vertex shader runs as the export shader (ES)
        regs[mmSQ_PGM_START_ES] = prog >> 8;
        regs[mmSQ_PGM_START_ES + 1] = size >> 3;
        regs[mmSQ_PGM_RESOURCES_ES] = ld32(vs + VS_PGM_RESOURCES);
    } else {
        regs[mmSQ_PGM_START_VS] = prog >> 8;
        regs[mmSQ_PGM_START_VS + 1] = size >> 3;
        regs[mmSQ_PGM_RESOURCES_VS] = ld32(vs + VS_PGM_RESOURCES);
        regs[mmVGT_PRIMITIVEID_EN] = ld32(vs + VS_PRIMITIVEID_EN);
        regs[mmSPI_VS_OUT_CONFIG] = ld32(vs + VS_OUT_CONFIG);
        regs[mmPA_CL_VS_OUT_CNTL] = ld32(vs + VS_PA_CL_VS_OUT_CNTL);
        uint32 n = std::min<uint32>(ld32(vs + VS_OUT_ID_COUNT), 10);
        for (uint32 i = 0; i < n; i++) regs[mmSPI_VS_OUT_ID_0 + i] = ld32(vs + VS_OUT_ID + 4 * i);
    }
    uint32 nsem = std::min<uint32>(ld32(vs + VS_SEMANTIC_COUNT), 32);
    if (nsem > 0) {
        regs[mmSQ_VTX_SEMANTIC_CLEAR] = 0xFFFFFFFF;
        for (uint32 i = 0; i < nsem; i++) regs[mmSQ_VTX_SEMANTIC_0 + i] = ld32(vs + VS_SEMANTIC + 4 * i);
    }
}

template <class Regs>
static void bind_ps(Regs& regs, uint32 ps) {
    uint32 size = 0;
    uint32 prog = pixel_shader_program(ps, &size);
    auto reg = [&](int i) { return ld32(ps + PS_REGS + 4 * i); };
    uint32 ninputs = std::min<uint32>(reg(4), 0x20);
    regs[mmSQ_PGM_START_PS] = prog >> 8;
    regs[mmSQ_PGM_START_PS + 1] = size >> 3;
    regs[mmSQ_PGM_RESOURCES_PS] = reg(0);
    regs[mmSPI_PS_IN_CONTROL_0] = reg(2);
    regs[mmSPI_PS_IN_CONTROL_1] = reg(3);
    for (uint32 i = 0; i < ninputs; i++) regs[mmSPI_PS_INPUT_CNTL_0 + i] = reg(5 + i);
    regs[mmCB_SHADER_MASK] = reg(37);
    regs[mmCB_SHADER_CONTROL] = reg(38);
    regs[mmDB_SHADER_CONTROL] = reg(39);
    regs[mmSPI_INPUT_Z] = reg(40);
}

void bind_vertex_shader_regs(uint32* regs, uint32 vs) { bind_vs(regs, vs); }
void bind_vertex_shader_regs(ShaderBindRegs& regs, uint32 vs) { bind_vs(regs, vs); }
void bind_pixel_shader_regs(uint32* regs, uint32 ps) { bind_ps(regs, ps); }
void bind_pixel_shader_regs(ShaderBindRegs& regs, uint32 ps) { bind_ps(regs, ps); }

void bind_geometry_shader_regs(uint32* regs, uint32 gs) {
    (void)regs;
    (void)gs;  // TODO: geometry shaders
}

}  // namespace gx2
