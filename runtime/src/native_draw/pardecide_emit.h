// pardecide (WWHD_NATIVE_PAR_DECIDE, decomp build fast mode): one POD extension command into the record worker's
// stream (opcode 31, record_mt.h). Included by draw.cpp (which compiles with WWHD_VK_SMOOTH) at file scope.
#pragma once
#if defined(WWHD_DECOMP_FAST) && defined(WWHD_VK_SMOOTH)
#include "native_draw/pardecide_ops.h"
#include "gfx/vulkan/deferred_commands.h"
#include "gfx/vulkan/pod_commands.h"
#include "gfx/vulkan/record_worker.h"

namespace native_draw::pardecide {
// PAYLOAD (sub-opcode + payload) appended to the worker stream when the active recorder is that stream (not a held
// eye, not immediate recording), then the draw-boundary publish (as vkCmdDrawIndexed does); false: nothing appended
inline bool emit_ext(const std::byte* payload, size_t size) {
  gfxvk::DeferredCommands* rec = gfxvk::dispatch_recorder();
  if (!rec || rec != gfxvk::active_deferred_commands || !rec->pod_mode()) return false;
  gfxvk::PodCommands* stream = gfxvk::record_worker_commands();
  if (!stream) return false;
#if defined(WWHD_PARDECIDE_PROF_INCLUDED)
  SubTimer pardecideSub(kSubRecord);  // (measurement branch only)
#endif
  if (!stream->extension({payload, size})) return false;
  gfxvk::publish_record_commands_at_draw_boundary();
  return true;
}
}  // namespace native_draw::pardecide
#endif
