// Controls drawing for the settings overlay (controls_view.h). The geometry is the one of the macOS
// Controls window (gfx/controls_ui.mm: controller_def, pro_body, the callout columns), ported to
// plain C++ and Dear ImGui's draw list; the colours are the overlay's.
#include "controls_view.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

using namespace input_map;

namespace overlay {
namespace {

// ---- colours (the overlay's sea blue)
ImU32 col(float r, float g, float b, float a = 1) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
struct Palette {
    ImU32 body = col(0.17f, 0.27f, 0.35f), outline = col(0.46f, 0.62f, 0.70f), well = col(0.07f, 0.13f, 0.19f),
          button = col(0.28f, 0.41f, 0.51f), buttonText = col(0.94f, 0.96f, 0.98f), screen = col(0.03f, 0.05f, 0.08f),
          screenText = col(0.88f, 0.92f, 0.95f), keycap = col(0.23f, 0.34f, 0.43f), keyEdge = col(0.07f, 0.12f, 0.17f),
          padChip = col(0.17f, 0.24f, 0.44f), padEdge = col(0.36f, 0.46f, 0.78f), box = col(0.05f, 0.11f, 0.17f, 0.92f),
          boxEdge = col(0.21f, 0.35f, 0.45f), leader = col(0.40f, 0.55f, 0.63f), label = col(0.93f, 0.95f, 0.97f),
          dim = col(0.55f, 0.63f, 0.69f), accent = col(0.35f, 0.80f, 1.0f), fill = col(0.10f, 0.47f, 0.85f), warn = col(1.0f, 0.62f, 0.20f), white = col(1, 1, 1);
};
ImU32 alpha(ImU32 c, float a) { return (c & 0x00FFFFFFu) | (uint32_t)(std::clamp(a, 0.0f, 1.0f) * ((c >> 24) & 0xFF)) << 24; }

// ---- geometry, in units of the controller's width (y down): controls_ui.mm controller_def
struct UPt { float x, y; };
struct URect { float x, y, w, h; };
struct ControllerDef {
    bool pro;
    float minY, maxY;
    UPt lstick, rstick, dpad, face, plus, minus, home;
    float stickR, faceSpread, faceR, smallR, dpadLen, dpadW;
    URect l, zl, screen;
};
ControllerDef controller_def(bool pro) {
    ControllerDef d{};
    d.pro = pro;
    if (!pro) {  // GamePad: a wide tablet, screen in the middle
        d.minY = -0.085f; d.maxY = 0.5f;
        d.lstick = {0.115f, 0.15f}; d.rstick = {0.885f, 0.15f};
        d.dpad = {0.115f, 0.335f}; d.face = {0.885f, 0.315f};
        d.minus = {0.85f, 0.44f}; d.plus = {0.92f, 0.44f}; d.home = {0.5f, 0.44f};
        d.stickR = 0.058f; d.faceSpread = 0.045f; d.faceR = 0.021f; d.smallR = 0.016f;
        d.dpadLen = 0.052f; d.dpadW = 0.034f;
        d.l = {0.06f, -0.04f, 0.15f, 0.07f};
        d.zl = {0.045f, -0.08f, 0.135f, 0.07f};
        d.screen = {0.255f, 0.07f, 0.49f, 0.28f};
    } else {  // Pro Controller: two grips
        d.minY = -0.05f; d.maxY = 0.63f;
        d.lstick = {0.2f, 0.2f}; d.rstick = {0.67f, 0.385f};
        d.dpad = {0.33f, 0.385f}; d.face = {0.8f, 0.2f};
        d.minus = {0.415f, 0.2f}; d.plus = {0.585f, 0.2f}; d.home = {0.5f, 0.265f};
        d.stickR = 0.068f; d.faceSpread = 0.052f; d.faceR = 0.025f; d.smallR = 0.019f;
        d.dpadLen = 0.056f; d.dpadW = 0.036f;
        d.l = {0.09f, -0.005f, 0.2f, 0.07f};
        d.zl = {0.12f, -0.045f, 0.16f, 0.07f};
    }
    return d;
}

enum PartKind { kPartRound, kPartShoulder, kPartDpad, kPartStick };
struct Part {
    int kind, action;
    ImVec2 a, b;           // rect (round: bounding box)
    ImVec2 va, vb;         // shoulder: visible strip
    std::string text;
    int dirs[4] = {-1, -1, -1, -1};
    ImVec2 c;
    float R = 0, capR = 0;
    int stick = 0;
};
struct Group {
    std::string title;
    std::vector<int> acts;
    std::vector<std::string> labels;
    ImVec2 anchor;
    bool right = false;
    bool leader = true;            // a line to the anchor (app actions have no part on the controller)
    ImVec2 a, b;                   // box
    std::vector<float> rows;       // row top y, one per action
};

// sizes of the callout columns (points at scale 1), as controls_ui.mm
const float kColW = 254, kRowH = 23, kMargin = 14, kHeaderH = 22;
const float kChipX[3] = {50, 110, 170}, kChipW[3] = {56, 56, 78};

struct Geo {
    float k = 1, s = 1;            // k: callout scale; s: controller width in points
    ImVec2 o;                      // controller origin
    ImVec2 org;                    // view origin (screen)
    float colLx = 0, colRx = 0;
    std::vector<ImVec2> body;      // Pro outline (GamePad: rounded rectangle)
    ImVec2 bodyA, bodyB;
    bool hasScreen = false;
    ImVec2 screenA, screenB;
    std::vector<ImVec2> leds;
    std::vector<Part> parts;
    std::vector<Group> groups;
};

// the Pro Controller's outline (controls_ui.mm pro_body), flattened
void pro_body(std::vector<ImVec2>& out, const std::function<ImVec2(float, float)>& P) {
    struct Seg { UPt c1, c2, p; };
    UPt start = {0.14f, 0.035f};
    const Seg segs[] = {
        {{0.14f, 0.035f}, {0.86f, 0.035f}, {0.86f, 0.035f}},  // straight top edge
        {{0.96f, 0.035f}, {1.0f, 0.11f}, {1.0f, 0.2f}},
        {{1.0f, 0.42f}, {0.96f, 0.625f}, {0.86f, 0.625f}},
        {{0.78f, 0.625f}, {0.745f, 0.545f}, {0.70f, 0.52f}},
        {{0.6f, 0.475f}, {0.4f, 0.475f}, {0.30f, 0.52f}},
        {{0.255f, 0.545f}, {0.22f, 0.625f}, {0.14f, 0.625f}},
        {{0.04f, 0.625f}, {0.0f, 0.42f}, {0.0f, 0.2f}},
        {{0.0f, 0.11f}, {0.04f, 0.035f}, {0.14f, 0.035f}},
    };
    UPt cur = start;
    out.push_back(P(cur.x, cur.y));
    for (const Seg& sg : segs) {
        const int n = 16;
        for (int i = 1; i <= n; i++) {
            float t = (float)i / n, u = 1 - t;
            float x = u * u * u * cur.x + 3 * u * u * t * sg.c1.x + 3 * u * t * t * sg.c2.x + t * t * t * sg.p.x;
            float y = u * u * u * cur.y + 3 * u * u * t * sg.c1.y + 3 * u * t * t * sg.c2.y + t * t * t * sg.p.y;
            out.push_back(P(x, y));
        }
        cur = sg.p;
    }
    out.pop_back();  // closed
}

Geo layout(bool pro, ImVec2 org, float w, float h) {
    Geo g;
    g.org = org;
    // the callouts need about 1060 x 520 points; smaller views shrink them, larger ones grow a little
    g.k = std::clamp(std::min(w / 1060.0f, h / 520.0f), 0.55f, 1.3f);
    const float k = g.k;
    ControllerDef d = controller_def(pro);
    g.colLx = org.x + kMargin * k;
    g.colRx = org.x + w - (kMargin + kColW) * k;
    float drawL = g.colLx + (kColW + 30) * k, drawR = g.colRx - 30 * k;
    float top = org.y + (kHeaderH + 10) * k, bottom = org.y + h - 12 * k;
    float aw = std::max(drawR - drawL, 100.0f), ah = std::max(bottom - top, 100.0f), uh = d.maxY - d.minY;
    g.s = std::min({aw, ah / uh, 640.0f * k});
    g.o = ImVec2((drawL + drawR) / 2 - g.s / 2, (top + bottom) / 2 - g.s * uh / 2 - g.s * d.minY);
    const float s = g.s;
    const ImVec2 o = g.o;
    auto P = [=](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };
    auto PU = [=](UPt u) { return P(u.x, u.y); };
    auto mirror = [](URect r) { return URect{1 - r.x - r.w, r.y, r.w, r.h}; };

    if (pro) {
        pro_body(g.body, P);
        for (int i = 0; i < 4; i++) g.leds.push_back(P(0.455f + i * 0.03f, 0.44f));
    } else {
        g.bodyA = P(0, 0);
        g.bodyB = P(1, 0.5f);
        g.hasScreen = true;
        g.screenA = P(d.screen.x, d.screen.y);
        g.screenB = P(d.screen.x + d.screen.w, d.screen.y + d.screen.h);
    }
    auto button = [&](int a, UPt c, float r, const char* text) {
        Part p;
        p.kind = kPartRound;
        p.action = a;
        p.c = PU(c);
        p.R = r * s;
        p.a = ImVec2(p.c.x - p.R, p.c.y - p.R);
        p.b = ImVec2(p.c.x + p.R, p.c.y + p.R);
        p.text = text;
        g.parts.push_back(p);
    };
    button(kX, {d.face.x, d.face.y - d.faceSpread}, d.faceR, "X");
    button(kA, {d.face.x + d.faceSpread, d.face.y}, d.faceR, "A");
    button(kB, {d.face.x, d.face.y + d.faceSpread}, d.faceR, "B");
    button(kY, {d.face.x - d.faceSpread, d.face.y}, d.faceR, "Y");
    button(kPlus, d.plus, d.smallR, "+");
    button(kMinus, d.minus, d.smallR, "\xE2\x88\x92");  // −
    button(kHome, d.home, d.smallR, "\xE2\x8C\x82");    // ⌂
    {
        ImVec2 c = PU(d.dpad);
        float L = d.dpadLen * s, W = d.dpadW * s;
        ImVec2 arms[4][2] = {{{c.x - W / 2, c.y - L}, {c.x + W / 2, c.y - W / 2}},
                             {{c.x - W / 2, c.y + W / 2}, {c.x + W / 2, c.y + L}},
                             {{c.x - L, c.y - W / 2}, {c.x - W / 2, c.y + W / 2}},
                             {{c.x + W / 2, c.y - W / 2}, {c.x + L, c.y + W / 2}}};
        int acts[4] = {kDUp, kDDown, kDLeft, kDRight};
        for (int i = 0; i < 4; i++) {
            Part p;
            p.kind = kPartDpad;
            p.action = acts[i];
            p.a = arms[i][0];
            p.b = arms[i][1];
            p.c = c;
            p.dirs[0] = i;
            p.R = W;
            g.parts.push_back(p);
        }
    }
    for (int i = 0; i < 2; i++) {
        Part p;
        p.kind = kPartStick;
        p.stick = i;
        p.c = PU(i ? d.rstick : d.lstick);
        p.R = d.stickR * s;
        p.capR = p.R * 0.62f;
        p.a = ImVec2(p.c.x - p.R, p.c.y - p.R);
        p.b = ImVec2(p.c.x + p.R, p.c.y + p.R);
        p.action = i ? kStickRClick : kStickLClick;
        int base = i ? kRUp : kLUp;
        for (int q = 0; q < 4; q++) p.dirs[q] = base + q;
        p.text = i ? "R" : "L";
        g.parts.push_back(p);
    }
    const float bodyTop = pro ? 0.035f : 0;
    auto shoulder = [&](int a, URect r, float coveredFrom, const char* t) {
        Part p;
        p.kind = kPartShoulder;
        p.action = a;
        p.a = P(r.x, r.y);
        p.b = P(r.x + r.w, r.y + r.h);
        p.va = p.a;
        p.vb = P(r.x + r.w, coveredFrom);
        p.c = ImVec2((p.va.x + p.vb.x) / 2, (p.va.y + p.vb.y) / 2);
        p.text = t;
        g.parts.push_back(p);
    };
    shoulder(kL, d.l, bodyTop, "L");
    shoulder(kR, mirror(d.l), bodyTop, "R");
    shoulder(kZL, d.zl, d.l.y, "ZL");
    shoulder(kZR, mirror(d.zl), d.l.y, "ZR");
    auto tabAnchor = [&](int a) {
        for (auto& p : g.parts)
            if (p.kind == kPartShoulder && p.action == a)
                return ImVec2(a == kL || a == kZL ? p.va.x + 3 : p.vb.x - 3, (p.va.y + p.vb.y) / 2);
        return ImVec2();
    };
    auto group = [&](const char* title, std::vector<int> acts, std::vector<std::string> labels, ImVec2 anchor, bool right) {
        Group gr;
        gr.title = title;
        gr.acts = std::move(acts);
        gr.labels = std::move(labels);
        gr.anchor = anchor;
        gr.right = right;
        g.groups.push_back(gr);
    };
    group("ZL", {kZL}, {"ZL"}, tabAnchor(kZL), false);
    group("ZR", {kZR}, {"ZR"}, tabAnchor(kZR), true);
    group("L", {kL}, {"L"}, tabAnchor(kL), false);
    group("R", {kR}, {"R"}, tabAnchor(kR), true);
    const std::vector<std::string> dirs = {"\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x90", "\xE2\x86\x92"};  // ↑↓←→
    std::vector<std::string> stickRows = dirs;
    stickRows.push_back("Click");
    group("Left stick (move)", {kLUp, kLDown, kLLeft, kLRight, kStickLClick}, stickRows, PU(d.lstick), false);
    group("Right stick (camera)", {kRUp, kRDown, kRLeft, kRRight, kStickRClick}, stickRows, PU(d.rstick), true);
    group("D-pad", {kDUp, kDDown, kDLeft, kDRight}, dirs, PU(d.dpad), false);
    // app actions: the bottom of the left column, no leader line ("Photo": the row label column is narrow;
    // hovering shows "Screenshot (app)")
    group("Photo", {kScreenshot}, {"Photo"}, ImVec2(org.x, org.y + h), false);
    g.groups.back().leader = false;
    for (auto& p : g.parts)
        if (p.kind == kPartRound) {
            bool right = p.action == kPlus || p.action == kA || p.action == kB || p.action == kX || p.action == kY ||
                         (p.action == kMinus && !pro) || (p.action == kHome && pro);
            std::string t = p.action == kHome ? "Home" : p.text;
            group(t.c_str(), {p.action}, {t}, p.c, right);
        }
    // stack each column in anchor order, as close to the anchors as fits (controls_ui.mm)
    for (int side = 0; side < 2; side++) {
        std::vector<Group*> colg;
        for (auto& gr : g.groups)
            if (gr.right == (side == 1)) colg.push_back(&gr);
        std::stable_sort(colg.begin(), colg.end(), [](Group* a, Group* b) {
            if (a->anchor.y != b->anchor.y) return a->anchor.y < b->anchor.y;
            return a->right ? a->anchor.x > b->anchor.x : a->anchor.x < b->anchor.x;
        });
        const float gap = 7 * k, minTop = org.y + (kHeaderH + 6) * k, maxBottom = org.y + h - 8 * k, rowH = kRowH * k;
        std::vector<float> y(colg.size()), hh(colg.size());
        for (size_t i = 0; i < colg.size(); i++) {
            int n = (int)colg[i]->acts.size();
            hh[i] = rowH * (n == 1 ? 1 : n + 1) + 4 * k;
            y[i] = std::max(colg[i]->anchor.y - rowH / 2 - 2 * k, i ? y[i - 1] + hh[i - 1] + gap : minTop);
        }
        for (size_t i = colg.size(); i-- > 0;) {
            float limit = i + 1 < colg.size() ? y[i + 1] - gap : maxBottom;
            y[i] = std::min(y[i], limit - hh[i]);
        }
        for (size_t i = 0; i < colg.size(); i++) {
            y[i] = std::max(y[i], i ? y[i - 1] + hh[i - 1] + gap : minTop);
            Group& gr = *colg[i];
            float x = side ? g.colRx : g.colLx;
            gr.a = ImVec2(x, y[i]);
            gr.b = ImVec2(x + kColW * k, y[i] + hh[i]);
            int n = (int)gr.acts.size();
            float ry = y[i] + 2 * k + (n == 1 ? 0 : rowH);
            for (int q = 0; q < n; q++, ry += rowH) gr.rows.push_back(ry);
        }
    }
    return g;
}

// ---- drawing helpers
ImFont* font() { return ImGui::GetFont(); }
// text centred in [a, b], shrunk (down to 8 points) and then clipped to fit
void text_fit(ImDrawList* dl, const std::string& s, ImVec2 a, ImVec2 b, float size, ImU32 c) {
    const float w = b.x - a.x;
    ImVec2 ts = font()->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    while (ts.x > w && size > 8) {
        size -= 0.5f;
        ts = font()->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    }
    ImVec4 clip(a.x, a.y - size, b.x, b.y + size);
    dl->AddText(font(), size, ImVec2(std::max(a.x, (a.x + b.x - ts.x) / 2), (a.y + b.y - ts.y) / 2), c, s.c_str(), nullptr, 0, &clip);
}
void text_centered(ImDrawList* dl, const std::string& s, ImVec2 c, float size, ImU32 col) {
    ImVec2 ts = font()->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    dl->AddText(font(), size, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), col, s.c_str());
}
void triangle(ImDrawList* dl, ImVec2 c, float ang, float sz, ImU32 col) {
    dl->AddTriangleFilled(ImVec2(c.x + std::cos(ang) * sz, c.y + std::sin(ang) * sz),
                          ImVec2(c.x + std::cos(ang + 2.3f) * sz, c.y + std::sin(ang + 2.3f) * sz),
                          ImVec2(c.x + std::cos(ang - 2.3f) * sz, c.y + std::sin(ang - 2.3f) * sz), col);
}
void dashed_rect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    const float dash = 3, gap = 2;
    for (float x = a.x + 2; x < b.x - 2; x += dash + gap) {
        float x1 = std::min(x + dash, b.x - 2);
        dl->AddLine(ImVec2(x, a.y), ImVec2(x1, a.y), col);
        dl->AddLine(ImVec2(x, b.y), ImVec2(x1, b.y), col);
    }
    for (float y = a.y + 2; y < b.y - 2; y += dash + gap) {
        float y1 = std::min(y + dash, b.y - 2);
        dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, y1), col);
        dl->AddLine(ImVec2(b.x, y), ImVec2(b.x, y1), col);
    }
}
bool in_rect(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y; }
bool in_circle(ImVec2 p, ImVec2 c, float r) { return std::hypot(p.x - c.x, p.y - c.y) <= r; }

