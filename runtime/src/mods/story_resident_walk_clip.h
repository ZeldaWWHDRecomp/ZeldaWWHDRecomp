// SPDX-License-Identifier: MPL-2.0
// A borrowed walk for the villagers without one (Ub, Uw, Sa), the pure part (no game memory): read a
// skeleton and a walk clip from the player's own BFRES files, retarget the clip to another villager's
// skeleton, and write it as a native skeletal animation (FSKA) for that skeleton. Nothing of the game
// ships: the clip is built on the player's machine from the player's files (story_resident_walk_files)
// and lives only in guest memory (story_resident_routes).
//
// All the Windfall people share one 19-bone rig (same names, order and parents; rest poses nearly
// alike, proportions not). The retarget copies joint rotations only:
// - every bone keeps the source's rotation away from its rest pose in model space, applied to the
//   target's own rest pose (Gt = Gs * Gs0^-1 * Gt0): limbs point the same way as the source's, a bone
//   twisted differently at rest (Ub's right upper arm) keeps its own twist, nothing flips; the neck and
//   head take the source's walk relative to its idle onto the target's idle (how it holds its head);
// - the target keeps its own bone lengths (translations) and scale 1; the hips' own movement (the
//   bones under the root) is scaled by the ratio of the leg lengths, and the root height is corrected
//   each frame so the lower ankle follows the source's lower ankle (feet stay on the ground);
// - the clip is resampled so that one cycle covers the target's stride at its play rate (the game plays
//   a walk at speed x factor frames per step): moving at any speed, the planted foot does not slide.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace story_resident_walk_clip {

using Bytes = std::vector<uint8_t>;
inline constexpr unsigned kMaxBones = 64, kMaxFrames = 600;
inline constexpr uint32_t kSkeletonField = 0x24;  // the FSKA's skeleton (self-relative), set where it is bound

struct Bone {
    std::string name;
    int parent = -1;
    double rot[3]{};  // Euler XYZ (R = Rz * Ry * Rx), radians
    double tr[3]{};
};
struct Curve {
    unsigned target = 0;  // byte offset in the bone transform: 0x10.. translation, 0x20.. rotation
    unsigned mode = 0;    // 0 cubic, 1 linear, 2 baked
    double start = 0, end = 0, scale = 1, offset = 0;
    std::vector<double> frames, values;
};
struct BoneAnim {
    std::string name;
    double rot[3]{}, tr[3]{};
    std::vector<Curve> curves;
};
struct Clip {
    std::string name;
    unsigned frames = 0;  // frame count (the last frame is the loop's first again)
    std::vector<BoneAnim> bones;
};

// ---- reading (big endian, every offset checked) ----
struct Reader {
    const Bytes& b;
    bool ok = true;
    bool in(uint64_t at, uint64_t n) { return ok = ok && at + n <= b.size(); }
    uint32_t u32(uint64_t at) { return in(at, 4) ? uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3] : 0; }
    uint16_t u16(uint64_t at) { return in(at, 2) ? uint16_t(b[at] << 8 | b[at + 1]) : 0; }
    uint8_t u8(uint64_t at) { return in(at, 1) ? b[at] : 0; }
    double f32(uint64_t at) { const uint32_t u = u32(at); float f; std::memcpy(&f, &u, 4); return f; }
    // a self-relative offset; 0 for none
    uint64_t rel(uint64_t at) {
        const int32_t v = int32_t(u32(at));
        if (!ok || !v) return 0;
        const int64_t t = int64_t(at) + v;
        if (t < 0 || uint64_t(t) >= b.size()) return ok = false, 0;
        return uint64_t(t);
    }
    std::string str(uint64_t at) {
        if (!ok || at >= b.size()) return ok = false, std::string();
        std::string s;
        for (uint64_t i = at; i < b.size() && b[i]; ++i) {
            if (s.size() > 64) return ok = false, std::string();
            s.push_back(char(b[i]));
        }
        return s;
    }
    // a BFRES index group: (name, data) of its entries
    std::vector<std::pair<std::string, uint64_t>> group(uint64_t field) {
        std::vector<std::pair<std::string, uint64_t>> out;
        const uint64_t g = rel(field);
        if (!g) return out;
        const uint32_t n = u32(g + 4);
        if (n > 4096) return ok = false, out;
        for (uint32_t i = 1; i <= n && ok; ++i) {
            const uint64_t e = g + 8 + 16 * uint64_t(i);
            const uint64_t name = rel(e + 8), data = rel(e + 12);
            out.emplace_back(str(name), data);
        }
        return out;
    }
};
inline uint64_t find(const std::vector<std::pair<std::string, uint64_t>>& g, const std::string& name) {
    for (const auto& e : g)
        if (e.first == name) return e.second;
    return 0;
}
inline bool fres(Reader& r) { return r.in(0, 0x60) && r.b[0] == 'F' && r.b[1] == 'R' && r.b[2] == 'E' && r.b[3] == 'S'; }

