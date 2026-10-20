// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <deque>
#include <string>
namespace story_world_arrival {
inline constexpr unsigned kMaxPending=8,kMaxTextBytes=96;
// Startup only, before overlay extensions seal. Dormant unless show() queues text. main() registers it
// only when declared() (or with --editor), so a start without an arrival adds no overlay layer.
void register_overlay();
bool registered();
// A loaded actor declares an arrival (story_actors, at start-up or a live edit). Any thread.
void note_declared();
bool declared();
// Any thread. Plain UTF-8, one line; duration 1..10 seconds. False leaves the queue unchanged.
bool show(const std::string& text,float seconds=4);
void state_loaded();
// Deterministic bounded queue; externally synchronized by the service above.
class Queue {
public:
    bool push(const std::string& text,float seconds=4);
    std::string current(double monotonic_seconds);
    void clear();
private:
    struct Notice {std::string text;float seconds;};
    std::deque<Notice> pending_;
    std::string active_;
    double expires_=0;
};
}
