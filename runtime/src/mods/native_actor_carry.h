// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
namespace native_actor_carry {
using Guard = bool(*)(uint32_t candidate); // read-only ownership predicate; no guest/native calls
using DeniedTrace = void(*)(uint32_t candidate); // optional host-only diagnostic
bool configure(void(*original)(Cpu*,uint32_t),uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>));
// Configure every module before install. Only an identical registration may repeat afterwards.
bool register_guard(const std::string& name,Guard,DeniedTrace=nullptr);
bool available(const std::function<std::string(uint32_t)>& owner);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
}