// The skeleton of model `model` in a BFRES file.
inline bool skeleton(const Bytes& bfres, const std::string& model, std::vector<Bone>& out, std::string& why) {
    Reader r{bfres};
    out.clear();
    if (!fres(r)) return why = "not a BFRES file", false;
    const uint64_t fmdl = find(r.group(0x20), model);
    if (!r.ok || !fmdl || !r.in(fmdl, 0x10) || std::memcmp(&bfres[fmdl], "FMDL", 4)) return why = "no model " + model, false;
    const uint64_t sk = r.rel(fmdl + 0xC);
    if (!sk || !r.in(sk, 0x24) || std::memcmp(&bfres[sk], "FSKL", 4)) return why = "no skeleton in " + model, false;
    if ((r.u32(sk + 4) & 0x7000) != 0x1000) return why = "the skeleton of " + model + " does not use Euler rotations", false;
    const unsigned n = r.u16(sk + 8);
    const uint64_t bones = r.rel(sk + 0x14);
    if (!n || n > kMaxBones || !bones || !r.in(bones, uint64_t(n) * 0x40)) return why = "bad skeleton in " + model, false;
    for (unsigned i = 0; i < n; ++i) {
        const uint64_t p = bones + uint64_t(i) * 0x40;
        Bone b;
        b.name = r.str(r.rel(p));
        b.parent = int16_t(r.u16(p + 6));
        for (int k = 0; k < 3; ++k) b.rot[k] = r.f32(p + 0x20 + 4 * k), b.tr[k] = r.f32(p + 0x30 + 4 * k);
        if (b.parent >= int(i) || b.parent < -1 || (b.parent < 0) != (i == 0) || r.u16(p + 4) != i) return why = "bad bone order in " + model, false;
        out.push_back(std::move(b));
    }
    if (!r.ok) return why = "bad skeleton in " + model, false;
    return true;
}

