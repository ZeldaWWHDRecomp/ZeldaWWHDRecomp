#pragma once
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#ifdef __APPLE__
#include <mach-o/ldsyms.h>
#include <mach-o/dyld.h>
#include <climits>
#include <pthread/qos.h>
#elif !defined(_WIN32)
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
namespace host {
#ifdef _WIN32
inline std::string utf8(const std::wstring& value) {
 int count=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
 std::string result(count,0);if(count)WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),result.data(),count,nullptr,nullptr);return result;
}
inline std::string environment_utf8(const wchar_t* name) {
 DWORD count=GetEnvironmentVariableW(name,nullptr,0);if(!count)return {};
 std::wstring value(count,0);DWORD used=GetEnvironmentVariableW(name,value.data(),count);value.resize(used);return utf8(value);
}
#endif
#if defined(__APPLE__) && defined(WWHD_HAS_VULKAN)
// Render command batches can create Objective-C temporaries inside MoltenVK.
void with_autorelease_pool(void (*fn)());
void with_autorelease_pool(void (*fn)(void*), void* context);
#else
inline void with_autorelease_pool(void (*fn)()) { fn(); }
inline void with_autorelease_pool(void (*fn)(void*), void* context) { fn(context); }
#endif
// The native call is synchronous: captured references remain valid and no heap
// allocation or callable lifetime extension is needed. Exceptions propagate.
template<class Fn> void with_autorelease_pool(Fn&& fn) {
 auto call = [&] { fn(); };
 with_autorelease_pool([](void* context) {
  (*static_cast<decltype(call)*>(context))();
 }, &call);
}
inline thread_local std::string thread_label;
// Diagnostic only, and called first thing in thread entry functions where an
// escaping exception would terminate the process: never throws, and the OS name
// is set without heap allocation (an exhausted allocator only loses the label).
inline void set_thread_name(const char* name) noexcept {
 try { thread_label=name; } catch(...) { thread_label.clear(); }
#ifdef __APPLE__
 pthread_setname_np(name);
#elif defined(_WIN32)
 using SetDescription=HRESULT(WINAPI*)(HANDLE,PCWSTR);
 auto f=(SetDescription)GetProcAddress(GetModuleHandleW(L"Kernel32.dll"),"SetThreadDescription");
 if(f) {
  wchar_t text[256]; size_t n=0;
  for(;name[n] && n+1<sizeof text/sizeof *text;++n) text[n]=(unsigned char)name[n];
  text[n]=0; f(GetCurrentThread(),text);
 }
#else
 char text[16]; snprintf(text,sizeof text,"%s",name); // Linux limit: 15 bytes + NUL.
 pthread_setname_np(pthread_self(),text);
#endif
}
// Game and render threads: keep them on fast cores and ahead of background work. macOS: QoS
// user-interactive (the default QoS let macOS park them on efficiency cores). Windows: above-normal
// priority and no power throttling (hybrid P/E-core CPUs otherwise move busy threads to E-cores).
// Linux: a small nice boost where the process may raise priority (needs CAP_SYS_NICE; otherwise a
// logged failure). WWHD_NO_QOS=1 leaves the thread untouched on every platform.
inline void boost_thread_priority() {
 if(getenv("WWHD_NO_QOS")) return;
#ifdef __APPLE__
 const int error = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
 if(error) std::fprintf(stderr,"[thread priority] thread=%s operation=set_qos error=%d\n",thread_label.c_str(),error);
#elif defined(_WIN32)
 if(!SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL)) {
  const DWORD error=GetLastError();
  std::fprintf(stderr,"[thread priority] thread=%s operation=set_priority error=%lu\n",thread_label.c_str(),(unsigned long)error);
 }
 // SetThreadInformation(ThreadPowerThrottling) exists from Windows 10 1709; looked up at run time
 struct PowerThrottling { ULONG Version, ControlMask, StateMask; };
 using SetInfo=BOOL(WINAPI*)(HANDLE,int,LPVOID,DWORD);
 static const auto set_info=(SetInfo)GetProcAddress(GetModuleHandleW(L"Kernel32.dll"),"SetThreadInformation");
 if(set_info) {
  PowerThrottling state{1 /* THREAD_POWER_THROTTLING_CURRENT_VERSION */,1 /* EXECUTION_SPEED */,0 /* off */};
  if(!set_info(GetCurrentThread(),3 /* ThreadPowerThrottling */,&state,sizeof state)) {
   const DWORD error=GetLastError();
   std::fprintf(stderr,"[thread priority] thread=%s operation=disable_power_throttling error=%lu\n",thread_label.c_str(),(unsigned long)error);
  }
 } else std::fprintf(stderr,"[thread priority] thread=%s operation=disable_power_throttling unavailable\n",thread_label.c_str());
