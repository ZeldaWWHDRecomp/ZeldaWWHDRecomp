// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
namespace story_native_refresh_stack {
// Sealed 145-function closure: sum of all constant native frame pushes 0xEB8,
// acyclic direct/computed call graph; tail epilogues restore caller SP. Add
// 0x120 below-frame register-helper space, 0x40 guest_call outgoing frame and
// linkage allowance. A conservative 0x2000-byte budget exceeds that full sum.
// Recheck at each native entry, independently of the pending-camera receipt.
inline constexpr uint32_t kAuditedFrameSum=0xEB8;
inline constexpr uint32_t kRegisterHelperBytes=0x120,kOutgoingBytes=0x40,kLinkageBytes=0x40;
inline constexpr uint32_t kDownwardBytes=0x2000;
static_assert(kDownwardBytes>=kAuditedFrameSum+kRegisterHelperBytes+kOutgoingBytes+kLinkageBytes);
constexpr bool fits(uint32_t bottom,uint32_t top,uint32_t sp,uint32_t required=kDownwardBytes){
 return bottom<top&&!(sp&7)&&required&&uint64_t(bottom)+required<=sp&&sp<=top;
}
}
