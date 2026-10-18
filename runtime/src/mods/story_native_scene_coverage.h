// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_world_manifest.h"
namespace story_native_scene_coverage {
enum class Selection {WholeFallback,NativeAlternative};
struct Coverage {
 Selection selection=Selection::WholeFallback;size_t end_step=0;
 std::vector<story_notebook::StoryAction>rewards;std::string reason;
};
// Pure family eligibility, NOT a native lease or completion receipt. The
// explicit native recipe controls the parallel alternative, not a translation
// of fallback targets/timings. Rewards may run once only AFTER genuine native
// release; this data alone never authorizes executing them.
Coverage inspect(const story_notebook::ManifestScene&);
}
