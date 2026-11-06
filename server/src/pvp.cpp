// SPDX-License-Identifier: MPL-2.0
// PvP at sea (C28; docs/online-server.md, "PvP at sea"). The games report hits, shots and knockouts; the server
// decides: both players must have PvP on, be outside safe zones and unprotected, close enough, not over the hit
// rate, not blocking each other, not in one party (unless the party's friendly fire is on), and not inside a
// repeat-knockout cooldown. Everything else is dropped and counted. A knockout costs the victim a share of its
// rupees (they vanish), protects it for a while and names the nearest safe zone to come back at.
#include <cmath>
#include <cstring>

#include "server.h"

namespace online::server {

using namespace online::proto;

namespace {
// why a hit was dropped (Metrics::pvp_drops)
enum PvpDrop {
    kDropOff = 0,        // PvP is off (server mode, or either player's toggle)
    kDropSafe,           // either player is in a safe zone (or an interior)
    kDropProtected,      // the victim is protected (busy, after leaving a zone, after a knockout)
    kDropDistance,       // too far, or the hit point is not at the victim
    kDropRate,           // over the hit rate
    kDropBlocked,        // a block between the two
    kDropParty,          // one party without friendly fire
    kDropCooldown,       // repeat knockouts of this victim by this attacker
    kDropUnknown,        // no such victim (here or on another worker)
    kDropMalformed,
    kDropSelf,
};
constexpr uint32_t kFightMs = 10000;  // "in a fight": a hit given or taken within this long
}  // namespace

static float sq_center_x(const Config& c, int sq) { return c.sea_origin_x + (float(sq % kSeaSquares) + 0.5f) * kSquareUnits; }
static float sq_center_z(const Config& c, int sq) { return c.sea_origin_z + (float(sq / kSeaSquares) + 0.5f) * kSquareUnits; }

void Server::pvp_zone_of(const Session& s, bool& in_zone, bool& in_band, int& nearest) const {
    in_zone = false;
    in_band = false;
    nearest = -1;
    if (s.where.stage_name() != cfg_.sea_stage) {
        in_zone = true;  // interiors, dungeons and their entrances are always safe
        return;
    }
    if (!s.has_pose) {
        in_zone = true;  // nothing known yet: safe
        return;
    }
    float best = 1e30f;
    for (size_t i = 0; i < cfg_.pvp.zones.size(); ++i) {
        const PvpZone& z = cfg_.pvp.zones[i];
        float dx = s.head.x - (sq_center_x(cfg_, z.square) + z.cx), dz = s.head.z - (sq_center_z(cfg_, z.square) + z.cz);
        float d = std::sqrt(dx * dx + dz * dz);
        if (d <= z.radius) in_zone = true;
        else if (d <= z.radius + cfg_.pvp.hide_band) in_band = true;
        if (d - z.radius < best) {
            best = d - z.radius;
            nearest = int(i);
        }
    }
}

bool Server::pvp_active(const Session& s) const {
    if (cfg_.pvp.mode == PvpMode::kOff) return false;
    return cfg_.pvp.mode == PvpMode::kOn || s.pvp_on;
}

// in a safe zone for PvP: in one, and not in the delay of entering one mid-fight
bool Server::pvp_safe(const Session& s, uint32_t now) const {
    if (!s.in_zone) return false;
    bool fighting_when_entered = s.fight_at && int32_t(s.zone_since - s.fight_at) < int32_t(kFightMs);
    return !fighting_when_entered || now - s.zone_since >= cfg_.pvp.enter_delay_ms;
}

bool Server::pvp_protected(const Session& s, uint32_t now) const {
    return s.pvp_busy || int32_t(s.protected_until - now) > 0;
}

uint8_t Server::pvp_flags(const Session& s, uint32_t now) const {
    uint8_t f = 0;
    bool safe = pvp_safe(s, now);
    if (pvp_active(s) && !safe) f |= kPvpActive;
    if (pvp_protected(s, now)) f |= kPvpProtected;
    if (safe) f |= kPvpInSafeZone;
    if (s.in_zone && !safe) f |= kPvpEntering;
    if (cfg_.pvp.mode == PvpMode::kOn) f |= kPvpForcedOn;
    return f;
}

void Server::pvp_welcome(Session& s, uint32_t now) {
    Bytes b;
    Writer w(b);
    put_pvp_rules(w, cfg_.pvp);
    s.chan.send_reliable(kMsgPvpRules, b);
    s.pvp_sent = -1;
    (void)now;
}

// once a tick: zones entered and left, protection, what each game is told, who is hidden in the band
void Server::pvp_tick(uint32_t now) {
    if (cfg_.pvp.mode == PvpMode::kOff) return;
    for (auto& [cid, sp] : sessions_) {
        Session& s = *sp;
        if (s.dead || !s.online) continue;
        if (!s.pvp_queued.empty()) {
            // hits that reached this worker before the player did (a handover): still checked here
            auto q = std::move(s.pvp_queued);
            s.pvp_queued.clear();
            for (auto& x : q) {
                if (now - x.at > 1000) continue;
                int why = pvp_check_victim(s, x.akh, x.aparty, s, now);
                if (why >= 0) metrics_.pvp_drops[why]++;
                else pvp_deliver_hit(x.aid, s, x.h, now);
            }
        }
        bool in_zone = false, band = false;
        int nearest = -1;
        pvp_zone_of(s, in_zone, band, nearest);
        if (in_zone && !s.in_zone) s.zone_since = now;
        if (!in_zone && s.in_zone && s.has_pose) {
            // left a zone: protected for a while (ended early by its own first hit or shot)
            s.protected_until = now + cfg_.pvp.protect_ms;
            if (!s.protected_until) s.protected_until = 1;
        }
        s.in_zone = in_zone;
        s.pvp_band = band && pvp_active(s);
        uint8_t f = pvp_flags(s, now);
        if (int(f) != s.pvp_sent) {
            s.pvp_sent = f;
            Bytes b;
            Writer w(b);
            w.u8(f);
            int32_t left = int32_t(s.protected_until - now);
            w.u16(uint16_t(std::clamp(left, 0, 65535)));
            s.chan.send_reliable(kMsgPvpState, b);
        }
    }
    // knockout records age out
    uint32_t window = uint32_t(cfg_.pvp.cooldown_window_s) * 1000;
    for (auto it = pvp_pairs_.begin(); it != pvp_pairs_.end();) {
        auto& p = it->second;
        while (!p.kos.empty() && now - p.kos.front() > window) p.kos.pop_front();
        if (p.kos.empty() && int32_t(p.blocked_until - now) <= 0) it = pvp_pairs_.erase(it);
        else ++it;
    }
}

void Server::on_pvp_flag(Session& s, Reader& r, uint32_t now) {
    uint8_t on = r.u8(), busy = r.u8();
    if (!r.done()) return;
    s.pvp_on = on != 0;
    s.pvp_busy = busy != 0;
    (void)now;
}

// the victim's side of a hit (here: the victim is ours); a drop reason or -1
int Server::pvp_check_victim(Session& a_or_null, uint64_t akh, uint32_t aparty, Session& v, uint32_t now) {
    (void)a_or_null;
    if (v.key_hash == akh) return kDropSelf;
    if (v.blocks.count(akh)) return kDropBlocked;
    bool party = aparty && aparty == v.party;
    if (party) {
        Party* p = party_of(v);
        if (!p || !(p->flags & kPartyFriendlyFire)) return kDropParty;
        return -1;  // the party's own friendly fire: no zone, toggle or protection checks
    }
    if (!pvp_active(v)) return kDropOff;
    if (pvp_safe(v, now)) return kDropSafe;
    if (pvp_protected(v, now)) return kDropProtected;
    auto pk = pvp_pairs_.find({akh, v.key_hash});
    if (pk != pvp_pairs_.end() && int32_t(pk->second.blocked_until - now) > 0) return kDropCooldown;
    return -1;
}

void Server::pvp_deliver_hit(uint16_t attacker_id, Session& v, const PvpHit& h, uint32_t now) {
    PvpHit out = h;
    out.other = attacker_id;
    Bytes b;
    Writer w(b);
    put_pvp_hit(w, out);
    v.chan.send_reliable(kMsgPvpHitOn, b);
    v.fight_at = now;
    v.last_hit_by = attacker_id;
    v.last_hit_at = now;
    metrics_.pvp_hits++;
}

void Server::on_pvp_hit(Session& a, Reader& r, uint32_t now) {
    PvpHit h;
    if (!get_pvp_hit(r, h) || !r.done() || h.damage == 0 || h.damage > cfg_.pvp_max_damage) {
        metrics_.pvp_drops[kDropMalformed]++;
        return;
    }
    auto drop = [&](int why) { metrics_.pvp_drops[why]++; };
    if (!a.pvp_hits.take(1, now)) return drop(kDropRate);
    // the attacker's side
    Session* v = nullptr;
    Ghost* g = nullptr;
    if (auto it = by_id_.find(h.other); it != by_id_.end()) {
        auto sp = sessions_.find(it->second);
        if (sp != sessions_.end() && sp->second->online && !sp->second->dead) v = sp->second.get();
    }
    if (!v)
        if (auto it = ghosts_.find(h.other); it != ghosts_.end()) g = &it->second;
    if (!v && !g) return drop(kDropUnknown);
    uint64_t vkh = v ? v->key_hash : g->key_hash;
    if (a.blocks.count(vkh)) return drop(kDropBlocked);
    bool party = v && a.party && a.party == v->party;
    if (!party) {
        if (!pvp_active(a)) return drop(kDropOff);
        if (pvp_safe(a, now)) return drop(kDropSafe);
    }
    // distance: attacker to victim, and the hit point at the victim
    const PoseHeader& vh = v ? v->head : g->head;
    float dx = vh.x - a.head.x, dz = vh.z - a.head.z;
    if (std::sqrt(dx * dx + dz * dz) > cfg_.pvp.max_hit_dist) return drop(kDropDistance);
    float hx = h.x - vh.x, hz = h.z - vh.z;
    if (std::sqrt(hx * hx + hz * hz) > 600.0f) return drop(kDropDistance);
    // attacking ends one's own protection
    a.protected_until = 0;
    a.fight_at = now;
    if (v && v->redirected) {
        // the victim is on its way to another worker: the hit follows it there
        if (v->redirect_to < 0 || v->redirect_to == me_) return drop(kDropUnknown);
        Bytes b;
        Writer w(b);
        w.u16(a.id);
        w.u64(a.key_hash);
        w.u32(a.party);
        put_pvp_hit(w, h);
        link_send(v->redirect_to, kLPvpHit, b);
        return;
    }
    if (v) {
        int why = pvp_check_victim(a, a.key_hash, a.party, *v, now);
        if (why >= 0) return drop(why);
        pvp_deliver_hit(a.id, *v, h, now);
        return;
    }
    // the victim is on another worker: it checks its side there
    Bytes b;
    Writer w(b);
    w.u16(a.id);
    w.u64(a.key_hash);
    w.u32(a.party);
    put_pvp_hit(w, h);
    link_send(g->from, kLPvpHit, b);
}

void Server::on_pvp_shot(Session& a, Reader& r, uint32_t now) {
    PvpShot sh;
    if (!get_pvp_shot(r, sh, false) || !r.done()) {
        metrics_.pvp_drops[kDropMalformed]++;
        return;
    }
    if (!a.pvp_shots.take(1, now)) {
        metrics_.pvp_drops[kDropRate]++;
        return;
    }
    if (cfg_.pvp.mode == PvpMode::kOff) return;
    a.protected_until = 0;  // shooting ends one's own protection
    a.fight_at = now;
    sh.shooter = a.id;
    Bytes b;
    Writer w(b);
    put_pvp_shot(w, sh, true);
    for_viewers(a, [&](Session& v, uint8_t tier) {
        if (tier == kTierNear && sees(v, a)) v.chan.send_unreliable(kMsgPvpShotOf, b);
    });
}

void Server::on_pvp_ko(Session& v, Reader& r, uint32_t now) {
    uint16_t by = r.u16();
    uint16_t wallet = r.u16();
    if (!r.done()) return;
    // only a knockout by someone who hit it in the last few seconds counts
    if (by != v.last_hit_by || now - v.last_hit_at > kFightMs) {
        metrics_.pvp_drops[kDropUnknown]++;
        return;
    }
    uint64_t akh = 0;
    uint32_t aparty = 0;
    if (auto it = by_id_.find(by); it != by_id_.end()) {
        auto sp = sessions_.find(it->second);
        if (sp != sessions_.end()) {
            akh = sp->second->key_hash;
            aparty = sp->second->party;
        }
    } else if (auto g = ghosts_.find(by); g != ghosts_.end()) {
        akh = g->second.key_hash;
    }
    metrics_.pvp_kos++;
    bool party = aparty && aparty == v.party;
    uint16_t rupees = 0;
    if (!party && akh) {
        auto& pair = pvp_pairs_[{akh, v.key_hash}];
        bool cooling = int32_t(pair.blocked_until - now) > 0;
        pair.kos.push_back(now);
        uint32_t window = uint32_t(cfg_.pvp.cooldown_window_s) * 1000;
        while (!pair.kos.empty() && now - pair.kos.front() > window) pair.kos.pop_front();
        if (pair.kos.size() >= cfg_.pvp.cooldown_kos) pair.blocked_until = now + uint32_t(cfg_.pvp.cooldown_s) * 1000;
        // the rupees: a share of the wallet, capped; none inside the repeat-knockout cooldown; they vanish
        if (!cooling) rupees = uint16_t(std::min<uint32_t>(uint32_t(wallet) * cfg_.pvp.ko_rupee_percent / 100, cfg_.pvp.ko_rupee_max));
    }
    if (rupees) {
        Bytes b;
        Writer(b).u16(rupees);
        v.chan.send_reliable(kMsgPvpRupees, b);
        metrics_.pvp_rupees += rupees;
    }
    // protected after the knockout, and back at the nearest safe zone's edge
    v.protected_until = now + cfg_.pvp.protect_ms;
    if (!v.protected_until) v.protected_until = 1;
    bool in_zone = false, band = false;
    int nearest = -1;
    pvp_zone_of(v, in_zone, band, nearest);
    if (nearest >= 0 && !party) {
        const PvpZone& z = cfg_.pvp.zones[size_t(nearest)];
        float cx = sq_center_x(cfg_, z.square) + z.cx, cz = sq_center_z(cfg_, z.square) + z.cz;
        float dx = v.head.x - cx, dz = v.head.z - cz, d = std::sqrt(dx * dx + dz * dz);
        float k = d > 1 ? (z.radius - 500.0f) / d : 0;  // just inside the edge, on the victim's side
        Bytes b;
        Writer w(b);
        w.u8(z.square);
        put_s24(w, cx + dx * k);
        put_s24(w, cz + dz * k);
        v.chan.send_reliable(kMsgPvpRespawn, b);
    }
    Bytes ko;
    Writer kw(ko);
    kw.u16(v.id);
    kw.u16(by);
    v.chan.send_reliable(kMsgPvpKoOf, ko);
    for_viewers(v, [&](Session& o, uint8_t tier) {
        if (tier <= kTierMid) o.chan.send_reliable(kMsgPvpKoOf, ko);
    });
}

// ---------------------------------------------------------------- handovers

// the player's PvP state now, to go with it to another worker
PvpCarry Server::pvp_carry_of(const Session& s, uint32_t now) const {
    PvpCarry c;
    c.valid = true;
    c.on = s.pvp_on;
    c.busy = s.pvp_busy;
    c.in_zone = s.in_zone;
    c.zone_age = now - s.zone_since;
    int32_t left = int32_t(s.protected_until - now);
    c.protect_left = s.protected_until && left > 0 ? uint32_t(left) : 0;
    c.fighting = s.fight_at != 0;
    c.fight_age = now - s.fight_at;
    c.last_hit_by = s.last_hit_by;
    c.last_hit_age = s.last_hit_by ? now - s.last_hit_at : 0xFFFFFFFF;
    for (auto& [k, p] : pvp_pairs_) {
        if (k.second != s.key_hash) continue;
        PvpCarry::Pair q;
        q.attacker = k.first;
        int32_t b = int32_t(p.blocked_until - now);
        q.blocked_left = p.blocked_until && b > 0 ? uint32_t(b) : 0;
        for (uint32_t t : p.kos) q.ko_ages.push_back(now - t);
        c.pairs.push_back(std::move(q));
        if (c.pairs.size() >= 1024) break;
    }
    return c;
}

// the state that came with a handover, on this worker's clock; replaces what this worker knew of the player
void Server::apply_pvp_carry(Session& s, const PvpCarry& c, uint32_t now) {
    if (!c.valid) return;
    auto at = [&](uint32_t age) {
        uint32_t t = now - age;
        return t ? t : 1;
    };
    s.pvp_on = c.on;
    s.pvp_busy = c.busy;
    s.in_zone = c.in_zone;  // no "left a zone" protection for crossing a worker border
    s.zone_since = now - c.zone_age;
    s.protected_until = c.protect_left ? now + c.protect_left : 0;
    if (c.protect_left && !s.protected_until) s.protected_until = 1;
    s.fight_at = c.fighting ? at(c.fight_age) : 0;
    s.last_hit_by = c.last_hit_age != 0xFFFFFFFF ? c.last_hit_by : 0;
    s.last_hit_at = c.last_hit_age != 0xFFFFFFFF ? now - c.last_hit_age : 0;
    for (auto it = pvp_pairs_.begin(); it != pvp_pairs_.end();)
        it = it->first.second == s.key_hash ? pvp_pairs_.erase(it) : std::next(it);
    for (auto& q : c.pairs) {
        auto& p = pvp_pairs_[{q.attacker, s.key_hash}];
        p.blocked_until = q.blocked_left ? now + q.blocked_left : 0;
        if (q.blocked_left && !p.blocked_until) p.blocked_until = 1;
        for (uint32_t age : q.ko_ages) p.kos.push_back(now - age);
    }
    s.pvp_sent = -1;
    s.pvp_carried_at = now ? now : 1;
}

void put_pvp_carry(Writer& w, const PvpCarry& c) {
    w.u8(uint8_t((c.on ? 1 : 0) | (c.busy ? 2 : 0) | (c.in_zone ? 4 : 0) | (c.fighting ? 8 : 0)));
    w.u32(c.zone_age);
    w.u32(c.protect_left);
    w.u32(c.fight_age);
    w.u16(c.last_hit_by);
    w.u32(c.last_hit_age);
    w.u16(uint16_t(c.pairs.size()));
    for (auto& q : c.pairs) {
        w.u64(q.attacker);
        w.u32(q.blocked_left);
        size_t n = std::min<size_t>(q.ko_ages.size(), 64);
        w.u8(uint8_t(n));
        for (size_t i = 0; i < n; ++i) w.u32(q.ko_ages[q.ko_ages.size() - n + i]);
    }
}

bool get_pvp_carry(Reader& r, PvpCarry& c) {
    c = PvpCarry{};
    uint8_t f = r.u8();
    c.on = f & 1;
    c.busy = f & 2;
    c.in_zone = f & 4;
    c.fighting = f & 8;
    c.zone_age = r.u32();
    c.protect_left = std::min<uint32_t>(r.u32(), 3600000);
    c.fight_age = r.u32();
    c.last_hit_by = r.u16();
    c.last_hit_age = r.u32();
    size_t n = r.u16();
    if (n > 1024) return false;
    for (size_t i = 0; i < n && r.ok(); ++i) {
        PvpCarry::Pair q;
        q.attacker = r.u64();
        q.blocked_left = std::min<uint32_t>(r.u32(), 3600000);
        size_t k = r.u8();
        if (k > 64) return false;
        for (size_t j = 0; j < k && r.ok(); ++j) q.ko_ages.push_back(r.u32());
        c.pairs.push_back(std::move(q));
    }
    c.valid = r.ok();
    return c.valid;
}

// a hit from a player on another worker (kLPvpHit): checked and delivered here, sent on after the victim if it
// moved on, or held for the victim that is on its way here
void Server::pvp_link_hit(const Bytes& body, uint32_t now) {
    Reader r(body);
    PvpQueuedHit q;
    q.aid = r.u16();
    q.akh = r.u64();
    q.aparty = r.u32();
    if (!get_pvp_hit(r, q.h) || !r.done()) return;
    q.at = now;
    if (auto it = by_id_.find(q.h.other); it != by_id_.end()) {
        auto sp = sessions_.find(it->second);
        if (sp != sessions_.end() && sp->second->online && !sp->second->dead) {
            Session& v = *sp->second;
            if (v.redirected && v.redirect_to >= 0 && v.redirect_to != me_) {
                link_send(v.redirect_to, kLPvpHit, body);
                return;
            }
            if (v.redirected) return;
            int why = pvp_check_victim(v, q.akh, q.aparty, v, now);
            if (why >= 0) {
                metrics_.pvp_drops[why]++;
                return;
            }
            pvp_deliver_hit(q.aid, v, q.h, now);
            return;
        }
    }
    for (auto& [t, tk] : tickets_)
        if (tk.who.id == q.h.other) {
            if (tk.hits.size() < 16) tk.hits.push_back(q);
            return;
        }
    metrics_.pvp_drops[kDropUnknown]++;
}

}  // namespace online::server
