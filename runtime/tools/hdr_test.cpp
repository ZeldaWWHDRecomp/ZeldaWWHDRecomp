#include "gfx/hdr.h"
#include <array>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>
int main() {
 // Every offered-format combination and requested preference, including neither.
 for(bool scrgb:{false,true})for(bool pq:{false,true})for(int preference=0;preference<3;++preference) {
  const auto expected=!scrgb&&!pq?hdr::Output::Sdr:
      pq&&(!scrgb||preference==2)?hdr::Output::Pq:hdr::Output::Scrgb;
  assert(hdr::choose_format(static_cast<hdr::Format>(preference),scrgb,pq)==expected);
 }
 for(std::string_view invalid:{"", "old", "1garbage", "3", "-1", "1.5", "nan", "inf"})assert(hdr::setting(invalid)==0);
 assert(hdr::setting(std::string_view("1"))==1&&hdr::setting(std::string_view("2"))==2);
 for(int curve=0;curve<3;++curve) {
  const auto choice=static_cast<hdr::SdrCurve>(curve);
  assert(hdr::decode(-.1f,choice)==0);
  assert(std::abs(hdr::decode(1.2f,choice)-(curve==2?1.f:std::pow(1.2f,curve==0?2.2f:2.4f)))<1e-7f);
  for(auto input:{std::array<float,3>{0,0,0},{.05f,.05f,.05f},{.5f,.5f,.5f},{1,1,1},{.05f,.4f,1}}) {
   auto decoded=hdr::decode(input,choice);
   for(int k=0;k<3;++k) {
    const float v=input[k];
    const float expected=curve==0?std::pow(v,2.2f):curve==1?std::pow(v,2.4f):v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);
    assert(std::abs(decoded[k]-expected)<1e-7f);
   }
   auto mapped=hdr::map(decoded,hdr::parameters());
   auto encoded=hdr::encode_pq(mapped);
   for(float v:encoded)assert(std::isfinite(v)&&v>=0&&v<=1);
  }
  for(float v:{.05f,.5f}) {
   auto mapped=hdr::map(hdr::decode(std::array<float,3>{v,v,v},choice),hdr::parameters());
   printf("curve=%d code=%.2f scRGB=%.9f\n",curve,v,mapped[0]);
  }
 }
 // A saved HDR preference must never prevent startup on an SDR display.
 for(bool display:{false,true})for(bool format:{false,true}) {
  hdr::enabled=true;hdr::displayEnabled=display;hdr::formatSupported=format;
  const bool available=display&&format;
  assert(hdr::resolve_output()==available);
  assert(hdr::enabled.load()==available);
  assert(hdr::fellBackToSdr.load()==!available);
  assert(hdr::resolve_output()==available); // no repeated swapchain request after fallback
 }
 hdr::enabled=false;hdr::displayEnabled=true;hdr::formatSupported=true;
 assert(!hdr::resolve_output()); // SDR remains explicit even on a capable display
 // The presentation path could not draw HDR on a capable display: SDR from the next swapchain on,
 // reported, and not re-requested by resolve_output until the user picks HDR again.
 hdr::enabled=true;assert(hdr::resolve_output());
 hdr::fall_back_to_sdr();
 assert(!hdr::enabled.load()&&hdr::fellBackToSdr.load());
 assert(!hdr::resolve_output()&&hdr::fellBackToSdr.load());
 for(float paper:{80.f,200.f,500.f})for(float peak:{500.f,1000.f,4000.f})
  for(float ev:{-3.f,0.f,3.f})for(float boost:{0.f,.5f,1.f}) {
   hdr::Parameters p{paper,peak,ev,boost};
   assert((hdr::map({0,0,0},p)==std::array<float,3>{0,0,0}));
   float last=-1;
   for(int i=0;i<=1000;++i) {
    float v=i/1000.f;auto c=hdr::map({v*.1f,v*.4f,v},p);
    assert(std::isfinite(c[2])&&c[2]+.00001f>=last&&c[2]<=peak/80.f+.001f);
    assert(std::abs(c[0]-c[2]*.1f)<.001f&&std::abs(c[1]-c[2]*.4f)<.001f);
    last=c[2];
   }
  }
 auto p=hdr::Parameters{200,1000,0,0};
 assert(std::abs(hdr::map({.18f,.18f,.18f},p)[0]-.45f)<.00001f);
 assert(hdr::map({1,1,1},{200,1000,0,.5f})[0]>200/80.f);
 // HDR10: ST 2084 reference points (BT.2100) and the BT.709 -> BT.2020 primaries.
 assert(hdr::pq(0)<1e-6f&&std::abs(hdr::pq(100)-.5081f)<.0005f);
 assert(std::abs(hdr::pq(1000)-.7518f)<.0005f&&std::abs(hdr::pq(10000)-1)<1e-5f&&hdr::pq(20000)==hdr::pq(10000));
 auto white=hdr::encode_pq({1.25f,1.25f,1.25f}); // 100 cd/m^2 white stays neutral
 for(float v:white)assert(std::abs(v-hdr::pq(100))<.0005f);
 auto red=hdr::encode_pq({1,0,0});
 assert(std::abs(red[0]-hdr::pq(80*.627404f))<1e-5f&&std::abs(red[1]-hdr::pq(80*.069097f))<1e-5f);
 // Metadata: mastering uses Peak; content uses mapped white or capped menu white.
 for(float paper:{80.f,203.f,500.f})for(float peak:{200.f,1150.f,4000.f})for(float boost:{0.f,.3f,1.f}) {
  const hdr::Parameters q{std::min(paper,peak),peak,0,boost};
  const auto m=hdr::metadata(q,160);
  assert(m.maxCll==std::max(hdr::map({1,1,1},q)[0]*80,std::min(160.f,peak)));
  assert(m.maxCll<=peak&&m.maxMastering==peak&&m.minMastering==0);
  assert(m.maxFall>0&&m.maxFall<=peak&&std::abs(m.maxFall-std::min(hdr::map({1,1,1},q)[0]*80,peak))<1e-3f);
 }
 const hdr::Parameters kde{203,1150,0,0};
 assert(std::abs(hdr::metadata(kde,160).maxFall-203)<.01f);
 assert(std::abs(hdr::metadata(kde,160).maxCll-203)<.01f); // no unused peak headroom in Max CLL
 assert(hdr::metadata(kde,400).maxCll==400); // brighter overlay must not be clipped
 assert(hdr::metadata(kde,2000).maxCll==1150); // overlay is capped at Peak
 assert(!(hdr::metadata(kde,160)==hdr::metadata(kde,400))); // live menu brightness update
 assert(!(hdr::metadata(kde,160)==hdr::metadata({203,1000,0,0},160)));
 assert(hdr::metadata({203,1150,0,.3f},160).maxCll>hdr::metadata(kde,160).maxCll);
 // Linux: Wayland for HDR only in a Wayland session, never under gamescope or a chosen SDL driver.
 assert(hdr::wayland_for_hdr(false,true,false));
 assert(!hdr::wayland_for_hdr(false,true,true)&&!hdr::wayland_for_hdr(true,true,false)&&!hdr::wayland_for_hdr(false,false,false));
 auto before=hdr::paperWhite.load();hdr::set(hdr::paperWhite,std::numeric_limits<float>::quiet_NaN(),80,500);
 assert(hdr::paperWhite.load()==before);
 puts("HDR: SDR fallback, black, calibrated grey, monotonicity, peak limit, hue, HDR10 PQ, metadata and invalid settings passed.");
}
