// Adapted from Cemu src/Cafe/HW/Latte/Core/LatteRenderTarget.cpp and Renderer/Metal/LatteToMtl.cpp
// Copyright (c) Cemu contributors. Licensed under the Mozilla Public License 2.0 (see LICENSE.txt).
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#ifdef ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"
#else
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#endif
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"

uint8 LatteMRT::GetActiveColorBufferMask(const LatteDecompilerShader* pixelShader, const LatteContextRegister& lcr)
{
	if (!pixelShader) [[unlikely]]
		return 0;
	const uint32* regView = lcr.GetRawView();
	// check if color buffer output is active
	const Latte::LATTE_CB_COLOR_CONTROL& colorControlReg = lcr.CB_COLOR_CONTROL;
	uint32 colorBufferDisable = colorControlReg.get_SPECIAL_OP() == Latte::LATTE_CB_COLOR_CONTROL::E_SPECIALOP::DISABLE;
	if (colorBufferDisable)
		return 0;
	cemu_assert_debug(colorControlReg.get_DEGAMMA_ENABLE() == false); // not supported
	// start with color buffer mask from pixel shader output
	uint8 colorBufferMask = pixelShader->pixelColorOutputMask;
	// combine color buffer mask with color channel mask from mmCB_TARGET_MASK (disable render buffer if all colors are blocked)
	uint32 channelTargetMask = lcr.CB_TARGET_MASK.get_MASK();
	for (uint32 i = 0; i < 8; i++)
	{
		if (((channelTargetMask >> (i * 4)) & 0xF) == 0)
			colorBufferMask &= ~(1 << i);
	}
	// render targets smaller than the scissor size are not allowed
	// this fixes a few render issues in Cemu but we dont know if this matches HW behavior
	// also check for color buffers without a valid pointer
	cemu_assert_debug(lcr.PA_SC_GENERIC_SCISSOR_TL.get_WINDOW_OFFSET_DISABLE() == true); // todo (not exposed by GX2 API)
	uint32 scissorAccessWidth = lcr.PA_SC_GENERIC_SCISSOR_BR.get_BR_X();
	uint32 scissorAccessHeight = lcr.PA_SC_GENERIC_SCISSOR_BR.get_BR_Y();
	for (uint32 i = 0; i < 8; i++)
	{
		if( (colorBufferMask&(1<<i)) == 0 )
			continue;
		if (regView[mmCB_COLOR0_BASE + i] == MPTR_NULL) [[unlikely]]
			colorBufferMask &= ~(1 << i);
		// get width/height
		uint32 regColorSize = regView[mmCB_COLOR0_SIZE + i];
		uint32 regColorInfo = regView[mmCB_COLOR0_INFO + i];
		// decode color buffer reg info
		uint32 colorBufferPitch = (((regColorSize >> 0) & 0x3FF) + 1);
		colorBufferPitch <<= 3;
		uint32 pitchHeight = (((regColorSize >> 10) & 0xFFFFF) + 1);
		pitchHeight <<= 6;
		uint32 colorBufferHeight = pitchHeight / colorBufferPitch;
		uint32 colorBufferWidth = colorBufferPitch;

		if ((colorBufferWidth < (sint32)scissorAccessWidth) || (colorBufferHeight < (sint32)scissorAccessHeight))
		{
            // log this?
			colorBufferMask &= ~(1<<i);
		}
	}
	return colorBufferMask;
}

// returns true if depth/stencil buffer is used
bool LatteMRT::GetActiveDepthBufferMask(const LatteContextRegister& lcr)
{
	bool depthBufferMask = true;
	// if depth test is not used then detach the depth buffer
	bool depthEnable = lcr.DB_DEPTH_CONTROL.get_Z_ENABLE();
	bool stencilTestEnable = lcr.DB_DEPTH_CONTROL.get_STENCIL_ENABLE();
	bool backStencilEnable = lcr.DB_DEPTH_CONTROL.get_BACK_STENCIL_ENABLE();

	if (!depthEnable && !stencilTestEnable && !backStencilEnable)
		depthBufferMask = false;

	return depthBufferMask;
}

