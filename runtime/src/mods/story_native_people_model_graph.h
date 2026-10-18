// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_people_cancel_refresh.h"
#include "story_native_people_pose_capture.h"
#include <optional>
#include <memory>
#include "story_native_refresh_dispatch_guard.h"
#include "native_quiescence.h"
#include "story_native_model_saved_graph.h"
namespace story_native_wait_runtime_model_graph {class Context;}
namespace story_native_people_model_graph {
namespace R=story_native_people_cancel_refresh;
using Expected=R::Expected;
struct Scope {
 Expected expected{};
 bool fenced=false,stage_live=false,resources_pinned=false,preclaim=false;
 std::shared_ptr<const story_native_refresh_dispatch::Guard> native_dispatch;
 std::shared_ptr<const threads::NativeQuiescence> native_quiescence;
 const Cpu* native_cpu=nullptr;
};
struct Sources {
 story_native_people_pose_capture::Read read;
 story_native_people_pose_capture::VerifyActor resident;
 // Actual Session/resource/logic fence, current Stage and native dispatch owner
 // checks. Read-only reservation; cannot be reconstructed from saved records.
 std::function<bool(Scope&)> scope;
};
struct Capture {
 R::ModelIdentity model{};R::Transform transform{};R::Animation animation{};
 uint32_t root_calc{},mtx_calc{};
};
enum class Refusal {Scope,Actor,ActorHeap,Morf,Model,ResourceRegistry,ResourceHeap,CacheRegistry,Skeleton,JointDescriptors,AnimationResource,AnimationPayload,Sampler,WorkSpans,Changed,Complete};
enum class Check {None,Read,ControlVtable,PoolStorage,PoolCapacity,PoolCount,NodeRange,NodeCycle,NodeVtable,NodeOwner,NamePointer,NameVtable,NameCapacity,DuplicateArchive,ArchiveCount,ArchiveMissing,IdCount,BodyIdentity,ModelPayload,ModelDictionary,SkeletonRelative,SkeletonPayload,SkeletonCount,MatrixSkeleton,MatrixRecords,MatrixOutput,MatrixPrevious,MatrixPrefetch,CalcMatrixPointer,MatrixScale,BoneRelative,BonePointer,BonePayload,DescriptorBone,BoneId,BoneParent,MorfBoneCount,MorfMatrix,MorfWork,MorfOutput,MorfCache,AnimationPayload,AnimationDictionary,AnimationRotation,AnimationGetterKind,AnimationGetter,AnimationEntryKind,AnimationEntry,DispatchWord,AnimationSecondary,AnimationBoneCount,AnimationCurveCount,AnimationTracks,AnimationSkeleton,AnimationMorfSkeleton,AnimationBinding,AnimationDirectBinding,AnimationFrame,AnimationCache,AnimationAssets,AnimationBindIndex,AnimationMapIndex,AnimationCacheSpan,AnimationAddress,AnimationRegistry,AnimationWrapperRange,AnimationVtable,AnimationTrackPointer,AnimationTrackCount,AnimationModelCount,AnimationMapCapacity,SamplerChannels,SamplerCurves,SamplerConstants,SamplerConstantFinite,SamplerLayout,SamplerFinite,SamplerFrameRange,SamplerPayload,SamplerValueFinite,SamplerKeyPayload,SamplerKeyOrder,SamplerKeyRange,SamplerCacheIndex,SamplerCacheWindow,SamplerCount,NpcDatPointer,NpcDatConstants,WorkRange,WorkActorAlias,WorkOverlap,WorkHeapOverlap,WorkImmutableAlias};
struct Range {uint32_t address{},bytes{};};
// Passive bounded observations only; no conversion to native-call authority.
struct Diagnostic {
 Check check=Check::None;uint32_t checked_address{},observed{},required{},read_bytes{};
 uint32_t sampler_flags{},sampler_keys{},animation_end{};
 unsigned actor_index{};Refusal refusal=Refusal::Scope;bool parsed=false;
 uint32_t actor{},morf{},model{},model_data{},fmdl{},skeleton{},animation{},bck{},archive_info{},archive{},fres{},cache_holder{},cache_resource{},parent_heap{},data_heap{};
 Range actor_heap{},metadata{},payload_heap{},cache_buffer{};uint32_t cache_size{};bool cache_membership=false;
};
// Reads the actual native resource registry, solid heap and concrete callback
// graph. Sampler payloads and matrix destination spans are bounded; independent
// downstream dispatch, alias and stack checks guard the private refresh path.
// No native calls, writes, arbitrary executable-address policy or caller
// graph-verifier callback. This establishes no lifetime/release authority.
class Adapter {
 struct Graph {
  Capture capture;story_native_model_saved_graph::Trace trace;
  std::vector<uint32_t> identity;
  std::vector<uint8_t> descriptors,bones,animation_payload;
 };
 Expected expected_;Sources sources_;std::array<std::optional<Graph>,2>baseline_{};
 // Only the host ModelContext may replace live observation sources while
 // preserving the original preclaim baselines. Saved bytes never prepare one.
 std::unique_ptr<Adapter>clone_sources(Sources)const;
 friend class story_native_wait_runtime_model_graph::Context;
 bool sample(unsigned,Graph&,Diagnostic* =nullptr,bool invocation=true)const;
 bool collect(unsigned,Graph&)const;
public:
 Adapter(Expected,Sources);
 static constexpr bool publication_enabled(){return false;}
 static constexpr bool refresh_admission_enabled(){return true;}
 Diagnostic diagnose(unsigned)const; // no baseline publication; skips only dispatch-owner requirement
 bool prepare(); // actual preclaim native baselines only; all-or-nothing
 bool capture(unsigned,Capture&)const;
 // Fresh bounded parser observations only; no saved-baseline or invocation authority.
 bool trace(unsigned,story_native_model_saved_graph::Trace&)const;
 bool verify_model(const Expected&,unsigned,const R::ModelIdentity&,const R::Transform&)const;
};
}
