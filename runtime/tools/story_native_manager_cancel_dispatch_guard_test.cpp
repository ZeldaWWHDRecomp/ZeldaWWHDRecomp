// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_manager_cancel_dispatch_guard.h"
#include "mods/story_native_manager_cancel_dispatch_audit.h"
#include <cassert>
#include <map>
#include <thread>
#include "mods/story_native_cancel_dispatch_lifetime.h"
#include <algorithm>
#include <vector>
#include <cstdint>
#include <utility>
#include <functional>
#include <memory>
namespace threads {class NativeQuiescenceIssuer {public:static NativeQuiescence make(std::function<void()>r){return NativeQuiescence([](const Cpu*){return true;},std::move(r));}};}
namespace S=story_native_manager_cancel_dispatch;
namespace story_native_manager_cancel_dispatch {struct Fixture{static Guard make(std::function<bool()>f){return Guard(std::move(f));}};}
void put(auto&b,unsigned p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-8*i));}
int main(){for(bool exceptional:{false,true}){story_native_stage_owner::Tracker tracker;bool released=false;
 std::shared_ptr<const threads::NativeQuiescence>q=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::make([&]{bool unlocked=false;std::thread other([&]{auto&m=tracker.lifetime_fence();unlocked=m.try_lock();if(unlocked)m.unlock();});other.join();assert(unlocked);released=true;}));
 auto stage=std::make_shared<story_native_stage_owner::Tracker::Scan>(tracker.scan());
 try{auto held=std::make_shared<S::detail::PinnedStage>(std::move(stage),std::move(q));assert(!stage&&!q);if(exceptional)throw 1;}catch(int){}assert(released);
 }
assert(S::required_functions().size()==8);bool live=true;auto g=S::Fixture::make([&]{return live;});assert(g.current());auto moved=std::move(g);assert(!g.current()&&moved.current());live=false;assert(!moved.current());
 for(unsigned bad=0;bad<9;++bad){std::map<uint32_t,std::vector<uint8_t>>m;const uint32_t h=0x101F3328,a=0x47000000,b=0x47001000;m[h].resize(12);m[a+0xC8].resize(20);m[b+0xC8].resize(20);put(m[h],0,a+0xC8);put(m[h],4,b+0xC8);put(m[h],8,2);put(m[a+0xC8],4,h);put(m[b+0xC8],4,h);put(m[b+0xC8],0,a+0xC8);m[a+0xC8][16]=m[b+0xC8][16]=1;put(m[a+0xC8],8,b+0xC8);put(m[a+0xC8],12,a);put(m[b+0xC8],12,b);
  if(bad==1)put(m[h],8,4097);if(bad==2)put(m[b+0xC8],8,a+0xC8);if(bad==3)put(m[h],4,a+0xC8);if(bad==4)put(m[a+0xC8],12,b);if(bad==5)put(m[h],0,a+0xC9);if(bad==6)put(m[h],8,1);if(bad==7)m.erase(b+0xC8);
  unsigned reads=0;S::detail::Read read=[&](auto p,auto out){++reads;auto it=m.find(p);if(it==m.end()||it->second.size()!=out.size())return false;std::copy(it->second.begin(),it->second.end(),out.begin());if(bad==8&&reads>3)out[0]^=1;return true;};S::detail::Census c;assert(S::detail::census(read,h,c)==(bad==0));
 }
 S::detail::Census c;assert(!S::detail::census([](auto,auto)->bool{throw 1;},0x101F3328,c));
}
