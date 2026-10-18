// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_stage_owner.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <stdexcept>
#include <cstdlib>
#include <exception>
#include <optional>
#include <utility>
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <optional>
#include <utility>
#endif
using namespace story_native_stage_owner;
namespace {
Receipt candidate() {
    Receipt r;
    auto& o = r.owner;
    o.info=0x60810000;
    o.archive_wrapper=0x60811000;
    o.archive_holder=0x60812000;
    o.archive_resource=0x60818000;
    o.archive_base=0x6081F000;
    o.archive_bytes=0x3000;
    o.outer_base=0x60820000;
    o.outer_bytes=0x1000;
    o.resource=0x60820240;
    o.resource_bytes=0x80;
    o.entry=0x60820118;
    o.archive_key=0x1004DF68;
    o.resource_key=0x1004DF70;
    o.request_heap=o.pack_heap=0x60830000;
    o.data_heap=0x60831000;
    o.data_heap_parent=0x60832000;
    o.stage_name[0]='s';
    return r;
}
}
int main() {
    assert(!current_thread_holds_lifetime_pin());
    {Tracker counted;auto first=counted.scan();assert(current_thread_holds_lifetime_pin());
     {auto second=counted.scan();auto moved=std::move(second);assert(current_thread_holds_lifetime_pin());first=std::move(moved);assert(current_thread_holds_lifetime_pin());}
     bool foreign=true;std::thread([&]{foreign=current_thread_holds_lifetime_pin();}).join();assert(!foreign&&current_thread_holds_lifetime_pin());
    }
    assert(!current_thread_holds_lifetime_pin());
    Tracker t;
    assert(!t.scan().valid());
    auto c = candidate();
    {Tracker busy;assert(busy.finish_setinfo(busy.begin_native(),[&]{return std::optional<Receipt>(c);}));
     std::atomic<bool> locked=false,release=false;std::thread holder([&]{auto guard=busy.scan();assert(guard.current());locked=true;while(!release.load())std::this_thread::yield();});
     while(!locked.load())std::this_thread::yield();
     auto refused=busy.try_scan();assert(!refused.valid()&&!refused.current()&&!refused.holds_lifetime_of(busy)&&!current_thread_holds_lifetime_pin());
     release=true;holder.join();auto guard=busy.try_scan();assert(guard.current()&&guard.holds_lifetime_of(busy));
     auto nested=busy.try_scan();assert(nested.current()&&current_thread_holds_lifetime_pin());
    }
    assert(!current_thread_holds_lifetime_pin());

    auto bad = c; bad.owner.resource_bytes = 0x2000;
    assert(!t.finish_setinfo(t.begin_native(),[&]{return std::optional<Receipt>(bad);}));
    bad=c; bad.owner.pack_heap++;
    assert(!t.finish_setinfo(t.begin_native(),[&]{return std::optional<Receipt>(bad);}));
    bad=c;bad.owner.data_heap_parent=0;
    assert(!t.finish_setinfo(t.begin_native(),[&]{return std::optional<Receipt>(bad);}));
    assert(t.finish_setinfo(t.begin_native(),[&]{return std::optional<Receipt>(c);}));
    std::atomic<bool> started{false}, finished{false};
    std::thread unload;
    {
        auto first = t.scan();
        assert(first.valid() && first.receipt().generation != 0);
        assert(first.current());
        unload = std::thread([&] {
            started.store(true);
            const auto cookie=t.begin_native();
            finished.store(true);
            assert(!t.scan().valid()); // body is active, even after pre-revoke
            t.finish_native(cookie);
        });
        while (!started.load()) std::this_thread::yield();
        // The worker has entered its observer, but its pre-revoke cannot
        // finish before the scan releases the fence.
        assert(first.current());
        assert(!finished.load());
    }
    unload.join();
    assert(finished.load() && !t.scan().valid());
    assert(t.finish_setinfo(t.begin_native(),[&]{return std::optional<Receipt>(c);}));
    {
        auto second = t.scan();
        assert(second.valid() && second.receipt().generation > 1);
        assert(second.current());
    }
    assert(!t.current(Receipt{c}));
    t.state_loaded();
    assert(!t.scan().valid());
    assert(t.finish_setinfo(t.begin_native(),[&]{return std::optional<Receipt>(c);}));
    {
        auto third = t.scan();
        assert(third.valid() && third.receipt().generation > 2);
    }
    t.finish_native(t.begin_native()); // uncertain native unload revokes fail-closed
    assert(!t.scan().valid());
    // The hook holds the fence only over callbacks. Nested callbacks may
    // re-enter Tracker operations under its recursive lock.
    {
        std::unique_lock<std::recursive_mutex> native(t.lifetime_fence());
        const auto cookie=t.begin_native();
        assert(!t.scan().valid());
        assert(t.finish_setinfo(cookie,[&]{return std::optional<Receipt>(c);}));
        auto nested=t.scan();
        assert(nested.valid()&&nested.current());
    }
    // SetInfo calls resource constructors and a heap resize before returning.
    // Model a nested native destruction hook during one of those callees. Once
    // it finishes, the final graph is verified under the exit fence.
    Tracker nested;
    auto outer=nested.begin_native();
    auto inner=nested.begin_native();
    nested.finish_native(inner);
    assert(nested.finish_setinfo(outer,[&]{return std::optional<Receipt>(c);}));
    assert(nested.scan().valid());
    // Concurrent teardown still in flight at the post-SetInfo observation
    // prevents publication, even if a verifier could read a candidate.
    Tracker overlap;
    auto first=overlap.begin_native();
    auto destroy=overlap.begin_native();
    bool verified=false;
    assert(!overlap.finish_setinfo(first,[&]{verified=true;return std::optional<Receipt>(c);}));
    assert(!verified);
    assert(!overlap.scan().valid());
    overlap.finish_native(destroy);
    assert(!overlap.scan().valid());
    Tracker restored;
    auto pending=restored.begin_native();
    restored.state_loaded();
    assert(!restored.finish_setinfo(pending,[&]{return std::optional<Receipt>(c);}));
    assert(!restored.scan().valid());
    // A verifier exception and an unmatched exit permanently quarantine.
    Tracker thrown;
    assert(!thrown.finish_setinfo(thrown.begin_native(),[]{
        throw 1;return std::optional<Receipt>{};}));
    assert(!thrown.finish_setinfo(thrown.begin_native(),[&]{return std::optional<Receipt>(c);}));
    Tracker unmatched;
    unmatched.finish_native(123);
    assert(!unmatched.finish_setinfo(unmatched.begin_native(),[&]{return std::optional<Receipt>(c);}));
    // A retained actual Scan is required; copied receipts cannot construct a
    // continuation. Restore is one epoch, one attempt and no native replay.
    for(unsigned fault=0;fault<8;++fault){
        Tracker issuer;assert(issuer.finish_setinfo(issuer.begin_native(),[&]{return std::optional<Receipt>(c);}));
        const auto before=issuer.scan().receipt();auto permit=issuer.prepare_restore(issuer.scan());assert(permit&&current_thread_holds_lifetime_pin());
        if(fault!=1)issuer.state_loaded();
        if(fault==2)issuer.state_loaded();
        if(fault==3)issuer.finish_native(issuer.begin_native());
        if(fault==4)issuer.fault();
        auto verifier=[&]()->std::optional<Receipt>{
            assert(current_thread_holds_lifetime_pin());
            if(fault==5){auto changed=c;++changed.owner.request_heap;return changed;}
            if(fault==6)throw std::runtime_error("restored inspection");
            if(fault==7)issuer.state_loaded();
            return c;
        };
        auto moved_permit=std::move(*permit);assert(current_thread_holds_lifetime_pin());
        auto restored=issuer.finish_restore(std::move(moved_permit),verifier);assert(!current_thread_holds_lifetime_pin());
        if(!fault){assert(restored&&restored->owner==c.owner&&restored->generation>before.generation&&issuer.current(*restored));}
        else if(fault==1)assert(!restored&&issuer.current(before));
        else assert(!restored&&!issuer.scan().valid());
        assert(!issuer.finish_restore(std::move(*permit),[&]{return std::optional<Receipt>(c);}));
    }
    Tracker issuer,wrong;assert(issuer.finish_setinfo(issuer.begin_native(),[&]{return std::optional<Receipt>(c);}));
    auto foreign_permit=issuer.prepare_restore(issuer.scan());assert(foreign_permit);issuer.state_loaded();
    assert(!wrong.finish_restore(std::move(*foreign_permit),[&]{return std::optional<Receipt>(c);}));
    assert(!issuer.finish_restore(std::move(*foreign_permit),[&]{return std::optional<Receipt>(c);}));
    Tracker active_body;auto body=active_body.begin_native();assert(!active_body.prepare_restore(active_body.scan()));active_body.finish_native(body);

    Tracker fenced;assert(fenced.finish_setinfo(fenced.begin_native(),[&]{return std::optional<Receipt>(c);}));
    auto held_restore=fenced.prepare_restore(fenced.scan());assert(held_restore);
    started=false;finished=false;
    std::thread waiting([&]{started=true;auto cookie=fenced.begin_native();finished=true;fenced.finish_native(cookie);});
    while(!started.load())std::this_thread::yield();
    assert(!finished.load());fenced.state_loaded();assert(!finished.load());
    assert(fenced.finish_restore(std::move(*held_restore),[&]{assert(!finished.load());return std::optional<Receipt>(c);}));
    waiting.join();assert(finished.load()&&!fenced.scan().valid());

    Tracker same_thread;assert(same_thread.finish_setinfo(same_thread.begin_native(),[&]{return std::optional<Receipt>(c);}));
    auto affine_scan=same_thread.scan();
    std::thread foreign_prepare([&]{assert(!affine_scan.valid()&&!affine_scan.current()&&!same_thread.prepare_restore(std::move(affine_scan)));});
    foreign_prepare.join();assert(affine_scan.current());
    auto affine=same_thread.prepare_restore(std::move(affine_scan));assert(affine);same_thread.state_loaded();
    unsigned restore_reads=0;
    std::thread foreign_finish([&]{assert(!same_thread.finish_restore(std::move(*affine),[&]{++restore_reads;return std::optional<Receipt>(c);}));});
    foreign_finish.join();assert(!restore_reads);
    assert(same_thread.finish_restore(std::move(*affine),[&]{++restore_reads;return std::optional<Receipt>(c);})&&restore_reads==1);
#ifndef _WIN32
    // Fail fast before unique_lock could unlock another thread's recursive
    // mutex. Separate child keeps this deliberate contract violation isolated.
    const pid_t child=fork();assert(child>=0);
    if(!child){
        std::set_terminate([]{std::_Exit(83);});
        Tracker owner;assert(owner.finish_setinfo(owner.begin_native(),[&]{return std::optional<Receipt>(c);}));
        auto token=owner.prepare_restore(owner.scan());assert(token);
        auto*wrong_thread_delete=new Tracker::RestorePermit(std::move(*token));
        std::thread doomed([&]{delete wrong_thread_delete;});doomed.join();std::_Exit(84);
    }
    int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==83);
    const pid_t scan_child=fork();assert(scan_child>=0);
    if(!scan_child){
        std::set_terminate([]{std::_Exit(83);});Tracker owner;
        auto*wrong_thread_scan=new Tracker::Scan(owner.scan());
        std::thread doomed([&]{delete wrong_thread_scan;});doomed.join();std::_Exit(84);
    }
    assert(waitpid(scan_child,&status,0)==scan_child&&WIFEXITED(status)&&WEXITSTATUS(status)==83);

#endif

}
