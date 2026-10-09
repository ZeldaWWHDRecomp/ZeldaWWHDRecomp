// Exercise the actual output producer in silent mode, using synthetic PCM only.
#include "audio_out.h"
#include "mods/guest_audio.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
namespace mods { bool fast_forward_mute(){return false;} }  // fast forward (mods/fast_forward.cpp): not exercised here
void log_msg(const char*,...) {}
static void env(const char* key,const char* value) {
#ifdef _WIN32
 _putenv_s(key,value);
#else
 setenv(key,value,1);
#endif
}
int main(int argc,char** argv) {
 assert(argc==3);bool muted=std::strcmp(argv[2],"mute")==0;
 env("WWHD_NO_AUDIO","1");env("WWHD_AUDIO_VOLUME",muted?"0":"0.5");env("WWHD_AUDIO_DUMP",argv[1]);
 audio::init();auto& s=guestmods::pcm::store();auto h=s.open("synthetic",48000,1);assert(h>0);
 int16_t mono[144];std::fill_n(mono,144,10000);assert(s.submit("synthetic",h,mono,144,1)==144);
 std::vector<int16_t> source(48000*2,30000);audio::push(source.data(),48000);
 assert(source[0]==30000&&s.available("synthetic",h)==8192);
 std::ifstream f(argv[1],std::ios::binary);char header[44];f.read(header,44);assert(!std::memcmp(header,"RIFF",4));
 int16_t first[2];f.read(reinterpret_cast<char*>(first),4);assert(first[0]==(muted?0:16383)&&first[1]==first[0]);
 f.seekg(44+144*4);f.read(reinterpret_cast<char*>(first),4);assert(first[0]==(muted?0:15000));
 auto epoch=s.epoch();audio::flush();assert(s.epoch()!=epoch&&s.available("synthetic",h)==-1);
 f.close();std::remove(argv[1]);
}
