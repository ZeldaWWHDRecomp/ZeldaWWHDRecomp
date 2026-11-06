// Turnip's settings before a custom driver loads (gfx/vulkan/turnip_debug.h): no UBWC on Adreno 6xx, Turnip's own
// defaults elsewhere, nothing for other drivers, and a TU_DEBUG the player set wins.
#include "gfx/vulkan/turnip_debug.h"
#include <cassert>
int main() {
    using namespace gfxvk::drivers;
    // which packages are Turnip
    assert(is_turnip("libvulkan_freedreno.so", "", "Mesa Vulkan driver"));
    assert(is_turnip("vulkan.ad07xx.so", "Mesa", "Adreno driver"));
    assert(is_turnip("vulkan.ad07xx.so", "mesa", "x"));
    assert(is_turnip("libvulkan.so", "", "Turnip 26.1 (Mr. Purple)"));
    assert(!is_turnip("vulkan.adreno.so", "Qualcomm", "Qualcomm driver v819"));
    assert(!is_turnip("vulkan.adreno.so", "", ""));
    // Adreno 6xx: no UBWC
    for (const char* gpu : {"Adreno650v2", "Adreno650v3", "Adreno618v1", "Adreno660v1", "Adreno642Lv1", "Adreno690"})
        assert(tu_debug_for(gpu, true, false) == "noubwc");
    // Adreno 7xx/8xx (the Xiaomi 14's Adreno 750), older ones and an unknown GPU keep Turnip's defaults
    for (const char* gpu : {"Adreno750v2", "Adreno740v2", "Adreno830v2", "Adreno540v2", "", "Mali-G710", "Adreno 650"})
        assert(tu_debug_for(gpu, true, false).empty());
    // the player's TU_DEBUG wins; Qualcomm's own driver gets nothing
    assert(tu_debug_for("Adreno650v2", true, true).empty());
    assert(tu_debug_for("Adreno650v2", false, false).empty());
}
