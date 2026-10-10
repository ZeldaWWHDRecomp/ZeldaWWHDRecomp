#include "gfx/vulkan/presentation_queue.h"
#include <array>
#include <cassert>
#include <cstdio>

using namespace gfxvk;
int main() {
  // Enumerate available counts/capabilities, including transfer-only and
  // graphics families without presentation, and a nonzero primary family.
  unsigned cases=0;
  for(uint32_t primary=0;primary<2;++primary)
  for(uint32_t a=0;a<3;++a)for(uint32_t b=0;b<3;++b)
  for(unsigned flags=0;flags<16;++flags) {
    const std::array<PresentationQueueFamily,2> families{{
        {a,bool(flags&1),bool(flags&2)}, {b,bool(flags&4),bool(flags&8)}}};
    const auto chosen=secondary_presentation_queue(families,primary);
    const auto& main=families[primary];
    const auto& other=families[1-primary];
    const bool primaryValid=main.count && main.graphics && main.present;
    const bool independent=primaryValid &&
        (main.count>=2 || (other.count && other.graphics && other.present));
    assert(bool(chosen)==independent);
    if(chosen) {
      assert(chosen->family<families.size());
      assert(chosen->index<families[chosen->family].count);
      assert(families[chosen->family].graphics && families[chosen->family].present);
      assert(chosen->family!=primary || chosen->index!=0);
      if(main.count>=2)assert(chosen->family==primary && chosen->index==1);
      else assert(chosen->family==1-primary && chosen->index==0);
    }
    ++cases;
  }
  const std::array<PresentationQueueFamily,3> mixed{{
      {1,true,true},{4,false,true},{1,true,true}}};
  const auto third=secondary_presentation_queue(mixed,0);
  assert(third && third->family==2 && third->index==0);
  assert(!secondary_presentation_queue({},0));
  const std::array<PresentationQueueFamily,1> single{{{1,true,true}}};
  assert(!secondary_presentation_queue(single,1));
  std::printf("Presentation queue selection: %u configurations passed\n",cases);
}
