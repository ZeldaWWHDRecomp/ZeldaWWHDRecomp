// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_save_gate.h"
#include "story_native_wait_preflight.h"
#include <algorithm>
#include <array>

namespace story_native_wait_save_gate {
using Record = story_native_wait_journal::Record;
using Layout = story_native_event_arena::Layout;

// A saved journal alone cannot establish a clean native scene. This accepts
// only a snapshot of the bootstrap arena, before any image or native pointer
// was written. Every byte comes from SavedView; never fall back to live EA.
std::optional<Record> saved_bootstrap(const Layout& layout,
                                      const ss::native_gate::SavedView& view) {
    const auto result = story_native_wait_preflight::extract(layout, view.allocations,
                                                            view.regions, view.chunks);
    if (result.status != story_native_wait_preflight::Status::Extracted ||
        result.journal.phase != story_native_wait_journal::Phase::Unpublished)
        return {};
    std::array<uint8_t, story_native_event_arena::kBytes> bytes{};
    if (!story_native_event_arena::read_saved(layout, view.allocations, view.regions,
                                              view.chunks, layout.allocation.address, bytes))
        return {};
    if (!std::all_of(bytes.begin() + story_native_wait_journal::kBytes, bytes.end(),
                     [](uint8_t byte) { return byte == 0; }))
        return {};
    return result.journal;
}
Controller::Guard::Guard(Controller& owner_,
                         std::unique_ptr<story_native_wait_session::Session::Fence> fence_,
                         bool loading_)
    : owner(owner_), fence(std::move(fence_)), loading(loading_) {}

Controller::Guard::~Guard() {
    if (loading) owner.pending_load_.reset();
}

std::unique_ptr<ss::native_gate::Hold> Controller::begin_save() {
    auto fence = session_.try_fence();
    if (!fence || pending_load_ || !session_.unexposed()) return {};
    return std::make_unique<Guard>(*this, std::move(fence), false);
}

std::unique_ptr<ss::native_gate::Hold> Controller::begin_load(
    const ss::native_gate::SavedView& view) {
    auto fence = session_.try_fence();
    if (!fence || pending_load_ || !session_.unexposed()) return {};
    const auto current = session_.journal();
    const auto saved = saved_bootstrap(session_.arena(), view);
    // Bootstrap never changes generation or serial. Equality also binds the
    // snapshot to the same arena and committed definition hash.
    if (!current || !saved || *saved != *current || !session_.begin_restore()) return {};
    pending_load_ = *saved;
    return std::make_unique<Guard>(*this, std::move(fence), true);
}

bool Controller::finish_load(const ss::native_gate::SavedView& view) {
    if (!session_.fence_held() || !pending_load_) return false;
    const auto saved = saved_bootstrap(session_.arena(), view);
    if (!saved || *saved != *pending_load_ || !session_.finish_restore(*pending_load_)) {
        session_.revoke_unexposed();
        pending_load_.reset();
        return false;
    }
    pending_load_.reset();
    return true;
}

std::unique_ptr<ss::native_gate::Hold> Controller::begin_progress_load() {
    auto fence = session_.try_fence();
    if (!fence || pending_load_ || !session_.unexposed()) return {};
    session_.begin_progress_load();
    return std::make_unique<Guard>(*this, std::move(fence), false);
}
} // namespace story_native_wait_save_gate
