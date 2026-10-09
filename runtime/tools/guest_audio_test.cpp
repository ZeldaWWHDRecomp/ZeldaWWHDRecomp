#include "mods/guest_audio.h"
#include <cassert>
#include <thread>
#include <memory>
#include <vector>
using namespace guestmods::pcm;
int main() {
 auto storage=std::make_unique<Store>();auto& s=*storage;
 assert(s.open("a",44100,2)==kInvalid);assert(s.open("a",48000,0)==kInvalid);
 auto h=s.open("a",48000,1);assert(h>0);
 int16_t samples[kMaxSubmit];std::fill_n(samples,kMaxSubmit,10000);
 assert(s.submit("b",h,samples,1,1)==kInvalid);
 assert(s.close("b",h)==kInvalid);
 assert(s.submit("a",h,samples,1,2)==kInvalid);
 assert(s.submit("a",h,samples,kMaxSubmit+1,1)==kInvalid);
 assert(s.submit("a",h,nullptr,1,1)==kInvalid);
 for(int i=0;i<4;++i)assert(s.submit("a",h,samples,kMaxSubmit,1)==kMaxSubmit);
 assert(s.available("a",h)==0);assert(s.submit("a",h,samples,1,1)==0);
 int16_t out[8]={30000,-30000,0,0,0,0,0,0};s.mix(out,2);
 assert(out[0]==32767&&out[1]==-20000&&out[2]==10000&&out[3]==10000);
 assert(s.available("a",h)==2);assert(s.submit("a",h,samples,3,1)==2);
 s.mix(out,2,true);assert(s.available("a",h)==2);
 auto epoch=s.epoch();s.reset();assert(s.epoch()!=epoch);
 assert(s.available("a",h)==kInvalid);assert(s.close("a",h)==kInvalid);
 auto fresh=s.open("a",48000,2);assert(fresh>h);
 int16_t stereo[]={-30000,20000};assert(s.submit("a",fresh,stereo,1,2)==1);
 int16_t clean[4]={};s.mix(clean,2);assert(clean[0]==-30000&&clean[1]==20000&&clean[2]==0&&clean[3]==0);
 for(int i=1;i<4;++i)assert(s.open("a",48000,1)>0);assert(s.open("a",48000,1)==kQuota);
 s.drop("a");assert(s.available("a",fresh)==kInvalid);
 for(int i=0;i<32;++i)assert(s.open(std::to_string(i),48000,1)>0);
 assert(s.open("overflow",48000,1)==kQuota);s.reset();
 auto concurrent=s.open("thread",48000,1);
 std::thread producer([&]{for(int i=0;i<10000;++i){auto n=s.submit("thread",concurrent,samples,32,1);assert(n==kBusy||(n>=0&&n<=32));}});
 for(int i=0;i<10000;++i){int16_t block[64]={};s.mix(block,32);}
 producer.join();
}
