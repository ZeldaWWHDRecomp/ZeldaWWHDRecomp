#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif

namespace r5_slow_render {
// Diagnostic surrogate only: background QoS is not hard CPU affinity.
inline bool enabled_value(const char* value) noexcept {
    return value && !std::strcmp(value, "1");
}
inline bool requested() noexcept {
#ifdef __APPLE__
    static const bool on = enabled_value(std::getenv("WWHD_R5_SLOW_RENDER"));
    return on;
#else
    return false;
#endif
}
// Called once at entry of ONLY GX2 render and Vulkan record. True means the
// override was requested, including failure: never hide a failed request by
// subsequently boosting the thread. A benchmark must reject nonzero errors or
// an actual class other than background. Guest-thread QoS remains unchanged.
inline bool apply(const char* thread) noexcept {
#ifdef __APPLE__
    if (!requested()) return false;
    const int set_error = pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
    qos_class_t actual = QOS_CLASS_UNSPECIFIED;
    int relative = 0;
    const int query_error = pthread_get_qos_class_np(pthread_self(), &actual, &relative);
    std::fprintf(stderr,
        "[r5 slow render] thread=%s requested=background set_error=%d query_error=%d actual=%u relative_priority=%d\n",
        thread, set_error, query_error, unsigned(actual), relative);
    return true;
#else
    (void)thread;
    return false;
#endif
}
} // namespace r5_slow_render
