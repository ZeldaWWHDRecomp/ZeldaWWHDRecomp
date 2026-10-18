// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_compile.h"
#include "story_actors.h"
#include <memory>
namespace guestmods {class NativeSceneRequestIssuer;}
namespace story_native_scene_request {
namespace detail {struct StartupState;}
// Only the final, committed guestmods startup call can construct this scope.
// It is synchronous and cannot be copied, moved, serialized or reopened by a
// Request or boolean. Closing it ends allocation admission for its requests.
class StartupWindow final {
 std::shared_ptr<detail::StartupState>state_;
 StartupWindow();friend class guestmods::NativeSceneRequestIssuer;
public:
 ~StartupWindow();StartupWindow(const StartupWindow&)=delete;StartupWindow&operator=(const StartupWindow&)=delete;
};
bool startup_window_current()noexcept;
class StartupWitness;
std::optional<StartupWitness>current_startup();
// Exact host-only issuing window lineage, including an empty catalog. No
// declarations, allocation, guest ownership or native authority are conveyed.
class StartupWitness final {
 std::weak_ptr<detail::StartupState>state_;
 explicit StartupWitness(const std::shared_ptr<detail::StartupState>&);
 friend std::optional<StartupWitness>current_startup();
public:
 bool current()const noexcept;
};
class DeclaredRequest;
std::shared_ptr<const DeclaredRequest>resolve(const std::string&mod,const std::string&scene,std::string&why);
// Host-only committed definition data, not live actor/PID ownership, a saved
// record, native lease, allocation or publication authority. Call current() on
// the serialized startup/designated game-thread path before consuming it.
class DeclaredRequest final {
 std::string mod_,scene_;story_native_scene_compile::Compiled compiled_;
 story_notebook::ManifestScene definition_;
 std::array<story_actors::ResidentEventDeclaration,2>actors_;
 std::array<uint8_t,32>hash_{};
 std::weak_ptr<detail::StartupState>startup_;
 DeclaredRequest(std::string,std::string,story_native_scene_compile::Compiled,
  story_notebook::ManifestScene,std::array<story_actors::ResidentEventDeclaration,2>,
  std::shared_ptr<detail::StartupState>);
 friend std::shared_ptr<const DeclaredRequest>resolve(const std::string&,const std::string&,std::string&);
public:
 const std::string&mod()const{return mod_;}const std::string&scene()const{return scene_;}
 const story_native_scene_compile::Compiled&compiled()const{return compiled_;}
 const story_notebook::ManifestScene&definition()const{return definition_;}
 const auto&actors()const{return actors_;}const auto&definition_hash()const{return hash_;}
 bool current()const; // full typed declarations AND digest; no startup authority
 bool startup_current()const noexcept; // exact authentic issuing startup scope
};
}
