// SPDX-License-Identifier: MPL-2.0
// The island buildings' memory budget per room (story_game_pieces_catalogue.h checks manifests against it,
// mod_actor_budget.h the running game): every package's game pieces in one room together, each set's
// archive once and each placement. One constant, so a measured value can replace it in one place.
#pragma once
#include <cstdint>
namespace story_game_pieces {
inline constexpr uint64_t kRoomBudgetBytes = 24u * 1024 * 1024;
}
