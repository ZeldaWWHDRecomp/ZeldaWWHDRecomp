#pragma once

namespace wayland_policy {
// Only NVIDIA reads this variable. Set it before loading Vulkan, since the selected GPU is
// not known yet. Preserve explicit user choices, including an empty value.
constexpr bool disable_nvidia_explicit_sync(bool wayland, bool user_override) {
    return wayland && !user_override;
}
}  // namespace wayland_policy
