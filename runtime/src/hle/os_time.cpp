// OSTime conversions (os_time.h)
#include "os_time.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace os_time {
namespace {
constexpr int64_t kDays1970To2000 = 10957;

// days since 1970-01-01 of y-m-d (H. Hinnant's days_from_civil)
int64_t days_from_1970(int64_t y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

uint32_t load_be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void store_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}
}  // namespace

int64_t days_from_2000(int64_t y, int m, int d) { return days_from_1970(y, m, d) - kDays1970To2000; }

Calendar to_calendar(int64_t ticks) {
    Calendar c{};
    const int64_t secs = floor_div(ticks, kTicksPerSec);
    const int64_t sub = ticks - secs * kTicksPerSec;  // 0 .. kTicksPerSec-1
    const int64_t days = floor_div(secs, kSecondsPerDay);
    const int64_t day_secs = secs - days * kSecondsPerDay;
    c.hour = int32_t(day_secs / 3600);
    c.min = int32_t(day_secs / 60 % 60);
    c.sec = int32_t(day_secs % 60);
    const int64_t usec_total = sub * 1000000 / kTicksPerSec;
    c.msec = int32_t(usec_total / 1000);
    c.usec = int32_t(usec_total % 1000);
    // civil_from_days
    const int64_t z = days + kDays1970To2000 + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp < 10 ? mp + 3 : mp - 9;
    const int64_t y = yoe + era * 400 + (m <= 2);
    c.year = int32_t(y);
    c.mon = int32_t(m - 1);
    c.mday = int32_t(d);
    c.yday = int32_t(days - days_from_2000(y, 1, 1));
    const int64_t wday = (days + 6) % 7;  // 2000-01-01 was a Saturday (6)
    c.wday = int32_t(wday < 0 ? wday + 7 : wday);
    return c;
}

int64_t from_local_tm(const std::tm& t, int64_t nanoseconds) {
    const int64_t days = days_from_2000(int64_t(t.tm_year) + 1900, t.tm_mon + 1, t.tm_mday);
    const int64_t secs = days * kSecondsPerDay + int64_t(t.tm_hour) * 3600 + int64_t(t.tm_min) * 60 + t.tm_sec;
    return secs * kTicksPerSec + nanoseconds * kTicksPerSec / 1000000000;
}

int64_t from_unix(int64_t unix_seconds, int64_t nanoseconds) {
    const std::time_t t = std::time_t(unix_seconds);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &t) != 0) return 0;
#else
    if (!localtime_r(&t, &local)) return 0;
#endif
    return from_local_tm(local, nanoseconds);
}

int64_t host_now() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t secs = floor_div(ns, 1000000000);
    return from_unix(secs, ns - secs * 1000000000);
}

int64_t start_bias(const char* env, bool fixed_seed, int64_t host_ostime, uint64_t guest, const char** why) {
    const int64_t host = host_ostime - (int64_t)guest;
    if (!env || !*env) return fixed_seed ? kFixedOSTime : host;
    if (!std::strcmp(env, "host")) return host;
    char* end = nullptr;
    const long long unix_seconds = std::strtoll(env, &end, 10);
    if (end == env || *end || unix_seconds < kUnixTo2000) {
        if (why) *why = "WWHD_OS_TIME: not a number of seconds since 1970 (from 2000 on) or \"host\"; using the host clock";
        return host;
    }
    return (int64_t(unix_seconds) - kUnixTo2000) * kTicksPerSec;
}

bool repair_save_stamp(uint8_t* block, int64_t stamp) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < kSaveSummed; i++) sum += block[i];
    const uint32_t complement = kSaveSummed * 0xFFFFFFFFu - sum;
    if (load_be32(block + kSaveSummed) != sum || load_be32(block + kSaveSummed + 4) != complement) return false;
    uint8_t* date = block + kSaveDateOffset;
    const int64_t old = int64_t(uint64_t(load_be32(date)) << 32 | load_be32(date + 4));
    if (old <= 0 || old >= kEarliestRealStamp || stamp < kEarliestRealStamp) return false;
    for (int i = 0; i < 8; i++) sum -= date[i];
    store_be32(date, uint32_t(uint64_t(stamp) >> 32));
    store_be32(date + 4, uint32_t(stamp));
    for (int i = 0; i < 8; i++) sum += date[i];
    store_be32(block + kSaveSummed, sum);
    store_be32(block + kSaveSummed + 4, kSaveSummed * 0xFFFFFFFFu - sum);
    return true;
}

}  // namespace os_time
