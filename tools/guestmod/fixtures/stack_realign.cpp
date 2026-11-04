#undef NDEBUG
#include "wwhd_guest_abi.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif

static const WWHDGuestModuleV1* module;
static unsigned expected_mode, alignment_errors, callbacks;
static uint32_t entry_sp;
static void map_memory(uint32_t address, size_t size) {
#ifdef _WIN32
    assert(VirtualAlloc(ppc_ptr(address), size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE) == ppc_ptr(address));
#else
    assert(mmap(ppc_ptr(address), size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) == ppc_ptr(address));
#endif
}
static void dispatch(Cpu* c) {
    for (uint32_t i=0;i<module->func_count;++i)
        if(module->funcs[i].addr==c->pc){module->funcs[i].fn(c);return;}
    assert(c->pc==0x02000010);
    const uint32_t frame=c->r[3], mode=c->r[4], output=c->r[5];
    assert(mode==expected_mode&&frame>=entry_sp-0x1000&&frame+0x220<=entry_sp);
    alignment_errors += (frame&15)!=0;
    for(unsigned i=0;i<0x220/4;++i)assert(ld32(frame+4*i)==((mode<<16)^i));
    st32(frame+0x70,0x76543210^mode);
    st32(frame+0x130,0x13579BDF^mode);
    st32(output,frame);st32(output+4,mode^0xA55ACC33);
    ++callbacks;
    // Legal caller-save clobbers must not corrupt the guest frame/captures.
    c->r[0]=0xCC000000;
    for(unsigned i=3;i<13;++i)c->r[i]=0xCC000000+i;
}
static void guards() {
    for(unsigned i=0;i<64;++i){assert(ld8(entry_sp-0x1000+i)==0xA5);assert(ld8(entry_sp+0x100+i)==0xA5);}
}
static void check_preserved(const Cpu& c,uint32_t lr) {
    assert(c.r[1]==entry_sp&&c.lr==lr);
    for(unsigned i=14;i<32;++i)assert(c.r[i]==0xBEEF0000+i);
    guards();
}
int main(int argc,char** argv) {
    assert(argc==3);
#ifdef _WIN32
    HMODULE library=LoadLibraryA(argv[1]);assert(library);
    auto init=(WWHDGuestInitV1)GetProcAddress(library,WWHD_GUEST_INIT_SYMBOL);
#else
    void* library=dlopen(argv[1],RTLD_NOW);assert(library);
    auto init=(WWHDGuestInitV1)dlsym(library,WWHD_GUEST_INIT_SYMBOL);
#endif
    assert(init);volatile int preempt[3]={0};WWHDGuestHostV1 host={};
    host.size=sizeof(host);host.abi_version=WWHD_GUEST_ABI_VERSION;host.core_preempt=preempt;host.dispatch=dispatch;
    module=init(&host);assert(module&&module->hook_count==2);
    map_memory(0x20000000,0x20000);
    map_memory(module->mem_base,(module->mem_size+0xFFFF)&~0xFFFFu);
    memcpy(ppc_ptr(module->mem_base),module->image,module->image_size);
    for(expected_mode=0;expected_mode<128;++expected_mode) {
        memset(ppc_ptr(0x20000000),0xA5,0x20000);
        entry_sp=0x20010000+(expected_mode&64?8:0);
        Cpu c={};c.r[1]=entry_sp;c.r[3]=expected_mode;c.r[4]=entry_sp+0x80;c.lr=0xDEADC0DE;
        for(unsigned i=14;i<32;++i)c.r[i]=0xBEEF0000+i;
        for(uint32_t i=0;i<module->hook_count;++i)if(module->hooks[i].target==0x02000000)module->hooks[i].fn(&c);
        assert(c.r[3]==((expected_mode<<16)^0x76543210^0x13579BDF));
        assert(ld32(entry_sp+0x80)>=entry_sp-0x1000&&ld32(entry_sp+0x84)==(expected_mode^0xA55ACC33));
        check_preserved(c,0xDEADC0DE);
        c={};c.r[1]=entry_sp;c.lr=0xFEEDCAFE;
        for(unsigned i=0;i<8;++i)c.r[3+i]=0x100+i;
        for(unsigned i=14;i<32;++i)c.r[i]=0xBEEF0000+i;
        const uint32_t byref=entry_sp+0x80;
        st32(entry_sp+8,byref);st32(entry_sp+12,0x222);st32(entry_sp+16,0x333);st32(entry_sp+20,0x444);
        for(uint32_t i=0;i<module->hook_count;++i)if(module->hooks[i].target==0x02000004)module->hooks[i].fn(&c);
        assert((ld32(byref)&31)==0&&ld32(byref)>=entry_sp-0x1000&&ld32(byref)<entry_sp);
        assert(ld32(byref+4)==(0x100^0x101^0x102^0x103^0x104^0x105^0x106^0x107^0x222^0x333^0x444));
        const uint32_t sum=0x100+0x101+0x102+0x103+0x104+0x105+0x106+0x107;
        assert(c.r[3]==(sum^(sum+7)^0x222^0x333^0x444));
        check_preserved(c,0xFEEDCAFE);
    }
    assert(callbacks==128&&alignment_errors==unsigned(std::strtoul(argv[2],nullptr,10)));
    std::printf("128 native-entry frame + 128 stack/byref cases; alignment errors=%u\n",alignment_errors);
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
}
