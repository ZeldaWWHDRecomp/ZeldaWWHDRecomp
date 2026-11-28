#include "platform/gc_adapter_hid.h"
#include "input_map.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace gc_adapter_hid {

using input_map::Pad;

bool pc_mode_adapter(uint16_t vendor, uint16_t product) {
    return vendor == 0x0079 && (product == 0x1843 || product == 0x1844 || product == 0x1846);
}

AxisRange axis_range(long mn, long mx) {
    if (mn == -128 && mx == 127) return {-128, 127};
    if (mn >= 0 && mx > mn && mx <= 255) return {(int)mn, (int)mx};
    return {};
}

static int half_span(const AxisRange& r) { return (r.max - r.min + 1) / 2; }
static int mid(const AxisRange& r) { return r.min + half_span(r); }

int parse_report(const uint8_t* data, size_t len, const AxisRange& range, Raw& out) {
    if (!data) return -1;
    int port;
    bool reversed_c;
    const uint8_t* p;
    if (len == 10 && data[0] >= 1 && data[0] <= kPorts) {  // older firmware: the port first
        port = data[0] - 1;
        p = data + 1;
        reversed_c = true;
    } else if (len == 9) {  // firmware 0x7 and newer: one port, no port byte
        port = 0;
        p = data;
        reversed_c = false;
    } else {
        return -1;
    }
    auto axis = [&](uint8_t b) { return range.min < 0 ? (int)(int8_t)b : (int)b; };
    auto flip = [&](int v) { return range.min + range.max - v; };
    Raw r;
    r.x = p[0] & 0x01; r.a = p[0] & 0x02; r.b = p[0] & 0x04; r.y = p[0] & 0x08;
    r.l = p[0] & 0x10; r.r = p[0] & 0x20; r.z = p[0] & 0x80;
    r.start = p[1] & 0x02;
    r.up = p[1] & 0x10; r.right = p[1] & 0x20; r.down = p[1] & 0x40; r.left = p[1] & 0x80;
    r.lx = axis(p[2]);
    r.ly = axis(p[3]);
    r.cy = reversed_c ? flip(axis(p[4])) : axis(p[4]);
    r.cx = reversed_c ? flip(axis(p[5])) : axis(p[5]);
    r.la = axis(p[6]);
    r.ra = axis(p[7]);
    bool blank = true;
    for (int i = 0; i < 8; i++) blank &= p[i] == 0;
    r.blank = blank;
    out = r;
    return port;
}

static float dead_zone(float x, float dz) {
    const float a = std::fabs(x);
    if (a <= dz) return 0.f;
    return std::copysign(std::min((a - dz) / (1.f - dz), 1.f), x);
}

void StickAxis::reset(const AxisRange& r, int value) {
    centre = (float)value;
    neg = pos = half_span(r) * 88.f / 128.f;
}
float StickAxis::read(int value) {
    const float d = (float)value - centre;
    float x;
    if (d >= 0) { pos = std::max(pos, d); x = pos > 0 ? d / pos : 0.f; }
    else { neg = std::max(neg, -d); x = neg > 0 ? d / neg : 0.f; }
    return dead_zone(std::clamp(x, -1.f, 1.f), kStickDeadZone);
}

void TriggerAxis::reset(const AxisRange& r, int value) {
    rest = (float)value;
    const float span = float(r.max - r.min);
    full = std::max(r.min + span * 216.f / 255.f, rest + span / 8.f);
}
float TriggerAxis::read(int value) {
    rest = std::min(rest, (float)value);
    full = std::max(full, (float)value);
    const float t = full > rest ? ((float)value - rest) / (full - rest) : 0.f;
    return dead_zone(std::clamp(t, 0.f, 1.f), kTriggerDeadZone);
}

