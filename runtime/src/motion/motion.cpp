// Gyro (motion) input for the virtual GamePad (see motion.h).
#include "motion.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "dsu.h"

void log_msg(const char* fmt, ...);
namespace mods { double game_time(); }

namespace motion {
namespace {

using Clock = std::chrono::steady_clock;
constexpr float kRadToDeg = 57.2957795f;
// a device turning faster than this (rad/s, about 20 deg/s) takes over from a resting one
constexpr float kMoving = 0.35f;

std::mutex g_mu;
Settings g_settings;
bool g_aiming = false;
int g_gyro_controllers = 0;
float g_mouse_dx = 0, g_mouse_dy = 0;
const bool g_log = getenv("WWHD_GYRO_LOG") != nullptr;  // raw samples and the aim, twice a second

struct Device {
    Fusion fusion;
    uint64_t last_ns = 0;
    Clock::time_point seen{}, moved{}, log_at{};
    bool stalled = false;       // "no motion" logged, waiting for samples again
    int bad_stamps = 0;         // samples in a row whose sensor time did not advance plausibly
    bool stamps_logged = false;
    unsigned samples = 0;       // since the last log line
    Aim logged_aim;             // aim since the last log line
    Vec3 last_gyro, last_acc;
};
constexpr uint64_t kNone = ~0ull, kDsuDevice = ~1ull;
std::map<uint64_t, Device> g_devices;   // controllers (key: host device id) and the DSU slot (key kDsuDevice)
uint64_t g_active = kNone;              // the device that turns the virtual GamePad
Aim g_pending;                          // the active device's aim not yet read by the game
VirtualPad g_pad;                       // the one virtual GamePad all sources turn
std::unique_ptr<dsu::Client> g_dsu;
VpadMotion g_last;                      // the previous read's values (repeated reads)
Clock::time_point g_last_read{}, g_read_log_at{};
double g_last_game_time = -1;           // WWHD_TEST_GYRO: game time of the previous read
Aim g_read_aim;                         // aim given to the GamePad since the last log line
float g_recalibrate_prev[2] = {};       // pad, key: held at the previous poll
// the right stick the game aims with: the game ignores the gyro while it is pushed (stick drift)
bool g_stick_out = false, g_stick_logged = false;
float g_stick_x = 0, g_stick_y = 0;
Clock::time_point g_stick_since{};

double secs(Clock::duration d) { return std::chrono::duration<double>(d).count(); }
std::string device_name(uint64_t id) {
    return id == kDsuDevice ? "the Cemuhook server" : "controller " + std::to_string((unsigned long long)id);
}

// a sample from `id` (g_mu held). The device the player moves turns the virtual GamePad: the active one
// keeps it while it moves or sends, another takes over when it turns while the active one rests or is quiet.
void feed(uint64_t id, uint64_t t_ns, Vec3 gyro_h, Vec3 acc_h) {
    const auto now = Clock::now();
    auto [it, fresh] = g_devices.try_emplace(id);
    Device& d = it->second;
    // dt from the sensor's own clock; the arrival time when that clock does not advance plausibly (a
    // driver without timestamps, a clock that stalls or wraps): the motion must never stop because of it
    float dt = 0;
    if (!fresh) {
        const double ds = d.last_ns && t_ns > d.last_ns ? (double)(t_ns - d.last_ns) * 1e-9 : -1.0;
        if (ds > 0 && ds < 0.25) {
            dt = (float)ds;
            d.bad_stamps = 0;
        } else {
            dt = (float)std::min(secs(now - d.seen), 0.1);
            if (++d.bad_stamps == 50 && !d.stamps_logged) {
                d.stamps_logged = true;
                log_msg("[gyro] %s: the sensor timestamps do not advance, using the arrival times", device_name(id).c_str());
            }
        }
        if (d.stalled) log_msg("[gyro] motion from %s again (after %.1f s without samples)", device_name(id).c_str(), secs(now - d.seen));
    }
    d.stalled = false;
    d.last_ns = t_ns;
    d.seen = now;
    d.samples++;
    d.last_gyro = gyro_h;
    d.last_acc = acc_h;
    d.fusion.update(dt, gyro_h, acc_h, g_settings.tuning.axis);
    const Aim aim = d.fusion.take();
    const bool moving = d.fusion.rate() > kMoving;
    if (moving) d.moved = now;
    bool take_over = g_active == kNone || !g_devices.count(g_active);
    if (!take_over && g_active != id) {
        const Device& a = g_devices[g_active];
        take_over = secs(now - a.seen) > 0.5 || (moving && secs(now - a.moved) > 0.5);
    }
    if (take_over && g_active != id) {
        log_msg("[gyro] motion from %s", device_name(id).c_str());
        g_active = id;
        g_pending = {};
    }
    if (g_active == id) g_pending = g_pending + aim;
    d.logged_aim = d.logged_aim + aim;
    if (g_log && secs(now - d.log_at) >= 0.5) {
        const double span = d.log_at.time_since_epoch().count() ? secs(now - d.log_at) : 0.5;
        const Vec3 b = d.fusion.bias(), g = d.fusion.gravity();
        log_msg("[gyro] %s%s: %.0f samples/s, gyro %.3f %.3f %.3f rad/s, acc %.2f %.2f %.2f g, bias %.3f %.3f %.3f%s, "
                "gravity %.2f %.2f %.2f, aim %.1f %.1f deg/s (%s)",
                device_name(id).c_str(), g_active == id ? " (active)" : "", d.samples / span, gyro_h.x, gyro_h.y,
                gyro_h.z, acc_h.x, acc_h.y, acc_h.z, b.x, b.y, b.z, d.fusion.calibrated() ? "" : " (calibrating)", g.x,
                g.y, g.z, d.logged_aim.yaw * kRadToDeg / span, d.logged_aim.pitch * kRadToDeg / span,
                axis_label(g_settings.tuning.axis));
        d.log_at = now;
        d.samples = 0;
        d.logged_aim = {};
    }
}

void dsu_sink(const dsu::PadData& p) {
    Vec3 g, a;
    from_dsu(p.gyro, p.accel, g, a);
    uint64_t t = p.timestamp_us ? p.timestamp_us * 1000 : (uint64_t)Clock::now().time_since_epoch().count();
    std::lock_guard lk(g_mu);
    if (g_settings.source == kCemuhook) feed(kDsuDevice, t, g, a);
}

// the Cemuhook client starts and stops outside g_mu: its thread's sink takes g_mu, and stop() joins it
std::mutex g_dsu_mu;
void apply_dsu(const Settings& s) {
    std::lock_guard lk(g_dsu_mu);
    if (s.source == kCemuhook) {
        if (!g_dsu) g_dsu = std::make_unique<dsu::Client>(dsu_sink);
        g_dsu->start(s.dsu_host, (uint16_t)std::clamp(s.dsu_port, 1, 65535), (uint8_t)std::clamp(s.dsu_slot, 0, 3));
    } else if (g_dsu) {
        g_dsu->stop();
    }
}

// WWHD_TEST_GYRO=from-to:yaw:pitch,... (game-time seconds, degrees per second)
struct TestTurn { double from, to; float yaw, pitch; };
const std::vector<TestTurn>& test_turns() {
    static const std::vector<TestTurn> v = [] {
        std::vector<TestTurn> out;
        const char* e = getenv("WWHD_TEST_GYRO");
        double a, b; float y, p; int n;
        while (e && sscanf(e, "%lf-%lf:%f:%f%n", &a, &b, &y, &p, &n) == 4) {
            out.push_back({a, b, y, p});
            e += n;
            if (*e != ',') break;
            e++;
        }
        if (!out.empty()) log_msg("[gyro] WWHD_TEST_GYRO: %zu turn(s)", out.size());
        return out;
    }();
    return v;
}

}  // namespace

const char* source_id(int s) {
    static const char* ids[] = {"off", "controller", "cemuhook", "mouse"};
    return s >= 0 && s < kSourceCount ? ids[s] : "off";
}
const char* source_label(int s) {
    static const char* labels[] = {"Off", "Controller gyro", "Cemuhook (DSU)", "Mouse (Steam Input gyro to mouse)"};
    return s >= 0 && s < kSourceCount ? labels[s] : "Off";
}
int source_from_id(const std::string& id) {
    for (int i = 0; i < kSourceCount; i++)
        if (id == source_id(i)) return i;
    return -1;
}

std::string value_of(const Settings& s, const std::string& k) {
    auto f = [](float x) { char b[32]; snprintf(b, sizeof b, "%g", x); return std::string(b); };
    if (k == "gyro.source") return source_id(s.source);
    if (k == "gyro.axis") return axis_id(s.tuning.axis);
    if (k == "gyro.sensitivityX") return f(s.tuning.sensitivity_x);
    if (k == "gyro.sensitivityY") return f(s.tuning.sensitivity_y);
    if (k == "gyro.invertX") return s.tuning.invert_x ? "1" : "0";
    if (k == "gyro.invertY") return s.tuning.invert_y ? "1" : "0";
    if (k == "gyro.mouseDegrees") return f(s.mouse_degrees);
    if (k == "gyro.dsuHost") return s.dsu_host;
    if (k == "gyro.dsuPort") return std::to_string(s.dsu_port);
    if (k == "gyro.dsuSlot") return std::to_string(s.dsu_slot);
    if (k == "gyro.recenterPad") return std::to_string(s.recenter_pad);
    if (k == "gyro.recenterKey") return std::to_string(s.recenter_key);
    return {};
}
std::string to_ini(const Settings& s) {
    std::string out;
    for (const char* k : kKeys) out += std::string(k) + "=" + value_of(s, k) + "\n";
    return out;
}
void from_kv(Settings& s, const std::string& k, const std::string& v) {
    auto num = [&](float lo, float hi, float def) {
        char* end = nullptr;
        float x = strtof(v.c_str(), &end);
        return end && end != v.c_str() && x == x ? std::clamp(x, lo, hi) : def;
    };
    const float lo = Tuning::kMinSensitivity, hi = Tuning::kMaxSensitivity, def = Tuning::kDefaultSensitivity;
    if (k == "gyro.source") { int i = source_from_id(v); if (i >= 0) s.source = i; }
    else if (k == "gyro.axis") { int i = axis_from_id(v.c_str()); if (i >= 0) s.tuning.axis = i; }
    else if (k == "gyro.sensitivityX") s.tuning.sensitivity_x = num(lo, hi, def);
    else if (k == "gyro.sensitivityY") s.tuning.sensitivity_y = num(lo, hi, def);
    else if (k == "gyro.invertX") s.tuning.invert_x = v == "1";
    else if (k == "gyro.invertY") s.tuning.invert_y = v == "1";
    else if (k == "gyro.mouseDegrees") s.mouse_degrees = num(0.005f, 2.0f, 0.1f);
    else if (k == "gyro.dsuHost") { if (!v.empty() && v.size() < 256) s.dsu_host = v; }
    else if (k == "gyro.dsuPort") s.dsu_port = (int)num(1, 65535, 26760);
    else if (k == "gyro.dsuSlot") s.dsu_slot = (int)num(0, 3, 0);
    else if (k == "gyro.recenterPad") s.recenter_pad = (int)num(0, 64, 0);
    else if (k == "gyro.recenterKey") s.recenter_key = (int)num(-1, 255, -1);
}
void upgrade_from_first_release(Settings& s) {
    if (s.tuning.sensitivity_x == 1.0f) s.tuning.sensitivity_x = Tuning::kDefaultSensitivity;
    if (s.tuning.sensitivity_y == 1.0f) s.tuning.sensitivity_y = Tuning::kDefaultSensitivity;
}

Settings settings() {
    std::lock_guard lk(g_mu);
    return g_settings;
}

bool env_override() {
    static const bool e = [] {
        const char* v = getenv("WWHD_GYRO");
        return v && source_from_id(v) >= 0;
    }();
    return e;
}

void set_settings(const Settings& in) {
    Settings s = in;
    if (env_override()) s.source = source_from_id(getenv("WWHD_GYRO"));
    bool restart_dsu;
    {
        std::lock_guard lk(g_mu);
        const bool source_changed = s.source != g_settings.source;
        const bool dsu_changed = s.dsu_host != g_settings.dsu_host || s.dsu_port != g_settings.dsu_port || s.dsu_slot != g_settings.dsu_slot;
        if (s.tuning.axis != g_settings.tuning.axis) log_msg("[gyro] left/right: %s", axis_label(s.tuning.axis));
        g_settings = s;
        if (source_changed) {
            log_msg("[gyro] source: %s", source_label(s.source));
            // the virtual GamePad stays where it is (the game would see a jump otherwise)
            g_devices.clear();
            g_active = kNone;
            g_pending = {};
            g_mouse_dx = g_mouse_dy = 0;
        }
        restart_dsu = source_changed || (dsu_changed && s.source == kCemuhook);
    }
    if (!restart_dsu && s.source == kCemuhook) {
        std::lock_guard lk(g_dsu_mu);
        restart_dsu = !(g_dsu && g_dsu->running());
    }
    if (restart_dsu) apply_dsu(s);
}

void controller_sample(uint64_t device, uint64_t t_ns, const float gyro[3], const float accel[3]) {
    Vec3 g, a;
    from_sdl(gyro, accel, g, a);
    if (!t_ns) t_ns = (uint64_t)Clock::now().time_since_epoch().count();
    std::lock_guard lk(g_mu);
    if (g_settings.source == kController) feed(device, t_ns, g, a);
}
void controller_gone(uint64_t device) {
    std::lock_guard lk(g_mu);
    if (g_devices.erase(device) && g_settings.source == kController) log_msg("[gyro] %s disconnected", device_name(device).c_str());
    if (g_active == device) {
        g_active = kNone;
        g_pending = {};
    }
}
bool wants_controller_sensors() {
    std::lock_guard lk(g_mu);
    return g_settings.source == kController;
}
void set_gyro_controllers(int n) {
    std::lock_guard lk(g_mu);
    if (n != g_gyro_controllers) log_msg("[gyro] controllers with motion sensors: %d", n);
    g_gyro_controllers = n;
}
int gyro_controllers() {
    std::lock_guard lk(g_mu);
    return g_gyro_controllers;
}

void mouse_motion(float dx, float dy) {
    std::lock_guard lk(g_mu);
    if (g_settings.source != kMouse || !g_aiming) return;
    g_mouse_dx += dx;
    g_mouse_dy += dy;
}
bool mouse_drives_gyro() {
    std::lock_guard lk(g_mu);
    return g_settings.source == kMouse && g_aiming;
}
void set_aiming(bool a) {
    std::lock_guard lk(g_mu);
    if (a != g_aiming && g_settings.source != kOff) log_msg("[gyro] the game %s", a ? "aims (gyro in use)" : "stopped aiming");
    if (a && !g_aiming) g_mouse_dx = g_mouse_dy = 0;
    g_aiming = a;
}
bool aiming() {
    std::lock_guard lk(g_mu);
    return g_aiming;
}

void recalibrate() {
    std::lock_guard lk(g_mu);
    for (auto& [id, d] : g_devices) d.fusion.recalibrate();
    g_pending = {};
    g_mouse_dx = g_mouse_dy = 0;
    log_msg("[gyro] recalibrating: the bias is learnt anew when the controller rests");
}

void poll_recalibrate(const float* pad, const bool* keys) {
    int p, k;
    {
        std::lock_guard lk(g_mu);
        if (g_settings.source == kOff) return;
        p = g_settings.recenter_pad;
        k = g_settings.recenter_key;
    }
    float now[2] = {p > 0 && pad ? pad[p] : 0.0f, k >= 0 && k < 256 && keys && keys[k] ? 1.0f : 0.0f};
    bool press = false;
    for (int i = 0; i < 2; i++) {
        if (now[i] > 0.5f && g_recalibrate_prev[i] <= 0.5f) press = true;
        g_recalibrate_prev[i] = now[i];
    }
    if (press) recalibrate();
}

bool drives_gamepad() {
    std::lock_guard lk(g_mu);
    return g_settings.source != kOff || !test_turns().empty();
}

void right_stick(float x, float y) {
    std::lock_guard lk(g_mu);
    const auto now = Clock::now();
    const bool out = std::sqrt(x * x + y * y) > 0.1f;  // the game's dead zone (CalcSubjectAngle)
    if (out && !g_stick_out) g_stick_since = now;
    if (!out) g_stick_logged = false;
    g_stick_out = out;
    g_stick_x = x;
    g_stick_y = y;
    if (out && g_aiming && g_settings.source != kOff && !g_stick_logged && secs(now - g_stick_since) > 2.0) {
        g_stick_logged = true;
        log_msg("[gyro] the right stick has rested at %.2f, %.2f for 2 s while aiming: the game ignores the gyro until "
                "it is back within 0.1 of the centre (stick drift? raise the stick dead zone in Controls)", x, y);
    }
}

VpadMotion vpad(bool repeat) {
    std::lock_guard lk(g_mu);
    if (repeat) return g_last;
    const auto now = Clock::now();
    const float dt = g_last_read.time_since_epoch().count() ? (float)secs(now - g_last_read) : 0.0f;
    g_last_read = now;
    Aim aim;
    float step = std::min(dt, 0.1f);
    if (!test_turns().empty()) {
        // game time, so a test turns the same however fast the machine runs; no sensitivity (it stands for
        // the GamePad itself)
        const double t = mods::game_time();
        step = g_last_game_time >= 0 ? (float)std::clamp(t - g_last_game_time, 0.0, 0.1) : 0.0f;
        g_last_game_time = t;
        float yaw = 0, pitch = 0;
        for (auto& tt : test_turns())
            if (t >= tt.from && t < tt.to) yaw = tt.yaw, pitch = tt.pitch;
        aim = {yaw * step / kRadToDeg, pitch * step / kRadToDeg};
    } else if (g_settings.source == kMouse) {
        // the mouse gives angles: the whole movement since the previous read, however long the frame took
        const float k = g_settings.mouse_degrees / kRadToDeg;
        aim = g_settings.tuning.apply({g_mouse_dx * k, -g_mouse_dy * k});
        g_mouse_dx = g_mouse_dy = 0;
    } else if (g_settings.source == kController || g_settings.source == kCemuhook) {
        aim = g_settings.tuning.apply(g_pending);
        g_pending = {};
        auto it = g_devices.find(g_active);
        if (it != g_devices.end() && !it->second.stalled && secs(now - it->second.seen) > 1.0) {
            it->second.stalled = true;
            log_msg("[gyro] no motion from %s for 1 s (the window lost focus, or the controller stopped sending)",
                    device_name(g_active).c_str());
        }
    }
    g_pad.turn(step, aim);
    g_read_aim = g_read_aim + aim;
    if (g_log && g_settings.source != kOff && secs(now - g_read_log_at) >= 0.5) {
        const double span = g_read_log_at.time_since_epoch().count() ? secs(now - g_read_log_at) : 0.5;
        log_msg("[gyro] GamePad turned %.1f right, %.1f up deg/s; aiming %d; right stick %.2f %.2f", g_read_aim.yaw * kRadToDeg / span,
                g_read_aim.pitch * kRadToDeg / span, (int)g_aiming, g_stick_x, g_stick_y);
        g_read_log_at = now;
        g_read_aim = {};
    }
    g_last = g_pad.vpad();
    return g_last;
}

std::string status() {
    std::string stick;
    {
        std::lock_guard lk(g_mu);
        if (g_aiming && g_stick_out && g_settings.source != kOff)
            stick = " The right stick is pushed: the game ignores the gyro until it is released.";
    }
    if (settings().source == kCemuhook) {  // (g_dsu_mu without g_mu: see apply_dsu)
        std::lock_guard dl(g_dsu_mu);
        return (g_dsu ? "Cemuhook: " + g_dsu->status() + "." : "Cemuhook: off.") + stick;
    }
    std::lock_guard lk(g_mu);
    switch (g_settings.source) {
    case kController: {
        auto it = g_devices.find(g_active);
        if (it != g_devices.end() && secs(Clock::now() - it->second.seen) < 1.0)
            return (it->second.fusion.calibrated() ? "Receiving motion." : "Receiving motion; calibrating (rest the controller for a second).") + stick;
        return g_gyro_controllers ? "A controller with a gyro is connected, no motion yet."
                                  : "No connected controller has a gyro (or this host cannot read it).";
    }
    case kMouse: return (g_aiming ? "The game aims: the mouse turns the GamePad." : "Mouse gyro waits for the game to aim.") + stick;
    default: return "Gyro off: the GamePad lies still.";
    }
}

}  // namespace motion
