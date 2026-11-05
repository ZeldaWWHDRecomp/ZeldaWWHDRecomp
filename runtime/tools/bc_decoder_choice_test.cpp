// Which decoder a device without BC textures uses (gfx/vulkan/bc_decoder_choice.h): the CPU decoder for
// Qualcomm's proprietary drivers before v700 and Huawei GPUs, the compute shader elsewhere, WWHD_VK_BC_CPU=0/1 overrides.
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
    // Huawei (Maleoon, vendor 0x19E5): the CPU decoder whatever the driver reports; WWHD_VK_BC_CPU=0 tries the shader
    const uint32_t huawei = 0x19E5;
    for (VkDriverId d : {VkDriverId(0), VK_DRIVER_ID_ARM_PROPRIETARY, VK_DRIVER_ID_MESA_TURNIP})
        for (const char* v : {(const char*)nullptr, "", "auto"}) {
            auto choice = choose_bc_decoder(d, huawei, 1, v);
            assert(choice.decoder == BcDecoder::Cpu && !std::strcmp(choice.reason, "driver rule: Huawei GPU"));
        }
    assert(choose_bc_decoder(VkDriverId(0), huawei, 1, "0").decoder == BcDecoder::Compute);
    // the Xiaomi 14's stock driver (Adreno 750, v762 era), Apple and Arm keep the compute shader
    assert(choose_bc_decoder(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, qualcomm, (512u << 22) | (762u << 12), nullptr).decoder == BcDecoder::Compute);
    assert(choose_bc_decoder(VK_DRIVER_ID_MOLTENVK, 0x106b, 0, nullptr).decoder == BcDecoder::Compute);
    assert(choose_bc_decoder(VK_DRIVER_ID_ARM_PROPRIETARY, 0x13b5, 0, nullptr).decoder == BcDecoder::Compute);
}
