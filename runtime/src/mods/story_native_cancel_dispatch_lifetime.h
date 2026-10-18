// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_stage_owner.h"
#include "native_quiescence.h"
#include <memory>
namespace story_native_manager_cancel_dispatch::detail {
// Owns existing opaque prerequisites only; never constructs either capability.
// Transfer both owners so failure/unwind cannot leave an extra Stage owner
// alive when the final physical-quiescence owner thaws guest threads.
struct PinnedStage final {
 std::shared_ptr<story_native_stage_owner::Tracker::Scan>stage;
 std::shared_ptr<const threads::NativeQuiescence>quiescence;
 PinnedStage(std::shared_ptr<story_native_stage_owner::Tracker::Scan>&&s,
             std::shared_ptr<const threads::NativeQuiescence>&&q)
  :stage(std::move(s)),quiescence(std::move(q)){}
 ~PinnedStage(){stage.reset();quiescence.reset();}
};
}
