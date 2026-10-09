#pragma once
#include <cstdint>

// WWHD: the value a pixel shader input reads when no vertex shader output feeds it:
// SPI_PS_INPUT_CNTL DEFAULT_VAL (bits 8-9). One table for the translation, which makes such inputs
// constants (LatteDecompilerEmitGLSLHeader.hpp, LatteDecompilerOptions::linkPSInputsToVS), and for
// Cemu packs, whose declarations of them become the same constants (gfx/vulkan/shaders.cpp).
inline const char* LattePSInputDefaultGLSL(uint32_t spiPsInputCntl)
{
	static const char* const defaults[4] = { "vec4(0.0, 0.0, 0.0, 0.0)", "vec4(0.0, 0.0, 0.0, 1.0)",
		"vec4(1.0, 1.0, 1.0, 0.0)", "vec4(1.0, 1.0, 1.0, 1.0)" };
	return defaults[(spiPsInputCntl >> 8) & 3];
}
