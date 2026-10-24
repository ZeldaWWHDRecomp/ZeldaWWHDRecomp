// Temporary probe: find where WWHD's J3DModel keeps its joint matrices (WWHD_PROBE_MODEL=n models).
#include <cmath>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <set>

#include "runtime.h"
#include "model_probe.h"

extern "C" void f_027F4D5C_orig(Cpu* c);

namespace {
std::mutex probe_mutex;
std::atomic<int>& remaining_probes() {
    // Capture the same startup option once for both the wrapper and its private
    // observer. Removing the environment variable cannot hide a pending probe.
    static std::atomic<int> remaining{[] {
        const char* value = std::getenv("WWHD_PROBE_MODEL");
        return value ? std::atoi(value) : 0;
    }()};
    return remaining;
}
}
namespace model_probe {
bool native_refresh_quiescent() noexcept {
    try {
        // A last probe decrements before scanning: zero alone would not prove
        // that its broad reads have finished. Fail closed while any scan runs.
        std::unique_lock lock(probe_mutex, std::try_to_lock);
        return lock.owns_lock() && remaining_probes().load(std::memory_order_acquire) <= 0;
    } catch (...) { return false; }
}
}

static bool plausible_mtx(uint32_t a) {
    // 3x4 row-major: rows of the 3x3 part have similar, non-zero length; translation finite
    double len[3];
    for (int r = 0; r < 3; r++) {
        double s = 0;
        for (int k = 0; k < 3; k++) {
            double v = ldf32(a + 16 * r + 4 * k);
            if (!std::isfinite(v)) return false;
            s += v * v;
        }
        len[r] = std::sqrt(s);
    }
    for (int r = 0; r < 3; r++) {
        double t = ldf32(a + 16 * r + 12);
        if (!std::isfinite(t) || std::fabs(t) > 1e6) return false;
    }
    if (len[0] < 0.01 || len[0] > 100) return false;
    return std::fabs(len[1] - len[0]) < 0.05 * len[0] && std::fabs(len[2] - len[0]) < 0.05 * len[0];
}

static bool guest_ptr(uint32_t p) { return p >= 0x10000000 && p < 0x50000000 && (p & 3) == 0; }

namespace interp { void model_calc_before(uint32_t model); }  // interp.cpp: kept actors' model matrices
extern "C" void hook_027F4D5C(Cpu* c) {
    uint32_t model = c->r[3];
    interp::model_calc_before(model);
    f_027F4D5C_orig(c);
    auto& left = remaining_probes();
    if (left.load(std::memory_order_acquire) <= 0) return;
    std::lock_guard probe_lock(probe_mutex);
    static std::set<uint32_t> seen;
    if (left <= 0 || !seen.insert(model).second) return;
    left--;
    char buf[1024];
    int n = snprintf(buf, sizeof buf, "[probe] model %08X data %08X:", model, ld32(model + 4));
    for (uint32_t off = 0; off < 0x140; off += 4) {
        uint32_t p = ld32(model + off);
        if (!guest_ptr(p)) continue;
        // direct array of matrices, or array of pointers to matrices
        int direct = 0;
        while (direct < 64 && plausible_mtx(p + 48 * direct)) direct++;
        if (direct >= 1)
            n += snprintf(buf + n, sizeof buf - n, " +%X->mtx[%d]", off, direct);
        else if (guest_ptr(ld32(p)) && plausible_mtx(ld32(p)))
            n += snprintf(buf + n, sizeof buf - n, " +%X->*mtx", off);
        if (n > 900) break;
    }
    // inline matrices in the model itself (base transform)
    for (uint32_t off = 0; off < 0x140; off += 4)
        if (plausible_mtx(model + off) && std::fabs(ldf32(model + off) ) <= 100) {
            n += snprintf(buf + n, sizeof buf - n, " inline@+%X", off);
            off += 44;
        }
    LOG("%s", buf);
}
