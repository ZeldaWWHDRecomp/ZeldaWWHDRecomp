// SPDX-License-Identifier: MPL-2.0
#include "story_native_stage_owner.h"
#include <algorithm>
#include <limits>
#include <exception>

namespace story_native_stage_owner {
namespace {thread_local size_t lifetime_pins=0;
struct LocalLifetimePin {
 std::unique_lock<std::recursive_mutex>lock;bool counted;
 ~LocalLifetimePin(){if(lock.owns_lock())lock.unlock();if(counted)detail::release_lifetime_pin();}
};}
bool current_thread_holds_lifetime_pin()noexcept{return lifetime_pins!=0;}
void detail::acquire_lifetime_pin()noexcept{if(lifetime_pins==std::numeric_limits<size_t>::max())std::terminate();++lifetime_pins;}
void detail::release_lifetime_pin()noexcept{if(!lifetime_pins)std::terminate();--lifetime_pins;}

Tracker::Scan::Scan(Scan&&other)noexcept{*this=std::move(other);}
Tracker::Scan&Tracker::Scan::operator=(Scan&&other)noexcept{
    if(this==&other)return *this;
    const auto current=std::this_thread::get_id();
    if((lock_.owns_lock()&&thread_!=current)||(other.lock_.owns_lock()&&other.thread_!=current))std::terminate();
    if(lock_.owns_lock())lock_.unlock();if(pin_counted_)detail::release_lifetime_pin();pin_counted_=other.pin_counted_;other.pin_counted_=false;
    owner_=other.owner_;valid_=other.valid_;receipt_=other.receipt_;thread_=other.thread_;
    lock_=std::move(other.lock_);other.owner_=nullptr;other.valid_=false;other.thread_={};
    return *this;
}
Tracker::Scan::~Scan(){if(lock_.owns_lock()&&thread_!=std::this_thread::get_id())std::terminate();if(lock_.owns_lock())lock_.unlock();if(pin_counted_)detail::release_lifetime_pin();}
bool Tracker::Scan::holds_lifetime_of(const Tracker&owner)const noexcept{return owner_==&owner&&thread_==std::this_thread::get_id()&&lock_.owns_lock();}
bool Tracker::Scan::current() const {
    return valid_ && thread_==std::this_thread::get_id() && owner_ && lock_.owns_lock() && !owner_->disabled_ &&
           !owner_->in_flight_count_ && owner_->active_.generation &&
           owner_->active_ == receipt_;
}
Tracker::RestorePermit::RestorePermit(Tracker*owner,Receipt receipt,uint64_t native_epoch,
    uint64_t restore_epoch,uint32_t serial,std::unique_lock<std::recursive_mutex>&& lock,bool pin_counted)
 :owner_(owner),receipt_(receipt),native_epoch_(native_epoch),restore_epoch_(restore_epoch),serial_(serial),thread_(std::this_thread::get_id()),lock_(std::move(lock)),pin_counted_(pin_counted){}
Tracker::RestorePermit::RestorePermit(RestorePermit&&other)noexcept{*this=std::move(other);}
Tracker::RestorePermit&Tracker::RestorePermit::operator=(RestorePermit&&other)noexcept{
    if(this==&other)return *this;
    if((lock_.owns_lock()&&!same_thread())||(other.lock_.owns_lock()&&!other.same_thread()))std::terminate();
    if(lock_.owns_lock())lock_.unlock();if(pin_counted_)detail::release_lifetime_pin();pin_counted_=other.pin_counted_;other.pin_counted_=false;
    owner_=other.owner_;receipt_=other.receipt_;native_epoch_=other.native_epoch_;
    restore_epoch_=other.restore_epoch_;serial_=other.serial_;consumed_=other.consumed_;thread_=other.thread_;
    lock_=std::move(other.lock_);other.owner_=nullptr;other.consumed_=true;other.thread_={};
    return *this;
}
Tracker::RestorePermit::~RestorePermit(){if(lock_.owns_lock()&&!same_thread())std::terminate();if(lock_.owns_lock())lock_.unlock();if(pin_counted_)detail::release_lifetime_pin();}
std::optional<Tracker::RestorePermit> Tracker::prepare_restore(Scan&& scan){
    if(scan.thread_!=std::this_thread::get_id()||scan.owner_!=this||!scan.current()||!scan.lock_.owns_lock()||
       restore_epoch_==std::numeric_limits<uint64_t>::max()||serial_>=std::numeric_limits<uint32_t>::max()-1)return {};
    auto lock=std::move(scan.lock_);scan.valid_=false;const bool counted=scan.pin_counted_;scan.pin_counted_=false;
    return RestorePermit(this,active_,epoch_,restore_epoch_,serial_,std::move(lock),counted);
}
std::optional<Receipt> Tracker::finish_restore(RestorePermit&& permit,const Verify&verify){
    if(!permit.same_thread())return {}; // no lock access/transfer on a foreign thread
    const bool owned=permit.owner_==this&&permit.lock_.owns_lock()&&!permit.consumed_;
    permit.consumed_=true;LocalLifetimePin lock{std::move(permit.lock_),permit.pin_counted_};permit.pin_counted_=false;
    if(!owned)return {};
    const auto eligible=[&]{return !disabled_&&!in_flight_count_&&!active_.generation&&
        epoch_==permit.native_epoch_&&restore_epoch_==permit.restore_epoch_+1&&serial_==permit.serial_+1;};
    if(!verify||!eligible())return {};
    try{
        const auto restored=verify();
        if(!restored||restored->generation||restored->owner!=permit.receipt_.owner||!eligible()||
           !issue_locked(*restored))return {};
        return active_;
    }catch(...){disabled_=true;revoke();return {};}
}
void Tracker::revoke() {
    active_ = {};
    if (serial_ == std::numeric_limits<uint32_t>::max()) disabled_ = true;
    else ++serial_;
}
bool Tracker::issue_locked(const Receipt& verified) {
    const auto& o = verified.owner;
    if (disabled_ || active_.generation || !o.info || !o.archive_wrapper ||
        !o.archive_holder || !o.archive_resource || !o.outer_base ||
        !o.outer_bytes || !o.archive_base || !o.archive_bytes || !o.resource ||
        !o.resource_bytes || !o.entry || !o.archive_key || !o.resource_key ||
        !o.request_heap || !o.data_heap || !o.data_heap_parent ||
        o.pack_heap != o.request_heap ||
        !o.stage_name[0] || verified.generation ||
        o.outer_bytes > 64u * 1024u * 1024u ||
        o.archive_bytes > 64u * 1024u * 1024u ||
        o.resource_bytes > 2u * 1024u * 1024u ||
        uint64_t(o.archive_base) + o.archive_bytes >
            uint64_t(std::numeric_limits<uint32_t>::max()) + 1 ||
        uint64_t(o.outer_base) + o.outer_bytes >
            uint64_t(o.archive_base) + o.archive_bytes ||
        o.outer_base < o.archive_base ||
        uint64_t(o.resource) + o.resource_bytes >
            uint64_t(o.outer_base) + o.outer_bytes ||
        o.resource < o.outer_base ||
        uint64_t(o.entry) < o.outer_base ||
        uint64_t(o.entry) >= uint64_t(o.outer_base) + o.outer_bytes)
        return false;
    if (serial_ == std::numeric_limits<uint32_t>::max()) {
        disabled_ = true;
        return false;
    }
    active_ = verified;
    active_.generation = ++serial_;
    return true;
}
uint64_t Tracker::begin_native() noexcept {
    std::lock_guard lock(mutex_);
    revoke();
    if (disabled_ || epoch_ == std::numeric_limits<uint64_t>::max() ||
        in_flight_count_ == in_flight_.size()) {
        disabled_ = true;
        return 0;
    }
    const uint64_t cookie = ++epoch_;
    in_flight_[in_flight_count_++] = {cookie, restore_epoch_};
    return cookie;
}
bool Tracker::retire_locked(uint64_t cookie) noexcept {
    const auto end = in_flight_.begin() + in_flight_count_;
    const auto found = std::find_if(in_flight_.begin(), end,
                                    [cookie](const InFlight& entry){return entry.cookie==cookie;});
    if (!cookie || found == end) {
        disabled_ = true;
        revoke();
        return false;
    }
    std::move(found + 1, end, found);
    --in_flight_count_;
    in_flight_[in_flight_count_] = {};
    return true;
}
void Tracker::finish_native(uint64_t cookie) noexcept {
    std::lock_guard lock(mutex_);
    retire_locked(cookie);
}
bool Tracker::finish_setinfo(uint64_t cookie, const Verify& verify) noexcept {
    std::lock_guard lock(mutex_);
    // Completed nested mutations are included in the final live verification.
    // An unfinished mutation or a state restore still prevents issuance.
    const bool eligible = !disabled_ && cookie && in_flight_count_ == 1 &&
        in_flight_[0].cookie == cookie &&
        in_flight_[0].restore_epoch == restore_epoch_ && bool(verify);
    std::optional<Receipt> verified;
    if (eligible) {
        try { verified = verify(); }
        catch (...) { disabled_ = true; revoke(); }
    }
    const bool still_current = eligible && in_flight_count_ == 1 &&
        in_flight_[0].cookie == cookie &&
        in_flight_[0].restore_epoch == restore_epoch_;
    if (!retire_locked(cookie)) return false;
    return still_current && verified && !disabled_ && issue_locked(*verified);
}
void Tracker::fault() noexcept {
    std::lock_guard lock(mutex_);
    disabled_ = true;
    revoke();
}
void Tracker::state_loaded() {
    std::lock_guard lock(mutex_);
    revoke();
    if (restore_epoch_ == std::numeric_limits<uint64_t>::max()) disabled_ = true;
    else ++restore_epoch_;
}
Tracker::Scan Tracker::scan() {
    // Scan retains this mutex until destruction; a destructive observer cannot
    // revoke or enter its original body while the scan is active.
    std::unique_lock lock(mutex_);
    return Scan(this, std::move(lock), !disabled_ && !in_flight_count_ &&
                active_.generation, active_);
}
Tracker::Scan Tracker::try_scan(){
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return Scan(this,std::move(lock),false,{});
    return Scan(this,std::move(lock),!disabled_&&!in_flight_count_&&active_.generation,active_);
}
bool Tracker::current(const Receipt& expected) const {
    std::lock_guard lock(mutex_);
    return !disabled_ && !in_flight_count_ && active_.generation && active_ == expected;
}
} // namespace story_native_stage_owner
