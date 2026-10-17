#pragma once
#include <vulkan/vulkan.h>
#include <cstring>
#include "serial_pipelines.h"

namespace gfxvk {
// Devices without BC textures get the game's BC1-BC5 textures decoded to RGBA8 at upload
// (bc_decode.cpp): by a compute shader, or on the CPU (bc_reference.h) where a driver rule says so.
enum class BcDecoder { Compute, Cpu };
struct BcDecoderChoice {
    BcDecoder decoder = BcDecoder::Compute;
    const char* reason = "compute shader";
};
// Qualcomm's proprietary Android drivers before v700 (Snapdragon 855/865/870 stock firmware,
// e.g. 512.502 on an Adreno 650) have no BC textures, and the compute decoder's output came out
// as vertical stripes and checkerboards there while Mesa Turnip on the same phone was correct.
// The CPU decoder needs no shader at all. Driver id 0 with Qualcomm's vendor id is a driver
// without VK_KHR_driver_properties, i.e. an old proprietary one (Turnip reports its id).
// WWHD_VK_BC_CPU=1 / 0 forces the CPU / compute decoder; other values keep the rule.
inline BcDecoderChoice choose_bc_decoder(VkDriverId driver, uint32_t vendor, uint32_t version,
                                         const char* overrideValue) {
    if (overrideValue && !std::strcmp(overrideValue, "1")) return {BcDecoder::Cpu, "WWHD_VK_BC_CPU=1"};
    if (overrideValue && !std::strcmp(overrideValue, "0")) return {BcDecoder::Compute, "WWHD_VK_BC_CPU=0"};
    const bool qualcomm = driver == VK_DRIVER_ID_QUALCOMM_PROPRIETARY || (driver == VkDriverId(0) && vendor == 0x5143);
    if (qualcomm && qualcomm_driver_release(version) < 700)
        return {BcDecoder::Cpu, "driver rule: Qualcomm proprietary < v700"};
    return {};
}
} // namespace gfxvk
