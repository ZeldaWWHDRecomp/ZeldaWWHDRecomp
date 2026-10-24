// Exercise the real hook with modelled guest calls: only input/focus APIs may
// write memory. A disabled, unavailable or unrelated prompt remains untouched.
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
extern "C" void site_026D44A8(Cpu*);
extern "C" void hook_026D50C4(Cpu*);
extern "C" void hook_026F8B54(Cpu*);
namespace {
constexpr uint32_t owner=0x11000000, screen=0x11001000, children=0x11002000,
    first=0x11003000, second=0x11004000, receiver=0x11005000,
    background=0x1100A000, header=0x1100B000, state=0x11006000, vt=0x11007000, manager=0x11008000, stack=0x11009000;
bool enabled=true, pro=false, available=true, title=true;
unsigned events=0, focuses=0, draws=0, background_draws=0;
uint32_t event_id=0;
void map(uint32_t a) {
#ifdef _WIN32
    auto p=VirtualAlloc(mem::ptr(a),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    auto p=mmap(mem::ptr(a),65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
    assert(p==mem::ptr(a));
}
}
namespace file_select_quit { void observe(Cpu*) {} }
namespace input { bool pro_controller(){return pro;} }
namespace input_map { bool skip_controller_selection(){return enabled;} }
void log_msg(const char*,...) {}
uint32_t guest_call(Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args) {
    std::vector<uint32_t> a(args);
    c->r[28]=0xBAD; c->f[1].ps0=13; c->cr[0]=1;
    if(fn==GC(0x02006478)) {assert(a[0]==owner+0x10);return state;}
    if(fn==0x02000000) return a[0]==GD(0x1049EA58) || (a[0]==state && !title) ? 8 : 7;
    if(fn==GC(0x026D43AC)||fn==GC(0x026D43DC)) {assert(a[0]==screen);return available;}
    if(fn==GC(0x026F5B74)) return a[0]==first?101:202;
    if(fn==GC(0x02615154)) {assert(a[0]==manager);assert(a[1]==receiver+(pro?0x100:0));++focuses;return 0;}
    assert(fn==GC(0x02001338));
    assert(a[0]==stack+0x30 && a[1]==17 && a[3]==0);
    event_id=a[2];++events;
    st32(a[0],a[1]);st32(a[0]+4,a[2]);return a[0];
}
extern "C" void f_026D50C4_orig(Cpu*) {++draws;}
extern "C" void f_026F8B54_orig(Cpu*) {++background_draws;}
int main() {
    for(uint32_t a:{0x10100000u,0x101F0000u,0x10490000u,owner}) map(a);
    st32(GD(0x101F82EC),owner);st32(owner+0x130,screen);st32(owner+0x148,0);
    st32(owner+0x12C,background);st32(background+0x58,header);
    st32(GD(0x1049EA58)+8,vt);
    st32(state+8,vt);st32(GD(0x1049EA2C)+8,vt);st32(vt+0x14,0x02000000);
    st32(screen+0x48,2);st32(screen+0x4C,children);
    st32(GD(0x10102B80),0);st32(GD(0x10102B80)+4,1);
    st32(children,first);st32(children+4,second);
    st32(first+0x50,receiver);st32(second+0x50,receiver+0x100);
    st32(GD(0x101F4FF0),manager);
    Cpu c{};c.r[1]=stack;c.r[28]=screen;c.r[3]=screen;
    Cpu saved=c;
    auto background_hidden = [&] {
        Cpu draw=c;draw.r[3]=background;Cpu before=draw;
        unsigned count=background_draws;
        hook_026F8B54(&draw);
        assert(memcmp(&draw,&before,sizeof draw)==0);
        return count==background_draws;
    };
    for(bool p:{false,true}) {
        pro=p;site_026D44A8(&c);
        assert(event_id==(p?202u:101u));assert(memcmp(&c,&saved,sizeof c)==0);
        assert(background_hidden());
        Cpu unrelated=c;unrelated.r[3]=header;
        unsigned before=background_draws;hook_026F8B54(&unrelated);
        assert(background_draws==before+1);
        hook_026D50C4(&c);assert(draws==0);assert(memcmp(&c,&saved,sizeof c)==0);
    }
    assert(events==2 && focuses==2);
    title=false;st32(owner+0x1C,GD(0x1049EA2C));
    st32(background+0x50,1);st32(background+0x54,0);
    auto memory=std::vector<uint8_t>(mem::ptr(owner),mem::ptr(owner)+65536);
    assert(background_hidden()); // old caption still fading out
    assert(memcmp(memory.data(),mem::ptr(owner),memory.size())==0);
    assert(memcmp(&c,&saved,sizeof c)==0);
    st32(background+0x54,1);
    assert(!background_hidden()); // file-select caption installed
    st32(background+0x54,0);st32(owner+0x1C,0);
    assert(!background_hidden()); // unrelated return to file select
    title=true;
    for(int fallback=0;fallback<9;++fallback) {
        enabled=fallback!=0;title=fallback!=1;available=fallback!=2;
        st32(screen+0x48,fallback==3?1:2);
        st32(GD(0x10102B80)+4,fallback==4?9:fallback==5?0:1);
        st32(GD(0x101F4FF0),fallback==6?0:manager);
        st32(GD(0x101F82EC),fallback==7?0:owner);
        st32(owner+0x130,fallback==8?screen+0x100:screen);
        auto before=std::vector<uint8_t>(mem::ptr(owner),mem::ptr(owner)+65536);
        site_026D44A8(&c);assert(events==2 && focuses==2);
        assert(memcmp(before.data(),mem::ptr(owner),before.size())==0);
        assert(!background_hidden());
        hook_026D50C4(&c);assert(draws==unsigned(fallback+1));
        assert(memcmp(&c,&saved,sizeof c)==0);
    }
    puts("controller_selection_test: passed");
}