#else
 if(setpriority(PRIO_PROCESS,(id_t)syscall(SYS_gettid),-5)==-1) {
  const int error=errno;
  std::fprintf(stderr,"[thread priority] thread=%s operation=setpriority error=%d\n",thread_label.c_str(),error);
 }
#endif
}
// Background compile and prefetch threads: below the game, so its own threads always win. macOS: QoS utility.
// Windows: below-normal priority. Linux and Android: nice +10 for this thread (raising nice needs no privilege).
// WWHD_NO_QOS=1 leaves the thread untouched on every platform.
inline void lower_thread_priority() {
 if(getenv("WWHD_NO_QOS")) return;
#ifdef __APPLE__
 const int error = pthread_set_qos_class_self_np(QOS_CLASS_UTILITY,0);
 if(error) std::fprintf(stderr,"[thread priority] thread=%s operation=lower_qos error=%d\n",thread_label.c_str(),error);
#elif defined(_WIN32)
 if(!SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL)) {
  const DWORD error=GetLastError();
  std::fprintf(stderr,"[thread priority] thread=%s operation=lower_priority error=%lu\n",thread_label.c_str(),(unsigned long)error);
 }
#else
 if(setpriority(PRIO_PROCESS,(id_t)syscall(SYS_gettid),10)==-1) {
  const int error=errno;
  std::fprintf(stderr,"[thread priority] thread=%s operation=lower_setpriority error=%d\n",thread_label.c_str(),error);
 }
