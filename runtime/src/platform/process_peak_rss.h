#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace process_peak_rss {
enum class Unit { Bytes, KiB };
struct Sample {
  int64_t raw = 0;
  Unit unit = Unit::Bytes;
};
inline bool bytes(const Sample &sample, uint64_t &result) {
  if (sample.raw < 0)
    return false;
  const uint64_t factor = sample.unit == Unit::KiB ? 1024 : 1;
  const auto raw = static_cast<uint64_t>(sample.raw);
  if (raw > std::numeric_limits<uint64_t>::max() / factor)
    return false;
  result = raw * factor;
  return true;
}
inline bool query(Sample &result) {
#if defined(_WIN32)
  (void)result;
  return false;
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0)
    return false;
  result.raw = usage.ru_maxrss;
#if defined(__APPLE__)
  result.unit = Unit::Bytes;
#else
  result.unit = Unit::KiB;
#endif
  return true;
#endif
}
// Injection seam also proves a disabled endpoint never calls the OS sampler.
using Query = bool (*)(Sample &);
inline bool write_endpoint_to(const char *path, uint64_t render_frame,
                              Query sampler = query) {
  if (!path || !*path)
    return true;
  Sample sample;
  uint64_t peak = 0;
  const bool sampled = sampler(sample);
  const bool valid = sampled && bytes(sample, peak);
  FILE *file = std::fopen(path, "wb");
  if (!file) {
    std::fprintf(stderr, "process_peak_rss: endpoint write failed\n");
    return false;
  }
  int count;
  if (valid) {
    count = std::fprintf(
        file,
        "{\"render_frame\":%llu,\"ru_maxrss_raw\":%lld,\"ru_maxrss_unit\":\"%"
        "s\","
        "\"peak_rss_bytes\":%llu,\"includes_boot\":true,"
        "\"scope\":\"whole_process_lifetime_through_scripted_endpoint\"}\n",
        static_cast<unsigned long long>(render_frame),
        static_cast<long long>(sample.raw),
        sample.unit == Unit::Bytes ? "bytes" : "KiB",
        static_cast<unsigned long long>(peak));
  } else {
    count = std::fprintf(
        file,
        "{\"render_frame\":%llu,\"includes_boot\":true,"
        "\"scope\":\"whole_process_lifetime_through_scripted_endpoint\","
        "\"error\":\"unsupported_or_failed_kernel_peak_rss\"}\n",
        static_cast<unsigned long long>(render_frame));
  }
  const bool flushed = std::fflush(file) == 0;
  const bool closed = std::fclose(file) == 0;
  const bool complete = count >= 0 && flushed && closed;
  std::fprintf(stderr, "process_peak_rss: endpoint %s\n",
               complete && valid ? "write complete" : "failed");
  return complete && valid;
}
inline bool write_endpoint(uint64_t render_frame) {
  return write_endpoint_to(std::getenv("WWHD_PROCESS_PEAK_RSS"), render_frame);
}
} // namespace process_peak_rss
