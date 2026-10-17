#ifdef __ANDROID__
#include "../../exception_report.h"
#include "android_driver.h"
#include "runtime.h"
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_vulkan.h>
#include <android/native_window.h>
#include <android/api-level.h>
#include <vulkan/vulkan_android.h>
#if WWHD_ADRENOTOOLS
#include <adrenotools/driver.h>
#endif
#include <dlfcn.h>
#include <jni.h>
#include <memory>
#include <algorithm>
#include <mutex>
#include <atomic>

namespace gfxvk::drivers {
namespace {
std::mutex mutex;
std::unique_ptr<Store> store;
std::string active_id, active_label="System driver", status, vulkan_identity;
std::atomic<bool> loss_noted{false};
void* driver_handle=nullptr; // process lifetime: do not unload while driver/SDL objects may exist
std::string jstring_text(JNIEnv* env,jstring value) {
    if(!value)return {};
    const char* utf=env->GetStringUTFChars(value,nullptr);
    std::string result=utf?utf:"";if(utf)env->ReleaseStringUTFChars(value,utf);return result;
}
std::string hook_directory() {
    auto env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!env||!activity)exception_report::raise("Android activity unavailable");
    auto cls=env->GetObjectClass(activity);auto method=env->GetMethodID(cls,"getApplicationInfo","()Landroid/content/pm/ApplicationInfo;");
    auto info=env->CallObjectMethod(activity,method);auto ic=env->GetObjectClass(info);
    auto field=env->GetFieldID(ic,"nativeLibraryDir","Ljava/lang/String;");auto value=static_cast<jstring>(env->GetObjectField(info,field));
    auto result=jstring_text(env,value);env->DeleteLocalRef(value);env->DeleteLocalRef(ic);env->DeleteLocalRef(info);env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);return result;
}
Store& storage() {
    if(!store) {
        const char* path=SDL_GetAndroidInternalStoragePath();
        if(!path)exception_report::raise("Android internal storage unavailable");
        store=std::make_unique<Store>(std::filesystem::path(path)/"gpu-drivers");
    }
    return *store;
}
}
void set_vulkan_identity(const std::string& identity) {std::lock_guard guard(mutex);vulkan_identity=identity;}
void note_device_lost() noexcept {
    if(loss_noted.exchange(true))return;
    try {
        std::lock_guard guard(mutex);
        storage().note_device_lost(active_label+(vulkan_identity.empty()?"":"; "+vulkan_identity),!active_id.empty());
        LOG("[vulkan driver] device lost; driver choice will be offered at next startup");
    }catch(...){LOG("[vulkan driver] could not persist device-loss recovery note");}
}
void offer_after_device_loss() {
    const char* hidden=getenv("WWHD_HIDDEN_WINDOWS");
    if(hidden&&*hidden&&strcmp(hidden,"0"))return;
    std::optional<DeviceLoss> loss;
    std::vector<Driver> choices;
    try {std::lock_guard guard(mutex);loss=storage().take_device_loss();if(loss){storage().recover_incomplete_probe();choices=storage().list();}}
    catch(const std::exception&){LOG("[vulkan driver] invalid/unavailable device-loss recovery note");return;}
    if(!loss)return;
    const std::string text="The GPU device was lost during the last run.\n\nDriver: "+loss->driver+
        "\n\nKeep the current selection, choose the system driver, or choose another installed Adreno driver. A driver ZIP can also be installed.";
    std::vector<std::string> labels{"Continue with current selection","System driver"};
    for(const auto& driver:choices)labels.push_back(driver.name+" "+driver.version);
    labels.push_back("Install GPU driver...");
    std::vector<SDL_MessageBoxButtonData> buttons;
    for(size_t i=0;i<labels.size();++i)buttons.push_back({i==0?SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT:0,int(i),labels[i].c_str()});
    SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING,nullptr,"GPU driver recovery",text.c_str(),int(buttons.size()),buttons.data(),nullptr};
    int selected=0;
    if(!SDL_ShowMessageBox(&box,&selected))return;
    try {
        if(selected==1)select("");
        else if(selected>=2&&size_t(selected)<choices.size()+2)select(choices[size_t(selected)-2].id);
        else if(selected==int(labels.size()-1)) {
            request_install(true);
            for(;;)SDL_Delay(1000); // Existing picker selects the driver and ends the process.
        }
        if(selected>=1&&size_t(selected)<choices.size()+2){std::lock_guard guard(mutex);status="Driver chosen for this start.";}
    }catch(const std::exception&) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,"GPU driver recovery",
            "The driver choice could not be saved. Continuing with the current selection.",nullptr);
    }
}
PFN_vkGetInstanceProcAddr open_custom() {
    std::lock_guard guard(mutex);
    try {
        auto& s=storage();
        if(s.start())status="A custom driver did not finish its 120-frame probe. Using the system driver.";
        if(s.selected().empty())return nullptr;
        auto list=s.list();auto item=std::find_if(list.begin(),list.end(),[&](const auto& d){return d.id==s.selected();});
        if(item==list.end()||item->min_api>android_get_device_api_level())exception_report::raise("Selected driver is unavailable or requires a newer Android version");
        auto hooks=hook_directory();auto dir=item->directory.string()+"/";
#if WWHD_ADRENOTOOLS
        driver_handle=adrenotools_open_libvulkan(RTLD_NOW|RTLD_LOCAL,ADRENOTOOLS_DRIVER_CUSTOM,nullptr,hooks.c_str(),dir.c_str(),item->library.c_str(),nullptr,nullptr);
#else
        (void)hooks;(void)dir;
        throw std::runtime_error("Custom drivers need an arm64 device; using the system driver");
#endif
        auto gipa=driver_handle?reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driver_handle,"vkGetInstanceProcAddr")):nullptr;
        if(!gipa)exception_report::raise("Custom driver could not load; using the system driver");
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
    auto result=create(instance,&info,allocator,surface);
    if(result==VK_ERROR_DEVICE_LOST)note_device_lost();
    return result==VK_SUCCESS || SDL_SetError("Custom driver Android surface creation failed");
}
void frame_done(){std::lock_guard guard(mutex);if(store&&!active_id.empty())try{store->rendered_frame();}catch(const std::exception& e){status=e.what();}}
std::vector<Driver> installed(){std::lock_guard guard(mutex);return storage().list();}
std::string selection(){std::lock_guard guard(mutex);return storage().selected();}
std::string active_name(){std::lock_guard guard(mutex);return active_label;}
std::string message(){std::lock_guard guard(mutex);return status;}
std::string pipeline_directory(){std::lock_guard guard(mutex);return storage().cache(active_id).string();}
void select(const std::string& id){std::lock_guard guard(mutex);storage().select(id);status="Driver selection saved. Restart the game to apply.";}
void remove(const std::string& id){std::lock_guard guard(mutex);if(id==active_id){status="Select the system driver and restart before removing the active driver";exception_report::raise(status);}storage().remove(id);status="Driver and its pipeline cache removed.";}
void request_install(bool afterFailure) {
    auto env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    auto cls=env->GetObjectClass(activity);auto method=env->GetMethodID(cls,"chooseGpuDriver","(Z)V");env->CallVoidMethod(activity,method,jboolean(afterFailure));env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
}
std::string install_file(const std::string& file,bool select) {
    std::lock_guard guard(mutex);
    try {
        auto id=storage().install(file,android_get_device_api_level());
        if(select)storage().select(id);
        status=select?"Driver installed and selected.":"Driver installed. Select it and restart to apply.";return "";
    }
    catch(const std::exception& e){status=e.what();return status;}
}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wwhdrecomp_wwhd_WwhdActivity_installGpuDriver(JNIEnv* env,jclass,jstring path,jboolean select) {
    const char* utf=env->GetStringUTFChars(path,nullptr);auto error=gfxvk::drivers::install_file(utf?utf:"",select);if(utf)env->ReleaseStringUTFChars(path,utf);return env->NewStringUTF(error.c_str());
}
#endif
