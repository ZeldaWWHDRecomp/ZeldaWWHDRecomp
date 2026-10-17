#pragma once
#include <cstdint>
namespace gfxvk {
// Producer-only append rejection metadata; never a replay or Vulkan error.
enum class PodFailureReason : uint8_t {
    None, UnsupportedPNext, CommandLimit, ByteBudgetOrOverflow,
    InvalidInput, AllocationFailure, UnclassifiedEncoderReject, Count
};
}
