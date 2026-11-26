#pragma once
#include <cstdint>
namespace gfxvk {
// Producer-only append rejection metadata; never a replay or Vulkan error.
enum class PodFailureReason : uint8_t {
    None, UnsupportedPNext, CommandLimit, ByteBudgetOrOverflow,
    InvalidInput, AllocationFailure, UnclassifiedEncoderReject, Count
};
// POD opcodes 1..kPodOpcodes-1 (pod_commands.cpp); 30: vkCmdDrawIndexedIndirect (emitted only by the native
// renderer's batches with WWHD_VK_RECORD_INDIRECT=1); 31: the decomp build's MT recording extension commands
// (record_mt.h; never encoded elsewhere)
constexpr unsigned kPodOpcodes = 32;
}
