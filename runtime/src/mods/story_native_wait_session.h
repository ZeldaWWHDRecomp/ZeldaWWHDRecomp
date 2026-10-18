// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_readiness.h"
#include "story_native_wait_journal_store.h"
#include "story_native_event_depth.h"
#include "story_native_people_pose_record.h"
#include "story_native_wait_saved_baseline.h"
#include "story_native_wait_session_idle.h"
#include "story_native_wait_session_pristine.h"
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace story_native_wait_controller { class Controller; }
namespace story_native_wait_active_save_gate {class Controller;}
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_wait_runtime_model_graph {class Context;}
namespace story_native_restore_resume {class Boundary;class CurrentLoadOwner;}
namespace threads {class NativeQuiescence;class QuiescenceWitness;}
namespace story_native_cancel_observation { class Result; }
namespace story_native_wait_session {
// Startup/readiness and private image staging. This object owns its arena but
// has no native manager/actor/camera lease. A controller must supply those
// proofs before it may bind an Install backend or publish an event.
class Session {
public:
    using Request = story_native_wait_readiness::Request;
    using Sources = story_native_wait_readiness::Sources;
    using Expected = story_native_event_observation::Expected;
    using Layout = story_native_event_arena::Layout;
    using Record = story_native_wait_journal::Record;
    using Check = story_native_wait_readiness::Check;

    // Issued only by a genuinely released Controller. Copies/decoded results
    // cannot create this one-use, live issuer capability.
    class RearmCertificate {
        friend class Session;
        friend class story_native_wait_controller::Controller;
        struct Seal {
            std::mutex mutex;
            std::atomic<bool> live{true};
            bool consumed=false;
            const Session* owner=nullptr;
            Expected expected;
            Record journal;
            std::function<bool(Cpu*)> verify;
        };
        std::shared_ptr<Seal> seal_;
        explicit RearmCertificate(std::shared_ptr<Seal> s):seal_(std::move(s)){}
    public:
        RearmCertificate(const RearmCertificate&)=delete;
        RearmCertificate& operator=(const RearmCertificate&)=delete;
        RearmCertificate(RearmCertificate&&)=default;
        RearmCertificate& operator=(RearmCertificate&&)=default;
    };

    class Fence {
        friend class Session;
        const Session* owner_;
        Fence* prior_;
        Fence* next_=nullptr;
        std::unique_lock<std::mutex> lock_;
        std::shared_ptr<const uint8_t>identity_=std::make_shared<const uint8_t>(0);
        Fence(const Session*, std::unique_lock<std::mutex>&&);
    public:
        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;
        ~Fence();
    };

    Session(Request, Sources);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    static constexpr bool publication_enabled() { return false; }

    // This is the actual host entry fence to be shared by future native/save
    // adapters. It never waits behind a guest thread parked for savestate.
    std::unique_ptr<Fence> try_fence();
    bool fence_held() const noexcept;
    bool startup(); // committed declarations, before guest threads start
    // Same immutable request only. Requires the actual held fence; consumes
    // the certificate on every attempt. Never restores bootstrap unexposed.
    bool rearm(RearmCertificate&&,Cpu*);
    // Genuine post-release idle snapshots; never restore bootstrap unexposed.
    std::optional<IdleCapture>begin_idle_capture(Cpu*,const threads::QuiescenceWitness&);
    std::optional<IdleCheckpoint>commit_idle_capture(IdleCapture&&,Cpu*,const threads::QuiescenceWitness&,
        std::unique_ptr<Fence>*retain_fence=nullptr);
    std::optional<IdleRestore>prepare_idle_restore(const IdleCheckpoint&,const ss::native_gate::SavedView&,
        Cpu*,const threads::QuiescenceWitness&,uint64_t host_epoch,
        std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner={});
    bool adopt_idle(IdleRestore&&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,
        std::unique_ptr<Fence>*retain_fence=nullptr);
    // Captured only under an existing genuine bootstrap SaveHold fence.
    std::optional<PristineCapture>begin_pristine_capture(Cpu*,const threads::QuiescenceWitness&);
    std::optional<PristineCheckpoint>commit_pristine_capture(PristineCapture&&,Cpu*,const threads::QuiescenceWitness&);
    std::optional<PristineRestore>prepare_pristine_restore(const PristineCheckpoint&,const ss::native_gate::SavedView&,
        Cpu*,const threads::QuiescenceWitness&,uint64_t host_epoch,
        std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner={});
    bool adopt_pristine(PristineRestore&&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,
        std::unique_ptr<Fence>*retain_fence=nullptr);
    bool inspect(Cpu*); // designated logic thread, read-only live readiness

