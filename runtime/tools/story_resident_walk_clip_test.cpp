// A borrowed walk (story_resident_walk_clip.h) on synthetic files, no game data: reading a BFRES
// skeleton and walk clip (linear, cubic and baked curves), the retarget (the same rig gives the same
// pose back; another rig keeps its own bone lengths and rest twist, its stride scales with its legs and
// its feet stay on the ground; nothing flips), the written FSKA read back, and damaged files refused.
// With WWHD_TEST_GAME=<game folder> it also builds the real borrowed walks from those files (nothing
// is written).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "mods/story_resident_types.h"
#include "mods/story_resident_walk_clip.h"
#include "mods/story_resident_walk_files.h"

namespace clip = story_resident_walk_clip;
using Bytes = clip::Bytes;

namespace {
constexpr double kPi = 3.14159265358979323846, kDeg = kPi / 180;

struct BoneDef { const char* name; int parent; double rot[3]; double tr[3]; };
// the villagers' leg layout (degrees, units), with one arm for the twist
std::vector<BoneDef> rig(double legs, double twist) {
    return {
        {"world_root", -1, {0, 0, 0}, {0, 0, 0}},
        {"backbone", 0, {90, 0, 90}, {0, 4.6 + 15 * legs / 42 + 42 * legs / 42, 0}},
        {"armR1", 1, {twist, 0, 0}, {50, 0, -8}},
        {"armR2", 2, {0, 0, 0}, {25, 0, 0}},
        {"waist", 0, {-90, 0, -90}, {0, 4.6 + 15 * legs / 42 + 42 * legs / 42, 0}},
        {"legL1", 4, {0, 0, 0}, {15 * legs / 42, 0, 12}},
        {"legL2", 5, {0, 0, 0}, {26 * legs / 42, 0, 0}},
        {"footL", 6, {0, 0, -90}, {16 * legs / 42, 0, 0}},
        {"legR1", 4, {180, 0, 0}, {15 * legs / 42, 0, -12}},
        {"legR2", 8, {0, 0, 0}, {26 * legs / 42, 0, 0}},
        {"footR", 9, {0, 0, 90}, {16 * legs / 42, 0, 0}},
        {"neck", 1, {0, 0, 0}, {55 * legs / 42, 0, 0}},
    };
}

// a little BFRES writer: one model with a skeleton, and the animations given
struct File : clip::Writer {
    size_t group(const std::vector<std::pair<std::string, size_t>>& entries) {
        const size_t g = add(8 + 16 * (entries.size() + 1));
        put32(g, uint32_t(8 + 16 * (entries.size() + 1)));
        put32(g + 4, uint32_t(entries.size()));
        for (size_t i = 0; i < entries.size(); ++i) {
            const size_t e = g + 8 + 16 * (i + 1);
            rel(e + 8, string(entries[i].first));
            rel(e + 12, entries[i].second);
        }
        return g;
    }
};
Bytes bfres(const std::vector<BoneDef>& bones, const std::vector<std::pair<std::string, Bytes>>& anims) {
    File f;
    const size_t head = f.add(0x60);
    std::memcpy(&f.b[head], "FRES", 4);
    const size_t fmdl = f.add(0x20);
    std::memcpy(&f.b[fmdl], "FMDL", 4);
    const size_t sk = f.add(0x24);
    std::memcpy(&f.b[sk], "FSKL", 4);
    f.put32(sk + 4, 0x1200);
    f.put16(sk + 8, uint16_t(bones.size()));
    const size_t array = f.add(0x40 * bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const size_t p = array + 0x40 * i;
        f.rel(p, f.string(bones[i].name));
        f.put16(p + 4, uint16_t(i));
        f.put16(p + 6, uint16_t(int16_t(bones[i].parent)));
        for (int k = 0; k < 3; ++k) f.putf(p + 0x14 + 4 * k, 1), f.putf(p + 0x20 + 4 * k, bones[i].rot[k] * kDeg), f.putf(p + 0x30 + 4 * k, bones[i].tr[k]);
        f.putf(p + 0x2C, 1);
    }
    f.rel(sk + 0x14, array);
    f.rel(fmdl + 0xC, sk);
    f.rel(head + 0x20, f.group({{"body", fmdl}}));
    std::vector<std::pair<std::string, size_t>> anim_entries;
    for (const auto& a : anims) {
        const size_t at = f.add(a.second.size());
        std::memcpy(&f.b[at], a.second.data(), a.second.size());
        anim_entries.push_back({a.first, at});
    }
    f.rel(head + 0x28, f.group(anim_entries));
    return f.b;
}
std::vector<clip::Bone> bones_of(const std::vector<BoneDef>& d) {
    std::vector<clip::Bone> out;
    for (const auto& b : d) {
        clip::Bone x;
        x.name = b.name, x.parent = b.parent;
        for (int k = 0; k < 3; ++k) x.rot[k] = b.rot[k] * kDeg, x.tr[k] = b.tr[k];
        out.push_back(x);
    }
    return out;
}
// the source walk: legs swing about the waist's side axis (opposite phases), knees bend, the hips bob,
// the arm swings; written with the port's own encoder (baked curves), at `frames` frames
Bytes source_walk(const std::vector<BoneDef>& d, unsigned end) {
    const auto s = bones_of(d);
    clip::Built b;
    b.name = "walk";
    b.end = end;
    b.rot.assign(s.size(), {}), b.tr.assign(s.size(), {});
    b.moves.assign(s.size(), false);
    for (size_t i = 0; i < s.size(); ++i) b.moves[i] = s[i].parent == 0;
    for (unsigned f = 0; f <= end; ++f) {
        const double ph = 2 * kPi * f / end;
        for (size_t i = 0; i < s.size(); ++i) {
            std::array<double, 3> r{s[i].rot[0], s[i].rot[1], s[i].rot[2]}, t{s[i].tr[0], s[i].tr[1], s[i].tr[2]};
            const std::string n = s[i].name;
            if (n == "legL1") r[2] += 0.45 * std::sin(ph);
            if (n == "legR1") r[2] += 0.45 * std::sin(ph);  // its frame is turned half round: the opposite way
            if (n == "legL2") r[2] -= 0.25 * (1 + std::cos(ph));
            if (n == "legR2") r[2] -= 0.25 * (1 - std::cos(ph));
            if (n == "armR1") r[2] += 0.3 * std::sin(ph);
            if (n == "armR2") r[1] += 0.4;
            if (n == "neck") r[2] += 0.1 * std::sin(2 * ph);
            if (s[i].parent == 0) t[1] -= 2 + std::cos(2 * ph);
            b.rot[i].push_back(r), b.tr[i].push_back(t);
        }
    }
    return clip::encode(b, s);
}
// a pose read back from a written clip
clip::Pose read_pose(const std::vector<clip::Bone>& s, const clip::Clip& c, double t) {
    std::vector<int> track;
    for (const auto& b : s) {
        int k = -1;
        for (size_t j = 0; j < c.bones.size(); ++j)
            if (c.bones[j].name == b.name) k = int(j);
        track.push_back(k);
    }
    return clip::sample(s, c, track, t);
}
double distance(const std::array<double, 3>& a, const std::array<double, 3>& b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
std::array<double, 3> direction(const clip::World& w, size_t from, size_t to) {
    std::array<double, 3> d;
    for (int k = 0; k < 3; ++k) d[k] = w.pos[to][k] - w.pos[from][k];
    const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (auto& x : d) x /= l;
    return d;
}
}  // namespace

int main() {
    // curves: cubic and linear segments, baked frames, clamped ends
    {
        clip::Curve c;
        c.mode = 0, c.start = 0, c.end = 10, c.frames = {0, 10}, c.values = {1, 2, 0, -1, 0, 0, 0, 0};
        assert(std::fabs(clip::evaluate(c, 5) - (1 + 1 - 0.125)) < 1e-9 && std::fabs(clip::evaluate(c, 20) - clip::evaluate(c, 10)) < 1e-9);
        c.mode = 1, c.frames = {0, 4, 10}, c.values = {0, 8, 8, -2, 6, 0};
        assert(std::fabs(clip::evaluate(c, 2) - 4) < 1e-9 && std::fabs(clip::evaluate(c, 7) - 7) < 1e-9);
        c.mode = 2, c.frames.clear(), c.start = 0, c.end = 3, c.values = {0, 1, 4, 9, 9};
        assert(std::fabs(clip::evaluate(c, 2.5) - 6.5) < 1e-9 && std::fabs(clip::evaluate(c, 3) - 9) < 1e-9);
        c.scale = 2, c.offset = 1;
        assert(std::fabs(clip::evaluate(c, 1) - 3) < 1e-9);
    }
    // Euler angles: R = Rz Ry Rx, the nearest solution
    {
        const double e[3] = {0.3, -0.7, 2.9}, close_to[3] = {0.3 + 2 * kPi, -0.7, 2.9 - 2 * kPi};
        const auto back = clip::to_euler(clip::euler(e), close_to);
        for (int k = 0; k < 3; ++k) assert(std::fabs(back[k] - close_to[k]) < 1e-9);
        const auto x = clip::euler(e).apply(std::array<double, 3>{1, 0, 0}.data());
        assert(std::fabs(x[1] - std::sin(2.9) * std::cos(-0.7)) < 1e-9);
    }
    const auto src_def = rig(42, 90), same_def = rig(42, 90), small_def = rig(35, 0);
    const Bytes src_file = bfres(src_def, {{"idle", source_walk(src_def, 10)}, {"walk", source_walk(src_def, 34)}});
    std::vector<clip::Bone> src, shorter;
    std::string why;
    assert(clip::skeleton(src_file, "body", src, why) && src.size() == src_def.size() && src[2].name == "armR1" && src[2].parent == 1);
    assert(std::fabs(src[2].rot[0] - kPi / 2) < 1e-6 && std::fabs(src[6].tr[0] - 26) < 1e-5);
    clip::Clip walk;
    assert(clip::clip(src_file, "walk", walk, why) && walk.frames == 35 && walk.bones.size() == src.size());
    assert(!clip::clip(src_file, "run", walk, why) && why == "no animation run");
    assert(!clip::skeleton(src_file, "head", src, why));
    assert(clip::clip(src_file, "walk", walk, why) && clip::skeleton(src_file, "body", src, why));
    assert(clip::skeleton(bfres(small_def, {}), "body", shorter, why));

    // the same rig: the walk comes back as it was (here at the frame count it already has)
    {
        clip::Built probe;
        assert(clip::retarget(src, walk, src, 1, "same", probe, why));
        const double factor = 34 / probe.stride;  // a play rate that keeps 34 frames
        clip::Built same;
        assert(clip::retarget(src, walk, bones_of(same_def), factor, "same", same, why) && same.end == 34);
        assert(std::fabs(same.leg_ratio - 1) < 1e-6 && same.largest_step < 0.2);
        for (unsigned f = 0; f <= 34; f += 3) {
            const auto a = clip::fk(src, read_pose(src, walk, f)), b = clip::fk(src, read_pose(src, walk, f));
            clip::Pose p = clip::rest(src);
            for (size_t i = 0; i < src.size(); ++i) p.local[i] = clip::euler(same.rot[i][f].data()), p.tr[i] = same.tr[i][f];
            const auto c = clip::fk(src, p);
            for (size_t i = 0; i < src.size(); ++i) assert(distance(a.pos[i], c.pos[i]) < 1e-3 && clip::angle_between(a.rot[i], c.rot[i]) < 1e-4);
            (void)b;
        }
    }
    // a smaller rig with its arm not twisted at rest
    clip::Built built;
    assert(clip::retarget(src, walk, shorter, 1, "small_walk", built, why));
    {
        clip::Built big;
        assert(clip::retarget(src, walk, src, 1, "big", big, why));
        assert(std::fabs(built.leg_ratio - 35.0 / 42) < 1e-6);
        assert(std::fabs(built.stride / big.stride - built.leg_ratio) < 0.03);  // the stride follows the legs
        assert(built.end == unsigned(std::round(built.stride)) && big.end == unsigned(std::round(big.stride)));
        assert(built.largest_step < 0.2);  // smooth: no bone flips between frames
        const int fl = clip::index_of(shorter, "footL"), fr = clip::index_of(shorter, "footR");
        const auto rest_small = clip::fk(shorter, clip::rest(shorter)), rest_src = clip::fk(src, clip::rest(src));
        const double ankle0 = std::min(rest_small.pos[size_t(fl)][1], rest_small.pos[size_t(fr)][1]), src_ankle0 = std::min(rest_src.pos[size_t(fl)][1], rest_src.pos[size_t(fr)][1]);
        for (unsigned f = 0; f <= built.end; ++f) {
            clip::Pose p = clip::rest(shorter);
            for (size_t i = 0; i < shorter.size(); ++i) p.local[i] = clip::euler(built.rot[i][f].data()), p.tr[i] = built.tr[i][f];
            const auto w = clip::fk(shorter, p);
            const double t = double(f) * 34 / built.end;
            const auto s = clip::fk(src, read_pose(src, walk, t));
            // its own bone lengths
            for (size_t i = 1; i < shorter.size(); ++i)
                if (shorter[i].parent > 0) assert(std::fabs(distance(w.pos[i], w.pos[size_t(shorter[i].parent)]) - std::hypot(shorter[i].tr[0], shorter[i].tr[1], shorter[i].tr[2])) < 1e-6);
            // limbs point where the source's do (the arm too, whatever its rest twist)
            for (auto [a, b] : {std::pair{5, 6}, {6, 7}, {8, 9}, {9, 10}, {2, 3}}) {
                const auto da = direction(w, size_t(a), size_t(b)), db = direction(s, size_t(a), size_t(b));
                assert(da[0] * db[0] + da[1] * db[1] + da[2] * db[2] > 0.9999);
            }
            // the lower ankle follows the source's lower ankle (scaled by the legs)
            const double lower = std::min(w.pos[size_t(fl)][1], w.pos[size_t(fr)][1]) - ankle0, src_lower = std::min(s.pos[size_t(fl)][1], s.pos[size_t(fr)][1]) - src_ankle0;
            assert(std::fabs(lower - src_lower * built.leg_ratio) < 1e-6);
        }
    }
    // with the idles as the head's reference: the neck takes the walk's change from the source's idle
    // onto the target's own idle (here the target holds its neck bent forward at rest)
    {
        auto bent_def = small_def;
        Bytes idle_bent;
        {
            const auto b = bones_of(bent_def);
            clip::Built idle;
            idle.name = "idle", idle.end = 4;
            idle.rot.assign(b.size(), {}), idle.tr.assign(b.size(), {}), idle.moves.assign(b.size(), false);
            for (unsigned f = 0; f <= 4; ++f)
                for (size_t i = 0; i < b.size(); ++i) {
                    std::array<double, 3> r{b[i].rot[0], b[i].rot[1], b[i].rot[2]};
                    if (b[i].name == "neck") r[2] += 0.3;
                    idle.rot[i].push_back(r), idle.tr[i].push_back({b[i].tr[0], b[i].tr[1], b[i].tr[2]});
                }
            for (size_t i = 0; i < b.size(); ++i) idle.moves[i] = b[i].parent == 0;
            idle_bent = clip::encode(idle, b);
        }
        clip::Clip src_idle, dst_idle;
        assert(clip::clip(src_file, "idle", src_idle, why) && clip::clip(bfres(small_def, {{"idle", idle_bent}}), "idle", dst_idle, why));
        clip::Built held;
        assert(clip::retarget(src, walk, shorter, 1, "held", held, why, &src_idle, &dst_idle) && held.end == built.end);
        const size_t neck = size_t(clip::index_of(shorter, "neck"));
        auto track = [](const std::vector<clip::Bone>& s, const clip::Clip& c) {
            std::vector<int> t;
            for (const auto& b : s) { int k = -1; for (size_t j = 0; j < c.bones.size(); ++j) if (c.bones[j].name == b.name) k = int(j); t.push_back(k); }
            return t;
        };
        const auto is = clip::fk(src, clip::sample(src, src_idle, track(src, src_idle), 0)), it = clip::fk(shorter, clip::sample(shorter, dst_idle, track(shorter, dst_idle), 0));
        for (unsigned f = 0; f <= held.end; f += 4) {
            clip::Pose p = clip::rest(shorter);
            for (size_t i = 0; i < shorter.size(); ++i) p.local[i] = clip::euler(held.rot[i][f].data()), p.tr[i] = held.tr[i][f];
            const auto w = clip::fk(shorter, p), s = clip::fk(src, read_pose(src, walk, double(f) * 34 / held.end));
            assert(clip::angle_between(w.rot[neck], s.rot[neck] * is.rot[neck].t() * it.rot[neck]) < 1e-5);
            // the other bones are as without the idles
            clip::Pose q = clip::rest(shorter);
            for (size_t i = 0; i < shorter.size(); ++i) q.local[i] = clip::euler(built.rot[i][f].data()), q.tr[i] = built.tr[i][f];
            const auto v = clip::fk(shorter, q);
            for (size_t i = 0; i < shorter.size(); ++i)
                if (i != neck) assert(clip::angle_between(w.rot[i], v.rot[i]) < 1e-6);
        }
    }
    // written and read back: the native layout, every curve baked, the skeleton left for the binder
    {
        const Bytes fska = clip::encode(built, shorter);
        assert(!std::memcmp(fska.data(), "FSKA", 4) && fska.size() % 4 == 0);
        clip::Reader r{fska};
        assert(r.u32(0xC) == 0x1105 && r.u32(0x10) == built.end + 1 && r.u16(0x14) == shorter.size() && r.u32(clip::kSkeletonField) == 0);
        const unsigned curves = r.u32(0x18);
        assert(curves == 3 * (shorter.size() - 1) + 3 * 2);  // rotations of every bone but the root, the hips' translations
        assert(r.u32(0x1C) == curves * 4 * (built.end + 2));
        const uint64_t bind = r.rel(0x28);
        for (unsigned i = 0; i < shorter.size(); ++i) assert(r.u16(bind + 2 * i) == i);
        const Bytes file = bfres(small_def, {{"small_walk", fska}});
        clip::Clip back;
        assert(clip::clip(file, "small_walk", back, why) && back.frames == built.end + 1);
        for (unsigned f = 0; f <= built.end; f += 5) {
            const auto p = read_pose(shorter, back, f);
            for (size_t i = 0; i < shorter.size(); ++i) {
                assert(clip::angle_between(p.local[i], clip::euler(built.rot[i][f].data())) < 1e-5);
                for (int k = 0; k < 3; ++k) assert(std::fabs(p.tr[i][k] - built.tr[i][f][k]) < 1e-3);
            }
        }
        // damaged files are refused, never read past their end
        for (size_t n = 0; n < file.size(); n += 7) {
            Bytes cut(file.begin(), file.begin() + std::ptrdiff_t(n));
            std::vector<clip::Bone> s;
            clip::Clip c;
            clip::skeleton(cut, "body", s, why);
            clip::clip(cut, "small_walk", c, why);
        }
        Bytes bad = file;
        for (size_t i = 0x60; i < bad.size(); i += 13) bad[i] ^= 0x5A;
        std::vector<clip::Bone> s;
        clip::Clip c;
        clip::skeleton(bad, "body", s, why);
        clip::clip(bad, "small_walk", c, why);
    }
    // rigs that differ are refused
    {
        auto other = shorter;
        other[3].name = "elbow";
        clip::Built b;
        assert(!clip::retarget(src, walk, other, 1, "x", b, why) && why.find("differ") != std::string::npos);
        other = shorter;
        other.pop_back();
        assert(!clip::retarget(src, walk, other, 1, "x", b, why));
    }
    // the game files' borrowed walks (only with WWHD_TEST_GAME)
    if (const char* game = std::getenv("WWHD_TEST_GAME")) {
        for (unsigned k = 0; k < story_resident_types::kBorrowedWalkCount; ++k) {
            story_resident_walk_files::Clip c;
            assert(story_resident_walk_files::build(game, k, c, why));
            assert(c.frames > 30 && c.frames < 80 && c.largest_step < 0.3 && c.fska.size() < 0x8000 - 0x80);
            std::printf("%s: %u frames, stride %.1f, largest step %.3f rad, %zu bytes\n", story_resident_types::kBorrowedWalks[k].name, c.frames, c.stride, c.largest_step, c.fska.size());
        }
    }
    // the walker table: every Windfall person walks; the borrowed ones name their lender
    for (const auto& w : story_resident_types::kWalkers) {
        const auto* b = story_resident_types::borrowed_walk(w.subtype);
        assert((w.borrowed >= 0) == (b != nullptr) && !story_resident_types::no_walk(w.subtype));
        if (b) assert(std::string(b->target).size() == 2 && b->factor == 1.f);
    }
    assert(story_resident_types::no_walk(story_resident_types::kSeatedYm1));
    std::printf("story_resident_walk_clip_test: ok\n");
    return 0;
}
