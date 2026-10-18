// The toon clouds' per-frame data, shared by Metal (gfx/metal_clouds.mm) and Vulkan (gfx/vulkan/clouds.cpp):
// the shader globals (camera, light, palette, tuning) and one instance per cloud impostor, far to near.
// Everything comes from the game's own sky clouds (cloud_proto.cpp snapshot): placement, drift, fade,
// weather; distant clouds near the horizon get satellites that turn them into banks.
#include "cloud_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "gfx/renderer.h"
#include "runtime.h"

namespace cloud_scene {
namespace {
float envf(const char* n, float d) { const char* e = getenv(n); return e && *e ? (float)atof(e) : d; }
bool env_on(const char* n) { const char* e = getenv(n); return e && *e && strcmp(e, "0"); }
}  // namespace

void build(const cloud_proto::Snapshot& s, float aspect, int mode, bool depthTested, uint64_t frame, Globals& g,
           std::vector<Inst>& sorted) {
    g = {};
    // view rows (world -> camera) into a float3x3 with columns c: m[c][r] = row r, column c
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) g.view[c * 4 + r] = s.viewRot[r * 4 + c];
    float f = 1.0f / std::tan(s.fovy * 0.5f * 3.14159265f / 180.0f);
    static const bool logOn = env_on("WWHD_CLOUDS_LOG");
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = aspect, g.proj[3] = mode == cloud_proto::kCombo ? 1.0f : 0.0f;
    // light: the sun; below the horizon the moon (dimmer, no rim); never from below
    auto dir = [&](const float* pos, float* out) {
        float d[3] = {pos[0] - s.eye[0], pos[1] - s.eye[1], pos[2] - s.eye[2]};
        float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 1e-3f) { out[0] = 0, out[1] = 1, out[2] = 0; return; }
        for (int i = 0; i < 3; i++) out[i] = d[i] / len;
    };
    float L[3] = {0, 1, 0};
    if (s.sunVisible) dir(s.sunPos, L);
    float elev = L[1];
    float day = std::clamp(elev * 5.0f + 0.3f, 0.0f, 1.0f);
    if (elev < 0.02f) {
        float M[3];
        dir(s.moonPos, M);
        if (M[1] > 0.05f) for (int i = 0; i < 3; i++) L[i] = M[i];
        else L[0] *= 0.3f, L[2] *= 0.3f, L[1] = 0.9f;
    }
    if (L[1] < 0.12f) {  // keep the light a little above the horizon so tops stay lit
        L[1] = 0.12f;
        float len = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        for (float& v : L) v /= len;
    }
    for (int i = 0; i < 3; i++) g.sun[i] = L[i];
    g.sun[3] = day;
    static const float strengthOverride = envf("WWHD_CLOUDS_STRENGTH", -1.0f);  // research: visual storm
    const float strength = strengthOverride >= 0 ? strengthOverride : s.strength;
    // warmth from the clock (the game's sun stays fairly high at dusk): dawn around 90, dusk around 270
    // (360 = a day); WWHD_CLOUDS_WARM overrides
    auto bump = [](float t, float c, float w) { return std::max(0.0f, 1.0f - std::fabs(t - c) / w); };
    float warm = std::clamp(bump(s.curTime, 268.0f, 28.0f) + bump(s.curTime, 92.0f, 22.0f), 0.0f, 1.0f);
    warm = std::max(warm, std::clamp(1.0f - (elev - 0.05f) * 3.0f, 0.0f, 1.0f) * day);
    static const float warmOverride = envf("WWHD_CLOUDS_WARM", -1.0f);
    if (warmOverride >= 0) warm = warmOverride;
    for (int i = 0; i < 3; i++) g.sky[i] = s.fogColor[i];
    g.sky[3] = strength;
    g.lit[3] = warm;
    const float haze = 1.0f - render::horizon_haze();  // 0 = the game's haze .. 1 = horizon haze band removed
    g.params[0] = envf("WWHD_CLOUDS_BAND", 0.010f), g.params[1] = envf("WWHD_CLOUDS_NOISE", 0.38f), g.params[2] = haze;
    // quality tiers: Lite = 20 march steps, proxy normals only, no self-shadow; High = 48, detail normals, self-shadow
    const bool lite = cloud_proto::tier() == 1;
    g.params[3] = envf("WWHD_CLOUDS_SELFSHADOW", lite ? 0.0f : 6.0f);
    g.tune[0] = envf("WWHD_CLOUDS_FRINGE", 0.02f), g.tune[1] = envf("WWHD_CLOUDS_SAT", 0.25f);
    g.tune[2] = envf("WWHD_CLOUDS_SHADOW", 0.72f), g.tune[3] = envf("WWHD_CLOUDS_MID", 0.92f);
    g.extra[0] = envf("WWHD_CLOUDS_STEPS", mode == cloud_proto::kSdf ? (lite ? 20.0f : 48.0f) : 24.0f);
    g.extra[1] = envf("WWHD_CLOUDS_CHEAP", lite ? 1.0f : 0.0f);
    g.extra[2] = depthTested ? envf("WWHD_CLOUDS_Z", 0.99999f) : 0.5f;

    // instances: one per game cloud (drawVrkumo's layer-0 size), plus, for distant clouds near the
    // horizon, satellites that turn them into banks; the game's placement, drift, fade and weather drive all
    std::vector<Inst> inst;
    std::vector<float> dist;
    const float* k = s.k;  // sizeBase, sizeGain, variation, pitchScale, sideGain, pitchGain, starSizeBase, farPlane
    static const float sizeK = envf("WWHD_CLOUDS_SIZE", 1.35f), hsK = envf("WWHD_CLOUDS_HS", 0.75f);
    static const float bankK = envf("WWHD_CLOUDS_BANKS", 1.0f), bankDist = envf("WWHD_CLOUDS_BANK_DIST", 10500.0f);
    auto h01 = [](uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
        return (x & 0xFFFF) / 65535.0f;
    };
    for (int n = 0; n < s.n; n++) {
        const cloud_proto::Cloud& c = s.c[n];
        float x = c.pos[0], y = c.pos[1], z = c.pos[2];
        float hor = std::sqrt(x * x + z * z), D = std::sqrt(hor * hor + y * y);
        if (D < 1) continue;
        float v = (float)(c.index & 15) * k[2];
        float shape = 1 - v * v * v;
        float weather = k[0] * strength + k[1];
        float ang = shape * weather * c.falloff;
        float pitch = std::atan2(y, hor);
        float q = std::min(pitch / k[3], 1.0f), qc = q * q * q;
        float side = ang * k[0] * (1 + qc * k[4]);
        float tall = ang * (1 + c.height) * k[6] * (1 + qc * k[5]);
        // overhead clouds: keep them from filling the sky (the game's cards grow toward the zenith)
        static const float maxAng = envf("WWHD_CLOUDS_MAXANG", 0.16f);
        // stormy weather (the game's cloud strength): wider, flatter, merged into one overcast layer
        float rw = D * std::tan(std::min(side, 1.2f)) * sizeK * (1.0f + 0.5f * strength);
        rw = std::min(rw, D * maxAng) * (1.0f - 0.45f * std::clamp(pitch / 0.6f, 0.0f, 1.0f));
        float rh = D * std::tan(std::min(tall, 1.2f));
        if (rw < 1) continue;
        bool isFar = hor > bankDist;
        float tower = h01(c.index * 7 + 1) < 0.3f * (1.0f - strength) ? 1.45f : 1.0f;  // some clouds grow into towers (not in storms)
        float hs = std::clamp(rh / rw * hsK * tower * (1.0f - 0.4f * strength), 0.55f, 1.6f);
        auto add = [&](float cx, float cy, float cz, float r, float shp, float seed, float a) {
            Inst it{};
            it.center[0] = cx, it.center[1] = cy, it.center[2] = cz, it.center[3] = r;
            it.shape[0] = shp, it.shape[1] = seed, it.shape[2] = std::min(1.0f, a), it.shape[3] = 1 - c.falloff;
            float blend = 1 - c.falloff, mx = 0, lit[3];
            for (int i = 0; i < 3; i++) {
                float cc = s.cloudColor[i] * s.cloudScale, ce = s.centerColor[i] * s.centerScale;
                lit[i] = ce + (cc - ce) * blend * 0.25f;
                mx = std::max(mx, lit[i]);
            }
            for (int i = 0; i < 3; i++) it.base[i] = std::min(1.0f, lit[i] / std::max(mx, 1e-3f) * std::clamp(mx, 0.0f, 1.0f));
            it.base[3] = isFar ? 1.0f : 0.0f;
            inst.push_back(it);
            dist.push_back(std::sqrt(cx * cx + cy * cy + cz * cz));
        };
        add(x, y, z, rw, hs, (float)c.index * 7.31f + 3.0f, c.alpha);
        if ((isFar || strength > 0.5f) && bankK > 0) {
            // satellites along the horizon (perpendicular to the view direction), lower and a bit smaller
            float tx = -z / hor, tz = x / hor;
            int ns = 2 + (h01(c.index * 13 + 5) < 0.5f ? 1 : 0);
            for (int j = 0; j < ns; j++) {
                float sgn = (j & 1) ? -1.0f : 1.0f;
                float off = rw * bankK * (0.85f + 0.5f * h01(c.index * 31 + j)) * (1.0f + 0.6f * (j / 2));
                float r2 = rw * (0.7f + 0.3f * h01(c.index * 17 + j));
                float hs2 = std::clamp(hs * (0.7f + 0.6f * h01(c.index * 19 + j)), 0.55f, 1.6f);
                add(x + tx * off * sgn, y - rw * 0.08f, z + tz * off * sgn, r2, hs2, (float)c.index * 7.31f + 50.0f + j * 11.0f, c.alpha);
            }
        }
    }
    std::vector<int> order(inst.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return dist[a] > dist[b]; });
    sorted.clear();
    for (int i : order) sorted.push_back(inst[i]);
    g.extra[3] = (float)sorted.size();
    if (logOn && frame % 150 == 0) {
        LOG("[clouds] frame %llu: %d game clouds, %zu instances, count %d strength %.2f time %.1f fovy %.1f light %.2f %.2f %.2f day %.2f warm %.2f "
            "cloud %.2f %.2f %.2f x%.2f centre %.2f %.2f %.2f x%.2f fog %.2f %.2f %.2f",
            (unsigned long long)frame, s.n, sorted.size(), s.count, strength, s.curTime, s.fovy, L[0], L[1], L[2], day, warm,
            s.cloudColor[0], s.cloudColor[1], s.cloudColor[2], s.cloudScale, s.centerColor[0], s.centerColor[1], s.centerColor[2],
            s.centerScale, s.fogColor[0], s.fogColor[1], s.fogColor[2]);
    }

}

}  // namespace cloud_scene
