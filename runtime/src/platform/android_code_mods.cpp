#include "android_code_mods.h"
#include "../exception_report.h"
#include <SDL3/SDL.h>
#include <jni.h>

namespace host::android {
namespace {
// A static String method of the game's activity (WwhdActivity), on the calling (game) thread.
bool call(const char* name,const char* signature,const jvalue* args,std::string& out) {
    auto env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if(!env||env->PushLocalFrame(8)!=0){if(env)env->ExceptionClear();return false;}
    bool ok=false;
    auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    auto cls=activity?env->GetObjectClass(activity):nullptr;
    auto method=cls?env->GetStaticMethodID(cls,name,signature):nullptr;
    if(method&&!env->ExceptionCheck()) {
        auto result=static_cast<jstring>(env->CallStaticObjectMethodA(cls,method,args));
        if(result&&!env->ExceptionCheck()) {
            if(auto text=env->GetStringUTFChars(result,nullptr)){out=text;env->ReleaseStringUTFChars(result,text);ok=true;}
        }
    }
    if(env->ExceptionCheck())env->ExceptionClear();
    env->PopLocalFrame(nullptr);
    return ok;
}
}
std::string guest_build_tools() {
    std::string result;
    if(!call("guestBuildTools","()Ljava/lang/String;",nullptr,result))exception_report::raise("Code-mod tools are unavailable in this app");
    if(result.rfind("ok:",0)==0)return result.substr(3);
    exception_report::raise(result.rfind("error:",0)==0?result.substr(6):result);
}
std::string guest_module_cache() {
    const char* internal=SDL_GetAndroidInternalStoragePath();
    if(!internal||!*internal)exception_report::raise("The app's private storage is unavailable");
    return std::string(internal)+"/guest-modules";
}
std::string request_code_mod_build(bool enabled) {
    std::string result;jvalue arg;arg.z=enabled?JNI_TRUE:JNI_FALSE;
    if(!call("requestCodeModBuild","(Z)Ljava/lang/String;",&arg,result))return "Setup could not start from the game";
    return result;
}
std::string code_mod_build_status() {
    std::string result;
    if(!call("codeModBuildStatus","()Ljava/lang/String;",nullptr,result))return "{\"state\":\"failed\",\"error\":\"Cannot read setup progress\"}";
    return result;
}
}