#endif
}
inline void get_thread_name(char* out,size_t size) {
 if(!size)return;
#ifdef __APPLE__
 pthread_getname_np(pthread_self(),out,size);
#else
 snprintf(out,size,"%s",thread_label.empty()?"host":thread_label.c_str());
#endif
}
inline uintptr_t executable_base() {
#ifdef __APPLE__
 return (uintptr_t)&_mh_execute_header;
#elif defined(_WIN32)
 return (uintptr_t)GetModuleHandleW(nullptr);
#else
 static int anchor;
 Dl_info info{}; return dladdr(&anchor,&info)?(uintptr_t)info.dli_fbase:0;
#endif
}
inline std::string executable_path() {
#ifdef _WIN32
 wchar_t path[32768]; DWORD n=GetModuleFileNameW(nullptr,path,32768);return utf8(std::wstring(path,n));
#elif !defined(__APPLE__)
 char path[4096];ssize_t n=readlink("/proc/self/exe",path,sizeof path);return n>0?std::string(path,n):std::string();
#else
 uint32_t n=0;_NSGetExecutablePath(nullptr,&n);
 std::vector<char> path(n);
 if(!n||_NSGetExecutablePath(path.data(),&n))return {};
 std::error_code ec;auto resolved=std::filesystem::canonical(path.data(),ec);
 return ec?std::string(path.data()):resolved.string();
#endif
}
inline size_t page_size() {
#ifdef _WIN32
 SYSTEM_INFO info;GetSystemInfo(&info);return info.dwPageSize;
#else
 return (size_t)getpagesize();
#endif
}
inline bool memory_touched(void* p,size_t bytes) {
#ifdef _WIN32
 // MEM_WRITE_WATCH records written pages even after paging them out. Residency alone
 // cannot distinguish untouched zero pages from modified pages in the swap file.
 size_t n=(bytes+page_size()-1)/page_size(); std::vector<void*> pages(n);
 ULONG_PTR count=n; DWORD granularity=0;
 if(GetWriteWatch(0,p,bytes,pages.data(),&count,&granularity)!=0)return true;
 return count!=0;
#elif defined(__APPLE__)
 std::vector<char> pages((bytes+page_size()-1)/page_size());
 if(mincore(p,bytes,pages.data()))return true;
 for(auto page:pages)if(page!=0)return true;
 return false;
#else
 // Linux mincore reports residency, not whether a swapped-out page contains data.
 // Scan the mapped region to preserve every byte; this is slower than macOS/Windows.
 (void)p;(void)bytes;return true;
#endif
}
inline bool replace_file(const std::string& from,const std::string& to) {
#ifdef _WIN32
 return MoveFileExW(std::filesystem::u8path(from).c_str(),std::filesystem::u8path(to).c_str(),
                    MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 return rename(from.c_str(),to.c_str())==0;
#endif
}
// Portable mode (release packages): a file "portable.txt" next to the executable keeps every
// per-user file (settings, controls, save states, shader caches) in "user" next to the executable's
// folder (<folder>/bin/wwhd -> <folder>/user) instead of the user's Library / AppData / .config.
// Without the marker (source builds) nothing changes.
inline std::string exe_dir() {
 static const std::string dir=[]{
  std::string p;
#if defined(__APPLE__)
  char buf[4096]; uint32_t n=sizeof buf;
  if(_NSGetExecutablePath(buf,&n)==0){ char real[PATH_MAX]; p=realpath(buf,real)?real:buf; }
#elif defined(_WIN32)
  p=executable_path();
#else
  char buf[4096]; ssize_t n=readlink("/proc/self/exe",buf,sizeof buf-1); if(n>0) p.assign(buf,(size_t)n);
#endif
  size_t s=p.find_last_of("/\\");
  return s==std::string::npos?std::string():p.substr(0,s);
 }();
 return dir;
}
inline const std::string& portable_user_dir() {
 static const std::string dir=[]{
#ifdef _WIN32
  const auto override=environment_utf8(L"WWHD_USER_DIR");if(!override.empty())return override;
#else
  if(const char* override=std::getenv("WWHD_USER_DIR"))return std::string(override);
#endif
  std::string e=exe_dir();
  if(e.empty()) return std::string();
#ifdef _WIN32
  FILE* f=_wfopen(std::filesystem::u8path(e+"/portable.txt").c_str(),L"rb");
#else
  FILE* f=fopen((e+"/portable.txt").c_str(),"rb");
#endif
  if(!f) return std::string();
  fclose(f);
  size_t s=e.find_last_of("/\\");
  return (s==std::string::npos?e:e.substr(0,s))+"/user";
 }();
 return dir;
}
inline bool portable() { return !portable_user_dir().empty(); }
// Shared Linux/Android XDG resolution; Android supplies its external app config root.
inline std::string xdg_config_dir(const char* xdg, const char* home) {
 if(xdg)return std::string(xdg)+"/wwhd";
 return std::string(home?home:".")+"/.config/wwhd";
}
inline std::string config_dir() {
#ifdef _WIN32
 const auto override=environment_utf8(L"WWHD_USER_DIR");if(!override.empty())return override;
#else
 if(const char* override=getenv("WWHD_USER_DIR"))return override;
#endif
 if(portable()) return portable_user_dir();
#ifdef __APPLE__
 const char* home=getenv("HOME");return std::string(home?home:".")+"/Library/Application Support/WWHD";
#elif defined(_WIN32)
 const auto root=environment_utf8(L"APPDATA");return (root.empty()?std::string("."):root)+"/WWHD";
#else
 return xdg_config_dir(getenv("XDG_CONFIG_HOME"),getenv("HOME"));
#endif
}
}