// The skeletal animation `name` in a BFRES file (cubic, linear and baked float curves).
inline bool clip(const Bytes& bfres, const std::string& name, Clip& out, std::string& why) {
    Reader r{bfres};
    out = Clip{};
    if (!fres(r)) return why = "not a BFRES file", false;
    const uint64_t a = find(r.group(0x28), name);
    if (!r.ok || !a || !r.in(a, 0x30) || std::memcmp(&bfres[a], "FSKA", 4)) return why = "no animation " + name, false;
    if ((r.u32(a + 0xC) & 0x7000) != 0x1000) return why = name + " does not use Euler rotations", false;
    out.name = name;
    out.frames = r.u32(a + 0x10);
    const unsigned n = r.u16(a + 0x14);
    const uint64_t bones = r.rel(a + 0x20);
    if (out.frames < 2 || out.frames > kMaxFrames || !n || n > kMaxBones || !bones || !r.in(bones, uint64_t(n) * 0x18)) return why = "bad animation " + name, false;
    for (unsigned i = 0; i < n && r.ok; ++i) {
        const uint64_t p = bones + uint64_t(i) * 0x18;
        BoneAnim b;
        const uint32_t flags = r.u32(p);
        b.name = r.str(r.rel(p + 4));
        const unsigned curves = r.u8(p + 0xA);
        uint64_t base = r.rel(p + 0x14);
        if (flags & 0x08) base += 12;
        if (flags & 0x10) { for (int k = 0; k < 3; ++k) b.rot[k] = r.f32(base + 4 * k); base += 16; }
        if (flags & 0x20) { for (int k = 0; k < 3; ++k) b.tr[k] = r.f32(base + 4 * k); }
        const uint64_t ca = curves ? r.rel(p + 0x10) : 0;
        for (unsigned k = 0; k < curves && r.ok; ++k) {
            const uint64_t c = ca + uint64_t(k) * 0x24;
            Curve cv;
            const unsigned cf = r.u16(c), keys = r.u16(c + 2), frame_type = cf & 3, value_type = (cf >> 2) & 3;
            cv.mode = (cf >> 4) & 7;
            cv.target = r.u32(c + 4);
            cv.start = r.f32(c + 8), cv.end = r.f32(c + 0xC), cv.scale = r.f32(c + 0x10), cv.offset = r.f32(c + 0x14);
            if (cv.mode > 2 || !keys || keys > 4096 || frame_type > 2 || value_type > 2 || !(cv.end >= cv.start)) return why = "unsupported curve in " + name, false;
            if (cv.mode != 2) {
                const uint64_t fr = r.rel(c + 0x1C);
                for (unsigned j = 0; j < keys; ++j)
                    cv.frames.push_back(frame_type == 0 ? r.f32(fr + 4 * j) : frame_type == 1 ? int16_t(r.u16(fr + 2 * j)) / 32.0 : r.u8(fr + j));
                for (unsigned j = 1; j < keys; ++j)
                    if (!(cv.frames[j] > cv.frames[j - 1])) return why = "unordered keys in " + name, false;
            }
            const unsigned per = cv.mode == 0 ? 4 : cv.mode == 1 ? 2 : 1;
            const uint64_t kd = r.rel(c + 0x20);
            for (unsigned j = 0; j < keys * per; ++j)
                cv.values.push_back(value_type == 0 ? r.f32(kd + 4 * j) : value_type == 1 ? double(int16_t(r.u16(kd + 2 * j))) : double(int8_t(r.u8(kd + j))));
            if (value_type == 0) cv.scale = 1;  // float keys are the values themselves
            for (double v : cv.values)
                if (!std::isfinite(v)) return why = "bad curve value in " + name, false;
            b.curves.push_back(std::move(cv));
        }
        out.bones.push_back(std::move(b));
    }
    if (!r.ok) return why = "bad animation " + name, false;
    return true;
}

inline double evaluate(const Curve& c, double t) {
    t = std::clamp(t, c.start, c.end);
    auto value = [&](double v) { return v * c.scale + c.offset; };
    if (c.mode == 2) {
        const double i = t - c.start;
        const size_t i0 = std::min(size_t(std::floor(i)), c.values.size() - 1), i1 = std::min(i0 + 1, c.values.size() - 1);
        const double u = i - std::floor(i);
        return value(c.values[i0] * (1 - u) + c.values[i1] * u);
    }
    const size_t n = c.frames.size();
    size_t j = 0;
    while (j + 1 < n && c.frames[j + 1] <= t) ++j;
    const double span = j + 1 < n ? c.frames[j + 1] - c.frames[j] : 1;
    const double u = span > 0 ? (t - c.frames[j]) / span : 0;
    if (c.mode == 0) {
        const double* k = &c.values[j * 4];
        return value(k[0] + k[1] * u + k[2] * u * u + k[3] * u * u * u);
    }
    const double* k = &c.values[j * 2];
    return value(k[0] + k[1] * u);
}

