// Which decoder a device without BC textures uses (gfx/vulkan/bc_decoder_choice.h): the CPU decoder for
// Qualcomm's proprietary drivers before v700, the compute shader elsewhere, WWHD_VK_BC_CPU=0/1 overrides.
#include "gfx/vulkan/bc_decoder_choice.h"
#include <cassert>
#include <initializer_list>
#include <cstring>
int main() {
    using namespace gfxvk;
    const uint32_t qualcomm = 0x5143;
    struct Case { uint32_t version; bool cpu; };
    // 512.502.0: the OnePlus 8 Pro (Adreno 650) stock driver whose compute output was striped
    for (Case c : {Case{(512u << 22) | (502u << 12), true}, Case{(512u << 22) | (615u << 12) | 3, true},
                   Case{(512u << 22) | (699u << 12), true}, Case{(512u << 22) | (700u << 12), false},
                   Case{(512u << 22) | (762u << 12) | 9, false}, Case{(512u << 22) | (800u << 12), false}}) {
        auto choice = choose_bc_decoder(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, qualcomm, c.version, nullptr);
        assert((choice.decoder == BcDecoder::Cpu) == c.cpu);
        assert(!std::strncmp(choice.reason, c.cpu ? "driver rule: " : "compute", c.cpu ? 13 : 7));
        // without VK_KHR_driver_properties (driver id 0) Qualcomm's vendor id stands for the proprietary driver
        assert((choose_bc_decoder(VkDriverId(0), qualcomm, c.version, "") .decoder == BcDecoder::Cpu) == c.cpu);
        // Turnip and other drivers on the same numbers keep the compute shader
        for (VkDriverId other : {VK_DRIVER_ID_MESA_TURNIP, VK_DRIVER_ID_ARM_PROPRIETARY, VK_DRIVER_ID_IMAGINATION_PROPRIETARY,
                                 VK_DRIVER_ID_MOLTENVK, VK_DRIVER_ID_NVIDIA_PROPRIETARY})
            assert(choose_bc_decoder(other, qualcomm, c.version, nullptr).decoder == BcDecoder::Compute);
        assert(choose_bc_decoder(VkDriverId(0), 0x13b5, c.version, nullptr).decoder == BcDecoder::Compute);  // Arm
        // the override wins both ways; other values keep the rule
        assert(choose_bc_decoder(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, qualcomm, c.version, "0").decoder == BcDecoder::Compute);
        assert(choose_bc_decoder(VK_DRIVER_ID_MESA_TURNIP, qualcomm, c.version, "1").decoder == BcDecoder::Cpu);
        assert((choose_bc_decoder(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, qualcomm, c.version, "auto").decoder == BcDecoder::Cpu) == c.cpu);
    }
    assert(!std::strcmp(choose_bc_decoder(VK_DRIVER_ID_MOLTENVK, 0x106b, 0, "1").reason, "WWHD_VK_BC_CPU=1"));
}
