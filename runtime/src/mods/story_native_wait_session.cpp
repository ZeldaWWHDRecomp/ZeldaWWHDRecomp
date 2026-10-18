// SPDX-License-Identifier: MPL-2.0
#include "story_native_cancel_observation.h"
#include "story_native_wait_session.h"
#include "story_native_event_collect.h"
#include "story_native_event_record.h"
#include "story_native_turn_record.h"
#include "runtime.h"
#include <cstring>
#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace story_native_wait_session { namespace {
bool turn(const Session::Expected&e){return e.program&&e.program->kind()==story_native_event_program::Kind::TurnCamera;}
story_native_event_arena::Instance instance(const Session::Layout&l,const Session::Expected&e){return turn(e)?l.turn:l.wait;}
std::optional<story_native_people_pose_record::Poses> turn_poses(std::span<const uint8_t> bytes,const Session::Expected&e,bool checkpoint=false){
 story_native_turn_record::Record record;
 if(!story_native_turn_record::decode(bytes,record)||record.expected!=e)return {};
 const auto phase=record.phase;
 if(phase==story_native_event_lease::Phase::Prepared){if(record.elapsed_ticks||record.completion_flags)return {};}
 else if(!checkpoint||(phase!=story_native_event_lease::Phase::Published&&phase!=story_native_event_lease::Phase::Running))return {};
 return record.poses;
}
bool same_poses(const story_native_people_pose_record::Poses&a,const story_native_people_pose_record::Poses&b){
 for(unsigned n=0;n<2;++n){const auto&x=a[n];const auto&y=b[n];
  if(x.identity!=y.identity||x.angles!=y.angles||x.shape!=y.shape||x.joint!=y.joint||x.cut!=y.cut||
     x.target!=y.target||x.velocity!=y.velocity||x.head_only!=y.head_only||x.noticed!=y.noticed||
     x.talk_ready!=y.talk_ready||x.target_mode!=y.target_mode||x.status!=y.status)return false;
 }return true;
}
std::atomic<uint32_t> next_generation{2}; // readiness's provisional 1 is never issued
uint32_t mint_generation() noexcept {
    uint32_t value=next_generation.load(std::memory_order_relaxed);
    while(value && value<std::numeric_limits<uint32_t>::max()) {
        if(next_generation.compare_exchange_weak(value,value+1,
                std::memory_order_relaxed,std::memory_order_relaxed)) return value;
    }
    return 0;
}
} // namespace
thread_local Session::Fence* Session::active_fence_=nullptr;
Session::Fence::Fence(const Session* owner,std::unique_lock<std::mutex>&& lock)
    :owner_(owner),prior_(active_fence_),lock_(std::move(lock)) {
    if(prior_)prior_->next_=this;
    active_fence_=this;
}
Session::Fence::~Fence(){
    if(next_)next_->prior_=prior_;
    else active_fence_=prior_;
    if(prior_)prior_->next_=next_;
}
Session::Session(Request request,Sources sources):root_request_(request),idle_sources_(sources),readiness_(std::move(request),std::move(sources)){}
std::unique_ptr<Session::Fence> Session::try_fence() {
    if(fence_held())return {};
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return {};
    return std::unique_ptr<Fence>(new Fence(this,std::move(lock)));
}
bool Session::fence_held()const noexcept{
    // Multiple scene arenas must remain fenced together during full-state
    // capture/restore. Each list entry owns its actual Session mutex; nesting
    // another Session never revokes the outer Session's held lock.
    for(auto*fence=active_fence_;fence;fence=fence->prior_)
        if(fence->owner_==this&&fence->lock_.owns_lock())return true;
    return false;
}
bool Session::startup(){
    auto hold=try_fence();if(!hold||attempted_)return false;
    attempted_=true;
    try{
        if(readiness_.startup()!=story_native_wait_readiness::Status::Allocated)
            return false;
        const auto layout=readiness_.arena();
        if(!story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top()))
            return quarantined_=true,false;
        generation_=mint_generation();
        if(!generation_)return quarantined_=true,false;
        journal_backend_=story_native_wait_journal_store::guest_backend(layout,
            [this]{return fence_held();});
        journal_store_=std::make_unique<story_native_wait_journal_store::Store>(layout,journal_backend_);
        Record initial;initial.generation=generation_;initial.serial=1;
        initial.arena=layout.allocation.address;
        initial.phase=story_native_wait_journal::Phase::Unpublished;
        initial.definition=readiness_.definition_hash();
        if(!journal_store_->persist_initial(initial))return quarantined_=true,false;
        unexposed_=true;
        return true;
    }catch(...){quarantined_=true;return false;}
}
std::optional<Session::Record> Session::journal()const{
    if(!fence_held()||quarantined_||!journal_store_)return {};
    try{
        const auto receipt=journal_store_->receipt();
        if(!receipt){quarantined_=true;return {};}
        std::array<uint8_t,story_native_wait_journal::kBytes> bytes{};
        Record actual;
        if(!journal_backend_.read(readiness_.arena().wait.header.address,bytes)||
           !story_native_wait_journal::decode(bytes,actual)||actual!=*receipt){
            quarantined_=true;return {};
        }
        return actual;
    }catch(...){quarantined_=true;return {};}
}
bool Session::preparing_admission()const{
    if(!fence_held()||quarantined_||restore_pending_)return false;
    if(!reusable_)return unexposed();
    const auto current=journal();
    return native_mutation_&&!unexposed_&&!publication_sealed_&&current&&
        current->phase==story_native_wait_journal::Phase::Unpublished&&
        current->generation==generation_&&current->definition==readiness_.definition_hash()&&
        current->arena==readiness_.arena().allocation.address&&
        story_native_event_arena::owns(readiness_.arena(),mem::runtime_alloc_log(),mem::runtime_top());
}
bool Session::rearm(RearmCertificate&& certificate,Cpu* cpu){
    // Move out even on an invalid attempt: caller cannot replay a refused token.
    auto seal=std::move(certificate.seal_);if(!seal)return false;
    std::lock_guard lock(seal->mutex);
    if(seal->consumed)return false;seal->consumed=true;
    if(!seal->live||seal->owner!=this||!cpu||!fence_held()||quarantined_||
       restore_pending_||!publication_sealed_||!native_mutation_||unexposed_||reusable_)
        return false;
    try{
        const auto current=journal();const auto old=baseline();
        if(!current||*current!=seal->journal||current->phase!=story_native_wait_journal::Phase::Unpublished||
           !old||*old!=seal->expected||!seal->verify||!seal->verify(cpu)){LOG("[native_wait] session rearm refused stage=initial-seal sp=%08X",cpu->r[1]);return false;}
        // Real read-only readiness is sampled anew, with the old issuer still
        // live under its mutex and this Session fence. It never allocates anew.
        const auto ready=readiness_.inspect(cpu);
        LOG("[native_wait] session rearm readiness status=%u check=%u sp=%08X",unsigned(ready),unsigned(readiness_.check()),cpu->r[1]);
        if(ready!=story_native_wait_readiness::Status::Ready)return false;
        const auto* candidate=readiness_.candidate();
        if(!candidate||candidate->identity.generation!=1)return false;
        auto next=*candidate;const auto original=readiness_.original_image();
        std::vector<uint8_t> image(original.begin(),original.end());
        if(image.size()!=next.identity.bytes||!seal->verify(cpu)||!seal->live||quarantined_){LOG("[native_wait] session rearm refused stage=final-seal sp=%08X",cpu->r[1]);return false;}
        const uint32_t generation=mint_generation();
        if(!generation)return false;
        auto idle_record=*current;idle_record.generation=generation;++idle_record.serial;
        next.identity.generation=generation;
        auto idle_lineage=std::make_shared<const IdleLineage>(IdleLineage{this,host_identity_,idle_record,next});
        if(!journal_store_->persist_rearm(*current,generation)){
            quarantined_=true;return false;
        }
        generation_=generation;next.identity.generation=generation;
        publication_sealed_=false;staged_expected_.reset();staged_image_.clear();
        staged_record_.clear();staged_depth_.reset();staged_pose_bytes_.clear();original_poses_.reset();
        expected_=std::move(next);canonical_image_=std::move(image);reusable_=true;idle_lineage_=std::move(idle_lineage);
        // native_mutation_ stays true and unexposed_ stays false permanently.
        return true;
    }catch(...){quarantined_=true;return false;}
}
bool Session::inspect(Cpu* cpu){
    auto hold=try_fence();if(!hold||quarantined_||(!unexposed_&&!reusable_)||restore_pending_||
        !journal_store_||!generation_)return false;
    expected_.reset();canonical_image_.clear();
    const auto record=journal();
    if(!record||record->phase!=story_native_wait_journal::Phase::Unpublished||
       record->generation!=generation_||record->arena!=readiness_.arena().allocation.address||
       record->definition!=readiness_.definition_hash())return quarantined_=true,false;
    try{
        if(readiness_.inspect(cpu)!=story_native_wait_readiness::Status::Ready)return false;
        const auto* candidate=readiness_.candidate();
        if(!candidate||candidate->identity.generation!=1)return quarantined_=true,false;
        expected_=*candidate;
        expected_->identity.generation=generation_;
        const auto image=readiness_.original_image();
        canonical_image_.assign(image.begin(),image.end());
        if(canonical_image_.size()!=expected_->identity.bytes){
            expected_.reset();canonical_image_.clear();quarantined_=true;return false;
        }
        return true;
    }catch(...){expected_.reset();canonical_image_.clear();quarantined_=true;return false;}
}
std::optional<Session::Expected> Session::expected()const{
    return fence_held()&&preparing_admission()?expected_:std::nullopt;
}
Session::Layout Session::arena()const{return fence_held()?readiness_.arena():Layout{};}
uint32_t Session::generation()const{return fence_held()?generation_:0;}
Session::Check Session::readiness_check()const{
    return fence_held()?readiness_.check():Check::Allocation;
}
std::vector<uint8_t> Session::canonical_image()const{
    return fence_held()&&preparing_admission()?canonical_image_:std::vector<uint8_t>{};
}
bool Session::stage_image(){return stage_image_impl(nullptr);}
bool Session::stage_image(const story_native_people_pose_record::Poses&poses){return stage_image_impl(&poses);}
bool Session::stage_image_impl(const story_native_people_pose_record::Poses*poses){
    if(!fence_held()||publication_sealed_||!preparing_admission()||!expected_||staged_expected_)return false;
    bool started=false;
    try{
        const auto layout=readiness_.arena();
        const auto baseline=*expected_;
        auto image=canonical_image_;
        std::vector<uint8_t> record;const auto selected=instance(layout,baseline);
        if(baseline.identity.image!=selected.image.address||baseline.identity.bytes!=selected.image.bytes||
           baseline.identity.generation!=generation_||image.size()!=selected.image.bytes)return false;
        if(turn(baseline)){
            if(!poses)return false;
            story_native_turn_record::Record data;data.expected=baseline;data.program=*baseline.program->turn_definition();
            data.poses=*poses;data.phase=story_native_event_lease::Phase::Prepared;data.watchdog_ticks=baseline.program->watchdog();
            if(!story_native_turn_record::encode(data,record)||record.size()>selected.record.bytes||!turn_poses(record,baseline))return false;
        }else if(poses||!story_native_event_record::encode(baseline,story_native_event_lease::Phase::Prepared,record)||
                 record.size()>kPoseRecordOffset||kPoseRecordOffset>=selected.record.bytes)return false;
        const auto stamp=story_native_event_collect::make_stamp(baseline);
        // From this point clean-only snapshot admission is permanently revoked.
        // Preparing records an incomplete private transaction, not a native lease.
        revoke_unexposed();started=true;
        if(!journal_store_->persist_transition(story_native_wait_journal::Phase::Preparing)){
            quarantined_=true;return false;
        }
        const auto put=[&](story_native_event_arena::Span target,std::span<const uint8_t> bytes){
            if(bytes.empty()||bytes.size()>target.bytes||!fence_held()||
               !story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top()))return false;
            const auto current=journal();
            if(!current||current->phase!=story_native_wait_journal::Phase::Preparing)return false;
            std::memcpy(mem::ptr(target.address),bytes.data(),bytes.size());
            return std::memcmp(mem::ptr(target.address),bytes.data(),bytes.size())==0;
        };
        if(!put(selected.image,image)||!put(selected.stamp,stamp)||!put(selected.record,record)){
            journal_store_->persist_transition(story_native_wait_journal::Phase::Quarantined);
            quarantined_=true;return false;
        }
        staged_expected_=baseline;staged_image_=std::move(image);staged_record_=std::move(record);
        if(poses)original_poses_=*poses;
        return bool(staged_expected());
    }catch(...){
        if(started){quarantined_=true;try{journal_store_->persist_transition(story_native_wait_journal::Phase::Quarantined);}catch(...){}}
        return false;
    }
}
std::optional<Session::Expected> Session::staged_expected()const{
    if(!fence_held()||publication_sealed_)return {};
    return checked_baseline(false);
}
std::optional<Session::Expected> Session::checked_baseline(bool native)const{
    if(!fence_held()||quarantined_||!staged_expected_)return {};
    const auto layout=readiness_.arena();const auto current=journal();
    if(!current||current->generation!=generation_||
       current->arena!=layout.allocation.address||current->definition!=readiness_.definition_hash()||
       (native?!publication_sealed_||current->phase==story_native_wait_journal::Phase::Quarantined:
                publication_sealed_||current->phase!=story_native_wait_journal::Phase::Preparing)||
       !story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top())){
        quarantined_=true;return {};
    }
    const auto selected=instance(layout,*staged_expected_);
    const auto stamp=story_native_event_collect::make_stamp(*staged_expected_);
    bool image_matches=staged_image_.size()==selected.image.bytes;
    if(image_matches){
        const auto* bytes=mem::ptr(selected.image.address);
        for(size_t p=0;p<staged_image_.size();++p){
            bool mutable_byte=p>=0xE4&&p<0xE8;
            for(unsigned n=0;n<4;++n)mutable_byte|=p>=0xF0+n*0x50+0x38&&p<0xF0+n*0x50+0x48;
            if((!native||!mutable_byte)&&bytes[p]!=staged_image_[p]){image_matches=false;break;}
        }
    }
    if(!image_matches||staged_record_.empty()||
       std::memcmp(mem::ptr(selected.stamp.address),stamp.data(),stamp.size())||
       std::memcmp(mem::ptr(selected.record.address),staged_record_.data(),staged_record_.size())||
       (staged_depth_&&std::memcmp(mem::ptr(selected.depth.address),staged_depth_->data(),staged_depth_->size()))||
       (!staged_pose_bytes_.empty()&&std::memcmp(mem::ptr(selected.record.address+kPoseRecordOffset),staged_pose_bytes_.data(),staged_pose_bytes_.size()))){
        quarantined_=true;return {};
    }
    if(turn(*staged_expected_)){
        const auto poses=turn_poses(staged_record_,*staged_expected_,native);
        if(!poses||!original_poses_||!same_poses(*poses,*original_poses_)){quarantined_=true;return {};}
    }
    return staged_expected_;
}
bool Session::stage_depth(const story_native_event_depth::Record& record){
    const auto baseline=staged_expected();
    if(!baseline||!story_native_event_depth::Tracker::valid_idle_record(record,*baseline))return false;
    const auto selected=instance(readiness_.arena(),*baseline);
    if(record.size()!=selected.depth.bytes)return false;
    std::memcpy(mem::ptr(selected.depth.address),record.data(),record.size());
    if(std::memcmp(mem::ptr(selected.depth.address),record.data(),record.size())){
        quarantined_=true;return false;
    }
    staged_depth_=record;return true;
}
bool Session::stage_poses(const story_native_people_pose_record::Poses& poses){
    const auto baseline=staged_expected();const auto current=journal();
    if(!baseline||!current||turn(*baseline)||!staged_pose_bytes_.empty())return false;
    std::vector<uint8_t> encoded;
    const auto layout=readiness_.arena();
    if(!story_native_people_pose_record::encode(*baseline,current->definition,poses,encoded)||
       encoded.size()>layout.wait.record.bytes-kPoseRecordOffset)return false;
    const uint32_t at=layout.wait.record.address+kPoseRecordOffset;
    std::memcpy(mem::ptr(at),encoded.data(),encoded.size());
    if(std::memcmp(mem::ptr(at),encoded.data(),encoded.size())){quarantined_=true;return false;}
    staged_pose_bytes_=std::move(encoded);original_poses_=poses;return bool(staged_poses());
}
std::optional<story_native_people_pose_record::Poses> Session::staged_poses()const{
    const auto baseline=staged_expected();const auto current=journal();
    if(!baseline||!current)return {};
    if(turn(*baseline))return turn_poses(staged_record_,*baseline);
    if(staged_pose_bytes_.empty())return {};
    story_native_people_pose_record::Poses poses;
    if(!story_native_people_pose_record::decode(staged_pose_bytes_,*baseline,current->definition,poses)){
        quarantined_=true;return {};
    }
    return poses;
}
bool Session::begin_publication(const story_native_event_observation::Result& prepare){
    using O=story_native_event_observation::Operation;
    if(!fence_held()||publication_sealed_)return false;
    const auto expected=staged_expected();
    if(!expected||!prepare.valid()||prepare.operation()!=O::Prepare||
       prepare.event_state()!=0||!prepare.matches(*expected)||!staged_depth_||
       !story_native_event_depth::Tracker::valid_idle_record(*staged_depth_,*expected)||
       !staged_poses())return false;
    // Header remains Preparing: it was persisted before private staging and
    // therefore before the controller's first native publication mutation.
    publication_sealed_=true;
    return bool(baseline());
}
std::optional<Session::Expected> Session::baseline()const{
    if(!fence_held()||!publication_sealed_)return {};
    return checked_baseline(true);
}
std::optional<story_native_people_pose_record::Poses> Session::baseline_poses()const{
    const auto expected=baseline();const auto current=journal();
    if(!expected||!current)return {};
    if(turn(*expected))return original_poses_;
    if(staged_pose_bytes_.empty())return {};
    story_native_people_pose_record::Poses poses;
    if(!story_native_people_pose_record::decode(staged_pose_bytes_,*expected,current->definition,poses)){
        quarantined_=true;return {};
    }
    return poses;
}
bool Session::checkpoint_native(const story_native_event_observation::Result&result,uint32_t elapsed,uint32_t flags,
    const std::function<bool()>&guard,const std::function<bool(uint32_t,std::span<const uint8_t>)>&write){
    using O=story_native_event_observation::Operation;using P=story_native_event_lease::Phase;
    using J=story_native_wait_journal::Phase;
    bool attempted=false;
    try{
        const auto expected=baseline();const auto current=journal();
        if(!expected||!current||!result.valid()||!result.matches(*expected)||!guard||!write||!guard())return false;
        const bool published=result.operation()==O::Publish&&result.event_state()==1&&current->phase==J::Published;
        const bool running=result.operation()==O::Running&&(result.event_state()==2||result.event_state()==4)&&current->phase==J::Running;
        if(!published&&!running)return false;
        const auto phase=published?P::Published:P::Running;std::vector<uint8_t> encoded;
        if(turn(*expected)){
            if(!original_poses_||(published&&flags))return false;
            story_native_turn_record::Record record;record.expected=*expected;record.program=*expected->program->turn_definition();
            record.poses=*original_poses_;record.phase=phase;record.elapsed_ticks=elapsed;record.completion_flags=flags;record.watchdog_ticks=expected->program->watchdog();
            story_native_turn_record::Record previous;
            if(!story_native_turn_record::decode(staged_record_,previous)||previous.elapsed_ticks>elapsed||
               (previous.completion_flags&~flags)||!story_native_turn_record::encode(record,encoded))return false;
        }else if(elapsed||flags||!story_native_event_record::encode(*expected,phase,encoded)||encoded.size()>kPoseRecordOffset)return false;
        const auto selected=instance(readiness_.arena(),*expected);
        if(encoded.empty()||encoded.size()>selected.record.bytes||!guard())return false;
        attempted=true;
        if(!write(selected.record.address,encoded)||!fence_held()||
           std::memcmp(mem::ptr(selected.record.address),encoded.data(),encoded.size())){
            quarantine_native();return false;
        }
        staged_record_=std::move(encoded);
        if(!guard()||!baseline()){quarantine_native();return false;}
        return true;
    }catch(...){if(attempted)quarantine_native();return false;}
}
bool Session::persist_depth(const story_native_event_depth::Record& record){
    using P=story_native_wait_journal::Phase;
    const auto expected=baseline();const auto current=journal();
    if(!expected||!current||current->phase==P::Unpublished||
       !story_native_event_depth::Tracker::valid_idle_record(record,*expected))return false;
    const auto serial=[](const auto&bytes){
        auto word=[&](unsigned p){return uint32_t(bytes[p])<<24|uint32_t(bytes[p+1])<<16|uint32_t(bytes[p+2])<<8|bytes[p+3];};
        return uint64_t(word(68))<<32|word(72);
    };
    if(!staged_depth_||serial(record)<serial(*staged_depth_))return false;
    const auto selected=instance(readiness_.arena(),*expected);
    if(record.size()!=selected.depth.bytes)return false;
    std::memcpy(mem::ptr(selected.depth.address),record.data(),record.size());
    if(std::memcmp(mem::ptr(selected.depth.address),record.data(),record.size())){
        quarantine_native();return false;
    }
    staged_depth_=record;return true;
}
bool Session::transition_native(story_native_wait_journal::Phase next,
                                const story_native_event_observation::Result& result){
    using P=story_native_wait_journal::Phase;
    using O=story_native_event_observation::Operation;
    const auto expected=baseline();const auto current=journal();
    if(!expected||!current||!result.valid()||!result.matches(*expected))return false;
    const auto operation=result.operation();const auto state=result.event_state();
    const auto* image=mem::ptr(instance(readiness_.arena(),*expected).image.address);
    const uint32_t actual=uint32_t(image[0xE4])<<24|uint32_t(image[0xE5])<<16|
                          uint32_t(image[0xE6])<<8|image[0xE7];
    if(state!=actual)return false; // no stale certificate for another event phase
    bool allowed=false;
    switch(next){
    case P::Published:allowed=current->phase==P::Preparing&&operation==O::Publish&&state==1;break;
    case P::Running:allowed=current->phase==P::Published&&operation==O::Running&&(state==2||state==4);break;
    case P::Draining:allowed=(current->phase==P::Published||current->phase==P::Running)&&
        ((operation==O::Terminal&&state==0)||(current->phase==P::Published&&operation==O::Publish&&state==1));break;
    // Persist BEFORE the first consumer restore. A TerminalDrain result only
    // describes a post-write observation and cannot authorize this write-ahead.
    case P::Detaching:allowed=current->phase==P::Draining&&
        ((operation==O::Terminal&&state==0)||(operation==O::Publish&&state==1));break;
    case P::Unpublished:allowed=current->phase==P::Detaching&&result.released()&&
        ((operation==O::ReleaseTerminal&&state==0)||(operation==O::Release&&state==1));break;
    default:break;
    }
    if(!allowed)return false;
    if(!journal_store_->persist_transition(next)){quarantine_native();return false;}
    return bool(baseline());
}
bool Session::transition_cancel_native(story_native_wait_journal::Phase next,const story_native_cancel_observation::Result&result){
    using P=story_native_wait_journal::Phase;using C=story_native_cancel_observation::Phase;
    if(!fence_held())return false;
    const auto expected=baseline();const auto current=journal();const auto poses=baseline_poses();
    if(!expected||!current||!poses||!result.matches(*expected)||!result.matches_baseline(*poses))return false;
    const auto*image=mem::ptr(instance(readiness_.arena(),*expected).image.address);
    if(image[0xE4]||image[0xE5]||image[0xE6]||image[0xE7])return false;
    const bool allowed=(next==P::Draining&&(current->phase==P::Published||current->phase==P::Running)&&result.phase()==C::Closed)||
        (next==P::Detaching&&current->phase==P::Draining&&result.phase()==C::Closed)||
        (next==P::Unpublished&&current->phase==P::Detaching&&result.released());
    if(!allowed)return false;
    if(!journal_store_->persist_transition(next)){quarantine_native();return false;}
    return bool(baseline());
}
bool Session::quarantine_native(){
    if(!fence_held())return false;
    quarantined_=true;
    if(!journal_store_)return false;
    try{
        const auto current=journal_store_->receipt();
        if(!current)return false;
        if(current->phase==story_native_wait_journal::Phase::Quarantined)return true;
        return journal_store_->persist_transition(story_native_wait_journal::Phase::Quarantined);
    }catch(...){return false;}
}
bool Session::unexposed()const{
    if(!fence_held()||quarantined_||!unexposed_||native_mutation_||restore_pending_)return false;
    const auto layout=readiness_.arena();
    if(!story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top())){
        quarantined_=true;return false;
    }
    const auto record=journal();
    if(!record||record->phase!=story_native_wait_journal::Phase::Unpublished||
       record->generation!=generation_||record->arena!=layout.allocation.address||
       record->definition!=readiness_.definition_hash()){
        quarantined_=true;return false;
    }
    // runtime_alloc zeroes the entire exact allocation. The header is the
    // bootstrap's sole permitted write until revoke_unexposed() is called.
    const uint8_t* first=mem::ptr(layout.allocation.address);
    const size_t header=story_native_wait_journal::kBytes;
    const bool clean=layout.wait.header.address==layout.allocation.address&&
        std::all_of(first+header,first+layout.allocation.bytes,
                    [](uint8_t byte){return byte==0;});
    if(!clean)quarantined_=true;
    return clean;
}
void Session::revoke_unexposed(){
    if(!fence_held())return;
    if(publication_sealed_){quarantine_native();return;}
    native_mutation_=true;unexposed_=false;reusable_=false;expected_.reset();canonical_image_.clear();
    staged_expected_.reset();staged_image_.clear();staged_record_.clear();
    staged_depth_.reset();
    staged_pose_bytes_.clear();original_poses_.reset();
}
bool Session::begin_restore(){
    if(!fence_held()||!unexposed())return false;
    restore_pending_=true;unexposed_=false;expected_.reset();canonical_image_.clear();
    return true;
}
bool Session::finish_restore(const Record& saved){
    if(!fence_held()||!restore_pending_||native_mutation_||quarantined_)return false;
    const auto current=journal();
    if(!current||*current!=saved||saved.phase!=story_native_wait_journal::Phase::Unpublished||
       saved.generation!=generation_||saved.arena!=readiness_.arena().allocation.address||
       saved.definition!=readiness_.definition_hash()){
        quarantined_=true;return false;
    }
    const auto layout=readiness_.arena();
    if(!story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top())){
        quarantined_=true;return false;
    }
    const uint8_t* first=mem::ptr(layout.allocation.address);
    if(layout.wait.header.address!=layout.allocation.address||
       !std::all_of(first+story_native_wait_journal::kBytes,
                    first+layout.allocation.bytes,[](uint8_t byte){return byte==0;})){
        quarantined_=true;return false;
    }
    restore_pending_=false;unexposed_=true;
    return true; // candidate stays deferred until a fresh logic inspect
}
void Session::begin_progress_load(){
    if(fence_held()){if(publication_sealed_)quarantine_native();else revoke_unexposed();}
}
bool Session::quarantined()const{return fence_held()&&quarantined_;}
} // namespace story_native_wait_session
