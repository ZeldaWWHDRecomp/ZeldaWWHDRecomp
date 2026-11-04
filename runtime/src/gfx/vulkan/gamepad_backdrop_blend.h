#pragma once
#include <cstdint>
namespace gfxvk {
struct BackdropBlend { bool supported=false,opaque=false; uint32_t control=0; };
// RGB follows the native opaque/source-over equation. Alpha instead records
// coverage, not the native RGB factors (which could square source alpha).
inline BackdropBlend backdrop_blend(uint32_t colorControl,uint32_t blend,uint32_t mask) {
 if(((colorControl>>16)&255)!=0xCC || (mask&7)!=7 || (blend&(1u<<13)))return {};
 if(!(colorControl&(1u<<8)))
  return {true,true,1u|(1u<<29)|(1u<<24)}; // RGB=source; preserve clear alpha=1
 const uint32_t src=blend&31,dst=(blend>>8)&31,op=(blend>>5)&7;
 if((src!=4&&src!=1)||dst!=5||op!=0)return {};
 return {true,false,(blend&0x1FFFu)|(1u<<29)|(1u<<16)|(5u<<24)};
}
}
