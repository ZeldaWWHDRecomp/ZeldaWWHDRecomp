// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
namespace story_switches {
inline constexpr uint16_t kSwitchProfile=0x7FFC,kDoorProfile=0x7FFB;
inline constexpr uint32_t kSwitchClassBytes=0x4B4,kDoorClassBytes=0x3C0;
enum class Kind { Switch,Door };
constexpr uint32_t parameters(Kind kind){return kind==Kind::Switch?0x0000FFFFu:0xFFu;}
constexpr bool valid_door_state(unsigned action,int angle){return action<=4&&angle>=0&&angle<=0x4000;}
using ReadFlag=std::function<bool(uint32_t switch_token,bool& value)>;
using WriteFlag=std::function<bool(uint32_t switch_token,bool value)>;
using Retired=std::function<void(uint32_t token,uint32_t old_pid)>;
uint32_t declare_switch(const std::string& mod,const std::string& id,std::string& why);
// Source token must name a declared switch belonging to the same mod.
uint32_t declare_door(const std::string& mod,const std::string& id,uint32_t source,std::string& why);
size_t checkpoint();
void rollback(size_t checkpoint) noexcept;
// restored=true only after caller validates the captured declaration fingerprint.
void bind(uint32_t token,uint32_t pid,bool restored=false);
bool available(uint32_t token);
void retry(uint32_t token);
void quarantine(uint32_t token);
void state_loaded();
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),
               uint32_t switch_profile,uint32_t door_profile,ReadFlag,WriteFlag,Retired={});
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
}
