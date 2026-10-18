// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_depth.h"
#include <span>
#include <vector>
namespace story_native_event_checkpoint {
enum class Transaction {Stable,Publishing,Invoking,Draining,Detaching,Invalid};
enum class Reason {None,Quiescence,Transaction,Depth,Layout,Regions,Missing,Record,Stamp,Image};
struct Range {uint32_t address=0,bytes=0;};
struct Region {uint32_t address=0;std::span<const uint8_t> bytes;};
struct Layout {Range record,stamp,depth;};
struct Result {bool valid=false;Reason reason=Reason::Layout;story_native_event_lease::Phase phase=story_native_event_lease::Phase::Empty;};
// Post-global-quiesce gate. Depth must be the persistent aggregate Tracker's
// actual record; caller must keep all guest/non-guest entry fenced afterward.
Result capture(bool globally_quiescent,Transaction,story_native_event_lease::Phase,
 const story_native_event_observation::Expected&,const story_native_event_depth::Record&);
// Strict saved byte views ONLY. Regions are actual fully materialized snapshot
// bytes (sparse zero chunks must be supplied explicitly); no live-memory reads.
// Independently authorized Expected and make_stamp(Expected) must be supplied.
// Success is NOT an opaque Restore certificate, allocation/live ownership proof
// or permission to thaw. Collector/Install pure rehydrate checks remain required.
Result saved(Transaction,const story_native_event_observation::Expected&,const Layout&,
 const std::array<uint8_t,64>& expected_stamp,std::span<const Region>);
}
