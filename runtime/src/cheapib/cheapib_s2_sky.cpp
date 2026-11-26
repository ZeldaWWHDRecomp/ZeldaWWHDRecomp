// cheapib stage 2, CPU-written sprite sites: sky clouds (WWHD_CHEAPIB_S2 bit 128) and wave crests (bit 256); decomp
// build, fast mode. See cheapib_s2.h and cheapib_s2_vertex.cpp (the sea: the same delivery).
//
// Both sites write each sprite's 4 vertices (20 bytes each: position, u, v) into a double-buffered slot per sprite while
// they draw (once per pass, logic pass only on a replayed step) and the logic pass's lists bind the slot it wrote. Their
// effect records are blended per pass by interp_fx (kankyo_move / wave_apply, re-applied at the pass's fraction by
// fx_hold_blend on blended hold passes, exact on the record pass), and stage 1's replay draws this pass's camera first.
// So on a replayed pass the port computes each noted sprite's corners from the records and camera as they are now, with
// the site's own arithmetic (the decomp's single-precision steps, gabi.h's rounding) and the game's own library calls
// (sqrtf, atan2f, sinf, cosf, cM_rad2s, PSMTX*), and sends the 80 bytes per sprite to the render thread
// (OP_CHEAPIB_VTX), which writes them into the logic slot before this pass's calls.
//
// Which sprites: the logic pass's inputs are kept at its end (the records as drawn, the camera); at the step's first
// replay every sprite the logic pass could have drawn is computed from them and kept only when its 80 bytes equal what
// the logic pass wrote into its slot: that is the self-check of the arithmetic (counted: "logic match" / "differ"). A
// sprite culled on the logic pass (its slot holds older bytes) differs and is left alone; nothing binds its slot.
// Colours (per-sprite uniforms: alpha, the sun fade) stay the logic pass's.
//
// Clouds (RainDraw::drawVrkumo 02575B6C, d_kankyo_rain_drawVrkumo.cpp): card (layer, i) of 3 x 100, record mInst[i]
//   (VRKUMO_EFF), slot GeometryBank mGeometry[1].slots[2 * (layer * 100 + i) + selected]; the bank's selector flips once
//   at the end of the call, so the logic slot is the side the selector does not point at after it. (The call runs in the
//   painter, after the per-frame function: the inputs kept at its end are the ones it draws with; the selector is read
//   at the first replay.)
// Wave crests (RainDraw::drawWave 02574D38, d_kankyo_rain_drawWave.cpp): crest i of mWaveCount, record mEff[i]
//   (WAVE_EFF), slot mGeometry[i].slots[selected], flipped per crest after its write; drawn when mDrawn[i].
#include "cheapib/cheapib_s2.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cheapib/cheapib.h"
#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"

namespace interp {
bool hold_pass();
uint64_t logic_steps();
}  // namespace interp

