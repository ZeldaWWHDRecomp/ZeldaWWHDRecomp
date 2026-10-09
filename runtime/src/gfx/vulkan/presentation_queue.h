#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace gfxvk {
struct PresentationQueueFamily {
  uint32_t count=0;
  bool graphics=false,present=false;
};
struct PresentationQueue { uint32_t family,index; };
// Prefer another queue in the same family to avoid cross-family image sharing.
// Otherwise use a graphics family that can present to the Android surface.
inline std::optional<PresentationQueue> secondary_presentation_queue(
    std::span<const PresentationQueueFamily> families,uint32_t primary) {
  if(primary>=families.size())return {};
  const auto& main=families[primary];
  if(!main.graphics || !main.present || !main.count)return {};
  if(main.count>=2)return PresentationQueue{primary,1};
  for(uint32_t family=0;family<families.size();++family) {
    const auto& candidate=families[family];
    if(family!=primary && candidate.count && candidate.graphics && candidate.present)
      return PresentationQueue{family,0};
  }
  return {};
}
}
