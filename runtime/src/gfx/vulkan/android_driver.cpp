#ifdef __ANDROID__
#include "android_driver.h"
#include "runtime.h"
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_vulkan.h>
#include <android/native_window.h>
#include <android/api-level.h>
#include <vulkan/vulkan_android.h>
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <jni.h>
#include <memory>
#include <algorithm>
#include <mutex>

namespace gfxvk::drivers {
namespace {
std::mutex mutex;
std::unique_ptr<Store> store;
std::string active_id, active_label="System driver", status;
void* driver_handle=nullptr; // process lifetime: do not unload while driver/SDL objects may exist
std::string jstring_text(JNIEnv* env,jstring value) {
    if(!value)return {};
    const char* utf=env->GetStringUTFChars(value,nullptr);
    std::string result=utf?utf:"";if(utf)env->ReleaseStringUTFChars(value,utf);return result;
}
std::string hook_directory() {
    auto env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!env||!activity)throw std::runtime_error("Android activity unavailable");
    auto cls=env->GetObjectClass(activity);auto method=env->GetMethodID(cls,"getApplicationInfo","()Landroid/content/pm/ApplicationInfo;");
    auto info=env->CallObjectMethod(activity,method);auto ic=env->GetObjectClass(info);
    auto field=env->GetFieldID(ic,"nativeLibraryDir","Ljava/lang/String;");auto value=static_cast<jstring>(env->GetObjectField(info,field));
    auto result=jstring_text(env,value);env->DeleteLocalRef(value);env->DeleteLocalRef(ic);env->DeleteLocalRef(info);env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);return result;
}
Store& storage() {
    if(!store) {
        const char* path=SDL_GetAndroidInternalStoragePath();
        if(!path)throw std::runtime_error("Android internal storage unavailable");
        store=std::make_unique<Store>(std::filesystem::path(path)/"gpu-drivers");
    }
    return *store;
}
}
PFN_vkGetInstanceProcAddr open_custom() {
    std::lock_guard guard(mutex);
    try {
        auto& s=storage();
        if(s.start())status="A custom driver did not finish its 120-frame probe. Using the system driver.";
        if(s.selected().empty())return nullptr;
        auto list=s.list();auto item=std::find_if(list.begin(),list.end(),[&](const auto& d){return d.id==s.selected();});
        if(item==list.end()||item->min_api>android_get_device_api_level())throw std::runtime_error("Selected driver is unavailable or requires a newer Android version");
        auto hooks=hook_directory();auto dir=item->directory.string()+"/";
        driver_handle=adrenotools_open_libvulkan(RTLD_NOW|RTLD_LOCAL,ADRENOTOOLS_DRIVER_CUSTOM,nullptr,hooks.c_str(),dir.c_str(),item->library.c_str(),nullptr,nullptr);
        auto gipa=driver_handle?reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driver_handle,"vkGetInstanceProcAddr")):nullptr;
        if(!gipa)throw std::runtime_error("Custom driver could not load; using the system driver");
        active_id=item->id;active_label=item->name+" "+item->version;
        LOG("[vulkan driver] probing %s for 120 rendered frames",active_label.c_str());return gipa;
    }catch(const std::exception& e){status=e.what();if(store)store->failed();LOG("[vulkan driver] %s",status.c_str());return nullptr;}
}
bool create_surface(SDL_Window* window,VkInstance instance,const VkAllocationCallbacks* allocator,VkSurfaceKHR* surface) {
    if(!driver_handle||active_id.empty())return SDL_Vulkan_CreateSurface(window,instance,allocator,surface);
    auto create=reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(vkGetInstanceProcAddr(instance,"vkCreateAndroidSurfaceKHR"));
    auto native=static_cast<ANativeWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,nullptr));
    if(!create||!native)return SDL_SetError("Custom driver Android surface entry point/window unavailable");
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};info.window=native;
    return create(instance,&info,allocator,surface)==VK_SUCCESS || SDL_SetError("Custom driver Android surface creation failed");
}
void frame_done(){std::lock_guard guard(mutex);if(store&&!active_id.empty())try{store->rendered_frame();}catch(const std::exception& e){status=e.what();}}
std::vector<Driver> installed(){std::lock_guard guard(mutex);return storage().list();}
std::string selection(){std::lock_guard guard(mutex);return storage().selected();}
std::string active_name(){std::lock_guard guard(mutex);return active_label;}
std::string message(){std::lock_guard guard(mutex);return status;}
std::string pipeline_directory(){std::lock_guard guard(mutex);return storage().cache(active_id).string();}
void select(const std::string& id){std::lock_guard guard(mutex);storage().select(id);status="Driver selection saved. Restart the game to apply.";}
void remove(const std::string& id){std::lock_guard guard(mutex);if(id==active_id){status="Select the system driver and restart before removing the active driver";throw std::runtime_error(status);}storage().remove(id);status="Driver and its pipeline cache removed.";}
void request_install() {
    auto env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    auto cls=env->GetObjectClass(activity);auto method=env->GetMethodID(cls,"chooseGpuDriver","()V");env->CallVoidMethod(activity,method);env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
}
std::string install_file(const std::string& file) {
    std::lock_guard guard(mutex);
    try {storage().install(file,android_get_device_api_level());status="Driver installed. Select it and restart to apply.";return "";}
    catch(const std::exception& e){status=e.what();return status;}
}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wwhdrecomp_wwhd_WwhdActivity_installGpuDriver(JNIEnv* env,jclass,jstring path) {
    const char* utf=env->GetStringUTFChars(path,nullptr);auto error=gfxvk::drivers::install_file(utf?utf:"");if(utf)env->ReleaseStringUTFChars(path,utf);return env->NewStringUTF(error.c_str());
}
#endif
