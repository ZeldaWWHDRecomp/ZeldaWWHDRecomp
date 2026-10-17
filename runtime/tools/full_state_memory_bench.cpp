// Synthetic full-state memory measurement. Includes the production serializer so
// capture, compression, file writing, parsing and restore use the actual path.
// This opt-in tool never loads game files or runs guest code. Run only on an idle
// machine through full_state_memory_bench.py; it is excluded from default builds.
#include "../src/savestate.cpp"
#include <sys/resource.h>
#include <cstdio>
#include <stdexcept>

namespace config { std::string game_dir = "/nonexistent-synthetic-full-state-fixture"; }
void log_msg(const char*, ...) {}

namespace {
constexpr uint32_t kSyntheticBase = 0x10000000;
constexpr size_t kMiB = 1024 * 1024;
constexpr size_t kLiveBytes = 320 * kMiB;
constexpr size_t kPresentBytes = 256 * kMiB;
uint64_t peak_bytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage)) throw std::runtime_error("getrusage failed");
#ifdef __APPLE__
    return uint64_t(usage.ru_maxrss);
#else
    return uint64_t(usage.ru_maxrss) * 1024;
#endif
}
void fill_synthetic(std::vector<uint8_t>& memory) {
    for (size_t i = 0; i < 192 * kMiB; ++i) memory[i] = uint8_t(1 + (i & 31));
    uint32_t random = 0x8f123a75;
    for (size_t i = 192 * kMiB; i < kPresentBytes; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        memory[i] = uint8_t(random);
    }
}
uint64_t checksum(const std::vector<uint8_t>& memory) {
    uint64_t sum = 0xcbf29ce484222325ull;
    for (auto byte : memory) sum = (sum ^ byte) * 0x100000001b3ull;
    return sum;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2 || (std::string(argv[1]) != "save" && std::string(argv[1]) != "load"))
            throw std::runtime_error("usage: full_state_memory_bench save|load (WWHD_STATE_DIR required)");
        if (!getenv("WWHD_STATE_DIR")) throw std::runtime_error("WWHD_STATE_DIR must name an isolated synthetic-fixture directory");
        if (!std::filesystem::exists(std::filesystem::path(getenv("WWHD_STATE_DIR")) / ".synthetic-full-state-bench"))
            throw std::runtime_error("isolated synthetic-fixture marker is missing");
        const bool save = std::string(argv[1]) == "save";
        std::vector<uint8_t> memory(kLiveBytes);
        fill_synthetic(memory);
        const uint64_t expected = checksum(memory);
        if (!save) std::fill(memory.begin(), memory.end(), 0xcc);
        const uint64_t baseline = peak_bytes();
        const auto begin = std::chrono::steady_clock::now();
        uint64_t raw_bytes = 0;
        if (save) {
            ss::Writer metadata, files;
            files.u32(1);files.u32(0);files.u32(0);
            ss::put_section(metadata,ss::kSecFiles,files);
            ss::FullStateHeader header{};
            memcpy(header.magic, "WWHDSTAT", 8);
            header.version = ss::kVersion; header.header_size = sizeof header;
            header.cpu_size = sizeof(Cpu); header.game_id = ss::game_id();
            if (!ss::write_memory_slot(1,header,metadata,{{kSyntheticBase,uint32_t(kLiveBytes)}},
                 [&](uint32_t a){return memory.data()+(a-kSyntheticBase);},
                 [](const uint8_t*,size_t){return true;},raw_bytes))
                throw std::runtime_error("production streaming write failed");
        } else {
            std::string why;
            auto state = ss::read_slot(1, why);
            if (!state) throw std::runtime_error("production read_slot failed: " + why);
            raw_bytes = state->payload.size();
            ss::restore_regions(state->regs, state->chunks,
                [&](uint32_t a) { return memory.data() + (a - kSyntheticBase); },
                [](const uint8_t*, size_t) { return true; });
            if (checksum(memory) != expected) throw std::runtime_error("restored synthetic memory differs");
        }
        const uint64_t peak = peak_bytes();
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        std::printf("{\"mode\":\"%s\",\"format\":%u,\"guest_bytes\":%zu,\"present_bytes\":%zu,\"raw_bytes\":%llu,"
                    "\"baseline_rss_bytes\":%llu,\"peak_rss_bytes\":%llu,\"extra_rss_bytes\":%llu,\"elapsed_ms\":%.3f}\n",
                    save ? "save" : "load", ss::kVersion, kLiveBytes, kPresentBytes,
                    (unsigned long long)raw_bytes, (unsigned long long)baseline,
                    (unsigned long long)peak, (unsigned long long)(peak - baseline), ms);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "synthetic full-state measurement: %s\n", error.what()); return 1;
    }
}
