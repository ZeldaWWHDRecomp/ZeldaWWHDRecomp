// Register forwarding for the renderer's draw threads. Ours, MPL-2.0 like the rest
// of the runtime.
//
// A draw thread draws from its own copy of the register file, a few draws behind the command processor. While a sink
// is set (only while a draw thread's work runs; set and cleared on the command processor's thread), the command
// processor hands it every change it makes to the register file, in order, after applying it: apply_regs, each block
// a context load restores, and a save-state load (the whole file). The derived state (the shader state generation,
// the full-state version, the dirty groups) is not forwarded: the command processor computes it as before and hands
// it over with each draw.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gx2::forward {
struct Sink {
    virtual void write(uint32_t first, const uint32_t* values, uint32_t count) = 0;
protected:
    ~Sink() = default;
};
inline Sink* g_sink = nullptr;
inline void set_sink(Sink* s) { g_sink = s; }
// one change applied to the register file
inline void note(uint32_t first, const uint32_t* values, uint32_t count) {
    if (g_sink) [[unlikely]] g_sink->write(first, values, count);
}

// (diagnostics) WWHD_GX2_FORWARD_CHECK=1: a sink keeps its own copy from the forwarded writes alone, compared with the
// register file at every draw; the first differences are logged (proof that the forwarding misses no write)
struct CheckSink final : Sink {
    std::vector<uint32_t> copy;
    uint64_t draws = 0, mismatchDraws = 0;
    void write(uint32_t first, const uint32_t* values, uint32_t count) override {
        if (first + count <= copy.size()) std::memcpy(&copy[first], values, count * 4);
    }
};
inline CheckSink* g_check = nullptr;
}  // namespace gx2::forward
