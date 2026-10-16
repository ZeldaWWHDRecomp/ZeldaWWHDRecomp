// Volumetric clouds with optional cel shading (renderer-independent GLSL).
//
// The clouds are a layer of raymarched density between two altitudes above a curved sea, after the
// techniques in Schneider, "The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn" (SIGGRAPH 2015)
// and Hillaire, "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite" (2016): a
// cumulus height profile, a coverage threshold, detail erosion, Beer-Lambert transmittance with a
// powder term and a two-lobe Henyey-Greenstein phase function. Cel shading replaces the continuous
// result with the lighting at the cloud's visible surface, quantised into bands, and a hard-edged
// silhouette, so the clouds sit with the game's toon look. style.x blends realistic (0) to cel (1).
//
// kCloudsGlsl defines the parameter block (set 0, binding 0) and
//   vec4 wwhd_clouds(vec3 ro, vec3 rd, float maxDist, vec2 pixel)
// which returns premultiplied colour and coverage in linear RGB. A user appends a main() that
// builds the view ray (and limits maxDist by scene depth) and composites the result.
#pragma once

namespace clouds {

// std140 mirror of the GLSL block: one mat4 and vec4s only, so C++ and GLSL layouts agree.
struct Params {
    float invViewProj[16];  // column-major; NDC (x, y, depth 0..1) -> world
    float camPos[4];        // xyz camera position (y = altitude above the sea), w time in seconds
    float sunDir[4];        // xyz unit vector towards the sun, w sun intensity
    float sunColor[4];      // rgb sun colour (linear), w unused
    float skyTop[4];        // rgb ambient light from above, w coverage 0..1
    float skyHorizon[4];    // rgb ambient from the horizon / haze colour, w extinction per unit
    float layer[4];         // x bottom altitude, y top altitude, z shape frequency, w detail frequency
    float wind[4];          // xyz wind velocity (units per second), w sea/planet radius
    float style[4];         // x cel mix 0..1, y light bands, z band edge softness, w silver-lining rim
    float shadowTint[4];    // rgb colour of cel-shaded shadow sides, w haze distance
    float march[4];         // x view steps, y light steps, z max march distance, w silhouette softness
};
static_assert(sizeof(Params) == 64 + 10 * 16, "std140 layout");

inline constexpr const char* kCloudsGlsl = R"glsl(
layout(std140, set=0, binding=0) uniform CloudParams {
    mat4 invViewProj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
    vec4 skyTop;
    vec4 skyHorizon;
    vec4 layer;
    vec4 wind;
    vec4 style;
    vec4 shadowTint;
    vec4 march;
} P;

float cl_hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float cl_noise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(cl_hash(i + vec3(0,0,0)), cl_hash(i + vec3(1,0,0)), f.x),
                   mix(cl_hash(i + vec3(0,1,0)), cl_hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(cl_hash(i + vec3(0,0,1)), cl_hash(i + vec3(1,0,1)), f.x),
                   mix(cl_hash(i + vec3(0,1,1)), cl_hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float cl_fbm(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * cl_noise(p);
        norm += amp;
        p = p * 2.03 + vec3(17.1, 3.7, 9.2);
        amp *= 0.5;
    }
    return sum / norm;
}
// 1 at feature points, falling to 0 between them: the round "billows" of cumulus
float cl_worley(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    float d = 1.0;
    for (int z = -1; z <= 1; z++)
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec3 o = vec3(x, y, z);
        vec3 h = vec3(cl_hash(i + o), cl_hash(i + o + 19.19), cl_hash(i + o + 47.47));
        vec3 r = o + h - f;
        d = min(d, dot(r, r));
    }
    return 1.0 - sqrt(d);
}
float cl_remap(float v, float a, float b, float c, float d) {
    return c + (v - a) / (b - a) * (d - c);
}

// Curvature of the sea: altitude = y + (horizontal distance from the camera)^2 / 2R (a parabolic
// approximation of a sphere of radius R = wind.w). Unlike a ray-sphere test with R ~ 1e7 it stays
// accurate in 32-bit floats, and it bends the layer down to the horizon the same way.
float cl_altitude(vec3 p) {
    vec2 d = p.xz - P.camPos.xz;
    return p.y + dot(d, d) / (2.0 * P.wind.w);
}
float cl_height(vec3 p) { return clamp((cl_altitude(p) - P.layer.x) / (P.layer.y - P.layer.x), 0.0, 1.0); }

// distances along a ray from the camera to the surface altitude == A (x: crossing downwards,
// y: crossing upwards; negative: none). The ray's altitude is k t^2 + b t + c.
vec2 cl_layer_hits(vec3 ro, vec3 rd, float A) {
    float k = dot(rd.xz, rd.xz) / (2.0 * P.wind.w);
    float b = rd.y;
    float c = cl_altitude(ro) - A;
    float disc = b * b - 4.0 * k * c;
    if (disc < 0.0) return vec2(-1.0);
    float s = sqrt(disc);
    if (c < 0.0) return vec2(-1.0, -2.0 * c / (b + s));  // below A: one upward crossing
    if (b >= 0.0) return vec2(-1.0);                      // above A, heading up: never down to A
    float tNear = 2.0 * c / (s - b);
    float tFar = k > 0.0 ? c / (k * tNear) : 1e30;
    return vec2(tNear, tFar);
}

// cumulus profile: flat base, rounded top
float cl_profile(float h) {
    return smoothstep(0.0, 0.08, h) * smoothstep(1.0, 0.55, h);
}

float cl_density(vec3 p, bool detail, float lod) {
    float h = cl_height(p);
    float prof = cl_profile(h);
    if (prof <= 0.0) return 0.0;
    vec3 q = (p + P.wind.xyz * P.camPos.w) * P.layer.z;
    // weather: large-scale variation of coverage so clouds come in groups with gaps of sky
    float weather = smoothstep(0.25, 0.75, cl_fbm(vec3(q.xz * 0.18, 0.5), 2));
    float coverage = clamp(P.skyTop.w * mix(0.35, 1.45, weather), 0.0, 1.0);
    float shape = mix(cl_fbm(q, 3), cl_worley(q * 1.6), 0.45);
    shape *= prof;
    // less coverage higher up: tops grow out of a cloud body instead of floating on their own
    coverage *= 1.0 - 0.3 * h;
    float d = clamp(cl_remap(shape, 1.0 - coverage, 1.0, 0.0, 1.0), 0.0, 1.0);
    if (detail && d > 0.0 && lod < 1.0) {
        vec3 dq = (p + P.wind.xyz * P.camPos.w * 1.6) * P.layer.w;
        float e = mix(cl_fbm(dq, 2), 1.0 - cl_worley(dq), 0.5);
        // erode more at the bottom (wispy) than at the top (billowy)
        float eroded = clamp(cl_remap(d, e * mix(0.35, 0.18, h), 1.0, 0.0, 1.0), 0.0, 1.0);
        d = mix(eroded, d * 0.85, lod);
    }
    return d;
}

float cl_hg(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (12.566 * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

vec4 wwhd_clouds(vec3 ro, vec3 rd, float maxDist, vec2 pixel) {
    float alt = cl_altitude(ro);
    float t0, t1;
    vec2 sb = cl_layer_hits(ro, rd, P.layer.x), st = cl_layer_hits(ro, rd, P.layer.y);
    if (alt < P.layer.x) {          // below the layer (normal case): from the base to the top
        if (sb.y < 0.0) return vec4(0.0);
        t0 = sb.y; t1 = st.y;
    } else if (alt < P.layer.y) {   // inside the layer
        t0 = 0.0; t1 = (sb.x > 0.0) ? sb.x : st.y;
    } else {                        // above the layer
        if (st.x < 0.0) return vec4(0.0);
        t0 = st.x; t1 = (sb.x > 0.0) ? sb.x : st.y;
    }
    t1 = min(t1, min(maxDist, P.march.z));
    if (t1 <= t0) return vec4(0.0);

    int steps = int(P.march.x), lightSteps = int(P.march.y);
    float dt = (t1 - t0) / float(steps);
    // interleaved gradient noise: decorrelates the step positions between pixels (no banding)
    float jitter = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    float t = t0 + dt * jitter;

    vec3 sun = normalize(P.sunDir.xyz);
    float cosTheta = dot(rd, sun);
    float phase = mix(cl_hg(cosTheta, 0.6), cl_hg(cosTheta, -0.25), 0.3);
    float sigma = P.skyHorizon.w;
    float lightStep = (P.layer.y - P.layer.x) * 0.6 / float(lightSteps);

    float T = 1.0;
    vec3 color = vec3(0.0);
    // the visible surface, for cel shading: lighting where the ray is 40% occluded
    float surfLight = -1.0, surfHeight = 0.0, surfT = t1;
    float lastLight = 1.0, lastHeight = 0.0;

    for (int i = 0; i < steps; i++) {
        vec3 p = ro + rd * t;
        float lod = smoothstep(0.8e6, 2.5e6, t);
        float d = cl_density(p, true, lod);
        if (d > 0.001) {
            float od = 0.0;
            for (int j = 0; j < lightSteps; j++)
                od += cl_density(p + sun * lightStep * (float(j) + 0.5), false, 1.0) * lightStep;
            // optical depth towards the sun in layer thicknesses (independent of the world scale)
            float odn = od / (P.layer.y - P.layer.x);
            float beer = exp(-odn * 9.0);
            float powder = 1.0 - exp(-odn * 18.0 - d * 0.5);
            float h = cl_height(p);
            vec3 ambient = mix(P.skyHorizon.rgb, P.skyTop.rgb, h) * mix(0.55, 1.0, h);
            vec3 radiance = P.sunColor.rgb * P.sunDir.w * beer * mix(1.0, powder, 0.6) * phase * 12.566
                          + ambient;
            float Ts = exp(-d * sigma * dt);
            color += T * (1.0 - Ts) * radiance;
            T *= Ts;
            lastLight = beer; lastHeight = h;
            if (surfLight < 0.0 && T < 0.6) { surfLight = beer; surfHeight = h; surfT = t; }
            if (T < 0.01) break;
        }
        t += dt;
    }
    float alpha = 1.0 - T;
    if (alpha <= 0.0) return vec4(0.0);
    if (surfLight < 0.0) { surfLight = lastLight; surfHeight = lastHeight; surfT = t; }

    // ---- cel shading: banded light at the surface, hard silhouette, optional rim
    float lit = clamp(mix(surfLight, smoothstep(0.1, 0.8, surfHeight), 0.3), 0.0, 1.0);
    float bands = max(P.style.y, 1.0);
    float x = lit * bands;
    float band = floor(x) + smoothstep(0.5 - P.style.z, 0.5 + P.style.z, fract(x));
    float q = clamp(band / bands, 0.0, 1.0);
    vec3 litColor = mix(vec3(1.0), P.sunColor.rgb, 0.5) * mix(0.75, 1.0, P.sunDir.w);
    vec3 shadowColor = P.shadowTint.rgb * mix(0.8, 1.0, P.sunDir.w) * mix(vec3(1.0), P.skyTop.rgb, 0.25);
    vec3 cel = mix(shadowColor, litColor, q);
    float soft = P.march.w + 0.2 * smoothstep(1.0e6, 3.5e6, surfT);
    float celAlpha = smoothstep(0.35 - soft, 0.35 + soft, alpha);
    // silver lining: bright edge where the silhouette faces the sun
    float edge = (1.0 - smoothstep(0.4, 0.9, alpha)) * celAlpha;
    cel += P.style.w * edge * pow(max(cosTheta, 0.0), 6.0) * P.sunColor.rgb;

    // ---- blend realistic and cel, then fade distant clouds into the horizon haze
    vec3 outColor = mix(color, cel * celAlpha, P.style.x);
    float outAlpha = mix(alpha, celAlpha, P.style.x);
    float fog = exp(-max(surfT, 0.0) / P.shadowTint.w);
    outColor = mix(P.skyHorizon.rgb * outAlpha, outColor, fog);
    outAlpha *= mix(0.6, 1.0, fog);
    return vec4(outColor, outAlpha);
}
)glsl";

// Defaults for the Wind Waker sea: units are the game's (about 1 cm); the sea is at y = 0.
inline Params default_params() {
    Params p{};
    p.camPos[3] = 0.0f;
    p.sunDir[0] = 0.3f; p.sunDir[1] = 0.6f; p.sunDir[2] = 0.74f; p.sunDir[3] = 1.0f;
    p.sunColor[0] = 1.0f; p.sunColor[1] = 0.96f; p.sunColor[2] = 0.88f;
    p.skyTop[0] = 0.12f; p.skyTop[1] = 0.36f; p.skyTop[2] = 0.85f; p.skyTop[3] = 0.5f;
    p.skyHorizon[0] = 0.55f; p.skyHorizon[1] = 0.75f; p.skyHorizon[2] = 0.95f; p.skyHorizon[3] = 0.0016f;
    p.layer[0] = 150000.0f; p.layer[1] = 560000.0f; p.layer[2] = 1.0f / 260000.0f; p.layer[3] = 1.0f / 52000.0f;
    p.wind[0] = 900.0f; p.wind[1] = 0.0f; p.wind[2] = 300.0f; p.wind[3] = 6.0e7f;
    p.style[0] = 1.0f; p.style[1] = 3.0f; p.style[2] = 0.06f; p.style[3] = 0.25f;
    p.shadowTint[0] = 0.62f; p.shadowTint[1] = 0.7f; p.shadowTint[2] = 0.92f; p.shadowTint[3] = 3.5e6f;
    p.march[0] = 96.0f; p.march[1] = 6.0f; p.march[2] = 6.0e6f; p.march[3] = 0.06f;
    return p;
}

}  // namespace clouds