const uint32 _colorBufferFormatBits[] =
{
	0, // 0
	0x200, // 1
	0, // 2
	0, // 3
	0x100, // 4
	0x300, // 5
	0x400, // 6
	0x800, // 7
};

Latte::E_GX2SURFFMT LatteMRT::GetColorBufferFormat(const uint32 index, const LatteContextRegister& lcr)
{
	cemu_assert_debug(index < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS);
	uint32 regColorInfo = lcr.GetRawView()[mmCB_COLOR0_INFO + index];
	uint32 colorBufferFormat = (regColorInfo >> 2) & 0x3F; // base HW format
	uint32 numberType = (regColorInfo >> 12) & 7;
	colorBufferFormat |= _colorBufferFormatBits[numberType];
	return (Latte::E_GX2SURFFMT)colorBufferFormat;
}

// return GX2 format of current depth buffer
Latte::E_GX2SURFFMT LatteMRT::GetDepthBufferFormat(const LatteContextRegister& lcr)
{
	uint32 regDepthBufferInfo = lcr.GetRawView()[mmDB_DEPTH_INFO];
	switch (regDepthBufferInfo & 7)
	{
	case 1:
		return Latte::E_GX2SURFFMT::D16_UNORM;
	case 3:
		return Latte::E_GX2SURFFMT::D24_S8_UNORM;
	case 5:
		return Latte::E_GX2SURFFMT::D24_S8_FLOAT;
	case 6:
		return Latte::E_GX2SURFFMT::D32_FLOAT;
	case 7:
		return Latte::E_GX2SURFFMT::D32_S8_FLOAT;
	default:
		debug_printf("Invalid DB_DEPTH_INFO depthbuffer format (%d)\n", (regDepthBufferInfo & 7));
		break;
	}
	return Latte::E_GX2SURFFMT::D16_UNORM;
}

#ifdef ENABLE_METAL
MTL::VertexFormat GetMtlVertexFormat(Latte::E_HWFMT format)
{
    switch (format)
	{
	case Latte::E_HWFMT::HWFMT_32_32_32_32_FLOAT:
		return MTL::VertexFormatUInt4;
	case Latte::E_HWFMT::HWFMT_32_32_32_FLOAT:
		return MTL::VertexFormatUInt3;
	case Latte::E_HWFMT::HWFMT_32_32_FLOAT:
		return MTL::VertexFormatUInt2;
	case Latte::E_HWFMT::HWFMT_32_FLOAT:
		return MTL::VertexFormatUInt;
	case Latte::E_HWFMT::HWFMT_8_8_8_8:
		return MTL::VertexFormatUChar4;
	case Latte::E_HWFMT::HWFMT_8_8_8:
		return MTL::VertexFormatUChar3;
	case Latte::E_HWFMT::HWFMT_8_8:
		return MTL::VertexFormatUChar2;
	case Latte::E_HWFMT::HWFMT_8:
		return MTL::VertexFormatUChar;
	case Latte::E_HWFMT::HWFMT_32_32_32_32:
		return MTL::VertexFormatUInt4;
	case Latte::E_HWFMT::HWFMT_32_32_32:
		return MTL::VertexFormatUInt3;
	case Latte::E_HWFMT::HWFMT_32_32:
		return MTL::VertexFormatUInt2;
	case Latte::E_HWFMT::HWFMT_32:
		return MTL::VertexFormatUInt;
	case Latte::E_HWFMT::HWFMT_16_16_16_16:
		return MTL::VertexFormatUShort4;
	case Latte::E_HWFMT::HWFMT_16_16_16:
		return MTL::VertexFormatUShort3;
	case Latte::E_HWFMT::HWFMT_16_16:
		return MTL::VertexFormatUShort2;
	case Latte::E_HWFMT::HWFMT_16:
		return MTL::VertexFormatUShort;
	case Latte::E_HWFMT::HWFMT_16_16_16_16_FLOAT:
		return MTL::VertexFormatUShort4;
	case Latte::E_HWFMT::HWFMT_16_16_16_FLOAT:
		return MTL::VertexFormatUShort3;
	case Latte::E_HWFMT::HWFMT_16_16_FLOAT:
		return MTL::VertexFormatUShort2;
	case Latte::E_HWFMT::HWFMT_16_FLOAT:
		return MTL::VertexFormatUShort;
    case Latte::E_HWFMT::HWFMT_2_10_10_10:
		return MTL::VertexFormatUInt;
	default:
		cemuLog_log(LogType::Force, "unsupported vertex format {}", (uint32)format);
		
		return MTL::VertexFormatInvalid;
	}
}

