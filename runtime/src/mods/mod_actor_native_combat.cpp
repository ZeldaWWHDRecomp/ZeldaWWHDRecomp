// SPDX-License-Identifier: MPL-2.0
#include "mod_actor_native_combat.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cmath>
#include <cstring>
namespace mod_actor_native_combat {namespace {
Invoke invoke=nullptr;VerifySource verify=nullptr;
constexpr uint32_t Magic=0x48445231;
bool span(uint32_t a,uint32_t n){return !(a&3)&&a>=mem::kMem2Start&&a<=mem::kMem2End-n;}
bool state_span(uint32_t a){return span(a,kHeartStateBytes)||(!(a&3)&&a>=mem::kRuntimeStart&&a<=mem::kHostStart-kHeartStateBytes);}
bool scratch(uint32_t a,uint32_t n){return span(a,n)||(!(a&3)&&a>=mem::kRuntimeStart&&a<=mem::kHostStart-n);}
struct Preserve {Cpu*c;Cpu saved;explicit Preserve(Cpu*p):c(p),saved(*p){}~Preserve(){*c=saved;}};
void disposition(uint32_t s,HeartDisposition d){st32(s+4,uint32_t(d));}
}
bool configure(Invoke i,VerifySource v){if(!i||!v)return false;if(invoke&&(invoke!=i||verify!=v))return false;invoke=i;verify=v;return true;}
bool validate_attack_spec(const AttackSpec&s){return std::isfinite(s.radius)&&std::isfinite(s.height)&&std::isfinite(s.forward_offset)&&std::isfinite(s.y_offset)&&s.radius>0&&s.radius<=200&&s.height>0&&s.height<=400&&std::fabs(s.forward_offset)<=300&&std::fabs(s.y_offset)<=300&&s.power>=1&&s.power<=4;}
bool write_attack_source(uint32_t p,const AttackSpec&s){if(!scratch(p,kAttackSourceBytes)||!validate_attack_spec(s))return false;std::memset(mem::ptr(p),0,kAttackSourceBytes);st32(p+4,kPhysicalAtType);st8(p+8,uint8_t(s.power));st32(p+0xC,kPhysicalAtFlags);st32(p+0x3C,f32_as_u32(s.radius));st32(p+0x40,f32_as_u32(s.height));return true;}
bool read_heart_state(uint32_t s,HeartState&out){if(!state_span(s)||ld32(s)!=Magic||ld32(s+4)>uint32_t(HeartDisposition::Admitted)||ld32(s+28)!=0)return false;out={HeartDisposition(ld32(s+4)),ld32(s+8),ld32(s+12),ld32(s+16),ld32(s+20),ld32(s+24)};return out.generation&&out.epoch&&out.source_pid&&out.source_pid!=0xFFFFFFFFu;}
HeartDisposition request_heart(Cpu*c,uint32_t s,const SourceIdentity&id,uint32_t pos,int32_t room){
 if(!c||!invoke||!verify||!state_span(s)||s!=id.heart_state||!span(id.actor,0x3AC)||pos!=id.actor+0x314||!id.pid||id.pid==0xFFFFFFFFu||!id.generation||!id.epoch||room<0||room>63)return HeartDisposition::Failed;
 Preserve cpu(c);bool valid=false;try{valid=verify(id);}catch(...){return HeartDisposition::Failed;}if(!valid)return HeartDisposition::Failed;
 for(unsigned i=0;i<3;++i)if(!std::isfinite(u32_as_f32(ld32(pos+i*4))))return HeartDisposition::Failed;
 HeartState prior{};if(ld32(s)){if(!read_heart_state(s,prior)||prior.source_pid!=id.pid||prior.generation!=id.generation||prior.epoch!=id.epoch)return HeartDisposition::Failed;return prior.disposition;}
 for(unsigned i=4;i<kHeartStateBytes;i+=4)if(ld32(s+i))return HeartDisposition::Failed;
 st32(s,Magic);disposition(s,HeartDisposition::Issuing);st32(s+12,id.pid);st32(s+16,id.generation);st32(s+20,id.epoch);
 uint32_t pid=0;try{pid=invoke(c,GC(0x025D8870),{pos,0,0xFFFFFFFFu,uint32_t(room),0,0,4,0});}catch(...){disposition(s,HeartDisposition::Failed);return HeartDisposition::Failed;}
 if(!pid||pid==0xFFFFFFFFu){disposition(s,HeartDisposition::Failed);return HeartDisposition::Failed;}
 st32(s+8,pid);disposition(s,HeartDisposition::Queued);return HeartDisposition::Queued;
}
bool admit_heart(uint32_t s,uint32_t a,uint32_t phase){if(phase!=4)return false;HeartState h{};if(!read_heart_state(s,h)||h.disposition!=HeartDisposition::Queued||!span(a,0x7F0)||ld32(a+4)!=h.queued_pid||ld16(a+0xE)!=0xFF||ld32(a+0xB0)!=0x10FFFF00||ld32(a+0xB4)!=GD(0x10011FF8)||ld8(a+0x74E)!=0||ld32(a+0x744)!=0x7F)return false;st32(s+24,a);disposition(s,HeartDisposition::Admitted);return true;}
}
