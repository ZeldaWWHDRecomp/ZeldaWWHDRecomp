// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_program.h"
#include "story_native_event_image.h"
#include <utility>
namespace story_native_event_program {
Program::Program(Kind k,uint32_t f,uint32_t w,std::optional<story_native_turn_camera_image::Program>t,std::vector<uint8_t>b):kind_(k),frames_(f),watchdog_(w),turn_(std::move(t)),image_(std::move(b)){}
std::optional<Program>Program::wait(uint32_t frames){
 if(!frames||frames>32767)return {};
 return Program(Kind::Wait,frames,0,{},story_native_event_image::make_wait_fixture(frames));
}
std::optional<Program>Program::turn(const story_native_turn_camera_image::Program&p,uint32_t watchdog){
 if(!watchdog||watchdog>18000||watchdog<=p.timekeeper_timer)return {};
 std::vector<uint8_t>b;std::string why;if(!story_native_turn_camera_image::make(p,b,why))return {};
 return Program(Kind::TurnCamera,p.wait_timer,watchdog,p,std::move(b));
}
std::array<uint32_t,8>Program::pool_offsets()const{
 if(kind_==Kind::Wait)return {0,0x40,0xF0,0x230,0x410,0,0x510,0};
 return {0,0x40,0xF0,0x230,0x500,0xD80,0xE00,0xE40};
}
uint32_t Program::initial_cut(unsigned staff)const{return staff>=4?0xFFFFFFFFu:kind_==Kind::TurnCamera&&staff<2?7+staff:staff;}
std::optional<Cut>Program::cut(unsigned staff,uint32_t index)const{
 if(staff>=4)return {};
 if(kind_==Kind::Wait){
  if((staff<3&&index!=staff)||(staff==3&&(index<3||index>5)))return {};
  return Cut{index,staff<2?0u:staff==2?1u:index==3?0u:1u,frames_};
 }
 const auto&p=*turn_;
 if(staff<2){if(index==staff)return Cut{index,0,p.wait_timer};if(index==7+staff)return Cut{index,3,1};return {};}
 if(staff==2){if(index==2)return Cut{index,4,0};if(index==6)return Cut{index,5,0};return {};}
 if(index<3||index>5)return {};
 return Cut{index,index==3?0u:1u,p.timekeeper_timer};
}
bool Program::immutable_matches(std::span<const uint8_t>b)const{
 if(b.size()!=image_.size())return false;
 for(size_t i=0;i<b.size();++i){bool mutable_byte=i>=0xE4&&i<0xE8;for(unsigned s=0;s<4;++s)mutable_byte|=i>=0xF0+s*0x50+0x38&&i<0xF0+s*0x50+0x48;if(!mutable_byte&&b[i]!=image_[i])return false;}
 return true;
}
}
