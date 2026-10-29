// OSTime conversions and the save-date repair (runtime/src/hle/os_time.h, #322)
#include "hle/os_time.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

using namespace os_time;

static void check_calendar(int64_t ticks, int year, int mon, int mday, int hour, int min, int sec, int wday, int yday) {
    const Calendar c = to_calendar(ticks);
    if (c.year != year || c.mon != mon || c.mday != mday || c.hour != hour || c.min != min || c.sec != sec ||
        c.wday != wday || c.yday != yday) {
        std::fprintf(stderr, "ticks %lld: got %d-%d-%d %d:%d:%d wday %d yday %d\n", (long long)ticks, c.year, c.mon,
                     c.mday, c.hour, c.min, c.sec, c.wday, c.yday);
        std::abort();
    }
}

static int64_t at(int y, int m, int d, int hh, int mm, int ss) {
    return (days_from_2000(y, m, d) * kSecondsPerDay + hh * 3600 + mm * 60 + ss) * kTicksPerSec;
}

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
static void put32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v); }
static void seal(uint8_t* b) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < kSaveSummed; i++) s += b[i];
    put32(b + kSaveSummed, s);
    put32(b + kSaveSummed + 4, kSaveSummed * 0xFFFFFFFFu - s);
}
static bool sealed(const uint8_t* b) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < kSaveSummed; i++) s += b[i];
    return be32(b + kSaveSummed) == s && be32(b + kSaveSummed + 4) == kSaveSummed * 0xFFFFFFFFu - s;
}
static int64_t stamp_of(const uint8_t* b) {
    return int64_t(uint64_t(be32(b + kSaveDateOffset)) << 32 | be32(b + kSaveDateOffset + 4));
}

