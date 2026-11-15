// How Graphics > Advanced "Record graphics on a second thread" is resolved (backend.cpp, record_worker.cpp): the
// environment variable WWHD_VK_RECORD_WORKER wins (exactly "1" on, any other value off), then the saved setting
// vkRecordWorker ("1" on, anything else off), then the device's default (smooth_defaults.h, record_worker.h).
// ENV / SAVED: null when not set. Header-only so that smooth_defaults_test.cpp checks the same rule.
#pragma once
#include <cstring>

namespace gfxvk {
inline bool record_worker_choice(const char* env, const char* saved, bool deviceDefault) {
    if (env) return !std::strcmp(env, "1");
    if (saved) return !std::strcmp(saved, "1");
    return deviceDefault;
}
}  // namespace gfxvk
