// SPDX-License-Identifier: MPL-2.0
#include "story_scene_choice.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cstring>
#include <utility>

namespace story_scene_choice { namespace {
constexpr uint32_t Marker=0x43484F32,Carrier=0x2CF3;
constexpr uint32_t Phase=4,Manager=8,Window=12,Decision=16,Default=20,Delivered=24;
constexpr uint32_t Labels[2]={0x40,0x90},Descriptor=0x100,SetDescriptor=0x108,Identifier=0x120,SetIdentifier=0x130;
constexpr uint32_t Open=1,Closing=2,Failed=3;
OriginalCall original=nullptr;GameCall invoke=nullptr;
uint32_t scratch=0;std::function<bool(Cpu*)> owns_scene;
bool installed=false,armed=false;
uint32_t call(Cpu* cpu,uint32_t fn,std::initializer_list<uint32_t> args={}){
    Cpu nested=*cpu;return invoke(&nested,fn,args);
}
bool pointer(uint32_t value,uint32_t bytes){return value>=0x10000000u&&value<0xC0000000u&&bytes<=0xC0000000u-value&&(value&3)==0;}
bool marked(){return scratch&&ld32(scratch)==Marker;}
bool lease(Cpu* cpu){return cpu&&invoke&&owns_scene&&owns_scene(cpu);}
bool links(uint32_t manager,uint32_t window){
    return pointer(manager,0x980)&&pointer(window,0x90)&&ld32(GD(0x101F4B5C))==manager&&ld32(manager+0x960)==window&&ld32(window+0x8C)==manager+0x14;
}
bool owned(Cpu* cpu,bool require_carrier){
    if(!marked()||!lease(cpu))return false;
    const uint32_t manager=ld32(scratch+Manager),window=ld32(scratch+Window);
    if(!links(manager,window))return false;
    return !require_carrier||(ld32(manager+0x938)==Carrier&&ld32(manager+0x94C)==5&&ld32(window+0x5C)==Carrier);
}
bool label_valid(const std::u16string& value){
    if(value.empty()||value.size()>32)return false;
    for(size_t i=0;i<value.size();++i){const uint16_t c=value[i];
        if(c<0x20||c==0x7F)return false;
        if(c>=0xD800&&c<=0xDBFF){if(++i>=value.size()||value[i]<0xDC00||value[i]>0xDFFF)return false;}
        else if(c>=0xDC00&&c<=0xDFFF)return false;
    }
    return true;
}
void reject(const char* reason){LOG("[scene_choice] %s",reason);if(marked())st32(scratch+Phase,Failed);}
void route(Cpu* cpu){
    if(!armed||!owned(cpu,false)||cpu->r[3]!=ld32(scratch+Manager)||cpu->r[4]!=Carrier){original(cpu,GC(0x025F7980));return;}
    const uint32_t manager=cpu->r[3],window=ld32(scratch+Window);
    // Numeric carrier labels search native message sets independently of the
    // cached set-name descriptor. Supply our own initialized "dummy" name and
    // never start an ordinary window and then a second renderer.
    const uint32_t resource=ld32(GD(0x101F4AE8));
    if(!pointer(resource,0x438)){
        reject("native message resource manager is unavailable");cpu->r[3]=0xFFFFFFFFu;return;
    }
    const uint32_t old_id=ld32(manager+0x938),old_mode=ld32(manager+0x94C);
    st32(manager+0x1C,0); // Native lookup failure clears only this result-set pointer.
    call(cpu,GC(0x025F50E8),{resource,manager+0x14,scratch+SetDescriptor,scratch+Descriptor});
    const uint32_t entry=ld32(manager+0x240),context=ld32(manager+0x12C),set=ld32(manager+0x1C);
    if(!pointer(set,0x118)||context>=ld32(set+4)||!pointer(entry,16)){
        reject("carrier metadata lookup failed");cpu->r[3]=0xFFFFFFFFu;return;
    }
    st32(manager+0x938,Carrier);st32(manager+0x94C,5);
    st32(manager+0x948,0xFFFFFFFFu);
    const uint32_t accepted=call(cpu,GC(0x026AE194),{window,context,Carrier,1,0});
    if(!accepted){st32(manager+0x938,old_id);st32(manager+0x94C,old_mode);reject("native carrier choice renderer rejected admission");cpu->r[3]=0xFFFFFFFFu;return;}
    st32(manager+0x14+0x6BC,ld32(scratch+Default));
    LOG("[scene_choice] opened manager=%08X window=%08X carrier=%04X default=%u cancel=0",manager,window,Carrier,ld32(scratch+Default));
    cpu->r[3]=0;
}
void pane(Cpu* cpu){
    if(armed&&owned(cpu,true)){
        const uint32_t workspace=ld32(scratch+Manager)+0x14,source=cpu->r[4];
        int option=-1;if(source==workspace+0x7CC)option=0;else if(source==workspace+0x6C0)option=1;
        if(option>=0){
            if(ld32(source)!=source+12||ld32(source+4)!=GD(0x100E0A98)||ld32(source+8)!=128){LOG("[scene_choice] label header=%08X data=%08X vtable=%08X capacity=%u",source,ld32(source),ld32(source+4),ld32(source+8));reject("native choice label buffer ownership changed");}
            else {
                st32(workspace+0x6BC,ld32(scratch+Default));
                for(unsigned i=0;i<=32;++i){const uint16_t ch=ld16(scratch+Labels[option]+i*2);st16(source+12+i*2,ch);if(!ch)break;}
            }
        }
    }
    original(cpu,GC(0x027046D4));
}
} // namespace
void configure(OriginalCall orig,GameCall game,uint32_t memory,std::function<bool(Cpu*)> owns){original=orig;invoke=game;scratch=memory;owns_scene=std::move(owns);armed=false;}
bool install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace){
    if(installed)return true;if(!original||!invoke||!scratch||!owns_scene)return false;
    if(!guest_code_unchanged(0x025F65F4)||!guest_code_unchanged(0x025F7424)||!guest_code_unchanged(0x025F7500)){LOG("[scene_choice] message-control constructors differ on %s; scene choices disabled",g_guest_build_name);return false;}
    for(uint32_t fn:{GC(0x025F7980),GC(0x027046D4)})if(!replaced_by(fn).empty()){LOG("[scene_choice] native hook %08X is already replaced",fn);return false;}
    replace(GC(0x025F7980),route);replace(GC(0x027046D4),pane);installed=true;return true;
}
bool begin(Cpu* cpu,const std::array<std::u16string,2>& labels,unsigned default_option){
    if(!installed||marked()||!lease(cpu)||default_option>1||!label_valid(labels[0])||!label_valid(labels[1]))return false;
    const uint32_t manager=ld32(GD(0x101F4B5C));
    if(!pointer(manager,0x980))return false;const uint32_t window=ld32(manager+0x960);
    if(!links(manager,window))return false;
    const uint32_t status=call(cpu,GC(0x025F795C),{manager});if(status!=0&&status!=15)return false;
    std::memset(mem::ptr(scratch),0,kScratchBytes);st32(scratch,Marker);st32(scratch+Phase,Open);st32(scratch+Manager,manager);st32(scratch+Window,window);st32(scratch+Decision,0xFFFFFFFFu);st32(scratch+Default,default_option);
    for(unsigned option=0;option<2;++option)for(size_t i=0;i<labels[option].size();++i)st16(scratch+Labels[option]+uint32_t(i)*2,labels[option][i]);
    std::memcpy(mem::ptr(scratch+Identifier),"11507",6);st32(scratch+Descriptor,scratch+Identifier);st32(scratch+Descriptor+4,GD(0x100E0AE4));
    std::memcpy(mem::ptr(scratch+SetIdentifier),"dummy",6);st32(scratch+SetDescriptor,scratch+SetIdentifier);st32(scratch+SetDescriptor+4,GD(0x100E0AE4));
    armed=true;
    const uint32_t result=call(cpu,GC(0x025F7DB0),{manager,Carrier,0});
    if(result==0xFFFFFFFFu||ld32(scratch+Phase)==Failed||!owned(cpu,true)){close(cpu);return false;}
    return true;
}
Result poll(Cpu* cpu,unsigned& selected){
    if(!armed||!owned(cpu,true)||ld32(scratch+Phase)==Failed){return Result::Failed;}
    const uint32_t manager=ld32(scratch+Manager),status=call(cpu,GC(0x025F795C),{manager});
    if(ld32(scratch+Phase)==Open&&status==14){
        const uint32_t choice=ld32(manager+0x948);if(choice>1){reject("native choice confirmation is outside two options");return Result::Failed;}
        st32(scratch+Decision,choice);st32(scratch+Phase,Closing);call(cpu,GC(0x025F74D0),{manager,16});
        LOG("[scene_choice] confirmed option=%u",choice);return Result::Pending;
    }
    if(ld32(scratch+Phase)==Closing&&status==18){
        if(ld32(scratch+Delivered))return Result::Failed;
        selected=ld32(scratch+Decision);st32(scratch+Delivered,1);call(cpu,GC(0x025F74D0),{manager,19});armed=false;st32(scratch,0);
        LOG("[scene_choice] closed option=%u",selected);return Result::Complete;
    }
    return Result::Pending;
}
void close(Cpu* cpu){
    if(marked()&&owned(cpu,true))call(cpu,GC(0x025F78C4),{ld32(scratch+Manager)});
    armed=false;if(scratch)st32(scratch,0);
}
void state_loaded(){armed=false;}
bool active(){return armed&&marked()&&ld32(scratch+Phase)!=Failed;}
} // namespace story_scene_choice
