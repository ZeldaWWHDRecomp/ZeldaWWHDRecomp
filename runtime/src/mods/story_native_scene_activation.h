// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_request.h"
#include <stdexcept>
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer;}
namespace story_native_scene_playback {
// Fresh host activation identity, minted only by the actual runtime frontend
// issuer. Immutable declaration data and identity, never a native-call lease.
class Activation final {
 std::shared_ptr<const story_native_scene_request::DeclaredRequest>declaration_;
 std::array<uint8_t,32>hash_;
 explicit Activation(std::shared_ptr<const story_native_scene_request::DeclaredRequest>declaration):declaration_(std::move(declaration)){
  if(!declaration_)throw std::invalid_argument("activation requires a committed declaration");hash_=declaration_->definition_hash();
 }
 friend class story_native_wait_runtime::NativeScenePlaybackIssuer;
public:
 Activation(const Activation&)=delete;Activation&operator=(const Activation&)=delete;
 const auto&declaration()const noexcept{return declaration_;}
 const auto&definition_hash()const noexcept{return hash_;}
};
}
