// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_stage_owner_hooks.h"
#include "runtime.h"
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <cstdint>
using namespace story_native_stage_owner_hooks;
namespace {
std::vector<int> events;
Args last{};
bool throw_destroy=false;
bool throw_setinfo=false;
bool expect_quarantine=false;
bool quarantined=false;
std::recursive_mutex lifetime_fence;
uint64_t before_setinfo(Cpu* c,const Args& a) {
    events.push_back(0);last=a;
    c->r[3]=0;c->r[4]=0;c->pc=0;
    if(throw_setinfo)throw std::runtime_error("pre-setinfo");
    return 11;
}
uint64_t destroy(Cpu* c,uint32_t fn,const Args& a) {
    events.push_back(1);last=a;assert(observed(fn));
    c->r[3]=0;c->r[4]=0;c->pc=0;
    if(throw_destroy)throw std::runtime_error("observer");
    return 12;
}
void loaded(Cpu* c,const Args& a,uint32_t ret,uint64_t cookie) {
    events.push_back(3);last=a;assert(ret==1&&(cookie==11||cookie==0));
    c->r[3]=0;c->r[4]=0;c->pc=0;
}
void failed(Cpu* c,const Args& a,uint64_t cookie) {
    assert(cookie==11||cookie==0);
    if(expect_quarantine)assert(quarantined);
    events.push_back(4);last=a;c->r[3]=0;c->pc=0;
}
void after_destroy(Cpu*,uint32_t,const Args&,uint64_t cookie){
    assert(cookie==12||cookie==0);
    if(expect_quarantine)assert(quarantined);
}
void fault() noexcept {quarantined=true;events.push_back(5);}
uint64_t before_free(Cpu* c,const Args& a){
    events.push_back(6);last=a;c->r[3]=0;c->pc=0;
    if(throw_destroy)throw std::runtime_error("pre-free");
    return 13;
}
void after_free(Cpu*,const Args&,uint64_t cookie){assert(cookie==13||cookie==0);}
const Observer observer{before_setinfo,destroy,loaded,failed,after_destroy,
    fault,&lifetime_fence,before_free,after_free};
}
int main() {
    assert(!register_observer(nullptr));
    Observer incomplete=observer;incomplete.before_setinfo=nullptr;
    assert(!register_observer(&incomplete));
    incomplete=observer;incomplete.after_free=nullptr;
    assert(!register_observer(&incomplete));
    assert(register_observer(&observer));
    assert(!register_observer(&observer));
    Cpu c{};c.pc=0x1234;c.r[3]=0x111;c.r[4]=0x222;c.r[5]=0x333;c.r[6]=0x444;
    // The same wrapper is invoked by the dispatcher body's original and by
    // guestmods::call_original. Both must observe exactly one native body.
    for (unsigned route=0;route<2;++route) {
        events.clear();unsigned bodies=0;
        invoke_dispatch(&c,DeleteInfo(),[&]{
            invoke_original(&c,DeleteInfo(),[&]{events.push_back(2);++bodies;
                assert(c.r[3]==0x111&&c.r[4]==0x222&&c.pc==0x1234);});
        });
        assert((events==std::vector<int>{1,2})&&bodies==1);
        assert(last.r3_r6[0]==0x111&&last.r3_r6[3]==0x444);
    }
    events.clear();
    invoke_dispatch(&c,SetInfo(),[&]{events.push_back(2);}); // guest replacement without original
    assert((events==std::vector<int>{2}));
    events.clear();
    invoke_original(&c,SetInfo(),[&]{events.push_back(2);c.r[3]=1;});
    assert((events==std::vector<int>{0,2,3})&&c.r[3]==1&&c.pc==0x1234);
    events.clear();
    invoke_original(&c,SetInfo(),[&]{events.push_back(2);c.r[3]=0;});
    assert((events==std::vector<int>{0,2,4})&&c.r[3]==0&&c.pc==0x1234);
    events.clear();quarantined=false;expect_quarantine=true;
    try {invoke_original(&c,SetInfo(),[&]{events.push_back(2);throw std::runtime_error("test");});assert(false);}
    catch (const std::runtime_error&) {}
    assert((events==std::vector<int>{0,2,5,4})&&quarantined);
    expect_quarantine=false;
    events.clear();quarantined=false;expect_quarantine=true;
    try {invoke_original(&c,DeleteInfo(),[&]{events.push_back(2);throw std::runtime_error("destroy");});assert(false);}
    catch (const std::runtime_error&) {}
    assert((events==std::vector<int>{1,2,5})&&quarantined);
    expect_quarantine=false;
    events.clear();throw_setinfo=true;c.r[3]=0x111;c.r[4]=0x222;
    invoke_original(&c,SetInfo(),[&]{events.push_back(2);assert(c.r[3]==0x111&&c.r[4]==0x222&&c.pc==0x1234);c.r[3]=1;});
    assert((events==std::vector<int>{0,5,2,3})&&c.r[3]==1);
    throw_setinfo=false;
    events.clear();
    invoke_original(&c,0x0261261C,[&]{events.push_back(2);c.r[3]=7;});
    assert((events==std::vector<int>{1,2})&&c.r[3]==7);
    for(uint32_t addr:{ManagerDisposer(),ArchiveHolderDtor(),PackHolderDtor(),StageHeapDtor(),
        ArchiveResourceDtor(),SarcResourceDtor()}){
        events.clear();unsigned bodies=0;
        invoke_dispatch(&c,addr,[&]{invoke_original(&c,addr,[&]{events.push_back(2);++bodies;});});
        assert((events==std::vector<int>{1,2})&&bodies==1);
    }
    events.clear();c.r[3]=0xABCDEF00;
    invoke_dispatch(&c,ArrayDelete(),[&]{invoke_original(&c,ArrayDelete(),[&]{
        events.push_back(2);assert(c.r[3]==0xABCDEF00&&c.pc==0x1234);});});
    assert((events==std::vector<int>{6,2})&&last.r3_r6[0]==0xABCDEF00);
    events.clear();throw_destroy=true;
    invoke_original(&c,ArrayDelete(),[&]{events.push_back(2);assert(c.r[3]==0xABCDEF00);});
    assert((events==std::vector<int>{6,5,2}));throw_destroy=false;
    events.clear();
    invoke_original(&c,0x01020304,[&]{events.push_back(2);});
    assert((events==std::vector<int>{2}));
    events.clear();throw_destroy=true;c.r[3]=0x111;
    invoke_original(&c,DeleteInfo(),[&]{events.push_back(2);assert(c.r[3]==0x111&&c.pc==0x1234);});
    assert((events==std::vector<int>{1,5,2}));
    throw_destroy=false;events.clear();
    std::promise<void> entered,release;
    auto ready=entered.get_future(),go=release.get_future();
    auto set=std::async(std::launch::async,[&]{
        Cpu x{};x.pc=0x1234;x.r[3]=0x111;
        invoke_original(&x,SetInfo(),[&]{events.push_back(2);entered.set_value();go.wait();x.r[3]=1;});
    });
    ready.wait();
    auto unlocked=std::async(std::launch::async,[&]{
        const bool got=lifetime_fence.try_lock();
        if(got)lifetime_fence.unlock();
        return got;
    });
    assert(unlocked.get()); // guest body cannot hold the host lifetime mutex
    release.set_value();set.get();
    Cpu x{};x.pc=0x1234;x.r[3]=0x111;
    invoke_original(&x,DeleteInfo(),[&]{events.push_back(2);});
    assert((events==std::vector<int>{0,2,3,1,2}));
}
