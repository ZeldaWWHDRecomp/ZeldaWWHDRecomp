#pragma once
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace overlay {
struct AndroidTelemetry {
    double busy = -1;
    std::vector<std::pair<std::string, double>> temperatures;
    static bool number(const std::filesystem::path& path, double& value) {
        FILE* f = fopen(path.string().c_str(), "r");
        if (!f) return false;
        bool ok = fscanf(f, "%lf", &value) == 1 && std::isfinite(value);
        fclose(f); return ok;
    }
    void read(const std::filesystem::path& root = "/sys") {
        busy = -1; temperatures.clear();
        // kgsl reports busy and total time over the same sampling interval.
        auto kgsl = root / "class/kgsl/kgsl-3d0/gpubusy";
        if (FILE* f = fopen(kgsl.string().c_str(), "r")) {
            double active = 0, total = 0;
            if (fscanf(f, "%lf %lf", &active, &total) == 2 && total > 0 && active >= 0 && active <= total)
                busy = 100 * active / total;
            fclose(f);
        }
        // Some Mali/MediaTek kernels expose a percentage through devfreq. Restrict to GPU nodes.
        std::error_code ec;
        if (busy < 0) for (const auto& entry : std::filesystem::directory_iterator(root / "class/devfreq", ec)) {
            auto name = entry.path().filename().string();
            if (name.find("gpu") == std::string::npos && name.find("mali") == std::string::npos) continue;
            double value;
            if (number(entry.path() / "load", value) && value >= 0 && value <= 100) { busy = value; break; }
        }
        ec.clear();
        for (const auto& entry : std::filesystem::directory_iterator(root / "class/thermal", ec)) {
            if (!entry.path().filename().string().starts_with("thermal_zone")) continue;
            char type[80] = {};
            FILE* f = fopen((entry.path() / "type").string().c_str(), "r");
            if (!f) continue;
            bool ok = fscanf(f, "%79s", type) == 1; fclose(f);
            std::string name(type);
            if (!ok || (name.find("gpu") == std::string::npos && name.find("cpu") == std::string::npos &&
                        name.find("soc") == std::string::npos)) continue;
            double value;
            if (number(entry.path() / "temp", value) && value >= -40000 && value <= 200000)
                temperatures.emplace_back(name, value / 1000);
            if (temperatures.size() == 8) break;
        }
    }
};
}
