// Descriptor tokens (MT recording stage 1, WWHD_VK_RECORD_MT bit 16 / WWHD_VK_RECORD_DESCRIPTORS=1; record_mt.h).
// Decomp build, fast mode (cmake WWHD_NATIVE_DRAW).
//
// stage_descriptor_set (draw.cpp) keeps its decision: the identity/hash cache and the last set per stage. Only a new
// set's allocation and write move to the emitter: while the worker's POD stream records, the producer returns a token
// instead of a VkDescriptorSet and puts a DefineSet command (layout + copied writes) into the stream in front of the
// first command that can bind it. The emitter allocates the set from the slot's own descriptor pools (emitter-owned,
// a chain that grows when full; reset when the slot is activated again) and writes it.
// Token = 0xA5C3 << 48 | slot << 40 | slot generation (8 bits) << 32 | index in the slot's use.
// Translation: while tokens are on, gfxvk::vkCmdBindDescriptorSets is a trampoline that replaces tagged values by the
// emitter's sets; every bind from every path passes it (audit list: vk-levers DESIGN.md section 5, stage 1 audit).
// On the render thread (encoder fallbacks, eye streams emitted after a drain) translation needs a drained emitter,
// which WWHD_VK_RECORD_CHECK=1 asserts. A rejected DefineSet drains and defines on the render thread.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include "loader.h"
#include <cstddef>
#include <cstdint>

namespace gfxvk::record_tokens {
// render thread: tokens apply to the next new set (bit 16, worker stream recording, a token-capable slot)
bool active();
// render thread, stage_descriptor_set: the token of a new set with these writes (DefineSet queued)
VkDescriptorSet define(VkDescriptorSetLayout layout, const VkWriteDescriptorSet* writes, uint32_t count);
// render thread, slot INDEX activated: starts its next use; true when the emitter must reset the slot's pools and
// table (record_mt's SlotReset carries GENERATION)
bool slot_activated(uint32_t index, uint8_t& generation);
// emitter (or the render thread with the emitter drained): the slot's pools reset, its table cleared
void reset_slot(uint32_t index, uint8_t generation, VkDevice device);
// opcode 31 DefineSet replay (payload after the sub-opcode)
void replay_define(const std::byte* bytes, size_t size, bool execute);
// worker start: install the bind trampoline (bit 16 on)
void install(VkDevice device);
void report(double frames, char* out, size_t outSize);
}  // namespace gfxvk::record_tokens
#endif
