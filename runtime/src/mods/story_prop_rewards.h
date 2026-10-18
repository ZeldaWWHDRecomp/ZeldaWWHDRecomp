// SPDX-License-Identifier: MPL-2.0
// Internal prop rewards: pure planning over an entire loaded story area.
#pragma once
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include "story_items_core.h"

namespace story_items {
using PropMarkerHandle = uint32_t;
using PropMarkerCheckpoint = uint32_t;
struct EnemyMarkerHandle { uint32_t value = 0; explicit operator bool() const { return value != 0; } };
struct PropDrop { std::string item; uint32_t quantity = 0; };
enum class PropClaimStatus { Committed, AlreadyBroken, InvalidDefinition, StoreUnavailable,
    NoRecordCapacity, StackFull, Quarantined, Stale, AlreadyDefeated = AlreadyBroken };
using EnemyClaimStatus = PropClaimStatus;
namespace prop_rewards {
inline constexpr uint8_t kBroken = 18, kDefeated = 19;
inline constexpr size_t kAreaSize = 0x400;
inline constexpr unsigned kRecords = 255;
struct Marker { std::string mod, actor, flag; uint16_t key = 0; uint8_t kind = kBroken; };
class Registry {
    struct Entry { PropMarkerHandle token; Marker marker; };
    std::vector<Entry> markers_;
    PropMarkerHandle next_ = 1; // never rewound by startup rollback; zero means exhausted
public:
    PropMarkerCheckpoint checkpoint() const { return uint32_t(markers_.size()); }
    bool rollback(PropMarkerCheckpoint checkpoint) {
        if (checkpoint > markers_.size()) return false;
        markers_.resize(checkpoint); return true;
    }
    PropMarkerHandle add(const std::string& mod, const std::string& actor,
                         const std::string& flag, std::string& why, uint8_t kind = kBroken) {
        bool valid_mod = !mod.empty() && mod.size() <= 64 && mod[0] != '.';
        for (char c : mod) valid_mod &= (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if ((kind != kBroken && kind != kDefeated) || !valid_mod || !story_mail::valid_id(actor) || !story_mail::valid_id(flag)) {
            why = "invalid prop marker identity"; return 0;
        }
        const uint16_t key = story_mail::letter_key(mod, std::string(kind == kBroken ? "prop-broken:" : "enemy-defeated:") + flag);
        for (size_t i = 0; i < markers_.size(); ++i) {
            const auto& m = markers_[i].marker;
            if (m.mod == mod && m.actor == actor && m.flag == flag && m.kind == kind) return markers_[i].token;
            if ((m.mod == mod && (m.actor == actor || (m.kind == kind && m.flag == flag))) || (m.kind == kind && m.key == key)) {
                why = "prop marker identity or saved key conflicts"; return 0;
            }
        }
        if (markers_.size() == 256 || !next_) { why = "prop marker registry full or handles exhausted"; return 0; }
        const PropMarkerHandle token = next_++;
        markers_.push_back({token, {mod, actor, flag, key, kind}});
        return token;
    }
    // World editor: a marker whose actor changed its flag goes (the actor registers the new one).
    bool remove(PropMarkerHandle h) {
        for (size_t i = 0; i < markers_.size(); ++i)
            if (markers_[i].token == h) { markers_.erase(markers_.begin() + long(i)); return true; }
        return false;
    }
    const Marker* get(PropMarkerHandle h, uint8_t kind = kBroken) const {
        for (const auto& entry : markers_) if (entry.token == h && entry.marker.kind == kind) return &entry.marker;
        return nullptr;
    }
};
struct Drop { uint16_t key; uint32_t quantity; uint8_t stack; };
struct Plan {
    PropClaimStatus status = PropClaimStatus::StoreUnavailable;
    std::array<uint8_t, kAreaSize> before{}, after{};
    std::vector<bool> placed;
    std::vector<unsigned> counts;
};
inline Plan stage(const uint8_t* live, uint16_t marker, const std::vector<Drop>& drops, unsigned pages, uint8_t marker_kind = kBroken) {
    Plan p;
    if (!live) return p;
    if (marker_kind != kBroken && marker_kind != kDefeated) { p.status = PropClaimStatus::InvalidDefinition; return p; }
    std::memcpy(p.before.data(), live, kAreaSize); p.after = p.before;
    story_mail::Book b(p.after.data(), kRecords);
    if (!b.usable()) return p;
    // Reject ambiguous duplicate records and malformed values used by this transaction.
    for (unsigned i = 0; i < kRecords; ++i) {
        const auto kind = b.record_kind(i);
        if (!kind) continue;
        for (unsigned j = 0; j < i; ++j)
            if (kind == b.record_kind(j) && b.record_key(i) == b.record_key(j)) return p;
        if (((kind == kBroken || kind == kDefeated || kind == kRecordItemCount || kind == kRecordItemPlace) && !b.record_key(i)) ||
            ((kind == kBroken || kind == kDefeated) && b.record_value(i) > 1) ||
            (kind == kRecordItemCount && (!b.record_value(i) || b.record_value(i) == 255))) return p;
    }
    for (unsigned i = 0; i < kRecords; ++i) {
        if (b.record_kind(i) != kRecordItemPlace) continue;
        const auto place = b.record_value(i);
        if (!place || place >= kFirstPagePlace + kMaxPages * kPageSlots ||
            !b.get(kRecordItemCount, b.record_key(i))) return p;
        for (unsigned j = 0; j < i; ++j)
            if (b.record_kind(j) == kRecordItemPlace && b.record_value(j) == place) return p;
    }
    if (!marker || drops.size() > 8) { p.status = PropClaimStatus::InvalidDefinition; return p; }
    for (size_t i = 0; i < drops.size(); ++i) {
        const auto& d = drops[i];
        if (!d.key || !d.quantity || d.quantity > 254 || !d.stack || d.stack > 254) {
            p.status = PropClaimStatus::InvalidDefinition; return p;
        }
        for (size_t j = 0; j < i; ++j) if (drops[j].key == d.key) {
            p.status = PropClaimStatus::InvalidDefinition; return p;
        }
    }
    if (b.get(marker_kind, marker)) { p.status = PropClaimStatus::AlreadyBroken; return p; }
    if (!b.set(marker_kind, marker, 1)) { p.status = PropClaimStatus::NoRecordCapacity; return p; }
    Bag bag(b, pages);
    std::vector<bool> newly_owned;
    // Mandatory count records are reserved before optional placement records.
    for (const auto& d : drops) {
        const unsigned before = bag.count(d.key), count = before + d.quantity;
        if (count > d.stack) { p.status = PropClaimStatus::StackFull; return p; }
        if (!b.set(kRecordItemCount, d.key, uint8_t(count))) {
            p.status = PropClaimStatus::NoRecordCapacity; return p;
        }
        newly_owned.push_back(!before); p.counts.push_back(count);
    }
    for (size_t i = 0; i < drops.size(); ++i)
        p.placed.push_back(newly_owned[i] ? bag.place_on_pages(drops[i].key) : bag.place(drops[i].key) != 0);
    p.status = PropClaimStatus::Committed; return p;
}
// Notification happens only after the single commit. A failed notification cannot undo a grant.
template<class Notify> inline PropClaimStatus commit(uint8_t* live, const Plan& p, Notify notify) {
    if (p.status != PropClaimStatus::Committed) return p.status;
    if (!live || std::memcmp(live, p.before.data(), kAreaSize)) return PropClaimStatus::Stale;
    std::memcpy(live, p.after.data(), kAreaSize);
    try { notify(); } catch (...) { /* committed inventory remains authoritative */ }
    return PropClaimStatus::Committed;
}
} // namespace prop_rewards
} // namespace story_items
