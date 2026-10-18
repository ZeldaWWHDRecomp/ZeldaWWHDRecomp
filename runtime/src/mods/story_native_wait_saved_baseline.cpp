// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_session.h"
#include "story_native_restore_resume.h"
#include "native_quiescence.h"
#include "runtime.h"
namespace story_native_wait_session {
struct SavedBaseline::Data {
 const Session*owner=nullptr;std::shared_ptr<const uint8_t>host_identity;
 story_native_event_arena::Layout arena;story_native_event_observation::Expected expected;
 story_native_wait_journal::Record journal;story_native_people_pose_record::Poses poses;
 std::vector<uint8_t>canonical,image,record,pose_bytes;
 std::optional<story_native_event_depth::Record>depth;
 story_native_wait_run_footprint::Footprint footprint;
};
SavedBaseline::SavedBaseline(std::shared_ptr<const Data>d):data_(std::move(d)){}
const story_native_event_observation::Expected&SavedBaseline::expected()const{return data_->expected;}
const story_native_people_pose_record::Poses&SavedBaseline::poses()const{return data_->poses;}
bool SavedBaseline::matches_live(const story_native_event_memory::Read&read)const{return data_&&story_native_wait_run_footprint::matches_live(read,data_->footprint);}
std::shared_ptr<const SavedBaseline>Session::capture_saved_baseline(const story_native_wait_run_footprint::Footprint&footprint,const story_native_event_memory::Read&read)const{
 try{
  const auto expected=baseline();const auto header=journal();const auto poses=baseline_poses();
  if(!fence_held()||!expected||!header||!poses||!publication_sealed_||!native_mutation_||unexposed_||quarantined_||!staged_depth_||
   (header->phase!=story_native_wait_journal::Phase::Published&&header->phase!=story_native_wait_journal::Phase::Running)||
   !story_native_wait_run_footprint::matches_live(read,footprint))return {};
  auto d=std::make_shared<SavedBaseline::Data>();d->owner=this;d->host_identity=host_identity_;d->arena=readiness_.arena();d->expected=*expected;d->journal=*header;d->poses=*poses;
  d->canonical=canonical_image_;d->image=staged_image_;d->record=staged_record_;d->depth=staged_depth_;d->pose_bytes=staged_pose_bytes_;d->footprint=footprint;
  if(!baseline()||journal()!=header||!story_native_wait_run_footprint::matches_live(read,d->footprint))return {};
  return std::shared_ptr<const SavedBaseline>(new SavedBaseline(std::move(d)));
 }catch(...){return {};}
}
bool Session::adopt_restored(const story_native_restore_resume::Boundary&b,const threads::NativeQuiescence&q,const story_native_event_memory::Read&read){
 try{
  auto*c=threads::current();const auto saved=b.completion().origin().session_baseline();
  if(!c||!b.current(c)||!q.current(c)||!fence_held()||!saved||!saved->data_)return false;
  const auto&d=*saved->data_;if(d.owner!=this||d.host_identity!=host_identity_||d.expected!=b.completion().expected()||d.arena!=readiness_.arena()||
   d.journal.definition!=readiness_.definition_hash()||b.load_epoch()<=last_adopt_epoch_||!saved->matches_live(read)||
   !story_native_event_arena::owns(d.arena,mem::runtime_alloc_log(),mem::runtime_top()))return false;
  // A load attempt is one-use, even if subsequent host adoption fails.
  last_adopt_epoch_=b.load_epoch();
  auto store=std::make_unique<story_native_wait_journal_store::Store>(d.arena,journal_backend_);
  if(!store->adopt_restored(d.journal)||!b.current(c)||!q.current(c)||!saved->matches_live(read))return false;
  adopted_baseline_=saved;journal_store_=std::move(store);generation_=d.expected.identity.generation;staged_expected_=d.expected;
  canonical_image_=d.canonical;staged_image_=d.image;staged_record_=d.record;staged_depth_=d.depth;staged_pose_bytes_=d.pose_bytes;original_poses_=d.poses;
  expected_.reset();publication_sealed_=true;native_mutation_=true;unexposed_=false;reusable_=false;restore_pending_=false;quarantined_=false;
  const auto restored=baseline();return restored&&*restored==d.expected&&journal()==std::optional(d.journal)&&b.current(c)&&q.current(c)&&saved->matches_live(read);
 }catch(...){return false;}
}
bool Session::adopted_restored(const story_native_restore_resume::Boundary&b)const{
 auto*c=threads::current();const auto saved=b.completion().origin().session_baseline();
 if(!c||!b.current(c)||!fence_held()||!saved||saved!=adopted_baseline_||last_adopt_epoch_!=b.load_epoch())return false;
 const auto current=baseline();return current&&*current==saved->expected();
}

}
