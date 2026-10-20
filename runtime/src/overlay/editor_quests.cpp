// The world editor's quest editor (editor_panel.h, docs/mod-editor.md "Quests"): the quest as a graph
// to edit (nodes for Start, the steps and End; edges for what moves the quest on), the step and
// transition inspector, the compact list, the Flags and Search panels, and the quest step the loaded
// game is at. It reads the session through unlogged queries and changes everything through commands
// (editor/commands.h), so every edit is one undo step and tools can do the same. Render thread.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../editor/editor_game.h"
#include "../gfx/renderer.h"
#include "editor_panel.h"
#include "editor_shell.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1),
    kWarn(0.98f, 0.78f, 0.35f, 1);
constexpr float kNodeW = 190, kStepH = 112, kEndH = 70, kPin = 7, kColW = 270, kRowH = 150;

struct View {
    ImVec2 pan{30, 30};
    float zoom = 1;
    bool fit = true;  // fit the graph into the view at its next frame
    ImVec2 last_size;
    int last_count = -1;
    bool moved = false;  // panned or zoomed by hand: no refit when the window changes size
};
struct QuestUi {
    std::string quest;  // the shown quest
    std::string node;   // the selected node ("start", "step:N", "end")
    int edge = -1;      // the selected edge (index into the graph's edges)
    Value graph;        // quest_graph (refreshed a few times a second)
    Value state;        // quest_state
    int graph_at = -100, state_at = -100;
    View views[2];  // the editor's graph, the graph while playing
    std::map<std::string, ImVec2> pos;  // node places on the canvas this frame
    std::string dragging;               // a node being moved
    ImVec2 grab;                        // where in the node it was grabbed
    bool moved = false, panning = false;
    std::string connecting;             // dragging from this node's output
    std::string connect_from, connect_to;  // the connection the popup completes
    bool connect_open = false;          // that popup shows
    bool connect_cancel = false;        // Esc: close it
    std::string editing;                // a node's text edited in place
    char edit_buf[256] = {};
    bool edit_focus = false;
    bool list_view = false;
    bool ask_remove = false;            // the node menu asked to remove the step (the confirmation opens outside it)
    ImVec2 add_at;                      // where "Add step here" was asked (canvas units)
    std::string select_after;           // select this node once the graph has it
    char new_title[96] = {};
    bool while_playing = true;          // the graph stays over the game while playing
    std::string test_connect;           // test aid: a connection drag shown (WWHD_TEST_EDITOR_CONNECT=from>to@first-last frame)
    uint64_t test_from = 0, test_to = ~0ull;
    std::string want_quest, want_node;  // test aid: shown once the quest exists (WWHD_TEST_EDITOR_QUEST=quest[:node])
    char search[96] = {};
    Value hits;
    std::string hits_for;
    std::vector<std::string> multi;     // the selected actors (Ctrl or Shift adds)
    Value flags;                        // list_flags
    int flags_at = -100;
} Q;

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }
const Value* by_id(const Value& list, const std::string& id) {
    for (const auto& v : list.array)
        if (v.get("id").string() == id) return &v;
    return nullptr;
}
int step_of(const std::string& node) { return node.rfind("step:", 0) == 0 ? std::atoi(node.c_str() + 5) : 0; }
Value query(const std::string& command, const Value& params) {
    Value out;
    editor::try_with_session([&](editor::Session& s) { out = s.run(command, params, false); });
    return out;
}
Value params_quest() {
    Value p;
    p["quest"] = Q.quest;
    return p;
}
bool edit_line(const char* label, const std::string& current, std::string& out, size_t max = 240) {
    static std::map<ImGuiID, std::vector<char>> buffers;
    auto& b = buffers[ImGui::GetID(label)];
    b.resize(max + 1);
    if (ImGui::GetActiveID() != ImGui::GetID(label)) std::snprintf(b.data(), b.size(), "%s", current.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field(label, b.data(), b.size());
    out = b.data();
    return ImGui::IsItemDeactivatedAfterEdit();
}
void edit_step(int n, const char* field, const Value& value) {
    Value p = params_quest();
    p["step"] = n;
    p["fields"][field] = value;
    command("edit_quest_step", p);
}
void select_node(const std::string& node) {
    Q.node = node;
    Q.edge = -1;
}

// ---------------------------------------------------------------- refresh
void refresh_quest(const Value& m) {
    const int frame = ImGui::GetFrameCount();
    if (!Q.want_quest.empty() && by_id(m.get("quests"), Q.want_quest)) {
        Q.quest = Q.want_quest;
        Q.node = Q.want_node;
        Q.want_quest.clear();
        Q.graph_at = -100;
    }
    if (Q.quest.empty() || !by_id(m.get("quests"), Q.quest)) {
        Q.quest = m.get("quests").array.empty() ? std::string() : m.get("quests").array[0].get("id").string();
        Q.node.clear(), Q.edge = -1, Q.graph = Value();
        Q.graph_at = -100;
    }
    if (Q.graph_at < 0) Q.views[0].fit = Q.views[1].fit = true;  // another quest: fit it into the view
    if (!Q.quest.empty() && frame - Q.graph_at >= 8) {
        const Value r = query("quest_graph", params_quest());
        if (r.get("ok").boolean) {
            Q.graph = r.get("result");
            Q.graph_at = frame;
        } else if (r.get("ok").type == Value::Bool)
            Q.graph = Value(), Q.graph_at = frame;
        if (!Q.select_after.empty())
            for (const auto& n : Q.graph.get("nodes").array)
                if (n.get("id").string() == Q.select_after) select_node(Q.select_after), Q.select_after.clear();
    }
    if (Q.edge >= int(Q.graph.get("edges").array.size())) Q.edge = -1;
}
void refresh_state() {
    const int frame = ImGui::GetFrameCount();
    if (frame - Q.state_at < 15) return;
    const Value r = query("quest_state", Value());
    if (r.get("ok").type == Value::Bool) {
        Q.state = r.get("result");
        Q.state_at = frame;
    }
}

// ---------------------------------------------------------------- the graph's layout
// Default places: columns by the shortest way from Start, rows in step order; End after the last column.
std::map<std::string, ImVec2> default_layout(const Value& g) {
    std::map<std::string, int> depth{{"start", 0}};
    std::deque<std::string> todo{"start"};
    while (!todo.empty()) {
        const std::string at = todo.front();
        todo.pop_front();
        for (const auto& e : g.get("edges").array)
            if (e.get("from").string() == at && e.get("to").string() != "end" && !depth.contains(e.get("to").string())) {
                depth[e.get("to").string()] = depth[at] + 1;
                todo.push_back(e.get("to").string());
            }
    }
    int deepest = 0;
    for (const auto& [n, d] : depth) deepest = std::max(deepest, d);
    std::map<int, int> rows;
    std::map<std::string, ImVec2> out;
    int unreached = 0;
    for (const auto& n : g.get("nodes").array) {
        const std::string id = n.get("id").string();
        if (id == "end") continue;
        int col;
        if (depth.contains(id)) col = depth[id];
        else col = deepest + 1 + (unreached++ / 3);  // steps nothing leads to: to the right, three a column
        const int row = rows[col]++;
        out[id] = ImVec2(col * kColW, row * kRowH);
    }
    int last = 0;
    for (const auto& [col, n] : rows) last = std::max(last, col);
    out["end"] = ImVec2((last + 1) * kColW, 0);
    return out;
}
void layout_positions() {
    const auto defaults = default_layout(Q.graph);
    const Value& saved = Q.graph.get("layout");
    std::map<std::string, ImVec2> next;
    for (const auto& n : Q.graph.get("nodes").array) {
        const std::string id = n.get("id").string();
        if (id == Q.dragging && Q.pos.contains(id)) next[id] = Q.pos[id];  // being moved: the hand's place
        else if (saved.get(id).get("x").type == Value::Number) next[id] = ImVec2(num(saved.get(id), "x"), num(saved.get(id), "y"));
        else next[id] = defaults.contains(id) ? defaults.at(id) : ImVec2(0, 0);
    }
    Q.pos = std::move(next);
}
float node_h(const Value& n) { return n.get("kind").string() == "step" ? kStepH : kEndH; }

// ---------------------------------------------------------------- drawing helpers
ImU32 col(const ImVec4& c, float a = 1) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a)); }
ImU32 badge_colour(const std::string& kind) {
    if (kind == "talk") return IM_COL32(120, 170, 255, 255);
    if (kind == "reward" || kind == "give") return IM_COL32(110, 210, 130, 255);
    if (kind == "event" || kind == "time") return IM_COL32(240, 190, 90, 255);
    if (kind == "letter") return IM_COL32(220, 160, 240, 255);
    if (kind == "chart") return IM_COL32(100, 210, 220, 255);
    return IM_COL32(190, 195, 205, 255);
}
const char* badge_letter(const std::string& kind) {
    if (kind == "talk") return "T";
    if (kind == "reward" || kind == "give") return "R";
    if (kind == "event") return "E";
    if (kind == "time") return "t";
    if (kind == "letter") return "L";
    if (kind == "chart") return "C";
    if (kind == "sail") return "S";
    return "?";
}
ImU32 edge_colour(const std::string& via) {
    if (via == "letter") return IM_COL32(220, 160, 240, 230);
    if (via == "item") return IM_COL32(110, 210, 130, 230);
    if (via == "scene") return IM_COL32(100, 210, 220, 230);
    if (via == "talk") return IM_COL32(120, 170, 255, 230);
    return IM_COL32(225, 228, 235, 230);
}
// the graph's zoom: node sizes, text and pins scale with it (canvas units -> pixels)
float g_z = 1;
float Z(float v) { return v * g_z; }
ImVec2 tsize(const char* s) {
    const ImVec2 t = ImGui::CalcTextSize(s);
    return ImVec2(t.x * g_z, t.y * g_z);
}
float line_h() { return ImGui::GetTextLineHeight() * g_z; }
void dtext(ImDrawList* dl, const ImVec2& at, ImU32 colour, const char* text) { dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * g_z, at, colour, text); }
ImVec2 bezier(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, float t) {
    const float u = 1 - t;
    return ImVec2(u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                  u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y);
}
void curve(const ImVec2& p0, const ImVec2& p3, ImVec2& c1, ImVec2& c2) {
    const float dx = std::max(Z(70), std::fabs(p3.x - p0.x) * 0.5f);
    c1 = ImVec2(p0.x + dx, p0.y);
    c2 = ImVec2(p3.x - dx, p3.y);
}
std::string ellipsis(const std::string& s, float width) {
    if (tsize(s.c_str()).x <= width) return s;
    std::string t = s;
    while (!t.empty() && tsize((t + "...").c_str()).x > width) t.pop_back();
    return t + "...";
}
// a text wrapped into at most `lines` lines of `width`
std::vector<std::string> wrap(const std::string& s, float width, size_t lines) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        const std::string tryline = line.empty() ? word : line + " " + word;
        if (tsize(tryline.c_str()).x > width && !line.empty()) {
            out.push_back(line);
            line = word;
        } else
            line = tryline;
        word.clear();
    };
    for (char c : s) {
        if (c == ' ' || c == '\n') flush_word();
        else word += c;
    }
    flush_word();
    if (!line.empty()) out.push_back(line);
    if (out.size() > lines) {
        out.resize(lines);
        out.back() = ellipsis(out.back() + " ...", width);
    }
    return out;
}

