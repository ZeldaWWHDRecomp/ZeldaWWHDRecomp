#include "platform/r5_slow_render.h"
#include <cassert>
#include <thread>

int main(int argc, char** argv) {
    using namespace r5_slow_render;
    assert(!enabled_value(nullptr));
    assert(!enabled_value(""));
    assert(!enabled_value("0"));
    assert(!enabled_value("true"));
    assert(!enabled_value("10"));
    assert(enabled_value("1"));
#ifdef __APPLE__
    const bool on = argc == 2 && !std::strcmp(argv[1], "on");
    assert(setenv("WWHD_R5_SLOW_RENDER", on ? "1" : "0", 1) == 0);
    assert(pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0);
    qos_class_t guest_before = QOS_CLASS_UNSPECIFIED;
    int relative = 0;
    assert(pthread_get_qos_class_np(pthread_self(), &guest_before, &relative) == 0);
    assert(guest_before == QOS_CLASS_USER_INTERACTIVE);
    assert(requested() == on);
    // The option is cached for the process, including the disabled path.
    assert(setenv("WWHD_R5_SLOW_RENDER", on ? "0" : "1", 1) == 0);
    assert(requested() == on);
    std::thread worker([&] {
        qos_class_t before = QOS_CLASS_UNSPECIFIED, after = QOS_CLASS_UNSPECIFIED;
        int priority = 0;
        assert(pthread_get_qos_class_np(pthread_self(), &before, &priority) == 0);
        assert(apply("synthetic render") == on);
        assert(pthread_get_qos_class_np(pthread_self(), &after, &priority) == 0);
        assert(after == (on ? QOS_CLASS_BACKGROUND : before));
    });
    worker.join();
    qos_class_t guest_after = QOS_CLASS_UNSPECIFIED;
    assert(pthread_get_qos_class_np(pthread_self(), &guest_after, &relative) == 0);
    assert(guest_after == guest_before);
#else
    (void)argc; (void)argv;
    assert(!requested());
    assert(!apply("synthetic render"));
#endif
}
