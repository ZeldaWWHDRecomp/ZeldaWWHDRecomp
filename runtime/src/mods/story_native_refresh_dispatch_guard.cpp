// SPDX-License-Identifier: MPL-2.0
#include "story_native_refresh_dispatch_guard.h"
#include "story_native_refresh_dispatch_audit.h"
#include <array>
#include "story_native_canonical_code_id.h"
#include <utility>
namespace story_native_refresh_dispatch {
std::span<const uint32_t>required_functions()noexcept{
 static constexpr std::array<uint32_t,145>addresses{
  story_native_code_identity::CANONICAL_CODE_ID(0x02019510).value(),story_native_code_identity::CANONICAL_CODE_ID(0x022BE034).value(),story_native_code_identity::CANONICAL_CODE_ID(0x022BF3A0).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x025E55A0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x025E564C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x025E5C2C).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x025E5C38).value(),story_native_code_identity::CANONICAL_CODE_ID(0x025F181C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x025F1884).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x025F18EC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x025F1BF4).value(),story_native_code_identity::CANONICAL_CODE_ID(0x025F1C28).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x025F1C5C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DA84C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DA898).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027DA8D8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DA9E8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DAAA0).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027DAAB8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DB1D4).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DD9AC).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027DEAD0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DEC10).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DEFE4).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027DF00C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DF01C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DF108).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027DF8B4).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DF9B0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DFCF8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027DFE74).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027DFF1C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E00AC).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E0288).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E0368).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E04C8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E0560).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E0710).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E0738).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E0834).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E1A8C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E1B24).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E1C34).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E1CF0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E1D5C).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E1E00).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E1E9C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E2AF4).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E2BF4).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E2CFC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E4D20).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E50A8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E565C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E5DA4).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E6AA8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E6C04).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E6EA8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E7004).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E7164).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E72CC).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E7538).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E7A14).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E89FC).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E8C4C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E8E54).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9020).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E9194).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E91F0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E924C).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E92A8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9378).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E94D4).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E95E0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E96B0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9780).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E9850).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E98F8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E99A0).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E9A48).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9AE0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9B90).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027E9C38).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9C88).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027E9CD8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027ED2D0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027ED3AC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027F363C).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027F3B28).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027F3CD0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027F3F94).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x027F4D5C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x027F7878).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028E90D4).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028E9108).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028E93CC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F4034).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F4114).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F4384).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F4700).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F4DC4).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F4FAC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F5564).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F5680).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6038).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6044).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6050).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F605C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F60E0).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F60E8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F60F0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F60F8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6130).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F613C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6154).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6158).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F615C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6184).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6190).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61A8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61AC).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F61B0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61D0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61D8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F61DC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61E0).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61E4).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F61E8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61EC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61F0).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F61F4).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61F8).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F61FC).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6200).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6228).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6230).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6234).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6238).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F623C).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6240).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6244).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6248).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F624C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6250).value(),story_native_code_identity::CANONICAL_CODE_ID(0x028F6254).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x028F6258).value()
 };return addresses;
}
bool detail::audit(const View&v)noexcept{try{
 if(!v.hooks_sealed||!v.text_sealed||!v.probe_quiet||!v.registration_exact||!v.route)return false;
 for(const auto address:required_functions()){Route r;if(!v.route(address,r)||!r.canonical||r.current!=r.canonical||r.flagged||r.chain)return false;}
 return true;
}catch(...){return false;}}
Guard::Guard(std::function<bool()>verify):verify_(std::move(verify)){}
Guard::Guard(Guard&&other)noexcept:verify_(std::move(other.verify_)){other.verify_={};}
Guard&Guard::operator=(Guard&&other)noexcept{if(this!=&other){verify_=std::move(other.verify_);other.verify_={};}return *this;}
bool Guard::current()const noexcept{try{return verify_&&verify_();}catch(...){return false;}}
}