// ---- rotations ----
struct M3 {
    double m[3][3]{};
    static M3 identity() { M3 r; for (int i = 0; i < 3; ++i) r.m[i][i] = 1; return r; }
    M3 operator*(const M3& o) const {
        M3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
    M3 t() const { M3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i]; return r; }
    std::array<double, 3> apply(const double v[3]) const {
        return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2], m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
    }
};
inline M3 euler(const double e[3]) {
    const double cx = std::cos(e[0]), sx = std::sin(e[0]), cy = std::cos(e[1]), sy = std::sin(e[1]), cz = std::cos(e[2]), sz = std::sin(e[2]);
    M3 r;  // Rz * Ry * Rx
    r.m[0][0] = cz * cy, r.m[0][1] = cz * sy * sx - sz * cx, r.m[0][2] = cz * sy * cx + sz * sx;
    r.m[1][0] = sz * cy, r.m[1][1] = sz * sy * sx + cz * cx, r.m[1][2] = sz * sy * cx - cz * sx;
    r.m[2][0] = -sy, r.m[2][1] = cy * sx, r.m[2][2] = cy * cx;
    return r;
}
// Euler XYZ angles of R nearest to `close_to` (both solutions, each unwrapped by whole turns)
inline std::array<double, 3> to_euler(const M3& R, const double close_to[3]) {
    const double sy = std::clamp(-R.m[2][0], -1.0, 1.0), y = std::asin(sy);
    double x, z;
    if (std::fabs(std::cos(y)) > 1e-6) x = std::atan2(R.m[2][1], R.m[2][2]), z = std::atan2(R.m[1][0], R.m[0][0]);
    else z = close_to[2], x = std::atan2(-R.m[1][2], R.m[1][1]) + (sy > 0 ? z : -z);
    const double pi = 3.14159265358979323846;
    const std::array<double, 3> cand[2] = {{x, y, z}, {x + pi, pi - y, z + pi}};
    std::array<double, 3> best{};
    double best_d = 1e300;
    for (const auto& c : cand) {
        std::array<double, 3> u;
        double d = 0;
        for (int k = 0; k < 3; ++k) u[k] = c[k] + 2 * pi * std::round((close_to[k] - c[k]) / (2 * pi)), d += (u[k] - close_to[k]) * (u[k] - close_to[k]);
        if (d < best_d) best_d = d, best = u;
    }
    return best;
}
inline double angle_between(const M3& a, const M3& b) {  // the rotation angle of a^-1 b
    const M3 d = a.t() * b;
    return std::acos(std::clamp((d.m[0][0] + d.m[1][1] + d.m[2][2] - 1) / 2, -1.0, 1.0));
}

struct Pose {
    std::vector<M3> local;
    std::vector<std::array<double, 3>> tr;
};
struct World {
    std::vector<M3> rot;
    std::vector<std::array<double, 3>> pos;
};
inline World fk(const std::vector<Bone>& s, const Pose& p) {
    World w;
    w.rot.resize(s.size()), w.pos.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i].parent < 0) { w.rot[i] = p.local[i]; w.pos[i] = p.tr[i]; continue; }
        const size_t q = size_t(s[i].parent);
        w.rot[i] = w.rot[q] * p.local[i];
        const auto d = w.rot[q].apply(p.tr[i].data());
        for (int k = 0; k < 3; ++k) w.pos[i][k] = w.pos[q][k] + d[k];
    }
    return w;
}
inline Pose rest(const std::vector<Bone>& s) {
    Pose p;
    for (const auto& b : s) p.local.push_back(euler(b.rot)), p.tr.push_back({b.tr[0], b.tr[1], b.tr[2]});
    return p;
}
// the clip's pose at frame t on skeleton s (a bone the clip has no track for keeps its rest)
inline Pose sample(const std::vector<Bone>& s, const Clip& c, const std::vector<int>& track, double t) {
    Pose p = rest(s);
    for (size_t i = 0; i < s.size(); ++i) {
        if (track[i] < 0) continue;
        const BoneAnim& b = c.bones[size_t(track[i])];
        double r[3] = {b.rot[0], b.rot[1], b.rot[2]}, tr[3] = {b.tr[0], b.tr[1], b.tr[2]};
        for (const auto& cv : b.curves) {
            const double v = evaluate(cv, t);
            if (cv.target >= 0x20 && cv.target <= 0x28 && !(cv.target & 3)) r[(cv.target - 0x20) / 4] = v;
            else if (cv.target >= 0x10 && cv.target <= 0x18 && !(cv.target & 3)) tr[(cv.target - 0x10) / 4] = v;
        }
        p.local[i] = euler(r);
        p.tr[i] = {tr[0], tr[1], tr[2]};
    }
    return p;
}