int main() {
    // epoch and known dates (wday: 0 = Sunday; mon 0-11; yday 0-365)
    check_calendar(0, 2000, 0, 1, 0, 0, 0, 6, 0);                      // Saturday
    check_calendar(at(2000, 2, 29, 12, 0, 0), 2000, 1, 29, 12, 0, 0, 2, 59);  // leap day, Tuesday
    check_calendar(at(2026, 10, 27, 7, 54, 0), 2026, 9, 27, 7, 54, 0, 2, 299);  // the #322 save: Tuesday
    check_calendar(at(2024, 12, 31, 23, 59, 59), 2024, 11, 31, 23, 59, 59, 2, 365);
    check_calendar(at(2100, 3, 1, 0, 0, 0), 2100, 2, 1, 0, 0, 0, 1, 59);  // 2100 is no leap year
    check_calendar(-1, 1999, 11, 31, 23, 59, 59, 5, 364);              // just before the epoch
    check_calendar(int64_t(0x1FFC9D62Cll), 2000, 0, 1, 0, 2, 18, 6, 0);  // the #322 save's stamp: 138 s
    // sub-second fields
    const Calendar half = to_calendar(kTicksPerSec / 2 + kTicksPerSec * 4 / 1000);  // 504 ms exactly
    assert(half.msec == 504 && half.usec == 0);
    // 2012-11-18 (Wii U launch day) is a Sunday
    assert(to_calendar(at(2012, 11, 18, 0, 0, 0)).wday == 0);

    // from_local_tm is the inverse of to_calendar for a local date
    std::tm t{};
    t.tm_year = 2026 - 1900, t.tm_mon = 9, t.tm_mday = 27, t.tm_hour = 7, t.tm_min = 54, t.tm_sec = 0;
    assert(from_local_tm(t) == at(2026, 10, 27, 7, 54, 0));
    assert(from_local_tm(t, 500000000) == at(2026, 10, 27, 7, 54, 0) + kTicksPerSec / 2);

    // from_unix follows the host time zone: same fields as localtime
    const int64_t unix_322 = 1793087040;  // 2026-10-27 07:44 UTC
    const std::time_t tt = std::time_t(unix_322);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    const Calendar lc = to_calendar(from_unix(unix_322));
    assert(lc.year == local.tm_year + 1900 && lc.mon == local.tm_mon && lc.mday == local.tm_mday &&
           lc.hour == local.tm_hour && lc.min == local.tm_min && lc.sec == local.tm_sec && lc.wday == local.tm_wday &&
           lc.yday == local.tm_yday);
#ifndef _WIN32
    // in UTC exactly 07:44
    setenv("TZ", "UTC", 1);
    tzset();
    check_calendar(from_unix(unix_322), 2026, 9, 27, 7, 44, 0, 2, 299);
    setenv("TZ", "Europe/Vienna", 1);  // UTC+1 in winter, UTC+2 in summer
    tzset();
    check_calendar(from_unix(unix_322), 2026, 9, 27, 8, 44, 0, 2, 299);  // after the switch back on 10-25
    check_calendar(from_unix(1782545040), 2026, 5, 27, 9, 24, 0, 6, 177);  // 2026-06-27 07:24 UTC, summer
#endif
    // host_now is a real date (after 2020)
    assert(to_calendar(host_now()).year >= 2020);

    // the bias: WWHD_OS_TIME, the fixed-seed default, the host clock
    check_calendar(kFixedOSTime, 2026, 0, 1, 12, 0, 0, 4, 0);  // Thursday
    const int64_t host = at(2026, 10, 28, 20, 0, 0);
    const uint64_t guest = 5 * uint64_t(kTicksPerSec);
    assert(start_bias(nullptr, false, host, guest) == host - int64_t(guest));
    assert(start_bias("", false, host, guest) == host - int64_t(guest));
    assert(start_bias(nullptr, true, host, guest) == kFixedOSTime);  // repeatable: no host time in it
    assert(start_bias("host", true, host, guest) == host - int64_t(guest));
    // seconds since 1970 read as console local time, whatever the host's time zone (TZ is Vienna here)
    assert(start_bias("1793087040", false, host, guest) == at(2026, 10, 27, 7, 44, 0));
    assert(start_bias("1793087040", true, host, guest) == at(2026, 10, 27, 7, 44, 0));
    const char* why = nullptr;
    assert(start_bias("tomorrow", true, host, guest, &why) == host - int64_t(guest) && why);
    why = nullptr;
    assert(start_bias("12", false, host, guest, &why) == host - int64_t(guest) && why);  // before 2000
    // a full save state's bias: never backwards, absent (older states) = keep the current one
    assert(restored_bias(100, false, 0) == 100);
    assert(restored_bias(100, true, 50) == 100);
    assert(restored_bias(100, true, 150) == 150);
    assert(restored_bias(kFixedOSTime, true, kFixedOSTime) == kFixedOSTime);  // repeatable runs stay so

    // the save-date repair
    std::vector<uint8_t> block(kSaveBlock, 0);
    for (uint32_t i = 0; i < 0x768; i++) block[i] = uint8_t(i * 7 + 1);
    put32(&block[kSaveDateOffset], 0x00000001u);  // 0x1FFC9D62C ticks: 138 s after the start
    put32(&block[kSaveDateOffset + 4], 0xFFC9D62Cu);
    seal(block.data());
    const int64_t when = at(2026, 10, 27, 7, 54, 0);
    std::vector<uint8_t> copy = block;
    assert(repair_save_stamp(copy.data(), when));
    assert(stamp_of(copy.data()) == when && sealed(copy.data()));
    for (uint32_t i = 0; i < kSaveSummed; i++)
        if (i < kSaveDateOffset || i >= kSaveDateOffset + 8) assert(copy[i] == block[i]);
    assert(!repair_save_stamp(copy.data(), when + kTicksPerSec));  // a real date stays
    assert(stamp_of(copy.data()) == when);
    // an unused Quest Log (stamp 0) stays
    std::vector<uint8_t> empty(kSaveBlock, 0);
    seal(empty.data());
    assert(!repair_save_stamp(empty.data(), when) && stamp_of(empty.data()) == 0);
    // a damaged block is not touched (and not made valid)
    std::vector<uint8_t> bad = block;
    bad[0x100] ^= 1;
    assert(!repair_save_stamp(bad.data(), when) && !sealed(bad.data()));
    // no real date to use: nothing changes
    copy = block;
    assert(!repair_save_stamp(copy.data(), 5) && copy == block);
    std::puts("os_time_test: ok");
    return 0;
}
