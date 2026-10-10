#pragma once
#include <atomic>

namespace dual_display {
// Physical surface 0 is the app's main window; 1 is the Presentation window.
// Source 0 is TV and source 1 is GamePad. Keep the preference when unplugged.
constexpr int source_index(int physical, bool connected, bool swapped) {
    return connected && swapped ? 1 - physical : physical;
}
inline std::atomic<bool> requested_swap{false};
inline std::atomic<bool> active_swap{false}; // latched once per renderer frame
}
