// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_stage_owner_observer.h"
#include <cassert>
#include <stdexcept>
#include <thread>
#include <atomic>
#include "savestate_native_gate.h"
#include <cstdint>
#include <utility>

namespace mem {uint32_t runtime_top(){return 0x68000000;}}
using namespace story_native_stage_owner_observer;
namespace {
Candidate candidate() {
    Candidate c;
    c.info=0x60810000; c.archive_wrapper=0x60811000;
    c.archive_holder=0x60812000; c.archive_resource=0x60818000;
    c.archive_base=0x6081F000; c.archive_bytes=0x3000;
    c.outer_base=0x60820000; c.outer_bytes=0x1000;
    c.resource=0x60820240; c.resource_bytes=0x80;
    c.entry=0x60820118; c.archive_key=0x1004DF68;
    c.resource_key=0x1004DF70;
    c.request_heap=c.pack_heap=0x60830000;
    c.data_heap=0x60831000; c.data_heap_parent=0x60832000;
    c.stage_name[0]='s';
    return c;
}
bool inspect_ok=true, live_ok=true, live_mismatch=false, throw_inspect=false,saved_ok=true,saved_mutate=false,saved_mismatch=false;
unsigned inspect_calls=0,live_calls=0,saved_calls=0,live_mismatch_after=0;
story_native_setinfo_observation::Result inspect(
    const Cpu*,const Args&,uint32_t result,Candidate& out) {
    ++inspect_calls;
    if(throw_inspect) throw std::runtime_error("inspect fault");
    if(!inspect_ok || result!=1)return story_native_setinfo_observation::Result::Candidate;
    out=candidate();return story_native_setinfo_observation::Result::Valid;
}
bool live(uint32_t,const Candidate& expected,Candidate& out) {
    ++live_calls;if(!live_ok)return false;
    out=expected;
    if(live_mismatch||(live_mismatch_after&&live_calls>live_mismatch_after))++out.resource;
    return true;
}
// Synthetic source isolates lifetime sequencing. The real SavedView reader
// and complete native resource graph are tested in setinfo_saved_test.
bool saved(const ss::native_gate::SavedView&,uint32_t control,const Candidate&expected,Candidate&out){
 ++saved_calls;out={};if(!saved_ok||control!=0x60700000)return false;
 if(saved_mutate){auto cookie=tracker().begin_native();tracker().finish_native(cookie);}
 out=expected;if(saved_mismatch)++out.resource;return true;
}
uint64_t enter_set(const Observer* o,const Args& a) {
    std::lock_guard lock(*o->lifetime_fence);
    return o->before_setinfo(nullptr,a);
}
void finish_set(const Observer* o,const Args& a,uint64_t cookie) {
    std::lock_guard lock(*o->lifetime_fence);
    o->after_setinfo(nullptr,a,1,cookie);
}
void issue(const Observer* o,const Args& a) {
    const auto cookie=enter_set(o,a);
    assert(cookie);
    assert(!snapshot());
    finish_set(o,a,cookie);
    assert(snapshot());
}
}
int main() {
    assert(!observer());
    assert(!configure(nullptr,live));
    assert(configure(inspect,live,saved));
    assert(!configure(inspect,live));
    const Observer* o=observer();
    assert(o && o->lifetime_fence==&tracker().lifetime_fence());
    const Args args{{0x60700000,0x60700010,0x60700020,0}};
    issue(o,args);
    {const auto history=mutation_history();assert(history.count==2);
     const auto&begin=history.events[0];const auto&end=history.events[1];
     assert(begin.phase==MutationPhase::Begin&&end.phase==MutationPhase::End&&begin.cookie==end.cookie&&begin.cookie);
     assert(begin.canonical==story_native_stage_owner_hooks::SetInfo()&&end.canonical==begin.canonical&&
      begin.args.r3_r6==args.r3_r6&&end.args.r3_r6==args.r3_r6&&begin.after_generation==0&&end.after_generation==snapshot()->receipt.generation);
    }
    auto first=snapshot();
    assert(first->control==args.r3_r6[0]);
    assert(first->receipt.owner==candidate());
    assert(first->receipt.generation);
    {auto pin=tracker().try_scan();assert(pin.current());const auto observed=snapshot(pin);assert(observed&&observed->control==first->control&&observed->receipt==first->receipt);
     story_native_stage_owner::Tracker alien;auto wrong=alien.try_scan();assert(!snapshot(wrong));
    }
    {std::atomic<bool>locked=false,release=false;std::thread holder([&]{auto pin=tracker().scan();assert(pin.current());locked=true;while(!release.load())std::this_thread::yield();});while(!locked.load())std::this_thread::yield();
     auto refused=tracker().try_scan();assert(!refused.current()&&!snapshot(refused));assert(mutation_history().count);release=true;holder.join();}


    // Unrelated generic delete leaves the receipt in place.
    Args free_args{{0x60840000,0,0,0}};
    {
        std::lock_guard lock(*o->lifetime_fence);
        auto cookie=o->before_free(nullptr,free_args);
        assert(!cookie);
        o->after_free(nullptr,free_args,cookie);
    }
    assert(snapshot());
    // An interior pointer anywhere in the observed backing span revokes
    // before the actual native delete body begins.
    free_args.r3_r6[0]=candidate().resource+4;
    {
        std::lock_guard lock(*o->lifetime_fence);
        auto cookie=o->before_free(nullptr,free_args);
        assert(cookie && !snapshot());
        live_ok=false; // the native tree no longer owns this backing
        o->after_free(nullptr,free_args,cookie);
        live_ok=true;
    }
    assert(!snapshot());
    for (uint32_t ptr : {candidate().archive_holder,candidate().archive_resource,
                         candidate().request_heap,candidate().data_heap,
                         candidate().archive_base+8,candidate().outer_base+8}) {
        issue(o,args);
        free_args.r3_r6[0]=ptr;
        std::lock_guard lock(*o->lifetime_fence);
        const auto cookie=o->before_free(nullptr,free_args);
        assert(cookie && !snapshot());
        live_ok=false;
        o->after_free(nullptr,free_args,cookie);
        live_ok=true;
        assert(!snapshot());
    }
    issue(o,args);
    for(uint32_t function : {0x0275558Cu,0x02613580u}) {
        std::lock_guard lock(*o->lifetime_fence);
        const auto cookie=o->before_destroy(nullptr,function,Args{{0x12345678,0,0,0}});
        assert(cookie && !snapshot());
        o->after_destroy(nullptr,function,{},cookie);
        assert(snapshot()); // unrelated native teardown was revalidated
    }
    {
        const auto set_cookie=enter_set(o,args);
        uint64_t destroy_cookie{};
        {
            std::lock_guard lock(*o->lifetime_fence);
            destroy_cookie=o->before_destroy(nullptr,0x0275558Cu,{});
        }
        finish_set(o,args,set_cookie);
        assert(!snapshot()); // another original body remains in flight
        {
            std::lock_guard lock(*o->lifetime_fence);
            o->after_destroy(nullptr,0x0275558Cu,{},destroy_cookie);
        }
        assert(snapshot()); // final exit rechecked the historical Stage
    }
    {
        std::lock_guard lock(*o->lifetime_fence);
        const auto cookie=o->before_destroy(nullptr,0x0260757Cu,{});
        assert(cookie && !snapshot());
        live_ok=false; // actual Stage Info tree deletion
        o->after_destroy(nullptr,0x0260757Cu,{},cookie);
        live_ok=true;
    }
    assert(!snapshot());
    // A failed live check discards historical identity. It cannot revive
    // merely because the old memory later happens to be readable again.
    inspect_ok=false;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot());
    inspect_ok=true;
    issue(o,args);
    inspect_ok=false;
    finish_set(o,args,enter_set(o,args));
    assert(snapshot()); // unrelated SetInfo retained verified Stage identity
    live_mismatch=true;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot()); // tree identity changed; history was discarded
    live_mismatch=false;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot()); // old candidate cannot regain authority
    inspect_ok=true;
    state_loaded();
    assert(!snapshot());
    // A state restore while SetInfo is in flight prevents its post callback
    // from issuing even if the structural candidate still verifies.
    const auto pending=enter_set(o,args);
    state_loaded();
    finish_set(o,args,pending);
    assert(!snapshot());
    inspect_ok=false;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot());
    inspect_ok=true;
    live_ok=false;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot());
    live_ok=true;
    issue(o,args);
    {
        const auto cookie=enter_set(o,args);
        std::lock_guard lock(*o->lifetime_fence);
        o->failed_setinfo(nullptr,args,cookie);
        assert(snapshot()); // unrelated failed SetInfo, live Stage still exact
    }
    // Stage-only restore is a continuation of this actual observed owner,
    // never a copied Snapshot or a fake native SetInfo callback.
    const ss::MemoryChunks empty_chunks;const ss::native_gate::SavedView view{{},{},empty_chunks};
    issue(o,args);auto known=*snapshot();
    const auto live_before_saved=live_calls;
    assert(matches_saved(known,view)&&live_calls==live_before_saved);
    live_mismatch=true;assert(matches_saved(known,view)&&live_calls==live_before_saved);live_mismatch=false;
    auto malformed=known;malformed.control=0;assert(!matches_saved(malformed,view));
    malformed=known;malformed.receipt.generation=0;assert(!matches_saved(malformed,view));
    saved_ok=false;assert(!matches_saved(known,view));saved_ok=true;
    saved_mismatch=true;assert(!matches_saved(known,view));saved_mismatch=false;
    auto wrong=known;++wrong.control;assert(!prepare_restore(wrong,view));
    wrong=known;++wrong.receipt.generation;assert(!prepare_restore(wrong,view));
    wrong=known;++wrong.receipt.owner.resource;assert(!prepare_restore(wrong,view));
    saved_ok=false;assert(!prepare_restore(known,view));saved_ok=true;
    saved_mutate=true;assert(!prepare_restore(known,view));saved_mutate=false;assert(!snapshot());
    issue(o,args);known=*snapshot();const auto inspected=inspect_calls;
    auto continuation=prepare_restore(known,view);assert(continuation);state_loaded();assert(!snapshot());
    std::thread foreign_finish([&]{assert(!finish_restore(std::move(*continuation)));});foreign_finish.join();
    auto restored=finish_restore(std::move(*continuation));assert(restored&&restored->control==known.control&&restored->receipt.owner==known.receipt.owner&&restored->receipt.generation>known.receipt.generation&&inspect_calls==inspected);
    assert(!finish_restore(std::move(*continuation))&&snapshot()->receipt==restored->receipt);
    assert(!prepare_restore(known,view)); // stale/cross-epoch host snapshot is data only
    // Separate same-resource continuation authenticates the CURRENT Tracker
    // epoch while preserving the exact saved graph. The ordinary API above
    // still rejects old generations.
    wrong=known;++wrong.control;assert(!prepare_restore_same_resource(wrong,view));
    wrong=known;++wrong.receipt.owner.resource;assert(!prepare_restore_same_resource(wrong,view));
    wrong=known;wrong.receipt.generation=0;assert(!prepare_restore_same_resource(wrong,view));
    wrong=known;wrong.receipt.generation=snapshot()->receipt.generation+1;assert(!prepare_restore_same_resource(wrong,view));
    saved_ok=false;assert(!prepare_restore_same_resource(known,view));saved_ok=true;
    live_mismatch_after=live_calls+1;assert(!prepare_restore_same_resource(known,view));live_mismatch_after=0;
    auto historical=prepare_restore_same_resource(known,view);assert(historical);state_loaded();
    auto renewed=finish_restore(std::move(*historical));assert(renewed&&renewed->receipt.owner==known.receipt.owner&&renewed->receipt.generation>restored->receipt.generation);
    assert(!finish_restore(std::move(*historical)));
    for(unsigned fault=0;fault<3;++fault){
        auto p=prepare_restore_same_resource(known,view);assert(p);
        if(fault!=0)state_loaded();if(fault==1)state_loaded();
        if(fault==2){auto native=tracker().begin_native();tracker().finish_native(native);}
        assert(!finish_restore(std::move(*p)));assert(!finish_restore(std::move(*p)));
        issue(o,args);
    }
    // Rebuilt historical graph protects an interior free before its body.
    free_args.r3_r6[0]=candidate().resource+4;
    {std::lock_guard lock(*o->lifetime_fence);const auto cookie=o->before_free(nullptr,free_args);assert(cookie&&!snapshot());live_ok=false;o->after_free(nullptr,free_args,cookie);live_ok=true;}
    assert(!snapshot());
    for(unsigned fault=0;fault<5;++fault){
        issue(o,args);known=*snapshot();auto permit=prepare_restore(known,view);assert(permit);
        if(fault!=0)state_loaded();if(fault==1)state_loaded();
        if(fault==2){auto cookie=tracker().begin_native();tracker().finish_native(cookie);}
        if(fault==3)live_mismatch=true;
        if(fault==4)live_mismatch_after=live_calls+1;
        assert(!finish_restore(std::move(*permit)));live_mismatch=false;live_mismatch_after=0;
        assert(!finish_restore(std::move(*permit)));
        if(fault==0)assert(snapshot()&&snapshot()->receipt==known.receipt);else assert(!snapshot());
    }
    issue(o,args);
    // Diagnostics wrap at a fixed16 records and remain nonblocking while a
    // different thread owns the real Stage lifetime mutex. Concurrent samples
    // are complete records or omitted, never reconstructed mixed arguments.
    {for(unsigned n=0;n<10;++n)issue(o,args);std::atomic<bool>done=false;std::thread writer([&]{for(unsigned n=0;n<40;++n)issue(o,args);done=true;});
     do{const auto history=mutation_history();assert(history.count<=16);uint64_t last=0;
      for(size_t n=0;n<history.count;++n){const auto&e=history.events[n];assert(e.sequence>last);last=e.sequence;
       assert(e.canonical==story_native_stage_owner_hooks::SetInfo()&&e.args.r3_r6==args.r3_r6&&e.cookie&&
        (e.phase==MutationPhase::Begin||e.phase==MutationPhase::End));}
     }while(!done.load());writer.join();const auto history=mutation_history();assert(history.count==16);
     assert(history.events[15].phase==MutationPhase::End&&history.events[15].after_generation==snapshot()->receipt.generation);
    }
    // A verifier exception permanently quarantines issuance.
    throw_inspect=true;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot());
    throw_inspect=false;
    finish_set(o,args,enter_set(o,args));
    assert(!snapshot());
}
