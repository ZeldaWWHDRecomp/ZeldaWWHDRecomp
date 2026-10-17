// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <string>
struct Cpu;
namespace mod_actor::attention {
enum class Priority:uint32_t {None,Low,Normal,High};
// Pure bounded field reads only: validate live declaration/PID/combat state.
// None means ordinary native behavior, not an owned combat candidate.
using Verify=Priority(*)(uint32_t actor);
bool configure(void(*original)(Cpu*,uint32_t),Verify);
bool available(const std::function<std::string(uint32_t)>& owner);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
}