    // All accessors require this thread's Fence. journal() additionally reads
    // and compares the current guest header against the Store receipt.
    std::optional<Record> journal() const;
    std::optional<Expected> expected() const;
    Layout arena() const;
    uint32_t generation() const;
    Check readiness_check() const; // diagnostic only; requires Fence
    std::vector<uint8_t> canonical_image() const;
    // Prepare SDK-owned bytes only: persist Preparing before touching the
    // private image/stamp/baseline. No native pointer, consumer, or lease is
    // changed. A staged baseline is data, not live publication authority.
    bool stage_image(); // WAIT only; requires Fence and a successful inspect
    // TURN requires both actual pre-write poses, embedded in its distinct full
    // record. No pending or overlapping pose side-record is ever staged.
    bool stage_image(const story_native_people_pose_record::Poses&);
    std::optional<Expected> staged_expected() const; // exact storage readback
    // Serialized observation only; a valid idle record is not entry-fence or
    // publication authority. The controller must authenticate its live Tracker.
    bool stage_depth(const story_native_event_depth::Record&);
    static constexpr uint32_t kPoseRecordOffset=0xA00;
    bool stage_poses(const story_native_people_pose_record::Poses&);
    std::optional<story_native_people_pose_record::Poses> staged_poses() const;
    // These copied validation results are necessary data checks, not native
    // ownership/freshness authority. Caller must retain its actual claim,
    // authenticated live collector and this entry Fence across every mutation.
    bool begin_publication(const story_native_event_observation::Result& prepare);
    std::optional<Expected> baseline() const;
    std::optional<story_native_people_pose_record::Poses> baseline_poses() const;
    bool transition_native(story_native_wait_journal::Phase,
                           const story_native_event_observation::Result&);
    bool persist_depth(const story_native_event_depth::Record&); // idle data receipt
    bool quarantine_native(); // retain allocation; persist Quarantined if possible
    // Bootstrap-only invariant: this Session has never started native/image
    // mutation, its journal is current Unpublished, and all other arena bytes
    // remain zero. It is not a general manager/consumer ownership receipt.
    bool unexposed() const;
    // Mandatory before a future controller's first image or native write.
    // The bootstrap invariant never becomes true again through this path.
    void revoke_unexposed();
    // Called under a single retained Fence spanning full-state restore.
    // Candidate admission is cleared before the first restored guest byte.
    bool begin_restore();
    bool finish_restore(const Record& saved);
    // Portable progress load has no snapshot journal: permanently revoke.
    void begin_progress_load();
    bool quarantined() const;
private:
    std::shared_ptr<const uint8_t>current_fence_identity()const noexcept;
    bool pristine_shape()const;
    friend class story_native_wait_active_save_gate::Controller;
    bool initial_import_roots(const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&)const;
    std::shared_ptr<const void>pristine_lineage_;
    uint64_t pristine_restore_epoch_=0,pristine_pending_epoch_=0;
    friend class PristineRestore;
    friend class story_native_wait_controller::Controller;
    friend class story_native_wait_runtime_controller::Context;
    friend class story_native_wait_runtime_model_graph::Context;
    std::shared_ptr<const SavedBaseline>capture_saved_baseline(const story_native_wait_run_footprint::Footprint&,const story_native_event_memory::Read&)const;
    bool adopt_restored(const story_native_restore_resume::Boundary&,const threads::NativeQuiescence&,const story_native_event_memory::Read&);
    bool adopted_restored(const story_native_restore_resume::Boundary&)const;
    bool transition_cancel_native(story_native_wait_journal::Phase,const story_native_cancel_observation::Result&);
    // Only the actual retained Controller may author checkpoint progress. The
    // record is data; it never reconstructs ownership from its own bytes.
    bool checkpoint_native(const story_native_event_observation::Result&,uint32_t elapsed,uint32_t flags,
        const std::function<bool()>& guard,
        const std::function<bool(uint32_t,std::span<const uint8_t>)>& write);
    std::optional<story_native_people_pose_record::Poses> original_poses_;
    bool preparing_admission() const;
    friend class IdleCapture;friend class IdleRestore;
    struct IdleLineage {
        const Session*owner;std::shared_ptr<const uint8_t>host_identity;Record journal;Expected expected;
    };
    std::shared_ptr<const IdleLineage>idle_lineage_;
    Request root_request_;
    Sources idle_sources_;
    uint64_t idle_restore_epoch_=0,idle_pending_epoch_=0;
    bool idle_shape()const;
    std::optional<Expected>idle_inspect(Cpu*,const threads::QuiescenceWitness&);
    bool idle_read(uint32_t,std::span<uint8_t>)const;
    bool snapshot_roots(story_native_wait_run_footprint::Footprint&)const;
    // Data-only fresh root exclusion; usable only under the genuine issuer
    // lifetime/fence. It cannot mint a release or rearm certificate.
    bool clean_rearm_roots()const;
    bool reusable_=false;
    bool stage_image_impl(const story_native_people_pose_record::Poses*);
    std::optional<Expected> checked_baseline(bool native) const;
    static thread_local Fence* active_fence_;
    mutable std::mutex mutex_;
    story_native_wait_readiness::Controller readiness_;
    story_native_wait_journal_store::Backend journal_backend_{};
    std::unique_ptr<story_native_wait_journal_store::Store> journal_store_;
    std::optional<Expected> expected_;
    std::vector<uint8_t> canonical_image_;
    std::optional<Expected> staged_expected_;
    std::vector<uint8_t> staged_image_,staged_record_;
    std::optional<story_native_event_depth::Record> staged_depth_;
    std::vector<uint8_t> staged_pose_bytes_;
    std::shared_ptr<const uint8_t>host_identity_=std::make_shared<const uint8_t>(0);
    uint64_t last_adopt_epoch_=0;
    std::shared_ptr<const SavedBaseline>adopted_baseline_;
    uint32_t generation_{};
    bool attempted_=false, unexposed_=false;
    mutable bool quarantined_=false;
    bool native_mutation_=false, restore_pending_=false, publication_sealed_=false;
};
} // namespace story_native_wait_session
