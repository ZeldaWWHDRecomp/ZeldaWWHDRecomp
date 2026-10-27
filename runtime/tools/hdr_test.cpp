#include "gfx/hdr.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
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
 // Linux: Wayland for HDR only in a Wayland session, never under gamescope or a chosen SDL driver.
 assert(hdr::wayland_for_hdr(false,true,false));
 assert(!hdr::wayland_for_hdr(false,true,true)&&!hdr::wayland_for_hdr(true,true,false)&&!hdr::wayland_for_hdr(false,false,false));
 auto before=hdr::paperWhite.load();hdr::set(hdr::paperWhite,std::numeric_limits<float>::quiet_NaN(),80,500);
 assert(hdr::paperWhite.load()==before);
 puts("HDR: SDR fallback, black, calibrated grey, monotonicity, peak limit, hue, HDR10 PQ and invalid settings passed.");
}
