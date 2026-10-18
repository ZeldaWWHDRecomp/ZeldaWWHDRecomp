// SPDX-License-Identifier: MPL-2.0
#include "story_native_stage_owner_observer.h"
#include <atomic>
#include <limits>
#include <mutex>

namespace story_native_stage_owner_observer { namespace {
story_native_stage_owner::Tracker owner;
std::atomic<Inspect> inspector{nullptr};
std::atomic<Live> live_inspector{nullptr};
std::atomic<Saved> saved_inspector{nullptr};
uint32_t active_control{};
story_native_stage_owner::Receipt active_receipt{};
uint32_t historical_control{};
Candidate historical_candidate{};
struct PassiveSlot {
 std::atomic<uint64_t>sequence{0},cookie{0};
 std::atomic<uint32_t>canonical{0},phase{0},before{0},after{0};
 std::array<std::atomic<uint32_t>,4>args{};
};
std::array<PassiveSlot,16>passive_history;
std::atomic<uint64_t>passive_sequence{0};
void note(uint32_t canonical,const Args&args,uint64_t cookie,MutationPhase phase,
 uint32_t before,uint32_t after)noexcept{
 // Called only inside the existing observer lifetime fence. Saturation merely
 // stops diagnostics; it never changes owner admission or native execution.
 auto sequence=passive_sequence.load(std::memory_order_seq_cst);
 if(sequence==std::numeric_limits<uint64_t>::max())return;
 ++sequence;auto&slot=passive_history[(sequence-1)%passive_history.size()];
 slot.sequence.store(0,std::memory_order_seq_cst);
 slot.cookie.store(cookie,std::memory_order_seq_cst);slot.canonical.store(canonical,std::memory_order_seq_cst);
 slot.phase.store(static_cast<uint32_t>(phase),std::memory_order_seq_cst);
 slot.before.store(before,std::memory_order_seq_cst);slot.after.store(after,std::memory_order_seq_cst);
 for(size_t i=0;i<4;++i)slot.args[i].store(args.r3_r6[i],std::memory_order_seq_cst);
 slot.sequence.store(sequence,std::memory_order_seq_cst);passive_sequence.store(sequence,std::memory_order_seq_cst);
}


void clear_active() noexcept { active_control=0; active_receipt={}; }
void discard() noexcept {
    clear_active(); historical_control=0; historical_candidate={};
}
bool within(uint32_t ptr, uint32_t base, uint32_t bytes) noexcept {
    return ptr && base && bytes && uint64_t(ptr)>=base &&
           uint64_t(ptr)<uint64_t(base)+bytes &&
           uint64_t(base)+bytes<=uint64_t(std::numeric_limits<uint32_t>::max())+1;
}
bool graph_pointer(uint32_t ptr, const Candidate& c, uint32_t control) noexcept {
    return ptr && (ptr==control || ptr==c.info || ptr==c.archive_wrapper ||
           ptr==c.archive_holder || ptr==c.archive_resource || ptr==c.entry ||
           ptr==c.archive_key || ptr==c.resource_key || ptr==c.request_heap ||
           ptr==c.pack_heap || ptr==c.data_heap || ptr==c.data_heap_parent ||
           within(ptr,c.archive_base,c.archive_bytes) ||
           within(ptr,c.outer_base,c.outer_bytes) ||
           within(ptr,c.resource,c.resource_bytes));
}
uint64_t before_setinfo(Cpu*, const Args&args) {
    const auto before=active_receipt.generation;clear_active();const auto cookie=owner.begin_native();
    note(story_native_stage_owner_hooks::SetInfo(),args,cookie,MutationPhase::Begin,before,0);return cookie;
}
uint64_t before_destroy(Cpu*, uint32_t canonical, const Args&args) {
    // Every non-free destructive boundary can invalidate the graph, including an
    // ancestor heap or manager teardown whose r3 does not name Stage.
    const auto before=active_receipt.generation;clear_active();const auto cookie=owner.begin_native();
    note(canonical,args,cookie,MutationPhase::Begin,before,0);return cookie;
}
void finish_revalidated(Cpu* cpu, const Args* args, uint32_t result,
                        uint64_t cookie) {
    if (!cookie) { discard(); owner.finish_native(cookie); return; }
    const Inspect inspect=inspector.load(std::memory_order_acquire);
    const Live live=live_inspector.load(std::memory_order_acquire);
    Candidate accepted{};
    uint32_t accepted_control{};
    bool history_dead=false;
    const bool issued=owner.finish_setinfo(cookie,[&]() -> std::optional<story_native_stage_owner::Receipt> {
        Candidate observed{}, again{};
        if (args && inspect && live &&
            inspect(cpu,*args,result,observed)==story_native_setinfo_observation::Result::Valid &&
            live(args->r3_r6[0],observed,again) && again==observed) {
            accepted=observed;
            accepted_control=args->r3_r6[0];
            return story_native_stage_owner::Receipt{observed,0};
        }
        // A prior Stage observation is historical data only. It must pass a
        // fresh, exact native tree/owner check after this original body.
        if (historical_control && live &&
            live(historical_control,historical_candidate,again) &&
            again==historical_candidate) {
            accepted=historical_candidate;
            accepted_control=historical_control;
            return story_native_stage_owner::Receipt{accepted,0};
        }
        if (historical_control) history_dead=true;
        return std::nullopt;
    });
    clear_active();
    if (history_dead) {historical_control=0; historical_candidate={};}
    if (issued) {
        auto scan=owner.scan();
        if (scan.valid() && scan.receipt().owner==accepted) {
            active_control=accepted_control;
            active_receipt=scan.receipt();
            historical_control=accepted_control;
            historical_candidate=accepted;
        } else {discard();owner.fault();}
    }
}
void after_setinfo(Cpu* cpu, const Args& args, uint32_t result, uint64_t cookie) {
    finish_revalidated(cpu,&args,result,cookie);
    note(story_native_stage_owner_hooks::SetInfo(),args,cookie,MutationPhase::End,0,active_receipt.generation);
}
void failed_setinfo(Cpu*, const Args&args, uint64_t cookie) {
    finish_revalidated(nullptr,nullptr,0,cookie);
    note(story_native_stage_owner_hooks::SetInfo(),args,cookie,MutationPhase::End,0,active_receipt.generation);
}
void after_destroy(Cpu*, uint32_t canonical, const Args&args, uint64_t cookie) {
    finish_revalidated(nullptr,nullptr,0,cookie);
    note(canonical,args,cookie,MutationPhase::End,0,active_receipt.generation);
}
uint64_t before_free(Cpu*, const Args& args) {
    // ArrayDelete is hot and generic. Ignore unrelated pointers. A matching
    // interior pointer is conservatively treated as destruction of Stage.
    if (!historical_control ||
        !graph_pointer(args.r3_r6[0],historical_candidate,historical_control)) return 0;
    const auto before=active_receipt.generation;clear_active();const auto cookie=owner.begin_native();
    note(story_native_stage_owner_hooks::ArrayDelete(),args,cookie,MutationPhase::Begin,before,0);return cookie;
}
void after_free(Cpu*, const Args&args, uint64_t cookie) {
    if (cookie) {finish_revalidated(nullptr,nullptr,0,cookie);
     note(story_native_stage_owner_hooks::ArrayDelete(),args,cookie,MutationPhase::End,0,active_receipt.generation);}
}
void fault() noexcept {
    std::lock_guard lock(owner.lifetime_fence());
    const auto before=active_receipt.generation;discard(); owner.fault();
    note(0,{},0,MutationPhase::Fault,before,0);
}
const Observer callbacks{before_setinfo,before_destroy,after_setinfo,
    failed_setinfo,after_destroy,fault,&owner.lifetime_fence(),before_free,after_free};
} // namespace

MutationHistory mutation_history()noexcept{
 MutationHistory result;const auto last=passive_sequence.load(std::memory_order_seq_cst);
 const auto first=last>passive_history.size()?last-passive_history.size()+1:1;
 for(uint64_t expected=first;expected&&expected<=last;++expected){
  const auto&slot=passive_history[(expected-1)%passive_history.size()];
  if(slot.sequence.load(std::memory_order_seq_cst)!=expected)continue;
  MutationDiagnostic d;d.sequence=expected;d.cookie=slot.cookie.load(std::memory_order_seq_cst);
  d.canonical=slot.canonical.load(std::memory_order_seq_cst);d.phase=static_cast<MutationPhase>(slot.phase.load(std::memory_order_seq_cst));
  d.before_generation=slot.before.load(std::memory_order_seq_cst);d.after_generation=slot.after.load(std::memory_order_seq_cst);
  for(size_t i=0;i<4;++i)d.args.r3_r6[i]=slot.args[i].load(std::memory_order_seq_cst);
  if(slot.sequence.load(std::memory_order_seq_cst)==expected)result.events[result.count++]=d;
 }
 return result;
}
bool configure(Inspect inspect, Live live,Saved saved) noexcept {
    if (!inspect || !live || !saved) return false;
    Inspect absent=nullptr;
    if (!inspector.compare_exchange_strong(absent,inspect,
            std::memory_order_release,std::memory_order_relaxed)) return false;
    saved_inspector.store(saved,std::memory_order_release);
    live_inspector.store(live,std::memory_order_release);
    return true;
}
const Observer* observer() noexcept {
    return live_inspector.load(std::memory_order_acquire) ? &callbacks : nullptr;
}
story_native_stage_owner::Tracker& tracker() noexcept { return owner; }
std::optional<Snapshot> snapshot() noexcept {
    std::lock_guard lock(owner.lifetime_fence());
    if (!active_control || !active_receipt.generation ||
        !owner.current(active_receipt)) return std::nullopt;
    return Snapshot{active_control,active_receipt};
}
std::optional<Snapshot> snapshot(const story_native_stage_owner::Tracker::Scan&scan)noexcept{
    if(!scan.holds_lifetime_of(owner)||!scan.current()||!active_control||!active_receipt.generation||
       scan.receipt()!=active_receipt)return {};
    return Snapshot{active_control,active_receipt};
}
bool matches_saved(const Snapshot&known,const ss::native_gate::SavedView&view)noexcept{
    try{const Saved saved=saved_inspector.load(std::memory_order_acquire);Candidate captured{};
        return known.control&&known.receipt.generation&&saved&&
            saved(view,known.control,known.receipt.owner,captured)&&captured==known.receipt.owner;
    }catch(...){return false;}
}
std::optional<RestorePermit> prepare_restore(const Snapshot&known,const ss::native_gate::SavedView&view){
    try{
        auto scan=owner.scan();const Live live=live_inspector.load(std::memory_order_acquire);
        const Saved saved=saved_inspector.load(std::memory_order_acquire);
        if(!scan.valid()||!known.control||active_control!=known.control||known.receipt!=active_receipt||
           scan.receipt()!=known.receipt||!live||!saved)return {};
        Candidate before{},captured{},after{};
        if(!live(known.control,known.receipt.owner,before)||before!=known.receipt.owner||
           !saved(view,known.control,known.receipt.owner,captured)||captured!=known.receipt.owner||
           !live(known.control,known.receipt.owner,after)||after!=before||!scan.current())return {};
        auto permit=owner.prepare_restore(std::move(scan));if(!permit)return {};
        return RestorePermit(std::move(*permit),known.control,captured);
    }catch(...){return {};}
}
std::optional<RestorePermit> prepare_restore_same_resource(const Snapshot&known,const ss::native_gate::SavedView&view){
    try{
        auto scan=owner.try_scan();const Live live=live_inspector.load(std::memory_order_acquire);
        const Saved saved=saved_inspector.load(std::memory_order_acquire);
        if(!scan.current()||!known.control||!known.receipt.generation||active_control!=known.control||
           !active_receipt.generation||known.receipt.generation>active_receipt.generation||scan.receipt()!=active_receipt||
           known.receipt.owner!=active_receipt.owner||!live||!saved)return {};
        const auto current=active_receipt;Candidate before{},captured{},after{};
        if(!live(known.control,current.owner,before)||before!=current.owner||
           !saved(view,known.control,known.receipt.owner,captured)||captured!=known.receipt.owner||
           !live(known.control,current.owner,after)||after!=before||!scan.current()||
           active_control!=known.control||active_receipt!=current)return {};
        auto permit=owner.prepare_restore(std::move(scan));if(!permit)return {};
        return RestorePermit(std::move(*permit),known.control,captured);
    }catch(...){return {};}
}
std::optional<Snapshot> finish_restore(RestorePermit&&permit){
    if(!permit.native_.same_thread()||permit.consumed_)return {};permit.consumed_=true;
    std::lock_guard lock(owner.lifetime_fence());
    const auto control=permit.control_;const auto expected=permit.candidate_;
    const auto restored=owner.finish_restore(std::move(permit.native_),[&]()->std::optional<story_native_stage_owner::Receipt>{
        const Live live=live_inspector.load(std::memory_order_acquire);Candidate first{},second{};
        if(!live||!live(control,expected,first)||first!=expected||
           !live(control,expected,second)||second!=first)return {};
        return story_native_stage_owner::Receipt{first,0};
    });
    if(!restored)return {};
    // The native epoch/restore epoch were checked under the retained original
    // Scan lock. Install the new graph before that lifetime fence is released.
    if(!owner.current(*restored)){discard();owner.fault();return {};}
    active_control=historical_control=control;active_receipt=*restored;historical_candidate=expected;
    return Snapshot{control,*restored};
}
void state_loaded() noexcept {
    std::lock_guard lock(owner.lifetime_fence());
    const auto before=active_receipt.generation;discard(); owner.state_loaded();
    note(0,{},0,MutationPhase::Restore,before,0);
}
} // namespace story_native_stage_owner_observer
