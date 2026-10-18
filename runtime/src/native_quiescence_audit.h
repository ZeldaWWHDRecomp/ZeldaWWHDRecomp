// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <thread>
namespace threads::detail {
// Data-only exclusive epoch bookkeeping. No parking or capability constructor.
// Actual threads.cpp uses it under its real freeze mutex and verifies registry.
enum class FreezeKind {None,Legacy,Native};
struct FreezeClaim {FreezeKind kind=FreezeKind::None;const void*owner=nullptr;uint64_t epoch=0;std::thread::id host;bool operator==(const FreezeClaim&)const=default;};
class FreezeOwnership {
 uint64_t epoch_=0;FreezeClaim active_{};
public:
 explicit FreezeOwnership(uint64_t initial=0):epoch_(initial){}
 bool claim(FreezeKind kind,const void*owner,FreezeClaim&out){out={};if(active_.epoch||epoch_==UINT64_MAX||kind==FreezeKind::None||!owner)return false;active_={kind,owner,++epoch_,std::this_thread::get_id()};out=active_;return true;}
 bool current(const FreezeClaim&c)const{return c.epoch&&active_==c&&c.host==std::this_thread::get_id();}
 bool release(const FreezeClaim&c){if(!current(c))return false;active_={};return true;}
};
}
