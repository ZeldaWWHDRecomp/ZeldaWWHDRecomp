// Host-side scene lifecycle. No guest pointers or game save bits are stored here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
#include <algorithm>
#include <deque>
#include <optional>

namespace story_scenes {
enum class StopReason { Finished, Skipped, Aborted, StateLoaded, Interrupted, Failed, TimedOut };
enum class StepResult { Running, Complete, Failed };
struct Timing { unsigned frames = 0; bool terminal = false; std::vector<size_t> successors{}; };
// Explicit edges form a forward DAG. All declared steps must be reachable and all
// paths must end, so an unselected branch can never hide a dead end or overlong path.
inline bool valid_graph(const std::vector<Timing>& steps) {
    if(steps.empty()||steps.size()>64)return false;
    std::vector<unsigned> longest(steps.size());std::vector<bool> reachable(steps.size());reachable[0]=true;
    for(size_t n=steps.size();n-->0;){const auto& step=steps[n];
        if(step.terminal){if(!step.successors.empty()||step.frames>18000)return false;longest[n]=step.frames;continue;}
        if(!step.frames||step.frames>18000||step.successors.size()>2)return false;
        const auto edges=step.successors.empty()?std::vector<size_t>{n+1}:step.successors;
        if(edges.size()==2&&edges[0]==edges[1])return false;
        unsigned remaining=0;for(size_t next:edges){if(next<=n||next>=steps.size())return false;remaining=std::max(remaining,longest[next]);}
        if(remaining>18000-step.frames)return false;longest[n]=step.frames+remaining;
    }
    for(size_t n=0;n<steps.size();++n)if(reachable[n]&&!steps[n].terminal){if(steps[n].successors.empty())reachable[n+1]=true;else for(size_t next:steps[n].successors)reachable[next]=true;}
    return std::all_of(reachable.begin(),reachable.end(),[](bool b){return b;});
}

// Values are immutable declaration indices, not actor pointers. Room/generation
// policy belongs to the native adapter; clear() drops every queued request on load.
class RequestQueue {
    std::deque<size_t> pending_;
public:
    bool push(size_t scene,std::optional<size_t> active={}){if(pending_.size()>=16||(active&&*active==scene)||std::find(pending_.begin(),pending_.end(),scene)!=pending_.end())return false;pending_.push_back(scene);return true;}
    std::optional<size_t> pop(){if(pending_.empty())return {};const size_t scene=pending_.front();pending_.pop_front();return scene;}
    std::optional<size_t> front()const{if(pending_.empty())return {};return pending_.front();}
    template<class Predicate> void discard(Predicate predicate){pending_.erase(std::remove_if(pending_.begin(),pending_.end(),predicate),pending_.end());}
    void clear(){pending_.clear();}
    size_t size()const{return pending_.size();}
};

// The owner must verify every effect before acquiring event ownership. The engine never treats
// an unimplemented effect as a successful step. release() must restore camera/control/message
// state for every stop reason, including a guest-memory generation change.
class Driver {
public:
    virtual ~Driver() = default;
    virtual bool acquire() = 0;
    virtual bool enter(size_t step) = 0;
    virtual StepResult update(size_t step, unsigned elapsed) = 0;
    virtual size_t successor(size_t step) { return step+1; } // chosen only after Complete; must match a validated edge
    virtual bool finish() = 0; // apply mod-owned end actions; never on abort/skip
    virtual void release(StopReason reason) = 0;
};

class Player {
public:
    Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;
    bool start(std::vector<Timing> steps, Driver& driver, uint64_t generation, bool game_event_running) {
        if (active_ || stopping_ || game_event_running || steps.empty() || steps.size() > 64 || !steps.back().terminal) return false;
        const bool branched=std::any_of(steps.begin(),steps.end(),[](const Timing& t){return !t.successors.empty();});
        if(!branched)for(size_t i=0;i<steps.size();++i)if(steps[i].terminal!=(i+1==steps.size()))return false;
        if(!valid_graph(steps)||!driver.acquire())return false;
        steps_ = std::move(steps);
        driver_ = &driver;
        generation_ = generation;
        index_ = elapsed_ = total_ = 0;
        active_ = true;
        if (!driver_->enter(0)) stop(StopReason::Failed);
        return active_;
    }
    void tick(uint64_t generation, bool interrupted = false, bool skip = false) {
        if (!active_) return;
        if (generation != generation_) return stop(StopReason::StateLoaded);
        if (interrupted) return stop(StopReason::Interrupted);
        if (skip) return stop(StopReason::Skipped);
        const Timing& step = steps_[index_];
        if (step.terminal) {
            const bool ok = driver_->finish();
            return stop(ok ? StopReason::Finished : StopReason::Failed);
        }
        if (++total_ > 18000) return stop(StopReason::TimedOut);
        const StepResult result = driver_->update(index_, ++elapsed_);
        if(!active_)return; // callbacks may abort their scene
        if (result == StepResult::Failed) return stop(StopReason::Failed);
        // Durations are deadlines: dialogue/movement that fails to complete aborts instead of
        // advancing and silently applying its rewards. A wait backend completes at its duration.
        if (result == StepResult::Running) {
            if (elapsed_ >= step.frames) stop(StopReason::TimedOut);
            return;
        }
        const size_t next=driver_->successor(index_);
        if(!active_)return;
        const bool allowed=step.successors.empty()?next==index_+1:std::find(step.successors.begin(),step.successors.end(),next)!=step.successors.end();
        if(!allowed||next>=steps_.size())return stop(StopReason::Failed);
        index_=next;
        elapsed_ = 0;
        if (!driver_->enter(index_)) stop(StopReason::Failed);
    }
    void stop(StopReason reason = StopReason::Aborted) {
        if (!active_) return;
        active_ = false;
        Driver* driver = driver_;
        driver_ = nullptr;
        last_ = reason;
        stopping_=true;driver->release(reason);stopping_=false;
        steps_.clear();
    }
    bool active() const { return active_; }
    size_t step() const { return index_; }
    unsigned elapsed() const { return elapsed_; }
    StopReason last_reason() const { return last_; }
private:
    std::vector<Timing> steps_;
    Driver* driver_ = nullptr;
    uint64_t generation_ = 0;
    size_t index_ = 0;
    unsigned elapsed_ = 0, total_ = 0;
    bool active_ = false,stopping_=false;
    StopReason last_ = StopReason::Aborted;
};
} // namespace story_scenes
