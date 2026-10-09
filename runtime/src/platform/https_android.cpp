#include "https.h"
#include "../mods/catalogue_schema.h"
#include <SDL3/SDL.h>
#include <jni.h>

namespace host {
void download_https(const std::string& url,const std::filesystem::path& destination,uint64_t limit) {
    if(!mods::catalogue::https_url(url))throw std::runtime_error("Download URL must use HTTPS");
    if(!limit||limit>INT64_MAX)throw std::runtime_error("Invalid download size limit");
    auto env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if(!env)throw std::runtime_error("Android HTTPS transport is unavailable");
    if(env->PushLocalFrame(8)!=0){env->ExceptionClear();throw std::runtime_error("Android HTTPS transport is unavailable");}
    std::string failure="Android HTTPS transport is unavailable";
    auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(activity) {
        auto cls=env->GetObjectClass(activity);
        auto method=cls?env->GetStaticMethodID(cls,"downloadCatalogue","(Ljava/lang/String;Ljava/lang/String;J)Ljava/lang/String;"):nullptr;
        auto address=env->NewStringUTF(url.c_str()),path=env->NewStringUTF(destination.string().c_str());
        if(method&&address&&path&&!env->ExceptionCheck()) {
            auto result=static_cast<jstring>(env->CallStaticObjectMethod(cls,method,address,path,jlong(limit)));
            if(result&&!env->ExceptionCheck()) {
                auto text=env->GetStringUTFChars(result,nullptr);
                if(text){failure=text;env->ReleaseStringUTFChars(result,text);}
            }
        }
    }
    if(env->ExceptionCheck())env->ExceptionClear();
    env->PopLocalFrame(nullptr);
    if(!failure.empty())throw std::runtime_error(failure);
}
}
