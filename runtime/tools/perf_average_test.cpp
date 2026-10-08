#include "overlay/perf_average.h"
#include "overlay/android_telemetry.h"
#include <cassert>
#include <filesystem>
#include <fstream>
int main() {
    overlay::PerfAverage a;
    a.sample(0, 100, 50, 0, 0, 30, 1);
    a.sample(2, 160, 110, 0, 0, 30, 1);
    assert(a.fps == 30 && a.logic == 30);
    a.sample(4, 340, 170, 0, 0, 30, 1);
    assert(a.fps == 60 && a.logic == 30); // cumulative, not mean of instantaneous rates
    for (int change = 0; change < 4; ++change) {
        a.sample(5 + change, 400, 200, change > 0, change > 1, change > 2 ? 120 : 30, 2);
        assert(a.fps == 0 && a.logic == 0);
    }
    a.sample(10, 640, 320, 1, 1, 120, 2);
    assert(a.fps == 120 && a.logic == 60);
    a.sample(11, 0, 0, 1, 1, 120, 2); assert(a.fps == 0);
    a.reset(); assert(!a.started);
    auto root = std::filesystem::temp_directory_path() / "wwhd-telemetry-test";
    std::filesystem::create_directories(root / "class/kgsl/kgsl-3d0");
    std::ofstream(root / "class/kgsl/kgsl-3d0/gpubusy") << "25 100";
    overlay::AndroidTelemetry telemetry; telemetry.read(root); assert(telemetry.busy == 25);
    std::ofstream(root / "class/kgsl/kgsl-3d0/gpubusy") << "25 0";
    telemetry.read(root); assert(telemetry.busy == -1);
    std::filesystem::remove_all(root);
}
