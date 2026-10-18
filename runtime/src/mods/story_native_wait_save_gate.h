// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_session.h"
#include "savestate_native_gate.h"
#include <optional>

namespace story_native_wait_save_gate {
// Snapshot-only preflight for the never-exposed bootstrap allocation. The
// caller must separately hold the Session fence and compare its live journal.
std::optional<story_native_wait_journal::Record> saved_bootstrap(
    const story_native_event_arena::Layout&, const ss::native_gate::SavedView&);
// The bootstrap session has never exposed its arena to native consumers. This
// gate admits only that narrow clean state. A future published WAIT transaction
// needs a separate release certificate before it can be captured or restored.
class Controller final : public ss::native_gate::Controller {
    story_native_wait_session::Session& session_;
    std::optional<story_native_wait_journal::Record> pending_load_;

    struct Guard final : ss::native_gate::Hold {
        Controller& owner;
        std::unique_ptr<story_native_wait_session::Session::Fence> fence;
        bool loading;
        Guard(Controller&, std::unique_ptr<story_native_wait_session::Session::Fence>, bool);
        ~Guard() override;
    };
public:
    explicit Controller(story_native_wait_session::Session& session) : session_(session) {}
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    std::unique_ptr<ss::native_gate::Hold> begin_save() override;
    std::unique_ptr<ss::native_gate::Hold> begin_load(const ss::native_gate::SavedView&) override;
    bool finish_load(const ss::native_gate::SavedView&) override;
    std::unique_ptr<ss::native_gate::Hold> begin_progress_load() override;
};
} // namespace story_native_wait_save_gate
