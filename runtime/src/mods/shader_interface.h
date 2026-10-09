#pragma once
#include "../exception_report.h"
#include <cctype>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace mods::cemu {
// Compare SPIR-V resource layouts, including array lengths, member offsets and
// strides. IDs and debug names are deliberately ignored. Packs may omit resources
// but cannot add descriptors or change the host's existing packed buffer layout.
inline std::map<std::string,std::string> shader_interface(const std::vector<uint32_t>& words) {
    using Words=std::vector<uint32_t>;
    std::map<uint32_t,Words> types,constants,variables;
    std::map<uint32_t,std::map<uint32_t,Words>> decorations;
    std::map<std::pair<uint32_t,uint32_t>,std::map<uint32_t,Words>> members;
    if(words.size()<5||words[0]!=0x07230203)exception_report::raise("Invalid SPIR-V");
    for(size_t pos=5;pos<words.size();) {
        auto count=words[pos]>>16,op=words[pos]&0xffff;
        if(!count||pos+count>words.size())exception_report::raise("Truncated SPIR-V");
        Words args(words.begin()+pos+1,words.begin()+pos+count);
        if(op>=19&&op<=33&&!args.empty())types[args[0]]=Words{op};
        if(op>=19&&op<=33&&!args.empty())types[args[0]].insert(types[args[0]].end(),args.begin()+1,args.end());
        if((op==43||op==50)&&args.size()>=3)constants[args[1]]=Words(args.begin()+2,args.end());
        if(op==59&&args.size()>=3)variables[args[1]]={args[0],args[2]};
        if(op==71&&args.size()>=2)decorations[args[0]][args[1]]=Words(args.begin()+2,args.end());
        if(op==72&&args.size()>=3)members[{args[0],args[1]}][args[2]]=Words(args.begin()+3,args.end());
        if(op>=73&&op<=75)exception_report::raise("Grouped SPIR-V decorations unsupported");
        pos+=count;
    }
    auto layout=[](const auto& values) {
        std::string out;
        for(const auto& [kind,args]:values)if(kind==2||kind==3||kind==4||kind==5||kind==6||kind==7||kind==35) {
            out+="d"+std::to_string(kind)+":";for(auto v:args)out+=std::to_string(v)+",";
        }
        return out;
    };
    auto type=[&](auto&& self,uint32_t id,unsigned depth)->std::string {
        if(depth>32||!types.count(id))exception_report::raise("Unsupported SPIR-V resource type");
        const auto& t=types.at(id);auto op=t[0];std::string out=std::to_string(op)+"(";
        for(size_t i=1;i<t.size();i++) {
            bool reference=(op==23||op==24||op==25||op==27||op==28||op==29)?i==1:op==30?true:op==32?i==2:false;
            if(reference)out+=self(self,t[i],depth+1);
            else if(op==28&&i==2) {if(!constants.count(t[i]))exception_report::raise("Nonconstant shader resource array");for(auto v:constants.at(t[i]))out+=std::to_string(v)+",";}
            else out+=std::to_string(t[i]);
            if(op==30)out+=layout(members[{id,uint32_t(i-1)}]);
            out+=";";
        }
        return out+")"+layout(decorations[id]);
    };
    std::map<std::string,std::string> result;
    for(const auto& [id,v]:variables) {
        if(v[1]==9)exception_report::raise("Custom push constants unsupported");
        auto& d=decorations[id];std::string key;
        if(v[1]==0||v[1]==2||v[1]==12) {
            if(!d.count(33)||!d.count(34)||d[33].size()!=1||d[34].size()!=1)exception_report::raise("Resource lacks descriptor binding");
            key="descriptor:"+std::to_string(d[34][0])+":"+std::to_string(d[33][0]);
        }else if((v[1]==1||v[1]==3)&&d.count(30)&&d[30].size()==1)
            key="io:"+std::to_string(v[1])+":"+std::to_string(d[30][0]);
        else continue;
        if(!result.emplace(key,type(type,v[0],0)).second)exception_report::raise("Duplicate shader binding/location");
    }
    return result;
}
// A pixel shader input that no vertex shader output feeds is a constant in the translated shader
// (gfx/vulkan/shaders.cpp ps_link: the value SPI_PS_INPUT_CNTL DEFAULT_VAL gives it). The pack's
// "layout(location = N) [flat|noperspective|smooth|centroid] in vec4 name;" for such an input
// becomes the same constant, otherwise its interface has an input the original lacks.
// values[N]: the constant for location N, or null for a fed input.
inline std::string const_pixel_inputs(std::string source,const char* const (&values)[32]) {
    auto ident=[](char c){return std::isalnum((unsigned char)c)||c=='_';};
    auto space=[&](size_t& at){while(at<source.size()&&std::isspace((unsigned char)source[at]))at++;};
    auto word=[&](size_t& at){size_t begin=at;while(at<source.size()&&ident(source[at]))at++;return source.substr(begin,at-begin);};
    for(size_t start=source.find("layout");start!=std::string::npos;start=source.find("layout",start+1)) {
        if(start&&ident(source[start-1]))continue;
        size_t at=start+6;space(at);
        if(at>=source.size()||source[at]!='(')continue;
        at++;space(at);
        if(word(at)!="location")continue;
        space(at);if(at>=source.size()||source[at]!='=')continue;
        at++;space(at);
        size_t digits=at;while(at<source.size()&&std::isdigit((unsigned char)source[at]))at++;
        if(at==digits||at-digits>2)continue;
        const int location=std::stoi(source.substr(digits,at-digits));
        space(at);if(at>=source.size()||source[at]!=')'||location>=32||!values[location])continue;
        at++;
        bool input=false;std::string type,name;
        for(;;) {
            space(at);auto token=word(at);
            if(token=="flat"||token=="noperspective"||token=="smooth"||token=="centroid")continue;
            if(token=="in"&&!input){input=true;continue;}
            type=token;space(at);name=word(at);break;
        }
        space(at);
        if(!input||type!="vec4"||name.empty()||at>=source.size()||source[at]!=';')continue;
        const std::string constant="const vec4 "+name+" = "+values[location]+";";
        source.replace(start,at+1-start,constant);
    }
    return source;
}
inline bool compatible_shader_interface(const std::vector<uint32_t>& original,const std::vector<uint32_t>& replacement,std::string& error) {
    try {
        auto expected=shader_interface(original),actual=shader_interface(replacement);
        for(const auto& [binding,signature]:actual)
            if(!expected.count(binding)||expected.at(binding)!=signature)exception_report::raise("Incompatible shader layout at "+binding);
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
