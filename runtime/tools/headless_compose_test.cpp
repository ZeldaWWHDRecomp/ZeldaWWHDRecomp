#include "gfx/headless_compose.h"
#include <cassert>
#include <stdexcept>
using namespace gfx::headless_compose;
int main() {
    auto disabled=parse(nullptr,nullptr,nullptr);assert(!disabled.enabled()&&!disabled.invalid);
    Counter untouched;unsigned allocations=0,draws=0;
    if(disabled.enabled()){++allocations;++draws;untouched.encoded(disabled,800,1200,1);}
    assert(allocations==0&&draws==0&&untouched.frames==0&&untouched.vertices==0);
    const char* invalid_values[]={"","0","10001","99999999999999999999","-1","12x"};
    for(auto text:invalid_values)assert(parse(text,"1","1").invalid);
    const char* invalid_guards[]={nullptr,"0","true"};
    for(auto text:invalid_guards){assert(parse("120",text,"1").invalid);assert(parse("120","1",text).invalid);}
    assert(parse("1","1","1").enabled());assert(parse("10000","1","1").enabled());
    const auto bounded=parse("2","1","1");Counter c;c.require_capacity(bounded);
    assert(c.encoded(bounded,0,0,0));assert(c.encoded(bounded,800,1200,1));
    assert(c.frames==2&&c.vertices==800&&c.indices==1200&&c.commands==1);
    bool refused=false;try{c.require_capacity(bounded);}catch(const std::runtime_error&){refused=true;}assert(refused);
    refused=false;try{c.encoded(bounded,800,1200,1);}catch(const std::runtime_error&){refused=true;}assert(refused);
    assert(c.frames==2&&c.vertices==800);
}
