// SPDX-License-Identifier: MPL-2.0
// What the render thread does with a pipeline the warm-up worker (pipeline_worker.cpp) compiled ahead.
//
// Cache only (the default): the worker's compile warms the driver pipeline cache that the render thread's own
// compiles read (the demand compiler's, pipeline_demand.cpp); the render thread then builds the pipeline itself, a
// driver-cache hit (Xiaomi 14, Adreno 750: ~0.25 ms each, 513 at Windfall in ~130 ms over a whole session), and the
// worker's copy is destroyed.
//
// Hand-over (WWHD_VK_WARM_HANDOVER=1, measurements only): the render thread takes the worker's VkPipeline as is. On
// Qualcomm's driver every frame drawn with such pipelines got slower (issue #334: Windfall at 40 fps 22 fps instead of
// 40 on every launch with local recipes; the render thread's whole frame ~1.4x the time, the driver's command
// memory unmapped and mapped again each frame, submits twice as costly, the GPU idle meanwhile). It was not the
// worker's thread priority, its descriptor layouts or the pipeline cache object it compiled into: only the pipeline
// objects it created.
#pragma once
#include <cstring>

namespace gfxvk::warm_handover {

enum class Use { CacheOnly, Handover };

// env: the value of WWHD_VK_WARM_HANDOVER (nullptr: not set)
inline Use use(const char* env) { return env && !std::strcmp(env, "1") ? Use::Handover : Use::CacheOnly; }

}  // namespace gfxvk::warm_handover
