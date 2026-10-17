#pragma once
#include "pod_failure_reason.h"
#include "handoff_trace.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>

namespace gfxvk::fallback_stats {
enum class Boundary : uint8_t {
    ExplicitState, EncoderFallback, ProducerPresentation, RawPrimarySnapshot,
    Shutdown, Endpoint, Count
};
// Diagnostic-only, producer-owned fixed storage. No per-operation output or
// growth. A selected frame must have an explicit completed row, including zero.
constexpr size_t MaxFrames = 8192, MaxIntervals = 16384;
constexpr size_t Reasons = size_t(PodFailureReason::Count);
struct Frame {
    uint64_t id = 0;
    bool completed = false;
    std::array<std::array<uint32_t, Reasons>, 30> rejected{};
    std::array<uint32_t, size_t(Boundary::Count)> boundaries{};
    uint32_t attempts = 0, completedDrains = 0, emptyPrefixes = 0;
    uint32_t nativePrefixPublications = 0;
    std::array<uint32_t, Reasons> fatalSubmitRetryReasons{};
};
struct Interval {
    uint64_t id = 0, frame = 0, begin = 0, end = 0;
    uint32_t opcode = 0;
    PodFailureReason reason = PodFailureReason::None;
    bool completed = false;
};
struct State {
    std::string path;
    std::array<Frame, MaxFrames> frames{};
    std::array<Interval, MaxIntervals> intervals{};
    size_t frameCount = 0, intervalCount = 0;
    uint64_t currentFrame = 0;
    bool invalid = false, inFallback = false, flushed = false;
    explicit State(const char* value) : path(value) {}
};
inline std::unique_ptr<State> storage;
inline bool initialized = false;
// Call exactly on the producer before publishing the first worker job. No
// worker reads this state; absence of the flag leaves all storage unallocated.
inline void initialize() {
    if (initialized) return;
    const char* path = std::getenv("WWHD_VK_FALLBACK_STATS");
    if (path && *path) storage = std::make_unique<State>(path);
    initialized = true; // Allocation failure remains fatal/retryable, never silently disables diagnostics.
}
inline Frame* frame(uint64_t id) {
    auto* s = storage.get();
    if (!s || s->flushed) return nullptr;
    s->currentFrame = id;
    if (s->frameCount && s->frames[s->frameCount - 1].id == id)
        return &s->frames[s->frameCount - 1];
    if ((s->frameCount && (id != s->frames[s->frameCount - 1].id + 1 ||
            !s->frames[s->frameCount - 1].completed)) || s->frameCount == MaxFrames) {
        s->invalid = true;
        return nullptr;
    }
    auto& result = s->frames[s->frameCount++];
    result.id = id;
    return &result;
}
inline void begin_frame(uint64_t id) { (void)frame(id); }
inline void complete_frame(uint64_t id) {
    if (auto* f = frame(id)) {
        if (f->completed) storage->invalid = true;
        f->completed = true;
    }
}
inline void increment(uint32_t& value) {
    if (value == std::numeric_limits<uint32_t>::max()) storage->invalid = true;
    else ++value;
}
// A queued native prefix is not a CPU drain or completion acknowledgement.
inline void native_prefix_publication() {
    auto* s = storage.get();
    if (!s || s->flushed) return;
    if (auto* f = frame(s->currentFrame)) increment(f->nativePrefixPublications);
}
// Measures ONLY encoder-rejection drains. Generic/state boundaries are counts,
// not wall time. The outer drain includes nested capacity/serial waits; these
// must never be added again to its elapsed wall time.
class Drain {
    State* state_ = nullptr;
    Frame* frame_ = nullptr;
    Interval* interval_ = nullptr;
    bool completed_ = false;
public:
    Drain(Boundary boundary, uint32_t opcode, PodFailureReason reason, bool empty) {
        state_ = storage.get();
        if (!state_ || state_->flushed) return;
        frame_ = frame(state_->currentFrame);
        if (!frame_) return;
        const size_t b = size_t(boundary), r = size_t(reason);
        if (b >= size_t(Boundary::Count)) { state_->invalid = true; return; }
        increment(frame_->boundaries[b]);
        if (boundary != Boundary::EncoderFallback) return;
        if (!opcode || opcode >= 30 || !r || r >= Reasons || state_->inFallback ||
                state_->intervalCount == MaxIntervals) { state_->invalid = true; return; }
        increment(frame_->attempts);
        increment(frame_->rejected[opcode][r]);
        if (empty) increment(frame_->emptyPrefixes);
        state_->inFallback = true;
        interval_ = &state_->intervals[state_->intervalCount++];
        interval_->id = state_->intervalCount;
        interval_->frame = frame_->id;
        interval_->opcode = opcode;
        interval_->reason = reason;
        interval_->begin = handoff_trace::native_now_ns();
        if (!interval_->begin) state_->invalid = true;
    }
    Drain(const Drain&) = delete;
    Drain& operator=(const Drain&) = delete;
    void complete() {
        completed_ = true;
        if (interval_) {
            increment(frame_->completedDrains);
            interval_->completed = true;
        }
    }
    ~Drain() {
        if (state_ && !state_->flushed && !completed_) state_->invalid = true;
        if (!interval_) return;
        interval_->end = handoff_trace::native_now_ns();
        if (!interval_->end || interval_->end < interval_->begin || !interval_->completed)
            state_->invalid = true;
        state_->inFallback = false;
    }
};
inline void fatal_submit_retry(PodFailureReason reason) {
    auto* s = storage.get();
    if (!s || s->flushed) return;
    s->invalid = true; // Never turn a fatal retry into a second fallback drain.
    if (auto* f = frame(s->currentFrame)) {
        const auto r = size_t(reason);
        if (!r || r >= Reasons) return;
        increment(f->fatalSubmitRetryReasons[r]);
    }
}
inline bool flush() {
    auto* s = storage.get();
    if (!s) return true;
    if (s->flushed) return !s->invalid;
    s->flushed = true;
    if (s->inFallback) s->invalid = true;
    FILE* f = std::fopen(s->path.c_str(), "wb");
    if (!f) { s->invalid = true; return false; }
    bool ok = std::fprintf(f,
        "{\"schema\":2,\"clock\":\"CACurrentMediaTime on Apple; steady_clock otherwise\","
        "\"max_frames\":%zu,\"max_intervals\":%zu,\"invalid\":%s,"
        "\"reason_names\":[\"None\",\"UnsupportedPNext\",\"CommandLimit\",\"ByteBudgetOrOverflow\",\"InvalidInput\",\"AllocationFailure\",\"UnclassifiedEncoderReject\"],"
        "\"boundary_names\":[\"ExplicitState\",\"EncoderFallback\",\"ProducerPresentation\",\"RawPrimarySnapshot\",\"Shutdown\",\"Endpoint\"],\"frames\":[",
        MaxFrames, MaxIntervals, s->invalid ? "true" : "false") >= 0;
    for (size_t i = 0; i < s->frameCount; ++i) {
        const auto& row = s->frames[i];
        ok &= std::fprintf(f, "%s{\"frame\":%llu,\"completed\":%s,\"attempts\":%u,\"completed_drains\":%u,\"empty_prefixes\":%u,\"native_prefix_publications\":%u,\"boundaries\":[",
            i ? "," : "", (unsigned long long)row.id, row.completed ? "true" : "false",
            row.attempts, row.completedDrains, row.emptyPrefixes, row.nativePrefixPublications) >= 0;
        for (size_t j = 0; j < row.boundaries.size(); ++j)
            ok &= std::fprintf(f, "%s%u", j ? "," : "", row.boundaries[j]) >= 0;
        ok &= std::fprintf(f, "],\"fatal_submit_retry_reasons\":[") >= 0;
        for (size_t j = 0; j < Reasons; ++j)
            ok &= std::fprintf(f, "%s%u", j ? "," : "", row.fatalSubmitRetryReasons[j]) >= 0;
        ok &= std::fprintf(f, "],\"rejections\":[") >= 0;
        bool comma = false;
        for (size_t opcode = 1; opcode < 30; ++opcode)
            for (size_t reason = 1; reason < Reasons; ++reason)
                if (row.rejected[opcode][reason]) {
                    ok &= std::fprintf(f, "%s[%zu,%zu,%u]", comma ? "," : "", opcode, reason, row.rejected[opcode][reason]) >= 0;
                    comma = true;
                }
        ok &= std::fprintf(f, "]}") >= 0;
    }
    ok &= std::fprintf(f, "],\"interval_fields\":[\"attempt\",\"frame\",\"opcode\",\"reason\",\"begin_ns\",\"end_ns\",\"completed\"],\"intervals\":[") >= 0;
    for (size_t i = 0; i < s->intervalCount; ++i) {
        const auto& row = s->intervals[i];
        ok &= std::fprintf(f, "%s[%llu,%llu,%u,%u,%llu,%llu,%s]", i ? "," : "",
            (unsigned long long)row.id, (unsigned long long)row.frame, row.opcode, unsigned(row.reason),
            (unsigned long long)row.begin, (unsigned long long)row.end, row.completed ? "true" : "false") >= 0;
    }
    ok &= std::fprintf(f, "]}\n") >= 0;
    ok &= std::fflush(f) == 0;
    ok &= std::fclose(f) == 0;
    s->invalid |= !ok;
    return !s->invalid;
}
}
