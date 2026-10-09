// Linux: load the system TLS transport only when a player requests a download.
#include "https.h"
#include "download_file.h"
#include "../mods/catalogue_schema.h"
#include <curl/curl.h>
#include <dlfcn.h>
#include <memory>

namespace host {
namespace {
struct Curl {
    void* library;
    decltype(&curl_global_init) global_init;
    decltype(&curl_easy_init) init;
    decltype(&curl_easy_setopt) option;
    decltype(&curl_easy_perform) perform;
    decltype(&curl_easy_getinfo) info;
    decltype(&curl_easy_cleanup) cleanup;
    template<class T>T symbol(const char* name) {
        auto result=reinterpret_cast<T>(dlsym(library,name));
        if(!result)throw std::runtime_error("System HTTPS transport is incomplete; install libcurl4");
        return result;
    }
    Curl() {
        library=dlopen("libcurl.so.4",RTLD_NOW|RTLD_LOCAL);
#ifdef __APPLE__
        // Allows the same transport to be exercised by developer tests on macOS.
        if(!library)library=dlopen("/usr/lib/libcurl.4.dylib",RTLD_NOW|RTLD_LOCAL);
#endif
        if(!library)throw std::runtime_error("HTTPS downloads need the system libcurl4 package");
        try {
            global_init=symbol<decltype(global_init)>("curl_global_init");
            init=symbol<decltype(init)>("curl_easy_init");option=symbol<decltype(option)>("curl_easy_setopt");
            perform=symbol<decltype(perform)>("curl_easy_perform");info=symbol<decltype(info)>("curl_easy_getinfo");
            cleanup=symbol<decltype(cleanup)>("curl_easy_cleanup");
            if(global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("Cannot initialize HTTPS transport");
        }catch(...){dlclose(library);throw;}
        // Retain the library for process lifetime; workers may still be exiting at shutdown.
    }
};
struct Transfer {
    DownloadFile file;
    std::string failure;
    Transfer(const std::filesystem::path& path,uint64_t limit):file(path,limit){}
    static size_t write(char* data,size_t size,size_t count,void* opaque) noexcept {
        auto& transfer=*static_cast<Transfer*>(opaque);
        try {
            if(size&&count>SIZE_MAX/size)throw std::runtime_error("Invalid HTTPS response size");
            transfer.file.append(data,size*count);return size*count;
        }catch(const std::exception& error){transfer.failure=error.what();return 0;}
        catch(...){return 0;}
    }
};
}
void download_https(const std::string& url,const std::filesystem::path& destination,uint64_t limit) {
    if(!mods::catalogue::https_url(url))throw std::runtime_error("Download URL must use HTTPS");
    static Curl curl;
    auto easy=curl.init();if(!easy)throw std::runtime_error("Cannot initialize HTTPS download");
    auto release=[&](CURL* handle){curl.cleanup(handle);};
    std::unique_ptr<CURL,decltype(release)> owner(easy,release);
    Transfer transfer(destination,limit);
    auto set=[&](CURLoption name,auto value){if(curl.option(easy,name,value)!=CURLE_OK)throw std::runtime_error("Cannot configure HTTPS download");};
    set(CURLOPT_URL,url.c_str());set(CURLOPT_FOLLOWLOCATION,1L);set(CURLOPT_MAXREDIRS,5L);
    // Bitmasks work with older runtime libcurl versions as well as current headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    set(CURLOPT_PROTOCOLS,long(CURLPROTO_HTTPS));set(CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTPS));
#pragma GCC diagnostic pop
    set(CURLOPT_SSL_VERIFYPEER,1L);set(CURLOPT_SSL_VERIFYHOST,2L);
    set(CURLOPT_CONNECTTIMEOUT,15L);set(CURLOPT_TIMEOUT,120L);set(CURLOPT_NOSIGNAL,1L);
    set(CURLOPT_FAILONERROR,1L);set(CURLOPT_WRITEFUNCTION,&Transfer::write);set(CURLOPT_WRITEDATA,&transfer);
    auto result=curl.perform(easy);
    if(!transfer.failure.empty())throw std::runtime_error(transfer.failure);
    long status=0;
    if(result!=CURLE_OK||curl.info(easy,CURLINFO_RESPONSE_CODE,&status)!=CURLE_OK||status!=200)
        throw std::runtime_error("HTTPS download failed; check the connection and try again");
    transfer.file.finish();
}
} // namespace host
