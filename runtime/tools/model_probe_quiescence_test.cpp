// SPDX-License-Identifier: MPL-2.0
// Links the actual production wrapper; each mode runs in a fresh process so its
// once-captured option and persistent remaining counter are exercised directly.
#include "runtime.h"
#include "model_probe.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
extern "C" void hook_027F4D5C(Cpu*);
namespace {
std::atomic<unsigned> original_calls{},logs{};
std::mutex gate;std::condition_variable changed;bool block_log=false,in_log=false,release_log=false;
void option(const char*value){
#ifdef _WIN32
 assert(_putenv_s("WWHD_PROBE_MODEL",value?value:"")==0);
#else
 assert(value?setenv("WWHD_PROBE_MODEL",value,1)==0:unsetenv("WWHD_PROBE_MODEL")==0);
#endif
}
void map_memory(){
#ifdef _WIN32
 assert(VirtualAlloc(PPC_MEM_BASE,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==PPC_MEM_BASE);
#else
 assert(mmap(PPC_MEM_BASE,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0)==PPC_MEM_BASE);
#endif
 std::memset(PPC_MEM_BASE,0,0x10000);
}
}
extern "C" void f_027F4D5C_orig(Cpu*c){++original_calls;c->r[4]=0xAABBCCDD;}
void log_msg(const char*,...){++logs;std::unique_lock lock(gate);if(block_log){in_log=true;changed.notify_all();changed.wait(lock,[]{return release_log;});}}
int main(int argc,char**argv){
 assert(argc==2);const std::string_view mode=argv[1];Cpu cpu{};cpu.r[3]=0x100;
 if(mode=="disabled"){
  option(nullptr);assert(model_probe::native_refresh_quiescent());
  // A later environment change cannot reactivate a permanently disabled probe.
  option("2");hook_027F4D5C(&cpu);assert(original_calls==1&&cpu.r[4]==0xAABBCCDD&&logs==0&&model_probe::native_refresh_quiescent());return 0;
 }
 assert(mode=="pending"||mode=="active");option("1");assert(!model_probe::native_refresh_quiescent());
 // Removing the option cannot conceal a pending once-captured native probe.
 option(nullptr);assert(!model_probe::native_refresh_quiescent());map_memory();
 if(mode=="pending"){
  hook_027F4D5C(&cpu);assert(original_calls==1&&logs==1&&cpu.r[4]==0xAABBCCDD&&model_probe::native_refresh_quiescent());
  hook_027F4D5C(&cpu);assert(original_calls==2&&logs==1&&model_probe::native_refresh_quiescent());return 0;
 }
 block_log=true;std::thread worker([&]{hook_027F4D5C(&cpu);});
 {std::unique_lock lock(gate);changed.wait(lock,[]{return in_log;});}
 // The last remaining probe has decremented to zero, but the actual wrapper
 // still holds its scan/output lock. Zero counter alone must never admit.
 assert(original_calls==1&&logs==1&&!model_probe::native_refresh_quiescent());
 {std::lock_guard lock(gate);release_log=true;}changed.notify_all();worker.join();
 assert(model_probe::native_refresh_quiescent());
 hook_027F4D5C(&cpu);assert(original_calls==2&&logs==1&&model_probe::native_refresh_quiescent());
}
