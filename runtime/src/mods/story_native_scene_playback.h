// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_activation.h"
#include "story_native_scene_completion.h"
#include "story_native_scene_coverage.h"
#include "story_scenes_core.h"
#include <functional>
namespace story_native_scene_checkpoint {class PreparedRestore;class PreparedRetire;}
namespace story_scenes {class NativeCheckpointIssuer;void tick(Cpu*,bool,unsigned);}
namespace story_native_scene_playback {
struct Progress {
 enum class Phase {Running,Retired,Quarantined};
 Phase phase=Phase::Running;
 std::optional<story_scenes::StopReason>reason;
 std::optional<story_native_scene_completion::Completion>natural;
};
class Player;class RunOwnerCell;
// The actual runtime retains its Context/Session independently of this host
// handle. poll never manufactures guest ticks; queue_stop is host-only.
class Run final {
 std::shared_ptr<const Activation>activation_;
 std::function<Progress(Cpu*)>poll_;
 std::function<void(story_scenes::StopReason)>queue_stop_;
 std::shared_ptr<RunOwnerCell>owner_;
 Run(std::shared_ptr<const Activation>,decltype(poll_),decltype(queue_stop_),std::shared_ptr<RunOwnerCell> = {});
 friend class story_native_wait_runtime::NativeScenePlaybackIssuer;friend class Player;friend class story_scenes::NativeCheckpointIssuer;
public:
 const auto&activation()const noexcept{return activation_;}
};
class Attempt final {
public:
 enum class Kind {WholeFallback,Owned,Denied,Deferred};
 Attempt(Attempt&&)noexcept=default;Attempt&operator=(Attempt&&)noexcept=default;
 Attempt(const Attempt&)=delete;Attempt&operator=(const Attempt&)=delete;
private:
 Kind kind_;std::unique_ptr<Run>run_;
 Attempt(Kind,std::unique_ptr<Run> = {});
 friend class story_native_wait_runtime::NativeScenePlaybackIssuer;friend class Player;friend class story_scenes::NativeCheckpointIssuer;
};
class Backend final {
public:
 using Start=std::function<Attempt(Cpu*,const std::string&,const story_notebook::ManifestScene&)>;
private:
 // Sole production caller is the actual runtime issuer during committed
 // startup. Once installed it cannot be replaced, removed or publicly enabled.
 static bool install(Start);
 friend class story_native_wait_runtime::NativeScenePlaybackIssuer;
};
enum class StartResult {WholeFallback,Owned,Denied,Deferred};
inline constexpr unsigned kNativeDeferredAttempts=60;
enum class TickResult {Active,Finished,Stopped,Failed};
class Player final {
 std::unique_ptr<Run>run_;bool blocked_=false,busy_=false,stop_sent_=false;
 uint64_t generation_=0;std::optional<story_scenes::StopReason>reason_,post_load_stop_;
 std::optional<uint64_t>load_generation_,reconciled_generation_;
 std::string mod_;story_native_scene_coverage::Coverage coverage_;
 std::function<bool(const std::string&,const std::vector<story_notebook::StoryAction>&)>finish_;
 void send_stop()noexcept;
 bool consume_load_reconciliation(uint64_t generation)noexcept;
 friend void story_scenes::tick(Cpu*,bool,unsigned);
 friend class story_scenes::NativeCheckpointIssuer;
 friend class story_native_scene_checkpoint::PreparedRestore;friend class story_native_scene_checkpoint::PreparedRetire;
 void swap_host(Player&)noexcept;
public:
 Player()=default;Player(const Player&)=delete;Player&operator=(const Player&)=delete;
 StartResult start(Cpu*,const std::string&,const story_notebook::ManifestScene&,uint64_t,
  decltype(finish_));
 TickResult tick(Cpu*,uint64_t);
 void request_stop(story_scenes::StopReason)noexcept;
 void state_loaded(uint64_t generation)noexcept; // stop only; never resumes ownership
 bool active()const noexcept{return bool(run_)||blocked_;}
 bool owns(const Activation&a)const noexcept{return run_&&run_->activation_.get()==&a;}
 const auto&stop_reason()const noexcept{return reason_;}
};
}