namespace cheapib {
namespace {

// ---- gabi.h's single-precision steps (tools/verify/include/gabi.h fadds_ppc ... fnmsubs; finite values)
inline float fadds(float a, float b) { return (float)((double)a + (double)b); }
inline float fsubs(float a, float b) { return (float)((double)a - (double)b); }
inline float fmuls(float a, float c) { return (float)((double)a * (double)c); }
inline float fmadds(float a, float c, float b) { return (float)((double)a * (double)c + (double)b); }   // a*c+b
inline float fnmsubs(float a, float c, float b) { return (float)(-((double)a * (double)c - (double)b)); }  // b-a*c
inline float wf(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
inline uint32_t fw(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }
inline float ldf(uint32_t ea) { return wf(ld32(ea)); }
inline float bef(const uint8_t* p) { uint32_t w; memcpy(&w, p, 4); return wf(__builtin_bswap32(w)); }
inline void put(uint8_t* p, float f) { const uint32_t w = __builtin_bswap32(fw(f)); memcpy(p, &w, 4); }

// ---- the game's library calls (as gabi::call passes floats: f1, f2; result f1 / r3)
constexpr uint32_t kSqrtf = 0x028F4384, kAtan2f = 0x028F4D28, kSinf = 0x028F43F8, kCosf = 0x028F4BE0,
                   kRad2s = 0x02019510;
constexpr uint32_t kMtxInverse = 0x028E91EC, kMtxRotDeg = 0x028E98C0, kMtxConcat = 0x028E9108,
                   kMtxMultVec = 0x028E8F64;
float g1(Cpu& cc, uint32_t fn, float a) {
    cc.f[1].ps0 = cc.f[1].ps1 = (double)a;
    guest_call(&cc, fn);
    return (float)cc.f[1].ps0;
}
double g1d(Cpu& cc, uint32_t fn, float a) {
    cc.f[1].ps0 = cc.f[1].ps1 = (double)a;
    guest_call(&cc, fn);
    return cc.f[1].ps0;
}
float g2(Cpu& cc, uint32_t fn, float a, float b) {
    cc.f[1].ps0 = cc.f[1].ps1 = (double)a;
    cc.f[2].ps0 = cc.f[2].ps1 = (double)b;
    guest_call(&cc, fn);
    return (float)cc.f[1].ps0;
}
uint32_t g1r(Cpu& cc, uint32_t fn, float a) {
    cc.f[1].ps0 = cc.f[1].ps1 = (double)a;
    return guest_call(&cc, fn);
}

// ---- guest addresses
constexpr uint32_t kEnv = 0x10475A68, kGameInfo = 0x1046F0B0;
constexpr uint32_t kGiCamera = 0x5AF8, kGiView = 0x5FA4, kGiStageVtable = 0x5150, kStageGetInfo = 0x15C;
constexpr uint32_t kCamEye = 0xDC, kCamBank = 0x100, kViewRotation = 0x1E4;
constexpr uint32_t kSlotSize = 0x254;
constexpr uint32_t kVtxBytes = 80;  // 4 vertices x (position, u, v)

// clouds
constexpr uint32_t kEnvVrkumo = 0xA94, kEnvVrkumoStrength = 0xA90;
constexpr uint32_t kCloudInst = 0xA4, kCloudStride = 0x2C, kCloudLoop = 0x11D4, kCloudBank1 = 0x11DC + 0x574F8;
constexpr uint32_t kBankSelected = 600 * kSlotSize;
constexpr int kCloudCards = 300;
enum : uint32_t {
    Zero = 0x1004F528, One = 0x1004F550, Drag = 0x1004F588, DirectionBias = 0x1004F844, StarSizeBase = 0x1004FC98,
    FarPlaneInset = 0x1004F594, CloudFarPlane = 0x1004FCC8, CloudLoopScale = 0x1004F5E8, CloudSizeBase = 0x1004FCD8,
    CloudSizeGain = 0x1004FCDC, CloudPitchLimit = 0x1004FCD4, CloudMinimumDrawAlpha = 0x1004F5B8,
    CloudMinimumAlpha = 0x1004FCE0, CloudVariation = 0x1004FCF8, CloudSmallOffset = 0x1004F544,
    CloudPitchScale = 0x1004FCFC, CloudPitchGain = 0x1004FD04, CloudSideGain = 0x1004FD00, SinCosTable = 0x104A44F8,
};
// cloud colours (layer 0: drawVrkumo's per-card colour, alpha = record alpha x the fade by screen distance from the sun)
constexpr uint32_t kEnvSun = 0xA34, kSunPos = 0x98, kEnvCloudColor = 0xB98, kEnvCloudCenter = 0xB9C,
                   kEnvVrboxInvisible = 0x1092, kEnvCloudColorScale = 0x10DC, kEnvCloudCenterScale = 0x10E0;
constexpr uint32_t kCloudUniforms = 0xAFFE8, kUniformStride = 0x370, kMatTriplet0 = 0x74 + 4 * 16;
constexpr uint32_t kSubVec = 0x0201AD78, kScaleVec = 0x0201AE48, kPrepProj = 0x02563DD8, kConvColor = 0x0257015C,
                   kProject = 0x025F1018, kAddColor = 0x0274D314, kSubColor = 0x0274D380, kScaleColor = 0x0274D458,
                   kDist2 = 0x028E8DE8, kSetTriplet = 0x027FC500, kUploadPrevious = 0x027FF6D0, kProjViewFn = 0x02524628;
enum : uint32_t { Half = 0x1004F57C, CloudFadeDistance = 0x1004F558, SkyRange = 0x1004F52C };
// waves
constexpr uint32_t kEnvWave = 0xAA0, kEnvWaveScale = 0x9E4, kEnvWaveBottom = 0x9F0, kEnvWaveFlat = 0x9F4,
                   kEnvWaveCount = 0x9F8;
constexpr uint32_t kWaveEff = 0xA0, kWaveStride = 0x38, kWaveSkew = 0x4240, kWaveGeometry = 0x424C,
                   kGeometrySize = 0x4C0, kGeometrySelected = 0x4A8, kWaveDrawn = 0x9DB90;
constexpr int kWaves = 300;
enum : uint32_t {
    BankToRadians = 0x1004F5F8, DegreeToRadians = 0x1004FB7C, WaveFadeScale = 0x1004FCBC,
    WaveWidthScale = 0x1004F8D4, WaveOffsets = 0x10477480,
};

// the inputs of one pass (the logic pass's kept at its end; a replay's read from memory)
struct CloudIn {
    std::vector<uint8_t> rec;  // mInst, 100 x 0x2C (guest byte order)
    float eye[3], strength, loop;
    void read(uint32_t pk, uint32_t cam) {
        rec.assign(ppc_ptr(pk + kCloudInst), ppc_ptr(pk + kCloudInst) + kCloudStride * 100);
        for (int a = 0; a < 3; a++) eye[a] = ldf(cam + kCamEye + 4 * a);
        strength = ldf(GD(kEnv) + kEnvVrkumoStrength);
        loop = ldf(pk + kCloudLoop);
    }
};
struct WaveIn {
    std::vector<uint8_t> rec;  // mEff, 300 x 0x38
    float skew_width, skew_dir, scale, bottom;
    uint32_t rotation[12];  // the view's mRotation (host-order words)
    int16_t bank;
    void read(uint32_t pk, uint32_t cam, uint32_t view) {
        rec.assign(ppc_ptr(pk + kWaveEff), ppc_ptr(pk + kWaveEff) + kWaveStride * kWaves);
        skew_width = ldf(pk + kWaveSkew);
        skew_dir = ldf(pk + kWaveSkew + 4);
        scale = ldf(GD(kEnv) + kEnvWaveScale);
        bottom = ldf(GD(kEnv) + kEnvWaveBottom);
        for (int w = 0; w < 12; w++) rotation[w] = ld32(view + kViewRotation + 4 * w);
        bank = (int16_t)ld16(cam + kCamBank);
    }
};

// cloud card (layer, i): drawVrkumo's corner arithmetic -> 80 bytes
void cloud_card(Cpu& cc, const CloudIn& in, int layer, int i, float farPlane, uint8_t* out) {
    const uint8_t* r = in.rec.data() + kCloudStride * i;
    const float one = ldf(GD(One)), zero = ldf(GD(Zero)), sizeBase = ldf(GD(CloudSizeBase)),
                sizeGain = ldf(GD(CloudSizeGain)), shapeGain = ldf(GD(DirectionBias)), pointMin = ldf(GD(Drag)),
                pitchLimit = ldf(GD(CloudPitchLimit)), loopScale = ldf(GD(CloudLoopScale));
    const float falloff = bef(r + 0x24), height = bef(r + 0x1C);
    const float variation = fmuls((float)((layer + i) & 15), ldf(GD(CloudVariation)));
    const float shape = -fmadds(fmuls(variation, variation), variation, -one);
    const float weather = fmadds(sizeBase, in.strength, sizeGain);
    float angularSize = fmuls(fmuls(shape, weather), falloff);
    const uint16_t angle = (uint16_t)g1r(cc, kRad2s, fmadds(in.loop, loopScale, (float)layer));
    const float sine = ldf(GD(SinCosTable) + (angle >> 3) * 8);
    angularSize = fmadds(fmuls(pointMin, angularSize), fmuls(sine, falloff), angularSize);
    const float verticalSize = fmadds(angularSize, height, angularSize),
                smallOffset = fmuls(angularSize, ldf(GD(CloudSmallOffset))), wideOffset = fmuls(angularSize, shapeGain);
    float pitchOffset = zero, yawOffset = zero;
    if (layer == 1) {
        switch (i & 3) {
        case 0: pitchOffset = smallOffset; yawOffset = wideOffset; break;
        case 1: pitchOffset = -smallOffset; yawOffset = smallOffset; break;
        case 2: pitchOffset = wideOffset; yawOffset = -smallOffset; break;
        case 3: pitchOffset = -wideOffset; yawOffset = wideOffset; break;
        }
    } else if (layer == 2) {
        switch (i & 3) {
        case 0: pitchOffset = wideOffset; yawOffset = smallOffset; break;
        case 1: pitchOffset = -wideOffset; yawOffset = wideOffset; break;
        case 2: pitchOffset = smallOffset; yawOffset = -wideOffset; break;
        case 3: pitchOffset = -smallOffset; yawOffset = smallOffset; break;
        }
    }
    const float x = bef(r + 4), y = bef(r + 8), z = bef(r + 12);
    const float horizontal = g1(cc, kSqrtf, fmadds(x, x, fmuls(z, z)));
    float yaw = g2(cc, kAtan2f, x, z);
    float pitch = g2(cc, kAtan2f, y, horizontal);
    pitch = fadds(pitch, pitchOffset);
    yaw = fadds(yaw, yawOffset);
    float q = pitch / ldf(GD(CloudPitchScale));
    if (q > one) q = one;
    const float qcube = fmuls(q, fmuls(q, q));
    const float pitchCenter = fmadds(fmuls(verticalSize, ldf(GD(StarSizeBase))), fmadds(qcube, ldf(GD(CloudPitchGain)), one), pitch);
    const float sideSize = fmuls(fmuls(angularSize, sizeBase), fmadds(qcube, ldf(GD(CloudSideGain)), one));
    const float tallSize = fmuls(fmuls(angularSize, sizeBase), fadds(fadds(qcube, qcube), one));
    // corners 0, 1 at the clamped pitch centre, 2, 3 at the pitch (sin/cos of a pitch once: the same call, the same value)
    const float cpTop = pitchCenter > pitchLimit ? pitchLimit : pitchCenter;
    const float cTop = g1(cc, kCosf, cpTop), sTop = g1(cc, kSinf, cpTop);
    const float cBot = g1(cc, kCosf, pitch), sBot = g1(cc, kSinf, pitch);
    const float yaws[4] = {fadds(yaw, sideSize), fsubs(yaw, sideSize), fsubs(yaw, tallSize), fadds(yaw, tallSize)};
    for (int k = 0; k < 4; k++) {
        const float c = k < 2 ? cTop : cBot, sp = k < 2 ? sTop : sBot;
        const float ss = g1(cc, kSinf, yaws[k]), cs = g1(cc, kCosf, yaws[k]);
        const float v[3] = {fmuls(c, ss), sp, fmuls(c, cs)};
        uint8_t* o = out + 20 * k;
        for (int a = 0; a < 3; a++) put(o + 4 * a, fadds(fmuls(v[a], farPlane), in.eye[a]));  // (addVector: v + eye)
        put(o + 12, k == 1 || k == 2 ? one : zero);
        put(o + 16, k >= 2 ? one : zero);
    }
}

// wave crests: the view matrix drawWave builds (into guest scratch VM; scratch RS for the rotation source, RT)
void wave_view(Cpu& cc, const WaveIn& in, uint32_t vm, uint32_t rs, uint32_t rt) {
    for (int w = 0; w < 12; w++) st32(rs + 4 * w, in.rotation[w]);
    guest_call(&cc, kMtxInverse, {rs, vm});
    const float angle = fmuls((float)in.bank, ldf(GD(BankToRadians)));
    cc.f[1].ps0 = cc.f[1].ps1 = (double)fmuls(angle, ldf(GD(DegreeToRadians)));
    guest_call(&cc, kMtxRotDeg, {rt, 90});
    guest_call(&cc, kMtxConcat, {vm, rt, vm});
}
// crest i: drawWave's arithmetic -> 80 bytes; false when the crest would not be drawn (height <= 0)
bool wave_crest(Cpu& cc, const WaveIn& in, int i, uint32_t vm, uint32_t vin, uint32_t vout, uint8_t* out) {
    const uint8_t* r = in.rec.data() + kWaveStride * i;
    const float zero = ldf(GD(Zero)), one = ldf(GD(One));
    float center[3];
    for (int a = 0; a < 3; a++) center[a] = fadds(bef(r + 0xC + 4 * a), bef(r + 4 * a));
    const double sine = g1d(cc, kSinf, bef(r + 0x24));
    if (!(sine > zero)) return false;
    const float scaled = fmuls(in.scale, bef(r + 0x1C));
    float height;
    {   // wave_multiply_double
        uint64_t bits;
        memcpy(&bits, &sine, 8);
        bits = (bits & 0xFFFFFFFFF8000000ull) + (bits & 0x08000000ull);
        double s;
        memcpy(&s, &bits, 8);
        height = (float)((double)scaled * s);
    }
    float width = fmuls(in.scale, bef(r + 0x1C));
    const float fade = bef(r + 0x2C);
    height = fmuls(height, fade);
    const float step = fmuls((float)((uint32_t)i * 31), ldf(GD(WaveFadeScale)));
    const float widthFade = fnmsubs(step, height, fade);
    width = fmuls(fmuls(width, in.bottom), widthFade);
    if (!(height > zero)) return false;
    const float widthScale = ldf(GD(WaveWidthScale)), speed = bef(r + 0x18);
    const float offset = fmuls(width, fmuls(widthScale, speed));
    const float skewY = in.skew_dir, skew = in.skew_width;
    const float in_x[4] = {skewY < zero ? fsubs(-fmuls(offset, skew), width) : fmadds(offset, skew, -width),
                           skewY < zero ? fnmsubs(offset, skew, width) : fmadds(offset, skew, width), width, -width};
    const float in_y[4] = {height, height, zero, zero};
    static const int uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int k = 0; k < 4; k++) {
        st32(vin, fw(in_x[k]));
        st32(vin + 4, fw(in_y[k]));
        st32(vin + 8, fw(zero));
        guest_call(&cc, kMtxMultVec, {vm, vin, vout});
        uint8_t* o = out + 20 * k;
        for (int a = 0; a < 3; a++)
            put(o + 4 * a, fadds(fadds(center[a], ldf(vout + 4 * a)), ldf(GD(WaveOffsets) + 4 * a)));
        put(o + 12, uv[k][0] ? one : zero);
        put(o + 16, uv[k][1] ? one : zero);
    }
    return true;
}

struct Site {
    uint32_t pk = 0;
    uint64_t step = ~0ull;    // the logic step the inputs were kept on
    bool checked = false;     // the step's sprites chosen (first replay)
    std::vector<uint16_t> ids;    // sprites to write (cloud: layer * 100 + i; wave: i)
    std::vector<uint32_t> bufs;   // their logic slots' buffers
    uint32_t lo = 0, hi = 0;      // the range registered (all slots' buffers)
};
struct G {
    Site cloud, wave;
    CloudIn cloud_in;
    WaveIn wave_in;
    uint32_t cloud_side = 0;  // the logic side of the cloud bank
    float far_plane = 0;      // the far plane the logic pass's cards matched
    bool far_known = false;
    // stats (per report)
    // cloud colours: the view's projection x view matrix (dComIfGd_getProjViewMtx) and the logic pass's copy of it
    uint32_t projview = 0;
    uint8_t projview_logic[64];
    bool projview_kept = false;
    std::vector<uint8_t> col_ok;  // per noted card: its colour reproduced on the logic pass (layer 0 only)
    uint64_t col_match = 0, col_differ = 0, colours = 0;
    uint64_t passes = 0, ns = 0, cards = 0, crests = 0, steps = 0, c_match = 0, c_differ = 0, w_match = 0,
             w_differ = 0, w_gone = 0, no_far = 0;
} g;

bool cloud_on() { return (s2_mask() & 128) != 0; }
bool wave_on() { return (s2_mask() & 256) != 0; }

// every slot buffer of a bank as one range (they are allocated together); false when they are not
bool register_bufs(Site& s, const std::vector<uint32_t>& all) {
    uint32_t lo = ~0u, hi = 0;
    for (uint32_t b : all) {
        if (!b) continue;
        lo = std::min(lo, b);
        hi = std::max(hi, b + kVtxBytes);
    }
    if (lo >= hi || hi - lo > (1u << 20)) return false;
    if (s.lo != lo || s.hi != hi) vtx_add_range(lo, hi - lo);
    s.lo = lo, s.hi = hi;
    return true;
}

uint32_t cloud_buf(uint32_t pk, int id, uint32_t side) {
    return ld32(pk + kCloudBank1 + (2 * id + side) * kSlotSize);
}
uint32_t wave_buf(uint32_t pk, int i, uint32_t side) {
    return ld32(pk + kWaveGeometry + kGeometrySize * i + side * kSlotSize);
}

// drawVrkumo's colour of a layer-0 card (its uniform's triplets 0 and 1: the colour and the highlight), from its record
// (REC, guest order) and its 4 vertices as written (BUF, 20 bytes each), with the game's own calls in the draw's order.
// S: 0x190 bytes of guest scratch; cloud_colour_setup first (the pass's colours, sun projection: S + 0 .. 0x60).
void cloud_colour_setup(Cpu& cc, uint32_t S) {
    const uint32_t env = GD(kEnv);
    auto col = [&](uint32_t from, uint32_t scale, uint32_t out) {
        for (int i = 0; i < 3; i++) st8(S + 0x100 + i, ld8(env + from + i));
        guest_call(&cc, kConvColor, {S + 0x110, S + 0x100});
        cc.f[1].ps0 = cc.f[1].ps1 = (double)ldf(env + scale);
        guest_call(&cc, kScaleColor, {out, S + 0x110});
    };
    // (the draw: packed colour bytes 0..2 into a Color whose byte 3 the work area left; convertColor's 4th channel is
    // not used by the result's rgb, and the alpha is set after)
    st8(S + 0x103, 0);
    col(kEnvCloudColor, kEnvCloudColorScale, S + 0x120);
    col(kEnvCloudCenter, kEnvCloudCenterScale, S + 0x130);
    guest_call(&cc, kPrepProj, {S});
    const uint32_t sun = ld32(env + kEnvSun);
    if (sun) guest_call(&cc, kProject, {sun + kSunPos, S + 0x40, S});
}
void cloud_colour(Cpu& cc, uint32_t S, const uint8_t* rec, const uint8_t* buf) {
    const uint32_t env = GD(kEnv);
    const float one = ldf(GD(One)), zero = ldf(GD(Zero)), half = ldf(GD(Half));
    for (int k = 0; k < 4; k++)
        for (int a = 0; a < 3; a++) {
            uint32_t w;
            memcpy(&w, buf + 20 * k + 4 * a, 4);
            st32(S + 0x50 + 12 * k + 4 * a, __builtin_bswap32(w));
        }
    float fade = one;
    if (ld8(env + kEnvVrboxInvisible) == 0 && ld32(env + kEnvSun)) {
        auto scale = [&](uint32_t in, uint32_t out) {
            cc.f[1].ps0 = cc.f[1].ps1 = (double)half;
            guest_call(&cc, kScaleVec, {in, out});
        };
        guest_call(&cc, kSubVec, {S + 0x50, S + 0x80, S + 0x5C});
        scale(S + 0x80, S + 0x90);
        guest_call(&cc, kSubVec, {S + 0x68, S + 0xA0, S + 0x74});
        scale(S + 0xA0, S + 0xB0);
        guest_call(&cc, kSubVec, {S + 0x90, S + 0xC0, S + 0xB0});
        scale(S + 0xC0, S + 0xD0);
        for (int a = 0; a < 3; a++) st32(S + 0xE0 + 4 * a, ld32(S + 0xD0 + 4 * a));
        guest_call(&cc, kProject, {S + 0xE0, S + 0xF0, S});
        st32(S + 0xF8, ld32(S + 0x48));
        guest_call(&cc, kDist2, {S + 0x40, S + 0xF0});
        fade = g1(cc, kSqrtf, (float)cc.f[1].ps0);
        if (fade < ldf(GD(CloudFadeDistance))) fade = zero;
        else if (fade < ldf(GD(SkyRange))) fade = fsubs(fade, ldf(GD(CloudFadeDistance))) / ldf(GD(CloudFadeDistance));
        else fade = one;
    }
    float blend = fsubs(one, bef(rec + 0x24));
    if (blend > ldf(GD(Drag))) blend = fsubs(blend, fmuls(0.0f, ldf(GD(0x1004F590))));  // (layer 0: layerBlendOffset 0)
    guest_call(&cc, kSubColor, {S + 0x140, S + 0x120, S + 0x130});
    cc.f[1].ps0 = cc.f[1].ps1 = (double)blend;
    guest_call(&cc, kScaleColor, {S + 0x150, S + 0x140});
    guest_call(&cc, kAddColor, {S + 0x160, S + 0x130, S + 0x150});
    st32(S + 0x16C, fw(fmuls(bef(rec + 0x20), fade)));
    cc.f[1].ps0 = cc.f[1].ps1 = (double)ldf(GD(DirectionBias));
    guest_call(&cc, kScaleColor, {S + 0x170, S + 0x160});
}

}  // namespace

// the logic pass's per-frame function returned: keep the inputs it drew with
void s2_sky_logic_done() {
    if (!cloud_on() && !wave_on()) return;
    const uint32_t env = GD(kEnv), gi = GD(kGameInfo);
    const uint32_t cam = ld32(gi + kGiCamera), view = ld32(gi + kGiView);
    if (!cam || !view) return;
    if (cloud_on()) {
        Site& s = g.cloud;
        const uint32_t pk = ld32(env + kEnvVrkumo);
        if (pk >= 0x10000000 && pk < 0x50000000) {
            if (s.pk != pk) s = Site{}, g.far_known = false;
            s.pk = pk;
            s.step = interp::logic_steps();
            s.checked = false;
            g.cloud_in.read(pk, cam);
            g.projview_kept = g.projview != 0;  // (the painter draws the clouds with this pass's matrix)
            if (g.projview_kept) memcpy(g.projview_logic, ppc_ptr(g.projview), 64);
        }
    }
    if (wave_on()) {
        Site& s = g.wave;
        const uint32_t pk = ld32(env + kEnvWave);
        if (pk >= 0x10000000 && pk < 0x50000000) {
            if (s.pk != pk) s = Site{};
            s.pk = pk;
            s.step = interp::logic_steps();
            s.checked = false;
            g.wave_in.read(pk, cam, view);
        }
    }
}

// a replayed in-between (cheapib_models.h replay, after stage 1's camera and models)
void s2_sky_rerun(Cpu* c) {
    if ((!cloud_on() && !wave_on()) || !replaying() || verifying()) return;
    const uint64_t step = interp::logic_steps();
    const bool cl = cloud_on() && g.cloud.pk && g.cloud.step == step;
    const bool wv = wave_on() && g.wave.pk && g.wave.step == step;
    if (!cl && !wv) return;
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t env = GD(kEnv), gi = GD(kGameInfo);
    const uint32_t cam = ld32(gi + kGiCamera), view = ld32(gi + kGiView);
    if (!cam || !view) return;
    Cpu cc = *c;
    const uint32_t scr = (cc.r[1] - 0x400) & ~0x3Fu;  // guest scratch below the stack pointer
    cc.r[1] = scr - 0x100;
    const uint32_t vm = scr, rs = scr + 0x40, rt = scr + 0x80, vin = scr + 0xC0, vout = scr + 0xD0;
    std::vector<uint32_t> ranges;
    std::vector<uint8_t> bytes;
    uint8_t buf[kVtxBytes];
    g.passes++;

    if (cl) {
        Site& s = g.cloud;
        const uint32_t pk = s.pk;
        if (!s.checked) {  // the step's first replay: the cards whose logic bytes this arithmetic reproduces
            s.checked = true;
            s.ids.clear();
            s.bufs.clear();
            g.steps++;
            // (the painter draws the clouds after the per-frame function: its selector flip has happened by now)
            g.cloud_side = (ld32(pk + kCloudBank1 + kBankSelected) & 1) ^ 1;
            std::vector<uint32_t> all;
            for (int id = 0; id < kCloudCards; id++)
                for (uint32_t side = 0; side < 2; side++) all.push_back(cloud_buf(pk, id, side));
            const bool reg = register_bufs(s, all);
            // far plane candidates: the sky's default, the stage's own (drawVrkumo: stage info far plane - inset)
            float fars[2] = {ldf(GD(CloudFarPlane)), 0};
            int nfar = 1;
            {
                const uint32_t vt = ld32(gi + kGiStageVtable);
                const uint32_t fn = vt ? ld32(vt + kStageGetInfo) : 0;
                const uint32_t info = fn ? guest_call(&cc, fn, {gi + kGiStageVtable}) : 0;
                if (info >= 0x10000000 && info < 0x50000000) fars[nfar++] = fsubs(ldf(info + 4), ldf(GD(FarPlaneInset)));
            }
            const float minAlpha = ldf(GD(CloudMinimumAlpha)), minDraw = ldf(GD(CloudMinimumDrawAlpha));
            for (int layer = 0; layer < 3 && reg; layer++)
                for (int i = 0; i < 100; i++) {
                    const float alpha = bef(g.cloud_in.rec.data() + kCloudStride * i + 0x20);
                    if (!(alpha > minAlpha) || !(alpha > minDraw)) continue;
                    const int id = layer * 100 + i;
                    const uint32_t b = cloud_buf(pk, id, g.cloud_side);
                    if (!b) continue;
                    bool hit = false;
                    for (int f = 0; f < nfar && !hit; f++) {
                        if (g.far_known && fars[f] != g.far_plane) continue;
                        cloud_card(cc, g.cloud_in, layer, i, fars[f], buf);
                        if (memcmp(buf, ppc_ptr(b), kVtxBytes) == 0) {
                            hit = true;
                            g.far_plane = fars[f];
                            g.far_known = true;
                        }
                    }
                    if (!hit && g.far_known) {  // (the stage may have changed its far plane)
                        g.far_known = false;
                        for (int f = 0; f < nfar && !hit; f++) {
                            cloud_card(cc, g.cloud_in, layer, i, fars[f], buf);
                            if (memcmp(buf, ppc_ptr(b), kVtxBytes) == 0) hit = true, g.far_plane = fars[f], g.far_known = true;
                        }
                    }
                    if (hit) {
                        s.ids.push_back((uint16_t)id);
                        s.bufs.push_back(b);
                        g.c_match++;
                    } else {
                        g.c_differ++;
                        // debug: WWHD_CHEAPIB_SKY_DBG=n logs the first n cards that differ (computed vs both slots)
                        static int dbg = getenv("WWHD_CHEAPIB_SKY_DBG") ? atoi(getenv("WWHD_CHEAPIB_SKY_DBG")) : 0;
                        if (dbg > 0) {
                            dbg--;
                            cloud_card(cc, g.cloud_in, layer, i, fars[0], buf);
                            const uint32_t o = cloud_buf(pk, id, g.cloud_side ^ 1);
                            auto f = [](const uint8_t* p, int k) { return bef(p + 4 * k); };
                            LOG("[cheapib] sky dbg card %d/%d side %u far %g/%g (n %d) eye %g %g %g alpha %g: computed "
                                "%g %g %g %g %g | %g %g %g; slot %g %g %g %g %g | %g %g %g; other %g %g %g",
                                layer, i, g.cloud_side, (double)fars[0], (double)fars[1], nfar, g.cloud_in.eye[0],
                                g.cloud_in.eye[1], g.cloud_in.eye[2], alpha, f(buf, 0), f(buf, 1), f(buf, 2), f(buf, 3),
                                f(buf, 4), f(buf, 5), f(buf, 6), f(buf, 7), f(ppc_ptr(b), 0), f(ppc_ptr(b), 1),
                                f(ppc_ptr(b), 2), f(ppc_ptr(b), 3), f(ppc_ptr(b), 4), f(ppc_ptr(b), 5), f(ppc_ptr(b), 6),
                                f(ppc_ptr(b), 7), o ? f(ppc_ptr(o), 0) : 0.f, o ? f(ppc_ptr(o), 1) : 0.f,
                                o ? f(ppc_ptr(o), 2) : 0.f);
                        }
                    }
                }
            if (s.ids.empty() && reg) g.no_far++;
            // the colours (layer 0): reproduced from the logic pass's records, slot bytes and matrix (self-check)
            g.col_ok.assign(s.ids.size(), 0);
            if (!g.projview) g.projview = guest_call(&cc, kProjViewFn);
            if (g.projview_kept && g.projview) {
                uint8_t now_pv[64];
                memcpy(now_pv, ppc_ptr(g.projview), 64);
                memcpy(ppc_ptr(g.projview), g.projview_logic, 64);
                cloud_colour_setup(cc, scr + 0x100);
                for (size_t k = 0; k < s.ids.size(); k++) {
                    const int id = s.ids[k];
                    if (id >= 100) continue;
                    cloud_colour(cc, scr + 0x100, g.cloud_in.rec.data() + kCloudStride * id, ppc_ptr(s.bufs[k]));
                    const uint32_t mat = pk + kCloudUniforms + kUniformStride * id;
                    const bool ok = memcmp(ppc_ptr(scr + 0x260), ppc_ptr(mat + kMatTriplet0), 32) == 0;
                    g.col_ok[k] = ok;
                    ok ? g.col_match++ : g.col_differ++;
                }
                memcpy(ppc_ptr(g.projview), now_pv, 64);
            }
        }
        if (!s.ids.empty()) {
            CloudIn now;
            now.read(pk, cam);
            bool col_setup = false;
            for (size_t k = 0; k < s.ids.size(); k++) {
                const int id = s.ids[k];
                cloud_card(cc, now, id / 100, id % 100, g.far_plane, buf);
                if (id < 100 && k < g.col_ok.size() && g.col_ok[k]) {  // its colour at this pass, into the logic-bound buffer
                    if (!col_setup) cloud_colour_setup(cc, scr + 0x100), col_setup = true;
                    cloud_colour(cc, scr + 0x100, now.rec.data() + kCloudStride * id, buf);
                    const uint32_t mat = pk + kCloudUniforms + kUniformStride * id;
                    {   // debug: WWHD_CHEAPIB_SKY_COLTEST=1 writes alpha 0 (does the replayed colour reach the picture?)
                        static const int ct = getenv("WWHD_CHEAPIB_SKY_COLTEST") ? atoi(getenv("WWHD_CHEAPIB_SKY_COLTEST")) : 0;
                        if (ct == 1) st32(scr + 0x26C, 0), st32(scr + 0x27C, 0);
                    }
                    guest_call(&cc, kSetTriplet, {mat, scr + 0x260, 0});
                    guest_call(&cc, kSetTriplet, {mat, scr + 0x270, 1});
                    guest_call(&cc, kUploadPrevious, {mat});
                    g.colours++;
                }
                {   // debug: WWHD_CHEAPIB_SKY_JUMP=n logs the first n cards whose corner 0 moved more than 1 % of the far
                    // plane from the logic pass's
                    static int jn = getenv("WWHD_CHEAPIB_SKY_JUMP") ? atoi(getenv("WWHD_CHEAPIB_SKY_JUMP")) : 0;
                    if (jn > 0) {
                        const uint8_t* lg = ppc_ptr(s.bufs[k]);
                        float d = 0;
                        for (int a = 0; a < 3; a++) d += (bef(buf + 4 * a) - bef(lg + 4 * a)) * (bef(buf + 4 * a) - bef(lg + 4 * a));
                        if (d > 4e-7f * g.far_plane * g.far_plane) {
                            jn--;
                            const uint8_t* r = now.rec.data() + kCloudStride * (id % 100);
                            const uint8_t* r0 = g.cloud_in.rec.data() + kCloudStride * (id % 100);
                            LOG("[cheapib] sky jump card %d step %llu: corner0 %g %g %g (slot now %g %g %g); record pos %g %g %g "
                                "alpha %g falloff %g (logic %g %g %g alpha %g falloff %g, status %u/%u); eye %g %g %g (logic %g %g "
                                "%g) loop %g/%g", id, (unsigned long long)step, bef(buf), bef(buf + 4), bef(buf + 8), bef(lg),
                                bef(lg + 4), bef(lg + 8), bef(r + 4), bef(r + 8), bef(r + 12), bef(r + 0x20), bef(r + 0x24),
                                bef(r0 + 4), bef(r0 + 8), bef(r0 + 12), bef(r0 + 0x20), bef(r0 + 0x24), r[0], r0[0], now.eye[0],
                                now.eye[1], now.eye[2], g.cloud_in.eye[0], g.cloud_in.eye[1], g.cloud_in.eye[2], now.loop,
                                g.cloud_in.loop);
                        }
                    }
                }
                ranges.insert(ranges.end(), {s.bufs[k], kVtxBytes, (uint32_t)bytes.size()});
                bytes.insert(bytes.end(), buf, buf + kVtxBytes);
                g.cards++;
            }
        }
    }

    if (wv) {
        Site& s = g.wave;
        const uint32_t pk = s.pk;
        if (!s.checked) {
            s.checked = true;
            s.ids.clear();
            s.bufs.clear();
            std::vector<uint32_t> all;
            for (int i = 0; i < kWaves; i++)
                for (uint32_t side = 0; side < 2; side++) all.push_back(wave_buf(pk, i, side));
            const int count = std::min<int>((int16_t)ld16(env + kEnvWaveCount), kWaves);
            if (register_bufs(s, all)) {
                wave_view(cc, g.wave_in, vm, rs, rt);
                for (int i = 0; i < count; i++) {
                    if (!ld8(pk + kWaveDrawn + i)) continue;
                    const uint32_t side = (ld32(pk + kWaveGeometry + kGeometrySize * i + kGeometrySelected) & 1) ^ 1;
                    const uint32_t b = wave_buf(pk, i, side);
                    if (!b) continue;
                    if (wave_crest(cc, g.wave_in, i, vm, vin, vout, buf) && memcmp(buf, ppc_ptr(b), kVtxBytes) == 0) {
                        s.ids.push_back((uint16_t)i);
                        s.bufs.push_back(b);
                        g.w_match++;
                    } else {
                        g.w_differ++;
                    }
                }
            }
        }
        if (!s.ids.empty() && ldf(env + kEnvWaveFlat) < ldf(GD(One))) {
            WaveIn now;
            now.read(pk, cam, view);
            wave_view(cc, now, vm, rs, rt);
            for (size_t k = 0; k < s.ids.size(); k++) {
                if (!wave_crest(cc, now, s.ids[k], vm, vin, vout, buf)) {
                    g.w_gone++;  // (a full pass would not draw it now; the logic pass's bytes stay)
                    continue;
                }
                ranges.insert(ranges.end(), {s.bufs[k], kVtxBytes, (uint32_t)bytes.size()});
                bytes.insert(bytes.end(), buf, buf + kVtxBytes);
                g.crests++;
            }
        }
    }

    vtx_emit(std::move(ranges), std::move(bytes));
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        LOG("[cheapib] s2 sky: cloud colours %.1f per pass, logic match %llu (differ %llu)", per(g.colours, g.passes),
            (unsigned long long)g.col_match, (unsigned long long)g.col_differ);
        g.colours = g.col_match = g.col_differ = 0;
        LOG("[cheapib] s2 sky: per replayed pass %.1f us; cloud cards %.1f, wave crests %.1f per pass (crests not drawn "
            "now %llu); logic match: cards %llu (differ %llu, far plane %g, steps without a match %llu), crests %llu "
            "(differ %llu)",
            per(g.ns, g.passes) / 1000.0, per(g.cards, g.passes), per(g.crests, g.passes), (unsigned long long)g.w_gone,
            (unsigned long long)g.c_match, (unsigned long long)g.c_differ, (double)g.far_plane,
            (unsigned long long)g.no_far, (unsigned long long)g.w_match, (unsigned long long)g.w_differ);
        g.passes = g.ns = g.cards = g.crests = g.steps = g.c_match = g.c_differ = g.w_match = g.w_differ = g.w_gone =
            g.no_far = 0;
    }
}

}  // namespace cheapib
