// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_turn_record.h"
#include "story_native_event_checkpoint.h"
namespace story_native_turn_checkpoint {
using Range=story_native_event_checkpoint::Range;
using Region=story_native_event_checkpoint::Region;
using Transaction=story_native_event_checkpoint::Transaction;
struct Layout {Range record,stamp,depth,flags;};
enum class Reason {None,Transaction,Layout,Regions,Missing,Record,Stamp,Depth,Flags,Image};
struct Result {bool valid=false;Reason reason=Reason::Layout;};
// authorized comes from independently authenticated definition/snapshot journal,
// never unchecked decoding of the same byte views. No live read fallback exists.
// Success grants neither collector Restore authority nor permission to thaw/free.
Result saved(Transaction,const story_native_turn_record::Record&authorized,const Layout&,
 const std::array<uint8_t,64>&expected_stamp,std::span<const Region>);
}
