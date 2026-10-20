// Live renderer objects and memory mappings (render_objects.h).
#include "render_objects.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#if defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace render_objects {
namespace {
std::atomic<uint64_t> shaderCount{0}, pipelineCount{0};

#if defined(__linux__) || defined(__ANDROID__)
std::mutex mappingMutex;
long lastMappings = -1, lastLimit = -1;
std::chrono::steady_clock::time_point lastRead{};
bool haveRead = false;

// newline count of a /proc file, read in blocks (no allocation); -1 if it can't be opened
long proc_lines(const char* path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buffer[16384];
    long lines = 0;
    for (ssize_t n; (n = ::read(fd, buffer, sizeof buffer)) > 0;) lines += count_lines(buffer, size_t(n));
    close(fd);
    return lines;
}
long proc_number(const char* path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buffer[64];
    ssize_t n = ::read(fd, buffer, sizeof buffer - 1);
    close(fd);
    if (n <= 0) return -1;
    buffer[n] = 0;
    return strtol(buffer, nullptr, 10);
}
#endif
}  // namespace

void set_shaders(uint64_t n) { shaderCount.store(n, std::memory_order_relaxed); }
void set_pipelines(uint64_t n) { pipelineCount.store(n, std::memory_order_relaxed); }

long count_lines(const char* text, size_t size) {
    long lines = 0;
    for (size_t i = 0; i < size; ++i) lines += text[i] == '\n';
    return lines;
}

std::string format(uint64_t shaders, uint64_t pipelines, long mappings, long limit) {
    char text[160];
    int n = snprintf(text, sizeof text, "renderer objects: %llu shaders, %llu pipelines", (unsigned long long)shaders,
                     (unsigned long long)pipelines);
    if (mappings >= 0 && n > 0 && n < int(sizeof text)) {
        if (limit > 0) snprintf(text + n, sizeof text - n, "; memory mappings %ld (limit %ld)", mappings, limit);
        // apps may not read the limit (SELinux on Android): the kernel's default is 65530
        else snprintf(text + n, sizeof text - n, "; memory mappings %ld (limit not readable, usually 65530)", mappings);
    }
    return text;
}

std::string line(double maxAgeSeconds) {
    long mappings = -1, limit = -1;
#if defined(__linux__) || defined(__ANDROID__)
    {
        std::lock_guard<std::mutex> lock(mappingMutex);
        const auto now = std::chrono::steady_clock::now();
        if (!haveRead || std::chrono::duration<double>(now - lastRead).count() >= maxAgeSeconds) {
            lastMappings = proc_lines("/proc/self/maps");
            lastLimit = proc_number("/proc/sys/vm/max_map_count");
            lastRead = now;
            haveRead = true;
        }
        mappings = lastMappings;
        limit = lastLimit;
    }
#else
    (void)maxAgeSeconds;
#endif
    return format(shaderCount.load(std::memory_order_relaxed), pipelineCount.load(std::memory_order_relaxed), mappings,
                  limit);
}
}  // namespace render_objects
