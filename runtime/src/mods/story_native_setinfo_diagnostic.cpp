// SPDX-License-Identifier: MPL-2.0
#include "story_native_setinfo_diagnostic.h"
#include "runtime.h"
#include <array>
#include <atomic>
#include <mutex>

namespace story_native_setinfo_diagnostic { namespace {
std::atomic<Inspect> inspector{nullptr};
std::atomic<LiveInspect> live_inspector{nullptr};
std::atomic<DeleteMatch> delete_matcher{nullptr};
std::atomic<uint32_t> setinfo{0},setinfo_entered{0},valid{0},candidate_invalid{0};
std::atomic<uint32_t> manager_invalid{0},layout_invalid{0},return_invalid{0};
std::atomic<uint32_t> failed{0},teardown{0},faults{0};
std::atomic<uint32_t> exact_matches{0},live_matches{0};
std::atomic<uint32_t> raw_frees{0},exact_raw_free_matches{0},live_at_raw_free{0};
struct Saved {uint32_t control{};uint32_t serial{};uint32_t seen{};
              story_native_setinfo_candidate::Candidate candidate{};};
std::mutex saved_mutex;
std::array<Saved,8> saved{};
uint32_t next_saved=0;
uint64_t before_setinfo(Cpu*,const story_native_stage_owner_hooks::Args&) {
    setinfo_entered.fetch_add(1,std::memory_order_relaxed);
    return 0;
}

uint64_t before_destroy(Cpu*,uint32_t fn,const story_native_stage_owner_hooks::Args& args) {
    teardown.fetch_add(1,std::memory_order_relaxed);
    const uint32_t object=args.r3_r6[0];
    if(!object)return 0;
    struct Match {Saved saved;const char* kind;};
    std::array<Match,8> matches{};
    unsigned count=0;
    const DeleteMatch deletion=delete_matcher.load(std::memory_order_acquire);
    {
        std::lock_guard lock(saved_mutex);
        for(auto& entry:saved){
            if(!entry.serial)continue;
            const auto& c=entry.candidate;
            const char* kind=nullptr;
            unsigned bit=0;
            if(fn==story_native_stage_owner_hooks::DeleteInfo()&&
               deletion&&deletion(args,entry.control,c)){
                kind="delete_info";bit=32;
            }else if(fn==story_native_stage_owner_hooks::StageHeapDtor()){
                if(object==c.request_heap){kind="request_heap";bit=1;}
                else if(object==c.data_heap){kind="data_heap";bit=2;}
            }else if(fn==story_native_stage_owner_hooks::ArchiveHolderDtor()&&
                     object==c.archive_holder){kind="holder";bit=4;}
            else if(fn==story_native_stage_owner_hooks::ArchiveResourceDtor()&&
                    object==c.archive_resource){kind="archive_resource";bit=8;}
            else if(fn==story_native_stage_owner_hooks::SarcResourceDtor()&&
                    object==c.archive_resource){kind="sarc_resource";bit=16;}
            if(kind&&!(entry.seen&bit)){
                entry.seen|=bit;
                matches[count++]={entry,kind};
            }
        }
    }
    const LiveInspect live=live_inspector.load(std::memory_order_acquire);
    for(unsigned i=0;i<count;++i){
        story_native_setinfo_candidate::Candidate again{};
        const bool current=live&&live(matches[i].saved.control,
                                       matches[i].saved.candidate,again);
        exact_matches.fetch_add(1,std::memory_order_relaxed);
        if(current)live_matches.fetch_add(1,std::memory_order_relaxed);
        LOG("[native_setinfo_probe] teardown=%s serial=%u ptr=%08X info=%08X holder=%08X resource=%08X live=%u",
            matches[i].kind,matches[i].saved.serial,object,
            matches[i].saved.candidate.info,matches[i].saved.candidate.archive_holder,
            matches[i].saved.candidate.archive_resource,unsigned(current));
    }
    return 0;
}
uint64_t before_free(Cpu*,const story_native_stage_owner_hooks::Args& args) {
    raw_frees.fetch_add(1,std::memory_order_relaxed);
    const uint32_t object=args.r3_r6[0];
    if(!object)return 0;
    struct Match {Saved saved;const char* kind;};
    std::array<Match,8> matches{};
    unsigned count=0;
    {
        std::lock_guard lock(saved_mutex);
        for(const auto& entry:saved){
            if(!entry.serial)continue;
            const auto& c=entry.candidate;
            const char* kind=object==c.archive_base?"archive_base":
                object==c.outer_base?"outer_base":
                object==c.resource?"event_list":nullptr;
            if(kind)matches[count++]={entry,kind};
        }
    }
    const LiveInspect live=live_inspector.load(std::memory_order_acquire);
    for(unsigned i=0;i<count;++i){
        story_native_setinfo_candidate::Candidate again{};
        const bool current=live&&live(matches[i].saved.control,
                                       matches[i].saved.candidate,again);
        exact_raw_free_matches.fetch_add(1,std::memory_order_relaxed);
        if(current)live_at_raw_free.fetch_add(1,std::memory_order_relaxed);
        LOG("[native_setinfo_probe] raw_free=%s serial=%u ptr=%08X info=%08X live=%u",
            matches[i].kind,matches[i].saved.serial,object,
            matches[i].saved.candidate.info,unsigned(current));
    }
    return 0;
}
void after_setinfo(Cpu* cpu,const story_native_stage_owner_hooks::Args& args,
                   uint32_t result,uint64_t) {
    story_native_setinfo_candidate::Candidate candidate;
    const Inspect fn=inspector.load(std::memory_order_acquire);
    const auto status=fn?fn(cpu,args,result,candidate):
                         story_native_setinfo_observation::Result::ReturnBoundary;
    const uint32_t n=setinfo.fetch_add(1,std::memory_order_relaxed)+1;
    const char* reason="return";
    using Result=story_native_setinfo_observation::Result;
    switch(status){
    case Result::Valid:{
        valid.fetch_add(1,std::memory_order_relaxed);reason="valid";
        std::lock_guard lock(saved_mutex);
        saved[next_saved%saved.size()]={args.r3_r6[0],n,0,candidate};
        ++next_saved;
        break;}
    case Result::Candidate:candidate_invalid.fetch_add(1,std::memory_order_relaxed);reason="candidate";break;
    case Result::Manager:manager_invalid.fetch_add(1,std::memory_order_relaxed);reason="manager";break;
    case Result::Layout:layout_invalid.fetch_add(1,std::memory_order_relaxed);reason="layout";break;
    case Result::ReturnBoundary:return_invalid.fetch_add(1,std::memory_order_relaxed);break;
    }
    if(n<=16||n%64==0)
        LOG("[native_setinfo_probe] count=%u result=%s valid=%u candidate=%u manager=%u layout=%u",
            n,reason,valid.load(std::memory_order_relaxed),
            candidate_invalid.load(std::memory_order_relaxed),
            manager_invalid.load(std::memory_order_relaxed),
            layout_invalid.load(std::memory_order_relaxed));
}
void failed_setinfo(Cpu*,const story_native_stage_owner_hooks::Args&,uint64_t) {
    failed.fetch_add(1,std::memory_order_relaxed);
}
void after_destroy(Cpu*,uint32_t,const story_native_stage_owner_hooks::Args&,uint64_t) {}
void after_free(Cpu*,const story_native_stage_owner_hooks::Args&,uint64_t) {}
void fault() noexcept {faults.fetch_add(1,std::memory_order_relaxed);}
const story_native_stage_owner_hooks::Observer observer{
    before_setinfo,before_destroy,after_setinfo,failed_setinfo,after_destroy,
    fault,nullptr,before_free,after_free};
}
bool enable(Inspect inspect,LiveInspect live,DeleteMatch deletion) noexcept {
    if(!inspect||!live||!deletion)return false;
    Inspect empty=nullptr;
    if(!inspector.compare_exchange_strong(empty,inspect,std::memory_order_release,
                                          std::memory_order_relaxed))return false;
    live_inspector.store(live,std::memory_order_release);
    delete_matcher.store(deletion,std::memory_order_release);
    if(!story_native_stage_owner_hooks::register_observer(&observer)){
        inspector.store(nullptr,std::memory_order_release);
        live_inspector.store(nullptr,std::memory_order_release);
        delete_matcher.store(nullptr,std::memory_order_release);
        return false;
    }
    return true;
}
Counts counts() noexcept {
    return {setinfo.load(),setinfo_entered.load(),valid.load(),candidate_invalid.load(),
            manager_invalid.load(),layout_invalid.load(),return_invalid.load(),
            failed.load(),teardown.load(),faults.load(),exact_matches.load(),
            live_matches.load(),raw_frees.load(),exact_raw_free_matches.load(),
            live_at_raw_free.load()};
}
} // namespace story_native_setinfo_diagnostic
