// SPDX-License-Identifier: MPL-2.0
// The heap diagnostics' heap queries: a heap getter that returns no heap (null, or a pointer outside the
// game's memory), a heap without a vtable and a vtable without the method are 0 answers, never a read
// of guest address 0 or a call through a null method.
#include "mods/guest_heap_query.h"
#include "runtime.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Page=0x20000000,Heap=Page+0x100,Vtable=Page+0x400,FreeFn=0x02000100,SizeFn=0x02000200,Unmapped=0x30000000;
unsigned calls=0;uint32_t last_heap=0;
uint32_t call(Cpu*,uint32_t fn,std::initializer_list<uint32_t> args){++calls;assert(args.size()==1);last_heap=*args.begin();return fn==FreeFn?4096u:fn==SizeFn?65536u:0xBADu;}
}
int main(){
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(Page),0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(Page),0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
    Cpu c{};
    // no heap at all: a null getter answer is never read (the crash was a read of guest address 0)
    assert(guest_heap::free_size(&c,0,call)==0&&guest_heap::size(&c,0,call)==0&&calls==0);
    // pointers outside the game's memory or unaligned are no heap either
    for(uint32_t bad:{0x00000010u,0x0FFFFFF0u,0x50000000u,0xFFFFFFF0u,Heap+2})assert(guest_heap::free_size(&c,bad,call)==0&&calls==0);
    (void)Unmapped;
    // a heap whose vtable is missing or not in the game's memory
    st32(Heap+0xC,0);assert(guest_heap::free_size(&c,Heap,call)==0&&calls==0);
    st32(Heap+0xC,0x00000400u);assert(guest_heap::free_size(&c,Heap,call)==0&&calls==0);
    // a vtable without the method
    st32(Heap+0xC,Vtable);st32(Vtable+0x74,0);st32(Vtable+0x6C,0);assert(guest_heap::free_size(&c,Heap,call)==0&&guest_heap::size(&c,Heap,call)==0&&calls==0);
    // a real heap: its own getFreeSize / getSize, called on it
    st32(Vtable+0x74,FreeFn);st32(Vtable+0x6C,SizeFn);
    assert(guest_heap::free_size(&c,Heap,call)==4096u&&last_heap==Heap&&calls==1);
    assert(guest_heap::size(&c,Heap,call)==65536u&&calls==2);
    // no call function or no Cpu: nothing is called
    assert(guest_heap::free_size(&c,Heap,nullptr)==0&&guest_heap::free_size(nullptr,Heap,call)==0&&calls==2);
    return 0;
}