std::string binding_summary(const Mapping& m, int a) {
    std::string out;
    for (int key : m.keys[a])
        if (key != kNoKey) out += (out.empty() ? "" : "  -  ") + key_label(key);
    if (m.pad[a] != kPadNone) out += (out.empty() ? "controller " : "  -  controller ") + std::string(pad_label(m.pad[a]));
    return out.empty() ? "not bound" : out;
}

}  // namespace

void draw_controls(ControlsView& v, float w, float h) {
    const Palette P;
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const Geo g = layout(v.pro, org, w, h);
    const float k = g.k, s = g.s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Mapping& m = v.m;
    const float pulse = 0.55f + 0.45f * std::sin((float)v.t * 7);
    auto lit = [&](int a) { return a >= 0 && v.act[a] > 0.5f; };

    // ---- hit test the drawing (mouse) and lay out the chips as buttons (mouse, keyboard, controller)
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool mouseIn = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && in_rect(mouse, org, ImVec2(org.x + w, org.y + h));
    int partHover = -1;
    if (mouseIn)
        for (auto& p : g.parts) {
            bool hit = false;
            switch (p.kind) {
            case kPartRound: hit = in_circle(mouse, p.c, p.R); break;
            case kPartDpad: hit = in_rect(mouse, p.a, p.b); break;
            case kPartShoulder: hit = in_rect(mouse, p.va, p.vb); break;
            case kPartStick:
                if (in_circle(mouse, p.c, p.capR)) hit = true;
                else if (in_circle(mouse, p.c, p.R)) {  // the well: the direction it points to
                    float dx = mouse.x - p.c.x, dy = mouse.y - p.c.y;
                    int q = std::fabs(dx) > std::fabs(dy) ? (dx < 0 ? 2 : 3) : (dy < 0 ? 0 : 1);
                    if (partHover < 0) partHover = p.dirs[q];
                }
                break;
            }
            if (hit && partHover < 0) partHover = p.action;
        }
    // a click on the drawing binds the input clicked: a key or a controller input
    const ImVec2 cursor0 = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(org);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##drawing", ImVec2(w, h)) && partHover >= 0 && v.cap_action < 0) v.start_action = partHover, v.start_col = kColAny;
    ImGui::PopItemFlag();
    // chips: invisible buttons in callout order (left column, then right), so navigation reaches all of them
    struct ChipItem { int action, col; ImVec2 a, b; bool hovered, focused; };
    std::vector<ChipItem> chips;
    for (int side = 0; side < 2; side++)
        for (auto& gr : g.groups) {
            if (gr.right != (side == 1)) continue;
            for (size_t q = 0; q < gr.acts.size(); q++)
                for (int c = 0; c < 3; c++) {
                    ChipItem ch;
                    ch.action = gr.acts[q];
                    ch.col = c;
                    ch.a = ImVec2(gr.a.x + kChipX[c] * k, gr.rows[q] + 2.5f * k);
                    ch.b = ImVec2(ch.a.x + kChipW[c] * k, gr.rows[q] + (kRowH - 2.5f) * k);
                    ImGui::SetCursorScreenPos(ch.a);
                    ImGui::PushID(ch.action * 4 + c);
                    bool pressed = ImGui::InvisibleButton("##chip", ImVec2(ch.b.x - ch.a.x, ch.b.y - ch.a.y),
                                                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_EnableNav);
                    ch.focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
                    ch.hovered = ImGui::IsItemHovered() || ch.focused;
                    if (pressed && v.cap_action < 0) {
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) v.clear_action = ch.action, v.clear_col = c;
                        else v.start_action = ch.action, v.start_col = c;
                    }
                    if (ImGui::IsItemFocused() && v.cap_action < 0 &&
                        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)))
                        v.clear_action = ch.action, v.clear_col = c;
                    if (ch.hovered) v.hover_action = ch.action, v.hover_col = c;
                    ImGui::PopID();
                    chips.push_back(ch);
                }
        }
    // (the drawing's button comes first: the chips on top of it take the mouse)
    if (v.hover_action < 0 && partHover >= 0) v.hover_action = partHover;
    ImGui::SetCursorScreenPos(cursor0);
    const int hover = v.hover_action;
    auto hot = [&](int a) { return a >= 0 && (a == hover || a == v.cap_action); };
    auto ring = [&](int a, float* wd) -> ImU32 {
        if (a == v.cap_action) { *wd = 2.5f; return alpha(P.accent, pulse); }
        if (a == hover) { *wd = 2.2f; return P.accent; }
        if (has_conflict(m, a)) { *wd = 2; return P.warn; }
        *wd = 1.2f;
        return P.outline;
    };

    // ---- column headers
    const char* heads[3] = {"Key", "Alt key", "Controller"};
    for (float x : {g.colLx, g.colRx})
        for (int c = 0; c < 3; c++)
            text_centered(dl, heads[c], ImVec2(x + (kChipX[c] + kChipW[c] / 2) * k, org.y + (kHeaderH / 2 + 2) * k), 13 * k, P.dim);

    // ---- shoulder tabs (behind the body; ZL/ZR behind L/R)
    for (int pass = 0; pass < 2; pass++)
        for (auto& p : g.parts) {
            if (p.kind != kPartShoulder) continue;
            bool z = p.action == kZL || p.action == kZR;
            if (z != (pass == 0)) continue;
            float wd;
            ImU32 rc = ring(p.action, &wd);
            dl->AddRectFilled(p.a, p.b, lit(p.action) ? P.fill : z ? P.well : P.button, 0.02f * s);
            dl->AddRect(p.a, p.b, rc, 0.02f * s, wd);
        }
    // ---- body (a soft shadow, then the shape)
    if (v.pro) {
        std::vector<ImVec2> sh = g.body;
        for (auto& q : sh) q.y += 4 * k;
        dl->AddConcavePolyFilled(sh.data(), (int)sh.size(), col(0, 0, 0, 0.30f));
        dl->AddConcavePolyFilled(g.body.data(), (int)g.body.size(), P.body);
        dl->AddPolyline(g.body.data(), (int)g.body.size(), P.outline, 1.5f, ImDrawFlags_Closed);
    } else {
        dl->AddRectFilled(ImVec2(g.bodyA.x, g.bodyA.y + 4 * k), ImVec2(g.bodyB.x, g.bodyB.y + 4 * k), col(0, 0, 0, 0.30f), 0.07f * s);
        dl->AddRectFilled(g.bodyA, g.bodyB, P.body, 0.07f * s);
        dl->AddRect(g.bodyA, g.bodyB, P.outline, 0.07f * s, 1.5f);
    }
    for (auto& p : g.parts)
        if (p.kind == kPartShoulder)
            text_fit(dl, p.text, p.va, p.vb, std::max(11.0f, (p.vb.y - p.va.y) * 0.62f), lit(p.action) ? P.white : P.buttonText);

    // ---- GamePad screen: the hovered / captured input
    if (g.hasScreen) {
        dl->AddRectFilled(g.screenA, g.screenB, P.screen, 0.01f * s);
        dl->AddRect(g.screenA, g.screenB, P.outline, 0.01f * s, 1);
        const int a = v.cap_action >= 0 ? v.cap_action : hover;
        const float x0 = g.screenA.x + 10, x1 = g.screenB.x - 10, cy = (g.screenA.y + g.screenB.y) / 2;
        if (a >= 0) {
            text_fit(dl, action_label(a), ImVec2(x0, cy - 0.06f * s), ImVec2(x1, cy - 0.01f * s), 0.036f * s, P.screenText);
            std::string sub = v.cap_action >= 0 ? (v.cap_col == kColPad   ? "Press a controller button..."
                                                   : v.cap_col == kColAny ? "Press a key or controller button..."
                                                                          : "Press a key...")
                                                : binding_summary(m, a);
            text_fit(dl, sub, ImVec2(x0, cy + 0.005f * s), ImVec2(x1, cy + 0.045f * s), 0.024f * s,
                     v.cap_action >= 0 ? alpha(P.accent, pulse) : alpha(P.screenText, 0.75f));
        } else {
            text_fit(dl, "Select a button to rebind it", ImVec2(x0, cy - 0.02f * s), ImVec2(x1, cy + 0.02f * s), 0.022f * s,
                     alpha(P.screenText, 0.45f));
        }
    }
    bool padConnected = false;
    if (v.pad)
        for (int q = 1; q < kPadCount; q++) padConnected |= v.pad[q] > 0.05f;
    for (ImVec2 c : g.leds) dl->AddCircleFilled(c, 0.006f * s, padConnected ? P.accent : P.well);

    // ---- leader lines (under the buttons)
    for (auto& gr : g.groups) {
        if (!gr.leader) continue;
        bool h1 = false, warn = false;
        for (int a : gr.acts) {
            h1 |= hot(a);
            warn |= has_conflict(m, a);
        }
        float y = gr.a.y + (2 + kRowH / 2) * k;
        float x0 = gr.right ? gr.a.x : gr.b.x;
        float x1 = gr.right ? g.colRx - 16 * k : g.colLx + (kColW + 16) * k;
        ImVec2 pts[3] = {ImVec2(x0, y), ImVec2(x1, y), gr.anchor};
        dl->AddPolyline(pts, 3, h1 ? P.accent : warn ? alpha(P.warn, 0.7f) : P.leader, h1 ? 2.0f : 1.0f);
        dl->AddCircleFilled(gr.anchor, 2.5f, h1 ? P.accent : P.leader);
    }

    // ---- buttons and sticks
    const float bf = std::max(11.0f, 0.024f * s);
    for (auto& p : g.parts) {
        float wd;
        if (p.kind == kPartRound) {
            dl->AddCircleFilled(p.c, p.R, lit(p.action) ? P.fill : P.button);
            dl->AddCircle(p.c, p.R, ring(p.action, &wd), 0, wd);
            bool small = p.action == kHome || p.action == kPlus || p.action == kMinus;
            text_centered(dl, p.text, p.c, small ? std::max(10.0f, 0.021f * s) : bf, lit(p.action) ? P.white : P.buttonText);
        } else if (p.kind == kPartStick) {
            dl->AddCircleFilled(p.c, p.R, P.well);
            bool dirHot = false, conflict = false;
            for (int q = 0; q < 4; q++) {
                dirHot |= hot(p.dirs[q]);
                conflict |= has_conflict(m, p.dirs[q]);
            }
            dl->AddCircle(p.c, p.R, dirHot ? P.accent : conflict ? P.warn : P.outline, 0, dirHot || conflict ? 2.0f : 1.2f);
            const float ang[4] = {-(float)M_PI_2, (float)M_PI_2, (float)M_PI, 0};
            for (int q = 0; q < 4; q++) {
                int a = p.dirs[q];
                float r0 = p.R * 0.83f;
                ImU32 c = lit(a) ? P.accent : a == v.cap_action ? alpha(P.accent, pulse) : a == hover ? P.accent
                          : has_conflict(m, a) ? P.warn : P.outline;
                triangle(dl, ImVec2(p.c.x + std::cos(ang[q]) * r0, p.c.y + std::sin(ang[q]) * r0), ang[q], p.R * 0.14f, c);
            }
            // cap, moved by the stick's deflection
            float dx = v.stick[p.stick][0], dy = -v.stick[p.stick][1];
            float len = std::hypot(dx, dy);
            if (len > 1) dx /= len, dy /= len;
            float travel = p.R - p.capR;
            ImVec2 cc(p.c.x + dx * travel, p.c.y + dy * travel);
            if (len > 0.02f) dl->AddLine(p.c, cc, P.accent, 2);
            dl->AddCircleFilled(cc, p.capR, lit(p.action) ? P.fill : P.button);
            dl->AddCircle(cc, p.capR, ring(p.action, &wd), 0, wd);
            dl->AddCircle(cc, p.capR * 0.7f, alpha(P.outline, 0.45f), 0, 1);
            dl->AddCircleFilled(cc, std::max(2.5f, p.capR * 0.16f), len > 0.02f ? P.accent : alpha(P.outline, 0.8f));
            text_centered(dl, p.text, ImVec2(cc.x, cc.y - p.capR * 0.42f), std::max(10.0f, p.capR * 0.34f),
                          lit(p.action) ? P.white : P.buttonText);
        }
    }
    // d-pad cross
    {
        float W = 0;
        ImVec2 c;
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) W = p.R, c = p.c;
        const float rr = W * 0.18f;
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) dl->AddRectFilled(p.a, p.b, P.button, rr);
        dl->AddRectFilled(ImVec2(c.x - W / 2 - 1, c.y - W / 2 - 1), ImVec2(c.x + W / 2 + 1, c.y + W / 2 + 1), P.button);
        for (auto& p : g.parts)
            if (p.kind == kPartDpad) {
                float wd;
                ImU32 rc = ring(p.action, &wd);
                if (lit(p.action)) dl->AddRectFilled(p.a, p.b, P.fill, rr);
                dl->AddRect(p.a, p.b, rc, rr, rc == P.outline ? 1.2f : wd);
                const float ang[4] = {-(float)M_PI_2, (float)M_PI_2, (float)M_PI, 0};
                triangle(dl, ImVec2((p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2), ang[p.dirs[0]], W * 0.22f,
                         lit(p.action) ? P.white : P.outline);
            }
        // cover the inner seams of the arms' outlines
        dl->AddRectFilled(ImVec2(c.x - W / 2 + 0.6f, c.y - W / 2 - 1.5f), ImVec2(c.x + W / 2 - 0.6f, c.y + W / 2 + 1.5f), P.button);
        dl->AddRectFilled(ImVec2(c.x - W / 2 - 1.5f, c.y - W / 2 + 0.6f), ImVec2(c.x + W / 2 + 1.5f, c.y + W / 2 - 0.6f), P.button);
    }

    // ---- callouts
    int hoverCode = -1;
    bool hoverPad = false;
    if (v.hover_action >= 0 && v.hover_col >= 0) {
        hoverPad = v.hover_col == kColPad;
        hoverCode = hoverPad ? m.pad[v.hover_action] : m.keys[v.hover_action][v.hover_col];
    }
    for (auto& gr : g.groups) {
        bool h1 = false;
        for (int a : gr.acts) h1 |= hot(a);
        dl->AddRectFilled(gr.a, gr.b, P.box, 7 * k);
        dl->AddRect(gr.a, gr.b, h1 ? P.accent : P.boxEdge, 7 * k, h1 ? 1.6f : 1.0f);
        const bool multi = gr.acts.size() > 1;
        if (multi)
            text_fit(dl, gr.title, ImVec2(gr.a.x + 8 * k, gr.a.y + 2 * k), ImVec2(gr.b.x - 8 * k, gr.a.y + (2 + kRowH) * k), 14 * k, P.label);
        for (size_t q = 0; q < gr.acts.size(); q++) {
            int a = gr.acts[q];
            float ry = gr.rows[q];
            if (a == hover || a == v.cap_action)
                dl->AddRectFilled(ImVec2(gr.a.x + 3 * k, ry + 0.5f), ImVec2(gr.b.x - 3 * k, ry + kRowH * k - 0.5f), alpha(P.accent, 0.14f), 5 * k);
            ImVec2 la(gr.a.x + 6 * k, ry), lb(gr.a.x + (kChipX[0] - 4) * k, ry + kRowH * k);
            if (lit(a)) dl->AddRectFilled(ImVec2(la.x + 1, la.y + 3 * k), ImVec2(lb.x - 1, lb.y - 3 * k), P.fill, 5 * k);
            text_fit(dl, gr.labels[q], la, lb, (multi ? 14.5f : 15.0f) * k, lit(a) ? P.white : P.label);
        }
    }
    for (const ChipItem& ch : chips) {
        const int a = ch.action, c = ch.col;
        const bool isPad = c == kColPad;
        const int code = isPad ? m.pad[a] : m.keys[a][c];
        const bool bound = isPad ? code != kPadNone : code != kNoKey;
        const bool capturing = v.cap_action == a && (v.cap_col == kColAny ? c != kColKey1 : v.cap_col == c);
        const bool held = bound && (isPad ? v.pad && v.pad[code] > 0.5f : v.keys && v.keys[code]);
        const bool conflict = bound && !(isPad ? pad_users(m, code, a) : key_users(m, code, a)).empty();
        const bool hovered = ch.hovered;
        const bool twin = bound && !hovered && hoverCode >= 0 && hoverPad == isPad && hoverCode == code;
        const float rad = isPad ? (ch.b.y - ch.a.y) / 2 : 4 * k;
        const float fs = 13.5f * k;
        if (ch.focused)  // keyboard / controller cursor, as ImGui's own (gold)
            dl->AddRect(ImVec2(ch.a.x - 3, ch.a.y - 3), ImVec2(ch.b.x + 3, ch.b.y + 3), ImGui::GetColorU32(ImGuiCol_NavCursor), rad + 3, 2.0f);
        if (!bound && !capturing) {
            dashed_rect(dl, ch.a, ch.b, hovered ? P.accent : P.boxEdge);
            text_fit(dl, hovered ? "+ add" : "\xE2\x80\x94", ch.a, ch.b, fs, hovered ? P.accent : alpha(P.dim, 0.6f));
            continue;
        }
        if (!isPad && !held) dl->AddRectFilled(ImVec2(ch.a.x, ch.a.y + 1.5f), ImVec2(ch.b.x, ch.b.y + 1.5f), P.keyEdge, rad);
        dl->AddRectFilled(ch.a, ch.b, held ? P.fill : isPad ? P.padChip : P.keycap, rad);
        ImU32 stroke = capturing ? alpha(P.accent, pulse) : hovered || twin ? P.accent : conflict ? P.warn : isPad ? P.padEdge : P.keyEdge;
        dl->AddRect(ch.a, ch.b, stroke, rad, capturing || hovered || twin || conflict ? 1.8f : 1.0f);
        std::string text = capturing ? "press..." : isPad ? pad_short_label(code) : key_short_label(code);
        ImU32 tc = held ? P.white : capturing ? P.accent : conflict ? P.warn : P.label;
        text_fit(dl, text, ImVec2(ch.a.x + 3, ch.a.y), ImVec2(ch.b.x - 3, ch.b.y), fs, tc);
    }
    ImGui::SetCursorScreenPos(ImVec2(org.x, org.y + h));
    ImGui::Dummy(ImVec2(w, 0));
}

}  // namespace overlay
