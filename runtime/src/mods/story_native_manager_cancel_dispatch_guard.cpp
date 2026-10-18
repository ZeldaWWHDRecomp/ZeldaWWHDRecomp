// SPDX-License-Identifier: MPL-2.0
#include "story_native_manager_cancel_dispatch_guard.h"
#include <array>
#include "story_native_canonical_code_id.h"
#include <utility>
namespace story_native_manager_cancel_dispatch {
std::span<const uint32_t>required_functions()noexcept{static constexpr std::array<uint32_t,8>a{
  story_native_code_identity::CANONICAL_CODE_ID(0x0254465C).value(),story_native_code_identity::CANONICAL_CODE_ID(0x02544044).value(),story_native_code_identity::CANONICAL_CODE_ID(0x02543708).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x025D5218).value(),story_native_code_identity::CANONICAL_CODE_ID(0x020100BC).value(),story_native_code_identity::CANONICAL_CODE_ID(0x02019DE8).value(),
  story_native_code_identity::CANONICAL_CODE_ID(0x0201AA08).value(),story_native_code_identity::CANONICAL_CODE_ID(0x02542C9C).value()
 };return a;}
Guard::Guard(std::function<bool()>v):verify_(std::move(v)){}
Guard::Guard(Guard&&o)noexcept:verify_(std::move(o.verify_)){o.verify_={};}
Guard&Guard::operator=(Guard&&o)noexcept{if(this!=&o){verify_=std::move(o.verify_);o.verify_={};}return *this;}
bool Guard::current()const noexcept{try{return verify_&&verify_();}catch(...){return false;}}
}
#include "story_native_manager_cancel_dispatch_audit.h"
namespace story_native_manager_cancel_dispatch::detail {
namespace {uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}}
bool census(const Read&read,uint32_t global,Census&out)noexcept{out={};try{
 if(!read||!read(global,out.header))return false;
 const auto count=word(out.header,8),head=word(out.header,0),tail=word(out.header,4);
 if(count>4096||(!count&&(head||tail))||(count&&(!head||!tail)))return false;
 auto node=head;for(unsigned n=0;n<count;++n){
  if(!node||(node&3)||node<0x10000000u||node>0x50000000u-20)return false;
  for(const auto&v:out.nodes)if(v.first==node)return false;
  std::array<uint8_t,20>bytes{};if(!read(node,bytes))return false;
  const auto actor=word(bytes,12);if((actor&3)||actor<0x10000000u||actor>0x50000000u-0xDC||uint64_t(actor)+0xC8!=node)return false;
  if(word(bytes,0)!=(n?out.nodes.back().first:0)||word(bytes,4)!=global||bytes[16]!=1)return false;
  out.nodes.push_back({node,bytes});node=word(bytes,8);
 }
 if(node||(count&&out.nodes.back().first!=tail))return false;
 std::array<uint8_t,12>after{};if(!read(global,after)||after!=out.header)return false;
 for(const auto&v:out.nodes){std::array<uint8_t,20>b{};if(!read(v.first,b)||b!=v.second)return false;}
 return true;
}catch(...){out={};return false;}}
}
