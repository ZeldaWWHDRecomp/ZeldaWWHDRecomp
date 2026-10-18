// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_setinfo_candidate.h"
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace story_native_stage_binding {class Cell;}
namespace story_native_stage_owner {
// Exact current-thread ownership of counted Scan/RestorePermit guards, including
// nested/moved guards. Bare lifetime_fence() locks are not covered; callers must
// not acquire physical quiescence while holding those either.
bool current_thread_holds_lifetime_pin() noexcept;
namespace detail {void acquire_lifetime_pin()noexcept;void release_lifetime_pin()noexcept;}

struct Receipt {
    // Keep the complete observed owner graph. Teardown observers must match
    // the recorded request heap as well as Info/archive identities.
    story_native_setinfo_candidate::Candidate owner{};
    uint32_t generation{};
    bool operator==(const Receipt&) const = default;
};

// Synchronizes short native entry/exit observations with a whole read-only
// event-table scan. Native bodies never hold the host mutex: any body in
// flight instead blocks publication and scans until its matching exit.
class Tracker {
public:
    using Verify = std::function<std::optional<Receipt>()>;
    // Like unique_lock, Scan moves/destruction belong to its issuing thread.
    class Scan {
    public:
        Scan(Scan&&) noexcept;
        Scan& operator=(Scan&&) noexcept;
        ~Scan();
        Scan(const Scan&) = delete;
        Scan& operator=(const Scan&) = delete;
        bool valid() const { return valid_&&thread_==std::this_thread::get_id(); }
        const Receipt& receipt() const { return receipt_; }
        // Re-read the tracker under this guard, for an inspect Query callback.
        bool current() const;
        bool holds_lifetime_of(const Tracker&)const noexcept;
    private:
        friend class Tracker;
        friend class story_native_stage_binding::Cell;
        Scan(Tracker* owner, std::unique_lock<std::recursive_mutex>&& lock,
             bool valid, Receipt receipt)
            : owner_(owner), lock_(std::move(lock)), valid_(valid), receipt_(receipt),thread_(std::this_thread::get_id()),pin_counted_(lock_.owns_lock()) {if(pin_counted_)detail::acquire_lifetime_pin();}
        Tracker* owner_{};
        std::unique_lock<std::recursive_mutex> lock_;
        bool valid_{};
        Receipt receipt_{};std::thread::id thread_;bool pin_counted_=false;
    };

    // A real retained Scan, never a copied Receipt, issues this one-use host
    // continuation. It retains the actual lifetime mutex through one full
    // restore on the SAME host thread; no native call or guest write occurs.
    class RestorePermit {
        friend class Tracker;
        Tracker* owner_{};Receipt receipt_{};
        uint64_t native_epoch_{},restore_epoch_{};uint32_t serial_{};
        bool consumed_=false;std::thread::id thread_;
        std::unique_lock<std::recursive_mutex> lock_;bool pin_counted_=false;
        RestorePermit(Tracker*,Receipt,uint64_t,uint64_t,uint32_t,
                      std::unique_lock<std::recursive_mutex>&&,bool);
    public:
        RestorePermit(const RestorePermit&)=delete;RestorePermit&operator=(const RestorePermit&)=delete;
        // Like the retained recursive mutex, moves/destruction are bound to
        // the issuing thread. Violating that lifetime contract fails fast.
        RestorePermit(RestorePermit&&) noexcept;RestorePermit&operator=(RestorePermit&&) noexcept;
        ~RestorePermit();
        bool same_thread()const noexcept{return thread_==std::this_thread::get_id();}
    };
    std::optional<RestorePermit> prepare_restore(Scan&&);
    // Exact one state_loaded(), no intervening native body/fault. Independent
    // restored verification must return the ORIGINAL complete owner graph.
    // Storage/candidate bytes alone cannot construct a permit or issue a receipt.
    std::optional<Receipt> finish_restore(RestorePermit&&,const Verify&);

    // Every observed SetInfo/destructive native body starts with begin_native
    // under the short hook entry fence. Its cookie must be passed exactly once
    // to finish_native or finish_setinfo on every return/exception path.
    // Begin revokes any old receipt before the original body may mutate it.
    uint64_t begin_native() noexcept;
    void finish_native(uint64_t cookie) noexcept;
    // Runs the independent live verifier while holding the short exit fence.
    // A receipt is issued only if this is the sole in-flight body, no state
    // restore occurred since entry, and the complete final graph verifies.
    // Completed nested native mutations are allowed: their final effects are
    // covered by that fresh verification.
    bool finish_setinfo(uint64_t cookie, const Verify& verify) noexcept;
    // Observer callback failures quarantine the tracker permanently.
    void fault() noexcept;
    // The hook holds this only around entry and exit callbacks, never across
    // a native body or guest wait. Recursive locking accommodates callbacks
    // that invoke the Tracker's own synchronized methods.
    std::recursive_mutex& lifetime_fence() noexcept { return mutex_; }
    // Called after full/progress state restore, before any native publication.
    // Restored guest pointers alone cannot rebuild a host lifetime receipt.
    void state_loaded();
    Scan scan();
    // For globally parked callers: the parked owner may hold the Stage mutex.
    // Busy returns an invalid guard immediately; never wait while holding Q.
    Scan try_scan();
    bool current(const Receipt& expected) const;
private:
    friend class story_native_stage_binding::Cell;
    void revoke(); // mutex already held
    bool issue_locked(const Receipt& verified);
    bool retire_locked(uint64_t cookie) noexcept;
    struct InFlight {uint64_t cookie{}, restore_epoch{};};
    mutable std::recursive_mutex mutex_;
    Receipt active_{};
    uint32_t serial_{};
    uint64_t epoch_{};
    uint64_t restore_epoch_{};
    std::array<InFlight, 16> in_flight_{};
    uint8_t in_flight_count_{};
    bool disabled_{};
};
} // namespace story_native_stage_owner
