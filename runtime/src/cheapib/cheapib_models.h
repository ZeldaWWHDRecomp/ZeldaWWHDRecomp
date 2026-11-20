// cheapib stage 1 (WWHD_CHEAPIB=1), part 1: the model patch (decomp build, fast mode; cheapib/DESIGN.md §6).
// Included into interp.cpp after the model hooks (it uses interp's model records, g_models / g_ubo_done / g_mstats).
//
// A replayed hold pass draws nothing: the painter calls the logic pass's display lists again. Those lists bind each
// model's uniform blocks in the ring slot its viewCalc (ModelPipeline_l::advanceFrame, 027F55FC) selected on the logic
// pass. Here, on a replayed pass, every viewCalc of the logic pass is run again into that same slot:
// - the logic pass notes each call (model, the slot index before its flip, its joint matrices as they were);
// - a replayed blended pass (t = 2/4, 3/4) re-runs interp's viewCalc hook for the models it blended on the logic
//   pass: the blend at this pass's fraction, the UBO update 027F5018 on it, the exact matrices back. Models drawn
//   exact (new, still, cut) keep the logic pass's bytes, which are this pass's bytes too;
// - the replayed record pass re-runs it for every model: the step's exact matrices drawn, and the records the next
//   step blends from taken as a full record pass takes them.
// The slot index is set back before each call (advanceFrame flips it, so it lands where the logic pass's call did).
// UBO updates run inline on the main thread (the update_ubo job of these calls is told to skip, as for blended
// models): nothing waits for that job on a replayed pass, and the painter's snapshot follows.
// Not patched (counted): particle models (dPa_modelEmitterDraw places pooled models per particle: a dynamic site),
// Sploosh Kaboom's pooled markers.
//
// The camera (part 2): before the models, the camera process the logic pass drew is drawn again by interp's camera
// hook (hook_024FFC40) as this pass would draw it: blended at pass_t() on blended passes, exact on the record pass
// (which also takes the step's camera record and the sound listener, as a full record pass does). Its focus lines
// (dCamera_c::Draw, 2D) are not drawn (they would go into lists nobody records; frozen with the 2D lists).
// j3dSys's view is then this pass's, so the models' frame uniforms (u2, RenderFrameUniforms: view and projection,
// light and fog vectors) follow it. When the view differs from the logic pass's, every model of the logic pass is
// run again (as on the record pass), not only the blended ones.
// u2 lives in each model's lighting pages (model +0x60 count, +0x64 RenderFrameUniforms[], 0x23C each) and the model's
// matrix pair (+0x84, RenderUniformPair): double-buffered pairs that viewCalc's RenderUniformPair::advance (027FB678)
// flips (mActiveIndex +0x48, mPreviousIndex +0x4C; the list binds mBuffers[mPreviousIndex]) and whose upload it queues
// to the update_ubo thread. The logic pass's active indices are noted with each call and set back before the call
// again, so the upload goes into the buffer the logic lists bind; after the patch the queue is drained
// (PipelineFrameQueue::finishFrame 027F9994: the completion message after every queued upload), so every upload has
// landed before the painter's calls snapshot the blocks. Shadow environment (the 672 B block hdShadowEnvironment-
// Parameters_l): kept at the logic pass's cascades (not patched).
#pragma once
#include "cheapib/cheapib.h"
#include "cheapib/cheapib_s2.h"
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" void hook_027F55FC(Cpu* c);
namespace interp {
namespace cheapib_models {

constexpr uint32_t kBufferIndex = 0x6C;  // ModelPipeline_l::mBufferIndex (u8)

struct Call {
    uint32_t model, jnt, mtx, n;
    uint8_t index_before;     // mBufferIndex at the call (advanceFrame flips it first)
    uint8_t marker_cell;      // Sploosh Kaboom marker (not patched when nonzero)
    bool blended = false;     // the logic pass drew it blended
    bool multi = false;       // the model's joints have more than one call this pass
    uint32_t words_at = 0;    // offset into words: the joint matrices at the call
    uint32_t pairs_at = 0, pairs_n = 0;  // offset into pairs: the uniform pairs this call advances
    uint32_t proj = 0;        // index into State::projs: the projection in effect at the call
};
constexpr uint32_t kPages = 0x60, kLightingCount = 0x70, kFinishObject = 0x84, kFinishDisabled = 0x88;
constexpr uint32_t kPageSize = 0x23C, kPairActive = 0x48, kPairPrevious = 0x4C;
constexpr uint32_t kFrameQueuePtr = 0x101F997C, kQueueCompleted = 0x388;
// j3d's projection (sJ3DProjection, Mtx44 as 16 words): the camera's draw writes it; viewCalc's lighting pages copy it
// into u2 (RenderFrameUniforms f1/f8). Some models are viewCalc'ed while another projection is in effect (the sky:
// vrbox2's sky layer, near 0.0001 / far 16000): each call notes the one it ran with.
constexpr uint32_t kProjection = 0x104B470C;
using Proj = std::array<uint32_t, 16>;
struct State {
    std::vector<Call> calls;
    std::vector<uint32_t> words;  // the joint matrices at each call (multi-call models are put back to them)
    std::vector<std::pair<uint32_t, uint32_t>> pairs;  // (pair, mActiveIndex before the call)
    std::vector<Proj> projs;      // the projections the calls ran with (deduplicated in call order)
    Proj proj{};                  // the projection the logic pass's camera left (the main one)
    uint32_t camera = 0, camera_id = 0;  // the camera process the logic pass drew (0: none)
    uint32_t view[12] = {};              // j3dSys's view the logic pass drew with
    uint32_t blended_at_note = 0;
    bool open = false;            // the logic pass's list is being collected
    bool valid = false;           // the list belongs to the step being replayed
    // counters (per report)
    uint64_t logic_calls = 0, logic_blended = 0, multi = 0, sploosh = 0, patched = 0, recorded = 0, passes = 0;
    uint64_t cam_drawn = 0, cam_moved = 0, pairs_set = 0, drains = 0, own_proj = 0;
};
inline State g_cm;

inline std::string report();

// The UBO update 027F5018 (ModelPipeline_l::updatePacketBindings) first runs each material's apply object into the
// model's material packets (colours and values the actor's draw set up just before it: its light colours, tev
// registers), then the model runtime's three view updates on mBuffer (+0x14) for mBufferIndex: updateUniforms 027DE824
// (the material uniform blocks, from the material values), updatePalette 027DE778 (the skinning palette) and
// updateViews 027DE780 (the joints' view matrices). A replayed pass runs no actor draw, so the apply and the material
// update would write what the last draw left (strict verify, 2026-11-16: u3/u17 material blocks, 872 B, wrong). The
// patch runs the two matrix updates only: the material blocks keep the logic pass's bytes, which the logic lists bind.
inline bool g_patching = false;
inline void ubo_update(Cpu* c) {
    if (!g_patching) {
        f_027F5018_orig(c);
        return;
    }
    const uint32_t model = c->r[3];
    guest_call(c, 0x027DE778, {model + 0x14, uint32_t(ld8(model + kBufferIndex))});
    guest_call(c, 0x027DE780, {model + 0x14, uint32_t(ld8(model + kBufferIndex))});
    c->r[3] = model;
}
inline void close_last() {
    if (!g_cm.open || g_cm.calls.empty()) return;
    if (g_mstats.blended != g_cm.blended_at_note) g_cm.calls.back().blended = true;
}

// the uniform pairs viewCalc (027F55FC) advances for `model`: its lighting pages (updateLighting: mLightingCount
// pages, an index past the array's count advances page 0) and its matrix pair (finishFrameObject, unless disabled)
inline void note_pairs(uint32_t model, Call& k) {
    k.pairs_at = (uint32_t)g_cm.pairs.size();
    auto add = [&](uint32_t pair) {
        if (!pair) return;
        for (uint32_t i = k.pairs_at; i < g_cm.pairs.size(); i++)
            if (g_cm.pairs[i].first == pair) return;
        g_cm.pairs.emplace_back(pair, ld32(pair + kPairActive));
    };
    const int32_t count = (int32_t)ld32(model + kLightingCount);
    const uint32_t pages = ld32(model + kPages), storage = ld32(model + kPages + 4);
    for (int32_t p = 0; storage && p < count && p < 64; p++) add(storage + kPageSize * ((uint32_t)p < pages ? p : 0));
    if (!ld8(model + kFinishDisabled)) add(ld32(model + kFinishObject));
    k.pairs_n = (uint32_t)g_cm.pairs.size() - k.pairs_at;
}
// the pairs back to the logic pass's state before the call (advance then binds what the logic call bound)
inline void set_pairs(const Call& k) {
    for (uint32_t i = k.pairs_at; i < k.pairs_at + k.pairs_n; i++) {
        const uint32_t pair = g_cm.pairs[i].first, active = g_cm.pairs[i].second;
        st32(pair + kPairActive, active);
        st32(pair + kPairPrevious, active == 0);
        g_cm.pairs_set++;
    }
}
// every upload queued to the update_ubo thread so far has run (PipelineFrameQueue::finishFrame; its flag back to 0, as
// resetWork leaves it)
inline void drain(Cpu* c) {
    const uint32_t q = ld32(GD(kFrameQueuePtr));
    if (q < 0x10000000 || q >= 0x50000000) return;
    st8(q + kQueueCompleted, 0);
    Cpu cc = *c;
    guest_call(&cc, 0x027F9994, {q});
    st8(q + kQueueCompleted, 0);
    g_cm.drains++;
}

// hook_027F55FC (logic pass, a model interp blends or draws exact): before anything else
inline void note(Cpu* c) {
    if (g_hold || !cheapib::stage1()) return;
    if (!g_cm.open) {
        g_cm.calls.clear();
        g_cm.words.clear();
        g_cm.pairs.clear();
        g_cm.projs.clear();
        g_cm.open = true;
        g_cm.valid = false;
    }
    close_last();
    const uint32_t model = c->r[3];
    const uint32_t jnt = ld32(model + kMdlJoints);
    const uint32_t mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    const uint32_t n = jnt ? ld16(jnt + kJntNum) : 0;
    g_cm.blended_at_note = g_mstats.blended;
    if (!mtx || n == 0 || n > kMaxJoints) return;
    Call k{model, jnt, mtx, n, ld8(model + kBufferIndex), sploosh_marker_cell(g_sploosh_markers, model)};
    k.words_at = (uint32_t)g_cm.words.size();
    const uint32_t* w = (const uint32_t*)ppc_ptr(mtx);
    g_cm.words.insert(g_cm.words.end(), w, w + 12 * n);
    note_pairs(model, k);
    {
        Proj p;
        memcpy(p.data(), ppc_ptr(GD(kProjection)), sizeof p);
        if (g_cm.projs.empty() || g_cm.projs.back() != p) g_cm.projs.push_back(p);
        k.proj = (uint32_t)g_cm.projs.size() - 1;
    }
    g_cm.calls.push_back(k);
    cheapib::s2_mat_call(model, k.index_before);  // (stage 2: the material animations attached to it)
}

// the logic pass's per-frame function returned: the list is complete
inline void logic_done() {
    if (!cheapib::stage1()) return;
    cheapib::s2_logic_done();
    cheapib::s2_mat_logic_done();
    cheapib::s2_vtx_logic_done();
    cheapib::s2_sky_logic_done();
    cheapib::s2_ptcl_logic_done();
    if (!g_cm.open) {  // (no model drawn)
        g_cm.calls.clear();
        g_cm.words.clear();
        g_cm.pairs.clear();
        g_cm.projs.clear();
    }
    // the camera the logic pass drew (camera_draw set j3dSys's view this pass)
    g_cm.camera = view_drawn_this_pass() ? last_camera() : 0;
    g_cm.camera_id = g_cm.camera ? ld32(g_cm.camera + 4) : 0;
    memcpy(g_cm.view, ppc_ptr(GD(0x104B45F8)), sizeof g_cm.view);
    memcpy(g_cm.proj.data(), ppc_ptr(GD(kProjection)), sizeof g_cm.proj);
    close_last();
    g_cm.open = false;
    g_cm.valid = true;
    // models with more than one call: keep each call's matrices (the actor placed the model again in between)
    std::unordered_map<uint32_t, uint32_t> count;
    for (const Call& k : g_cm.calls) count[k.jnt]++;
    for (Call& k : g_cm.calls) {
        g_cm.logic_calls++;
        if (k.blended) g_cm.logic_blended++;
        if (count[k.jnt] > 1) k.multi = true;
    }
    static uint64_t steps = 0;
    if (++steps % 300 == 0) LOG("[cheapib] %s", report().c_str());
}
// a replayed hold pass, after its per-frame function (before the painter's calls)
inline void replay(Cpu* c) {
    if (!cheapib::stage1() || !g_cm.valid) return;
    const bool verify = cheapib::verifying();  // (a full in-between: the patch goes to the logic slots, indices kept)
    if (!verify && !cheapib::replaying()) return;
    std::vector<std::pair<uint32_t, uint8_t>> indices;
    std::vector<std::pair<uint32_t, uint32_t>> pair_indices;  // (verify: the full pass's pair indices, put back after)
    if (verify) {
        for (const Call& k : g_cm.calls) indices.emplace_back(k.model, ld8(k.model + kBufferIndex));
        for (const auto& p : g_cm.pairs) {
            pair_indices.emplace_back(p.first + kPairActive, ld32(p.first + kPairActive));
            pair_indices.emplace_back(p.first + kPairPrevious, ld32(p.first + kPairPrevious));
        }
    }
    const bool record = record_pass();
    g_cm.passes++;
    // the camera at this pass (a verified in-between drew it already)
    if (!verify && g_cm.camera && ld32(g_cm.camera + 4) == g_cm.camera_id) {
        Cpu k = *c;
        k.r[3] = g_cm.camera;
        g_blend_draw = blended_hold();
        hook_024FFC40(&k);  // (hook_024FFC18 draws no focus lines while cheapib::replaying())
        g_blend_draw = false;
        g_cm.cam_drawn++;
    }
    const bool moved = memcmp(g_cm.view, ppc_ptr(GD(0x104B45F8)), sizeof g_cm.view) != 0;
    if (moved) g_cm.cam_moved++;
    if (verify) cheapib::verify_patch(true);  // (the verify's patched view: what this patch writes)
    Proj now_proj;  // this pass's projection (the camera drawn above), put back after each own-projection call
    memcpy(now_proj.data(), ppc_ptr(GD(kProjection)), sizeof now_proj);
    g_patching = true;
    // the final matrices of multi-call models, put back after their calls
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> finals;
    std::unordered_map<uint32_t, bool> saved_final;
    Cpu cc = *c;
    for (const Call& k : g_cm.calls) {
        if (k.marker_cell) { g_cm.sploosh++; continue; }
        if (!record && !k.blended && !moved) continue;  // (the logic pass's bytes are this pass's bytes)
        // the model must still be the one the logic pass drew
        if (ld32(k.model + kMdlJoints) != k.jnt || ld32(k.jnt + kJntMtx) != k.mtx || ld16(k.jnt + kJntNum) != k.n) continue;
        if (k.multi) {
            g_cm.multi++;
            if (!saved_final[k.jnt]) {
                saved_final[k.jnt] = true;
                const uint32_t* w = (const uint32_t*)ppc_ptr(k.mtx);
                finals.emplace_back(k.mtx, std::vector<uint32_t>(w, w + 12 * k.n));
            }
            memcpy(ppc_ptr(k.mtx), g_cm.words.data() + k.words_at, 48 * k.n);
        }
        st8(k.model + kBufferIndex, k.index_before);
        set_pairs(k);
        // a call that ran with its own projection on the logic pass (the sky layer's) runs with it again: this pass's
        // projection, with the rows the logic call's own projection changed (the sky's near/far z row; its x/y rows
        // when its fovy/aspect/offsets made them differ) taken from the noted one
        const Proj& own = g_cm.projs[k.proj];
        const bool own_proj = own != g_cm.proj;
        if (own_proj) {
            uint32_t* g = (uint32_t*)ppc_ptr(GD(kProjection));
            for (int r = 0; r < 4; r++)
                if (memcmp(&own[4 * r], &g_cm.proj[4 * r], 16) != 0) memcpy(g + 4 * r, &own[4 * r], 16);
            g_cm.own_proj++;
        }
        {
            std::lock_guard<std::mutex> lk(g_ubo_mu);
            g_ubo_done[k.model]++;  // (the job this viewCalc queues skips; the update runs here)
        }
        const uint32_t blended = g_mstats.blended;
        cc.r[3] = k.model;
        hook_027F55FC(&cc);
        if (g_mstats.blended != blended) {
            // the hook ran the update itself (and told the job to skip once more): ours is not needed
            std::lock_guard<std::mutex> lk(g_ubo_mu);
            auto it = g_ubo_done.find(k.model);
            if (it != g_ubo_done.end() && --it->second == 0) g_ubo_done.erase(it);
        } else {
            cc.r[3] = k.model;
            ubo_update(&cc);  // drawn exact: its update now, on the exact matrices
        }
        if (own_proj) memcpy(ppc_ptr(GD(kProjection)), now_proj.data(), sizeof now_proj);
        g_cm.patched++;
        if (record) g_cm.recorded++;
    }
    g_patching = false;
    for (auto& f : finals) memcpy(ppc_ptr(f.first), f.second.data(), 4 * f.second.size());
    cheapib::s2_ptcl_rerun(c);  // (stage 2: particles, cheapib_s2_particles.cpp; their queued uploads land with the drain)
    cheapib::qnote(3);
    drain(c);  // (the queued u2 uploads land before the verify's view closes and before the painter)
    cheapib::qnote(4);
    cheapib::s2_rerun(c);  // (stage 2: the sway sites' matrices, cheapib_s2.h; after every queued upload has landed)
    cheapib::s2_mat_rerun(c);  // (stage 2: the material animations, cheapib_s2_material.cpp)
    cheapib::s2_vtx_rerun(c);  // (stage 2: CPU-written vertex sites, cheapib_s2_vertex.cpp)
    cheapib::s2_sky_rerun(c);  // (stage 2: sky clouds and wave crests, cheapib_s2_sky.cpp)
    cheapib::s2_sun_rerun(c);  // (stage 2: the sun, moon and lens flare, cheapib_s2_sun.cpp)
    cheapib::s2_weather_rerun(c);  // (stage 2: the weather sprites, cheapib_s2_weather.cpp)
    if (verify) cheapib::verify_patch(false);
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) st8(it->first + kBufferIndex, it->second);
    for (auto it = pair_indices.rbegin(); it != pair_indices.rend(); ++it) st32(it->first, it->second);
}

inline std::string report() {
    char b[512];
    snprintf(b, sizeof b, "models: logic calls %.1f (blended %.1f) per step, patched %.1f per replayed pass (record pass "
             "%.1f), multi-call %llu, sploosh markers %llu; camera drawn on %llu of %llu replayed passes (view moved %llu), "
             "pairs set back %.1f per pass, drains %llu, own-projection calls %.1f per pass",
             g_cm.logic_calls / 300.0, g_cm.logic_blended / 300.0, g_cm.passes ? double(g_cm.patched) / g_cm.passes : 0.0,
             g_cm.recorded / 300.0, (unsigned long long)g_cm.multi, (unsigned long long)g_cm.sploosh,
             (unsigned long long)g_cm.cam_drawn, (unsigned long long)g_cm.passes, (unsigned long long)g_cm.cam_moved,
             g_cm.passes ? double(g_cm.pairs_set) / g_cm.passes : 0.0, (unsigned long long)g_cm.drains,
             g_cm.passes ? double(g_cm.own_proj) / g_cm.passes : 0.0);
    g_cm.logic_calls = g_cm.logic_blended = g_cm.multi = g_cm.sploosh = g_cm.patched = g_cm.recorded = g_cm.passes = 0;
    g_cm.cam_drawn = g_cm.cam_moved = g_cm.pairs_set = g_cm.drains = g_cm.own_proj = 0;
    return b;
}

}  // namespace cheapib_models
}  // namespace interp