// ---------------------------------------------------------------- the canvas
void connect_popup(const Value& m);
void node_menu(const Value& m);

void canvas(const Value& m, ImVec2 size, bool live_view) {
    View& V = Q.views[live_view ? 1 : 0];
    g_z = V.zoom;
    ImGui::BeginChild("quest_canvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin_screen = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 origin(origin_screen.x + V.pan.x, origin_screen.y + V.pan.y);
    // background grid
    dl->AddRectFilled(origin_screen, ImVec2(origin_screen.x + avail.x, origin_screen.y + avail.y), IM_COL32(22, 25, 31, 235));
    const float grid = std::max(8.0f, Z(40));
    for (float x = std::fmod(V.pan.x, grid); x < avail.x; x += grid) dl->AddLine(ImVec2(origin_screen.x + x, origin_screen.y), ImVec2(origin_screen.x + x, origin_screen.y + avail.y), IM_COL32(40, 44, 52, 255));
    for (float y = std::fmod(V.pan.y, grid); y < avail.y; y += grid) dl->AddLine(ImVec2(origin_screen.x, origin_screen.y + y), ImVec2(origin_screen.x + avail.x, origin_screen.y + y), IM_COL32(40, 44, 52, 255));
    // the whole canvas is one invisible button: clicks, drags and the context menu
    ImGui::SetNextItemAllowOverlap();  // the text edited in place goes over it
    ImGui::InvisibleButton("canvas", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;
    ImVec2 at_canvas((mouse.x - origin.x) / g_z, (mouse.y - origin.y) / g_z);

    layout_positions();
    // fit the graph into the view: on first show, for another quest, Fit, and while playing when it changes
    {
        const int count = int(Q.graph.get("nodes").array.size());
        if (live_view && (std::fabs(V.last_size.x - avail.x) > 1 || std::fabs(V.last_size.y - avail.y) > 1 || V.last_count != count)) V.fit = true;
        if (!live_view && !V.moved && (std::fabs(V.last_size.x - avail.x) > 8 || std::fabs(V.last_size.y - avail.y) > 8)) V.fit = true;
        // the editor's view: a node added or moved out of sight (by a command, a tool) brings the whole graph back
        if (!live_view && V.last_count != count && count && Q.dragging.empty())
            for (const auto& n : Q.graph.get("nodes").array) {
                const ImVec2 p = Q.pos[n.get("id").string()];
                const float x = V.pan.x + p.x * V.zoom, y = V.pan.y + p.y * V.zoom;
                if (x < 0 || y < 0 || x + kNodeW * V.zoom > avail.x || y + node_h(n) * V.zoom > avail.y) V.fit = true;
            }
        V.last_size = avail;
        V.last_count = count;
        if (V.fit && count) {
            float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
            for (const auto& n : Q.graph.get("nodes").array) {
                const ImVec2 p = Q.pos[n.get("id").string()];
                x0 = std::min(x0, p.x), y0 = std::min(y0, p.y - 20), x1 = std::max(x1, p.x + kNodeW), y1 = std::max(y1, p.y + node_h(n));
            }
            const float margin = 24;
            V.zoom = std::clamp(std::min((avail.x - 2 * margin) / std::max(1.0f, x1 - x0), (avail.y - 2 * margin) / std::max(1.0f, y1 - y0)), 0.35f, 1.15f);
            V.pan = ImVec2((avail.x - (x1 - x0) * V.zoom) / 2 - x0 * V.zoom, (avail.y - (y1 - y0) * V.zoom) / 2 - y0 * V.zoom);
            V.fit = false;
            V.moved = false;
            g_z = V.zoom;
            at_canvas = ImVec2((mouse.x - origin_screen.x - V.pan.x) / g_z, (mouse.y - origin_screen.y - V.pan.y) / g_z);
            origin = ImVec2(origin_screen.x + V.pan.x, origin_screen.y + V.pan.y);
        }
    }
    const auto& nodes = Q.graph.get("nodes").array;
    const auto& edges = Q.graph.get("edges").array;
    auto rect_of = [&](const Value& n, ImVec2& a, ImVec2& b) {
        const ImVec2 p = Q.pos[n.get("id").string()];
        a = ImVec2(origin.x + Z(p.x), origin.y + Z(p.y));
        b = ImVec2(a.x + Z(kNodeW), a.y + Z(node_h(n)));
    };
    auto in_pin = [&](const Value& n) {
        ImVec2 a, b;
        rect_of(n, a, b);
        return ImVec2(a.x, (a.y + b.y) / 2);
    };
    auto out_pin = [&](const Value& n) {
        ImVec2 a, b;
        rect_of(n, a, b);
        return ImVec2(b.x, (a.y + b.y) / 2);
    };
    auto node_by = [&](const std::string& id) -> const Value* {
        for (const auto& n : nodes)
            if (n.get("id").string() == id) return &n;
        return nullptr;
    };
    const int saved = int(num(Q.graph, "saved_step", -1));
    const bool live = Q.graph.get("saved_step").type == Value::Number;

    // what the mouse is over (topmost: the last drawn)
    std::string over_node, over_out;
    for (const auto& n : nodes) {
        ImVec2 a, b;
        rect_of(n, a, b);
        if (n.get("kind").string() != "end") {
            const ImVec2 o = out_pin(n);
            if ((mouse.x - o.x) * (mouse.x - o.x) + (mouse.y - o.y) * (mouse.y - o.y) <= (Z(kPin) + 4) * (Z(kPin) + 4)) over_out = n.get("id").string();
        }
        if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) over_node = n.get("id").string();
    }
    int over_edge = -1;
    if (over_node.empty() && over_out.empty() && hovered) {
        float best = 7 * 7;
        for (size_t i = 0; i < edges.size(); i++) {
            const Value *f = node_by(edges[i].get("from").string()), *t = node_by(edges[i].get("to").string());
            if (!f || !t) continue;
            const ImVec2 p0 = out_pin(*f), p3 = in_pin(*t);
            ImVec2 c1, c2;
            curve(p0, p3, c1, c2);
            for (int k = 0; k <= 24; k++) {
                const ImVec2 q = bezier(p0, c1, c2, p3, k / 24.0f);
                const float d = (q.x - mouse.x) * (q.x - mouse.x) + (q.y - mouse.y) * (q.y - mouse.y);
                if (d < best) best = d, over_edge = int(i);
            }
        }
    }

    // edges
    dl->PushClipRect(origin_screen, ImVec2(origin_screen.x + avail.x, origin_screen.y + avail.y), true);
    for (size_t i = 0; i < edges.size(); i++) {
        const auto& e = edges[i];
        const Value *f = node_by(e.get("from").string()), *t = node_by(e.get("to").string());
        if (!f || !t) continue;
        const ImVec2 p0 = out_pin(*f), p3 = in_pin(*t);
        ImVec2 c1, c2;
        curve(p0, p3, c1, c2);
        const bool sel = int(i) == Q.edge, hot = int(i) == over_edge;
        ImU32 c = edge_colour(e.get("via").get("kind").string());
        dl->AddBezierCubic(p0, c1, c2, p3, sel ? IM_COL32(255, 210, 90, 255) : c, sel ? 3.5f : hot ? 3.0f : 2.0f);
        // the arrow head at the input
        dl->AddTriangleFilled(ImVec2(p3.x - 2, p3.y), ImVec2(p3.x - Z(12), p3.y - Z(6)), ImVec2(p3.x - Z(12), p3.y + Z(6)), sel ? IM_COL32(255, 210, 90, 255) : c);
        // its label: what moves the quest on, with a mark for conditions and rewards
        std::string label = e.get("label").string();
        if (!e.get("conditions").array.empty()) label += "  [if]";
        if (!e.get("rewards").array.empty()) label += "  [+]";
        label = ellipsis(label, Z(200));
        const ImVec2 mid = bezier(p0, c1, c2, p3, 0.5f);
        const ImVec2 ts = tsize(label.c_str());
        const ImVec2 la(mid.x - ts.x / 2 - 4, mid.y - ts.y - 4), lb(mid.x + ts.x / 2 + 4, mid.y - 2);
        dl->AddRectFilled(la, lb, sel ? IM_COL32(110, 85, 25, 235) : IM_COL32(30, 34, 42, 225), 3);
        dtext(dl, ImVec2(la.x + 4, la.y + 1), sel ? IM_COL32(255, 235, 170, 255) : IM_COL32(215, 220, 230, 255), label.c_str());
        if (e.get("branch").boolean) dl->AddCircleFilled(p0, 4, IM_COL32(255, 210, 90, 255));  // a branch starts here
    }
    // nodes
    for (const auto& n : nodes) {
        const std::string id = n.get("id").string(), kind = n.get("kind").string();
        ImVec2 a, b;
        rect_of(n, a, b);
        const int stepn = int(num(n, "n"));
        const bool now = live && ((kind == "step" && stepn == saved) || (kind == "end" && saved == 255));  // the step the game is at (none before it starts)
        const bool done = live && ((kind == "step" && saved > 0 && (stepn < saved || saved == 255)) || (kind == "start" && saved > 0));
        const bool sel = id == Q.node;
        const ImU32 head = kind == "start" ? IM_COL32(46, 120, 70, 255) : kind == "end" ? IM_COL32(110, 70, 140, 255) : IM_COL32(48, 82, 130, 255);
        if (now) dl->AddRect(ImVec2(a.x - 5, a.y - 5), ImVec2(b.x + 5, b.y + 5), IM_COL32(255, 220, 90, 200), 9, 4.0f);  // the game is here
        dl->AddRectFilled(a, b, IM_COL32(34, 38, 47, 245), 6);
        dl->AddRectFilled(a, ImVec2(b.x, a.y + Z(22)), head, 6, ImDrawFlags_RoundCornersTop);
        dl->AddRect(a, b, sel ? IM_COL32(120, 205, 255, 255) : n.get("reachable").type == Value::Bool && !n.get("reachable").boolean ? IM_COL32(200, 90, 80, 255) : IM_COL32(85, 92, 108, 255), 6,
                    sel ? 2.5f : 1.2f);
        // title: Start / Step N  where / End
        std::string title = kind == "start" ? "Start" : kind == "end" ? "End: done" : "Step " + std::to_string(stepn);
        if (kind == "step" && n.get("where").type == Value::String) title += "  " + n.get("where").text;
        dtext(dl, ImVec2(a.x + Z(8), a.y + Z(3)), IM_COL32(240, 242, 248, 255), ellipsis(title, Z(kNodeW - (done ? 46 : 30))).c_str());
        if (done) dtext(dl, ImVec2(b.x - Z(40), a.y + Z(3)), IM_COL32(140, 230, 150, 255), "done");
        if (now) {
            const ImVec2 ts = tsize("now");
            dl->AddRectFilled(ImVec2(b.x - ts.x - Z(14), a.y - ts.y - 2), ImVec2(b.x - 2, a.y - 1), IM_COL32(255, 220, 90, 255), 3);
            dtext(dl, ImVec2(b.x - ts.x - Z(8), a.y - ts.y - 1), IM_COL32(40, 30, 10, 255), "now");
        }
        // the goal (the step's text), the place
        float y = a.y + Z(26);
        if (id != Q.editing) {
            const std::string text = n.get("text").string(kind == "step" ? "(no text)" : "");
            for (const auto& line : wrap(text, Z(kNodeW - 16), 2)) {
                dtext(dl, ImVec2(a.x + Z(8), y), kind == "step" && n.get("text").string().empty() ? IM_COL32(150, 155, 165, 255) : IM_COL32(220, 224, 232, 255), line.c_str());
                y += line_h();
            }
        } else
            y += 2 * line_h();
        if (kind == "step") {
            const Value& place = n.get("place");
            char where[96];
            if (place.get("x").type == Value::Number) std::snprintf(where, sizeof where, "place %.0f, %.0f", num(place, "x"), num(place, "z"));
            else std::snprintf(where, sizeof where, "%s", n.get("talk").type == Value::String ? "place: the resident's" : "no place");
            dtext(dl, ImVec2(a.x + Z(8), b.y - Z(40)), IM_COL32(150, 158, 172, 255), where);
        }
        // badges: conditions and rewards
        float bx = a.x + Z(8);
        for (const auto& bd : n.get("badges").array) {
            const std::string k = bd.get("kind").string();
            const ImVec2 c(bx + Z(8), b.y - Z(12));
            dl->AddCircleFilled(c, Z(8), badge_colour(k));
            const ImVec2 ts = tsize(badge_letter(k));
            dtext(dl, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), IM_COL32(20, 22, 28, 255), badge_letter(k));
            if (hovered && (mouse.x - c.x) * (mouse.x - c.x) + (mouse.y - c.y) * (mouse.y - c.y) < Z(9) * Z(9)) ImGui::SetTooltip("%s", bd.get("text").string().c_str());
            bx += Z(20);
        }
        // problems: a red mark, the problems as its tooltip
        if (!n.get("problems").array.empty()) {
            const ImVec2 c(b.x - 2, a.y + 2);
            dl->AddCircleFilled(c, std::max(6.0f, Z(9)), IM_COL32(215, 70, 60, 255));
            const ImVec2 ts = tsize("!");
            dtext(dl, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), IM_COL32(255, 255, 255, 255), "!");
            if (hovered && (mouse.x - c.x) * (mouse.x - c.x) + (mouse.y - c.y) * (mouse.y - c.y) < 140) {
                std::string tip;
                for (const auto& p : n.get("problems").array) tip += (tip.empty() ? "" : "\n") + p.string();
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        // pins
        if (kind != "start") dl->AddCircleFilled(in_pin(n), Z(kPin - 2), IM_COL32(170, 178, 192, 255));
        if (kind != "end") {
            const bool hot = over_out == id || Q.connecting == id;
            dl->AddCircleFilled(out_pin(n), Z(hot ? kPin + 1 : kPin), hot ? IM_COL32(255, 210, 90, 255) : IM_COL32(200, 206, 218, 255));
            dl->AddCircle(out_pin(n), Z(hot ? kPin + 1 : kPin), IM_COL32(30, 34, 42, 255), 0, 1.5f);
        }
    }
    // a connection being dragged (or shown for a picture)
    std::string drag_from = Q.connecting, drag_to;
    const uint64_t now_frame = render::frame_count();
    if (!Q.test_connect.empty() && drag_from.empty() && now_frame >= Q.test_from && now_frame <= Q.test_to) {
        const size_t gt = Q.test_connect.find('>');
        drag_from = Q.test_connect.substr(0, gt);
        if (gt != std::string::npos) drag_to = Q.test_connect.substr(gt + 1);
    }
    if (const Value* f = drag_from.empty() ? nullptr : node_by(drag_from)) {
        const ImVec2 p0 = out_pin(*f);
        ImVec2 p3 = mouse;
        const Value* target = drag_to.empty() ? (over_node.empty() || over_node == drag_from ? nullptr : node_by(over_node)) : node_by(drag_to);
        if (target && target->get("kind").string() != "start") {
            p3 = in_pin(*target);
            ImVec2 a, b;
            rect_of(*target, a, b);
            dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), IM_COL32(255, 210, 90, 255), 8, 2.5f);
        }
        ImVec2 c1, c2;
        curve(p0, p3, c1, c2);
        dl->AddBezierCubic(p0, c1, c2, p3, IM_COL32(255, 210, 90, 255), 3, 0);
        dl->AddCircleFilled(p3, 5, IM_COL32(255, 210, 90, 255));
        const char* hint = "drop on a step: choose the actor that moves the quest on";
        dtext(dl, ImVec2(p3.x + 10, p3.y + 8), IM_COL32(255, 230, 160, 255), hint);
    }
    if (nodes.empty()) dtext(dl, ImVec2(origin_screen.x + 16, origin_screen.y + 16), IM_COL32(170, 175, 185, 255), "No quest yet: New quest... adds one.");
    dl->PopClipRect();

    // ---- input
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!Q.editing.empty() && over_node != Q.editing) Q.editing.clear();
        if (!over_out.empty()) Q.connecting = over_out;
        else if (!over_node.empty() && over_node == Q.editing) {
        } else if (!over_node.empty()) {
            select_node(over_node);
            Q.dragging = over_node;
            Q.grab = ImVec2(at_canvas.x - Q.pos[over_node].x, at_canvas.y - Q.pos[over_node].y);
            Q.moved = false;
        } else if (over_edge >= 0) {
            Q.edge = over_edge;
            Q.node.clear();
        } else {
            Q.panning = true;
            Q.node.clear(), Q.edge = -1;
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && step_of(over_node) > 0) {
        Q.editing = over_node;
        Q.edit_focus = true;
        const Value* n = node_by(over_node);
        std::snprintf(Q.edit_buf, sizeof Q.edit_buf, "%s", n ? n->get("text").string().c_str() : "");
        Q.dragging.clear();
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!Q.dragging.empty() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
            Q.pos[Q.dragging] = ImVec2(std::round(at_canvas.x - Q.grab.x), std::round(at_canvas.y - Q.grab.y));
            Q.moved = true;
        }
        if (Q.panning && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) V.pan = ImVec2(V.pan.x + io.MouseDelta.x, V.pan.y + io.MouseDelta.y), V.moved = true;
    }
    if (hovered && io.MouseWheel != 0) {
        const float z = std::clamp(V.zoom * (1 + 0.1f * io.MouseWheel), 0.4f, 1.6f);
        V.pan = ImVec2(mouse.x - origin_screen.x - at_canvas.x * z, mouse.y - origin_screen.y - at_canvas.y * z);
        V.zoom = z;
        V.moved = true;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) V.pan = ImVec2(V.pan.x + io.MouseDelta.x, V.pan.y + io.MouseDelta.y);
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!Q.dragging.empty() && Q.moved) {  // one command, one undo step
            Value p = params_quest();
            p["positions"][Q.dragging]["x"] = double(Q.pos[Q.dragging].x);
            p["positions"][Q.dragging]["y"] = double(Q.pos[Q.dragging].y);
            command("set_quest_layout", p);
            // keep the hand's place until the layout comes back
            Q.graph["layout"][Q.dragging]["x"] = double(Q.pos[Q.dragging].x);
            Q.graph["layout"][Q.dragging]["y"] = double(Q.pos[Q.dragging].y);
        }
        if (!Q.connecting.empty()) {
            if (!over_node.empty() && over_node != Q.connecting && over_node != "start") {
                Q.connect_from = Q.connecting;
                Q.connect_to = over_node;
                ImGui::OpenPopup("connect_via");
            }
            Q.connecting.clear();
        }
        Q.dragging.clear();
        Q.panning = false;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        Q.add_at = at_canvas;
        if (!over_node.empty()) {
            select_node(over_node);
            ImGui::OpenPopup("node_menu");
        } else if (over_edge >= 0) {
            Q.edge = over_edge, Q.node.clear();
            ImGui::OpenPopup("edge_menu");
        } else
            ImGui::OpenPopup("canvas_menu");
    }
    // the delete key: the selected transition, or the selected step (asks first)
    if (ImGui::IsWindowFocused() && !io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        if (Q.edge >= 0 && edges[size_t(Q.edge)].get("editable").boolean) {
            const auto& e = edges[size_t(Q.edge)];
            Value p = params_quest();
            p["from"] = step_of(e.get("from").string());
            p["to"] = step_of(e.get("to").string());
            p["via"] = e.get("via").get("id");
            command("disconnect_quest_steps", p);
            Q.edge = -1;
        } else if (step_of(Q.node) > 0)
            ImGui::OpenPopup("remove_step");
    }
    // the node's text, edited in place
    if (!Q.editing.empty()) {
        if (const Value* n = node_by(Q.editing)) {
            ImVec2 a, b;
            rect_of(*n, a, b);
            ImGui::SetCursorScreenPos(ImVec2(a.x + Z(6), a.y + Z(25)));
            ImGui::SetNextItemWidth(Z(kNodeW - 12));
            if (Q.edit_focus) ImGui::SetKeyboardFocusHere(), Q.edit_focus = false;
            if (text_entry::field("##node_text", Q.edit_buf, sizeof Q.edit_buf, ImGuiInputTextFlags_EnterReturnsTrue)) {
                edit_step(step_of(Q.editing), "text", Value(std::string(Q.edit_buf)));
                Q.editing.clear();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                Q.editing.clear();
        } else
            Q.editing.clear();
    }
    // menus
    if (ImGui::BeginPopup("canvas_menu")) {
        if (ImGui::MenuItem("Add step here")) {
            const ImVec2 at = Q.add_at;
            command("add_quest_step", params_quest(), [at](const Value& r) {
                if (!r.get("ok").boolean) return;
                const std::string node = "step:" + std::to_string(int(r.get("result").get("step").number));
                Value p = params_quest();
                p["positions"][node]["x"] = std::round(at.x - kNodeW / 2);
                p["positions"][node]["y"] = std::round(at.y - 20);
                command("set_quest_layout", p);
                Q.select_after = node;
            });
        }
        if (ImGui::MenuItem("Arrange all")) {
            Value p = params_quest();
            for (const auto& [id, at] : default_layout(Q.graph)) {
                p["positions"][id]["x"] = double(at.x);
                p["positions"][id]["y"] = double(at.y);
            }
            command("set_quest_layout", p);
            V.fit = true;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("edge_menu")) {
        if (Q.edge >= 0) {
            const auto& e = edges[size_t(Q.edge)];
            ImGui::TextDisabled("%s", e.get("label").string().c_str());
            if (ImGui::MenuItem("Remove this transition", nullptr, false, e.get("editable").boolean)) {
                Value p = params_quest();
                p["from"] = step_of(e.get("from").string());
                p["to"] = step_of(e.get("to").string());
                p["via"] = e.get("via").get("id");
                command("disconnect_quest_steps", p);
                Q.edge = -1;
            }
            if (e.get("via").get("kind").string() == "actor" && ImGui::MenuItem("Select the actor")) {
                selected() = e.get("via").get("id").string();
                Value p;
                p["id"] = selected();
                command("focus", p);
                editor_shell::show_inspector();
            }
        }
        ImGui::EndPopup();
    }
    node_menu(m);
    connect_popup(m);
    ImGui::EndChild();
}

void start_at(int n) {
    Value p = params_quest();
    p["step"] = n;
    command("start_at_step", p);
}

void node_menu(const Value&) {
    if (ImGui::BeginPopup("node_menu")) {
        const int n = step_of(Q.node);
        if (n > 0) {
            if (ImGui::MenuItem("Start at this step")) start_at(n);
            ImGui::Separator();
            if (ImGui::MenuItem("Edit text")) {
                Q.editing = Q.node;
                Q.edit_focus = true;
                for (const auto& nd : Q.graph.get("nodes").array)
                    if (nd.get("id").string() == Q.node) std::snprintf(Q.edit_buf, sizeof Q.edit_buf, "%s", nd.get("text").string().c_str());
            }
            if (ImGui::MenuItem("Duplicate")) {
                Value p = params_quest();
                p["step"] = n;
                command("duplicate_quest_step", p, [](const Value& r) {
                    if (r.get("ok").boolean) Q.select_after = "step:" + std::to_string(int(r.get("result").get("step").number));
                });
            }
            if (ImGui::MenuItem("Add a step after it")) {
                Value p = params_quest();
                p["after"] = n;
                command("add_quest_step", p, [](const Value& r) {
                    if (r.get("ok").boolean) Q.select_after = "step:" + std::to_string(int(r.get("result").get("step").number));
                });
            }
            if (ImGui::MenuItem("Copy (Ctrl+C)")) {
                Value p = params_quest();
                p["steps"].type = Value::Array;
                p["steps"].array.push_back(Value(n));
                command("copy", p);
            }
            if (ImGui::MenuItem("Remove...")) Q.ask_remove = true;
        } else
            ImGui::TextDisabled(Q.node == "start" ? "Start: the quest's letter or what starts it" : "End: a step's resident talk finishes the quest");
        ImGui::EndPopup();
    }
    if (Q.ask_remove) {
        Q.ask_remove = false;
        ImGui::OpenPopup("remove_step");
    }
    if (ImGui::BeginPopupModal("remove_step", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove step %d of %s?\nThe actor conditions and actions that name it go too; Undo brings them back.", step_of(Q.node), Q.quest.c_str());
        if (ImGui::Button("Remove")) {
            Value p = params_quest();
            p["step"] = step_of(Q.node);
            p["confirm"] = true;
            command("remove_quest_step", p);
            Q.node.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Dropped on a step: which actor moves the quest on (talking to it, reading or opening it).
void connect_popup(const Value& m) {
    Q.connect_open = ImGui::BeginPopup("connect_via");
    if (!Q.connect_open) return void(Q.connect_cancel = false);
    if (Q.connect_cancel) {  // Esc (editor_shell): the connection is dropped
        Q.connect_cancel = Q.connect_open = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const int from = step_of(Q.connect_from), to = step_of(Q.connect_to);
    if (Q.connect_to == "end") {
        ImGui::TextColored(kHead, "Step %d -> End", from);
        muted("A quest ends with a step's resident talk: the resident speaks the quest's lines, gives the reward and the "
              "quest is done. Set it in the step's Talk (Inspector).");
        if (from > 0 && ImGui::Button("Open the step")) select_node(Q.connect_from), ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextColored(kHead, "%s -> step %d: what moves the quest on?", from ? ("Step " + std::to_string(from)).c_str() : "Start (any time)", to);
    if (from && to <= from) {
        ImGui::TextColored(kBad, "A quest only goes forward: step %d is not after step %d.", to, from);
        ImGui::EndPopup();
        return;
    }
    muted("Talking to the actor, reading or opening it sets the step. It is then there only while the quest is at the first step.");
    int shown = 0;
    for (int group = 0; group < 3; group++) {
        for (const auto& a : m.get("actors").array) {
            const Value& when = a.get("when");
            const bool here = when.get("quest").string() == Q.quest && int(num(when, "step")) == from && from;
            const bool free = when.get("quest").type == Value::Null;
            const int g = here ? 0 : free ? 1 : 2;
            if (g != group) continue;
            const std::string actor = a.get("actor").string();
            if (actor == "pot" || actor == "barrel" || actor == "crate" || actor == "door" || a.get("combat").type != Value::Null) continue;  // no talk
            const std::string id = a.get("id").string();
            const std::string label = id + (group == 0 ? "  (there at this step)" : group == 1 ? "  (there any time)" : "  (there at another step)");
            if (ImGui::Selectable(label.c_str(), false, group == 2 ? ImGuiSelectableFlags_Disabled : 0)) {
                Value p = params_quest();
                p["from"] = from;
                p["to"] = to;
                p["via"] = id;
                command("connect_quest_steps", p);
            }
            shown++;
        }
    }
    if (!shown) muted("The mod has no actor to talk to yet: place a sign, a resident or a model with Add.");
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- the inspector
void transition_inspector(const Value& m, const Value& e) {
    ImGui::TextColored(kHead, "Transition");
    ImGui::Text("%s  ->  %s", e.get("from").string().c_str(), e.get("to").string().c_str());
    ImGui::TextWrapped("%s", e.get("label").string().c_str());
    const std::string via_kind = e.get("via").get("kind").string(), via = e.get("via").get("id").string();
    if (!e.get("conditions").array.empty()) {
        ImGui::SeparatorText("Conditions");
        for (const auto& c : e.get("conditions").array) ImGui::BulletText("%s", c.get("text").string().c_str());
    }
    if (!e.get("rewards").array.empty()) {
        ImGui::SeparatorText("Hands out");
        for (const auto& c : e.get("rewards").array) ImGui::BulletText("%s", c.get("text").string().c_str());
    }
    if (via_kind != "actor") {
        muted(via_kind == "talk" ? "A step's resident talk: change it in the step (Talk)." : via_kind == "letter" ? "The quest's letter: change it on Start."
              : via_kind == "item" ? "The item's use: change it in the mod's items." : "A scene's end actions: change the scene.");
        return;
    }
    const Value* a = by_id(m.get("actors"), via);
    if (!a) return;
    ImGui::SeparatorText("Edit");
    const Value& when = a->get("when");
    auto connect = [&](const char* key, const std::string& value) {
        Value p = params_quest();
        p["from"] = step_of(e.get("from").string());
        p["to"] = step_of(e.get("to").string());
        p["via"] = via;
        p[key] = value;
        command("connect_quest_steps", p);
    };
    // the item Link must carry
    const std::string item = when.get("item").string();
    ImGui::TextUnformatted("Link carries");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##edge_item", item.empty() ? "(nothing needed)" : item.c_str())) {
        if (ImGui::Selectable("(nothing needed)", item.empty())) connect("item", "-");
        for (const auto& it : m.get("items").array) {
            const std::string id = "item:" + it.get("id").string();
            if (ImGui::Selectable(id.c_str(), id == item)) connect("item", id);
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Time of day");
    const std::string time = when.get("time").string();
    const char* const times[] = {"any", "day", "night"};
    int t = time == "day" ? 1 : time == "night" ? 2 : 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##edge_time", &t, times, 3)) connect("time", t == 0 ? "-" : times[t]);
    if (ImGui::Button("Actor's dialogue and actions")) {
        selected() = via;
        editor_shell::show_inspector();
        Value p;
        p["id"] = via;
        command("focus", p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        Value p = params_quest();
        p["from"] = step_of(e.get("from").string());
        p["to"] = step_of(e.get("to").string());
        p["via"] = via;
        command("disconnect_quest_steps", p);
        Q.edge = -1;
    }
}

void step_inspector(const Value& m, const Value& q, int n) {
    const Value* step = nullptr;
    for (const auto& s : q.get("steps").array)
        if (int(num(s, "n")) == n) step = &s;
    if (!step) return;
    ImGui::TextColored(kHead, "Step %d", n);
    ImGui::SameLine();
    if (ImGui::SmallButton("Start at this step")) start_at(n);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Test mode: sets this mod's quest step and what the earlier steps hand out, warps there and plays");
    std::string t;
    ImGui::TextUnformatted("Goal (what to do next)");
    if (edit_line("##goal", step->get("text").string(), t, 120)) edit_step(n, "text", Value(t));
    ImGui::TextUnformatted("Where (the place's name)");
    if (edit_line("##where", step->get("where").string(), t, 60)) edit_step(n, "where", t.empty() ? Value() : Value(t));
    // the place on the sea chart
    const Value& place = step->get("place");
    if (place.get("x").type == Value::Number) ImGui::Text("Place on the chart: %.0f, %.0f", num(place, "x"), num(place, "z"));
    else muted(step->get("npc").type == Value::Object ? "Place: where the resident stands." : "No place on the sea chart.");
    auto set_place = [&](const char* from, const std::string& location = "") {
        Value p = params_quest();
        p["step"] = n;
        p["from"] = from;
        if (!location.empty()) p["location"] = location;
        command("set_step_place", p);
    };
    if (ImGui::SmallButton("Link's place")) set_place("link");
    ImGui::SameLine();
    if (ImGui::SmallButton("Camera's place")) set_place("camera");
    ImGui::SameLine();
    if (ImGui::SmallButton("From the warp list...")) ImGui::OpenPopup("place_list");
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) set_place("none");
    if (ImGui::BeginPopup("place_list")) {
        for (const auto& l : snapshot().locations.array) {
            if (l.get("stage").string() != "sea") continue;
            if (ImGui::Selectable((l.get("label").string() + "##" + l.get("id").string()).c_str())) set_place("location", l.get("id").string());
        }
        ImGui::EndPopup();
    }
    // conditions of the step itself
    ImGui::SeparatorText("Conditions");
    const Value& av = step->get("available");
    if (av.get("event").type != Value::Null || av.get("not_event").type != Value::Null)
        ImGui::BulletText("needs game progress (read only): %s", av.get("text").string().c_str());
    const std::string time = av.get("time").string("any");
    const char* const times[] = {"any", "day", "night"};
    int ti = time == "day" ? 1 : time == "night" ? 2 : 0;
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("time of day##step_time", &ti, times, 3)) {
        Value a = av.type == Value::Object ? av : Value();
        if (ti == 0) a.object.erase("time");
        else a["time"] = times[ti];
        if (a.get("text").type != Value::String) a["text"] = ti == 1 ? "Come back by day." : "Come back at night.";
        edit_step(n, "available", a.object.size() <= 1 && a.get("time").type == Value::Null && a.get("event").type == Value::Null ? Value() : a);
    }
    // incoming and outgoing transitions
    ImGui::SeparatorText("Transitions");
    const auto& edges = Q.graph.get("edges").array;
    const std::string id = "step:" + std::to_string(n);
    for (size_t i = 0; i < edges.size(); i++) {
        const auto& e = edges[i];
        const bool in = e.get("to").string() == id, out = e.get("from").string() == id;
        if (!in && !out) continue;
        const std::string label = std::string(in ? "from " + e.get("from").string() : "to " + e.get("to").string()) + ": " + e.get("label").string();
        if (ImGui::Selectable((label + "##tr" + std::to_string(i)).c_str())) Q.edge = int(i), Q.node.clear();
    }
    muted("Drag from a step's right-hand dot to another step to add one.");
    // the resident talk that finishes the quest
    ImGui::SeparatorText("Talk (finishes the quest)");
    const Value& npc = step->get("npc");
    if (npc.type == Value::Object) {
        ImGui::Text("%s says:", npc.get("who").string().c_str());
        for (const auto& l : npc.get("lines").array) ImGui::BulletText("%s", l.string().c_str());
        if (npc.get("then").get("give").type == Value::String) ImGui::Text("gives %s", npc.get("then").get("give").text.c_str());
        if (ImGui::SmallButton("Remove the talk")) edit_step(n, "npc", Value());
    } else {
        static char who[48] = "windfall-um1", line[120] = {};
        ImGui::SetNextItemWidth(140);
        text_entry::field("resident##npc_who", who, sizeof who);
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field("##npc_line", line, sizeof line, 0, "what the resident says");
        if (ImGui::SmallButton("Add the talk") && who[0] && line[0]) {
            Value v;
            v["who"] = std::string(who);
            v["lines"].type = Value::Array;
            v["lines"].array.push_back(Value(std::string(line)));
            edit_step(n, "npc", v);
        }
    }
    ImGui::SeparatorText("Step");
    if (ImGui::SmallButton("Duplicate")) {
        Value p = params_quest();
        p["step"] = n;
        command("duplicate_quest_step", p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Earlier")) {
        Value p = params_quest();
        p["step"] = n;
        int index = 0, i = 0;
        for (const auto& s : q.get("steps").array) {
            ++i;
            if (int(num(s, "n")) == n) index = i;
        }
        p["to"] = std::max(1, index - 1);
        command("move_quest_step", p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Later")) {
        Value p = params_quest();
        p["step"] = n;
        int index = 0, i = 0;
        for (const auto& s : q.get("steps").array) {
            ++i;
            if (int(num(s, "n")) == n) index = i;
        }
        p["to"] = std::min(int(q.get("steps").array.size()), index + 1);
        command("move_quest_step", p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove...")) ImGui::OpenPopup("remove_step_i");
    if (ImGui::BeginPopupModal("remove_step_i", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove step %d? Undo brings it back.", n);
        if (ImGui::Button("Remove")) {
            Value p = params_quest();
            p["step"] = n;
            p["confirm"] = true;
            command("remove_quest_step", p);
            Q.node.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    (void)m;
}

void quest_inspector(const Value& q) {
    ImGui::TextColored(kHead, "Quest %s", Q.quest.c_str());
    auto set = [&](const char* property, const Value& v) {
        Value p = params_quest();
        p["property"] = property;
        p["value"] = v;
        command("set_quest", p);
    };
    std::string t;
    ImGui::TextUnformatted("Title");
    if (edit_line("##title", q.get("title").string(), t, 60) && !t.empty()) set("title", Value(t));
    ImGui::TextUnformatted("Before step 1 the notebook says");
    if (edit_line("##start_text", q.get("start").get("text").string(), t, 120)) {
        Value s;
        if (!t.empty()) s["text"] = t;
        set("start", t.empty() ? Value() : s);
    }
    ImGui::TextUnformatted("Reward (text)");
    if (edit_line("##reward_text", q.get("reward").get("text").string(), t, 60)) {
        Value r = q.get("reward").type == Value::Object ? q.get("reward") : Value();
        if (t.empty()) r.object.erase("text");
        else r["text"] = t;
        set("reward", r.object.empty() ? Value() : r);
    }
    const char* const journals[] = {"on_enable", "on_trigger", "hidden"};
    const std::string j = q.get("journal").string("on_enable");
    int ji = j == "on_trigger" ? 1 : j == "hidden" ? 2 : 0;
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("in the notebook##journal", &ji, journals, 3)) set("journal", ji == 0 ? Value() : Value(journals[ji]));
    if (q.get("letter").type == Value::Object)
        ImGui::TextWrapped("Starts with the letter \"%s\" from %s.", q.get("letter").get("id").string().c_str(), q.get("letter").get("sender").string().c_str());
    else
        muted("No letter: an actor (or an item) that sets a step starts the quest. Drag from Start to a step.");
    if (ImGui::SmallButton("Remove quest...")) ImGui::OpenPopup("remove_quest");
    if (ImGui::BeginPopupModal("remove_quest", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove the quest %s and every actor condition and action that names it?\nUndo brings it back.", Q.quest.c_str());
        if (ImGui::Button("Remove")) {
            Value p = params_quest();
            p["confirm"] = true;
            command("remove_quest", p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// the compact list: the steps in order, moved with the arrows
void step_list(const Value& q) {
    const auto& steps = q.get("steps").array;
    if (ImGui::BeginTable("steps", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 28);
        ImGui::TableSetupColumn("goal");
        ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 52);
        const int saved = int(num(Q.graph, "saved_step", -1));
        for (size_t i = 0; i < steps.size(); i++) {
            const int n = int(num(steps[i], "n"));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(n);
            if (n == saved) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(110, 90, 20, 160));
            if (ImGui::Selectable(std::to_string(n).c_str(), Q.node == "step:" + std::to_string(n), ImGuiSelectableFlags_SpanAllColumns))
                select_node("step:" + std::to_string(n));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(steps[i].get("text").string().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(steps[i].get("where").string().c_str());
            ImGui::TableNextColumn();
            auto move = [&](int to) {
                Value p = params_quest();
                p["step"] = n;
                p["to"] = to;
                command("move_quest_step", p);
            };
            if (i > 0 && ImGui::SmallButton("^")) move(int(i));
            if (i + 1 < steps.size()) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton("v")) move(int(i) + 2);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
}  // namespace

// ---------------------------------------------------------------- public
std::vector<std::string>& multi_selection() { return Q.multi; }

std::string quest_status() {
    refresh_state();
    return Q.state.get("current").get("text").string();
}

void show_quest(const std::string& quest, const std::string& field) {
    Q.quest = quest;
    Q.graph_at = -100;
    select_node(field.rfind("step:", 0) == 0 ? field : std::string("start"));
    editor_shell::show_quests();
}

void quest_editor(const Value& m) {
    static bool test_aids = [] {
        if (const char* e = std::getenv("WWHD_TEST_EDITOR_QUEST")) {  // quest[:node] (pictures)
            const std::string v = e;
            const size_t colon = v.find(':');
            Q.want_quest = v.substr(0, colon);
            if (colon != std::string::npos) Q.want_node = v.substr(colon + 1);
        }
        if (const char* e = std::getenv("WWHD_TEST_EDITOR_CONNECT")) {
            std::string v = e;
            if (const size_t at = v.find('@'); at != std::string::npos) {
                Q.test_from = std::strtoull(v.c_str() + at + 1, nullptr, 10);
                if (const size_t dash = v.find('-', at); dash != std::string::npos) Q.test_to = std::strtoull(v.c_str() + dash + 1, nullptr, 10);
                v.resize(at);
            }
            Q.test_connect = v;
        }
        return true;
    }();
    (void)test_aids;
    refresh_quest(m);
    // the bar: the quest, new quest, add step, graph or list, the game's step
    ImGui::SetNextItemWidth(220);
    const Value* q = by_id(m.get("quests"), Q.quest);
    if (ImGui::BeginCombo("##quest", q ? (q->get("title").string() + "  (" + Q.quest + ")").c_str() : "(no quest)")) {
        for (const auto& qq : m.get("quests").array)
            if (ImGui::Selectable((qq.get("title").string() + "  (" + qq.get("id").string() + ")").c_str(), qq.get("id").string() == Q.quest)) {
                Q.quest = qq.get("id").string();
                Q.node.clear(), Q.edge = -1, Q.graph_at = -100;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New quest...")) ImGui::OpenPopup("new_quest");
    if (ImGui::BeginPopup("new_quest")) {
        ImGui::SetNextItemWidth(260);
        text_entry::field("##new_title", Q.new_title, sizeof Q.new_title, 0, "the quest's title");
        if (ImGui::Button("Add") && Q.new_title[0]) {
            Value p;
            p["title"] = std::string(Q.new_title);
            command("add_quest", p, [](const Value& r) {
                if (r.get("ok").boolean) Q.quest = r.get("result").get("quest").string(), Q.graph_at = -100, Q.select_after = "step:1";
            });
            Q.new_title[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!q) {
        muted("This mod has no quest yet. New quest... adds one with its first step.");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add step")) command("add_quest_step", params_quest(), [](const Value& r) {
            if (r.get("ok").boolean) Q.select_after = "step:" + std::to_string(int(r.get("result").get("step").number));
        });
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    int view = Q.list_view ? 1 : 0;
    const char* const views[] = {"Graph", "List"};
    if (ImGui::Combo("##view", &view, views, 2)) Q.list_view = view == 1;
    ImGui::SameLine();
    if (ImGui::Button("Fit")) Q.views[0].fit = true;
    ImGui::SameLine();
    ImGui::Checkbox("Show while playing", &Q.while_playing);
    if (Q.graph.get("saved_step").type == Value::Number) {
        ImGui::SameLine();
        for (const auto& qs : Q.state.get("quests").array)
            if (qs.get("quest").string() == Q.quest) ImGui::TextColored(kHead, "  in the game: %s", qs.get("text").string().c_str());
    }
    // Ctrl+C / Ctrl+V here: the selected step, pasted as the quest's new last step
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && (io.KeyCtrl || io.KeySuper)) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && step_of(Q.node) > 0) {
            Value p = params_quest();
            p["steps"].type = Value::Array;
            p["steps"].array.push_back(Value(step_of(Q.node)));
            command("copy", p);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            command("paste", params_quest(), [](const Value& r) {
                if (r.get("ok").boolean && !r.get("result").get("ids").array.empty())
                    Q.select_after = "step:" + std::to_string(int(r.get("result").get("ids").array.back().number));
            });
    }
    // the graph (or the list) and the inspector beside it
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float inspector_w = std::min(320.0f, avail.x * 0.38f);
    if (Q.list_view) {
        ImGui::BeginChild("quest_list", ImVec2(avail.x - inspector_w - 8, avail.y), ImGuiChildFlags_Borders);
        step_list(*q);
        ImGui::EndChild();
    } else
        canvas(m, ImVec2(avail.x - inspector_w - 8, avail.y), false);
    ImGui::SameLine();
    ImGui::BeginChild("quest_inspector", ImVec2(0, avail.y), ImGuiChildFlags_Borders);
    if (Q.edge >= 0 && Q.edge < int(Q.graph.get("edges").array.size())) transition_inspector(m, Q.graph.get("edges").array[size_t(Q.edge)]);
    else if (step_of(Q.node) > 0) step_inspector(m, *q, step_of(Q.node));
    else quest_inspector(*q);
    // the node's problems, below
    for (const auto& n : Q.graph.get("nodes").array)
        if (n.get("id").string() == (Q.node.empty() ? "start" : Q.node) && !n.get("problems").array.empty()) {
            ImGui::SeparatorText("Problems");
            for (const auto& p : n.get("problems").array) {
                ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
                ImGui::TextWrapped("%s", p.string().c_str());
                ImGui::PopStyleColor();
            }
        }
    ImGui::EndChild();
}

bool quest_graph_while_playing() { return Q.while_playing; }
bool quest_connecting() { return !Q.connecting.empty() || Q.connect_open; }
void cancel_quest_connect() {
    Q.connecting.clear();
    if (Q.connect_open) Q.connect_cancel = true;
}

// While playing: the quest graph small, read only, the game's step highlighted.
void quest_live(const Value& m) {
    refresh_quest(m);
    if (Q.quest.empty()) return;
    canvas(m, ImGui::GetContentRegionAvail(), true);
}

void flags_area() {
    const int frame = ImGui::GetFrameCount();
    if (frame - Q.flags_at >= 15) {
        const Value r = query("list_flags", Value());
        if (r.get("ok").boolean) Q.flags = r.get("result"), Q.flags_at = frame;
    }
    const bool live = Q.flags.get("live").boolean, test = Q.flags.get("test_mode").boolean;
    if (!live) muted("The values show while a story mod's progress is loaded (a save file in the world).");
    else if (!test) muted("Turn test mode on to change them. Only this mod's own flags: never the game's.");
    if (Q.flags.get("flags").array.empty()) {
        muted("This mod has no flags yet (signs, chests, switches and flag actions make them).");
        return;
    }
    if (ImGui::BeginTable("flags", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("flag");
        ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("used by");
        ImGui::TableHeadersRow();
        for (const auto& f : Q.flags.get("flags").array) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string id = f.get("id").string();
            ImGui::PushID(id.c_str());
            bool on = f.get("value").boolean;
            ImGui::BeginDisabled(!live || !test || !f.get("settable").boolean);
            if (ImGui::Checkbox("##on", &on)) {
                Value p;
                p["id"] = id;
                p["value"] = on;
                command("set_flag", p);
                Q.flags_at = -100;
            }
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (live) ImGui::TextColored(on ? kGood : kMuted, "%s", id.c_str());
            else ImGui::TextUnformatted(id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kMuted, "%s", f.get("kind").string().c_str());
            ImGui::TableNextColumn();
            std::string users;
            for (const auto& u : f.get("used_by").array) users += (users.empty() ? "" : ", ") + u.string();
            ImGui::TextUnformatted(users.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void search_area() {
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##search", Q.search, sizeof Q.search, 0, "Search actors, quests, scenes, letters, charts, items, flags");
    if (Q.hits_for != Q.search || ImGui::GetFrameCount() % 30 == 0) {
        Value p;
        p["text"] = std::string(Q.search);
        const Value r = query("search", p);
        if (r.get("ok").boolean) Q.hits = r.get("result"), Q.hits_for = Q.search;
    }
    if (!Q.search[0]) return;
    if (Q.hits.array.empty()) muted("Nothing found.");
    for (size_t i = 0; i < Q.hits.array.size(); i++) {
        const auto& h = Q.hits.array[i];
        const std::string kind = h.get("kind").string(), id = h.get("id").string();
        std::string label = kind + "  " + id;
        if (kind == "quest_step") label = "step " + std::to_string(int(num(h, "step"))) + " of " + id;
        label += "   " + h.get("text").string();
        if (ImGui::Selectable((label + "##hit" + std::to_string(i)).c_str())) {
            if (kind == "actor") {
                selected() = id;
                Q.multi = {id};
                Value p;
                p["id"] = id;
                command("focus", p);
                editor_shell::show_inspector();
            } else if (kind == "quest" || kind == "quest_step")
                show_quest(id, kind == "quest_step" ? "step:" + std::to_string(int(num(h, "step"))) : std::string());
            else if (kind == "scene")
                show_scene(id, int(num(h, "step")));
            else if (kind == "letter" || kind == "chart" || kind == "item")
                show_story(kind, id);
            else if (kind == "flag" && !h.get("used_by").array.empty()) {
                selected() = h.get("used_by").array[0].string();
                editor_shell::show_inspector();
            }
        }
    }
}

}  // namespace editor_ui
