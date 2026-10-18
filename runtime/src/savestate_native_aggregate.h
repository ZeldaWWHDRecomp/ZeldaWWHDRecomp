// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "savestate_native_gate.h"
#include <string>
#include <functional>
namespace ss::native_gate {
class FinishedLoad;
// All child Controllers and their Sessions must outlive this process-lifetime
// coordinator and every Hold. The child list is immutable after construction.
// No registration or native/publication authority is provided by this helper.
class Aggregate final : public Controller {
 struct Impl;std::shared_ptr<Impl>impl_;
 explicit Aggregate(std::shared_ptr<Impl>);
 enum class Mode {Save,Load,Progress};
 std::unique_ptr<Hold>begin(Mode,const SavedView*);
public:
 struct LoadGroup {
  std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>stage;
  void(*close)(story_native_stage_restore_group::StageRestoreGroup&)noexcept=nullptr;
  std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner;
  void(*close_owner)(story_native_restore_resume::CurrentLoadOwner&)noexcept=nullptr;
  bool(*finish)(const FinishedLoad&)noexcept=nullptr;
 };
 using LoadGroupFactory=std::function<LoadGroup(const SavedView&)>;
 static constexpr size_t kMaxChildren=129; // 128 declared slots + private pilot
 static std::unique_ptr<Aggregate>create(std::span<Controller* const>,std::string&why,LoadGroupFactory={});
 ~Aggregate()override;
 Aggregate(const Aggregate&)=delete;Aggregate&operator=(const Aggregate&)=delete;
 size_t child_count()const noexcept;
 std::unique_ptr<Hold>begin_save()override;
 bool finish_save()override;
 std::unique_ptr<Hold>begin_load(const SavedView&)override;
 bool finish_load(const SavedView&)override;
 std::unique_ptr<Hold>begin_progress_load()override;
};
}
