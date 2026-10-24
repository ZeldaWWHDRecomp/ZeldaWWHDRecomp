#pragma once
#ifdef __ANDROID__
#include <sys/system_properties.h>
#include <cstring>
struct ANativeWindow;
namespace quest_theater {
inline bool device() {
 char model[PROP_VALUE_MAX]{};
 __system_property_get("ro.product.model",model);
 return std::strncmp(model,"Quest",5)==0 || std::strncmp(model,"Meta Quest",10)==0;
}
#ifdef WWHD_QUEST_THEATER
// Called by the SDL host before creating its Vulkan surface. Only QuestActivity
// opts in. OpenXR owns the compositor thread; GX2 remains the Vulkan owner.
void initialize();
bool active();
bool visible();
ANativeWindow* window();
constexpr int width=1280, height=720;
// Publish the packing of the submitted image, not a requested future setting.
void presented(int stereoMode);
#else
inline void initialize() {}
inline bool active() { return false; }
inline bool visible() { return false; }
inline ANativeWindow* window() { return nullptr; }
inline void presented(int) {}
constexpr int width=1280, height=720;
#endif
}
#endif
