// SPDX-License-Identifier: MPL-2.0
#include "messages.h"

#include <cmath>
#include <cstring>

namespace online::proto {

namespace {
void s24(Writer& w, int32_t v) {
    if (v > 0x7FFFFF) v = 0x7FFFFF;
    if (v < -0x800000) v = -0x800000;
    uint32_t u = uint32_t(v) & 0xFFFFFF;
    w.u8(uint8_t(u));
    w.u8(uint8_t(u >> 8));
    w.u8(uint8_t(u >> 16));
}
int32_t get_s24(Reader& r) {
    uint32_t u = r.u8();
    u |= uint32_t(r.u8()) << 8;
    u |= uint32_t(r.u8()) << 16;
    if (u & 0x800000) u |= 0xFF000000u;
    return int32_t(u);
}
int32_t q(float f, float step) { return int32_t(std::lround(f / step)); }
}  // namespace

void put_near(Writer& w, uint16_t id, uint32_t ms, const PoseHeader& h, const uint8_t* blob, size_t n) {
    w.u16(id);
    w.u8(kTierNear);
    w.u32(ms);
    put_pose_header(w, h);
    w.u16(uint16_t(n));
    w.bytes(blob, n);
}

void put_mid(Writer& w, uint16_t id, uint32_t ms, const PoseHeader& h) {
    w.u16(id);
    w.u8(kTierMid);
    s24(w, q(h.x, 1));
    s24(w, q(h.z, 1));
    int32_t y = q(h.y, 2);
    w.s16(int16_t(y > 32767 ? 32767 : y < -32768 ? -32768 : y));
    w.u16(h.yaw);
    w.u16(h.anim);
    w.u8(h.frame);
    w.u8(h.flags);
    w.u8(h.boat);
    w.u16(uint16_t(ms));
}

void put_far(Writer& w, uint16_t id, const PoseHeader& h) {
    w.u16(id);
    w.u8(kTierFar);
    s24(w, q(h.x, 1));
    s24(w, q(h.z, 1));
    w.u8(uint8_t(h.yaw >> 8));
    w.u8(h.flags);
}

bool get_pose_batch(const uint8_t* d, size_t n, std::vector<PoseEntry>& out) {
    Reader r(d, n);
    while (r.ok() && r.left()) {
        PoseEntry e;
        e.id = r.u16();
        e.tier = Tier(r.u8());
        if (e.tier == kTierNear) {
            e.ms = r.u32();
            e.head = get_pose_header(r);
            size_t bn = r.u16();
            if (bn > kMaxPoseBlob) return false;
            e.blob.resize(bn);
            r.bytes(e.blob.data(), bn);
        } else if (e.tier == kTierMid) {
            e.head.x = float(get_s24(r));
            e.head.z = float(get_s24(r));
            e.head.y = float(r.s16()) * 2;
            e.head.yaw = r.u16();
            e.head.anim = r.u16();
            e.head.frame = r.u8();
            e.head.flags = r.u8();
            e.head.boat = r.u8();
            e.ms = r.u16();
        } else if (e.tier == kTierFar) {
            e.head.x = float(get_s24(r));
            e.head.z = float(get_s24(r));
            e.head.yaw = uint16_t(r.u8() << 8);
            e.head.flags = r.u8();
        } else {
            return false;
        }
        if (!r.ok()) return false;
        out.push_back(std::move(e));
    }
    return r.ok();
}

void put_marker(Writer& w, const Marker& m) {
    w.u16(m.id);
    w.u8(m.square);
    w.u8(m.x);
    w.u8(m.z);
    w.u8(m.flags);
}

bool get_markers(const uint8_t* d, size_t n, std::vector<Marker>& out) {
    if (n % kMarkerBytes) return false;
    Reader r(d, n);
    while (r.left()) {
        Marker m;
        m.id = r.u16();
        m.square = r.u8();
        m.x = r.u8();
        m.z = r.u8();
        m.flags = r.u8();
        out.push_back(m);
    }
    return r.ok();
}

Bytes encode_player_in(const PlayerIn& p) {
    Bytes b;
    Writer w(b);
    w.u16(p.id);
    w.u64(p.key_hash);
    w.u32(p.look);
    w.u8(p.flags);
    w.str8(p.name.substr(0, 24));
    return b;
}

bool decode_player_in(const uint8_t* d, size_t n, PlayerIn& p) {
    Reader r(d, n);
    p.id = r.u16();
    p.key_hash = r.u64();
    p.look = r.u32();
    p.flags = r.u8();
    p.name = r.str8(24);
    return r.done();
}

void put_team_fields(Writer& w, const std::vector<TeamField>& f) {
    w.u16(uint16_t(f.size()));
    for (auto& x : f) {
        w.u16(x.offset);
        w.u8(x.mode);
        w.u8(uint8_t(x.value.size()));
        w.bytes(x.value.data(), x.value.size());
    }
}

bool get_team_fields(Reader& r, std::vector<TeamField>& out) {
    size_t n = r.u16();
    if (n > kMaxTeamFields) return false;
    out.reserve(out.size() + n);
    for (size_t i = 0; i < n && r.ok(); ++i) {
        TeamField f;
        f.offset = r.u16();
        f.mode = r.u8();
        size_t len = r.u8();
        if (len == 0 || len > kMaxTeamFieldBytes || f.mode > kFieldFill) return false;
        f.value = std::string(r.view(len));
        out.push_back(std::move(f));
    }
    return r.ok();
}

Bytes encode_app(const AppHeader& h, const uint8_t* body, size_t n) {
    Bytes b;
    Writer w(b);
    w.u8(h.type);
    w.u8(h.target);
    if (h.target == kToMember) w.u16(h.member);
    w.bytes(body, n);
    return b;
}

bool decode_app(const uint8_t* d, size_t n, AppHeader& h, const uint8_t*& body, size_t& body_n) {
    Reader r(d, n);
    h.type = r.u8();
    h.target = r.u8();
    if (h.target > kToPlace) return false;
    if (h.target == kToMember) h.member = r.u16();
    if (!r.ok()) return false;
    body = r.here();
    body_n = r.left();
    return true;
}

Bytes encode_party_state(const PartyState& s) {
    Bytes b;
    Writer w(b);
    w.u32(s.party);
    w.u16(s.leader);
    w.u16(s.flags);
    w.u8(uint8_t(s.members.size()));
    for (auto& m : s.members) {
        w.u16(m.id);
        w.u64(m.key_hash);
        w.u8(m.online ? 1 : 0);
        w.str8(m.name.substr(0, 24));
    }
    return b;
}

bool decode_party_state(const uint8_t* d, size_t n, PartyState& s) {
    Reader r(d, n);
    s.party = r.u32();
    s.leader = r.u16();
    s.flags = r.u16();
    size_t c = r.u8();
    if (c > kMaxPartySize) return false;
    for (size_t i = 0; i < c; ++i) {
        PartyMember m;
        m.id = r.u16();
        m.key_hash = r.u64();
        m.online = r.u8() != 0;
        m.name = r.str8(24);
        s.members.push_back(std::move(m));
    }
    return r.done();
}

Bytes encode_runners(const std::vector<RunnerEntry>& v) {
    Bytes b;
    Writer w(b);
    w.u8(uint8_t(v.size() > 255 ? 255 : v.size()));
    for (size_t i = 0; i < v.size() && i < 255; ++i) {
        put_where(w, v[i].where);
        w.u16(v[i].runner);
    }
    return b;
}

bool decode_runners(const uint8_t* d, size_t n, std::vector<RunnerEntry>& out) {
    Reader r(d, n);
    size_t c = r.u8();
    for (size_t i = 0; i < c && r.ok(); ++i) {
        RunnerEntry e;
        e.where = get_where(r);
        e.runner = r.u16();
        out.push_back(e);
    }
    return r.done();
}

static Bytes party_record_body(const PartyRecord& r) {
    Bytes b;
    Writer w(b);
    w.u8(1);
    w.u32(r.party);
    w.u64(r.leader);
    w.u16(r.flags);
    w.u32(r.issued);
    w.u8(uint8_t(r.members.size()));
    for (uint64_t k : r.members) w.u64(k);
    return b;
}

Bytes encode_party_record(const PartyRecord& r) {
    Bytes b = party_record_body(r);
    Writer w(b);
    w.u8(r.is_signed ? 1 : 0);
    if (r.is_signed) {
        w.bytes(r.pub, 32);
        w.bytes(r.sig, 64);
    }
    return b;
}

bool decode_party_record(const uint8_t* d, size_t n, PartyRecord& r) {
    Reader rd(d, n);
    r = PartyRecord();
    if (rd.u8() != 1) return false;
    r.party = rd.u32();
    r.leader = rd.u64();
    r.flags = rd.u16();
    r.issued = rd.u32();
    size_t m = rd.u8();
    if (m < 1 || m > size_t(kMaxPartySize)) return false;
    for (size_t i = 0; i < m; ++i) r.members.push_back(rd.u64());
    r.is_signed = rd.u8() != 0;
    if (r.is_signed) {
        rd.bytes(r.pub, 32);
        rd.bytes(r.sig, 64);
    }
    return rd.done();
}

static Bytes party_record_signed_bytes(const PartyRecord& r) {
    static const char kLabel[] = "wwhd-online party";
    Bytes b(kLabel, kLabel + sizeof kLabel - 1);
    Bytes body = party_record_body(r);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

void sign_party_record(PartyRecord& r, const crypto::Identity& id) {
    std::memcpy(r.pub, id.pub, 32);
    Bytes b = party_record_signed_bytes(r);
    crypto::sign(id, b.data(), b.size(), r.sig);
    r.is_signed = true;
}

PartyRecord make_party_record(const PartyState& st, const crypto::Identity& id, uint32_t unix_now) {
    PartyRecord r;
    r.party = st.party;
    r.leader = key_hash(id.pub);
    r.flags = st.flags;
    r.issued = unix_now;
    r.members.push_back(r.leader);
    for (auto& m : st.members)
        if (m.key_hash != r.leader) r.members.push_back(m.key_hash);
    sign_party_record(r, id);
    return r;
}

bool party_record_signed_by_leader(const PartyRecord& r) {
    if (!r.is_signed || key_hash(r.pub) != r.leader) return false;
    Bytes b = party_record_signed_bytes(r);
    return crypto::verify(r.pub, b.data(), b.size(), r.sig);
}

std::string stranger_name(uint64_t kh) {
    static const char a[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string s = "Sailor ";
    uint64_t v = kh >> 40;
    for (int i = 0; i < 4; ++i) {
        s += a[v & 31];
        v >>= 5;
    }
    return s;
}

}  // namespace online::proto
