// memory_trim.h: requests from any thread merge into the strongest until the render thread takes them.
#include "platform/memory_trim.h"

#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "memory_trim_test: %s\n", what); std::exit(1); }
}

int main() {
    int source = 0;
    check(memory_trim::take(source) == memory_trim::kNone, "nothing pending at start");
    memory_trim::request(memory_trim::kLight, 20);
    check(memory_trim::take(source) == memory_trim::kLight && source == 20, "a light request");
    check(memory_trim::take(source) == memory_trim::kNone, "taken once");
    memory_trim::request(memory_trim::kLight, 5);
    memory_trim::request(memory_trim::kFull, 15);
    memory_trim::request(memory_trim::kLight, 20);
    check(memory_trim::take(source) == memory_trim::kFull && source == 15, "the strongest wins, with its source");
    memory_trim::request(memory_trim::kFull, -1);
    memory_trim::request(memory_trim::kFull, 80);
    check(memory_trim::take(source) == memory_trim::kFull && source == 80, "equal levels: the latest source");
    // save states and the free memory above the system's line (-1: unknown, never refuses)
    memory_trim::set_headroom(-5, -1);
    check(memory_trim::headroom_mb() == -1 && memory_trim::threshold_mb() == -1, "unknown headroom");
    check(!memory_trim::save_refused(memory_trim::headroom_mb(), memory_trim::kSaveMinHeadroomMb), "unknown: saved");
    memory_trim::set_headroom(95, 512);
    check(memory_trim::headroom_mb() == 95 && memory_trim::threshold_mb() == 512, "headroom stored");
    check(memory_trim::save_refused(95, memory_trim::kSaveMinHeadroomMb), "95 MB: refused");
    check(memory_trim::save_refused(0, memory_trim::kSaveMinHeadroomMb), "at the line: refused");
    check(!memory_trim::save_refused(96, memory_trim::kSaveMinHeadroomMb), "96 MB: saved");
    check(!memory_trim::save_refused(4000, memory_trim::kSaveMinHeadroomMb), "plenty: saved");
    check(memory_trim::save_refused(4000, 100000), "WWHD_STATE_MIN_HEADROOM_MB=100000: refused");
    std::puts("memory_trim_test: ok");
    return 0;
}
