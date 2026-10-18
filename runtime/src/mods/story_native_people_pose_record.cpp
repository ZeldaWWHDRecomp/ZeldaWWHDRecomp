// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_pose_record.h"
#include "story_native_event_record.h"
#include "mod_hash.h"
#include <algorithm>
namespace story_native_people_pose_record {namespace {
constexpr uint32_t Magic=0x4E505231;
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::vector<uint8_t>&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
uint32_t checksum(std::span<const uint8_t>b){uint32_t h=2166136261u;for(size_t p=0;p<b.size();++p)h=(h^(p>=12&&p<16?0:b[p]))*16777619u;return h;}
bool span(uint32_t p,uint32_t n){return !(p&3)&&p>=0x10000000&&uint64_t(p)+n<=0x50000000;}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;}
bool nonzero(const auto&b){return std::any_of(b.begin(),b.end(),[](uint8_t v){return v!=0;});}
bool binding(const story_native_event_observation::Expected&e,const Definition&definition,Definition&out){
 if(!nonzero(definition))return false;
 std::vector<uint8_t>canonical;if(!story_native_event_record::encode(e,story_native_event_lease::Phase::Prepared,canonical))return false;
 const std::string data(reinterpret_cast<const char*>(canonical.data()),canonical.size());const auto hash=mods::hash::sha256_text(data);
 auto digit=[](char c){return uint8_t(c<='9'?c-'0':c-'a'+10);};
 for(unsigned n=0;n<32;++n)out[n]=uint8_t(digit(hash[2*n])*16+digit(hash[2*n+1]));return true;
}
bool valid(const story_native_event_observation::Expected&e,const Poses&poses){
 for(unsigned n=0;n<2;++n){const auto&s=poses[n];const auto&i=s.identity;const auto&a=e.actors[n];const auto&event=e.identity;
 if(i.actor!=a.address||i.pid!=a.pid||i.token!=a.token||i.stage!=event.stage||i.room!=event.room||i.generation!=event.generation||s.cut!=a.cut||s.cut[0x60]>1||s.cut[0x61]>1||!span(i.actor,0x920)||!span(i.morf,0xC8)||!span(i.model,0xBC))return false;
 // A baseline must not already borrow the event image or another actor's
 // attention/joint object. These are the same pointer roles used by capture.
 const uint32_t name=word(s.cut,0),actor=word(s.cut,8),talk=word(s.cut,12),joint=word(s.cut,0x68);
 if((uint64_t(name)>=event.image&&uint64_t(name)<uint64_t(event.image)+event.bytes)||(actor&&actor!=i.actor)||(talk&&talk!=i.actor)||(joint&&joint!=i.actor+0x3AC))return false;
 }
 // Native actor, Morf and model objects are separate allocations. Reject all
 // cross-object aliasing, including an actor pointing into its peer's model.
 std::array<std::pair<uint32_t,uint32_t>,6>regions{};
 for(unsigned n=0;n<2;++n){const auto&i=poses[n].identity;regions[n*3]={i.actor,0x920};regions[n*3+1]={i.morf,0xC8};regions[n*3+2]={i.model,0xBC};}
 for(unsigned a=0;a<regions.size();++a)for(unsigned b=a+1;b<regions.size();++b)if(overlap(regions[a].first,regions[a].second,regions[b].first,regions[b].second))return false;
 return true;
}
struct Writer {
 std::vector<uint8_t>&b;
 void u(uint32_t v){const size_t p=b.size();b.resize(p+4);put(b,p,v);}
 template<size_t N>void a(const std::array<uint8_t,N>&v){b.insert(b.end(),v.begin(),v.end());}
};
struct Reader {
 std::span<const uint8_t>b;size_t p=24;bool ok=true;
 uint32_t u(){if(p+4>b.size()){ok=false;return 0;}auto v=word(b,p);p+=4;return v;}
 template<size_t N>void a(std::array<uint8_t,N>&v){if(p+N>b.size()){ok=false;return;}std::copy_n(b.begin()+p,N,v.begin());p+=N;}
};
void pose(Writer&w,const story_native_people_pose_plan::Snapshot&s){
 const auto&i=s.identity;for(auto v:{i.actor,i.pid,i.token,i.stage,i.room,i.generation,i.morf,i.model})w.u(v);
 w.a(s.angles);w.a(s.shape);w.a(s.joint);w.a(s.cut);w.a(s.target);w.a(s.velocity);
 w.b.push_back(s.head_only);w.b.push_back(s.noticed);w.b.push_back(s.talk_ready);w.b.push_back(s.target_mode);w.u(s.status);
}
void pose(Reader&r,story_native_people_pose_plan::Snapshot&s){
 auto&i=s.identity;i.actor=r.u();i.pid=r.u();i.token=r.u();i.stage=r.u();i.room=r.u();i.generation=r.u();i.morf=r.u();i.model=r.u();
 r.a(s.angles);r.a(s.shape);r.a(s.joint);r.a(s.cut);r.a(s.target);r.a(s.velocity);
 std::array<uint8_t,4>bytes{};r.a(bytes);s.head_only=bytes[0];s.noticed=bytes[1];s.talk_ready=bytes[2];s.target_mode=bytes[3];s.status=r.u();
}
}
bool encode(const story_native_event_observation::Expected&e,const Definition&definition,const Poses&poses,std::vector<uint8_t>&out){
 out.clear();try{Definition hash{};if(!binding(e,definition,hash)||!valid(e,poses))return false;
 std::vector<uint8_t>b;Writer w{b};w.u(Magic);w.u(kVersion);w.u(kBytes);w.u(0);w.u(0);w.u(0);w.a(definition);w.a(hash);for(const auto&a:e.actors)w.a(a.fingerprint);for(const auto&s:poses)pose(w,s);
 if(b.size()!=kBytes||b.size()>kMaximumBytes)return false;put(b,12,checksum(b));out=std::move(b);return true;}catch(...){return false;}
}
bool decode(std::span<const uint8_t>in,const story_native_event_observation::Expected&e,const Definition&definition,Poses&out){
 out={};if(in.size()!=kBytes||word(in,0)!=Magic||word(in,4)!=kVersion||word(in,8)!=kBytes||word(in,12)!=checksum(in)||word(in,16)||word(in,20))return false;
 try{Definition expected_hash{},stored_definition{},stored_hash{};if(!binding(e,definition,expected_hash))return false;Reader r{in};r.a(stored_definition);r.a(stored_hash);
 if(stored_definition!=definition||stored_hash!=expected_hash)return false;
 for(const auto&a:e.actors){Definition fingerprint{};r.a(fingerprint);if(fingerprint!=a.fingerprint)return false;}
 Poses poses{};for(auto&s:poses)pose(r,s);if(!r.ok||r.p!=in.size()||!valid(e,poses))return false;out=std::move(poses);return true;}catch(...){return false;}
}
}
