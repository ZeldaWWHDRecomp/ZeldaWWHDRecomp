// SPDX-License-Identifier: MPL-2.0
// Parties (DESIGN.md 2.3-2.5, decision 2026-11-06 later): invites by friend code, membership by key so a member who
// comes back is in the party again, the enemy runner of each place chosen by the server and handed over when the
// runner leaves or goes silent, and the team progress store: one shared progress, merged as a union (flags are only
// set, counters only grow), kept as the last known team state while no member is online.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "server.h"

namespace online::server {

using namespace online::proto;

namespace {
constexpr uint32_t kInviteMs = 60000;

bool big_endian_greater(const std::string& a, const std::string& b) {
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (uint8_t(a[i]) != uint8_t(b[i])) return uint8_t(a[i]) > uint8_t(b[i]);
    return false;
}
}  // namespace

std::vector<TeamField> TeamStore::merge(const std::vector<TeamField>& in, int& refused) {
    std::vector<TeamField> changed;
    for (const TeamField& f : in) {
        if (f.value.empty() || f.value.size() > kMaxTeamFieldBytes || size_t(f.offset) + f.value.size() > 0x10000 ||
            f.mode > kFieldFill) {
            ++refused;
            continue;
        }
        auto it = fields.find(f.offset);
        if (it == fields.end()) {
            if (fields.size() >= kMaxTeamFields) {
                ++refused;
                continue;
            }
            fields.emplace(f.offset, f);
            changed.push_back(f);
            continue;
        }
        TeamField& have = it->second;
        if (have.mode != f.mode || have.value.size() != f.value.size()) {
            ++refused;  // one offset is one field: its size and mode never change
            continue;
        }
        bool diff = false;
        if (f.mode == kFieldBits) {
            for (size_t i = 0; i < f.value.size(); ++i) {
                char v = char(uint8_t(have.value[i]) | uint8_t(f.value[i]));
                if (v != have.value[i]) {
                    have.value[i] = v;
                    diff = true;
                }
            }
        } else if (f.mode == kFieldFill) {
            for (size_t i = 0; i < f.value.size(); ++i) {
                uint8_t h = uint8_t(have.value[i]), v = uint8_t(f.value[i]);
                if ((h == 0x00 || h == 0xFF) && v != 0x00 && v != 0xFF) {
                    have.value[i] = char(v);
                    diff = true;
                }
            }
        } else if (big_endian_greater(f.value, have.value)) {
            have.value = f.value;
            diff = true;
        }
        if (diff) changed.push_back(have);
    }
    if (!changed.empty()) ++version;
    return changed;
}

std::vector<TeamField> TeamStore::all() const {
    std::vector<TeamField> v;
    v.reserve(fields.size());
    for (auto& [o, f] : fields) v.push_back(f);
    return v;
}

Party* Server::party_of(const Session& s) {
    if (!s.party) return nullptr;
    auto it = parties_.find(s.party);
    return it == parties_.end() ? nullptr : &it->second;
}

Session* Server::online_member(uint64_t kh) {
    Session* s = find_by_key(kh);
    return s && s->online && !s->dead && !s->redirected ? s : nullptr;
}

bool Server::party_mods_ok(Party& p, Session& s) {
    if (!p.mods_set) return true;
    std::vector<ModProblem> d = party_mod_problems(p.mods, s.mods);
    if (d.empty()) return true;
    party_result(s, 10, "your gameplay mods differ from the party's", &d);
    return false;
}

void Server::party_result(Session& s, uint8_t code, const std::string& text, const std::vector<ModProblem>* problems) {
    Bytes b;
    Writer w(b);
    w.u8(code);
    w.str8(text);
    if (problems) put_mod_problems(w, *problems);
    s.chan.send_reliable(kMsgPartyResult, b);
}

void Server::party_send_state(Party& p) {
    PartyState st;
    st.party = p.id;
    st.flags = p.flags;
    st.max = uint8_t(cfg_.party_max);
    for (uint64_t kh : p.members) {
        PartyMember m;
        m.key_hash = kh;
        Session* t = online_member(kh);
        m.id = t ? t->id : 0;
        m.online = t != nullptr;
        m.name = p.names[kh];
        if (kh == p.leader) st.leader = m.id;
        st.members.push_back(std::move(m));
    }
    Bytes b = encode_party_state(st);
    for (uint64_t kh : p.members)
        if (Session* t = online_member(kh)) t->chan.send_reliable(kMsgPartyState, b);
}

void Server::party_send_team(Party& p, Session& to) {
    if (!(p.flags & kPartySharedProgress)) return;
    Bytes b;
    Writer w(b);
    w.u32(p.team.version);
    put_team_fields(w, p.team.all());
    to.chan.send_reliable(kMsgTeamState, b);
}

void Server::party_join(Party& p, Session& s, uint32_t now) {
    team_dirty_ = true;
    // the party's mod set is its leader's (the first member is the leader of a new party)
    if (!p.mods_set || s.key_hash == p.leader) {
        p.mods = s.mods;
        p.mods_set = true;
    }
    if (std::find(p.members.begin(), p.members.end(), s.key_hash) == p.members.end()) p.members.push_back(s.key_hash);
    p.names[s.key_hash] = s.name;
    party_by_key_[s.key_hash] = p.id;
    s.party = p.id;
    p.empty_since = 0;
    p.arrived[s.key_hash] = {PartyPlace{s.where.stage, s.where.room}, now};
    // a party coming back after a restart may have its leader still away: the first member back leads, so no state
    // ever says "leader 0" while someone is online
    // (not while the leader is on its way here with the party: a handover between workers keeps the leader)
    bool leader_coming = std::any_of(tickets_.begin(), tickets_.end(),
                                     [&](const auto& t) { return t.second.who.key_hash == p.leader; });
    if (!online_member(p.leader) && !leader_coming)
        for (uint64_t kh : p.members)
            if (online_member(kh)) {
                p.leader = kh;
                break;
            }
    party_send_state(p);
    party_send_team(p, s);  // joining or loading while in a party: the game takes the team state
    // the runner table so far
    std::vector<RunnerEntry> all;
    for (auto& [place, kh] : p.runner) {
        RunnerEntry e;
        e.where.stage = place.stage;
        e.where.room = place.room;
        Session* t = online_member(kh);
        e.runner = t ? t->id : 0;
        all.push_back(e);
    }
    if (!all.empty()) s.chan.send_reliable(kMsgRunner, encode_runners(all));
    // members see each other again as party members (display names, tiers)
    s.seen.clear();
    for (uint64_t kh : p.members)
        if (Session* t = online_member(kh); t && t != &s) t->seen.erase(s.id);
}

void Server::party_member_online(Session& s, uint32_t now) {
    auto it = party_by_key_.find(s.key_hash);
    if (it == party_by_key_.end()) {
        s.party = 0;
        return;
    }
    auto p = parties_.find(it->second);
    if (p == parties_.end() || p->second.world != s.world) {
        s.party = 0;
        return;
    }
    if (s.key_hash != p->second.leader && !party_mods_ok(p->second, s)) {
        s.party = 0;  // not back in it with other mods (the party stays; the player is told)
        return;
    }
    party_join(p->second, s, now);
}

void Server::party_member_offline(Session& s, uint32_t now) {
    Party* p = party_of(s);
    if (!p) return;
    p->arrived.erase(s.key_hash);
    bool any = false;
    for (uint64_t kh : p->members)
        if (kh != s.key_hash && online_member(kh)) any = true;
    if (any) {
        // the leader left: a successor at once (the member in the party longest), not "leader 0"
        if (p->leader == s.key_hash)
            for (uint64_t kh : p->members)
                if (kh != s.key_hash && online_member(kh)) {
                    p->leader = kh;
                    break;
                }
    }
    if (!any) {
        p->empty_since = now ? now : 1;
        if (linked() && me_ > 0) {
            // nobody online: the coordinator keeps the last known team state (members come back through it)
            link_send(0, kLPartyPark, encode_party(*p));
            uint32_t pid = p->id;
            for (uint64_t kh : p->members) party_by_key_.erase(kh);
            s.party = 0;
            parties_.erase(pid);
            return;
        }
    }
    // the runner table is redone by runners_tick; members see who is offline
    party_send_state(*p);
}

void Server::party_place_changed(Session& s, uint32_t now) {
    Party* p = party_of(s);
    if (!p) return;
    PartyPlace here{s.where.stage, s.where.room};
    auto it = p->arrived.find(s.key_hash);
    if (it == p->arrived.end() || it->second.first < here || here < it->second.first) p->arrived[s.key_hash] = {here, now};
}

// After a server restart (DESIGN.md 2.6 / 3.4) the party comes back from what the games re-send: each member sends
// the party record its leader signed (or the leader its own). The record names the members by key hash; each
// claimant is proven by its signed HELLO. The party is rebuilt with its old id when the leader claims it or a member
// shows a record the leader signed; the other listed members join when they claim the same party id. A newer
// record (issued later) replaces the member list of an older one. With files.team_state (on by default) the
// party and its team progress are already back from disk, and members are put back in it as they sign in.
void Server::party_restore(Session& s, Reader& r, uint32_t now) {
    PartyRecord rec;
    if (!decode_party_record(r.here(), r.left(), rec)) return party_result(s, 9, "your party could not be restored");
    bool listed = std::find(rec.members.begin(), rec.members.end(), s.key_hash) != rec.members.end();
    bool by_leader = s.key_hash == rec.leader || party_record_signed_by_leader(rec);
    if (!listed || !rec.party) return party_result(s, 9, "your party could not be restored");
    if (s.party == rec.party) return;  // back already (from the saved team state)
    auto it = parties_.find(rec.party);
    if (it != parties_.end() && it->second.world != s.world) return party_result(s, 9, "your party could not be restored");
    if (rec.members.size() > size_t(cfg_.party_max)) rec.members.resize(size_t(cfg_.party_max));  // this server's size
    if (std::find(rec.members.begin(), rec.members.end(), s.key_hash) == rec.members.end())
        return party_result(s, 9, "your party could not be restored (larger than this server allows)");
    if (it == parties_.end()) {
        if (!by_leader) {
            // a member without a leader-signed record: it waits for the leader's claim
            s.restore_party = rec.party;
            s.restore_at = now;
            return;
        }
        Party p;
        p.id = rec.party;
        p.world = s.world;
        p.leader = rec.leader;
        p.flags = rec.flags;
        p.issued = rec.issued;
        p.members = rec.members;
        p.empty_since = 0;
        for (uint64_t kh : p.members) party_by_key_[kh] = p.id;
        it = parties_.emplace(p.id, std::move(p)).first;
        log("party " + std::to_string(rec.party) + " rebuilt from its record after a restart");
    } else if (by_leader && rec.issued > it->second.issued) {
        // a newer record from the leader: its member list wins
        Party& p = it->second;
        for (uint64_t kh : p.members) party_by_key_.erase(kh);
        p.members = rec.members;
        p.leader = rec.leader;
        p.flags = rec.flags;
        p.issued = rec.issued;
        for (uint64_t kh : p.members) party_by_key_[kh] = p.id;
    } else if (std::find(it->second.members.begin(), it->second.members.end(), s.key_hash) == it->second.members.end()) {
        return party_result(s, 9, "your party could not be restored");
    }
    Party& p = it->second;
    if (s.key_hash != p.leader && !party_mods_ok(p, s)) return;
    if (s.party && s.party != p.id) party_leave(s, now);
    party_join(p, s, now);
    // members who claimed this party before it was rebuilt come in now
    for (uint64_t kh : p.members)
        if (Session* t = online_member(kh); t && t != &s && t->party != p.id && t->restore_party == p.id &&
                                             now - t->restore_at < 600000) {
            t->restore_party = 0;
            party_join(p, *t, now);
        }
    party_elect_leader(p, now);  // the leader may not be back yet
}

void Server::party_invite(Session& s, uint64_t code, uint32_t now) {
    // a full party costs no invite token
    if (Party* full = party_of(s); full && full->members.size() >= size_t(cfg_.party_max))
        return party_result(s, 5, "the party is full");
    if (!s.invite.take(1, now)) return party_result(s, 1, "wait a little before the next invite");
    Session* to = nullptr;
    for (auto& [kh, cid] : by_key_)
        if (code40_of(kh) == code) {
            Session* t = online_member(kh);
            if (t && t->world == s.world) to = t;
        }
    if (!to && !linked()) return party_result(s, 2, "that player is not online on this server");
    if (to == &s) return party_result(s, 3, "that is your own code");
    if (to && !sees(s, to->key_hash)) return party_result(s, 2, "that player is not online on this server");
    Party* p = party_of(s);
    if (!p) {
        // unique over all workers: parties move between them
        uint32_t id = 0;
        // random, so a party rebuilt after a restart keeps its old id without meeting a new party's
        do {
            crypto::random_bytes(&id, sizeof id);
            id = (uint32_t(std::max(me_, 0)) << 24) | (id & 0xFFFFFF);
        } while (parties_.count(id) || !(id & 0xFFFFFF));
        Party np;
        np.id = id;
        np.world = s.world;
        np.leader = s.key_hash;
        parties_[id] = std::move(np);
        p = &parties_[id];
        party_join(*p, s, now);
    }
    if (!to) {
        // on another worker, maybe: every worker gets the invite and the one hosting the friend shows it
        p->invites.erase(std::remove_if(p->invites.begin(), p->invites.end(),
                                        [&](const Party::Invite& i) { return now - i.at > kInviteMs; }),
                         p->invites.end());
        if (p->members.size() + p->invites.size() >= size_t(cfg_.party_max)) return party_result(s, 5, "the party is full");
        p->invites.push_back({code | (uint64_t(1) << 63), now});  // by code: the key is not known here
        Bytes b;
        Writer w(b);
        w.u32(p->id);
        w.u64(code);
        w.u16(s.id);
        w.u64(s.key_hash);
        w.str8(s.name);
        w.u64(s.world);
        put_mod_refs(w, p->mods_set ? p->mods : s.mods);
        for (size_t i = 0; i < peers_.size(); ++i)
            if (int(i) != me_ && peers_[i].up) link_send(int(i), kLInvite, b);
        return party_result(s, 0, "invite sent");
    }
    if (std::find(p->members.begin(), p->members.end(), to->key_hash) != p->members.end())
        return party_result(s, 4, "already in your party");
    p->invites.erase(std::remove_if(p->invites.begin(), p->invites.end(),
                                    [&](const Party::Invite& i) { return now - i.at > kInviteMs || i.to == to->key_hash; }),
                     p->invites.end());
    if (p->members.size() + p->invites.size() >= size_t(cfg_.party_max)) return party_result(s, 5, "the party is full");
    p->invites.push_back({to->key_hash, now});
    Bytes b;
    Writer w(b);
    w.u32(p->id);
    w.u16(s.id);
    w.u64(s.key_hash);
    w.str8(s.name);
    put_mod_refs(w, p->mods);  // the party's mod set, so the invitee can offer to use it
    to->chan.send_reliable(kMsgPartyInvited, b);
    party_result(s, 0, "invite sent");
}

void Server::party_accept(Session& s, uint32_t id, uint32_t now) {
    auto it = parties_.find(id);
    if (it == parties_.end()) {
        // a party on another worker: go there to join it (the handover carries the acceptance)
        auto r = remote_invites_.find(id);
        if (r == remote_invites_.end() || size_t(r->second) >= peers_.size() || !peers_[size_t(r->second)].up)
            return party_result(s, 6, "that party is gone");
        int target = r->second;
        remote_invites_.erase(r);
        if (s.party) party_leave(s, now);
        hand_over(s, target, now, nullptr, id);
        return;
    }
    Party& p = it->second;
    auto inv = std::find_if(p.invites.begin(), p.invites.end(), [&](const Party::Invite& i) {
        return (i.to == s.key_hash || i.to == (code40_of(s.key_hash) | (uint64_t(1) << 63))) && now - i.at <= kInviteMs;
    });
    if (inv == p.invites.end()) return party_result(s, 7, "the invite expired");
    if (!party_mods_ok(p, s)) return;  // (the invite stays: the player may switch mods and accept again)
    p.invites.erase(inv);
    if (p.members.size() >= size_t(cfg_.party_max)) return party_result(s, 5, "the party is full");
    if (s.party && s.party != id) party_leave(s, now);
    party_join(p, s, now);
    party_result(s, 0, "joined the party");
}

void Server::party_decline(Session& s, uint32_t id) {
    auto it = parties_.find(id);
    if (it == parties_.end()) {
        auto r = remote_invites_.find(id);
        if (r == remote_invites_.end()) return;
        // the leader is not known here by key; the inviting worker tells whoever leads that party
        Bytes b;
        Writer w(b);
        w.u32(id);
        w.u8(8);
        w.str8(s.name + " declined");
        link_send(r->second, kLInviteAnswer, b);
        remote_invites_.erase(r);
        return;
    }
    Party& p = it->second;
    p.invites.erase(std::remove_if(p.invites.begin(), p.invites.end(),
                                   [&](const Party::Invite& i) { return i.to == s.key_hash; }),
                    p.invites.end());
    if (Session* l = online_member(p.leader)) party_result(*l, 8, s.name + " declined");
}

void Server::party_leave(Session& s, uint32_t now) {
    team_dirty_ = true;
    Party* p = party_of(s);
    if (!p) return;
    (void)now;
    p->members.erase(std::remove(p->members.begin(), p->members.end(), s.key_hash), p->members.end());
    p->arrived.erase(s.key_hash);
    p->names.erase(s.key_hash);
    party_by_key_.erase(s.key_hash);
    s.party = 0;
    s.seen.clear();
    for (uint64_t kh : p->members)
        if (Session* t = online_member(kh)) t->seen.erase(s.id);
    PartyState none;
    s.chan.send_reliable(kMsgPartyState, encode_party_state(none));
    uint32_t pid = p->id;
    if (p->members.empty()) {
        parties_.erase(pid);
        return;
    }
    // leadership passes to the member who has been in the party longest
    if (p->leader == s.key_hash) p->leader = p->members.front();
    for (auto it = p->runner.begin(); it != p->runner.end();) it = it->second == s.key_hash ? p->runner.erase(it) : std::next(it);
    party_send_state(*p);
}

void Server::party_settings(Session& s, uint16_t flags) {
    team_dirty_ = true;
    Party* p = party_of(s);
    if (!p || p->leader != s.key_hash) return;
    bool sharing_on = !(p->flags & kPartySharedProgress) && (flags & kPartySharedProgress);
    p->flags = flags;
    party_send_state(*p);
    if (sharing_on)
        for (uint64_t kh : p->members)
            if (Session* t = online_member(kh)) party_send_team(*p, *t);
}

void Server::party_progress(Session& s, Reader& r) {
    std::vector<TeamField> in;
    if (!get_team_fields(r, in) || !r.done()) return strike(s, monotonic_ms(), "progress");
    Party* p = party_of(s);
    if (!p || !(p->flags & kPartySharedProgress)) return;
    int refused = 0;
    std::vector<TeamField> changed = p->team.merge(in, refused);
    if (!changed.empty()) team_dirty_ = true;
    if (refused) strike(s, monotonic_ms(), "progress field");
    if (changed.empty()) return;
    Bytes b;
    Writer w(b);
    w.u32(p->team.version);
    put_team_fields(w, changed);
    for (uint64_t kh : p->members)
        if (Session* t = online_member(kh); t && t != &s) t->chan.send_reliable(kMsgTeamState, b);
}

// once a second every online member learns where all the others are (stage, room, square, position), however
// far apart they are: the sea chart and the Online panel never depend on the interest tiers
void Server::party_where_tick(uint32_t now) {
    (void)now;
    for (auto& [pid, p] : parties_) {
        if (p.empty_since) continue;
        Bytes b;
        Writer w(b);
        int n = 0;
        for (uint64_t kh : p.members) {
            Session* t = online_member(kh);
            if (!t) continue;
            w.u16(t->id);
            put_where(w, t->has_pose ? t->head.where : t->where);
            w.u8(uint8_t(t->square >= 0 && t->has_pose ? t->square : 255));
            auto s24 = [&](float f) {
                int32_t v = int32_t(std::lround(f));
                v = std::clamp(v, -0x800000, 0x7FFFFF);
                uint32_t u = uint32_t(v) & 0xFFFFFF;
                w.u8(uint8_t(u));
                w.u8(uint8_t(u >> 8));
                w.u8(uint8_t(u >> 16));
            };
            s24(t->has_pose ? t->head.x : 0);
            s24(t->has_pose ? t->head.z : 0);
            ++n;
        }
        if (n < 2) continue;
        for (uint64_t kh : p.members)
            if (Session* t = online_member(kh)) t->chan.send_unreliable(kMsgPartyWhere, b);
    }
}

// the leader is gone or silent: the member in the party longest (and present) leads now
void Server::party_elect_leader(Party& p, uint32_t now) {
    auto present = [&](uint64_t kh) {
        Session* t = online_member(kh);
        return t && now - t->chan.last_heard_ms() <= uint32_t(cfg_.runner_drop_ms);
    };
    if (present(p.leader)) return;
    // the leader is on its way here (a handover between workers): it stays the leader
    if (std::any_of(tickets_.begin(), tickets_.end(), [&](const auto& t) { return t.second.who.key_hash == p.leader; }))
        return;
    for (uint64_t kh : p.members)
        if (kh != p.leader && present(kh)) {
            p.leader = kh;
            party_send_state(p);
            return;
        }
}

void Server::runners_tick(uint32_t now) {
    for (auto& [pid, p] : parties_) {
        if (p.empty_since) continue;
        party_elect_leader(p, now);
        // places with present members, in arrival order
        std::map<PartyPlace, std::vector<std::pair<uint32_t, uint64_t>>> places;
        bool runner_anywhere = false;
        for (auto& [kh, at] : p.arrived) {
            Session* t = online_member(kh);
            if (!t || now - t->chan.last_heard_ms() > uint32_t(cfg_.runner_drop_ms)) continue;  // dropped: replaced
            if (t->where.stage_name().empty()) continue;
            places[at.first].push_back({at.second, kh});
        }
        std::vector<RunnerEntry> changes;
        for (auto it = p.runner.begin(); it != p.runner.end();) {
            if (!places.count(it->first)) {
                RunnerEntry e;
                e.where.stage = it->first.stage;
                e.where.room = it->first.room;
                e.runner = 0;
                changes.push_back(e);
                it = p.runner.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& [place, who] : places) {
            std::sort(who.begin(), who.end());
            uint64_t pick = who.front().second;
            auto cur = p.runner.find(place);
            bool cur_here = cur != p.runner.end() &&
                            std::any_of(who.begin(), who.end(), [&](auto& w) { return w.second == cur->second; });
            if (cur_here) pick = cur->second;  // stays with the runner while they are there
            if (p.flags & kPartyRunnerPinned)
                for (auto& w : who)
                    if (w.second == p.leader) pick = p.leader;
            if (cur == p.runner.end() || cur->second != pick) {
                p.runner[place] = pick;
                RunnerEntry e;
                e.where.stage = place.stage;
                e.where.room = place.room;
                Session* t = online_member(pick);
                e.runner = t ? t->id : 0;
                changes.push_back(e);
            }
        }
        for (auto& [place, kh] : p.runner) {
            Session* t = online_member(kh);
            if (t) {
                double rate = cfg_.runner_bytes_per_s;
                if (t->bytes.rate != rate) t->bytes.init(rate, rate * 2 + 4096, now);
                runner_anywhere = true;
            }
        }
        (void)runner_anywhere;
        if (changes.empty()) continue;
        Bytes b = encode_runners(changes);
        for (uint64_t kh : p.members)
            if (Session* t = online_member(kh)) t->chan.send_reliable(kMsgRunner, b);
    }
}

void Server::parties_gc(uint32_t now) {
    uint64_t keep_ms = uint64_t(cfg_.team_state_keep_h) * 3600ull * 1000ull;
    for (auto it = parties_.begin(); it != parties_.end();) {
        Party& p = it->second;
        if (p.empty_since && uint64_t(now - p.empty_since) > keep_ms) {
            for (uint64_t kh : p.members) party_by_key_.erase(kh);
            it = parties_.erase(it);
        } else {
            ++it;
        }
    }
}

// the optional team-state file: what the server knew when it stopped, so a restart keeps the last known states
void Server::save_team_states() {
    if (cfg_.team_state_file.empty()) return;
    std::string tmp = cfg_.team_state_file + ".new";
    std::FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    for (auto& [id, p] : parties_) {
        std::fprintf(f, "P %u %llu %llu %u %zu", p.id, (unsigned long long)p.world, (unsigned long long)p.leader,
                     unsigned(p.flags), p.members.size());
        for (uint64_t kh : p.members) std::fprintf(f, " %llu", (unsigned long long)kh);
        std::fprintf(f, "\n");
        for (auto& [o, fl] : p.team.fields) {
            std::fprintf(f, "F %u %u ", unsigned(o), unsigned(fl.mode));
            for (unsigned char c : fl.value) std::fprintf(f, "%02x", c);
            std::fprintf(f, "\n");
        }
        std::fprintf(f, "V %u\n", p.team.version);
        std::fprintf(f, "I %u\n", p.issued);
    }
    std::fclose(f);
    std::rename(tmp.c_str(), cfg_.team_state_file.c_str());
}

void Server::load_team_states() {
    if (cfg_.team_state_file.empty()) return;
    std::ifstream in(cfg_.team_state_file);
    std::string line;
    Party* cur = nullptr;
    uint32_t now = monotonic_ms();
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        char k = 0;
        ls >> k;
        if (k == 'P') {
            Party p;
            unsigned flags = 0;
            size_t n = 0;
            unsigned long long world = 0, leader = 0;
            ls >> p.id >> world >> leader >> flags >> n;
            p.world = world;
            p.leader = leader;
            p.flags = uint16_t(flags);
            for (size_t i = 0; i < n && i < size_t(kMaxPartySize); ++i) {
                unsigned long long kh = 0;
                ls >> kh;
                p.members.push_back(kh);
            }
            if (!ls || p.members.empty()) {
                cur = nullptr;
                continue;
            }
            p.empty_since = now;
            next_party_ = std::max(next_party_, p.id + 1);
            for (uint64_t kh : p.members) party_by_key_[kh] = p.id;
            cur = &(parties_[p.id] = std::move(p));
        } else if (k == 'F' && cur) {
            unsigned o = 0, mode = 0;
            std::string hex;
            ls >> o >> mode >> hex;
            TeamField f;
            f.offset = uint16_t(o);
            f.mode = uint8_t(mode);
            for (size_t i = 0; i + 1 < hex.size(); i += 2) f.value += char(std::stoi(hex.substr(i, 2), nullptr, 16));
            if (ls && !f.value.empty() && f.value.size() <= kMaxTeamFieldBytes) cur->team.fields[f.offset] = f;
        } else if (k == 'V' && cur) {
            ls >> cur->team.version;
        } else if (k == 'I' && cur) {
            ls >> cur->issued;
        }
    }
}

}  // namespace online::server
