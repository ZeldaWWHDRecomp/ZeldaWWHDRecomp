#pragma once
#include <cstdint>
namespace gfxvk {
// Shared production barrier: a GPU fence from the previous use cannot authorize
// access until the exact CPU-recording submission completes. Wait may throw;
// preserve the serial in that case so failure cannot masquerade as an ack.
template<class Wait>
inline void acknowledge_submission_cpu(uint64_t& serial, Wait&& wait) {
    if (!serial) return;
    const uint64_t expected = serial;
    wait(expected);
    serial = 0;
}
}
