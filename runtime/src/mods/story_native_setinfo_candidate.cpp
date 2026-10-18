// SPDX-License-Identifier: MPL-2.0
#include "story_native_setinfo_candidate.h"
#include "story_native_bfres_extent.h"
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace story_native_setinfo_candidate { namespace {
uint32_t be32(const uint8_t* b) {
    return uint32_t(b[0])<<24 | uint32_t(b[1])<<16 |
           uint32_t(b[2])<<8 | b[3];
}
bool bytes(const Read& read,uint32_t at,std::span<uint8_t> dst) {
    return at && uint64_t(at)+dst.size()<=uint64_t(UINT32_MAX)+1 && read(at,dst);
}
bool word(const Read& read,uint32_t at,uint32_t& out) {
    std::array<uint8_t,4>b{};
    if(!bytes(read,at,b))return false;
    out=be32(b.data());return true;
}
bool text(const Read& read,uint32_t ptr,size_t cap,std::string& out) {
    out.clear();
    if(!ptr||cap>64)return false;
    for(size_t i=0;i<=cap;++i){
        uint8_t c{};
        if(uint64_t(ptr)+i>UINT32_MAX||!read(ptr+uint32_t(i),std::span<uint8_t>(&c,1)))return false;
        if(!c)return !out.empty();
        if(c<0x20||c>0x7e||i==cap)return false;
        out.push_back(char(c));
    }
    return false;
}
bool safe_string(const Read& read,uint32_t object,size_t cap,std::string& out) {
    uint32_t ptr{};return word(read,object,ptr)&&text(read,ptr,cap,out);
}
uint32_t crc(std::string_view s) {
    uint32_t value=0xffffffffu;
    for(unsigned char c:s){
        value^=c;
        for(unsigned i=0;i<8;++i)value=(value>>1)^(value&1?0xedb88320u:0u);
    }
    return ~value;
}
bool hash_node(const Read& read,uint32_t pool,uint32_t hash,
               uint32_t vtable,uint32_t& value) {
    value=0;
    uint32_t node{};
    if(!pool||!hash||!word(read,pool,node))return false;
    std::unordered_set<uint32_t> seen;
    for(unsigned i=0;node&&i<1024;++i){
        if(!seen.insert(node).second||uint64_t(node)+32>uint64_t(UINT32_MAX)+1)return false;
        uint32_t key{},owner{},vt{};
        if(!word(read,node+12,key)||!word(read,node+28,owner)||
           !word(read,node+16,vt)||owner!=pool||vt!=vtable)return false;
        if(key==hash)return word(read,node+24,value)&&value;
        if(!word(read,node+(key<hash?4:0),node))return false;
    }
    return false;
}
bool archive_holder(const Read& read,uint32_t manager,std::string_view key,
                    uint32_t holder_vtable,uint32_t& out) {
    out=0;
    if(!manager||key.empty()||key.size()>47)return false;
    uint32_t root{},count{};
    if(!word(read,manager+0x14,root)||!word(read,manager+0x20,count)||
       !root||!count||count>2048)return false;
    std::vector<uint32_t> stack{root};
    std::unordered_set<uint32_t> seen;
    while(!stack.empty()){
        const uint32_t node=stack.back();stack.pop_back();
        if(!node||uint64_t(node)+0x54>uint64_t(UINT32_MAX)+1||
           !seen.insert(node).second||seen.size()>count)return false;
        uint32_t left{},right{},name_ptr{},holder{},node_owner{};
        if(!word(read,node,left)||!word(read,node+4,right)||
           !word(read,node+0x0c,name_ptr)||!word(read,node+0x18,holder)||
           !word(read,node+0x1c,node_owner)||node_owner!=manager+0x14)return false;
        std::string node_name;
        if(!text(read,name_ptr,48,node_name))return false;
        if(node_name==key){
            if(out||!holder||uint64_t(holder)+0x54>uint64_t(UINT32_MAX)+1)return false;
            uint32_t vt{},holder_name{};
            std::string actual;
            if(!word(read,holder+0x0c,vt)||vt!=holder_vtable||
               !word(read,holder+0x10,holder_name)||
               !text(read,holder_name,48,actual)||actual!=key)return false;
            out=holder;
        }
        if(left)stack.push_back(left);
        if(right)stack.push_back(right);
    }
    return seen.size()==count && out;
}
} // namespace

static bool verify_named(const Read& read,const Layout& l,uint32_t control,
                         std::string_view stage,uint32_t manager,Candidate& out) {
    out={};
    if(!read||!control||uint64_t(control)+0x103c>uint64_t(UINT32_MAX)+1||
       stage.empty()||stage.size()>63||!manager||!l.manager_vtable||
       !l.info_vtable||!l.wrapper_vtable||
       !l.holder_vtable||!l.archive_resource_vtable||!l.info_node_vtable||
       !l.resource_node_vtable||!l.solid_heap_vtable)return false;
    try{
        const std::string archive="Stage";
        // The event manager requests the canonical Stage archive. A different
        // resource can contain a file with the same raw name but is not this owner.
        const std::string key=std::string(stage)+"_"+archive;
        if(key.size()>47)return false;
        const uint32_t archive_hash=crc(key),resource_hash=crc("event_list.dat");
        uint32_t manager_vt{},info{};
        if(!word(read,manager,manager_vt)||manager_vt!=l.manager_vtable)return false;
        if(!hash_node(read,control+0x1028,archive_hash,l.info_node_vtable,info)||
           uint64_t(info)+200>uint64_t(UINT32_MAX)+1)return false;
        uint32_t info_vt{},stage_ptr{},archive_ptr{},wrapper{},resource{};
        std::string actual_stage,actual_archive;
        if(!word(read,info+0x0c,info_vt)||info_vt!=l.info_vtable||
           !word(read,info+0x10,stage_ptr)||stage_ptr!=info+0x1c||
           !word(read,info+0x5c,archive_ptr)||archive_ptr!=info+0x68||
           !text(read,stage_ptr,63,actual_stage)||actual_stage!=stage||
           !text(read,archive_ptr,63,actual_archive)||actual_archive!=archive||
           !word(read,info+0xa8,wrapper)||!wrapper||
           !hash_node(read,info+0xac,resource_hash,l.resource_node_vtable,resource))return false;
        uint32_t wrapper_vt{},bfres{};
        if(!word(read,wrapper+0x0c,wrapper_vt)||wrapper_vt!=l.wrapper_vtable||
           !word(read,wrapper+0x14,bfres)||!bfres)return false;
        uint32_t holder{};
        if(!archive_holder(read,manager,key,l.holder_vtable,holder))return false;
        uint32_t archive_resource{},archive_vt{},archive_flags{};
        if(!word(read,holder+0x4c,archive_resource)||!archive_resource||
           uint64_t(archive_resource)+0x24>uint64_t(UINT32_MAX)+1||
           !word(read,archive_resource+0x10,archive_vt)||
           archive_vt!=l.archive_resource_vtable||
           !word(read,archive_resource+0x20,archive_flags))return false;
        uint32_t request_heap{},data_heap{},pack_heap{},heap_vt{},heap_parent{};
        if(!word(read,info+0xc0,request_heap)||!request_heap||
           !word(read,info+0xc4,data_heap)||!data_heap||
           !word(read,holder+0x50,pack_heap)||pack_heap!=request_heap||
           request_heap==data_heap||
           uint64_t(request_heap)+0x28>uint64_t(UINT32_MAX)+1||
           uint64_t(data_heap)+0x28>uint64_t(UINT32_MAX)+1||
           !word(read,request_heap+0x0c,heap_vt)||heap_vt!=l.solid_heap_vtable||
           !word(read,data_heap+0x0c,heap_vt)||heap_vt!=l.solid_heap_vtable||
           !word(read,data_heap+0x24,heap_parent)||!heap_parent)return false;
        story_native_sarc_extent::Extent outer;
        if(!story_native_sarc_extent::find(read,archive_resource,archive+".bfres",bfres,outer))
            return false;
        story_native_bfres_extent::Extent embedded;
        if(!story_native_bfres_extent::find(read,outer.base,outer.bytes,
                                            "event_list.dat",resource,embedded)||
           embedded.bytes>2u*1024u*1024u)return false;
        out={info,wrapper,holder,archive_resource,outer.base,outer.bytes,
             outer.archive_base,outer.archive_bytes,embedded.base,embedded.bytes,
             embedded.entry,archive_hash,resource_hash,request_heap,data_heap,heap_parent,pack_heap,
             bool(archive_flags&1u)};
        std::copy(stage.begin(),stage.end(),out.stage_name.begin());
        return true;
    }catch(...){out={};return false;}
}
bool verify(const Read& read,const Layout& l,uint32_t control,
            uint32_t archive_name,uint32_t path,uint32_t manager,Candidate& out) {
    out={};
    std::string archive,stage;
    if(!read||!safe_string(read,archive_name,63,archive)||archive!="Stage"||
       !safe_string(read,path,63,stage))return false;
    return verify_named(read,l,control,stage,manager,out);
}
bool verify_live(const Read& read,const Layout& l,uint32_t control,
                 uint32_t manager,const Candidate& expected,Candidate& out) {
    out={};
    const auto end=std::find(expected.stage_name.begin(),expected.stage_name.end(),'\0');
    if(end==expected.stage_name.begin()||end==expected.stage_name.end())return false;
    const std::string_view stage(expected.stage_name.data(),
                                 size_t(end-expected.stage_name.begin()));
    return verify_named(read,l,control,stage,manager,out)&&out==expected;
}
} // namespace story_native_setinfo_candidate
