// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_game_thread.h"
#include <cassert>
#include <thread>
#include <cstdint>
struct Cpu {};
namespace {
thread_local Cpu* running=nullptr;
thread_local uint32_t guest=0;
}
namespace threads {
Cpu* current(){return running;}
uint32_t current_thread(){return guest;}
}
int main(){
 using namespace story_native_event_game_thread;
 Cpu main{},other{};
 assert(!observe_logic(&main));
 running=&main;guest=0x20001000;
 assert(observe_logic(&main));
 const Ticket first=ticket(&main);
 assert(first.epoch&&first.guest==guest&&verify(&main,first));
 assert(!ticket(&other).epoch&&!verify(&other,first));
 running=&other;
 assert(!verify(&main,first));
 running=&main;
 assert(verify(&main,first));
 std::thread foreign([&]{running=&main;guest=first.guest;assert(!verify(&main,first));assert(!observe_logic(&main));});
 foreign.join();
 assert(!verify(&main,first)&&!observe_logic(&main)); // ambiguity sticks
 state_loaded();
 assert(!verify(&main,first)&&!ticket(&main).epoch);
 assert(observe_logic(&main));
 const Ticket second=ticket(&main);
 assert(second.epoch!=first.epoch&&verify(&main,second)&&!verify(&main,first));
 guest=0x20002000;
 assert(!verify(&main,second)&&!observe_logic(&main));
 state_loaded();
 running=nullptr;
 assert(!observe_logic(&main));
}
