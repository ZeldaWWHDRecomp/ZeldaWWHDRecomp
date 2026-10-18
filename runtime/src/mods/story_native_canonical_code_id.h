// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <type_traits>
namespace story_native_code_identity {
class CanonicalCodeId;
consteval CanonicalCodeId CANONICAL_CODE_ID(uint32_t);
// Host dispatch/hook metadata uses canonical USA identities, even on EUR.
// This is deliberately not a guest function pointer: GC() performs the one
// regional translation when the canonical dispatch API resolves the identity.
class CanonicalCodeId final {
 uint32_t value_;
 explicit consteval CanonicalCodeId(uint32_t value):value_(value){
  if(value<32u*1024*1024||value>=48u*1024*1024||(value&3u))throw "invalid canonical code identity";
 }
 friend consteval CanonicalCodeId CANONICAL_CODE_ID(uint32_t);
public:
 constexpr uint32_t value()const noexcept{return value_;}
};
consteval CanonicalCodeId CANONICAL_CODE_ID(uint32_t value){return CanonicalCodeId(value);}
static_assert(!std::is_convertible_v<CanonicalCodeId,uint32_t>);
}