// ---- the retarget ----
struct Built {
    std::string name;
    unsigned end = 0;                                         // the last frame (= the first again)
    std::vector<std::vector<std::array<double, 3>>> rot;      // [bone][frame 0..end]
    std::vector<std::vector<std::array<double, 3>>> tr;       // [bone][frame 0..end]
    std::vector<bool> moves;                                  // bones with translation curves (under the root)
    double stride = 0;                                        // units the body moves in one cycle (no sliding)
    double leg_ratio = 1;
    double largest_step = 0;                                  // largest bone rotation change between frames (radians)
};
inline int index_of(const std::vector<Bone>& s, const char* name) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i].name == name) return int(i);
    return -1;
}
// `factor`: the target's play rate per unit of speed (the game plays a walk at speed x factor frames
// per step; the stride is matched to it)
// `src_idle`, `dst_idle` (optional, both or neither): the two characters' own idles. The neck and head
// then take the source's walk relative to its idle onto the target's idle (a character's head is held
// its own way: the sailors look ahead and down, which their rest pose does not show).
inline bool retarget(const std::vector<Bone>& src, const Clip& clip, const std::vector<Bone>& dst, double factor, const std::string& name, Built& out, std::string& why,
                     const Clip* src_idle = nullptr, const Clip* dst_idle = nullptr) {
    out = Built{};
    out.name = name;
    if (src.size() != dst.size() || src.size() < 2) return why = "the two villagers' skeletons differ", false;
    for (size_t i = 0; i < src.size(); ++i)
        if (src[i].name != dst[i].name || src[i].parent != dst[i].parent) return why = "the two villagers' skeletons differ at bone " + dst[i].name, false;
    std::vector<int> track(src.size(), -1);
    for (size_t i = 0; i < src.size(); ++i)
        for (size_t j = 0; j < clip.bones.size(); ++j)
            if (clip.bones[j].name == src[i].name) track[i] = int(j);
    const int fl = index_of(dst, "footL"), fr = index_of(dst, "footR"), ll = index_of(dst, "legL2");
    if (fl < 0 || fr < 0 || ll < 0 || !(factor > 0.1 && factor < 10)) return why = "the skeleton has no legs", false;
    const double leg_src = src[size_t(ll)].tr[0] + src[size_t(fl)].tr[0], leg_dst = dst[size_t(ll)].tr[0] + dst[size_t(fl)].tr[0];
    if (!(leg_src > 1 && leg_dst > 1)) return why = "the skeleton has no legs", false;
    const double k = leg_dst / leg_src;
    out.leg_ratio = k;
    const Pose src_rest = rest(src), dst_rest = rest(dst);
    const World ws0 = fk(src, src_rest), wt0 = fk(dst, dst_rest);
    // the reference each bone's rotation is taken from: the rest pose, or (neck, head) the idles
    std::vector<M3> ref_s = ws0.rot, ref_t = wt0.rot;
    if (src_idle && dst_idle) {
        auto tracks = [](const std::vector<Bone>& s, const Clip& c) {
            std::vector<int> t(s.size(), -1);
            for (size_t i = 0; i < s.size(); ++i)
                for (size_t j = 0; j < c.bones.size(); ++j)
                    if (c.bones[j].name == s[i].name) t[i] = int(j);
            return t;
        };
        const World is = fk(src, sample(src, *src_idle, tracks(src, *src_idle), 0)), it = fk(dst, sample(dst, *dst_idle, tracks(dst, *dst_idle), 0));
        for (const char* bone : {"neck", "head"})
            if (const int i = index_of(dst, bone); i >= 0) ref_s[size_t(i)] = is.rot[size_t(i)], ref_t[size_t(i)] = it.rot[size_t(i)];
    }
    const double ankle_s0 = std::min(ws0.pos[size_t(fl)][1], ws0.pos[size_t(fr)][1]), ankle_t0 = std::min(wt0.pos[size_t(fl)][1], wt0.pos[size_t(fr)][1]);
    const size_t n = dst.size();
    out.moves.assign(n, false);
    for (size_t i = 0; i < n; ++i) out.moves[i] = dst[i].parent == 0;
    // the target's pose at source time t
    auto pose = [&](double t, World* world) {
        const Pose ps = sample(src, clip, track, t);
        const World ws = fk(src, ps);
        std::vector<M3> g(n);
        for (size_t i = 0; i < n; ++i) g[i] = ws.rot[i] * ref_s[i].t() * ref_t[i];
        Pose pt = dst_rest;
        for (size_t i = 0; i < n; ++i) {
            pt.local[i] = dst[i].parent < 0 ? g[i] : g[size_t(dst[i].parent)].t() * g[i];
            if (out.moves[i])
                for (int c = 0; c < 3; ++c) pt.tr[i][c] = dst[i].tr[c] + (ps.tr[i][c] - src[i].tr[c]) * k;
        }
        World wt = fk(dst, pt);
        const double hs = std::min(ws.pos[size_t(fl)][1], ws.pos[size_t(fr)][1]) - ankle_s0, ht = std::min(wt.pos[size_t(fl)][1], wt.pos[size_t(fr)][1]) - ankle_t0;
        const double dy = hs * k - ht;
        for (size_t i = 0; i < n; ++i)
            if (out.moves[i]) pt.tr[i][1] += dy;
        if (world) *world = fk(dst, pt);
        return pt;
    };
    // the stride: twice the way a foot travels (forward, z) in one cycle
    const unsigned src_end = clip.frames - 1;
    double lo = 1e300, hi = -1e300;
    for (unsigned f = 0; f <= src_end; ++f) {
        World w;
        pose(f, &w);
        lo = std::min(lo, w.pos[size_t(fl)][2]), hi = std::max(hi, w.pos[size_t(fl)][2]);
    }
    out.stride = 2 * (hi - lo);
    const double frames = std::round(out.stride * factor);
    if (!(out.stride > 4) || frames < 8 || frames > kMaxFrames - 2) return why = "the walk has no usable stride", false;
    out.end = unsigned(frames);
    out.rot.assign(n, {}), out.tr.assign(n, {});
    std::vector<M3> previous;
    for (unsigned f = 0; f <= out.end; ++f) {
        const Pose p = pose(double(f) * src_end / out.end, nullptr);
        for (size_t i = 0; i < n; ++i) {
            const double* close_to = f ? out.rot[i].back().data() : dst[i].rot;
            out.rot[i].push_back(to_euler(p.local[i], close_to));
            out.tr[i].push_back(p.tr[i]);
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(out.rot[i].back()[c]) || !std::isfinite(out.tr[i].back()[c])) return why = "the retarget is not finite", false;
        }
        if (!previous.empty())
            for (size_t i = 0; i < n; ++i) out.largest_step = std::max(out.largest_step, angle_between(previous[i], p.local[i]));
        previous = p.local;
    }
    return true;
}

