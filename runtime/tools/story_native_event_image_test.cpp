// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_image.h"
#include <cassert>
#include <stdexcept>
#include <string>
using namespace story_native_event_image;
int main(){std::string why;auto a=make_wait_fixture();assert(validate_wait_fixture(a,why));auto b=a;b[0xf0+0x38]=0;assert(a[0xf0+0x38]==0xff);assert(!validate_wait_fixture(b,why));
 for(auto n:{1u,32767u})assert(validate_wait_fixture(make_wait_fixture(n),why));
 for(auto n:{0u,32768u,0xffffffffu}){bool threw=false;try{make_wait_fixture(n);}catch(const std::invalid_argument&){threw=true;}assert(threw);}
 // Each byte is constrained: covers section overflow/overlap, all indices,
 // linked-parameter cycles, invalid types/pools/flags and unterminated names.
 for(size_t i=0;i<a.size();++i){b=a;b[i]^=0x80;assert(!validate_wait_fixture(b,why));}
 b=a;b.pop_back();assert(!validate_wait_fixture(b,why));b=a;b.push_back(0);assert(!validate_wait_fixture(b,why));
 // A valid alternate timer requires all four tracks to agree.
 b=make_wait_fixture(31);assert(validate_wait_fixture(b,why));b[0x513]=30;assert(!validate_wait_fixture(b,why));
}
