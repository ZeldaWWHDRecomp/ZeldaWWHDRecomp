#include "crash_memory.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <new>
#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
static std::string output;
static void collect(int,const char* s,size_t n){output.append(s,n);}
static std::vector<crash_memory::Section> fixture(){
 return {{0x02000000,6,std::vector<uint8_t>(256,0xA5)},
         {0x10000000,3,std::vector<uint8_t>(256,0x53)},
         {0x03000000,6,std::vector<uint8_t>(256,0x42)},
         {0x03000080,3,{},256}, // overlap invalidates trusted executable window
         {0x04000000,7,std::vector<uint8_t>(256,0x54)}};
}
#ifndef _WIN32
static volatile sig_atomic_t in_signal=0;
void* operator new(std::size_t n){if(in_signal)_exit(8);if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
static int signal_fd;
static uint32_t signal_regs[32];
static void raw(int fd,const char* s,size_t n){while(n){auto w=write(fd,s,n);if(w<=0)_exit(3);s+=w;n-=size_t(w);}}
static void handler(int){in_signal=1;crash_memory::note(signal_fd,raw,signal_regs);_exit(0);}
#endif
int main(int argc,char** argv){
 const bool on=argc>1 && !std::strcmp(argv[1],"on");
#ifdef _WIN32
 _putenv_s("WWHD_CRASH_GUEST_MEMORY",on?"1":"0");
#else
 setenv("WWHD_CRASH_GUEST_MEMORY",on?"1":"0",1);
#endif
 // Only this code page is readable: all rejected windows must avoid reads.
#ifndef _WIN32
 auto* arena=static_cast<uint8_t*>(mmap(nullptr,size_t(1)<<32,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(arena!=MAP_FAILED);assert(mprotect(arena+0x02000000,4096,PROT_READ|PROT_WRITE)==0);
 // An original input mapping can disappear while the guest arena remains valid.
 void* input=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(input!=MAP_FAILED);
 std::memset(input,0xA5,256);
#else
 std::vector<uint8_t> storage(0x02001000);auto* arena=storage.data();
#endif
 auto sections=fixture();
#ifndef _WIN32
 sections[0].bytes.assign(static_cast<uint8_t*>(input),static_cast<uint8_t*>(input)+256);
 assert(munmap(input,4096)==0);
#endif
 std::memset(arena+0x02000000,0xA5,256);
 crash_memory::install(sections,arena);
 // Input mutation must never alter the immutable published original image.
 sections[0].bytes.assign(256,0x50);
 uint32_t registers[32]{};registers[0]=0x02000000;registers[1]=0x02000080;
 registers[2]=0x02000081;registers[3]=0x10000000;registers[4]=0xFFFFFFFF;
 registers[5]=0x03000000;registers[6]=0x04000000;registers[7]=0x123;
 registers[8]=0x60000000;registers[9]=0xE0000000;registers[10]=0xF4000000;
 crash_memory::refresh(7,0x60001000);crash_memory::note(-1,collect,registers);
 if(!on){assert(output.empty());return 0;}
 assert(output.find("current-verified baseline")!=std::string::npos);
 assert(output.find("A5A5A5A5")!=std::string::npos);
 assert(output.find("50505050")==std::string::npos);assert(output.find("53535353")==std::string::npos);
 assert(output.find("42424242")==std::string::npos);assert(output.find("54545454")==std::string::npos);
 assert(output.find("address 02000081: <redacted")!=std::string::npos);
 assert(output.find("address FFFFFFFF: <redacted")!=std::string::npos);
 for(const char* address:{"10000000","60000000","E0000000","F4000000"})assert(output.find(std::string("address ")+address+": <redacted")!=std::string::npos);
 assert(output.find("registered_guest_threads=7")!=std::string::npos);
 assert(output.size()<16000);
 // A synthetic secret written into code must redact the WHOLE window: no
 // unchanged prefix, changed bytes or secret-dependent hash is emitted.
 const std::string unchanged=output;std::memset(arena+0x02000000,0x53,256);
 output.clear();crash_memory::note(-1,collect,registers);
 assert(output.find("modified code")!=std::string::npos);
 assert(output.find("A5A5A5A5")==std::string::npos);
 assert(output.find("53535353")==std::string::npos);
 std::memset(arena+0x02000000,0xA5,256);output=unchanged;
#ifndef _WIN32
 int fds[2];assert(pipe(fds)==0);pid_t child=fork();assert(child>=0);
 if(child==0){close(fds[0]);signal_fd=fds[1];std::memcpy(signal_regs,registers,sizeof registers);struct sigaction action{};action.sa_handler=handler;sigemptyset(&action.sa_mask);assert(sigaction(SIGUSR1,&action,nullptr)==0);raise(SIGUSR1);_exit(4);}
 close(fds[1]);std::string received;char buffer[512];ssize_t n;
 while((n=read(fds[0],buffer,sizeof buffer))>0)received.append(buffer,size_t(n));close(fds[0]);int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);assert(received==output);
 // No live publication must not read even a valid baseline window.
 crash_memory::install(fixture(),nullptr);output.clear();crash_memory::note(-1,collect,registers);
 assert(output.find("current code unavailable")!=std::string::npos);
 assert(output.find("A5A5A5A5")==std::string::npos);
 assert(munmap(arena,size_t(1)<<32)==0);
#endif
}
