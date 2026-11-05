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
// The capture replay's alpha override, applied to exactly these registers of
// the draw's own register file and restored on scope exit (exceptions too):
// no copy of the 64K-register file per captured draw.
class BackdropRegisterPatch {
 uint32_t* regs_;
 uint32_t index_[3],saved_[3];
public:
 BackdropRegisterPatch(uint32_t* regs,uint32_t blend0,uint32_t colorControl,uint32_t targetMask,const BackdropBlend& blend)
  : regs_(regs),index_{blend0,colorControl,targetMask},saved_{regs[blend0],regs[colorControl],regs[targetMask]} {
  regs[blend0]=blend.control;
  regs[colorControl]|=1u<<8;
  regs[targetMask]|=8;
 }
 ~BackdropRegisterPatch() { for(int i=2;i>=0;--i)regs_[index_[i]]=saved_[i]; }
 BackdropRegisterPatch(const BackdropRegisterPatch&)=delete;
 BackdropRegisterPatch& operator=(const BackdropRegisterPatch&)=delete;
};
}
