// Source-labelled stand-in for native A cleanup before B's independent start.
// Historical A is host-revoked; none of its old admission is reused for B.
assert(b_pristine->matches_saved(saved.view()));
fixture_frozen=false;++freeze_epoch;
context->invalidate();
for(const auto&[at,bytes]:cross_retail_memory)f.memory[at]=bytes;
const auto true_sp=cpu.r[1];cpu.r[1]=0x600300C0;assert(b_session.inspect(&cpu));cpu.r[1]=true_sp;
auto b_hold=b_session.try_fence();assert(b_hold);
const auto b_expected=*b_session.expected();assert(b_expected!=expected&&b_expected.program!=expected.program&&b_expected.identity.program!=expected.identity.program);
auto b_poses=poses;for(unsigned n=0;n<2;++n){b_poses[n].identity.generation=b_expected.identity.generation;b_poses[n].cut=b_expected.actors[n].cut;}
assert(b_session.stage_image(b_poses));
D::Tracker b_depth(b_expected,{{1,1,true},{2,2,true},{3,3,true},true});
auto b_config=config;b_config.memory.stamp=b_arena.turn.stamp.address;b_config.collector.stamp=b_arena.turn.stamp.address;b_config.memory.record={b_arena.turn.record.address,b_arena.turn.record.bytes};
RC::Context*b_actual=nullptr;
auto b_callbacks=restore_callbacks;b_callbacks.session=&b_session;b_callbacks.read=read;
b_callbacks.fence=[&]{return b_session.fence_held();};
b_callbacks.retained=[&](const auto&e){return e==b_expected&&b_actual&&b_actual->retained();};
b_callbacks.baseline=[&](const auto&e){auto b=b_session.baseline();if(!b)b=b_session.staged_expected();return b&&*b==e;};
b_callbacks.resident=[&](const Actor&a,const auto&i){return i==b_expected.identity&&std::any_of(b_expected.actors.begin(),b_expected.actors.end(),[&](const auto&v){return a.address==v.address&&a.pid==v.pid&&a.token==v.token&&a.profile==v.profile&&a.subtype==v.subtype&&a.fingerprint==v.fingerprint;});};
b_callbacks.allocation=[&](const auto&i,const auto&c){return i==b_expected.identity&&c.stamp==b_config.memory.stamp;};
assert(!b_config.collector.clock&&b_session.fence_held()&&b_session.staged_expected()&&*b_session.staged_expected()==b_expected);
assert(story_native_event_arena::owns(b_arena,mem::runtime_alloc_log(),mem::runtime_top()));
assert(GT::verify(&cpu,GT::ticket(&cpu)));
auto b_backend=RB::make(b_expected,b_arena,b_config,*stage,GT::ticket(&cpu),&cpu,b_depth,b_callbacks);assert(b_backend);
auto b_model=story_native_wait_runtime_model_graph::Context::prepare(b_session,b_depth,b_expected,*stage,GT::ticket(&cpu),&cpu,*b_backend,graph_reason);assert(b_model);
auto b_sources=restored_sources;b_sources.model_graph=b_model;b_sources.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>regs){
 ++native_calls;std::vector<uint32_t>a(regs);switch(fn){
 case 0x0253EC0C:f.byte(play+0x51D0+0xC0,1);return 1u;
 case 0x0253FD1C:f.byte(play+0x51D0+0xC0,0);f.byte(play+0x51D0+0xC2,3);for(unsigned p:{0xC4,0xC8,0xCC,0xD0})w(play+0x51D0+p,~0u);f.half(play+0x51D0+0xD8,0xFFFF);f.half(play+0x51D0+0xDE,0xFFFF);return 1u;
 case 0x0254273C:for(unsigned n=0;n<8;++n)w(b_expected.identity.manager+n*4,0);return 0u;
 case 0x025443B4:{const auto pristine=b_expected.program->pristine();for(unsigned n=0;n<8;++n){const unsigned off=(n?n-1:0)*8;const uint32_t pool=uint32_t(pristine[off])<<24|uint32_t(pristine[off+1])<<16|uint32_t(pristine[off+2])<<8|pristine[off+3];const uint32_t count=uint32_t(pristine[off+4])<<24|uint32_t(pristine[off+5])<<16|uint32_t(pristine[off+6])<<8|pristine[off+7];w(b_expected.identity.manager+n*4,n?(count?b_expected.identity.image+pool:0):b_expected.identity.image);}return 0u;}
 case 0x0259F7D4:w(a[0],a[1]);w(a[0]+8,a[2]);w(a[0]+0x10,~0u);return 0u;
 case 0x02544534:w(b_expected.identity.image+0xE4,1);return 1u;
 default:assert(false);return 0u;
 }};
