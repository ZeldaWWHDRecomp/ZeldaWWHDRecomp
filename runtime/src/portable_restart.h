// Native player-entry parameters for a portable progress/position restore.
#pragma once
#include <cstdint>
#include "portable_state.h"

namespace pstate {
// CRAWL_START, CRAWL_MOVE, CRAWL_AUTO_MOVE and CRAWL_END. Resume all four in
// controllable crawl, rather than replaying a transition with missing transient state.
constexpr bool crawl_procedure(int proc) { return proc >= 0x0F && proc <= 0x12; }
inline bool restore_crawl(const State& s) { return !s.on_ship && crawl_procedure(s.link_proc); }
inline uint32_t restart_mode(const State& s) {
    return s.has_ship && s.on_ship ? 2u : restore_crawl(s) ? 5u : 0u;
}
inline uint32_t restart_parameters(const State& s) {
    return uint32_t(s.room & 0x3F) | (restart_mode(s) << 12) | 0xFF000000u | (s.has_ship ? 0x100u : 0u);
}
// Native start mode 5 + last-scene mode 2 enters procCrawlMove_init before the
// first player execution, with the normal crawl collision and animation setup.
inline uint32_t restart_scene_mode(const State& s) { return restore_crawl(s) ? 2u : 0u; }
} // namespace pstate
