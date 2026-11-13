#include "gfx/vulkan/rendering_path.h"
#include <cassert>
#include <cstring>

int main() {
    using namespace gfxvk;
    constexpr uint32_t intel = 0x8086, amd = 0x1002;
    constexpr auto oldIntel = intel_windows_driver(101, 2115);
    static_assert(decode_intel_windows_driver(oldIntel)[0] == 101);
    static_assert(decode_intel_windows_driver(oldIntel)[1] == 2115);
    static_assert(decode_intel_windows_driver(intel_windows_driver(100, 16383))[1] == 16383);
    auto select = [](RenderingCapabilities caps, const char* env = nullptr) {
        return choose_rendering_path(caps, amd, 0, true, env, false).path;
    };
    assert(select({VK_API_VERSION_1_0, true, true}) == RenderingPath::None);
    assert(select({VK_API_VERSION_1_0, false, false}, "1") == RenderingPath::None);
    assert(select({VK_API_VERSION_1_1, false, false}) == RenderingPath::RenderPass);
    assert(select({VK_API_VERSION_1_2, false, false}) == RenderingPath::RenderPass); // #51
    assert(select({VK_API_VERSION_1_3, true, true}) == RenderingPath::Core);
    assert(select({VK_API_VERSION_1_3, false, true}) == RenderingPath::KHR);
    assert(select({VK_API_VERSION_1_3, false, false}) == RenderingPath::RenderPass);
    assert(select({VK_API_VERSION_1_2, true, true}) == RenderingPath::KHR); // Forced API cap.
    assert(select({VK_API_VERSION_1_1, false, true}) == RenderingPath::KHR);
    for (const auto caps : {RenderingCapabilities{VK_API_VERSION_1_1, false, false},
            RenderingCapabilities{VK_API_VERSION_1_2, false, true},
            RenderingCapabilities{VK_API_VERSION_1_3, true, true}})
        assert(select(caps, "1") == RenderingPath::RenderPass);
    assert(select({VK_API_VERSION_1_2, false, false}, "0") == RenderingPath::None);
    assert(select({VK_API_VERSION_1_3, true, true}, "0") == RenderingPath::Core);
    assert(select({VK_API_VERSION_1_1, false, true}, "0") == RenderingPath::KHR);
    for (const char* env : std::array<const char*, 5>{nullptr, "", "auto", "true", "10"}) {
        const auto choice = choose_rendering_path({VK_API_VERSION_1_3, true, true}, intel, oldIntel, true, env, false);
        assert(choice.path == RenderingPath::RenderPass); // #144, despite advertised support.
        assert(!std::strcmp(choice.reason, "driver rule: Intel Windows < 101.5000"));
    }
    const RenderingCapabilities core{VK_API_VERSION_1_3, true, true};
    assert(choose_rendering_path(core, intel, oldIntel, true, "0", false).path == RenderingPath::Core);
    assert(choose_rendering_path(core, intel, oldIntel, false, nullptr, false).path == RenderingPath::Core);
    assert(choose_rendering_path(core, amd, oldIntel, true, nullptr, false).path == RenderingPath::Core);
    for (auto version : {intel_windows_driver(100, 9999), intel_windows_driver(101, 2120),
            intel_windows_driver(101, 3111) /* #204 */, intel_windows_driver(101, 4999)})
        assert(choose_rendering_path(core, intel, version, true, nullptr, false).path == RenderingPath::RenderPass);
    for (auto version : {intel_windows_driver(101, 5000), intel_windows_driver(101, 6083),
            intel_windows_driver(102, 0)})
        assert(choose_rendering_path(core, intel, version, true, nullptr, false).path == RenderingPath::Core);
    // Platform default render passes (Android): chosen over dynamic rendering unless =0 forces it.
    auto android = [](RenderingCapabilities caps, const char* env = nullptr) {
        return choose_rendering_path(caps, amd, 0, false, env, true);
    };
    for (const auto caps : {RenderingCapabilities{VK_API_VERSION_1_1, false, false},
            RenderingCapabilities{VK_API_VERSION_1_1, false, true},
            RenderingCapabilities{VK_API_VERSION_1_3, true, true}}) {
        for (const char* env : std::array<const char*, 4>{nullptr, "", "auto", "1"})
            assert(android(caps, env).path == RenderingPath::RenderPass);
    }
    assert(!std::strcmp(android(core).reason, "platform default"));
    assert(!std::strcmp(android(core, "1").reason, "WWHD_VK_FORCE_RENDERPASS=1"));
    assert(android(core, "0").path == RenderingPath::Core);
    assert(android({VK_API_VERSION_1_1, false, true}, "0").path == RenderingPath::KHR);
    assert(android({VK_API_VERSION_1_1, false, false}, "0").path == RenderingPath::None);
    assert(android({VK_API_VERSION_1_0, true, true}).path == RenderingPath::None);
#ifdef __ANDROID__
    static_assert(kRenderPassesByDefault);
#else
    static_assert(!kRenderPassesByDefault);
#endif
}