bool Port::feed(const Raw& r, const AxisRange& range, int index) {
    const bool was = listed(index);
    raw = r;
    if (r.blank) {  // nothing plugged into this port (or it was unplugged): forget the calibration
        calibrated = used = false;
        vlx = vly = vcx = vcy = vla = vra = 0;
        return was;
    }
    if (!calibrated) {
        // calibrate on a resting report only: a stick held at plug-in would otherwise read as centred
        const int m = mid(range), tol = half_span(range) * 48 / 128;
        auto rests = [&](int v) { return std::abs(v - m) <= tol; };
        if (!(rests(r.lx) && rests(r.ly) && rests(r.cx) && rests(r.cy))) return false;
        lx.reset(range, r.lx); ly.reset(range, r.ly);
        cx.reset(range, r.cx); cy.reset(range, r.cy);
        la.reset(range, r.la); ra.reset(range, r.ra);
        calibrated = true;
    }
    vlx = lx.read(r.lx); vly = ly.read(r.ly);
    vcx = cx.read(r.cx); vcy = cy.read(r.cy);
    vla = la.read(r.la); vra = ra.read(r.ra);
    used |= r.a || r.b || r.x || r.y || r.z || r.l || r.r || r.start || r.up || r.down || r.left || r.right ||
            std::fabs(vlx) > 0.5f || std::fabs(vly) > 0.5f || std::fabs(vcx) > 0.5f || std::fabs(vcy) > 0.5f ||
            vla > 0.5f || vra > 0.5f;
    return was != listed(index);
}

int Adapter::feed(const uint8_t* data, size_t len, bool* changed) {
    Raw r;
    const int port = parse_report(data, len, range, r);
    if (port < 0) return -1;
    const bool c = ports[port].feed(r, range, port);
    if (changed) *changed = c;
    last_port = port;
    return port;
}

void port_values(const Port& p, float* v) {
    for (int i = 0; i < input_map::kPadCount; i++) v[i] = 0;
    if (!p.calibrated) return;
    const Raw& r = p.raw;
    auto b = [&](bool on, int pad) { if (on) v[pad] = 1.f; };
    b(r.a, Pad::kPadA); b(r.b, Pad::kPadB); b(r.x, Pad::kPadX); b(r.y, Pad::kPadY);
    b(r.z, Pad::kPadRB);
    b(r.l, Pad::kPadMisc3); b(r.r, Pad::kPadMisc4);
    b(r.start, Pad::kPadMenu);
    b(r.up, Pad::kPadDUp); b(r.down, Pad::kPadDDown); b(r.left, Pad::kPadDLeft); b(r.right, Pad::kPadDRight);
    v[Pad::kPadLSRight] = std::max(p.vlx, 0.f); v[Pad::kPadLSLeft] = std::max(-p.vlx, 0.f);
    v[Pad::kPadLSUp] = std::max(p.vly, 0.f);    v[Pad::kPadLSDown] = std::max(-p.vly, 0.f);
    v[Pad::kPadRSRight] = std::max(p.vcx, 0.f); v[Pad::kPadRSLeft] = std::max(-p.vcx, 0.f);
    v[Pad::kPadRSUp] = std::max(p.vcy, 0.f);    v[Pad::kPadRSDown] = std::max(-p.vcy, 0.f);
    // the analog travel, and full travel while the click is held (a worn trigger may stop short)
    v[Pad::kPadLT] = r.l ? 1.f : p.vla;
    v[Pad::kPadRT] = r.r ? 1.f : p.vra;
}

bool shadows(std::string_view name, std::string_view category, bool extended, std::string_view product) {
    if (!product.empty() && name == product) return true;  // GameController names its pad after the product string
    if (extended) return false;
    std::string who(name);
    who += ' ';
    who += category;
    for (char& c : who) c = (char)std::tolower((unsigned char)c);
    return who.find("gamecube") != std::string::npos;
}

bool DumpLimiter::allow(uint64_t now) {
    if (lines >= max_lines) return false;
    if (started && now - last_ms < min_interval_ms) return false;
    started = true;
    last_ms = now;
    ++lines;
    return true;
}

std::string hex(const uint8_t* data, size_t len) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < len; i++) {
        snprintf(b, sizeof b, i ? " %02X" : "%02X", data[i]);
        s += b;
    }
    return s;
}

std::string describe(const Raw& r) {
    char b[160];
    snprintf(b, sizeof b, "%s %s %s %s %s %s %s %s | %s %s %s %s | main %d,%d C %d,%d L/R %d,%d",
             r.a ? "A" : ".", r.b ? "B" : ".", r.x ? "X" : ".", r.y ? "Y" : ".", r.z ? "Z" : ".", r.l ? "L" : ".",
             r.r ? "R" : ".", r.start ? "Start" : ".", r.up ? "up" : ".", r.down ? "down" : ".", r.left ? "left" : ".",
             r.right ? "right" : ".", r.lx, r.ly, r.cx, r.cy, r.la, r.ra);
    return b;
}

}  // namespace gc_adapter_hid