auto b_context=RC::Context::prepare(b_session,b_depth,b_expected,b_arena,*stage,GT::ticket(&cpu),&cpu,*b_backend,{{{"test","first"},{"test","second"}}},std::move(b_sources));assert(b_context);b_actual=b_context.get();
assert(b_context->tick(&cpu,true)==RC::State::Started&&native_calls==14);b_hold.reset();
std::atomic<RC::Context*>front_context{b_context.get()};std::atomic<D::Tracker*>front_depth{&b_depth};
std::vector<std::unique_ptr<D::Tracker>>restored_depths;std::vector<std::unique_ptr<RC::Context>>restored_contexts;
cross_observer=[&](Cpu*c,const story_native_restore_resume::Boundary&boundary){
 assert(b_context->suspended_for_restore(boundary));
 auto fresh=guestmods::NativeRestoreTrackerIssuer::acquire(boundary,c);assert(fresh);auto*new_depth=fresh.get();restored_depths.push_back(std::move(fresh));
 const auto fresh_stage=SO::snapshot();assert(fresh_stage&&boundary.completion().stage()&&fresh_stage->receipt==boundary.completion().stage()->receipt);
 auto pending=std::make_shared<const story_native_restore_resume::Boundary*>(&boundary);
 struct Clear{std::shared_ptr<const story_native_restore_resume::Boundary*>p;~Clear(){*p=nullptr;}}clear{pending};
 auto next_callbacks=restore_callbacks;
 next_callbacks.retained=[&,new_depth,pending](const auto&e){if(e!=expected)return false;if(*pending)return (*pending)->current(&cpu)&&new_depth->restoring(**pending)&&session.baseline()&&*session.baseline()==expected;return actual_context==front_context.load()&&actual_context&&actual_context->retained();};
 RC::Context::RestoredBackendFactory factory=[&,new_depth,fresh_stage,next_callbacks](const auto&e,const auto&a){return RB::make(e,a,config,*fresh_stage,GT::ticket(c),c,*new_depth,next_callbacks);};
 auto next=RC::Context::prepare_restored(boundary,session,*new_depth,*fresh_stage,GT::ticket(c),c,std::move(factory),{{{"test","first"},{"test","second"}}},restored_sources);
 assert(next&&next->state()==RC::State::RestoredDormant&&next->retained());auto*next_ptr=next.get();restored_contexts.push_back(std::move(next));
 assert(next_ptr->resume_restored(boundary,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth)));
 actual_context=next_ptr;assert(front_context.load()==next_ptr&&front_depth.load()==new_depth&&new_depth->ready()&&native_calls==14);
};
fixture_frozen=true;++freeze_epoch;const auto parked=threads::current_quiescence(&cpu);assert(parked);
auto view=saved.view();auto owner=RC::Context::prepare_current_load(story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),view,1);assert(owner&&owner->current(&cpu,*parked,view,1)&&owner->holds_session(b_session));
assert(!owner->holds_session(session)&&b_context->state()==RC::State::Quarantined);
auto group=story_native_stage_restore_group::StageRestoreGroup::create();assert(group);
auto attempt=checkpoint->fork_attempt();assert(attempt);
auto permit=context->prepare_run_restore(std::move(*attempt),view,1,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),group,owner);assert(permit);
assert(context->suspend_run_restore(*permit));
auto b_restore=b_session.prepare_pristine_restore(*b_pristine,view,&cpu,*parked,1,owner);assert(b_restore);
// Restore captured bytes for both SDK arenas and all native graph/consumer
// memory. This is the snapshot primitive, not a source of host authority.
for(auto&[at,bytes]:f.memory)for(size_t n=0;n<bytes.size();++n){const auto addr=at+uint32_t(n),page=addr&~(ss::kMemoryChunk-1);auto found=saved.bytes.find(page);if(found!=saved.bytes.end())bytes[n]=found->second[addr&(ss::kMemoryChunk-1)];}
saved.restore(write,mapped,arena);
for(uint32_t n=0;n<b_arena.allocation.bytes;++n){const auto at=b_arena.allocation.address+n;mem::ptr(at)[0]=saved.bytes.at(at&~(ss::kMemoryChunk-1))[at&(ss::kMemoryChunk-1)];}
SO::state_loaded();GT::state_loaded();front_depth.store(nullptr);
auto window=ss::NativeRestorePipelineIssuer::open(&cpu);assert(window);
std::unique_ptr<S::Session::Fence>a_finished_fence;
assert(owner->current(&cpu,*parked,view,1)&&context->finish_run_restore(std::move(*permit),&a_finished_fence));
assert(b_session.adopt_pristine(std::move(*b_restore),view,&cpu,*parked));
group->close();assert(!story_native_stage_owner::current_thread_holds_lifetime_pin());
assert(a_finished_fence&&session.fence_held()&&b_session.fence_held());a_finished_fence.reset();assert(!session.fence_held());owner->close();assert(!b_session.fence_held()&&!owner->current(&cpu,*parked,view,1));
ss::NativeRestorePipelineIssuer::threads_loaded(*window);fixture_frozen=false;++freeze_epoch;ss::NativeRestorePipelineIssuer::thawed(*window);window.reset();interp::NativeResumeBoundaryIssuer::returned(&cpu);
assert(restored_contexts.size()==1&&resume_boundaries==1&&native_calls==14&&front_context.load()!=b_context.get());
cross_observer={};actual_context=context.get();restored_contexts.clear();restored_depths.clear();b_context.reset();
