#pragma once
#include "backend.h"
namespace gfxvk {
bool bc_decode_required(const FormatInfo&);
void bc_decode_upload(Surface*, uint32_t level, const std::vector<uint8_t>& blocks, uint32_t w, uint32_t h, uint32_t slices);
void bc_decode_shutdown();
void bc_decode_smoke();
}
