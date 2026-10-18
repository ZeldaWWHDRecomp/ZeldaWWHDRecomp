// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_stage_restore_group.h"
#include "savestate_native_gate.h"
#include <cassert>
#include <thread>
#include <array>
#include <vector>
#include <cstdint>
namespace mem {uint32_t runtime_top(){return 0x68000000;}}
namespace threads {class NativeQuiescenceIssuer {public:static QuiescenceWitness witness(Cpu*c,bool&live){return QuiescenceWitness([c,&live](const Cpu*x){return live&&c==x;});}};}
namespace O=story_native_stage_owner_observer;
using G=story_native_stage_restore_group::StageRestoreGroup;
namespace {
O::Candidate candidate(){O::Candidate c;c.info=0x60810000;c.archive_wrapper=0x60811000;c.archive_holder=0x60812000;c.archive_resource=0x60818000;c.archive_base=0x6081F000;c.archive_bytes=0x3000;c.outer_base=0x60820000;c.outer_bytes=0x1000;c.resource=0x60820240;c.resource_bytes=0x80;c.entry=0x60820118;c.archive_key=0x1004DF68;c.resource_key=0x1004DF70;c.request_heap=c.pack_heap=0x60830000;c.data_heap=0x60831000;c.data_heap_parent=0x60832000;c.stage_name[0]='s';return c;}
story_native_setinfo_observation::Result inspect(const Cpu*,const O::Args&,uint32_t r,O::Candidate&out){out=candidate();return r==1?story_native_setinfo_observation::Result::Valid:story_native_setinfo_observation::Result::Candidate;}
bool live(uint32_t control,const O::Candidate&e,O::Candidate&out){out=candidate();return control==0x60700000&&e==out;}
// Isolated lifecycle source; complete real SavedView parser tested separately.
bool saved(const ss::native_gate::SavedView&,uint32_t control,const O::Candidate&e,O::Candidate&out){return live(control,e,out);}
void issue(){const auto*o=O::observer();O::Args a{{0x60700000,0x60700010,0x60700020,0}};std::lock_guard lock(*o->lifetime_fence);const auto k=o->before_setinfo(nullptr,a);assert(k);o->after_setinfo(nullptr,a,1,k);assert(O::snapshot());}
}
int main(){assert(O::configure(inspect,live,saved));uint64_t cpu_storage=0;auto*cpu=reinterpret_cast<Cpu*>(&cpu_storage);bool parked=true;auto witness=threads::NativeQuiescenceIssuer::witness(cpu,parked);
 std::array<uint8_t,8>allocation{};std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;
 auto view=[&]{return ss::native_gate::SavedView{ss::Reader(allocation.data(),allocation.size()),regions,chunks};};
 for(unsigned mode=0;mode<9;++mode){issue();const auto original=*O::snapshot();auto group=G::create();auto a=group->join(original,view(),cpu,witness);assert(a);
  if(mode==1){auto bad=original;++bad.receipt.owner.resource;assert(!group->join(bad,view(),cpu,witness));O::state_loaded();assert(!a->finish(view(),cpu,witness));continue;}
  if(mode==2){ss::MemoryChunks other;auto wrong=ss::native_gate::SavedView{ss::Reader(allocation.data(),allocation.size()),regions,other};assert(!group->join(original,wrong,cpu,witness));O::state_loaded();assert(!a->finish(view(),cpu,witness));continue;}
  auto b=group->join(original,view(),cpu,witness);assert(b);
  if(mode==3){assert(!a->finish(view(),cpu,witness));O::state_loaded();assert(!b->finish(view(),cpu,witness));continue;}
  if(mode==4){O::state_loaded();O::state_loaded();assert(!a->finish(view(),cpu,witness)&&!b->finish(view(),cpu,witness));continue;}
  if(mode==5){a.reset();O::state_loaded();assert(!b->finish(view(),cpu,witness));continue;}
  O::state_loaded();const auto first=a->finish(view(),cpu,witness);assert(first&&first->receipt.generation>original.receipt.generation);
  if(mode==6){parked=false;assert(!b->finish(view(),cpu,witness));parked=true;continue;}
  if(mode==7){group->close();assert(!b->finish(view(),cpu,witness));b->invalidate();continue;}
  if(mode==8){std::thread other([&]{assert(!b->finish(view(),cpu,witness));});other.join();}
  const auto second=b->finish(view(),cpu,witness);assert(second&&second->control==first->control&&second->receipt==first->receipt);assert(O::snapshot()->receipt==first->receipt&&!a->finish(view(),cpu,witness)&&!b->finish(view(),cpu,witness));group->close();
 }
}
