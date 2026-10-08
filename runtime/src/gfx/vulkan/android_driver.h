#pragma once
#ifdef __ANDROID__
#include "loader.h"
#include "driver_store.h"
#include <SDL3/SDL.h>
namespace gfxvk::drivers {
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
void request_install();
}
#endif
