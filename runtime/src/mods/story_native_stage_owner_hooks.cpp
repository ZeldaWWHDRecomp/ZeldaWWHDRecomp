// SPDX-License-Identifier: MPL-2.0
#include "story_native_stage_owner_hooks.h"
#include "guest_addr.h"
#include "runtime.h"
#include <atomic>

namespace story_native_stage_owner_hooks { namespace {
std::atomic<const Observer*> observer{nullptr};
struct DispatchFrame {const Cpu* cpu{};uint32_t canonical{};unsigned delegated{};DispatchFrame* prior{};};
thread_local DispatchFrame* frame=nullptr;
Args arguments(const Cpu* c) {
    return {{c->r[3], c->r[4], c->r[5], c->r[6]}};
}
template<class Fn> bool preserve(Cpu* c, Fn&& fn) noexcept {
    const Cpu saved = *c;
    try {fn();}
    catch (...) {*c = saved;return false;}
    *c = saved;
    return true;
}
} // namespace
bool register_observer(const Observer* candidate) noexcept {
    if (!candidate || !candidate->before_setinfo || !candidate->before_destroy ||
        !candidate->after_setinfo ||
        !candidate->failed_setinfo || !candidate->after_destroy ||
        !candidate->fault || (candidate->before_free && !candidate->after_free) ||
        (!candidate->before_free && candidate->after_free)) return false;
    const Observer* empty = nullptr;
    return observer.compare_exchange_strong(empty,candidate,std::memory_order_release,
                                            std::memory_order_relaxed);
}
bool observed(uint32_t canonical) noexcept {
    return canonical && (canonical==SetInfo() || canonical==DeleteInfo() ||
           canonical==FreePackEntry() || canonical==UnloadArchive() ||
           canonical==ManagerDisposer() || canonical==ArchiveHolderDtor() ||
           canonical==PackHolderDtor() || canonical==StageHeapDtor() ||
           canonical==ArchiveResourceDtor() ||
           canonical==SarcResourceDtor() || canonical==ArrayDelete());
}
void invoke_dispatch(Cpu* c, uint32_t canonical, const std::function<void()>& body) {
    if (!body) return;
    if (!c || !observed(canonical)) {body();return;}
    DispatchFrame current{c,canonical,0,frame};frame=&current;
    try {body();}
    catch (...) {frame=current.prior;throw;}
    frame=current.prior;
}
void invoke_original(Cpu* c, uint32_t canonical, const std::function<void()>& body) {
    if (!body) return;
    const Observer* o=observer.load(std::memory_order_acquire);
    if (!c || !o || !observed(canonical)) {body();return;}
    // The dispatcher itself performs no owner observation. A delegated
    // call_original is the one native body boundary; if it nests another real
    // original, that second native execution is observed separately.
    for (auto* f=frame;f;f=f->prior) if(f->cpu==c && f->canonical==canonical) {++f->delegated;break;}
    const Args args=arguments(c);
    const auto under_fence=[&](const auto& callback) noexcept {
        try {
            std::unique_lock<std::recursive_mutex> lifetime;
            if(o->lifetime_fence)
                lifetime=std::unique_lock<std::recursive_mutex>(*o->lifetime_fence);
            if(!preserve(c,callback)){o->fault();return false;}
            return true;
        }catch(...){o->fault();return false;}
    };
    uint64_t cookie=0;
    if(canonical==SetInfo()){
        under_fence([&]{cookie=o->before_setinfo(c,args);});
    }else if(canonical==ArrayDelete()){
        if(o->before_free)under_fence([&]{cookie=o->before_free(c,args);});
    }else under_fence([&]{cookie=o->before_destroy(c,canonical,args);});
    const auto after=[&](bool success) noexcept {
        if(canonical==SetInfo()){
            const uint32_t result=c->r[3];
            under_fence([&]{
                if(success && result)o->after_setinfo(c,args,result,cookie);
                else o->failed_setinfo(c,args,cookie);
            });
        }else if(canonical==ArrayDelete()){
            if(o->after_free)under_fence([&]{o->after_free(c,args,cookie);});
        }else under_fence([&]{o->after_destroy(c,canonical,args,cookie);});
    };
    try {body();}
    catch (...) {
        // A partially executed original may have mutated or freed the owner.
        // Quarantine while holding the same entry/exit fence before a post
        // callback can revalidate and issue any historical receipt.
        under_fence([&]{o->fault();});
        after(false);
        throw;
    }
    after(true);
}
} // namespace story_native_stage_owner_hooks
