#pragma once
// Internal, opt-in renderer diagnostic. No public SDK surface or ordinary drawing work.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace gfx::headless_compose {
constexpr unsigned width=1280,height=720,max_frames=10000;
struct Policy {
    unsigned limit=0;
    bool invalid=false;
    bool enabled() const {return limit!=0&&!invalid;}
};
inline Policy parse(const char* frames,const char* hidden,const char* no_input) {
    if(!frames)return {};
    if(!*frames||!hidden||std::strcmp(hidden,"1")||!no_input||std::strcmp(no_input,"1"))return {0,true};
    unsigned value=0;
    for(const char* p=frames;*p;++p) {
        if(*p<'0'||*p>'9')return {0,true};
        value=value*10+unsigned(*p-'0');
        if(value>max_frames)return {0,true};
    }
    return value?Policy{value,false}:Policy{0,true};
}
inline const Policy& policy() {
    static const auto result=parse(std::getenv("WWHD_TEST_OFFSCREEN_FRAMES"),std::getenv("WWHD_HIDDEN_WINDOWS"),std::getenv("WWHD_NO_HOST_INPUT"));
    if(result.invalid)throw std::runtime_error("WWHD_TEST_OFFSCREEN_FRAMES requires 1..10000, hidden windows and no host input");
    return result;
}
struct Counter {
    unsigned frames=0;
    uint64_t vertices=0,indices=0,commands=0;
    void require_capacity(const Policy& p) const {
        if(!p.enabled()||frames>=p.limit)throw std::runtime_error("headless composition diagnostic frame limit exhausted");
    }
    bool encoded(const Policy& p,unsigned v,unsigned i,unsigned c) {
        require_capacity(p);++frames;vertices+=v;indices+=i;commands+=c;
        return frames==1||frames%120==0||frames==p.limit;
    }
};
}