uint32 GetMtlVertexFormatSize(Latte::E_HWFMT format)
{
    switch (format)
	{
	case Latte::E_HWFMT::HWFMT_32_32_32_32_FLOAT:
		return 16;
	case Latte::E_HWFMT::HWFMT_32_32_32_FLOAT:
		return 12;
	case Latte::E_HWFMT::HWFMT_32_32_FLOAT:
		return 8;
	case Latte::E_HWFMT::HWFMT_32_FLOAT:
		return 4;
	case Latte::E_HWFMT::HWFMT_8_8_8_8:
		return 4;
	case Latte::E_HWFMT::HWFMT_8_8_8:
		return 3;
	case Latte::E_HWFMT::HWFMT_8_8:
		return 2;
	case Latte::E_HWFMT::HWFMT_8:
		return 1;
	case Latte::E_HWFMT::HWFMT_32_32_32_32:
		return 16;
	case Latte::E_HWFMT::HWFMT_32_32_32:
		return 12;
	case Latte::E_HWFMT::HWFMT_32_32:
		return 8;
	case Latte::E_HWFMT::HWFMT_32:
		return 4;
	case Latte::E_HWFMT::HWFMT_16_16_16_16:
		return 8;
	case Latte::E_HWFMT::HWFMT_16_16_16:
		return 6;
	case Latte::E_HWFMT::HWFMT_16_16:
		return 4;
	case Latte::E_HWFMT::HWFMT_16:
		return 2;
	case Latte::E_HWFMT::HWFMT_16_16_16_16_FLOAT:
		return 8;
	case Latte::E_HWFMT::HWFMT_16_16_16_FLOAT:
		return 6;
	case Latte::E_HWFMT::HWFMT_16_16_FLOAT:
		return 4;
	case Latte::E_HWFMT::HWFMT_16_FLOAT:
		return 2;
	case Latte::E_HWFMT::HWFMT_2_10_10_10:
		return 4;
	default:
		return 0;
	}
}
#endif

