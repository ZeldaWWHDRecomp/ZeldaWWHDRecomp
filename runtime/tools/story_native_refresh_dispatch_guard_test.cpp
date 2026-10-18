// SPDX-License-Identifier: MPL-2.0
#define WWHD_NATIVE_DISPATCH_GUARD_TEST 1
#include "mods/story_native_refresh_dispatch_guard.h"
#include "mods/story_native_refresh_dispatch_audit.h"
#include "native_text_startup_seal.h"
#include <cassert>
#include <map>
#include <future>
#include <thread>
#include <atomic>
#include <type_traits>
#include <cstdint>
#include <utility>
#include <functional>
namespace S=story_native_refresh_dispatch;
namespace story_native_refresh_dispatch {struct Fixture {static Guard make(std::function<bool()>v){return Guard(std::move(v));}};}
void native(Cpu*){} void other(Cpu*){}
int main(){
 static_assert(!std::is_copy_constructible_v<S::Guard>);
 dispatch::detail::StartupSeal seal;assert(!seal.current()&&!seal.seal());assert(seal.begin_initialize()&&seal.writable()&&seal.seal()&&seal.current());assert(!seal.writable()&&!seal.current());assert(!seal.seal());
 dispatch::detail::StartupSeal repeated;assert(repeated.begin_initialize());assert(!repeated.begin_initialize()&&!repeated.seal());
 // The actual seal lock drains a mutator that already passed writable().
 dispatch::detail::StartupSeal concurrent;assert(concurrent.begin_initialize());
 std::promise<void>entered,release,seal_entered;auto go=release.get_future();std::atomic<bool>sealed=false;int table=0;
 std::thread writer([&]{auto held=concurrent.lock();assert(concurrent.writable());entered.set_value();go.wait();table=7;});
 entered.get_future().wait();std::thread sealer([&]{seal_entered.set_value();auto held=concurrent.lock();assert(concurrent.seal());assert(table==7);sealed=true;});
 seal_entered.get_future().wait();assert(!sealed.load());release.set_value();writer.join();sealer.join();assert(sealed&&concurrent.current());
 {auto held=concurrent.lock();assert(!concurrent.writable());}assert(table==7&&!concurrent.current());
 std::map<uint32_t,S::detail::Route>routes;for(auto a:S::required_functions())routes[a]={native,native,false,false};assert(routes.size()==145);
 S::detail::View view{true,true,true,true,[&](auto a,auto&r){auto it=routes.find(a);if(it==routes.end())return false;r=it->second;return true;}};
 auto guard=S::Fixture::make([&]{return S::detail::audit(view);});assert(guard.current());
 for(auto&[a,r]:routes){r.flagged=true;assert(!guard.current());r.flagged=false;r.chain=true;assert(!guard.current());r.chain=false;r.current=other;assert(!guard.current());r.current=native;assert(guard.current());}
 view.probe_quiet=false;assert(!guard.current());view.probe_quiet=true;view.registration_exact=false;assert(!guard.current());view.registration_exact=true;view.hooks_sealed=false;assert(!guard.current());view.hooks_sealed=true;view.text_sealed=false;assert(!guard.current());view.text_sealed=true;
 auto moved=std::move(guard);assert(moved.current()&&!guard.current());routes.erase(routes.begin());assert(!moved.current());
 auto throwing=S::Fixture::make([]()->bool{throw 1;});assert(!throwing.current());
}
