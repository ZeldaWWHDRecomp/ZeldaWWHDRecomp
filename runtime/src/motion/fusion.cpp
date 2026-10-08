// Motion sensor math (see fusion.h).
#include "fusion.h"

#include <algorithm>
#include <cstring>

namespace motion {

static constexpr float kPi = 3.14159265358979f;
static constexpr float kTwoPi = 2 * kPi;
static constexpr float kStandardGravity = 9.80665f;
// gravity direction: how fast the accelerometer pulls the gyro-carried estimate (per second). Only the
// player-space axis uses it, as the axis to turn about, so it can follow quickly (the estimate itself never
// moves the view).
static constexpr float kGravityGain = 3.0f;
// player space: a turn about the controller's yaw and roll axes counts up to this much more than its share
// about the vertical, so a controller held at an angle still turns easily (JoyShockMapper's "relax factor")
static constexpr float kYawRelax = 1.41f;

Quat Quat::axis_angle(Vec3 a, float r) {
    float len = a.length();
    if (len <= 0) return {};
    float s = std::sin(r * 0.5f) / len;
    return {std::cos(r * 0.5f), a.x * s, a.y * s, a.z * s};
}
void Quat::normalize() {
    float n = std::sqrt(w * w + x * x + y * y + z * z);
    if (!(n > 0) || !std::isfinite(n)) { *this = {}; return; }
    w /= n; x /= n; y /= n; z /= n;
}
Vec3 Quat::rotate(Vec3 v) const {
    Vec3 u{x, y, z};
    Vec3 t = u.cross(v) * 2.0f;
    return v + t * w + u.cross(t);
}
Vec3 Quat::unrotate(Vec3 v) const { return conj().rotate(v); }

// SDL: x right, y up, z towards the player; H = SDL turned 180 degrees about x. Accelerometers measure
// the specific force (up at rest); acc_H is the gravity direction, so the sign flips once more.
void from_sdl(const float g[3], const float a[3], Vec3& gyro_h, Vec3& acc_h) {
    gyro_h = {g[0], -g[1], -g[2]};
    acc_h = Vec3{-a[0], a[1], a[2]} * (1.0f / kStandardGravity);
}
// DSU servers send DS4 conventions: gyro (pitch, yaw, roll) deg/s, acceleration in g
void from_dsu(const float g[3], const float a[3], Vec3& gyro_h, Vec3& acc_h) {
    const float k = kPi / 180.0f;
    gyro_h = {g[0] * k, g[1] * k, g[2] * k};
    acc_h = {a[0], -a[1], -a[2]};
}

const char* axis_id(int a) {
    static const char* ids[] = {"player", "yaw", "roll"};
    return a >= 0 && a < kAxisModeCount ? ids[a] : ids[0];
}
const char* axis_label(int a) {
    static const char* labels[] = {"Player space", "Yaw", "Roll"};
    return a >= 0 && a < kAxisModeCount ? labels[a] : labels[0];
}
int axis_from_id(const char* id) {
    for (int i = 0; i < kAxisModeCount; i++)
        if (id && !strcmp(id, axis_id(i))) return i;
    return -1;
}

static bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

void Fusion::recalibrate() {
    bias_samples_ = 0;  // the next rest replaces the bias (the old one stays until then)
    still_time_ = 0;
    have_grav_ = false;
}

Aim Fusion::aim_rate(Vec3 w, Vec3 g, int mode) {
    switch (mode) {
    case kYawAxis: return {w.y, w.x};
    case kRollAxis: return {w.z, w.x};
    default: {
        // the turn about the vertical (gravity, frame H: +y is a right turn of a flat controller, +z a right
        // turn of an upright one); the pitch axis (x) does not count, so tilting up never turns sideways
        float world_yaw = g.y * w.y + g.z * w.z;
        float yaw = std::min(std::fabs(world_yaw) * kYawRelax, std::sqrt(w.y * w.y + w.z * w.z));
        return {std::copysign(yaw, world_yaw), w.x};
    }
    }
}

void Fusion::update(float dt, Vec3 gyro, Vec3 acc, int mode) {
    if (!(dt > 0)) return;
    if (!finite(gyro) || !finite(acc)) return;  // a broken sample must not poison the bias or the gravity
    dt = std::min(dt, 0.1f);  // a stall (or a reconnected source) must not throw the aim around
    const float acc_len = acc.length();
    const bool have_acc = acc_len > 1e-4f;
    // bias: running average of the rate while the controller rests (rate small and steady, gravity
    // steady near 1 g). Before the first estimate the rate limit is loose (some controllers rest at
    // 5 deg/s), afterwards tight, so slow aiming never ends up in the bias.
    if (have_acc) {
        float acc_change = (acc - last_acc_h_).length();
        float limit = calibrated() ? 0.06f : 0.2f;
        bool still = (gyro - bias_).length() < limit && (gyro - last_gyro_h_).length() < 0.03f &&
                     std::fabs(acc_len - 1.0f) < 0.1f && acc_change < 0.02f;
        still_time_ = still ? still_time_ + dt : 0;
        if (still_time_ > 0.3f) {
            bias_samples_ = std::min(bias_samples_ + 1, kBiasSamples * 10);
            bias_ = bias_ + (gyro - bias_) * (1.0f / bias_samples_);
        }
        last_acc_h_ = acc;
    }
    last_gyro_h_ = gyro;
    Vec3 w = gyro - bias_;
    if (w.length() < kNoise) w = {};  // noise floor: a controller held still reports nothing
    rate_ = w.length();
    // gravity direction in the controller's frame: carried along by the gyro (a fixed world vector turns
    // against the body), pulled towards the accelerometer while it measures about 1 g (not shaken)
    if (have_acc && !have_grav_) { grav_ = acc * (1.0f / acc_len); have_grav_ = true; }
    grav_ = grav_ + grav_.cross(w) * dt;
    if (have_acc) {
        float err = std::fabs(acc_len - 1.0f);
        if (err < 0.25f) grav_ = grav_ + (acc * (1.0f / acc_len) - grav_) * std::min(1.0f, kGravityGain * dt * (1 - err / 0.25f));
    }
    float gl = grav_.length();
    grav_ = gl > 1e-3f ? grav_ * (1.0f / gl) : Vec3{0, 1, 0};
    Aim r = aim_rate(w, grav_, mode);
    aim_.yaw += r.yaw * dt;
    aim_.pitch += r.pitch * dt;
}

Aim Fusion::take() {
    Aim a = aim_;
    aim_ = {};
    return a;
}

// flat on the table, top away from the player: H turned 90 degrees about x
VirtualPad::VirtualPad() : q_(Quat::axis_angle({1, 0, 0}, kPi / 2)) {}

void VirtualPad::turn(float dt, Aim a) {
    if (!std::isfinite(a.yaw) || !std::isfinite(a.pitch)) return;
    // right = +y, up = +x of frame H, about the virtual GamePad's own axes (q' = q * turn)
    Vec3 v{a.pitch, a.yaw, 0};
    if (float len = v.length(); len > 0) {
        q_ = q_ * Quat::axis_angle(v, len);
        q_.normalize();
    }
    // the GamePad frame is a mirror of H: the turn (a pseudo-vector) flips once more
    Vec3 turn_v = Vec3{v.x, v.y, -v.z} * (-1.0f / kTwoPi);
    angle_ = angle_ + turn_v;
    if (dt > 0) {
        last_rate_v_ = turn_v * (1.0f / dt);
        window_ += dt;
    }
}

VpadMotion VirtualPad::vpad() {
    VpadMotion m;
    // gyro: mean rate since the previous read
    if (window_ > 0) m.gyro = (angle_ - angle_read_) * (float)(1.0 / window_);
    else m.gyro = last_rate_v_;
    last_rate_v_ = {};
    angle_read_ = angle_;
    window_ = 0;
    m.angle = angle_;
    // acc: gravity from the virtual pose (the game reads only dir; acc stays consistent with it)
    Vec3 d = q_.unrotate({0, 0, 1});  // world down in frame H
    // VPAD acc is the specific force, gravity points the other way: flat on a table it reads (0, -1, 0)
    m.acc = {-d.x, -d.y, d.z};
    m.acc_magnitude = m.acc.length();
    m.acc_variation = 0;
    // acc_xy: the GamePad's tilt in its screen plane (cos, sin), 1, 0 when upright
    float xy = std::sqrt(m.acc.x * m.acc.x + m.acc.y * m.acc.y);
    if (xy > 0.1f) { m.acc_xy[0] = -m.acc.y / xy; m.acc_xy[1] = m.acc.x / xy; }
    // dir: the GamePad's axes (x = H x, y = H y, z = -H z) in world coordinates, swizzled (x, z, y)
    auto sw = [](Vec3 v) { return Vec3{v.x, v.z, v.y}; };
    m.dir[0] = sw(q_.rotate({1, 0, 0}));
    m.dir[1] = sw(q_.rotate({0, 1, 0}));
    m.dir[2] = sw(q_.rotate({0, 0, -1}));
    return m;
}

}  // namespace motion
