#pragma once
#ifdef __ANDROID__
#include "loader.h"
#include "result_observer.h"
#include "driver_store.h"
#include <SDL3/SDL.h>
namespace gfxvk::drivers {
// Called before Vulkan startup, using the existing driver selection/install APIs.
void offer_after_device_loss();
void set_vulkan_identity(const std::string&);
void note_device_lost() noexcept;
template<typename Function> using ObservedFunction=detail::ResultFunction<Function,VkResult,VK_ERROR_DEVICE_LOST,note_device_lost>;
PFN_vkGetInstanceProcAddr open_custom();
bool create_surface(SDL_Window*,VkInstance,const VkAllocationCallbacks*,VkSurfaceKHR*);
void frame_done();
std::vector<Driver> installed();
std::string selection();
std::string active_name();
std::string message();
std::string pipeline_directory();
void select(const std::string&);
void remove(const std::string&);
// the Android file picker for a driver ZIP; afterFailure: the renderer could not start, so the
// driver is also selected and the app ends (it is used from the next start)
void request_install(bool afterFailure=false);
}
#endif
