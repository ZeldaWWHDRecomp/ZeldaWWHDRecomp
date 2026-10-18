// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include "story_native_manager_terminal.h"
#include <atomic>
#include <functional>
#include <mutex>
struct Cpu;
namespace story_native_camera_return {
using Identity=story_native_event_lease::Identity;
using Camera=story_native_event_observation::Camera;
// Copied at an exact original Run or idle controller boundary. The source must independently
// verify the retained allocation, camera PID, native tables and owner thread.
struct Capture {
 Identity identity{};Camera camera{};
 uint32_t event_state=~0u,camera_play=~0u;
 uint64_t terminal_serial=0;
 bool retained=false,exclusive=false,foreign=false;
};
struct Sources {
 std::function<bool(const Cpu*,Capture&)>capture;
 std::function<bool(const Camera&,const Identity&)>verify_camera;
 std::function<bool(const Cpu*)>owned_thread;
 // Must certify the exact native Run original wrapper, never a replacement.
 std::function<bool()>route;
};
enum class Decision {Delegate,Observed,Quarantined};
// Wait for the real native camera Run to consume the saved Event return type.
// No camera writes, hook installation, manager detach or image-free authority.
// The owning controller keeps the image retained throughout this barrier.
class Barrier {
 Identity expected_{};Camera baseline_{};Sources sources_{};
 std::atomic<uint64_t> terminal_serial_{0};
 std::atomic<bool> quarantined_{false},returned_{false},running_{false};
 std::mutex arm_mutex_;
 story_native_manager_terminal::Completion completion_{};
 bool check(const Capture&)const;
public:
 Barrier(Identity,Camera,Sources);
 bool arm(const story_native_manager_terminal::Completion&);
 // Delegate never calls original. Every other decision handles this entry;
 // Quarantined may follow a partial/completed original and must never replay it.
 Decision run(Cpu*,uint32_t function,const std::function<void()>& original);
 bool returned()const{return returned_&&!quarantined_;}
 bool quarantined()const{return quarantined_;}
 bool matches(const story_native_event_observation::Expected&e)const{
  return expected_==e.identity&&baseline_==e.camera;
 }
 bool matches_terminal(const story_native_manager_terminal::Terminal&)const;
 // Fresh independent capture under the controller's idle entry fence,
 // immediately before detach. Reject camera re-entry after a prior return.
 bool verify_fresh(const Cpu*,const Identity&,const Camera&)const;
};
}
