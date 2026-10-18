// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_request.h"
#include <vector>
namespace story_native_scene_catalog {
inline constexpr size_t kMaxDeclarations=128;
class StartupCatalog;
std::optional<StartupCatalog>collect(std::string&why);
// Immutable host definition data only; no scene selection, allocation, native
// lease or run receipt. Every member was resolved in the actual startup window.
class StartupCatalog final {
 story_native_scene_request::StartupWitness startup_;
 std::vector<std::pair<std::string,std::string>>ids_;
 std::vector<std::shared_ptr<const story_native_scene_request::DeclaredRequest>>requests_;
 StartupCatalog(story_native_scene_request::StartupWitness,decltype(ids_),decltype(requests_));
 friend std::optional<StartupCatalog>collect(std::string&);
public:
 const auto&requests()const{return requests_;}
 bool current()const;
 bool startup_current()const;
};
}
