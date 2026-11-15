// SPDX-License-Identifier: MPL-2.0
// Synthetic native contracts. Real pane/input rendering needs a game golden.
#include "mods/story_scene_choice.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Arena=0x20000000,Manager=Arena,Window=Arena+0x2000,Scratch=Arena+0x4000,Resource=Arena+0x6000,Entry=Arena+0x8000;
std::map<uint32_t,void(*)(Cpu*)> hooks;
bool owns=true,metadata=true,admit=true;unsigned resets=0,routes=0,panes=0,starts=0;uint32_t status=0,metadata_count=8;std::vector<uint32_t> statuses;
void map(uint32_t at,uint32_t bytes){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(at),bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(at),bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*,uint32_t fn){if(fn==0x025F7980)++routes;else{assert(fn==0x027046D4);++panes;}}
uint32_t call(Cpu* cpu,uint32_t fn,std::initializer_list<uint32_t> args){std::vector<uint32_t>a(args);switch(fn){
case 0x025F795C:assert(a[0]==Manager);return status;
case 0x025F50E8:assert(a.size()==4&&a[0]==Resource&&a[1]==Manager+0x14&&a[2]==Scratch+0x108);assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(a[2]))),"dummy")==0);assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(a[3]))),"11507")==0);st32(Manager+0x240,Entry);st32(Manager+0x1C,metadata?Resource:0);st32(Resource+4,metadata_count);st32(Manager+0x12C,7);return 1;
case 0x026AE194:assert(a==std::vector<uint32_t>({Window,7,0x2CF3,1,0}));++starts;if(admit){st32(Window+0x5C,0x2CF3);st8(Window+0x89,0);}return admit;
case 0x025F7DB0:{assert(a==std::vector<uint32_t>({Manager,0x2CF3,0}));Cpu nested=*cpu;nested.r[3]=a[0];nested.r[4]=a[1];nested.r[5]=a[2];hooks.at(0x025F7980)(&nested);if(nested.r[3]==0xFFFFFFFFu)return 0xFFFFFFFFu;status=1;return 0;}
case 0x025F74D0:assert(a[0]==Manager);status=a[1];statuses.push_back(a[1]);return 0;
case 0x025F78C4:assert(a[0]==Manager);++resets;status=0;return 0;
default:assert(false);return 0;}}
void headers(){for(uint32_t offset:{0x6C0u,0x7CCu}){uint32_t p=Manager+0x14+offset;st32(p,p+12);st32(p+4,0x100E0A98);st32(p+8,128);}}
void ready(){metadata_count=8;owns=true;metadata=true;admit=true;status=0;st32(0x101F4B5C,Manager);st32(Manager+0x960,Window);st32(Window+0x8C,Manager+0x14);st32(0x1048D774,1);headers();}
void pane(Cpu&c,uint32_t source){c.r[3]=Arena+0xA000;c.r[4]=source;c.r[5]=0;hooks.at(0x027046D4)(&c);}
}
int main(){map(Arena,0x10000);map(0x101F0000,0x10000);map(0x10480000,0x10000);st32(0x101F4AE8,Resource);st32(0x1048D760,Arena+0xF000);Cpu c{};
story_scene_choice::configure(original,call,Scratch,[](Cpu*){return owns;});unsigned replacements=0;
auto replace=[&](uint32_t fn,void(*hook)(Cpu*)){hooks[fn]=hook;++replacements;};
#ifdef TEST_JP
ready();assert(!story_scene_choice::install([](uint32_t){return std::string();},replace)&&replacements==0);
assert(!story_scene_choice::begin(&c,{u"Walk",u"Stay"},0));
return 0;
#endif
assert(!story_scene_choice::install([](uint32_t fn){return fn==0x027046D4?std::string("foreign"):std::string();},replace));assert(replacements==0);
assert(story_scene_choice::install([](uint32_t){return std::string();},replace)&&replacements==2);
ready();c.r[3]=Manager;c.r[4]=0x1234;hooks.at(0x025F7980)(&c);assert(routes==1);
pane(c,Manager+0x14+0x7CC);assert(panes==1); // Unarmed native labels untouched.
assert(!story_scene_choice::begin(&c,{u"",u"No"}));assert(!story_scene_choice::begin(&c,{u"Yes\n",u"No"}));assert(!story_scene_choice::begin(&c,{std::u16string(33,u'x'),u"No"}));assert(!story_scene_choice::begin(&c,{std::u16string(1,char16_t(0xD800)),u"No"}));assert(!story_scene_choice::begin(&c,{u"Yes",u"No"},2));
for(unsigned answer=0;answer<2;++answer){ready();statuses.clear();assert(story_scene_choice::begin(&c,{u"Walk",u"Stay"},answer));assert(story_scene_choice::active());assert(!story_scene_choice::begin(&c,{u"Again",u"No"}));
    const uint32_t left=Manager+0x14+0x7CC,right=Manager+0x14+0x6C0;
    st16(left+12,0);st16(right+12,0);st32(Manager+0x14+0x6BC,0);pane(c,left);pane(c,right);
    for(unsigned i=0;i<5;++i){assert(ld16(left+12+i*2)==u"Walk"[i]);assert(ld16(right+12+i*2)==u"Stay"[i]);}
    assert(ld32(Manager+0x14+0x6BC)==answer&&ld8(Window+0x89)==0);
    const unsigned before=panes;pane(c,Manager+0x14+0x5B4);assert(panes==before+1);
    unsigned selected=77;assert(story_scene_choice::poll(&c,selected)==story_scene_choice::Result::Pending&&selected==77);
    st32(Manager+0x948,answer);status=14;assert(story_scene_choice::poll(&c,selected)==story_scene_choice::Result::Pending&&selected==77&&status==16);
    status=17;assert(story_scene_choice::poll(&c,selected)==story_scene_choice::Result::Pending);
    status=18;assert(story_scene_choice::poll(&c,selected)==story_scene_choice::Result::Complete&&selected==answer&&status==19);assert(!story_scene_choice::active());assert(story_scene_choice::poll(&c,selected)==story_scene_choice::Result::Failed);assert(statuses==std::vector<uint32_t>({16,19}));
}
ready();assert(story_scene_choice::begin(&c,{u"A",u"B"}));st32(Manager+0x948,2);status=14;unsigned answer=9;assert(story_scene_choice::poll(&c,answer)==story_scene_choice::Result::Failed&&answer==9);unsigned count=resets;story_scene_choice::close(&c);assert(resets==count+1);
ready();assert(story_scene_choice::begin(&c,{u"A",u"B"}));st32(Manager+0x14+0x7CC+8,64);pane(c,Manager+0x14+0x7CC);assert(story_scene_choice::poll(&c,answer)==story_scene_choice::Result::Failed);story_scene_choice::close(&c);
ready();assert(story_scene_choice::begin(&c,{u"A",u"B"}));count=resets;story_scene_choice::state_loaded();assert(!story_scene_choice::active());const unsigned prior_routes=routes;c.r[3]=Manager;c.r[4]=0x2CF3;hooks.at(0x025F7980)(&c);assert(routes==prior_routes+1);story_scene_choice::close(&c);assert(resets==count+1); // Restored native marker permits owned cleanup, not resumed branch.
ready();assert(story_scene_choice::begin(&c,{u"A",u"B"}));count=resets;owns=false;story_scene_choice::close(&c);assert(resets==count); // A foreign scene cannot reset the native window.
ready();assert(story_scene_choice::begin(&c,{u"A",u"B"}));count=resets;st32(Manager+0x960,Window+0x100);assert(story_scene_choice::poll(&c,answer)==story_scene_choice::Result::Failed);story_scene_choice::close(&c);assert(resets==count);
ready();admit=false;assert(!story_scene_choice::begin(&c,{u"A",u"B"}));assert(!story_scene_choice::active());
ready();metadata=false;assert(!story_scene_choice::begin(&c,{u"A",u"B"})); // Entry remains stale and valid, but missing result set rejects it.
ready();metadata_count=7;assert(!story_scene_choice::begin(&c,{u"A",u"B"})); // Native context is outside its actual result-set count.
ready();st32(0x1048D774,0);st32(0x1048D760,0);const unsigned old_starts=starts;assert(story_scene_choice::begin(&c,{u"A",u"B"})&&starts==old_starts+1);story_scene_choice::close(&c); // Numeric own carrier needs no cached retail set-name initialization.
ready();status=1;assert(!story_scene_choice::begin(&c,{u"A",u"B"}));owns=false;status=0;assert(!story_scene_choice::begin(&c,{u"A",u"B"}));
return 0;}