// LatteFetchShader: we build fetch shaders directly from GX2 attribute descriptions,
// so the cache machinery of Cemu's FetchShader.cpp is not needed.
LatteFetchShader::~LatteFetchShader() {}
uint32 LatteParsedFetchShaderBufferGroup::getCurrentBufferStride(uint32* contextRegister) const {
    uint32 bufferIndex = this->attributeBufferIndex;
    uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
    return (contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;
}

// ---- from Cemu src/Cafe/HW/Latte/Core/LatteShader.cpp
LatteShaderPSInputTable _activePSImportTable;

LatteShaderPSInputTable* LatteSHRC_GetPSInputTable()
{
	return &_activePSImportTable;
}

void LatteShader_CreatePSInputTable(LatteShaderPSInputTable* psInputTable, uint32* contextRegisters)
{
    // PS control
	uint32 psControl0 = contextRegisters[mmSPI_PS_IN_CONTROL_0];
	uint32 spi0_positionEnable = (psControl0 >> 8) & 1;
	uint32 spi0_positionCentroid = (psControl0 >> 9) & 1;
	cemu_assert_debug(spi0_positionCentroid == 0); // controls gl_FragCoord
	uint32 spi0_positionAddr = spi0_positionEnable ? ((psControl0 >> 10) & 0x1F) : 0xFFFFFFFF; // controls gl_FragCoord
	uint32 spi0_paramGen = (psControl0 >> 15) & 0xF; // used for gl_PointCoords
	uint32 spi0_paramGenAddr = (psControl0 >> 19) & 0x7F;
	sint32 importIndex = 0;

	//cemu_assert_debug(((psControl0>>26)&3) == 1); // BARYC_SAMPLE_CNTL
	//cemu_assert_debug((psControl0&(1 << 28)) == 0); // PERSP_GRADIENT_ENA
	//cemu_assert_debug((psControl0&(1 << 29)) == 0); // LINEAR_GRADIENT_ENA
	// if LINEAR_GRADIENT_ENA_bit is enabled, the pixel shader accesses gl_ClipSize?

	// VS/GS parameters
	uint32 numPSInputs = contextRegisters[mmSPI_PS_IN_CONTROL_0] & 0x3F;
	uint64 key = 0;

	if (spi0_positionEnable)
	{
		key += (uint64)spi0_positionAddr + 1;
	}

	// parameter gen
	if (spi0_paramGen != 0)
	{
		key += std::rotr<uint64>(spi0_paramGen, 7);
		key += std::rotr<uint64>(spi0_paramGenAddr, 3);
		psInputTable->paramGen = spi0_paramGen;
		psInputTable->paramGenGPR = spi0_paramGenAddr;
	}
	else
	{
		psInputTable->paramGen = 0;
	}

	// semantic imports from vertex shader
#ifdef CEMU_DEBUG_ASSERT
	uint8 semanticMask[256 / 8] = { 0 };
#endif
	cemu_assert_debug(numPSInputs <= GPU7_PS_MAX_INPUTS);
	numPSInputs = std::min<uint32>(numPSInputs, GPU7_PS_MAX_INPUTS);

	for (uint32 f = 0; f < numPSInputs; f++)
	{
		uint32 psInputControl = contextRegisters[mmSPI_PS_INPUT_CNTL_0 + f];
		uint32 psSemanticId = (psInputControl & 0xFF);

		uint8 defaultValue = (psInputControl>>8)&3;
		// default:
		// 0 -> 0.0 0.0 0.0 0.0
		// 1 -> 0.0 0.0 0.0 1.0
		// 2 -> 1.0 1.0 1.0 0.0
		// 3 -> 1.0 1.0 1.0 1.0
		cemu_assert_debug(defaultValue <= 1);

		uint32 uknBits = psInputControl & ~((0xFF)|(0x3<<8) | (1 << 10) | (1 << 12));
		uknBits &= ~0x800; // FLAT_SHADE
		//cemu_assert_debug(uknBits == 0);
		//cemu_assert_debug(((psInputControl >> 11) & 1) == 0); // centroid
		//cemu_assert_debug(((psInputControl >> 17) & 1) == 0); // point sprite coord
		cemu_assert_debug(psSemanticId != 0xFF);

		key += (uint64)psInputControl;
		key = std::rotl<uint64>(key, 7);
		if (f == spi0_positionAddr)
		{
			psInputTable->import[f].semanticId = LATTE_ANALYZER_IMPORT_INDEX_SPIPOSITION;
			psInputTable->import[f].isFlat = false;
			psInputTable->import[f].isNoPerspective = false;
			key += (uint64)0x33;
		}
		else
		{
#ifdef CEMU_DEBUG_ASSERT
			if (semanticMask[psSemanticId >> 3] & (1 << (psSemanticId & 7)))
			{
				cemuLog_logDebug(LogType::Force, "SemanticId already used");
			}
			semanticMask[psSemanticId >> 3] |= (1 << (psSemanticId & 7));
#endif

			psInputTable->import[f].semanticId = psSemanticId;
			psInputTable->import[f].isFlat = (psInputControl&(1 << 10)) != 0;
			psInputTable->import[f].isNoPerspective = (psInputControl&(1 << 12)) != 0;
		}
	}
	psInputTable->key = key;
	psInputTable->count = numPSInputs;
}

// both vertex and geometry/pixel shader depend on PS inputs
// we prepare the PS import info in advance
void LatteShader_UpdatePSInputs(uint32* contextRegisters)
{
	LatteShader_CreatePSInputTable(&_activePSImportTable, contextRegisters);
}

// ---- from Cemu src/Cafe/HW/Latte/Core/LatteTextureLegacy.cpp
Latte::E_GX2SURFFMT LatteTexture_ReconstructGX2Format(const Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N& texUnitWord1, const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N& texUnitWord4)
{
	Latte::E_GX2SURFFMT gx2Format = (Latte::E_GX2SURFFMT)texUnitWord1.get_DATA_FORMAT();
	auto nfa = texUnitWord4.get_NUM_FORM_ALL();
	if (nfa == Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N::E_NUM_FORMAT_ALL::NUM_FORMAT_SCALED)
		gx2Format |= Latte::E_GX2SURFFMT::FMT_BIT_FLOAT;
	else if (nfa == Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N::E_NUM_FORMAT_ALL::NUM_FORMAT_INT)
		gx2Format |= Latte::E_GX2SURFFMT::FMT_BIT_INT;

	if(texUnitWord4.get_FORCE_DEGAMMA())
		gx2Format |= Latte::E_GX2SURFFMT::FMT_BIT_SRGB;

	if (texUnitWord4.get_FORMAT_COMP_X() == Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N::E_FORMAT_COMP::COMP_SIGNED)
		gx2Format |= Latte::E_GX2SURFFMT::FMT_BIT_SIGNED;

	return gx2Format;
}

// ---- from Cemu src/Cafe/HW/Latte/Core/LatteShader.cpp
static void InitUniformLayoutFromDecompiler(
    LatteDecompilerShader* shader,
    const LatteDecompilerOutput_t& decompilerOutput
)
{
	if (g_renderer->GetType() == RendererAPI::OpenGL)
	{
		// hack - for OpenGL these are retrieved in _prepareSeparableUniforms()
		shader->uniform.count_uniformRegister = decompilerOutput.uniformOffsetsGL.count_uniformRegister;
		return;
	}
    const auto& offsets = decompilerOutput.uniformOffsetsVK;

    shader->uniform.loc_remapped = offsets.offset_remapped;
    shader->uniform.loc_uniformRegister = offsets.offset_uniformRegister;
    shader->uniform.count_uniformRegister = offsets.count_uniformRegister;
    shader->uniform.loc_windowSpaceToClipSpaceTransform = offsets.offset_windowSpaceToClipSpaceTransform;
    shader->uniform.loc_alphaTestRef = offsets.offset_alphaTestRef;
    shader->uniform.loc_pointSize = offsets.offset_pointSize;
    shader->uniform.loc_fragCoordScale = offsets.offset_fragCoordScale;
    for (sint32 t = 0; t < LATTE_NUM_MAX_TEX_UNITS; t++)
        shader->uniform.loc_framebufferFetchSize[t] = offsets.offset_framebufferFetchSize[t];
    for (sint32 t = 0; t < LATTE_NUM_MAX_TEX_UNITS; t++)
        shader->uniform.loc_texLodBias[t] = offsets.offset_texLodBias[t];

    // Texture scale uniforms
    shader->uniform.list_ufTexRescale.clear();
    for (sint32 t = 0; t < LATTE_NUM_MAX_TEX_UNITS; t++)
    {
        if (offsets.offset_texScale[t] >= 0)
        {
            LatteUniformTextureScaleEntry_t entry{};
            entry.texUnit = t;
            entry.uniformLocation = offsets.offset_texScale[t];
            shader->uniform.list_ufTexRescale.push_back(entry);
        }
    }

    shader->uniform.loc_verticesPerInstance = offsets.offset_verticesPerInstance;

    // Streamout buffers
    for (sint32 t = 0; t < LATTE_NUM_STREAMOUT_BUFFER; t++)
    {
        shader->uniform.loc_streamoutBufferBase[t] = offsets.offset_streamoutBufferBase[t];
    }

    shader->uniform.uniformRangeSize = offsets.offset_endOfBlock;
}

// resource mapping + uniform layout, as LatteShader_CreateShaderFromDecompilerOutput does for Metal
LatteDecompilerShader* FinishDecompiledShader(LatteDecompilerOutput_t& decompilerOutput)
{
	LatteDecompilerShader* shader = decompilerOutput.shader;
	shader->resourceMapping = g_renderer->GetType() == RendererAPI::Vulkan
        ? decompilerOutput.resourceMappingVK : decompilerOutput.resourceMappingMTL;
	shader->textureUnitMask2 = decompilerOutput.textureUnitMask;
	shader->streamoutBufferWriteMask = decompilerOutput.streamoutBufferWriteMask;
	shader->hasStreamoutBufferWrite = decompilerOutput.streamoutBufferWriteMask.any();
	InitUniformLayoutFromDecompiler(shader, decompilerOutput);
	return shader;
}
