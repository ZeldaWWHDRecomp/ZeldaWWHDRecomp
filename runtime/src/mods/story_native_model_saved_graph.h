// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include "story_native_event_memory.h"
#include "savestate_native_gate.h"
#include <memory>
#include <optional>
#include <vector>
namespace story_native_wait_runtime_model_graph {class Context;}
namespace story_native_wait_runtime_controller {class Context;class SavedRunCheckpoint;}
namespace story_native_model_saved_graph {
// Proposed successful-parser export: every bounded downstream read plus every
// native writable scratch/consumer span. A DTO alone carries no provenance.
struct Range {uint32_t address{},bytes{};bool operator==(const Range&)const=default;};
struct Trace {std::vector<Range> reads,writes;bool operator==(const Trace&)const=default;};
class Lineage {
 struct Identity {};std::shared_ptr<const Identity>identity_;
 explicit Lineage(std::shared_ptr<const Identity>p):identity_(std::move(p)){}
 static Lineage issue();
 friend class story_native_wait_runtime_model_graph::Context;
 friend class Receipt;
public:
 Lineage()=default;
 bool valid()const noexcept{return bool(identity_);}
};
// Host-only exact bytes, never a serializable preclaim baseline or a native
// entry/rebind capability. Only the actual preclaim Context can issue lineage
// and capture; SavedView can only be compared with an already issued receipt.
class Receipt {
 struct Bytes {uint32_t address{};std::vector<uint8_t>bytes;};
 story_native_event_observation::Expected expected_;
 Lineage lineage_;std::vector<Bytes>bytes_;
 Receipt(story_native_event_observation::Expected,Lineage,std::vector<Bytes>);
 static std::optional<Receipt>capture(const story_native_event_observation::Expected&,
  const Lineage&,const Trace&,const story_native_event_memory::Read&);
 friend class story_native_wait_runtime_model_graph::Context;
 friend class story_native_wait_runtime_controller::Context;
 friend class story_native_wait_runtime_controller::SavedRunCheckpoint;
 Receipt fork_host()const{return Receipt(expected_,lineage_,bytes_);}
public:
 Receipt(const Receipt&)=delete;Receipt&operator=(const Receipt&)=delete;
 Receipt(Receipt&&)noexcept=default;Receipt&operator=(Receipt&&)noexcept=default;
 bool matches(const story_native_event_observation::Expected&,const Lineage&)const noexcept;
 bool matches_live(const story_native_event_memory::Read&)const;
 bool matches_saved(const ss::native_gate::SavedView&)const;
 size_t span_count()const noexcept{return bytes_.size();}
 size_t byte_count()const noexcept;
};
}
