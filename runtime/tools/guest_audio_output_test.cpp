// Exercise the actual output producer in silent mode, using synthetic PCM only.
#include "audio_out.h"
#include "mods/guest_audio.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

// The saved master volume (issue #124); set() calls are recorded for the persistence checks.
static std::string g_saved; static bool g_has_saved=false;
static std::vector<std::pair<std::string,std::string>> g_sets;
namespace hostui {
bool get(const char* key,std::string& value) {
    if (g_has_saved&&!std::strcmp(key,"audioVolume")) { value=g_saved; return true; }
    return false;
}
void set(const char* key,const std::string& value) { g_sets.push_back({key,value}); }
}
namespace mods { bool fast_forward_mute(){return false;} }  // fast forward (mods/fast_forward.cpp): not exercised here
void log_msg(const char*,...) {}
static void env(const char* key,const char* value) {
#ifdef _WIN32
 _putenv_s(key,value);
#else
 setenv(key,value,1);
#endif
}
static void unenv(const char* key) {  // a saved setting must win over an inherited environment
#ifdef _WIN32
 _putenv_s(key,"");
#else
 unsetenv(key);
#endif
}
static int16_t sample(const char* path,long frame) {  // the left channel of one stereo frame
 std::ifstream f(path,std::ios::binary);f.seekg(44+frame*4);int16_t value=0;f.read(reinterpret_cast<char*>(&value),2);return value;
}
int main(int argc,char** argv) {
 assert(argc==3);const std::string mode=argv[2];
 env("WWHD_NO_AUDIO","1");env("WWHD_AUDIO_DUMP",argv[1]);
 if (mode=="gain") env("WWHD_AUDIO_VOLUME","0.5");
 if (mode=="mute") env("WWHD_AUDIO_VOLUME","0");
 if (mode=="volume") env("WWHD_AUDIO_VOLUME","1");
 if (mode=="saved"||mode=="invalid") { unenv("WWHD_AUDIO_VOLUME"); g_has_saved=true; g_saved=mode=="saved"?"25":"not a number"; }
 audio::init();

 // The saved volume starts the session; a broken value falls back to full volume.
 if (mode=="saved"||mode=="invalid") {
  assert(audio::master_volume()==(mode=="saved"?0.25f:1.0f));
  std::vector<int16_t> source(48000*2,30000);audio::push(source.data(),48000);
  audio::finish_dump();
  assert(sample(argv[1],0)==(mode=="saved"?7500:30000));
  std::remove(argv[1]);
  return 0;
 }

 // Live changes take effect on the next mix, clamp to 0..1 and are saved as a percentage.
 if (mode=="volume") {
  assert(audio::master_volume()==1.0f);
  std::vector<int16_t> source(48000*2,30000);
  audio::push(source.data(),48000);
  audio::set_master_volume(0.5f);assert(audio::master_volume()==0.5f);
  audio::push(source.data(),48000);
  audio::set_master_volume(2.0f);assert(audio::master_volume()==1.0f);
  audio::push(source.data(),48000);
  audio::set_master_volume(0.0f);assert(audio::master_volume()==0.0f);
  audio::push(source.data(),48000);
  audio::finish_dump();
  assert(sample(argv[1],0)==30000&&sample(argv[1],48000)==15000);
  assert(sample(argv[1],96000)==30000&&sample(argv[1],144000)==0);
  assert(g_sets.size()==3);
  for (size_t i=0;i<g_sets.size();i++) assert(g_sets[i].first=="audioVolume");
  assert(g_sets[0].second=="50"&&g_sets[1].second=="100"&&g_sets[2].second=="0");
  std::remove(argv[1]);
  return 0;
 }

 bool muted=mode=="mute";auto& s=guestmods::pcm::store();auto h=s.open("synthetic",48000,1);assert(h>0);
 int16_t mono[144];std::fill_n(mono,144,10000);assert(s.submit("synthetic",h,mono,144,1)==144);
 std::vector<int16_t> source(48000*2,30000);audio::push(source.data(),48000);
 assert(source[0]==30000&&s.available("synthetic",h)==8192);
    audio::finish_dump();
 std::ifstream f(argv[1],std::ios::binary);char header[44];f.read(header,44);assert(!std::memcmp(header,"RIFF",4));
 int16_t first[2];f.read(reinterpret_cast<char*>(first),4);assert(first[0]==(muted?0:16383)&&first[1]==first[0]);
 f.seekg(44+144*4);f.read(reinterpret_cast<char*>(first),4);assert(first[0]==(muted?0:15000));
 auto epoch=s.epoch();audio::flush();assert(s.epoch()!=epoch&&s.available("synthetic",h)==-1);
 f.close();std::remove(argv[1]);
}
