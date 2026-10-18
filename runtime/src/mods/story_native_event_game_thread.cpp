// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_game_thread.h"
#include "runtime.h"
#include "story_native_restore_resume.h"
#include <limits>
#include <mutex>
#include <thread>
namespace story_native_event_game_thread {namespace {
std::mutex lock;
const Cpu* cpu=nullptr;
std::thread::id host;
uint32_t guest=0;
uint64_t epoch=1;
bool fault=false;
bool actual(const Cpu*c,uint32_t&g) noexcept {
 g=threads::current_thread();
 return c&&g&&threads::current()==c;
}
bool same(const Cpu*c,uint32_t g) noexcept {
 return !fault&&cpu==c&&guest==g&&host==std::this_thread::get_id();
}
}
bool observe_logic(const Cpu*c) noexcept {
 uint32_t g=0;if(!actual(c,g))return false;
 std::lock_guard<std::mutex> guard(lock);
 if(fault)return false;
 if(!cpu){cpu=c;guest=g;host=std::this_thread::get_id();return true;}
 if(same(c,g))return true;
 // A second executor within one load epoch makes game-thread authority
 // ambiguous. Do not silently rebind even if the old thread has exited.
 fault=true;return false;
}
bool verify(const Cpu*c,Ticket t) noexcept {
 uint32_t g=0;if(!actual(c,g))return false;
 std::lock_guard<std::mutex> guard(lock);
 return t.epoch&&t.epoch==epoch&&t.guest==g&&same(c,g);
}
Ticket ticket(const Cpu*c) noexcept {
 uint32_t g=0;if(!actual(c,g))return {};
 std::lock_guard<std::mutex> guard(lock);
 return same(c,g)?Ticket{epoch,g}:Ticket{};
}
uint64_t current_load_epoch() noexcept {std::lock_guard<std::mutex>guard(lock);return epoch;}
bool reissue_after_restore(const Cpu*c,const story_native_restore_resume::Boundary&b)noexcept{
 if(!b.current(c))return false;uint32_t g=0;if(!actual(c,g))return false;
 std::lock_guard<std::mutex>guard(lock);
 if(fault||cpu||b.load_epoch()!=epoch)return false;
 cpu=c;guest=g;host=std::this_thread::get_id();return true;
}
void state_loaded() noexcept {
 std::lock_guard<std::mutex> guard(lock);
 cpu=nullptr;guest=0;host={};
 if(epoch==std::numeric_limits<uint64_t>::max())fault=true;
 else {++epoch;fault=false;}
}
}
