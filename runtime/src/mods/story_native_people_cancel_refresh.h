// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_people_pose_plan.h"
#include "story_native_camera_restore.h"
#include "story_native_event_invoke.h"
#include <atomic>
namespace story_native_people_cancel_refresh {
using Expected=story_native_event_observation::Expected;
namespace P=story_native_people_pose_plan;
namespace C=story_native_camera_restore;
namespace I=story_native_event_invoke;
// Concrete raw-native roles. This does not encode an authenticated callback
// graph: an actual adapter must independently prove its complete live graph.
struct ModelIdentity {
 uint32_t morf{},model{},user_area{},model_data{},material_table{},joint_tree{},callback{};
 bool operator==(const ModelIdentity&)const=default;
};
struct Transform {
 std::array<uint8_t,12> position{},scale{},model_scale{};
 std::array<uint8_t,48> base{};
 bool operator==(const Transform&)const=default;
};
struct Animation {
 uint32_t address{};
 std::array<uint8_t,16> frame_control{}; // Morf+98, includes current frame/rate
 std::array<uint8_t,12> morph{}; // Morf+B0 current/previous/step
 std::array<uint8_t,4> cached_frame{}; // animation+0, native cache only
};
struct State {
 std::array<P::Snapshot,2> poses{};
 std::array<ModelIdentity,2> models{};
 std::array<Transform,2> transforms{};
 std::array<Animation,2> animations{};
 std::array<uint32_t,2> root_calc{}; // shared ModelData root+14, live native binding
 std::array<uint32_t,2> mtx_calc{}; // model reference+30, native traversal receiver
 C::Boundary boundary{};
 uint32_t current_model{};
};
struct Sources {
 std::function<bool(State&)> capture;
 std::function<bool(const Expected&)> retained;
 // Actual fresh Stage/resource/Session fence, native terminal/camera return,
 // designated logic thread and exclusive quiescent boundary; no saved record.
 std::function<bool(const Cpu*,const Expected&,const C::Boundary&)> boundary;
 // Must prove actual preclaim provenance of model/transform, owned allocations,
 // and the full live executable callback graph, including native material and
 // joint callbacks. Pointer equality or a generic true callback is insufficient.
 std::function<bool(const Expected&,unsigned,const ModelIdentity&,const Transform&)> model;
 std::function<bool(const Cpu*,const I::Config&)> stack;
 I::Read read;I::Write write;I::Call call;
 std::function<void()> quarantine;
};
class Completion {
 Expected expected_{};std::array<P::Snapshot,2> poses_{};C::Boundary boundary_{};bool valid_=false;
 friend class Invoker;
public:
 bool valid()const{return valid_;}
 bool matches(const Expected&,const std::array<P::Snapshot,2>&,const C::Boundary&)const;
};
enum class Result {Refused,Complete,Quarantined};
// Source-adapter-dependent private helper. It cannot establish cancellation,
// detach/image release or publication authority. Never writes guest matrices or
// restores native global currentModel: original calc intentionally changes it.
class Invoker {
 Expected expected_;std::array<P::Snapshot,2> baseline_;C::Boundary boundary_;
 std::array<ModelIdentity,2> models_;std::array<Transform,2> transforms_;
 I::Config stack_;Sources sources_;Completion completion_;
 bool attempted_=false;std::atomic<bool>busy_{false},fault_{false},complete_{false};
 bool capture(Cpu*,State&,const std::array<bool,2>&);
 bool invoke(Cpu*,uint32_t,uint32_t);
 void fail()noexcept;
public:
 Invoker(Expected,std::array<P::Snapshot,2>,C::Boundary,std::array<ModelIdentity,2>,std::array<Transform,2>,I::Config,Sources);
 static constexpr bool publication_enabled(){return false;}
 Result run(Cpu*);
 Completion completion()const{return !complete_.load()||fault_.load()?Completion{}:completion_;}
};
}
