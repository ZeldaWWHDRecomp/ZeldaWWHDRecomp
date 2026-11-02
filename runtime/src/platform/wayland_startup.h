#pragma once
namespace wayland_startup {
// Main thread, after SDL video init and before loading the Vulkan driver.
void configure();
}
