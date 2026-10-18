// Esc and a platform layer's actions (extensions.h InputLayer::cancel; the world editor's placing, handle
// drag and quest connection). Esc opens Settings; with a captured mouse the first Esc frees the mouse
// (overlay.cpp); next, while the layer has an action going, the first Esc cancels that action and is
// taken; only an Esc with nothing to cancel opens Settings.
#pragma once
#include <atomic>
#include <functional>

namespace overlay {

// Input thread (overlay::key): the Esc events that belong to the layer. A press the layer's cancel took
// keeps its repeats and its release, so the release never reaches the Settings shortcut.
class LayerEscape {
    bool took_ = false;

public:
    // true: this Esc event is the layer's (nothing else sees it)
    bool route(bool down, bool repeat, bool layer_active, const std::function<bool()>& cancel) {
        if (took_) {
            if (!down) took_ = false;
            return true;
        }
        if (down && !repeat && layer_active && cancel && cancel()) return took_ = true;
        return false;
    }
};

}  // namespace overlay

namespace editor_escape {

// What the editor's first Esc cancels: the handle drag first (the mouse is down on it), then a quest
// connection being made, then placing new actors.
enum class Cancel { None, Drag, Connect, Placing };
inline Cancel what(bool dragging, bool connecting, bool placing) {
    return dragging ? Cancel::Drag : connecting ? Cancel::Connect : placing ? Cancel::Placing : Cancel::None;
}

// Between the render thread (which knows what is going on) and the input thread (which gets the Esc):
// the render thread publishes what an Esc would cancel every frame and carries out a taken request on its
// next frame; the input thread's take() answers at once and never waits.
class Gate {
    std::atomic<int> going_{0}, request_{0};

public:
    void publish(Cancel c) {
        if (request_.load() == 0) going_.store(int(c));  // a taken Esc waits for its frame: nothing new to take
    }
    bool take() {  // input thread: true when there was something to cancel
        const int c = going_.exchange(0);
        if (c == 0) return false;
        request_.store(c);
        return true;
    }
    Cancel requested() { return Cancel(request_.exchange(0)); }  // render thread: once per taken Esc
};

}  // namespace editor_escape
