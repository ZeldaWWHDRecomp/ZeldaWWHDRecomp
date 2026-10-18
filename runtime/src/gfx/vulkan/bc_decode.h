#pragma once
#include "backend.h"
namespace gfxvk {
bool bc_decode_required(const FormatInfo&);
void bc_decode_upload(Surface*, uint32_t level, const std::vector<uint8_t>& blocks, uint32_t w, uint32_t h, uint32_t slices);
// device creation: on devices that decode BC uploads with the compute shader, check the shader against
// the CPU decoder on known-answer blocks; on a difference use the CPU decoder (WWHD_VK_BC_CPU=0 keeps compute)
void bc_decode_startup_check();
void bc_decode_shutdown();
void bc_decode_smoke();
}
