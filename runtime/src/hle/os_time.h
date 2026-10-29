// The Wii U calendar clock (coreinit OSTime): signed timer ticks since 2000-01-01 00:00:00 local
// time, at the timer clock (bus clock / 4 = timebase::kTicksPerSec). OSGetTime returns it,
// OSTicksToCalendarTime turns it into a date. The game stamps every save with OSGetTime
// (SaveMgr 02721A8C into dSv_player_status_b_c::mDateIPL, save data +0x18) and the Quest Log
// list shows that stamp (hdPictureSlotSummaries_c::refresh 02726FC4 -> 02642EEC ->
// OSTicksToCalendarTime), so both must use this clock (#322).
//
// Pure conversions here (runtime/tools/os_time_test.cpp); the boot bias that turns the guest
// timebase into this clock lives with the timebase (threads.cpp, os_time::now).
#pragma once
#include <cstdint>
#include <ctime>

namespace os_time {

constexpr int64_t kTicksPerSec = 62156250;  // = timebase::kTicksPerSec (Espresso bus clock / 4)
constexpr int64_t kSecondsPerDay = 86400;

// OSCalendarTime (coreinit): ten big-endian int32 in this order
struct Calendar {
    int32_t sec, min, hour, mday, mon /* 0-11 */, year, wday /* 0 = Sunday */, yday /* 0-365 */, msec, usec;
};

// days from 2000-01-01 to y-m-d (proleptic Gregorian, m 1-12; negative before 2000)
int64_t days_from_2000(int64_t y, int m, int d);
// OSTicksToCalendarTime: OSTime -> calendar fields (no time zone: OSTime is already local time)
Calendar to_calendar(int64_t ticks);
// OSTime of a broken-down local time (tm_year since 1900, tm_mon 0-11) plus nanoseconds
int64_t from_local_tm(const std::tm& t, int64_t nanoseconds = 0);
// OSTime of a host time stamp (seconds since 1970 UTC + nanoseconds), in the host's time zone
int64_t from_unix(int64_t unix_seconds, int64_t nanoseconds = 0);
// OSTime of the host's wall clock now
int64_t host_now();

// The bias: OSGetTime = guest timebase + bias. Chosen once per run (start_bias):
//   - WWHD_OS_TIME=<seconds since 1970>: that date and time read as the console's local time, the
//     same on every host and in every time zone (tests);
//   - WWHD_OS_TIME=host: the host's local wall clock;
//   - unset: the host's clock, or kFixedOSTime under WWHD_TEST_FIXED_SEED, so that repeatable runs
//     stay repeatable (OSGetTime then differs between runs only as the guest timebase does, as
//     before this clock existed).
// Full save states keep the bias; loading one sets restored_bias, so OSGetTime never runs
// backwards against the times the state's memory holds (and a repeatable run that loads a state
// stays repeatable). States of builds without the bias (OSGetTime was the guest timebase then)
// keep the current bias: their clock jumps forward to this run's date.
constexpr int64_t kUnixTo2000 = 946684800;  // 2000-01-01 00:00:00 UTC in seconds since 1970
constexpr int64_t kFixedOSTime = (9497 * kSecondsPerDay + 12 * 3600) * kTicksPerSec;  // 2026-01-01 12:00
// env: the value of WWHD_OS_TIME (null if unset); host_ostime: host_now(); guest: the guest
// timebase now. `why` gets a note for an invalid value (the host clock is used then).
int64_t start_bias(const char* env, bool fixed_seed, int64_t host_ostime, uint64_t guest, const char** why = nullptr);
inline int64_t restored_bias(int64_t current, bool state_has_bias, int64_t state_bias) {
    return state_has_bias && state_bias > current ? state_bias : current;
}

// the guest clock (threads.cpp): the bias, OSGetTime, and an OSTime back on the guest timebase
// (alarm starts)
int64_t bias();
void set_bias(int64_t bias);  // a full save state's (restored_bias)
uint64_t now();
uint64_t to_guest(uint64_t os_ticks);

// cking.sav: three file blocks of 0xA94 bytes; each holds the save data, then at +0xA8C the
// byte sum and complement sum (u32, big-endian) over its first 0xA8C bytes (SaveMgr 02721AFC)
constexpr uint32_t kSaveBlock = 0xA94;
constexpr uint32_t kSaveSummed = 0xA8C;
constexpr uint32_t kSaveDateOffset = 0x18;  // dSv_player_status_b_c::mDateIPL (u64, big-endian)
// Earlier port builds stamped saves with the time since the game started instead of OSTime
// (always far below one year), so their Quest Log list showed no real date. A stamp below this
// is one of those; 0 is an unused Quest Log and stays.
constexpr int64_t kEarliestRealStamp = 365 * kSecondsPerDay * kTicksPerSec;
// One file block read from cking.sav (`block`, kSaveBlock bytes): when its checksum is valid and
// its stamp is such a time-since-start value, writes `stamp` (the file's last-modified time as
// OSTime) and the matching checksum. True if the block changed.
bool repair_save_stamp(uint8_t* block, int64_t stamp);

}  // namespace os_time