// ---- writing: one FSKA, every offset relative to itself; the skeleton field (kSkeletonField) is left
// 0 for the place that binds it ----
struct Writer {
    Bytes b;
    void align(size_t a) { while (b.size() % a) b.push_back(0); }
    size_t add(size_t n, size_t a = 4) { align(a); const size_t at = b.size(); b.resize(at + n); return at; }
    void put32(size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (24 - 8 * i)); }
    void put16(size_t at, uint16_t v) { b[at] = uint8_t(v >> 8), b[at + 1] = uint8_t(v); }
    void putf(size_t at, double v) { const float f = float(v); uint32_t u; std::memcpy(&u, &f, 4); put32(at, u); }
    void rel(size_t field, size_t target) { put32(field, uint32_t(int32_t(int64_t(target) - int64_t(field)))); }
    size_t string(const std::string& s) {  // BFRES string: its length, then the characters (the offset points at them)
        const size_t at = add(4 + s.size() + 1);
        put32(at, uint32_t(s.size()));
        std::memcpy(&b[at + 4], s.data(), s.size());
        return at + 4;
    }
};
inline Bytes encode(const Built& c, const std::vector<Bone>& dst) {
    Writer w;
    const size_t n = dst.size(), head = w.add(0x30);
    std::memcpy(&w.b[head], "FSKA", 4);
    w.rel(head + 4, w.string(c.name));
    w.rel(head + 8, w.string(""));
    w.put32(head + 0xC, 0x1105);  // looping, baked curves, Euler rotations (the port's own clips' layout)
    w.put32(head + 0x10, c.end + 1);
    w.put16(head + 0x14, uint16_t(n));
    const size_t bones = w.add(0x18 * n);
    w.rel(head + 0x20, bones);
    const size_t bind = w.add(2 * n);
    for (size_t i = 0; i < n; ++i) w.put16(bind + 2 * i, uint16_t(i));  // bound bone by bone: the skeletons match
    w.rel(head + 0x28, bind);
    unsigned curve_count = 0, baked = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t at = bones + 0x18 * i;
        std::vector<std::pair<unsigned, std::vector<double>>> curves;  // (target offset, values)
        uint32_t flags = 0x38;
        if (dst[i].parent >= 0)
            for (int k = 0; k < 3; ++k) {
                std::vector<double> v;
                for (const auto& r : c.rot[i]) v.push_back(r[k]);
                curves.push_back({0x20u + 4 * unsigned(k), std::move(v)});
                flags |= 1u << (9 + k);
            }
        if (c.moves[i])
            for (int k = 0; k < 3; ++k) {
                std::vector<double> v;
                for (const auto& t : c.tr[i]) v.push_back(t[k]);
                curves.push_back({0x10u + 4 * unsigned(k), std::move(v)});
                flags |= 1u << (13 + k);
            }
        w.put32(at, flags);
        w.rel(at + 4, w.string(dst[i].name));
        w.b[at + 8] = 3, w.b[at + 9] = 6, w.b[at + 0xA] = uint8_t(curves.size()), w.b[at + 0xC] = uint8_t(curve_count);
        const size_t base = w.add(40);
        const double* r0 = dst[i].parent >= 0 ? c.rot[i][0].data() : dst[i].rot;
        const double* t0 = c.moves[i] ? c.tr[i][0].data() : dst[i].tr;
        const double values[10] = {1, 1, 1, r0[0], r0[1], r0[2], 1, t0[0], t0[1], t0[2]};
        for (int k = 0; k < 10; ++k) w.putf(base + 4 * k, values[k]);
        w.rel(at + 0x14, base);
        if (!curves.empty()) {
            const size_t rows = w.add(0x24 * curves.size());
            w.rel(at + 0x10, rows);
            for (size_t j = 0; j < curves.size(); ++j) {
                const size_t cv = rows + 0x24 * j;
                auto keys = curves[j].second;
                keys.push_back(keys.back());  // the baked evaluator reads one past the last frame
                w.put16(cv, 0x20);            // baked float values, float frames
                w.put16(cv + 2, uint16_t(keys.size()));
                w.put32(cv + 4, curves[j].first);
                w.putf(cv + 8, 0), w.putf(cv + 0xC, double(c.end)), w.putf(cv + 0x10, 1), w.putf(cv + 0x14, 0), w.putf(cv + 0x18, keys.back() - keys.front());
                const size_t data = w.add(4 * keys.size());
                for (size_t q = 0; q < keys.size(); ++q) w.putf(data + 4 * q, keys[q]);
                w.rel(cv + 0x20, data);
                baked += unsigned(4 * keys.size());
            }
        }
        curve_count += unsigned(curves.size());
    }
    w.put32(head + 0x18, curve_count);
    w.put32(head + 0x1C, baked);
    w.align(4);
    return w.b;
}

}  // namespace story_resident_walk_clip
